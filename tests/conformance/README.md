# Static conformance suites

This tree holds the LEX/SYN, IMPORT and SEM static conformance cases and the
positive acceptance/property gates.  Every case is derived from the standard,
never backfilled from compiler output.

- S1 LEX/SYN: `lex/`, `syn/`, `accept/`, `fuzz/` (source table
  `s1_cases.py`).
- S2 module discovery/IMPORT and S3 names/types/conversions/layout:
  `import/`, `sem/`, `accept_s3/` (source table `s2s3_cases.py`).  Import
  fixtures live under `import/fixtures/<case-id>/`; the same fixture tree is
  reused by `tc_sem_test` for module-order/instance checks.
- S4 CFG/DI/returns/calls/safety/annotations: `sem4/`, `accept_s4/` (source
  table `s4_cases.py`).  Each case owns `sem4/fixtures/<case-id>/` or
  `accept_s4/<case-id>/`; `@HERE@`/`@REL@` positions follow the same
  §12.5.1 conventions.  S4 acceptance tool cases use `--sem-only` because they
  assert SEM acceptance only.
- S5 CT/const/static-var: `ct/`, `accept_s5/` (source table `s5_cases.py`).
  Negative cases pin the six `TC_CE_CONST_*` CT codes, attribution/order,
  stage boundary and static-var initializers; positive cases cover the §3.10
  allowlist, const paths/struct chains, casts/bitcasts/`sizeof`, `make` and
  static initializers.  Positions follow §12.5.1 with the target-declaration
  convention documented in the table.

```text
tests/conformance/
  static/
    s1_cases.py        auditable source table for the S1 cases
    s2s3_cases.py      auditable source table for S2/S3 cases
    s4_cases.py        auditable source table for S4 cases
    s5_cases.py        auditable source table for S5 CT cases
    lex/               LEX diagnostics (TC_CE_SYNTAX, TC_CE_LITERAL_LEX_RANGE)
    syn/               SYN diagnostics (the other five SYN codes + TC_CE_SYNTAX)
    accept/            positive LEX/SYN acceptance sources
    import/            IMPORT cases + root/library fixtures
    sem/               SEM name/type/conversion/layout cases
    sem4/              S4 CFG/DI/call/safety/annotation cases
    ct/                S5 CT/const/static-var cases
    accept_s3/         positive IMPORT/SEM acceptance sources
    accept_s4/         positive CFG/DI/call/safety/annotation sources
    accept_s5/         positive CT acceptance sources
  fuzz/                L7 property case (random bytes; no crash/hang)
```

## Source table and generated files

`static/s1_cases.py` is the **auditable source table** (text/marker cases plus
`RAW_CASES` byte fixtures with explicit positions): it records each case
id, source, `spec` citation, expected stage/code/primary position, and whether
a position comes from an explicit §12.5.1 rule or from the documented
`@convention` (the first token that cannot be consumed by the EBNF parse).  The
marker `@HERE@` records where the primary diagnostic must point; the generator
removes it and computes the 1-based line and Unicode-scalar column.  It never
reads compiler output.

```sh
# regenerate the S1, S2/S3, S4 and S5 YAML+source files
python3 tests/conformance/static/s1_cases.py --write
python3 tests/conformance/static/s2s3_cases.py --write
python3 tests/conformance/static/s4_cases.py --write
python3 tests/conformance/static/s5_cases.py --write

# gate: every generated file must match the table
python3 tests/conformance/static/s1_cases.py --check
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/conformance/static/s4_cases.py --check
python3 tests/conformance/static/s5_cases.py --check
```

`@HERE@` and `@REL@` mark primary/related positions before marker removal.
Compile cases now run the full static pipeline (`--static-only`, including CT
and IR finalization), so an imported module's LEX/SYN diagnostic still wins
over an earlier IMPORT error (stage priority).  Tool acceptance cases name
`--syntax-only`, `--sem-only` or `--static-only` explicitly.  Profile-sensitive cases set `profile: w32` or
`profile: w64`; the adapter passes `--profile=w32` for the former and the
default 64-bit profile for the latter.

Do not edit generated `.tc`/`.yaml` files by hand; update the table and
regenerate.  CRLF/CR and invalid-UTF-8 byte fixtures are marked `-text` in `.gitattributes`
so Git preserves their exact bytes; `--check` compares bytes, not normalized
text.  `RAW_CASES` pins the malformed-numeric-candidate recovery (`1.5.5` LEX at
the number start) and invalid-UTF-8 line/scalar-column behavior.  A new case needs a `spec` citation and, for positions not
enumerated in §12.5.1, an explicit `@convention` note.

## Case ids

Negative cases use `TC-LEX-SYN-NNNN` (LEX) and `TC-SYN-<DOMAIN>-NNNN` (SYN).
Positive tool cases use `TC-TOOL-SYN-NNNN`; the property case uses
`TC-FZ-LEX-NNNN`.  All ids are unique across the selected suite.

## Running

The harness requires an adapter.  `tests/CMakeLists.txt` configures
`build/tc_adapter.py` with this build tree's `tc-vm` path and registers:

```sh
ctest --test-dir build -R harness.conformance --output-on-failure
ctest --test-dir build -R 'determinism.s1|determinism.s2s3|determinism.s4' --output-on-failure
ctest --test-dir build -R 'sem.unit|conformance.s2s3_cases|conformance.s4_cases' --output-on-failure
```

`adapter.selftest` uses fake tc-vm scripts to prove the adapter is fail-closed
(wrong file, junk stderr, stdout, property path).  The adapter runs
`tc-vm --syntax-only` and parses the stable diagnostic line;
compile cases require exactly one diagnostic (stage/code/primary/related).
Acceptance cases are tool cases: exit 0, empty stdout/stderr, two runs
byte-identical.  The property case generates deterministic random byte inputs
and requires no crash/hang and, on rejection, exactly one LEX/SYN diagnostic.

`not_implemented` is never allowed by the gate; every selected case is either a
pass or a failure.
