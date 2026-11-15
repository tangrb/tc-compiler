# TC-Compiler

TC **0.0.46** language/compiler specification and target design for the native compiler (TC → ASM → machine code).

This branch is the **0.0.46 clean-sheet branch**. The S0 contract layer/test infrastructure and the **S1 LEX/SYN front end (flat parse IR)** are implemented (see the [G1 gate report](docs/TC-G1门禁报告-0.0.46.md) for evidence/status); IMPORT/SEM/CT, the VM, and the native backend start in S2. See [src/CONTRACTS.md](src/CONTRACTS.md) and the [G0 gate report](docs/TC-G0门禁报告-0.0.46.md).

## Documents

| Document | Purpose |
| -------- | ------- |
| [TC Language and Compiler Specification 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | Sole authority for language semantics, diagnostics, compiler requirements, and platform profiles |
| [TC Native Compiler Design 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | Target architecture: flat TAC IR, runtime, platform profiles, backend, and phasing |
| [Documentation map](docs/README.en.md) | Entry point and reading order |

## Scope

- **Both 32-bit and 64-bit are 0.0.46 targets**; `usize`/`isize` are platform-word types, and `ptr<T>`/`fnptr` are `usize` wide. The standard and design have been revised for both 32/64 profiles.
- **Closed set of 80 diagnostic codes**: 65 `TC_CE_*` + 15 `TC_RE_*`.
- No C host public library, embedding API, or ABI; `tc-vm`/`tc-aot` are the only tool entry points. `--syntax-only` runs LEX/SYN only (exit 0 means no lexical/syntax diagnostic, not that the program is valid); a normal invocation still exits 70 after LEX/SYN because later phases are unavailable.
- No historical design or implementation compatibility. Historical code remains in git tags and is not maintained on this branch.

## Repository layout

```text
docs/                0.0.46 standard and design
src/                 0.0.46 implementation (S0 contract layer)
  base/               assert / version
  contract/           S0 contract check / zero-pass policy
  diagnostic/         closed 80-code table / first-diagnostic selection
  frontend/           input / num / lex / parse (S1); module / resolve / analyze / consteval (S2+)
  ir/                 type / instr / operand / block / span / region / object / address / pointer_state
  runtime/            alloc / io / trap / profile / module_loader (sem_* from S6)
  vm/                 interp / memory / pointer_state (S6)
  backend/            aarch64 / arm32 / riscv32 / common (S7+)
  driver/             tc-vm / tc-aot + shared CLI/profile selection (S1 front end)
tests/                L0/L1 tests, L3 conformance, harness, traceability, golden and determinism gates
```

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

S0/S1 build the private `tc_core` library plus `tc-vm`/`tc-aot`. `--syntax-only` performs LEX/SYN (exit 0 = no lexical/syntax diagnostic, a partial check); a normal invocation exits 70 after LEX/SYN because IMPORT/SEM/CT/code generation are unavailable. Documented S1 limits: 4 MiB source, 16 KiB token (identifier/annotation/float), 256 nesting levels; exceeding them returns `tc_error_internal` (exit 71), never a language diagnostic.

## Contributing

- [CONTRIBUTING.en.md](CONTRIBUTING.en.md) ([中文](CONTRIBUTING.md))
- [SECURITY.en.md](SECURITY.en.md) ([中文](SECURITY.md))
- [CODE_OF_CONDUCT.en.md](CODE_OF_CONDUCT.en.md) ([中文](CODE_OF_CONDUCT.md))
- [CHANGELOG.en.md](CHANGELOG.en.md) ([中文](CHANGELOG.md))

## License

See [LICENSE](LICENSE).
