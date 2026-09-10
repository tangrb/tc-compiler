# Changelog

[中文](CHANGELOG.md)

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project roughly follows [Semantic Versioning](https://semver.org/) for toolchain releases (`vMAJOR.MINOR.PATCH` tags).

## [Unreleased]

### Changed

- **Language specification upgraded to 0.0.44** (an errata/convergence release: no new language capabilities, no change to how programs are written; the diagnostic-code set stays at **86** = 74 `TC_CE_*` + 12 `TC_RE_*`). Added `docs/TC语言标准设计说明书-0.0.44.md`, the companion revision/compatibility note (change log D1–D34), and the consistency errata checklist (63 first-round items plus 27 second-round items, all resolved). IEEE 754-2019 remains the only external normative reference.
- **All downstream design documents synced to 0.0.44**: compiler specification, VM design, VM command reference, AOT design, Embed design, and libtc design were renamed to `*-0.0.44.md`, with baseline lines, terminology ("restricted recovery" → "structural-class syntax-stage diagnostic"), and D1–D34 change points realigned; each document gained a "0.0.44 sync status" section (docs-first).
- `scripts/sync/check_doc_counts.py` now reads the 0.0.44 specification as its fact source, with fixed extraction logic (appendix B boundary; error-kind count taken from the `TcErrorKind` enum); the previously failing gate is green again.
- Version lines across metadata and navigation surfaces now point at 0.0.44: README (zh/en), `AGENTS.md`, `docs/README` (doc map), `CONTRIBUTING`, `examples/README`, `.cursor` rules and skills; the doc-filename check in `docs/release-checklist` was updated as well.
- Fixed cross-document code-count drift: compiler spec "appendix B 85 → **86**", four libtc places "85 language codes / total 86" → "**86 language codes / 87 implementation enum**", `platform.md` enum size 86 → **87**, and `errors.md`.

### Known issues

- **Implementation follow-ups** (spec and docs are ahead; tracked in errata checklist §9): pointer `cast` must drop the equal-width requirement (W-6; the implementation still rejects non-equal-width pointees), nested calls in `const_rhs` must report `TC_CE_SYNTAX` (W-26), and CT-category diagnostics must be reported after all SEM diagnostics (W-27). The toolchain behaviour version remains **v0.0.43**; this release changes no implementation behaviour.

## [0.0.43] - 2026-09-01

### Added

- Open-source repository scaffolding: contributing guides, code of conduct, security policy, issue/PR templates, Dependabot, examples, and documentation index.
- Bilingual surface docs (zh / en pairs with cross-links).
- Cursor Agent doc optimization: loading tiers, skill triggers, workflows, and deduplicated routing.

### Changed

- Toolchain and Embed implementation bumped to v0.0.43; language specification design docs remain 0.0.42 (no language semantics change).

## [0.0.42] - 2026-08-30

### Added

- `TC_CE_EXTRA_ARGUMENT` error code (N-13).
- Deterministic self-implemented float decimal output (FP-4.6).

### Fixed

- Endianness-independent `memblock` / `struct` layout (FP-4.5).
- Portable no-FENV underflow detection without `__int128` (N-12).
- Remaining N-12 portability debt (pointer offset, AOT alloc, `.count`).

### Changed

- Design docs and implementation version bumped to v0.0.42; 0.0.42 debt-cleanup closeout.

## [0.0.41] - 2026-08-30

### Added

- Qualified names required for imported structs.
- End-to-end support for struct `field_access` as an operand.

### Fixed

- Language-standard conformance gaps (P0–P6 closeout).
- Diamond import / topology, const composite, and `memcopy` index gaps.
- Const heap free path and const width callbacks.
- `Self.field` evaluation in static `let` / `var` initializers.
- Preserve struct `.count` base across `memblock_count` rewrite.

### Changed

- Design docs rebased on language-standard conformance; version bump to v0.0.41.

## [0.0.40] - 2026-08-24

### Changed

- Implementation and design docs bumped to v0.0.40.
- Cursor agent documentation overhaul and features map split.
- CI/ASan triggers for the `tc-0.0.40` branch.

## [0.0.39] - 2026-08-22

### Added

- Struct self-reference / pointer types and struct-form compliance work for 0.0.39.
- Session-scoped `-I` include path support (remaining 0.0.39 gaps).

### Fixed

- `memcopy_unsafe` operates on `ptr<T>` without memblock header offset.
- Block comments broken by documentation refresh.

### Changed

- Cross-platform CI/CD and test-port integration carried from the 0.0.38 line into the 0.0.39 release train.

[Unreleased]: https://github.com/tangrb/tc-compiler/compare/v0.0.43...HEAD
[0.0.43]: https://github.com/tangrb/tc-compiler/compare/v0.0.42...v0.0.43
[0.0.42]: https://github.com/tangrb/tc-compiler/compare/v0.0.41...v0.0.42
[0.0.41]: https://github.com/tangrb/tc-compiler/compare/v0.0.40...v0.0.41
[0.0.40]: https://github.com/tangrb/tc-compiler/compare/v0.0.39...v0.0.40
[0.0.39]: https://github.com/tangrb/tc-compiler/compare/v0.0.38...v0.0.39
