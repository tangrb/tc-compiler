# TC-Compiler

A **TC** language compiler implemented in C99. Includes **libtc** (compile/execute static library), **TC-VM** (direct execution engine), and **TC-AOT** (ahead-of-time compilation, transpiles `.tc` to C99 source code).

Version: **v0.0.24** (`src/vm/driver/tc_version.h`)

## Directory Structure

```text
docs/                  Language specification, VM/AOT/lib design documents
src/
├── libtc/             Shared static library (compile + execute pipeline, CMakeLists.txt)
├── vm/                TC-VM source (lexer / parser / analyzer / executor / runtime)
└── aot/               TC-AOT source (codegen / rt shim / CLI)
tests/
    ├── valid/              Conformance tests (covers bool/let/I/O/format/if/bitwise/shift etc.)
    ├── errors/             Error tests (72 static + 37 runtime)
    ├── unit/              C unit tests (lexer*2 / parser / semantics / types / io / bitwise / shift / symbol / warning / analyzer / stmt-index — 12 modules)
    └── stress/             Stress tests
scripts/
├── ci.sh              Local CI pipeline (build + test + static check)
├── run_tests.sh        Unified test entry (recommended)
├── run_asan_all.sh     ASan one-click build + full test
├── run_memcheck_macos.sh  macOS memory check (MallocScribble + leaks)
├── valgrind-suppressions.supp  Valgrind suppressions (libc startup alloc)
├── git-hooks/          Git hooks (commit-msg strips Cursor trailer)
├── vm/                 VM test scripts
├── aot/                AOT differential test scripts
├── sync/               RHS coverage checker (check_rhs_coverage.py)
└── install-git-hooks.sh
build/                  Build artifacts (git ignored)
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
| Constant Folding | Compile-time evaluation of `let` initializers (arith/compare/logic/cast/bitwise all supported) |
| Control Flow | `if-then-else-end` (indentation-sensitive, supports nesting, block scoping) |
| Block Scoping | then/else mutually exclusive child scopes, allows same-name locals, nested shadowing |
| Bitwise | `and` / `or` / `xor` / `not` (bitwise, no overflow); `shl` (strict/wrap) / `shr` (arithmetic/logical) |
| REPL | Interactive line-by-line execution with persistent variables (if not supported) |

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
make test-valgrind      # Valgrind Memcheck mode (Linux)
make test-leaks         # macOS leaks mode
make memcheck-macos     # macOS full memory check (MallocScribble + leaks)
make bench              # Performance benchmarks
make build-asan         # ASan build
make build-ubsan        # UBSan build
make ci                 # Local CI (build + all tests + static checks)
make ci-coverage        # Local CI + coverage report
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

### Sanitizer Modes

#### AddressSanitizer (Memory Error Detection)

```sh
# Method 1: Makefile shortcut
make build-asan
bash scripts/run_tests.sh --asan

# Method 2: Manual cmake
cmake -S . -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan
bash scripts/run_tests.sh --asan

# Method 3: One-click script (build + full test + report)
bash scripts/run_asan_all.sh
```

#### UndefinedBehaviorSanitizer (UB Detection)

```sh
make build-ubsan
bash scripts/run_tests.sh --ubsan
```

### Memory Safety Checks

#### Linux: Valgrind Memcheck

```sh
make test-valgrind
# Equivalent to:
bash scripts/run_tests.sh --valgrind
```

#### macOS: leaks + MallocScribble

```sh
make test-leaks                   # Leak detection only (leaks --atExit)
make memcheck-macos               # Two-phase: MallocScribble (OOB/UAF) + leaks
# Equivalent to:
bash scripts/run_tests.sh --leaks
bash scripts/run_memcheck_macos.sh
```

The macOS `run_memcheck_macos.sh` script runs sequentially:
1. **Standard build** (`make vm`)
2. **MallocScribble mode**: `MallocScribble=1 MallocPreScribble=1`, detects out-of-bounds reads and use-after-free
3. **leaks --atExit mode**: Reports unreleased memory on process exit

> **Note**: Valgrind has poor compatibility on macOS. Prefer the built-in `leaks` + `MallocScribble` combination.

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

The REPL supports entering TC statements one by one with immediate execution; variables persist across lines. Built-in meta-commands include `:quit` (exit), `:reset` (clear variables), `:vars` (list variables), and `:help` (help). The REPL does not support `if` statements.

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
| [TC Language Specification (Chinese)](docs/TC语言标准设计说明书.md) | Authoritative definition of TC syntax and semantics (v0.0.24) |
| [TC-VM Design Document (Chinese)](docs/TC-VM详细设计说明书.md) | Direct execution engine architecture and implementation (v0.0.24) |
| [TC-VM Command Reference (Chinese)](docs/TC-VM命令行参考.md) | CLI instructions for tc-vm (v0.0.24) |
| [TC-AOT Design Document (Chinese)](docs/TC-AOT详细设计说明书.md) | AOT code generation and shim layer (v0.0.24) |
| [libtc Design Document (Chinese)](docs/libtc设计说明书.md) | libtc static library architecture and error contract (v0.0.24) |
| [libtc Embedding API](docs/libtc-api.md) | Quick reference for libtc embedding programming interface |

Implementation behavior follows the language specification; VM / AOT design documents define each backend's architecture without duplicating language semantics.

## Performance Profiling

Set the `TC_BENCH=1` environment variable to output phase timing to stderr:

```sh
TC_BENCH=1 ./build/vm/bin/tc-vm tests/valid/example.tc
```

Use with `scripts/vm/bench.sh` for local regression comparison.

## Embedding libtc

libtc provides a compile (Parse + Analyze) and execute separated embedding interface:

```c
#include "tc_lib.h"

TcDiagnostic diag;
TcTypedProgram program;

tc_diagnostic_init(&diag);
if (tc_compile_source("var x: int32 = 1\nwriteln(int32, x)\n", &program, &diag) != 0
    || tc_run_typed(&program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    tc_typed_program_free(&program);
    return 1;
}
tc_typed_program_free(&program);
tc_diagnostic_clear(&diag);
return 0;
```

See [docs/libtc-api.md](docs/libtc-api.md) for details.

## Testing

### Unified Entry (Recommended)

```sh
bash scripts/run_tests.sh                          # Run all tests
bash scripts/run_tests.sh --filter foo             # VM tests matching "foo" only
bash scripts/run_tests.sh --verbose                # Verbose logging
bash scripts/run_tests.sh --asan                   # AddressSanitizer mode
bash scripts/run_tests.sh --ubsan                  # UndefinedBehaviorSanitizer mode
bash scripts/run_tests.sh --valgrind               # Valgrind Memcheck mode (Linux)
bash scripts/run_tests.sh --leaks                  # macOS leaks mode
```

### Equivalent Makefile

```sh
make test
```

### Local CI

The local CI script replaces remote GitHub Actions, running the full build, test, and static-check pipeline locally.

**Manual trigger** (no push required):

```sh
make ci                          # Standard CI (build + all tests + static checks)
make ci-coverage                 # With coverage collection and HTML report
# Equivalent to:
bash scripts/ci.sh               # Standard CI
bash scripts/ci.sh --coverage    # With coverage
bash scripts/ci.sh --full        # Same
```

CI pipeline consists of 5 stages:

| Stage | Check | Command |
|-------|-------|---------|
| 1/5 | Build (VM + AOT + libtc) | `cmake --build build` |
| 2/5 | VM Conformance tests | `make test-vm` |
| 3/5 | C unit tests | `make test-unit` |
| 4/5 | AOT Differential tests | `make test-aot` |
| 5/5 | Static checks (RHS coverage + naming) | `check_rhs_coverage.py` + `check_source_naming.py` |

Each CI run covers about **1000+** check points (VM + unit + AOT).

### GitHub Actions: ASan CI

`.github/workflows/asan.yml` automatically triggers on push to `main` or PR:

- **Platform**: `ubuntu-latest`
- **Pipeline**: cmake ASan configure → build → VM conformance tests → unit tests → AOT differential tests
- Detects memory errors (leaks, out-of-bounds, use-after-free) and fails on any finding

Coverage report is generated at `build-coverage/coverage_html/index.html`, viewable in a browser.

### Git hooks (Optional)

It is recommended to install hooks after initial clone to automatically strip `Co-authored-by: Cursor <cursoragent@cursor.com>` from commit messages:

```sh
make hooks
# Or: bash scripts/install-git-hooks.sh
```

## Author

- **唐荣兵** ([yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)) — Project creator and maintainer
