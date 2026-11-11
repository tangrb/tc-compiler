# Documentation map

Formal design and specification documents for TC-Compiler. 中文：[README.md](README.md).

Start with the root [README.en.md](../README.en.md) and [`examples/`](../examples/), then dive into the docs below as needed.

## User docs

| Document | Purpose |
| -------- | ------- |
| [TC Language Standard 0.0.46](TC语言标准-0.0.46.md) | **Sole authority** for 0.0.46 syntax, semantics, and diagnostics (implementation baseline 0.0.44) |
| [TC-VM Command Reference 0.0.44](TC-VM命令行参考-0.0.44.md) | `tc-vm` usage, output, and exit behavior |
| [TC-Embed Design 0.0.44](TC-Embed详细设计说明书-0.0.44.md) | C↔TC embed API (includes caller-facing API) |
| [libtc Design 0.0.44](libtc设计说明书-0.0.44.md) | Embeddable library lifecycle and caller API quick reference (§15) |

## Contributor / implementer docs

| Document | Purpose |
| -------- | ------- |
| [TC Compiler Specification 0.0.46](TC编译器标准-0.0.46.md) | 0.0.46 compiler target specification: 13-step pipeline, diagnostic priority, complete diagnostic code tables, platform profile, implementation verification |
| [TC Language Specification 0.0.44](TC语言标准设计说明书-0.0.44.md) | 0.0.44 implementation-baseline language specification (historical baseline; the factual reference until 0.0.46 is implemented) |
| [TC Compiler Specification 0.0.44](TC编译器标准设计说明书-0.0.44.md) | 0.0.44 implementation baseline: 13-stage pipeline, diagnostic priority, call graph |
| [TC-VM Design 0.0.44](TC-VM详细设计说明书-0.0.44.md) | VM pipeline, IR, CFG, executor |
| [TC-AOT Design 0.0.44](TC-AOT详细设计说明书-0.0.44.md) | C99 codegen, runtime shim, differential verification |

## Repository-level docs (root)

| Document | Purpose |
| -------- | ------- |
| [README.en.md](../README.en.md) ([中文](../README.md)) | Getting started, tests and quality gates, **GitHub CI / coverage** |
| [CHANGELOG.en.md](../CHANGELOG.en.md) ([中文](../CHANGELOG.md)) | User-visible changes |
| [CONTRIBUTING.en.md](../CONTRIBUTING.en.md) ([中文](../CONTRIBUTING.md)) | Contribution workflow and test gates |
| [CODE_OF_CONDUCT.en.md](../CODE_OF_CONDUCT.en.md) ([中文](../CODE_OF_CONDUCT.md)) | Community code of conduct |
| [SECURITY.en.md](../SECURITY.en.md) ([中文](../SECURITY.md)) | Vulnerability reporting |
| [release-checklist.en.md](release-checklist.en.md) ([中文](release-checklist.md)) | Release checklist |
| [AGENTS.md](../AGENTS.md) | Cursor Agent entry (optional for maintainers) |

Current implementation version: **v0.0.44** (Phases 1–7 aligned with the 0.0.44 design documents). **0.0.45 was never released and is deprecated**. The 0.0.46 language standard is final and is the single source of truth: [TC语言标准-0.0.46.md](TC语言标准-0.0.46.md) (implementation not yet updated). The 0.0.46 compiler standard has been rewritten: [TC编译器标准-0.0.46.md](TC编译器标准-0.0.46.md) (§12 complete diagnostic code tables, §13 platform profile and implementation verification).
