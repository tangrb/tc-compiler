# TC-Compiler

TC **0.0.46** language/compiler specification and target design for the native
compiler (TC → ASM → machine code). 0.0.46 is the clean-sheet baseline: only the
standard, design, test plan and the implementation rewritten from them are
kept; historical implementation and tools live in git tags. The `tc-0.0.47`
branch delivers the development/debugging toolchain and project model
while language semantics, opcodes, diagnostics and the ABI stay frozen. Both
designs meet the freeze criteria. P0–P5, T0–T6, P4b formal acceptance and G11 are
complete, and G0–G11 PASS. Fixed Linux CI real-gdb signed evidence across all four
profiles passed the release audit. Scope
and contracts are in
[TC Development and Debugging Toolchain 0.0.47](docs/TC开发调试工具链-0.0.47.md)
and [TC Project Model 0.0.47](docs/TC工程模型-0.0.47.md).
RTOS, interrupts and concurrency move to 0.0.48.

## Current status

- **0.0.47 toolchain, observation and source mapping**: toolchain version 0.0.47 is separate from language/contract
  version 0.0.46/1. The private `tc_core` links three entry points:
  `tc`, `tc-vm` and `tc-aot`. Implementation errors share the 0/2/70/71 mapping.
  `tc` implements project CLI grammar, `--help`, `--version` and the full
  `--init-project` template with atomic writes, rollback and identity checks.
  `tc-vm` and `tc-aot` consume `tc.project`, canonical roots, ordered include
  roots and the manifest default profile. Canonical paths remain internal while
  diagnostics, IR metadata, VM traps and AOT source strings use
  project-root-relative display paths. Module identity is bound to parent
  directories and actual entry names; source reads are nonblocking and bounded.
  `tc --test` implements one-level discovery/filtering, fd-anchored beneath
  resolution, strict sidecar validation, private read-only snapshots, serial fork
  workers, timeouts, stream capture, structured results, reports and exit mapping.
  P4a adds the shared deterministic line map and private AOT debug emission:
  `.file`/`.asciz`/`.loc`, source comments, a minimal DWARF v4 CU and clang's
  `.debug_line`, with input/output identity and stdio guards before publication.
  T1 adds VM/AOT `--dump-ir[=<path>]`, VM `--trace=ir,call,mem,io,trap` with
  `text`/`jsonl`, `--trace-limit=<n>` and `--trace-out=<path>`, VM/AOT `--stats`
  with `--stats-out=<path>`, structured trap context, explicit output identity and
  reservation guards, and deterministic observation output; its evidence entry
  point is `t1.observe`. T2 adds VM/AOT `--dump-linemap[=<path>]` and public
  `tc-aot --debug`, reusing the final-IR line map for JSON v3, synthetic VM
  addresses, `.loc`/source comments and the DWARF v4 CU. Real gdb through the
  qemu gdbstub verifies `info line`, source breakpoints and single-step;
  evidence entry points are `t2.debug` and `t2.gdb`. T3 adds
  `tc-vm --gdb[=<port>]` (default 0, loopback only) with mandatory
  `--gdb-stdout-journal=<dir>`, synthetic IR addresses, `target.xml`/memory map,
  breakpoints/step/continue, monitor, extended restart, an atomic stdout journal
  and resumable stdin. `--gdb-allow-write` enables restricted writes. Evidence
  entry points are `gdb_journal.unit`, `gdb_rsp.unit`, `vm.gdb.protocol`
  (242 checks), `vm.gdb.semantics` and `vm.gdb.e2e`. P4b adds
  `tc-vm --gdb-elf-out=<path>`: a private internal writer emits symbols and
  DWARF v4 lines without external tools for AArch64, ARM32 and RISC-V32 profiles.
  Independent semantic decoding covers all four profiles, and `toolchain.debug_elf`
  passes real-gdb debug ELF, source breakpoints, 64-bit `tc_ir_pc` reads, IR
  single-step and program-semantic regressions across all four profiles. Synthetic
  `tc_ir_pc` is a 64-bit `uint64`, architectural PCs remain `code_ptr` (64-bit on
  AArch64, 32-bit on ARM32/RISC-V32), and ARM/RISC-V Z0 kind=2 is accepted.
  ARM clients need `set arm force-mode thumb`. Synthetic ARM/RISC-V IR steps use
  maintenance packet `s` with a register-cache flush to verify the same RSP step.
  T4 adds `--diag-format=json` and the stable JSONL schema. T5 adds CMake presets,
  fixed CTest labels, the unified `tests/run.py` entry point and authenticated
  `tc-reduce`. T6 verifies repeatable and root-independent debug `asm/obj/elf/flat`
  artifacts for all three architectures through `release.t6_repro`.
- **Later stages**: RTOS, interrupts and concurrency move to 0.0.48.
- **Implemented**: contracts, LEX/SYN and flat parse IR, module discovery/IMPORT,
  names/types/conversions/layout, static instruction-shape checks, const_expr forms,
  CFG/reachability/DI, missing returns, call ordering/safety, safety contexts and the
  annotation matrix, CT evaluation (demand order/cache, strict error mapping, static
  initializers), IR finalization/freeze and the deterministic printer; the W-aware
  runtime semantic core (integer/float/conversions, exact §11.2 formatting, §11.3
  input) and the reference VM (five regions, objects/field sub-objects, pointer
  states, all 51 opcodes, calls/`D`, I/O, mandatory traps, commit semantics); 32/64
  profiles, with the VM able to execute `aarch64-linux-user`, `aarch64-baremetal`,
  `arm32-baremetal`, `riscv32-baremetal` and `w32`; the full W=64 AArch64 backend (integer/bit/comparison/
  float/conversion, pointers/memory/struct, all 51 opcodes, AAPCS64 composites/HFA/
  large by-reference composites and indirect returns, `fnptr`/`call_indirect`,
  multi-size and unaligned access, checked build); the freestanding target runtime
  (object table/pointer states/mandatory traps/checked shadow, deterministic I/O,
  allocator, trap and exit), with `aarch64-baremetal` using PL011 UART0 for stdout and framed stdin,
  PL011 UART1 for the exit record and PSCI SYSTEM_OFF (no semihosting); the first
  32-bit target `arm32-baremetal` (Cortex-M7 MPS2 AN500, AAPCS32/ILP32 softfp, an
  independent ARM32 backend for all 51 opcodes, `int64` helpers, software binary64,
  unaligned
  access, all four device policies and qemu-system-arm execution); the second
  32-bit target `riscv32-baremetal` (RV32IMAFDC, ILP32 soft-float ABI, an
  independent RISC-V backend for all 51 opcodes, `__divdi3`/`__udivdi3` 64-bit
  helpers, NS16550A UART0 stdout/framed stdin, a semihosting exit record, the
  SiFive test finisher and qemu-system-riscv32 virt); reset entry with BSS
  clearing before `tc_main`, region bases, static `.data`, ELF/flat artifacts
  with ICF disabled and clang/lld driving; far conditional/switch/return
  transfers use a local skip plus an unconditional transfer (RISC-V
  auipc/jalr); the full VM↔AOT differential/ABI/consistency/artifact/
  determinism suites, bidirectional C-caller/TC-caller AAPCS64/AAPCS32/RISC-V
  ILP32 interoperability, 32/64 profile isolation, the S10 long-term regression
  corpus and release audit, generated ABI/layout/memory/float fuzz suites, and
  implementation-limit/robustness/checked/scale/far-branch regressions.
- **Not implemented**: the optional optimization pipeline (SSA/regalloc/
  inlining); the 0.0.46 baseline stays zero-pass.
- **Current verification**: 99 registered 0.0.47 CTests and 51 closing checks.
  P0–P5, T0–T6, P4b formal acceptance and G11 are complete, and fixed Linux CI
  real-gdb signed evidence across all four profiles passed the release audit.
  The accepted 0.0.46 baseline remains harness 873/873 and coverage 636/636.
  **G0–G11 PASS; G11 completes the 0.0.47 stage exit**. See
  [gate status](docs/gates/README.en.md).

## Documents

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | Sole authority for language semantics, diagnostics, compiler requirements and platform profiles |
| [TC Native Compiler Design 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | Target architecture: flat TAC IR, runtime, platform profiles, backend and phasing |
| [TC Development Test Plan 0.0.46](docs/TC开发测试计划-0.0.46.md) | Stages, gates, coverage and traceability |
| [TC Development and Debugging Toolchain 0.0.47](docs/TC开发调试工具链-0.0.47.md) | Toolchain scope, contracts, milestones and acceptance; P0–P5, T0–T6, P4b formal acceptance and G11 are complete |
| [TC Project Model 0.0.47](docs/TC工程模型-0.0.47.md) | Project layout, `tc.project`, `tc` init/test, root anchor and path rules; P0–P5 and G11 are complete |
| [Documentation map](docs/README.en.md) | Entry point and reading order |

## Scope

- **Both 32-bit and 64-bit are targets**; `usize`/`isize` are platform-word types
  and `ptr<T>`/`fnptr` are `usize` wide.
- **Closed set of 80 diagnostic codes**: 65 `TC_CE_*` + 15 `TC_RE_*`.
- No C host public library, embedding API or ABI; private entry points are the
  `tc-vm`/`tc-aot` compilers and the `tc` project tool, see
  [TC Project Model 0.0.47](docs/TC工程模型-0.0.47.md).

## Repository layout

```text
docs/                 standard, design, test plan, gate status
src/                  0.0.46 implementation
  base/               assert / version / canonical and display path context
  contract/           contract checks / zero-pass policy
  diagnostic/         closed 80-code set / generated table / first diagnostic
  frontend/           input num lex parse module sem ct irgen
  ir/                 type instr operand block ir region object address structs pointer_state print
  runtime/            alloc io trap profile module_loader; semantics/format/input
  vm/                 vm.h interp memory observe gdb gdb_rsp gdb_journal
                      (regions/objects/pointer states/all opcodes/GDB stub)
  backend/            common (target-neutral freestanding runtime core/target
                      description); aarch64 (full backend + semihosting/PL011
                      platform instances); arm32 (Cortex-M7 backend + MPS2
                      platform instance); riscv32 (RISC-V 32 backend + QEMU
                      virt platform instance)
  driver/             tc / tc-vm / tc-aot CLI, AOT driver and test runner
  project/            name validation, manifest/root discovery, path binding,
                      atomic init
tests/                L0–L7: unit, static/runtime conformance, differential/ABI/
                      platform, harness, traceability, golden, determinism
```

## Build and test

```sh
cmake -S . -B .tmp/build
cmake --build .tmp/build
ctest --test-dir .tmp/build --output-on-failure
```

The S7/S8/S9/S10 differential/toolchain tests require `clang`, `llvm-ar` (or the
matching target ar), `ld.lld`, `qemu-system-aarch64`, `qemu-system-arm` and
`qemu-system-riscv32` by default; P4a DWARF verification also needs
`llvm-dwarfdump`, and the T2/T3 real-gdb smoke needs `gdb` (or `gdb-multiarch`).
Configuration fails when a required tool is missing.
`-DTC_ENABLE_AOT_TESTS=OFF` only skips the lld/qemu-dependent ctest entries;
building the target runtimes still requires clang and llvm-ar (or the matching
target ar), and this is not a gate build. Tools currently support:

- `tc --help`/`--version`: project-tool help and dual-version output.
  `tc --init-project [--] <name>` generates the full template in the canonical
  current directory, creates only missing targets, and supports identity
  preflight and failure rollback. `tc --test [--test-timeout=<seconds>] [--]
  [test_name...]` runs `tests/*/main.tc` inside the project root in name-byte
  order, with private snapshots, timeout/stream capture, structured results and
  pass/fail reports; an omitted or zero timeout has no deadline.
- `--syntax-only <source>`: LEX/SYN only; exit 0 means no LEX/SYN diagnostic,
  **not a valid program**.
- `--sem-only <source>`: LEX/SYN/IMPORT/SEM (including CFG/DI, calls, safety and
  annotations); exit 0 still does not claim a valid program, since CT, IR and code
  generation have not run.
- `--static-only <source>`: all static stages through CT and IR finalization; exit 0
  still does not claim a valid program, since code generation has not run.
- A normal `tc-vm` invocation executes the reference VM after the static stages;
  normal termination exits 0, and the first `TC_RE_*` trap is reported as
  `<file>:<line>:<column>: error[RT]: <code>: <print-name>` with exit 1.
  `--profile=w32` selects the 32-bit profile, `--checked` adds UB access checks
  (mandatory traps are unchanged), and `--opt=O0|O2` stays a zero-pass baseline.
  T1 observation supports `--dump-ir[=<path>]`, `--trace=ir,call,mem,io,trap`
  (text/jsonl, limit/output) and `--stats` with a separate output destination.
  T2 adds `--dump-linemap[=<path>]`: JSON v3 follows final global IR instruction
  order, with profile-specific synthetic instruction addresses on the VM.
  T3 adds `--gdb[=<port>]` (default 0, loopback only) with mandatory
  `--gdb-stdout-journal=<dir>`, breakpoints/step/continue, monitor, extended
  restart, an atomic stdout journal and resumable stdin. It is read-only by
  default; `--gdb-allow-write` enables restricted writes.
- A normal `tc-aot` invocation emits target assembly to stdout after the static
  stages. `--emit=asm` (default)/`--emit=obj`/`--emit=elf`/`--emit=flat` with
  `--output=<path>` produce `.s`, an ELF relocatable/executable or a flat binary;
  `--checked` produces a checked build and `--opt=O0|O2` stays the zero-pass
  baseline. `--profile=` selects `aarch64-linux-user`, `aarch64-baremetal`,
  `arm32-baremetal` or `riscv32-baremetal` (`w32` is a VM-only static profile), and
  `--device=reject|read|readwrite|volatile` overrides the platform instance's
  device policy. T1 observation supports `--dump-ir[=<path>]` and `--stats` with a
  separate output destination. T2 adds `--dump-linemap[=<path>]` and `--debug`;
  debug mode emits `.loc`/source comments for every IR instruction and a minimal
  DWARF v4 CU with `.debug_line` in ELF/object artifacts. Flat images have no final
  debug sections. A profile without a backend/toolchain mapping exits 70 without
  publishing a wrong artifact.
- AOT platform instances: `aarch64-linux-user` uses qemu-system-aarch64 +
  semihosting; `aarch64-baremetal` uses PL011 UART0 (stdout/stdin)/UART1 + PSCI;
  `arm32-baremetal` uses qemu-system-arm MPS2 AN500 (semihosting stdout, CMSDK
  UART0 exit record); `riscv32-baremetal` uses qemu-system-riscv32 virt
  (NS16550A UART0 stdout/stdin, a semihosting exit record and the SiFive test
  finisher). A qemu-user/Linux-user runtime is not on the gate path;
  differential tests compare program stdout, the first trap code and the exit
  policy, always within one profile.

Implementation errors never use the language-diagnostic channel. The shared
mapping is `TC_IMPL_OK`→0, `TC_IMPL_USAGE`→2,
`TC_IMPL_IO`/`TC_IMPL_UNAVAILABLE`/`TC_IMPL_OOM`→70, `TC_IMPL_INTERNAL`→71.
OOM prints exactly `<tool>: TC_ERR_OUT_OF_MEMORY: OutOfMemory: memory allocation failed`.
Documented limits: 4 MiB source,
16 KiB token, 256 type/block nesting levels; exceeding them returns
`tc_error_internal` (exit 71). Built-in profiles define their frame region/stack
(default `D`: 512 hosted, 256 `aarch64-baremetal`/`w32`,
32 `arm32-baremetal`, 64 `riscv32-baremetal`). VM frames are host-backed; AOT
additionally enforces `frame_total*(D+1) <= stack_size` before emission and
rejects an oversized frame with the implementation error (exit 71) and no
artifact; this is not a language diagnostic. The ARM32 code window is 1 MiB and
oversized functions are rejected at link time; a 30000-instruction far-branch
regression exists, but larger single functions depend on host memory and the
assembler/linker and are not covered by the gate. Target/ABI/region and
runtime-seam conventions, along with contract and test conventions, are in
[src/CONTRACTS.md](src/CONTRACTS.md) and [tests/README.md](tests/README.md).

## Contributing

- [CONTRIBUTING.en.md](CONTRIBUTING.en.md)
- [SECURITY.en.md](SECURITY.en.md)
- [CODE_OF_CONDUCT.en.md](CODE_OF_CONDUCT.en.md)
- [CHANGELOG.en.md](CHANGELOG.en.md)

## License

See [LICENSE](LICENSE).
