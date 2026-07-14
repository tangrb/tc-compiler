# TC-VM 命令行参考

> **版本**：0.0.26  
> **作者**：唐荣兵（[yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)）  
> **依赖**：[TC语言标准设计说明书.md](./TC语言标准设计说明书.md) v0.0.26  
> **工程**：[TC-Compiler](../README.md) 之 `tc-vm` 可执行文件（`TC_VM_VERSION` 0.0.26）  
> **定位**：`tc-vm` 命令行用法、退出码与诊断输出格式

---

## 目录

1. [概述](#1-概述)
2. [安装与路径](#2-安装与路径)
3. [用法](#3-用法)
4. [运行模式](#4-运行模式)
5. [退出码](#5-退出码)
6. [诊断输出](#6-诊断输出)
7. [错误类型](#7-错误类型)
8. [示例](#8-示例)
9. [与测试脚本的关系](#9-与测试脚本的关系)
- [附录 A：文档修订记录](#附录-a文档修订记录)

---

## 1. 概述

`tc-vm` 是 TC 语言的直接执行引擎入口：读取 `.tc` 源文件，经词法、语法、静态分析后执行，或将源文件仅做静态检查。

程序可通过 `write` / `writeln` 向 stdout 输出数据（支持 `%f`/`%e`/`%E`/`%g`/`%G` 浮点格式符），通过 `read` 从 stdin 读取数据（支持 `float32`/`float64` 输入）。

v0.0.26 起：受限 `goto`/`label`；读取未初始化变量为**静态错误**（不再发出编译警告）。文件模式支持 `if`/`goto`；REPL **拒绝**多行控制流（见 VM 详设 §18.8）。

语言语义以《[TC 语言标准设计说明书](./TC语言标准设计说明书.md)》为准；内部架构见《[TC-VM 详细设计说明书](./TC-VM详细设计说明书.md)》。

---

## 2. 安装与路径

在 [TC-Compiler](../README.md) 工程根目录构建后，可执行文件位于：

```text
build/vm/bin/tc-vm
```

构建方式：

```sh
make            # 或 make vm
# 等价于：
cmake -S . -B build && cmake --build build
```

---

## 3. 用法

```text
tc-vm [options] [<file.tc>]
```

| 选项 | 简写 | 说明 |
|------|------|------|
| `--check` | `-c` | 仅执行静态分析，不运行程序 |
| `--repl` | `-i` | 进入交互式 REPL 模式 |
| `--help` | `-h` | 显示帮助并退出（退出码 0） |
| `--version` | `-V` | 显示版本并退出（退出码 0） |
| `<file.tc>` | — | TC 源文件路径（文件模式下必填，恰好一个） |

选项与源文件路径的顺序任意；`--check` 可写在文件路径之前或之后。
REPL 模式与文件模式互斥：`--repl` 不可同时与 `<file.tc>` 或 `--check` 使用。

### 3.1 合法调用

```sh
# 文件模式
tc-vm tests/valid/example.tc
tc-vm --check tests/valid/example.tc
tc-vm tests/valid/example.tc --check   # 选项顺序灵活
tc-vm -c tests/valid/example.tc

# REPL 模式
tc-vm --repl
tc-vm -i

# 帮助与版本
tc-vm --help
tc-vm --version
```

### 3.2 非法调用

以下情况向 **stderr** 输出错误与用法提示，退出码为 **1**，不产生语言语义诊断：

```sh
tc-vm a.tc b.tc                    # 多余的位置参数
tc-vm --foo                        # 未知选项
tc-vm --repl tests/valid/example.tc   # REPL 模式与文件互斥
tc-vm --repl --check               # REPL 模式与 --check 互斥
```

用法提示格式（节选）：

```text
Usage: tc-vm [options] <file.tc>

Options:
  -c, --check       static analysis only, do not execute
  -h, --help        show this help and exit
  -V, --version     show version and exit
```

---

## 4. 运行模式

### 4.1 执行模式（默认）

```sh
tc-vm <file.tc>
```

流水线：读文件 → 逐行词法/语法 → 静态分析 → 顺序执行。

程序可通过 `write` / `writeln` 向 stdout 输出数据，通过 `read` 从 stdin 读取数据。

- 成功：程序执行完毕，向 stdout 可能产生输出（由程序中的 `write`/`writeln` 决定），退出码 **0**
- 失败：向 **stderr** 输出一条诊断，退出码 **1**

### 4.2 仅检查模式

```sh
tc-vm --check <file.tc>
```

流水线：读文件 → 逐行词法/语法 → 静态分析 → **不执行**。

适用于 CI 或编辑器集成中快速验证语法与类型，无需触发运行时错误（如除零）。

- 静态检查通过：无标准输出，退出码 **0**（v0.0.26 起当前无活跃编译警告种类；`TcWarningList` 为空壳预留）
- 静态检查失败：向 **stderr** 输出一条诊断，退出码 **1**（含未初始化变量、非法 `goto` 等静态错误）

### 4.3 交互式 REPL 模式

```sh
tc-vm --repl
tc-vm -i
```

进入交互式 REPL（Read-Eval-Print Loop），逐行输入 TC 语句并立即执行，变量定义与赋值状态跨行保留。支持三种类型输入：

| 输入类型 | 说明 |
|----------|------|
| TC 语句 | 逐条解析、分析并执行；语法/静态/运行时错误即时报告 |
| 内置元命令 | 以 `:` 开头，控制 REPL 会话状态 |
| 空行/注释行 | 忽略，不产生任何副作用 |

**内置元命令**：

| 命令 | 功能 |
|------|------|
| `:quit` / `:exit` / `:q` | 退出 REPL，返回退出码 0 |
| `:reset` | 清空当前会话所有变量定义与值 |
| `:vars` | 列出当前会话中所有变量及其类型与值 |
| `:help` | 显示 REPL 内置命令帮助 |

**会话行为**：

- 提示符为 `tc> `（输出到 stderr，仅 TTY 交互时显示）
- 每行输入经词法分析 → 语法分析 → 增量静态分析 → 执行，结果无显式输出（除非语句中有 `write`/`writeln`）
- 变量编号从 1 开始，每输入一行（无论是否可执行）递增
- 非 TTY 模式（如管道输入）不显示提示符和欢迎信息，静默处理输入直至 EOF
- `read()` 语句与 REPL 共享 stdin：请确保输入顺序正确
- 正常退出 REPL 返回退出码 **0**

---

## 5. 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 成功（执行完成，或 `--check` 下静态分析通过） |
| `1` | 失败（参数错误、I/O 错误、静态错误、运行时错误） |

不区分不同失败原因的退出码；具体原因见 stderr 诊断中的错误类型与消息（`tc_error_kind_name()`）。

---

## 6. 诊断输出

### 6.1 错误诊断格式

失败时，`tc-vm` 向 **stderr** 打印 **一条** 错误诊断（遇首个致命错误即停止）。

采用类 GCC/clang 的位置前缀格式：

**含行号与列号**（词法/语法错误等）：

```text
<file>:<行号>:<列号>: error: <消息>
  <出错行源码>
  <列指示符>
```

**含行号、无列号**（多数静态分析与运行时错误）：

```text
<file>:<行号>: error: <消息>
  <出错行源码>
```

**无行号**（文件 I/O 等）：

```text
<file>: error: <消息>
```

- **file**：输入的 `.tc` 文件路径；无文件上下文时为 `<source>`
- **行号**：1-based 源文件行号
- **列号**：1-based 字符位置（仅部分错误提供）；有列号时另附 `^` 指示符
- **消息**：英文简短说明

### 6.2 编译警告（预留）

`TcWarningList` 与 `warning: …` 打印路径仍保留，供未来警告种类使用。

v0.0.26 起：原「读取未初始化变量」警告已升级为静态错误 `UninitializedVariable`（`TC_ERR_UNINITIALIZED_VARIABLE`），**当前无活跃警告种类**；正常编译/执行路径下不应再出现未初始化相关 `warning:` 行。

### 6.3 文件 I/O 错误

无法打开或读取输入文件时，无行号：

```text
/path/to/missing.tc: error: cannot open input file
/path/to/broken.tc: error: cannot read input file
```

---

## 7. 错误类型

`ErrorKind` 字符串与 `tc_error_kind_name()` / 语言标准 §11.4 对齐；`SyntaxError` 为实现扩展（词法/语法/文件 I/O）。

| ErrorKind | 说明 | 典型阶段 |
|-----------|------|----------|
| `SyntaxError` | 词法 / 语法 / 部分文件 I/O | 词法 / 语法 |
| `OutOfMemory` | 内存分配失败 | 任意 |
| `UndefinedVariable` | 未定义标识符 | 静态分析 |
| `DuplicateDefinition` | 重复定义 | 静态分析 |
| `TypeMismatch` | 类型不匹配 | 静态分析 |
| `LiteralOutOfRange` | 字面量超出类型范围 | 词法 / 静态分析 |
| `LiteralTypeError` | 字面量类型错误 | 静态分析 |
| `OverflowModeError` | 溢出模式关键字非法 | 静态分析 |
| `KeywordError` | 关键字误用 | 静态分析 |
| `ConstantAssignmentError` | 对 `let` 赋值 | 静态分析 |
| `ConstantExpressionError` | 非常量表达式用于 `let` | 静态分析 |
| `ConstantCircularDependency` | `let` 循环依赖 | 静态分析 |
| `ConstantOverflow` | 常量算术溢出 | 静态分析 |
| `ConstantDivisionByZero` | 常量除零 | 静态分析 |
| `ConstantCastOverflow` | 常量转换溢出 | 静态分析 |
| `ComparisonTypeMismatch` | 比较运算类型不匹配 | 静态分析 |
| `FormatStringError` | 格式字符串非法 | 静态分析 |
| `FormatTypeMismatch` | 格式符与操作数类型不匹配 | 静态分析 |
| `OperandCountError` | 操作数数量错误 | 静态分析 |
| `IndentMixedError` | 空格与制表符混用 (v0.0.24) | 语法 |
| `IndentInsufficientError` | 块内缩进不足 (v0.0.24) | 语法 |
| `IndentElseEndError` | `else`/`end` 缩进与 `if` 不一致 (v0.0.24) | 语法 |
| `MissingEndError` | `if` 缺少 `end` (v0.0.24) | 语法 |
| `ElsePositionError` | `else` 位置错误 (v0.0.24) | 语法 |
| `ConditionTypeError` | `if` 条件非 `bool` (v0.0.24) | 静态分析 |
| `CrossBlockReferenceError` | 跨块引用局部变量 (v0.0.24) | 静态分析 |
| **`UninitializedVariable`** (v0.0.26) | 存在读取未初始化变量的可达路径 | 静态分析 |
| **`LabelNotFound`** (v0.0.26) | `goto` 目标标签未定义 | 静态分析 |
| **`DuplicateLabel`** (v0.0.26) | 同作用域重复定义标签 | 静态分析 |
| **`JumpIntoBlockError`** (v0.0.26) | `goto` 跳入内层子块 | 静态分析 |
| **`JumpToSiblingBlockError`** (v0.0.26) | `goto` 跳入兄弟分支 | 静态分析 |
| `DivisionByZero` | 除零（含浮点严格模式） | 执行 |
| `IntegerOverflow` | 整数溢出 | 执行 |
| `FloatOverflow` | 浮点上溢（严格模式）(v0.0.25) | 执行 |
| `FloatUnderflow` | 浮点下溢（严格模式）(v0.0.25) | 执行 |
| `FloatInvalidOperation` | 浮点无效操作（严格模式）(v0.0.25) | 执行 |
| `FloatCastOverflow` | 浮点↔整数转换超范围 (v0.0.25) | 执行 |
| `CastOverflow` | 整数转换溢出 | 执行 |
| `ModeMismatch` | `ieee`/`wrap` 用于非法上下文 (v0.0.25) | 静态分析 |
| `IOError` | `read` 输入失败 | 执行 |

完整触发条件见语言标准 §11；实现架构见《TC-VM 详细设计说明书》§11。

---

## 8. 示例

### 8.1 执行成功

```sh
$ tc-vm tests/valid/example.tc
$ echo $?
0
```

### 8.2 静态检查

```sh
$ tc-vm --check tests/valid/example.tc
$ echo $?
0
```

### 8.3 重复定义（静态）

```sh
$ tc-vm tests/errors/static/duplicate_def.tc
tests/errors/static/duplicate_def.tc:2: error: duplicate definition of 'a'
  var a: int32 = 1
$ echo $?
1
```

### 8.4 除零（运行时）

```sh
$ tc-vm tests/errors/runtime/div_zero.tc
tests/errors/runtime/div_zero.tc:3: error: division by zero
  x = div(int32, x, 0)
$ echo $?
1
```

### 8.5 语法错误（含列号）

```sh
$ tc-vm /path/to/invalid.tc
/path/to/invalid.tc:1:12: error: unexpected character
  var x: int32 = @
           ^
```

### 8.6 带 I/O 的程序执行（管道）

```sh
$ echo -e "10\n20" | tc-vm tests/valid/add.tc
30
$ echo $?
0
```

### 8.7 I/O 输入错误（运行时）

```sh
$ echo "abc" | tc-vm tests/valid/add.tc
tests/valid/add.tc:1: error: invalid input
  read(int32, a)
$ echo $?
1
```

### 8.8 REPL 交互（TTY）

```text
$ tc-vm --repl
TC-VM interactive mode. Enter one TC statement per line.

Meta commands:
  :quit, :exit, :q   exit REPL
  :reset             clear all variables
  :vars              list current variables
  :help              show this help

Note: read() and REPL both use stdin.

tc> var a: int32 = 10
tc> var b: int32 = 20
tc> var sum: int32 = add(int32, a, b)
tc> :vars
a: int32 = 10
b: int32 = 20
sum: int32 = 30
tc> :quit
```

### 8.9 REPL 管道模式

```sh
$ printf 'var a: int32 = 10\nvar b: int32 = 20\nwriteln(int32, add(int32, a, b))\n:quit\n' | tc-vm --repl
30
$ echo $?
0
```

### 8.10 浮点运算示例（v0.0.25）

```sh
$ tc-vm tests/valid/fp_basic.tc
3.14159
$ echo $?
0
```

### 8.11 浮点 I/O 管道示例（v0.0.25）

```sh
$ echo "3.14" | tc-vm tests/valid/fp_read.tc
input: 3.14
$ echo $?
0
```

---

## 9. 与测试脚本的关系

工程内一致性测试通过 `make test`（或 `make test-vm`）调用 `scripts/vm/run_tests.sh`，该脚本对 `build/vm/bin/tc-vm` 执行以下约定：

| 用例目录 | 调用方式 | 预期 |
|----------|----------|------|
| `tests/valid/` | `tc-vm <file>` | 退出码 0；部分用例校验 stdout |
| `tests/errors/static/` | `tc-vm <file>` | 退出码非 0；诊断含 `file:line: error:` |
| `tests/errors/runtime/` | `tc-vm <file>` | 退出码非 0；诊断含 `file:line: error:` |
| `tests/stress/` | `tc-vm <file>` | 退出码 0；stdout 期望 |
| 静态错误子集 | `tc-vm --check <file>` | 退出码非 0 |
| REPL | 管道输入 `tc-vm --repl` | 输出含期望子串 |

可选 ASAN 构建：`ASAN=1 make test` 或 `scripts/vm/run_tests.sh --asan`（使用 `build-asan/vm/bin/tc-vm`）。

---

## 附录 A：文档修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-07-01 | 首版：tc-vm 命令行、退出码与诊断格式 |
| 1.1 | 2026-07-01 | 采用 getopt 解析；新增 `--help`/`--version`；选项顺序灵活 |
| 1.2 | 2026-07-01 | 补充 I/O 支持描述；新增 I/O 管道执行示例；错误类型表新增 `IOError` |
| 1.3 | 2026-07-01 | 新增 `-i/--repl` 选项及交互式 REPL 模式（§4.3）；新增 REPL 使用示例（§8.8、§8.9）；修复 I/O 错误示例消息；更新用法格式为文件可选 |
| **0.0.13** | **2026-07-02** | **版本号对齐语言标准 v0.0.13；错误类型表新增 `LiteralTypeError`、`KeywordError`、`ConstantAssignmentError`、`ConstantExpressionError`；更正语言标准引用为 §11；更新退出码与诊断说明中的版本引用** |
| **0.0.14** | **2026-07-03** | **与实现对齐**：新增 §6.2 编译警告格式；`--check` 模式纳入自动化回归；补充 stress/REPL/ASAN 测试说明；诊断格式校验说明 |
| **0.0.18** | **2026-07-03** | **版本号对齐语言标准 v0.0.18**；`TC_VM_VERSION` 集中于 `tc_version.h` |
| **0.0.21** | **2026-07-04** | **与语言标准 v0.0.21 对齐**：错误类型表新增 `ConstantCircularDependency`、`ConstantOverflow`、`ConstantDivisionByZero`、`ConstantCastOverflow`、`ComparisonTypeMismatch`、`FormatStringError`、`FormatTypeMismatch`、`OperandCountError`；版本、依赖更新至 v0.0.21 |
| **0.0.25** | **2026-07-13** | **浮点支持**：错误类型表新增 `FloatOverflow`/`FloatUnderflow`/`FloatInvalidOperation`/`FloatCastOverflow`/`ModeMismatch`；概述补充浮点 I/O 与格式符说明；新增 §8.10–§8.11 浮点示例；版本、依赖更新至 v0.0.25 |
| **0.0.26** | **2026-07-14** | **对齐语言标准 v0.0.26**：版本/依赖升版；§6.2 未初始化改为静态错误说明；§7 补齐 if/缩进与 goto/label/未初始化错误码；概述补充控制流限制；修正 `OutOfMemory` 名称与测试约定中的警告表述 |

---
