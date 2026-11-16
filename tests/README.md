# Tests

测试层级、覆盖清单与门禁要求见
[TC 开发测试计划 0.0.46](../docs/TC开发测试计划-0.0.46.md)；当前门禁状态见
[门禁状态](../docs/gates/README.md)。

| Suite | Location | Level | Content |
| --- | --- | --- | --- |
| Contract/unit | `unit/` | L0/L1 | 契约检查；`tc_frontend_test`（LEX/SYN、parse IR、位置/限制）；`tc_sem_test`（类型身份、布局、W 尺寸、冻结、模块顺序/实例）；`tc_ct_test`（CT 向量/缓存/局部 const）；`tc_ir_test`（最终 IR 形态/CFG/打印） |
| Conformance | `conformance/` | L3 | 静态 LEX/SYN/IMPORT/SEM/CFG-DI/调用/安全/注解/CT 用例、接受工具用例、属性用例；见 [conformance/README.md](conformance/README.md) |
| Harness | `harness/` | — | 元数据解析、7 判定器、失败关闭自测、`--syntax-only`/`--static-only` adapter |
| Traceability | `traceability/` | — | 80 码表、结构性覆盖矩阵、生成器检查 |
| Golden | `golden/` | L3 | 规范推导的期望值及规范来源 |
| Tools | `tools/` | L6 | `tc-vm`/`tc-aot` CLI 契约（用法、档案、部分检查、诊断、实现错误） |
| Determinism | `determinism/` | L7 | 重复运行、哈希种子/顺序扰动、逐字节一致的 harness JSON（S1/S2-S3/S4/S5） |
| Docs | `tools/docs_cleanup_check.py` | — | 文档收尾门禁：无过程/历史叙事、门禁记录简短、changelog 版本不重复 |

运行：

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

门禁使用 adapter 且不允许 `not_implemented`；缺失、跳过、超时、崩溃均为失败。
每次阶段/分支收尾还必须通过 `docs.cleanup`（见 `AGENTS.md` 的“收尾工作”）。
一致性用例按规范对象组织，期望必须引用标准条款，不得由编译器输出回填。
S1/S2/S3/S4/S5 用例表分别由 `tests/conformance/static/s1_cases.py`、
`s2s3_cases.py`、`s4_cases.py`、`s5_cases.py` 生成并 `--check`。
S4 接受用例使用 `--sem-only`（SEM 阶段的部分检查）；编译用例与 S5 接受用例使用
`--static-only`（含 CT 与 IR 最终化）。
32/64 结果分档案保存；差分测试（后续阶段）只在同一档案内比较 VM 与 AOT。
