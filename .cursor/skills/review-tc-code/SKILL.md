---
name: review-tc-code
description: >-
  Review TC-Compiler C99 changes for correctness, safety, and style. Use for PR
  review, code review, or pre-merge checks — TcRhsKind coverage, AOT/I/O delegation,
  let/CFG consistency, Embed abort. Checklist below; feature checklist see
  add-compiler-feature skill.
---

# TC-Compiler Code Review

遵循 Rule `coding-standards`。合入前扫 [gotchas.md](../tc-architecture/gotchas.md)。

**跨模块只读一个 kg**：CFG→[kg-cfg.md](../tc-architecture/kg-cfg.md) · let→[kg-eval.md](../tc-architecture/kg-eval.md) · 模块→[kg-module.md](../tc-architecture/kg-module.md) · 函数→[kg-func.md](../tc-architecture/kg-func.md) · Embed→[kg-embed.md](../tc-architecture/kg-embed.md)

新特性/RHS 分层 checklist → Skill `add-compiler-feature`（本节只列 **Review 专有** 项）。

## 正确性

- [ ] 有符号 strict：`tc_sadd/ssub/smul_overflow`；无符号 wrap 用掩码
- [ ] div/mod 除零；INT_MIN/-1
- [ ] strict cast / truncate / bitcast 全组合
- [ ] bool：`is_bool`；compare→bool；logic 操作数 bool
- [ ] 短路：and/or 三侧一致（Pass2 + CFG + Executor + let）
- [ ] 位运算：`xor` 整数；`shl` wrap / `shr` 禁 wrap
- [ ] I/O 仅在 `tc_io.c`；executor / `tc_aot_rt.c` 委托
- [ ] if/while bool；while 范式；break/continue loop id
- [ ] goto 块路径；uninit → `tc_analyze_definite_init`（`tc_cfg.c`）
- [ ] 诊断阶段顺序 §11.0

## 内存与安全

- [ ] init/free 配对；`tc_rhs_free` 覆盖新 RHS
- [ ] `tc_warning_list_add`：先 strdup 再 realloc
- [ ] CFG OOM → `TC_ERR_OUT_OF_MEMORY`

## 错误处理

- [ ] fail-fast；OOM 消息 `memory allocation failed`
- [ ] 新 `TcErrorKind`：`tc_error_kind_name` + `test_types.c` + §11.4

## 测试与风格

- [ ] valid + static 已注册；CFG/AOT/Embed 对应 target
- [ ] `tc_<module>.h` ↔ `.c`；4 空格；`tc_`/`Tc`/`TC_`

## 常见反例

```c
/* ❌ executor 内联 format → ✅ 委托 tc_io_write_formatted */
/* ❌ tc_analyze_definite_init 在 tc_analyzer_dfa.c → ✅ 在 tc_cfg.c */
```

错误码查表：[errors.md](../tc-architecture/errors.md)（`rg`，勿通读）
