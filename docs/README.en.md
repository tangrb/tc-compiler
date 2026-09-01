# Documentation map

Formal design and specification documents for TC-Compiler. 中文：[README.md](README.md).

Start with the root [README.en.md](../README.en.md) and [`examples/`](../examples/), then dive into the docs below as needed.

## User docs

| Document | Purpose |
| -------- | ------- |
| [TC Language Specification 0.0.42](TC语言标准设计说明书-0.0.42.md) | **Sole authority** for syntax, semantics, and diagnostics |
| [TC-VM Command Reference 0.0.42](TC-VM命令行参考-0.0.42.md) | `tc-vm` usage, output, and exit behavior |
| [TC-Embed Design 0.0.42](TC-Embed详细设计说明书-0.0.42.md) | C↔TC embed API (includes caller-facing API) |
| [libtc Design 0.0.42](libtc设计说明书-0.0.42.md) | Embeddable library lifecycle and caller API quick reference (§15) |

## Contributor / implementer docs

| Document | Purpose |
| -------- | ------- |
| [TC Compiler Specification 0.0.42](TC编译器标准设计说明书-0.0.42.md) | 13-stage pipeline, diagnostic priority, call graph |
| [TC-VM Design 0.0.42](TC-VM详细设计说明书-0.0.42.md) | VM pipeline, IR, CFG, executor |
| [TC-AOT Design 0.0.42](TC-AOT详细设计说明书-0.0.42.md) | C99 codegen, runtime shim, differential verification |

## Internal / historical

Conformance analysis and closeout plans — **not** day-to-day normative specs:

| Document | Purpose |
| -------- | ------- |
| [0.0.41 Conformance Analysis](TC-0.0.41-语言标准符合性检查分析报告.md) | 0.0.41 conformance analysis |
| [0.0.41 Fix Plan](TC-0.0.41-语言标准符合性修复计划.md) | 0.0.41 fix plan |
| [0.0.42 Debt-Cleanup Plan](TC-0.0.42-遗留问题清零计划.md) | 0.0.42 debt cleanup and closeout |

## Repository-level docs (root)

| Document | Purpose |
| -------- | ------- |
| [CHANGELOG.en.md](../CHANGELOG.en.md) ([中文](../CHANGELOG.md)) | User-visible changes |
| [CONTRIBUTING.en.md](../CONTRIBUTING.en.md) ([中文](../CONTRIBUTING.md)) | Contribution workflow and test gates |
| [CODE_OF_CONDUCT.en.md](../CODE_OF_CONDUCT.en.md) ([中文](../CODE_OF_CONDUCT.md)) | Community code of conduct |
| [SECURITY.en.md](../SECURITY.en.md) ([中文](../SECURITY.md)) | Vulnerability reporting |
| [release-checklist.en.md](release-checklist.en.md) ([中文](release-checklist.md)) | Release checklist |
| [AGENTS.md](../AGENTS.md) | Cursor Agent entry (optional for maintainers) |

Current core version: **v0.0.43** (language spec documents remain 0.0.42).
