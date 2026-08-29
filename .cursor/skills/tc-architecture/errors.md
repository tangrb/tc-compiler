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

## 86 种错误（`TcErrorKind`）

权威对照：语言标准附录 B（**85** 语言码）· `docs/TC编译器标准设计说明书-0.0.41.md` §11.4（镜像）· 实现枚举：`tc_types.h`（**86** = 85 + `TC_ERR_OUT_OF_MEMORY`，`test_types.c` 断言）· 打印名见 `tc_error_kind_name()`

命名：编译期 `TC_CE_*`，运行时 `TC_RE_*`，实现专用 `TC_ERR_OUT_OF_MEMORY`。下表按附录 B 分组列出全部语言码与标准章节；历史文档偶用 `TC_ERR_*` 别称时以 `tc_types.h` 为准。

### 词法与语法（B.1）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_SYNTAX` | `SyntaxError` | Lex/Parse | §2、附录 A；**不含**文件/API/OOM |
| `TC_CE_MISSING_END` | `MissingEndError` | Parse | §1.3 白名单、§7 |
| `TC_CE_OPERAND_COUNT` | `OperandCountError` | Parse | §1.3 白名单、§10 |
| `TC_CE_LITERAL_OUT_OF_RANGE` | `LiteralOutOfRange` | Lex/Analyzer | §2.3.5、§3.6 |
| `TC_CE_INDENT_MIXED` | `IndentMixedError` | Lex | §2.8、§7.1.2 |
| `TC_CE_INDENT_INSUFFICIENT` | `IndentInsufficientError` | Lex | §7.1.2 |
| `TC_CE_INDENT_ELSE_END` | `IndentElseEndError` | Lex | 附录 A.2；`else`/`end` 不对齐 |

### 模块系统（B.2）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_MODULE_LAYER` | `ModuleLayerError` | Parse 受限恢复 | §4 |
| `TC_CE_MISSING_VISIBILITY` | `MissingVisibilityError` | Parse 受限恢复 | §4.4 |
| `TC_CE_PROGRAM_MODE_MISUSE` | `ProgramModeMisuseError` | Parse 受限恢复 | §4.1 |
| `TC_CE_DUPLICATE_IMPORT` | `DuplicateImport` | Analyzer | §4.5 |
| `TC_CE_IMPORT_NAME_CONFLICT` | `ImportNameConflict` | Analyzer | §4.5 |
| `TC_CE_IMPORT_NOT_FOUND` | `ImportNotFound` | Analyzer | §4.5 |
| `TC_CE_IMPORT_NOT_LIB` | `ImportNotLib` | Analyzer | §4.5 |
| `TC_CE_IMPORT_AMBIGUOUS` | `ImportAmbiguous` | Analyzer | §4.5 |
| `TC_CE_CIRCULAR_IMPORT` | `CircularImport` | Analyzer | §4.5 |
| `TC_CE_PRIVATE_MEMBER_ACCESS` | `PrivateMemberAccessError` | Analyzer | §4.4 |

### 名称解析（B.3）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_UNDEFINED_VARIABLE` | `UndefinedVariable` | Analyzer | §9.1 |
| `TC_CE_UNDEFINED_FUNCTION` | `UndefinedFunction` | Analyzer | §8.2 |
| `TC_CE_FUNCTION_SCOPE_ACCESS` | `FunctionScopeAccessError` | Analyzer | §8.4.1 |
| `TC_CE_DUPLICATE_DEFINITION` | `DuplicateDefinition` | Analyzer | §8.1.2、§9.1 |
| `TC_CE_DUPLICATE_PARAMETER` | `DuplicateParameter` | Analyzer | §8.1.2 |
| `TC_CE_FUNCTION_NAME_CONFLICT` | `FunctionNameConflict` | Analyzer | §8.1 |

### 类型、字面量与模式（B.4）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_TYPE_MISMATCH` | `TypeMismatch` | Analyzer | §3、§6 |
| `TC_CE_LITERAL_TYPE` | `LiteralTypeError` | Lex/Analyzer | §3.6 |
| `TC_CE_MODE_MISMATCH` | `ModeMismatch` | Analyzer | §6.3.1、§6.6 |
| `TC_CE_COMPARISON_TYPE_MISMATCH` | `ComparisonTypeMismatch` | Analyzer | §6.5.1 |
| `TC_CE_FORMAT_TYPE_MISMATCH` | `FormatTypeMismatch` | Analyzer | §10.1 |
| `TC_CE_FORMAT_SPECIFIER` | `FormatSpecifierError` | Analyzer | §10.5 |
| `TC_CE_BITCAST_WIDTH` | `BitcastWidthError` | Analyzer | §6.6.6 |

### 声明与 memblock 静态（B.5）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_VAR_MISSING_INIT` | `VarMissingInitializer` | Parser | §5.1 |
| `TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE` | `MemblockIndexOutOfRange` | Analyzer | §6.7 |
| `TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH` | `MemblockElementCountMismatch` | Analyzer | §3.8.3 |
| `TC_CE_MEMBLOCK_SIZE_MISMATCH` | `MemblockSizeMismatch` | Analyzer | §3.8.4 |

### 常量表达式（B.6）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_CONSTANT_EXPRESSION` | `ConstantExpressionError` | Const eval | §5.2.1 |
| `TC_CE_CONSTANT_OVERFLOW` | `ConstantOverflow` | Const eval | §5.2.3 |
| `TC_CE_CONSTANT_DIV_ZERO` | `ConstantDivisionByZero` | Const eval | §5.2.3 |
| `TC_CE_CONSTANT_CAST_OVERFLOW` | `ConstantCastOverflow` | Const eval | §5.2.3、§6.6 |

### 结构体（B.7）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_STRUCT_MISSING_FIELD` | `StructMissingField` | Analyzer | §3.9 |
| `TC_CE_STRUCT_UNKNOWN_FIELD` | `StructUnknownField` | Analyzer | §3.9 |
| `TC_CE_STRUCT_DUPLICATE_FIELD` | `StructDuplicateField` | Analyzer | §3.9 |
| `TC_CE_STRUCT_FIELD_ORDER` | `StructFieldOrderError` | Analyzer | §3.9 |
| `TC_CE_STRUCT_IMMUTABLE_FIELD` | `StructImmutableFieldError` | Analyzer | §3.9.5 |
| `TC_CE_STRUCT_VALUE_SELF_REF` | `StructValueSelfRefError` | Analyzer | §3.9.1 |
| `TC_CE_DUPLICATE_STRUCT` | `DuplicateStruct` | Analyzer | §3.9 |
| `TC_CE_UNDEFINED_STRUCT` | `UndefinedStruct` | Analyzer | §3.9 |

### 赋值（B.8）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_CONSTANT_ASSIGNMENT` | `ConstantAssignmentError` | Analyzer | §5、§6.8.3：对 `let` 赋值；经 `ptr_address` 取 `let`/`static let`/**形参**地址后再 `ptr_store`/`memcopy_unsafe` |
| `TC_CE_PARAMETER_ASSIGNMENT` | `ParameterAssignmentError` | Analyzer | §8.1.2：对形参绑定本身赋值/`read`；不含经指针写穿 |

### 控制流（B.9）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_GOTO_OUTSIDE_FUNCTION` | `GotoOutsideFunction` | Analyzer | §7.3 |
| `TC_CE_LABEL_OUTSIDE_FUNCTION` | `LabelOutsideFunction` | Analyzer | §7.3 |
| `TC_CE_GOTO_INSIDE_LOOP` | `GotoInsideLoop` | Analyzer | §7.2.3 |
| `TC_CE_LABEL_INSIDE_LOOP` | `LabelInsideLoop` | Analyzer | §7.2.3 |
| `TC_CE_JUMP_INTO_BLOCK` | `JumpIntoBlockError` | Analyzer | §7.3.2 |
| `TC_CE_JUMP_INCOMPATIBLE_BLOCK` | `JumpIncompatibleBlockError` | Analyzer | §7.3.2 |
| `TC_CE_LABEL_NOT_FOUND` | `LabelNotFound` | Analyzer | §7.3.2（含跨函数同名标签） |
| `TC_CE_DUPLICATE_LABEL` | `DuplicateLabel` | Analyzer | §7.3 |
| `TC_CE_BREAK_OUTSIDE_LOOP` | `BreakOutsideLoop` | Analyzer | §7.4 |
| `TC_CE_CONTINUE_OUTSIDE_LOOP` | `ContinueOutsideLoop` | Analyzer | §7.4 |
| `TC_CE_MISSING_RETURN` | `MissingReturn` | CFG | §8.3 |
| `TC_CE_CONDITION_TYPE` | `ConditionTypeError` | Analyzer | §7.1.1、§7.2.1 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` | `ReturnOutsideFunction` | Analyzer | §8.3.1 |
| `TC_CE_RETURN_FORM` | `ReturnFormError` | Analyzer | §8.3.1 |
| `TC_CE_RETURN_TYPE` | `ReturnTypeError` | Analyzer | §8.3.1 |

### 确定初始化与可达性（B.10）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_UNINITIALIZED_VARIABLE` | `UninitializedVariable` | CFG | §9.2 |
| `TC_CE_UNREACHABLE_STATEMENT` | `UnreachableStatement` | CFG | §9.2 |

### 指针静态（B.11）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE` | `MemcopyUnsafeInvalidRange` | Analyzer | §6.8.9 |

### 函数调用（B.12）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_CE_UNKNOWN_ARGUMENT` | `UnknownArgument` | Analyzer | §8.2.2 |
| `TC_CE_DUPLICATE_ARGUMENT` | `DuplicateArgument` | Analyzer | §8.2.2 |
| `TC_CE_MISSING_ARGUMENT` | `MissingArgument` | Analyzer | §8.2.2 |
| `TC_CE_ARGUMENT_ORDER` | `ArgumentOrderError` | Analyzer | §8.2.2 |
| `TC_CE_FUNCALL_POSITION` | `FunctionCallPositionError` | Analyzer | §8.2.3 |
| `TC_CE_FUNCALL_RESULT_TYPE` | `FunctionCallResultTypeError` | Analyzer | §8.2.3 |
| `TC_CE_RECURSION` | `RecursionError` | 调用图 | §8.6 |

### 运行时（B.13）

| 错误码 | 打印名 | 阶段 | 语言标准 |
|--------|--------|------|---------|
| `TC_RE_DIVISION_BY_ZERO` | `DivisionByZero` | Executor | §6.3、§6.3.7（浮点 `mod` 有限/零） |
| `TC_RE_INTEGER_OVERFLOW` | `IntegerOverflow` | Executor | §6.3、§6.4.2 |
| `TC_RE_FLOAT_OVERFLOW` | `FloatOverflow` | Executor | §6.3.2 |
| `TC_RE_FLOAT_UNDERFLOW` | `FloatUnderflow` | Executor | §6.3.2 |
| `TC_RE_FLOAT_INVALID` | `FloatInvalidOperation` | Executor | §6.3.2、§6.3.7 |
| `TC_RE_NEGATIVE_SHIFT_COUNT` | `NegativeShiftCount` | Executor | §6.4.2 |
| `TC_RE_CAST_OVERFLOW` | `CastOverflow` | Executor | §6.6 |
| `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE` | `MemblockIndexOutOfRange` | Executor | §6.7 |
| `TC_RE_IO` | `IOError` | Executor | §10 |
| `TC_RE_NULL_POINTER_DEREFERENCE` | `NullPointerDereference` | Executor | §6.8 |
| `TC_RE_NULL_POINTER_ARITHMETIC` | `NullPointerArithmetic` | Executor | §6.8 |
| `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE` | `MemcopyUnsafeInvalidRange` | Executor | §6.8.9 |

### 实现专用（非附录 B）

| 错误码 | 打印名 | 阶段 | 说明 |
|--------|--------|------|------|
| `TC_ERR_OUT_OF_MEMORY` | `OutOfMemory` | 任意 | 内部分配失败；消息 `memory allocation failed` |

新增错误时同步：`tc_types.h` → `tc_error_kind_name()` → `test_types.c` → 语言标准附录 B → 编译器标准 §11.4 → VM 详设 → `errors.md`。

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
| 位运算/shift + wrap | `wrap cannot be used with bitwise/shift operations`（码为 `TC_CE_SYNTAX`；const_shift 非法组合为 `TC_CE_MODE_MISMATCH`） |
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
| 浮点 `mod` 无效（NaN/无穷/`0 mod 0`） | `float invalid operation` |
| 浮点 `mod` 有限/零 | `division by zero` |
| 整数 + ieee | `ieee mode is only allowed for floating-point operations` |
| 浮点比较 + wrap | `wrap cannot be used with floating-point compare` |
| 浮点位运算 | `bitwise operation requires integer type` |
| strict 浮点上溢 | `float overflow` |
| strict 浮点无效 | `float invalid operation` |
| 浮点 cast 超范围 | `cast result out of range for target type` |
| 导入结构体裸名 | `undefined struct` |
| 导入 private struct | `private member access` |
| 浮点 I/O 格式错 | `format specifier does not match operand type` |
| format `+` 与无符号 | `'+' flag not supported for this format specifier` |
| format `#` 与 bool | `'#' flag not supported for` |
| format `0` 与 `%t` | `'0' flag not supported for %t` |
| format `%t` 与 `+`/`#`/`0`/精度 | `%%t does not support '+', '#', '0', or precision` |
| 非法格式串 | `invalid format specifier` |
| 未初始化变量 | `use of uninitialized variable` |
| 标签未找到 | `label '…' not found`（含跨函数同名标签） |
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
