# Golden independent-review record (S0)

This file records the independent review of the S0 golden sample. The sample
expectation is derived from the standard only; no compiler or VM was run and
nothing was backfilled from implementation output.

```json
{
  "schema": "tc-golden-review-record/1",
  "language_version": "0.0.46",
  "rehearsal": false,
  "date": "2026-11-13",
  "scope": "S0 golden layout, derivation and independent review",
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
      "author_role": "traceability/golden S0 author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "commit": "3d2c1ef",
      "evidence_version": "S0-2026-11-13",
      "status": "approved",
      "date": "2026-11-13",
      "notes": "Independent audit re-derived line 2 column 30 from §2.1/§2.2/§12.3/§12.5.1; the comment-external first slash is the sole lexical diagnostic."
    }
  ],
  "validator": {
    "command": "python3 tests/golden/validate.py",
    "result": "pass"
  }
}
```
