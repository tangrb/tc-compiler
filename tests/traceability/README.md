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

# 5. Golden library validation and runtime golden execution.
python3 tests/golden/validate.py
python3 tests/golden/run.py build/src/driver/tc-vm
```

`coverage.py --check` proves the JSON and Markdown are current; it does not
require every row to be covered before its stage.  Current matrix: 231 items /
636 slots, **636 covered / 0 uncovered / 0 suspended**.  Covered rows include
the completed contract/LEX/SYN/IMPORT/SEM, S4 CFG/DI/call/safety/annotation, S5
CT/IR requirements, S6 VM/runtime requirements and the S7 differential/ABI
requirements (all 80 codes, all 155 opcode slots including per-opcode static
shape errors, device policy, I/O source states, exit branches and design §12
items 1/3/4/5/6/7/8/10/12/14/15); S9 adds the freestanding bare-metal
platform, ARM32 differential/ABI, 32/64 profile isolation and zero-pass
evidence; S10 adds RISC-V 32 target admission, ILP32 C interop, static parity,
long-term regression and release-audit evidence.  Stage detail lives in the generated coverage report; gate
conclusions are in [docs/gates/README.md](../../docs/gates/README.md).
