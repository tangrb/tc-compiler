# 门禁状态（0.0.46 / 0.0.47）

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
| G10 | S10 多目标扩展与长期回归 | **PASS** | [G10](TC-G10门禁报告-0.0.46.md) |
| G11 | 0.0.47 工具链与工程模型 | **未完成** | 本页当前状态 |

**当前状态**：G10 通过，0.0.46 阶段出口完成；`tc-0.0.47` 的 P0/T0、P1、P2、P3 与 P4a
已完成：版本拆分、三个私有入口、共享实现级退出码映射、`tc` CLI 契约、发布审计、
`tc --init-project` 完整模板/原子写入/回滚/身份检查、`tc.project` v1 解析与最近根
发现，以及 manifest/include/profile 的编译器消费。canonical 路径与工程根相对显示
路径已分离，诊断、IR、VM trap 与 AOT 源字符串统一使用显示路径；模块身份使用
绑定父目录与实际目录项名，源读取采用非阻塞有界 fd。`tc --test` 已完成一层发现/
筛选、fd 锚定 beneath 解析、严格侧车、私有只读快照、fork worker、超时、流捕获、
结构化结果、报告与退出映射，证据入口为 `tests/project/test_runner_test.py` 与
`project.test_runner`。P4a 已完成共享确定性行表、私有 AOT `.loc`/源注释与最小
DWARF v4 CU（clang 生成 `.debug_line`），并在发布诊断/产物前保护 stdio、项目输入
与输出保留身份；证据入口为 `linemap.unit`、`tests/conformance/tools/p4a_debug_cases.py`
与 `p4a.debug`。公开 `--debug`/`--dump-linemap`、P4b、P5 与 T1–T6 尚未开始，G11 未完成。
0.0.47 CTest 注册总数 82；Debug、Release 与 ASan+UBSan 全量 CTest 均为 82/82，
50 项收尾检查全部通过。0.0.46 已验收 harness 873/873。
前端、CT/IR、运行时语义核、参考 VM、W=64 AArch64 全量后端、W=32 ARM32 后端、
W=32 RISC-V `riscv32-baremetal` 后端、freestanding 目标运行时核与三个裸机平台实例、
四设备策略、checked build、C caller/TC caller 双向 AAPCS64/AAPCS32/RISC-V ILP32
互操作、全量差分/生成式 ABI 对抗测试、实现边界/规模/长跳转回归、复位入口 BSS
清零、32/64 分档案隔离、长期回归语料与发布审计均已实现。覆盖矩阵 636/636；
design §12 十五项均有套件映射。

复现：

```sh
cmake -S . -B .tmp/build
cmake --build .tmp/build
ctest --test-dir .tmp/build --output-on-failure
ctest --test-dir .tmp/build -R '^project\.|^linemap\.|^p4a\.' --output-on-failure
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
python3 tests/conformance/abi/reverse_interop_cases.py --check
python3 tests/tools/abi_mutation_audit.py --check
python3 tests/tools/s9_capacity_report.py --check
python3 tests/tools/s9_softfloat_audit.py --check
python3 tests/conformance/tools/s9_static_parity.py --check
python3 tests/conformance/platform/s9_profiles.py --check
python3 tests/determinism/s9_determinism.py --check
python3 tests/conformance/platform/s10_riscv32_cases.py --check
python3 tests/conformance/platform/s10_limits_cases.py --check
python3 tests/conformance/abi/s10_riscv32_interop.py --check
python3 tests/conformance/abi/s10_reverse_interop.py --check
python3 tests/conformance/abi/s10_abi_fuzz.py --check --seeds 8
python3 tests/conformance/tools/s10_static_parity.py --check
python3 tests/conformance/tools/s8_static_fuzz.py --check --profile=riscv32-baremetal
python3 tests/conformance/tools/s10_frontend_scale.py --check
python3 tests/conformance/checked/s10_checked_cases.py --check
python3 tests/conformance/platform/s10_long_branch.py --check
python3 tests/conformance/longterm/s10_longterm.py --check
python3 tests/determinism/s10_determinism.py --check
python3 tests/tools/s10_release_audit.py --check
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
