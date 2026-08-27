# TC-AOT 详细设计说明书

> **规范基线（唯一权威）**：[TC 语言标准 0.0.41](./TC语言标准设计说明书-0.0.41.md) · [TC 编译器标准 0.0.41](./TC编译器标准设计说明书-0.0.41.md)
>
> **当前实现基线**：TC-AOT v0.0.41（`TC_AOT_VERSION`）
>
> **状态**：0.0.41 代码生成架构设计，涵盖模块系统、函数、memblock、ptr、struct 与完整 C99 代码生成。
>
> **上游契约**：[TC-VM 详细设计说明书](./TC-VM详细设计说明书-0.0.41.md) 的 typed program、完整 CFG 与共享运行时语义

---

## 目录

1. [边界与目标](#1-边界与目标)
2. [总体架构](#2-总体架构)
3. [输入契约](#3-输入契约)
4. [生成 C99 的布局](#4-生成-c99-的布局)
5. [语句代码生成](#5-语句代码生成)
6. [控制流代码生成](#6-控制流代码生成)
7. [函数代码生成](#7-函数代码生成)
8. [模块代码生成](#8-模块代码生成)
9. [RHS 与运行时 shim](#9-rhs-与运行时-shim)
10. [类型系统与值布局](#10-类型系统与值布局)
11. [指针、memblock 与 memcopy 代码生成](#11-指针memblock-与-memcopy-代码生成)
12. [`bitcast` 与数值一致性](#12-bitcast-与数值一致性)
13. [`let` 常量](#13-let-常量)
14. [I/O 与诊断](#14-io-与诊断)
15. [CLI、构建与产物](#15-cli构建与产物)
16. [差分验证](#16-差分验证)
17. [模块与接口](#17-模块与接口)
18. [实现基线与迁移](#18-实现基线与迁移)

---

## 1. 边界与目标

### 1.1 版本基线

| 维度 | 版本 | 状态 |
| ---- | ---- | ---- |
| 目标语言 | TC 0.0.41 | 规范已确定 |
| 编译器规范 | TC 编译器 0.0.41 | 13 阶段管线已确定 |
| 本文 | 0.0.41 设计 | 面向当前语言能力的实现设计 |

### 1.2 目标

- 将成功的 `TcTypedProgram` 确定性转译为可移植 C99。
- 生成程序与 VM 在 stdout、stderr 分类、退出状态、数值位模式和运行时错误时机上一致。
- 所有静态错误在生成 C 前完成；AOT 不重新发明另一套合法性规则。
- 支持多文件模块系统：生成单一 C 文件或按模块分别生成 C 文件，通过内部链接约定共享 `static var` 槽。
- 支持函数定义、`funcall`、`return`、命名实参、按值形参。
- 支持 `ptr<T>` 指针类型及全部 `ptr_*` 指令。
- 支持 `memblock<T, N>` 内存块类型及深拷贝语义。
- 支持 `struct` 结构体类型及字段赋值双层可变性。
- 结构化循环优先生成结构化 C；受限 goto 生成唯一 C 标签。
- 算术、浮点、cast、bitcast 与 I/O 复用共享语义或经差分证明等价。
- 生成代码不依赖 C 的有符号溢出、严格别名违规、未初始化读取或实现定义移位。

### 1.3 非目标

- 不输出机器码、目标文件或自定义链接器格式。
- 不把宿主 C 编译器诊断当成 TC 语言诊断。
- 不承诺生成 C 的人工可维护性优先于语义一致性。
- 不在 codegen 中接受 Analyzer 已拒绝的程序。
- 不生成多线程代码。

---

## 2. 总体架构

AOT 代码生成位于编译器标准 §1.2 的第 13 阶段，仅在全部 12 个静态分析阶段成功完成后执行：

```
.tc source files
  │
  ├─ libtc: 13 阶段确定性编译管线（阶段 1–12）
  │    ├─ 阶段 1-3: UTF-8 解码 → 词法与缩进 → 语法解析
  │    ├─ 阶段 4: 模块结构与导入解析 (4a→4b→4c→结构体表→4d)
  │    ├─ 阶段 5: 函数重名与签名
  │    ├─ 阶段 6: 名称/作用域/类型 (6a→6b→6c→6d→6e)
  │    ├─ 阶段 7-8: funcall / return 检查
  │    ├─ 阶段 9-10: let 求值 / 静态布尔判定
  │    └─ 阶段 11-12: CFG 与确定初始化 / 调用图环检查
  │
  └─ TcTypedProgram（静态合法）
       │
       ├─ 阶段 13: tc_aot_emit_c
       │    ├─ preamble / slots / memblock 存储 / static var 拓扑初始化
       │    ├─ 函数代码生成（声明/定义/调用约定）
       │    ├─ statement emission（全部 statement kind 分发）
       │    └─ runtime error guards（每个可能失败的操作）
       │
       └─ generated .c
            + tc_aot_rt.c
            + shared runtime semantics (tc_sem_int, tc_sem_fp, tc_sem_cast, etc.)
                 │
                 └─ host C99 compiler → executable
```

### 2.1 分层

| 层 | 责任 | 不负责 |
| -- | ---- | ------ |
| libtc/Analyzer | 全部静态合法性、槽位、绑定、CFG 语义 | 生成 C |
| codegen | 结构与表达式的确定性发射 | 重新判断语言是否合法 |
| AOT runtime shim | 运行时数值、I/O、诊断桥接、memblock 堆管理 | 源码解析与作用域 |
| host compiler | 编译合法 C99 | 定义 TC 语义 |

### 2.2 失败边界

- `tc_compile_file_opts` 失败：打印 TC/实现诊断，不创建可信输出。
- `tc_aot_emit_c` 失败：通常为输出 I/O、OOM 或内部未覆盖 kind；删除/忽略不完整产物。
- host C 编译失败：属于工具链失败，不映射为 TC `SyntaxError`。
- 生成程序失败：通过 `TcDiagnostic` 与 `tc_aot_abort` 报告运行时 TC 错误。

---

## 3. 输入契约

### 3.1 必须已满足的条件

codegen 入口只接受 Analyzer 成功产出的 typed program。编译器标准 §1.2 的 12 个静态阶段均已成功完成：

- `var` 均有 RHS（阶段 3 `TC_CE_VAR_MISSING_INIT` 已通过）；
- 名称、源序和块作用域均已解析（阶段 6b）；
- 每个运行时变量有固定 slot（含 `static var` 的全程序唯一槽）；
- 模块导入依赖图无环（阶段 4c `TC_CE_CIRCULAR_IMPORT` 检查通过）；
- 模块五层结构、可见性修饰符已验证（阶段 4a）；
- 导入解析全部成功，无未找到/非库/歧义/重复/名称冲突（阶段 4b）；
- 函数签名全部收集且无冲突（阶段 4d + 阶段 5）；
- 函数调用图无环（阶段 12）；
- `static var` 初始化器已验证（阶段 9），依赖拓扑序已确定（阶段 4c）；
- `static let` / `let` 已求值为精确 `TcValue`（阶段 9）；
- RHS 类型、运算模式和格式符已检查（阶段 6d–6e）；
- funcall 目标/位置/实参、return 形式/类型已检查（阶段 7–8）；
- while/goto 范式隔离已检查（阶段 6a）；
- break/continue 已绑定到最内层 while；
- goto 目标与块路径已解析（阶段 6c）；
- 完整多域 CFG 确定初始化已通过（阶段 11：顶层 + 各函数独立）；
- 静态布尔三态与逻辑读边已判定（阶段 10；[语言标准 §5.2.2]、[编译器标准 §5.2.4]）。

### 3.2 需要持久化的信息

codegen 至少需要：

- 语句树与稳定 `stmt_index`；
- 变量 binding → slot 映射（含 `static var` 的模块级唯一槽）；
- 每个语句的词法 scope/block path；
- goto → label 的已解析目标；
- break/continue → loop id；
- RHS 的解析类型与模式；
- `let` 的编译期位模式；
- 函数签名（形参列表、返回类型、`public`/`private`）；
- 结构体定义（字段列表、类型、可变性、padding、总宽度）；
- memblock 绑定的 `N` 值；
- 源文件名和行号。

---

## 4. 生成 C99 的布局

### 4.1 目标骨架

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "tc_aot_rt.h"

/* ── 槽位布局 ── */
#define SLOT_COUNT <total_var_slots>
#define STATIC_SLOT_COUNT <total_static_var_slots>
#define MEMBLOCK_SLOT_COUNT <total_memblock_bindings>
#define STRUCT_SLOT_COUNT <total_struct_bindings>

static uint64_t slots[SLOT_COUNT];
static uint64_t static_slots[STATIC_SLOT_COUNT];
static void *memblock_storage[MEMBLOCK_SLOT_COUNT];
static uint8_t struct_storage[STRUCT_TOTAL_BYTES];

/* ── memblock 运行时管理 ── */
static uint8_t *memblock_heap[NUM_MEMBLOCK_ALLOCS];
static size_t memblock_heap_sizes[NUM_MEMBLOCK_ALLOCS];

/* ── 前向声明 ── */
static void tc_func_<id>(TcDiagnostic *diag);

/* ── static var 初始化 ── */
static void tc_init_static_vars(TcDiagnostic *diag) {
    /* 按依赖拓扑序生成的初始化代码 */
    /* 每个 static var 的初始化 RHS，带错误 guard */
}

int main(void) {
    TcDiagnostic diag;
    tc_aot_diag_init(&diag);
    tc_aot_init_slots(slots, SLOT_COUNT);
    tc_aot_init_slots(static_slots, STATIC_SLOT_COUNT);
    tc_init_static_vars(&diag);
    if (diag.kind != TC_DIAG_OK) {
        tc_aot_abort(&diag, __LINE__);
    }

    /* generated top-level statements */

    tc_diagnostic_clear(&diag);
    return 0;
}
```

### 4.2 固定槽位

所有词法 `var` / `static var` 的 slot 在程序开始前固定。TC `var` 语句生成的是一次 RHS 求值和槽写入：

```c
if (tc_aot_arith(..., &slots[X], slots[A], slots[B], &diag, line) != 0) {
    tc_aot_abort(&diag, line);
}
```

循环下一迭代或向后 goto 再次到达同一 `var` 时，覆盖同一 slot。

### 4.3 memblock 槽位

memblock 槽位存储指向堆上存储区的指针：

```c
/* memblock<int32, 10> 的存储布局：
   offset 0:  uint64_t count = 10   (长度头部，平台指针宽度)
   offset 8:  int32_t data[10]      (元素数据)
   total = 8 + 10*4 = 48 bytes
*/

/* 分配 memblock 存储 */
void *tc_aot_memblock_alloc(size_t total_bytes, TcDiagnostic *diag);
/* 深拷贝 memblock */
void tc_aot_memblock_copy(void *dst, const void *src, size_t total_bytes);
/* 释放 memblock 存储 */
void tc_aot_memblock_free(void *ptr);
```

### 4.4 结构体槽位

结构体槽位按字段布局存储在连续字节数组中：

```c
/* struct Point { let x: int32; var y: float64; @padding(4) }
   布局: x(4B) + y(8B) + padding(4B) = 16B total
   struct_storage[offset..offset+16]
*/
```

### 4.5 名称与确定性

生成的内部名称使用稳定 id：

- label：`tc_label_<stmt_index>`；
- 函数：`tc_func_<func_id>`；
- loop：`tc_loop_<stmt_index>`；
- 条件临时值：`tc_cond_<stmt_index>`；
- 返回值临时值：`tc_ret_<func_id>`；
- memblock 槽：`memblock_storage[mb_slot]`。

相同 typed program 必须生成语义等价且顺序稳定的 C 文本。

---

## 5. 语句代码生成

### 5.1 映射表

| TC 语句 | 目标 C99 |
| ------- | -------- |
| `var` | RHS → 固定 slot |
| `static var` | RHS → static_slots[unique_id]（在 `tc_init_static_vars` 中） |
| `static let` | 不生成运行时语句；使用编译期位模式 |
| `let` | 不生成运行时语句；使用编译期位模式 |
| 赋值 | RHS → 已有 slot |
| 字段赋值 `a.b = rhs` | 按字段偏移写入 struct_storage |
| `write`/`writeln` | `tc_aot_write` |
| `read` | `tc_aot_read` + abort guard |
| `if` | 条件 RHS + 原生 C `if/else` |
| `while` | 原生无限循环 + 每次迭代显式条件 |
| `break` | 原生 C `break` |
| `continue` | 原生 C `continue` |
| `label` | `tc_label_<stmt_index>: ;` |
| `goto` | `goto tc_label_<target_stmt_index>;` |
| `funcall` (void) | `tc_func_<id>(&diag); if (diag.kind) tc_aot_abort(...);` |
| `var x = funcall(...)` | `tc_func_<id>(&diag); if (diag.kind) tc_aot_abort(...); slots[X] = tc_ret_<id>;` |
| `x = funcall(...)` | `tc_func_<id>(&diag); if (diag.kind) tc_aot_abort(...); slots[X] = tc_ret_<id>;` |
| `return` | `return;` 或 `*retval = value; return;` |
| `func` 定义 | 见 §7 |
| `struct` 定义 | 不生成运行时语句（仅记录到类型元数据） |
| `import` | 不生成运行时语句（仅在模块解析阶段处理） |
| `memblock_store` | `tc_aot_memblock_store` + abort guard |
| `memblock_copy` | `tc_aot_memblock_copy` + abort guard |
| `ptr_store` | `tc_aot_ptr_store` + abort guard |
| `memcopy_unsafe` | `tc_aot_memcopy_unsafe` + abort guard |

### 5.2 运行时错误 guard

所有可能失败的 shim 统一生成：

```c
if (tc_aot_<op>(..., &diag, source_line) != 0) {
    tc_aot_abort(&diag, source_line);
}
```

`tc_aot_abort` 打印与 VM 同类的诊断并终止生成程序。codegen 不忽略返回码，也不把失败结果继续写入 slot。

### 5.3 条件 RHS

`if` 和 `while` 条件是普通 bool RHS，可能包含运行时操作。必须先完整求值到独立 `uint64_t` 临时值，再按 TC bool 语义分支：

```c
uint64_t tc_cond_12;
/* emit condition into tc_cond_12 */
if (tc_cond_12 != 0) {
    /* then */
} else {
    /* else */
}
```

---

## 6. 控制流代码生成

### 6.1 `if`

```c
uint64_t tc_cond_12;
/* emit condition */
if (tc_cond_12 != 0) {
    /* then statements */
} else {
    /* else statements (if present) */
}
```

### 6.2 `while`

```c
for (;;) {
    uint64_t tc_cond_20;
    /* emit TC condition; may abort */
    if (tc_cond_20 == 0) {
        break;
    }
    /* body; native break/continue target this loop */
}
```

Analyzer 已禁止 while 体内的 goto 和 label，且结构化语句树保留嵌套关系。最内层 TC while 与最内层生成 C 循环一一对应，原生 `break`/`continue` 不会被非结构化跳转破坏。

### 6.3 `goto`/`label`

goto 只在 `#lib` 函数体内且不在 `while` 内时出现。Analyzer 提供解析后的目标；codegen 直接发射唯一 C 标签：

```c
tc_label_42: ;
...
goto tc_label_42;
```

对向外 goto，当前值均为标量/指针，没有 C 自动对象需要析构。memblock 槽位保持在 `memblock_storage[]` 中，不随 C 作用域离开而失效。

### 6.4 结构保持与显式标签策略

若 host 编译器或未来资源清理要求不适合原生 C `while`，可用唯一内部标签实现同一 CFG。两种策略必须通过相同差分用例。

---

## 7. 函数代码生成

### 7.1 函数声明与定义

```c
/* 前向声明 */
static void tc_func_<func_id>(TcDiagnostic *diag);

/* 定义 */
static void tc_func_<func_id>(TcDiagnostic *diag) {
    uint64_t params[PARAM_COUNT];       /* 形参槽位 */
    uint64_t locals[LOCAL_COUNT];       /* 局部 var 槽位 */
    uint64_t retval;                    /* 返回值槽位（若非 void） */

    /* generated function body */
}
```

### 7.2 调用约定

**函数调用侧**：

```c
/* void 独立调用 */
tc_func_<func_id>(&diag);
if (diag.kind != TC_DIAG_OK) tc_aot_abort(&diag, line);

/* 非 void 调用：var x = func(...) */
tc_func_<func_id>(&diag);
if (diag.kind != TC_DIAG_OK) tc_aot_abort(&diag, line);
slots[X] = tc_ret_<func_id>;

/* 非 void 调用：x = func(...) */
tc_func_<func_id>(&diag);
if (diag.kind != TC_DIAG_OK) tc_aot_abort(&diag, line);
slots[X] = tc_ret_<func_id>;
```

**形参按值传递**：调用前将实参槽位值写入函数的 params 数组。memblock 参数执行深层拷贝；struct 参数执行整块字节复制；ptr 参数复制指针值。

### 7.3 函数体代码生成

函数体内语句按源序生成。`var` 定义先求 RHS 再写入 locals 槽。

**`return` 生成**：

```c
/* void return */
diag->kind = TC_RETURN_SIGNAL;  /* 或生成专用返回标记 */
return;

/* 有值 return */
*retval_ptr = <operand_value>;
diag->kind = TC_RETURN_SIGNAL;
return;
```

函数末尾隐式 `return` 仅在静态分析已证明不可达时省略生成。

### 7.4 `funcall` 在函数体内的嵌套

由于调用图无环，函数内 `funcall` 可以直接生成对另一 `tc_func_<id>(...)` 的调用。调用栈深度在编译期确定。

---

## 8. 模块代码生成

### 8.1 多文件策略

推荐将所有可达模块合并为单一 C 文件。策略：

1. `#program` 入口模块和所有导入的 `#lib` 模块的全部 `static var` 槽位、`static let` 值、函数定义合并到一个 C 文件。
2. 每个模块的 `static var` 分配全程序唯一槽位。
3. `static var` 初始化按依赖拓扑序生成（先初始化被依赖模块的 `static var`）。
4. 所有函数定义生成到同一 C 文件中（均为 `static` 函数）。

### 8.2 `static var` 初始化顺序

```c
static void tc_init_static_vars(TcDiagnostic *diag) {
    /* 按依赖拓扑序：先初始化无外部依赖的模块 */
    /* 假设 import 顺序：A imports B, B imports C */
    /* 初始化顺序：C → B → A */

    /* 模块 C 的 static var 初始化 */
    /* static_slots[C.x] = <init_rhs> */
    if (tc_aot_eval_rhs(..., &static_slots[C.x], &diag, line) != 0) { return; }

    /* 模块 B 的 static var 初始化（可能引用 C.x） */
    /* ... */

    /* 模块 A 的 static var 初始化 */
    /* ... */
}
```

### 8.3 公开成员引用

- `Self.<成员>` → 直接引用本模块的 static_slots 槽位。
- `<模块>.<成员>` → 引用目标模块的 static_slots 槽位。
- 公开 `static let` → 内联编译期位模式。

---

## 9. RHS 与运行时 shim

### 9.1 分发原则

每个 `TcRhsKind` 必须在 codegen 中有显式分支。新增 RHS kind 后必须同步：

- Parser/Analyzer 目标 kind；
- `tc_aot_emit_rhs`；
- runtime helper（若需要）；
- VM Executor；
- const evaluator；
- 覆盖检查和单元/差分测试。

未知 kind 是内部错误，不能静默生成 0。

### 9.2 shim 职责

| shim 类别 | 目标委托 |
| --------- | -------- |
| integer arithmetic/unary | `tc_sem_int` |
| compare/logic | 共享 semantics |
| bitwise/shift | `tc_sem_bitwise` |
| float arithmetic/unary/compare | `tc_sem_fp` |
| strict cast/truncate | 共享 cast 语义 |
| bitcast | 位宽验证已静态完成；运行时只复制规范化位模式 |
| ptr_load / ptr_store | `tc_aot_ptr_shim` |
| ptr_address / ptr_add / ptr_sub | `tc_aot_ptr_shim` |
| ptr_eq / ptr_ne / ptr_lt/le/gt/ge | `tc_aot_ptr_shim` |
| ptr_size | 编译期内联，不生成运行时调用 |
| memblock_load / memblock_store | `tc_aot_memblock_shim` |
| memblock_copy / memcopy_unsafe | `tc_aot_memblock_shim` |
| struct 构造器 / 字段读取 | `tc_aot_struct_shim` |
| I/O | `tc_io` |

### 9.3 求值顺序

TC 每条语句至多一个非嵌套调用。codegen 仍须固定：左 operand 读取 → 右 operand 读取 → shim 调用 → 成功写回。逻辑短路要按 TC 规则避免求值不可达右 operand。

---

## 10. 类型系统与值布局

宽度语义权威定义见 [语言标准 §3.8.2]、[语言标准 §3.9.3]、[语言标准 §3.10.5]；实现查阅用常数表见 [编译器标准 §3.0.1]。AOT 的 `tc_width_table`、`ptr_size` 内联与 struct/memblock 布局计算必须与该表一致。

### 10.1 标量类型宽度表

```c
static const size_t tc_width_table[] = {
    [TC_INT8]   = 8,  [TC_UINT8] = 8,  [TC_BOOL] = 8,
    [TC_INT16]  = 16, [TC_UINT16] = 16,
    [TC_INT32]  = 32, [TC_UINT32] = 32, [TC_FLOAT32] = 32,
    [TC_INT64]  = 64, [TC_UINT64] = 64, [TC_FLOAT64] = 64,
    [TC_ISIZE]  = TARGET_PTR_WIDTH, [TC_USIZE] = TARGET_PTR_WIDTH,
    [TC_PTR]    = TARGET_PTR_WIDTH,
};
```

### 10.2 memblock 布局生成

`memblock<T, N>` 的类型等价仅由 `T` 决定；`N` 不参与 `tc_type_equals`，但赋值/传参须比较两侧声明的 `N`（[语言标准 §3.8.1]、[编译器标准 §3.1]）。布局宽度按 [编译器标准 §3.0.1]：`sizeof_bits(usize) + N × sizeof_bits(T)`。

对 `memblock<T, N>` 声明，生成：

```c
/* memblock mb1 : memblock<int32, 10>
   total_bytes = sizeof(usize) + 10 * sizeof(int32) = 8 + 40 = 48
*/
static const size_t mb1_total = 48;
static const size_t mb1_count = 10;

/* 运行时分配 */
memblock_storage[MB_SLOT_X] = tc_aot_memblock_alloc(mb1_total, &diag);
if (diag.kind != TC_DIAG_OK) tc_aot_abort(&diag, line);
/* 写入长度头部 */
*(uint64_t*)memblock_storage[MB_SLOT_X] = mb1_count;

/* memblock_store 写入元素（index 为变量时） */
if (tc_aot_memblock_store(memblock_storage[MB_SLOT_X], mb1_count,
                          sizeof(int32_t), (uint64_t)index,
                          (uint64_t)value, &diag, line) != 0) {
    tc_aot_abort(&diag, line);
}
```

### 10.3 结构体布局生成

字段类型检查与 [语言标准 §3.9.1] 一致：值位置（字段类型本身或 `memblock` 元素类型）禁止本模块自引用与前向引用，允许已 import 的 `public struct`（须写 `<模块名>.<结构体名>`）；指针所指位置允许 `ptr<正在定义的本结构体>`（指针自引用）。`ptr<S>` 字段宽度恒为指针宽度，不阻塞 `sizeof(S)` 累加。未决结构体名经 `TcType.pending_name`（裸名或 `Mod.Name`）在注册后按当前程序的 import 列表解析为 `struct_id`（与 VM 共用 Analyzer；表按 `(module_name, name)` 定界；构造器名规范化为 `"<模块>.<名>"` 后由 `tc_struct_table_find` 查找）。

**字段存储模型**（与 VM 一致，§3.9.3 内联值语义）：

- `memblock<U, N>` 字段：按 `sizeof(usize) + N × sizeof(U)` 内联存储完整数据（头部+元素位串）；构造器/字段赋值用 `tc_aot_struct_memcpy_field` 写入内容，字段读取用 `tc_aot_struct_extract` 抽出独立堆块；整块赋值/传参复制内联数据即深拷贝。
- `ptr<T>` 字段：存储指针位模式（宽度 = 平台指针宽）。
- 嵌套 `struct` 字段：内联字节序列，读取时 `tc_aot_struct_extract` 抽出。

对结构体类型定义，codegen 编译期计算字段字节偏移表：

```c
/* struct Foo { let x: int32; var y: float64; @padding(4) }
   sizeof(Foo) = 4 + 8 + 4 = 16
   offsetof(x) = 0
   offsetof(y) = 4
   struct_storage 槽位存储 Foo 值的 16 字节序列
*/

/* 字段读取 a.y */
/* slots[DST] = *(uint64_t*)(struct_storage + foo_offset + offsetof(y)) */

/* 字段赋值 a.y = rhs */
/* *(uint64_t*)(struct_storage + foo_offset + offsetof(y)) = rhs_value */
```

---

## 11. 指针、memblock 与 memcopy 代码生成

### 11.1 指针操作 shim

```c
/* ptr_load(T, ptr) */
int tc_aot_ptr_load(void **target, void *ptr, size_t element_size,
                    TcDiagnostic *diag, int line);
/* ptr_store(T, ptr, value) */
int tc_aot_ptr_store(void *ptr, const void *value, size_t element_size,
                     TcDiagnostic *diag, int line);
/* ptr_address(T, ident) → 返回绑定槽的地址（&slots[X] 或 &static_slots[Y]） */
void *tc_aot_ptr_address(void *slot_addr);
/* ptr_add(T, ptr, offset) */
void *tc_aot_ptr_add(void *ptr, size_t element_size, uint64_t offset,
                     TcDiagnostic *diag, int line);
/* ptr_sub(T, ptr, offset) */
void *tc_aot_ptr_sub(void *ptr, size_t element_size, uint64_t offset,
                     TcDiagnostic *diag, int line);
/* ptr_cmp : ptr_eq/ne/lt/le/gt/ge */
bool tc_aot_ptr_eq(const void *p1, const void *p2, int op_kind,
                   TcDiagnostic *diag, int line);
```

### 11.2 memblock 操作 shim

```c
/* memblock_load(T, mb, idx) */
int tc_aot_memblock_load(void *mb, size_t count, size_t element_size,
                         uint64_t idx, uint64_t *out, TcDiagnostic *diag, int line);
/* memblock_store(T, mb, idx, value) */
int tc_aot_memblock_store(void *mb, size_t count, size_t element_size,
                          uint64_t idx, uint64_t value, TcDiagnostic *diag, int line);
/* memblock_copy(T, dst, d_idx, src, s_idx, len) */
int tc_aot_memblock_copy(void *dst, size_t dst_count, void *src, size_t src_count,
                         size_t element_size, uint64_t d_idx, uint64_t s_idx,
                         uint64_t len, TcDiagnostic *diag, int line);
```

### 11.3 memcopy_unsafe shim

```c
int tc_aot_memcopy_unsafe(void *dst, void *src, size_t element_size,
                          uint64_t d_idx, uint64_t s_idx, uint64_t len,
                          TcDiagnostic *diag, int line);
```

---

## 12. `bitcast` 与数值一致性

### 12.1 位模式策略

内部 slot 以 `uint64_t` 保存位模式。若源和目标都是规范化的等位宽 slot 表示，bitcast 可以直接复制并按位宽掩码：

```c
slots[DST] = slots[SRC] & tc_width_mask(TARGET_TYPE);
```

若 helper 需要在 `float`/`double` 宿主对象与整数之间转换，必须使用 `memcpy`：

```c
uint32_t bits;
float value;
memcpy(&value, &bits, sizeof(value));
```

禁止通过不兼容指针解引用或依赖 union type-punning 扩展。

### 12.2 严格 cast 与 truncate

均委托共享 `tc_sem_cast`。

### 12.3 浮点一致性

- 每个 float32 操作在该步舍入到 float32；float64 同理。
- strict 按标准优先级报告除零、无效、上溢、下溢。
- ieee 产生标准结果。
- 生成 C 不依赖宿主开启 fast-math；构建参数不得破坏 NaN、Infinity 或舍入契约。

---

## 13. `let` 常量

### 13.1 输入状态

Analyzer/const evaluator 已把合法 `let` 求为声明类型的精确 `TcValue`。AOT 不重新使用宿主 C 常量表达式计算。

### 13.2 发射

- `let` 定义本身不生成运行时 slot 或赋值。
- 引用处直接发射已求得的十六进制位模式。
- float32/float64 也发射位模式，不用十进制文本让 host 编译器重新舍入。
- `static let` 同理，在所有引用处内联。
- `ptr_size` 和 `.count` 也是编译期常量，直接发射数学值。

---

## 14. I/O 与诊断

### 14.1 I/O

`tc_aot_write`/`tc_aot_read` 委托共享 `tc_io`：

- 13 种格式符；
- 整数符号和进制；
- bool 文本；
- float32/float64 格式；
- 输入非法、范围、EOF；
- stdout/stderr 写入失败。

### 14.2 运行时诊断

生成程序持有单个 `TcDiagnostic`。shim 失败设置 kind、行号和消息，`tc_aot_abort` 打印并以非零状态终止。打印名来自共享 `tc_error_kind_name()`，确保 VM/AOT 一致。

### 14.3 静态诊断

AOT `--check` 和普通转译都经 libtc 完成全部静态阶段。覆盖编译器标准 §11.4 全部错误码。

---

## 15. CLI、构建与产物

### 15.1 CLI

```text
tc-aot [options] <file.tc>
  -o, --output FILE   输出 C 文件路径
  -c, --check         仅静态检查，不生成 C
  -r, --run           编译并运行生成 C
  -h, --help          显示帮助
  -V, --version       显示版本
```

### 15.2 C99 构建

`--run` 调用 host `cc -std=c99 -Wall -Wextra -Werror -pedantic` 并链接 AOT runtime 与共享 runtime 模块：

- 不依赖 GNU C 扩展；
- fenv 能力有明确配置与回退；
- 不启用破坏浮点语义的优化选项；
- 临时/输出路径安全引用；
- 生成或编译失败不执行陈旧二进制。

### 15.3 产物

- 默认 `.tc` → `.c`；
- `-o` 指定 C 输出；
- `--check` 不发射 C；
- `--run` 编译并运行生成 C。

---

## 16. 差分验证

### 16.1 原则

AOT 的核心正确性证据是同一源文件经 VM 与 AOT 产生相同可观察结果。比较至少包括：

- stdout 字节；
- stderr 的 TC 错误种类和关键消息；
- 退出成功/失败；
- 对专门用例导出的数值位模式；
- `--check` 的接受/拒绝结果。

### 16.2 0.0.41 测试矩阵

| 类别 | 用例 |
| ---- | ---- |
| 模块系统 | 多文件导入、依赖拓扑、循环导入拒绝、公开/私有访问 |
| 函数 | 签名、funcall、return、void 返回、无环调用图 |
| 类型系统 | memblock N 比较、ptr 同型、struct 字段、isize/usize |
| structured loop | 零次、一次、多次、嵌套 while |
| loop control | 最内层 break/continue、嵌套 if 中控制 |
| paradigm isolation | while 内 goto/label 全部静态拒绝 |
| 函数内 goto/label | 向外跳转、跨控制流拒绝 |
| definite init | 条件、回边、continue、break、goto 会合、多域 CFG |
| fixed slots | 每迭代 var 重初始化、后向 goto 重入 |
| memblock | 分配、读写、区间拷贝、深拷贝传参、越界错误 |
| ptr | 取地址、读写、算术、等值/序比较、空指针分类、等宽 `cast(ptr<T>, …)`（含 `cast(ptr<T>, nullptr)`）与 `bitcast` 的 `ptr`↔整数往返（位模式复制，不经数值 shim） |
| struct | 构造器、字段读写、双层可变性、整块复制 |
| memcopy_unsafe | 空指针、负长度、memmove 语义 |
| bitcast | 等宽往返、NaN payload、-0.0、最高位；含 `ptr` 路径 |
| cast | 全严格可表示性、整数 truncate；指针目标仅复制位模式 |
| float | strict/ieee、每步精度、异常优先级 |
| let | 编译期与 runtime 位模式一致 |
| static var/let | 拓扑初始化、跨模块共享 |
| diagnostics | 目标错误 kind、行号、打印名、新诊断覆盖 |

### 16.4 提交门槛

- 新 statement/RHS kind 的 codegen 分发覆盖；
- `check_rhs_coverage.py` 通过；
- VM/AOT/let 数值一致性通过；
- 生成 C 以 C99 严格警告编译；
- 全量测试基线不回退。

---

## 17. 模块与接口

### 17.1 当前/目标文件

| 文件 | 责任 |
| ---- | ---- |
| `src/aot/main.c` | CLI、文件输出、host 编译/运行、版本 |
| `src/aot/tc_aot_codegen.c/h` | typed program → C99 |
| `src/aot/tc_aot_rt.c/h` | 生成程序的 shim（含 memblock 管理、指针操作） |
| `src/vm/runtime/tc_sem_int.c` | 共享整数语义 |
| `src/vm/runtime/tc_sem_fp.c` | 共享浮点语义 |
| `src/vm/runtime/tc_sem_bitwise.c` | 共享位运算语义 |
| `src/vm/runtime/tc_sem_cast.c` | 共享转换语义 |
| `src/vm/runtime/tc_io.c` | 共享 I/O |

### 17.2 公共接口

```c
int tc_aot_emit_c(FILE *out,
                  const TcTypedProgram *program,
                  const char *source_name,
                  bool multi_module);
```

新增 `multi_module` 标志指示是否将所有可达模块合并为单一 C 文件。

### 17.3 分发完整性

每个 statement kind 和 RHS kind 都要在 VM、AOT、free、Analyzer、const-eval（适用时）出现明确处理。AOT 不允许通过 default 分支把新 kind 当作普通赋值。

---

## 18. 实现基线与迁移

### 18.1 v0.0.31 → v0.0.41 关键迁移

| 类别 | v0.0.31 | v0.0.41 |
| ---- | ------- | ------- |
| 槽位模型 | 仅 `uint64_t slots[]` | 增加 `static_slots[]`、`memblock_storage[]` 指针数组、`struct_storage[]` 字节数组 |
| 值传递 | 全部标量按值 | memblock/struct 增加深拷贝语义 |
| 函数 | 无 | 函数定义/调用代码生成、调用帧管理 |
| 模块 | 单文件 | 多文件合并、static var 拓扑初始化 |
| RHS | ~15 种 | 增加 ptr_*、memblock_*、struct 构造器/字段读取 等 |
| shim | 算术/浮点/转换 | 增加指针、memblock、memcopy_unsafe shim |

### 18.2 预计迁移顺序

1. 共享 types/IR 与错误枚举更新；
2. 槽位模型扩展（mem_block/struct 存储）；
3. 函数声明/定义/调用代码生成；
4. 模块多文件合并与 static var 拓扑初始化；
5. memblock 类型代码生成与 shim；
6. 指针操作代码生成与 shim；
7. 结构体代码生成；
8. 新增 RHS kind 的 codegen 分发；
9. 扩展 runtime shim 与错误打印名；
10. 差分测试扩展；
11. 全量门禁通过后升版。

---

*语言合法性与可观察语义以 [TC 语言标准 0.0.41](./TC语言标准设计说明书-0.0.41.md) 与 [TC 编译器标准 0.0.41](./TC编译器标准设计说明书-0.0.41.md) 为准。*
