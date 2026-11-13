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
| `TC-L0-TYPE-0001` | `test_type_table` | scalar singletons are materialized before freeze; freeze adds no nodes; `void` is not a scalar value type; intern/freeze identity |
| `TC-L0-CONTRACT-0001` | `test_contract_declarations` | enum values, struct fields, defaults, sentinels, operand kinds, `TcInstr` mode/type/args/switch/span, 1-based positions and byte/bit units |
| `TC-L0-OBJ-0001` | `test_pointer_states` | standard §3.12 four-state pointer-permission matrix (null/live/one-past/dangling; bitcast only live/dangling) |
| `TC-L0-DIAG-0007` | `test_call_check_phase_order` | §8.8.2 call/make check phases: target/visibility → safety → LHS/return shape → arguments → receiver |
| `TC-L0-DIAG-0008` | `test_site_id_scope` | `site_id` scopes §12.2 #10 to one call/make site; zero id/phase and out-of-range phase are rejected by the buffer; distinct sites fall back to source order |

Run directly:

```sh
./build/tests/tc_contract_test
ctest --test-dir build -R contract.unit --output-on-failure
```
