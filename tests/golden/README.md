# Golden expectation layout (0.0.46)

This directory is the **specification-derived** golden library.  It is not a
copy of any compiler output.  At S0 no compiler implementation exists; every
file here is a process/format rehearsal.

## Directory layout

```text
tests/golden/
  common/   expectations that are independent of the word size W
  w32/      expectations instantiated for the 32-bit profile (W=32)
  w64/      expectations instantiated for the 64-bit profile (W=64)
  validate.py
```

- `common/` is only for W-independent expectations, such as pure lexical
  diagnostics, EBNF/SYN acceptance, or source forms whose observable result is
  the same under both profiles.
- `w32/` and `w64/` are separate libraries.  A test must never compare a W=32
  run with a W=64 golden.  A W-sensitive item needs either two files or an
  explicit per-profile record; conditional skipping is not allowed.
- A `common/` golden that would become W-sensitive in any part must be split or
  moved.  Never fall back from `w64/` to `common/` (or the reverse).

File names use `<area>_<subject>.golden.yaml`, for example
`lex_syntax_comment_slash.golden.yaml`.  Each file has one `id`; ids follow the
test plan §3.3 pattern (for example `TC-LEX-...`) and are unique across the
whole golden tree.

## Required fields

Every `*.golden.yaml` must contain:

```yaml
id: TC-LEX-...            # unique
kind: diagnostic          # diagnostic | runtime | diff | platform | unit
spec: "§2.1 ...; §12.3 ...; §12.5.1 ..."
derivation: |
  Why this is the required expectation, computed only from the standard.
review:
  reviewer: "..."         # independent reviewer identity
  status: approved        # only approved goldens are usable
  date: "YYYY-MM-DD"
source: |
  ... TC source ...
expect:                   # required for kind: diagnostic
  stage: LEX
  code: TC_CE_SYNTAX
  primary:
    line: 2
    column: 30
  diagnostics: exactly_one
```

`validate.py` accepts this deliberately small YAML subset (block mappings,
block sequences, quoted scalars, and `|` block scalars).  Do not add YAML
features that the validator cannot check.

For diagnostic goldens, `expect` must state the stage, the code, the primary
1-based line/column, and `diagnostics: exactly_one`.  `related` may list
associated positions when the standard requires them (for example the first
declaration of a duplicate name).  Runtime goldens compare exact stdout bytes
(or the documented trap/exit tuple), not decimal approximations of floats.

## Derivation and review rules

1. **`spec` is mandatory.**  Cite the exact standard section(s) that determine
   the expectation, plus the diagnostic position rule (§12.5.1) when a code is
   involved.  “The implementation does X” is never a spec citation.
2. **`derivation` is a computation, not a summary.**  Show how the expected
   value follows from the cited clauses: token order, field widths + padding +
   W, the §12.2 first-diagnostic order, or the §11 output algorithm.
3. **Independent review is mandatory.**  Every new or modified golden needs a
   reviewer other than the author, with `review.status: approved` and a date.
   The review record lives in `review-record.md`.
4. **Never backfill from implementation output.**  Static and runtime goldens
   must not be produced by running a compiler/VM and copying its result.
   Differential output is evidence that VM and AOT agree; it is not a
   specification golden.  If implementation output disagrees with a golden,
   investigate the specification and review the golden explicitly — do not
   overwrite it to make the implementation pass.
5. **Uncovered is explicit.**  A missing golden is a coverage gap listed in
   `tests/traceability/`; it is never treated as implicitly passing.

## Validation

```sh
python3 tests/golden/validate.py
```

The validator walks this tree recursively, rejects missing required fields,
unapproved reviews, malformed dates, duplicate ids, and diagnostic goldens
without exactly-one stage/code/primary/diagnostics data.  Exit code 1 means
the golden library is not usable.

## S0 rehearsal sample

`common/lex_syntax_comment_slash.golden.yaml` is a lexical `TC_CE_SYNTAX`
expectation derived from standard §2.1/§2.2/§12.3/§12.5.1.  It is a rehearsal
record only: **it has not been run against any implementation**, and its
review entry is a process placeholder to be replaced by a real second reviewer
before S1 starts using goldens.
