# Changelog

## [0.0.46] - 2026-11-16

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
- **const_expr forms**: the §3.10 positive op set for `const`/`static var`
  initializers, const-only value references, and `CONST_FORM` for forbidden
  suffixes, pointer bitcasts and var/parameter/static references.
- **CFG, DI, returns, calls, safety and annotations**: one closed CFG per
  program/function, all edges and reachability, `U[n]` initialization and the DI
  maximum fixed point, `MISSING_RETURN`; labels/control shapes; direct/indirect call
  order (target → safety → LHS → arguments → receiver) and call safety; safe/unsafe
  contexts with `@safe` priority; function/parameter/field annotation matrix,
  `@align`/`@range` endpoint constants (same-type integer `lo`/`hi`,
  power-of-two `N`), fixed binary32/64 NaN/underflow/cast rules
  independent of host `fenv`/`long double`, strict failures that never become
  SEM annotation diagnostics, 64-bit `@align`/`@padding` metadata, and promise violations that
  are never a static rejection; duplicate labels and malformed branches use
  conservative fallthrough, and missing labels no longer suppress DI on known
  edges.
- **CT evaluation**: compile-time evaluation of `const`/`static var`/local `const`;
  the §3.10 positive allowlist, §6.3–§6.6 strict integer/float semantics and `CONST_*`
  mapping, exact `mod`, underflow as tininess-after-rounding; demand-driven in
  `module_order`/module source order with success/failure caching, the first failure
  attributed to its target declaration and no cascade; aggregate const field trees and
  static `TcInitImage` (profile endianness, zero padding, bool 0x00/0x01).
- **IR finalization and freeze**: materialized `TcIR`/`TcUnit`/`TcBlock`/`TcInstr`;
  field paths become vreg + field indices, labels/`switch` targets use block ids, named
  arguments and spans survive; an aggregate const inlines as a use-site `make` and owns
  no object; static/frame object tables, module instance ids, per-profile layout and the
  frozen type table; deterministic `tc_ir_print` text dump.
- **Runtime semantic core and deterministic I/O**: W-aware strict/wrap integers,
  division/modulo/shift boundaries, fixed binary32/64 rounding and exception
  priority, exact underflow, `cast`/`trunc`/`bitcast`; CT and the VM share this core
  (CT maps failures to the six `TC_CE_CONST_*` codes); exact six-significant-digit
  §11.2 rendering without host `printf`, and §11.3 complete-token input with
  EOF/short-read/error states.
- **Reference VM (W=32/64)**: five-region address space, runtime objects and field
  sub-objects, pointer states, all 51 opcodes, calls/`D`, no tail-call depth change,
  all-or-nothing I/O, the 15 mandatory `TC_RE_*` traps, and no rollback of output
  committed before a trap; `--checked` adds UB bounds checks;
  `aarch64-linux-user`, `aarch64-baremetal` and `w32` profiles.
- **Tool capabilities**: `--syntax-only` (LEX/SYN), `--sem-only` (LEX/SYN/IMPORT/SEM)
  and `--static-only` (through CT and IR finalization) partial checks; a normal
  `tc-vm` invocation executes the reference VM and reports the first trap as
  `error[RT]` with exit 1; a normal `tc-aot` invocation still exits 70 (backend not
  implemented).
- **Tests**: `tc_frontend_test`, `tc_sem_test`, `tc_ct_test`, `tc_ir_test`,
  `tc_vm_test`, S1/S2/S3/S4/S5/S6 conformance tables, W=32/64 runtime goldens,
  determinism suites and a fail-closed adapter self-test; currently 32/32 `ctest`
  and 753/753 harness cases (557 compile, 129 tool, 66 run, 1 property) in
  Debug/Release/ASan builds; vm unit 413 checks.

### Changed

- SEM validates and freezes the type table; CT and IR finalization run afterwards with
  read-only type lookups only (no interning); SEM's CFG/DI facts are materialized into
  explicit block edges during IR finalization.
- Wrap-up is now fixed process: process/historical documentation is removed and the
  `docs.cleanup` gate must pass; current conventions and known limits live in
  `src/CONTRACTS.md`.
- Diagnostic positions, CT order/cache/error mapping, static layout, IR operand and
  runtime/VM conventions live in `src/CONTRACTS.md`; `--sem-only`/`--static-only`
  never claim a valid program, and a normal `tc-vm` run reports only the first trap
  or normal termination.

### Removed

- Historical implementation, tests, tools, scripts and examples; historical code is
  available only in git tags.
