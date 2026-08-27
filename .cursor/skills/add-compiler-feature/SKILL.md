---
name: add-compiler-feature
description: >-
  Add TC language features — new types, operators, statements, TcRhsKind, or
  TcStmtKind. Use when implementing new syntax, extending let/CFG/modules/functions,
  or adding format specifiers. Step 1: read feature-kinds.md ONE section only;
  reference features.md routing table then ONE features/*.md file; dispatch via
  @knowledge-graph + one kg-*.md.
---

# Add TC-Compiler Feature

> **v0.0.41** + Embed v0.0.41 · 类型内核 [types.md](../tc-architecture/types.md)

## Step 0：分类（只读 feature-kinds 一个 §）

[feature-kinds.md](feature-kinds.md) → 确定类型 → **只走对应 §**，勿读全文。

| 类型 | 首文件 |
|------|--------|
| 新标量类型 | `tc_types.h`（`TcTypeTag`）→ semantics → `tc_io.c` |
| 类型内核 / equals / sizeof / 槽位 | `tc_types.h`/`.c` → `test_types.c` |
| 新运算符/RHS | `tc_types.h` → parser → **8 分发点** → `check_rhs_coverage.py` |
| 新语句 | `TcStmtKind` → parser/analyzer/**tc_cfg**/executor/aot |
| I/O/format | **`tc_io.c`** + `tc_check_io_format` |
| let 扩展 | **`tc_const_eval.c`** → [kg-eval.md](../tc-architecture/kg-eval.md) |
| CFG / 未初始化 | **`tc_cfg.c`** → [kg-cfg.md](../tc-architecture/kg-cfg.md) |
| 模块 / import / Self | `tc_module.c` / `tc_scope.c` → [kg-module.md](../tc-architecture/kg-module.md) |
| 函数 / funcall / 调用图 | `tc_func_check.c` / `tc_callgraph.c` → [kg-func.md](../tc-architecture/kg-func.md) |
| ptr/memblock/struct | `tc_*_check.c` / `tc_*_exec.c` → [kg-eval.md](../tc-architecture/kg-eval.md) |
| Embed | `src/vm/embed/` → [kg-embed.md](../tc-architecture/kg-embed.md) |
| 仅诊断 | `TcErrorKind` + `tc_error_kind_name` + `test_types` + static |

参考：[features.md](../tc-architecture/features.md) 路由 → **一个** `features/*.md` · 分发：`@knowledge-graph` · 测试：[test-map.md](../tc-architecture/test-map.md)

## 实施顺序

`types → lexer → parser → analyzer(+module/func/const_eval+cfg) → semantics/io → executor → aot → embed → tests`

`tc_<m>.h`↔`.c` 成对 · `check_source_naming.py`

## Checklist（摘要）

| 层 | 必做 |
|----|------|
| types.h/c | 枚举/结构；`tc_error_kind_name` |
| lexer | `tc_keyword_token` |
| parser | `tc_parse_rhs` / `tc_parse_const_rhs` / `tc_rhs_free` / `tc_statement_free` |
| analyzer | Pass1/2 + `tc_stmt_index`；let→`tc_eval_const_rhs`；短路读集剪枝 |
| cfg | 新控制流必改 `tc_cfg_build_stmt`；测 `test_cfg` + uninit |
| semantics | `tc_exec_*` |
| executor | `tc_eval_rhs`；I/O 委托 |
| aot | emit + shim → `tc_exec_*` / `tc_io_*` |
| tests | valid+static+run_tests.sh；AOT；unit；`check_rhs_coverage.py` |
| docs | 语言标准；`features/*.md` + `test-map.md` + 对应 `kg-*.md` |

错误消息：[errors.md](../tc-architecture/errors.md)
