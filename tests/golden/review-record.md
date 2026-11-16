# Golden independent-review record (S0)

This file records the independent review of the S0 golden sample. The sample
expectation is derived from the standard only; no compiler or VM was run and
nothing was backfilled from implementation output.

```json
{
  "schema": "tc-golden-review-record/1",
  "language_version": "0.0.46",
  "rehearsal": false,
  "date": "2026-11-15",
  "scope": "S0 golden layout/derivation; S1 LEX/SYN goldens (derivation and independent review)",
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
    "spec": [
      "§2.1",
      "§2.2",
      "§12.3",
      "§12.5.1"
    ],
    "expected": {
      "stage": "LEX",
      "code": "TC_CE_SYNTAX",
      "primary": {
        "line": 2,
        "column": 30
      },
      "diagnostics": "exactly_one"
    },
    "derivation_reviewed": true,
    "implementation_run": false
  },
  "s1_reviews": {
    "scope": "S1 (LEX/SYN) specification-derived goldens; no compiler/VM run",
    "reviewer": "gate-auditor (independent read-only subagent)",
    "reviewer_kind": "independent-agent",
    "reviewer_independent": true,
    "review_kind": "independent derivation audit",
    "date": "2026-11-15",
    "policy": {
      "expectation_source": "standard clauses only",
      "implementation_run": false,
      "backfill_from_implementation_forbidden": true
    },
    "artifacts": [
      {
        "artifact": "tests/golden/common/lex_non_ascii_column.golden.yaml",
        "id": "TC-LEX-GOLD-0001",
        "derived": {
          "stage": "LEX",
          "code": "TC_CE_SYNTAX",
          "primary": {
            "line": 2,
            "column": 30
          }
        },
        "verdict": "approved"
      },
      {
        "artifact": "tests/golden/common/lex_literal_lex_range_negative.golden.yaml",
        "id": "TC-LEX-GOLD-0002",
        "derived": {
          "stage": "LEX",
          "code": "TC_CE_LITERAL_LEX_RANGE",
          "primary": {
            "line": 2,
            "column": 28
          }
        },
        "verdict": "approved"
      },
      {
        "artifact": "tests/golden/common/syn_missing_end_earliest.golden.yaml",
        "id": "TC-SYN-GOLD-0001",
        "derived": {
          "stage": "SYN",
          "code": "TC_CE_MISSING_END",
          "primary": {
            "line": 2,
            "column": 1
          }
        },
        "verdict": "approved"
      },
      {
        "artifact": "tests/golden/common/syn_module_layer.golden.yaml",
        "id": "TC-SYN-GOLD-0002",
        "derived": {
          "stage": "SYN",
          "code": "TC_CE_MODULE_LAYER",
          "primary": {
            "line": 3,
            "column": 1
          }
        },
        "verdict": "approved"
      },
      {
        "artifact": "tests/golden/common/syn_discard_field.golden.yaml",
        "id": "TC-SYN-GOLD-0003",
        "derived": {
          "stage": "SYN",
          "code": "TC_CE_SYNTAX",
          "primary": {
            "line": 3,
            "column": 14
          }
        },
        "verdict": "approved"
      },
      {
        "artifact": "tests/golden/common/syn_var_missing_init.golden.yaml",
        "id": "TC-SYN-GOLD-0004",
        "derived": {
          "stage": "SYN",
          "code": "TC_CE_VAR_MISSING_INIT",
          "primary": {
            "line": 2,
            "column": 19
          }
        },
        "verdict": "approved"
      },
      {
        "artifact": "tests/golden/common/syn_program_mode_self.golden.yaml",
        "id": "TC-SYN-GOLD-0005",
        "derived": {
          "stage": "SYN",
          "code": "TC_CE_PROGRAM_MODE_MISUSE",
          "primary": {
            "line": 2,
            "column": 1
          }
        },
        "verdict": "approved"
      }
    ],
    "concerns": [
      "MODULE_LAYER primary is the construct first token, including the #lib visibility keyword.",
      "Multiple unpaired block openers produce one MISSING_END at the earliest opener, not one per opener.",
      "TC_CE_SYNTAX is dual-stage: golden cases assert the concrete LEX or SYN stage.",
      "Unicode-scalar columns are additionally exercised by unit tests using a non-ASCII comment prefix followed by an invalid UTF-8 byte, where scalar and byte columns diverge."
    ]
  },
  "reviews": [
    {
      "artifact": "tests/golden/common/lex_syntax_comment_slash.golden.yaml",
      "author_role": "traceability/golden S0 author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-13",
      "notes": "Independent audit re-derived line 2 column 30 from §2.1/§2.2/§12.3/§12.5.1; the comment-external first slash is the sole lexical diagnostic."
    },
    {
      "artifact": "tests/golden/common/lex_non_ascii_column.golden.yaml",
      "author_role": "S1 front-end/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-15",
      "notes": "Independently re-derived LEX/TC_CE_SYNTAX at 2:30; no compiler/VM run."
    },
    {
      "artifact": "tests/golden/common/lex_literal_lex_range_negative.golden.yaml",
      "author_role": "S1 front-end/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-15",
      "notes": "Independently re-derived LEX/TC_CE_LITERAL_LEX_RANGE at 2:28; no compiler/VM run."
    },
    {
      "artifact": "tests/golden/common/syn_missing_end_earliest.golden.yaml",
      "author_role": "S1 front-end/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-15",
      "notes": "Independently re-derived SYN/TC_CE_MISSING_END at 2:1; no compiler/VM run."
    },
    {
      "artifact": "tests/golden/common/syn_module_layer.golden.yaml",
      "author_role": "S1 front-end/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-15",
      "notes": "Independently re-derived SYN/TC_CE_MODULE_LAYER at 3:1; no compiler/VM run."
    },
    {
      "artifact": "tests/golden/common/syn_discard_field.golden.yaml",
      "author_role": "S1 front-end/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-15",
      "notes": "Independently re-derived SYN/TC_CE_SYNTAX at 3:14; no compiler/VM run."
    },
    {
      "artifact": "tests/golden/common/syn_var_missing_init.golden.yaml",
      "author_role": "S1 front-end/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-15",
      "notes": "Independently re-derived SYN/TC_CE_VAR_MISSING_INIT at 2:19; no compiler/VM run."
    },
    {
      "artifact": "tests/golden/common/syn_program_mode_self.golden.yaml",
      "author_role": "S1 front-end/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-15",
      "notes": "Independently re-derived SYN/TC_CE_PROGRAM_MODE_MISUSE at 2:1; no compiler/VM run."
    }
  ],
  "validator": {
    "command": "python3 tests/golden/validate.py",
    "result": "pass"
  }
}
```
