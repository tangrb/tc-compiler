# 特性类型决策树

先分类，再只走对应路径；完整 checklist 见 [SKILL.md](SKILL.md)。

## 1. 判断特性类型

```
新特性是什么？
├─ 新基础/标量类型（如 bool）   → §2 新类型
├─ 类型内核 API（equals/sizeof/槽位）→ §2 + types.md
├─ 新运算符（算术/比较/逻辑/一元）→ §3 新运算符
├─ 新语句形式（新 STMT kind）   → §4 新语句
├─ 新表达式/RHS 形式            → §5 新 RHS
├─ 新格式符 / I/O 行为          → §6 格式与 I/O
├─ 新关键字/修饰符（wrap 类）   → §7 关键字
├─ 扩展 let 编译期能力          → §8 编译期常量
├─ 模块 / 函数 / 复合类型扩展   → 对应 kg-module / kg-func / kg-eval
├─ Embed / 宿主 API             → kg-embed + embed-src
└─ 仅新诊断/错误消息            → §9 仅诊断
```

> 现网：`TcTypeTag`（含 PTR/MEMBLOCK/STRUCT）、`TcType`、24 STMT / 34 RHS / **90** 错误已入 `tc_types.h`；解析与运行时已落地。

## §2 新类型

| 层 | 改动 |
|----|------|
| types.h | `TcTypeTag`（标量 AST）；完整类型用 `TcType` + `tc_type_make_*` |
| types.c | `tc_type_is_*`、parse/name、`tc_type_equals`、`tc_sizeof_bits` |
| lexer | 类型名关键字、字面量 token |
| parser | var/let/read/write 类型参数 |
| analyzer | 类型检查、格式符匹配、literal_fits |
| semantics | cast、literal_to_value、运算类型分发 |
| tc_io.c | read/write 格式化 |
| executor / aot rt | 委托 `tc_io.c` |
| tests | 全类型边界 + cast + format；内核 API → `test_types.c` |

## §3 新运算符

| 层 | 改动 |
|----|------|
| types.h | `TcXxxOp` 枚举、`TcRhsKind`、Token union |
| types.c | `tc_xxx_op_parse` |
| lexer | 运算符关键字 → token |
| parser | `tc_parse_rhs` + 可选 `tc_parse_const_rhs` |
| analyzer | 操作数/结果类型；let 则 `tc_eval_const_rhs` |
| semantics | `tc_exec_xxx` 实现 |
| executor | `tc_eval_rhs` case |
| aot | emit + `tc_aot_xxx` → `tc_exec_xxx` |
| tests | valid 全覆盖 + static 类型错 + runtime 溢出/除零 |

**短路逻辑**（and/or bool 路径）：Pass2 仍检名称/类型；CFG 不记 RHS 读槽；Executor 跳过 RHS。左为 var → 不做跨语句常量推测。

**and/or/not 整数重载**：parser 按类型参数分派 `TC_RHS_LOGIC_*` vs `TC_RHS_BITWISE_*`。

## §4 新语句

| 层 | 改动 |
|----|------|
| types.h | `TcStmtKind` + 结构体 |
| lexer | 新关键字 token |
| parser | `tc_parse_statement` 或专用 `tc_parse_*_stmt`；多行则 `tc_parse_source_to_program` |
| parser | `tc_statement_free` 递归释放 |
| symbol | 若块级作用域：`push_scope`/`pop_scope`/`find_in_scope` |
| analyzer | Pass1/Pass2 **递归** |
| cfg | **`tc_cfg_build_stmt`**；控制流必测 `test_cfg` / uninit |
| executor | `tc_execute_statement_impl` |
| aot | `tc_aot_emit_statement_impl` |
| tests | valid + static + AOT 差分 |

**参考**：`if` / `while` / `goto` — [features.md](../tc-architecture/features.md)；分发点 `@knowledge-graph` + [kg-cfg.md](../tc-architecture/kg-cfg.md)。

## §5 新 RHS

同 §3，但重点在 `TcRhsKind` 与 `tc_parse_rhs`/`tc_parse_const_rhs` 双路径。

- 仅 let 可用 → 只加 `tc_parse_const_rhs` + `tc_eval_const_rhs`
- 运行时可用 → `tc_parse_rhs` + `tc_eval_rhs`
- 两者都可用 → 两处都加，let 侧禁 wrap/truncate
- 覆盖：补齐 8 分发点 + `check_rhs_coverage.py`（per-point skip 须写明理由）

## §6 格式与 I/O

只改 `tc_io.c` + `tc_check_io_format`（`tc_analyze_6e.c`）；executor/AOT **委托**。测 VM + AOT。

## §7 关键字

`tc_lexer.c` → parser → 诊断（`TC_CE_KEYWORD` / `MODE_MISMATCH` 等）。

## §8 编译期常量

`tc_parse_const_rhs` + `tc_eval_const_rhs`；源序可见；禁 `FUNCALL_EXPR`；见 [kg-eval.md](../tc-architecture/kg-eval.md)。

## §9 仅诊断

`TcErrorKind` + `tc_error_kind_name` + `test_types.c` + 语言/编译器标准 + static fixture。
