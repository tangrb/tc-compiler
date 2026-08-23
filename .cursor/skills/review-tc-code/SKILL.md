---
name: review-tc-code
description: >-
  Review TC-Compiler C99 code for correctness, safety, and style. Use for code
  review, PR review, or validating compiler changes — especially TcRhsKind dispatch
  coverage, AOT/I/O delegation, let/CFG consistency, and Embed abort semantics.
  Follows coding-standards rule; load one kg-*.md only if cross-module.
---

# TC-Compiler Code Review

遵循 Rule `coding-standards`。错误种类 [errors.md](../tc-architecture/errors.md)。

**跨模块时只读一个**：CFG→[kg-cfg.md](../tc-architecture/kg-cfg.md) · let→[kg-eval.md](../tc-architecture/kg-eval.md) · 模块→[kg-module.md](../tc-architecture/kg-module.md) · 函数→[kg-func.md](../tc-architecture/kg-func.md) · Embed→[kg-embed.md](../tc-architecture/kg-embed.md)

## 正确性

- [ ] 有符号 strict：`tc_sadd/ssub/smul_overflow`；无符号 wrap 用掩码
- [ ] 所有 div/mod 路径检查除零；INT_MIN/-1 特殊处理
- [ ] strict cast 全源/目标组合（含 bool）；truncate 截断+扩展；bitcast 等宽非 bool
- [ ] `abs/neg(INT_MIN)` strict 报 `TC_ERR_INTEGER_OVERFLOW`
- [ ] bool：`is_bool` 标志；compare→bool；logic 操作数 bool
- [ ] 短路：and/or 三侧一致（Pass2 + CFG 读集剪枝 + Executor + let）
- [ ] 位运算：`xor` 仅整数；`shl` wrap / `shr` 禁 wrap；let 禁 wrap
- [ ] let：`tc_eval_const_rhs` 全覆盖；源序可见；禁嵌套调用
- [ ] 格式符：`%t`↔bool；整数/浮点格式符与类型匹配；标志互斥（见 [features/scalar.md](../tc-architecture/features/scalar.md) § I/O）
- [ ] I/O 逻辑集中在 `tc_io.c`；executor 与 `tc_aot_rt.c` 仅委托
- [ ] `var` 缺 `=` → `TC_ERR_VAR_MISSING_INIT`（不得降为 SYNTAX）
- [ ] if/while：条件须 `TC_BOOL`；各自 push/pop
- [ ] while：范式隔离；break/continue 绑定最内层 loop id
- [ ] goto：块路径判定；uninit 走 `tc_analyze_definite_init`
- [ ] 块作用域：`scope_end_stmt_index`；跨块 → undefined
- [ ] 诊断阶段顺序符合 §11.0

## 内存与安全

- [ ] init/free 配对，所有退出路径
- [ ] `tc_rhs_free` 覆盖新 RHS；NULL 守卫
- [ ] `tc_warning_list_add`：先 strdup 再 realloc
- [ ] CFG 节点/边 OOM → `TC_ERR_OUT_OF_MEMORY`

## 错误处理

- [ ] fail-fast；`tc_diagnostic_set` 正确 kind/line/col
- [ ] OOM → `TC_ERR_OUT_OF_MEMORY`，消息 `memory allocation failed`
- [ ] 新 `TcErrorKind` 有 `tc_error_kind_name` + `test_types.c` + 标准 §11.4

## 测试

- [ ] 新行为有 valid + static 且已注册 `run_tests.sh`
- [ ] 动控制流/uninit → `test_cfg`；动 AOT → AOT 差分
- [ ] 动 semantics/io/cfg → 对应 `test_*.c`

## 风格

- [ ] 4 空格；`/* */` 注释；`tc_`/`Tc`/`TC_` 命名
- [ ] `tc_<module>.h` ↔ `tc_<module>.c`（`check_source_naming.py`）

## 常见反例

```c
/* ❌ 有符号加法无溢出检查 */
int64_t r = a + b;

/* ✅ */
if (tc_sadd_overflow(a, b, &r)) { ... }

/* ❌ executor 内联 format 逻辑 */
/* ✅ 委托 tc_io_write_formatted */

/* ❌ 固定点入口写错文件 */
/* tc_analyze_definite_init 在 tc_cfg.c，非 tc_analyzer_dfa.c */
```
