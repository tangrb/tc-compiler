# S1 LEX/SYN conformance suite

This tree holds the S1 (LEX/SYN) static conformance cases and the positive
acceptance/property gates.  Later stages add their own subdirectories; every
case is still derived from the standard, never backfilled from compiler output.

```text
tests/conformance/
  static/
    s1_cases.py        auditable source table for the S1 cases
    lex/               LEX diagnostics (TC_CE_SYNTAX, TC_CE_LITERAL_LEX_RANGE)
    syn/               SYN diagnostics (the other five SYN codes + TC_CE_SYNTAX)
    accept/            positive LEX/SYN acceptance sources
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
# regenerate lex/ syn/ accept/ fuzz/ YAML+source files
python3 tests/conformance/static/s1_cases.py --write

# gate: every generated file must match the table
python3 tests/conformance/static/s1_cases.py --check
```

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
ctest --test-dir build -R determinism.s1 --output-on-failure
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
