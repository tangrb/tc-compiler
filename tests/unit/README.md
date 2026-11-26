# Unit tests

CTest entries:

- `contract.unit` (`tc_contract_test`): frozen contracts (L0).
- `frontend.unit` (`tc_frontend_test`): LEX/SYN + flat parse IR (L1).
- `sem.unit` (`tc_sem_test`): IMPORT/SEM type identity, layout, word sizes,
  type-table freeze and module order/instances (L1).
- `ct.unit` (`tc_ct_test`): CT vectors for strict integer/float semantics,
  casts/bitcasts/trunc, exact `mod`, `sizeof`/layout, 32/64 results, aggregate
  const images, demand order/cache and local consts (L1).
- `ir.unit` (`tc_ir_test`): finalized IR shape: module/struct/const/object
  identity, types and profile, flat operands/field paths, CFG edges, named
  arguments, const inlining, word-size layout and the deterministic printer
  (L1).
- `vm.unit` (`tc_vm_test`): runtime semantic core, exact scalar formatting,
  token input, regions/objects/pointer states, all mandatory traps and their
  positions, calls/`D`, device policies, I/O source states, checked build and
  the W=32/W=64 matrix (L1/L2).
- `project.paths_unit` (`tc_path_test`): canonical/display path text and
  allocation, internal/external/fallback display, IR module display metadata,
  fd-bound hardlink/symlink module identity and symlink-parent root binding (P2).
- `aot.unit` (`tc_aot_test`): AArch64 lowering shape for integer/float/memory
  helpers, frame alignment, HFA and indirect-return ABI, checked-build shadow
  and define tracking, static initializer emission, deterministic assembly,
  large-function-safe conditional/switch lowering, and explicit rejection of
  unsupported platform instances (L1).

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
currently runs 102 checks.

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


## L1 CT tests (`tc_ct_test`)

`tc_ct_test` evaluates spec-derived CT vectors through `tc_analyze` and reads
the CT value table directly.  It currently runs 370 checks.  Expectations come
from the standard (integer strict/mode matrix, IEEE strict arithmetic and
exception priority, exact `mod`, `cast`/`trunc`/`bitcast`, `sizeof`/layout and
the 32/64 matrix), never from compiler output.

| Case ID | Function | Pins |
| --- | --- | --- |
| `TC-UNIT-CT-0001` | `run_vector` | §3.10/§6.3-§6.6 CT vectors: strict signed overflow, unsigned modular wrap, `abs`/`neg` edges, div/mod edges, negative/infinite shift counts, odd shifts, strict float invalid/div-zero/overflow/underflow (tininess after rounding, exact subnormals), exact floating `mod`, NaN comparisons and sign ops, cast/trunc/bitcast, `sizeof`/layout and 32/64 result flips |
| `TC-UNIT-CT-0002` | `test_ct_cache` | §4.10 demand order and success cache: a diamond dependency evaluates the shared const once (`tc_ct_eval_count`) |
| `TC-UNIT-CT-0003` | `test_ct_failure_cache` | §4.10 first failure stops CT, is attributed to the target declaration, is evaluated once, and yields no `TcIR` |
| `TC-UNIT-CT-0004` | `test_local_const` | §5.2 function-local `const` evaluation, later-local reference and cache |

## L1/L2 VM tests (`tc_vm_test`)

`tc_vm_test` links the private runtime/VM interfaces and also analyzes complete
programs with an in-memory I/O harness.  It currently runs 413 checks.

| Case ID | Function | Pins |
| --- | --- | --- |
| `TC-UNIT-VM-0001` | `test_integer_semantics` | §6.3 strict signed overflow, unsigned wrap, `.wrap`, division/modulo edges, `div(INT_MIN,-1)`, negative shift precedence, `k>=n` shifts, W-dependent isize overflow and 64-bit types on W=32 |
| `TC-UNIT-VM-0002` | `test_float_semantics` | §6.3.1-§6.3.3 strict exception priority, `.ieee` inf/NaN, exact floating `mod` signs and `b=inf`, exact subnormal vs inexact underflow, `abs`/`neg` sign-bit behavior, NaN comparisons |
| `TC-UNIT-VM-0003` | `test_conversions` | §6.6 `cast` range/truncation/NaN/inf, float32↔float64 and overflow, same-type NaN payload, cross-type canonical NaN, bool truth rules, `trunc` low bits, `bitcast` bit strings and W widths |
| `TC-UNIT-VM-0004` | `test_formatting` | §11.2 integers/bool, float `%g` six-digit rounding, fixed/scientific threshold, `-0`, `inf`/`nan`, binary32 rendering and min/max finite values |
| `TC-UNIT-VM-0005` | `test_input` | §11.3 complete-token parsing, whitespace/leading zeros, `-0`, `nan`, invalid/range tokens, EOF/error source states and single-byte short reads |
| `TC-UNIT-VM-0006` | `test_program_*` | §1.2/§1.3/§12.5.2 complete programs: all 15 `TC_RE_*` traps and their instruction positions, committed output before a trap, call-depth `D` boundary, struct/pointer/memmove behavior, all four `TcDevicePolicy` values, I/O source OK/EOF/ERROR and sink failure, checked-build bounds, and the W=32/W=64 output matrix |

## L1 IR tests (`tc_ir_test`)

`tc_ir_test` analyzes small multi-module programs and inspects the finalized
`TcIR` directly.  It currently runs 674 checks.

| Case ID | Function | Pins |
| --- | --- | --- |
| `TC-UNIT-IR-0001` | `test_ir_shape` | design §3/§12.11: `module_order` modules, struct layout/ids, `TcIR.consts` (scalar bits, aggregate image, no static var), static/frame objects and init images, function module ids/signatures/flags/bodies, `br_if` keeps both edges, named `make` field, field operand paths, span retention, const inlining |
| `TC-UNIT-IR-0002` | `test_ir_control_flow` | design §3.3/§12.11: `switch` target block ids, unique CFG edges, `trap` terminal, unreachable block retained with no predecessors |
| `TC-UNIT-IR-0003` | `test_ir_aggregate_const` | design §3.1/§3.4: aggregate const image bytes; use-site synthetic `make` for nested const structs; `const` owns no object |
| `TC-UNIT-IR-0004` | `test_ir_word_sizes` | standard §13.2.1/§13.2.2 and design §2.2: per-profile struct size, static object size/alignment, `sizeof isize/ptr/fnptr` const values in 32/64 |
| `TC-UNIT-IR-0005` | `test_ir_forms` | design §12.11: field lvalue, `addr` on frame and static fields, `funcaddr`/`call`/`call_indirect` operands and named call argument, `sizeof` type parameter, static field object mapping |
| `TC-UNIT-IR-0006` | `test_ir_print` | deterministic textual dump: exact golden text, 1-based spans, repeated print is identical |
| `TC-UNIT-IR-0007` | `test_ir_sem_only` | a SEM-only analysis has no finalized `TcIR` |
| `TC-UNIT-IR-0008` | `test_ir_static_layout` | standard §13.3: static objects use import-DAG reverse topological order (importers first, `module_order` ties), module source order, W/8 alignment and one object per module instance in a diamond |
| `TC-UNIT-IR-0009` | `test_ir_multi_function_bodies` | design §3.4: with multiple library functions, every `TcFunc.body` points into the final `TcIR.units` array and matches its `func_id` after unit-array growth |
| `TC-UNIT-IR-0010` | `test_ir_switch_successor_capacity` | design §3.3: a switch with 12 targets produces 12 unique successors without a fixed-size scratch buffer |

## L1 AArch64 backend tests (`tc_aot_test`)

`tc_aot_test` analyzes small programs, emits assembly to memory and inspects the
lowering shape.  It currently runs 81 checks and needs no qemu/linker.

| Case ID | Function | Pins |
| --- | --- | --- |
| `TC-UNIT-AOT-0001` | `test_lowering_shapes` | design §10.3: native strict add overflow (`adds`/`b.vs`), signed `sdiv`/`msub`, shift-count checks, signed/unsigned `cset`, call depth, far-safe switch compare chain, `sizeof` immediate and frame prologue |
| `TC-UNIT-AOT-0002` | `test_frame_alignment` | design §10.2: every emitted `sub sp, sp, #N` frame size is a multiple of 16 |
| `TC-UNIT-AOT-0003` | `test_unsupported` | design §10.3: pointer/load programs return `TC_AARCH64_EMIT_UNSUPPORTED` with an explicit unsupported-opcode reason |
| `TC-UNIT-AOT-0004` | `test_static_initializer` | design §4/§10: static object bytes appear in `__tc_static_region` at their object offsets |
| `TC-UNIT-AOT-0005` | `test_deterministic` | design §10.1: two emissions of the same IR are byte-identical |
