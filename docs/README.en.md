# Documentation map

Formal design and specification documents for TC-Compiler. 中文：[README.md](README.md).

Start with the root [README.en.md](../README.en.md) and [`examples/`](../examples/), then dive into the docs below as needed.

## User docs

| Document | Purpose |
| -------- | ------- |
| [TC Language Specification 0.0.44](TC语言标准设计说明书-0.0.44.md) | **Sole authority** for syntax, semantics, and diagnostics |
| [TC-VM Command Reference 0.0.44](TC-VM命令行参考-0.0.44.md) | `tc-vm` usage, output, and exit behavior |
| [TC-Embed Design 0.0.44](TC-Embed详细设计说明书-0.0.44.md) | C↔TC embed API (includes caller-facing API) |
| [libtc Design 0.0.44](libtc设计说明书-0.0.44.md) | Embeddable library lifecycle and caller API quick reference (§15) |

## Contributor / implementer docs

| Document | Purpose |
| -------- | ------- |
| [TC Compiler Specification 0.0.44](TC编译器标准设计说明书-0.0.44.md) | 13-stage pipeline, diagnostic priority, call graph |
| [TC-VM Design 0.0.44](TC-VM详细设计说明书-0.0.44.md) | VM pipeline, IR, CFG, executor |
| [TC-AOT Design 0.0.44](TC-AOT详细设计说明书-0.0.44.md) | C99 codegen, runtime shim, differential verification |

## 0.0.45 (frozen; not yet in the 0.0.44 implementation)

| Document | Purpose |
| -------- | ------- |
| [TC language specification 0.0.45](TC语言标准设计说明书-0.0.45.md) | **0.0.45 frozen language standard**: `ref<T>` (managed reference) and `addr<T>` (linear address) spaces, public-IR text-form positioning |
| [TC compiler standard 0.0.45](TC编译器标准设计说明书-0.0.45.md) | **0.0.45 frozen compiler standard**: checks aligned with the language spec, typed intermediate form and lowering contract (§1.4), local checkability (§1.5), call-depth bound (§8.10) |

> The table above is the design source for a 0.0.45 language increment, **not** part of the shipping 0.0.44 language specification. Until it is implemented and merged into the language spec, the set of legal programs remains 0.0.44.
>
> **Frozen (2026-09-26)**: both 0.0.45 design documents moved from draft to frozen text (status line, scope of effect, change policy, and the compiler standard's lockstep-versioning clause); the anchor is the `spec-0.0.45` tag. After the freeze only errata are accepted and any semantic change requires a version bump; errata and cross-version deltas are recorded in `CHANGELOG.md`, never in the design documents themselves.

## Process records

This directory **no longer keeps** process documents (conformance audit reports, remediation ledgers, analysis reports, fix plans), so that the design/spec documents remain the single source of truth.

> The historical process documents (0.0.44 conformance audit report, 0.0.44 remediation ledger, 0.0.41 analysis report / fix plan, 0.0.42 debt-cleanup plan, and the 0.0.42 specification text) have been removed from the repository; retrieve them from release tags when needed, e.g. `git show v0.0.44:docs/TC-0.0.44-语言标准符合性审计报告.md`.
>
> Design/spec documents **must not** cite process records (enforced by `scripts/sync/check_doc_layering.py`, whose process-document name markers remain in effect).

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

Current core version: **v0.0.44** (the language specification and shipping design documents are **0.0.44**). The frozen 0.0.45 text is listed above and is not yet part of the shipping implementation.
