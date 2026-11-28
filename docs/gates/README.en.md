# Gate status (0.0.46 / 0.0.47)

This directory keeps only the current gate conclusion and reproducible evidence
entry points. Round-by-round audits, fix logs, historical commits and environment
snapshots stay in git history rather than branch documentation.

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
| G11 | 0.0.47 toolchain and project model | **PASS** | [G11](TC-G11门禁报告-0.0.47.md) |

## Current conclusion

- G0–G11 PASS. G10 completes the 0.0.46 stage exit and G11 completes the 0.0.47 stage exit.
- P0–P5, T0–T6 and P4b formal acceptance of 0.0.47 are complete; 99 CTests and 51 closing checks are registered.
- Fixed Linux CI real-gdb signed evidence across all four profiles passed the release audit.
  Local, SKIP or incomplete evidence cannot close G11.
- Current capabilities, implementation boundaries and full reproduction commands are in the
  [README](../../README.md), [AGENTS](../../AGENTS.md) and [tests/README](../../tests/README.md).

## 0.0.47 evidence entry points

| Scope | Current evidence |
| ---- | --------------- |
| P0–P3 versions, CLI, initialization, manifest and paths | `cli.contract`, `project.manifest`, `project.init`, `project.init_fault`, `project.paths_unit`, `project.paths`, `project.paths_aot`, `project.test_runner` |
| P4a / T2 line maps and source mapping | `linemap.unit`, `p4a.debug`, `t2.debug`, `t2.gdb` |
| T1 observation | `t1.observe` |
| T3 GDB stub | `gdb_journal.unit`, `gdb_rsp.unit`, `vm.gdb.protocol`, `vm.gdb.semantics`, `vm.gdb.e2e` |
| P4b / G11 debug ELF and signed evidence | `toolchain.debug_elf`, `toolchain.g11_evidence`, `release.s10_audit` |
| T4–T6 diagnostics, test infrastructure and reproducible release | `t4.diag`, `toolchain.diag_json`, `toolchain.presets`, `toolchain.reduce`, `release.t6_repro` |

The normative sources remain the [standard](../TC语言及编译器标准-0.0.46.md),
[design](../TC原生编译器设计说明书-0.0.46.md) and
[test plan](../TC开发测试计划-0.0.46.md); gate records never add or relax a requirement.
