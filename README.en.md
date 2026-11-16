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
  initializers), IR finalization/freeze and the deterministic printer; 32/64 static profiles.
- **Not implemented**: the VM, runtime semantics and native backends.
- Gates: G0–G5 PASS; **G5 allows S6**. See [gate status](docs/gates/README.en.md).

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
  runtime/            alloc io trap profile module_loader
  vm/                 interp (later)
  backend/            aarch64 arm32 riscv32 common (later)
  driver/             tc-vm / tc-aot CLI
tests/                L0/L1, L3 conformance, harness, traceability, golden, determinism
```

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Tools currently support:

- `--syntax-only <source>`: LEX/SYN only; exit 0 means no LEX/SYN diagnostic,
  **not a valid program**.
- `--sem-only <source>`: LEX/SYN/IMPORT/SEM (including CFG/DI, calls, safety and
  annotations); exit 0 still does not claim a valid program, since CT, IR and code
  generation have not run.
- `--static-only <source>`: all static stages through CT and IR finalization; exit 0
  still does not claim a valid program, since code generation has not run.
- A normal invocation explicitly exits 70 after the static stages.

Implementation-level file/resource failures use `tc_error_io`/`tc_error_internal`/
`TC_ERR_OUT_OF_MEMORY`, never a language diagnostic. Documented limits: 4 MiB source,
16 KiB token, 256 type/block nesting levels; exceeding them returns
`tc_error_internal` (exit 71). Contract and test conventions are in
[src/CONTRACTS.md](src/CONTRACTS.md) and [tests/README.md](tests/README.md).

## Contributing

- [CONTRIBUTING.en.md](CONTRIBUTING.en.md)
- [SECURITY.en.md](SECURITY.en.md)
- [CODE_OF_CONDUCT.en.md](CODE_OF_CONDUCT.en.md)
- [CHANGELOG.en.md](CHANGELOG.en.md)

## License

See [LICENSE](LICENSE).
