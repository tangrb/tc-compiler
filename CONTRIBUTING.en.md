# Contributing

Thanks for your interest in TC-Compiler. This branch is the **0.0.46 clean-sheet branch**: only the standard, design document, and implementation skeleton are maintained; the historical implementation has been removed.

Chinese: [CONTRIBUTING.md](CONTRIBUTING.md)

By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.en.md) ([中文](CODE_OF_CONDUCT.md)). Report security issues privately per [SECURITY.en.md](SECURITY.en.md) ([中文](SECURITY.md)).

## What to contribute

- 0.0.46 standard errata, ambiguity clarifications, diagnostic/platform-profile revisions.
- Native compiler design: IR, ABI, profiles, roadmap.
- `src/` skeleton module boundaries and interfaces.
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

## Skeleton build

```sh
cmake -S . -B build
cmake --build build
```

There are no compiler targets yet; a successful build only means the skeleton configures.

## Issues

Please search existing issues and documents first. Include:

- target document and section (standard/design);
- expected rule vs current text;
- for 32/64 issues, the word size and an example;
- a minimal TC snippet if it clarifies semantics (no compiler exists yet).

## Pull requests

1. Branch from `tc-0.0.46` with a short-lived branch.
2. Keep the change minimal; avoid unrelated formatting.
3. Check internal links and `§` references.
4. Update [CHANGELOG.md](CHANGELOG.md) / [CHANGELOG.en.md](CHANGELOG.en.md) for user-visible changes.
