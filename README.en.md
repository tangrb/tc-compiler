# TC-Compiler

TC-Compiler is a TC language toolchain implemented in C99. It includes:

- **libtc**: an embeddable static library for compilation, static analysis, and execution;
- **TC-VM**: a command-line tool that directly executes TC source files;
- **TC-AOT**: an ahead-of-time compiler that transpiles TC source into strict C99.

Current version: **v0.0.35**. The [TC Language Specification](docs/TC语言标准设计说明书-0.0.35.md) is the sole authority for language syntax and observable semantics.

## Quick Start

Requirements: a C99 compiler, CMake, and Make. TC-AOT `--run` mode also requires a host `cc` command.

```sh
make
./build/vm/bin/tc-vm tests/valid/example.tc
```

Check source without executing it:

```sh
./build/vm/bin/tc-vm --check tests/valid/example.tc
```

Transpile TC to C99:

```sh
./build/aot/bin/tc-aot tests/valid/example.tc
# Produces tests/valid/example.c
```

Run the complete standard regression suite:

```sh
bash scripts/run_tests.sh
```

The current release baseline is **459 VM + 1726 unit + 272 AOT**, for 2,457 test assertions or scenarios, with zero failures in all three groups.

## Implemented Capabilities

| Category | Current capabilities |
| -------- | -------------------- |
| Types | `int8` / `int16` / `int32` / `int64`, corresponding unsigned integers, `bool`, `float32`, and `float64` |
| Literals | Decimal, hexadecimal, octal, binary, digit separators, scientific notation, `f` suffix, and `inf` / `nan` |
| Bindings | Mandatory-initialized `var`; compile-time `let` with no runtime slot |
| Operations | Integer and floating-point arithmetic, comparisons, short-circuit logic, unary operations, bitwise operations, and shifts |
| Conversions | Strict numeric `cast`, integer-narrowing `truncate`, and equal-width non-`bool` `bitcast` |
| Control flow | `if-then-else-end`, `while-then-end`, innermost `break` / `continue`, and restricted `goto` / `label` |
| Static analysis | Lexical scope, complete CFG, reachability, static boolean pruning, and fixed-point definite initialization |
| I/O | `write` / `writeln` / `read` with 13 integer, boolean, and floating-point format specifiers |
| Backend consistency | VM, AOT, and `let` reuse shared numeric and I/O semantics; AOT differentials lock observable results |
| Modules/functions | `#program`/`#lib`, `import`, `func`/`funcall`/`return`, acyclic call graph, `static var`/`let` |
| Compound types | `ptr<T>`, `memblock<T,N>` (struct static checks landed; struct runtime pending) |

Version 0.0.31 does not include functions, arrays, structures, pointers, strings, a module system, JIT, or a bytecode file format. Structured `while` bodies cannot contain `goto` / `label`; this is a language specification boundary.

## Build

The root Makefile is a convenience interface to CMake.

```sh
make                    # Configure and build all default targets: libtc, TC-VM, TC-AOT
make vm                 # Currently equivalent to make; builds all default targets
make aot                # Build TC-AOT and its dependencies
make clean              # Remove build/
```

To build an exact CMake target:

```sh
cmake -S . -B build
cmake --build build --target tc-vm
cmake --build build --target tc-aot
cmake --build build --target libtc
```

The project compiles with `-std=c99 -Wall -Wextra -pedantic`; AOT differential tests additionally use `-Werror` for generated C.

## Usage

### TC-VM

```sh
./build/vm/bin/tc-vm program.tc          # Compile and execute
./build/vm/bin/tc-vm --check program.tc  # Compile and statically analyze only
./build/vm/bin/tc-vm -I ./lib program.tc # Add module search paths
./build/vm/bin/tc-vm --help
./build/vm/bin/tc-vm --version
```

See the [TC-VM Command Reference](docs/TC-VM命令行参考-0.0.35.md) for complete behavior.

### TC-AOT

```sh
./build/aot/bin/tc-aot source.tc             # Generate source.c
./build/aot/bin/tc-aot -o output.c source.tc # Select the C output path
./build/aot/bin/tc-aot --check source.tc      # Static analysis only; emit no C
./build/aot/bin/tc-aot --run source.tc        # Generate, compile with host cc, and run
./build/aot/bin/tc-aot --help
```

`--run` depends on a host C99 toolchain. Pure code generation and `--check` do not make host compiler availability a condition of TC language conformance.

## Embedding libtc

libtc uses a success-only ownership contract: after successful compilation, the caller must free the `TcTypedProgram`; after compilation failure, the caller did not acquire output ownership and must not free that output.

```c
#include <stdio.h>

#include "tc_lib.h"

int main(void) {
    const char *source =
        "#program\n"
        "var x: int32 = 1\n"
        "writeln(int32, %d, x)\n";
    TcDiagnostic diag;
    TcTypedProgram program;

    tc_diagnostic_init(&diag);

    if (tc_compile_source(source, "example.tc", &program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (tc_run_program(&program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_typed_program_free(&program);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
    return 0;
}
```

The public entry points are `tc_compile_source`, `tc_compile_file`, `tc_set_module_search_paths`, and `tc_run_program`. See the [libtc Embedding API](docs/libtc-api-0.0.35.md) for complete ownership, diagnostics, and build details.

## Tests and Quality Gates

### Standard Regression

```sh
bash scripts/run_tests.sh                # VM + AOT + unit
bash scripts/run_tests.sh --filter foo   # Filters VM only; AOT and unit still run in full
bash scripts/run_tests.sh --verbose      # Adds VM logging only
```

The groups can also be run separately:

```sh
make test-vm
make test-unit
make test-aot
make test
```

Current standard regression size:

| Test group | Size | Primary coverage |
| ---------- | ---: | ---------------- |
| VM conformance | 459 | Execution, `--check`, diagnostics, REPL, and stress scenarios |
| C unit | 1726 | 16 targets covering lexer, parser, semantics, CFG, analyzer, libtc, executor, and more |
| AOT differential | 272 | VM/AOT stdout, static acceptance, runtime errors, I/O, and bit-pattern differentials |

These numbers are assertions or scenarios reported by the test scripts, not source-file counts.

### Static Structure Checks

```sh
python3 scripts/sync/check_rhs_coverage.py
python3 scripts/sync/check_source_naming.py
```

### Sanitizers and Memory Checks

```sh
bash scripts/run_asan_all.sh                 # ASan build and full VM/AOT/unit matrix

make build-ubsan
bash scripts/run_tests.sh --ubsan            # UBSan VM; AOT/unit use the standard build

make test-valgrind                           # Linux; Valgrind VM + standard AOT/unit
make memcheck-macos                          # macOS; MallocScribble + leaks
```

`bash scripts/run_tests.sh --asan`, `--ubsan`, `--valgrind`, and `--leaks` pass the mode only to VM tests; AOT and unit still use the standard build. Use `scripts/run_asan_all.sh` when a complete ASan matrix is required.

### Local and Remote CI

```sh
make ci                  # Build, three test groups, RHS coverage, and source naming
make ci-coverage         # Also generate a coverage report
```

The local entry point is implemented by `scripts/ci.sh` and matches the core five stages in `.github/workflows/ci.yml`. GitHub Actions additionally runs:

- the standard Ubuntu and macOS matrix;
- the no-fenv floating-point fallback;
- benchmark regression;
- a coverage artifact;
- a separate Ubuntu ASan workflow.

Coverage HTML is written to `build-coverage/coverage_html/index.html`.

## Performance Observation

When `TC_BENCH` is set, parse, analyze, and execute timings are written to stderr:

```sh
TC_BENCH=1 ./build/vm/bin/tc-vm tests/valid/example.tc
```

Local benchmarks:

```sh
make bench
sh scripts/vm/bench.sh --check
```

Regression limits are stored in `tests/stress/bench_limits.txt`.

## Project Structure

```text
docs/               Formal language, implementation, CLI, and API documents
src/
├── libtc/          Embeddable compile/execute library
├── vm/             lexer, parser, analyzer, executor, runtime, and driver
└── aot/            C99 codegen, runtime shim, and CLI
tests/
├── valid/          Valid programs and observable output
├── errors/         Static and runtime errors
├── unit/           C unit tests
└── stress/         Stress and performance scenarios
scripts/
├── vm/             VM regression and benchmarks
├── aot/            AOT differential tests
└── sync/           RHS dispatch and source naming checks
```

## Documentation

| Document | Responsibility |
| -------- | -------------- |
| [TC Language Specification](docs/TC语言标准设计说明书-0.0.35.md) | Sole authority for 0.0.31 syntax, semantics, and diagnostics |
| [TC-VM Command Reference](docs/TC-VM命令行参考.md) | `tc-vm` usage, output, and exit behavior |
| [libtc Embedding API](docs/libtc-api.md) | Quick reference for public functions, ownership, and diagnostics |
| [TC-VM Design Document](docs/TC-VM详细设计说明书.md) | VM pipeline, IR, CFG, executor, and REPL design |
| [TC-AOT Design Document](docs/TC-AOT详细设计说明书.md) | C99 generation, runtime shim, and differential verification |
| [libtc Design Document](docs/libtc设计说明书.md) | libtc architecture, transactions, lifecycle, and error contract |
| [Design–Implementation Conformance Report](docs/设计实现合规审查报告.md) | The 48-item 0.0.31 conformance matrix and release evidence |

## Git Hooks

Optionally install the repository hooks:

```sh
make hooks
# Or: bash scripts/install-git-hooks.sh
```

## Author

**唐荣兵** ([yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)) — Project creator and maintainer
