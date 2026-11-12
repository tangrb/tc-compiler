# TC 原生编译器设计说明书

> **文档定位**：本文是 TC 原生编译器（TC → 汇编 → 机器码）的**目标架构设计**，以
> [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md) 为**唯一设计输入**，不锚定任何
> 既有实现的设计或代码。**本文描述待实现的目标（待落地），不描述当前实现现状。**
> 语言合法性、语义、诊断与平台契约以 0.0.46 标准为准；本文规定实现架构与契约。

---

## 目录

- [0. 依据、目标与规范注记](#0-依据目标与规范注记)
- [1. 总体架构](#1-总体架构)
- [2. 类型系统表示](#2-类型系统表示)
- [3. 扁平 TAC IR](#3-扁平-tac-ir)
- [4. 内存与区模型](#4-内存与区模型)
- [5. 前端](#5-前端)
- [6. 诊断子系统](#6-诊断子系统)
- [7. 语义运行时库](#7-语义运行时库)
- [8. 平台档案](#8-平台档案)
- [9. VM 解释器](#9-vm-解释器)
- [10. 后端（AArch64 先行）](#10-后端aarch64-先行)
- [11. 工具链与构建](#11-工具链与构建)
- [12. 验证策略](#12-验证策略)
- [13. 分期路线](#13-分期路线)
- [14. 模块结构](#14-模块结构)
- [15. 已定设计决策](#15-已定设计决策)
- [16. 风险与取舍](#16-风险与取舍)

---

## 0. 依据、目标与规范注记

### 0.1 设计依据

唯一输入为 0.0.46 标准。下文主要章节标注所依据的标准章节。既有实现不作为设计输入；正确性参考由新建 VM 与标准精确规则提供（见本文 §9、本文 §12）。

### 0.2 目标

| 项 | 决定 |
| ---- | ---- |
| 编译路径 | **TC → ASM → 机器码**；不转译 C99；不依赖宿主 C 编译器 |
| 应用目标 | **裸机 + OS 双目标** |
| 字长 | **32/64 双档案**；`usize`/`isize`/指针/`fnptr` 宽度为平台字长 W |
| 首个验证平台 | **AArch64（64 位先行）**；32 位后端后续接入 |
| 工具链 | 复用平台汇编器/链接器（clang 集成汇编 + lld），不自写编码器/链接器 |
| IR | 单级扁平 TAC IR，目标无关 |
| 运行时 | freestanding-first，平台档案参数化 |

### 0.3 规范注记

标准 §1.7 步骤 13 的代码生成输出为「生成 VM 指令流或 AOT 目标汇编/机器码」，与本设计 **TC → ASM → 机器码** 路径一致（AOT 不再转译 C99、不依赖宿主 C 编译器）。§1.9「VM 与 AOT 共享同一套前端静态语义；代码生成可以不同，但不得改变本文规定的静态结果与可观察行为」是 VM 与原生后端共享前端、以差分对拍验证一致性的规范依据。

**引用约定**：本文中「标准 §N」指 0.0.46 标准；「本文 §N」指本文自身章节；未标注的 §N 默认指标准。

---

## 1. 总体架构

```
TC 源 ─► 前端(LEX/SYN/IMPORT/SEM/CT) ─► 扁平 TAC IR（目标无关）
                                              │
                    ┌─────────────────────────┼─────────────────────────┐
                    ▼                          ▼                         ▼
              VM 解释器(参考)           目标后端(AArch64 先行)      优化契约（0.0.46 零 pass）
                    │                          │
                    │                    .s ─► clang 汇编 ─► .o
                    │                          │
                    │              freestanding 运行时.a ─► lld ─► ELF/flat
                    ▼                          ▼
                 参考输出 ◄──── 差分对拍 ────► qemu/硬件执行
```

**§1.9 不变式**：VM 与 AOT 共享同一前端静态语义与同一份 IR；代码生成不同，但静态结果与可观察行为一致。这是差分参考的根基。

分层职责：

| 层 | 职责 | 目标相关性 |
| ---- | ---- | ---- |
| 前端 | LEX/SYN/IMPORT/SEM/CT，产出扁平 IR | 无关 |
| IR | 类型/指令/基本块/CFG/区地址 | 无关 |
| 运行时 | 语义核 + 平台接口（四缝） | 核无关；档案相关 |
| VM | 解释执行，作可执行参考 | 无关 |
| 后端 | 指令选择 + 调用约定 + 帧/区布局 + 汇编发射 | 相关 |
| 工具链驱动 | asm → obj → ELF/flat | 相关 |

---

## 2. 类型系统表示

依据：§3.1–§3.9。

```c
typedef enum {
    TC_TY_I8, TC_TY_I16, TC_TY_I32, TC_TY_I64,
    TC_TY_U8, TC_TY_U16, TC_TY_U32, TC_TY_U64,
    TC_TY_ISIZE, TC_TY_USIZE,          /* 字长类型，独立等价类 §3.9 */
    TC_TY_F32, TC_TY_F64, TC_TY_BOOL, TC_TY_VOID,
    TC_TY_PTR, TC_TY_FNPTR, TC_TY_STRUCT
} TcTypeTag;

typedef struct TcType {
    TcTypeTag tag;
    union {
        struct { const TcType *pointee; } ptr;                       /* §3.5 */
        struct { const TcType *ret; const TcType **params; size_t n; } fnptr; /* §3.6 */
        struct { uint32_t struct_id; } struct_;                      /* §3.7 名义等价 */
    };
} TcType;
```

规则：

- **intern + 单例**：标量走单例；复合类型 intern 到稳定节点，指针相等即等价（§3.9）。类型节点不可指向可 realloc 缓冲。
- **等价**：标量按关键字；`isize/usize` 独立类（不与固定宽度等价）；struct 名义；`ptr<T>` 按 T；`fnptr` 逐项（参数名无关）。无子类型/隐式加宽/窄化/符号转换。
- **值类型**：标量、`ptr<T>`、`fnptr<…>`、struct；`void` 仅返回类型，非值类型（§3.8）。核心无数组/字符串/联合/枚举/泛型/闭包/引用/GC。
- **布局**：`sizeof` 返回字节数；struct 字段紧排 + `@padding(N)` 插 0x00，无隐式对齐/尾填充（§3.7）；布局求和溢出 `usize` → `TC_CE_LAYOUT_OVERFLOW`。
- **bool 规范化**（§3.4）：任何读 0→false/非0→true；任何写存储写 0x00/0x01。VM 与后端共同遵守。
- **自引用**：struct 字段允许 `ptr<S>`/`ptr<ptr<S>>` 及 `fnptr` 签名中的 S；值字段直接/间接自引用 → `TC_CE_STRUCT_SELF_VALUE`（§3.7）。

### 2.1 类型表、结构体与布局

```c
typedef struct {
    const char *name;
    const TcType *type;
    int readonly;
    uint32_t padding;          /* @padding(N) 的 N；0 表示无 */
    uint64_t offset;           /* 当前 profile 下的字段偏移（本文 §2.1 计算） */
} TcField;

typedef struct {
    uint32_t id;
    uint32_t module;           /* 模块实例 id */
    const char *name;
    TcField *fields; size_t nfields;
    uint64_t size;             /* 当前 profile W 下计算出的 sizeof S */
} TcStructDef;
```

- **类型身份不随 W 改变**：`isize`/`usize` 仍是独立类型；32 位档案与 `int32`/`uint32` 同宽但不同型，64 位档案与 `int64`/`uint64` 同宽但不同型。`ptr<T>`/`fnptr` 宽度 = W。
- **不在 TcType 中缓存布局**：`sizeof`/字段 offset/`TcStructDef.size` 由 `TcPlatformProfile.addr_width` 计算；同一类型表可跨目标共享，布局表必须按目标实例化或按 `(target, struct)` 缓存。
- **结构体**：字段紧排、`@padding(N)` 插 0x00、无隐式对齐/尾填充；`sizeof` 求和在 W 位 `usize` 中溢出 → `TC_CE_LAYOUT_OVERFLOW`（标准 §3.7/§6.13/§13.3）。
- **fnptr**：签名参数名不参与等价；函数指针不携带 `@unsafe` 或参数前置条件（`@nonnull`/`@align`/`@range`）；直接调用前置条件由 SEM 校验并记录在 `TcFunc` 注解中，供优化器假设；运行期不检查（标准 §3.6/§8.5/§8.6）。

### 2.2 类型表生命周期

- **创建/填充**：Analyze 入口创建 `TcTypeTable`；Pass1 对声明类型做 intern；Pass2 对 `cast`/`trunc`/`bitcast` 的目标类型做 intern。
- **冻结**：Analyze 完成后 `TcTypeTable` 冻结，之后只读；`TcIR.types` 指向冻结表。
- **禁止改写**：VM/AOT 不得重新解析或改写类型表；不得向绑定/值另存裸标签再投影回类型；绑定/值/签名/对象持有稳定共享类型指针。
- **跨目标**：类型身份（`isize`/`usize`/`ptr`/`fnptr`/struct 名义）不随 W 改变；`sizeof`/布局/字段 offset 不在类型节点缓存，按 profile 单独计算（本文 §2.1）。
- **失败**：类型表构建失败即 SEM 失败，不得进入 CT/代码生成。


---

## 3. 扁平 TAC IR

依据：§6.1（统一指令形态）、§6.2（opcode 表）、§7（控制流）、§3.12（对象/指针）。

**单级扁平**：0.0.46 源已是「一条指令一个 op、操作数是值、不得嵌套 op/call」（§6.1），故 parser 直接产扁平 IR，无需 AST→TAC 降级。临时变量是源语言一等公民（§6.1 规则 6 要求先落临时）。

### 3.1 操作数、lvalue 与命名实参

```c
typedef enum { TC_OP_VREG, TC_OP_FIELD, TC_OP_CONST, TC_OP_LABEL,
               TC_OP_FUNC, TC_OP_STRUCT, TC_OP_DISCARD } TcOperandKind;

typedef struct {
    TcOperandKind kind;
    union {
        uint32_t vreg;                                   /* 帧/静态/程序变量或临时 */
        struct { uint32_t base; uint32_t *fields; size_t nfields; } field; /* x.f.g */
        struct { uint64_t bits; const TcType *type; } konst;
        uint32_t label;
        uint32_t func;
        uint32_t struct_id;
    };
} TcOperand;

typedef struct {
    const char *name;                  /* NULL = 位置实参/位置字段 */
    TcOperand value;
} TcArg;

typedef struct {
    const char *base;                  /* parse 阶段的源名 */
    const char **fields; size_t nfields;
} TcFieldRef;                          /* 仅 parse 临时；SEM 后消失 */

typedef struct {
    const char *file;
    uint32_t line, column;             /* 1 基；标准 §12.5.1 */
    uint32_t valid_prefix_bytes;       /* LEX：首个非法字节前的最长有效前缀 */
    uint32_t invalid_byte_offset;      /* LEX：0 基非法字节偏移；无错误 = UINT32_MAX */
} TcSpan;
```

`TC_OP_DISCARD` 对应 `_`（标准 §6.1 规则 7）：仅作 LHS，不作读取操作数，不参与 DI。

- **字段路径**：`TC_OP_FIELD{base vreg, fields…}` 统一表示 `x.f`/`x.f.g`/`Self.x.f`/`m.x.f`；它既可作为值（`copy T, s.f`、`br_if s.flag`、`call_indirect s.fp`）也可作为 lvalue（`s.f = …`）。`TcInstr.dst` 使用 VREG/FIELD/DISCARD；`addr T, s.f` 的字段目标放在 `TcArg.value`（`TC_OP_FIELD`），不另设 lvalue 字段。
- **parse/SEM 分工**：parser 只能持 `TcFieldRef{base 名, fields 名…}`；SEM 解析字段后替换为 `TC_OP_FIELD` 的 vreg + 字段索引路径。最终 `TcIR` 不保留字段名。
- **sizeof**：参数是类型，经 `TcInstr.type` 承载；不存在“sizeOf lvalue 目标”。
- **命名实参**：`call`/`make` 的 `name: value` 必须在 IR 中保留名字；parser 不能因为尚未收集签名/字段表而丢弃。`TcArg.name == NULL` 表示位置形式。
- **无源名**：最终 IR 操作数已解析为 vreg/field/const/label/func/struct；源名仅保留在 `TcSpan`/诊断元数据，不参与代码生成。

### 3.2 指令

```c
typedef enum {
    /* 标准 §6.2.1 算术 */ TC_ADD,TC_SUB,TC_MUL,TC_DIV,TC_MOD,TC_NEG,TC_ABS,TC_SHL,TC_SHR,
    /* §6.2.2 位/逻辑 */ TC_AND,TC_OR,TC_XOR,TC_NOT,
    /* §6.2.3 比较 */ TC_EQ,TC_NE,TC_LT,TC_LE,TC_GT,TC_GE,
    /* §6.2.4 转换 */ TC_CAST,TC_TRUNC,TC_BITCAST,
    /* §6.2.5 内存/指针 */ TC_LOAD,TC_STORE,TC_MEMMOVE,TC_ADDR,TC_PTRADD,TC_PTRSUB,
                 TC_PTRDIFF,TC_PTREQ,TC_PTRNE,TC_PTRLT,TC_PTRLE,TC_PTRGT,TC_PTRGE,TC_SIZEOF,
    /* §6.2.6 函数 */ TC_FUNCADDR,TC_CALL,TC_CALL_INDIRECT,TC_FUNCEQ,TC_FUNCNE,
    /* §6.2.7 结构体/复制 */ TC_MAKE,TC_COPY,
    /* §6.2.8 控制 */ TC_BR,TC_BR_IF,TC_SWITCH,TC_RETURN,TC_TRAP,
    /* §6.2.9 I/O */ TC_READ,TC_WRITE,TC_WRITELN
} TcOpcode;

typedef enum { TC_STRICT, TC_WRAP, TC_IEEE, TC_NSW, TC_NUW, TC_VOLATILE } TcOpMode;

typedef struct {
    TcOpcode op;
    TcOpMode mode;             /* .wrap/.ieee/.volatile/.nsw/.nuw；默认 strict */
    const TcType *type;        /* 类型参数，位置由 opcode 表定 */
    TcOperand dst;             /* LHS：VREG/FIELD/DISCARD；无结果指令必须为 DISCARD */
    TcArg *args; size_t nargs; /* 实参/字段；保留命名 */
    uint32_t *switch_targets; size_t ntargets;   /* switch：0→L0…d−1→Ld−1，其它→Ld */
    TcSpan span;               /* 诊断锚点：文件 + 行列 */
} TcInstr;
```

- `TcInstr.args` 统一承载 `call`/`make` 的位置与命名形式；`TcArg.name` 非空时参与名字、顺序、重复、缺失检查（标准 §6.2.7/§8.8.2）。
- 无结果指令（`br`/`br_if`/`switch`/`return`/`trap`/`store`/`memmove`/`write`/`writeln`）的 `dst.kind` 必须是 `TC_OP_DISCARD`；否则 SEM 报 `TC_CE_OPERAND_COUNT`（标准 §6.15.1）。
- `.nsw`/`.nuw` 只允许在 `@fast` 的 `add/sub/mul/shl` 上；其它 base 在 IR 构造期即报 `TC_CE_SYNTAX`，合法 base 但非 `@fast` 报 `TC_CE_ANNOTATION`（标准 §3.13.2/§6.15.1/§10.9）。
- `TcSpan` 必须同时保留行与列；只存 `line` 不足以满足标准 §12.5.1 的诊断位置与跨文件规则。

### 3.3 基本块、CFG 与单元

```c
typedef struct {
    uint32_t id;
    TcInstr *instrs; size_t n;
    uint32_t *succs, *preds; size_t nsucc, npred;   /* 显式 CFG 边（标准 §7） */
} TcBlock;

typedef struct {
    uint32_t func_id;                  /* UINT32_MAX = 顶层程序 */
    TcBlock *blocks; size_t nblocks;
    uint32_t entry;
    const TcType **vreg_types; size_t nvregs;
    uint32_t *vreg_objects; size_t nvreg_objects;   /* vreg → TcObject id */
} TcUnit;
```

- **控制流扁平**：仅 `br/br_if/switch/return/trap` + `label` 声明（标准 §7.1）；基本块 + 显式边，无结构化 if/while。
- **分析单元封闭**（标准 §7.7）：顶层与各函数 CFG 独立，不拼接/内联/共享节点边。
- **非 SSA**（本文 §15 决策①）：vreg 可多次赋值；SSA 留待优化 pass 按需构造。
- **对象映射**：`vreg_objects` 把每个存储对象 vreg 映射到 `TcObject`，供 `addr`/`ptrdiff`/`memmove`/越界检查与 DI 使用；不能只靠 region+offset 判断同一对象。

### 3.4 IR 元数据与容器

```c
typedef struct TcObject TcObject;            /* 本文 §4 定义 */
typedef struct TcPlatformProfile TcPlatformProfile; /* 本文 §8 定义 */
typedef struct TcTypeTable TcTypeTable;      /* 实现定义的类型表 */

typedef enum {
    TC_PARAM_NONNULL  = 1u << 0,
    TC_PARAM_NO_ALIAS = 1u << 1,
    TC_PARAM_ALIGN    = 1u << 2,
    TC_PARAM_RANGE    = 1u << 3
} TcParamFlags;

typedef struct {
    const char *name;
    const TcType *type;
    TcParamFlags flags;        /* SEM/优化器前置条件；fnptr 不携带；运行期不检查 */
    uint32_t align;            /* @align(N) 的 N；flags&ALIGN 有效 */
    uint64_t lo, hi;           /* @range(lo,hi) 的位模式；按 type 解释；flags&RANGE 有效 */
} TcParam;

typedef struct {
    uint32_t id;
    const char *path;          /* 解析后的文件路径；模块实例键 */
} TcModule;

typedef struct {
    const TcType *ret;
    TcParam *params; size_t nparams;
} TcFuncSig;

typedef enum {
    TC_FN_UNSAFE = 1u << 0, TC_FN_SAFE = 1u << 1, TC_FN_FAST = 1u << 2,
    TC_FN_FAST_MATH = 1u << 3, TC_FN_PURE = 1u << 4, TC_FN_READONLY = 1u << 5,
    TC_FN_NOUNWIND = 1u << 6, TC_FN_WILLRETURN = 1u << 7, TC_FN_NO_OVERFLOW = 1u << 8,
    TC_FN_INLINE = 1u << 9, TC_FN_NOINLINE = 1u << 10
} TcFuncFlags;

typedef struct {
    uint32_t id;
    uint32_t module;           /* 模块实例 id */
    const char *name;
    TcFuncSig sig;
    TcFuncFlags flags;
    TcUnit *body;
} TcFunc;

typedef struct {
    const uint8_t *bytes;
    size_t size;
} TcInitImage;

typedef struct {
    uint32_t id;
    uint32_t module;           /* 模块实例 id */
    const char *name;
    const TcType *type;
    uint64_t bits;             /* 标量 const 的位模式 */
    const TcInitImage *init;   /* 聚合 const / 静态初值 */
} TcConst;

typedef struct {
    TcTypeTable *types;
    TcModule *modules; size_t nmodules; /* module_order 顺序；实例键 = path */
    TcStructDef *structs; size_t nstructs;
    TcFunc *funcs; size_t nfuncs;
    TcConst *consts; size_t nconsts;
    TcObject *objects; size_t nobjects;
    TcUnit *units; size_t nunits;
    const TcPlatformProfile *profile;   /* W/ABI/区表；本文 §8 */
} TcIR;
```

- **TcIR 是编译单元/目标实例的根**；`TcPlatformProfile` 决定 W、区表、ABI、`sizeof`/布局/常量求值，不能把布局或 CT 结果全局缓存到类型节点。
- **TcFunc/TcParam** 保存函数与形参注解；`@nonnull`/`@no_alias`/`@align`/`@range` 由 SEM 校验并记录，供优化器假设。`fnptr` 不携带这些信息（标准 §3.6/§8.5/§8.6），`call_indirect` 不做前置条件检查。**运行期不在调用入口检查 `TcParam`，不产生 trap；违反即 UB**（标准 §10.2/§10.2.1）。
- **`@inline`/`@noinline`**：保存为 `TC_FN_INLINE`/`TC_FN_NOINLINE`；SEM 校验互斥（冲突 → `TC_CE_ANNOTATION`），0.0.46 零 pass 基线只保存不执行。
- **TcStructDef** 在本文 §2.1 定义；字段/padding/`readonly` 与 `sizeof` 按 profile W 计算。
- **const/静态初值**：`TcIR.consts` 保存按 profile 求值的 const（标量 `bits`、聚合 `TcInitImage`）；`TcObject.init` 保存 `static var` 初值。`const` 使用处内联，不占对象；VM 预载静态区，AOT 生成 `.data`。CT 结果不得跨 32/64 共享。CT 成功/失败缓存只存在于前端 CT 求值表；进入 `TcIR` 的 const 均已成功求值，失败不进入代码生成。
- **TcIR 可跨目标共享类型表**，但 `structs` 的 size/offset、`objects`、`units`/CT 结果必须按 profile 实例化。
- **模块实例键控**：`TcIR.modules` 按 `module_order` 保存解析路径；`TcStructDef`/`TcFunc`/`TcConst`/静态 `TcObject` 均带 `module` id。签名、静态存储、成员表以模块实例（文件路径）为键，不以模块名为键；钻石依赖复用同一实例、同一份静态存储。


---

## 4. 内存与区模型

依据：§3.12（对象/存储期/指针状态）、§13.2（区表）、§13.3（布局与地址）。

```c
typedef enum { TC_REG_FRAME, TC_REG_STATIC, TC_REG_DEVICE, TC_REG_CODE, TC_REG_HEAP } TcRegionKind;

typedef enum { TC_LIFE_FRAME, TC_LIFE_PROGRAM, TC_LIFE_HOST } TcLifetime;

struct TcObject {
    uint32_t id;
    uint32_t module;           /* 静态对象的模块实例 id；帧/宿主为 UINT32_MAX */
    TcRegionKind region;
    uint64_t offset, size;     /* offset/size 均按目标地址空间解释 */
    const TcType *type;
    TcLifetime lifetime;
    const TcInitImage *init;   /* static var / 静态区初值；非静态为 NULL */
    int writable;              /* 参数对象为只读 */
    uint32_t parent;           /* 字段子对象的父对象；UINT32_MAX = 无父 */
};

typedef struct {
    TcRegionKind region;
    uint32_t object;           /* TcObject id；代码/设备地址为 UINT32_MAX */
    uint64_t offset;           /* 区内偏移；后端按档案基址映射成真实地址 */
    const TcType *type;
} TcAddress;
```

- **五区**：帧/静态/设备/代码/宿主（堆）（§13.2）。地址 0 保留 `nullptr`，不放 TC 可访问对象。
- **对象模型**（§3.12）：每个 var/参数/程序变量/static var 是对象；struct 值是对象、字段是子对象；宿主分配块是 n≥1 个 T 元素的对象，位于宿主（堆）区。代码区/设备区非语言对象。
- **生命期**：帧内对象（调用进入→返回）；程序变量/static var（程序始→终）；宿主对象（运行时库创建/释放）。生命期结束后指针悬垂。
- **指针状态**（§3.12 表）：

| 状态 | 允许 | 禁止 |
| ---- | ---- | ---- |
| null | copy/比较/ptrdiff(按 0) | load/store/ptradd/ptrsub/memmove/call_indirect（trap） |
| live | 读写/算术/比较/memmove | 超出对象边界 |
| one-past | 比较/ptrdiff/ptradd n=0/ptrsub 回对象内 | load/store/ptradd 非零/memmove |
| dangling | copy/返回/存字段/bitcast ptr↔ptr/比较/ptrdiff | 解引用/ptradd/ptrsub/memmove |

- **悬垂是地址值，非立即 UB**：仅解引用/算术/memmove/对原对象访问产生 UB。
- **对象表**：`TcObject` 是 `addr`/`ptrdiff`/`memmove`/越界检查/DI 的共同事实源；每个 vreg 经 `TcUnit.vreg_objects` 映射到对象，不能只用 region+offset 判断同一对象。
- **宿主对象**：位于 `TC_REG_HEAP`，`lifetime=TC_LIFE_HOST`，`writable=1`；`TcAllocator` 负责创建/释放，核心不提供分配 API。
- **静态布局**（标准 §13.3）：依赖 DAG 逆向拓扑 + 模块内源序；`#program` 顶层 `var` 与 `static var` 同属静态区；按 W 位对齐（32/64 → 4/8 字节）；同一档案布局确定。
- **目标无关**：IR 用区+偏移；真实基址由后端按 `TcPlatformProfile` 映射。翻译不可观察，不改变 `bitcast` 观察到的目标地址值（标准 §13.3）。
- **宽度与算术**：`usize`/`isize`/`ptr`/`fnptr` 宽度 = W；`memmove`/`ptradd` 的 `n` 为 W 位 `usize`；`n × sizeof T` 按数学值计算，超出地址空间即 UB。
- **trap 边界**：空指针/只读区/代码区/设备拒绝是标准 §6.7 的必选 trap；越界/悬垂/one-past/未初始化 UB 只在 checked build 检测。

---

## 5. 前端

依据：§1.7（13 步）、§1.8（处理顺序）、§2（词法）、§4（模块）、§5（存储对象/DI）、§14（EBNF）。

| 步 | 模块 | 依据 |
| ---- | ---- | ---- |
| 1–2 | 读取 + UTF-8 + LEX（记号/缩进/注解/opcode 后缀） | §2 |
| 3 | SYN：按 EBNF 直接建扁平 IR；保留 `TcOperand`/`TcFieldRef`/`TcArg`/`TcSpan` | 标准 §14/本文 §3 |
| 4 | 模块发现：根必须是 `#program`；import DFS 闭包；导入目标为 `#program` 时仍执行 LEX→SYN、不递归其 import，IMPORT 阶段报 `IMPORT_NOT_LIB`；固定 `module_order` | 标准 §4.1/§4.11.1 |
| 5 | IMPORT：解析导入/校验模式/名称冲突/收集签名 | 标准 §4.5/§4.11.2 |
| 6 | SEM 名称：绑定/可见性/成员命名空间/标签；模块值成员 vs struct 名 | 标准 §5.6/§4.6 |
| 7 | SEM 类型与指令：W 相关类型/转换/字面量/`@padding`/`@range`/安全/注解；fnptr 前置条件 | 标准 §3/§6/§8/§9/§13.2 |
| 8 | SEM 布局：`TcStructDef`/字段 offset/`sizeof`/静态布局；按 W 计算 | 标准 §3.7/§6.13/§13.3 |
| 9 | SEM CFG/DI：可达性 + 确定初始化不动点；`vreg_objects`/对象表 | 标准 §5.4/§7.7 |
| 10 | SEM 返回与调用：返回路径/形参/结果/末尾检查；call/make 顺序与安全 | 标准 §8.3/§8.8.2 |
| 11 | SEM 调用图：效果摘要（仅分析/优化，不静态拒绝） | 标准 §8.6 |
| 12 | CT：const/static var 初始化器按需求值；正向 allowlist；按 profile W | 标准 §3.10/§4.10/§13.2.2 |
| 13 | 代码生成：VM 指令流 / 目标汇编；同一 profile | 标准 §1.9 |

**处理顺序**（§1.8）：

1. LEX/SYN 闭包：从根模块按 import 源序 DFS；LEX/SYN 错误立即报告停止；导入缺失/循环/重复记入 IMPORT 缓冲，不阻止发现其它模块。
2. IMPORT：全部可达模块 LEX/SYN 成功后，按 `module_order` + 模块内源序解析导入、校验模式、检查名称冲突、收集签名。
3. SEM：步骤 6–11，全缓冲；按 `module_order`、模块内源位置、标准 §12.2 第 9/10 项的同位置规则选首诊；跨文件位置按 §12.5.1；不按实现步骤抢先报告。
4. CT：全部 SEM 无触发后进入；首个失败按求值遇到的第一个失败报告，位置为目标声明；按 profile W 求值。
5. 代码生成：LEX/SYN/IMPORT/SEM/CT 全成功后执行；以 `TcIR.profile` 实例化布局/对象表/运行时。

- **IR 产物**：`TcIR` 绑定 profile；解析器必须保留字段 lvalue、命名实参、行列 span；不要等到 SEM 再凭源文本重建。
- **目标相关**：`isize`/`usize` 宽度、`sizeof`/布局、`bitcast`/`trunc`、字面量范围、`@range`/`@align` 均由 profile W 决定；跨 32/64 不比较静态结果。
- **EBNF/词法**：`MNEMONIC`、`ANNOTATION`、`field_padding`、`const_path` 按标准 §14；模式专用误用形态由 parser 报专用 SYN 码。

**依赖序 vs 诊断序**（标准 §1.8）：静态布局/签名注册/依赖分析用导入 DAG 逆向拓扑（同层按 `module_order` 打破平局）；诊断顺序用 `module_order`。两者不混用。

---

## 6. 诊断子系统

依据：§12.1（模型）、§12.2（首诊顺序）、§12.3（80 码）、§1.8（缓冲）、§13.5（一致性）。

- **域**：LEX/SYN/IMPORT/SEM/CT/RT。IMPORT 码表归 SEM 子阶段，但报告时机早于 SEM。
- **首诊选取**（标准 §12.2）：阶段优先（LEX→SYN→IMPORT→SEM→CT）→ `module_order` → 模块内源序 → 第 4 项同位置专用码 → 第 5 条款内顺序 → 第 6 项布局归属 → 第 7 项遍历无关 → 第 8 项档案字长 → 第 9/10 项操作数/参数顺序。
- **位置规则**（标准 §12.5.1）：主位置/关联位置是定位规则，不参与首诊排序；跨文件诊断按导入方使用、被导入模块自身代码、import 语句三类确定主位置。
- **80 码**（标准 §12.3）：不增不减。实现资源限制用 `TC_ERR_OUT_OF_MEMORY` 或明确实现级错误，不伪装语言诊断（标准 §13.5）。
- **跨文件位置**（标准 §12.5.1）：导入方使用/`import` 语句定位导入方；被导入模块自身代码定位该模块；关联位置不得改变主位置文件。
- **目标相关**：`bitcast`/`trunc`、字面量范围、布局、`ptrdiff`、`@range`/`@align` 等按 W 产生不同候选码；实现必须在同一档案内固定。
- **无警告**（标准 §12.1）：本标准不定义警告；所有规范诊断都是错误。
- **缓冲**：SEM 全缓冲后选首诊（§1.8 第 3 步）；CT 挂起至 SEM 全过。
- **确定性**（标准 §1.9）：同一档案内不因优化级别/代码生成后端/内部哈希序/线程数改变首诊及其错误码；32/64 跨档案允许目标相关差异（§13.2.2）。
- **不可达代码**（§12.4）：不报告错误；函数入口是独立分析根，不因未被调用判不可达。
- **非法 UTF-8**（标准 §12.5.1）：`TcSpan` 保留最长有效前缀字节数、首个非法字节的 0 基偏移；主位置按最长有效前缀计算，首个非法字节占下一列，主码仍 `TC_CE_SYNTAX`。

---

## 7. 语义运行时库

依据：§6.3（算术语义）、§6.6（转换）、§10.6（浮点）、§11（I/O）。freestanding-first：核心无 libc 依赖，hosted 是其一个档案实例。

### 7.1 语义核（W 相关，纯位运算，按目标编译一次）

- **整数 arith**（标准 §6.3）：`isize`/`usize` 宽度为 W；strict 溢出 trap；`.wrap` 模 2^n；`div` 除零 + `INT_MIN/−1` trap，`mod(INT_MIN,−1)=0` 不 trap；`shl` 负计数/结果不可表示 trap，`shr` 负计数 trap；`int64`/`uint64` 始终 64 位。
- **浮点**（§6.3/§10.6）：IEEE binary32/binary64，固定 roundTiesToEven，固定异常优先级；禁宿主扩展精度/FMA/FTZ/DAZ；strict vs `.ieee`（返回 inf/NaN，不 trap）；`abs/neg` 只操作符号位。
- **cast/trunc/bitcast**（标准 §3.11/§6.6）：cast 范围检查 → `TC_RE_CAST_OVERFLOW`；trunc 仅窄化保留低位，源/目标位宽含 W；bitcast 类别矩阵先于位宽（禁止 → `TC_CE_TYPE_MISMATCH`，允许但不等宽 → `TC_CE_BITCAST_WIDTH`）；`trunc` 字面量源类型按标准 §6.6.3。
- **确定性 I/O**（标准 §11.1/§11.2/§11.6）：十进制输出以源 binary32/64 精确数学值为输入，roundTiesToEven 渲染，不经宿主低精度类型；整条字节全部成功后才提交；失败该条贡献 0 字节；不受 locale 影响；输入保留 `-0` 负零。
- **32 位目标**：`int64`/`uint64` 的 `mul/div/mod/shl/shr/compare`、`float64` 软浮点、未对齐 64 位访问需要运行时辅助；`n × sizeof T` 按数学值计算，不使用 32 位 `usize` 回绕。

### 7.2 平台接口（四条缝）

```c
typedef struct {
    int (*write)(void*, const uint8_t*, size_t);   /* 全部成功或返回错误，不做部分提交 */
    int (*flush)(void*);
    void *ctx;
} TcIoSink;

typedef enum { TC_IO_OK, TC_IO_EOF, TC_IO_ERROR } TcIoStatus;

typedef struct {
    int (*read)(void*, uint8_t*, size_t cap, size_t *n, TcIoStatus *status);
    void *ctx;
} TcIoSrc;

typedef struct { TcIoSink *out; TcIoSrc *in; } TcIo;

typedef struct { void *(*alloc)(void*, size_t); void *(*realloc)(void*, void*, size_t);
                 void (*free)(void*, void*); void *ctx; } TcAllocator;
typedef struct { void (*trap)(void*, TcErrorKind, int line); void *ctx; } TcTrapHandler;
```

| 缝 | hosted 实例 | freestanding 实例 |
| ---- | ---- | ---- |
| ① I/O 汇/源 | stdin/stdout 包装 | UART（PL011）收/发回调 |
| ② 分配器 | 宿主 heap 包装 | 宿主（堆）区 arena/bump |
| ③ trap | 错误标记 / exit | halt / 复位 |
| ④ 模块加载 | 宿主编译期文件解析 | **退出设备端**（设备运行时不解析 import） |

缝④是构建/链接边界：裸机程序在宿主 AOT 编译，设备运行时只有代码+数据+区表。

- `TcIoSink.write` 必须 all-or-nothing；`TcIoSrc.read` 允许短读，返回 `n` 与 `TC_IO_OK/EOF/ERROR`；`read` 在 token 未完成时遇 EOF/错误 → `TC_RE_IO`，不得产生部分 token。
- `TcIoSrc` 的 EOF/错误状态是核心语义输入；差分测试必须固定短读/EOF/错误三种路径。
- `TcAllocator` 的 size/offset 按目标 W 解释；32 位档案不得把宿主 64 位 `size_t` 泄漏到目标对象边界。
- `TcTrapHandler` 只接收首个 trap 的错误码与当前指令行；不得用于实现级诊断。

---

## 8. 平台档案

依据：§13.1（环境定义项）、§13.2（档案字段）。

```c
typedef enum {
    TC_DEV_REJECT,       /* 设备区不可访问：ADDR_UNAVAILABLE */
    TC_DEV_READ,         /* 可读，写拒绝 */
    TC_DEV_READWRITE,    /* 可读可写 */
    TC_DEV_VOLATILE      /* 仅允许 volatile load/store */
} TcDevicePolicy;

typedef struct {
    int normal_status;                         /* 正常终止退出状态 */
    int trap_status;                           /* 首个 trap 的退出状态 */
} TcExitPolicy;

typedef enum { TC_ABI_INT, TC_ABI_FP, TC_ABI_STACK, TC_ABI_INDIRECT } TcABIClass;
typedef enum { TC_ABI_RET_DIRECT, TC_ABI_RET_INDIRECT, TC_ABI_RET_HFA } TcABIRetClass;

typedef struct {
    TcABIClass param_int, param_fp, param_ptr, param_fnptr;
    TcABIClass ret_int, ret_fp;
    int hfa;                                   /* 浮点聚合使用 HFA 规则 */
    uint32_t composite_max_bytes;              /* AAPCS64 = 16；按值复合阈值 */
    uint32_t hfa_max_regs;                     /* AAPCS64 = 4；HFA 最大寄存器数 */
    TcABIRetClass composite_return;            /* 直接/间接/HFA 返回 */
    unsigned stack_align;                      /* 8/16 */
    uint32_t callee_saved;                     /* 寄存器位掩码 */
    int int64_pair;                            /* 32 位 int64 双字传递 */
    int float64_pair;                          /* 32 位 float64 双字/软浮点 */
} TcABIDesc;

typedef struct {
    const char *name;                          /* 例：aarch64-linux-user */
    unsigned addr_width;                       /* 32 或 64 */
    int little_endian;
    unsigned stack_align;                      /* 8/16 */
    unsigned pointer_align;                    /* 4/8 */
    const TcABIDesc *abi;                      /* 参数/返回/struct/HFA 规则 */
} TcTargetDesc;

struct TcPlatformProfile {
    unsigned addr_width;                       /* 32 或 64 */
    int little_endian;
    struct { uint64_t base, size; } region[5]; /* 帧/静态/设备/代码/宿主 */
    size_t stack_size;                         /* 保证 D 个抽象帧可执行 */
    unsigned depth_D;                          /* 抽象调用深度上限 ≥1 */
    TcIo *io;                                  /* stdout + stdin（本文 §7.2） */
    TcAllocator *alloc;                        /* 宿主（堆）区 allocator */
    TcTrapHandler *trap;
    TcExitPolicy exit;                         /* 正常/trap 退出状态（环境定义） */
    uint64_t entry;                            /* 复位入口 / 装载器提供 */
    const TcTargetDesc *target;                /* ABI/字长/端序描述 */
    const char *const *include_roots;          /* import 搜索根；可空 */
    size_t ninclude_roots;
    TcDevicePolicy device;                     /* 设备区策略 */
    int checked_build;                         /* 仅增加 UB 检查，不改变必选 trap */
    int disable_icf;                           /* 必须为 1：禁用 ICF/入口合并 */
};
```

- 64 位实例：`aarch64-linux-user`（hosted）与 `aarch64-baremetal`（freestanding）；32 位实例（Cortex-M/RV32/ARM32-Linux）后续接入。
- 0.0.46 同时定义 32/64 档案；`isize`/`usize`/`ptr`/`fnptr` 宽度为 W，`sizeof`/`ptrdiff`/布局按 W。32 位下 `int64`/`float64` 仍为 64 位，需运行时辅助与独立 ABI。
- 同一档案下 VM 与 AOT 生成相同区内偏移（标准 §13.2/§13.4）；跨档案不比较绝对地址/静态结果。
- `io` 同时提供输出 `TcIoSink` 与输入 `TcIoSrc`；`read`/`write` 的失败映射 `TC_RE_IO`（标准 §11）。
- `device` 决定设备区可读/可写/volatile-only/拒绝；所有构建都必须执行空指针/只读区/代码区/设备拒绝等必选 trap（标准 §6.7/§13.3）。
- `include_roots` 固定 import 搜索顺序（标准 §4.5 的环境定义项）；VM/AOT 必须使用同一列表。
- `checked_build` 只增加越界/悬垂/one-past/未初始化等 UB 检查，不改变必选 trap、合法程序集合或已定义语义。
- `disable_icf` 必须为 1；不同函数入口地址必须互不相同（标准 §3.13.3）。
- `target` 描述 ABI；`TcTargetDesc.abi` 指向 `TcABIDesc`，AArch64 64 位与 32 位 AAPCS32/ILP32 使用不同描述（本文 §10）。
- `exit` 固定正常/trap 退出状态；进程退出状态是环境定义项（标准 §13.1/§4.9），VM/AOT 同档案必须一致并文档化。
- 设备策略映射：`TC_DEV_REJECT` 全部拒绝 → `TC_RE_ADDR_UNAVAILABLE`；`TC_DEV_READ` 读允许、写 → `TC_RE_ADDR_UNAVAILABLE`；`TC_DEV_READWRITE` 读写允许；`TC_DEV_VOLATILE` 仅允许 `load.volatile`/`store.volatile`，其它访问 → `TC_RE_ADDR_UNAVAILABLE`。`TC_RE_WRITE_PROTECTED` 只用于只读参数对象写穿，不用于设备区。

---

## 9. VM 解释器

依据：§1.2（抽象机状态）、§1.3（可观察行为）、§3.12（指针状态）、§5.4（DI）。

- 在扁平 IR + 区内存模型上解释执行；每个 `TcIR` 绑定一个 `TcPlatformProfile`，不得在 32/64 profile 间复用布局/CT/对象表。
- 实现：指针状态检查、确定初始化、trap 语义、bool 规范化、抽象深度 D 检查（`depth==D` → `TC_RE_STACK_OVERFLOW`，标准 §1.2）。
- **角色**：可执行参考。期望输出由标准精确规则推导（标准 §11.1/§11.2/§11.3 等），不依赖既有实现。
- 与目标后端（AArch64 64 位 / 32 位后端）消费同一 IR → 同档案差分位级一致（标准 §13.4）。
- 可观察行为符合标准 §1.3；地址相关结果环境定义且不保证跨优化稳定（标准 §10.5）。
- **目标宽度**：`ptr`/`fnptr`/`isize`/`usize` 值按 W 位截断/检查；宿主 64 位 `uintptr_t`/`size_t` 不得泄漏到目标观察值；`n × sizeof T` 按数学值计算。
- **对象/内存**：使用 `TcIR.objects`/`TcUnit.vreg_objects` 判断同一对象、边界、生命期；宿主对象位于 `TC_REG_HEAP`；`addr` 只产生核心对象地址。
- **I/O**：`TcIo` 的 `out` 按整条字节提交，`in` 负责 `read` 的字节/EOF/错误；`read`/`write` 失败映射 `TC_RE_IO`。
- **检查**：必选 trap（空指针/只读区/代码区/设备拒绝）始终执行；checked build 只增加 UB 检测。
- **提交语义**（标准 §12.5.2）：先完成全部检查再提交目标/输出；失败不部分提交，I/O 不回滚；trap 后不提交。

---

## 10. 后端（AArch64 先行）

依据：§6（指令集）、§13（平台档案/布局）。

### 10.1 优化契约与零 pass 基线

0.0.46 基线采用**显式零 pass**，先把正确性/差分闭合，再引入优化：

- `O0`/`O2` 初期均不执行 IR 变换；两者语义一致，区别仅保留为工具链接口/未来扩展。
- `@fast` 承诺被接受并记录，但优化器暂不消费；`@fast_math` 只保留标准允许的语义放宽，不做重关联/FMA/FTZ/DAZ 变换。
- `@inline`/`@noinline` 只保存为 `TcFuncFlags`，不执行内联；SSA/regalloc/内联/别名优化全部延后。
- 若后续增加优化，必须新增 pass 管线与标准 §10.8 不变量测试；不得在未声明的情况下改变本基线。
- P0 契约包含“零 pass 基线”；§12 的内联相关验证标注为“延后”。

### 10.2 入口与调用约定

- 入口：`tc_emit_asm(FILE*, const TcIR*, const TcPlatformProfile*, const TcTargetDesc*)`；`TcIR` 定义见本文 §3.4，`TcTargetDesc` 见本文 §8。
- **AAPCS64**：整数/指针用 x0–x7，`float32/float64` 用 v0–v7；x0/v0 返回；x8 间接结果；x0/x1 或 v0–v3 返回小复合/HFA；x19–x28 + v8–v15 callee-saved；x16/x17 为 IP0/IP1；x18 平台寄存器；x29 FP、x30 LR；sp 16 字节对齐。struct 按 AAPCS64 复合/HFA 规则；bool/int8/int16 扩展到 32 位。
- **AAPCS32/ILP32（32 位目标）**：r0–r3 参数，r0/r1 或 v0 返回；r4–r11 callee-saved；sp 8 字节对齐；`int64`/`float64` 走双字/软浮点；struct 按 AAPCS32 复合规则；bool/int8/int16 扩展到 32 位。需独立 backend 与 `TcTargetDesc`，不复用 AArch64 ABI。
- **帧/区布局**：按档案把区偏移映射到真实地址；帧区栈上，静态区/设备区/宿主区固定基址；`TcObject` 的 offset/size 按 W 解释。
- **ABI 表**：每个 `TcTargetDesc.abi`（`TcABIDesc`）规定整数/浮点/指针/fnptr 参数类、返回类、`composite_max_bytes`、`hfa_max_regs`、`composite_return`、栈对齐、callee-saved、`int64`/`float64` 传递方式；具体寄存器分配由各 target ABI 文档细化，后端不得把 32 位目标当 AArch64 处理。
- **直接调用前置条件**：`TcParam` 的 `@nonnull`/`@align`/`@range` 只由 SEM 校验并作为优化器假设；**运行期不在调用入口检查，不产生 trap**；`call_indirect` 不携带这些信息，不做检查。违反即 UB（标准 §3.6/§8.5/§8.6/§10.2/§10.2.1）。

### 10.3 lowering 分工

| IR 类别 | 初期实现 |
| ---- | ---- |
| 整数 add/sub/mul/bitwise/shift | 原生 + 溢出/负计数检查→trap（strict）；32 位 `int64` 需双字或运行时辅助 |
| div/mod | 原生 sdiv/udiv + msub + 除零/INT_MIN÷−1 trap（`mod(INT_MIN,−1)=0` 不 trap） |
| compare/逻辑 | 原生 cmp/cset；32 位 `int64` 比较需双字/辅助 |
| br/br_if/switch/return/trap | 原生 b/b.cond/cbz/tbz/ret + 标签映射；trap 调运行时 |
| call/call_indirect | bl/blr + ABI 帧 + 深度 D 检查；达到 D → `TC_RE_STACK_OVERFLOW` |
| load/store（标量/指针/fnptr） | 原生 ldr/str + 必选空指针/只读区/代码区/设备检查；bool 读写规范化；越界/悬垂/one-past 仅 checked build |
| load.volatile/store.volatile | 原生 volatile 访问；不得合并/删除/重排相对其它 volatile 访问；不得被普通 load/store 重排跨越 |
| load/store（struct/int64/float64） | 按大小拆字/调运行时；未对齐按目标能力拆分；32 位 64 位值需双字 |
| funcaddr/funceq/funcne | 原生 `adrp/add`/地址比较；`call_indirect` 空指针/签名由运行时/UB 规则处理 |
| sizeof | 编译期常量；按 `TcStructDef.size`/profile W |
| **FP/cast/bitcast** | **调运行时**（确定性，§6.3/§10.6） |
| read/write/writeln | 调运行时 I/O |
| neg/abs/ptreq/ptrne/ptrlt/ptrle/ptrgt/ptrge | 原生比较/符号处理；`abs/neg` 溢出 trap |
| make/copy/memmove/addr/ptr* | 调运行时（复杂内存/指针状态）；`n × sizeof T` 按数学值 |

- 初期**栈上局部 + 固定临时寄存器，无 regalloc**（本文 §15 决策③）；线性扫描 regalloc 留作后续优化。
- **必选检查**：空指针/只读区/代码区/设备拒绝/trap/深度/转换溢出在 release 构建也执行；checked build 只加 UB 检查。
- **32 位**：`int64`/`uint64` 的乘除/移位/比较、`float64` 软浮点、未对齐 64 位访问可能需要运行时辅助；不得假设原生 64 位。
- **`@fast_math`**：只在 `@fast_math` 函数词法体内放宽；0.0.46 零 pass 不内联；间接调用不扩散许可；未来若内联，保留同样边界。
- FP 留运行时保确定性；逐项原生化需差分验证。
- `.nsw/.nuw`（§10.2）仅在 `@fast` 内作为优化承诺，不改变默认 trap 语义。

---

## 11. 工具链与构建

- **汇编**：clang 集成汇编（`clang -target aarch64 -c`）。
- **链接**：lld（`ld.lld`）。Apple ld64 不能链 ELF，故主路径用 lld。
- **执行**：64 位 hosted 用 qemu-aarch64 用户态（ELF，qemu-user 需 Linux 宿主）；64 位裸机用 qemu-system-aarch64（virt + PL011 UART）；32 位用 qemu-arm/qemu-riscv32 或 qemu-system-arm/riscv32。
- **驱动**：TC → `.s` → `.o` →（+ 运行时.a）→ ELF / flat binary。
- **主机现实**：macOS arm64 上 clang/lld 可用，qemu-system 可用；qemu-user 通常需要 Linux 宿主，aarch64/arm32 用户态差分应放 Linux CI/容器。
- **零安装冒烟备选**：arm64-apple-darwin Mach-O（现有工具，host 即 arm64 可直接跑）；Apple ABI 有变体，仅作非规范性开发便利，不进入差分门禁，不改变 ELF 主路径。
- **裸机**：链接脚本定义复位入口/区基址；运行时以 `-ffreestanding -nostdlib` 编译；32 位后端使用独立 target triples/ABI。
- **32/64 目标**：`clang -target aarch64-...`、`armv7m-none-eabi`、`riscv32-unknown-elf` 等由 `TcTargetDesc` 选择；每次构建只绑定一个 profile。
- **qemu 限制**：qemu-user 的可用性属于宿主环境，不改变标准/ABI；不可用时用 qemu-system 或 Linux CI，不把 macOS qemu-user 作为门禁。

---

## 12. 验证策略

依据：§13.4（验证要求）、§13.3（checked build）。

1. **VM↔AOT 差分**：同源同档案（32/64 分档案），输出/退出/trap 位级一致；按地址字节差对拍，不比绝对地址；不比较跨 32/64 结果。
2. **标准 §13.4 八项一致性**：静态一致 / 模块顺序 / CFG-DI / 类型转换 / 安全 / 浮点 / I-O / 平台。
3. **golden 向量**：从标准精确规则推导期望输出；32/64 分别建库，不比较跨档案结果。
4. **freestanding 编译门禁**：`-ffreestanding -nostdlib` 交叉编译运行时 + 最小 UART stub。
5. **checked build**：trap 用例与静态诊断分开统计；空指针/只读区/代码区/设备拒绝等定义好的 trap 在所有构建中执行；悬垂栈水位/毒化检查只在产生 UB 的访问触发（标准 §13.3），不放宽规范、不改变合法程序集合。
6. **32/64 静态矩阵**：`bitcast`/`trunc`/`cast`、`sizeof`/`@padding` 布局、`ptrdiff`、字面量范围、`@range`/`@align`、`read/write` 的 32/64 翻转；`TC_CE_BITCAST_WIDTH`/`TRUNC_WIDTH`/`LITERAL_RANGE`/`LAYOUT_OVERFLOW` 等按档案。
7. **ABI/调用**：64 位 AAPCS64 与 32 位 AAPCS32/ILP32 的参数/返回/struct/HFA/int64/float64/bool 扩展；`sp` 对齐、callee-saved、`addr` 参数、`funcaddr`；直接 `call` 的前置条件只进入优化器假设、运行期无检查；`call_indirect` 无检查；SEM 注解位置/类型/范围错误仍按 80 码。
8. **对象/内存**：`TcObject` 边界/生命期/宿主 heap、`ptrdiff`/`memmove`/`n × sizeof T`、未对齐 64 位访问、必选 trap 与 checked build 分离。
9. **诊断/语法**：同位置候选总序、跨文件位置、call/make 参数顺序、`(base,suffix)` 矩阵、EBNF token/mode-misuse；80 码双射。
10. **`@fast_math`**：直接 float 内存/I-O、`cast` 派生值、控制流、间接调用边界；零 pass 基线不执行内联，相关内联验证延后。
11. **IR 形态**：字段路径作为值（`copy T, s.f`、`br_if s.flag`、`call_indirect s.fp`）、字段作为 lvalue、`addr T, s.f`、命名实参/字段；所有合法源形态可构造。
12. **const/静态初值**：`TcIR.consts`/`TcObject.init` 的 VM 预载与 AOT `.data` 字节一致；`const` 使用处内联；CT 结果按 32/64 实例化。
13. **类型表/模块实例**：Analyze 后类型表冻结、不可重解析/改写；`TcIR.modules`/`TcModule` 键控；同路径钻石依赖共享静态存储，同名不同路径分离。
14. **退出/设备/输入**：正常/trap 退出状态；四种 `TcDevicePolicy` 的 trap 码；`TcIoSrc` 短读/EOF/错误路径。
15. **零 pass**：O0/O2 无 IR 变换；`@fast` 承诺不消费；`@inline`/`@noinline` 只保存；无内联/regalloc/SSA。

---

## 13. 分期路线

清洁建造（无迁移期）：

| 期 | 内容 | 验证 |
| ---- | ---- | ---- |
| **P0 契约** | IR 规格 / 32+64 Profile / 运行时 ABI / 诊断模型 / 零 pass 优化契约 / AArch64 target desc（设计先行） | 契约评审 |
| **P1 前端+IR+VM** | 0.0.46 标量子集前端 + 扁平 IR + VM 参考（32/64 profile） | VM 跑通子集；诊断序符合 §12.2；字长相关结果分档案 |
| **P2 AArch64 子集** | 子集后端 + 工具链 + qemu | VM↔AArch64 子集差分位级一致 |
| **P3 全量** | ptr/区/struct/控制流/fp 全量 + 全量运行时 | 全量差分 + §13.4 八项 |
| **P4 裸机** | freestanding 档案 + aarch64-elf + 32 位后端（Cortex-M/RV32）+ 链接脚本 + UART | qemu-system；freestanding 门禁；32/64 分档案 |
| **P5 多目标** | RISC-V / x86-64 / Cortex-M 按需 | 各目标差分 |

---

## 14. 模块结构

```
src/
  frontend/   lex parse module resolve analyze consteval diagnostic ebnf
  ir/         type instr block cfg region address operand field span object module const ir print
  runtime/    sem_int sem_fp sem_cast sem_bitwise io sink input heap alloc trap profile fastmath
  vm/         interp memory pointer_state
  backend/
    aarch64/  lower emit_asm frame layout regalloc(后期)
    arm32/    32 位目标（AAPCS32；目标待定）
    riscv32/  32 位目标（ILP32；目标待定）
    common/   target_desc abi toolchain_driver
  driver/     tc-vm tc-aot
tests/
  conformance/ static runtime abi 32-64-diff
```

目标无关层（frontend/ir/runtime 核/vm）与目标相关层（backend/<target>）分离；新增目标 = 新增 backend 目录，其余复用。`TcIR`/`TcStructDef.size`/`TcObject`/CT 结果按 profile 实例化；类型表可共享。

构建目标只有私有 `tc_core`（不安装、不导出）与 `tc-vm`/`tc-aot`；不重新引入 C 宿主公共库/嵌入 API。

---

## 15. 已定设计决策

按设计预期（裸机+OS、确定性优先、清洁多目标、AArch64 先行、正确性优先于激进优化）选定：

| # | 决策 | 理由 |
| ---- | ---- | ---- |
| ① | **非 SSA 扁平 TAC IR + vreg + 显式 CFG**；SSA 延后为按需优化形态 | 源已扁平，IR 贴近源利于差分调试；0.0.46 先零 pass，目标是裸机+确定性+多目标，非激进优化；标准 §10 契约为 as-if 保持行为，不要求 SSA |
| ② | **FP/cast/复杂内存调运行时，简单整数/控制流原生** | 最难确定性语义留在与 VM 共享的运行时核，正确性最有保障；运行时 freestanding，裸机无 libc 依赖 |
| ③ | **初期无 regalloc**（栈上局部 + 固定临时） | 正确性优先、跨目标可移植；regalloc 是优化非正确性需求；线性扫描留作后续 |
| ④ | **ELF + clang 集成汇编 + lld + qemu** | ELF 是裸机终态同格式/标准 ABI（AAPCS64/AAPCS32/ILP32），无 ABI 返工；Mach-O 为裸机死路；clang/lld 是标准平台工具 |
| ⑤ | **分层模块结构**（目标无关 core + 每目标 backend） | 加目标 = 加 backend 目录，其余复用；支持多目标+清洁架构 |
| ⑥ | **32/64 双字长正式档案**；`usize`/`isize`/`ptr`/`fnptr` 宽度 = W | 0.0.46 要求同时支持 32/64；AArch64 64 先行，32 位后端后续；`isize`/`usize` 保持独立类型，不做固定宽度别名 |
| ⑦ | **私有 core + 两个 CLI**；不提供 C 宿主公共库/嵌入 API | `tc-vm`/`tc-aot` 是唯一交付入口；`tc_core` 不安装、不导出 ABI；避免引入 C 宿主 API 负担 |
| ⑧ | **五区 + 对象表**（帧/静态/设备/代码/宿主） | 宿主对象有明确区归属；`TcObject` 统一边界/生命期/指针状态，支持 `ptrdiff`/`memmove`/checked build |
| ⑨ | **必选 trap 与 checked build 分离** | 空指针/只读/代码区/设备/转换溢出等定义好的 trap 始终执行；checked build 只加 UB 检查，保证 release 与规范一致 |
| ⑩ | **诊断/参数/后缀用单一规则表** | 首诊总序（§12.2 第 9/10 项）、call/make 顺序、`(base,suffix)` 矩阵、EBNF token 规则集中维护，避免实现分叉 |

---

## 16. 风险与取舍

- **确定性语义重新验证**：fp 渲染/异常优先级从规范重新推导并验证（真实工作量；标准精确、可验证）。
- **溢出/trap 正确性**：原生指令需补检查分支，是后端正确性关键。
- **32 位后端与 ABI**：AAPCS32/ILP32、`int64`/`float64` 辅助、未对齐访问与 32 位 `n × sizeof T` 是额外工作量。
- **heap/宿主对象**：第五区、allocator 边界、`ADDR_UNAVAILABLE` 与 host 对象生命周期需实现验证。
- **输入缝**：`TcIoSrc` 的 EOF/错误/部分读语义已在 §7.2 固定；仍需 VM/AOT 差分覆盖短读/EOF/错误三种路径。
- **诊断/参数/后缀表**：若实现各自解释，首诊与错误码会分叉；必须共用标准 §12.2（第 9/10 项）/§6.15 的规则表。
- **fnptr 前置条件**：`@nonnull`/`@align`/`@range` 不经 `fnptr` 携带；运行期不检查，违反即 UB；测试需覆盖 SEM 注解校验与间接调用的 unsafe 边界。
- **`@fast_math` 内联**：数据依赖闭包与权限边界需在优化 pass 中显式保持。
- **qemu-user 可用性**：macOS 通常无 qemu-user，32/64 hosted 差分放 Linux CI；不能用本机 Mach-O 代替 ELF 门禁。
- **无 regalloc 初期性能**：可控，后续优化。
- **工具链安装**：lld + qemu 一次性。
- **体量**：全栈清洁重建，重大 effort，独立分支。
- **参考自建**：新 VM 先行作可执行参考，无既有实现依赖。

---

*语言合法性、语义、诊断码与平台契约以 [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md) 为准；本文规定实现架构与契约，描述待实现目标。*