# TC 0.0.46 S0 frozen contracts

**Status: frozen for the 0.0.46 clean-sheet implementation.** This file is the
review index for the S0 contract deliverables. Language legality, diagnostics
and observable behavior remain governed only by
[the 0.0.46 standard](../docs/TC语言及编译器标准-0.0.46.md); architecture is
governed by [the design](../docs/TC原生编译器设计说明书-0.0.46.md).

S0 infrastructure lives outside the four language layers so no phase owns it:
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

## S1 front end (implemented; not a frozen cross-module contract)

S1 adds the LEX/SYN pipeline under `src/frontend/`:

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

The tool-visible additions are `--syntax-only` (LEX/SYN only) and
`--static-only` (LEX/SYN/IMPORT/SEM); exit 0 for either is a partial check, not
a valid-program claim.  A normal invocation still exits 70 after the
implemented static stages.  The driver prints one stable line per diagnostic:

```text
<file>:<line>:<column>: error[<LEX|SYN>]: <TC_CE_*>: <print-name>
```

## S2/S3 static pipeline (implemented; not a frozen cross-module contract)

| Area | Files | Facts |
| --- | --- | --- |
| Module discovery | `frontend/module.h`, `module.c` | root must be `#program`; import DFS in source order; importer directory then include roots in order; canonical resolved path is the instance key; `module_order` fixed before IMPORT/SEM; `#program` import target is parsed but not recursed |
| IMPORT | `frontend/module.c`, `sem.c` | the five `TC_CE_IMPORT_*` codes report in the IMPORT stage; duplicate/circular priority; import names occupy the module namespace; imported private access; diamond reuse keeps one instance |
| Names/members | `frontend/sem.h`, `sem.c` | value/type/label namespaces; module `Self`/`m`; bare value member is `FUNCTION_SCOPE_ACCESS`; struct name in value position is `TYPE_MISMATCH`; duplicate member/parameter/local diagnostics; public/private and `PRIVATE_TYPE` positions |
| Types/literals | `frontend/sem.c` | one interned type table per Analyze, scalar singletons, `ptr`/`fnptr` interning, nominal structs; `isize`/`usize` independent of fixed-width types; literal category/suffix/sign/range per W |
| Conversions | `frontend/sem.c` | category matrix before width for `bitcast`; `trunc` only narrows; `cast` is numeric/bool only; W-dependent widths |
| Layout/`sizeof` facts | `frontend/sem.c`, `ir/structs.h` | fields are compact, `@padding(N)` adds N bytes, no implicit alignment or tail padding; sum overflow in W-bit `usize` is SEM `LAYOUT_OVERFLOW`; types never cache layout |
| Instruction shape | `frontend/sem.c` | opcode arity/operand category checks, mode matrix, no-result LHS rule, field/make and call argument type checks, `sizeof` type argument | 

**Known representation limit**: `TcField.padding` is `uint32_t` (design §2.1) while `@padding(N)` accepts any non-negative decimal N; `TcStructDef.size`/`TcField.offset` are computed from the full 64-bit N, but a padding byte count above `UINT32_MAX` is capped in `TcField.padding`. Later stages must use the size/offset facts or revisit the frozen field before consuming the count.

The type table is frozen after Analyze.  Final `TcInstr`/`TcIR` materialization,
CFG/DI, call ordering/safety, annotation matrices and CT are later stages.

## Not frozen here (later stages, same headers)

The contract freezes representation, not the semantic tables that later phases
must fill in:

| Item | Stage |
| --- | --- |
| opcode arity/shape/type/mode table | implemented; security table remains with the safety stage |
| type equivalence/conversion/literal algorithms | implemented |
| struct layout/`sizeof` computation | implemented |
| CFG/DI fixed-point algorithm | S4 |
| CT allowlist/evaluation/caching | S5 |
| runtime semantics and I/O formatting | S6 |
| AArch64 lowering/ABI/frame layout | S7 |

Changing a frozen structure requires a contract review, updating this file, the
L0 tests and the coverage matrix in the same change.
