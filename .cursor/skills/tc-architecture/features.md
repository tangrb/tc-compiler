# 特性实现地图

按特性查「改哪些文件、哪些 switch」；加新特性时对照 bool/compare/logic/**bitwise**/let 与模块/函数/复合类型路径。

> **现网已实现**地图：**v0.0.39 Phase 1–6**（含 struct 运行时）+ **Embed v0.0.39**。  
> 规格：语言/编译器/VM/AOT 标准-0.0.39 · Embed 详设-0.0.39。

**分发点总表**见 Rule `knowledge-graph`；浮点/复合 RHS [kg-dispatch.md](kg-dispatch.md)；模块 [kg-module.md](kg-module.md)；函数 [kg-func.md](kg-func.md)；Embed [kg-embed.md](kg-embed.md)。

## 类型内核（0.0.39 Phase 1 / 模块 A）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 种类/完整类型 | `tc_types.h` | `TcTypeTag`（判别标签）；`TcType{tag,params}`；运行时/绑定/标量 AST 存 `const TcType*`（单例/`TcTypeTable`） |
| 工具 | `tc_types.c` | `tc_type_scalar` / `make_ptr|memblock|struct`；`tc_type_equals`（memblock 忽略 N）；`tc_sizeof_bits`；`tc_target_ptr_width_bits` |
| 槽位 | `tc_types.h` / `.c` | `TcSlotDomain`；`TcRuntimeSlots`（含 memblock/struct 堆跟踪） |
| 符号扩展 | `tc_symbol.c` | `slot_domain` / `memblock_count` / `struct_id`；`TC_SYM_PARAMETER|STATIC_*` |
| 枚举规模 | `tc_types.h` | STMT **24** / RHS **34** / 错误 **90**（CE/RE 越界对可同打印名） |
| 覆盖闸门 | `check_rhs_coverage.py` | 8 分发点 + per-point skip（无全局 Phase1 reserved） |

测试：`tests/unit/runtime/test_types.c`（check-types）。摘要：[types.md](types.md)。

## 模块系统（Phase 2）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| Lexer/Parser | `tc_lexer.c` / `tc_parser.c` | `#program`/`#lib`/`import`/`Self`/`@` |
| 模块检查 | `tc_module.c` | `tc_module_check_structure` / `resolve_imports` / `collect_signatures` |
| 作用域 | `tc_scope.c` | `tc_member_index_*` / `tc_scope_check_self_usage` |
| CLI/API | `tc_driver.c` / `tc_lib.c` | `-I`（`TcCompileOptions` 会话路径）/ `tc_compile_file_opts` |

细节：[kg-module.md](kg-module.md)。测试：check-module · `tests/errors/module/` · `include_search_ok`。

## 函数 / 调用图 / static（Phase 4–5）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 验证 | `tc_func_check.c` | 签名、funcall、return、static let/var |
| 调用图 | `tc_callgraph.c` | `tc_callgraph_check` → `TC_CE_RECURSION` |
| VM | `tc_call_frame.c` / `tc_executor.c` | 调用帧、`FUNCALL`/`RETURN`/`FUNCALL_EXPR` |
| AOT | `tc_aot_codegen.c` | 函数 codegen + `tc_aot_emit_funcall` |

细节：[kg-func.md](kg-func.md)。

## ptr / memblock / struct（Phase 3 验证 + Phase 5 运行时）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 验证 | `tc_{type,ptr,memblock,struct}_check.c` | `tc_type_check_rhs` 调度 |
| VM | `tc_{ptr,memblock,struct}_exec.c` | load/store/ctor/field/… |
| AOT | `tc_aot_rt.c` | `tc_aot_{ptr,memblock,struct}_*` |

细节：[kg-eval.md](kg-eval.md) · 测试账本 Phase 3/5：[test-map.md](test-map.md)。

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
| 语义 | `tc_semantics.c` | `tc_exec_bitwise_binary/unary`；`tc_exec_shift`（shl wrap、shr 算术/逻辑） |
| 执行 | `tc_executor.c` | 无短路；委托 `tc_exec_bitwise_*` / `tc_exec_shift` |
| AOT | `tc_aot_codegen.c` | `tc_aot_bitwise_*` / `tc_aot_shift` |
| AOT rt | `tc_aot_rt.c` | shim → `tc_exec_bitwise_*` / `tc_exec_shift` |

let：`tc_eval_const_rhs` 委托 semantics；**禁止 wrap**；`shl` 严格溢出 → `constant overflow`。

测试：`bitwise_*_valid` 系列；错误 `bitwise_*` static/runtime。

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

测试：`format_output`, `format_bool`, `read_write`, `io_extended`, `fp_io`, `format_spec_fp`；错误 `format_string_error`, `format_type_mismatch`, `read_invalid`, `read_out_of_range*`, `format_fp_type_mismatch`。

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

测试：`fp_basic`, `fp_arith`, `fp_arith_ieee`, `fp_compare`, `fp_cast`, `fp_bitcast_roundtrip`, `bitcast_roundtrip32/64`, `cast_literal`, `fp_io`, `fp_const_expr`, `fp_if_block`, `format_spec_fp`；static `fp_mod_type_error`, `fp_ieee_on_int`, `fp_*wrap*_mode_mismatch`, `bitcast_*_mismatch`, `fp_bitwise_type_error`, `fp_literal_range`, `format_fp_type_mismatch`；runtime `fp_strict_*`, `fp_cast_overflow`, `fp_div_zero`。

## if 控制流（v0.0.24）

| 层 | 文件 | 关键符号 | 状态 |
|----|------|---------|------|
| 枚举 | `tc_types.h` | `TC_STMT_IF`；`TcIfStmt`；7 种新 `TcErrorKind` | ✅ |
| Token | `tc_lexer.h` / `tc_lexer.c` | `TC_TOK_IF/THEN/ELSE/END` | ✅ |
| 释放 | `tc_parser_free.c` | `tc_statement_free` 递归 then/else body | ✅ |
| 符号表 | `tc_symbol.c` | `push_scope`/`pop_scope`/`find_in_scope`/`find_in_current_scope` | ✅ |
| 语法 | `tc_parser.c` | `tc_parse_if_stmt`；`tc_parse_source_to_program`；缩进 R1–R7（R7=`label` 不增层级） | ✅ |
| 分析 | `tc_analyzer_pass1.c` / `tc_analyzer_pass2.c` | 递归 + DFS stmt_index（header-only `tc_stmt_index.h`） | ✅ |
| 执行 | `tc_executor.c` | `tc_execute_statement_impl`：条件求值 → 分支递归（`tc_stmt_index` 跳过未执行分支） | ✅ |
| AOT | `tc_aot_codegen.c` | `tc_aot_emit_statement_impl`：原生 C if-else | ✅ |
| stmt_index | `runtime/tc_stmt_index.h` | `TcStmtIndexCursor` + DFS 子树 span + 跳过 | ✅ |

分发点总表见 Rule `knowledge-graph` →「TcStmtKind 分发点」+ [kg-cfg.md](kg-cfg.md)「块作用域」。

测试：`if_basic`, `if_else`, `if_nested`, `if_chain`, `if_bool_literal`, `if_local_same_name`, `if_shadow_global`, `if_false_skip_nested_then`；static `indent_*` / `if_cond_type_*` / `if_cross_block_ref_*` / `if_missing_end_*`；stress `stress_if_nested`；unit `test_symbol.c` / `test_parser.c` / `test_stmt_index.c` / `test_analyzer.c`。

## while / break / continue（v0.0.31）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举/AST | `tc_types.h` | `TC_STMT_WHILE/BREAK/CONTINUE`；body、loop id、条件 RHS |
| Lexer/Parser | `tc_lexer.c` / `tc_parser.c` | 通用块解析、while 条件、最内层控制语句 |
| 分析 | `tc_analyzer_pass1.c` / `tc_analyzer_pass2.c` | while scope、固定 slot、loop id、范式隔离 |
| CFG | `tc_cfg.c` | condition true/false、back edge、break exit、continue edge；`tc_analyze_definite_init` 固定点 |
| 辅助 | `tc_analyzer_dfa.c` | InitState / 块路径工具（非固定点主路径） |
| VM | `tc_executor.c` | 显式控制结果传播，break/continue 绑定最内层 loop id |
| AOT | `tc_aot_codegen.c` | 原生 C `while (1)` + 显式条件、原生 break/continue |

测试：`while_false`, `while_counted`, `while_nested`, `while_break_continue`, `while_var_reinitialize`；static `goto_inside_loop`, `label_inside_loop`, `break_outside_loop`, `continue_outside_loop`；unit `test_cfg.c`, `test_analyzer.c`, `test_executor.c`。

## goto / label 受限跳转（v0.0.31 沿用并加入范式隔离）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 枚举 | `tc_types.h` | `TC_STMT_LABEL_DEF` / `TC_STMT_GOTO`；`TcLabelDef` / `TcGoto`；5 个 goto/uninit `TcErrorKind` |
| Token | `tc_lexer.h` / `tc_lexer.c` | `TC_TOK_GOTO` / `TC_TOK_LABEL`；`:` → `TC_TOK_COLON` |
| 语法 | `tc_parser.c` | `label name:` / `goto name`；`tc_statement_free` 释放名 |
| 符号表 | `tc_symbol.c` | `TcLabelEntry`；`add_label` / `find_label` / `pop_labels`（随 `pop_scope`）|
| 分析 | `tc_analyzer_pass1.c` / `tc_analyzer_pass2.c` | 收标签、块路径、resolved target、while 范式隔离；**仅函数内** |
| 执行 | `tc_executor.c` | label 零成本；goto → `index.next = label.stmt_index + 1` |
| AOT | `tc_aot_codegen.c` | `tc_label_<n>: ;` / `goto tc_label_<n>;`（无 shim） |

**跳转判定**（块路径 = 祖先 if 的 then/else 编码）：

| 关系 | 结果 | 错误码 |
|------|------|--------|
| 同路径同深度 | ✅ 平级 | — |
| label 为 goto 祖先 | ✅ 向外 | — |
| label 在子路径 | ❌ | `TC_CE_JUMP_INTO_BLOCK` |
| 兄弟 / 非祖先 | ❌ | `TC_CE_JUMP_TO_SIBLING_BLOCK` |
| 未找到 | ❌ | `TC_CE_LABEL_NOT_FOUND` |
| 同作用域重名 | ❌ | `TC_CE_DUPLICATE_LABEL` |

不新增 `TcRhsKind`。详设：VM §6.1.7/§7.7/§8.6；AOT §4.7；图谱 [kg-cfg.md](kg-cfg.md)。

测试：`goto_simple`, `goto_forward`, `goto_out_of_if`, `goto_nested_out`, `goto_label_same_name`；static `goto_undefined`, `label_duplicate`, `goto_into_block`, `goto_sibling`；unit `test_symbol.c` / `test_analyzer.c` / `test_types.c`。

## var 强制初始化（v0.0.31）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| Parser | `tc_parser.c` | 缺 `=` → `TC_ERR_VAR_MISSING_INIT`（形态检查，与 CFG 无关） |
| AST | `tc_types.h` | `TcVarDef` 始终带 RHS；无 `has_rhs` |
| 错误 | `tc_types.c` | 打印名 `VarMissingInitializer` |

与数据流分工：缺初始化器 ≠ 未定义标识符 ≠ 路径未初始化（标准 §11.0）。测试：`var_missing_initializer`。

## 完整 CFG 确定初始化（v0.0.31）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 错误码 | `tc_types.h` / `tc_types.c` | `TC_ERR_UNINITIALIZED_VARIABLE` → `"UninitializedVariable"` |
| CFG + 固定点 | `tc_cfg.c` | `tc_cfg_build` / `tc_analyze_definite_init`；边、剪枝、读集、bitset 固定点 |
| 静态布尔 | `tc_const_eval.c` | `tc_try_eval_static_bool` / `_operand` |
| 辅助 | `tc_analyzer_dfa.c` | `TcInitState` / `tc_check_operand_init`；文件模式 `defer_to_cfg=1` |
| 警告 | `tc_warning.h` | 无（0.0.39 无语言警告） |

规则：仅全部可达前驱皆 `INIT` 才合流为 `INIT`；while 回边、continue、break 与 goto 均由 CFG 边表达；`tc_try_eval_static_bool` 对完整合法单层 bool RHS 做 true/false/unknown，剪枝后再固定点。字面量或更早可见 `let bool` 裁剪逻辑 RHS 读槽；`var` 不做跨语句值推测。

测试：valid `uninit_both_paths`, `uninit_shortcircuit`, `uninit_shortcircuit_let_bool`, `uninit_const_condition_if`, `uninit_const_condition_while`；static `uninit_simple`, `uninit_chain`, `uninit_multi`, `uninit_slot_value`, `uninit_if_path`, `uninit_goto_skip_init`, `uninit_shortcircuit_var_lhs`, `shortcircuit_let_*`, `diag_priority_*`；白盒 `test_analyzer` / `test_cfg`。

## 诊断阶段（v0.0.39）

权威：标准 §11.0 · Agent 摘要：[errors.md](errors.md) §诊断阶段 · 图谱：[kg-cfg.md](kg-cfg.md) / [pipeline.md](pipeline.md)。

用例：`diag_priority_*`；与 uninit / shortcircuit / var_missing 分工见 [test-map.md](test-map.md)。

## 加新特性时同步更新

1. 本文件新增 § 或扩展现有 §
2. [test-map.md](test-map.md) + `@knowledge-graph` 维护约定 + 对应 `kg-*.md`
3. `errors.md` / `syntax.md`（语言变更）
4. `scripts/vm/run_tests.sh`（+ AOT）
5. 新 `TcRhsKind` → `check_rhs_coverage.py [--fix]`

## TC-Embed 嵌入式运行时（v0.0.39 / Phase 7）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 公共 API | `src/vm/embed/tc_embed.h` | `TcEmbedCtx`、`TcEmbedFuncInfo`、`tc_embed_create/destroy`、`tc_embed_call`、`tc_embed_func_info`、`tc_embed_slot_write/read`、`tc_embed_ptr_encode/is_null/decode_slot` |
| 值桥接 | `src/vm/embed/tc_value_bridge.h` | `tc_value_from/to_int{8,16,32,64}`、`tc_value_from/to_uint{8,16,32,64}`、`tc_value_from/to_double/float`、`tc_value_from/to_bool` |
| VM 路径 | `src/vm/embed/tc_embed.c` | VM 模式：`TcExecuteCtx` + `tc_exec_call_function_public`；AOT 模式：`tc_aot_func_entry` 直调 |
| AOT 运行时 shim | `src/aot/tc_aot_embed_rt.h` | `tc_aot_func_entry` 函数表类型、`tc_aot_embed_abort`（非致命错误）、`tc_aot_embed_error_flag` |
| AOT codegen 嵌入模式 | `src/aot/tc_aot_codegen.c` | `tc_aot_emit_c`（`embed_mode` 参数）、`tc_aot_emit_func_table`、`tc_aot_emit_embed_header` |
| AOT CLI | `src/aot/main.c` | `--embed`、`-H/--header` |
| Executor 扩展 | `src/vm/executor/tc_executor.c/h` | `tc_exec_call_function_public`（原 static 提升为公共） |

**设计文档**：`docs/TC-Embed详细设计说明书-0.0.39.md`

**测试**：
- VM 单元：`test_embed.c` / check-embed（17 tests，含标量/float64/bool/ptr/static_var/错误路径）
- AOT 差分：`test_embed_aot.c` / check-embed-aot（7 tests，VM vs AOT 逐对对比）
- TC 用例：`tests/vm/embed/*.tc`（4 文件，ptr_sum/ptr_inplace/ptr_loop/nested_call）

**运行**：`cmake --build build --target check-embed` / `check-embed-aot`
