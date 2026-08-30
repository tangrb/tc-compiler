# 特性 — 标量运算与 I/O

**只读本文件** — 由 [features.md](../features.md) 路由指向。

## bool 类型（参考实现 v0.0.21）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举 | `tc_types.h` | `TC_BOOL` in `TcTypeTag`；`TcLiteral.is_bool` |
| 类型工具 | `tc_types.c` | `tc_type_is_bool()`；`tc_type_is_integer()` 排除 bool |
| 词法 | `tc_lexer.c` | `true`/`false` → `TC_TOK_BOOL_LIT`；`bool` → `TC_TOK_INT_TYPE` |
| 语法 | `tc_parser.c` / `tc_parser_rhs.c` | 字面量 `is_bool`；compare/logic 要求整数/bool 操作数 |
| 分析 | `tc_analyzer_pass2.c` | `tc_check_literal` bool 分支；`tc_check_io_format` `%t` |
| 语义 | `tc_semantics.c` | `tc_literal_to_value`；cast bool↔int |
| I/O | `tc_io.c` | `tc_io_write_value` / `tc_io_read_value`；bool → `"true"/"false"`；`%t` |
| 执行 | `tc_executor.c` | I/O 委托 `tc_io.c`；`tc_eval_rhs` 各 kind |
| AOT | `tc_aot_codegen.c` | emit 字面量/compare/logic |
| AOT rt | `tc_aot_rt.c` | `tc_aot_compare/logic`；write/read 委托 `tc_io.c` |

测试：`bool_var`, `bool_cast`, `format_bool`, `read_bool`, `let_bool_constant`；错误 `bool_literal_type_error`。

## 比较运算（eq/ne/lt/le/gt/ge）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举 | `tc_types.h` | `TcCompareOp`；`TC_RHS_COMPARE` |
| 类型工具 | `tc_types.c` | `tc_compare_op_parse()` |
| 词法 | `tc_lexer.c` | `eq`/`ne`/… → `TC_TOK_COMPARE_OP` |
| 语法 | `tc_parser_rhs.c` | `tc_parse_rhs` / `tc_parse_const_rhs` compare 分支 |
| 分析 | `tc_analyzer_pass2.c` | `tc_check_rhs` 操作数须整数；结果 `TC_BOOL` |
| 语义 | `tc_semantics.c` | `tc_exec_compare()` — 有符号/无符号分支 |
| 执行 | `tc_executor.c` | `TC_RHS_COMPARE` → `tc_exec_compare` |
| AOT | `tc_aot_codegen.c` | 生成 `tc_aot_compare(...)` |
| AOT rt | `tc_aot_rt.c` | `tc_aot_compare` → `tc_exec_compare` |

let：`tc_eval_const_rhs` compare 分支 → `tc_exec_compare` + `tc_const_map_runtime_error`。

测试：`compare_ops`, `const_expr`；错误 `compare_type_mismatch`（`TC_ERR_COMPARISON_TYPE_MISMATCH`）。

## 逻辑运算（and/or/not，短路）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举 | `tc_types.h` | `TcLogicOp`；`TC_RHS_LOGIC_BIN` / `TC_RHS_LOGIC_UN` |
| 词法 | `tc_lexer.c` | `and`/`or`/`not` → `TC_TOK_LOGIC_OP` |
| 语法 | `tc_parser_rhs.c` | logic 单/双目分支 |
| 分析 | `tc_analyzer_pass2.c` + `tc_cfg.c` | 操作数 `TC_BOOL`；字面量或更早可见 `let bool` → Pass2 仍检名称/类型，CFG 不记 RHS 读槽 |
| 语义 | `tc_semantics.c` | `tc_exec_logic_binary/unary` |
| 执行 | `tc_executor.c` | **短路**：and 左 false / or 左 true 跳过 RHS 求值 |
| AOT | `tc_aot_codegen.c` | `tc_aot_logic` / `tc_aot_logic_unary` |
| AOT rt | `tc_aot_rt.c` | 委托 `tc_exec_logic_*` |

let：`tc_eval_const_rhs` 内同样短路。左为 **var** → 静态 UNKNOWN，不做跨语句常量推测。

测试：`logic_ops`, `const_expr`, `uninit_shortcircuit*`, `shortcircuit_let_*`；错误 `logic_type_error`。

## 位运算与移位（and/or/xor/not/shl/shr，v0.0.23）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举 | `tc_types.h` | `TcBitwiseOp`；`TcShiftOp`；`TC_RHS_BITWISE_BIN/UN/SHIFT` |
| 类型工具 | `tc_types.c` | `tc_bitwise_op_parse/name`；`tc_shift_op_parse/name` |
| 词法 | `tc_lexer.c` | `xor` → `TC_TOK_BITWISE_OP`；`shl`/`shr` → `TC_TOK_SHIFT_OP`；`and`/`or`/`not` 仍 `TC_TOK_LOGIC_OP` |
| 语法 | `tc_parser_rhs.c` | `tc_parse_and_or_not_rhs` 按类型分派 logic/bitwise；`tc_parse_bitwise_bin_rhs`；`tc_parse_shift_rhs` |
| 分析 | `tc_analyzer_pass2.c` | `tc_check_rhs` 整数操作数同类型；移位 value/count 同类型 |
| 语义 | `tc_semantics.c` | `tc_exec_bitwise_binary/unary`；`tc_exec_shift`（shl wrap、shr 显式算术/逻辑右移，不依赖宿主有符号 `>>`） |
| 执行 | `tc_executor.c` | 无短路；委托 `tc_exec_bitwise_*` / `tc_exec_shift` |
| AOT | `tc_aot_codegen.c` | `tc_aot_bitwise_*` / `tc_aot_shift` |
| AOT rt | `tc_aot_rt.c` | shim → `tc_exec_bitwise_*` / `tc_exec_shift` |

let：`tc_eval_const_rhs` 委托 semantics；**禁止 wrap**；`shl` 严格溢出 → `constant overflow`。

测试：`bitwise_*_valid` 系列、**shl_int64_neg_boundary**；错误 `bitwise_*` static/runtime。

## let 编译期常量

| 函数 | 文件 | 作用 |
|------|------|------|
| `tc_eval_const_rhs` | `tc_const_eval.c` | 递归求值所有 const_rhs kind |
| `tc_eval_const_operand` | `tc_const_eval.c` | lit / const_ref / var 拒绝 |
| `tc_resolve_const_value` | `tc_const_eval.c` | Pass2 写入 `TcSymbol.const_value` |
| `tc_const_cast_allowed` | `tc_const_eval.c` | 仅扩宽 cast |
| `tc_const_map_runtime_error` | `tc_const_eval.c` | 运行时错 → 编译期错 |
| `tc_const_visit_contains` | `tc_const_eval.c` | 循环依赖检测 |

Pass2 流程：`tc_pass2_type_check` → `tc_resolve_const_value` → `tc_eval_const_rhs`。

Executor：`tc_eval_operand` 见 `has_const_value` 直接加载，不走 `tc_eval_rhs`。

AOT：`TC_RHS_CONST_REF` / `TC_RHS_CONST_CAST` 在 codegen 折叠为字面量。

测试：`const_expr`, `let_constant*`；错误 `const_expr`, `const_cyclic_dep`, `const_overflow`, `const_div_zero`, `let_const_literal_range`。

## I/O 与格式输出

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举 | `tc_types.h` | `TcFormatSpec`；`TC_STMT_WRITE` / `TC_STMT_READ` |
| 类型工具 | `tc_types.c` | `tc_format_spec_parse/name` |
| 语法 | `tc_parser.c` | write/writeln/read 语句 |
| 分析 | `tc_analyzer_pass2.c` | `tc_check_io_format` — 格式符与操作数类型（含 `%t`↔bool） |
| I/O 实现 | `tc_io.c` | `tc_io_write_formatted` / `tc_io_write_value` / `tc_io_read_value` / `tc_io_read_digits` |
| 执行 | `tc_executor.c` | write/read 语句 → 委托 `tc_io.c` |
| AOT rt | `tc_aot_rt.c` | `tc_aot_write` / `tc_aot_read` → 委托 `tc_io.c` |

**勿**在 `executor.c` 与 `tc_aot_rt.c` 各写一套 I/O；改格式/读入逻辑只改 `tc_io.c`，必要时补 `tests/unit/runtime/test_io.c`。

测试：`format_output`, `format_bool`, `read_write`, `io_extended`, `fp_io`, `format_spec_fp`, **format_spec_flags**, **format_spec_table**；错误 `format_string_error`, `format_type_mismatch`, `read_invalid`, `read_out_of_range*`, `format_fp_type_mismatch`, `format_specifier_plus_unsigned`, `format_specifier_hash_bool`, `format_specifier_flags_mutex`（`%#d`）、`format_specifier_t_width`（`%.1t`）、**format_width_overflow**；合法上限 **format_width_max**。

## 浮点类型 float32/float64（v0.0.25）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举 | `tc_types.h` | `TC_FLOAT32`/`TC_FLOAT64`；`TcFloatMode`；3 个 `TC_RHS_FLOAT_*`；共享 `TC_RHS_CAST` 与 `TC_RHS_BITCAST` |
| 类型工具 | `tc_types.c` | `tc_type_is_float()`；`tc_type_bit_width()` 扩展 |
| 词法 | `tc_lexer.c` | 浮点字面量、`inf`/`nan`、`float32`/`float64`/`ieee` 关键字 |
| 语法 | `tc_parser.c` | `tc_parse_type_token` 浮点分支；`FLOAT_ARITH/UNARY/COMPARE/CAST` |
| 分析 | `tc_analyzer.c` | `tc_check_rhs` 4 浮点分支；`tc_check_io_format` `%f`/`%e`/`%E`/`%g`/`%G` |
| let | `tc_const_eval.c` | `tc_eval_const_rhs` 浮点路径；共享 cast/bitcast；禁浮点 `wrap` 与 `truncate` |
| 语义 | `tc_sem_fp.c` / `tc_sem_cast.c` | `tc_exec_fp_arith/unary/compare`；共享 strict cast、integer truncate、bitcast；浮点仅 strict/ieee |
| I/O | `tc_io.c` | 5 浮点格式符；`tc_io_read_value` 浮点输入 |
| 执行 | `tc_executor.c` | `tc_eval_rhs` 3 浮点 case + cast/bitcast → 共享 semantics |
| AOT | `tc_aot_codegen.c` / `tc_aot_rt.c` | 3 个 fp shim + 共享 cast/bitcast shim |

测试：`fp_basic`, `fp_arith`, `fp_arith_ieee`, `fp_compare`, `fp_cast`, `fp_bitcast_roundtrip`, `bitcast_roundtrip32/64`, **nan_canonical_bits**, `cast_literal`, `fp_io`, `fp_const_expr`, `fp_if_block`, `format_spec_fp`, **fp_mod**, **fp_mod_ieee_nan**, **fp_mod_edges**；static `fp_ieee_on_int`, `fp_*wrap*_mode_mismatch`, `bitcast_*_mismatch`, `fp_bitwise_type_error`, `fp_literal_range`, `format_fp_type_mismatch`；runtime `fp_strict_*`, `fp_cast_overflow`, `fp_div_zero`, **fp_mod_invalid**, **fp_mod_divzero**, **fp_mod_invalid_inf**, **fp_mod_invalid_before_divzero**。
