# TC-Compiler

TC **0.0.46** language/compiler specification and target design for the native compiler (TC → ASM → machine code).

This branch is the **0.0.46 clean-sheet branch**: only the 0.0.46 standard, design document, and implementation skeleton remain. The historical implementation, tools, tests, scripts, and examples have been removed. The 0.0.46 compiler is not implemented yet.

## Documents

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | Sole authority for language semantics, diagnostics, compiler requirements, and platform profiles |
| [TC Native Compiler Design 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | Target architecture: flat TAC IR, runtime, platform profiles, backend, and phasing |
| [Documentation map](docs/README.en.md) | Entry point and reading order |

## Scope

- **Both 32-bit and 64-bit are 0.0.46 targets**; `usize`/`isize` are platform-word types, and `ptr<T>`/`fnptr` are `usize` wide. The normative text will be revised for 32/64 as a documentation task.
- **Closed set of 80 diagnostic codes**: 65 `TC_CE_*` + 15 `TC_RE_*`.
- No C host public library, embedding API, or ABI; `tc-vm`/`tc-aot` will be the only tool entry points.
- No historical design or implementation compatibility. Historical code remains in git tags and is not maintained on this branch.

## Repository layout

```text
docs/                0.0.46 standard and design
src/                 0.0.46 implementation skeleton (no implementation yet)
  frontend/           lex / parse / module / resolve / analyze / consteval / diagnostic
  ir/                 type / instr / block / cfg / region / address / print
  runtime/            sem_int / sem_fp / sem_cast / sem_bitwise / io / sink / alloc / trap / profile
  vm/                 interp / memory / pointer_state
  backend/            aarch64 / arm32 / riscv32 / common
  driver/             tc-vm / tc-aot
tests/                conformance test placeholder (to be built)
```

## Build

Only the skeleton exists; there are no compiler targets yet:

```sh
cmake -S . -B build
cmake --build build
```

A successful build does not mean `tc-vm`/`tc-aot` exist; implementation starts from the 0.0.46 design.

## Contributing

- [CONTRIBUTING.en.md](CONTRIBUTING.en.md) ([中文](CONTRIBUTING.md))
- [SECURITY.en.md](SECURITY.en.md) ([中文](SECURITY.md))
- [CODE_OF_CONDUCT.en.md](CODE_OF_CONDUCT.en.md) ([中文](CODE_OF_CONDUCT.md))
- [CHANGELOG.en.md](CHANGELOG.en.md) ([中文](CHANGELOG.md))

## License

See [LICENSE](LICENSE).
