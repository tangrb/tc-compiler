# libtc 设计说明书

> **规范基线（唯一权威）**：[TC 语言标准 0.0.39](./TC语言标准设计说明书-0.0.39.md) · [TC 编译器标准 0.0.39](./TC编译器标准设计说明书-0.0.39.md)
>
> **当前实现基线**：libtc / TC-VM v0.0.39
>
> **状态**：承载 TC 0.0.39 的 libtc 架构设计，涵盖模块系统、函数、memblock、ptr、struct 与完整 13 阶段编译管线。
>
> **调用者速查**：[libtc API](./libtc-api-0.0.39.md)

---

## 目录

1. [边界与目标](#1-边界与目标)
2. [公共 API](#2-公共-api)
3. [目标编译流水线](#3-目标编译流水线)
4. [`TcTypedProgram` 数据契约](#4-tctypedprogram-数据契约)
5. [所有权与生命周期](#5-所有权与生命周期)
6. [失败、回滚与 OOM](#6-失败回滚与-oom)
7. [诊断契约](#7-诊断契约)
8. [Parser 与 Analyzer 集成](#8-parser-与-analyzer-集成)
9. [Executor 与 AOT 消费边界](#9-executor-与-aot-消费边界)
10. [多文件模块实现](#10-多文件模块实现)
11. [性能与并发](#11-性能与并发)
12. [构建与嵌入](#12-构建与嵌入)
13. [验证](#13-验证)
14. [实现基线与迁移](#14-实现基线与迁移)

---

## 1. 边界与目标

### 1.1 版本基线

| 维度 | 版本 | 说明 |
| ---- | ---- | ---- |
| 目标语言规范 | 0.0.39 | libtc 最终必须实现的接受集、结果和诊断阶段 |
| 编译器规范 | 0.0.39 | 确定的 13 阶段编译管线、错误码与检查顺序 |
| 本文 | 0.0.39 设计 | 面向当前语言能力的实现设计 |

### 1.2 libtc 的职责

libtc 是 TC 编译器前端与执行器的嵌入入口：

- 从字符串或文件建立完整多模块编译单元；
- 从入口 `#program` 出发，按 `import` 自动加载所有可达 `#lib` 模块；
- 调用 Lexer/Parser 构造每个模块的 `TcModule`；
- 完成模块依赖图 DAG 检查、函数签名收集（子阶段 4a→4b→4c→4d）；
- 完成名称、作用域、类型、控制上下文、完整多域 CFG 与确定初始化（子阶段 6a→6b→6c→6d→6e）；
- 完成 funcall / return 专用检查（阶段 7、8）、`let`/`static let` 求值（阶段 9）、静态布尔判定（阶段 10）、调用图环检查（阶段 12）；
- 成功时返回可被 VM/AOT 消费的 `TcTypedProgram`；
- 执行 typed program 并传播运行时诊断；
- 管理阶段间所有权转移和失败回滚。

### 1.3 非职责

- libtc 不定义语言规范；合法性以 0.0.39 标准为准。
- libtc 不累积多错误；目标采用 fail-fast 单诊断，首个诊断按编译器标准 §1.3 确定性规则选择。
- libtc 不提供 AOT 输出文件或 host C 工具链管理。
- libtc 不含 REPL 模式（0.0.39 已删除 REPL）。

### 1.4 设计原则

1. **成功才转移所有权**：失败不把半构造对象交给调用方。
2. **静态完成后才执行**：执行器不补做 Parser/Analyzer 检查。
3. **一个 typed program，多后端**：VM 与 AOT 消费同一静态结果。
4. **错误域分离**：语言错误、API/文件错误和 OOM 不互相冒充。
5. **阶段确定性**：编译器标准 §1.2 的 13 阶段顺序必须完整保留，阶段间不交叉。

---

## 2. 公共 API

### 2.1 函数签名

```c
int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out,
                      TcDiagnostic *diag);

int tc_compile_file(const char *path,
                    TcTypedProgram *out,
                    TcDiagnostic *diag);

int tc_run_program(const TcTypedProgram *program,
                   TcDiagnostic *diag);

void tc_typed_program_free(TcTypedProgram *program);

int tc_set_module_search_paths(char *const *paths, size_t count,
                               TcDiagnostic *diag);
```

0.0.39 新增 `tc_set_module_search_paths` 用于多文件模块搜索；`tc_compile_source` 新增 `name` 参数用于诊断和模块解析。`tc_run_typed` 重命名为 `tc_run_program` 以反映其涵盖模块静态初始化的语义。

### 2.2 源兼容与二进制兼容

函数签名保持源兼容，但 `TcTypedProgram`、`TcDiagnostic` 和错误枚举是公开可见 C 结构；0.0.39 大幅扩展这些结构（函数签名、模块元数据、memblock/ptr/struct 类型参数）。嵌入方必须重新编译。本文不承诺跨版本的二进制 ABI 兼容。

### 2.3 `tc_compile_source`

成功：返回 0，把完整 `TcTypedProgram` 所有权交给调用方。

失败：返回 -1，设置 `diag`；调用方不取得 `out` 所有权。

### 2.4 `tc_compile_file`

从入口 `#program` 文件出发，自动加载所有可达 `#lib` 模块。模块搜索路径优先级：入口文件所在目录 → `tc_set_module_search_paths` 设置的路径 → 默认路径。成功/失败契约与 `tc_compile_source` 相同。

### 2.5 `tc_run_program`

输入必须来自成功编译且尚未释放。执行前先按依赖拓扑序初始化所有可达模块的 `static var`。每次执行建立新的运行时 slots。成功返回 0；运行时错误返回 -1 并设置 diag。

---

## 3. 目标编译流水线

### 3.1 13 阶段图

```text
source files (.tc)
  │
  ├─ 阶段 1: UTF-8 解码
  ├─ 阶段 2: 词法与缩进扫描（最长匹配、缩进栈、字面量上限检查）
  ├─ 阶段 3: 语法解析（受限恢复、操作数数量检查）
  ├─ 阶段 4: 模块结构与导入解析
  │    ├─ 4a: 单文件结构（五层排序、可见性、模式误用）
  │    ├─ 4b: 导入解析（定位、非库检查、歧义、重复、名称冲突）
  │    ├─ 4c: 依赖图环检查（DAG）
  │    └─ 4d: 收集函数签名
  ├─ 阶段 5: 函数重名与签名内名称冲突
  ├─ 阶段 6: 名称/作用域/类型语义
  │    ├─ 6a: 控制流上下文（goto/label 祖先检查）
  │    ├─ 6b: 名称作用域预建（函数表、标签表、本库成员索引）
  │    ├─ 6c: goto/label 名称解析
  │    ├─ 6d: 类型、模式与字面量语义检查
  │    └─ 6e: I/O 格式检查
  ├─ 阶段 7: funcall 检查（目标、位置、实参）
  ├─ 阶段 8: return 检查（位置、形式、操作数）
  ├─ 阶段 9: let/static let 求值 + static var 初始化器验证
  ├─ 阶段 10: 静态布尔三态判定 / 逻辑读边判定
  ├─ 阶段 11: CFG 构建、可达性、确定初始化固定点（多域）
  ├─ 阶段 12: 调用图递归环检查
  └─ 阶段 13: VM 代码生成 / 执行
```

### 3.2 编译事务

目标实现用局部临时对象承载每一阶段：

```text
init temporary program/typed state
load all modules (stage 4)
collect function signatures (stage 4d)
for each phase in 5..12:
    if failure: free all temporary state, set diag, return -1
move complete typed state to *out
```

这样可以实现"成功才转移所有权"，避免任一阶段失败产生两套调用者规则。

### 3.3 Parser 输出

Parser 为每个模块产生 `TcModule`（或统一 `TcProgram`）保留：

- 树形 if/while/func/struct 子块；
- break/continue/goto/label/return 节点；
- var/let/赋值/I/O/funcall；
- 模块模式指令和 import 声明；
- RHS 和 operand（含所有新增 ptr_*/memblock_* 指令）；
- 行、列、缩进相关源位置；
- 尚未解析的标识符文本。

### 3.4 Analyzer 输出

Analyzer 成功意味着：

- 模块依赖图无环；
- 每个名称绑定确定，含导入限定名和 `Self.` 解析；
- 每个 `var`/`static var` 有固定 slot；
- 每个 RHS 的类型和模式确定；
- `let`/`static let` 有规范化 `TcValue`；
- goto、label、break、continue 的目标确定；
- 完整多域 CFG（顶层 + 各函数）与可达性确定；
- 所有可达使用满足确定初始化；
- 函数调用图无环；
- 不存在任何静态错误。

---

## 4. `TcTypedProgram` 数据契约

### 4.1 0.0.39 静态信息

typed program 必须直接保存或能够无歧义重建：

- 所有成功编译的模块及元数据；
- 完整作用域树与 block path；
- statement → scope 映射；
- binding → slot 映射（含 `static var` 的全程序唯一槽）；
- 函数签名列表（形参、返回类型、可见性、所属模块）；
- label/goto 解析目标；
- break/continue → loop id；
- 每个 RHS 的源/目标类型与合法模式；
- 完整多域 CFG（顶层 + 各函数，封闭独立）；
- `let`/`static let` 的目标精度位模式；
- memblock 绑定的 `N` 数学值与存储布局；
- 结构体类型定义（字段列表、类型、可变性、padding、总宽度）；
- 源位置映射。

### 4.2 不变量

成功的 typed program 满足：

- 所有指针字段要么有效、要么为可释放的 NULL；
- count/capacity/items 三元组一致；
- statement/RHS kind 全部属于已实现集合；
- slot 在 `[0, variable_slot_count)`；
- `let`/`static let` 不占 runtime slot；
- CFG 目标引用有效节点，各域入口和出口正确；
- 函数调用图无环（已由阶段 12 保证）；
- 不存在未解析名称或待定类型。

### 4.3 可重复消费

Executor 和 AOT 只读 typed program。一个成功对象可以：

1. 执行零次或多次；
2. 生成 C 零次或多次；
3. 在消费完成后释放一次。

任何消费者不得修改共享 symbol、const value、CFG 或 `type_table`（Analyze 完成后类型池只读）。

---

## 5. 所有权与生命周期

### 5.1 总表

| 对象 | 创建者 | 成功后的所有者 | 释放者 |
| ---- | ------ | -------------- | ------ |
| 调用方 source | 调用方 | 调用方 | 调用方 |
| 文件缓冲 | `tc_compile_file` | libtc | 函数返回前 |
| `TcProgram` / `TcModule` | Parser | Analyzer/typed program | 失败回滚或 `tc_typed_program_free` |
| 符号/作用域/标签 | Analyzer | typed program | `tc_typed_program_free` |
| 函数签名 | Analyzer | typed program | `tc_typed_program_free` |
| CFG（多域） | Analyzer | typed program | `tc_typed_program_free` |
| `let`/`static let` 值 | Analyzer | typed program 内嵌 | 随 typed program |
| runtime slots（含 static var） | Executor | Executor | 每次运行返回前 |
| `TcDiagnostic` 内部字符串 | diagnostic 模块 | 调用方持有的 diag | `tc_diagnostic_clear` |

### 5.2 AST 递归释放

`tc_statement_free` 必须按 kind 释放所有新增的 0.0.39 payload：函数定义的 body 和形参列表；funcall 的目标名和命名实参；return 的操作数；struct 定义的字段列表；memblock 构造器的元素列表；ptr_* 指令的操作数；import 声明；以及字段赋值/读取的字段名。每个新增 kind 必须同步 free 分发；未知 kind 在调试构建中断言，不能静默泄漏。

### 5.3 CFG 所有权

多域 CFG（顶层 + 各函数）的 nodes、edges、前驱/后继数组、bitset 和 source mapping 使用单一所有权根。构建过程中的临时工作队列由 Analyzer 自身释放，不进入 typed program。

---

## 6. 失败、回滚与 OOM

### 6.1 阶段回滚

| 失败点 | 必须释放 |
| ------ | -------- |
| source capture | 已复制的 filename/source |
| Lexer/Parser | tokens、当前语句、已完成语句树、块栈 |
| 模块解析（阶段 4） | 已加载模块、已解析的依赖图、部分签名字典 |
| Binder/Type（阶段 5-6） | program、symbols、scope/label 路径、临时绑定 |
| funcall/return 检查（阶段 7-8） | 阶段 6 已建的符号表保持不变，仅回滚本阶段临时状态 |
| let 求值（阶段 9） | 临时值/名称，program/symbols/CFG |
| CFG（阶段 11） | 全部已建 nodes/edges/bitsets、program、symbols |
| 调用图（阶段 12） | 图结构与临时邻接表 |
| Executor | runtime slots、执行上下文；typed program 仍归调用方 |

### 6.2 OOM

所有 `malloc/calloc/realloc/strdup` 失败：

```text
domain  = implementation
kind    = TC_ERR_OUT_OF_MEMORY
message = memory allocation failed
return  = -1
```

OOM 不得降级为 `SyntaxError`。`realloc` 采用临时指针，失败时保留旧块以便统一回滚。

### 6.3 输出对象

推荐实现只在全部阶段（1-12）成功后赋值 `*out`。在任意失败路径：

- 调用方不读取 `out`；
- 调用方不释放 `out`；
- libtc 保证已释放本次调用创建的所有对象。

---

## 7. 诊断契约

### 7.1 三个诊断域

0.0.39 目标明确区分：

| 域 | 例子 | 是否决定 TC 程序合法性 |
| -- | ---- | ----------------------- |
| Language | Syntax、TypeMismatch、UninitializedVariable、运行时 overflow | 是 |
| API/Environment | 文件无法打开/读取、无效调用前置条件 | 否 |
| Implementation | OutOfMemory、内部不变量破坏 | 否 |

### 7.2 语言错误

0.0.39 编译器标准 §11.4 是 `TcErrorKind` 的权威清单。错误码按阶段分为：

- §11.4.1 通用与核心（60+ 码）：语法、名称、类型、字面量、常量、比较、格式、操作数数量、缩进、块结构、控制流等，含所有 `TC_CE_*` 和运行时 `TC_RE_*` 码。
- §11.4.2 函数诊断（20 码）
- §11.4.3 memblock 诊断（4 码）
- §11.4.4 结构体诊断（7 码）
- §11.4.5 模块诊断（10 码）
- §11.4.6 指针与 memcopy 诊断（4 码）
- 实现码：`TC_ERR_OUT_OF_MEMORY`

### 7.3 阶段确定性

首个诊断的选择遵循编译器标准 §1.3 的三个原则：阶段优先 → 源位置优先 → 规则优先。各阶段有其专用优先级（如 §4.1 模块错误优先级、§8.2 funcall 错误优先级、§7.6 CFG 错误优先级）。同一源程序经由不同入口编译应产生相同诊断。

---

## 8. Parser 与 Analyzer 集成

### 8.1 批量解析

`tc_compile_source` 和 `tc_compile_file` 始终解析完整程序。`if`/`while`/`func`/`struct` 的缩进和 `end` 必须在 Parser 返回前闭合。Parser 不执行全局类型或可达性判断。

### 8.2 0.0.39 新结构

Parser/Analyzer 的目标扩展涵盖：

| 能力 | Parser | Analyzer |
| ---- | ------ | -------- |
| 模块系统 | 模式指令、import、可见性修饰符 | 层级检查、导入解析、DAG、命名空间 |
| 函数定义 | func 关键字、形参列表、返回类型、函数体 | 签名收集、名称保护、无环调用图 |
| funcall | 命名实参语法 | 目标检查、实参名称/顺序/类型、调用位置 |
| return | 有值/无值形式 | 函数体内位置、形式匹配、操作数类型 |
| `ptr<T>` 类型 | 类型参数语法 | 同型约束、宽度计算、禁止通用运算 |
| `memblock<T,N>` 类型 | count 参数语法 | N 记录、类型等价不含 N（绑定级容量约束）、N 比较 → `MEMBLOCK_SIZE_MISMATCH` |
| `struct` 类型 | 字段声明、@padding | 字段类型位置规则（§3.9.1）、构造器全字段验证、双层可变性、布局 |
| `isize`/`usize` | 关键字识别 | 平台字长约束、cast 要求 |
| static var/let | 关键字 + 可见性 | 依赖拓扑初始化、跨模块共享 |
| ptr_* 指令 | 内建调用外壳 | 类型验证、可变性、空指针分类 |
| memblock_* 指令 | 内建调用外壳 | 边界检查、深拷贝语义、区间拷贝 |
| memcopy_unsafe | 内建调用外壳 | 空指针/负长度检查、memmove 语义 |
| 静态布尔三态 | `if`/`while` 条件 RHS | 阶段 10：`tc_try_eval_static_bool*`；形态见 [语言标准 §5.2.2] |
| `sizeof_bits` | 类型宽度查询 | `tc_sizeof_bits`；速查 [编译器标准 §3.0.1] |

### 8.3 完整 CFG

libtc 的 Analyze 阶段必须在成功返回前完成完整多域 CFG 固定点（阶段 11）。顶层 CFG 与各函数 CFG 是封闭独立的分析单元，不得拼接或共享状态。

---

## 9. Executor 与 AOT 消费边界

### 9.1 Executor

`tc_run_program`：

```text
按依赖拓扑序初始化所有可达模块的 static var
allocate fresh slots/context
execute typed statements（含顶层和函数调用路径）
on runtime error: set diag, free runtime state, return -1
on success: free runtime state, return 0
```

### 9.2 AOT

AOT 通过同一 typed program 读取语句、slot、目标和常量。libtc 不提供 AOT 特化编译入口；`tc-aot` 先调用 `tc_compile_file`，再调用 codegen。

### 9.3 一致性

- 静态接受集完全由 libtc 决定；
- VM/AOT 不各自放宽或收紧；
- 数值和 I/O 使用共享 runtime；
- `let`/`static let` 的位模式由 Analyzer 一次确定；
- 新后端必须消费同一 typed contract。

---

## 10. 多文件模块实现

### 10.1 模块加载流程

0.0.39 的模块加载按编译器标准 §4.1 执行：

1. 从入口 `#program` 文件出发；
2. 对每个 `import` 语句，在搜索路径中唯一定位 `模块名.tc`；
3. 验证目标为 `#lib` 模式；
4. 递归解析被导入模块的依赖；
5. 构建模块依赖图，验证 DAG；
6. 按依赖拓扑序遍历模块。

### 10.2 命名空间与可见性

- 每个 `#lib` 模块维护一个命名空间：`static let`、`static var`、`func`、`struct` 名；
- `Self.<成员名>` 在函数体内访问本库成员；
- `<模块名>.<成员名>` 访问导入模块公开成员；
- `private` 成员外部访问 → `TC_CE_PRIVATE_MEMBER_ACCESS`。

### 10.3 `static var` 初始化

所有可达模块的 `static var` 按依赖拓扑序在程序入口执行前初始化。同一模块的 `static var` 槽全程序唯一，所有导入者共享。初始化器操作数仅可为字面量、已成功初始化的 `Self` 成员，以及导入的公开 `static let`/`static var`。

---

## 11. 性能与并发

### 11.1 性能阶段

当前 `TC_BENCH=1` 输出 parse、module resolve、analyze、execute 时间。

### 11.2 复杂度目标

- Lex/Parse：O(source bytes + statements)
- Module resolve：O(modules + imports)
- binding：平均 O(statements)
- CFG build：O(nodes + edges)
- bitset dataflow：O(iterations × edges × bitset words)
- Call graph：O(functions + call edges)
- execution：O(dynamic statements)

### 11.3 可重入

除环境变量读取和标准 I/O 外，编译状态都在调用栈/对象中，不使用可变全局编译器状态。不同线程可使用各自 `TcDiagnostic`、`TcTypedProgram` 和输入并发编译；同一对象不得无同步并发修改/释放。

---

## 12. 构建与嵌入

### 12.1 头文件与链接

```c
#include "tc_lib.h"
```

libtc 由 CMake 生成并链接 Lexer、Parser、Analyzer、Executor、runtime semantics 和 I/O。新增 CFG 模块、模块解析、函数签名收集和调用图模块必须进入相同静态库。

### 12.2 最小调用模式

```c
TcDiagnostic diag;
TcTypedProgram program;

tc_diagnostic_init(&diag);
if (tc_compile_file("input.tc", &program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    tc_diagnostic_clear(&diag);
    return 1;
}

if (tc_run_program(&program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
}

tc_typed_program_free(&program);
tc_diagnostic_clear(&diag);
```

---

## 13. 验证

### 13.1 API 契约测试

- compile source/file 成功只转移一次所有权；
- 每个失败阶段不要求调用方释放 out；
- source 在 compile 返回后可释放；
- typed program 可重复运行；
- runtime 失败后 program 仍可释放；
- diagnostic 可 clear 并复用；
- 文件/API 错误不返回 SyntaxError；
- OOM 每个分配点无泄漏且 kind 正确。

### 13.2 语言集成测试（0.0.39 必测）

- 多文件模块（DAG、循环导入拒绝、可见性、跨模块共享）；
- 函数签名与重名冲突、funcall 实参检查、return 匹配；
- ptr 同型约束、可变性、空指针分类；
- memblock N 比较、深拷贝、越界；
- struct 构造器、双层可变性、嵌套字段；
- 13 阶段确定性顺序；
- 70+ 错误码覆盖。

---

## 14. 实现基线与迁移

### 14.1 v0.0.31 → v0.0.39 关键迁移

| 类别 | v0.0.31 | v0.0.39 |
| ---- | ------- | ------- |
| 模块系统 | 单文件 | `#program`/`#lib`、`import`、`public`/`private`、`Self` |
| 函数 | 无 | `func`/`funcall`/`return`、无环调用图 |
| 类型 | 标量 | 标量 + `ptr<T>` + `memblock<T,N>` + `struct` + `isize`/`usize` + `void` |
| 编译管线 | 6 阶段 | 13 确定性阶段 |
| 错误码 | 41+1 | 70+（含函数、模块、memblock、struct、ptr 专用诊断） |
| REPL | 包含 | 已删除 |

### 14.2 预计迁移顺序

1. types/IR 与错误枚举更新；
2. Lexer 新关键字与 Token；
3. Parser 模块头、函数、类型定义；
4. 模块系统（加载、导入、依赖图）；
5. 作用域、函数级作用域、本库成员索引；
6. 类型系统（memblock / ptr / struct 验证）；
7. 函数签名收集与调用检查；
8. CFG 多域与确定初始化；
9. 常量求值与 `static var` 初始化；
10. Executor（调用帧、memblock 堆存储、ptr 指令）；
11. AOT codegen；
12. CLI/API 与全量验证。

---

*语言合法性与错误种类以 [TC 语言标准 0.0.39](./TC语言标准设计说明书-0.0.39.md) 与 [TC 编译器标准 0.0.39](./TC编译器标准设计说明书-0.0.39.md) 为准；当前可调用行为以 [libtc API](./libtc-api-0.0.39.md) 为准。*
