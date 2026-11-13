# Changelog

## [0.0.46] - 2026-11-13

### Added

- **S0 contract freeze and test infrastructure (G0 PASS)**: type/IR/object/profile/diagnostic/zero-pass/build contracts and the private `tc_core` library.
- Machine-readable 80-code table and generated C table (65 `TC_CE_*` + 15 `TC_RE_*`), checked against the standard.
- First-diagnostic comparator/buffer, separate `TC_ERR_OUT_OF_MEMORY` channel, L0 contract tests, Python case harness with 7 deciders, traceability/coverage matrix, golden review/validation and CLI contract tests.
- `src/CONTRACTS.md` contract ownership index; G0 gate report.

### Changed

- Builds as C11; only `tc_core`, `tc-vm` and `tc-aot` are produced; no install rules or public ABI.
- `src/` reorganized into `base/`, `contract/`, `diagnostic/`, `ir/`, `runtime/`, `vm/`, `backend/` and `driver/`.
- Coverage is 634 slots / 231 items: 12 L0-covered, 3 N/A(S0) first-diagnostic rows (S3/S4), 619 uncovered.

## [0.0.46] - 2026-11-12

### Changed

- The branch is now **0.0.46 clean-sheet**: only the 0.0.46 standard, design document, and implementation skeleton remain.
- `src/` is rebuilt as the `frontend` / `ir` / `runtime` / `vm` / `backend` / `driver` skeleton from design §14; no implementation, no `tc-vm`/`tc-aot` targets.
- The root `CMakeLists.txt` is a skeleton configuration; `make`, `ctest`, VM/AOT test, and example flows are removed.
- README, AGENTS, CONTRIBUTING, SECURITY, and docs/README now describe the 0.0.46 docs/skeleton branch.
- The 32/64-bit target and `usize`/`isize` platform-word policy are recorded at the repository entry; the standard and design now define 32/64 word-size profiles (types, sizeof/bitcast/trunc, layout, diagnostics, and platform profiles).

### Removed

- Historical implementation, old tests, and old build configuration.
- Old C host public library/embedding API, embed mode, and slot bridge.
- Old design documents and old release checklist.
- `tests/`, `scripts/`, `examples/`, `tools/`, `.cursor/`, `.github/`, `Makefile`, and historical build directories.
- Old version strings, CLI goldens, error-code counts, and old implementation navigation.

### Kept

- `docs/TC语言及编译器标准-0.0.46.md` (80 codes; language/compiler rules).
- `docs/TC原生编译器设计说明书-0.0.46.md` (target architecture).
- `LICENSE`, `CODE_OF_CONDUCT*`, `CONTRIBUTING*`, `SECURITY*`.

The historical implementation can be recovered from git tags; this branch does not maintain it.
