# TC-VM 命令行参考

> **当前版本**：TC-VM v0.0.41
>
> **目标语言规范（唯一权威）**：[TC 0.0.41](./TC语言标准设计说明书-0.0.41.md)
>
> **文档职责**：本页准确描述 0.0.41 可执行文件行为与兼容边界。
>
> **内部架构**：[TC-VM 详细设计说明书](./TC-VM详细设计说明书-0.0.41.md)

---

## 目录

1. [概述](#1-概述)
2. [用法与选项](#2-用法与选项)
3. [文件执行模式](#3-文件执行模式)
4. [静态检查模式](#4-静态检查模式)
5. [输出、诊断与退出码](#5-输出诊断与退出码)
6. [当前错误种类](#6-当前错误种类)
7. [性能计时](#7-性能计时)
8. [当前示例](#8-当前示例)
9. [0.0.41 迁移与兼容边界](#9-0041-迁移与兼容边界)
10. [测试脚本约定](#10-测试脚本约定)

---

## 1. 概述

`tc-vm` 是 TC 源文件的直接执行引擎。0.0.41 提供：

- 多文件批量编译并执行（`#program` + `import` 引用 `#lib` 模块）；
- 只做静态检查（`--check`）；
- 整数、bool、float32/float64；
- `isize` / `usize` 平台字长类型；
- `ptr<T>` 指针类型及全部 `ptr_*` 指令；
- `memblock<T, N>` 内存块类型及深拷贝语义；
- `struct` 用户定义结构体；
- `void` 函数返回；
- 函数定义、`funcall` 调用、命名实参、`return`；
- `#lib` / `#program` 模块系统、`import`、`public`/`private`、`Self`；
- `static var` / `static let` 模块静态成员；
- if/else 块与缩进检查；
- while/break/continue 与循环范式隔离；
- 函数内受限 goto/label（`while` 内禁止）；
- 等宽 `bitcast`、收敛后的 cast/float/let 语义；
- 强制 `var` 初始化器和完整 CFG 确定初始化检查；
- 无环函数调用图检查；
- 13 阶段确定性编译管线；
- Language、API/Environment、Implementation 三类诊断域。

查看实际版本：

```bash
build/vm/bin/tc-vm --version
```

预期输出：

```text
tc-vm 0.0.41
```

---

## 2. 用法与选项

### 2.1 用法

```text
tc-vm [options] [<file.tc>]
```

实际程序要求：

- 文件模式至少给出一个输入文件（`#program` 入口）；
- 可选通过 `-I` 指定模块搜索路径；
- `--check` 不与 `-I` 冲突。

### 2.2 选项

| 短选项 | 长选项 | 行为 | 输出 | 成功退出码 |
| ------ | ------ | ---- | ---- | ---------- |
| `-c` | `--check` | 只编译与静态分析（含模块解析），不执行 | 成功时默认无输出 | 0 |
| `-I <path>` | `--include <path>` | 添加模块搜索路径（可多次指定） | — | — |
| `-h` | `--help` | 显示帮助并退出 | stderr | 0 |
| `-V` | `--version` | 显示版本并退出 | stdout | 0 |

当前不存在 `--bench` 选项；性能计时使用环境变量 `TC_BENCH=1`，见 §7。

### 2.3 非法组合

| 命令 | 结果 |
| ---- | ---- |
| `tc-vm` | 缺少输入文件，stderr 帮助，退出 1 |
| `tc-vm --unknown` | 选项解析失败，stderr 帮助，退出 1 |

---

## 3. 文件执行模式

### 3.1 命令

```bash
build/vm/bin/tc-vm path/to/program.tc
build/vm/bin/tc-vm -I ./lib path/to/program.tc
```

### 3.2 流水线

```text
read entry file + import resolution
  → module dependency DAG check
  → Parse all reachable modules
  → Analyze (13 phases)
  → Execute
  → free typed program
```

当前分支没有活跃 warning kind，正常路径不输出编译警告。

### 3.3 输出

- TC `write`/`writeln` 输出到 stdout；
- `writeln` 输出单个 LF（`\n`），不进行 CRLF 改写；输出以一次写入原子提交。
- 编译或运行时错误输出到 stderr；
- 成功且程序本身无输出时，命令保持安静；
- 文件打开/读取失败也输出到 stderr。

### 3.4 文件名

程序不强制检查 `.tc` 扩展名；输入参数会作为文件路径交给 libtc。建议使用 `.tc` 以保持工具链和测试约定一致。

### 3.5 多文件模块

`#program` 入口文件通过 `import` 引用 `#lib` 模块。编译器在阶段 4 自动加载并解析所有可达模块。模块搜索路径优先级：

1. 入口文件所在目录；
2. `-I` 指定的路径（按参数顺序）；
3. 默认搜索路径。

---

## 4. 静态检查模式

### 4.1 命令

```bash
build/vm/bin/tc-vm --check path/to/program.tc
build/vm/bin/tc-vm -c path/to/program.tc
build/vm/bin/tc-vm -c -I ./lib path/to/program.tc
```

### 4.2 行为

`--check` 执行文件读取、模块解析、全部 13 个阶段的静态分析，但不执行任何 TC 语句。因此：

- 不产生程序 stdout；
- 不读取 TC `read` 的 stdin；
- 不触发仅运行时才能出现的除零、溢出或 I/O 错误；
- 会报告语法、名称、类型、缩进、控制流、模块、函数、bitcast 和 CFG 确定初始化等所有静态错误；
- 会执行 `let`/`static let` 编译期求值并报告常量求值错误。

### 4.3 结果

| 状态 | stdout | stderr | 退出码 |
| ---- | ------ | ------ | ------ |
| 静态检查通过 | 空 | 默认空 | 0 |
| 静态检查失败 | 空 | 单条诊断 | 1 |
| 文件/API 失败 | 空 | 工具诊断 | 1 |

---

## 5. 输出、诊断与退出码

### 5.1 退出码

| 退出码 | 含义 |
| ------ | ---- |
| 0 | 帮助/版本成功、文件执行成功、静态检查成功 |
| 1 | 命令行用法错误、文件/编译失败或运行时失败 |

当前没有更细的进程退出码。程序化调用方通过 libtc 的 `TcDiagnostic.domain` 区分 Language、API/Environment 与 Implementation；Language/Implementation 使用 `kind`，API/Environment 使用 `api_code`。CLI 只返回 0/1。

### 5.2 诊断格式

当前 `tc_diagnostic_print` 的基本格式：

```text
<filename>:<line>:<column>: error: <message>
  <source line>
  <spaces>^
<filename>: api error: <ApiCode>: <message>
<filename>: implementation error: <ErrorKind>: <message>
```

行或列未知时相应位置会省略。CLI 当前打印人类可读 message，不把 `SyntaxError`、`TypeMismatch` 等 kind 名直接写在诊断首行。

### 5.3 单槽

编译和执行均 fail-fast，只打印第一条错误。修复后重新运行才能看到下一条。

### 5.4 stdout/stderr 分工

| 内容 | 流 |
| ---- | -- |
| TC `write`/`writeln` | stdout |
| `--version` | stdout |
| 命令帮助 | stderr |
| 错误诊断 | stderr |
| `TC_BENCH=1` 性能行 | stderr |

---

## 6. 当前错误种类

### 6.1 说明

下表描述 0.0.41 的 `TcErrorKind` 完整映射。Language 诊断默认打印 message，不直接打印这些名称。完整错误码清单、阶段归属与触发条件见编译器标准 §11.4。

**86 码口径**：86 = 85 个语言码（附录 B：73 `TC_CE` + 12 `TC_RE`）+ 1 `TC_ERR_OUT_OF_MEMORY`。

### 6.2 词法、语法、名称与类型

| 打印名 | 典型条件 |
| ------ | -------- |
| `SyntaxError` | Token 或语法错误；不包含文件错误 |
| `UndefinedVariable` | 名称未定义或前向引用 |
| `DuplicateDefinition` | 同一作用域重复 var/let/形参 |
| `TypeMismatch` | 运算、赋值或 operand 类型不匹配（含 ptr 跨类型） |
| `LiteralOutOfRange` | 字面量超范围 |
| `LiteralTypeError` | 字面量类别/后缀与上下文冲突 |
| `ComparisonTypeMismatch` | 比较 operand 类型不一致 |
| `ModeMismatch` | 浮点/模式组合不合法 |
| `BitcastWidthError` | bitcast 源/目标位宽不相等 |
| `StructMissingField` | 结构体值构造器缺失字段 |
| `StructUnknownField` | 结构体未知字段名 |
| `StructDuplicateField` | 结构体值构造器/定义字段重复 |
| `StructFieldOrderError` | 结构体值构造器字段顺序错误 |
| `StructImmutableFieldError` | 对 let 字段赋值 |
| `StructValueSelfRefError` | 字段在值位置引用正在定义的本结构体（§3.9.1） |
| `DuplicateStruct` | 同模块重复 struct 定义 |
| `UndefinedStruct` | 未定义的结构体类型名（含导入结构体裸名、未 import 的 `Mod.Name`） |
| `PrivateMemberAccessError` | 外部访问 private 成员（含 private struct 类型名/构造器） |
| `MemblockIndexOutOfRange` (static) | 编译期可确定的 memblock 越界 |
| `MemblockElementCountMismatch` | memblock 构造器逐值数量 != count |
| `MemblockSizeMismatch` | memblock 赋值/传参 N 不匹配 |
| `MemcopyUnsafeInvalidRange` (static) | memcopy_unsafe 的 length 或下标编译期数学值 `< 0` |

### 6.3 常量、格式与 I/O

| 打印名 | 典型条件 |
| ------ | -------- |
| `ConstantAssignmentError` | 对 let/static let 赋值；经 ptr_address 取 let/static let/**形参**地址后再 ptr_store / memcopy_unsafe |
| `ConstantExpressionError` | let RHS 非法形态 |
| `ConstantOverflow` | 编译期溢出 |
| `ConstantDivisionByZero` | 编译期除零 |
| `ConstantCastOverflow` | 编译期转换超范围 |
| `FormatSpecifierError` | 非法格式控制项 |
| `FormatTypeMismatch` | 格式符与类型不匹配 |
| `OperandCountError` | operand 数量错误 |
| `IOError` | TC 运行时 read/write 错误 |
| `OutOfMemory` | 实现内存分配失败 |

### 6.4 运行时数值

| 打印名 | 典型条件 |
| ------ | -------- |
| `DivisionByZero` | 整数/严格浮点除零 |
| `IntegerOverflow` | 严格整数运算超范围 |
| `NegativeShiftCount` | shl/shr 移位计数为负 |
| `CastOverflow` | 整数或浮点严格转换超范围 |
| `FloatOverflow` | 严格浮点上溢 |
| `FloatUnderflow` | 严格浮点下溢 |
| `FloatInvalidOperation` | 严格浮点无效操作 |
| `MemblockIndexOutOfRange` (runtime) | 运行时 memblock 下标/区间越界 |
| `MemcopyUnsafeInvalidRange` (runtime) | 运行时 memcopy_unsafe 的 length 或下标数学值 `< 0` |
| `NullPointerDereference` | ptr_load/store/memcopy_unsafe/序比较操作数为 nullptr |
| `NullPointerArithmetic` | ptr_add/sub 操作数为 nullptr |

### 6.5 块、控制流与初始化

| 打印名 | 典型条件 |
| ------ | -------- |
| `IndentMixedError` | 空格/制表符混用 |
| `IndentInsufficientError` | 块内缩进不足/层级非法 |
| `IndentElseEndError` | else/end 未与对应块头对齐 |
| `MissingEndError` | func/if/while/struct 缺 end |
| `ConditionTypeError` | if/while 条件非 bool |
| `UninitializedVariable` | 路径敏感分析发现未初始化使用 |
| `VarMissingInitializer` | var 声明缺少初始化器 |
| `LabelNotFound` | goto 目标不存在（含跨函数同名标签） |
| `DuplicateLabel` | 同一作用域重复标签 |
| `JumpIntoBlockError` | goto 跳入子块 |
| `JumpIncompatibleBlockError` | goto 跳入不可比块 |
| `LabelInsideLoop` | while 内出现 label |
| `GotoInsideLoop` | while 内出现 goto |
| `GotoOutsideFunction` | 函数外 goto |
| `LabelOutsideFunction` | 函数外 label |
| `BreakOutsideLoop` | while 外出现 break |
| `ContinueOutsideLoop` | while 外出现 continue |

### 6.6 函数与模块

| 打印名 | 典型条件 |
| ------ | -------- |
| `FunctionNameConflict` | 同名函数重复，或值绑定与全局函数同名 |
| `UndefinedFunction` | 调用目标不存在 |
| `DuplicateParameter` | 同签名参数重名 |
| `MissingArgument` | funcall 实参缺失 |
| `DuplicateArgument` | funcall 实参重复 |
| `UnknownArgument` | funcall 未知实参名 |
| `ArgumentOrderError` | funcall 实参顺序错误 |
| `FunctionCallPositionError` | funcall 调用位置错误 |
| `FunctionCallResultTypeError` | funcall 接收变量类型不匹配 |
| `ReturnOutsideFunction` | 函数体外 return |
| `ReturnFormError` | return 有值/无值形式不匹配 |
| `ReturnTypeError` | return 操作数类型不匹配 |
| `MissingReturn` | 函数末尾可达但无 return |
| `UnreachableStatement` | 不可达语句 |
| `ParameterAssignmentError` | 对形参绑定本身赋值或作为 read 目标（不含经指针写穿） |
| `FunctionScopeAccessError` | 函数内裸名引用本库成员 |
| `RecursionError` | 函数调用图存在环 |
| `ModuleLayerError` | 模块层序错误 |
| `MissingVisibilityError` | #lib 缺少 public/private |
| `ProgramModeMisuseError` | #program 误用 func/static 等 |
| `ImportNotFound` | 导入目标未找到 |
| `ImportNotLib` | 导入目标非 #lib |
| `ImportAmbiguous` | 导入目标歧义 |
| `DuplicateImport` | 重复导入 |
| `ImportNameConflict` | 导入名与顶层名称冲突 |
| `CircularImport` | 循环导入 |
| `PrivateMemberAccessError` | 外部访问 private 成员（含 private struct 类型名/构造器） |

### 6.7 警告

0.0.41 没有活跃 `TcWarningKind`。`TcWarningList` 仍在内部/API 结构中，但正常编译不产生 warning 行。

---

## 7. 性能计时

### 7.1 启用

使用环境变量：

```bash
TC_BENCH=1 build/vm/bin/tc-vm program.tc
TC_BENCH=1 build/vm/bin/tc-vm --check program.tc
```

### 7.2 输出

性能行写到 stderr：

```text
bench parse: 0.000123 s
bench module resolve: 0.000045 s
bench analyze: 0.000456 s
bench execute: 0.000078 s
```

`--check` 不含 execute。环境变量存在即启用，值不要求为 `1`；文档示例使用 `1` 以表达意图。

### 7.3 注意

性能行会与错误诊断共享 stderr。自动化比较 stderr 时不要设置 `TC_BENCH`。

---

## 8. 当前示例

以下示例适用于 v0.0.41。每个源文件须以 `#program` 或 `#lib` 开头（[语言标准 §4](./TC语言标准设计说明书-0.0.41.md)）；未写模式指令为语法错误。

### 8.1 基础执行

```text
#program
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

### 8.2 静态检查

```bash
build/vm/bin/tc-vm --check sum.tc
```

### 8.3 多文件模块

`main.tc` (入口)：

```text
#program
import math_lib

var v: float64 = 3.14
var area: float64 = funcall(math_lib.compute_area, r: v)
writeln(float64, %f, area)
```

`math_lib.tc` (库)：

```text
#lib

public static let pi: float64 = 3.141592653589793

public func compute_area(r: float64) float64 then
    var rsq: float64 = mul(float64, r, r)
    var result: float64 = mul(float64, rsq, Self.pi)
    return result
end
```

```bash
build/vm/bin/tc-vm main.tc
```

### 8.4 指针与 memblock

```text
#program
var arr: memblock<int32, 5> = memblock(int32, count: 5, 1, 2, 3, 4, 5)
var idx: int32 = 2
var p: ptr<int32> = ptr_address(int32, arr)
var offset: usize = cast(usize, idx)
var p2: ptr<int32> = ptr_add(int32, p, offset)
var val: int32 = ptr_load(int32, p2)
writeln(int32, %d, val)
```

---

## 9. 0.0.41 迁移与兼容边界

### 9.1 当前发布状态

| 0.0.41 项目 | 状态 |
| ----------- | ---- |
| 多文件模块系统 | 已落地 |
| 函数/`funcall`/`return` | 已落地 |
| `ptr<T>` 及全部 ptr_* 指令 | 已落地 |
| `memblock<T, N>` 及深拷贝 | 已落地 |
| `struct` 及字段双层可变性 | 已落地 |
| `struct` 指针自引用（`ptr<本结构体>`，[语言标准 §3.9.1]） | 已落地（`TcType.pending_name` 位置规则校验 + 解析；测试 `phase5_struct_ptr_self_ref` 等） |
| 导入 `public struct`（`<模块名>.<结构体名>` 类型名/构造器） | 已落地（裸名 → `UndefinedStruct`；`private struct` → `PrivateMemberAccessError`；测试 `import_struct_type` 等） |
| `isize`/`usize` | 已落地 |
| `static var` / `static let` | 已落地 |
| 13 阶段编译管线 | 已落地 |
| 完整诊断码表（**86** 码 = 85 语言码 + OOM） | 已落地 |

### 9.2 与 v0.0.31 的关键差异

| 特性 | v0.0.31 | v0.0.41 |
| ---- | ------- | ------- |
| 模块 | 单文件 | 多文件 `#program`/`#lib` + `import` |
| 函数 | 无 | `func`/`funcall`/`return` |
| 类型 | 标量 | 标量 + `ptr<T>` + `memblock<T,N>` + `struct` + `isize`/`usize` |
| REPL | 支持 | 无 |
| 错误码 | 41+1 | **86** |
| 入口 | 顶层语句 | `#program` 顶层语句 |

### 9.3 迁移提示

- 无 REPL 模式；所有执行均通过批量文件模式完成。
- 批量文件支持完整控制流，无 REPL 控制流限制。
- 依赖浮点 wrap 的源文件使用 ieee 数值语义，或 bitcast 到等宽整数后处理位模式。
- 无 RHS 的 `var` 在声明时初始化。
- `#program` 文件不可定义 `func`、使用 `static` 或 `Self`。

---

## 10. 测试脚本约定

### 10.1 当前入口

```bash
bash scripts/run_tests.sh
bash scripts/vm/run_tests.sh
bash scripts/vm/run_tests.sh --filter <name>
```

### 10.2 标签

VM 脚本输出中的 `OK`、`OUT`、`ERR`、`CHK`、`CFL` 等是测试运行器标签，不是 tc-vm 用户输出。

---

*当前命令行为以 0.0.41 源码与本页为准，语言规则以 [TC 0.0.41 标准](./TC语言标准设计说明书-0.0.41.md) 为准。*
