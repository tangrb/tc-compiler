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
  "scope": "S0 golden layout/derivation; S1 LEX/SYN goldens; S6 runtime goldens (integer/float formatting, control flow, memory, I/O, traps, device policy, word size)",
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
    },
    {
      "artifact": "tests/golden/w64/run_int_format.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "2d3132380a31383434363734343037333730393535313631350a747275650a2d393232333337323033363835343737353830380a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0001 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/run_float_format.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "302e310a2d300a31652b30360a302e303030310a6e616e0a2d696e660a302e310a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0002 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/run_control_flow.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "31300a320a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0003 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/run_memory_struct.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "310a310a330a340a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0004 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/run_io_read.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "2d370a322e350a66616c73650a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0005 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/run_trap_divzero.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "310a",
        "trap": "TC_RE_DIV_ZERO",
        "exit": 1,
        "termination": "trap"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0006 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/run_ieee_float.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "696e660a6e616e0a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0007 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/run_device_reject.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "",
        "trap": "TC_RE_ADDR_UNAVAILABLE",
        "exit": 1,
        "termination": "trap"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0008 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w32/run_word_size.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "340a340a380a323134373438333634370a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0101 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w32/run_isize_overflow.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "",
        "trap": "TC_RE_INTEGER_OVERFLOW",
        "exit": 1,
        "termination": "trap"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0102 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w32/run_usize_max.golden.yaml",
      "author_role": "S6 runtime/test author",
      "reviewer": "gate-auditor (independent read-only subagent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent derivation audit",
      "status": "approved",
      "date": "2026-11-16",
      "derived": {
        "stdout_hex": "343239343936373239350a",
        "trap": null,
        "exit": 0,
        "termination": "normal"
      },
      "notes": "Independently re-derived TC-RT-GOLD-0103 from the cited standard/design clauses and the documented reference profile; no tc-vm run was used to produce the expectation."
    },
    {
      "artifact": "tests/golden/w64/observe_aot_static_writeln.golden.yaml",
      "author_role": "T1 observation/test author",
      "reviewer": "T1 independent derivation reviewer (read-only agent)",
      "reviewer_kind": "independent-agent",
      "reviewer_independent": true,
      "review_kind": "independent specification derivation",
      "status": "approved",
      "date": "2026-11-27",
      "derived": {
        "stdout_hex": "",
        "stderr_hex": "",
        "exit": 0,
        "stats": "# tc-stats 1 tc-aot aarch64-linux-user\nfuncs=0\nir_instrs=1\nstatic_bytes=0\nop.writeln=1\n"
      },
      "notes": "Approved from frozen toolchain sections 5.1/5.2/5.2.1/5.5/5.9 and language/design clauses 4.1-4.3/4.9/6.2.9/7.1/11 and design 3/10.1; no compiler or VM run and no implementation-output backfill."
    }
  ],
  "validator": {
    "command": "python3 tests/golden/validate.py",
    "result": "pass"
  },
  "s6_reviews": {
    "scope": "S6 runtime goldens (11 kind:run); w64/w32; current content after the field-subobject correction",
    "reviewer": "gate-auditor (independent read-only subagent)",
    "reviewer_kind": "independent-agent",
    "reviewer_independent": true,
    "review_kind": "independent derivation audit",
    "date": "2026-11-16",
    "implementation_run": false,
    "concerns": [
      "Device address 0x20000000 is an environment-defined profile fact; the golden now cites §13.1 and src/CONTRACTS.md, and the runner selects aarch64-linux-user explicitly.",
      "Normal exit 0 / trap exit 1 are environment-defined TcExitPolicy values; every derivation cites §13.1/design §8 and src/CONTRACTS.md.",
      "Contiguous struct fields are separate sub-objects: memmove with n>1 across sibling fields is UB. The runtime golden uses n=1 and a one-past ptrdiff; the conformance self-copy covers the overlap path.",
      "Spec citations include §11.1/§11.2 where output bytes or pre-trap retention are asserted."
    ]
  }
}
```
