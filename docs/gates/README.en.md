# Gate status (0.0.46 / 0.0.47)

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
| G11 | 0.0.47 toolchain and project model | **Incomplete** | Current status on this page |

**Current status**: G10 passed and the 0.0.46 stage exit is complete. P0/T0, P1,
P2, P3, P4a, T1, T2 and T3 of `tc-0.0.47` are complete: split versions, three private entry
points, the shared implementation-error exit mapping, the `tc` CLI contract,
release audit, full `tc --init-project` templates with atomic writes/rollback/
identity checks, `tc.project` v1 parsing with nearest-ancestor root discovery,
and compiler consumption of manifest/include/profile data. Canonical and
project-root-relative display paths are separate; diagnostics, IR metadata, VM
traps and AOT source strings use display paths. Module identity uses bound parent
directories and actual entry names, and source reads are nonblocking and
descriptor-bound. `tc --test` implements one-level discovery/filtering, fd-anchored
beneath resolution, strict sidecars, private read-only snapshots, fork workers,
timeouts, stream capture, structured results, reports and exit mapping; evidence
entry points are `tests/project/test_runner_test.py` and `project.test_runner`.
P4a adds the shared deterministic line map, private AOT `.loc`/source comments
and a minimal DWARF v4 CU with clang-generated `.debug_line`, plus stdio/project-
input/output-reservation guards before diagnostics or artifacts; evidence entry
points are `linemap.unit`, `tests/conformance/tools/p4a_debug_cases.py` and
`p4a.debug`. T1 adds VM/AOT `--dump-ir`, VM `--trace=ir,call,mem,io,trap` with
text/jsonl, limits and output selection, VM/AOT `--stats`, structured trap context,
explicit output identity/reservation guards and deterministic observation output;
its evidence entry point is `t1.observe`. T2 completes VM/AOT
`--dump-linemap[=<path>]` and public `tc-aot --debug`: every IR instruction uses
one final-IR line-map entry for JSON v3, synthetic VM addresses, `.loc`/source
comments and the DWARF v4 CU. Real gdb through the qemu gdbstub verifies
`info line`, source breakpoints and single-step; evidence entry points are
`t2.debug` and `t2.gdb`. T3 completes the VM GDB stub: `tc-vm --gdb[=<port>]`
defaults to port 0 and binds loopback only, with mandatory
`--gdb-stdout-journal=<dir>`. It supports synthetic IR addresses,
`target.xml`/memory map, breakpoints/step/continue, monitor, extended restart,
an atomic stdout journal and resumable stdin. The default is read-only;
`--gdb-allow-write` enables only restricted writes. Evidence entry points are
`gdb_journal.unit`, `gdb_rsp.unit`, `vm.gdb.protocol` (242 checks),
`vm.gdb.semantics` and `vm.gdb.e2e`; the first four are fast, while e2e is not.
P4b completes the private VM debug ELF writer and local real-gdb validation across
all four profiles: `tc-vm --gdb-elf-out=<path>` reuses the shared line map and
synthetic addresses, and `toolchain.debug_elf` verifies debug ELF, source breakpoints,
64-bit `tc_ir_pc` reads, IR single-step and program-semantic regressions. Synthetic
`tc_ir_pc` is a 64-bit `uint64`, architectural PCs remain `code_ptr` (64-bit on
AArch64, 32-bit on ARM32/RISC-V32), and ARM/RISC-V Z0 kind=2 is accepted. ARM clients need `set arm force-mode thumb`;
GDB 18.1 ARM `stepi` cannot be used directly because sp is not modelled, so tests
verify the same RSP step with maintenance packet `s` and a register-cache flush.
G11 evidence collection/validation and a manual signing job are wired. A real signed
artifact from fixed Linux CI remains pending, so P4b formal acceptance and G11 are
incomplete; P5 and T4–T6 have not started.
There are 92 registered 0.0.47 CTests; the P4b/G11 infrastructure checkpoint passed
Debug, Release and ASan+UBSan full CTest runs at 92/92, and closing checks passed 51/51.
The ARM32 blocker fix passed full CTest 92/92 and closing checks 51/51 in the current
Debug tree, plus 15 real-gdb e2e checks each for ARM32 and RISC-V32.
macOS does not support ASan leak detection, so the
sanitizer run used `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. The accepted 0.0.46 harness remains 873/873.
The front end, CT/IR, runtime semantic
core, reference VM, the full W=64 AArch64 backend, the W=32 ARM32 backend, the
W=32 RISC-V `riscv32-baremetal` backend, the freestanding target runtime core
with the `aarch64-baremetal`/`arm32-baremetal`/`riscv32-baremetal` platform
instances, all four device policies, checked build, bidirectional
C-caller/TC-caller AAPCS64/AAPCS32/RISC-V ILP32 interoperability, the full
differential and generated ABI adversarial suites, implementation-limit/scale/
far-branch regressions, reset-entry BSS clearing, 32/64 profile isolation, the
long-term regression corpus and the release audit are implemented. Coverage is
636/636; every design §12 item has a suite mapping.

Reproduce:

```sh
cmake -S . -B .tmp/build
cmake --build .tmp/build
ctest --test-dir .tmp/build --output-on-failure
ctest --test-dir .tmp/build -R '^project\.|^linemap\.|^p4a\.|^toolchain\.(debug_elf|g11_evidence)|^t1\.|^t2\.|^gdb_|^vm\.gdb\.' --output-on-failure
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
python3 tests/golden/run.py .tmp/build/src/driver/tc-vm
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

The normative sources remain the [standard](../TC语言及编译器标准-0.0.46.md),
[design](../TC原生编译器设计说明书-0.0.46.md) and
[test plan](../TC开发测试计划-0.0.46.md); gate records never add or relax a
requirement.
