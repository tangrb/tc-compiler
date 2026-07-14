# libtc 嵌入 API

> **版本**：0.0.26 · **代码**：v0.0.26（`TC_VM_VERSION`）  
> **详设**：[libtc设计说明书.md](./libtc设计说明书.md)

libtc 是 TC 编译器的静态库，提供「编译（Parse + Analyze）」与「执行」分离的嵌入接口。

## v0.0.26 简述

| 项 | 说明 |
|----|------|
| **goto / label** | 经 `tc_compile_*` Analyze 做跳转合法性检查；执行经 `tc_run_typed`；**API 不变** |
| **未初始化** | Analyze 路径敏感数据流 → `TC_ERR_UNINITIALIZED_VARIABLE`（不再是警告） |
| **新错误码** | `LABEL_NOT_FOUND` / `DUPLICATE_LABEL` / `JUMP_INTO_BLOCK` / `JUMP_TO_SIBLING_BLOCK` / `UNINITIALIZED_VARIABLE` |
| **REPL** | **不支持** `if` / `goto` / `label` |
| **嵌入方** | 签名与所有权契约不变；非法 goto / 未初始化在编译阶段经 `diag` 返回 |

## v0.0.25 简述

| 项 | 说明 |
|----|------|
| **浮点支持** | `float32`/`float64` 类型经 `tc_compile_*` 全链路编译（词法/语法/分析/执行/AOT）；**API 不变** |
| **类型扩展** | `TcType` 现在可编码 `FloatType`（`TC_FLOAT32`/`TC_FLOAT64`）；`TcLiteral` 新增 `is_float` 标记 |
| **新 RHS** | 4 个浮点 RHS kind（`FLOAT_ARITH`/`FLOAT_UNARY`/`FLOAT_COMPARE`/`FLOAT_CAST`）经现有 RHS 分发路径 |
| **错误码** | 新增 5 种浮点相关 `TcErrorKind`（`FLOAT_OVERFLOW`/`FLOAT_UNDERFLOW`/`FLOAT_INVALID`/`FLOAT_CAST_OVERFLOW`/`MODE_MISMATCH`） |
| **运行时** | `tc_run_typed` 可产生浮点运行时错误（严格模式溢出/下溢/无效操作/转换溢出） |
| **嵌入方** | 无需为新类型或 RHS 特殊处理；`TcTypedProgram` 结构体字段无变更 |

v0.0.24 特性（控制流 `if-then-else-end`、块级作用域）仍通过现有 API 完整支持；浮点扩展不改变任何 API 签名。

| 项 | 说明 |
|----|------|
| **控制流** | `if-then-else-end` 经 `tc_compile_*` 全文件编译；`TcTypedProgram.program` 可含 `TC_STMT_IF`（嵌套 `then_body`/`else_body`） |
| **解析** | `tc_lib.c` 内 `tc_parse_source` **采用**两遍行扫描（缩进 + `tc_parse_if_stmt`）；**API 不变** |
| **作用域** | 块内 `var`/`let` 在 Analyze 阶段处理；`symbols.count` 含块局部 slot（AOT 同长度 `slots[]`） |
| **释放** | `tc_typed_program_free` 递归释放 if 子树（`tc_statement_free`） |
| **REPL** | **不**走 libtc；`tc-repl` 逐行路径且 **不支持** `if` |
| **嵌入方** | 仍只需 `tc_compile_file` / `tc_compile_source` + 可选 `tc_run_typed`；无需为 if 单独调用 |

Parse 失败类型在 v0.0.24 增加缩进相关静态错误（`TC_ERR_INDENT_*` 等）；OOM 错误从 `TC_ERR_SYNTAX` 分离为独立 `TC_ERR_OUT_OF_MEMORY`，仍通过 `diag` 单槽返回。

---

## 头文件

```c
#include "tc_lib.h"
```

构建后 include 路径：`src/libtc/` 及各 VM 模块头文件目录（见 CMake `TC_LIBTC_INCLUDE_DIRS`）。

## API

### `tc_compile_source`

```c
int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag);
```

- **输入**：完整 TC 源文本（单行或多行，`source` 须在 `diag` 打印前保持有效）
- **输出**：成功时 `out` 为已通过静态分析的 `TcTypedProgram`；警告写入 `out->warnings`
- **返回**：`0` 成功；`-1` 失败（`diag` 已设置）
- **失败时 `out` 的状态**：
  - **Parse 失败**（词法/语法/缩进/OOM）：`out` **不会被修改**；调用方不得读取 `out`，也**无需** `tc_typed_program_free`。OOM 错误使用独立 `TC_ERR_OUT_OF_MEMORY`（v0.0.24 Day1）。
  - **Analyze 失败**（静态分析错误）：`tc_analyze` 内部已调用 `tc_typed_program_free(out)`，`out` 处于**空状态**（count 为 0、指针为 NULL）；**无需**再次释放
- **所有权**：仅成功时 `out` 由调用方拥有，须 `tc_typed_program_free(out)`

### `tc_compile_file`

```c
int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag);
```

等价于读文件后调用 `tc_compile_source`；`diag->filename` 设为 `path`。

- **返回**：与 `tc_compile_source` 相同
- **失败时 `out` 的状态**：文件 I/O 失败时 `out` 不会被修改；编译阶段失败时的行为同 `tc_compile_source`

### `tc_run_typed`

```c
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);
```

- **输入**：已通过 `tc_analyze` 的程序（不可修改）
- **返回**：`0` 成功；`-1` 运行时错误（除零、溢出、I/O 等）
- **副作用**：`write`/`writeln` 输出到 stdout；`read` 从 stdin 读取
- **警告**：不在此函数内打印；调用方应在执行前自行 `tc_warning_list_print`

### `tc_typed_program_free`

```c
void tc_typed_program_free(TcTypedProgram *program);
```

释放 `TcTypedProgram` 内所有堆内存（语句树含嵌套 if、符号表、警告列表）。声明于 `tc_analyzer.h`（`tc_lib.h` 已 include）。释放后各子字段指针置 NULL；对已是空状态的 `TcTypedProgram` 再次调用是安全的（no-op）。

## 内存所有权约定

| 对象 | 分配方 | 释放方 |
|------|--------|--------|
| `TcDiagnostic` | 调用方栈/堆 | `tc_diagnostic_clear`（可重复） |
| `TcTypedProgram` | `tc_compile_*`（成功时） | `tc_typed_program_free`（Analyze 失败时 libtc 内部已释放） |
| `TcIfStmt` 子语句数组 | parser（`tc_parse_if_stmt`） | `tc_typed_program_free` → `tc_statement_free` 递归 |
| `source` 字符串（`tc_compile_source`） | 调用方 | 调用方（编译完成后即可释放） |
| `tc_compile_file` 读入缓冲 | `tc_read_file` | `tc_compile_file` 返回前 `free` |
| `diag->message` / `diag->filename` | libtc 内 `malloc` | `tc_diagnostic_clear` |

## 性能分析

设置环境变量 `TC_BENCH=1` 时，`tc_compile_source` / `tc_run_typed` 向 stderr 输出各阶段耗时：

```text
bench parse: 0.001234 s
bench analyze: 0.000567 s
bench execute: 0.000890 s
```

配合 `scripts/vm/bench.sh` 用于本地回归对比。

## 链接

```cmake
target_link_libraries(my_app PRIVATE libtc)
target_include_directories(my_app PRIVATE ${TC_LIBTC_INCLUDE_DIRS})
```

## 示例

```c
TcDiagnostic diag;
TcTypedProgram program;

tc_diagnostic_init(&diag);
if (tc_compile_source("var x: int32 = 1\nwriteln(int32, x)\n", &program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    /* 编译失败：无需 tc_typed_program_free（Parse 未写入 out，Analyze 失败时已内部释放） */
    return 1;
}
if (program.warnings.count > 0) {
    tc_warning_list_print(&program.warnings, stderr);
}
if (tc_run_typed(&program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    tc_typed_program_free(&program);
    return 1;
}
tc_typed_program_free(&program);
tc_diagnostic_clear(&diag);
```
