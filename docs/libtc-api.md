# libtc 嵌入 API

> **当前版本**：libtc / TC-VM v0.0.31
>
> **目标语言规范（唯一权威）**：[TC 0.0.31](./TC语言标准设计说明书.md)
>
> **文档职责**：本页是 0.0.31 调用者速查；内部设计见 [libtc 设计说明书](./libtc设计说明书.md)。

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

| 函数 | 成功 | 失败 | 所有权 |
| ---- | ---- | ---- | ------ |
| `tc_compile_source` | 返回 0，写入 typed program | 返回 -1，设置 diag | 仅成功时调用方取得 `out` |
| `tc_compile_file` | 返回 0，写入 typed program | 返回 -1，设置 diag | 仅成功时调用方取得 `out` |
| `tc_run_typed` | 返回 0 | 返回 -1，设置运行时 diag | 不取得 program |
| `tc_typed_program_free` | 释放并清空 | — | 只对成功编译所得对象调用 |

---

## 3. `tc_compile_source`

```c
int tc_compile_source(const char *source,
                      TcTypedProgram *out,
                      TcDiagnostic *diag);
```

### 当前行为

- 对完整 NUL 结尾 source 执行 Parse + Analyze；
- 成功后 `out` 包含程序树、符号表、CFG 和 warnings 字段；
- 当前分支支持 if/else、while/break/continue、受限 goto/label、bitcast、整数、bool 与浮点；
- 完整 CFG 确定初始化检查在编译阶段完成；
- `diag` 保存 source 副本以生成源码片段，调用返回后可释放原 source。

### 失败时 `out`

- Parse、Binder/Type、const eval 或 CFG 任一阶段失败时，`out` 都不被修改；
- libtc 回收本次调用创建的全部临时对象；
- 调用方不读取 `out`，也不调用 `tc_typed_program_free(out)`。

### 前置条件

- `source`、`out`、`diag` 为有效指针；
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

1. 读取整个文件；
2. 把 filename/source 复制到 diag；
3. 调用与 `tc_compile_source` 相同的 Parse + Analyze；
4. 返回前释放内部文件缓冲。

文件打开/读取失败时 `out` 不被修改。文件不存在使用 `TC_DIAG_API / TC_API_ERR_FILE_OPEN`；seek/read/close 失败使用 `TC_DIAG_API / TC_API_ERR_FILE_READ`，不会伪装成语言 `SyntaxError`。

---

## 5. `tc_run_typed`

```c
int tc_run_typed(const TcTypedProgram *program,
                 TcDiagnostic *diag);
```

- program 必须来自成功的 `tc_compile_source/file`；
- 每次调用建立新的 runtime slots；
- program 在执行期间只读；
- 可对同一个 program 重复调用；
- 成功返回 0；除零、溢出、cast、浮点或 I/O 运行时错误返回 -1；
- 失败后 program 仍归调用方，必须正常释放。

`tc_run_typed` 不打印诊断；调用方决定是否调用 `tc_diagnostic_print`。

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

当前 `TcDiagnostic` 包含：

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
  → zero or more tc_run_typed / AOT reads
  → tc_typed_program_free exactly once
```

### 所有权表

| 对象 | 所有者 | 释放 |
| ---- | ------ | ---- |
| 输入 source | 调用方 | 调用方；compile 返回后即可释放 |
| `TcDiagnostic` | 调用方 | `tc_diagnostic_clear` |
| 成功的 `TcTypedProgram` | 调用方 | `tc_typed_program_free` |
| program 内 AST/名称/if/while 子树 | typed program | 随 `tc_typed_program_free` 递归释放 |
| 符号、标签、常量值与 CFG | typed program | 随 `tc_typed_program_free` |
| runtime slots | libtc Executor | `tc_run_typed` 返回前 |
| `tc_compile_file` 文件缓冲 | libtc | compile 返回前 |

### 禁止模式

- 编译失败后调用 `tc_typed_program_free`；
- 成功对象不释放或释放两次；
- 在 program 释放后执行；
- 对同一个 out 覆盖编译而未先释放旧对象；
- 多线程无同步地共享并释放同一对象。

---

## 8. 当前 warnings

`TcTypedProgram` 仍有 `warnings` 字段，但 `TcWarningKind` 只有空壳 `TC_WARN_NONE`，当前没有活跃编译警告。调用方无需打印 warning list。

未初始化使用是静态错误，不是警告。

---

## 9. 性能计时

设置环境变量 `TC_BENCH` 即可让 libtc 向 stderr 输出阶段耗时：

```bash
TC_BENCH=1 ./my_program
```

当前阶段名：

```text
bench parse: <seconds> s
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
    static const char source[] =
        "var x: int32 = 7\n"
        "writeln(int32, %d, x)\n";
    TcDiagnostic diag;
    TcTypedProgram program;
    int rc = 0;

    tc_diagnostic_init(&diag);

    if (tc_compile_source(source, &program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (tc_run_typed(&program, &diag) != 0) {
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
| if/else、块作用域、缩进 | 支持 |
| goto/label 与跳转限制 | 支持 |
| while/break/continue 与范式隔离 | 支持 |
| float32/float64、cast、truncate、let 收敛 | 支持 |
| `var` 强制初始化器 | 支持 |
| bitcast | 支持 |
| 完整 CFG 固定点 | 支持 |
| success-only ownership 与诊断分域 | 支持 |
| 0.0.31 错误枚举最终清理 | 支持；41 个语言错误 + OutOfMemory 全表无重复 |

这些能力是正式 0.0.31 API 契约的一部分。

---

## 12. 0.0.31 发布兼容性

### 函数

0.0.31 未增加新的 compile/run/free 公共函数；现有调用序列保持。

### 发布门禁

- 旧错误与旧语义残留已删除；
- 标准错误枚举最终映射与合规报告已关闭；
- VM 459、unit 1726、AOT 272 以及 ASan、UBSan、no-fenv、平台内存门禁已通过；
- 实现版本为 0.0.31。

### 重新编译

函数签名保持源兼容，但 `TcTypedProgram`、`TcDiagnostic` 和枚举是公开结构。升级到 0.0.31 时，嵌入应用需要重新编译，并审查直接访问结构字段或具体错误枚举的代码。

### 交付判定

实现版本、全量测试和合规报告均已完成，调用方可把上述行为作为当前 API 契约。

---

*当前调用契约以 [src/libtc/tc_lib.h](../src/libtc/tc_lib.h) 和本页为准；语言规则以 [TC 0.0.31 标准](./TC语言标准设计说明书.md) 为准。*
