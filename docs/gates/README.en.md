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

**Current status**: G8 passed and S9 is allowed. The front end, CT/IR, runtime
semantic core, reference VM, the full W=64 AArch64 backend (full ABI, float/
pointer/struct/indirect calls, checked build, ELF/ICF/region layout) and the full
differential suite are implemented; freestanding/bare metal, the device instance
and the 32-bit backend are not. Coverage is 636/636; every design §12 item has a suite
mapping (item 4 is evidenced by the freestanding compile/symbol audit).

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
python3 tests/conformance/checked/s8_checked_cases.py --check
python3 tests/conformance/consistency/s8_consistency_cases.py --check
python3 tests/conformance/tools/s8_artifact_cases.py --check
python3 tests/determinism/s8_determinism.py --check
python3 tests/golden/validate.py
python3 tests/golden/run.py build/src/driver/tc-vm
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

The normative sources remain the [standard](../TC语言及编译器标准-0.0.46.md),
[design](../TC原生编译器设计说明书-0.0.46.md) and
[test plan](../TC开发测试计划-0.0.46.md); gate records never add or relax a
requirement.
