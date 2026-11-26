# Changelog

## [0.0.47] - Unreleased

### Added

- **P0/T0 governance**: toolchain version 0.0.47 is separate from language version
  0.0.46; a private `tc` project-tool entry point provides CLI grammar, `--help`
  and `--version`.
- **P1 project initialization**: `tc --init-project` generates the full project
  template in the canonical current directory, creates only missing targets, and
  supports joint pre-write identity checks, failure rollback and post-commit reporting.
- **P1 manifest APIs**: `src/project/manifest.c` implements `tc.project` v1 parsing,
  defaults, bounded reads and nearest-ancestor root discovery. These private APIs
  are not yet consumed by compiler path handling.
- **Unified implementation errors**: every entry point shares the `TcImplError`
  exit mapping. Host I/O, unavailable capabilities and actual OOM exit 70;
  usage exits 2; internal contract violations and documented limits exit 71.
- **Contract verification**: exact three-entry version output, `tc` option/
  terminator/uint64-timeout grammar, side-effect-free unavailable `--test`,
  missing-file/output-I/O behavior, release audit and test labels.
- **P1 tests**: register `project.manifest`, `project.init` and
  `project.init_fault`, and update `cli.contract`; cover manifest/root discovery,
  the full template, preflight/identity/alias checks and rollback at every fault point.
- **Failure-path verification**: allocation-by-allocation OOM injection and cleanup
  checks for SEM, the VM and all three backends, closed stdout/stderr and
  side-effect checks,
  and release-audit mutation self-tests. The unified OOM mapping and future
  extension boundary are recorded in `src/CONTRACTS.md`.

### Status

- There are 76 registered 0.0.47 CTests (73 at P0 plus three P1 tests). P1
  full Debug/Release/ASan+UBSan regressions each pass 76/76. The accepted 0.0.46
  baseline remains harness 873/873, coverage 636/636, with all closing checks
  and release audits passing.
- P0/T0 and P1 are complete; P2–P5/T1–T6 have not started and G11 is incomplete.
  `tc --init-project` is available; the manifest is not yet consumed by the
  compiler, and `tc --test` still explicitly exits 70.
- P4b VM debug-ELF internal layout is implementation verification with evidence
  only and does not write back to the specification.

## [0.0.46] - 2026-11-19

### Status

- **G10 PASS; the 0.0.46 phase exit is complete**: S8 AArch64 semantics/ABI,
  S9 freestanding/ARM32 and S10 `riscv32-baremetal` plus long-term regression
  all passed their gates.
- Current verification: Debug/Release/ASan+UBSan `ctest` **71/71**; harness
  **873/873**; coverage **636/636**; `docs.cleanup`, `release.s10_audit` and
  golden checks all pass.

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
- **Full W=64 AArch64 backend and toolchain**: lowering for all 51 opcodes and
  all value types (integer/bit/comparison, float, conversion, pointer/object,
  `struct`/`make`/`copy`/`memmove`, direct/indirect calls, `funcaddr`/`funceq`/
  `funcne`, `sizeof`, I/O, control flow); AAPCS64 composites/HFA/large indirect
  returns and large by-reference composites, `fnptr`/`call_indirect`, multi-size
  and unaligned access; frame/static
  layout at `TcObject` offsets, object descriptor tables, static initializer
  `.data`, reset entry, region bases, deterministic assembly emission, no
  regalloc; `tc-aot --emit=asm|obj|elf`, clang/lld driving with `--icf=none` and
  the `aarch64-none-elf` target-triple mapping; a freestanding AArch64 runtime
  (startup, semihosting output, deterministic formatting/input, object/pointer
  checks, checked shadow, trap/exit, depth counter and runtime allocator).
- **Full runtime and differential/consistency verification**: every W=64 S6
  runtime case plus new struct/HFA/large-composite/many-argument/indirect-call/
  pointer/static-byte/`@fast_math` differential cases; full AAPCS64 ABI and an
  assembly-shape audit; a standard §13.4 eight-item consistency suite; checked
  build (out-of-object, one-past, dangling and uninitialized) counted separately
  from mandatory traps without changing the legal program set; ELF entry/region/
  symbol/static-byte/ICF and determinism suites; the S7 differential/ABI/
  toolchain/static-parity/zero-pass suites keep passing; byte-identical VM/AOT
  first diagnostics over every static `.tc`; Release and ASan+UBSan pass.
- **Freestanding/bare-metal and the 32-bit backend**: a target-neutral
  freestanding runtime core with platform seams (object-table mandatory and
  checked traps, all four device policies, deterministic I/O, arena allocator,
  trap/exit records); `aarch64-baremetal` (PL011 UART0 stdout/framed stdin, UART1 exit
  record, PSCI SYSTEM_OFF, no semihosting, ELF/flat/symbol/region
  audit); the first 32-bit target `arm32-baremetal` (Cortex-M7 MPS2 AN500,
  `thumbv7em-none-eabihf`, base AAPCS32/ILP32 softfp, an independent ARM32
  backend for all 51 opcodes, composite arguments as Rule C.4/C.5 memory words,
  64-bit pair returns/indirect returns, `int64` helpers, software binary64, unaligned
  access, checked build, ARM EABI `__aeabi_*` division/memory helpers and
  `qemu-system-arm` execution); `tc-aot --profile=` target dispatch and
  `--device=` platform-policy override; 32/64 static results, goldens and
  differentials stay isolated.
- **S10 multi-target expansion and long-term regression**: the second 32-bit
  target `riscv32-baremetal` (RV32IMAFDC, ILP32 soft-float ABI,
  `TcTargetDesc`/`TcABIDesc`, an independent RISC-V 32 backend for all 51
  opcodes, a0-a7 arguments / 16-byte stack alignment / 2×XLEN by-value
  composites / larger by-reference composites / hidden-pointer indirect
  returns, 64-bit division helpers, NS16550A UART0 stdout and framed stdin, a
  semihosting exit record, the SiFive test finisher and qemu-system-riscv32
  virt); the S10 RISC-V platform/ELF/device/checked/UART-input suite, 32 C-caller
  and 14 TC-caller RISC-V ILP32 interoperability cases, 1037 static sources with
  identical VM/AOT first diagnostics, 149×2 long-term static sources, 16×3
  differential samples, 15 fuzz regression cases and 3-target platform corpus,
  plus RISC-V repeat-build/execute and O0/O2 determinism; the release audit
  checks the closed 80-code set, private `tc_core`, two CLIs, freestanding
  symbols, version and documentation consistency.
- **S10 adversarial expansion**: generated ABI fuzz (random scalar/composite/
  padding signatures, C→TC and TC→C, all three AOT targets), RISC-V
  implementation-limit/robustness cases (frame and static-region budgets,
  `D=63/64`, device-window end, empty input, flat execution, long trap-record
  paths) and a RISC-V static-mutation suite; the trap/exit record buffer grows
  to 8192 bytes so practical paths retain `TC-EXIT`; C interop harnesses use
  `-mstrict-align` for unaligned packed composites. The full `ctest` suite grows
  to 71 tests. The front-end arena no longer rejects a single allocation above
  4 MiB (a legal source crossing the 32768-token growth step was misreported as
  OOM); an 800-case switch regression covers the boundary, 39 checked-build UB
  fixtures are rerun on RISC-V for VM/AOT agreement, and a 30000-instruction
  far-branch regression covers the auipc/jalr lowering used when a RISC-V
  branch exceeds the 1 MiB `j`/`b.cond` range. The reset entry zeros BSS before
  `tc_main`, so ELF and flat images start from a deterministic zero runtime
  state; AArch64/ARM32 conditionals over far blocks use a conditional skip plus
  an unconditional transfer. The ARM32 1 MiB code window remains an explicit
  implementation boundary.
- **Tool capabilities**: `--syntax-only` (LEX/SYN), `--sem-only` (LEX/SYN/IMPORT/SEM)
  and `--static-only` (through CT and IR finalization) partial checks; a normal
  `tc-vm` invocation executes the reference VM and reports the first trap as
  `error[RT]` with exit 1; a normal `tc-aot` invocation emits target assembly,
  `--emit=asm|obj|elf|flat` produces assembly, ELF or flat artifacts and
  `--checked` produces a checked build; `--profile=`/`--device=` select a target profile and device policy;
  unmapped platform capabilities exit 70 without publishing a wrong artifact.
- **Tests**: `tc_frontend_test`, `tc_sem_test`, `tc_ct_test`, `tc_ir_test`,
  `tc_vm_test`, `tc_aot_test`, S1/S2/S3/S4/S5/S6 conformance tables, S7
  differential/ABI/toolchain/static-parity/determinism suites, S8 full
  differential/ABI/C-interop/generated-fuzz/checked/consistency/artifact/
  determinism/static-mutation suites, W=32/64 runtime goldens, S9 bare-metal/
  ARM32/interop/static-parity/profile-isolation/determinism suites, AAPCS64/
  AAPCS32 bidirectional interop, ABI mutation audit, capacity report,
  determinism suites and a fail-closed adapter self-test; currently 71/71
  `ctest` and 873/873 harness cases (608 compile, 131 tool,
  133 run, 1 property) in Debug/Release/ASan builds; unit checks: frontend 688,
  sem 102, ct 370, ir 674, vm 413, aot 81; S9 ARM32 differential: 29 fixed
  cases plus 296 S6/S8 corpus cases, 14 C-caller AAPCS32 cases, 33 C-caller
  AAPCS64 cases, 19 reverse ABI cases and 1037 static sources under each of two
  bare-metal profiles; the S10 RISC-V suite adds 29 fixed cases plus 296 S6/S8
  corpus cases, 32 C-caller/14 reverse ABI cases, 1037 static sources, 149×2
  long-term static sources and 15 fuzz cases; 5/5 ABI mutants were found by
  interop.

### Changed

- **Diagnostic closure**: annotations on a bare `NAME` are classified by
  syntactic type; `@align`/`@nonnull`/`@no_alias` on a non-pointer and `@range`
  on a non-integer report `TC_CE_ANNOTATION` at the annotation token.  The
  0-based invalid-UTF-8 byte offset is carried through every diagnostic path
  (default, `--sem-only`, `--static-only`, `--syntax-only`, AOT).  Added the S4
  annotation matrix, all-path/malformed/imported/pipe CLI checks and an adapter
  offset expectation; the harness rejects undeclared offsets and validates the
  note's file/position/stage; harness is 873/873.
- **Specification wording clarifications**: §12.2 #9 applies only when the
  candidate primary positions are identical; fixed-shape errors
  (`OPERAND_COUNT`/`RETURN_FORM`) precede later-column literals by column;
  device policy constrains accesses, while pure `ptradd`/`ptrsub` only traps
  under the reject policy; one-past `ptrsub n=0` stays one-past;
  `CIRCULAR_IMPORT` relates to the target module header; "cycle before
  duplicate" is per import edge; `make` fields accept only const value forms
  (op_form/nested `make` is SYNTAX). No diagnostic code, acceptance set or
  observable behavior changes.
- **Runtime/diagnostic fixes**: finite/±inf division no longer reports a false
  underflow (RT/CT); decimal literals that round across a binade boundary no
  longer double the value; same-parameter annotation errors precede later type
  errors by §12.2 #3; each stdout write/writeln record is committed immediately
  and a failure reports `TC_RE_IO` at that instruction; invalid UTF-8 emits a
  0-based byte-offset note. Added S6/S4/CLI regressions; the Debug gate is
  71/71 `ctest` and 873/873 harness cases.
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
