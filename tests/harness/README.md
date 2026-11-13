# TC 0.0.46 S0 test harness

Dependency-free Python 3 harness for TC case metadata, the seven deciders from
the test plan section 3.4, hard timeouts, output capture and fail-closed
reporting.  Standard library only.  The harness never invokes a compiler and
never uses the network; an adapter supplied by the caller provides compiler,
VM, tool or generator behaviour.

## Files

| File | Purpose |
| ---- | ------- |
| `yaml_subset.py` | Appendix C YAML subset parser |
| `diag_codes.py` | Frozen 80-code table (65 `TC_CE_*` + 15 `TC_RE_*`) |
| `case_schema.py` | Strict metadata validation, kind inference, hex parsing |
| `deciders.py` | compile / run / diff / unit / platform / tool / property |
| `runner.py` | Discovery, fork worker, timeout, environment, CLI |
| `selftest.py` | Named in-process fake scenarios; exits 0 only on full success |

Run the selftest with:

```sh
python3 tests/harness/selftest.py
```

## CLI

```text
python3 tests/harness/runner.py PATH [PATH ...] [options]

--adapter SPEC             file.py, module, or module:attribute
--filter PATTERN           case-id substring, or fnmatch when * ? [ appears
--list                     print "id<TAB>kind<TAB>path" and exit
--json-out PATH            write a deterministic JSON report
--allow-not-implemented    allow only not_implemented cases to exit 0
--timeout-s SECONDS        default per-case hard timeout (default 10)
```

Examples:

```sh
# Inventory without an adapter (does not run cases).
python3 tests/harness/runner.py tests/conformance --list

# Run every case; without --adapter every case is not_implemented.
python3 tests/harness/runner.py tests/conformance

# Run with an adapter and write the gate report.
python3 tests/harness/runner.py tests/conformance \
    --adapter tests/adapters/tc_adapter.py \
    --json-out build/harness-report.json

# One ID family; use quotes so the shell does not expand *.
python3 tests/harness/runner.py tests/conformance --filter 'TC-RT-FP-*' \
    --adapter tests/adapters/tc_adapter.py

# Explicitly allow S0 placeholder cases (still not counted as pass).
python3 tests/harness/runner.py tests/conformance --allow-not-implemented
```

Directory arguments are searched recursively for `*.yaml` / `*.yml`; hidden
files and directories are skipped.  Explicit file arguments are always read.
Output is sorted by case id.  Case ids must be unique across the selected
inputs.

Exit status:

| Status | Meaning |
| ------ | ------- |
| 0 | no failures; `not_implemented` is zero, or `--allow-not-implemented` was used |
| 1 | at least one `fail`, or `not_implemented` without the allowance |
| 2 | usage error, metadata/schema error, adapter load error, or report write error |

`SUMMARY pass=.. fail=.. not_implemented=.. total=..` is printed on every run.
`not_implemented` is never added to `pass`.

## Case metadata

The parser accepts nested maps, block sequences, inline lists and maps, quoted
and unquoted scalars, comments, `null`/`~`, booleans, decimal and `0x` integers,
and decimal floats.  Duplicate keys, tabs in indentation, block scalars,
anchors/tags and malformed flow collections are rejected.  All metadata files
are UTF-8.

Common fields:

| Field | Rules |
| ----- | ----- |
| `id` | required non-empty string; unique in a run |
| `profiles` or `profile` | exactly one form; non-empty strings |
| `source` | required for compile/run/diff/platform; optional for unit/tool/property |
| `spec` | required non-empty derivation reference (standard/design clause) |
| `expect` | required; mapping except for unit cases, where it is any JSON value |
| `include_roots` | optional list of strings |
| `timeout_s` | optional positive number; overrides the CLI/default timeout |
| `kind` | optional; one of the seven kinds; normally inferred |
| `checked` | optional boolean |
| `opt` | optional O0/O2-style string or integer |
| `run` | run only: mapping with optional `stdin_hex` |
| `compare`, `normalize`, `oracle` | diff only |
| `link`, `uart`, `device` | platform only |
| `tool` | tool only |
| `unit` | unit only: arbitrary mapping passed to the adapter |
| `property` | property only |

Unknown fields, known fields under the wrong kind, missing required fields and
bad value types are schema errors (exit 2), never skips.  A case kind is
inferred from `tool`/`property`/`unit`, then `expect.diff`/`compare`, then
`link`/`uart`/`device`, then compile expectations (`stage`/`code`/...), then
runtime expectations.  Add `kind:` when a case is ambiguous.

Relative `source`, `include_roots` and `link` paths are resolved by trying the
case file's directory first and the suite root (the directory argument, or the
explicit file's parent) second.  The resolved paths and the isolated work
directory are exposed to the adapter as `case["_harness"]`.

### Compile (static diagnostic)

```yaml
id: TC-SEM-CONV-0042@w32
profiles: [w32]
source: sem/conv/bitcast_ptr_int_32.tc
include_roots: [fixtures/include]
spec: "section 3.11 bitcast matrix"
expect:
  stage: SEM
  code: TC_CE_BITCAST_WIDTH
  primary: {file: main.tc, line: 7, column: 11}
  related: []
  diagnostics: exactly-one
```

All five expectation keys are required.  `primary` and each `related` entry are
`{file, line, column}` with 1-based integer line/column.  The code must exist in
the frozen table and be legal for the expected stage.  `LEX/SYN` may be
specialized to `LEX` or `SYN`; `TC_CE_IMPORT_*` cases normally use `IMPORT`.
The decider requires exactly one diagnostic, then compares stage, code, primary
and the ordered related list.  Zero diagnostics, extra diagnostics or any
mismatch fail.

### Run (VM/observed behaviour)

```yaml
id: TC-RT-FP-0107@w64
profiles: [w64]
source: runtime/float/format_g6.tc
run:
  stdin_hex: ""
spec: "section 11.2 %g output; section 6.3 rounding"
expect:
  stdout_hex: "31 2e 32 33 34 35 36 0a"
  trap: null
  exit: 0
  termination: normal
```

All four expectations are required.  `trap` is explicit `null` or one of the 15
`TC_RE_*` codes.  `termination` is `normal`, `trap`, `timeout`, `signal` or
`abnormal`.  stdout is compared byte-for-byte.

### Diff (VM vs AOT)

```yaml
id: TC-DIFF-A64-0003
profile: aarch64-linux-user
source: diff/subset/add_overflow.tc
oracle: vm
compare: [stdout_bytes, trap_code, exit_status, termination]
normalize: [address_literals, stack_offsets_unobservable]
spec: "section 13.4 static consistency; design section 12.1"
expect:
  diff: identical
```

`compare` is a non-empty subset of the four names.  `normalize` is optional and
currently accepts `address_literals` and `stack_offsets_unobservable`;
`expect.diff` must be `identical`.  Normalization is applied to stdout before
the comparison.

Normalization is a documented hook: `deciders.NORMALIZER_HOOKS` maps a rule name
to a `bytes -> bytes` callable.  The defaults turn `0x...` address literals into
`<addr>` and `sp/fp/bp +/- number` into `<off>`.  Add new rules there and to
`NORMALIZE_RULES` in `diag_codes.py` when a new environment-defined address form
needs masking.  Do not use normalization to hide non-address behavioural
differences.

### Unit

```yaml
id: TC-UNIT-LAYOUT-0001@w64
profiles: [w64]
spec: "layout table, independent derivation"
kind: unit
unit: {entry: sizeof_struct}
expect: {size: 24, align: 8}
```

The adapter returns any JSON-serializable observed value.  Comparison is strict
JSON: object order is irrelevant, array order matters, and `true` is not equal
to `1`.

### Platform

```yaml
id: TC-PLT-BARE-0006
profile: aarch64-baremetal
source: platform/baremetal/uart_write.tc
link: fixtures/baremetal/link.ld
uart: pl011
device: reject
spec: "design section 7.2; section 8 profile"
expect:
  uart_hex: "4f 4b 0a"
  artifacts: [uart.elf]
  symbols: [_start, main]
  trap: null
  exit: 0
  termination: normal
```

At least one of `link`/`uart`/`device` is required, and at least one of
`uart_hex`/`stdout_hex`/`artifacts`/`symbols` must be expected.  `trap` and
`exit` are required.  Expected artifacts (and artifacts returned by the
adapter) must exist below the case work directory; absolute/escaping paths are
rejected.  Symbols are compared as a set.  Device policy names are normalized
(`reject`, `read`, `readwrite`, `volatile`; `TC_DEV_*` spellings accepted).

### Tool

```yaml
id: TC-TOOL-CLI-0002
profiles: [w64]
spec: "CLI contract; reproducibility"
kind: tool
tool:
  argv: [tc-vm, --version]
  stdin_hex: ""
  repeat: 2
expect:
  exit: 0
  stdout_hex: "30 2e 30 2e 34 36 0a"
  stderr_hex: ""
```

`argv` is required; `repeat` defaults to 2 and must be at least 2 because the
decider always checks reproducibility.  Expected `exit` is required;
stdout/stderr expectations and expected artifacts are optional.  All runs must
match the expectations and each other, including artifact SHA-256 digests.

### Property

```yaml
id: TC-FZ-LEX-0008
profiles: [w64]
spec: "generator must not crash or hang"
kind: property
property: {name: lex_random, trials: 1000, seed: 1}
expect: {}
```

The adapter returns a list of counterexamples, or `{counterexamples: [...]}`.
The case passes only for an empty list.  A malformed return value fails.

## Adapter API

An adapter is supplied by the caller and can be:

1. a callable `adapter(case, action)`;
2. an object with methods named after actions (`compile(case)`, `run(case)`,
   `vm(case)`, `aot(case)`, `diff(case)`, `unit(case)`, `platform(case)`,
   `tool(case)`, `property(case)`);
3. a mapping of action name to callable;
4. a Python file containing `ADAPTER` or `adapter`, or a module with one of
   those names.

Actions:

| Kind | Actions |
| ---- | ------- |
| compile | `compile` |
| run | `run` |
| diff | `vm` and `aot`, or a combined `diff` returning `{vm: ..., aot: ...}` |
| unit | `unit` |
| platform | `platform` |
| tool | `tool` (called `repeat` times) |
| property | `property` |

`case` is the validated metadata plus `_harness`:

```python
{
  "suite_root": "...", "case_file": "...", "workdir": "...",
  "timeout_s": 10.0,
  "source_path": "...",          # when source is present
  "include_roots": ["..."],      # when include_roots is present
  "link_path": "...",            # when link is present
  "stdin_hex": "",               # when run/tool declares it
}
```

The adapter must use `_harness["workdir"]` for outputs and must not assume the
parent process's current directory.  The runner changes directory to that
workdir and clears the environment to `LC_ALL=C`, `LANG=C`, `TZ=UTC`, plus the
inherited `PATH`.

Result shapes (stdout/stderr may be `bytes`, `str` (encoded UTF-8) or `*_hex`):

```python
# compile
{"diagnostics": [{"stage": "SEM", "code": "TC_CE_...",
                  "primary": {"file": "main.tc", "line": 7, "column": 11},
                  "related": []}]}
# or one flattened diagnostic {stage, code, primary, related}

# run / vm / aot
{"stdout_hex": "4f 4b 0a", "trap": None, "exit": 0,
 "termination": "normal"}

# platform
{"uart_hex": "...", "artifacts": ["uart.elf"], "symbols": ["_start"],
 "trap": None, "exit": 0, "termination": "normal"}
# output fields may instead be nested under "output"

# tool
{"exit": 0, "stdout_hex": "...", "stderr_hex": "...",
 "artifacts": ["artifact.bin"]}

# property
[]   # empty counterexample list
```

Signalling not implemented is explicit: raise `NotImplementedError`, or return
`{"status": "not_implemented", "reason": "..."}`.  Omit `--adapter` to mark every
selected case `not_implemented`.  Other exceptions and malformed results are
failures.

## Interface decisions for the CTest integration

- The only required S0 CTest entry is `harness.selftest` (command above).  No
  compiler adapter is needed for S0; when the adapter is absent every selected
  case is `not_implemented`, and the runner deliberately exits nonzero.
- Later conformance gates should invoke `runner.py` with an adapter and no
  `--allow-not-implemented`.  Use `--json-out` for machine-readable evidence.
- Diagnostic `primary.file` and `related[].file` strings are compared exactly.
  The adapter should report the source names written in the case metadata
  (for example `main.tc`), not absolute host paths.
- Adapter callables use `(case, action)`; object/mapping adapters use one
  argument per method.  A combined diff action is `diff(case)` returning
  `{"vm": ..., "aot": ...}`.
- Work directories are created under the system temp directory and deleted
  after each case.  Artifact paths are relative to that work directory.
- The JSON report is sorted by case id and contains no timestamps, so repeated
  runs can be compared byte-for-byte when adapter output is deterministic.

## Decider summary

| Kind | Pass condition |
| ---- | -------------- |
| compile | exactly one diagnostic; stage, code, primary, related all match |
| run | stdout bytes, trap, exit and termination all match |
| diff | every `compare` field matches after normalization |
| unit | observed JSON equals `expect` |
| platform | artifacts exist, symbols match, output/trap/exit/termination match |
| tool | every run matches exit/stdout/stderr/artifacts and runs are reproducible |
| property | counterexample list is empty |

## Fail-closed rules

- No adapter, `NotImplementedError`, or an explicit `not_implemented` result is
  status `not_implemented`.  It is not a pass and makes the gate exit nonzero
  unless `--allow-not-implemented` is given.
- Any adapter exception, worker exception, signal/crash, timeout, missing
  result, malformed decision record, missing diagnostic, extra diagnostic or
  malformed observation is `fail`.
- A compiler that "succeeds" for a static diagnostic case still fails because
  the diagnostic list is empty.
- Diagnostic cases never accept "code only": stage, primary position and the
  related list are also compared.
- Runtime comparisons are byte-level for stdout; no locale, timezone or text
  decoding is involved in expected values.
- A hard timeout kills the worker.  Default 10 seconds; `timeout_s` in the case
  overrides the CLI default.  Harness stdout/stderr are captured from the worker
  and included in failure details as `captured_stdout` / `captured_stderr`.
- Each case gets a fresh temporary directory below the system temp directory.
  The directory is removed after the case.
- The harness itself performs no network access and removes proxy variables by
  clearing the environment.  Adapters must not depend on network access.

## CTest integration

S0 needs one CTest entry.  The lead adds this to `tests/CMakeLists.txt` (not
modified by this harness):

```cmake
add_test(
    NAME harness.selftest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/selftest.py)
set_tests_properties(
    harness.selftest
    PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

Equivalent direct command line:

```sh
python3 tests/harness/selftest.py
```

When the compiler adapter and the S1+ conformance tree exist, add the case
runner as a separate gate (do not pass `--allow-not-implemented` for a real
gate):

```cmake
add_test(
    NAME harness.conformance
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/runner.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tc_adapter.py
            --json-out ${CMAKE_BINARY_DIR}/harness-report.json)
set_tests_properties(
    harness.conformance
    PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

The runner prints `SUMMARY pass=... fail=... not_implemented=... total=...`;
CTest treats the nonzero exit as the gate failure.  The JSON report contains the
same per-case records and summary, sorted by id, with no timestamps.

## S0 source references

- Test plan section 3.4 (seven deciders), section 5 S0, Appendix C.
- Standard section 12.1/12.2 (first diagnosis), 12.3 (80 codes), 12.5.1
  (positions), 1.3/1.7 (observable behaviour and phases).
- Design section 12 (verification strategy) and section 8 (profile fields).
