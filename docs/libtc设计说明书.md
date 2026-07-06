# libtc 设计说明书

> **版本**：0.0.21（草案）  
> **依赖**：[TC语言标准设计说明书.md](./TC语言标准设计说明书.md)  
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
7. [编译流水线（逐行解析）](#7-编译流水线逐行解析)
8. [性能分析支持](#8-性能分析支持)
9. [构建集成](#9-构建集成)
10. [示例](#10-示例)
11. [与 VM / AOT 的关系](#11-与-vm--aot-的关系)

附录

- [附录 A：API 签名速查](#附录-aapi-签名速查)
- [附录 B：文档修订记录](#附录-b文档修订记录)

---

## 1. 设计目标与原则

### 1.1 目标

libtc 是 TC 编译器的静态库，提供以下能力：

- **嵌入友好**：其他 C 程序可调用 libtc 编译并执行 TC 源码，无需启动子进程
- **编译与执行分离**：调用方可先编译（`tc_compile_source`），再独立执行（`tc_run_typed`）
- **与 CLI 共享**：`tc-vm` 和 `tc-aot` 的 CLI 入口都通过 libtc 完成编译，不重复实现解析流水线

### 1.2 核心原则

| 原则 | 说明 |
| ---- | ---- |
| **薄封装** | libtc 是 VM 各模块（lexer/parser/analyzer/executor）的编排层，本身不含语义逻辑 |
| **fail-fast** | 词法/语法/分析阶段遇首错即停，不累积错误 |
| **所有权明确** | 成功时调用方拥有 `TcTypedProgram`，失败时 libtc 内部处理清理 |
| **零外部依赖** | 仅依赖 POSIX 标准 API（`fopen`、`gettime` 等） |

---

## 2. 总体架构

```text
调用方 C 程序
    │
    ├─ tc_compile_source / tc_compile_file
    │      │  Parse: tc_parse_source() → tc_tokenize_line → tc_parse_statement
    │      │  Analyze: tc_analyze() → tc_pass1_collect_symbols → tc_pass2_type_check
    │      ▼
    │      TcTypedProgram { statements, symbols, warnings }
    │
    ├─ tc_run_typed
    │      │  Execute: tc_execute()
    │      ▼
    │      stdout / stderr / runtime errors
    │
    └─ tc_typed_program_free
          释放所有堆内存
```

### 2.1 文件结构

| 文件 | 职责 |
|------|------|
| `tc_lib.h` | 对外 API 声明 |
| `tc_lib.c` | 编译流水线编排、文件读取、性能计时 |

### 2.2 编译时依赖

libtc 是静态库，编译时包含 VM 各模块的源文件：

```
src/libtc/
└── tc_lib.c              ← 编排逻辑

src/vm/runtime/            ← 类型、诊断、符号、语义、I/O
├── tc_types.c
├── tc_diagnostic.c
├── tc_symbol.c
├── tc_warning.c
├── tc_semantics.c
└── tc_io.c

src/vm/lexer/
└── tc_lexer.c

src/vm/parser/
└── tc_parser.c

src/vm/analyzer/
├── tc_analyzer.c
└── tc_const_eval.c

src/vm/executor/
└── tc_executor.c
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
| `diag` | 输出 | 失败时写入诊断信息 |

**返回值**：`0` 成功；`-1` 失败（`diag` 已设置）。

### 3.2 执行接口

```c
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);
```

**约束**：`program` 必须已通过 `tc_compile_*` 且返回值 `0`。

**返回值**：`0` 成功；`-1` 运行时错误（除零、溢出、I/O 等）。

**副作用**：`write`/`writeln` 输出到 stdout；`read` 从 stdin 读取。

### 3.3 释放接口

```c
void tc_typed_program_free(TcTypedProgram *program);
```

释放 `TcTypedProgram` 内所有堆内存。释放后各子字段指针置 NULL；对空状态再次调用安全。

### 3.4 接口设计决策

**为什么传递裸指针而非结构体**：TcDiagnostic 和 TcTypedProgram 由调用方在栈上分配，libtc 内部填充；避免了堆分配所有权模糊。

**为什么编译和执行分离**：允许调用方在执行前检查警告列表，决定是否继续；也支持 `--check` 模式（仅编译不执行）。

---

## 4. 内部调用链

### 4.1 tc_compile_source

```
tc_compile_source(source, out, diag)
    │
    ├─ diag->source = source          ← 诊断打印时引用源码行
    │
    ├─ tc_parse_source(source, &program, diag)
    │     逐行循环:
    │       ├─ tc_is_skippable_line() → 跳过空行/注释行
    │       ├─ tc_tokenize_line()     → 词法分析
    │       ├─ tc_parse_statement()   → 语法分析 → TcStatement
    │       └─ tc_program_push()      → 追加到 TcProgram
    │
    └─ tc_analyze(&program, out, diag)
           ├─ tc_pass1_collect_symbols()
           └─ tc_pass2_type_check()
                └─ tc_resolve_const_value()  →  let 编译期求值
```

### 4.2 tc_compile_file

```
tc_compile_file(path, out, diag)
    ├─ tc_diagnostic_set_source(diag, path, NULL)    ← 设置文件名
    ├─ tc_read_file(path)                              → 读取文件到字符串
    └─ tc_compile_source(source, out, diag)            → 委托编译
```

### 4.3 tc_run_typed

```
tc_run_typed(program, diag)
    └─ tc_execute(program, diag)   → 执行引擎
         └─ tc_execute_statement() × N
              ├─ tc_eval_rhs()      → 运算求值（委托 tc_semantics.c）
              ├─ tc_io_write_value()/tc_io_read_value()  → I/O
              └─ 变量槽位写入
```

---

## 5. 错误状态契约

`tc_compile_source` 和 `tc_compile_file` 失败时，`out` 的状态取决于失败阶段：

### 5.1 Parse 失败（词法/语法/OOM）

- `out` **不会被修改**
- 调用方不得读取 `out`，也**无需**调用 `tc_typed_program_free`

原因：Parse 阶段将结果写入中间 `TcProgram`，尚未写入 `out`；失败时 `tc_parse_source` 内部已释放 `TcProgram`。

### 5.2 Analyze 失败（静态分析错误）

- `tc_analyze` 内部已调用 `tc_typed_program_free(out)`
- `out` 处于**空状态**（`count` 为 0、指针为 NULL）
- 调用方**无需**再次释放，再次调用 `tc_typed_program_free` 是安全的

### 5.3 文件 I/O 失败

- `tc_compile_file` 在打开/读取文件阶段失败时，`out` 不会被修改
- 行为同 Parse 失败

### 5.4 成功

- 调用方拥有 `TcTypedProgram`，必须调用 `tc_typed_program_free` 释放

---

## 6. 内存所有权约定

| 对象 | 分配方 | 释放方 |
|------|--------|--------|
| `TcDiagnostic` | 调用方栈/堆 | `tc_diagnostic_clear`（可重复使用） |
| `TcTypedProgram` | `tc_compile_*`（成功时） | `tc_typed_program_free` |
| `source` 字符串（`tc_compile_source`） | 调用方 | 调用方（编译完成后即可释放） |
| `diag->message` / `diag->filename` | libtc 内 `malloc` | `tc_diagnostic_clear` |

**特别说明**：`source` 指针在 `diag` 打印前必须有效。`tc_compile_file` 内部读取文件后调用 `tc_compile_source`，文件缓冲区的生命周期由 libtc 内部管理。

---

## 7. 编译流水线（逐行解析）

### 7.1 行模型

TC 语言的每条语句占一行，`tc_parse_source` 按行拆分源文本：

```
source text → cursor 逐字符扫描
    ├─ 定位行边界（\n / \r / \r\n）
    ├─ 复制行文本
    ├─ tc_is_skippable_line → 空行/注释行跳过
    └─ 非跳过行:
         ├─ tc_tokenize_line → TcTokenList
         └─ tc_parse_statement → TcStatement → tc_program_push
```

### 7.2 空行与注释

- 空行和仅含空白字符的行跳过
- `;` 开头的注释行跳过
- 行内注释（`;` 后的内容在词法分析阶段由 lexer 处理）

### 7.3 行终止符

支持 `\n`（Unix）、`\r\n`（Windows）、`\r`（旧 Mac）三种行终止符。

---

## 8. 性能分析支持

设置环境变量 `TC_BENCH=1` 时，libtc 向 stderr 输出各阶段耗时：

```text
bench parse: 0.001234 s
bench analyze: 0.000567 s
bench execute: 0.000890 s
```

实现方式：`clock_gettime(CLOCK_MONOTONIC)` 计时，三个埋点分别在 `tc_parse_source`（parse）、`tc_compile_source`（analyze）、`tc_run_typed`（execute）。

配合 `scripts/vm/bench.sh` 可用于本地回归对比。

---

## 9. 构建集成

### 9.1 CMake 目标

```cmake
target_link_libraries(my_app PRIVATE libtc)
target_include_directories(my_app PRIVATE ${TC_LIBTC_INCLUDE_DIRS})
```

### 9.2 头文件路径

libtc 导出的 include 目录：

- `src/libtc/` — `tc_lib.h`
- `src/vm/runtime/` — `TcValue`、`TcDiagnostic`、`TcWarning` 等类型
- `src/vm/lexer/`、`parser/`、`analyzer/`、`executor/` — 各模块头文件

### 9.3 编译选项

libtc 使用 `-std=c99 -Wall -Wextra -pedantic` 编译，与 VM 一致。

---

## 10. 示例

### 10.1 完整嵌入流程

```c
TcDiagnostic diag;
TcTypedProgram program;

tc_diagnostic_init(&diag);
if (tc_compile_source("var x: int32 = 1\nwriteln(int32, x)\n", &program, &diag) != 0) {
    /* 编译失败 */
    tc_diagnostic_print(&diag, stderr);
    return 1;
}

/* 可选：打印警告 */
if (program.warnings.count > 0) {
    tc_warning_list_print(&program.warnings, stderr);
}

/* 执行 */
if (tc_run_typed(&program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    tc_typed_program_free(&program);
    return 1;
}

tc_typed_program_free(&program);
tc_diagnostic_clear(&diag);
```

### 10.2 示例说明

1. 调用方在栈上分配 `TcDiagnostic` 和 `TcTypedProgram`
2. `tc_compile_source` 返回 `0` 时，`program` 由调用方拥有
3. 执行前可检查 `program.warnings`
4. 无论执行成功与否，最后都需 `tc_typed_program_free`

---

## 11. 与 VM / AOT 的关系

### 11.1 三个组件的调用关系

```
tc-vm (CLI)          tc-aot (CLI)
    │                     │
    │ tc_run_source       │ tc_compile_file
    │ tc_run_file         │ tc_aot_emit_c
    │                     │
    └──────┬──────┘       │
           │              │
      ┌────┴────┐         │
      │  libtc  │◄────────┘
      └────┬────┘
           │
      ┌────┴────┐
      │ VM 模块  │
      │ (lexer,  │
      │ parser,  │
      │analyzer, │
      │executor) │
      └─────────┘
```

| 组件 | 使用 libtc 的目的 |
|------|-----------------|
| **tc-vm** | 通过 `tc_run_source`/`tc_run_file` 完成编译+执行（driver 封装） |
| **tc-aot** | 通过 `tc_compile_file` 完成编译，然后 codegen 生成 C |
| **嵌入应用** | 直接调用 `tc_compile_*` + `tc_run_typed` |

### 11.2 共享的模块

| 模块 | 共享方 |
|------|--------|
| `tc_types.c` / `tc_diagnostic.c` | libtc 内部链接 |
| `tc_semantics.c` / `tc_io.c` | tc-aot 运行时额外链接 |
| `tc_analyzer.c` / `tc_lexer.c` / `tc_parser.c` | libtc 内部，tc-aot 间接使用 |

---

## 附录 A：API 签名速查

```c
/* src/libtc/tc_lib.h */

/* 编译：解析 + 静态分析 */
int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag);
int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag);

/* 执行：运行已编译的程序 */
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);

/* 释放 */
void tc_typed_program_free(TcTypedProgram *program);
```

---

## 附录 B：文档修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.0.21 | 2026-07-06 | 首版：从 TC-VM 详细设计说明书独立——提取 §12.1 工程布局中的 libtc 描述；补充 API 设计决策、错误状态契约、内部调用链 |
