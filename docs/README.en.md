# Documentation map

This directory keeps the 0.0.46 standard, design, development test plan, the
0.0.47 toolchain and project model, and current gate status.
Chinese: [README.md](README.md).

## Core documents

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](TC语言及编译器标准-0.0.46.md) | Sole authority for syntax, semantics, diagnostics, compiler requirements and platform profiles (§0–§14; 80 codes) |
| [TC Native Compiler Design 0.0.46](TC原生编译器设计说明书-0.0.46.md) | Target architecture: flat TAC IR, semantic runtime, platform profiles, backend and phasing |
| [TC Development Test Plan 0.0.46](TC开发测试计划-0.0.46.md) | S0–S10 stages, gates, code/opcode/chapter coverage and traceability |
| [TC Development and Debugging Toolchain 0.0.47](TC开发调试工具链-0.0.47.md) | 0.0.47 toolchain scope, interface contracts, milestones and acceptance; language semantics remain 0.0.46 |
| [TC Project Model 0.0.47](TC工程模型-0.0.47.md) | 0.0.47 project layout, `tc.project`, `tc` init/test, root anchor and path rules; P0–P4a implemented |
| [Gate status](gates/README.en.md) | Current conclusions, evidence entry points and later-stage boundaries |
| [ABI rule matrix](../tests/conformance/abi/README.md) | AAPCS64/AAPCS32/RISC-V ILP32 reference editions, bidirectional C/TC interop, rule coverage and explicit scope limits |

## Current status

- The 0.0.47 toolchain and project-model designs meet the freeze criteria.
  P0/T0, P1, P2, P3, P4a and T1 are complete. `tc --init-project` implements the full
  template, atomic writes, rollback and identity checks. The compiler consumes
  `tc.project`, canonical roots, ordered include roots and the manifest default
  profile, while keeping canonical paths separate from project-root-relative
  display paths. Diagnostics, IR metadata, VM traps and AOT source strings use
  display paths; module identity uses bound parent directories and actual entry
  names, and source reads are nonblocking and descriptor-bound. `tc --test`
  implements one-level discovery/filtering, fd-anchored beneath resolution,
  strict sidecars, private read-only snapshots, fork workers, timeouts, stream
  capture, structured results, reports and exit mapping. P4a adds the shared
  deterministic line map plus private AOT `.loc`/source comments, a minimal
  DWARF v4 CU and clang-generated `.debug_line`. T1 adds VM/AOT `--dump-ir`, VM
  `--trace=ir,call,mem,io,trap` with text/jsonl, limits and output selection,
  VM/AOT `--stats`, structured trap context, explicit output identity/reservation
  guards and deterministic observation output; its evidence entry point is
  `t1.observe`. Public `tc-aot --debug`/`--dump-linemap`, P4b, P5 and T2–T6 have not
  started, and G11 is incomplete. Language/contract versions remain 0.0.46/1;
  RTOS, interrupts and concurrency move to 0.0.48.
- S0–S10 are implemented and G0–G10 passed; **G10 completes the 0.0.46 stage
  exit**. There are 83 registered 0.0.47 CTests (73 at P0, three P1 tests, three
  P2 tests, one P3 test, two P4a tests and one T1 test). T1 implementation and
  focused tests are complete; Debug, Release and ASan+UBSan full CTest runs each
  passed 83/83, and all closing checks passed. The 0.0.46 coverage
  matrix is 636/636.
- Implemented: the front end, CT/IR, the runtime semantic core, the reference VM
  (32/64 profiles), the full W=64 AArch64, W=32 ARM32 and W=32 RISC-V
  backends/toolchains (full ABI, checked build, ELF/region layout, reset-entry
  BSS clearing, far-branch long jumps), the freestanding target runtime core
  with the `aarch64-baremetal`/`arm32-baremetal`/`riscv32-baremetal` platform
  instances, all four device policies, 32/64 profile isolation, the full
  differential/ABI/consistency/artifact/determinism suites, generated ABI fuzz,
  implementation-limit/checked/scale/far-branch regressions, the S10 long-term
  regression corpus and the release audit.
- Not implemented: the optional optimization pipeline (SSA/regalloc/inlining).
- No C host public library, embedding API or ABI; the only private entry points
  are `tc`/`tc-vm`/`tc-aot`.
- Both 32/64 are targets; `usize`/`isize` are platform-word types (§13.2.1/§13.2.2).

## Repository documents

| Document | Purpose |
| -------- | ------- |
| [README.en.md](../README.en.md) | Project entry, build and current status |
| [CHANGELOG.en.md](../CHANGELOG.en.md) | 0.0.46 implementation and current 0.0.47 delivery status |
| [CONTRIBUTING.en.md](../CONTRIBUTING.en.md) | Contribution workflow |
| [CODE_OF_CONDUCT.en.md](../CODE_OF_CONDUCT.en.md) | Code of conduct |
| [SECURITY.en.md](../SECURITY.en.md) | Security reporting |
| [AGENTS.md](../AGENTS.md) | Agent guide |

Historical implementation and old design documents live only in git tags.
