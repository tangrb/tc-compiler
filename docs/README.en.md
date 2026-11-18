# Documentation map

This directory keeps the 0.0.46 standard, design, development test plan and
current gate status. Chinese: [README.md](README.md).

## Core documents

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](TC语言及编译器标准-0.0.46.md) | Sole authority for syntax, semantics, diagnostics, compiler requirements and platform profiles (§0–§14; 80 codes) |
| [TC Native Compiler Design 0.0.46](TC原生编译器设计说明书-0.0.46.md) | Target architecture: flat TAC IR, semantic runtime, platform profiles, backend and phasing |
| [TC Development Test Plan 0.0.46](TC开发测试计划-0.0.46.md) | S0–S10 stages, gates, code/opcode/chapter coverage and traceability |
| [Gate status](gates/README.en.md) | Current conclusions, evidence entry points and later-stage boundaries |
| [ABI rule matrix](../tests/conformance/abi/README.md) | AAPCS64/AAPCS32/RISC-V ILP32 reference editions, bidirectional C/TC interop, rule coverage and explicit scope limits |

## Current status

- S0–S10 are implemented and G0–G10 passed; **G10 completes the 0.0.46 stage
  exit**; full `ctest` 71/71 and coverage matrix 636/636.
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
- No C host public library, embedding API or ABI.
- Both 32/64 are targets; `usize`/`isize` are platform-word types (§13.2.1/§13.2.2).

## Repository documents

| Document | Purpose |
| -------- | ------- |
| [README.en.md](../README.en.md) | Project entry, build and current status |
| [CHANGELOG.en.md](../CHANGELOG.en.md) | 0.0.46 changes |
| [CONTRIBUTING.en.md](../CONTRIBUTING.en.md) | Contribution workflow |
| [CODE_OF_CONDUCT.en.md](../CODE_OF_CONDUCT.en.md) | Code of conduct |
| [SECURITY.en.md](../SECURITY.en.md) | Security reporting |
| [AGENTS.md](../AGENTS.md) | Agent guide |

Historical implementation and old design documents live only in git tags.
