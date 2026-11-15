# Tests

S0 provides the contract/traceability infrastructure; S1 adds the LEX/SYN
front-end suites; S2/S3 add module/IMPORT and name/type/conversion/layout
suites.  The staging, suites, coverage checklist and gates are defined in
[TC 开发测试计划 0.0.46](../docs/TC开发测试计划-0.0.46.md).

| Suite | Location | Level | Current content |
| --- | --- | --- | --- |
| Contract/unit | `unit/` | L0/L1 | S0 contract checks; `tc_frontend_test` (688 checks); `tc_sem_test` (58 checks: type identity, layout, W sizes, freeze, module order/instances) |
| Conformance | `conformance/` | L3 | S1 LEX/SYN cases; S2 import and S3 name/type/conversion/layout cases; acceptance tool cases; property case; see [conformance/README.md](conformance/README.md) |
| Harness | `harness/` | — | Metadata parser, 7 deciders, fail-closed self-tests, adapter for `--syntax-only` and `--static-only` (configured by CMake) |
| Traceability | `traceability/` | — | 80-code table, coverage matrix (634 slots; S0–S3 rows marked covered, later stages explicit), generator checks |
| Golden | `golden/` | L3 | Specification-derived expectations with independent review (`common/`; W-independent LEX/SYN) |
| Tools | `tools/` | L6 | `tc-vm`/`tc-aot` CLI contract (usage, profiles, `--syntax-only`, diagnostics, implementation errors) |
| Determinism | `determinism/` | L7 | repeated runs, hash-seed/order perturbation; byte-identical harness JSON (`s1`, `s2s3`) |

Run everything:

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

S1/S2/S3 gate entries include `frontend.unit`, `sem.unit`,
`conformance.s1_cases`, `conformance.s2s3_cases`, `harness.conformance`,
`adapter.selftest`, `determinism.s1`, `determinism.s2s3` and `cli.contract`.  A
real gate uses the adapter with no `--allow-not-implemented`;
`not_implemented` is never a pass.

Case-level conformance suites are organized by normative object, not by
implementation module.  The static expectations must cite the standard
(`spec`) and are never derived from compiler output.  Differential tests
(S7+) compare VM and AOT at the same profile; 32/64 results are never compared
cross-profile.
