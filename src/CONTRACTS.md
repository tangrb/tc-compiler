# TC 0.0.46 frozen contracts

**Status: frozen for the 0.0.46 clean-sheet implementation.** This file is the
review index for the frozen contract deliverables. Language legality, diagnostics
and observable behavior remain governed only by
[the 0.0.46 standard](../docs/TC语言及编译器标准-0.0.46.md); architecture is
governed by [the design](../docs/TC原生编译器设计说明书-0.0.46.md).

Shared infrastructure lives outside the four language layers so no phase owns it:
`base/` (assert/version), `contract/` (freeze check and zero-pass policy) and
`diagnostic/` (closed code set and first-diagnostic selection, also used by the
runtime trap seam).  `frontend/` owns the LEX/SYN/IMPORT/SEM pipeline.

## Ownership and freeze markers

| Contract | Header(s) | Owner | Frozen facts | Consumers |
| --- | --- | --- | --- | --- |
| Foundation | `base/assert.h`, `base/version.h` | contract/build | compile-time assertions; language/contract version | all modules |
| Type identity | `ir/type.h` | IR + SEM | scalar singletons; interned `ptr`/`fnptr`/struct nodes; pointer equality; no W-dependent layout in a type; freeze after Analyze | SEM, CT, VM, AOT |
| Span | `ir/span.h` | LEX/SYN | file + 1-based line/column (Unicode scalar column); LEX valid-prefix bytes + 0-based invalid byte offset; `TC_SPAN_NO_INVALID_BYTE` | all diagnostics |
| Operands/args | `ir/operand.h` | SYN + SEM | VREG/FIELD/CONST/LABEL/FUNC/STRUCT/DISCARD; `_` is LHS-only; `TcArg.name == NULL` means positional; field names disappear after SEM | SEM, IR freeze, VM, AOT |
| Instruction | `ir/instr.h`, `ir/instr.c` | SYN + SEM + IR | 51 opcodes in standard §6.2 order; mode set; `dst` VREG/FIELD/DISCARD; named args retained; explicit switch targets; no-result and terminator structure | SEM, CT, VM, AOT |
| CFG/unit | `ir/block.h` | SEM | explicit successor/predecessor edges; one closed unit per function/program; vreg→type and vreg→object maps | SEM DI, VM, AOT |
| Struct/layout | `ir/structs.h` | SEM | field order/padding/readonly; per-profile `offset`/`size`; layout is never cached in `TcType` | SEM, CT, VM, AOT |
| Object/address | `ir/object.h`, `ir/address.h`, `ir/region.h` | SEM + runtime | five regions; object is the single boundary/lifetime/DI fact source; region+offset is not object identity; address 0 is reserved | VM, checked build, AOT |
| Pointer state | `ir/pointer_state.h`, `ir/pointer_state.c` | SEM + VM + AOT | §3.12 null/live/one-past/dangling permission matrix; one-past `ptradd` only zero; shared by addr/ptrdiff/memmove/bounds checks | SEM, VM, AOT |
| IR root | `ir/ir.h` | IR | `TcIR` binds exactly one profile; module identity is resolved path; frozen type table; per-profile consts/objects/units | VM, AOT |
| CT results | `frontend/ct.h` | frontend/CT | typed CT value table (scalar bits / struct field tree), demand order and success/failure cache, target-declaration failure position, target-endian `TcInitImage` | IR finalization, L1/L3 tests |
| IR finalization/print | `frontend/irgen.h`, `frontend/sem_internal.h`, `ir/print.h`, `ir/print.c` | frontend/IR | flat `TcInstr`/`TcUnit`/`TcIR` materialization from parse IR + SEM + CT; deterministic text dump | VM/AOT, L1 tests |
| Platform profile | `runtime/profile.h`, `runtime/profile.c` | runtime/driver | W∈{32,64}; five non-overlapping regions excluding address 0; `D≥1`; `disable_icf == 1`; stack/pointer alignment from `TcTargetDesc`; checked build only adds UB checks | VM, AOT, tests |
| Runtime seams | `runtime/io.h`, `runtime/alloc.h`, `runtime/trap.h`, `runtime/module_loader.h` | runtime | all-or-nothing sink; short-read/EOF/error source; allocator sizes are target-W; trap gets first error + line; device runtime resolves no imports | VM, AOT, freestanding |
| Diagnostics | `diagnostic/codes.h`, `diagnostic/diagnostic.h`, `diagnostic/diagnostic.c`, generated `diagnostic/table.c` | diagnostic (cross-cutting) | closed 80 codes (65 `TC_CE_*` + 15 `TC_RE_*`); stage and report stage; 1-based positions; first-diagnostic comparator; `TC_ERR_OUT_OF_MEMORY` separate | all phases, VM |
| Zero pass | `contract/optimizer.h`, `contract/optimizer.c` | contract (frontend/backend consumers) | O0 and O2 run zero IR passes; `@fast`/`@fast_math` recorded only; `@inline`/`@noinline` stored only | VM, AOT |
| Build/CLI | `driver/cli.h`, `driver/cli.c`, `driver/profile_select.h`, `src/CMakeLists.txt` | build | private `tc_core`; only `tc-vm`/`tc-aot`; no install, no public C ABI; unavailable capabilities fail explicitly with exit 70 | tools, tests |

`tc_contract_check()` in `contract/contract.c` is the startup/L0 aggregate
check. It must return `NULL`; any violation is an internal error, never a
language diagnostic.

首诊比较键序：一般位置为 `origin`（标准 §12.2 #9，操作数内部优先）→
`same_site_rank`（#4，同 origin 类内专用码）→ `clause_rank`（#5）；在
`CALL`/`MAKE` 位置先应用 §12.2 #10 的 `same_site_rank`（调用安全 →
重复 → 未知 → 缺失 → 数量/多余 → 顺序 → 类型），再应用 origin。设计 §6 的
编号列表是规则索引，不是扁平字典序键链；#6/#7/#8 是布局/遍历/档案约束。
`CALL`/`MAKE` 的 `site_id` 把 #10 限制在同一调用的候选集合内；跨位置的同一 site 仍先按参数/字段检查顺序。L0 钉扎见 `tests/unit/README.md` 的 `TC-L0-DIAG-0001`…`0009`。

`tc_instr_lhs_is_valid()` 遵循 §6.15.1：任何有结果指令都可以用 `_` 丢弃结果，
无结果指令必须用 `_`。类型表在创建时预创建全部标量单例，`freeze` 后不再新增
任何类型节点（L0 `TC-L0-IR-0001`/`TC-L0-TYPE-0001`）。

## Closed-set rules

- The diagnostic table is generated from `tests/traceability/codes.json`; the
  JSON is generated from standard §12.3/§12.5.1 and the test plan's appendix A.
  Neither count nor spelling may be changed by an implementation change.
- `TC_ERR_OUT_OF_MEMORY` is not in `TcDiagCode`; it has a separate channel with
  the fixed print name `OutOfMemory` and message `memory allocation failed`.
- No warnings and no codes outside the 80-code set. Resource limits are never a
  substitute for a language diagnostic.
- 32-bit and 64-bit profiles are separate instances. Static results, layouts,
  consts, objects and golden values are never compared across W.

## Front end (implemented; not a frozen cross-module contract)

The LEX/SYN pipeline lives under `src/frontend/`:

| Area | Files | Facts |
| --- | --- | --- |
| Input/arena/limits | `frontend/input.h`, `input.c` | source read is an API error on failure; documented limits (4 MiB source, 16 KiB token (identifier/annotation/float), 256 nesting) are implementation errors, never language codes; arena-owned parse lifetime |
| Exact literals | `frontend/num.h`, `num.c` | decimal -> binary32/64 with roundTiesToEven using integer arithmetic only; reports rounded-to-zero / rounded-to-infinity for `TC_CE_LITERAL_LEX_RANGE` |
| LEX | `frontend/lex.h`, `lex.c` | UTF-8 decode, BOM/U+0000/non-ASCII rejection, `///` comments, `\` continuation, longest-match tokens, mnemonics/suffixes, annotations, DISCARD, INT/FLOAT values, token spans |
| SYN | `frontend/parse.h`, `parse.c` | EBNF §14; flat one-op-per-line parse IR; source names and type syntax retained for SEM; `TcFieldRef`-shaped paths; named args; `switch` targets; spans and end positions; all six SYN-specific codes plus `TC_CE_SYNTAX` |

The parse IR is **front-end internal** (`TcParseModule`/`TcParseStmt`/
`TcParseInstr`/`TcParseType`/`TcParseValue`).  It deliberately does not force
the frozen final `TcInstr`/`TcIR`: opcode shape/type/security checks, name
resolution and struct identity are semantic facts.  SEM resolves the parse IR
and freezes the type table; materializing `TcInstr`/`TcIR` belongs to IR
finalization.  No source names may be added to `TcOperand`/`TcInstr`.

The tool-visible partial checks are `--syntax-only` (LEX/SYN),
`--sem-only` (LEX/SYN/IMPORT/SEM) and `--static-only` (all static stages
through CT and IR finalization); exit 0 for any of them is a partial check, not
a valid-program claim.  A normal invocation still exits 70 after the
implemented static stages.  The driver prints one stable line per diagnostic:

```text
<file>:<line>:<column>: error[<LEX|SYN|IMPORT|SEM|CT>]: <TC_CE_*>: <print-name>
```

## Static pipeline (implemented; not a frozen cross-module contract)

| Area | Files | Facts |
| --- | --- | --- |
| Module discovery | `frontend/module.h`, `module.c` | root must be `#program`; import DFS in source order; importer directory then include roots in order; canonical resolved path is the instance key; `module_order` fixed before IMPORT/SEM; `#program` import target is parsed but not recursed |
| IMPORT | `frontend/module.c`, `sem.c` | the five `TC_CE_IMPORT_*` codes report in the IMPORT stage; duplicate/circular priority; import names occupy the module namespace; imported private access; diamond reuse keeps one instance |
| Names/members | `frontend/sem.h`, `sem.c` | value/type/label namespaces; module `Self`/`m`; bare value member is `FUNCTION_SCOPE_ACCESS`; struct name in value position is `TYPE_MISMATCH`; duplicate member/parameter/local diagnostics; public/private and `PRIVATE_TYPE` positions |
| Types/literals | `frontend/sem.c` | one interned type table per Analyze, scalar singletons, `ptr`/`fnptr` interning, nominal structs; `isize`/`usize` independent of fixed-width types; literal category/suffix/sign/range per W |
| Conversions | `frontend/sem.c` | category matrix before width for `bitcast`; `trunc` only narrows; `cast` is numeric/bool only; W-dependent widths |
| Layout/`sizeof` facts | `frontend/sem.c`, `ir/structs.h` | fields are compact, `@padding(N)` adds N bytes, no implicit alignment or tail padding; sum overflow in W-bit `usize` is SEM `LAYOUT_OVERFLOW`; types never cache layout |
| Instruction shape | `frontend/sem.c` | opcode arity/operand category checks, mode matrix, no-result LHS rule, field/make and call argument type checks, `sizeof` type argument |
| const_expr form | `frontend/sem.c` | `const`/`static var` initializers use the §3.10 positive op set and only const value references; unlisted ops, mode/volatile suffixes, pointer/`fnptr` bitcasts, and var/parameter/static-var references are SEM `CONST_FORM`; `@padding(N)` N form is the separate `CONST_FORM` check |
| CFG/DI | `frontend/sem.c` | one closed unit per program/function; instruction/declaration nodes, labels as block entries, all `br`/`br_if`/`switch` edges, `return`/`trap` terminal, call atomic, reachable end; `U[n]` initialization and the maximum fixed point of standard §5.7.1; unreachable code excluded from DI and return propagation |
| Returns | `frontend/sem.c` | `return` only in functions; void/non-void forms; operand type; non-void reachable end and empty body report `MISSING_RETURN` at the return type with the `end` related position; void/program natural end, trap and non-exiting loops are legal |
| Calls | `frontend/sem.c` | signature collection first; direct/indirect; target visibility, call safety, result shape, arguments (duplicate → unknown → missing → count → order → type), receiver; recursion allowed; `call_indirect` positional only |
| Safety | `frontend/sem.c` | lexical unsafe context (`@unsafe`/`@fast`/`unsafe` block); load/store/memmove/ptradd/ptrsub/call_indirect and ptr↔int/fnptr↔int bitcast are unsafe; `@safe` rejects any unsafe block or unsafe operation with `SAFE_VIOLATION` priority |
| Annotations | `frontend/sem.c` | function/parameter/field positions, unknown/duplicate/conflict, `@unsafe`/`@safe`/`@fast` exclusion, `@fast_math`/promise placement, `@inline`/`@noinline`, `@align`/`@range` endpoint constants per §8.6 (`@range` `lo`/`hi` same-type integer constants; `@align` `N` a positive power of two checked by W-bit address width), resolved by the SEM const resolver (fixed binary32/64 rules), `@padding`; promise-content violations are not static rejections |
| CT evaluation | `frontend/ct.h`, `ct.c` | runs only after every SEM buffer is empty and the type table is frozen; evaluates module `const`/`static var` initializers (and later local `const` initializers) on demand in `module_order`/module source order; §3.10 allowlist and §6.3-§6.6 strict semantics; successes and failures cached; the first failure is attributed to its target declaration and stops CT; no cascade; local consts are evaluated after all module-level initializers |
| IR finalization | `frontend/irgen.h`, `irgen.c` | materializes `TcIR`/`TcUnit`/`TcBlock`/`TcInstr` from the frozen type table, layout, CT results and parse IR; operand field paths become base vreg + field indices; labels/switch targets carry block ids; named call/make arguments survive; aggregate const values inline as a use-site `make`; no object is created for a `const`; static/Frame object tables and per-profile offsets |

The type table is frozen after SEM.  CT and IR finalization run after the freeze
and use read-only type lookups (`tc_type_ptr_find`/`tc_type_fnptr_find`/
`tc_type_struct_find`) so a frozen table is never mutated.  Code generation
(VM/AOT) remains a later stage; SEM's CFG/DI fact is re-derived into explicit
`TcBlock` edges during IR finalization.

### Current implementation conventions

- `TC_CE_OPERAND_COUNT` always reports at the instruction mnemonic, including
  type-argument shape, operand-kind and no-result-LHS violations.
- A `const`/`static var` initializer is checked against the §3.10 positive
  allowlist before instruction checks: an unlisted op reports `CONST_FORM` at
  the opcode, an op with a mode/volatile suffix reports at the suffix token,
  and a `bitcast` involving a pointer/`fnptr` type reports at the opcode.
  A value reference inside the initializer must resolve to a `const`; a
  `var`/parameter/`static var` reference reports `CONST_FORM` at the target
  token (including `make` field values and nested operands).
- Named operands are accepted only for `make` fields and direct `call`
  arguments; `call_indirect` named arguments are `UNKNOWN_ARGUMENT`, and a
  named function/`fp` target or type argument is `OPERAND_COUNT`.
- If positional and named call arguments or make fields are mixed and the
  argument/field set would otherwise be complete, `ARGUMENT_ORDER`/`FIELD_ORDER`
  is reported at the first *named* argument/field.
- Module layout/type definitions use a dependency-first DFS postorder over the
  import DAG; diagnostics still use `module_order`.
- `Self.S` is not type syntax (`Self` is a keyword, not `IDENT`); type
  parameters do not accept `Self.*`. Qualified imported type names use
  `m.S`.
- CFG/DI nodes are source statements (instructions, `var`, `const`, `label`);
  `unsafe` markers are lexical only.  A unit is built and discarded per body;
  units never share nodes, edges, reachability or DI state.
- Call diagnostics carry a call-site id and the §8.8.2 phase
  (`TARGET` → `SAFETY` → `RESULT_SHAPE` → `ARGUMENTS` → `RECEIVER`), so call
  safety precedes LHS/argument checks at one call.
- `@range` endpoints record `TcParam.lo`/`hi` bit patterns; `@align` records
  `TcParam.align` (both full 64-bit fields).  Endpoint values are SEM facts:
  literals and const paths are evaluated by the SEM const resolver (literals
  including `inf`/`nan`, prior/`Self.K`/`m.K` references, const struct field
  reads, integer pure expressions including the six comparisons, `sizeof`,
  numeric/bool `cast`/`trunc`/`bitcast`, and scalar
  float arithmetic/conversion forms including `mod` and int-to-float chains),
  then validated and recorded.  Invalid values report `TC_CE_ANNOTATION` at
  the endpoint token.  Floating evaluation uses fixed binary32/binary64 bit
  rules, not host `fenv` or `long double`: cross-type NaN becomes canonical
  quiet NaN, exact subnormals retain their value, and strict invalid/overflow/
  underflow cases return unresolved so CT reports `TC_CE_CONST_*`.  SEM does
  not wrap and does not emit an annotation diagnostic.
- `MISSING_ARGUMENT` points its related position at the earliest missing
  parameter declaration (call mnemonic stays primary); `DUPLICATE_ARGUMENT`
  relates the first duplicate.
- A `void` call used as a value/target reports `TC_CE_VOID` at the call
  mnemonic, because the call site has no `void` token; the callee signature is
  not used as the primary position.
- A `switch` integer literal selector has no declared expected type; it is
  typed by its own source type (`int64` without suffix, `uint64` with `u`) and
  range-checked before the integer-selector check.
- Duplicate labels and malformed branch/switch forms use conservative
  fallthrough edges; they do not suppress DI or `MISSING_RETURN`.  A valid
  branch whose target label is absent keeps the unresolved edge out of the CFG;
  DI and `MISSING_RETURN` are evaluated on the known edges, so an earlier
  diagnostic on a known path may win over the label error.  Name, type, safety
  and construct checks still run for every statement.
- Direct call target errors precede call safety; `call_indirect` resolves its
  `fp` operand before the instruction-level safety check.  Other unsafe
  memory/pointer instructions check their unsafe context at the mnemonic before
  operand shape, so a malformed `load`/`store` in safe code reports the unsafe
  diagnostic first.

### Compile-time evaluation and finalized IR conventions

- Stage flags: `--syntax-only` = LEX/SYN; `--sem-only` = LEX/SYN/IMPORT/SEM;
  `--static-only` = all static stages through CT and IR finalization; none
  claim a valid program (no VM/AOT execution).  A normal invocation exits 70
  after the static stages until code generation exists.
- CT startup order is `module_order` then module source order.  A referenced
  `Self.K`/`m.K`/local const is evaluated first; initializer references are
  left to right and `make` fields in written order.  Every declaration is
  evaluated at most once; success and failure are cached.  The first failure is
  reported at the target declaration (name token) and stops CT; referencing
  declarations contribute no cascade diagnostic.
- CT values are scalars (own-width bit pattern) or struct field trees.  The
  strict error mapping is §3.10: signed integer/float overflow and strict
  `shl` overflow → `CONST_OVERFLOW`; integer/strict float division or `mod` by
  zero → `CONST_DIV_ZERO`; negative shift count → `CONST_SHIFT`; strict float
  invalid/NaN input → `CONST_FLOAT_INVALID`; strict float underflow
  (tininess after rounding and inexact) → `CONST_FLOAT_UNDERFLOW`;
  non-representable `cast` → `CONST_CAST`.
- Floating evaluation uses fixed binary32/binary64 operations with
  `FLT_EVAL_METHOD == 0` and `-ffp-contract=off`; exact subnormal results do
  not underflow, and float→float `cast` overflow is `CONST_CAST` while
  underflow to zero/subnormal is legal (standard §6.6.1).  Pointer/fnptr
  `bitcast` never reaches CT (SEM `CONST_FORM`).
- `TcConst.bits` stores the scalar bit pattern in its own width, zero-extended
  into 64 bits; consumers must ignore higher bits for narrower types.  Struct
  consts use `TcInitImage` (field offsets, `@padding` zero bytes, bool
  0x00/0x01, profile endianness).  `static var` init images use the same
  encoding; `TcObject.init` points at them.
- Static object layout: modules in reverse topological order of the import DAG
  (importers first, same layer broken by `module_order`), then module source
  order; each object is aligned to W/8 bytes; program top-level `var` and
  `static var` share the static region.  Frame objects are appended in unit
  order and laid out per unit with params first, then `var` declarations.
- Final IR operand conventions: a field operand is `{base vreg, field index
  path}`; `TC_OP_LABEL` and `switch` targets carry the **target block id**;
  `call`/`call_indirect` put the function/fnptr operand in `args[0]`;
  `sizeof` carries its type in `instr.type` with no args; `funcaddr` records
  the resulting `fnptr` signature in `instr.type`; `addr` keeps its target in
  `args[0]`.  Named call/make arguments keep `TcArg.name`; all other operands
  carry no source names.
- A reference to a scalar `const` is inlined as a `TC_OP_CONST`; a reference to
  an aggregate `const` emits a synthetic `make` (recursively for nested
  aggregates) immediately before the use and passes the temp vreg.  A `const`
  never owns an object; `TcIR.consts` contains only successfully evaluated
  module `const` declarations.
- `tc_ir_print()` is a stable, deterministic text dump for tests/debugging;
  it is not an input format and does not define semantics.

### Deferred decisions

- Field sub-objects: IR finalization creates one object per `var`/parameter/
  `static var`; struct field sub-objects and `TcObject.parent` links are
  materialized by the pointer/object runtime stage (standard §3.12/§6.8).  Until
  then every object has `parent == UINT32_MAX`.

- `call_indirect nullptr`: statically rejected as `LITERAL_TYPE` because no
  signature can be inferred from an untyped null operand; the runtime
  `TC_RE_NULL_FUNCTION_POINTER` path and the signature-inference question remain
  deferred to the VM stage.
- `-0` for an unsigned expected type: treated as an explicit negative literal
  (`LITERAL_TYPE`); the mathematical-zero reading remains an open clarification.

## Not frozen here (later stages, same headers)

The contract freezes representation, not the semantic tables that later phases
must fill in:

| Item | Stage |
| --- | --- |
| opcode arity/shape/type/mode table | implemented |
| security table (unsafe instruction/call set) | implemented |
| type equivalence/conversion/literal algorithms | implemented |
| struct layout/`sizeof` computation | implemented |
| CFG/DI fixed-point and return reachability | implemented (internal per unit) |
| CT allowlist/evaluation/caching | implemented |
| final `TcInstr`/`TcIR`/`TcUnit` materialization and printing | implemented |
| runtime semantics and I/O formatting | S6 |
| AArch64 lowering/ABI/frame layout | S7 |

Changing a frozen structure requires a contract review, updating this file, the
L0 tests and the coverage matrix in the same change.
