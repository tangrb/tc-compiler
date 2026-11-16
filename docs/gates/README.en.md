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

**Current status**: G6 passed and S7 is allowed. The front end, CT/IR, runtime
semantic core and reference VM are implemented; the native backend and the
freestanding/ABI/toolchain stages are not. Coverage is 626/636; the 10 remaining
rows are design §12 items owned by S7–S9 (differential/ABI/freestanding/
`@fast_math`).

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
python3 tests/golden/validate.py
python3 tests/golden/run.py build/src/driver/tc-vm
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

The normative sources remain the [standard](../TC语言及编译器标准-0.0.46.md),
[design](../TC原生编译器设计说明书-0.0.46.md) and
[test plan](../TC开发测试计划-0.0.46.md); gate records never add or relax a
requirement.
