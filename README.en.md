# TC-Compiler

The implementation project of the **TC** language. Currently includes **TC-VM** (a direct execution engine, implemented) and **TC-AOT** (ahead-of-time compilation of `.tc` to native object code, directory reserved, not yet implemented).

## Directory Structure

```text
docs/                  Language specification, VM design documents, etc.
src/
├── vm/                TC-VM source code, CMakeLists.txt
└── aot/               TC-AOT reserved (CMakeLists.txt)
tests/                 Conformance tests (currently for VM)
scripts/
├── vm/                VM test scripts
└── aot/               AOT test scripts (reserved)
build/                 Build artifacts (git ignored)
├── vm/bin/tc-vm       VM executable
└── aot/bin/           AOT executable (reserved)
```

## Build

The build is managed by **CMake**; the root `Makefile` is a thin wrapper around CMake, and `CMakeLists.txt` defines each component target.

### Makefile (Recommended)

```sh
make            # Configure and build VM (default)
make vm         # Same as above
make aot        # Build AOT (not yet implemented, will error)
make test       # Run VM conformance tests
make test-vm    # Same as above
make test-aot   # Run AOT tests (not yet implemented)
make clean      # Remove build/ directory
```

### CMake (Equivalent Commands)

```sh
cmake -S . -B build
cmake --build build                  # Build tc-vm
cmake --build build --target check-vm
cmake --build build --target check-aot
cmake --build build --target check   # Currently equivalent to check-vm
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

## Documents

| Document | Description |
|----------|-------------|
| [TC Language Specification (Chinese)](docs/TC语言标准设计说明书.md) | Authoritative definition of TC syntax and semantics (v0.8.1) |
| [TC-VM Design Document (Chinese)](docs/TC-VM详细设计说明书.md) | Direct execution engine architecture and implementation conventions (v1.2) |
| [TC-VM Command Reference (Chinese)](docs/TC-VM命令行参考.md) | Command-line instructions for using tc-vm with `.tc` source files (v1.3) |

Implementation behavior follows the language specification; VM / AOT design documents define each backend's implementation architecture and do not redefine language semantics.

## Author

- **唐荣兵** ([yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)) — Project creator and maintainer
