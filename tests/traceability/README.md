# Traceability and structural coverage (0.0.46)

This directory implements the traceability skeleton required by the test
plan §3.6, §7.5, §8 and §10.1:

- `codes.json` — the 80-code machine-readable table (65 `TC_CE_*` +
  15 `TC_RE_*`), generated from standard §12.3/§12.5.1 and test-plan appendix A
  by `gen_codes.py`.  This directory must never add, remove or rename a code.
- `gen_codes.py` / `gen_c_table.py` — regenerate `codes.json`, the C X-macro
  table and `src/diagnostic/table.c`; `--check` detects stale files.
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
python3 tests/traceability/coverage.py --out-dir build/coverage

# 5. Golden library validation.
python3 tests/golden/validate.py
```

`coverage.py --check` proves the JSON and Markdown are current; it does not
require every row to be covered before its stage.  Current matrix: 231 items /
634 slots, **245 covered / 389 uncovered / 0 suspended**.  Covered rows are the
completed contract/LEX/SYN/IMPORT/SEM requirements; uncovered rows are S4–S10
work (CFG/DI, call ordering/safety, annotations, CT, opcodes, VM/backend,
platform).  Stage detail lives in the generated coverage report; gate conclusions
are in [docs/gates/README.md](../../docs/gates/README.md).
