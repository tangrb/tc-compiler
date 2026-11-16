# Contributing

Thanks for your interest in TC-Compiler. This branch is the **0.0.46 clean-sheet branch**: contracts, LEX/SYN, IMPORT, names/types/conversions/layout, static instruction checks, const_expr forms, CFG/DI, call ordering/safety, the annotation matrix, CT evaluation and IR finalization/printing are implemented; the VM, runtime semantics and native backend are still to be built.

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

- Development branch: `tc-0.0.46`.
- Suggested commit subjects: `docs(0.0.46): ...`, `design(0.0.46): ...`, `chore(0.0.46): ...`.
- Do not commit `build/` or generated files.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Builds the private `tc_core` library and the `tc-vm`/`tc-aot` entry points. The tools support `--syntax-only` and `--static-only` partial checks; a normal well-formed invocation exits 70 after the static stages.

## Issues

Please search existing issues and documents first. Include:

- target document and section (standard/design);
- expected rule vs current text;
- for 32/64 issues, the word size and an example;
- a minimal TC snippet if it clarifies semantics (reproducible with `--static-only`).

## Pull requests

1. Branch from `tc-0.0.46` with a short-lived branch.
2. Keep the change minimal; avoid unrelated formatting.
3. Check internal links and `§` references.
4. Update [CHANGELOG.md](CHANGELOG.md) / [CHANGELOG.en.md](CHANGELOG.en.md) for user-visible changes.
5. For a stage/branch wrap-up, follow the "wrap-up" checklist in [AGENTS.md](AGENTS.md) and pass `ctest -R docs.cleanup`.
