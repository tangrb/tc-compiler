# Traceability and structural coverage (0.0.46)

This directory implements the S0 traceability skeleton required by the test
plan §3.6, §7.5, §8 and §10.1:

- `codes.json` — the 80-code machine-readable table (65 `TC_CE_*` +
  15 `TC_RE_*`), generated from standard §12.3/§12.5.1 and test-plan appendix A
  by `gen_codes.py`.  This directory must never add, remove or rename a code.
- `gen_codes.py` / `gen_c_table.py` — regenerate `codes.json`, the C X-macro
  table and `src/frontend/diagnostic_table.c`; `--check` detects stale files.
- `check_codes.py` — validates `codes.json` against standard §12.3 and,
  optionally, the C table dump.
- `check_harness_codes.py` — cross-checks the standalone harness code table
  against `codes.json`.
- `coverage.py` — generates the structural coverage matrix skeleton.
- `coverage.generated.json` / `coverage-report.md` — committed skeleton output.

All scripts use the Python standard library only, are deterministic, and never
touch the network.  They locate the repository from `__file__`, so CTest can
run them from any working directory.

## Commands

```sh
# 1. Closed 80-code table check (standard §12.3, §12.2 IMPORT exception,
#    12-code W-sensitive preamble).  Exit 1 on any mismatch.
python3 tests/traceability/check_codes.py

# Optional: compare a TSV dump of the C code table.  Columns are
# code, print_name, stage, report_stage, w_sensitive, group.  The argument may
# be a TSV file or an executable that writes the TSV to stdout.
python3 tests/traceability/check_codes.py --c-table-dump build/tests/tc_dump_codes

# Regenerate sources after a deliberate, reviewed standard change.
python3 tests/traceability/gen_codes.py
python3 tests/traceability/gen_c_table.py
python3 tests/traceability/coverage.py

# 2. Regenerate the committed coverage skeleton.
python3 tests/traceability/coverage.py

# 3. Gate check: fail (exit 1) if the committed generated files are stale, or
#    if any standard §12.3 code is missing/extra in codes.json.
#    Prints the uncovered slot count on success and failure.
python3 tests/traceability/coverage.py --check

# 4. Write a gate report elsewhere without touching the committed skeleton.
python3 tests/traceability/coverage.py --out-dir build/coverage/S0

# 5. Golden library validation.
python3 tests/golden/validate.py
```

`coverage.py --check` proves the JSON and Markdown are current; it does **not**
require every row to be covered at S0.  A covered-row gate belongs to later
stages, where zero uncovered rows (or an approved suspension list) is required.
At S0 the matrix must exist and every gap must be visible.

## What the matrix contains

| Category | Rows (S0) | Source |
|---|---:|---|
| `chapter` | 15 chapters × positive/negative = 30 | test plan §8.1, cross-checked against standard §0–§14 |
| `code` | 80 codes × trigger/no-false-positive/position/stage = 320, plus w32/w64 slots for the 12 W-sensitive codes = 344 | `codes.json`, standard §12.3/§12.5.1, §13.2.2 |
| `opcode` | 63 opcodes × normal/static-error, plus a trap slot where §6.2 says trap | standard §6.2, cross-checked against the `TcOpcode` enum in design §3.2 |
| `design12` | 15 items × suite-mapping/gate = 30 | design §12, test plan §8.2 |
| `first_diag_rule` | 10 rules × combination case = 10 | standard §12.2, test plan §6.1 |
| `flip32_64` | 17 rows × w32/w64 = 34 | standard §13.2.2 |
| `annotation` | 16 | standard §2.3/§8.6 |
| `device_policy` | 4 | design §8 `TcDevicePolicy` |
| `io_source_state` | 4: OK, EOF, ERROR, short read | design §7.2 `TcIoSrc` |
| `exit_strategy` | 2: normal and trap exit | design §8 `TcExitPolicy` |

The opcode list is parsed from the normative §6.2 tables, so it contains the
63 spellings including `.wrap`, `.ieee` and `.volatile`; the 51 base names are
cross-checked against the design's 51 `TC_*` enum values.  If either document
changes, generation fails instead of silently producing a stale list.

## Explicit-uncovered rule

1. Every row is emitted with `"status": "uncovered"` and `"case_ids": []`.
2. A row may be marked `covered` **only** when it carries at least one real
   case id (test plan §3.3 format, e.g. `TC-LEX-LIT-0007@w64`).
3. `suspended`/`not-implemented` is an explicit status with an explanation in
   the gate report; it is never counted as covered.  Do not silently omit a
   row, rename it, or replace a missing case with a skip.
4. Missing coverage is therefore always visible in both
   `coverage.generated.json` and `coverage-report.md`.  An unexplained
   uncovered item blocks the stage gate (§7.5, Appendix D).

## CTest interface for the lead

Suggested CMake/CTest registration (paths relative to the repository root;
the scripts need no environment variables):

```cmake
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME trace.codes
         COMMAND ${Python3_EXECUTABLE} tests/traceability/check_codes.py)
add_test(NAME trace.coverage
         COMMAND ${Python3_EXECUTABLE} tests/traceability/coverage.py --check)
add_test(NAME trace.golden
         COMMAND ${Python3_EXECUTABLE} tests/golden/validate.py)

set_tests_properties(trace.codes trace.coverage trace.golden
                     PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- `check_codes.py [--codes PATH] [--standard PATH] [--c-table-dump PATH]`
  returns 0/1 and prints a one-line `OK:`/`FAIL:` summary plus details.
- `coverage.py [--check] [--out-dir DIR] [--codes PATH]` returns 0/1 and
  prints per-category counts plus the uncovered total.  `--out-dir` lets a
  gate copy the report into a build artifact directory.
- `validate.py [ROOT]` recursively validates every `*.golden.yaml`; default
  root is `tests/golden`.

If `codes.json` is absent, `check_codes.py` and `coverage.py` fail with a
clear `codes.json missing: ...` message instead of fabricating a table.
