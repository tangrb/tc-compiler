# TC-Compiler

TC-Compiler is a TC language toolchain implemented in C99. It includes:

- **libtc**: an embeddable static library for compilation, static analysis, and execution;
- **TC-VM**: a command-line tool that directly executes TC source files;
- **TC-AOT**: an ahead-of-time compiler that transpiles TC source into strict C99;
- **TC-Embed**: a zero-copy embedded runtime for C host programs calling TC compilation artifacts (new in v0.0.36).

Current core version: **v0.0.35**, Embed module version: **v0.0.36**. The [TC Language Specification](docs/TC语言标准设计说明书-0.0.35.md) is the sole authority for language syntax and observable semantics.

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

## Implemented Capabilities

| Category | Current capabilities |
| -------- | -------------------- |
| Types | `int8` / `int16` / `int32` / `int64`, corresponding unsigned integers, `bool`, `float32`, `float64`, `isize` / `usize` (platform word size) |
| Literals | Decimal, hexadecimal, octal, binary, digit separators, scientific notation, `f` suffix, and `inf` / `nan` |
| Bindings | Mandatory-initialized `var`; compile-time `let` with no runtime slot |
| Operations | Integer and floating-point arithmetic, comparisons, short-circuit logic, unary operations, bitwise operations, and shifts |
| Conversions | Strict numeric `cast`, integer-narrowing `truncate`, and equal-width non-`bool` `bitcast` |
| Control flow | `if-then-else-end`, `while-then-end`, innermost `break` / `continue`, and restricted `goto` / `label` |
| Static analysis | 13-stage deterministic compilation pipeline, lexical scope, complete CFG, reachability, static boolean pruning, and fixed-point definite initialization |
| I/O | `write` / `writeln` / `read` with 13 integer, boolean, and floating-point format specifiers |
| Backend consistency | VM, AOT, and `let` reuse shared numeric and I/O semantics; AOT differentials lock observable results |
| Modules/functions | `#program`/`#lib`, `import`, `func`/`funcall`/`return`, acyclic call graph, `static var`/`let` |
| Compound types | `ptr<T>`, `memblock<T,N>`, `struct` (constructors / field r/w / deep copy; VM + AOT) |
| Embed interop | C→TC zero-copy function calls, shared `slots[]` data plane, `ptr<T>` handle encoding, symbol lookup; API-compatible VM and AOT dual mode (v0.0.36) |

Version 0.0.35 does not include strings, a bytecode file format, or JIT.

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
./build/aot/bin/tc-aot source.tc             # Generate source.c (standard mode, with main())
./build/aot/bin/tc-aot -o output.c source.tc # Select the C output path
./build/aot/bin/tc-aot --check source.tc      # Static analysis only; emit no C
./build/aot/bin/tc-aot --run source.tc        # Generate, compile with host cc, and run
./build/aot/bin/tc-aot --embed source.tc      # Embed library mode: no main(), public symbols + func table
./build/aot/bin/tc-aot --embed -H out.h source.tc  # Embed mode + generate host header file
./build/aot/bin/tc-aot --help
```

`--run` depends on a host C99 toolchain. Pure code generation and `--check` do not make host compiler availability a condition of TC language conformance. `--embed` and `--run` are mutually exclusive.

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

## Embedding TC-Embed (v0.0.36)

TC-Embed provides C host programs with zero-copy calling of TC compilation artifacts. C and TC share the same `TcValue slots[]` array, and the `ptr<T>` slot encoding `(slot << 1) | 1` serves as the unified handle for passing variable references between C and TC.

Core headers: `src/vm/embed/tc_embed.h` + `src/vm/embed/tc_value_bridge.h`.

### VM Mode

Compile TC source via libtc, then create an embed context with `tc_embed_create`:

```c
#include "tc_embed.h"
#include "tc_value_bridge.h"
#include "tc_lib.h"

int main(void) {
    const char *src =
        "#lib\n"
        "func add(a: int32, b: int32) -> int32\n"
        "    return(a + b)\n"
        "end\n";
    TcDiagnostic diag;
    TcTypedProgram prog;

    tc_diagnostic_init(&diag);
    if (tc_compile_source(src, "add.tc", &prog, &diag) != 0) { /* handle */ }

    TcEmbedCtx *ctx = tc_embed_create(&prog, &diag);

    /* Look up function info */
    const TcEmbedFuncInfo *info = tc_embed_func_info(ctx, NULL, "add");

    /* Construct arguments and call */
    TcValue args[] = { tc_value_from_int32(3), tc_value_from_int32(4) };
    TcValue result;
    tc_embed_call(ctx, NULL, "add", 2, args, &result);

    int32_t ret;
    tc_value_to_int32(result, &ret);
    printf("3 + 4 = %d\n", ret);  /* Prints: 3 + 4 = 7 */

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
    return 0;
}
```

### AOT Mode

Transpile TC source via `tc-aot --embed` into embed-library C code, compile with host `cc` as a shared library, and load via `tc_embed_create_aot`. The same `tc_embed_call` / `tc_embed_slot_*` / `tc_embed_ptr_*` API is compatible across VM and AOT modes.

```sh
# Generate embed library C code and header
./build/aot/bin/tc-aot --embed -o mylib.c -H mylib.h mylib.tc
# Compile as a shared library, link with host program
cc -shared -o libmylib.so mylib.c src/vm/runtime/tc_sem_*.c -I src/vm/runtime
```

### Value Bridging

`tc_value_bridge.h` provides `tc_value_from_*` / `tc_value_to_*` pure inline helper functions covering `int8`–`int64`, `uint8`–`uint64`, `float32`, `float64`, and `bool`, with no extra compilation units or runtime overhead.

```c
TcValue v = tc_value_from_int64(42);
int64_t x;
tc_value_to_int64(v, &x);
```

See the [TC-Embed Design Document](docs/TC-Embed详细设计说明书-0.0.36.md) for the complete API design.

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
├── vm/
│   ├── lexer/      Lexer
│   ├── parser/     Parser
│   ├── analyzer/   Static analysis (CFG, type checking, function/call graph)
│   ├── executor/   Executor and call frames
│   ├── runtime/    Runtime (types, semantics, I/O, symbols, diagnostics)
│   ├── embed/      TC-Embed embedded runtime (v0.0.36)
│   └── driver/     Entry point and version
└── aot/            C99 codegen, runtime shim, CLI, embed-mode runtime
tests/
├── valid/          Valid programs and observable output
├── errors/         Static and runtime errors
├── vm/embed/       TC-Embed integration tests
├── unit/           C unit tests (27 targets, incl. check-embed / check-embed-aot)
├── modules/        Module system tests
└── stress/         Stress and performance scenarios
scripts/
├── vm/             VM regression and benchmarks
├── aot/            AOT differential and embed codegen tests
└── sync/           RHS dispatch and source naming checks
```

## Documentation

| Document | Responsibility |
| -------- | -------------- |
| [TC Language Specification](docs/TC语言标准设计说明书-0.0.35.md) | Sole authority for 0.0.35 syntax, semantics, and diagnostics |
| [TC Compiler Specification](docs/TC编译器标准设计说明书-0.0.35.md) | 13-stage pipeline, diagnostic priority, and call-graph spec |
| [TC-VM Command Reference](docs/TC-VM命令行参考-0.0.35.md) | `tc-vm` usage, output, and exit behavior |
| [libtc Embedding API](docs/libtc-api-0.0.35.md) | Quick reference for public functions, ownership, and diagnostics |
| [TC-VM Design Document](docs/TC-VM详细设计说明书-0.0.35.md) | VM pipeline, IR, CFG, and executor design |
| [TC-AOT Design Document](docs/TC-AOT详细设计说明书-0.0.35.md) | C99 generation, runtime shim, and differential verification |
| [libtc Design Document](docs/libtc设计说明书-0.0.35.md) | libtc architecture, transactions, lifecycle, and error contract |
| [TC-Embed Design Document](docs/TC-Embed详细设计说明书-0.0.36.md) | C→TC embed interop API, `ptr<T>` handle model, VM/AOT dual-mode design |
| [Design–Implementation Conformance Report](docs/设计实现合规审查报告-0.0.35.md) | The ~182-item 0.0.35 conformance matrix and release evidence |

## Git Hooks

Optionally install the repository hooks:

```sh
make hooks
# Or: bash scripts/install-git-hooks.sh
```

## License

This project is released under the Apache License 2.0. See [LICENSE](LICENSE).

## Author

**唐荣兵** ([yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)) — Project creator and maintainer
