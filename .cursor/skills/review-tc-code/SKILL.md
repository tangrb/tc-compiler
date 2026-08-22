---
name: review-tc-code
description: Review TC-Compiler C99 code for correctness, safety, and style. Use for code review, PR review, validating compiler changes, or checking TcRhsKind/AOT/I/O/let/CFG consistency.
---

# TC-Compiler Code Review

遵循 Rule `coding-standards`。错误种类见 [errors.md](../tc-architecture/errors.md)。

> **现网 checklist ≈ v0.0.39 + Embed 0.0.39**。查：`TcType`、模块可见性、funcall、多域 CFG/`MISSING_RETURN`、ptr/memblock/struct 语义、Embed 非致命 abort；无 REPL 检查项。

**跨模块**：8 分发点 → `check_rhs_coverage.py`；TcStmtKind / CFG → [kg-cfg.md](../tc-architecture/kg-cfg.md)；let/短路 → [kg-eval.md](../tc-architecture/kg-eval.md)；模块 → [kg-module.md](../tc-architecture/kg-module.md)；函数 → [kg-func.md](../tc-architecture/kg-func.md)；Embed → [kg-embed.md](../tc-architecture/kg-embed.md)；测试映射 → [test-map.md](../tc-architecture/test-map.md)

## 正确性

- [ ] 有符号 strict：`tc_sadd/ssub/smul_overflow`；无符号 wrap 用掩码
- [ ] 所有 div/mod 路径检查除零；INT_MIN/-1 特殊处理
- [ ] strict cast 全源/目标组合（含 bool）；truncate 截断+扩展；bitcast 等宽非 bool
- [ ] `abs/neg(INT_MIN)` strict 报 `TC_ERR_INTEGER_OVERFLOW`
- [ ] 64 位无符号乘：`tc_umul64`
- [ ] bool：`is_bool` 标志；compare→bool；logic 操作数 bool；cast bool↔int
- [ ] 短路：`and` false / `or` true 跳过 RHS（Pass2 名称/类型仍检 + CFG 读集剪枝 + Executor + let）；左为 var 不推测
- [ ] 位运算：`and`/`or`/`not` 整数重载 vs bool 逻辑；`xor` 仅整数；`shl` wrap / `shr` 禁 wrap
- [ ] shift：`k >= n` 边界；let 禁 wrap
- [ ] let：`tc_eval_const_rhs` 全覆盖；源序可见；const_cast 规则；禁嵌套调用
- [ ] 格式符：`%t`↔bool；整数/浮点格式符与类型匹配
- [ ] I/O 逻辑集中在 `tc_io.c`；executor 与 `tc_aot_rt.c` 仅委托
- [ ] `var` 缺 `=` → `TC_ERR_VAR_MISSING_INIT`（不得降为 SYNTAX）
- [ ] if/while：条件须 `TC_BOOL`；各自 push/pop；`tc_statement_free` 递归释放
- [ ] while：范式隔离（内禁 goto/label）；break/continue 绑定最内层 loop id
- [ ] goto：块路径判定；无 `GOTO_SKIPS_VAR_INIT`；uninit 走 `tc_analyze_definite_init`
- [ ] 块作用域：`scope_end_stmt_index`；跨块 → undefined
- [ ] 跳过分支：`tc_stmt_index_skip_block` 按子树 span 推进
- [ ] 诊断阶段顺序符合 §11.0；同 Token 优先级与 `diag_priority_*` 一致

## 内存与安全

- [ ] init/free 配对，所有退出路径
- [ ] `tc_rhs_free` 覆盖新 RHS；`cast.source` / `const_ref.name` / `name` NULL 守卫
- [ ] `tc_warning_list_add`：先 strdup 再 realloc；失败回滚
- [ ] 动态数组不越界；CFG 节点/边 OOM → `TC_ERR_OUT_OF_MEMORY`

## 错误处理

- [ ] fail-fast；`tc_diagnostic_set` 正确 kind/line/col
- [ ] OOM 路径用 `TC_ERR_OUT_OF_MEMORY`，消息 `memory allocation failed`
- [ ] 返回 0/-1；新 `TcErrorKind` 有 `tc_error_kind_name` + `test_types.c` + 标准 §11.4

## 测试

- [ ] 新行为有 valid + static（或 runtime）且已注册 `run_tests.sh`
- [ ] 动控制流/uninit → `test_cfg` + uninit fixtures；动 AOT → AOT 差分
- [ ] 动 semantics/io/bitwise/shift/symbol/analyzer → 对应 `test_*.c`

## 风格

- [ ] 4 空格；`/* */` 注释；`tc_`/`Tc`/`TC_` 命名
- [ ] 模块文件 `tc_<module>.h` ↔ `tc_<module>.c`（`check_source_naming.py`）
- [ ] `#ifndef` 守卫

## 常见反例

```c
/* ❌ 有符号加法无溢出检查 */
int64_t r = a + b;

/* ✅ */
if (tc_sadd_overflow(a, b, &r)) { ... }

/* ❌ bool 字面量缺 is_bool */
TcLiteral lit = { .magnitude = 1 };

/* ✅ */
TcLiteral lit = { .magnitude = 1, .is_bool = 1 };

/* ❌ 固定点入口写错文件 */
/* tc_analyze_definite_init 在 tc_cfg.c，非 tc_analyzer_dfa.c */

/* ❌ executor 内联 format 逻辑 */
/* ✅ 委托 tc_io_write_formatted */
```
