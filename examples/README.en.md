# Examples

Short teaching programs for TC 0.0.42. These are **not** the conformance suite; regressions live under `tests/`.

中文：[README.md](README.md)

## Prerequisites

```sh
make
```

Commands below assume the repository root and `./build/vm/bin/tc-vm`.

## Catalog

| File | Topic | Expected stdout (summary) |
| ---- | ----- | ------------------------- |
| [`hello.tc`](hello.tc) | Minimal `#program` | `42` |
| [`arith.tc`](arith.tc) | Typed arithmetic and `if` | `30` / `1` |
| [`control_flow.tc`](control_flow.tc) | `while` + `continue` / `break` | `1` `3` `4` |
| [`lib_and_import/`](lib_and_import/) | `#lib` + `import` + `funcall` | `7` |
| [`struct_basic.tc`](struct_basic.tc) | Struct construction and fields | `10` `2` |
| [`ptr_basic.tc`](ptr_basic.tc) | `ptr` address / store / load | `2` |

## Run

```sh
./build/vm/bin/tc-vm examples/hello.tc
./build/vm/bin/tc-vm examples/arith.tc
./build/vm/bin/tc-vm examples/control_flow.tc
./build/vm/bin/tc-vm examples/struct_basic.tc
./build/vm/bin/tc-vm examples/ptr_basic.tc
./build/vm/bin/tc-vm examples/lib_and_import/main.tc
```

Static check only:

```sh
./build/vm/bin/tc-vm --check examples/hello.tc
```

Translate to C99 (writes `<input>.c` beside the source; ignored by git — do not commit):

```sh
./build/aot/bin/tc-aot examples/hello.tc
```

## Embed

See the “TC-Embed” section in the root [README.en.md](../README.en.md) and integration tests under `tests/vm/embed/` (not minimal teaching samples).

## Next steps

- Language spec: [docs/TC语言标准设计说明书-0.0.42.md](../docs/TC语言标准设计说明书-0.0.42.md)
- Doc map: [docs/README.en.md](../docs/README.en.md)
- Contributing: [CONTRIBUTING.en.md](../CONTRIBUTING.en.md)
