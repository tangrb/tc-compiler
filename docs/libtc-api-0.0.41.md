# libtc 嵌入 API

> **当前版本**：libtc / TC-VM v0.0.41
>
> **目标语言规范（唯一权威）**：[TC 0.0.41](./TC语言标准设计说明书-0.0.41.md) · [TC 编译器标准 0.0.41](./TC编译器标准设计说明书-0.0.41.md)
>
> **文档职责**：本页是 0.0.41 调用者速查；内部设计见 [libtc 设计说明书](./libtc设计说明书-0.0.41.md)。

---

## 1. 头文件与链接

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

## 2. API 一览

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

## 3. `tc_compile_source`

```c
int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out,
                      TcDiagnostic *diag);
```

无路径的内存源仅做结构检查、不解析 import，故无搜索路径参数；文件编译（含 import 解析）见第 4 章 `tc_compile_file_opts`。

### 当前行为

- 对完整 NUL 结尾 source 执行 13 阶段确定性编译管线：词法（含缩进栈）→ 语法解析（含受限恢复）→ 模块结构与导入解析（4a→4b→4c→4d）→ 函数签名收集 → 名称/作用域/类型语义（6a→6b→6c→6d→6e）→ funcall 检查 → return 检查 → `let`/`static let` 求值 + `static var` 初始化器验证 → 静态布尔三态判定（[语言标准 §5.2.2]）→ CFG 可达性与确定初始化固定点 → 调用图环检查 → 代码生成前完成；
- 支持多文件模块系统：`#program` / `#lib`、`import`、`public`/`private`、`Self`；
- 支持函数定义、`funcall` 调用、命名实参、按值只读形参、`return`、无环调用图；
- 支持 `ptr<T>`、`memblock<T, N>`、`struct`、`isize`/`usize`、`void` 返回等类型；
- 支持 `static var` / `static let` 模块静态成员；
- 完整 CFG（顶层 + 各函数独立域）确定初始化检查在编译阶段完成；
- `diag` 保存 source 副本以生成源码片段，调用返回后可释放原 source。

### 失败时 `out`

- 任一阶段失败时，`out` 都不被修改；
- libtc 回收本次调用创建的全部临时对象；
- 调用方不读取 `out`，也不调用 `tc_typed_program_free(out)`。

### 前置条件

- `source`、`name`、`out`、`diag` 为有效指针；
- `diag` 已初始化；
- `out` 当前不持有未释放的 typed program。

---

## 4. `tc_compile_file_opts`

```c
int tc_compile_file_opts(const char *path,
                         const TcCompileOptions *opts,
                         TcTypedProgram *out,
                         TcDiagnostic *diag);
```

`opts`（可为 NULL）携带本次编译的 `-I` 等价搜索路径（`TcCompileOptions`，内部借用不复制，仅调用期间须有效）。编译**无进程级全局状态**：同一进程内多个编译单元可各自携带不同的搜索路径，互不污染，亦无线程安全问题。

### 当前行为

1. 从入口 `#program` 文件出发，按 `import` 语句逐层加载所有可达模块；
2. 在模块搜索路径中唯一定位 `.tc` 文件（入口文件所在目录 → `-I` 路径 → 默认路径）；
3. 检查模块依赖图 DAG（循环导入 → `TC_CE_CIRCULAR_IMPORT`）；
4. 对全部可达模块执行 13 阶段编译管线；
5. 返回前释放内部文件缓冲。

文件打开/读取失败时 `out` 不被修改。文件不存在使用 `TC_DIAG_API / TC_API_ERR_FILE_OPEN`；seek/read/close 失败使用 `TC_DIAG_API / TC_API_ERR_FILE_READ`，不会伪装成语言 `SyntaxError`。

模块搜索路径通过 `opts` 会话选项或 CLI `-I` 选项设置；`opts` 为 NULL 或 `search_paths` 为空时本次编译无额外搜索路径（入口文件所在目录仍最先搜索）。

---

## 5. `tc_run_program`

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

## 6. 诊断生命周期

### 初始化与释放

```c
TcDiagnostic diag;

tc_diagnostic_init(&diag);
/* compile / run */
tc_diagnostic_clear(&diag);
```

`tc_diagnostic_clear` 释放 message、filename、snippet 和 source，可在已初始化对象上重复调用。

### 打印

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

### 程序化访问

`TcDiagnostic` 包含：

- `TcDiagnosticDomain domain`；
- `TcApiErrorCode api_code`；
- `TcErrorKind kind`；
- message、filename、snippet、source；
- 1-based line/column，未知列使用 `TC_COLUMN_UNKNOWN`。

当前实现为单槽 fail-fast，只保留第一条错误。Language 使用 `kind`，API/Environment 使用 `api_code`，OOM 使用 Implementation 域和 `TC_ERR_OUT_OF_MEMORY`。

---

## 7. Typed program 所有权

### 成功路径

```text
tc_compile_* succeeds
  → caller owns TcTypedProgram
  → zero or more tc_run_program / AOT reads
  → tc_typed_program_free exactly once
```

### 所有权表

| 对象 | 所有者 | 释放 |
| ---- | ------ | ---- |
| 输入 source | 调用方 | 调用方；compile 返回后即可释放 |
| `TcDiagnostic` | 调用方 | `tc_diagnostic_clear` |
| 成功的 `TcTypedProgram` | 调用方 | `tc_typed_program_free` |
| program 内 AST/名称/if/while/func 子树 | typed program | 随 `tc_typed_program_free` 递归释放 |
| 符号、标签、常量值、函数签名与 CFG | typed program | 随 `tc_typed_program_free` |
| runtime slots | libtc Executor | 每次运行返回前 |
| `tc_compile_file_opts` 文件缓冲 | libtc | compile 返回前 |

### 禁止模式

- 编译失败后调用 `tc_typed_program_free`；
- 成功对象不释放或释放两次；
- 在 program 释放后执行；
- 对同一个 out 覆盖编译而未先释放旧对象；
- 多线程无同步地共享并释放同一对象。

---

## 8. 当前错误码

0.0.41 语言错误码覆盖编译器标准 §11.4 完整清单，包括：

- **通用与核心**（60+ 个 `TC_CE_*` 码）：语法、名称、类型、字面量、常量赋值、常量表达式、溢出、除零、转换溢出、比较、格式说明符、操作数数量、缩进、控制流、块结构等；
- **函数诊断**（20 个）：重名、冲突、未定义、参数、实参、调用位置、返回形式/类型、缺少返回、不可达语句、递归等；
- **memblock 诊断**（4 个）：索引越界（静态+运行时）、构造器实参数量、规划个数不匹配；
- **结构体诊断**（7 个）：字段缺失、未知字段、重复字段、顺序、不可变字段、重复定义、未定义；
- **模块诊断**（10 个）：层序错误、缺少可见性、模式误用、导入失败与循环等；
- **指针与 memcopy 诊断**（4 个）：空指针解引用/算术、memcopy 非法区间等；
- **运行时错误**（16 个 `TC_RE_*` 码）：除零、整数溢出、负移位、浮点异常、转换溢出、I/O、空指针、memblock 越界等。

TC 没有编译警告。

---

## 9. 性能计时

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

## 10. 最小示例

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

## 11. 当前能力边界

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
| 70+ 语言错误码 + OutOfMemory 完整表 | 支持 |

---

## 12. API 契约

### 函数签名

- `tc_compile_source(source, name, out, diag)`：编译无路径内存源，无搜索路径参数；
- `tc_compile_file_opts(path, opts, out, diag)`：编译文件，`opts` 可为 NULL 携带 `TcCompileOptions` 搜索路径；
- `tc_run_program(program, diag)`：执行已类型化程序，行为涵盖模块静态初始化。

### 数据结构

`TcTypedProgram`、`TcDiagnostic`、`TcErrorKind` 和语句/RHS kind 枚举为公共类型；`TcTypedProgram`、`TcDiagnostic` 和枚举值均随版本演进，嵌入方不得直接依赖结构字段布局或具体错误枚举取值，应按公开函数访问。

---

*当前调用契约以 [src/libtc/tc_lib.h](../src/libtc/tc_lib.h) 和本页为准；语言规则以 [TC 0.0.41 标准](./TC语言标准设计说明书-0.0.41.md) 与 [TC 编译器标准 0.0.41](./TC编译器标准设计说明书-0.0.41.md) 为准。*
