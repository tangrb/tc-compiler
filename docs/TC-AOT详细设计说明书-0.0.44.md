# TC-AOT 详细设计说明书

> **规范基线（唯一权威）**：[TC 语言标准 0.0.44](./TC语言标准设计说明书-0.0.44.md) · [TC 编译器标准 0.0.44](./TC编译器标准设计说明书-0.0.44.md)
>
> **当前实现基线**：TC-AOT v0.0.43（`TC_AOT_VERSION` ⇐ `TC_VERSION_CORE`，见 `src/vm/runtime/tc_version.h`）
>
> **状态**：0.0.44 代码生成架构设计（语言规范 0.0.44 同步版），涵盖模块系统、函数、memblock、ptr、struct 与完整 C99 代码生成。
>
> **上游契约**：[TC-VM 详细设计说明书](./TC-VM详细设计说明书-0.0.44.md) 的 typed program、完整 CFG 与共享运行时语义

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
18. [已知可移植性债务](#18-已知可移植性债务)

---

## 1. 边界与目标

### 1.1 版本基线

| 维度 | 版本 | 状态 |
| ---- | ---- | ---- |
| 目标语言 | TC **0.0.44** | 规范已确定 |
| 编译器规范 | TC 编译器 **0.0.44** | 13 阶段管线已确定（含诊断类阶段 LT/SYN/SEM/CT 的报告顺序） |
| 本文 | **0.0.44 设计** | 面向当前语言能力的实现设计 |

### 1.2 目标

- 将成功的 `TcTypedProgram` 确定性转译为可移植 C99。
- 生成程序与 VM 在 [语言标准 §1.3] 的「可观察行为」上一致：是否通过静态检查；**首个**规范诊断及其错误码（选取规则见 [语言标准 §11]「诊断顺序」的四步规则）；按源序成功提交到 TC 抽象标准输出的字节；正常结束或**首个**运行时错误及错误码。进程退出状态、stderr 消息文本、数值位模式的宿主表示不是可观察行为，只作辅助诊断信息。
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

### 1.4 0.0.44 同步状态（实现侧标注）

本文按 0.0.44 规范口径书写（**文档先行**）。AOT 不自行实现前端，静态项由**共享 Analyzer**（见 [TC-VM 详细设计说明书 §1.5](./TC-VM详细设计说明书-0.0.44.md)）保证；下表列出与 `src/aot/` 的落地差异状态。

| 条目 | 影响面 | 状态 |
| ---- | ------ | ---- |
| `var` 缺初始化器（第 3 阶段）、结构体名三类冲突、前导零与实参形态码 | 输入契约（§3.1 要求「已通过阶段 3」） | **已同步**（共享 Analyzer；AOT 只消费其结果） |
| `read` 标准输入读取失败报 `TC_RE_IO` | 共享 `tc_io`（§14.1） | **已同步** |
| 浮点语义：IEEE 754-2019、roundTiesToEven、禁止 FMA／FTZ／DAZ、`mod` 商向零截断 | 共享纯语义 core（§12.3） | **已同步**（VM/AOT 共用 core） |
| 指针 `cast` **不附加等宽条件** | 共享 Analyzer | **已同步**：语料 `ptr_cast_remark.tc`（VM+AOT 差分），旧等宽语料删除 |
| `bitcast(ptr ↔ 浮点)` 改为拒绝 | 共享 Analyzer（§12 生成路径不受影响） | **已同步**：语料 `bitcast_ptr_float*`（`--check` 三路）；`bitcast(T, nullptr)` 另经 A-4 裁决静态拒绝，见 `bitcast_nullptr_source*` |
| `ptr_size` 只可整条充当 `const_rhs` | 共享 Analyzer（§9.2 内联、§13.2 发射） | **已核实一致** |
| `const_rhs` 中嵌套调用报 `TC_CE_SYNTAX` | 共享 Analyzer | **已同步** |
| CT 类诊断须晚于全部 SEM 类诊断报告 | 共享 Analyzer 阶段顺序 | **已同步** |
| `ptr_load` 结果类型为 `bool` 时规范化；所有 `bool` 写路径归一化 | 共享 core 与 §4 槽表示 | **已同步**：AOT 原已规范化，VM 侧缺口已补（`tc_ptr_exec.c`），两端语料 `ptr_load_bool_normalize.tc` 一致 |
| `static let` 不得引用 `static var`／命名 N 与 `count:` 前置解析 | 共享 Analyzer | **已同步**：语料 `static_let_ref_var_*.tc` 与正例 `static_let_rule_ok.tc` 均入 AOT 差分 |
| 函数体 `Self.` 强制访问与 `Self.<名>` 赋值目标 | 共享 Analyzer + §4 槽表示（[语言标准 §4.3]） | **已同步**：字段赋值经 `TcFieldAssign.base_binding` 取槽（原按名解析限定名失败报 `unresolved struct base`）；语料 `self_bare_*.tc`、`self_access_ok.tc` |
| 静态布尔原子集合 / 条件 RHS 形态 / 限定标识符作 `operand` | 共享 Analyzer | **已同步**：语料 `static_bool_cond.tc`／`cond_ptr_compare.tc`／`cond_readonly_field.tc`／`self_qual_operand.tc` 均入 AOT 差分 |
| `ptr_*` 调用型 RHS 不属于 `operand`、不得嵌套（嵌套 → `TC_CE_SYNTAX`） | 共享 Analyzer（A-1） | **已同步**：AOT `--check` 与 VM 同码；语料 `ptr_{address,add,sub,lt}_nested_operand.tc` |
| `TC_CE_LITERAL_OUT_OF_RANGE` 覆盖 LT 与 SEM 两阶段 | 共享 Analyzer（A-2） | **已核实一致**：LT/SEM 两路径同码（`invalid_hex_overflow`／`literal_range` 等） |
| `memcopy_unsafe` 编译期可确定负 `length`／负下标 → 静态拒绝；不可确定者 → 运行时 `TC_RE_*` | 共享 Analyzer ＋ §11.3 shim（A-3） | **已同步**：shim 只处理不可确定的负值；语料 `memcopy_unsafe_neg_*`（静态四条 / 运行时三条） |
| `nullptr` 不参与 `bitcast`（静态拒绝） | 共享 Analyzer（A-4） | **已同步**：语料 `bitcast_nullptr_source{,_int}.tc`、`bitcast_nullptr_float.tc` |
| `else`/`end` 不对齐（过深或过浅）一律 `TC_CE_INDENT_ELSE_END` | 共享 Analyzer（B-41） | **已同步**：语料 `indent_end_deeper`／`indent_else_deeper` |
| 条件位置 RHS 专用码优先于 `TC_CE_CONDITION_TYPE` | 共享 Analyzer（B-61） | **已核实一致**：语料 `if_cond_literal_direct`／`cond_var_not_bool` |
| 顶层行缩进必须为 0，否则 `TC_CE_INDENT_INSUFFICIENT` | 共享 Analyzer（B-63） | **已同步**：语料 `toplevel_indent_*.tc`（AOT `--check` 同码） |
| `<模块名>.<成员>` 可作 RHS `operand`（含 struct 整体读取） | 共享 Analyzer ＋ 表达式发射（既有-1） | **已同步**：常量基址的 struct／memblock 内联字节后经 `tc_aot_struct_extract` 深拷贝；语料 `import_member_operand.tc`／`import_member_struct.tc` |
| `ptr_address(T, Self.<名>／<模块名>.<名>)` 合法 | 共享 Analyzer（既有-2） | **已同步**：语料 `import_addr_self.tc`／`import_addr_qual.tc`（AOT 差分） |
| `ptr_store`／`memcopy_unsafe` 只读判据取**所指外层绑定**；常量空指针归运行期 | 共享 Analyzer ＋ §11 运行时（既有-4） | **已同步**：零声明槽位程序的 `ptr_load`／`ptr_store`／`memcopy_unsafe` 实参由 `tc_aot_slots_arg` 发 `NULL, 0`（不发射 `slots[]`，运行期按容量 0 判空指针）；语料 `ptr_store_null_let{,_copy}`／`memcopy_unsafe_null_let`（`run_runtime_fail`）、`ptr_store_readonly_copy`（`run_check_fail`） |
| 经导入限定解析到 `private` 的 `static let`／`static var` → `TC_CE_PRIVATE_MEMBER_ACCESS` | 共享 Analyzer（[语言标准 §4.4]） | **已同步**：AOT `--check` 与 VM 同码；语料 `member_private_{read,field,addr,memblock,read_target}.tc`（AOT `run_check_fail`） |

上表只登记 0.0.44 规范口径的同步差异，**不代表实现侧零未决**。审计报告 §5 复核的 9 项既有未关闭差异（`既有-1`～`既有-9`）**已全部闭合**：`既有-1` 导入限定名成员作 RHS 操作数（`87b988f`）、`既有-2` `Self.<名>`／导入限定名取址（`0093b58`）、`既有-4` 只读判据改为所指绑定、常量空指针归运行期（`0851a37`），其余 6 项分别由 B-6／B-8／B-9／B-17／B-20／A-3 闭合（映射与证据见符合性整改进度台账 §5）。§5 复核后新发现的既有缺陷中，**限定名成员可见性未校验**（`<模块>.<private static let/var>` 跨模块可读）已按 [语言标准 §4.4] 修复为 `TC_CE_PRIVATE_MEMBER_ACCESS`（共享 Analyzer，AOT `--check` 同码）；其余 3 项（`Self.` 限定名整体读取 struct 常量、AOT 常量 `memblock` 的 `.count` 作赋值 RHS、限定名整绑定赋值目标）仍为观察项、尚未修复。这些项**不改变本文的 codegen 口径**：AOT 不得为迁就现状放宽任何生成规则，凡本文要求静态拒绝或运行时报 `TC_RE_*` 的形态，一律按本文发射，不得以「当前实现接受」为由生成等价路径。

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
/* Auto-generated by tc-aot. Do not edit. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tc_aot_rt.h"

/* ── 槽位布局：唯一的运行时槽数组（顶层与全部函数共享） ──
 * 槽号由 Analyzer 固定；static var 使用全程序唯一槽，仍在同一 slots[] 中。 */
#define SLOT_COUNT <total_var_slots>

static uint64_t slots[SLOT_COUNT];
static TcDiagnostic *tc_aot_cur_diag;

/* ── 前向声明与返回通道（非 void 函数各有一个全局返回槽） ── */
static uint64_t tc_aot_ret_<func_id>;
static void tc_aot_func_<func_id>(TcDiagnostic *diag);

/* ── static var 初始化（按依赖拓扑序） ── */
static void tc_init_static_vars(TcDiagnostic *diag) {
    tc_aot_cur_diag = diag;
    /* 每个 static var 的初始化 RHS 直接发射到 slots[<static_slot>]，
     * 失败即 tc_aot_abort。 */
}

int main(void) {
    TcDiagnostic diag;
    tc_aot_diag_init(&diag);
    tc_aot_cur_diag = &diag;
    if (tc_diagnostic_set_source(&diag, "<source_file>", NULL) != 0) {
        tc_aot_abort(&diag, 0);
    }
    /* slots 以未初始化哨兵值预填充（tc_slot_bits_init_uninitialized），
     * 正确性依赖上游「确定初始化」静态保证（[语言标准 §9.2]）：任何读槽操作
     * 之前变量必已赋值，哨兵只兜底「使用未初始化变量」的实现错误检测。 */
    tc_aot_init_slots(slots, SLOT_COUNT);
    tc_init_static_vars(&diag);
    if (diag.domain != TC_DIAG_NONE) tc_aot_abort(&diag, 0);

    /* generated top-level statements */

    tc_aot_memblock_heap_free_all();
    tc_aot_struct_heap_free_all();
    return 0;
}
```

`SLOT_COUNT` 是 codegen 计算的唯一槽数宏。codegen **不生成**独立的 `static_slots[]`、`memblock_storage[]`、`struct_storage[]`，也不生成固定容量的堆表：`static var`、`memblock` 与 `struct` 的堆块句柄都存放在同一个 `slots[]` 中，堆块由 `tc_aot_rt.c` 内部的动态跟踪表登记，并由 `tc_aot_memblock_heap_free_all` / `tc_aot_struct_heap_free_all` 统一释放。

### 4.2 固定槽位

所有词法 `var` / `static var` 的 slot 在程序开始前固定。TC `var` 语句生成的是一次 RHS 求值和槽写入：

```c
if (tc_aot_arith(..., &slots[X], slots[A], slots[B], tc_aot_cur_diag, line) != 0) {
    tc_aot_abort(tc_aot_cur_diag, line);
}
```

循环下一迭代或向后 goto 再次到达同一 `var` 时，覆盖同一 slot。

### 4.3 memblock 槽位

memblock 的槽位保存**堆块句柄**（`uint64_t` 位模式，指向 `tc_aot_memblock_alloc`/`tc_aot_memblock_clone`/`tc_aot_memblock_from_bytes` 返回的堆块），不是宿主地址，也不是独立的 `memblock_storage[]` 数组：

```c
/* memblock<int32, 10> 堆块布局：
   offset 0:  uint64_t count = 10   (长度头部 = usize；本实现固定 64-bit-only，头宽 64 位)
   offset 8:  int32_t data[10]      (元素数据)
   total = 8 + 10*4 = 48 bytes
   句柄写入 slots[<mb_slot>]
*/

/* 分配：count 个元素、每元素 element_bytes 字节；返回堆块句柄（失败置诊断并返回 0） */
uint64_t tc_aot_memblock_alloc(uint64_t count, size_t element_bytes, TcDiagnostic *diag,
                               int line);
/* 整块深拷贝（赋值语义，[语言标准 §3.8.4]）：按元素步长与 count 复制出一块新的独立堆块 */
uint64_t tc_aot_memblock_clone(uint64_t src, size_t element_bytes, uint64_t count,
                               TcDiagnostic *diag, int line);
/* 进程内全部 memblock 堆块释放（进程退出/嵌入清理时一次性调用） */
void tc_aot_memblock_heap_free_all(void);
```

区间拷贝是语句级 shim `tc_aot_memblock_copy`（不是整块深拷贝），原型见 §11.2。**不得**把整块复制实现为 `tc_aot_memblock_copy(dst, src, total_bytes)` 这类三参形态。

### 4.4 结构体槽位

结构体槽位保存**堆块句柄**（`uint64_t` 位模式），堆块内是字段与填充字节的连续序列；不存在独立的 `struct_storage[]` 字节数组：

```c
/* TC 源（合法形态）：
     struct Point then
         let x: int32
         var y: float64 @padding(4)
     end
   堆块布局: x(4B) + y(8B) + y 之后 padding(4B) = 16B total
   sizeof(Point) = 16；句柄写入 slots[<point_slot>]
   字段读写经 tc_aot_struct_load_bits_value / tc_aot_struct_store_bits（§10.3）
*/
```

### 4.5 名称与确定性

生成的内部名称使用稳定 id：

- label：`tc_label_<stmt_index>`；
- 函数：`tc_aot_func_<func_id>`；
- 条件临时值：`tc_cond_<stmt_index>`（`while` 直接生成为 `for (;;)` + 该临时值，不另设 loop 名）；
- 返回值槽：`tc_aot_ret_<func_id>`（非 `void` 函数）；
- memblock / struct 槽：`slots[<slot>]`（保存堆块句柄）。

相同 typed program 必须生成语义等价且顺序稳定的 C 文本。

---

## 5. 语句代码生成

### 5.1 映射表

| TC 语句 | 目标 C99 |
| ------- | -------- |
| `var` | RHS → 固定 slot |
| `static var` | RHS → `slots[<static_slot>]`（在 `tc_init_static_vars` 中） |
| `static let` | 不生成运行时语句；使用编译期位模式 |
| `let` | 不生成运行时语句；使用编译期位模式 |
| 赋值 | RHS → 已有 slot |
| 字段赋值 `a.b = rhs` | 经 `tc_aot_struct_store_bits` / `tc_aot_struct_memcpy_field` 按字段偏移写入基址堆块 |
| `write`/`writeln` | `tc_aot_write` |
| `read` | `tc_aot_read` + abort guard |
| `if` | 条件 RHS + 原生 C `if/else` |
| `while` | 原生无限循环 + 每次迭代显式条件 |
| `break` | 原生 C `break` |
| `continue` | 原生 C `continue` |
| `label` | `tc_label_<stmt_index>: ;` |
| `goto` | `goto tc_label_<target_stmt_index>;` |
| `funcall` (void) | `tc_aot_func_<id>(tc_aot_cur_diag); if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(...);` |
| `var x = funcall(...)` | `tc_aot_func_<id>(tc_aot_cur_diag); if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(...); slots[X] = tc_aot_ret_<id>;` |
| `x = funcall(...)` | `tc_aot_func_<id>(tc_aot_cur_diag); if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(...); slots[X] = tc_aot_ret_<id>;` |
| `return` | `return;`（嵌入模式 `return 0;`）；有值先写 `tc_aot_ret_<func_id>` |
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
if (tc_aot_<op>(..., tc_aot_cur_diag, source_line) != 0) {
    tc_aot_abort(tc_aot_cur_diag, source_line);
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

对向外 goto，当前值均为标量/指针，没有 C 自动对象需要析构。memblock/struct 句柄保存在全局 `slots[]` 中，所指堆块由运行时跟踪表持有，不随 C 作用域离开而失效。

### 6.4 结构保持与显式标签策略

若 host 编译器或未来资源清理要求不适合原生 C `while`，可用唯一内部标签实现同一 CFG。两种策略必须通过相同差分用例。

---

## 7. 函数代码生成

### 7.1 函数声明与定义

```c
/* 独立程序（embed_mode = 0）：void，abort 走 exit(1) */
static void tc_aot_func_<func_id>(TcDiagnostic *diag);

/* 嵌入模式（embed_mode = 1）：int，非致命 abort 后 return 1，正常返回 0 */
int tc_aot_func_<func_id>(TcDiagnostic *diag);
```

独立程序形态（全部函数共享同一个全局 `slots[]`，函数内**不分配**独立的形参/局部数组）：

```c
static void tc_aot_func_<func_id>(TcDiagnostic *diag) {
    tc_aot_cur_diag = diag;
    /* 函数体内每条语句直接以全局 slots[<slot>] 读写；
     * 形参、局部 var 与 static var 使用同一槽空间中的不同槽号。 */
    /* generated function body */
}
```

嵌入模式函数体末尾生成 `return 0;`；`tc_aot_abort` 被宏替换为 `tc_aot_embed_abort` + `return 1`（见 Embed 详设与 `tc_aot_embed_rt.h`）。TC 函数的语言级返回值走全局 `tc_aot_ret_<func_id>` 槽，不占用 C 返回值通道。

### 7.2 调用约定

**函数调用侧**：

诊断对象只承载错误信号（`domain != TC_DIAG_NONE`）；被调函数的正常返回走独立返回槽 `tc_aot_ret_<func_id>`（见 §7.3），绝不写入诊断。因此调用侧统一按「诊断域非空即 abort」处理，不会把正常返回误判为错误：

```c
/* void 独立调用 */
tc_aot_func_<func_id>(tc_aot_cur_diag);
if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, line);

/* 非 void 调用：var x = func(...) */
tc_aot_func_<func_id>(tc_aot_cur_diag);
if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, line);
slots[X] = tc_aot_ret_<func_id>;

/* 非 void 调用：x = func(...) */
tc_aot_func_<func_id>(tc_aot_cur_diag);
if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, line);
slots[X] = tc_aot_ret_<func_id>;
```

**形参按值传递**：调用前把每个实参 RHS 求值并写入被调函数的形参槽 `slots[<param_slot>]`（形参槽号由 Analyzer 固定，与被调函数体内的读槽一致），随后调用 `tc_aot_func_<id>`；函数签名不接收实参列表。memblock 参数执行深层拷贝；struct 参数执行整块字节复制；ptr 参数复制指针位模式。

### 7.3 函数体代码生成

函数体内语句按源序生成。`var` 定义先求 RHS 再写入该变量的固定槽。

**`return` 生成**：

正常返回只写独立返回槽 `tc_aot_ret_<func_id>`（非 `void` 函数）后 `return`，不复用错误信号通道；诊断域仅在出错时被设置，正常返回时保持 `TC_DIAG_NONE`：

```c
/* void return（独立模式） */
return;

/* 有值 return：先把操作数值写入返回槽（memblock/struct 走深拷贝），再返回 */
tc_aot_ret_<func_id> = <operand_value>;
return;
```

嵌入模式下 `return` 发射为 `return 0;`（正常返回；`tc_aot_abort` 宏返回 1）。

`tc_aot_ret_<func_id>` 是独立的全局返回槽，只由被调函数写、由调用侧读；调用侧只检查 `tc_aot_cur_diag->domain != TC_DIAG_NONE`，不把返回值当错误信号。`TcDiagnostic` **没有** `ret_kind` 字段，也不存在 `TC_AOT_RETURN_OK` 状态，返回不得写入诊断对象。

函数末尾隐式 `return` 仅在静态分析已证明不可达时省略生成。

### 7.4 `funcall` 在函数体内的嵌套

由于调用图无环，函数内 `funcall` 可以直接生成对另一 `tc_aot_func_<id>(tc_aot_cur_diag)` 的调用。调用栈深度在编译期确定。

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
    tc_aot_cur_diag = diag;
    /* 按依赖拓扑序：先初始化无外部依赖的模块 */
    /* 假设 import 顺序：A imports B, B imports C */
    /* 初始化顺序：C → B → A */

    /* 模块 C 的 static var 初始化：RHS 按 kind 直接发射到 slots[<static_slot>] */
    slots[C_x] = tc_aot_lit(TC_INT32, 7ULL, 0, 0);

    /* 模块 B 的 static var 初始化（可能引用 C.x）：失败即 abort */
    if (tc_aot_arith(..., &slots[B_y], slots[C_x], slots[B_y], tc_aot_cur_diag, line) != 0) {
        tc_aot_abort(tc_aot_cur_diag, line);
    }

    /* 模块 A 的 static var 初始化 */
    /* ... */
}
```

初始化器一律按 `TcRhsKind` 直接发射对应的 `tc_aot_*` shim（与函数体内 RHS 发射共用同一路径），**不存在**通用的 `tc_aot_eval_rhs` 入口。diag 非空时由 `tc_aot_abort` 终止准备阶段（独立程序以非零状态退出；嵌入模式返回非零）。

### 8.3 公开成员引用

- `Self.<成员>` → 直接引用本模块成员的槽位 `slots[<slot>]`。
- `<模块>.<成员>` → 引用目标模块成员的槽位 `slots[<slot>]`。
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
| integer arithmetic/unary | `tc_aot_arith` / `tc_aot_unary` → 共享 `tc_sem_int` |
| compare / logic | `tc_aot_compare` / `tc_aot_logic` / `tc_aot_logic_unary` → 共享 semantics |
| bitwise / shift | `tc_aot_bitwise_binary` / `tc_aot_bitwise_unary` / `tc_aot_shift` → `tc_sem_bitwise` |
| float arithmetic/unary/compare | `tc_aot_fp_arith` / `tc_aot_fp_unary` / `tc_aot_fp_compare` → `tc_sem_fp` |
| strict cast / truncate | `tc_aot_cast` / `tc_aot_fp_cast` → 共享 cast 语义 |
| bitcast | `tc_aot_bitcast`；位宽验证已静态完成，运行时只复制规范化位模式 |
| 字面量 | `tc_aot_lit` |
| ptr_load / ptr_store | `tc_aot_ptr_load` / `tc_aot_ptr_store` |
| ptr_address / ptr_add / ptr_sub | `tc_aot_ptr_address` / `tc_aot_ptr_arith` |
| ptr_eq / ptr_ne / ptr_lt/le/gt/ge | `tc_aot_ptr_compare` |
| ptr_size | `tc_aot_ptr_size(sizeof_bits)`（宽度在编译期算出，调用本身恒等返回） |
| memblock_load / memblock_store | `tc_aot_memblock_load` / `tc_aot_memblock_store` |
| memblock 构造 / 赋值深拷贝 | `tc_aot_memblock_alloc` / `tc_aot_memblock_clone` / `tc_aot_memblock_from_bytes` / `tc_aot_memblock_set_elem` / `tc_aot_memblock_set_elem_struct` |
| memblock_copy / memcopy_unsafe | `tc_aot_memblock_copy` / `tc_aot_memcopy_unsafe` |
| memblock `.count` | `tc_aot_memblock_get_count` |
| struct 构造 / 字段读 | `tc_aot_struct_alloc` / `tc_aot_struct_clone` / `tc_aot_struct_load_bits` / `tc_aot_struct_load_bits_value` / `tc_aot_struct_extract` |
| struct 字段写 | `tc_aot_struct_store_bits` / `tc_aot_struct_memcpy_field` |
| I/O | `tc_aot_write` / `tc_aot_read` → 共享 `tc_io` |

### 9.3 求值顺序

TC 每条语句至多一个非嵌套调用。codegen 仍须固定：左 operand 读取 → 右 operand 读取 → shim 调用 → 成功写回。逻辑短路要按 TC 规则避免求值不可达右 operand。

---

## 10. 类型系统与值布局

宽度语义权威定义见 [语言标准 §3.8.2]、[语言标准 §3.9.3]、[语言标准 §3.10.5]；实现查阅用常数表见 [编译器标准 §3.0.1]。AOT 的宽度判定、`ptr_size` 发射与 struct/memblock 布局计算必须与该表一致；codegen **不另建宽度表**，一律委托共享 `tc_sizeof_bits_ex()`（struct 宽度经 `tc_struct_table_width_bits` 回调）。

### 10.1 标量类型宽度表

| 类型 | `sizeof_bits` |
| ---- | ------------- |
| `int8` / `uint8` / `bool` | 8 |
| `int16` / `uint16` | 16 |
| `int32` / `uint32` / `float32` | 32 |
| `int64` / `uint64` / `float64` | 64 |
| `isize` / `usize` / `ptr<T>` | 目标指针宽（本实现固定 64） |

上表是 [编译器标准 §3.0.1] 在 AOT 侧的速查视图，由共享 `tc_sizeof_bits_ex()` 统一实现。

### 10.2 memblock 布局生成

`memblock<T, N>` 的类型等价仅由 `T` 决定；`N` 不参与 `tc_type_equals`，但赋值/传参须比较两侧声明的 `N`（[语言标准 §3.8.1]、[编译器标准 §3.1]）。布局宽度按 [编译器标准 §3.0.1]：`sizeof_bits(usize) + N × sizeof_bits(T)`。

> **目标字长标注**：本实现固定 64-bit-only 目标字长；memblock 长度头宽 = sizeof_bits(usize) = 64 位（8 字节），32 位目标另行立项。

对 `memblock<T, N>` 声明，生成：

```c
/* TC 源：var mb1: memblock<int32, 10> = memblock(int32, count: 10, fill: 0)
   total_bytes = sizeof(usize) + 10 * sizeof(int32) = 8 + 40 = 48
   elem_bytes  = sizeof(int32) = 4
*/
static const size_t mb1_elem_bytes = 4;
static const uint64_t mb1_count = 10;

/* 运行时分配：count 与 element_bytes 由 codegen 传入；alloc 自己写入长度头，
   返回堆块句柄（失败置 diag 并返回 0） */
slots[MB_SLOT_X] = tc_aot_memblock_alloc(mb1_count, mb1_elem_bytes, tc_aot_cur_diag, line);
if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, line);

/* memblock_store 写入元素（index 为变量时） */
if (tc_aot_memblock_store(slots[MB_SLOT_X], mb1_elem_bytes, (uint64_t)index,
                          (uint64_t)value, TC_INT32, tc_aot_cur_diag, line) != 0) {
    tc_aot_abort(tc_aot_cur_diag, line);
}
```

`tc_aot_memblock_alloc` 在分配时即把 `count` 写入长度头（位组装，与端序无关），codegen **不再**单独发射 `memcpy` 写头；`slots[MB_SLOT_X]` 保存的是堆块句柄而非宿主指针。

### 10.3 结构体布局生成

字段类型检查与 [语言标准 §3.9.1] 一致：值位置（字段类型本身或 `memblock` 元素类型）禁止本模块自引用与前向引用，允许已 import 的 `public struct`（须写 `<模块名>.<结构体名>`）；指针所指位置允许 `ptr<正在定义的本结构体>`（指针自引用）。`ptr<S>` 字段宽度恒为指针宽度，不阻塞 `sizeof(S)` 累加。未决结构体名经 `TcType.pending_name`（裸名或 `Mod.Name`）在注册后按当前程序的 import 列表解析为 `struct_id`（与 VM 共用 Analyzer；表按 `(module_name, name)` 定界；构造器名规范化为 `"<模块>.<名>"` 后由 `tc_struct_table_find` 查找）。

**字段存储模型**（与 VM 一致，[语言标准 §3.9.3] 内联值语义）：

- `memblock<U, N>` 字段：在所属 struct 堆块内按 `sizeof(usize) + N × sizeof(U)` 内联存储完整数据（头部+元素位串）；构造器/字段赋值用 `tc_aot_struct_memcpy_field` 写入内容，字段读取用 `tc_aot_struct_extract` 抽出独立堆块；整块赋值/传参复制内联数据即深拷贝。
- `ptr<T>` 字段：存储指针位模式（宽度 = 平台指针宽）。
- 嵌套 `struct` 字段：内联字节序列，读取时 `tc_aot_struct_extract` 抽出。

**`let` / `static let` 基址的字段读（codegen 约束）**：

- 标量 / `ptr` 字段：在 codegen 期从 Analyzer 已求值的字节载荷折叠为整型位模式字面量，**禁止**把分析期宿主堆指针写入生成 C。
- `STRUCT` / `MEMBLOCK` 字段：把字段字节以 C99 复合字面量内联进生成代码，再经 `tc_aot_struct_extract`（offset=0）深拷贝为独立堆块，语义与 VM 值拷贝一致。

对结构体类型定义，codegen 编译期计算字段字节偏移表：

```c
/* TC 源（合法形态）：
     struct Foo then
         let x: int32
         var y: float64 @padding(4)
     end
   sizeof(Foo) = 4 + 8 + 4 = 16
   offsetof(x) = 0
   offsetof(y) = 4
   slots[<foo_slot>] 保存 Foo 堆块句柄（堆块内为 16 字节字段+填充序列）
*/

/* 字段读取 a.y（标量/指针字段）：按位宽 nbytes 读位，避免严格别名与对齐 UB */
/* slots[DST] = tc_aot_struct_load_bits_value(slots[foo_slot], offsetof(y), nbytes); */

/* 字段赋值 a.y = rhs（标量/指针字段）：按位宽 nbytes 写位 */
/* tc_aot_struct_store_bits(slots[foo_slot], offsetof(y), nbytes, rhs_value); */
```

`nbytes = (sizeof_bits(字段类型) + 7) / 8`；memblock/struct 字段走 `tc_aot_struct_memcpy_field` 而非 `store_bits`。整块赋值与传参一律经 `tc_aot_struct_clone` 深拷贝。

---

## 11. 指针、memblock 与 memcopy 代码生成

### 11.1 指针操作 shim

**指针表示模型**：`ptr<T>` 的槽位保存的是**确定性的抽象槽编码**，位模式为

```c
/* 编译期常量；不是宿主对象地址 */
#define TC_AOT_PTR_TAG 1ULL
/* ptr_address 的返回值：((uint64_t)slot << 1) | TC_AOT_PTR_TAG */
/* 0 保留给 nullptr 的抽象位模式（实现定义，与用户值编码区分） */
```

该编码与 VM 侧一致，是[语言标准 §3.5] 允许的**实现定义的抽象槽编码**，不是宿主对象地址。规范只要求同一实现每次运行映射确定（[语言标准 §1.3]），因此 shim **禁止**返回或使用 `&slots[X]`、`&static_slots[Y]`、`malloc` 返回值等宿主地址——否则 `bitcast(usize, ptr_address(T, x))` 会暴露 ASLR/PIE 相关地址，同一程序两次运行的标准输出不同，同时造成 VM/AOT 分歧。

```c
/* ptr_address(T, ident)：ident 的绑定槽号由 Analyzer 固定，返回抽象槽编码 */
uint64_t tc_aot_ptr_address(int slot);

/* ptr_load(T, ptr)：按槽号回读 slots[slot]；ptr_bits == 0 或编码非法
   → TC_RE_NULL_POINTER_DEREFERENCE */
int tc_aot_ptr_load(uint64_t *slots, uint64_t ptr_bits, uint64_t *out, TcDiagnostic *diag,
                    int line);

/* ptr_store(T, ptr, value)：按槽号写入；store_type 为 bool 时归一化到 0/1 */
int tc_aot_ptr_store(uint64_t *slots, uint64_t ptr_bits, uint64_t value_bits,
                     TcTypeTag store_type, TcDiagnostic *diag, int line);

/* ptr_add / ptr_sub：以解码出的槽号为线性索引做 ± offset（is_add = 1/0）；
   nullptr 操作数 → TC_RE_NULL_POINTER_ARITHMETIC */
int tc_aot_ptr_arith(int is_add, uint64_t ptr_bits, int64_t offset, uint64_t *out,
                     TcDiagnostic *diag, int line);

/* ptr_eq / ptr_ne / ptr_lt/le/gt/ge：按编码比较；序关系比较遇 nullptr
   → TC_RE_NULL_POINTER_DEREFERENCE，ptr_eq(null, null) 为真 */
int tc_aot_ptr_compare(TcCompareOp op, uint64_t lhs, uint64_t rhs, uint64_t *out,
                       TcDiagnostic *diag, int line);

/* ptr_size(T)：T 的抽象宽度（位）在编译期算出，运行时恒等返回 */
uint64_t tc_aot_ptr_size(size_t sizeof_bits);
```

`ptr_add` / `ptr_sub` 计算的是**槽号位移**：`new_slot = slot ± offset`，再编码回 `((uint64_t)new_slot << 1) | TC_AOT_PTR_TAG`。`offset` 为 `usize` 的数学值，按 `int64_t` 传入。越界指针（解码后槽号越界）上的 `ptr_load` / `ptr_store` 属[语言标准 §1.3] 列出的实现定义行为，同一实现内必须确定。

### 11.2 memblock 操作 shim

```c
/* memblock_load(T, mb, idx)：mb_bits 为 slots 中的堆块句柄，
   element_bytes = (sizeof_bits(T) + 7) / 8；
   越界 → TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE */
int tc_aot_memblock_load(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                         TcTypeTag elem_type, uint64_t *out, TcDiagnostic *diag, int line);

/* memblock_store(T, mb, idx, value)：elem_type 为 bool 时把值归一化到 0/1 后写入 */
int tc_aot_memblock_store(uint64_t mb_bits, size_t element_bytes, uint64_t index,
                          uint64_t value_bits, TcTypeTag elem_type, TcDiagnostic *diag,
                          int line);

/* memblock_copy(T, dst, d_idx, src, s_idx, len)：区间拷贝（不是整块深拷贝）；
   长度头写入、元素个数由堆块头部读出，codegen 只传 element_bytes */
int tc_aot_memblock_copy(uint64_t dst_bits, uint64_t dst_index, uint64_t src_bits,
                         uint64_t src_index, uint64_t length, size_t element_bytes,
                         TcDiagnostic *diag, int line);
```

`tc_aot_memblock_copy` 对应 TC 语句 `memblock_copy`，是**区间拷贝**（[语言标准 §6.7.2.3]）；整块深拷贝是**赋值语义**（[语言标准 §3.8.4]），由 `tc_aot_memblock_clone` 承载（§4.3），两者不得共用一个三参 `(dst, src, total_bytes)` 原型。区间按半开区间检查：`0 ≤ dst_index ≤ dst_count`、`dst_index + length ≤ dst_count`、`0 ≤ src_index ≤ src_count`、`src_index + length ≤ src_count`；**空拷贝（`length == 0`）同样执行检查**，允许下标等于 `count`（[语言标准 §6.7.2.3]）。区间不合法 → `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`，不得因 `length == 0` 提前返回而跳过检查。拷贝经临时缓冲完成，重叠区间行为确定（memmove 语义）。

### 11.3 memcopy_unsafe shim

与 VM 一致：空指针 → `TC_RE_NULL_POINTER_DEREFERENCE`；`length < 0` 或有符号下标数学值 `< 0` → `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`。**注意**：编译期**可确定**为负的 `length` / `dst_idx` / `src_idx`（整数字面量或 `let` / `static let` 常量来源）已由共享 Analyzer 以静态 `TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE` 拒绝（A-3 裁决，[语言标准 §6.8.9]、[编译器标准 §11.4.6]），不会到达本 shim；shim 只处理编译期不可确定的负值。codegen 须按下标操作数的有符号性（字面量负号 / 绑定或字段类型）传入对应 `TcTypeTag`，shim 按该类型判负；**不得**一律按 `usize` 求值后再检查（否则负字面量回绕导致漏检）。

```c
int tc_aot_memcopy_unsafe(uint64_t *slots, uint64_t dst_ptr, uint64_t dst_index,
                          TcTypeTag dst_idx_type, uint64_t src_ptr, uint64_t src_index,
                          TcTypeTag src_idx_type, int64_t length, size_t element_bytes,
                          TcTypeTag elem_tag, TcDiagnostic *diag, int line);
```

---

## 12. `bitcast` 与数值一致性

### 12.1 位模式策略

内部 slot 以 `uint64_t` 保存位模式，写入时已按各自类型位宽规范化。`ptr` ↔ `ptr` 与 `ptr` ↔ 等宽整数的 bitcast 只复制位模式；其余组合委托 `tc_aot_bitcast`（内部经 `memcpy` 在宿主对象与位模式之间搬运）。**`ptr` ↔ 浮点的 bitcast 为静态拒绝**（语言标准 §6.6.6／§3.10.9）：AOT 不得为 `bitcast(float64, ptr_val)` 一类形态生成任何等价路径，也不得把 `ptr` 值经宿主指针/浮点对象表示中转：

```c
/* ptr ↔ ptr / ptr ↔ 等宽整数：等宽位重解释，直接复制规范化位模式（不再掩码） */
slots[DST] = slots[SRC];

/* 其余组合（整数/浮点等）委托 shim，失败即 abort */
if (tc_aot_bitcast(TARGET_TYPE, SOURCE_TYPE, &slots[DST], slots[SRC], tc_aot_cur_diag, line) != 0) {
    tc_aot_abort(tc_aot_cur_diag, line);
}
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
- strict 按标准优先级报告无效、除零、上溢、下溢（优先级：无效操作 → 除零 → 上溢 → 下溢，见语言标准 §6.3.2）。
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
- 对 `let` / `static let` 结构体基址的复合字段整体读出，按 §10.3 codegen 约束内联字节并深拷贝，不得嵌入分析期堆地址。
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
  -o, --output FILE    输出 C 文件路径（默认 <input>.c）
  -H, --header FILE    生成嵌入模式头文件（需 --embed）
  -c, --check          仅静态检查，不生成 C
  -r, --run            编译并运行生成 C
  -I, --include PATH   添加模块搜索路径（可重复，最多 64 条）
  -e, --embed          嵌入库模式（不生成 main()，生成非 static 符号 + 函数表）
  -h, --help           显示帮助
  -V, --version        显示版本
```

`--embed` 与 `--run` 互斥；`--header` 仅在 `--embed` 下有意义。`--embed` 生成非 `static` 的 `slots[]`、`tc_aot_func_*`、`tc_aot_ret_*`、`tc_aot_func_table` 与 `tc_aot_init` / `tc_aot_cleanup`，并把 `tc_aot_abort` 宏替换为非致命的 `tc_aot_embed_abort` + `return 1`（§7.1、§17.2）。

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

AOT 的核心正确性证据是同一源文件经 VM 与 AOT 产生相同的**可观察行为**（[语言标准 §1.3]）。差分比较必须逐项覆盖下列四项，**不多不少**：

1. 是否通过静态检查（两侧接受/拒绝一致）；
2. **首个**规范诊断及其错误码——按 [语言标准 §11]「诊断顺序」的四步规则（阶段优先 LT→SYN→SEM→CT、同阶段按源序位置、专用码优先、条款内既有顺序优先）在两侧选出**同一个**首个诊断；
3. 按源序成功提交到 TC 抽象标准输出的字节；
4. 正常结束，或**首个**运行时错误及错误码（fail-fast，[语言标准 §11.1]）。

`stderr` 的消息文本、进程退出状态、数值位模式的宿主表示**不是**可观察行为：两侧措辞或退出码可以不同，不构成差分失败，只作辅助诊断信息记录。对专门用例导出的数值位模式用于定位分歧，其判据仍归入上述第 3、4 项。`--check` 的接受/拒绝结果属于第 1 项。

### 16.2 0.0.44 测试矩阵

| 类别 | 用例 |
| ---- | ---- |
| 模块系统 | 多文件导入、依赖拓扑、循环导入拒绝、公开/私有访问 |
| `#lib`-only 程序 | `--check` 接受且 `-r` 生成并运行成功；差分矩阵须覆盖 `#lib`-only 输入（`tests/valid/` 下 28 个 `#lib`-only 语料，当前 220 项 `run_diff_test` 中无此类用例，属覆盖缺口，须补齐） |
| 函数 | 签名、funcall、return、void 返回、无环调用图 |
| 类型系统 | memblock N 比较、ptr 同型、struct 字段、isize/usize |
| structured loop | 零次、一次、多次、嵌套 while |
| loop control | 最内层 break/continue、嵌套 if 中控制 |
| paradigm isolation | while 内 goto/label 全部静态拒绝 |
| 函数内 goto/label | 向外跳转、跨控制流拒绝 |
| definite init | 条件、回边、continue、break、goto 会合、多域 CFG |
| fixed slots | 每迭代 var 重初始化、后向 goto 重入 |
| memblock | 分配、读写、区间拷贝、深拷贝传参、越界错误 |
| ptr | 取地址、读写、算术、等值/序比较、空指针分类、指针 `cast`（**重标记，不附加等宽条件**，语言标准 §3.7／§3.10.5；含 `cast(ptr<T>, nullptr)`）与 `bitcast` 的 `ptr`↔整数往返（位模式复制，不经数值 shim）。**不提供 `ptr` ↔ 浮点的位重解释**（语言标准 §6.6.6） |
| struct | 构造器、字段读写、双层可变性、整块复制 |
| memcopy_unsafe | 空指针、负长度/负下标（有符号求值）、memmove 语义 |
| bitcast | 等宽往返、NaN payload、-0.0、最高位；含 `ptr`↔整数路径（**不含 `ptr` ↔ 浮点**，该组合为静态拒绝） |
| cast | 全严格可表示性、整数 truncate；指针目标仅复制位模式 |
| float | strict/ieee、每步精度、异常优先级 |
| let | 编译期与 runtime 位模式一致 |
| static var/let | 拓扑初始化、跨模块共享 |
| diagnostics | 目标错误 kind、行号、打印名、新诊断覆盖 |

### 16.3 `#lib`-only 输入的覆盖要求

`#lib`-only 程序（无 `#program`、只含 `#lib` 模块的合法输入）必须在 `--check` 下被接受，并在 `-r` 下成功生成 C 且宿主编译通过，**不得**因生成的 `static` 函数/变量未被 `main()` 引用而被 `-Werror=unused-function` / `-Werror=unused-variable` 判为宿主编译失败（合法程序被拒属可观察行为分歧）。矩阵要求：

- 至少覆盖 `#lib` 顶层仅 `static let`、仅 `static var`、仅 `func`、以及三者混合四类；
- 每类在 `--check`（两侧接受）与 `-r`（生成成功、无宿主编译错误）两侧比对 §16.1 的四项可观察行为。

生成侧口径：独立模式下 `tc_aot_func_<id>` / `slots[]` / `tc_aot_ret_<id>` 仍为 `static`，但 codegen 必须保证每个发射的静态符号在 `main()` 侧被引用（例如统一由一个顶层引用点消费），使 `-Werror` 下仍编译通过；不得依赖宿主放宽警告、也不得把合法 `#lib`-only 程序改判为静态错误。

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
| `src/aot/tc_aot_codegen.c/h` | typed program → C99（preamble、`main()`、嵌入头、总入口） |
| `src/aot/tc_aot_emit_rhs.c/h` | 每个 `TcRhsKind` → C 表达式 / 槽写入 |
| `src/aot/tc_aot_emit_stmt.c/h` | 每个 `TcStmtKind` → C 语句（含字段赋值、ptr/memblock/memcopy 调用点） |
| `src/aot/tc_aot_emit_func.c/h` | 函数定义/前向声明、`static var` 初始化、嵌入函数表 |
| `src/aot/tc_aot_codegen_internal.h` | codegen 子模块共享声明与 `TcAotEmitCtx` |
| `src/aot/tc_aot_rt.c/h` | 生成程序的 shim（含 memblock 管理、指针操作、诊断桥接） |
| `src/aot/tc_aot_embed_rt.h` | 嵌入模式运行时接口（`tc_aot_cur_diag`、`tc_aot_func_entry`、`tc_aot_embed_abort`） |
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
                  int embed_mode);    /* 0 = 独立程序, 1 = 嵌入库 */
```

`embed_mode = 1` 时生成非 `static` 符号、函数表、`int` 返回的 `tc_aot_func_*` / `tc_aot_init`，并以非致命 abort 宏替换 `tc_aot_abort`。

### 17.3 分发完整性

每个 statement kind 和 RHS kind 都要在 VM、AOT、free、Analyzer、const-eval（适用时）出现明确处理。AOT 不允许通过 default 分支把新 kind 当作普通赋值。

---

## 18. 已知可移植性债务

本实现当前无可移植性债务，相关现役约定如下：

- **原生字节序无关**：memblock/struct 的长度头部、标量元素与字段一律经显式小端位组装（`tc_aot_store_bits` / `tc_aot_load_bits`）存取，任意字节序主机上数值一致（[语言标准 §3.5]）。
- **无 FENV 时浮点判定位级精确**：`tc_sem_fp.c` 的无 FENV 路径以 `tc_fp_exact_neq`（uint64 对 128 位整数运算）判定下溢，与 fenv 构建逐字节一致。
- **浮点十进制输出为自实现**：`tc_io.c` 按位模式精确生成十进制（roundTiesToEven、`%e` 两位指数、`%g` 阈值），不委托宿主 `snprintf`（[语言标准 §10.4]）。
- **偏移与分配无宿主依赖**：`ptr_add` / `ptr_sub` 的偏移严格为 `usize`（Analyzer 与运行时一致）；`tc_aot_memblock_alloc` 对 `count × element_bytes` 做溢出守卫（与 VM 侧一致）。

本实现维持 64-bit-only（memblock 头部宽 = `sizeof_bits(usize)` = 64 位，含编译期断言）；32 位目标 / 大端主机支持不在 0.0.44 范围，上述位级约定已保证任意字节序主机的数值一致性。

---

*语言合法性与可观察语义以 [TC 语言标准 0.0.44](./TC语言标准设计说明书-0.0.44.md) 与 [TC 编译器标准 0.0.44](./TC编译器标准设计说明书-0.0.44.md) 为准。*
