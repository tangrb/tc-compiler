# TC-VM 命令行参考

> **当前版本**：TC-VM v0.0.31
>
> **目标语言规范**：[TC 0.0.31](./TC语言标准设计说明书_0.0.31.md)
>
> **文档职责**：本页准确描述 0.0.31 可执行文件行为与兼容边界。
>
> **内部架构**：[TC-VM 详细设计说明书](./TC-VM详细设计说明书.md)

---

## 目录

1. [概述](#1-概述)
2. [用法与选项](#2-用法与选项)
3. [文件执行模式](#3-文件执行模式)
4. [静态检查模式](#4-静态检查模式)
5. [REPL 模式](#5-repl-模式)
6. [输出、诊断与退出码](#6-输出诊断与退出码)
7. [当前错误种类](#7-当前错误种类)
8. [性能计时](#8-性能计时)
9. [当前示例](#9-当前示例)
10. [0.0.31 迁移与兼容边界](#10-0031-迁移与兼容边界)
11. [测试脚本约定](#11-测试脚本约定)

---

## 1. 概述

`tc-vm` 是 TC 源文件的直接执行引擎。0.0.31 提供：

- 批量编译并执行一个 `.tc` 文件；
- 只做静态检查；
- 单行交互式 REPL；
- 整数、bool、float32/float64；
- if/else 块与缩进检查；
- 受限 goto/label；
- while/break/continue 与循环范式隔离；
- 等宽 `bitcast`、收敛后的 cast/float/let 语义；
- 强制 `var` 初始化器和完整 CFG 确定初始化检查；
- Language、API/Environment、Implementation 三类诊断域。

上述能力均已通过 M10 发布门禁。

查看实际版本：

```bash
build/vm/bin/tc-vm --version
```

当前输出：

```text
tc-vm 0.0.31
```

---

## 2. 用法与选项

### 2.1 用法

```text
tc-vm [options] [<file.tc>]
```

实际程序要求：

- 文件模式必须且只能给出一个输入文件；
- REPL 模式不能同时给出文件；
- `--check` 不能与 `--repl` 同用。

### 2.2 选项

| 短选项 | 长选项 | 行为 | 输出 | 成功退出码 |
| ------ | ------ | ---- | ---- | ---------- |
| `-c` | `--check` | 只编译与静态分析，不执行 | 成功时默认无输出 | 0 |
| `-i` | `--repl` | 进入交互模式 | TTY 下显示帮助和提示符 | 0 |
| `-h` | `--help` | 显示帮助并退出 | stderr | 0 |
| `-V` | `--version` | 显示版本并退出 | stdout | 0 |

当前不存在 `--bench` 选项；性能计时使用环境变量 `TC_BENCH=1`，见 §8。

### 2.3 非法组合

| 命令 | 结果 |
| ---- | ---- |
| `tc-vm` | 缺少输入文件，stderr 帮助，退出 1 |
| `tc-vm a.tc b.tc` | 多余参数，退出 1 |
| `tc-vm --repl a.tc` | `--repl` 与文件冲突，退出 1 |
| `tc-vm --check --repl` | 两模式冲突，退出 1 |
| `tc-vm --unknown` | 选项解析失败，stderr 帮助，退出 1 |

---

## 3. 文件执行模式

### 3.1 命令

```bash
build/vm/bin/tc-vm path/to/program.tc
```

### 3.2 流水线

```text
read file
  → Parse
  → Analyze
  → print warnings if any
  → Execute
  → free typed program
```

当前分支没有活跃 warning kind，正常路径不输出编译警告。

### 3.3 输出

- TC `write`/`writeln` 输出到 stdout；
- 编译或运行时错误输出到 stderr；
- 成功且程序本身无输出时，命令保持安静；
- 文件打开/读取失败也输出到 stderr。

### 3.4 文件名

程序不强制检查 `.tc` 扩展名；输入参数会作为文件路径交给 libtc。建议使用 `.tc` 以保持工具链和测试约定一致。

---

## 4. 静态检查模式

### 4.1 命令

```bash
build/vm/bin/tc-vm --check path/to/program.tc
build/vm/bin/tc-vm -c path/to/program.tc
```

### 4.2 行为

`--check` 执行文件读取、Parse 和 Analyze，但不执行任何 TC 语句。因此：

- 不产生程序 stdout；
- 不读取 TC `read` 的 stdin；
- 不触发仅运行时才能出现的除零、溢出或 I/O 错误；
- 会报告语法、名称、类型、缩进、控制流、bitcast 和 CFG 确定初始化等静态错误。

### 4.3 结果

| 状态 | stdout | stderr | 退出码 |
| ---- | ------ | ------ | ------ |
| 静态检查通过 | 空 | 默认空 | 0 |
| 静态检查失败 | 空 | 单条诊断 | 1 |
| 文件/API 失败 | 空 | 工具诊断 | 1 |

---

## 5. REPL 模式

### 5.1 启动

```bash
build/vm/bin/tc-vm --repl
build/vm/bin/tc-vm -i
```

TTY 下提示符写到 stderr：

```text
tc>
```

### 5.2 当前模型

REPL 每次读取一行，依次做 Lexer → Parser → 增量 Analyze → Execute。成功定义的变量和常量、变量当前值与语句历史在会话内保留。

空行、全空白行和以 `;` 开头的注释行被忽略。单行错误会打印诊断并继续会话；EOF 或退出命令结束 REPL。

### 5.3 内置命令

| 命令 | 行为 |
| ---- | ---- |
| `:quit` / `:exit` / `:q` | 退出 |
| `:reset` | 清空变量、值和会话历史；打印 `session reset` |
| `:vars` | 列出当前符号和值；空时打印 `(no variables)` |
| `:help` | 显示 REPL 帮助 |

未知 `:<command>` 输出：

```text
unknown command: :<command> (try :help)
```

### 5.4 控制流限制

当前 REPL 显式拒绝：

- `if`；
- `while`；
- `goto`；
- `label`；
- `break`；
- `continue`。

相应消息为 `... statements are not supported in REPL mode`，诊断域为 API/Environment，当前打印前缀为 `api error: InvalidArgument`。`bitcast` 是单行 RHS，可在 REPL 支持的普通语句中使用。

这些限制是 tc-vm 的单行交互能力边界，不是 TC 0.0.31 批量源文件语法规则。失败语句不会提交新变量或常量，也不会修改已经提交的值。

### 5.5 stdin 注意事项

REPL 自身和 TC `read` 都读取 stdin。在自动化输入中，`read` 会消费后续输入行；请按执行顺序提供数据。

### 5.6 退出码

`tc_repl_run` 在正常 EOF 或用户退出时返回 0。单行 TC 错误不会把整个 REPL 会话退出码改为 1。

---

## 6. 输出、诊断与退出码

### 6.1 退出码

| 退出码 | 含义 |
| ------ | ---- |
| 0 | 帮助/版本成功、文件执行成功、静态检查成功或 REPL 正常结束 |
| 1 | 命令行用法错误、文件/编译失败或运行时失败 |

当前没有更细的进程退出码。程序化调用方通过 libtc 的 `TcDiagnostic.domain` 区分 Language、API/Environment 与 Implementation；Language/Implementation 使用 `kind`，API/Environment 使用 `api_code`。CLI 只返回 0/1。

### 6.2 诊断格式

当前 `tc_diagnostic_print` 的基本格式：

```text
<filename>:<line>:<column>: error: <message>
  <source line>
  <spaces>^
<filename>: api error: <ApiCode>: <message>
<filename>: implementation error: <ErrorKind>: <message>
```

行或列未知时相应位置会省略。CLI 当前打印人类可读 message，不把 `SyntaxError`、`TypeMismatch` 等 kind 名直接写在诊断首行。

### 6.3 单槽

编译和执行均 fail-fast，只打印第一条错误。修复后重新运行才能看到下一条。

### 6.4 stdout/stderr 分工

| 内容 | 流 |
| ---- | -- |
| TC `write`/`writeln` | stdout |
| `--version` | stdout |
| 命令帮助 | stderr |
| 错误诊断 | stderr |
| REPL 提示符 | stderr |
| REPL 帮助/`:vars`/`:reset` | stdout |
| `TC_BENCH=1` 性能行 | stderr |

---

## 7. 当前错误种类

### 7.1 说明

下表完整描述 0.0.31 的 `TcErrorKind`，用于理解实现和 libtc 程序化接口。Language 诊断默认打印 message，不直接打印这些名称。

### 7.2 词法、语法、名称与类型

| 打印名 | 典型条件 |
| ------ | -------- |
| `SyntaxError` | Token 或语法错误；不包含文件错误和 REPL 能力限制 |
| `UndefinedVariable` | 名称未定义或前向引用 |
| `DuplicateDefinition` | 同一作用域重复 var/let |
| `TypeMismatch` | 运算、赋值或 operand 类型不匹配 |
| `LiteralOutOfRange` | 字面量超范围 |
| `LiteralTypeError` | 字面量类别/后缀与上下文冲突 |
| `KeywordError` | 关键字位置或旧模式组合错误 |
| `ComparisonTypeMismatch` | 比较 operand 类型不一致 |
| `ModeMismatch` | 浮点/模式组合不合法 |
| `BitcastWidthError` | bitcast 源/目标位宽不相等 |

### 7.3 常量、格式与 I/O

| 打印名 | 典型条件 |
| ------ | -------- |
| `ConstantAssignmentError` | 给 let 赋值 |
| `ConstantExpressionError` | let RHS 非法 |
| `ConstantOverflow` | 编译期溢出 |
| `ConstantDivisionByZero` | 编译期除零 |
| `ConstantCastOverflow` | 编译期转换超范围 |
| `FormatStringError` | 非法格式符 |
| `FormatTypeMismatch` | 格式符与类型不匹配 |
| `OperandCountError` | operand 数量错误 |
| `IOError` | TC 运行时 read/write 错误 |
| `OutOfMemory` | 实现内存分配失败 |

### 7.4 运行时数值

| 打印名 | 典型条件 |
| ------ | -------- |
| `DivisionByZero` | 整数/严格浮点除零 |
| `IntegerOverflow` | 严格整数运算超范围 |
| `CastOverflow` | 整数或浮点严格转换超范围 |
| `FloatOverflow` | 严格浮点上溢 |
| `FloatUnderflow` | 严格浮点下溢 |
| `FloatInvalidOperation` | 严格浮点无效操作 |

### 7.5 块、控制流与初始化

| 打印名 | 典型条件 |
| ------ | -------- |
| `IndentMixedError` | 空格/制表符混用 |
| `IndentInsufficientError` | 块内缩进不足/层级非法 |
| `IndentElseEndError` | else/end 未与 if 对齐 |
| `MissingEndError` | if 缺 end |
| `ElsePositionError` | else 位置非法 |
| `ConditionTypeError` | if 条件非 bool |
| `UninitializedVariable` | 路径敏感分析发现未初始化使用 |
| `VarMissingInitializer` | var 声明缺少初始化器 |
| `LabelNotFound` | goto 目标不存在 |
| `DuplicateLabel` | 同一作用域重复标签 |
| `JumpIntoBlockError` | goto 跳入子块 |
| `JumpToSiblingBlockError` | goto 跳入兄弟块 |
| `LabelInsideLoop` | while 内出现 label |
| `GotoInsideLoop` | while 内出现 goto |
| `BreakOutsideLoop` | while 外出现 break |
| `ContinueOutsideLoop` | while 外出现 continue |

### 7.6 警告

0.0.31 没有活跃 `TcWarningKind`。`TcWarningList` 仍在内部/API 结构中，但正常编译不产生 warning 行。

---

## 8. 性能计时

### 8.1 启用

当前没有 `--bench` 命令行选项。使用：

```bash
TC_BENCH=1 build/vm/bin/tc-vm program.tc
TC_BENCH=1 build/vm/bin/tc-vm --check program.tc
```

### 8.2 输出

性能行写到 stderr：

```text
bench parse: 0.000123 s
bench analyze: 0.000456 s
bench execute: 0.000078 s
```

`--check` 不含 execute。环境变量存在即启用，值不要求为 `1`；文档示例使用 `1` 以表达意图。

### 8.3 注意

性能行会与错误诊断共享 stderr。自动化比较 stderr 时不要设置 `TC_BENCH`。

---

## 9. 当前示例

以下示例适用于 v0.0.31，并避免依赖已经废止的旧写法。

### 9.1 执行

```text
var a: int32 = 10
var b: int32 = 20
var sum: int32 = add(int32, a, b)
writeln(int32, %d, sum)
```

```bash
build/vm/bin/tc-vm sum.tc
```

输出：

```text
30
```

### 9.2 检查

```bash
build/vm/bin/tc-vm --check sum.tc
```

检查通过时无输出，退出 0。

### 9.3 受限 goto 非结构化循环

```text
var i: int32 = 0
label start:
i = add(int32, i, 1)
if lt(int32, i, 10) then
    goto start
end
writeln(int32, %d, i)
```

当前输出 `10`。0.0.31 仍保留这种 while 外的 if + goto 范式。

### 9.4 管道式 REPL

```bash
printf 'var x: int32 = 7\nwriteln(int32, %d, x)\n:q\n' | build/vm/bin/tc-vm --repl
```

非 TTY 输入不显示启动帮助和提示符。

---

## 10. 0.0.31 迁移与兼容边界

### 10.1 当前发布状态

| 0.0.31 项目 | 当前状态 |
| ----------- | ----------------- |
| `var` 声明必须有 RHS | 已实现，缺失时为专用静态错误 |
| `while`/`break`/`continue` | 已实现；REPL 单行模式明确拒绝 |
| while 内禁止 goto/label | 已实现 |
| 完整 CFG 固定点 | 已实现并由 VM/AOT 共用 typed program |
| `bitcast` | 已实现 |
| 浮点、cast、truncate、let 收敛 | 已实现并完成 VM/AOT 差分 |
| 文件/API/REPL 与语言诊断分域 | 已实现 |
| 41 个语言错误 + OutOfMemory | 已完成全表、完整性与唯一性验收 |
| 发布版本号 | 0.0.31 |

### 10.2 预计不变的 CLI

0.0.31 不新增命令行选项。文件执行与 `--check` 走同一完整 libtc 批量流水线；`tc-aot --check` 使用相同接受集。

### 10.3 REPL

0.0.31 只规范批量源文件。tc-vm 可以继续限制多行控制流，但应把限制作为 REPL 能力错误，而不是声称 while/if/goto 在 TC 语言中非法。

### 10.4 迁移提示

- 依赖浮点 wrap 的源文件改用 ieee 数值语义，或 bitcast 到等宽整数后处理位模式；
- 依赖浮点 truncate 位重解释的源文件改用 bitcast；
- 无 RHS 的 `var` 改为声明时初始化；
- 嵌入调用方应按新的诊断域读取 `domain`、`api_code` 或 `kind`；
- 嵌入应用升级时需重新编译，并审查直接依赖旧错误枚举或公开结构布局的代码。

---

## 11. 测试脚本约定

### 11.1 当前入口

```bash
bash scripts/run_tests.sh
bash scripts/vm/run_tests.sh
bash scripts/vm/run_tests.sh --filter <name>
```

### 11.2 标签

VM 脚本输出中的 `OK`、`OUT`、`ERR`、`CHK`、`CFL` 等是测试运行器标签，不是 tc-vm 用户输出。

### 11.3 0.0.31 发布基线

0.0.31 发布门禁实测为 VM 435、unit 1617、AOT 257；后续变更仍以脚本实际汇总为准。

---

*当前命令行为以 0.0.31 源码与本页为准，语言规则以 [TC 0.0.31 标准](./TC语言标准设计说明书_0.0.31.md) 为准。*
