# Changelog

## [0.0.46] - 2026-11-15

### Added

- **0.0.46 clean-sheet implementation**: a private `tc_core` rewritten from the
  standard/design, with `tc-vm`/`tc-aot` as the only tool entry points; no C host
  public library, embedding API or ABI.
- **Contracts and test infrastructure**: type/IR/object/region/profile/diagnostic/
  zero-pass contracts; closed 80-code set and first-diagnostic comparator; harness,
  golden files and coverage matrix; L0/L1 tests.
- **LEX/SYN and flat parse IR**: UTF-8/comments/continuations/longest match, exact
  decimal literals, keywords/mnemonics/suffixes/annotations, EBNF, module layers,
  field paths, named arguments, `switch` targets and spans; the seven LEX/SYN codes.
- **Module discovery and IMPORT**: import-source-order DFS, importer-directory then
  include-root search, resolved-path instance keys, `module_order`, the five IMPORT
  codes and import-name conflicts.
- **Names, types, conversions and layout**: members/namespaces/visibility, type-table
  interning and freeze, W-dependent literal typing, `cast`/`trunc`/`bitcast` matrices,
  struct layout/`@padding`, `sizeof`, field/assignment paths, instruction shapes/types,
  mode matrix and the 32/64 static profile.
- **Tool capabilities**: `--syntax-only` (LEX/SYN) and `--static-only`
  (LEX/SYN/IMPORT/SEM) partial checks; a normal invocation explicitly exits 70 after
  the static stages. Added the `w32` static profile (not an executable target).
- **Tests**: `tc_frontend_test`, `tc_sem_test`, S1/S2/S3 conformance tables,
  determinism suites and a fail-closed adapter self-test; currently 19/19 `ctest`
  and 346/346 harness cases in Debug/Release/ASan builds.

### Changed

- Final `TcInstr`/`TcIR` materialization remains part of IR finalization; S3 validates
  and freezes the type table.
- Diagnostic-position and mixed-form conventions live in `src/CONTRACTS.md`;
  `--static-only` and normal invocation never claim a valid program.

### Removed

- Historical implementation, tests, tools, scripts and examples; historical code is
  available only in git tags.
