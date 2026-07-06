# TC-Compiler

The implementation project of the **TC** language (C99). Includes **libtc** (shared static library for compilation/execution), **TC-VM** (a direct execution engine), and **TC-AOT** (ahead-of-time compilation from `.tc` to C99 source code).

Version: **v0.0.23** (`src/vm/driver/tc_version.h`)

## Directory Structure

```text
docs/                  Language specification, VM/AOT/lib design documents
src/
├── libtc/             Shared static library (compile + execute pipeline, CMakeLists.txt)
├── vm/                TC-VM source (lexer / parser / analyzer / executor / runtime)
└── aot/               TC-AOT source (codegen / rt shim / CLI)
tests/
├── valid/             Conformance tests (56, covering bool/let/I/O/format etc.)
├── errors/            Error tests (52 static + 31 runtime)
├── unit/              C unit tests (lexer / runtime)
└── stress/            Stress tests
scripts/
├── vm/                VM test scripts
├── aot/               AOT differential test scripts
├── sync/              RHS coverage checker (check_rhs_coverage.py)
└── run_tests.sh       Unified test entry (recommended)
build/                 Build artifacts (git ignored)
├── vm/bin/tc-vm       VM executable
└── aot/bin/tc-aot     AOT executable
```

## Implemented Features

| Category | Features |
|----------|----------|
| Type System | `int8` / `int16` / `int32` / `int64` / `uint8` / `uint16` / `uint32` / `uint64` / `bool` |
| Literals | Decimal, hex (`0x`/`0X`), octal (`0o`/`0O`), binary (`0b`/`0B`), digit separators (`_`) |
| Variables | `var` declarations (optional init), `let` constants (compile-time evaluation) |
| Arithmetic | `add` / `sub` / `mul` / `div` / `mod`, strict (overflow error) and wrap modes |
| Comparison | `eq` / `neq` / `lt` / `gt` / `le` / `ge` |
| Logic | `and` / `or` (short-circuit), `not` |
| Unary | `abs` / `neg` |
| Cast | `cast` with truncate / strict / widen modes |
| I/O | `write` / `writeln` / `read`, format specifiers (`d`/`i`/`u`/`x`/`X`/`o`/`b`/`t`) |
| Constant Folding | Compile-time evaluation of `let` initializers (arith/compare/logic/cast) |
| REPL | Interactive line-by-line execution with persistent variables |

## Build

The build is managed by **CMake**; the root `Makefile` is a thin wrapper around CMake.

### Makefile (Recommended)

```sh
make                    # Configure and build all (libtc + VM + AOT)
make vm                 # Build VM only
make aot                # Build AOT only
make test               # Run all tests (VM + AOT + unit)
make test-vm            # VM conformance tests
make test-aot           # AOT differential tests
make test-unit          # C unit tests
make bench              # Performance benchmarks
make clean              # Remove build/ directory
```

### CMake (Equivalent Commands)

```sh
cmake -S . -B build
cmake --build build                       # Build all
cmake --build build --target tc-vm        # Build VM only
cmake --build build --target tc-aot       # Build AOT only
cmake --build build --target check-vm     # VM conformance tests
cmake --build build --target check-aot    # AOT differential tests
cmake --build build --target check-unit   # C unit tests
cmake --build build --target check        # All tests
```

### AddressSanitizer Mode

```sh
cmake -S . -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address -g"
cmake --build build-asan
bash scripts/run_tests.sh --asan
```

## Usage

### File Mode

```sh
./build/vm/bin/tc-vm tests/valid/example.tc
./build/vm/bin/tc-vm --check tests/valid/example.tc   # Static analysis only
./build/vm/bin/tc-vm --help                           # View usage
```

### Interactive REPL

```sh
./build/vm/bin/tc-vm --repl        # Start interactive REPL
./build/vm/bin/tc-vm -i            # Same (short option)
```

The REPL supports entering TC statements one by one with immediate execution; variables persist across lines. Built-in meta-commands include `:quit` (exit), `:reset` (clear variables), `:vars` (list variables), and `:help` (help).

### AOT Mode

```sh
./build/aot/bin/tc-aot source.tc                      # Transpile to C99 (output: source.tc.c)
./build/aot/bin/tc-aot -o output.c source.tc           # Specify output path
./build/aot/bin/tc-aot -r source.tc                    # Compile and run generated C code
./build/aot/bin/tc-aot --check source.tc               # Static analysis only
./build/aot/bin/tc-aot --help                          # View usage
```

## Documents

| Document | Description |
|----------|-------------|
| [TC Language Specification (Chinese)](docs/TC语言标准设计说明书.md) | Authoritative definition of TC syntax and semantics (v0.0.21) |
| [TC-VM Design Document (Chinese)](docs/TC-VM详细设计说明书.md) | Direct execution engine architecture and implementation (v0.0.21) |
| [TC-VM Command Reference (Chinese)](docs/TC-VM命令行参考.md) | CLI instructions for tc-vm (v0.0.21) |
| [libtc Embedding API](docs/libtc-api.md) | Embedding programming interface for the libtc static library |

Implementation behavior follows the language specification; VM / AOT design documents define each backend's architecture.

## Performance Profiling

Set `TC_BENCH=1` to output phase timing to stderr:

```sh
TC_BENCH=1 ./build/vm/bin/tc-vm tests/valid/example.tc
```

Use with `scripts/vm/bench.sh` for local regression comparison.

## Embedding libtc

libtc provides a "compile (Parse + Analyze)" and "execute" separated API:

```c
#include "tc_lib.h"

TcDiagnostic diag;
TcTypedProgram program;

tc_diagnostic_init(&diag);
if (tc_compile_source("var x: int32 = 1\nwriteln(int32, x)\n", &program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    return 1;
}
if (tc_run_typed(&program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    tc_typed_program_free(&program);
    return 1;
}
tc_typed_program_free(&program);
tc_diagnostic_clear(&diag);
```

See [docs/libtc-api.md](docs/libtc-api.md) for details.

## Testing

### Unified Entry (Recommended)

```sh
bash scripts/run_tests.sh                          # Run all tests
bash scripts/run_tests.sh --filter foo             # VM tests matching "foo" only
bash scripts/run_tests.sh --verbose                # Verbose logging
bash scripts/run_tests.sh --asan                   # AddressSanitizer mode
```

### Equivalent Makefile

```sh
make test
```

### CI

GitHub Actions workflow at `.github/workflows/ci.yml` runs on push / PR:

- **Dual platform**: Ubuntu + macOS
- **Pipeline**: VM conformance -> RHS coverage check -> AOT differential -> Codecov
- Each run covers **550+** check points (VM ~198 + unit ~404 + AOT ~22 + RHS check)

## Author

- **唐荣兵** ([yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)) — Project creator and maintainer
