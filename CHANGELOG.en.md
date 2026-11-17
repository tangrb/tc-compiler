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
- **AArch64 W=64 subset backend and toolchain**: lowering for scalar integer/bit/
  comparison ops, `br`/`br_if`/`switch`/`return`/`trap`, direct calls, integer/bool
  `read`/`write`/`writeln`, `sizeof` and `copy`; the AAPCS64 subset (x0-x7/x0,
  16-byte stack alignment, callee-saved frame/static bases, bool/int8/int16
  extension, `D`); frame/static layout at `TcObject` offsets, static initializer
  `.data`, deterministic assembly emission, no regalloc; `tc-aot
  --emit=asm|obj|elf`, clang/lld driving and the `aarch64-none-elf` target-triple
  mapping; a freestanding AArch64 runtime (startup, semihosting output, integer/
  bool formatting/input, trap/exit and depth counter).
- **S7 VM↔AOT differential, ABI and toolchain verification**: reuse of the S6
  W=64 subset cases, 64-bit/static/switch/§11.3-input boundary and adversarial
  cases, fixed-seed integer fuzz,
  AAPCS64 subset ABI, byte-reproducible `.s`/ELF, O0/O2 zero pass, byte-identical
  VM/AOT first diagnostics over every static `.tc`, and explicit exit-70 rejection
  of unsupported opcodes/checked build/non-target profiles; Release and
  ASan+UBSan pass.
- **S7 hardening**: dynamic switch-CFG successor capacity; arbitrary leading zeros
  in AOT input; executable permission on ELF artifacts; CMake requires
  clang/llvm-ar/lld/qemu and no longer silently degrades; linker-provided input/
  stack addresses with overlap assertions; regression tests for switch successors
  and `TcFunc.body`; test adapter canonicalizes symlinked workspaces; the
  differential suite separates actual differential cases from explicit
  rejections.
- **Tool capabilities**: `--syntax-only` (LEX/SYN), `--sem-only` (LEX/SYN/IMPORT/SEM)
  and `--static-only` (through CT and IR finalization) partial checks; a normal
  `tc-vm` invocation executes the reference VM and reports the first trap as
  `error[RT]` with exit 1; a normal `tc-aot` invocation emits AArch64 subset
  assembly and `--emit=obj|elf` produces ELF artifacts; capabilities outside the
  subset exit 70 without publishing a wrong artifact.
- **Tests**: `tc_frontend_test`, `tc_sem_test`, `tc_ct_test`, `tc_ir_test`,
  `tc_vm_test`, `tc_aot_test`, S1/S2/S3/S4/S5/S6 conformance tables, S7
  differential/ABI/toolchain/static-parity/determinism suites, W=32/64 runtime
  goldens, determinism suites and a fail-closed adapter self-test; currently
  39/39 `ctest` and 754/754 harness cases (557 compile, 129 tool, 67 run,
  1 property) in Debug/Release/ASan builds; unit checks: frontend 688, sem 102,
  ct 370, ir 674, vm 413, aot 34.

### Changed

- SEM validates and freezes the type table; CT and IR finalization run afterwards with
  read-only type lookups only (no interning); SEM's CFG/DI facts are materialized into
  explicit block edges during IR finalization; `TcFunc.body` pointers are fixed up
  after the unit array stops growing and point into the final `TcIR.units`.
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
