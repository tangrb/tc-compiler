# 门禁状态（0.0.46）

本目录只保留每个已完成阶段的**当前门禁结论与可复现证据入口**。逐轮审计记录、缺陷修复过程、
历史 commit/环境快照不进入分支；需要追溯时使用 git 历史。

| 门禁 | 阶段 | 结论 | 记录 |
| ---- | ---- | ---- | ---- |
| G0 | S0 契约冻结与测试基建 | **PASS** | [G0](TC-G0门禁报告-0.0.46.md) |
| G1 | S1 LEX/SYN 与扁平 parse IR | **PASS** | [G1](TC-G1门禁报告-0.0.46.md) |
| G2 | S2 模块发现与 IMPORT | **PASS** | [G2](TC-G2门禁报告-0.0.46.md) |
| G3 | S3 名称、类型、转换与布局 | **PASS** | [G3](TC-G3门禁报告-0.0.46.md) |
| G4 | S4 CFG、DI、返回、调用、安全与注解 | **PASS** | [G4](TC-G4门禁报告-0.0.46.md) |
| G5 | S5 CT 与 IR 冻结 | **PASS** | [G5](TC-G5门禁报告-0.0.46.md) |
| G6 | S6 VM 参考实现（W=32/64） | **PASS** | [G6](TC-G6门禁报告-0.0.46.md) |
| G7 | S7 AArch64 子集后端、工具链与差分 | **PASS** | [G7](TC-G7门禁报告-0.0.46.md) |
| G8 | S8 全量语义、运行时与全量差分 | **PASS** | [G8](TC-G8门禁报告-0.0.46.md) |
| G9 | S9 freestanding/裸机与 32 位后端 | **PASS** | [G9](TC-G9门禁报告-0.0.46.md) |

**当前状态**：G9 通过，允许进入 S10。前端、CT/IR、运行时语义核、参考 VM、
W=64 AArch64 全量后端、W=32 ARM32 后端、freestanding 目标运行时核与
`aarch64-baremetal`/`arm32-baremetal` 平台实例、四设备策略、checked build、
C 调用者 AAPCS64/AAPCS32 互操作、全量差分/生成式对抗测试与 32/64 分档案
隔离均已实现。覆盖矩阵 636/636；design §12 十五项均有套件映射。

复现：

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/conformance/static/s1_cases.py --check
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/conformance/static/s4_cases.py --check
python3 tests/conformance/static/s5_cases.py --check
python3 tests/conformance/runtime/s6_cases.py --check
python3 tests/conformance/static/s6_static_cases.py --check
python3 tests/conformance/diff/s7_cases.py --check
python3 tests/conformance/diff/s7_fuzz.py --check
python3 tests/conformance/abi/s7_abi_cases.py --check
python3 tests/conformance/tools/s7_toolchain_cases.py --check
python3 tests/conformance/tools/s7_static_parity.py --check
python3 tests/determinism/s7_determinism.py --check
python3 tests/conformance/diff/s8_cases.py --check
python3 tests/conformance/abi/s8_abi_cases.py --check
python3 tests/conformance/abi/s8_interop_cases.py --check
python3 tests/conformance/diff/s8_abi_fuzz.py --check
python3 tests/conformance/diff/s8_layout_fuzz.py --check
python3 tests/conformance/diff/s8_mem_fuzz.py --check
python3 tests/conformance/diff/s8_fp_fuzz.py --check
python3 tests/conformance/checked/s8_checked_cases.py --check
python3 tests/conformance/consistency/s8_consistency_cases.py --check
python3 tests/conformance/tools/s8_artifact_cases.py --check
python3 tests/conformance/tools/s8_static_fuzz.py --check
python3 tests/determinism/s8_determinism.py --check
python3 tests/conformance/platform/s9_baremetal_cases.py --check
python3 tests/conformance/platform/s9_arm32_cases.py --check
python3 tests/conformance/abi/s9_arm32_interop.py --check
python3 tests/conformance/tools/s9_static_parity.py --check
python3 tests/conformance/platform/s9_profiles.py --check
python3 tests/determinism/s9_determinism.py --check
python3 tests/golden/validate.py
python3 tests/golden/run.py build/src/driver/tc-vm
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

收尾清理由 `docs.cleanup`（`tests/tools/docs_cleanup_check.py`）门禁检查；详细过程记录
保留在 git 历史中。

规范依据始终是 [标准](../TC语言及编译器标准-0.0.46.md)、
[设计](../TC原生编译器设计说明书-0.0.46.md) 与
[开发测试计划](../TC开发测试计划-0.0.46.md)；门禁记录不增加或放宽任何规范要求。
