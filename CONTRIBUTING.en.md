# Contributing

Thanks for your interest in TC-Compiler. This branch is the **`tc-0.0.47` toolchain branch**: the 0.0.46 clean-sheet baseline (contracts, LEX/SYN, IMPORT, names/types/conversions/layout, static instruction checks, const_expr forms, CFG/DI, call ordering/safety, the annotation matrix, CT evaluation, IR finalization/printing, the runtime semantic core, the reference VM, the full W=64 AArch64, W=32 ARM32 and W=32 RISC-V backends/toolchains, three freestanding/bare-metal platform instances, all four device policies, checked build, the full differential/ABI/consistency/artifact suites, generated ABI fuzz, implementation-limit/scale/far-branch regressions, the long-term regression corpus and the release audit) is implemented and the optional optimization pipeline is not enabled; this branch delivers the development/debug toolchain and the engineering model without changing language semantics (see [TC开发调试工具链-0.0.47.md](docs/TC开发调试工具链-0.0.47.md) and [TC工程模型-0.0.47.md](docs/TC工程模型-0.0.47.md)).

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

## Branch and commits

- Development branch: `tc-0.0.47`.
- Suggested commit subjects: `docs(0.0.47): ...`, `design(0.0.47): ...`, `chore(0.0.47): ...`.
- Do not commit `build/` or generated files.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Builds the private `tc_core` library and the `tc-vm`/`tc-aot` entry points. The tools
support `--syntax-only`/`--sem-only`/`--static-only` partial checks; a normal `tc-vm`
invocation executes the reference VM (exit 0 on normal termination, 1 on the first
trap), while a normal `tc-aot` invocation emits target assembly;
`--emit=asm|obj|elf|flat` drives clang/lld to assembly, ELF or flat artifacts and `--checked`
produces a checked
build. Capabilities outside the current platform instance exit 70
without publishing a wrong artifact. The S7/S8/S9/S10 differential/toolchain
tests need `clang`, `llvm-ar`, `ld.lld`, `qemu-system-aarch64`,
`qemu-system-arm` and `qemu-system-riscv32`.

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
