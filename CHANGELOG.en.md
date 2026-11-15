# Changelog

## [0.0.46] - 2026-11-15

### Added

- **S2 module discovery and IMPORT**: `src/frontend/module.{h,c}` add root `#program` validation, import-source-order DFS, deterministic importer-directory-then-include-roots search, resolved-path instance keys, a fixed `module_order`, and buffered missing/duplicate/circular/non-library/name-conflict IMPORT diagnostics.  A `#program` import target is LEX/SYN-parsed but its imports are not recursed.
- **S3 names, types, conversions and layout**: `src/frontend/sem.{h,c}` add module member/namespace and visibility rules, `Self`/`m`/bare-name resolution, type-table interning and post-Analyze freeze, scalar/ptr/fnptr/struct equivalence, W-dependent literal category/suffix/sign/range checks, `cast`/`trunc`/`bitcast` category-before-width rules, struct fields/`@padding`/layout/`LAYOUT_OVERFLOW`, `sizeof` type arguments, field-assignment paths and `readonly`, `make`/call argument shape and types, mode-matrix and `.nsw/.nuw` `@fast` constraints, and W range checks for `@align`/`@range`.
- `tc-vm`/`tc-aot` gain `--static-only` (LEX/SYN/IMPORT/SEM; exit 0 still does not claim a valid program) and a `w32` static profile (`tc_profile_init_generic_w32`) for the 32/64 matrix; it is not bound to an executable target.
- Tests: `tc_sem_test` (58 L1 checks: type identity, layout/offsets, W sizes, type-table freeze, diamond `module_order`, same-name/different-path instances); `tests/conformance/static/s2s3_cases.py` generates 126 SEM/flip, 28 import/library-fixture and 29 acceptance tool cases; `determinism.s2s3`; the adapter supports `--static-only`, imported-module positions, related notes and include roots.
- G2/G3 gate reports; coverage is now 245 covered / 389 uncovered / 0 suspended (`flip32_64` 34/34, `first_diag_rule` 10/10).

### Changed

- A normal invocation still exits 70 after the implemented static stages; CT/CFG/DI/safety/annotations/VM/backend are not implemented and are not described as supported.
- Compile conformance cases use the full static pipeline so a LEX/SYN error still outranks an earlier IMPORT error; acceptance tool cases keep `--syntax-only`/`--static-only` explicit.
- Final `TcInstr`/`TcIR` materialization remains part of IR finalization; SEM resolves/validates and freezes the type table.

### Fixed

- Fixed a `TYPE_MISMATCH` cascade after an unknown field, private same-module/#program struct access, and the missing signed upper-bound check for unsuffixed non-negative literals.
- Fixed module-instance duplication from a non-canonical root path, `Self.f`/`m.f` value-position diagnostics, `.nsw/.nuw` suffix positions, and padding radix/suffix/sign checks.

- **S1 LEX/SYN and flat parse IR**: `src/frontend/` now provides `input` (arena/source/documentated limits), `num` (exact decimal -> binary32/64 with roundTiesToEven and integer arithmetic only), `lex` (UTF-8 decoding, BOM/U+0000/non-ASCII rejection, `///` comments, `\` continuations, longest-match tokens, mnemonics/suffixes, annotations, DISCARD, literals, line/column spans) and `parse` (§14 EBNF, module pattern/layers/visibility, blocks, all declarations/statements/types/instruction forms, field paths, named args, `switch` targets, unsafe depth).
- Trigger/no-false-positive/position/stage coverage for the seven LEX/SYN codes (`TC_CE_SYNTAX`, `TC_CE_MISSING_END`, `TC_CE_MODULE_LAYER`, `TC_CE_MISSING_VISIBILITY`, `TC_CE_PROGRAM_MODE_MISUSE`, `TC_CE_VAR_MISSING_INIT`, `TC_CE_LITERAL_LEX_RANGE`); `TC_CE_SYNTAX` keeps LEX and SYN stages distinct.
- `--syntax-only` partial check in `tc-vm`/`tc-aot` (exit 0 = no LEX/SYN diagnostic, not a valid-program claim); a normal invocation still exits 70 after LEX/SYN. Stable diagnostic line `<file>:<line>:<column>: error[<STAGE>]: <CODE>: <name>`; I/O and resource failures use `tc_error_io`/`tc_error_internal`/`TC_ERR_OUT_OF_MEMORY`, never language codes.
- Tests: `tc_frontend_test` (688 L1 checks: all 51 opcodes/suffixes, parse IR shape, UTF-8 prefix/offset, first-diagnostic stage/source order, documented limits, repeated-parse determinism); `tests/conformance/` (96 LEX/SYN diagnostics + 10 acceptance + 1 random-byte property case, generated/checked by `s1_cases.py`); `tests/determinism/` (repeat runs, hash seeds, argument order); CMake-configured S1 harness adapter, `adapter.selftest` (fake-tool fail-closed checks), and the `harness.conformance`/`determinism.s1` gates.
- Seven specification-derived S1 goldens (Unicode column, negative literal lexical range, earliest missing `end`, module layer order, DISCARD field name, `static var` missing initializer, `Self` in `#program`).

### Changed

- LEX/SYN implemented inside the private `tc_core` (`frontend/`); still no install, public API or ABI; only `tc-vm`/`tc-aot`.
- `src/CONTRACTS.md` records the S1 front-end boundary: the parse IR is front-end-internal (names/type syntax retained for SEM) and the frozen `TcInstr`/`TcIR` are materialized in S3; `--syntax-only` is partial.
- Coverage is now 44 covered / 587 uncovered / 3 N/A(S0); S1 covers the §2/§14 chapter rows, four slots of each of the seven codes, and the stage/source-order rows.
- Gate reports are consolidated under `docs/gates/` (G0/G1 plus an index); README/AGENTS/docs map and the reports' relative links were updated.
- Code-comment cleanup: removed references to phase numbers (S0/S1...), gate reports, the test plan and audit results; comments now cite standard/design clauses or are self-contained. The CLI `--version` suffix is `(LEX/SYN frontend)`.

### Fixed

- Fixed a use-after-free on the parser failure path (reading `impl` after freeing the parser); nesting/source/token limits report implementation-level errors, never language codes.
- CRLF/CR fixtures are marked `-text` in `.gitattributes` so Git does not normalize away their bytes; `s1_cases.py --check` compares bytes.
- Pre-S2 pinning: added a `1.5.5` malformed-numeric-candidate L3 case and `1..5`/`1.5e5.5` L1 assertions, recording the "one LEX token" recovery convention; added an L3 invalid-UTF-8 byte fixture (scalar column after non-ASCII comment scalars); added field-padding `ANNOTATION(INT)` shape tests and the S4 field-annotation checklist.
- G1 independent audit A1–A9: paired nested blocks no longer report a false `MISSING_END` (lexical `then`/`end` pairing lookahead; paired nesting is `SYNTAX` at the nested construct); corrected the first-diagnostic rule 04 coverage mapping (`TC-SYN-END-0001`/`TC-SYN-VAR-0001` specific-over-generic); added nested-block/`Self`/suffix regression cases; rebuilt Debug/Release/ASan from current source; hardened the adapter (reported-file check, empty stdout, property stage check); clarified the plan's S1 `TcParseInstr`/`TcInstr` split; unified `#program` `Self` in IDENT-only positions to `PROGRAM_MODE_MISUSE` and documented incomplete/separated suffix positions.

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
