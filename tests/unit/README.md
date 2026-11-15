# Unit tests

CTest entries:

- `contract.unit` (`tc_contract_test`): frozen contracts (L0).
- `frontend.unit` (`tc_frontend_test`): LEX/SYN + flat parse IR (L1).
- `sem.unit` (`tc_sem_test`): IMPORT/SEM type identity, layout, word sizes,
  type-table freeze and module order/instances (L1).

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
| `TC-L0-CONTRACT-0001` | `test_contract_declarations` | enum values, struct fields (including full-width `padding`/`align`), defaults, sentinels, operand kinds, `TcInstr` mode/type/args/switch/span, 1-based positions and byte/bit units |
| `TC-L0-OBJ-0001` | `test_pointer_states` | standard §3.12 four-state pointer-permission matrix (null/live/one-past/dangling; bitcast only live/dangling) |
| `TC-L0-DIAG-0007` | `test_call_check_phase_order` | §8.8.2 call/make check phases: target/visibility → safety → LHS/return shape → arguments → receiver |
| `TC-L0-DIAG-0008` | `test_site_id_scope` | `site_id` scopes §12.2 #10 to one call/make site; zero id/phase and out-of-range phase are rejected by the buffer; distinct sites fall back to source order |
| `TC-L0-DIAG-0009` | `test_diag_ordering` | §12.2 #1 stage, #2 module order, #3 source order and #4 same-position behavior |

Run directly:

```sh
./build/tests/tc_contract_test
ctest --test-dir build -R contract.unit --output-on-failure
```


## L1 front-end tests (`tc_frontend_test`)

`tc_frontend_test` calls the private `frontend/` interfaces directly.  It is
registered as `frontend.unit` and currently runs 688 checks.

| Case ID | C function | Pins |
| --- | --- | --- |
| `TC-UNIT-NUM-0001` | `test_num` | §2.4/§6.6.3 exact decimal -> binary32/64: bit patterns, ties-to-even, subnormal/overflow boundaries, negative zero |
| `TC-UNIT-LEX-0001` | `test_lex_tokens` | §2.3/§2.4 token kinds and literal values: identifiers, `-0x1F`, `1_000u`, `0.5f`, `-0.0`, `@padding/@unsafe/@readonly` |
| `TC-UNIT-LEX-0002` | `test_lex_mnemonics` | §6.2 all defined suffixes and undefined `(base,suffix)` combinations |
| `TC-UNIT-LEX-0003` | `test_lex_positions`, `test_lex_errors` | §2/§12.5.1 spans, CRLF/CR, comments/continuations, illegal bytes/scalars, BOM/U+0000, numeric forms, `-nan`, literal ranges |
| `TC-UNIT-LEX-0005` | `test_lex_errors` | §2.7.1 recovery convention: malformed numeric candidates (`1.5.5`/`1..5`/`1.5e5.5`) are one LEX token at the candidate start |
| `TC-UNIT-UTF8-0001` | `test_utf8_spans` | §12.5.1 longest valid prefix + 0-based invalid-byte offset for invalid/truncated UTF-8; `TC_SPAN_NO_INVALID_BYTE` for valid-but-illegal scalars |
| `TC-UNIT-SYN-0001` | `test_parse_program_shape` | §14.3/§14.4/§14.5 flat parse IR: regions, fields/padding, named args, `sizeof` type arg, field paths/lvalue, retained base/field spans and import-name span |
| `TC-UNIT-SYN-0002` | `test_lib_shape`, `test_unsafe_depth` | §4.2/§8.1 `#lib` regions, annotations/params, qualified type member spans, nested unsafe markers/depth, paired-vs-unpaired nested block behavior |
| `TC-UNIT-SYN-0003` | `test_switch_targets` | §6.2.8 explicit `switch` targets retained in order; a one-operand `switch` has zero targets (static analysis reports the shape error) |
| `TC-UNIT-SYN-0004` | `test_field_annotation_shape` | §2.6/§8.6/§14.3: SYN accepts `ANNOTATION(INT)` field padding (`@align(4)`/`@fast(1)`/`@padding(4)`), while a bare field annotation is SYNTAX; name/N constraints are SEM checks |
| `TC-UNIT-OPCODE-0001` | `test_all_opcodes` | all 51 base opcodes and every defined mode suffix parse to the expected op/mode |
| `TC-UNIT-DIAG-0001` | `test_diagnostics` | the seven LEX/SYN codes, stages and §12.5.1 primary positions; nested paired/unpaired blocks; `#program` `Self` in IDENT positions; incomplete vs whitespace-separated suffixes |
| `TC-UNIT-ORDER-0001` | `test_first_diagnostic_order` | §12.2 #1 stage priority: a later LEX error beats an earlier SYN error |
| `TC-UNIT-ORDER-0002` | `test_first_diagnostic_order` | §12.2 #3 source order for two SYN errors |
| `TC-UNIT-ORDER-0003` | `test_first_diagnostic_order` | §12.2 #3 earlier column at the same source line |
| `TC-UNIT-LIMIT-0001` | `test_nesting_limit` | documented nesting limits are `TC_IMPL_INTERNAL`, never language diagnostics |
| `TC-UNIT-LIMIT-0002` | `test_token_limits` | a pathological float token is `TC_IMPL_INTERNAL`; a huge integer token is still the language `TC_CE_LITERAL_LEX_RANGE` |
| `TC-UNIT-DET-0001` | `test_determinism` | repeated parse/lex produce identical structure fingerprints and tokens |
| `TC-UNIT-ACCEPT-0001` | `test_accept_forms` | representative legal EBNF forms are not rejected |

## L1 SEM tests (`tc_sem_test`)

`tc_sem_test` calls the private `frontend/sem.h` interface directly.  It
currently runs 87 checks.

| Case ID | C function | Pins |
| --- | --- | --- |
| `TC-UNIT-LAYOUT-0001@w32`, `@w64` | `test_layout` | §3.7/§6.13/§13.3 compact fields, `@padding` 0x00 bytes, no implicit alignment/tail padding, offsets/size; §13.2.1 ptr/fnptr/isize/usize sizes per W |
| `TC-UNIT-TYPE-0001` | `test_type_identity` | §3.9 scalar singletons, interned `ptr<T>`, nominal structs, `isize` != `int64`, `fnptr` per-signature equivalence |
| `TC-UNIT-CONV-0001@w32`, `@w64` | `test_conversion_flip` | §13.2.2 `bitcast int32,isize` is W-dependent; the type table is frozen after Analyze |
| `TC-UNIT-ANN-0001` | `test_annotation_metadata` | §8.6 const-path endpoints are evaluated and written to `TcParam.align`/`lo`/`hi`; 64-bit `@align`/`@padding` metadata is not truncated |
| `TC-UNIT-MODULE-0001` | `test_module_order` | §4.11.1 diamond import order `root, a, c, b`; each module parsed once |
| `TC-UNIT-MODULE-0002` | `test_module_instances` | §4.5 same module name at two resolved paths is two instances |

Run directly:

```sh
./build/tests/tc_frontend_test
./build/tests/tc_sem_test
ctest --test-dir build -R 'frontend.unit|sem.unit' --output-on-failure
```
