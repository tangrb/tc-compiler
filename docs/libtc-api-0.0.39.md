# libtc 嵌入 API

> **当前版本**：libtc / TC-VM v0.0.39
>
> **目标语言规范（唯一权威）**：[TC 0.0.39](./TC语言标准设计说明书-0.0.39.md) · [TC 编译器标准 0.0.39](./TC编译器标准设计说明书-0.0.39.md)
>
> **文档职责**：本页是 0.0.39 调用者速查；内部设计见 [libtc 设计说明书](./libtc设计说明书-0.0.39.md)。

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

| 函数 | 成功 | 失败 | 所有权 |
| ---- | ---- | ---- | ------ |
| `tc_compile_source` | 返回 0，写入 typed program | 返回 -1，设置 diag | 仅成功时调用方取得 `out` |
| `tc_compile_file` | 返回 0，写入 typed program | 返回 -1，设置 diag | 仅成功时调用方取得 `out` |
| `tc_run_program` | 返回 0 | 返回 -1，设置运行时 diag | 不取得 program |
| `tc_typed_program_free` | 释放并清空 | — | 只对成功编译所得对象调用 |
| `tc_set_module_search_paths` | 返回 0 | OOM 返回 -1 | 必须在编译前调用；复制路径 |

0.0.39 新增 `tc_set_module_search_paths` 用于多文件模块系统；`tc_compile_source` 增加 `name` 参数以在诊断中区分源码名称。

---

## 3. `tc_compile_source`

```c
int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out,
                      TcDiagnostic *diag);
```

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

## 4. `tc_compile_file`

```c
int tc_compile_file(const char *path,
                    TcTypedProgram *out,
                    TcDiagnostic *diag);
```

### 当前行为

1. 从入口 `#program` 文件出发，按 `import` 语句逐层加载所有可达模块；
2. 在模块搜索路径中唯一定位 `.tc` 文件（入口文件所在目录 → `-I` 路径 → 默认路径）；
3. 检查模块依赖图 DAG（循环导入 → `TC_CE_CIRCULAR_IMPORT`）；
4. 对全部可达模块执行 13 阶段编译管线；
5. 返回前释放内部文件缓冲。

文件打开/读取失败时 `out` 不被修改。文件不存在使用 `TC_DIAG_API / TC_API_ERR_FILE_OPEN`；seek/read/close 失败使用 `TC_DIAG_API / TC_API_ERR_FILE_READ`，不会伪装成语言 `SyntaxError`。

模块搜索路径通过 `tc_set_module_search_paths` 或 CLI `-I` 选项设置。

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

## 6. `tc_set_module_search_paths`

```c
int tc_set_module_search_paths(char *const *paths, size_t count,
                               TcDiagnostic *diag);
```

设置导入模块的搜索路径（复制路径字符串；`count=0` 清空）。必须在编译前调用。路径按传入顺序搜索，入口文件所在目录总是最先搜索。成功返回 0；OOM 返回 -1。进程级全局，非线程安全。

---

## 7. 诊断生命周期

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

## 8. Typed program 所有权

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
| `tc_compile_file` 文件缓冲 | libtc | compile 返回前 |

### 禁止模式

- 编译失败后调用 `tc_typed_program_free`；
- 成功对象不释放或释放两次；
- 在 program 释放后执行；
- 对同一个 out 覆盖编译而未先释放旧对象；
- 多线程无同步地共享并释放同一对象。

---

## 9. 当前错误码

0.0.39 语言错误码覆盖编译器标准 §11.4 完整清单，包括：

- **通用与核心**（60+ 个 `TC_CE_*` 码）：语法、名称、类型、字面量、常量赋值、常量表达式、溢出、除零、转换溢出、比较、格式说明符、操作数数量、缩进、控制流、块结构等；
- **函数诊断**（20 个）：重名、冲突、未定义、参数、实参、调用位置、返回形式/类型、缺少返回、不可达语句、递归等；
- **memblock 诊断**（4 个）：索引越界（静态+运行时）、构造器实参数量、规划个数不匹配；
- **结构体诊断**（7 个）：字段缺失、未知字段、重复字段、顺序、不可变字段、重复定义、未定义；
- **模块诊断**（10 个）：层序错误、缺少可见性、模式误用、导入失败与循环等；
- **指针与 memcopy 诊断**（4 个）：空指针解引用/算术、memcopy 非法区间等；
- **运行时错误**（16 个 `TC_RE_*` 码）：除零、整数溢出、负移位、浮点异常、转换溢出、I/O、空指针、memblock 越界等。

TC 没有编译警告。

---

## 10. 性能计时

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

## 11. 最小示例

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

## 12. 当前能力边界

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

## 13. 0.0.39 兼容性

### 函数签名变更

0.0.39 在 0.0.31 基础上：

- `tc_compile_source` 新增 `name` 参数（源码名称，用于诊断和模块解析）；
- 新增 `tc_set_module_search_paths` 用于多文件模块搜索；
- `tc_run_typed` 重命名为 `tc_run_program`，行为涵盖模块静态初始化。

### 数据变化

0.0.31 的公共 compile/run/free 模式保持，但 `TcTypedProgram`、`TcDiagnostic`、`TcErrorKind` 和语句/RHS kind 枚举大幅扩展。嵌入方升级到 0.0.39 时需要重新编译，并审查直接访问结构字段或具体错误枚举的代码。

---

*当前调用契约以 [src/libtc/tc_lib.h](../src/libtc/tc_lib.h) 和本页为准；语言规则以 [TC 0.0.39 标准](./TC语言标准设计说明书-0.0.39.md) 与 [TC 编译器标准 0.0.39](./TC编译器标准设计说明书-0.0.39.md) 为准。*
