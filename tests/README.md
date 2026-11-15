# Tests

S0 provides the contract/traceability infrastructure; S1 adds the LEX/SYN
front-end suites.  The staging, suites, coverage checklist and gates are
defined in [TC 开发测试计划 0.0.46](../docs/TC开发测试计划-0.0.46.md).

| Suite | Location | Level | Current content |
| --- | --- | --- | --- |
| Contract/unit | `unit/` | L0/L1 | S0 contract checks; `tc_frontend_test` (688 checks: exact literals, tokens/positions, all 51 opcodes, parse IR shape, LEX/SYN codes, UTF-8 spans, ordering, limits, determinism) |
| Conformance | `conformance/` | L3 | S1 LEX/SYN cases + acceptance tool cases + property case; see [conformance/README.md](conformance/README.md) |
| Harness | `harness/` | — | Metadata parser, 7 deciders, fail-closed self-tests, S1 adapter (configured by CMake) |
| Traceability | `traceability/` | — | 80-code table, coverage matrix (634 slots; S1 marks LEX/SYN rows covered), generator checks |
| Golden | `golden/` | L3 | Specification-derived expectations with independent review (`common/`; W-independent LEX/SYN) |
| Tools | `tools/` | L6 | `tc-vm`/`tc-aot` CLI contract (usage, profiles, `--syntax-only`, diagnostics, implementation errors) |
| Determinism | `determinism/` | L7 | repeated runs, hash-seed/order perturbation; byte-identical harness JSON |

Run everything:

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

S1 gate entries include `frontend.unit`, `conformance.s1_cases`,
`harness.conformance`, `adapter.selftest`, `determinism.s1` and `cli.contract`.  A real gate uses
the adapter with no `--allow-not-implemented`; `not_implemented` is never a
pass.

Case-level conformance suites are organized by normative object, not by
implementation module.  The static expectations must cite the standard
(`spec`) and are never derived from compiler output.  Differential tests
(S7+) compare VM and AOT at the same profile; 32/64 results are never compared
cross-profile.
