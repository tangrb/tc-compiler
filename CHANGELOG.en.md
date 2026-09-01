# Changelog

[中文](CHANGELOG.md)

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project roughly follows [Semantic Versioning](https://semver.org/) for toolchain releases (`vMAJOR.MINOR.PATCH` tags).

## [Unreleased]

### Added

- Open-source repository scaffolding: contributing guides, code of conduct, security policy, issue/PR templates, examples, and documentation index.
- Bilingual surface docs (zh / en pairs with cross-links).

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

[Unreleased]: https://github.com/tangrb/tc-compiler/compare/v0.0.42...HEAD
[0.0.42]: https://github.com/tangrb/tc-compiler/compare/v0.0.41...v0.0.42
[0.0.41]: https://github.com/tangrb/tc-compiler/compare/v0.0.40...v0.0.41
[0.0.40]: https://github.com/tangrb/tc-compiler/compare/v0.0.39...v0.0.40
[0.0.39]: https://github.com/tangrb/tc-compiler/compare/v0.0.38...v0.0.39
