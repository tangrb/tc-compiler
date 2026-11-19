# TC 0.0.46 S0 门禁记录（G0）

**结论：G0 PASS，允许进入 S1。**

## 范围

S0 冻结跨模块契约与测试基建：类型/IR/对象与区/平台档案/诊断/零 pass 契约；私有
`tc_core` 与 `tc-vm`/`tc-aot` 骨架；L0/L1 测试框架、harness、码表与覆盖矩阵骨架。

## 交付与验证

| 项 | 证据 |
| ---- | ---- |
| 类型/IR/对象/档案契约 | `src/ir/`、`src/runtime/`、`src/CONTRACTS.md`；L0 `TC-L0-TYPE-0001`、`TC-L0-IR-0001`、`TC-L0-OBJ-0001` |
| 80 码闭集与首诊 | `src/diagnostic/`；`trace.codes`、`trace.gen_c_table`、`trace.harness_codes` |
| 零 pass 与私有构建 | `src/contract/optimizer.*`；无 install 规则、无公共 ABI |
| 测试基建 | `tests/harness/`、`tests/traceability/`、`tests/golden/`；失败关闭自测 |
| 构建/测试 | Debug、Release、ASan+UBSan `ctest` 通过；`leaks` 0；`nm` 仅 `_main` |

复现：`ctest --test-dir build --output-on-failure`、`python3 tests/traceability/coverage.py --check`。
