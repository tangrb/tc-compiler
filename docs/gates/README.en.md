# Gate status (0.0.46)

This directory keeps only the current gate conclusion and reproducible evidence
entry points. Round-by-round audits, defect-fix logs and historical commit or
environment snapshots are not kept on the branch; use git history if needed.

| Gate | Stage | Result | Record |
| ---- | ----- | ------ | ------ |
| G0 | S0 contracts and test infrastructure | **PASS** | [G0](TC-G0门禁报告-0.0.46.md) |
| G1 | S1 LEX/SYN and flat parse IR | **PASS** | [G1](TC-G1门禁报告-0.0.46.md) |
| G2 | S2 module discovery and IMPORT | **PASS** | [G2](TC-G2门禁报告-0.0.46.md) |
| G3 | S3 names, types, conversions and layout | **PASS** | [G3](TC-G3门禁报告-0.0.46.md) |
| G4 | S4 CFG, DI, returns, calls, safety and annotations | **PASS** | [G4](TC-G4门禁报告-0.0.46.md) |
| G5 | S5 CT and IR freeze | **PASS** | [G5](TC-G5门禁报告-0.0.46.md) |
| G6 | S6 reference VM (W=32/64) | **PASS** | [G6](TC-G6门禁报告-0.0.46.md) |
| G7 | S7 AArch64 subset backend, toolchain and differential | **PASS** | [G7](TC-G7门禁报告-0.0.46.md) |
| G8 | S8 full semantics, runtime and full differential | **PASS** | [G8](TC-G8门禁报告-0.0.46.md) |
| G9 | S9 freestanding/bare metal and 32-bit backend | **PASS** | [G9](TC-G9门禁报告-0.0.46.md) |
| G10 | S10 multi-target expansion and long-term regression | **PASS** | [G10](TC-G10门禁报告-0.0.46.md) |

**Current status**: G10 passed and the 0.0.46 stage exit is complete; full
`ctest` is 71/71. The front end, CT/IR, runtime semantic core, reference VM, the
full W=64 AArch64 backend, the W=32 ARM32 backend, the W=32 RISC-V
`riscv32-baremetal` backend, the freestanding target runtime core with the
`aarch64-baremetal`/`arm32-baremetal`/`riscv32-baremetal` platform instances,
all four device policies, checked build, bidirectional C-caller/TC-caller
AAPCS64/AAPCS32/RISC-V ILP32 interoperability, the full differential and
generated ABI adversarial suites, implementation-limit/scale/far-branch
regressions, reset-entry BSS clearing, 32/64 profile isolation, the long-term
regression corpus and the release audit are implemented. Coverage is 636/636;
every design §12 item has a suite mapping.

Reproduce:

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

The normative sources remain the [standard](../TC语言及编译器标准-0.0.46.md),
[design](../TC原生编译器设计说明书-0.0.46.md) and
[test plan](../TC开发测试计划-0.0.46.md); gate records never add or relax a
requirement.
