# Contributing

Thanks for your interest in TC-Compiler. This branch is the **`tc-0.0.47` toolchain branch**: the 0.0.46 clean-sheet baseline (contracts, LEX/SYN, IMPORT, names/types/conversions/layout, static instruction checks, const_expr forms, CFG/DI, call ordering/safety, the annotation matrix, CT evaluation, IR finalization/printing, the runtime semantic core, the reference VM, the full W=64 AArch64, W=32 ARM32 and W=32 RISC-V backends/toolchains, three freestanding/bare-metal platform instances, all four device policies, checked build, the full differential/ABI/consistency/artifact suites, generated ABI fuzz, implementation-limit/scale/far-branch regressions, the long-term regression corpus and the release audit) is implemented and the optional optimization pipeline is not enabled. This branch targets delivery of the development/debug toolchain and project model without changing language semantics (see [TC开发调试工具链-0.0.47.md](docs/TC开发调试工具链-0.0.47.md) and [TC工程模型-0.0.47.md](docs/TC工程模型-0.0.47.md)). Both designs meet the freeze criteria; P0/T0 governance and the three-entry contract, P1 project initialization and manifest/root-discovery APIs, P2 canonical/display paths plus compiler path consumption, the P3 project test runner, P4a AOT line maps/debug information, T1 VM/AOT observation, T2 source mapping and T3 VM GDB debugging are complete. P4b implementation and local real-gdb validation across all four profiles are complete. Fixed Linux CI signed evidence remains pending, so P4b formal acceptance and G11 are incomplete. P5 and T4–T6 have not started.

Chinese: [CONTRIBUTING.md](CONTRIBUTING.md)

By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.en.md) ([中文](CODE_OF_CONDUCT.md)). Report security issues privately per [SECURITY.en.md](SECURITY.en.md) ([中文](SECURITY.md)).

## What to contribute

- 0.0.46 standard errata, ambiguity clarifications, diagnostic/platform-profile revisions.
- Native compiler design: IR, ABI, profiles, roadmap.
- `src/` frozen contracts, module boundaries and front-end interfaces.
- 32/64-bit semantics: `usize`/`isize`, `sizeof`, `bitcast`/`trunc`, layout, and target-dependent rules.

Not accepted:

- Restoring the historical implementation or a C host public library/embedding API.
- Compatibility patches based on old implementation behavior instead of the 0.0.46 standard.
- Large unrelated refactors.

## Documentation first

Language/semantic/diagnostic changes must first update the [TC Language and Compiler Specification 0.0.46](docs/TC语言及编译器标准-0.0.46.md); architecture changes update the [TC Native Compiler Design 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md).
0.0.47 toolchain and project changes follow the two 0.0.47 designs; design acceptance clauses are not evidence of current implementation.

## Branch and commits

- Development branch: `tc-0.0.47`.
- Suggested commit subjects: `docs(0.0.47): ...`, `design(0.0.47): ...`, `chore(0.0.47): ...`.
- Do not commit `build/`, `.tmp/` or generated files.

## Build and test

```sh
cmake -S . -B .tmp/build
cmake --build .tmp/build
ctest --test-dir .tmp/build --output-on-failure
```

Builds the private `tc_core` library and the `tc`/`tc-vm`/`tc-aot` entry points.
The compilers support `--syntax-only`/`--sem-only`/`--static-only` partial checks; a normal `tc-vm`
invocation executes the reference VM (exit 0 on normal termination, 1 on the first
trap), while a normal `tc-aot` invocation emits target assembly;
`--emit=asm|obj|elf|flat` drives clang/lld to assembly, ELF or flat artifacts and
`--checked` produces a checked build. Capabilities outside the current platform
instance exit 70 without publishing a wrong artifact. `tc --init-project`
implements the full template, atomic writes, rollback and identity checks in the
canonical current directory. The compiler consumes manifest parsing/root
discovery, canonical/display paths, includes and profiles. `tc --test` implements
discovery/filtering, fd-anchored beneath resolution, strict sidecars, private
read-only snapshots, fork workers, timeouts, stream capture, structured results,
reports and exit mapping. T1 adds VM/AOT `--dump-ir`, VM
`--trace=ir,call,mem,io,trap` with text/jsonl, limits and output selection, VM/AOT
`--stats`, structured trap context, explicit output identity/reservation guards and
deterministic observation output. T2 adds VM/AOT `--dump-linemap[=<path>]`, public
`tc-aot --debug`, the DWARF v4 CU and a real qemu-gdbstub/gdb smoke test.
T3 adds `tc-vm --gdb[=<port>]` (default 0, loopback only), a mandatory stdout
journal, synthetic IR addresses/`target.xml`, breakpoints/step/continue,
monitor/extended restart, an atomic stdout journal, resumable stdin and optional
restricted writes. P4b adds `tc-vm --gdb-elf-out=<path>`, independent ELF/DWARF
validation for all four profiles, and real-gdb debug ELF, source breakpoints,
64-bit `tc_ir_pc` reads, IR single-step and program-semantic regressions across all
four profiles. Synthetic `tc_ir_pc` is a 64-bit `uint64`, architectural PCs remain
`code_ptr` (64-bit on AArch64, 32-bit on ARM32/RISC-V32), and ARM/RISC-V Z0 kind=2 is accepted. ARM clients need
`set arm force-mode thumb`; the GDB 18.1 ARM `stepi` limitation is covered by
verifying the same RSP step with maintenance packet `s` and a register-cache flush.
The P1/P2/P3/P4a/P4b/T1/T2/T3 test entry points are
`project.manifest`, `project.init`, `project.init_fault`, `project.paths_unit`,
`project.paths`, `project.paths_aot`, `project.test_runner`, `linemap.unit`,
`p4a.debug`, `toolchain.debug_elf`, `toolchain.g11_evidence`, `t1.observe`, `t2.debug`, `t2.gdb`, `gdb_journal.unit`, `gdb_rsp.unit`,
`vm.gdb.protocol`, `vm.gdb.semantics` and `vm.gdb.e2e`, runnable with
`ctest --test-dir .tmp/build -R '^project\.|^linemap\.|^p4a\.|^toolchain\.(debug_elf|g11_evidence)|^t1\.|^t2\.|^gdb_|^vm\.gdb\.'
--output-on-failure`. There are 92 registered tests; G11 evidence collection/validation
and a manual signing job are wired. A real signed artifact from fixed Linux CI
remains pending, so P4b formal acceptance and G11 are incomplete.
The S7/S8/S9/S10 differential/toolchain tests need `clang`, `llvm-ar`, `ld.lld`,
`qemu-system-aarch64`, `qemu-system-arm` and `qemu-system-riscv32`; P4a also needs
`llvm-dwarfdump`, and T2/T3 need `gdb`.

## Issues

Please search existing issues and documents first. Include:

- target document and section (standard/design);
- expected rule vs current text;
- for 32/64 issues, the word size and an example;
- a minimal TC snippet if it clarifies semantics (reproducible with `--static-only`).

## Pull requests

1. Branch from `tc-0.0.47` with a short-lived branch.
2. Keep the change minimal; avoid unrelated formatting.
3. Check internal links and `§` references.
4. Update [CHANGELOG.md](CHANGELOG.md) / [CHANGELOG.en.md](CHANGELOG.en.md) for user-visible changes.
5. For a stage/branch wrap-up, follow the "wrap-up" checklist in [AGENTS.md](AGENTS.md) and pass `ctest -R docs.cleanup`.
