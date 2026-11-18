# TC-Compiler

TC **0.0.46** language/compiler specification and target design for the native
compiler (TC → ASM → machine code). This is the 0.0.46 clean-sheet branch: only
the standard, design, test plan and the implementation rewritten from them are
kept; historical implementation and tools live in git tags.

## Current status

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
- **Current verification**: full `ctest` 71/71; coverage matrix 636/636; G0–G10
  PASS; **G10 completes the 0.0.46 stage exit**. See
  [gate status](docs/gates/README.en.md).

## Documents

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | Sole authority for language semantics, diagnostics, compiler requirements and platform profiles |
| [TC Native Compiler Design 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | Target architecture: flat TAC IR, runtime, platform profiles, backend and phasing |
| [TC Development Test Plan 0.0.46](docs/TC开发测试计划-0.0.46.md) | Stages, gates, coverage and traceability |
| [Documentation map](docs/README.en.md) | Entry point and reading order |

## Scope

- **Both 32-bit and 64-bit are targets**; `usize`/`isize` are platform-word types
  and `ptr<T>`/`fnptr` are `usize` wide.
- **Closed set of 80 diagnostic codes**: 65 `TC_CE_*` + 15 `TC_RE_*`.
- No C host public library, embedding API or ABI; `tc-vm`/`tc-aot` are the only
  tool entry points.

## Repository layout

```text
docs/                 standard, design, test plan, gate status
src/                  0.0.46 implementation
  base/               assert / version
  contract/           contract checks / zero-pass policy
  diagnostic/         closed 80-code set / generated table / first diagnostic
  frontend/           input num lex parse module sem ct irgen
  ir/                 type instr operand block ir region object address structs pointer_state print
  runtime/            alloc io trap profile module_loader; semantics/format/input
  vm/                 vm.h interp memory (regions/objects/pointer states/all opcodes)
  backend/            common (target-neutral freestanding runtime core/target
                      description); aarch64 (full backend + semihosting/PL011
                      platform instances); arm32 (Cortex-M7 backend + MPS2
                      platform instance); riscv32 (RISC-V 32 backend + QEMU
                      virt platform instance)
  driver/             tc-vm / tc-aot CLI and AOT driver
tests/                L0–L7: unit, static/runtime conformance, differential/ABI/
                      platform, harness, traceability, golden, determinism
```

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The S7/S8/S9/S10 differential/toolchain tests require `clang`, `llvm-ar` (or the
matching target ar), `ld.lld`, `qemu-system-aarch64`, `qemu-system-arm` and
`qemu-system-riscv32` by default; configuration fails when they are missing.
`-DTC_ENABLE_AOT_TESTS=OFF` only skips the lld/qemu-dependent ctest entries;
building the target runtimes still requires clang and llvm-ar (or the matching
target ar), and this is not a gate build. Tools currently support:

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
- A normal `tc-aot` invocation emits target assembly to stdout after the static
  stages. `--emit=asm` (default)/`--emit=obj`/`--emit=elf`/`--emit=flat` with
  `--output=<path>` produce `.s`, an ELF relocatable/executable or a flat binary;
  `--checked` produces a checked build and `--opt=O0|O2` stays the zero-pass
  baseline. `--profile=` selects `aarch64-linux-user`, `aarch64-baremetal`,
  `arm32-baremetal` or `riscv32-baremetal` (`w32` is a VM-only static profile), and
  `--device=reject|read|readwrite|volatile` overrides the platform instance's
  device policy. A profile without a backend/toolchain mapping exits 70 without
  publishing a wrong artifact.
- AOT platform instances: `aarch64-linux-user` uses qemu-system-aarch64 +
  semihosting; `aarch64-baremetal` uses PL011 UART0 (stdout/stdin)/UART1 + PSCI;
  `arm32-baremetal` uses qemu-system-arm MPS2 AN500 (semihosting stdout, CMSDK
  UART0 exit record); `riscv32-baremetal` uses qemu-system-riscv32 virt
  (NS16550A UART0 stdout/stdin, a semihosting exit record and the SiFive test
  finisher). A qemu-user/Linux-user runtime is not on the gate path;
  differential tests compare program stdout, the first trap code and the exit
  policy, always within one profile.

Implementation-level file/resource failures use `tc_error_io`/`tc_error_internal`/
`TC_ERR_OUT_OF_MEMORY`, never a language diagnostic. Documented limits: 4 MiB source,
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
runtime-seam conventions are in [src/CONTRACTS.md](src/CONTRACTS.md); contract and
test conventions are in [src/CONTRACTS.md](src/CONTRACTS.md) and
[tests/README.md](tests/README.md).

## Contributing

- [CONTRIBUTING.en.md](CONTRIBUTING.en.md)
- [SECURITY.en.md](SECURITY.en.md)
- [CODE_OF_CONDUCT.en.md](CODE_OF_CONDUCT.en.md)
- [CHANGELOG.en.md](CHANGELOG.en.md)

## License

See [LICENSE](LICENSE).
