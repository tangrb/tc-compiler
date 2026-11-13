# Golden review rehearsal record (S0)

This file is the machine-readable record for the S0 golden-review rehearsal.
The canonical payload is the JSON object in the fenced block below.  It records
the process only; **no compiler or VM was run**, and no expectation was
backfilled from implementation output.

```json
{
  "schema": "tc-golden-review-record/1",
  "language_version": "0.0.46",
  "rehearsal": true,
  "date": "2026-11-13",
  "scope": "S0 golden layout, derivation and independent-review rehearsal",
  "implementation_run": false,
  "policy": {
    "expectation_source": "standard clauses only",
    "backfill_from_implementation_forbidden": true,
    "diff_output_is_not_a_golden": true,
    "approved_review_required": true,
    "common_profile_is_w_independent_only": true
  },
  "sample": {
    "path": "tests/golden/common/lex_syntax_comment_slash.golden.yaml",
    "id": "TC-LEX-COMMENT-0001",
    "kind": "diagnostic",
    "spec": ["§2.1", "§2.2", "§12.3", "§12.5.1"],
    "expected": {
      "stage": "LEX",
      "code": "TC_CE_SYNTAX",
      "primary": {"line": 2, "column": 30},
      "diagnostics": "exactly_one"
    },
    "derivation_reviewed": true,
    "implementation_run": false
  },
  "reviews": [
    {
      "artifact": "tests/golden/common/lex_syntax_comment_slash.golden.yaml",
      "author_role": "traceability/golden S0 rehearsal author",
      "reviewer": "S0 independent-review rehearsal (placeholder; replace before S1)",
      "reviewer_independent": true,
      "review_kind": "process rehearsal",
      "status": "approved",
      "date": "2026-11-13",
      "notes": "Placeholder identity for the S0 rehearsal only. A named second reviewer must replace it before S1 accepts real goldens."
    }
  ],
  "validator": {
    "command": "python3 tests/golden/validate.py",
    "result": "expected to pass for this sample"
  }
}
```

## Notes

- The reviewer entry is intentionally marked as a rehearsal placeholder.  It
  demonstrates the required `review.reviewer` / `review.status` / `review.date`
  fields without pretending that a production sign-off already happened.
- The sample expectation is derived in its `derivation` field from standard
  §2.1, §2.2, §12.3 and §12.5.1.  The validation command only checks the record
  shape; it does not and cannot run a compiler.
