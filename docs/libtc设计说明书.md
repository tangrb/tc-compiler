# libtc 设计说明书

> **版本**：0.0.25（草案）  
> **实现状态**：**规范与代码 v0.0.25**（`if` 控制流与块级作用域 v0.0.24 已交付；浮点 `float32`/`float64` 全链路 v0.0.25 已交付；见 §12）  
> **依赖**：[TC语言标准设计说明书.md](./TC语言标准设计说明书.md) v0.0.25  
> **关联**：[TC-VM详细设计说明书.md](./TC-VM详细设计说明书.md) · [TC-AOT详细设计说明书.md](./TC-AOT详细设计说明书.md)  
> **工程**：[TC-Compiler](../README.md) 之 `src/libtc/` 组件  
> **定位**：TC 编译器的嵌入式接口库——将「编译（Parse + Analyze）」与「执行」分离

---

## 目录

1. [设计目标与原则](#1-设计目标与原则)
2. [总体架构](#2-总体架构)
3. [API 设计](#3-api-设计)
4. [内部调用链](#4-内部调用链)
5. [错误状态契约](#5-错误状态契约)
6. [内存所有权约定](#6-内存所有权约定)
7. [编译流水线与解析模型](#7-编译流水线与解析模型)
8. [TcTypedProgram 数据契约](#8-tctypedprogram-数据契约)
9. [性能分析支持](#9-性能分析支持)
10. [构建集成](#10-构建集成)
11. [示例](#11-示例)
12. [与 VM / AOT / REPL 的关系](#12-与-vm--aot--repl-的关系)
13. [v0.0.24 变更影响（libtc 层）](#13-v0024-变更影响libtc-层)
14. [v0.0.25 变更影响（libtc 层）](#14-v0025-变更影响libtc-层)

附录

- [附录 A：API 签名速查](#附录-aapi-签名速查)
- [附录 B：文档修订记录](#附录-b文档修订记录)

---

## 1. 设计目标与原则

### 1.1 目标

libtc 是 TC 编译器的静态库，提供以下能力：

- **嵌入友好**：其他 C 程序可调用 libtc 编译并执行 TC 源码，无需启动子进程
- **编译与执行分离**：调用方可先编译（`tc_compile_source`），再独立执行（`tc_run_typed`）
- **与 CLI 共享**：`tc-vm`（经 `tc_driver`）和 `tc-aot` 的 CLI 入口都通过 libtc 完成编译，不重复实现解析流水线
- **单一编译前端**：VM 解释执行与 AOT 代码生成消费**同一份** `TcTypedProgram`，保证语义一致

### 1.2 核心原则

| 原则 | 说明 |
| ---- | ---- |
| **薄封装** | libtc 是 VM 各模块（lexer/parser/analyzer/executor）的**编排层**，本身不含运算/I/O 语义 |
| **fail-fast** | 词法/语法/分析阶段遇首错即停，不累积错误（`TcDiagnostic` 单槽） |
| **所有权明确** | 成功时调用方拥有 `TcTypedProgram`；失败时 libtc 内部处理清理 |
| **零外部依赖** | 仅依赖 POSIX 标准 API（`fopen`、`clock_gettime` 等） |
| **解析入口集中** | 全文件解析逻辑位于 `tc_lib.c` 的 `tc_parse_source()`，便于 v0.0.24 扩展多行 `if` |

### 1.3 实现版本

| 项目 | 版本 |
|------|------|
| 本文档 / 语言标准 | 0.0.25（草案） |
| 当前 `tc-vm`（`TC_VM_VERSION`） | 0.0.25 |
| v0.0.24 交付 | `tc_parse_source` 两遍扫描 + 树形 `TcIfStmt`；Analyzer 递归 Pass1/Pass2 |
| v0.0.25 交付 | 浮点类型 `float32`/`float64` 全链路——词法/语法/分析/执行/AOT 代码生成；`tc_types.h` 扩展浮点枚举及相关 RHS kind/shim 函数；`tc_semantics.c`/`tc_io.c` 扩展浮点算术/比较/cast/I/O |

---

## 2. 总体架构

```text
调用方 C 程序 / tc-vm driver / tc-aot
    │
    ├─ tc_compile_source / tc_compile_file
    │      │  Parse:  tc_parse_source()     ← libtc/tc_lib.c（编排入口）
    │      │           ├─ [v0.0.23] 逐行 tokenize + tc_parse_statement
    │      │           └─ [v0.0.24] 两遍行扫描 + tc_parse_if_stmt（缩进敏感）
    │      │  Analyze: tc_analyze()
    │      │           ├─ tc_pass1_collect_stmt()   [v0.0.24 递归]
    │      │           └─ tc_pass2_check_stmt()     [v0.0.24 递归]
    │      ▼
    │      TcTypedProgram { program, symbols, warnings }
    │
    ├─ tc_run_typed
    │      │  Execute: tc_execute() → tc_execute_statement() [含 if 分支递归]
    │      ▼
    │      stdout / stderr / runtime errors
    │
    └─ tc_typed_program_free
          释放语句树、符号表、警告（含嵌套 if body）
```

### 2.1 文件结构

| 文件 | 职责 |
|------|------|
| `tc_lib.h` | 对外 API 声明（`tc_compile_*`、`tc_run_typed`） |
| `tc_lib.c` | **全文件解析编排**（`tc_parse_source`）、文件读取、`TC_BENCH` 计时 |

> **边界**：`tc_parse_source` 为 `static`，不对外导出；嵌入方仅通过 `tc_compile_*` 间接使用。

### 2.2 编译时依赖

libtc 是静态库，链接 VM 各模块源文件：

```text
src/libtc/
└── tc_lib.c              ← 编排：parse 入口 + compile/run API

src/vm/runtime/           ← 类型、诊断、符号、语义、I/O
├── tc_types.c
├── tc_diagnostic.c
├── tc_symbol.c
├── tc_warning.c
├── tc_semantics.c
└── tc_io.c

src/vm/lexer/tc_lexer.c
src/vm/parser/tc_parser.c
src/vm/analyzer/
├── tc_analyzer.c
└── tc_const_eval.c
src/vm/executor/tc_executor.c
```

---

## 3. API 设计

### 3.1 编译接口

```c
int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag);
int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag);
```

**输入输出**：

| 参数 | 方向 | 说明 |
|------|------|------|
| `source` / `path` | 输入 | 源文本或文件路径；`source` 须在 `diag` 打印前保持有效 |
| `out` | 输出 | 成功时写入已通过静态分析的 `TcTypedProgram` |
| `diag` | 输出 | 失败时写入诊断信息（首错单槽） |

**返回值**：`0` 成功；`-1` 失败（`diag` 已设置）。

**v0.0.24 新增静态错误**（经 Analyze 报出，与语言标准 §11.1 一致）：缩进类 4 种、`TC_ERR_CONDITION_TYPE`、`TC_ERR_CROSS_BLOCK_REFERENCE`（或与 `TC_ERR_UNDEFINED_VARIABLE` 统一，见 VM 详设 §7.3）等。

**v0.0.25 新增静态/运行时错误**（经 Analyze 或 `tc_run_typed` 报出）：`TC_ERR_FLOAT_OVERFLOW`、`TC_ERR_FLOAT_UNDERFLOW`、`TC_ERR_FLOAT_INVALID`、`TC_ERR_FLOAT_CAST_OVERFLOW`、`TC_ERR_MODE_MISMATCH`（见语言标准 §11.4、VM 详设 §11.3）。libtc API 不变，仍经 `diag` 单槽返回。

### 3.2 执行接口

```c
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);
```

**约束**：`program` 必须已通过 `tc_compile_*` 且返回值 `0`。

**返回值**：`0` 成功；`-1` 运行时错误（除零、溢出、I/O 等）。

**副作用**：`write`/`writeln` 输出到 stdout；`read` 从 stdin 读取。

**v0.0.24**：`tc_execute` 对顶层 `TC_STMT_IF` 在运行时分支，块内语句递归 `tc_execute_statement`；PC 仅递增顶层语句（每个 `if` 占一条顶层 PC）。

### 3.3 释放接口

```c
void tc_typed_program_free(TcTypedProgram *program);   /* 声明于 tc_analyzer.h */
```

由 Analyzer 模块实现，libtc 调用方通过 `tc_analyzer.h` 或 `tc_lib.h`（间接 include）使用。

释放 `TcTypedProgram` 内所有堆内存，含嵌套 `TcIfStmt` 的 `then_body`/`else_body`（经 `tc_statement_free` 递归）。释放后各子字段指针置 NULL；对空状态再次调用安全。

### 3.4 接口设计决策

**为什么传递裸指针而非结构体**：`TcDiagnostic` 和 `TcTypedProgram` 由调用方在栈上分配，libtc 内部填充；避免堆分配所有权模糊。

**为什么编译和执行分离**：允许调用方在执行前检查警告列表；支持 `tc-vm --check`（仅编译不执行）；AOT 仅需 `tc_compile_file` 无需 `tc_run_typed`。

**为什么 `tc_parse_source` 留在 libtc 而非 parser 模块**：parser 提供单行/块级原语（`tc_parse_statement`、`tc_parse_if_stmt`）；**全文件行扫描与两遍调度**属于编译驱动职责，与 `tc_driver` 解耦。

---

## 4. 内部调用链

### 4.1 tc_compile_source

```text
tc_compile_source(source, out, diag)
    │
    ├─ diag->source = source
    │
    ├─ tc_parse_source(source, &program, diag)    /* static，tc_lib.c */
    │     [v0.0.24+ 现状] 两遍扫描：
    │       第一遍: 切行 + tokenize + 记录行缩进 → line_tokens[]
    │       第二遍: 遇 TC_TOK_IF → tc_parse_if_stmt；否则 parse_statement
    │
    └─ tc_analyze(&program, out, diag)
           ├─ tc_pass1_collect_symbols / collect_stmt   [递归 if 块]
           └─ tc_pass2_type_check / check_stmt         [递归 if 块]
                └─ tc_resolve_const_value → let 编译期求值（含浮点 ConstFloat*，§7.4.1）
```

### 4.2 tc_compile_file

```text
tc_compile_file(path, out, diag)
    ├─ tc_diagnostic_set_source(diag, path, NULL)
    ├─ tc_read_file(path)              → malloc 缓冲区
    ├─ tc_compile_source(source, out, diag)
    └─ free(source)                    → 文件缓冲由 libtc 释放
```

### 4.3 tc_run_typed

```text
tc_run_typed(program, diag)
    └─ tc_execute(program, diag)
         for each top-level stmt:
              tc_execute_statement()
                   ├─ VarDef/ConstDef/Assign/IO …
                   └─ [v0.0.24] TC_STMT_IF:
                        eval condition → 递归执行 then_body 或 else_body
```

### 4.4 与 REPL 的路径分叉

REPL（`tc_repl.c`）**不经过** libtc 的 `tc_compile_source`：

```text
tc_repl:  tokenize_line → parse_statement → tc_analyze_statement → tc_execute_statement
```

v0.0.24 起 REPL **显式拒绝** `TC_STMT_IF`（见 VM 详设 §18.8）；含控制流的程序须经 `tc_compile_file` / `tc_compile_source`（文件模式）。

---

## 5. 错误状态契约

`tc_compile_source` 和 `tc_compile_file` 失败时，`out` 的状态取决于失败阶段：

### 5.1 Parse 失败（词法/语法/缩进/OOM）

> **v0.0.24-rev2**：OOM 错误不再归类为 `TC_ERR_SYNTAX`，而是使用独立的 `TC_ERR_OUT_OF_MEMORY`（见 `tc_types.h`）。

- `out` **不会被修改**
- 调用方不得读取 `out`，也**无需**调用 `tc_typed_program_free`
- `tc_parse_source` 内部已 `tc_program_free` 清理中间 `TcProgram`（含已解析的部分 if 子树）

### 5.2 Analyze 失败（静态分析错误）

- `tc_analyze` 内部已调用 `tc_typed_program_free(out)`
- `out` 处于**空状态**（`count` 为 0、指针为 NULL）
- 调用方**无需**再次释放；再次调用 `tc_typed_program_free` 安全（no-op）

### 5.3 文件 I/O 失败

- `tc_compile_file` 在打开/读取文件阶段失败时，`out` 不会被修改
- 行为同 Parse 失败

### 5.4 成功

- 调用方拥有 `TcTypedProgram`，必须调用 `tc_typed_program_free` 释放

### 5.5 执行阶段失败

- `tc_run_typed` 失败不改变 `program` 内容；调用方仍可 `tc_typed_program_free`
- 已执行的语句副作用保留（与 VM fail-fast 一致）

---

## 6. 内存所有权约定

| 对象 | 分配方 | 释放方 |
|------|--------|--------|
| `TcDiagnostic` | 调用方栈/堆 | `tc_diagnostic_clear`（可重复使用） |
| `TcTypedProgram` | `tc_compile_*` 成功时 | `tc_typed_program_free` |
| `TcProgram.items[]` | `tc_program_push` / if 解析 | `tc_program_free` → `tc_statement_free` 递归 |
| `TcIfStmt.then_body/else_body` | `tc_parse_if_stmt` | `tc_statement_free`（`TC_STMT_IF` 分支） |
| `source`（`tc_compile_source`） | 调用方 | 调用方（编译返回后即可释放） |
| `tc_compile_file` 读入缓冲 | `tc_read_file` | `tc_compile_file` 内 `free` |
| `diag->message` / `filename` / `snippet` | libtc/analyzer 内 `malloc` | `tc_diagnostic_clear` |

**特别说明**：

- `diag->source` 在 `tc_compile_source` 中指向调用方 `source`；`tc_compile_file` 在 `free` 文件缓冲**之前**已完成编译，故诊断打印不依赖该缓冲（行片段已拷贝至 `diag->snippet` 若适用）
- 嵌套 if 形成**树形**语句结构；`tc_typed_program_free` 必须递归释放，不可仅 `free` 顶层数组

---

## 7. 编译流水线与解析模型

### 7.1 两遍扫描（v0.0.24+ 现状）

多行 `if-then-else-end` 需要**行级缩进上下文**，`tc_parse_source` 采用两遍扫描：

```text
第一遍  for each line:
            tokenize_line + 计算 leading whitespace（缩进列）
            存入 line_tokens[]

第二遍  index = 0
        while index < n:
            if first_token == TC_TOK_IF:
                tc_parse_if_stmt(...)   /* 消费多行，递归嵌套 if */
            else:
                tc_parse_statement(single line)
            tc_program_push(top-level stmt)
```

缩进规则与语言标准 §4.7.2 / VM 详设 §19 一致。`tc_tokenize_line` **不变**；缩进在 libtc 编排层计算。

> **历史**：v0.0.23 及以前为逐行 `tokenize + tc_parse_statement`（无缩进上下文）；v0.0.24 起由本节两遍扫描取代。

### 7.2 空行与注释

- 空行和仅含空白字符的行跳过（不占 PC）
- `;` 开头的整行注释跳过
- 行内 `;` 由 lexer 作为语句终结符/注释处理

### 7.3 语句树与顶层 PC

- 顶层 `TcProgram.items[]`：顺序列表，PC 从 0 递增
- `TC_STMT_IF`：顶层占 **1** 个 PC；`then_body`/`else_body` 为子数组，**不**进入顶层 PC 序列
- Executor / AOT 在 if 分支内递归处理子语句

---

## 8. TcTypedProgram 数据契约

Analyzer 通过后，`TcTypedProgram` 是 VM 与 AOT 的**唯一程序表示**：

```c
typedef struct {
    TcProgram program;        /* TcStatement[]，可含 TC_STMT_IF 树 */
    TcSymbolTable symbols;    /* Pass1 符号表：含 scope_level、slot */
    TcWarningList warnings;   /* Pass2 未初始化变量等警告 */
} TcTypedProgram;
```

| 字段 | 用途 | 消费方 |
|------|------|--------|
| `program` | 语句 AST | `tc_execute`、`tc_aot_emit_c` |
| `symbols` | 变量/常量槽位、`const_value`、作用域 | Executor 查 slot；AOT 生成 `slots[N]` |
| `warnings` | 编译期警告 | 调用方打印；不阻止执行 |

**v0.0.24 符号表**：块内 `var`/`let` 在 Pass1 分配全局唯一 `slot`（统一 slot 池）；`scope_level` + `pop_scope` 控制可见性。`symbols.count` 决定 AOT `slots[]` 长度（含互斥分支上的局部变量）。

**嵌入方只读约束**：`tc_run_typed` 不修改 `program`；若需多次执行，应重复 `tc_compile_*` 或自行拷贝（当前未提供 clone API）。

---

## 9. 性能分析支持

设置环境变量 `TC_BENCH=1` 时，libtc 向 stderr 输出各阶段耗时：

```text
bench parse: 0.001234 s
bench analyze: 0.000567 s
bench execute: 0.000890 s
```

实现：`clock_gettime(CLOCK_MONOTONIC)`；埋点位于 `tc_parse_source`（parse）、`tc_compile_source`（analyze）、`tc_run_typed`（execute）。

配合 `scripts/vm/bench.sh` 用于本地回归对比。v0.0.24 两遍 parse 可能略增 parse 阶段耗时，属预期。

---

## 10. 构建集成

### 10.1 CMake 目标

```cmake
target_link_libraries(my_app PRIVATE libtc)
target_include_directories(my_app PRIVATE ${TC_LIBTC_INCLUDE_DIRS})
```

### 10.2 头文件路径

| 路径 | 内容 |
|------|------|
| `src/libtc/` | `tc_lib.h` |
| `src/vm/runtime/` | `tc_types.h`、`tc_diagnostic.h`、… |
| `src/vm/analyzer/` | `tc_analyzer.h`（`tc_typed_program_free`） |
| `src/vm/lexer/`、`parser/`、`executor/` | 各模块头文件 |

完整速查见 [libtc-api.md](./libtc-api.md)。

### 10.3 编译选项

`-std=c99 -Wall -Wextra -pedantic`，与 VM 一致。

---

## 11. 示例

### 11.1 完整嵌入流程

```c
#include "tc_lib.h"
#include "tc_diagnostic.h"
#include "tc_warning.h"

int main(void) {
    TcDiagnostic diag;
    TcTypedProgram program;

    tc_diagnostic_init(&diag);
    if (tc_compile_source("var x: int32 = 1\nwriteln(int32, x)\n", &program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (program.warnings.count > 0) {
        tc_warning_list_print(&program.warnings, stderr);
    }

    if (tc_run_typed(&program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_typed_program_free(&program);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
    return 0;
}
```

### 11.2 仅编译（AOT / --check 模式）

```c
TcTypedProgram program;
if (tc_compile_file("input.tc", &program, &diag) != 0) { /* 错误处理 */ }

/* AOT: tc_aot_emit_c(&program, fp); 不调用 tc_run_typed */
/* --check: 打印警告后 tc_typed_program_free */

tc_typed_program_free(&program);
```

### 11.3 v0.0.24 含 if 的源文件

含 `if-then-else-end` 的 `.tc` 文件经**同一** `tc_compile_file` 编译；无需嵌入方特殊处理。解析与作用域由 libtc 内部完成。

---

## 12. 与 VM / AOT / REPL 的关系

### 12.1 组件调用关系

```text
tc-vm (main.c)
    └─ tc_run_source / tc_run_file     [driver/tc_driver.c]
           └─ tc_compile_* + tc_run_typed + tc_typed_program_free
                  └─ libtc (tc_lib.c)

tc-aot (main.c)
    └─ tc_compile_file
           └─ libtc
    └─ tc_aot_emit_c(program)          [不经过 tc_run_typed]

tc-repl (tc_repl.c)
    └─ 绕过 libtc 全文件编译
    └─ 逐行 analyze_statement / execute_statement
    └─ v0.0.24: 不支持 if
```

| 组件 | libtc API | 说明 |
|------|-----------|------|
| **tc-vm 文件模式** | `tc_compile_*` + `tc_run_typed` | 经 `tc_driver` 封装 |
| **tc-vm --check** | 仅 `tc_compile_*` | 不执行 |
| **tc-aot** | 仅 `tc_compile_*` | 然后 `tc_aot_emit_c` |
| **嵌入应用** | `tc_compile_*` + 可选 `tc_run_typed` | 直接使用 |
| **tc-repl** | **不使用** `tc_compile_source` | 增量单行路径 |

### 12.2 共享模块

| 模块 | libtc 链接 | AOT 额外链接 |
|------|------------|--------------|
| `tc_lexer/parser/analyzer/executor` | ✅ | 间接（经 libtc 编译） |
| `tc_semantics.c` / `tc_io.c` | ✅（执行路径） | ✅（生成代码 + shim） |
| `tc_aot_codegen.c` | ❌ | ✅ |

### 12.3 行为一致性

对同一 `.tc` 源文件：

1. `tc_compile_file` 产出相同 `TcTypedProgram`（VM 与 AOT 共享）
2. VM：`tc_run_typed` 解释执行
3. AOT：`tc_aot_emit_c` 生成 C 后原生执行
4. 差分测试要求 stdout 逐字节一致（见 AOT 详设 §10）

---

## 13. v0.0.24 变更影响（libtc 层）

| 变更项 | libtc 职责 | 关联模块 |
|--------|------------|----------|
| 两遍 `tc_parse_source` | **主改造文件** `tc_lib.c` | `tc_parse_if_stmt` @ parser |
| `TcIfStmt` AST | 无类型变更（types.h） | `tc_statement_free` @ parser |
| 块级作用域 Analyze | 无逻辑（委托 `tc_analyze`） | `tc_symbol.c` push/pop、`find_in_scope` |
| then/else 双作用域 | 无 | analyzer Pass1/Pass2 递归 |
| DFS `stmt_index` | 无 | analyzer Pass2 |
| `tc_run_typed` / if 执行 | 无代码变更（委托 executor） | `tc_execute_statement` |
| REPL 拒绝 if | 无 | `tc_analyze_statement` @ analyzer |
| DIAG 错误码分离 | 无（`tc_lib.c` OOM 路径使用 `TC_ERR_OUT_OF_MEMORY`） | `tc_types.h`、全线 .c 模块 |

**开发顺序**（与 [TC-0.0.24开发计划.md](./TC-0.0.24开发计划.md) 对齐）：types → symbol → lexer → **`tc_lib.c` parse** → analyzer → executor → AOT。

**知识图谱**：`tc_parse_source` 分发点登记在 `@knowledge-graph` §TcStmtKind（文件：`libtc/tc_lib.c`）。

---

## 14. v0.0.25 变更影响（libtc 层）

| 变更项 | libtc 职责 | 关联模块 |
|--------|------------|----------|
| `FloatType` / `FloatMode` / 4 个浮点 `TcRhsKind` | **无 API 变更**；`tc_compile_*` 透传 Parse + Analyze | `tc_types.h`、`tc_parser.c`、`tc_analyzer.c`、`tc_const_eval.c` |
| 浮点词法/语法/分析 | 无逻辑（委托现有流水线） | `tc_lexer.c`、`tc_parser.c`、`tc_analyzer.c` |
| 浮点常量求值 | 无代码变更（委托 `tc_const_eval.c`） | 以 `float64` 精度编译期求值；禁 `ieee`/`wrap`（语言标准 §4.3） |
| `tc_run_typed` | 无代码变更（委托 `tc_executor.c`） | `exec_fp_arith`/`exec_fp_unary`/`exec_fp_compare`/`exec_fp_cast` |
| `TcTypedProgram` 契约 | **结构体字段无变更**；语句树含浮点 RHS 变体 | VM 详设 §5.5、§8 |
| 槽位分配 | 规则不变：Pass1 统一 slot 池 | 浮点 `var`/`let` 与整数共用 slot 索引 |
| 错误码 | 透传 Analyze / Execute 诊断 | 5 种浮点相关 `TcErrorKind` + `TC_ERR_MODE_MISMATCH` |
| AOT 嵌入 | 无变更：仍 `tc_compile_file` → `tc_aot_emit_c` | AOT 详设 §4.4、§5.2 |

**嵌入方影响**：无需为新类型或 RHS 特殊处理；`tc_compile_source` / `tc_compile_file` / `tc_run_typed` 签名与所有权契约不变（见 [libtc-api.md](./libtc-api.md) §v0.0.25 简述）。

---

## 附录 A：API 签名速查

```c
/* src/libtc/tc_lib.h */

int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag);
int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag);
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);

/* src/vm/analyzer/tc_analyzer.h — tc_lib.h 已 include */

void tc_typed_program_free(TcTypedProgram *program);
int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag);  /* 一般不经嵌入方直接调用 */
```

用户向速查：[libtc-api.md](./libtc-api.md)。

---

## 附录 B：文档修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.0.21 | 2026-07-06 | 首版：从 TC-VM 详设独立；API、错误契约、逐行解析流水线 |
| **0.0.24** | **2026-07-07** | 对齐语言/VM/AOT v0.0.24：`tc_parse_source` 两遍扫描规划；`TcIfStmt` 树形 AST 与内存所有权；`TcTypedProgram` 数据契约；与 REPL 路径分叉；v0.0.24 libtc 变更表；实现状态标注 |
| **0.0.24-rev1** | **2026-07-07** | 合规审查跟进：then/else 双作用域、Analyzer 递归、统一 slot 池、知识图谱交叉引用 |
| **0.0.24-rev2** | **2026-07-09** | OOM 错误码分离：`tc_lib.c` OOM 路径使用 `TC_ERR_OUT_OF_MEMORY` 而非 `TC_ERR_SYNTAX`；§5.1 错误状态契约同步更新 |
| **0.0.25** | **2026-07-13** | **浮点全链路**：`tc_types.h` 扩展 `FloatType`/`FloatMode` 枚举、`TcRhsKind` 新增 4 个浮点变体；`tc_semantics.h/c` 新增 `exec_fp_arith`/`exec_fp_unary`/`exec_fp_compare`/`exec_fp_cast`；`tc_io.h/c` 扩展浮点格式符与输入解析；`tc_lib.c` 编译流水线无变化（Parse/Analyze 层已吸收浮点 AST 变体）；版本号同步更新 |
| **0.0.25-doc1** | **2026-07-13** | **实现设计文档评审修正**：§3.1 补充 v0.0.25 浮点错误码；§7 合并为两遍扫描现状并标注 v0.0.23 历史；新增 §14 v0.0.25 变更影响表 |

---

*— 文档结束 —*
