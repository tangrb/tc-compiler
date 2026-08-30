# 测试映射速查

**何时读**：写/查 `.tc` 测试、确认错误是否已有用例。

## Agent 用法

1. **优先** `rg <名|子串> scripts/` — 比通读本文件快。
2. 按下方目录只打开**一节**账本；改 C 源码不必加载本文件。
3. 新增用例后：注册 `run_tests.sh` → 更新本文件规模/相关行 → `check_doc_counts.py`。

**规模**（`check_doc_counts.py` 校验）：**914 VM** · **~411 AOT（注册）** / **464 AOT（执行）** · unit **~3205** `check()`。跑法：Skill `run-tests`。

## 目录

| 节 | 内容 |
|----|------|
| Phase 6 / 7 | 模块 K、Embed L |
| Phase 1–2 | types / lexer / parser / module unit |
| 0.0.31 迁移 | 破坏性用例迁移动账 |
| Phase 3–5 | struct / CFG / 复合运行时等（下续各表） |
| Stress / 单元 / AOT | 文末汇总 |

## Phase 6（模块 K）账本

| 覆盖 | 文件 / 入口 | 要点 |
|------|-------------|------|
| CLI `-I` / 无 REPL / 版本 0.0.42 | `scripts/vm/run_tests.sh` cli golden、`include_search_ok` | K1/K4 |
| libtc `name` / `tc_run_program` | `test_libtc.c` / check-libtc | K2 |
| 源命名 / RHS 覆盖 | `check_source_naming.py` / `check_rhs_coverage.py` | K4-1/K4-2 |

## Phase 7（模块 L）TC-Embed 账本

| 覆盖 | 文件 / 入口 | 要点 |
|------|-------------|------|
| VM Embed 生命周期 / 槽位读写 | `test_embed.c` / check-embed | 创建/销毁/slot rw/越界/错误消息 |
| VM Embed 函数调用（标量/float64/bool） | `test_embed.c` / check-embed | plus/mult/ident 调用与返回值 |
| VM Embed ptr 数组 & ptr_store | `test_embed.c` / check-embed | ptr_load_sum、ptr_store_offset、static_var_persist |
| VM Embed 符号查询 | `test_embed.c` / check-embed | func_info/top_var/self_var |
| AOT Embed 差分测试（标量/ptr/嵌套/static/错误） | `test_embed_aot.c` / check-embed-aot | VM vs AOT 结果逐对对比；除零不终止 |
| AOT Embed 生命周期/幂等性 | `test_embed_aot.c` / check-embed-aot | tc_aot_init 幂等，多次创建/销毁 |
| TC Embed tc 用例（--check） | `tests/vm/embed/*.tc` → `scripts/vm/run_tests.sh` | ptr_sum/ptr_inplace/ptr_loop/nested_call |

## Phase 1（模块 A）unit 账本

| 覆盖 | 文件 / target | 要点 |
|------|---------------|------|
| `TcTypeTag` / `TcType` 编码 | `test_types.c` / check-types | scalar/make_ptr|memblock|struct |
| `tc_type_equals` / `tc_sizeof_bits` | 同上 | memblock 等价忽略 N；ptr 宽依赖目标 |
| STMT 24 / RHS 34 / 错误 86 | 同上 | inventory；`tc_error_kind_name` 白名单唯一性 |
| `TcRuntimeSlots` / `TcSlotDomain` | 同上 | init/free；符号 `slot_domain` 等 |
| RHS coverage | `check_rhs_coverage.py` | 8 分发点；const_eval 对复合 kind per-point skip |

## Phase 2（B+C+D）测试账本

| 覆盖 | 文件 / target | 要点 |
|------|---------------|------|
| Lexer 新关键字 / `#program` / `@` / `isize` | `test_lexer.c` / check-lexer | B-1～B-4 |
| Parser 模块头 / 类型 / return / funcall | `test_parser.c` / check-parser | C-1～C-7 |
| 模块 4a～4d / Self / 签名 / 歧义导入 / 菱形拓扑 | `test_module.c` / check-module | D-1～D-6、D-8；`diamond_import_*` |
| 模块错误 `.tc` | `tests/errors/module/`、`tests/modules/` | 无头、可见性、环、Self、成功 import（`--check`）；导入 struct 负例见 Phase 3 `imported_struct_*` |

## 0.0.31 破坏性迁移账本

| v0.0.26 用例 / 行为 | 0.0.31 已完成结果 | 阶段 |
|----------------------|------------------|------|
| `var_no_init.tc` | 迁为 static `var_missing_initializer.tc`，期望 `VarMissingInitializer` | M2 |
| `fp_arith_wrap.tc` / `fp_wrap_arith.tc` | 迁为 `ModeMismatch` static；位操作示例用 bitcast | M5 |
| `fp_cast_truncate.tc` | bitcast 往返正例 | M5 |
| `cast_literal.tc` | 迁为字面量 cast 正例或范围错误 | M5 |
| `const_cyclic_dep.tc` / `self_ref_let.tc` / `forward_reference.tc` | 统一期望 `UndefinedVariable` | M6/M10 |
| `if_cross_block_ref_*` | 统一期望 `UndefinedVariable` | M10 |
| `fp_cast_overflow.tc` | 统一期望 `CastOverflow` | M5/M10 |

上述 fixture 已全部完成迁移并同步 VM/AOT 注册。

## 错误 → 测试

| TcErrorKind | 代表用例 |
|-------------|---------|
| TC_ERR_SYNTAX | syntax_error, invalid_hex_overflow, **utf8_bom**, **null_char**, **unexpected_char**, **float_trailing_dot**, **compare_mode**, **memblock_count_only**, **invalid_utf8_comment**, **embedded_nul**, **funcall_arg_expr**, **struct_ctor_field_expr**, **module_layer_interleave**, **fp_wrap_on_compare** |
| TC_ERR_UNDEFINED_VARIABLE | undefined_variable, forward_reference, const_cyclic_dep, self_ref_let, let_short_circuit_invalid_rhs, **self_member_undefined**, **self_member_bare_name** |
| TC_ERR_DUPLICATE_DEFINITION | duplicate_def, duplicate_let_var, duplicate_var_let |
| TC_ERR_TYPE_MISMATCH | type_mismatch*, logic_type_error, bitwise_shift_type_mismatch, **self_member_type_mismatch**, **read_type_mismatch**, **bitcast_struct** |
| TC_ERR_LITERAL_OUT_OF_RANGE | literal_range* |
| TC_ERR_LITERAL_TYPE | literal_type_error, bool_literal_type_error |
| TC_ERR_SYNTAX（u 后缀） | **float_unsigned_suffix**、**negative_unsigned**、negative_unsigned_literal |
| TC_ERR_MODE_MISMATCH | wrap_mode_error*, abs_wrap_error, fp_ieee_on_int, fp_*wrap*_mode_mismatch, **const_shift_wrap_mode** |
| TC_ERR_SYNTAX（模式关键字） | keyword_error, cast_wrap_keyword, truncate_in_arith, bitwise_wrap_on_and_keyword_error, bitwise_shl_truncate_keyword_error, bitwise_wrap_on_shr_keyword_error |
| TC_ERR_CONSTANT_* | const_assign, assign_to_let, **struct_assign_let_outer_let_field**, **struct_assign_let_outer_var_field**, const_expr, const_overflow, const_div_zero, **let_const_cast_overflow**, **let_const_cast_overflow_fp**, let_const_literal_range, let_non_literal, let_nested_call, let_short_circuit_invalid_rhs, **operand_field_var_in_let**, **operand_field_var_in_const_op**, **operand_field_static_var_forward**, **ptr_store_through_param**, **memblock_type_count_zero**, **memblock_negative_count_type** |
| TC_ERR_COMPARISON_TYPE_MISMATCH | compare_type_mismatch, **compare_type_mismatch_var** |
| TC_ERR_FORMAT_* | format_string_error, format_type_mismatch*, format_operand_count*, **format_specifier_plus_unsigned**, **format_specifier_hash_bool**, **format_specifier_flags_mutex**, **format_specifier_t_width**, **format_width_overflow** |
| TC_ERR_DIVISION_BY_ZERO | div_zero*, mod_zero* |
| TC_ERR_INTEGER_OVERFLOW | signed_strict_overflow*, neg_int_min*, abs_int_min*, bitwise_shl_overflow_runtime |
| TC_ERR_CAST_OVERFLOW | cast_strict_overflow*, fp_cast_overflow |
| TC_ERR_IO | read_invalid*, read_out_of_range*, read_bool_invalid_input, read_fp_invalid, **read_fp_out_of_range**, **read_float_invalid** |
| TC_ERR_MODULE_LAYER | module_layer, **module_layer_interleave** |
| TC_ERR_MISSING_VISIBILITY | missing_visibility |
| TC_ERR_PROGRAM_MODE_MISUSE | program_mode_misuse, self_in_program, func_in_program, static_in_program |
| TC_ERR_IMPORT_NOT_FOUND | import_not_found |
| TC_ERR_IMPORT_NOT_LIB | import_not_lib |
| TC_ERR_DUPLICATE_IMPORT | duplicate_import |
| TC_ERR_CIRCULAR_IMPORT | circular_import, self_import |
| TC_ERR_FLOAT_OVERFLOW | fp_strict_overflow |
| TC_ERR_FLOAT_UNDERFLOW | fp_strict_underflow |
| TC_ERR_FLOAT_INVALID | fp_strict_invalid, **fp_mod_invalid**, **fp_mod_invalid_inf**, **fp_mod_invalid_before_divzero** |
| TC_ERR_UNINITIALIZED_VARIABLE | uninit_simple, uninit_chain, uninit_multi, uninit_slot_value, uninit_if_path, uninit_goto_skip_init |
| TC_ERR_LABEL_NOT_FOUND | goto_undefined, **goto_cross_function_label_not_found** |
| TC_ERR_DUPLICATE_LABEL | label_duplicate |
| TC_ERR_JUMP_INTO_BLOCK | goto_into_block |
| TC_ERR_JUMP_TO_SIBLING_BLOCK | goto_sibling |
| TC_ERR_VAR_MISSING_INIT | var_missing_initializer |
| TC_ERR_BITCAST_WIDTH | bitcast_width_mismatch |
| TC_ERR_LABEL_INSIDE_LOOP | label_inside_loop |
| TC_ERR_GOTO_INSIDE_LOOP | goto_inside_loop |
| TC_ERR_BREAK_OUTSIDE_LOOP | break_outside_loop |
| TC_ERR_CONTINUE_OUTSIDE_LOOP | continue_outside_loop |
| TC_ERR_OUT_OF_MEMORY | 无 .tc 用例（单元：`test_types.c` OutOfMemory；`rg TC_ERR_OUT_OF_MEMORY src/`） |
| TC_ERR_NULL_POINTER_DEREFERENCE | null_ptr_deref, null_ptr_store, null_ptr_cmp, memcopy_unsafe_null |
| TC_ERR_NULL_POINTER_ARITHMETIC | null_ptr_arith |
| TC_ERR_MEMBLOCK_INDEX_OUT_OF_RANGE_RT | memblock_oob_rt, memblock_oob_store_rt, **memblock_copy_overflow_guard** |
| TC_ERR_MEMCOPY_UNSAFE_INVALID_RANGE_RT | memcopy_unsafe_neg, **memcopy_unsafe_neg_index** |
| TC_ERR_INDENT_MIXED | indent_mixed_tab_body, indent_mixed_space_if |
| TC_ERR_INDENT_INSUFFICIENT | indent_insufficient_then, indent_insufficient_nested, indent_insufficient_block |
| TC_ERR_INDENT_ELSE_END | indent_else_mismatch, indent_end_mismatch, **indent_else_position** |
| TC_ERR_MISSING_END | if_missing_end_eof, if_missing_end_stmt, **while_missing_end** |
| TC_ERR_CONDITION_TYPE | if_cond_type_arith, **while_cond_type_arith** |
| TC_ERR_IMPORT_AMBIGUOUS | **import_ambiguous**（CLI 双 `-I`） |
| TC_ERR_CONSTANT_ASSIGNMENT | const_assign, assign_to_let, **struct_assign_let_outer_let_field**, **struct_assign_let_outer_var_field**, **read_into_let** |
| TC_ERR_STRUCT_IMMUTABLE_FIELD | struct_immutable_field |
| TC_ERR_PARAMETER_ASSIGNMENT | parameter_assignment, **parameter_assignment_read**, struct_assign_through_param, struct_assign_param_let |
| TC_CE_UNDEFINED_STRUCT | undefined_struct*, **imported_struct_bare_name**, **imported_struct_bare_ctor**, **imported_struct_not_imported**, **imported_struct_transitive**, struct_memblock_undefined, ptr_undefined_struct |
| TC_CE_PRIVATE_MEMBER_ACCESS | private_member_access, **imported_struct_private** |
| TC_ERR_TYPE_MISMATCH（if 条件字面量） | if_cond_type_literal |
| TC_ERR_UNDEFINED_VARIABLE（块外/跨块） | if_cross_block_ref_after_end, if_cross_block_ref_else_to_then, if_cross_block_ref_then_to_else |

stderr 子串详情：[errors.md](errors.md)。

## 特性 → 测试

| 特性 | valid | error |
|------|-------|-------|
| bool | bool_var, bool_cast, format_bool, read_bool, let_bool_constant, compare_ops, logic_ops | bool_literal_type_error, compare_type_mismatch, logic_type_error, read_bool_invalid_input |
| 比较 | compare_ops, const_expr, **compare_unsigned** | compare_type_mismatch, **compare_type_mismatch_var** |
| 逻辑 | logic_ops, const_expr, let_logic_short_circuit | logic_type_error |
| 位运算/移位 | bitwise_runtime, bitwise_and_or_xor_not_valid, bitwise_shift_shl_shr_valid, bitwise_shl_wrap_valid, bitwise_shift_k_ge_n_valid, bitwise_let_const_valid, bitwise_io_format_valid, let_wrap_allowed, **shift_edge_cases**, **shl_int64_neg_boundary** | bitwise_xor_bool_type_error, bitwise_wrap_on_*_keyword_error, bitwise_shl_truncate_keyword_error, bitwise_shift_type_mismatch, bitwise_shl_const_overflow, bitwise_shl_overflow_runtime |
| let | let_constant*, const_expr, let_bool_constant, let_logic_short_circuit, let_runtime_equivalence, let_wrap_allowed, let_float_ieee, let_float32_step_rounding, let_bitcast_payload, let_goto_inline, let_block_local_chain, **let_cast_const**, **fp_const_let_arith** | const_assign, assign_to_let, const_expr, const_cyclic_dep, self_ref_let, forward_reference, let_nested_call, let_short_circuit_invalid_rhs, const_overflow, const_div_zero, **let_const_cast_overflow***, let_const_literal_range, let_non_literal |
| cast | strict_cast_widen, truncate_cast, sign_extend_cast, cast_operations_all, bool_cast, cast_literal, **let_cast_const** | keyword_error, cast_wrap_keyword, truncate_in_arith, cast_bool_truncate_keyword_error, cast_truncate_bool_source_error, cast_strict_overflow* |
| bitcast | fp_bitcast_roundtrip, bitcast_roundtrip32, bitcast_roundtrip64, let_bitcast_payload, **nan_canonical_bits** | bitcast_width_mismatch, bitcast_bool_type_mismatch |
| wrap | wrap_arithmetic_all, wrap_sub_mul, unary_wrap, unary_wrap_unsigned | wrap_mode_error*, abs_wrap_error |
| format | format_output, format_hex_bin, format_bool, format_spec_all, format_spec_flags, **format_spec_table**, **format_width_max**, io_extended | format_string_error, format_type_mismatch*, format_operand_count*, **format_specifier_***, **format_width_overflow** |
| 多进制字面量 | hex_literal, oct_literal, bin_literal, literal_separator, literal_edge_cases, bin_hex_oct_io | leading_zero, negative_unsigned_literal, invalid_hex_overflow |
| I/O | read_write, read_bool, io_extended, fp_io, format_spec_fp, **format_spec_flags**；unit：`test_write_atomic_commit_no_partial`（I-10） | read_invalid*, read_out_of_range*, read_bool_invalid_input, read_fp_invalid, **read_fp_out_of_range**, **read_into_let**, **read_type_mismatch**, format_fp_type_mismatch |
| 浮点 float32/float64 | fp_basic, fp_arith, fp_arith_ieee, fp_compare, fp_cast, fp_bitcast_roundtrip, bitcast_roundtrip32, bitcast_roundtrip64, **nan_canonical_bits**, fp_io, fp_const_expr, fp_if_block, format_spec_fp, let_float_ieee, let_float32_step_rounding, let_bitcast_payload, **fp_neg_abs**, **fp_const_let_arith**, **fp_ieee_ops**, **fp_exact_subnormal**, **fp_mod**, **fp_mod_ieee_nan**, **fp_mod_edges** | fp_ieee_on_int, fp_wrap_on_compare, fp_arith_wrap_mode_mismatch, fp_wrap_arith_mode_mismatch, fp_wrap_mode_mismatch, fp_bitwise_type_error, fp_literal_range, format_fp_type_mismatch, fp_strict_*, **fp_strict_invalid_before_divzero**, fp_cast_overflow, fp_div_zero, **fp_mod_invalid**, **fp_mod_divzero**, **fp_mod_invalid_inf**, **fp_mod_invalid_before_divzero** |
| if 控制流 | if_basic, if_else, if_nested, if_chain, if_bool_literal, if_local_same_name, if_shadow_global, **if_false_skip_nested_then**, **if_and_or_condition**, **if_comparison_condition**, **if_not_condition**, **if_empty_body**, stress_if_nested | if_cross_block_ref_*, if_cond_type_*, indent_*, if_missing_end_* |
| goto / label | （顶层 goto 已禁；函数内执行 Phase 5） | goto_outside_function, label_outside_function, goto_toplevel_*, goto_undefined, **goto_cross_function_label_not_found**, label_duplicate, goto_into_block, goto_sibling |
| while / break / continue | while_false, while_counted, while_nested, while_break_continue, while_var_reinitialize | goto_inside_loop, label_inside_loop, break_outside_loop, continue_outside_loop；unit：test_analyzer / test_executor / test_cfg |
| 未初始化与静态布尔剪枝 | uninit_both_paths, uninit_shortcircuit, **uninit_shortcircuit_let_bool**, **uninit_const_condition_if**, **uninit_const_condition_while**, assign_uninit_var_valid, uninitialized_bool | var_missing_initializer, uninit_simple, uninit_chain, uninit_multi, uninit_slot_value, uninit_if_path, uninit_goto_skip_init, **uninit_shortcircuit_var_lhs**, **shortcircuit_let_invalid_rhs**, **shortcircuit_let_rhs_type**, **shortcircuit_let_forward_lhs**, **shortcircuit_let_out_of_scope_lhs**, **diag_priority_*** |
| Phase 3 复合类型 / 多域 CFG | phase3_nullptr, phase3_struct_ctor, phase3_memblock, phase3_memblock_fill, phase3_memblock_store, phase3_ptr_ops, phase3_ptr_load, phase3_ptr_cmp, phase3_struct_nested, phase3_struct_mut_ok（`--check` only）；**struct_field_operand_*** / **struct_field_named_count** / **struct_field_static_init**（含跨模块 `struct_field_static_init_run`）/ **struct_field_static_topo_ops**（unary/bitwise/shift/logic/float/cast/bitcast 操作数字段读 + 跨模块 `_run`）；**import_struct_type**（`<模块名>.<结构体名>` 类型/构造器/memblock）；**imported_struct_mid_ok**（中间库内限定名） | memblock_*, struct_*, ptr_*, float_special_non_float, float32_suffix_mismatch, unreachable_after_return, missing_return, goto/label outside, struct_nested_non_struct, struct_assign_through_param / param_let, **struct_assign_let_outer_***, **struct_self_ref**（值自引用 → STRUCT_VALUE_SELF_REF）、**struct_memblock_self_ref**、**struct_ptr_fwd_ref**、**struct_memblock_undefined**、**ptr_undefined_struct**、**ptr_struct_type_distinct**、**operand_field_var_in_***、**operand_field_static_var_forward**、**imported_struct_bare_name**、**imported_struct_bare_ctor**、**imported_struct_not_imported**、**imported_struct_transitive**、**imported_struct_private**；unit：test_type_check / **test_struct_self_reference** / **test_struct_field_access** / **test_module**（导入 struct 白盒） |
| Phase 4 函数 / static let | phase4_self_funcall, phase4_static_let, phase4_func_goto（`--check` only） | duplicate_function, function_name_conflict, parameter_name_conflict, param_shadow_local, duplicate_parameter, undefined_function, function_scope_access, funcall_*, argument_*, return_*, parameter_assignment, recursion_*, static_let_forward, static_var_bad_init, funcall_memblock_size, private_member_access |
| Phase 5 Executor / AOT | phase5_funcall_return, phase5_ptr_*, phase5_memblock_*, **phase5_static_var**, **phase5_self_static_let**, **phase5_self_static_ops**, phase5_nested_funcall；**struct 运行时**：phase5_struct_* / **mut_matrix_ok**、**ptr_self_ref**、**ptr_nested_self_ref**、**ptr_roundtrip**、**memblock_of_struct**、**memblock_deepcopy**（VM+AOT 差分）；StructLib / SelfStaticLetLib / SelfStaticOpsLib / StaticVarLib / **SelfMemblockCopyLib** / **SelfStructCopyLib**；**self_member_memblock_copy** / **self_member_struct_copy** | null_ptr_*, memblock_oob_*, memcopy_unsafe_*、**memcopy_unsafe_neg_index**；**self_member_***；struct static 错误见上 |
| Phase 6 CLI / API | cli version/help golden、`-I` include_search_ok；**cli -I path limit is not OutOfMemory**（D-15） | include_search_ok（无 `-I` → import not found）；`import_ambiguous`（双 `-I`） |
| K.3 补测（2026-07-24） | `isize_arith`/`usize_arith`；`let_const_cast_overflow*`；`while_cond_type_arith`/`while_missing_end`；`compare_type_mismatch_var`；`parameter_assignment_read` | unit：`test_libtc` search paths |
| P2 接受集（2026-08-29） | **identifier_named_padding**、**let_ptr_size**、**let_memblock_const**、**qualified_memblock_count**（+ QualMemblockCountLib）、**qualified_read_target**（+ QualReadLib）、**let_ptr_cast_nullptr** | **float_trailing_dot**、**compare_mode**、**memblock_count_only**、**invalid_utf8_comment**、**embedded_nul**、**funcall_arg_expr**、**struct_ctor_field_expr**、**module_layer_interleave**、**bitcast_struct**、**read_float_invalid**、**fp_wrap_on_compare**（比较模式现为 SYNTAX） |
| P3 错误码对齐（2026-08-29） | — | **float_unsigned_suffix**、**negative_unsigned**、**diag_priority_format_after_operand**、**const_shift_wrap_mode**；duplicate_function → `FUNCTION_NAME_CONFLICT`；goto_cross_function → `LABEL_NOT_FOUND`；keyword/cast wrap → `SYNTAX`；indent_else_position → `INDENT_ELSE_END` |
| P4 边界/格式/可移植性（2026-08-30） | **shl_int64_neg_boundary**、**nan_canonical_bits** | **memcopy_unsafe_neg_index**；unit：`test_umul64` / 算术 shr / AOT `slot_read` 类型 |
| P6 测试补齐与门禁（2026-08-30） | **format_spec_table**（§10.4 负值位模式 / isize / `#` 备用 / `%-12f`）、**format_width_max**（65535 `--check`）、**fp_mod_edges**（有限 mod ±inf、-0.0） | **fp_mod_invalid_inf**、**fp_mod_invalid_before_divzero**、**memblock_type_count_zero**；unit：`test_types` 86 码唯一 + `%0008d`/`%65535d` + intern count=0 哨兵；`test_type_check` §10.2 顺序 + 命名 N 折叠；`check_doc_counts.py` 附录 B 85 = 实现 86−1 |

## Stress（VM 10 个）

deep_recursion · let_chain · io_stress · many_vars_stress · type_combinatorial · stress_if_nested · stress_fp_chain · stress_many_ifs · massive_vars · many_operations

## 单元测试

| 文件（`N passed`） | 被测模块 | target |
|------|---------|--------|
| test_lexer.c (126) / test_lexer_extended.c (127) | tc_lexer.c | check-lexer / check-lexer-extended |
| test_semantics.c (508) / test_fp_mod.c (13) | tc_semantics.c / tc_sem_int.c / tc_sem_fp.c / tc_sem_cast.c | check-semantics / check-fp-mod |
| test_types.c (284) | tc_types.c | check-types |
| test_symbol.c (73) | tc_symbol.c | check-symbol |
| test_io.c (144) | tc_io.c | check-io |
| test_bitwise.c (16) / test_shift.c (23) | tc_semantics.c | check-bitwise / check-shift |
| test_parser.c (156) | tc_parser.c | check-parser |
| test_analyzer.c (296) | tc_analyzer*.c + tc_cfg.c + tc_const_eval.c；静态 bool 三态、DFA、诊断优先级 | check-analyzer |
| test_diagnostic.c (33) / test_libtc.c (95) | diagnostic / libtc ownership | check-diagnostic / check-libtc |
| test_cfg.c (91) / test_executor.c (20) | CFG 静态条件边与逻辑读集 / executor | check-cfg / check-executor |
| test_stmt_index.c (18) | tc_stmt_index.h | check-stmt-index |
| test_warning.c (59) | tc_warning.c | check-warning |
| test_type_check.c (54) | tc_type_check.c + analyzer 管线 | check-type-check |
| test_module.c (65) | tc_module.c（含菱形 import） | check-module |
| test_struct_field_access.c (34) | tc_struct_check.c + analyzer（字段读 / const 复合 / static let memblock 计数） | check-struct-field-access |
| test_embed.c (590) / test_embed_aot.c (380) | tc_embed.c / tc_aot_codegen.c | check-embed / check-embed-aot |

AOT（`scripts/aot/run_tests.sh`）：**411** 注册项（`run_diff_test` + `run_check_ok/fail` + CLI golden）；**464** 执行通过项（另含 `run_runtime_fail`、embed codegen 等）。历史 Release Gate **272** 仅作基线参考。

新用例注册 `scripts/vm/run_tests.sh`（+ AOT 如适用）；同步本文件 + `@knowledge-graph` + 对应 `kg-*.md` + `features/*.md`。
