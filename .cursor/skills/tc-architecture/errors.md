# 诊断系统

**何时读**：错误码种类、stderr 格式、诊断阶段顺序、static 测试期望子串。

## API

- 结构体：`TcDiagnostic`（`tc_types.h`）
- 函数：`tc_diagnostic_init/clear/set_source/set/print`（`tc_diagnostic.c`）
- 格式：`<file>:<line>:<col>: error: <message>`
- **单槽**：`tc_diagnostic_set` 覆盖前一条，不累积

## 诊断阶段（标准 §11.0 / 现网编排）

前一阶段无错才进后一阶段；同阶段按行列首错。现网 Analyze 编排见 [pipeline.md](pipeline.md)。

1. UTF-8 解码（文件打开失败属实现/API）
2. 词法与缩进扫描
3. 语法解析（含模块头）
4. 模块结构 / import / 签名
5. Pass1 名称解析与作用域
6. Pass2 类型、操作数数量与模式 / funcall / return / goto
7. `let` / static let 常量求值
8. `if`/`while` 静态布尔三态 + 逻辑 RHS 读边判定
9. CFG 构建与不可达边裁剪（多域）
10. 确定初始化 DFA（`tc_analyze_definite_init_all`）
11. 调用图（递归环）

同 Token 多规则优先级：操作数数量 → 类型类别 → 模式 → bitcast 位宽 → 字面量范围/类型 → 格式符/其他。  
用例：`diag_priority_*`（见 [test-map.md](test-map.md)）。

初始化三分工：

| 层 | 错误码 | 性质 |
|----|--------|------|
| 语法 | `TC_CE_VAR_MISSING_INIT` | `var` 缺 `= rhs`，与控制流无关 |
| 名称 | `TC_CE_UNDEFINED_VARIABLE` | 未声明 / 源序不可见 / 超作用域 |
| 数据流 | `TC_CE_UNINITIALIZED_VARIABLE` | CFG 可达点未确定初始化 |

无 `GOTO_SKIPS_VAR_INIT`：跳过初始化统一走数据流错误。

## Fail-fast

| 阶段 | 行为 |
|------|------|
| Parse | 首错即停，不进 Analyzer |
| Analyzer | 首条静态错误（按阶段推进） |
| Executor | 首条运行时错误（此前语句已执行） |
| Embed AOT | 非致命：置 error_flag，经 `tc_embed_had_error` 可见 |
| Warning | 无语言警告；列表仅为空兼容壳 |

## 91 种错误（`TcErrorKind`）

权威对照：`docs/TC编译器标准设计说明书-0.0.41.md` §11.4 · 实现枚举：`tc_types.h`（**91** 项，`test_types.c` 断言）· 打印名见 `tc_error_kind_name()`

命名：编译期多为 `TC_CE_*`，运行时多为 `TC_RE_*`，实现专用 `TC_ERR_OUT_OF_MEMORY`。下表列出常见核心码（历史文档偶用 `TC_ERR_*` 别称时以 `tc_types.h` 为准）。

| 错误码 | 打印名 | 阶段 | 典型触发 |
|--------|--------|------|---------|
| `TC_CE_SYNTAX` | `SyntaxError` | Lex/Parse | 非法字符、语法；**不含**文件/API/OOM |
| `TC_CE_UNDEFINED_VARIABLE` | `UndefinedVariable` | Analyzer | 未定义引用、前向引用、自引用 |
| `TC_CE_DUPLICATE_DEFINITION` | `DuplicateDefinition` | Analyzer | 同名重复定义 |
| `TC_CE_TYPE_MISMATCH` | `TypeMismatch` | Analyzer | 类型不兼容 |
| `TC_CE_LITERAL_OUT_OF_RANGE` | `LiteralOutOfRange` | Analyzer | 字面量超范围 |
| `TC_CE_LITERAL_TYPE` | `LiteralTypeError` | Lex/Analyzer | bool↔integer 字面量不符 |
| `TC_CE_KEYWORD` | `KeywordError` | Parser | wrap/truncate 误用 |
| `TC_CE_CONSTANT_ASSIGNMENT` | `ConstantAssignmentError` | Analyzer | 对 let 赋值 |
| `TC_CE_CONSTANT_EXPRESSION` | `ConstantExpressionError` | Analyzer | let 引用 var 等 |
| `TC_CE_CONSTANT_OVERFLOW` | `ConstantOverflow` | Analyzer | let 算术溢出 |
| `TC_CE_CONSTANT_DIV_ZERO` | `ConstantDivisionByZero` | Analyzer | let 除零 |
| `TC_CE_CONSTANT_CAST_OVERFLOW` | `ConstantCastOverflow` | Analyzer | let cast 溢出 |
| `TC_CE_COMPARISON_TYPE_MISMATCH` | `ComparisonTypeMismatch` | Analyzer | 比较操作数类型不一致 |
| `TC_CE_FORMAT_TYPE_MISMATCH` | `FormatTypeMismatch` | Analyzer | 格式符与类型不匹配 |
| `TC_CE_FORMAT_SPECIFIER` | `FormatSpecifierError` | Analyzer | 格式标志/宽度/精度非法或与转换符不兼容 |
| `TC_CE_OPERAND_COUNT` | `OperandCountError` | Parser | write 操作数数量 |
| `TC_RE_DIVISION_BY_ZERO` | `DivisionByZero` | Executor | 运行时除零/模零 |
| `TC_RE_INTEGER_OVERFLOW` | `IntegerOverflow` | Executor | strict 算术/一元溢出 |
| `TC_RE_CAST_OVERFLOW` | `CastOverflow` | Executor | strict cast 溢出 |
| `TC_RE_IO` | `IOError` | Executor | read 失败/非法 bool 输入 |
| `TC_ERR_OUT_OF_MEMORY` | `OutOfMemory` | 静态/运行时 | 内部分配失败；消息 `memory allocation failed` |
| `TC_CE_INDENT_*` / `MISSING_END` / `ELSE_POSITION` | … | Parse | 缩进与 if/while 结构 |
| `TC_CE_CONDITION_TYPE` | `ConditionTypeError` | Analyzer | if/while 条件非 bool |
| `TC_CE_MODE_MISMATCH` | `ModeMismatch` | Analyzer | ieee/wrap 误用 |
| `TC_CE_UNINITIALIZED_VARIABLE` | `UninitializedVariable` | Analyzer | CFG 未确定初始化 |
| `TC_CE_LABEL_*` / `JUMP_*` | … | Analyzer | goto/label 规则 |
| `TC_CE_VAR_MISSING_INIT` | `VarMissingInitializer` | Parser | `var` 缺少 `= rhs` |
| `TC_CE_BITCAST_WIDTH` | `BitcastWidthError` | Analyzer | bitcast 位宽不等 |
| `TC_CE_*_INSIDE_LOOP` / `*_OUTSIDE_LOOP` | … | Analyzer | while 范式 / break·continue |
| `TC_CE_GOTO/LABEL_OUTSIDE_FUNCTION` | … | Analyzer | 顶层 goto/label |
| `TC_CE_RECURSION` / `MISSING_RETURN` 等 | … | Analyzer | 函数/调用图 |

函数/memblock/struct/模块/指针扩展码见编译器标准 §11.4.2–11.4.6。

新增错误时同步：`tc_types.h` → `tc_error_kind_name()` → `test_types.c` → 标准 §11.4 → VM 详设 → `errors.md`。

## 警告

- `TcWarningKind` 仅有空壳 `TC_WARN_NONE`；编译成功后 warnings 始终为空
- 列表管理：`tc_warning_list_*`（`tc_warning.c`）；单元测试 `test_warning.c`

## 静态测试期望消息（常见）

| 场景 | stderr 子串 |
|------|--------------|
| bool 字面量 → 整数类型 | `literal type does not match variable type` |
| 整数字面量 → bool | `literal type does not match context` |
| 比较/逻辑/位运算类型错 | `operand type does not match operation type` |
| cast truncate 非整数窄化 | `truncate requires an integer target narrower than the source` |
| xor 用于 bool | `expected integer type` |
| 位运算/shift + wrap | `wrap cannot be used with bitwise/shift operations` |
| shl + truncate | `truncate cannot be used with shift operations` |
| let shl wrap | `wrap cannot be used in constant expression` |
| 运行时 shl 溢出 | `shift left overflow` |
| let 自引用/前向引用 | `undefined variable` |
| let 溢出 | `constant overflow` |
| let 除零 | `constant division by zero` |
| let 引用 var | `constant expression cannot reference var variable` |
| if 条件非 bool | `if condition must be bool` |
| 混用空格/tab 缩进 | `mixed spaces and tabs in indentation` |
| 块内缩进不足 | `insufficient indentation in block` |
| else/end 缩进不对齐 | `else indentation does not match if` / `end indentation does not match if` |
| 缺少 end | `missing end for if statement` |
| 跨块引用局部变量 | `undefined variable` |
| 浮点 mod | `modulo is not supported for floating-point types` |
| 整数 + ieee | `ieee mode is only allowed for floating-point operations` |
| 浮点比较 + wrap | `wrap cannot be used with floating-point compare` |
| 浮点位运算 | `bitwise operation requires integer type` |
| strict 浮点上溢 | `float overflow` |
| strict 浮点无效 | `float invalid operation` |
| 浮点 cast 超范围 | `cast result out of range for target type` |
| 浮点 I/O 格式错 | `format specifier does not match operand type` |
| format `+` 与无符号 | `'+' flag not supported for this format specifier` |
| format `#` 与 bool | `'#' flag not supported for` |
| format `0`/`-` 互斥 | `'0' and '-' flags are mutually exclusive` |
| format `%t` 带宽/精度 | `does not support flags, width, or precision` |
| 非法格式串 | `invalid format specifier` |
| 未初始化变量 | `use of uninitialized variable` |
| 标签未找到 | `label '…' not found` |
| 标签重复 | `duplicate label` |
| 跳入子块 | `cannot jump into inner block` |
| 跳入兄弟块 | `cannot jump into sibling block` |
| var 缺初始化器 | `variable definition requires initializer` |
| bitcast 位宽不等 | `bitcast source and target widths must match` |
| while 条件非 bool | `while condition must be bool` |
| while 缺 end | `missing end for while statement` |
| while 内 label | `label is not allowed inside while` |
| while 内 goto | `goto is not allowed inside while` |
| 循环外 break | `break used outside while` |
| 循环外 continue | `continue used outside while` |
