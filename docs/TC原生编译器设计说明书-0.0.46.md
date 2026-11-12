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
- [10. AArch64 后端](#10-aarch64-后端)
- [11. 工具链与构建](#11-工具链与构建)
- [12. 验证策略](#12-验证策略)
- [13. 分期路线](#13-分期路线)
- [14. 模块结构](#14-模块结构)
- [15. 已定设计决策](#15-已定设计决策)
- [16. 风险与取舍](#16-风险与取舍)

---

## 0. 依据、目标与规范注记

### 0.1 设计依据

唯一输入为 0.0.46 标准。下文每节标注所依据的标准章节。既有实现不作为设计输入；正确性参考由新建 VM 与标准精确规则提供（见 §9、§12）。

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

---

## 1. 总体架构

```
TC 源 ─► 前端(LEX/SYN/IMPORT/SEM/CT) ─► 扁平 TAC IR（目标无关）
                                              │
                    ┌─────────────────────────┼─────────────────────────┐
                    ▼                          ▼                         ▼
              VM 解释器(参考)           目标后端(AArch64 先行)      优化 pass(§10)
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

---

## 3. 扁平 TAC IR

依据：§6.1（统一指令形态）、§6.2（opcode 表）、§7（控制流）、§3.12（对象/指针）。

**单级扁平**：0.0.46 源已是「一条指令一个 op、操作数是值、不得嵌套 op/call」（§6.1），故 parser 直接产扁平 IR，无需 AST→TAC 降级。临时变量是源语言一等公民（§6.1 规则 6 要求先落临时）。

### 3.1 操作数

```c
typedef enum { TC_OP_VREG, TC_OP_CONST, TC_OP_LABEL, TC_OP_FUNC,
               TC_OP_STRUCT, TC_OP_DISCARD } TcOperandKind;

typedef struct {
    TcOperandKind kind;
    union {
        uint32_t vreg;                                   /* 虚拟寄存器/临时 */
        struct { uint64_t bits; const TcType *type; } konst;
        uint32_t label;
        uint32_t func;
        uint32_t struct_id;
    };
} TcOperand;
```

`TC_OP_DISCARD` 对应 `_`（§6.1 规则 7）：仅作 LHS，不作读取操作数，不参与 DI。

### 3.2 指令

```c
typedef enum {
    /* §6.2.1 算术 */ TC_ADD,TC_SUB,TC_MUL,TC_DIV,TC_MOD,TC_NEG,TC_ABS,TC_SHL,TC_SHR,
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
    TcOperand dst;             /* LHS 或 DISCARD */
    TcOperand *args; size_t nargs;
    uint32_t *switch_targets; size_t ntargets;   /* switch：0→L0…d−1→Ld−1，其它→Ld */
    int line;                  /* 诊断锚点 */
} TcInstr;
```

opcode 与 §6.2 表一一对应；模式后缀语义见 §6.3（算术）、§10.2（`.nsw/.nuw` 为 `@fast` 优化承诺）。

### 3.3 基本块与 CFG

```c
typedef struct {
    uint32_t id;
    TcInstr *instrs; size_t n;
    uint32_t *succs, *preds; size_t nsucc, npred;   /* 显式 CFG 边 §7 */
} TcBlock;

typedef struct {
    uint32_t func_id;                  /* -1 = 顶层程序 */
    TcBlock *blocks; size_t nblocks;
    uint32_t entry;
    const TcType **vreg_types; size_t nvregs;
    /* 对象/区映射、生命期元数据：供 DI 与指针状态分析 */
} TcUnit;
```

- **控制流扁平**：仅 `br/br_if/switch/return/trap` + `label` 声明（§7.1）；基本块 + 显式边，无结构化 if/while。
- **分析单元封闭**（§7.7）：顶层与各函数 CFG 独立，不拼接/内联/共享节点边。
- **无源名**：操作数已解析为 vreg/const/label/func/struct；源名仅留诊断元数据。
- **非 SSA**（见 §15 决策①）：vreg 可多次赋值；SSA 留待优化 pass 按需构造。

---

## 4. 内存与区模型

依据：§3.12（对象/存储期/指针状态）、§13.2（区表）、§13.3（布局与地址）。

```c
typedef enum { TC_REG_FRAME, TC_REG_STATIC, TC_REG_DEVICE, TC_REG_CODE } TcRegionKind;

typedef struct {
    TcRegionKind region;
    uint64_t offset;           /* 区内偏移；后端按档案基址映射成真实地址 */
    const TcType *type;
} TcAddress;
```

- **四区**：帧/静态/设备/代码（§13.2）。地址 0 保留 `nullptr`，不放 TC 可访问对象。
- **对象模型**（§3.12）：每个 var/参数/程序变量/static var 是对象；struct 值是对象、字段是子对象；宿主分配块是 n≥1 个 T 元素的对象。代码区/设备区非语言对象。
- **生命期**：帧内对象（调用进入→返回）；程序变量/static var（程序始→终）；宿主对象（运行时库创建/释放）。生命期结束后指针悬垂。
- **指针状态**（§3.12 表）：

| 状态 | 允许 | 禁止 |
| ---- | ---- | ---- |
| null | copy/比较/ptrdiff(按 0) | load/store/ptradd/ptrsub/memmove/call_indirect（trap） |
| live | 读写/算术/比较/memmove | 超出对象边界 |
| one-past | 比较/ptrdiff/ptradd n=0/ptrsub 回对象内 | load/store/ptradd 非零/memmove |
| dangling | copy/返回/存字段/bitcast ptr↔ptr/比较/ptrdiff | 解引用/ptradd/ptrsub/memmove |

- **悬垂是地址值，非立即 UB**：仅解引用/算术/memmove/对原对象访问产生 UB。
- **静态布局**（§13.3）：依赖 DAG 逆向拓扑 + 模块内源序；按地址宽度对齐；同一档案布局确定。
- **目标无关**：IR 用区+偏移；真实基址由后端按 `TcPlatformProfile` 映射。翻译不可观察，不改变 `bitcast` 观察到的目标地址值（§13.3）。

---

## 5. 前端

依据：§1.7（13 步）、§1.8（处理顺序）、§2（词法）、§4（模块）、§5（存储对象/DI）、§14（EBNF）。

| 步 | 模块 | 依据 |
| ---- | ---- | ---- |
| 1–2 | 读取 + UTF-8 + LEX（记号/缩进/注解/opcode 后缀） | §2 |
| 3 | SYN：按 EBNF 直接建扁平 IR | §14 |
| 4 | 模块发现：import DFS 闭包，固定 `module_order` | §4.11.1 |
| 5 | IMPORT：解析导入/校验模式/收集签名 | §4.5 |
| 6 | SEM 名称：绑定/可见性/成员命名空间/标签 | §5.6 |
| 7 | SEM 类型与指令：类型检查/转换/字面量/安全/注解放置 | §6/§9 |
| 8 | SEM 布局：struct/sizeof/静态布局 | §3.7/§13.3 |
| 9 | SEM CFG：可达性 + 确定初始化不动点 | §5.4/§7.7 |
| 10 | SEM 返回与调用：返回路径/形参/结果/末尾检查 | §8 |
| 11 | SEM 调用图：效果摘要（仅分析/优化，不静态拒绝） | §8.6 |
| 12 | CT：const/static var 初始化器按需求值 | §3.10/§4.10 |
| 13 | 代码生成：VM 指令流 / 目标汇编 | §1.9 |

**处理顺序**（§1.8）：

1. LEX/SYN 闭包：从根模块按 import 源序 DFS；LEX/SYN 错误立即报告停止；导入缺失/循环/重复记入 IMPORT 缓冲，不阻止发现其它模块。
2. IMPORT：全部可达模块 LEX/SYN 成功后，按 `module_order` + 模块内源序解析导入、校验模式、检查名称冲突、收集签名。
3. SEM：步骤 6–11，全缓冲；按 `module_order`、模块内源位置、同位置专用码优先选首诊；不按实现步骤抢先报告。
4. CT：全部 SEM 无触发后进入；首个失败按求值遇到的第一个失败报告，位置为目标声明。
5. 代码生成：LEX/SYN/IMPORT/SEM/CT 全成功后执行。

**依赖序 vs 诊断序**（§1.8）：静态布局/签名注册/依赖分析用导入 DAG 逆向拓扑（同层按 `module_order` 打破平局）；诊断顺序用 `module_order`。两者不混用。

---

## 6. 诊断子系统

依据：§12.1（模型）、§12.2（首诊顺序）、§12.3（80 码）、§1.8（缓冲）、§13.5（一致性）。

- **域**：LEX/SYN/IMPORT/SEM/CT/RT。IMPORT 码表归 SEM 子阶段，但报告时机早于 SEM。
- **首诊选取**（§12.2）：阶段优先（LT→SYN→SEM→CT）→ 同阶段按源位置 → 规则优先（专用码优先）→ 关联位置 → 遍历无关。
- **80 码**（§12.3）：不增不减。实现资源限制用 `TC_ERR_OUT_OF_MEMORY` 或明确实现级错误，不伪装语言诊断（§13.5）。
- **缓冲**：SEM 全缓冲后选首诊（§1.8 第 3 步）；CT 挂起至 SEM 全过。
- **确定性**（§1.9）：不因优化级别/目标后端/内部哈希序/线程数改变首诊及其错误码。
- **不可达代码**（§12.4）：不报告错误；函数入口是独立分析根，不因未被调用判不可达。

---

## 7. 语义运行时库

依据：§6.3（算术语义）、§6.6（转换）、§10.6（浮点）、§11（I/O）。freestanding-first：核心无 libc 依赖，hosted 是其一个档案实例。

### 7.1 语义核（目标无关，纯位运算，每目标编译一次）

- **整数 arith**（§6.3）：strict 溢出 trap；`.wrap` 模 2^n；div/mod 除零 + 整数 `INT_MIN/−1` trap；shl 负计数/结果不可表示 trap；shr 负计数 trap。
- **浮点**（§6.3/§10.6）：IEEE binary32/binary64，固定 roundTiesToEven，固定异常优先级；禁宿主扩展精度/FMA/FTZ/DAZ；strict vs `.ieee`（返回 inf/NaN，不 trap）；`abs/neg` 只操作符号位。
- **cast/trunc/bitcast**（§3.11/§6.6）：cast 范围检查 → `TC_RE_CAST_OVERFLOW`；trunc 仅窄化保留低位；bitcast 类别矩阵先于位宽（禁止 → `TC_CE_TYPE_MISMATCH`，允许但不等宽 → `TC_CE_BITCAST_WIDTH`）。
- **确定性 I/O**（§10.4/§11.2）：十进制输出以源 binary32/64 精确数学值为输入，roundTiesToEven 渲染，不经宿主低精度类型；内存生成完整字节串→一次提交（§10.5）；不受 locale 影响。

### 7.2 平台接口（四条缝）

```c
typedef struct { int (*write)(void*, const uint8_t*, size_t); int (*flush)(void*); void *ctx; } TcIoSink;
typedef struct { void *(*alloc)(void*, size_t); void *(*realloc)(void*, void*, size_t);
                 void (*free)(void*, void*); void *ctx; } TcAllocator;
typedef struct { void (*trap)(void*, TcErrorKind, int line); void *ctx; } TcTrapHandler;
```

| 缝 | hosted 实例 | freestanding 实例 |
| ---- | ---- | ---- |
| ① I/O 汇 | libc 流包装 | UART（PL011）回调 |
| ② 分配器 | malloc 包装 | 区表上 arena/bump |
| ③ trap | 错误标记 / exit | halt / 复位 |
| ④ 模块加载 | 宿主编译期文件解析 | **退出设备端**（设备运行时不解析 import） |

缝④是构建/链接边界：裸机程序在宿主 AOT 编译，设备运行时只有代码+数据+区表。

---

## 8. 平台档案

依据：§13.1（环境定义项）、§13.2（档案字段）。

```c
typedef struct {
    unsigned addr_width;                       /* 32 或 64 */
    int little_endian;
    struct { uint64_t base, size; } region[4]; /* 帧/静态/设备/代码 */
    size_t stack_size;                         /* 保证 D 个抽象帧可执行 */
    unsigned depth_D;                          /* 抽象调用深度上限 ≥1 */
    TcIoSink *io;
    TcAllocator *alloc;
    TcTrapHandler *trap;
    uint64_t entry;                            /* 复位入口 / 装载器提供 */
} TcPlatformProfile;
```

- 64 位实例：`aarch64-linux-user`（hosted）与 `aarch64-baremetal`（freestanding）；32 位实例（Cortex-M/RV32/ARM32-Linux）后续接入。
- 0.0.46 同时定义 32/64 档案；`isize`/`usize`/`ptr`/`fnptr` 宽度为 W，`sizeof`/`ptrdiff`/布局按 W。32 位下 `int64`/`float64` 仍为 64 位，需运行时辅助与独立 ABI。
- 同一档案下 VM 与 AOT 生成相同区内偏移（§13.2/§13.4）。
- 32/64 均为正式档案；AArch64 为 64 位先行目标，32 位后端（AAPCS32/ILP32）在 P4/P5 接入（§13.2）。
- 代码合并必须禁用 ICF 或等价入口合并（§13.2）。

---

## 9. VM 解释器

依据：§1.2（抽象机状态）、§1.3（可观察行为）、§3.12（指针状态）、§5.4（DI）。

- 在扁平 IR + 区内存模型上解释执行。
- 实现：指针状态检查、确定初始化、trap 语义、bool 规范化、抽象深度 D 检查（`depth==D` → `TC_RE_STACK_OVERFLOW`，§1.2）。
- **角色**：可执行参考。期望输出由标准精确规则推导（§10.4 等），不依赖既有实现。
- 与目标后端（AArch64 64 位 / 32 位后端）消费同一 IR → 同档案差分位级一致（§12）。
- 可观察行为符合 §1.3；地址相关结果环境定义且不保证跨优化稳定（§10.5）。

---

## 10. AArch64 后端

依据：§6（指令集）、§13（平台档案/布局）。

### 10.1 入口与调用约定

- 入口：`tc_emit_asm(FILE*, const TcIR*, const TcPlatformProfile*, target)`。
- **AAPCS64**：x0–x7 参数 / x0 返回 / x9–x15 临时 / x19–x28 callee-saved / x29 FP / x30 LR / sp 16 字节对齐。整数/指针标量按 64 位 x 寄存器传入；`float32`/`float64` 走 v0–v7，结构体按 AAPCS64 复合规则。
- **AAPCS32/ILP32（32 位目标）**：r0–r3 参数 / r0–r1 或 v0 返回 / r4–r11 callee-saved / sp 8 字节对齐；`int64`/`float64` 双字或软浮点；需独立 backend 与 target desc，不复用 AArch64 ABI。
- **帧/区布局**：按档案把区偏移映射到真实地址；帧区栈上，静态区/设备区固定基址。

### 10.2 lowering 分工

| IR 类别 | 初期实现 |
| ---- | ---- |
| 整数 add/sub/mul/bitwise/shift | 原生 + 溢出/负计数检查→trap（strict） |
| div/mod | 原生 sdiv/udiv + msub + 除零/INT_MIN÷−1 trap |
| compare/逻辑 | 原生 cmp/cset |
| br/br_if/switch/return | 原生 b/b.cond/cbz/tbz/ret + 标签映射 |
| call/call_indirect | bl/blr + AAPCS64 帧 + 深度 D 检查 |
| load/store（标量） | 原生 ldr/str + 区/状态检查（checked build） |
| **FP/cast/bitcast** | **调运行时**（确定性，§6.3/§10.6） |
| read/write/writeln | 调运行时 I/O |
| make/copy/memmove/addr/ptr* | 调运行时（复杂内存/指针状态） |

- 初期**栈上局部 + 固定临时寄存器，无 regalloc**（§15 决策③）；线性扫描 regalloc 留作后续优化。
- FP 留运行时保确定性；逐项原生化需差分验证。
- `.nsw/.nuw`（§10.2）仅在 `@fast` 内作为优化承诺，不改变默认 trap 语义。

---

## 11. 工具链与构建

- **汇编**：clang 集成汇编（`clang -target aarch64 -c`）。
- **链接**：lld（`ld.lld`）。Apple ld64 不能链 ELF，故主路径用 lld。
- **执行**：qemu-aarch64 用户态（ELF）；qemu-system-aarch64（裸机 virt + PL011 UART）。
- **驱动**：TC → `.s` → `.o` →（+ 运行时.a）→ ELF / flat binary。
- **主机现实**：macOS arm64，clang/as/ld64 在；lld/qemu 需一次性安装（`brew install llvm qemu`）。
- **零安装冒烟备选**：arm64-apple-darwin Mach-O（现有工具，host 即 arm64 可直接跑）；Apple ABI 有变体，非主路径。
- **裸机**：链接脚本定义复位入口/区基址；运行时以 `-ffreestanding -nostdlib` 编译。

---

## 12. 验证策略

依据：§13.4（验证要求）、§13.3（checked build）。

1. **VM↔AOT 差分**：同源同档案（32/64 分档案），输出/退出/trap 位级一致；按地址字节差对拍，不比绝对地址；不比较跨 32/64 结果。
2. **§13.4 八项一致性**：静态一致 / 模块顺序 / CFG-DI / 类型转换 / 安全 / 浮点 / I-O / 平台。
3. **golden 向量**：从标准精确规则推导期望输出。
4. **freestanding 编译门禁**：`-ffreestanding -nostdlib` 交叉编译运行时 + 最小 UART stub。
5. **checked build**：trap 用例与静态诊断分开统计；悬垂栈水位/毒化检查只在产生 UB 的访问触发（§13.3），不放宽规范、不改变合法程序集合。

---

## 13. 分期路线

清洁建造（无迁移期）：

| 期 | 内容 | 验证 |
| ---- | ---- | ---- |
| **P0 契约** | IR 规格 / 32+64 Profile / 运行时 ABI / 诊断模型 / AArch64 target desc（设计先行） | 契约评审 |
| **P1 前端+IR+VM** | 0.0.46 标量子集前端 + 扁平 IR + VM 参考（32/64 profile） | VM 跑通子集；诊断序符合 §12.2；字长相关结果分档案 |
| **P2 AArch64 子集** | 子集后端 + 工具链 + qemu | VM↔AArch64 子集差分位级一致 |
| **P3 全量** | ptr/区/struct/控制流/fp 全量 + 全量运行时 | 全量差分 + §13.4 八项 |
| **P4 裸机** | freestanding 档案 + aarch64-elf + 32 位后端（Cortex-M/RV32）+ 链接脚本 + UART | qemu-system；freestanding 门禁；32/64 分档案 |
| **P5 多目标** | RISC-V / x86-64 / Cortex-M 按需 | 各目标差分 |

---

## 14. 模块结构

```
src/
  frontend/   lex parse module resolve analyze consteval diagnostic
  ir/         type instr block cfg region address print
  runtime/    sem_int sem_fp sem_cast sem_bitwise io sink alloc trap profile
  vm/         interp memory pointer_state
  backend/
    aarch64/  lower emit_asm frame layout regalloc(后期)
    arm32/    32 位目标（AAPCS32；目标待定）
    riscv32/  32 位目标（ILP32；目标待定）
    common/   target_desc abi toolchain_driver
  driver/     tc-vm tc-aot
```

目标无关层（frontend/ir/runtime 核/vm）与目标相关层（backend/<target>）分离；新增目标 = 新增 backend 目录，其余复用。

---

## 15. 已定设计决策

按设计预期（裸机+OS、确定性优先、清洁多目标、AArch64 先行、正确性优先于激进优化）选定：

| # | 决策 | 理由 |
| ---- | ---- | ---- |
| ① | **非 SSA 扁平 TAC IR + vreg + 显式 CFG**；SSA 延后为按需优化形态 | 源已扁平，IR 贴近源利于差分调试；目标是裸机+确定性+多目标，非激进优化；§10 契约为 as-if 保持行为，不要求 SSA |
| ② | **FP/cast/复杂内存调运行时，简单整数/控制流原生** | 最难确定性语义留在与 VM 共享的运行时核，正确性最有保障；运行时 freestanding，裸机无 libc 依赖 |
| ③ | **初期无 regalloc**（栈上局部 + 固定临时） | 正确性优先、跨目标可移植；regalloc 是优化非正确性需求；线性扫描留作后续 |
| ④ | **ELF + clang 集成汇编 + lld + qemu** | ELF 是裸机终态同格式/标准 ABI（AAPCS64/AAPCS32/ILP32），无 ABI 返工；Mach-O 为裸机死路；clang/lld 是标准平台工具 |
| ⑤ | **分层模块结构**（目标无关 core + 每目标 backend） | 加目标 = 加 backend 目录，其余复用；支持多目标+清洁架构 |
| ⑥ | **32/64 双字长正式档案**；`usize`/`isize`/`ptr`/`fnptr` 宽度 = W | 0.0.46 要求同时支持 32/64；AArch64 64 先行，32 位后端后续；`isize`/`usize` 保持独立类型，不做固定宽度别名 |

---

## 16. 风险与取舍

- **确定性语义重新验证**：fp 渲染/异常优先级从规范重新推导并验证（真实工作量；标准精确、可验证）。
- **溢出/trap 正确性**：原生指令需补检查分支，是后端正确性关键。
- **32 位后端与 ABI**：AAPCS32/ILP32、`int64`/`float64` 辅助、未对齐访问与 32 位 `n × sizeof T` 是额外工作量。
- **无 regalloc 初期性能**：可控，后续优化。
- **工具链安装**：lld + qemu 一次性。
- **体量**：全栈清洁重建，重大 effort，独立分支。
- **参考自建**：新 VM 先行作可执行参考，无既有实现依赖。

---

*语言合法性、语义、诊断码与平台契约以 [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md) 为准；本文规定实现架构与契约，描述待实现目标。*