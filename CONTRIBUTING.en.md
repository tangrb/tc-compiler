# Contributing

Thanks for your interest in TC-Compiler. This branch is the **`tc-0.0.47` toolchain branch**: the 0.0.46 clean-sheet baseline (contracts, LEX/SYN, IMPORT, names/types/conversions/layout, static instruction checks, const_expr forms, CFG/DI, call ordering/safety, the annotation matrix, CT evaluation, IR finalization/printing, the runtime semantic core, the reference VM, the full W=64 AArch64, W=32 ARM32 and W=32 RISC-V backends/toolchains, three freestanding/bare-metal platform instances, all four device policies, checked build, the full differential/ABI/consistency/artifact suites, generated ABI fuzz, implementation-limit/scale/far-branch regressions, the long-term regression corpus and the release audit) is implemented and the optional optimization pipeline is not enabled. This branch targets delivery of the development/debug toolchain and project model without changing language semantics (see [TC开发调试工具链-0.0.47.md](docs/TC开发调试工具链-0.0.47.md) and [TC工程模型-0.0.47.md](docs/TC工程模型-0.0.47.md)). Both designs meet the freeze criteria; P0/T0 governance and the three-entry contract, P1 project initialization and manifest/root-discovery APIs, P2 canonical/display paths plus compiler path consumption, the P3 project test runner, and P4a AOT line maps/debug information are complete. T1 VM/AOT observation is also complete, and G11 is incomplete. P4b, P5 and T2–T6 have not started; public `tc-aot --debug`/`--dump-linemap` has not started.

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
- Do not commit `build/` or generated files.

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
deterministic observation output. The P1/P2/P3/P4a/T1 test entry points are
`project.manifest`, `project.init`, `project.init_fault`, `project.paths_unit`,
`project.paths`, `project.paths_aot`, `project.test_runner`, `linemap.unit`,
`p4a.debug` and `t1.observe`, runnable with
`ctest --test-dir .tmp/build -R '^project\.|^linemap\.|^p4a\.|^t1\.'
--output-on-failure`. There are 83 registered tests; Debug, Release and ASan+UBSan
full CTest runs each passed 83/83, and all closing checks passed.
The S7/S8/S9/S10 differential/toolchain tests need `clang`, `llvm-ar`, `ld.lld`,
`qemu-system-aarch64`, `qemu-system-arm` and `qemu-system-riscv32`; P4a also needs
`llvm-dwarfdump`.

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
