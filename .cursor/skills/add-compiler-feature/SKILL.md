---
name: add-compiler-feature
description: Add TC language features — types, operators, statements, TcRhsKind. Step 1 read feature-kinds.md ONE section only; reference features.md ONE section; dispatch in knowledge-graph index + one kg-*.md.
---

# Add TC-Compiler Feature

> 管线 **v0.0.39**（Phase 1–6）+ Embed **v0.0.39**。类型内核见 [types.md](../tc-architecture/types.md)。

## Step 0：分类（只读 feature-kinds 一个 §）

[feature-kinds.md](feature-kinds.md) → 确定类型 → **只走对应 §**，勿读全文。

| 类型 | 首文件 |
|------|--------|
| 新标量类型 | `tc_types.h`（`TcTypeTag`）→ semantics → `tc_io.c` |
| 类型内核 / equals / sizeof / 槽位 | `tc_types.h`/`.c` → `test_types.c`；摘要 [types.md](../tc-architecture/types.md) |
| 新运算符/RHS | `tc_types.h` → parser → semantics → **8 分发点** → `check_rhs_coverage.py` |
| 新语句 | `TcStmtKind` → parser/analyzer/**tc_cfg**/executor/aot + [kg-cfg.md](../tc-architecture/kg-cfg.md) |
| I/O/format | **`tc_io.c`** + `tc_check_io_format` |
| let 扩展 | **`tc_const_eval.c`** + `tc_parse_const_rhs` → [kg-eval.md](../tc-architecture/kg-eval.md) |
| CFG / 未初始化 | **`tc_cfg.c`** + `tc_try_eval_static_bool` → [kg-cfg.md](../tc-architecture/kg-cfg.md) |
| 模块 / import / Self | `tc_module.c` / `tc_scope.c` → [kg-module.md](../tc-architecture/kg-module.md) |
| 函数 / funcall / 调用图 | `tc_func_check.c` / `tc_callgraph.c` → [kg-func.md](../tc-architecture/kg-func.md) |
| ptr/memblock/struct | `tc_*_check.c` / `tc_*_exec.c` / AOT shim → [kg-eval.md](../tc-architecture/kg-eval.md) |
| Embed | `src/vm/embed/` → [kg-embed.md](../tc-architecture/kg-embed.md) |
| 仅诊断 | `TcErrorKind` + `tc_error_kind_name` + `test_types` + static |

参考实现：[features.md](../tc-architecture/features.md) **单 §** · 分发点：`@knowledge-graph` · 测试：[test-map.md](../tc-architecture/test-map.md)

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
| cfg | 新控制流必改 `tc_cfg_build_stmt` / 读集；测 `test_cfg` + uninit |
| semantics | `tc_exec_*` |
| executor | `tc_eval_rhs`；I/O 委托；跳过用 `tc_stmt_index_skip_block` |
| aot | emit + shim → `tc_exec_*` / `tc_io_*` / 复合 shim |
| tests | valid+static+run_tests.sh；AOT；unit；`check_rhs_coverage.py` |
| docs | 语言标准；`features.md` + `test-map.md` + `@knowledge-graph` / 对应 `kg-*.md` |

## 领域速记

| 主题 | 规则 |
|------|------|
| bool | `is_bool`；compare→bool；logic 操作数 bool |
| and/or/not 重载 | 整数→BITWISE；bool→LOGIC（短路读集剪枝） |
| shift | `xor` 仅整数；`shl` wrap；`shr` 禁 wrap；let 禁 wrap |
| let | 编译期；禁嵌套与 `FUNCALL_EXPR`；源序可见 |
| if/while | 多行 parse；缩进 R1–R7；块作用域 |
| goto | 仅函数内；块路径；while 范式隔离；uninit 走 CFG |
| var | 强制 `= rhs` → `VAR_MISSING_INIT` |
| AOT/I/O | shim→semantics；I/O→tc_io；cast→tc_sem_cast |
| Embed | 非致命 abort；API VM/AOT 兼容 |

错误消息：[errors.md](../tc-architecture/errors.md)
