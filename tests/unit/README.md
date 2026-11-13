# L0 contract tests

`tc_contract_test` asserts the frozen S0 contracts. It is registered in CTest
as `contract.unit`.

## L0 case ids

The traceability matrix may cite L0 cases directly. These ids identify the
specific contract assertions; they are not conformance `.tc` cases.

| Case ID | C function | Pins |
| --- | --- | --- |
| `TC-L0-DIAG-0001` | `test_diag_origin_rank_conflict` | §12.2 #9 `origin` dominates §12.2 #4 `same_site_rank`; synthetic comparator pin, not a reachable return-site pair (standard §8.3) |
| `TC-L0-DIAG-0002` | `test_safe_violation_priority` | §12.3/§9 `SAFE_VIOLATION` outranks `UNSAFE_OPERATION`/`UNSAFE_CALL`/`FAST_CALL` within the construct origin class; §12.2 #9 still dominates rank outside the site-specific order |
| `TC-L0-DIAG-0003` | `test_call_argument_order` | §12.2 #10 call order: duplicate → unknown → missing → count → order → type; literal type still beats generic type mismatch |
| `TC-L0-DIAG-0004` | `test_make_field_order` | §12.2 #10 make order: duplicate → unknown → missing → extra positional → order → type |
| `TC-L0-DIAG-0005` | `test_call_safety_first` | §8.8.2/§9 call safety precedes argument checks; `SAFE_VIOLATION` outranks non-`@safe` safety codes |
| `TC-L0-DIAG-0006` | `test_negative_shift_first` | §12.2 #5 negative shift precedes strict `shl` overflow |
| `TC-L0-IR-0001` | `test_ir_shape_and_zero_pass` | §6.15.1 any result instruction may discard with `_`; no-result LHS must be `_`; opcode/terminator shape |
| `TC-L0-TYPE-0001` | `test_type_table` | scalar singletons are materialized before freeze; freeze adds no nodes; intern/freeze identity |

Run directly:

```sh
./build/tests/tc_contract_test
ctest --test-dir build -R contract.unit --output-on-failure
```
