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

## Current status

- S0–S5 are implemented and passed G0–G5; **G5 allows S6**.
- Not implemented: the VM, runtime semantics and backends.
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
