# 0.0.46 implementation layout

This directory is the **target module layout** from
[TC Native Compiler Design 0.0.46](../docs/TC原生编译器设计说明书-0.0.46.md) §14.
Implemented: the private `tc_core` library, the `tc-vm`/`tc-aot` entry points,
the frozen headers, the `frontend/` reader/lexer/parser/module-loader/analyzer
(LEX/SYN, IMPORT, names, types, conversions, layout, static instruction checks,
const_expr form, CFG/reachability/DI, missing returns, call ordering/safety,
safety contexts and annotations), the CT evaluator (const/static-var/const-local
values, demand order/cache, static initializer images), IR finalization
(`TcIR`/`TcUnit`/`TcBlock`/`TcInstr`, objects, consts, named arguments), the
deterministic IR text dump, the W-aware runtime semantic core (integer/float
arithmetic, conversions, exact deterministic scalar formatting, standard input
tokens) and the reference VM (regions, objects/sub-objects, pointer states, all
51 opcodes, calls/`D`, I/O, mandatory traps, commit semantics).  The AArch64
W=64 subset backend emits deterministic assembly and drives clang/lld to an ELF;
the full ABI, checked build, freestanding/bare metal and 32-bit targets are
later work.  Frozen contract ownership is indexed in
[CONTRACTS.md](CONTRACTS.md).

```text
src/
  base/         assert version (shared foundations, no language semantics)
  contract/     frozen-contract check + zero-pass optimization policy
  diagnostic/   closed 80-code set, generated table, first-diagnostic selection
  frontend/     input num lex parse; module (discovery/IMPORT); sem (names/types/
                conversions/layout/instruction shape/const_expr form/CFG/DI/calls/
                safety/annotations); ct (compile-time values/cache/images);
                irgen (final IR materialization)
  ir/           type instr block operand span region object address structs pointer_state
                print (deterministic IR text dump)
  runtime/      alloc io trap profile module_loader (platform seams);
                semantics (W-aware integer/float/conversion core);
                format (exact §11.2 scalar rendering); input (§11.3 tokens)
  vm/           vm.h interp memory (regions/objects/pointer state/all opcodes)
  backend/      aarch64 (subset emitter + freestanding runtime) common (target
                description); arm32 riscv32 not implemented
  driver/       tc-vm tc-aot + shared CLI/profile selection and AOT driver
```

`base/`, `contract/` and `diagnostic/` are shared **infrastructure**, not
language pipeline stages: their facts are consumed by every phase (and by the
runtime trap seam), so they must not be buried inside `frontend/`.  `frontend/`
owns the LEX/SYN/IMPORT/SEM/CT pipeline plus IR finalization: `input`
(arena/source/decode), `num` (exact decimal -> binary32/64), `lex`
(tokens/spans/LEX diagnostics), `parse` (EBNF + flat name-carrying parse IR),
`module` (discovery/IMPORT), `sem` (names/types/conversions/layout/instruction
shape, per-unit CFG/DI, call checks, safety contexts and annotation validation),
`ct` (CT values, demand order/cache, initializer images) and `irgen` (final flat
IR from parse IR + SEM + CT).  `ir/`, `runtime/`, `vm/`, `backend/` and
`driver/` follow design §14.

## Rules

- Source comments explain the code itself, cite the standard/design where
  useful, or state a self-contained fact.  Do not cite stage numbers, gate
  records, the test plan or audit findings in implementation comments;
  test-case ids belong to tests and the coverage matrix.
- Keep the target-independent core (`frontend`, `ir`, `runtime`, `vm`) separate
  from target-specific backends (`backend/<target>`). The first 64-bit target is
  AArch64; the first 32-bit target is still to be selected (arm32 / riscv32).
- The private build target `tc_core` contains the compiler, IR, runtime, and VM
  implementation. It is not installed and exposes no public API/ABI.
- Each source file has one module owner: `base/` + `contract/` are frozen
  infrastructure; `diagnostic/` owns the single diagnostic fact source;
  `ir/` owns representation; `runtime/` owns platform seams/profiles;
  `driver/` owns only CLI parsing and tool policy.  Do not create a second
  table, profile instance or diagnostic code set in another directory.
- `tc-vm` and `tc-aot` are the only tool entry points.  `--syntax-only`,
  `--sem-only` and `--static-only` are partial checks: exit 0 never claims a
  valid program.  A normal `tc-vm` invocation executes the finalized program on
  the reference VM.  A normal `tc-aot` invocation emits AArch64 subset assembly;
  `--emit=obj|elf` assembles/links with the bundled freestanding runtime.
  Unsupported opcodes/types/ABIs fail explicitly with exit 70 and publish no
  wrong artifact.
- Parse IR (`frontend/parse.h`) is intentionally not the frozen final IR:
  source names/type syntax stay until SEM, and field paths keep the
  `TcFieldRef` shape.  SEM resolves and validates them and owns the frozen type
  table; `frontend/irgen.c` materializes `TcInstr`/`TcIR` from the frozen facts
  and CT results.  Do not add source names to the frozen
  `TcOperand`/`TcInstr`.
- CT runs only after SEM is clean.  It owns `analysis->ct`; IR finalization owns
  `analysis->ir`.  After the type table freeze, only read-only type lookups are
  allowed (`tc_type_*_find`), never interning.
- Documented limits: 4 MiB source, 16 KiB token (identifier/annotation/float), 256 type/block nesting
  levels.  Exceeding them returns `tc_error_internal` (exit 71); it is never a
  language diagnostic.
- Do not reintroduce a C host public library, embedding API, host ABI, or
  AOT embed mode.
- Both 32-bit and 64-bit targets are in scope. `usize`/`isize` are platform-word
  types; `sizeof`, `bitcast`, `trunc`, layout, and diagnostics must handle both.
  The VM executes the `aarch64-linux-user`, `aarch64-baremetal` and `w32`
  profiles; the 32-bit profile is an executable reference VM profile while its
  native backend is still to be selected.
- The AArch64 backend consumes the same frozen `TcIR` as the VM and never
  re-parses the source or rewrites the type table.  It keeps object offsets
  identical to the VM for the in-region layout; absolute region bases are a
  platform/loader instance choice.  Values are allocated on the stack with a
  fixed temporary-register set (no regalloc); unsupported IR is rejected before
  any bytes are emitted.
- The runtime semantic core is shared with CT: one implementation of strict
  integer/float rounding, exception priority and conversions.  `CT` maps a
  runtime trap kind to the corresponding `TC_CE_CONST_*` code; it does not keep a
  second arithmetic implementation.
- VM frame storage is host-backed but addressed in the profile's frame region.
  The built-in profiles reserve a 16 MiB frame region and `stack_size`, enough
  for the default `D` (512 for hosted, 256 for baremetal/w32); a program whose
  static frame footprints exhaust that region is an implementation error, not a
  language diagnostic.

Implementation starts from the 0.0.46 standard and design document; historical
code exists only in git tags.
