# L0 contract tests

`tc_contract_test` asserts the frozen S0 contracts. It is registered in CTest
as `contract.unit`.

## L0 case ids

The traceability matrix may cite L0 cases directly. These ids identify the
specific contract assertions; they are not conformance `.tc` cases.

| Case ID | C function | Pins |
| --- | --- | --- |
| `TC-L0-DIAG-0001` | `test_diag_origin_rank_conflict` | §12.2 #9 `origin` dominates §12.2 #4 `same_site_rank`; synthetic comparator pin, not a reachable return-site pair (standard §8.3) |
| `TC-L0-DIAG-0002` | `test_safe_violation_priority` | §12.3/§9 `SAFE_VIOLATION` outranks `UNSAFE_OPERATION`/`UNSAFE_CALL`/`FAST_CALL` within the construct origin class; §12.2 #9 still dominates rank |

Run directly:

```sh
./build/tests/tc_contract_test
ctest --test-dir build -R contract.unit --output-on-failure
```
