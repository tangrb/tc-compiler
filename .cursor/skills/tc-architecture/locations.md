# 符号 → 文件定位

**何时读**：需要知道函数/类型定义在哪个 `.c` 文件。**更快**：`rg "符号名" src/ --glob '*.c'`（见底部命令）。

用 `rg` 快速定位；**跨模块分发点**见 `@knowledge-graph`。**v0.0.41** + Embed · **无 REPL**。

## 流水线入口

| 符号 | 文件 |
|------|------|
| `tc_compile_source` / `tc_compile_file_opts` / `TcCompileOptions` / `tc_run_program` | `src/libtc/tc_lib.c` |
| `tc_parse_source_to_program` | `src/vm/parser/tc_parser.c` |
| `tc_run_file` / `tc_run_source` | `src/vm/driver/tc_driver.c` |
| `tc_tokenize_line` | `src/vm/lexer/tc_lexer.c` |
| `tc_parse_statement` / `tc_parse_if_stmt` / `tc_parse_while_stmt` | `src/vm/parser/tc_parser.c` |
| `tc_parse_struct_def` / 字段行 / `@padding` | `src/vm/parser/tc_parser_struct.c` |
| `tc_parse_type_syntax` | `src/vm/parser/tc_parser_type.c`（含 `Mod.Name`） |
| `tc_struct_table_register_program` / `tc_struct_table_find` | `src/vm/analyzer/tc_struct_check.c` |
| `tc_parse_func_def` | `src/vm/parser/tc_parser_func.c` |
| var/let/static/import/return/funcall/field/ptr/memblock/memcopy 语句 | `src/vm/parser/tc_parser_stmt.c` |
| `tc_parse_rhs` / `tc_parse_const_rhs` | `src/vm/parser/tc_parser_rhs.c` |
| `tc_analyze` / `tc_analyze_ex` / `tc_pass1_collect_symbols` / `tc_pass2_type_check` | `src/vm/analyzer/tc_analyzer.c` |
| `tc_module_check_structure` / `tc_module_resolve_imports` / `tc_module_collect_signatures` | `src/vm/analyzer/tc_module.c` |
| `tc_scope_*` / `tc_member_index_*` / Self 可见性 | `src/vm/analyzer/tc_scope.c` |
| `tc_func_check_*` / `tc_func_eval_static_lets` | `src/vm/analyzer/tc_func_check.c` |
| `tc_callgraph_check` | `src/vm/analyzer/tc_callgraph.c` |
| `tc_pass1_collect_stmt` / `tc_mark_block_scope_end` | `src/vm/analyzer/tc_analyzer_pass1.c` |
| `tc_pass2_check_stmt` / `tc_pass2_collect_labels` | `src/vm/analyzer/tc_analyzer_pass2.c` |
| `tc_check_rhs` / `tc_check_operand` / `tc_check_condition` | `src/vm/analyzer/tc_analyzer_pass2_rhs.c` |
| `tc_analyze_6a_collect_labels` | `src/vm/analyzer/tc_analyze_6a.c` |
| `tc_check_io_format` | `src/vm/analyzer/tc_analyze_6e.c` |
| `tc_cfg_build` / `tc_cfg_build_all` / `tc_analyze_definite_init(_all)` / `tc_cfg_free` | `src/vm/analyzer/tc_cfg.c` |
| `tc_cfg_add_rhs_reads` / `tc_cfg_prune_constant_edges` | `src/vm/analyzer/tc_cfg.c` |
| `tc_check_operand_init` / `tc_prescan_init_history` / `tc_block_path_*` | `src/vm/analyzer/tc_analyzer_dfa.c` |
| `tc_eval_const_rhs` / `tc_try_eval_static_bool` / `tc_try_eval_static_bool_operand` | `src/vm/analyzer/tc_const_eval.c` |
| `tc_type_check_rhs` / `tc_*_check_*` | `src/vm/analyzer/tc_{type,ptr,memblock,struct}_check.c` |
| `tc_execute` / `tc_eval_rhs` / `tc_execute_statement_impl` | `src/vm/executor/tc_executor.c` |
| `tc_exec_call_function_public` / `tc_exec_init_all_static_vars` | `src/vm/executor/tc_executor.c` |
| `tc_call_frame_push` / `tc_call_frame_pop` | `src/vm/executor/tc_call_frame.c` |
| `tc_exec_ptr_*` / `tc_exec_memblock_*` / `tc_exec_struct_*` | `src/vm/executor/tc_{ptr,memblock,struct}_exec.c` |
| `tc_aot_emit_c` / `tc_aot_emit_rhs` / `tc_aot_emit_statement_impl` | `src/aot/tc_aot_codegen.c` |
| `tc_aot_emit_func_table` / `tc_aot_emit_embed_header` | `src/aot/tc_aot_codegen.c` |

## 嵌入运行时（v0.0.41）

| 符号 | 文件 |
|------|------|
| `tc_embed_create` / `tc_embed_create_aot` / `tc_embed_destroy` | `src/vm/embed/tc_embed.c` |
| `tc_embed_call` / `tc_embed_func_info` | `src/vm/embed/tc_embed.c` |
| `tc_embed_top_var_slot` / `tc_embed_self_var_slot` | `src/vm/embed/tc_embed.c` |
| `tc_embed_slot_write` / `tc_embed_slot_count` / `tc_embed_slot_read` | `src/vm/embed/tc_embed.c` |
| `tc_embed_get_error` / `tc_embed_had_error` | `src/vm/embed/tc_embed.c` |
| `tc_embed_ptr_encode` / `tc_embed_ptr_is_null` / `tc_embed_ptr_decode_slot` | `src/vm/embed/tc_embed.h`（static inline） |
| `tc_value_from_*` / `tc_value_to_*` | `src/vm/embed/tc_value_bridge.h`（static inline） |
| `tc_aot_func_entry` / `tc_aot_embed_abort` / `tc_aot_embed_error_flag` | `src/aot/tc_aot_embed_rt.h` |

## 语义

| 符号 | 文件 |
|------|------|
| `tc_exec_arith` / `tc_exec_unary` | `src/vm/runtime/tc_sem_int.c` |
| `tc_exec_compare` / `tc_exec_logic_binary` / `tc_exec_logic_unary` | `src/vm/runtime/tc_semantics.c` |
| `tc_exec_bitwise_binary` / `tc_exec_bitwise_unary` / `tc_exec_shift` | `src/vm/runtime/tc_semantics.c` |
| `tc_exec_cast` / `tc_exec_truncate` / `tc_exec_bitcast` | `src/vm/runtime/tc_sem_cast.c` |
| `tc_literal_fits_context` / `tc_literal_to_value` | `src/vm/runtime/tc_semantics.c` |

## 类型与枚举

| 符号 | 文件 |
|------|------|
| `TcTypeTag` / `TcType` / `TcRhsKind` / `TcStmtKind` / `TcErrorKind` | `src/vm/runtime/tc_types.h` |
| `TcSlotDomain` / `TcRuntimeSlots` / `TcSymKind`（含 PARAMETER/STATIC_*） | `src/vm/runtime/tc_types.h` |
| `tc_type_parse` / `tc_type_is_*` / `tc_*_op_parse` | `src/vm/runtime/tc_types.c` |
| `tc_type_scalar` / `tc_type_from_tag` / `tc_type_scalar_tag` / `tc_type_make_{ptr,memblock,struct}` | `src/vm/runtime/tc_types.c` |
| `tc_type_equals` / `tc_sizeof_bits(_ex)` / `tc_target_ptr_width_bits` | `src/vm/runtime/tc_types.c` |
| `tc_runtime_slots_init` / `tc_runtime_slots_free` | `src/vm/runtime/tc_types.c` |
| `tc_bitwise_op_parse` / `tc_shift_op_parse` | `src/vm/runtime/tc_types.c` |
| `tc_diagnostic_set` / `tc_diagnostic_print` | `src/vm/runtime/tc_diagnostic.c` |
| `tc_error_kind_name` / `tc_warning_kind_name` | `src/vm/runtime/tc_types.c` |
| `tc_warning_list_init/add/free/print` | `src/vm/runtime/tc_warning.c` |
| `tc_symbol_table_add` / `push_scope` / `pop_scope` / `find_in_scope` / `find_in_current_scope` / `current_scope` | `src/vm/runtime/tc_symbol.c` |
| `TcLabelEntry` / `tc_symbol_table_add_label` / `find_label` / `pop_labels` / `clear_labels` | `src/vm/runtime/tc_symbol.c` |
| `tc_stmt_index_reset` / `tc_stmt_index_take` / `tc_stmt_subtree_index_count` / `tc_stmt_block_index_span` / `tc_stmt_index_skip_block` | `src/vm/runtime/tc_stmt_index.h`（header-only） |

## let 编译期常量

| 符号 | 文件 |
|------|------|
| `tc_eval_const_rhs` / `tc_eval_const_operand` | `src/vm/analyzer/tc_const_eval.c` |
| `tc_resolve_const_value` | `src/vm/analyzer/tc_const_eval.c` |
| `tc_const_cast_allowed` / `tc_const_map_runtime_error` | `src/vm/analyzer/tc_const_eval.c` |
| `tc_const_visit_contains` | `src/vm/analyzer/tc_const_eval.c` |
| `tc_try_eval_static_bool` / `tc_try_eval_static_bool_operand` | `src/vm/analyzer/tc_const_eval.c` |

## AOT shim

| 符号 | 文件 |
|------|------|
| `tc_aot_arith` / `tc_aot_cast` / `tc_aot_truncate` / `tc_aot_bitcast` / `tc_aot_compare` | `src/aot/tc_aot_rt.c` |
| `tc_aot_logic` / `tc_aot_write` / `tc_aot_read` | `src/aot/tc_aot_rt.c` |
| `tc_aot_bitwise_binary` / `tc_aot_bitwise_unary` / `tc_aot_shift` | `src/aot/tc_aot_rt.c` |
| `tc_aot_ptr_*` / `tc_aot_memblock_*` / `tc_aot_struct_*` | `src/aot/tc_aot_rt.c` |

## I/O（统一入口）

| 符号 | 文件 |
|------|------|
| `tc_io_write_formatted` / `tc_io_write_value` | `src/vm/runtime/tc_io.c` |
| `tc_io_read_value` / `tc_io_read_digits` | `src/vm/runtime/tc_io.c` |
| executor 调用点 | `src/vm/executor/tc_executor.c`（委托） |
| AOT 调用点 | `src/aot/tc_aot_rt.c`（`tc_aot_write/read` → `tc_io_*`） |

## 常用 rg 命令

```bash
rg "TC_RHS_" src/ --glob '*.c'
rg "TC_ERR_|TC_CE_|TC_RE_" src/vm/runtime/tc_types.h
rg "TC_ERR_OUT_OF_MEMORY" src/
rg "tc_analyze_definite_init|tc_cfg_build" src/
rg "tc_stmt_index" src/ --glob '*.{c,h}'
rg "tc_keyword_token" src/vm/lexer/tc_lexer.c
rg "foo.tc" scripts/
```

## Parser 分发点

| 函数 | 用途 |
|------|------|
| `tc_parse_statement` | 语句入口（单行） |
| `tc_parse_if_stmt` / `tc_parse_while_stmt` | if/while 多行块 + 缩进 |
| `tc_parse_source_to_program` | 文件模式：模块头 + 行扫描 + 通用块分派 |
| `tc_statement_free` | 释放语句（含 if/while/func 递归 body） |
| `tc_parse_rhs` | var 初始化 / assign 右侧 |
| `tc_parse_const_rhs` | let 初始化（更严） |
| `tc_parse_and_or_not_rhs` | and/or/not 按类型分派 logic/bitwise |
| `tc_parse_bitwise_bin_rhs` | xor 入口（及整数 and/or/not） |
| `tc_parse_shift_rhs` | shl/shr（仅 shl 可选 wrap） |
| `tc_rhs_free` / `tc_operand_free` | 释放所有 RHS 变体（NULL 守卫） |

## Analyzer 分发点

| 函数 | 用途 |
|------|------|
| `tc_pass1_collect_stmt` | Pass1 递归（if/while/func 作用域、固定 slot/loop id） |
| `tc_pass2_check_stmt` | Pass2 递归 + DFS stmt_index |
| `tc_check_rhs` | Pass2 类型检查（`tc_analyzer_pass2_rhs.c`）；复合经 `tc_type_check_rhs` |
| `tc_cfg_build_all` / `tc_analyze_definite_init_all` | 多域 CFG + 确定初始化 |
| `tc_try_eval_static_bool` | 单层合法 bool RHS → true/false/unknown |
| `tc_eval_const_rhs` | let 编译期求值 |
| `tc_check_operand` / `tc_check_operand_init` | 操作数类型 / 未初始化（文件模式 defer 给 CFG） |
| `tc_check_literal` | 字面量类型/范围（含 bool is_bool） |
| `tc_check_io_format` | write 格式符与类型匹配（含 `%t`） |

## TcRhsKind 全链路分发（8 个文件）

加新 RHS kind 需改：`tc_parse_rhs`、`tc_parse_const_rhs`、`tc_rhs_free`、`tc_check_rhs`、`tc_eval_const_rhs`、`tc_eval_rhs`、`tc_aot_emit_rhs`、对应 `tc_aot_*` shim。改读集/静态布尔另看 `tc_cfg_add_rhs_reads` / `tc_try_eval_static_bool`。

## TcStmtKind 全链路分发（24 种，全部可解析）

加新 STMT kind 需改：`tc_parse_source_to_program` / `tc_parse_*_stmt`、`tc_statement_free`、`tc_pass1_collect_stmt`、`tc_pass2_check_stmt`、`tc_cfg_build_stmt`、`tc_execute_statement_impl`、`tc_aot_emit_statement_impl`；若涉及作用域则同步 `tc_symbol.c`；控制流必动 `tc_cfg.c`。

完整表：`@knowledge-graph` 分发点表；RHS 细节 [kg-dispatch.md](kg-dispatch.md)；测试映射：[test-map.md](test-map.md)；特性：[features.md](features.md) 路由 → `features/*.md`。
