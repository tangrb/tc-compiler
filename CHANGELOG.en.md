# Changelog

[中文](CHANGELOG.md)

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project roughly follows [Semantic Versioning](https://semver.org/) for toolchain releases (`vMAJOR.MINOR.PATCH` tags).

## [Unreleased]

### Added

- **0.0.45 design drafts (docs first, not implemented)**: added `docs/TC语言标准设计说明书-0.0.45.md` and `docs/TC编译器标准设计说明书-0.0.45.md`. Deltas vs 0.0.44: `ptr<T>` → `ref<T>` (managed reference), new `addr<T>` (linear address) and the `addr_*` family, `nullptr` split into two null literals: `none` (null reference, `ref<T>` only) and `nil` (null address, `addr<T>` only); `ptr_size(T, p)` → `bits_of(T)`; managed references lose arithmetic/ordering and integer round-trips; `memcopy_unsafe`, address difference, address ordering and region copy are no longer provided (language specification §1.4); null-value error codes split by address space: `TC_RE_NULL_REFERENCE_DEREFERENCE` / `TC_RE_NULL_ADDRESS_DEREFERENCE` / `TC_RE_NULL_ADDRESS_ARITHMETIC` (no shared null-pointer code); 86 diagnostic codes = 73 `TC_CE_*` + 13 `TC_RE_*`; the compiler standard gains the lowering contract (§1.4), local checkability (§1.5) and the call-depth bound (§8.10). The language specification carries present-tense clauses only; cross-version deltas live in this file.
- Removed historical process documents (0.0.44 conformance audit report and remediation ledger) and synced `docs/README`.

## [0.0.44] - 2026-09-21

### Changed

- GitHub Actions `ci.yml` / `asan.yml` now trigger on **`tc-0.0.44`** push/PR. Coverage collection goes through `scripts/lcov_compat.sh` for lcov 1.x, Ubuntu apt 2.0, and Homebrew 2.5 (`lcov` and `genhtml` probe `--ignore-errors` kinds separately). Root README, CONTRIBUTING, and `docs/README` document the CI branches, the lcov dependency, and the gitignored `build-coverage/` output.

- **Language specification upgraded to 0.0.44** (an errata/convergence release: no new language capabilities, no change to how programs are written; the diagnostic-code set stays at **86** = 74 `TC_CE_*` + 12 `TC_RE_*`). Added `docs/TC语言标准设计说明书-0.0.44.md`. IEEE 754-2019 remains the only external normative reference.
- **All downstream design documents synced to 0.0.44**: compiler specification, VM design, VM command reference, AOT design, Embed design, and libtc design were renamed to `*-0.0.44.md`, with baseline lines, terminology ("restricted recovery" → "structural-class syntax-stage diagnostic"), and D1–D35 change points realigned; each document gained a "0.0.44 sync status" section (docs-first).
- `scripts/sync/check_doc_counts.py` now reads the 0.0.44 specification as its fact source, with fixed extraction logic (appendix B boundary; error-kind count taken from the `TcErrorKind` enum); the previously failing gate is green again.
- Version lines across metadata and navigation surfaces now point at 0.0.44: README (zh/en), `AGENTS.md`, `docs/README` (doc map), `CONTRIBUTING`, `examples/README`, `.cursor` rules and skills; the doc-filename check in `docs/release-checklist` was updated as well.
- **Documentation cleanup**: removed the closed-out historical process documents (0.0.41 analysis report / fix plan, 0.0.42 debt-cleanup plan) and the 0.0.42 specification text; renamed the errata checklist to the version-less `TC-语言标准一致性勘误清单.md` (it now covers both the 0.0.42 first round and the 0.0.44 second round). Historical content can be recovered from release tags (e.g. `git show v0.0.42:docs/TC语言标准设计说明书-0.0.42.md`), as noted in `docs/README`; all 411 relative links across the repository were re-checked with zero dead links.
- **`Self.`-only module access inside `#lib` function bodies**: accessing a module-scope `static` from the same module inside a function must be written `Self.<name>`. The implementation previously classified only assignment/`read` targets as `TC_CE_FUNCTION_SCOPE_ACCESS`, degrading every other read position (arithmetic/comparison/logic/bitwise/shift, `return` operand, `if`/`while` condition, output operand) to `UNDEFINED_VARIABLE`, and silently accepted bare names as a `memblock` `N`/`count:`. This batch unifies the classification and adds parsing for `Self.<name> = v` and `Self.<name>.<field> = v` assignment targets (spec §6.2), `TcFieldAssign.base_binding` so VM and AOT share the base resolution (fixing `unresolved struct base`), and excludes `static` slots from a function's definite-initialization analysis (fixing a spurious uninitialized read of a `static var` field).
- **`static let` sources tightened (D35)**: a `static let` initializer may reference only literals and earlier, already-evaluated `let`/`static let` bindings — never a `static var`; violations report `TC_CE_CONSTANT_EXPRESSION` uniformly in every position (whole RHS, operand, field-read base, `.count` base, constructor field value, `cast`/`bitcast` source, `memblock`'s `N` and `count:`). The same batch fixes four related defects: the diagnostic for a whole-`Self.<name>` reference, `static let` declared-type named `N` never being resolved (`.count` silently read 0), constructor `count: <name>` evaluated before Pass2, and bare `usize_operand` names not falling back to the global table.
- **New language-spec conformance audit** (audit report: removed from the repository in the 0.0.45 cycle; retrievable from release tags): the six design documents and all of `src/` were checked against the language specification as sole authority. All 9 pre-existing open items were confirmed, plus 70 implementation-side, 48+ documentation-side and 4 specification-side new findings (including two process-level faults: out-of-bounds read/write via forged pointers, and stack overflow in nested type parsing). The audit changed no `src/`, tests, or existing documents.
- **Implementation fully synced to 0.0.44** (Batch S): (1) **acceptance set** — pointer `cast` is now a pointee re-marking without the equal-width requirement (D14); `bitcast(ptr ↔ float)` is rejected (D10); `Self.<name>` qualified identifiers are accepted as operands (D11); `if`/`while` conditions accept the full §6.1.1 RHS set (read-only field reads, `Self.static let`, pointer comparisons; D29); (2) **diagnostics** — nested calls in `const_rhs` are a syntax rejection (D34); CT-category diagnostics (`TC_CE_CONSTANT_*`) are now **held back** until every SEM-category diagnostic is clear (language spec §11 four-step rule / D19), and derived failures (depending on an already-failed constant) no longer surface as SEM codes; referencing a later-or-self static member in a `static var` initializer yields `TC_CE_UNDEFINED_VARIABLE` (D28); (3) **semantics** — `ptr_load(bool)` normalizes `0x00` → `false`, any other byte → `true` (D16, **fixing a VM/AOT divergence**); `static var` initializers may reference an earlier `static var` and report `TC_RE_*` on preparation failure (D16); the static-boolean three-state atom set is extended to the §5.2.1 atom expressions (D29); (4) **corpus and gates** — 24 VM/AOT corpus entries added or corrected and 26 unit assertions updated, `test-map.md` counts refreshed to 966 VM / 443 AOT, with VM, AOT, unit and all three sync gates green.
- Fixed cross-document code-count drift: compiler spec "appendix B 85 → **86**", four libtc places "85 language codes / total 86" → "**86 language codes / 87 implementation enum**", `platform.md` enum size 86 → **87**, and `errors.md`.
- **Toolchain implementation bumped to v0.0.44** (`TC_VERSION_CORE` / `TC_VERSION_EMBED`); CLI `--version` goldens now expect `tc-vm 0.0.44` / `tc-aot 0.0.44`.

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

[Unreleased]: https://github.com/tangrb/tc-compiler/compare/v0.0.44...HEAD
[0.0.44]: https://github.com/tangrb/tc-compiler/compare/v0.0.43...v0.0.44
[0.0.43]: https://github.com/tangrb/tc-compiler/compare/v0.0.42...v0.0.43
[0.0.42]: https://github.com/tangrb/tc-compiler/compare/v0.0.41...v0.0.42
[0.0.41]: https://github.com/tangrb/tc-compiler/compare/v0.0.40...v0.0.41
[0.0.40]: https://github.com/tangrb/tc-compiler/compare/v0.0.39...v0.0.40
[0.0.39]: https://github.com/tangrb/tc-compiler/compare/v0.0.38...v0.0.39
