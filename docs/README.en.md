# Documentation map

Formal design and specification documents for TC-Compiler. 中文：[README.md](README.md).

Start with the root [README.en.md](../README.en.md) and [`examples/`](../examples/), then dive into the docs below as needed.

## User docs

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](TC语言及编译器标准-0.0.46.md) | **Sole authority** for 0.0.46 syntax, semantics, diagnostics, and compiler implementation rules (topic chapters §0–§14: language rules and compiler implementation requirements; implementation baseline 0.0.44) |
| [TC-VM Command Reference 0.0.44](TC-VM命令行参考-0.0.44.md) | `tc-vm` usage, output, and exit behavior |
| [libtc Design 0.0.44](libtc设计说明书-0.0.44.md) | Embeddable library lifecycle and caller API quick reference (§15) |

## Contributor / implementer docs

| Document | Purpose |
| -------- | ------- |
| [TC Native Compiler Design 0.0.46](TC原生编译器设计说明书-0.0.46.md) | **Target architecture design (not yet implemented)** for the TC → assembly → machine-code native compiler; takes the 0.0.46 standard as sole input and specifies the flat TAC IR / semantic runtime / platform profile / AArch64 backend / phasing. Does not describe the current v0.0.44 implementation |

Compiler implementation rules (13-step pipeline, diagnostic priority, platform profile) are in §1.7–§1.9, §12.2/§12.5, and §13 of the standard. For implementation code navigation, see [.cursor/README.md](../.cursor/README.md) (knowledge graph `kg-*.md`, `gotchas.md`).

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

Current implementation version: **v0.0.44** (Phases 1–7 aligned with the 0.0.44 design documents). **0.0.45 was never released and is deprecated**. The 0.0.46 standard is final and is the single source of truth: [TC语言及编译器标准-0.0.46.md](TC语言及编译器标准-0.0.46.md) (topic chapters §0–§14, including the §12.3 diagnostic code table and §13 platform profile; implementation not yet updated).
