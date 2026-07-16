# libtc 设计说明书

> **规范基线**：[TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md)
>
> **当前实现基线**：libtc / TC-VM v0.0.31
>
> **状态**：承载 TC 0.0.31 的 libtc 架构与公共契约已实现并通过所有权、OOM 与平台内存门禁。
>
> **调用者速查**：[libtc API](./libtc-api.md)

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
10. [性能与并发](#10-性能与并发)
11. [构建与嵌入](#11-构建与嵌入)
12. [验证](#12-验证)
13. [实现基线与迁移](#13-实现基线与迁移)

---

## 1. 边界与目标

### 1.1 版本基线

| 维度 | 版本 | 说明 |
| ---- | ---- | ---- |
| 目标语言规范 | 0.0.31 | libtc 最终必须实现的接受集、结果和诊断阶段 |
| 当前库实现 | v0.0.31 | 当前头文件、结构和运行行为 |
| 本文 | 0.0.31 已实现设计 | 当前编译门面与所有权契约 |

### 1.2 libtc 的职责

libtc 是 TC 编译器前端与执行器的嵌入入口：

- 从字符串或文件建立完整编译单元；
- 调用 Lexer/Parser 构造 `TcProgram`；
- 完成名称、类型、控制上下文、完整 CFG 与确定初始化；
- 成功时返回可被 VM/AOT 消费的 `TcTypedProgram`；
- 执行 typed program 并传播运行时诊断；
- 管理阶段间所有权转移和失败回滚。

### 1.3 非职责

- libtc 不定义语言规范；合法性以 0.0.31 标准为准。
- libtc 不把 REPL 的逐行限制应用到批量编译。
- libtc 不提供 AOT 输出文件或 host C 工具链管理。
- libtc 不累积多错误；目标仍采用 fail-fast 单诊断。

### 1.4 设计原则

1. **成功才转移所有权**：失败不把半构造对象交给调用方。
2. **静态完成后才执行**：`tc_run_typed` 不补做 Parser/Analyzer 检查。
3. **一个 typed program，多后端**：VM 与 AOT 消费同一静态结果。
4. **错误域分离**：语言错误、API/文件错误和 OOM 不互相冒充。
5. **公共入口稳定**：0.0.31 目标不要求新增编译或执行函数。

---

## 2. 公共 API

### 2.1 目标保持的函数签名

```c
int tc_compile_source(const char *source,
                      TcTypedProgram *out,
                      TcDiagnostic *diag);

int tc_compile_file(const char *path,
                    TcTypedProgram *out,
                    TcDiagnostic *diag);

int tc_run_typed(const TcTypedProgram *program,
                 TcDiagnostic *diag);

void tc_typed_program_free(TcTypedProgram *program);
```

前三个入口声明在 `src/libtc/tc_lib.h`；释放函数由共享 types 接口提供。0.0.31 新语法和语义通过内部结构扩展，不要求调用者选择另一套 API。

### 2.2 源兼容与二进制兼容

函数签名目标上保持源兼容，但 `TcTypedProgram`、`TcDiagnostic` 和错误枚举是公开可见 C 结构；0.0.31 扩展这些结构时，嵌入方必须重新编译。本文不承诺跨 v0.0.26/0.0.31 的二进制 ABI 兼容。

### 2.3 `tc_compile_source`

前置条件：

- `source` 指向 NUL 结尾字符串；
- `out` 与 `diag` 非空；
- `diag` 已由 `tc_diagnostic_init` 初始化；
- `out` 不持有尚未释放的 typed program。

成功：返回 0，把完整 `TcTypedProgram` 所有权交给调用方。

失败：返回 -1，设置 `diag`；调用方不取得 `out` 所有权，不调用 `tc_typed_program_free(out)`。实现可以保持 `out` 原值或清空它，但不得留下要求调用方猜测是否释放的部分所有权。

source 内容在编译期间只读。诊断若需要保留 source/snippet，libtc 复制所需文本；函数返回后调用方可释放 source。

### 2.4 `tc_compile_file`

`tc_compile_file` 读取整个文件并复用与 `tc_compile_source` 相同的批量流水线。成功与 typed program 所有权契约完全相同。文件打开、定位、读取失败属于 API/环境错误，不是 TC 源码 `SyntaxError`。

### 2.5 `tc_run_typed`

- 输入必须来自成功编译且尚未释放；
- program 在执行期间只读，可在执行后再次运行；
- 成功返回 0；运行时算术、cast、浮点或 I/O 错误返回 -1；
- 执行不取得 program 所有权；
- 运行失败后仍由调用方释放 program。

重复执行时每次建立新的 runtime slots 和执行上下文，不复用上一次的变量值。

---

## 3. 目标编译流水线

### 3.1 阶段图

```text
source bytes
  │
  ├─ source/filename capture
  ├─ Lex + Parse
  │    └─ TcProgram
  ├─ Bind + Scope + Slot allocation
  ├─ Type + Mode + Statement checks
  ├─ Control-context and label resolution
  ├─ Complete CFG construction
  ├─ Reachability + definite initialization fixed point
  ├─ let constant evaluation / canonical values
  │
  └─ TcTypedProgram
       ├─ tc_run_typed → Executor
       └─ tc_aot_emit_c → AOT
```

### 3.2 编译事务

目标实现用局部临时对象承载每一阶段：

```text
init temporary program/typed state
parse
analyze
if success:
    move complete typed state to *out
else:
    free all temporary state
```

这样可以实现“成功才转移所有权”，避免 Parse 与 Analyze 失败产生两套调用者规则。

### 3.3 Parser 输出

`TcProgram` 保留：

- 树形 if/while 子块；
- break/continue/goto/label 节点；
- var/let/赋值/I/O；
- RHS 和 operand；
- 行、列、缩进相关源位置；
- 尚未解析的标识符文本。

Parser 直接拒绝缺初始化器的 `var`，使用 `VarMissingInitializer`。其它名称、类型和控制上下文留给 Analyzer。

### 3.4 Analyzer 输出

Analyzer 成功意味着：

- 每个名称绑定确定；
- 每个 `var` 有固定 slot；
- 每个 RHS 的类型和模式确定；
- `let` 有规范化 `TcValue`；
- goto、label、break、continue 的目标确定；
- 完整 CFG 与可达性确定；
- 所有可达使用满足确定初始化；
- 不存在任何静态错误。

---

## 4. `TcTypedProgram` 数据契约

### 4.1 v0.0.31 当前形态

```c
typedef struct {
    TcProgram program;
    TcSymbolTable symbols;
    TcCfg *cfg;
    TcWarningList warnings;
} TcTypedProgram;
```

该结构承载语句树、符号/标签和空警告预留。

### 4.2 0.0.31 静态信息

目标 typed program 还必须直接保存或能够无歧义重建：

- 完整作用域树与 block path；
- statement → scope 映射；
- binding → slot 映射；
- label/goto 解析目标；
- break/continue → loop id；
- 每个 RHS 的源/目标类型与合法模式；
- 完整 CFG、可达性或等价的稳定只读表示；
- `let` 的目标精度位模式；
- 源位置映射。

当前形态：

```c
typedef struct {
    TcProgram program;
    TcSymbolTable symbols;
    TcCfg *cfg;
    TcWarningList warnings; /* 兼容空壳；0.0.31 无语言警告 */
} TcTypedProgram;
```

`TcCfg` 使用前置声明和只读指针保存在公开结构中，其节点细节保持内部；`tc_typed_program_free` 统一释放。Executor/AOT 不各自重建语义不同的 CFG。

### 4.3 不变量

成功的 typed program 满足：

- 所有指针字段要么有效、要么为可释放的 NULL；
- count/capacity/items 三元组一致；
- statement/RHS kind 全部属于已实现集合；
- slot 在 `[0, variable_slot_count)`；
- `let` 不占 runtime slot；
- CFG 目标引用有效节点；
- 不存在未解析名称或待定类型；
- warnings 为空。

### 4.4 可重复消费

Executor 和 AOT 只读 typed program。一个成功对象可以：

1. 执行零次或多次；
2. 生成 C 零次或多次；
3. 在消费完成后释放一次。

任何消费者不得修改共享 symbol、const value 或 CFG。

---

## 5. 所有权与生命周期

### 5.1 总表

| 对象 | 创建者 | 成功后的所有者 | 释放者 |
| ---- | ------ | -------------- | ------ |
| 调用方 source | 调用方 | 调用方 | 调用方 |
| 文件缓冲 | `tc_compile_file` | libtc | `tc_compile_file` 返回前 |
| `TcProgram` | Parser | Analyzer/typed program | 失败回滚或 `tc_typed_program_free` |
| 符号/作用域/标签 | Analyzer | typed program | `tc_typed_program_free` |
| CFG | Analyzer | typed program | `tc_typed_program_free` |
| `let` 值 | Analyzer | typed program 内嵌 | 随 typed program |
| runtime slots | Executor | Executor | 每次运行返回前 |
| `TcDiagnostic` 内部字符串 | diagnostic 模块 | 调用方持有的 diag | `tc_diagnostic_clear` |

### 5.2 AST 递归释放

`tc_statement_free` 必须按 kind 释放：

- var/let/assign 的名称与 RHS；
- I/O operand 名称；
- if then/else 数组；
- while body 数组；
- label/goto 字符串；
- bitcast/cast operand 中的名称。

每个新增 kind 必须同步 free 分发；未知 kind 在调试构建中断言，不能静默泄漏。

### 5.3 CFG 所有权

CFG 的 nodes、edges、前驱/后继数组、bitset 和 source mapping 使用单一所有权根。构建过程中的临时工作队列由 Analyzer 自身释放，不进入 typed program。

### 5.4 诊断 source

`tc_diagnostic_set_source` 复制 filename/source 所需内容。新的错误覆盖旧单槽时，先释放旧 message/snippet，再保存新内容；不得保留指向 `tc_compile_file` 临时缓冲的悬空指针。

---

## 6. 失败、回滚与 OOM

### 6.1 阶段回滚

| 失败点 | 必须释放 |
| ------ | -------- |
| source capture | 已复制 filename/source |
| Lexer/Parser | tokens、当前语句、已完成语句树、块栈 |
| Binder/Type | program、symbols、scope/label 路径、临时绑定 |
| CFG | 全部已建 nodes/edges/bitsets、program、symbols |
| Const eval | 临时值/名称、program、symbols/CFG |
| Executor | runtime slots、执行上下文；typed program 仍归调用方 |

### 6.2 统一返回

- 0：成功，输出契约成立；
- -1：失败，diag 有可打印信息；
- 不使用正数编码阶段；具体类别由诊断域和 kind 表达。

### 6.3 OOM

所有 `malloc/calloc/realloc/strdup` 失败：

```text
domain  = implementation
kind    = TC_ERR_OUT_OF_MEMORY
message = memory allocation failed
return  = -1
```

OOM 不得降级为 `SyntaxError`。`realloc` 采用临时指针，失败时保留旧块以便统一回滚。

### 6.4 输出对象

推荐实现只在全部成功后赋值 `*out`。在任意失败路径：

- 调用方不读取 `out`；
- 调用方不释放 `out`；
- libtc 保证已释放本次调用创建的所有对象。

这个调用者规则消除了早期 Parse/Analyze 失败差异，为 0.0.31 提供单一稳定契约。

---

## 7. 诊断契约

### 7.1 三个诊断域

0.0.31 目标明确区分：

| 域 | 例子 | 是否决定 TC 程序合法性 |
| -- | ---- | ----------------------- |
| Language | Syntax、TypeMismatch、UninitializedVariable、运行时 overflow | 是 |
| API/Environment | 文件无法打开/读取、无效调用前置条件 | 否 |
| Implementation | OutOfMemory、内部不变量破坏 | 否 |

文件打开失败不得继续使用 `TC_ERR_SYNTAX`。目标 `TcDiagnostic` 应增加 domain/origin，并为 API 文件错误提供独立 code；函数签名无需变化。

### 7.2 语言错误

0.0.31 标准 §11.4 是 `TcErrorKind` 的权威清单：41 个语言错误码加 `OutOfMemory` 实现扩展。关键迁移包括：

- 新增/恢复 VarMissingInitializer、UninitializedVariable、BitcastWidth；
- 新增循环/范式隔离错误；
- 删除 ConstantCircular、CrossBlockReference、OverflowMode、FloatCastOverflow、GotoSkipsVarInit；
- 模式和 cast 错误统一。

### 7.3 阶段确定性

同一源程序经 `tc_compile_source` 与 `tc_compile_file` 应在相同语言阶段返回相同 language kind。文件 API 仅在读取源码前增加 API/Environment 失败可能。

### 7.4 单槽

libtc 继续只报告第一条错误。阶段排序必须固定，避免实现遍历顺序改变可观察 kind。建议顺序：形态/语法 → 名称 → 类型/模式 → 控制合法性 → CFG 初始化。

### 7.5 打印与程序化访问

- `tc_diagnostic_print` 根据 domain 打印适当前缀和源片段；
- `tc_error_kind_name()` 只字符串化语言/实现错误枚举；
- API 文件错误使用 API code 的独立 name；
- line/column 不适用于文件打开时可为 0/unknown，但不得伪造源码位置。

---

## 8. Parser 与 Analyzer 集成

### 8.1 批量解析

`tc_compile_source` 始终解析完整 source。`if`/`while` 的缩进和 `end` 必须在 Parser 返回前闭合。REPL 的逐行 Parser 不是该入口的替代实现。

### 8.2 0.0.31 新结构

Parser/Analyzer 的目标扩展：

| 能力 | Parser | Analyzer |
| ---- | ------ | -------- |
| 强制 var RHS | 形态与专用错误 | RHS 类型 |
| while | 块与 condition AST | bool、scope、CFG |
| break/continue | statement AST | 最内层 loop 绑定 |
| goto/label 隔离 | statement AST | while 祖先检查 |
| bitcast | RHS AST | 非 bool、等位宽 |
| let 单层表达式 | 语法形态 | 来源、类型、逐步语义 |

### 8.3 完整 CFG

libtc 的 Analyze 阶段必须在成功返回前完成完整 CFG 固定点。AOT `--check`、VM 文件模式和嵌入 API 都走同一检查，不能由调用方选择跳过。

### 8.4 常量与可达性

合法 `let` 条件可剪除 if/while 的不可能边，但不可达语句仍做语法、名称和类型检查。该顺序由 Analyzer 内部保证，libtc 只暴露最终结果。

---

## 9. Executor 与 AOT 消费边界

### 9.1 Executor

`tc_run_typed`：

```text
allocate fresh slots/context
execute typed statements
on runtime error: set diag, free runtime state, return -1
on success: free runtime state, return 0
```

typed program 不被修改。完整执行设计见 [TC-VM 详细设计说明书](./TC-VM详细设计说明书.md)。

### 9.2 AOT

AOT 通过同一 public/internal typed program 读取语句、slot、目标和常量。libtc 不提供 AOT 特化编译入口；`tc-aot` 先调用 `tc_compile_file`，再调用 codegen。详见 [TC-AOT 详细设计说明书](./TC-AOT详细设计说明书.md)。

### 9.3 一致性

- 静态接受集完全由 libtc 决定；
- VM/AOT 不各自放宽或收紧；
- 数值和 I/O 使用共享 runtime；
- `let` 的位模式由 Analyzer 一次确定；
- 新后端必须消费同一 typed contract。

### 9.4 REPL

REPL 增量分析是 driver 侧的独立实现能力。它可以拒绝多行控制流，但不得影响 `tc_compile_source` 的批量语义。未来多行 REPL 应先缓冲完整单元再调用 libtc。

---

## 10. 性能与并发

### 10.1 性能阶段

当前 `TC_BENCH=1` 输出 parse、analyze、execute 时间。0.0.31 目标可把 analyze 细分为：

```text
bench parse
bench bind-type
bench cfg
bench dataflow
bench execute
```

默认不输出性能行；bench 输出到 stderr，不改变程序 stdout。

### 10.2 复杂度目标

- Lex/Parse：O(source bytes + statements)；
- binding：平均 O(statements)；
- CFG build：O(nodes + edges)；
- bitset dataflow：O(iterations × edges × bitset words)；
- execution：O(dynamic statements)。

循环固定点必须有单调传递函数和有限终止，不使用无界路径枚举。

### 10.3 可重入

除环境变量读取和标准 I/O 外，编译状态都在调用栈/对象中，不使用可变全局编译器状态。不同线程可使用各自 `TcDiagnostic`、`TcTypedProgram` 和输入并发编译；同一对象不得无同步并发修改/释放。

### 10.4 复用

同一 typed program 可重复执行或 AOT 发射。若未来缓存 CFG/后端临时信息，缓存必须只读或由调用方显式同步，不能破坏当前 const 输入契约。

---

## 11. 构建与嵌入

### 11.1 头文件

调用者包含：

```c
#include "tc_lib.h"
```

`tc_lib.h` 当前传递包含 Analyzer、Diagnostic、Executor 和 Types 头。0.0.31 可通过前置声明减少暴露，但不能让公共类型不完整到无法按当前模式栈分配。

### 11.2 链接

libtc 由 CMake 生成并链接 Lexer、Parser、Analyzer、Executor、runtime semantics 和 I/O。新增 CFG 模块必须进入相同静态库，不要求嵌入方单独链接内部对象。

### 11.3 版本

libtc 没有独立公开版本函数，随 TC-VM 0.0.31 版本管理。本轮未新增运行时版本查询 API。

### 11.4 最小调用模式

```c
TcDiagnostic diag;
TcTypedProgram program;

tc_diagnostic_init(&diag);
if (tc_compile_file("input.tc", &program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    tc_diagnostic_clear(&diag);
    return 1;
}

if (tc_run_typed(&program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
}

tc_typed_program_free(&program);
tc_diagnostic_clear(&diag);
```

0.0.31 无语言警告，调用者不需要警告打印步骤。

---

## 12. 验证

### 12.1 API 契约测试

- compile source/file 成功只转移一次所有权；
- 每个失败阶段不要求调用方释放 out；
- source 在 compile 返回后可释放；
- typed program 可重复运行；
- runtime 失败后 program 仍可释放；
- diagnostic 可 clear 并复用；
- 文件/API 错误不返回 SyntaxError；
- OOM 每个分配点无泄漏且 kind 正确。

### 12.2 结构所有权测试

- 深层 if/while 树递归释放；
- break/continue/label/goto/bitcast payload 释放；
- CFG 构建中途失败释放 nodes/edges/bitsets；
- Analyze 成功后原始 program 不被双重释放；
- AOT/Executor 只读消费。

### 12.3 语言集成测试

- while/loop control；
- goto 范式隔离；
- 完整 CFG 确定初始化；
- var missing initializer；
- strict cast/truncate/bitcast；
- float strict/ieee 与 let 精度；
- 42 项错误码/扩展映射。

### 12.4 工具

正常 unit/VM/AOT、ASan、UBSan 与 no-fenv 均已通过；macOS MallocScribble 全矩阵通过，`test-libtc` 在系统 `leaks` 下为 0 leaks / 0 bytes。

| 契约 | 实现链接 | 测试链接 |
| ---- | -------- | -------- |
| success-only ownership 与阶段回滚 | [tc_lib.c](../src/libtc/tc_lib.c) | [test_libtc.c](../tests/unit/runtime/test_libtc.c) |
| typed program + CFG 生命周期 | [tc_analyzer.c](../src/vm/analyzer/tc_analyzer.c)、[tc_cfg.c](../src/vm/analyzer/tc_cfg.c) | [test_cfg.c](../tests/unit/runtime/test_cfg.c)、[test_libtc.c](../tests/unit/runtime/test_libtc.c) |
| 诊断域、OOM 与免分配后备 | [tc_diagnostic.c](../src/vm/runtime/tc_diagnostic.c) | [test_diagnostic.c](../tests/unit/runtime/test_diagnostic.c) |
| 重复 VM/AOT 消费 | [tc_executor.c](../src/vm/executor/tc_executor.c)、[tc_aot_codegen.c](../src/aot/tc_aot_codegen.c) | `test_libtc.c` 重复执行/发射用例 |
| REPL 事务边界 | [tc_repl.c](../src/vm/driver/tc_repl.c)、[tc_analyzer_repl.c](../src/vm/analyzer/tc_analyzer_repl.c) | [repl_extended_scenarios.txt](../tests/valid/repl_extended_scenarios.txt) |

---

## 13. 实现基线与迁移

### 13.1 v0.0.31 当前事实

- 公共入口为 `tc_compile_source`、`tc_compile_file`、`tc_run_typed`。
- Parse → Analyze 成功后返回 program/symbols/CFG/空 warnings。
- `tc_run_typed` 委托 `tc_execute`。
- 文件打开/读取与无效参数使用 API 诊断域，不冒充语言错误。
- typed program 包含 while/break/continue/bitcast 元数据和显式完整 CFG。
- warning list 是兼容空壳；0.0.31 无语言警告。

### 13.2 已完成的 0.0.31 迁移

1. 扩展共享 types、statement/RHS kind 与释放分发；
2. 把 Parser 与 Analyzer 的结果构建改为成功才转移所有权；
3. 加入通用块、循环与 bitcast；
4. 建立作用域/目标元数据和完整 CFG；
5. 收敛诊断枚举并引入 API/Environment 诊断域；
6. 更新 Executor/AOT 消费；
7. 增加 OOM、所有权、API、语言和差分测试；
8. 全部门禁通过后把实现版本升至 0.0.31。

### 13.3 兼容结论

0.0.31 目标不要求调用者改用新函数，但错误枚举、公开结构和接受程序集变化，因此嵌入方需要重新编译并审查对具体错误 kind 或结构字段的依赖。

---

*语言合法性与错误种类以 [TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md) 为准；当前可调用行为以 [libtc API](./libtc-api.md) 为准。*
