# Compiler and toolchain implementation layout

This directory is the **target module layout** from
[TC Native Compiler Design 0.0.46](../docs/TC原生编译器设计说明书-0.0.46.md) §14.
Implemented: the private `tc_core` library, the `tc`/`tc-vm`/`tc-aot` entry points,
the frozen headers, the `frontend/` reader/lexer/parser/module-loader/analyzer
(LEX/SYN, IMPORT, names, types, conversions, layout, static instruction checks,
const_expr form, CFG/reachability/DI, missing returns, call ordering/safety,
safety contexts and annotations), the CT evaluator (const/static-var/const-local
values, demand order/cache, static initializer images), IR finalization
(`TcIR`/`TcUnit`/`TcBlock`/`TcInstr`, objects, consts, named arguments), the
deterministic IR text dump, the W-aware runtime semantic core (integer/float
arithmetic, conversions, exact deterministic scalar formatting, standard input
tokens) and the reference VM (regions, objects/sub-objects, pointer states, all
51 opcodes, calls/`D`, I/O, mandatory traps, commit semantics, checked-build
bounds/dangling/uninitialized detection).  The full W=64 AArch64 backend emits
deterministic assembly for all value types and opcodes, implements the AAPCS64
argument/return surface (composites, HFA, large by-reference composites and indirect returns, `fnptr`/
`call_indirect`), the object-table driven memory helpers (mandatory and
checked-build traps), reset entry/region layout/static `.data` and drives
clang/lld to an ELF or flat image with ICF disabled.  The reset entry clears
BSS before `tc_main`, and far conditional/switch/return transfers use a local
skip plus an unconditional transfer (RISC-V uses the auipc/jalr sequence).  The
freestanding runtime is a target-neutral core with platform seams: `aarch64-baremetal` uses PL011 UART0
for program stdout and framed stdin, PL011 UART1 for the exit record and PSCI
shutdown (no semihosting), while `arm32-baremetal` is an independent Cortex-M7
backend with the base AAPCS32/ILP32 softfp classifier, 32-bit word-size
semantics, int64 helpers and software binary64, qemu-system-arm execution and
a semihosting exit record; binary32 uses FPv5-D.  `riscv32-baremetal` is an
independent RV32IMAFDC/ILP32 soft-float backend with an NS16550A UART0
stdout/stdin seam, a semihosting exit record, the SiFive test finisher and
qemu-system-riscv32 virt execution; its runtime adds the RV32 compiler-runtime
64-bit divide/remainder helpers.  All four device policies, checked build, generated ABI fuzz,
implementation-limit/scale/far-branch regressions, ELF/symbol audits and 32/64
profile isolation are tested.  Frozen contract
ownership is indexed in [CONTRACTS.md](CONTRACTS.md).

The 0.0.47 toolchain implements split language/toolchain versions, three private
entry points, one implementation-error exit mapping, project initialization,
compiler path consumption and the project test runner. `tc --init-project`
generates the complete template in the canonical current directory with pre-write
identity checks and rollback. `project/manifest.c` provides the private
`tc.project` v1 parser and nearest-ancestor root-discovery APIs.
`project/paths.c` and `base/path.c` bind the canonical source, ordered
manifest/CLI include roots, manifest default profile and project-root-relative
display paths into `tc-vm` and `tc-aot`; diagnostics, IR metadata, VM traps and
AOT source strings consume those display paths. Module identity uses bound parent
directories and actual entry names, and root/import sources are parsed from their
bound descriptors with nonblocking bounded reads. `driver/test_runner.c` implements
one-level discovery/filtering, fd-anchored beneath resolution, strict sidecars,
private read-only snapshots, serial fork workers, timeouts, stream capture,
structured results, reports and exit mapping. P4a adds the shared deterministic
line map, private AOT `.file`/`.asciz`/`.loc` and source-comment emission, and a
minimal DWARF v4 CU with clang-generated `.debug_line`. T1 adds VM/AOT
`--dump-ir[=<path>]`, VM `--trace=ir,call,mem,io,trap` with text/jsonl encoding,
limits and output selection, VM/AOT `--stats` with output selection, structured trap
context, explicit output identity/reservation guards and deterministic observation
output; its evidence entry point is `t1.observe`. T2 exposes VM/AOT
`--dump-linemap[=<path>]` and public `tc-aot --debug`, reusing one final-IR
line map for JSON v3, synthetic VM addresses, `.loc`, source comments and the
DWARF v4 CU. Its evidence entry points are `t2.debug` and the real
qemu-gdbstub/gdb smoke `t2.gdb`. T3 adds the private VM GDB stub
(`vm/gdb.c`, `gdb_rsp.c`, `gdb_journal.c`): `tc-vm --gdb[=<port>]` defaults to
port 0 and binds loopback only, with mandatory `--gdb-stdout-journal=<dir>`.
It exposes synthetic IR addresses and `target.xml`/memory map, supports
breakpoints/step/continue, monitor and extended restart, and provides an atomic
stdout journal plus resumable stdin. Writes are rejected by default;
`--gdb-allow-write` enables restricted writes. Evidence entry points are
`gdb_journal.unit`, `gdb_rsp.unit`, `vm.gdb.protocol` (235 checks),
`vm.gdb.semantics` and `vm.gdb.e2e`; the first four are fast, while e2e is not.
P4b adds `vm/debug_elf.c`, a private ELF/DWARF writer for
`tc-vm --gdb-elf-out=<path>`. It reuses the shared line map and synthetic addresses,
emits source function symbols and DWARF v4 lines without external tools, and covers
AArch64, ARM32 and RISC-V32 profiles. Its evidence entry point is
`toolchain.debug_elf`. Its 32-bit real-GDB compatibility and fixed Linux CI signed
evidence are pending, so P4b remains incomplete; P5 and T4–T6 have not started. Compiler and observation output guards protect
stdio, project inputs, explicit outputs and reservations before publishing diagnostics
or artifacts.

```text
src/
  base/         assert version, canonical/display path context (shared foundations,
                no language semantics)
  contract/     frozen-contract check + zero-pass optimization policy
  diagnostic/   closed 80-code set, generated table, first-diagnostic selection
  frontend/     input arena num lex parse; module (discovery/IMPORT); sem (names/
                types/conversions/layout/instruction shape/const_expr form/CFG/DI/
                calls/safety/annotations); ct (compile-time values/cache/images);
                irgen (final IR materialization)
  ir/           type type_width instr block operand span region object address
                structs pointer_state print (deterministic IR text dump)
  runtime/      alloc io trap profile module_loader (platform seams);
                semantics (W-aware integer/float/conversion core);
                format (exact §11.2 scalar rendering); input (§11.3 tokens)
  vm/           vm.h interp memory observe debug_elf gdb gdb_rsp gdb_journal
                (regions/objects/pointer state/all opcodes, deterministic
                tracing/statistics/trap context, private GDB stub/stdout journal)
  backend/      common (target-neutral freestanding runtime core, ABI data,
                toolchain mapping, emission statistics); aarch64 (emitter/AAPCS64 classifier,
                semihosting + PL011/PSCI platform instances, linker script);
                arm32 (independent Cortex-M7 emitter/AAPCS32 classifier, ARM
                EABI helpers, MPS2 platform, linker script); riscv32
                (independent RV32IMAFDC emitter/ILP32 classifier, compiler
                runtime helpers, QEMU virt platform, linker script)
  driver/       tc tc-vm tc-aot + shared CLI/profile selection, observation output,
                AOT driver and project test runner
  project/      name validation, tc.project v1 parser/root discovery, compiler path
                binding/profile/include consumption, atomic init
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
  from target-specific backends (`backend/<target>`). AArch64 is the 64-bit
  target; Cortex-M7 `arm32-baremetal` and RISC-V `riscv32-baremetal` are the
  two implemented 32-bit targets.
- The private build target `tc_core` contains the compiler, IR, runtime, and VM
  implementation. It is not installed and exposes no public API/ABI.
- Each source file has one module owner: `base/` + `contract/` are frozen
  infrastructure; `diagnostic/` owns the single diagnostic fact source;
  `ir/` owns representation; `runtime/` owns platform seams/profiles;
  `driver/` owns only CLI parsing and tool policy; `project/` owns project
  metadata/root discovery and initialization. Do not create a second
  table, profile instance or diagnostic code set in another directory.
- `tc`, `tc-vm` and `tc-aot` are the only tool entry points. `tc` does not compile
  or run user programs itself. Its `--init-project` operation creates missing project
  targets with pre-write identity checks and rollback; `--test` discovers and runs
  project tests with strict sidecars, private read-only snapshots, fork workers,
  timeout/stream capture, structured results, reports and exit mapping.
  `--syntax-only`, `--sem-only` and `--static-only` are partial checks:
  exit 0 never claims a
  valid program.  A normal `tc-vm` invocation executes the finalized program on
  the reference VM.  A normal `tc-aot` invocation emits target assembly
  (AArch64, ARM32 or RISC-V 32);
  `--emit=asm|obj|elf|flat` assembles/links with the bundled freestanding target
  runtime.
  Capabilities outside the bound platform instance fail explicitly with exit 70
  and publish no wrong artifact. `tc_impl_error_exit` is the only private
  implementation-error mapping: OK→0, USAGE→2, IO/UNAVAILABLE/OOM→70,
  INTERNAL→71.
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
  The VM executes the `aarch64-linux-user`, `aarch64-baremetal`,
  `arm32-baremetal`, `riscv32-baremetal` and `w32` profiles; the bare-metal
  profiles have both a reference VM profile and a native backend.
- The AArch64 backend consumes the same frozen `TcIR` as the VM and never
  re-parses the source or rewrites the type table.  It keeps object offsets
  identical to the VM for the in-region layout; absolute region bases are a
  platform/loader instance choice.  Values are allocated on the stack with a
  fixed temporary-register set (no regalloc); unsupported IR is rejected before
  any bytes are emitted.
- The runtime semantic core is shared with CT and all AOT targets: one
  implementation of strict integer/float rounding, exception priority and
  conversions.  `CT` maps a runtime trap kind to the corresponding
  `TC_CE_CONST_*` code; it does not keep a second arithmetic implementation.
  The exact 128-bit comparisons use `runtime/wide.c` so the core builds without
  `__int128` on the 32-bit target.
- VM frame storage is host-backed but addressed in the profile's frame region.
  The built-in profiles reserve a frame region and `stack_size` for the default
  `D` (512/256/32/64).  AOT additionally enforces
  `frame_total*(D+1) <= stack_size` before emission, so a large frame may run on
  the VM but is rejected by AOT with the implementation error (exit 71) and no
  artifact; this is an implementation resource boundary, not a language
  diagnostic.  ARM32's 1 MiB code window rejects oversized functions at link
  time; a 30000-instruction far-branch regression is covered, but larger single
  functions depend on host/toolchain resources.
- Target backends share the freestanding runtime core, not target lowering:
  AArch64, ARM32 and RISC-V 32 keep separate emitters, ABI classifiers and
  startup/linker scripts.  Platform seam files own the console/termination policy; the core
  owns object checks, device-policy mapping and deterministic I/O.

Implementation starts from the 0.0.46 standard and design document; historical
code exists only in git tags.
