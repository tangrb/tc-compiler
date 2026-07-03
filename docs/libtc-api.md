# libtc 嵌入 API

libtc 是 TC 编译器的静态库，提供「编译（Parse + Analyze）」与「执行」分离的嵌入接口。

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
  - **Parse 失败**（词法/语法/OOM）：`out` **不会被修改**；调用方不得读取 `out`，也**无需** `tc_typed_program_free`
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

释放 `TcTypedProgram` 内所有堆内存（语句、符号表、警告列表）。释放后各子字段指针置 NULL；对已是空状态的 `TcTypedProgram` 再次调用是安全的（no-op）。

## 内存所有权约定

| 对象 | 分配方 | 释放方 |
|------|--------|--------|
| `TcDiagnostic` | 调用方栈/堆 | `tc_diagnostic_clear`（可重复） |
| `TcTypedProgram` | `tc_compile_*`（成功时） | `tc_typed_program_free`（Analyze 失败时 libtc 内部已释放） |
| `source` 字符串（`tc_compile_source`） | 调用方 | 调用方（编译完成后即可释放） |
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
