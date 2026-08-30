# 特性 — 控制流与数据流

**只读本文件** — 由 [features.md](../features.md) 路由指向。

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

分发点总表见 Rule `knowledge-graph` →「TcStmtKind 分发点」+ [kg-cfg.md](../kg-cfg.md)「块作用域」。

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

不新增 `TcRhsKind`。详设：VM §6.1.7/§7.7/§8.6；AOT §4.7；图谱 [kg-cfg.md](../kg-cfg.md)。

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
| 警告 | `tc_warning.h` | 无（0.0.42 无语言警告） |

规则：仅全部可达前驱皆 `INIT` 才合流为 `INIT`；while 回边、continue、break 与 goto 均由 CFG 边表达；`tc_try_eval_static_bool` 对完整合法单层 bool RHS 做 true/false/unknown，剪枝后再固定点。字面量或更早可见 `let bool` 裁剪逻辑 RHS 读槽；`var` 不做跨语句值推测。

测试：valid `uninit_both_paths`, `uninit_shortcircuit`, `uninit_shortcircuit_let_bool`, `uninit_const_condition_if`, `uninit_const_condition_while`；static `uninit_simple`, `uninit_chain`, `uninit_multi`, `uninit_slot_value`, `uninit_if_path`, `uninit_goto_skip_init`, `uninit_shortcircuit_var_lhs`, `shortcircuit_let_*`, `diag_priority_*`；白盒 `test_analyzer` / `test_cfg`。

## 诊断阶段（v0.0.42）

权威：标准 §11.0 · Agent 摘要：[errors.md](../errors.md) §诊断阶段 · 图谱：[kg-cfg.md](../kg-cfg.md) / [pipeline.md](../pipeline.md)。

用例：`diag_priority_*`；与 uninit / shortcircuit / var_missing 分工见 [test-map.md](../test-map.md)。
