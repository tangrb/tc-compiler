# libtc 设计说明书

> **规范基线（唯一权威）**：[TC 语言标准 0.0.42](./TC语言标准设计说明书-0.0.42.md) · [TC 编译器标准 0.0.42](./TC编译器标准设计说明书-0.0.42.md)
>
> **当前实现基线**：libtc / TC-VM v0.0.42
>
> **状态**：承载 TC 0.0.42 的 libtc 架构设计，涵盖模块系统、函数、memblock、ptr、struct 与完整 13 阶段编译管线。
>
> **调用者速查**：见 §15「调用者 API 速查」（原 `libtc-api-0.0.42.md` 已并入本文）

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
| 目标语言规范 | 0.0.42 | libtc 最终必须实现的接受集、结果和诊断阶段 |
| 编译器规范 | 0.0.42 | 确定的 13 阶段编译管线、错误码与检查顺序 |
| 本文 | 0.0.42 设计 | 面向当前语言能力的实现设计 |

### 1.2 libtc 的职责

libtc 是 TC 编译器前端与执行器的嵌入入口：

- 从字符串或文件建立完整多模块编译单元；
- 从入口 `#program` 出发，按 `import` 自动加载所有可达 `#lib` 模块；
- 调用 Lexer/Parser 构造每个模块的 `TcModule`；
- 完成模块依赖图 DAG 检查、结构体表注册、函数签名收集（子阶段 4a→4b→4c→结构体表→4d）；
- 完成名称、作用域、类型、控制上下文、完整多域 CFG 与确定初始化（子阶段 6a→6b→6c→6d→6e）；
- 完成 funcall / return 专用检查（阶段 7、8）、`let`/`static let` 求值（阶段 9）、静态布尔判定（阶段 10）、调用图环检查（阶段 12）；
- 成功时返回可被 VM/AOT 消费的 `TcTypedProgram`；
- 执行 typed program 并传播运行时诊断；
- 管理阶段间所有权转移和失败回滚。

### 1.3 非职责

- libtc 不定义语言规范；合法性以 0.0.42 标准为准。
- libtc 不累积多错误；目标采用 fail-fast 单诊断，首个诊断按编译器标准 §1.3 确定性规则选择。
- libtc 不提供 AOT 输出文件或 host C 工具链管理。
- libtc 不含 REPL 模式。

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
typedef struct {
    const char *const *search_paths; /* 会话级 -I 目录；NULL/空 = 无额外路径 */
    size_t search_path_count;
} TcCompileOptions;

int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out, TcDiagnostic *diag);

int tc_compile_file_opts(const char *path, const TcCompileOptions *opts,
                         TcTypedProgram *out, TcDiagnostic *diag);

int tc_run_program(const TcTypedProgram *program,
                   TcDiagnostic *diag);

void tc_typed_program_free(TcTypedProgram *program);
```

0.0.42 编译入口统一为会话式 `tc_compile_source` / `tc_compile_file_opts`（`TcCompileOptions` 携带 -I 搜索路径，无进程级全局）；`tc_run_program` 执行已类型化程序，涵盖模块静态初始化的语义。

### 2.2 源兼容与二进制兼容

函数签名保持源兼容，但 `TcTypedProgram`、`TcDiagnostic` 和错误枚举是公开可见 C 结构，包含函数签名、模块元数据、memblock/ptr/struct 类型参数等扩展字段；嵌入方须按版本重新编译。本文不承诺跨版本的二进制 ABI 兼容。

### 2.3 `tc_compile_source`

成功：返回 0，把完整 `TcTypedProgram` 所有权交给调用方。

失败：返回 -1，设置 `diag`；调用方不取得 `out` 所有权。

### 2.4 `tc_compile_file_opts`

从入口 `#program` 文件出发，自动加载所有可达 `#lib` 模块。模块搜索路径优先级：入口文件所在目录 → `TcCompileOptions` 设置的路径 → 默认路径。成功/失败契约与 `tc_compile_source` 相同。

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
  │    ├─ 结构体表注册（4c 成功后、4d 之前：`tc_struct_table_register_program`）
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
  └─ 阶段 13: VM 执行 / AOT 代码生成
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

### 4.1 0.0.42 静态信息

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
- 结构体类型定义（字段列表、类型、可变性、padding、总宽度；表按模块定界，导入须限定名）；
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
| 文件缓冲 | `tc_compile_file_opts` | libtc | 函数返回前 |
| `TcProgram` / `TcModule` | Parser | Analyzer/typed program | 失败回滚或 `tc_typed_program_free` |
| 符号/作用域/标签 | Analyzer | typed program | `tc_typed_program_free` |
| 函数签名 | Analyzer | typed program | `tc_typed_program_free` |
| CFG（多域） | Analyzer | typed program | `tc_typed_program_free` |
| `let`/`static let` 值 | Analyzer | typed program 内嵌 | 随 typed program |
| runtime slots（含 static var） | Executor | Executor | 每次运行返回前 |
| `TcDiagnostic` 内部字符串 | diagnostic 模块 | 调用方持有的 diag | `tc_diagnostic_clear` |

### 5.2 AST 递归释放

`tc_statement_free` 必须按 kind 释放所有新增的 0.0.42 payload：函数定义的 body 和形参列表；funcall 的目标名和命名实参；return 的操作数；struct 定义的字段列表；memblock 构造器的元素列表；ptr_* 指令的操作数；import 声明；以及字段赋值/读取的字段名。每个新增 kind 必须同步 free 分发；未知 kind 在调试构建中断言，不能静默泄漏。

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

0.0.42 目标明确区分：

| 域 | 例子 | 是否决定 TC 程序合法性 |
| -- | ---- | ----------------------- |
| Language | Syntax、TypeMismatch、UninitializedVariable、运行时 overflow | 是 |
| API/Environment | 文件无法打开/读取、无效调用前置条件 | 否 |
| Implementation | OutOfMemory、内部不变量破坏 | 否 |

### 7.2 语言错误

0.0.42 编译器标准 §11.4 是 `TcErrorKind` 的权威清单（镜像语言标准附录 B **85** 码，另记 `TC_ERR_OUT_OF_MEMORY`）。错误码按分表唯一计数：

- §11.4.1 通用与核心（**44** 语言码，另列 OOM）：语法、名称、类型、字面量、常量、比较、格式、操作数数量、缩进、块结构、控制流，以及 10 个运行时 `TC_RE_*`（空指针两码亦交叉列于 §11.4.6）
- §11.4.2 函数诊断（**18** 码）
- §11.4.3 memblock 诊断（**4** 码）
- §11.4.4 结构体诊断（**8** 码）
- §11.4.5 模块诊断（**10** 码）
- §11.4.6 指针与 memcopy 专用诊断（**2** 码：`MEMCOPY` 静/动各一；空指针两码不另计）
- 运行时 `TC_RE_*` 全集 **12** 码
- 实现码：`TC_ERR_OUT_OF_MEMORY`（合计实现枚举 **86**）

### 7.3 阶段确定性

首个诊断的选择遵循编译器标准 §1.3 的三个原则：阶段优先 → 源位置优先 → 规则优先。各阶段有其专用优先级（如 §4.1 模块错误优先级、§8.2 funcall 错误优先级、§7.6 CFG 错误优先级）。同一源程序经由不同入口编译应产生相同诊断。

---

## 8. Parser 与 Analyzer 集成

### 8.1 批量解析

`tc_compile_source` 和 `tc_compile_file_opts` 始终解析完整程序。`if`/`while`/`func`/`struct` 的缩进和 `end` 必须在 Parser 返回前闭合。Parser 不执行全局类型或可达性判断。

### 8.2 0.0.42 新结构

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
| memcopy_unsafe | 内建调用外壳 | 空指针 / 负长度与负下标检查、memmove 语义 |
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

AOT 通过同一 typed program 读取语句、slot、目标和常量。libtc 不提供 AOT 特化编译入口；`tc-aot` 先调用 `tc_compile_file_opts`，再调用 codegen。

### 9.3 一致性

- 静态接受集完全由 libtc 决定；
- VM/AOT 不各自放宽或收紧；
- 数值和 I/O 使用共享 runtime；
- `let`/`static let` 的位模式由 Analyzer 一次确定；
- 新后端必须消费同一 typed contract。

---

## 10. 多文件模块实现

### 10.1 模块加载流程

0.0.42 的模块加载按编译器标准 §4.1 执行：

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
if (tc_compile_file_opts("input.tc", NULL, &program, &diag) != 0) {
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

### 13.2 语言集成测试（0.0.42 必测）

- 多文件模块（DAG、循环导入拒绝、可见性、跨模块共享）；
- 函数签名与重名冲突、funcall 实参检查、return 匹配；
- ptr 同型约束、可变性、空指针分类；
- memblock N 比较、深拷贝、越界；
- struct 构造器、双层可变性、嵌套字段；
- 13 阶段确定性顺序；
- **86** 错误码覆盖（85 语言码 + `TC_ERR_OUT_OF_MEMORY`）。

---

## 14. 实现基线与迁移

### 14.1 v0.0.31 → v0.0.42 关键迁移

| 类别 | v0.0.31 | v0.0.42 |
| ---- | ------- | ------- |
| 模块系统 | 单文件 | `#program`/`#lib`、`import`、`public`/`private`、`Self` |
| 函数 | 无 | `func`/`funcall`/`return`、无环调用图 |
| 类型 | 标量 | 标量 + `ptr<T>` + `memblock<T,N>` + `struct` + `isize`/`usize` + `void` |
| 编译管线 | 6 阶段 | 13 确定性阶段 |
| 错误码 | 41+1 | **86**（85 语言码 + `TC_ERR_OUT_OF_MEMORY`） |
| REPL | 包含 | 无 |

### 14.2 已完成的迁移顺序（0.0.31 → 0.0.42）

下列步骤均已落地，保留为历史对照，不是待办：

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

本实现固定 64-bit-only（memblock 头部宽 = `sizeof_bits(usize)` = 64 位）。0.0.42 起：memblock/struct 头部/标量元素/字段按固定 LE 位级存取（端序无关，符合 §3.5）；浮点十进制输出为自实现位模式精确渲染（符合 §10.4，无宿主 `snprintf` 委托）。详见 [AOT 详设 §19](./TC-AOT详细设计说明书-0.0.42.md)（债务已清零）。

---

*语言合法性与错误种类以 [TC 语言标准 0.0.42](./TC语言标准设计说明书-0.0.42.md) 与 [TC 编译器标准 0.0.42](./TC编译器标准设计说明书-0.0.42.md) 为准；当前可调用行为以本 §15「调用者 API 速查」为准。*

---

## 15. 调用者 API 速查（0.0.42）

> 本节为调用者速查，原独立文件 `libtc-api-0.0.42.md` 已并入本文以消除 API 双源漂移（见《语言标准符合性检查分析报告》M-15）。函数签名以本节为准；内部架构见 §1–§14。错误码分类计数见 §15.8（P5 已与附录 B 对齐）。

### 15.1. 头文件与链接

```c
#include "tc_lib.h"
```

当前公共头文件：[src/libtc/tc_lib.h](../src/libtc/tc_lib.h)。CMake target 为静态库 `libtc`，并公开 libtc 与 VM runtime/parser/analyzer/executor 的包含目录。

最小 CMake 用法：

```cmake
target_link_libraries(my_program PRIVATE libtc)
```

libtc 以 C99 编译。

---

### 15.2. API 一览

```c
typedef struct {
    const char *const *search_paths; /* 会话级 -I 目录；NULL/空 = 无额外路径 */
    size_t search_path_count;
} TcCompileOptions;

int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out, TcDiagnostic *diag);

int tc_compile_file_opts(const char *path, const TcCompileOptions *opts,
                         TcTypedProgram *out, TcDiagnostic *diag);

int tc_run_program(const TcTypedProgram *program,
                   TcDiagnostic *diag);

void tc_typed_program_free(TcTypedProgram *program);
```

| 函数 | 成功 | 失败 | 所有权 |
| ---- | ---- | ---- | ------ |
| `tc_compile_source` | 返回 0，写入 typed program | 返回 -1，设置 diag | 仅成功时调用方取得 `out` |
| `tc_compile_file_opts` | 返回 0，写入 typed program | 返回 -1，设置 diag | 仅成功时调用方取得 `out` |
| `tc_run_program` | 返回 0 | 返回 -1，设置运行时 diag | 不取得 program |
| `tc_typed_program_free` | 释放并清空 | — | 只对成功编译所得对象调用 |

编译入口为 `tc_compile_source`（无路径源，无搜索路径参数）与 `tc_compile_file_opts`（会话式 `opts` 携带 `-I` 等价路径，可为 NULL = 无额外路径），**无进程级全局状态**——多编译单元 / 多线程嵌入场景各自携带路径，互不污染。

---

### 15.3. `tc_compile_source`

```c
int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out,
                      TcDiagnostic *diag);
```

无路径的内存源仅做结构检查、不解析 import，故无搜索路径参数；文件编译（含 import 解析）见第 4 章 `tc_compile_file_opts`。

#### 当前行为

- 对完整 NUL 结尾 source 执行 13 阶段确定性编译管线：词法（含缩进栈）→ 语法解析（含受限恢复）→ 模块结构与导入解析（4a→4b→4c→4d）→ 函数签名收集 → 名称/作用域/类型语义（6a→6b→6c→6d→6e）→ funcall 检查 → return 检查 → `let`/`static let` 求值 + `static var` 初始化器验证 → 静态布尔三态判定（[语言标准 §5.2.2]）→ CFG 可达性与确定初始化固定点 → 调用图环检查 → 代码生成前完成；
- 支持多文件模块系统：`#program` / `#lib`、`import`、`public`/`private`、`Self`；
- 支持函数定义、`funcall` 调用、命名实参、按值只读形参、`return`、无环调用图；
- 支持 `ptr<T>`、`memblock<T, N>`、`struct`、`isize`/`usize`、`void` 返回等类型；
- 支持 `static var` / `static let` 模块静态成员；
- 完整 CFG（顶层 + 各函数独立域）确定初始化检查在编译阶段完成；
- `diag` 保存 source 副本以生成源码片段，调用返回后可释放原 source。

#### 失败时 `out`

- 任一阶段失败时，`out` 都不被修改；
- libtc 回收本次调用创建的全部临时对象；
- 调用方不读取 `out`，也不调用 `tc_typed_program_free(out)`。

#### 前置条件

- `source`、`name`、`out`、`diag` 为有效指针；
- `diag` 已初始化；
- `out` 当前不持有未释放的 typed program。

---

### 15.4. `tc_compile_file_opts`

```c
int tc_compile_file_opts(const char *path,
                         const TcCompileOptions *opts,
                         TcTypedProgram *out,
                         TcDiagnostic *diag);
```

`opts`（可为 NULL）携带本次编译的 `-I` 等价搜索路径（`TcCompileOptions`，内部借用不复制，仅调用期间须有效）。编译**无进程级全局状态**：同一进程内多个编译单元可各自携带不同的搜索路径，互不污染，亦无线程安全问题。

#### 当前行为

1. 从入口 `#program` 文件出发，按 `import` 语句逐层加载所有可达模块；
2. 在模块搜索路径中唯一定位 `.tc` 文件（入口文件所在目录 → `-I` 路径 → 默认路径）；
3. 检查模块依赖图 DAG（循环导入 → `TC_CE_CIRCULAR_IMPORT`）；
4. 对全部可达模块执行 13 阶段编译管线；
5. 返回前释放内部文件缓冲。

文件打开/读取失败时 `out` 不被修改。文件不存在使用 `TC_DIAG_API / TC_API_ERR_FILE_OPEN`；seek/read/close 失败使用 `TC_DIAG_API / TC_API_ERR_FILE_READ`，不会伪装成语言 `SyntaxError`。

模块搜索路径通过 `opts` 会话选项或 CLI `-I` 选项设置；`opts` 为 NULL 或 `search_paths` 为空时本次编译无额外搜索路径（入口文件所在目录仍最先搜索）。

---

### 15.5. `tc_run_program`

```c
int tc_run_program(const TcTypedProgram *program,
                   TcDiagnostic *diag);
```

- program 必须来自成功的编译；
- 每次调用建立新的运行时 slots（含 `static var` 初始化）；
- program 在执行期间只读；
- 可对同一个 program 重复调用；
- 成功返回 0；除零、溢出、cast、浮点、空指针、memblock 越界或 I/O 运行时错误返回 -1；
- 失败后 program 仍归调用方，必须正常释放。

`tc_run_program` 不打印诊断；调用方决定是否调用 `tc_diagnostic_print`。

---

### 15.6. 诊断生命周期

#### 初始化与释放

```c
TcDiagnostic diag;

tc_diagnostic_init(&diag);
/* compile / run */
tc_diagnostic_clear(&diag);
```

`tc_diagnostic_clear` 释放 message、filename、snippet 和 source，可在已初始化对象上重复调用。

#### 打印

```c
tc_diagnostic_print(&diag, stderr);
```

当前格式：

```text
<file>:<line>:<column>: error: <message>
  <source line>
  <spaces>^
<file>: api error: <ApiCode>: <message>
<file>: implementation error: <ErrorKind>: <message>
```

#### 程序化访问

`TcDiagnostic` 包含：

- `TcDiagnosticDomain domain`；
- `TcApiErrorCode api_code`；
- `TcErrorKind kind`；
- message、filename、snippet、source；
- 1-based line/column，未知列使用 `TC_COLUMN_UNKNOWN`。

当前实现为单槽 fail-fast，只保留第一条错误。Language 使用 `kind`，API/Environment 使用 `api_code`，OOM 使用 Implementation 域和 `TC_ERR_OUT_OF_MEMORY`。

---

### 15.7. Typed program 所有权

#### 成功路径

```text
tc_compile_* succeeds
  → caller owns TcTypedProgram
  → zero or more tc_run_program / AOT reads
  → tc_typed_program_free exactly once
```

#### 所有权表

| 对象 | 所有者 | 释放 |
| ---- | ------ | ---- |
| 输入 source | 调用方 | 调用方；compile 返回后即可释放 |
| `TcDiagnostic` | 调用方 | `tc_diagnostic_clear` |
| 成功的 `TcTypedProgram` | 调用方 | `tc_typed_program_free` |
| program 内 AST/名称/if/while/func 子树 | typed program | 随 `tc_typed_program_free` 递归释放 |
| 符号、标签、常量值、函数签名与 CFG | typed program | 随 `tc_typed_program_free` |
| runtime slots | libtc Executor | 每次运行返回前 |
| `tc_compile_file_opts` 文件缓冲 | libtc | compile 返回前 |

#### 禁止模式

- 编译失败后调用 `tc_typed_program_free`；
- 成功对象不释放或释放两次；
- 在 program 释放后执行；
- 对同一个 out 覆盖编译而未先释放旧对象；
- 多线程无同步地共享并释放同一对象。

---

### 15.8. 当前错误码

0.0.42 语言错误码覆盖编译器标准 §11.4 完整清单（镜像附录 B **85** 码），包括：

- **通用与核心**（§11.4.1：**44** 语言码）：语法、名称、类型、字面量、常量赋值、常量表达式、溢出、除零、转换溢出、比较、格式说明符、操作数数量、缩进、控制流、块结构等；
- **函数诊断**（§11.4.2：**18** 码）：重名、冲突、未定义、参数、实参（含多余实参）、调用位置、返回形式/类型、缺少返回、不可达语句、递归等；
- **memblock 诊断**（§11.4.3：**4** 码）：索引越界（静态+运行时）、构造器实参数量、规划个数不匹配；
- **结构体诊断**（§11.4.4：**8** 码）：字段缺失、未知字段、重复字段、顺序、不可变字段、值自引用、重复定义、未定义；
- **模块诊断**（§11.4.5：**10** 码）：层序错误、缺少可见性、模式误用、导入失败与循环等；
- **指针与 memcopy 专用诊断**（§11.4.6：**2** 码）：memcopy 非法区间（静态+运行时）；空指针解引用/算术列于 §11.4.1，本表交叉列出；
- **运行时错误**（**12** 个 `TC_RE_*` 码）：除零、整数溢出、负移位、浮点异常、转换溢出、I/O、空指针、memblock 越界、memcopy 区间非法（负长度/负下标）。

另含实现码 `TC_ERR_OUT_OF_MEMORY`（合计 **86**）。TC 没有编译警告。

---

### 15.9. 性能计时

设置环境变量 `TC_BENCH` 即可让 libtc 向 stderr 输出阶段耗时：

```bash
TC_BENCH=1 ./my_program
```

当前阶段名：

```text
bench parse: <seconds> s
bench module resolve: <seconds> s
bench analyze: <seconds> s
bench execute: <seconds> s
```

默认不输出。嵌入应用若需要干净 stderr，不设置该变量。

---

### 15.10. 最小示例

```c
#include <stdio.h>

#include "tc_lib.h"

int main(void) {
    const char *source =
        "#program\n"
        "var x: int32 = 7\n"
        "writeln(int32, %d, x)\n";
    TcDiagnostic diag;
    TcTypedProgram program;
    int rc = 0;

    tc_diagnostic_init(&diag);

    if (tc_compile_source(source, "example", &program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (tc_run_program(&program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        rc = 1;
    }

    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
    return rc;
}
```

成功输出：

```text
7
```

---

### 15.11. 当前能力边界

| 能力 | 当前状态 |
| ---- | -------- |
| 多文件模块系统（`#program`/`#lib`、`import`、`public`/`private`、`Self`） | 支持 |
| 函数定义（`func`/`funcall`/`return`、命名实参、按值形参、无环调用图） | 支持 |
| `ptr<T>` 指针及全部 `ptr_*` 指令 | 支持 |
| `memblock<T, N>` 及深拷贝语义 | 支持 |
| `struct` 结构体及双层可变性 | 支持 |
| `isize`/`usize` 平台字长类型 | 支持 |
| `static var` / `static let` 模块静态成员 | 支持 |
| if/else、块作用域、缩进 | 支持 |
| while/break/continue 与范式隔离 | 支持 |
| 函数内受限 goto/label（`while` 内禁止） | 支持 |
| float32/float64、cast、truncate、let 收敛 | 支持 |
| `var` 强制初始化器 | 支持 |
| bitcast | 支持 |
| 完整 CFG 固定点（多域：顶层 + 各函数独立） | 支持 |
| 13 阶段确定性编译管线 | 支持 |
| success-only ownership 与诊断分域 | 支持 |
| 85 语言错误码 + OutOfMemory 完整表 | 支持 |

---

### 15.12. API 契约

#### 函数签名

- `tc_compile_source(source, name, out, diag)`：编译无路径内存源，无搜索路径参数；
- `tc_compile_file_opts(path, opts, out, diag)`：编译文件，`opts` 可为 NULL 携带 `TcCompileOptions` 搜索路径；
- `tc_run_program(program, diag)`：执行已类型化程序，行为涵盖模块静态初始化。

#### 数据结构

`TcTypedProgram`、`TcDiagnostic`、`TcErrorKind` 和语句/RHS kind 枚举为公共类型；`TcTypedProgram`、`TcDiagnostic` 和枚举值均随版本演进，嵌入方不得直接依赖结构字段布局或具体错误枚举取值，应按公开函数访问。

---

*当前调用契约以 [src/libtc/tc_lib.h](../src/libtc/tc_lib.h) 和本页为准；语言规则以 [TC 0.0.42 标准](./TC语言标准设计说明书-0.0.42.md) 与 [TC 编译器标准 0.0.42](./TC编译器标准设计说明书-0.0.42.md) 为准。*
