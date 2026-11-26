# Golden expectation layout (0.0.46)

This directory is the **specification-derived** golden library.  It is not a
copy of any compiler output.  Review metadata follows the test-plan requirements.

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
kind: diagnostic          # diagnostic, run or observe (see below)
profile: common           # common | w32 | w64; must match the directory
spec: "§2.1 ...; §12.3 ...; §12.5.1 ..."
derivation: |
  Why this is the required expectation, computed only from the standard.
review:
  reviewer: "..."         # independent non-author reviewer identity
  reviewer_independent: true
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
declaration of a duplicate name).

For runtime goldens, use `kind: run` and `expect.stdout_hex`, `expect.trap`
(`null` or a `TC_RE_*` code), `expect.exit` and `expect.termination`
(`normal`/`trap`).  `run.stdin_hex` supplies exact stdin bytes.  The source is a
complete `#program`; the runtime golden runner executes `tc-vm` explicitly with
`--profile=w32` for `w32/` and `--profile=aarch64-linux-user` for `w64/`, then
compares stdout bytes, trap, exit and termination.  Floating expectations are derived with an independent exact
decimal renderer, never from VM output.

For T1 observation goldens, use `kind: observe` under `w64/` with
`tool: tc-aot`. The runner invokes `tc-aot --static-only --stats` with an
explicit stats path and compares exact stdout, stderr, exit status and stats
bytes. Expectations derive from the frozen 0.0.47 toolchain §§5.1/5.2.1/5.5
and the language clauses that determine the finalized IR, not from captured
tool output.

## Derivation and review rules

1. **`spec` is mandatory.**  Cite the exact standard or frozen toolchain
   section(s) that determine the expectation, plus the diagnostic position rule
   (§12.5.1) when a code is involved.  “The implementation does X” is never a
   spec citation.
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
python3 tests/golden/run.py <path-to-tc-vm>
```

The validator walks this tree recursively and rejects missing required fields,
unknown fields, duplicate keys/ids, unapproved or placeholder reviews,
malformed dates, code names/stages that are not in the frozen 80-code table,
source/profile/kind violations, and diagnostic goldens without exactly-one
stage/code/primary/diagnostics data.  For the repository tree it also
cross-checks the required reviewer/status/date and independence metadata in
`review-record.md`.  Exit code 1 means the golden library is not usable.
`tests/golden/selftest.py` exercises the positive and negative validator paths.
`tests/golden/run.py` is the L4 execution gate for `kind: run` and `kind: observe`
goldens; `w32` and `w64` runtime libraries are executed separately and never
compared with each other.

## Golden samples

`common/lex_syntax_comment_slash.golden.yaml` is a lexical `TC_CE_SYNTAX`
sample derived from standard §2.1/§2.2/§12.3/§12.5.1, with the expected primary
position at 2:30.

The diagnostic set includes seven LEX/SYN goldens derived only from the standard:

| ID | File | Expectation |
| --- | --- | --- |
| `TC-LEX-GOLD-0001` | `common/lex_non_ascii_column.golden.yaml` | LEX `TC_CE_SYNTAX` at 2:30 (Unicode scalar column) |
| `TC-LEX-GOLD-0002` | `common/lex_literal_lex_range_negative.golden.yaml` | LEX `TC_CE_LITERAL_LEX_RANGE` at the `-`, 2:28 |
| `TC-SYN-GOLD-0001` | `common/syn_missing_end_earliest.golden.yaml` | SYN `TC_CE_MISSING_END` at the earliest opener, 2:1 |
| `TC-SYN-GOLD-0002` | `common/syn_module_layer.golden.yaml` | SYN `TC_CE_MODULE_LAYER` at the construct first token, 3:1 |
| `TC-SYN-GOLD-0003` | `common/syn_discard_field.golden.yaml` | SYN `TC_CE_SYNTAX` at DISCARD `_`, 3:14 |
| `TC-SYN-GOLD-0004` | `common/syn_var_missing_init.golden.yaml` | SYN `TC_CE_VAR_MISSING_INIT` at the identifier, 2:19 |
| `TC-SYN-GOLD-0005` | `common/syn_program_mode_self.golden.yaml` | SYN `TC_CE_PROGRAM_MODE_MISUSE` at `Self`, 2:1 |

The runtime set contains eleven `kind: run` goldens: eight under `w64/`
(integer/float formatting, control flow, memory/struct, stdin I/O, division trap,
`.ieee` inf/NaN, device rejection) and three under `w32/` (word sizes, isize
overflow trap, usize maximum).  Current test suites also exercise these
conventions: the `MODULE_LAYER` construct
first token includes the `#lib` visibility keyword; multiple unclosed block
openers yield one `MISSING_END` at the earliest opener; `TC_CE_SYNTAX` cases
must assert the concrete LEX or SYN stage; and Unicode-scalar column counting is
further exercised by unit tests where scalar and byte columns diverge.
