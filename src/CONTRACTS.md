# TC frozen contracts and toolchain governance

**Language status: frozen at 0.0.46. Toolchain version: 0.0.47.** This file is the
index for frozen language contracts and current private implementation conventions.
Language legality, diagnostics
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
| Foundation | `base/assert.h`, `base/version.h` | contract/build | compile-time assertions; language 0.0.46, contract 1, toolchain 0.0.47 | all modules |
| Type identity | `ir/type.h` | IR + SEM | scalar singletons; interned `ptr`/`fnptr`/struct nodes; pointer equality; no W-dependent layout in a type; freeze after Analyze | SEM, CT, VM, AOT |
| Span | `ir/span.h` | LEX/SYN | file + 1-based line/column (Unicode scalar column); LEX valid-prefix bytes + 0-based invalid byte offset; `TC_SPAN_NO_INVALID_BYTE` | all diagnostics |
| Operands/args | `ir/operand.h` | SYN + SEM | VREG/FIELD/CONST/LABEL/FUNC/STRUCT/DISCARD; `_` is LHS-only; `TcArg.name == NULL` means positional; field names disappear after SEM | SEM, IR freeze, VM, AOT |
| Instruction | `ir/instr.h`, `ir/instr.c` | SYN + SEM + IR | 51 opcodes in standard §6.2 order; mode set; `dst` VREG/FIELD/DISCARD; named args retained; explicit switch targets; no-result and terminator structure | SEM, CT, VM, AOT |
| CFG/unit | `ir/block.h` | SEM | explicit successor/predecessor edges; one closed unit per function/program; vreg→type and vreg→object maps | SEM DI, VM, AOT |
| Struct/layout | `ir/structs.h` | SEM | field order/padding/readonly; per-profile `offset`/`size`; layout is never cached in `TcType` | SEM, CT, VM, AOT |
| Object/address | `ir/object.h`, `ir/address.h`, `ir/region.h` | SEM + runtime | five regions; object is the single boundary/lifetime/DI fact source; region+offset is not object identity; address 0 is reserved | VM, checked build, AOT |
| Pointer state | `ir/pointer_state.h`, `ir/pointer_state.c` | SEM + VM + AOT | §3.12 null/live/one-past/dangling permission matrix; one-past `ptradd`/`ptrsub` zero stays one-past; shared by addr/ptrdiff/memmove/bounds checks | SEM, VM, AOT |
| IR root | `ir/ir.h` | IR | `TcIR` binds exactly one profile; module identity is resolved path; frozen type table; per-profile consts/objects/units | VM, AOT |
| CT results | `frontend/ct.h` | frontend/CT | typed CT value table (scalar bits / struct field tree), demand order and success/failure cache, target-declaration failure position, target-endian `TcInitImage` | IR finalization, L1/L3 tests |
| Runtime semantic core | `runtime/semantics.h`, `runtime/semantics.c`, `runtime/wide.h`, `runtime/wide.c`, `runtime/rt_memory.h` | runtime | W-aware strict/wrap integer arithmetic, division/shift boundaries, binary32/64 arithmetic with fixed exception priority and exact underflow, exact `mod`, `cast`/`trunc`/`bitcast`; failures are the 15 `TcErrorKind` values; freestanding-safe (no libc/math.h); exact 128-bit comparisons use `TcWide`, not `__int128`, so the same source builds for W=32 and W=64. VM and both target runtimes share one implementation of rounding and exception rules | CT (maps to `TC_CE_CONST_*`), VM, AOT |
| Deterministic scalar I/O | `runtime/format.h`, `runtime/input.h`, corresponding `.c`; `frontend/num.c`, `frontend/arena.c` | runtime | exact six-significant-digit `%g` rendering from the binary value, target integer/bool text, §11.3 complete-token parsing with EOF/short-read/error states; the same freestanding-safe formatter/parser/decimal converter serve the VM and both target runtimes | VM, AOT, tests |
| Reference VM | `vm/vm.h`, `vm/memory.c`, `vm/interp.c` | vm | five-region address space, runtime objects and field sub-objects, pointer states, frames/`D`, all 51 opcodes, mandatory traps, I/O commit semantics | `tc-vm`, tests, AOT reference |
| IR finalization/print | `frontend/irgen.h`, `frontend/sem_internal.h`, `ir/print.h`, `ir/print.c` | frontend/IR | flat `TcInstr`/`TcUnit`/`TcIR` materialization from parse IR + SEM + CT; deterministic text dump | VM/AOT, L1 tests |
| Platform profile | `runtime/profile.h`, `runtime/profile.c` | runtime/driver | W∈{32,64}; five non-overlapping regions excluding address 0; `D≥1`; `disable_icf == 1`; stack/pointer alignment from `TcTargetDesc`; checked build only adds UB checks | VM, AOT, tests |
| Runtime seams | `runtime/io.h`, `runtime/alloc.h`, `runtime/trap.h`, `runtime/module_loader.h` | runtime | all-or-nothing sink; short-read/EOF/error source; allocator sizes are target-W; trap gets first error + line; device runtime resolves no imports | VM, AOT, freestanding |
| Diagnostics | `diagnostic/codes.h`, `diagnostic/diagnostic.h`, `diagnostic/diagnostic.c`, generated `diagnostic/table.c` | diagnostic (cross-cutting) | closed 80 codes (65 `TC_CE_*` + 15 `TC_RE_*`); stage and report stage; 1-based positions; first-diagnostic comparator; `TC_ERR_OUT_OF_MEMORY` separate | all phases, VM |
| Zero pass | `contract/optimizer.h`, `contract/optimizer.c` | contract (frontend/backend consumers) | O0 and O2 run zero IR passes; `@fast`/`@fast_math` recorded only; `@inline`/`@noinline` stored only | VM, AOT |
| Build/CLI | `driver/cli.h`, `driver/cli.c`, `driver/tc.c`, `driver/profile_select.h`, `src/CMakeLists.txt` | build | private static `tc_core`; exactly `tc`/`tc-vm`/`tc-aot`; no install/public C ABI; one implementation-error exit mapping | tools, tests |
| Project model | `project/name.h`, `project/manifest.h`, `project/manifest.c`, `project/init.h`, `project/init.c` | project | private project-name validation; `tc.project` v1 parsing/defaults/bounded reads; nearest-ancestor discovery; init template, nofollow creation, identity preflight and rollback; no compiler consumers for manifest/path APIs yet | `tc`, P1 tests |
| AArch64 backend | `backend/aarch64/emit.h`, `emit_internal.h`, `emit.c`, `emit_abi.c`, `emit_ops.c`, `start.S`, `link.ld`, `platform_semihost.c`, `platform_baremetal.c`; `backend/common/toolchain.h` | backend | W=64 full lowering and deterministic assembly; AAPCS64 composites/HFA/indirect returns; object-table memory helpers; frame/static layout, static `.data`, reset entry with BSS clearing and region facts; far conditional/switch transfers use a local skip plus `b`; semihosting and bare-metal platform instances; ICF disabled; platform-instance capability limits rejected before emission | `tc-aot`, L5/L6 tests |
| ARM32 backend | `backend/arm32/emit.h`, `emit_internal.h`, `emit.c`, `emit_abi.c`, `emit_ops.c`, `rt_helpers.c`, `aeabi.c`, `aeabi_glue.S`, `platform_mps2.c`, `start.S`, `link.ld` | backend | Independent W=32 Cortex-M7 lowering; base AAPCS32/ILP32 softfp classifier (core-register pairs for int64/float64; composites marshalled as memory words with Rule C.4/C.5 split; >4-byte returns indirect); object-table helpers, frame/static layout, reset vector with BSS clearing, 1 MiB code window, ICF disabled | `tc-aot`, L5/L6 tests |
| RISC-V 32 backend | `backend/riscv32/emit.h`, `emit_internal.h`, `emit.c`, `emit_abi.c`, `emit_ops.c`, `rt_helpers.c`, `compiler_rt.c`, `semi.S`, `platform_virt.c`, `start.S`, `link.ld` | backend | Independent RV32IMAFDC lowering; base ILP32 soft-float classifier (a0-a7, aligned 64-bit pairs, 2*XLEN by-value aggregates with the one-register split, >2*XLEN by reference, indirect returns); far `br`/`br_if`/`switch`/`return` use auipc/jalr; reset entry with BSS clearing; NS16550A UART0 stdout/stdin, semihosting exit record, SiFive test finisher; RV32 compiler-runtime 64-bit division helpers; ICF disabled | `tc-aot`, L5/L6 tests |
| Target runtime core | `backend/common/runtime.c`, `rt_abi.h`, `rt_platform.h` | runtime/backend | One freestanding runtime for every target: object-table memory checks (mandatory and checked), device-policy mapping, semantic helpers, deterministic I/O, arena allocator, 8192-byte static trap/exit record; platform seam supplies program stdout, exit-record channel and termination | AArch64/ARM32/RISC-V AOT, bare-metal tests |

`tc_contract_check()` in `contract/contract.c` is the startup/L0 aggregate
check. It must return `NULL`; any violation is an internal error, never a
language diagnostic. The toolchain version is not part of this frozen language
contract check.

## Toolchain governance

- `TC_LANGUAGE_VERSION` is `0.0.46`, `TC_CONTRACT_VERSION` is `1`, and
  `TC_TOOLCHAIN_VERSION` is `0.0.47`. Version output identifies both versions and
  the tool role; the three exact lines are asserted by the CLI and release tests.
- `tc_impl_error_exit()` in `driver/cli.c` is the only private implementation-error
  mapping: `TC_IMPL_OK`→0, `TC_IMPL_USAGE`→2, `TC_IMPL_IO`→70,
  `TC_IMPL_UNAVAILABLE`→70, `TC_IMPL_OOM`→70, `TC_IMPL_INTERNAL`→71. An unknown
  enum value fails closed as 71. Entry points and drivers must not add local
  non-usage→71 mappings.
- Source/import reads, diagnostic/output/temp-file writes, unavailable external
  tools or platform capabilities, and actual allocation failure are environment
  or capability failures (70). Contract violations and documented implementation
  limits remain internal failures (71). Implementation errors never enter the
  80-code language diagnostic table or diagnostic JSON.
- OOM prints exactly
  `<tool>: TC_ERR_OUT_OF_MEMORY: OutOfMemory: memory allocation failed`.
  Other implementation errors print `<tool>: <impl-error>: message[: detail]`.
- Frontend, VM and AOT allocation failures use this single external OOM class
  (`TC_IMPL_OOM`, exit 70). Backend `TC_*_EMIT_OOM` statuses and emitter OOM flags
  are private propagation details; the AOT driver must collapse them before
  reporting. If a later release exposes allocation subcategories, it must add
  them below this boundary without changing the frozen language diagnostic set
  or the current external name, message and exit status.
- `tc` is a project tool, not a compiler/run entry. Its operation must be first;
  help/version stand alone; operations and timeout cannot repeat; `--` terminates
  options only after a recognized operation. `--test-timeout=<seconds>` is
  test-only, nonempty ASCII decimal and uint64-bounded. `--init-project` is
  implemented; a valid `--test` invocation still reports `tc_error_unavailable`
  and exits 70 without filesystem or test execution side effects.

### Project model conventions (implemented)

- `project/` owns private project-name validation, `tc.project` v1 parsing and
  nearest-ancestor discovery. Discovery canonicalizes the start directory and
  stops at the first existing `tc.project` entry; a manifest symlink supplies
  content but does not move the project root. With no manifest, the start
  directory is the root and no include root is added. These APIs have no compiler
  consumers yet; path consumption belongs to the later project work.
- Manifest I/O is nonblocking and bounded at 4 MiB. Host I/O failures and actual
  OOM map to 70, malformed/field/path input maps to 2, and size/growth limit
  failures map to 71. A bad nearest manifest is an error, not a reason to search
  outward again.
- Init canonicalizes cwd and never creates a child directory named after the
  project. Its fixed order is name validation, `tc.project` precheck, `tests`
  precheck, joint fd1/fd2/target identity checks, directory creation, six target
  file creations, then the report. Existing files and symlinks are skipped;
  existing non-directory/symlink `tests` entries are usage errors. Directory
  prefixes are not followed.
- Init compares `(st_dev, st_ino)` for stdout, stderr, all six targets and
  resolvable skipped referents before any mutation or cached diagnostic output.
  An alias is a usage error (2) and does not replace an already determined first
  error. A closed fd is not itself an alias; report-time I/O determines the result.
  Internal descriptors are relocated above fd2 so they cannot reuse closed stdio.
  If a target identity cannot be resolved, stderr is treated as unsafe and cached
  diagnostics are discarded without changing the original error class.
- A create/write/close failure rolls back the current transaction in reverse
  creation order, rechecking identity before unlink/rmdir. Replaced entries are
  preserved and reported as `cleanup failed: <path>`; stdout stays empty. The
  created/skipped report is post-commit: its I/O failure exits 70 without rolling
  back committed files.

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

## Target and platform instances (implemented)

- **AArch64 64-bit**: `aarch64-linux-user` (semihosting stdout, loader-image
  stdin, PL011 exit record, semihosting exit) and `aarch64-baremetal` (PL011
  UART0 stdout and framed stdin, PL011 UART1 exit record, PSCI
  `SYSTEM_OFF`, no semihosting call).  The bare-metal linker script fixes code
  at `0x40000000`, the unused input-image symbol at `0x41000000` and the stack
  top at `0x4f000000`; the device region is `0x09000000` (both PL011s).  The
  UART stdin frame is `TCINTCIN`, an eight-byte little-endian length and the
  payload; the duplicate magic tolerates a console backend that consumes one
  leading byte.
- **ARM32 32-bit**: the selected target is `arm32-baremetal` (QEMU MPS2 AN500,
  Cortex-M7).  clang triple `thumbv7em-none-eabihf`, `-mfloat-abi=softfp
  -mfpu=fpv5-d16`.  The C/ABI convention is the base AAPCS32/ILP32 variant:
  arguments in r0-r3 then the stack; `int64`/`uint64`/`float64` in aligned
  core-register pairs; composites marshalled as 32-bit memory words (Rule
  C.4/C.5 split) with 4-byte alignment because TC layout is compact; signed
  narrow arguments are sign-extended to a full word, unsigned ones are
  zero-extended; returns in r0/r0:r1, and composites larger than four bytes
  through a hidden r0 pointer.  The VFP register classes of the VFP variant
  are not used.  Binary32 uses FPv5-D with roundTiesToEven and no FMA
  contraction; binary64 is implemented by the shared semantic core in integer
  arithmetic and contains no double-register FPU instruction, so the soft-float
  requirement does not depend on the target FPU.  The semantic core still owns
  overflow/underflow classification.
  Code starts above the M-profile vector page (`0x00001000`), the static
  region at `0x00100000`, the stack at `0x00300000`-`0x00400000` (`D=32`) and
  the injected stdin image at `0x20000000`; CMSDK UART0 at `0x40004000` is the
  exit-record channel and semihosting is the program stdout/termination
  service.
- **RISC-V 32-bit**: `riscv32-baremetal` (QEMU virt).  clang triple
  `riscv32-unknown-elf`, `-march=rv32imafdc -mabi=ilp32` (hardware F/D
  arithmetic, soft-float base-integer calling convention).  Arguments use
  a0-a7: 32-bit scalars in one register, `int64`/`uint64`/`float64` in a
  register pair (or one register plus one stack word when only one remains),
  composites of at most 2*XLEN bytes by value in words (whole composite on the
  stack when two words do not fit, with the one-register end-of-list split),
  and larger composites by reference to a caller copy.  Stack alignment is 16
  bytes; returns use a0/a1 and composites larger than eight bytes use a hidden
  a0 pointer.  TC structs are byte-packed, so by-value aggregates have a
  four-byte stack slot.  Signed narrow arguments are sign-extended; bool is
  0x00/0x01.  Code starts at `0x80000000`, the static region at `0x81000000`,
  the stack at `0x87800000`-`0x88000000` (`D=64`) and the device window at
  `0x10000000` (NS16550A UART0 and SiFive test finisher).  Program stdout and
  framed stdin use UART0 (`TCINTCIN`, length and payload); the exit record
  uses semihosting SYS_WRITE0 on stderr and termination uses the test finisher.
  The runtime provides `__udivdi3`/`__umoddi3`/`__divdi3`/`__moddi3` because
  RV32 has no 64-bit division instruction.  startup sets `mstatus.FS` before
  any F/D instruction.
- **Device policy**: the target runtime applies all four `TcDevicePolicy`
  values with the VM's mapping (`REJECT`/`READ`/`READWRITE`/`VOLATILE`, the
  last only for `load.volatile`/`store.volatile`).  Device accesses use one
  target-sized transaction, not a byte-wise copy.  `--device=<policy>`
  overrides the selected profile's device region policy for a test/platform
  instance; it is not a language-level option.
- **Device-side imports**: AOT resolves imports at compile time only.  The
  freestanding runtime has no module loader and no host file access; stdin is a
  loader-injected length-plus-bytes image.
- **Runtime arenas**: 8 MiB on AArch64 and 128 KiB on the 32-bit targets for
  the runtime's private token/decimal buffers.  The ARM32 profile uses 64-bit
  and 32-bit helpers from `aeabi.c`/`aeabi_glue.S`; the RISC-V profile uses
  `compiler_rt.c` for 64-bit division/remainder.  No libc or host compiler-rt
  archive is linked.  Checked build is available on all targets and only adds
  UB checks.
- **Artifacts**: `tc-aot --emit=asm|obj|elf|flat` binds one target/profile per build;
  the ELF is static, has no interpreter/dynamic section/undefined symbols, and
  ICF is disabled.  Toolchain or platform-instance failures exit 70 without
  publishing an artifact.

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

The tool-visible partial checks are `--syntax-only` (LEX/SYN of the root
source only; imports are not loaded), `--sem-only` (LEX/SYN/IMPORT/SEM) and
`--static-only` (all static stages through CT and IR finalization); exit 0 for
any of them is a partial check, not a valid-program claim.  A normal `tc-vm` invocation executes the reference VM;
a normal `tc-aot` invocation emits target assembly (AArch64 or ARM32), or
exits 70 without publishing an artifact when the platform instance is outside
the implemented capability.  The driver prints one stable line per
diagnostic:

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
| Annotations | `frontend/sem.c` | function/parameter/field positions, unknown/duplicate/conflict, `@unsafe`/`@safe`/`@fast` exclusion, `@fast_math`/promise placement, `@inline`/`@noinline`, `@align`/`@range` endpoint constants per §8.6 (`@range` `lo`/`hi` same-type integer constants; `@align` `N` a positive power of two checked by W-bit address width), resolved by the SEM const resolver (fixed binary32/64 rules), `@padding`; parameter annotation placement is decided from the syntactic type (bare `NAME` is a struct, `ptr<T>` is a pointer; unresolved names do not suppress placement); promise-content violations are not static rejections |
| CT evaluation | `frontend/ct.h`, `ct.c`, `runtime/semantics.c` | runs only after every SEM buffer is empty and the type table is frozen; evaluates module `const`/`static var` initializers (and later local `const` initializers) on demand in `module_order`/module source order; §3.10 allowlist and the shared §6.3-§6.6 strict semantic core (one implementation with the VM, mapped to the six `TC_CE_CONST_*` codes); successes and failures cached; the first failure is attributed to its target declaration and stops CT; no cascade; local consts are evaluated after all module-level initializers |
| IR finalization | `frontend/irgen.h`, `irgen.c` | materializes `TcIR`/`TcUnit`/`TcBlock`/`TcInstr` from the frozen type table, layout, CT results and parse IR; operand field paths become base vreg + field indices; labels/switch targets carry block ids; named call/make arguments survive; aggregate const values inline as a use-site `make`; no object is created for a `const`; static/Frame object tables and per-profile offsets |

The type table is frozen after SEM.  CT and IR finalization run after the freeze
and use read-only type lookups (`tc_type_ptr_find`/`tc_type_fnptr_find`/
`tc_type_struct_find`) so a frozen table is never mutated.  Code generation
(VM/AOT) remains a later stage; SEM's CFG/DI fact is re-derived into explicit
`TcBlock` edges during IR finalization.

### Current implementation conventions

- `TC_CE_OPERAND_COUNT` always reports at the instruction mnemonic, including
  type-argument shape, operand-kind and no-result-LHS violations.
- Parameter annotation placement is decided from the syntactic type: a bare
  `NAME` is a struct, not a pointer; unresolved names do not suppress placement
  diagnostics.  A misplaced `@align`/`@nonnull`/`@no_alias`/`@range` reports
  `TC_CE_ANNOTATION` at the annotation token; only a syntactically valid
  pointer placement reaches the `@align` N check (N token on failure).
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

- Stage flags: `--syntax-only` = root-module LEX/SYN (imports are not loaded);
  `--sem-only` = LEX/SYN/IMPORT/SEM;
  `--static-only` = all static stages through CT and IR finalization; none
  claim a valid program (no VM/AOT execution).  A normal `tc-vm` invocation
  runs the reference VM; a normal `tc-aot` invocation emits target assembly,
  or exits 70 without publishing an artifact when the selected platform
  instance is outside the implemented capability (word size or device policy).
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
- Every `TcFunc.body` points into the final `TcIR.units` array at the unit
  whose `func_id` is that function's id (body pointers are fixed up after the
  unit array stops growing).
- `tc_ir_print()` is a stable, deterministic text dump for tests/debugging;
  it is not an input format and does not define semantics.

### Runtime and VM conventions

- The VM executes one finalized `TcIR` with the profile bound to that IR.  A
  target address is `profile.region[kind].base + object.offset`; host pointers
  are never language values and never leak into target integers.
- `TcIR.objects` contains one base object per `var`/parameter/`static var`.
  The VM clones static objects at setup, frame objects on every call, and
  appends field sub-objects recursively from `TcStructDef` offsets.  A field
  sub-object inherits its ancestor's writability, so a write through a
  parameter's field is `TC_RE_WRITE_PROTECTED`.  Frame clones are appended in
  call order and truncated on return; static clones remain for the whole run.
- Static objects are preloaded from `TcObject.init` (padding/endianness already
  encoded by CT); program `var` storage starts zeroed and is initialized by the
  program's own instructions.  Frame storage is zeroed per call; DI guarantees
  that accepted programs do not read it before initialization.
- `addr` produces a pointer into a live object/sub-object.  Load/store/memmove
  and `ptradd`/`ptrsub` check null, code/outside, device policy (pure
  arithmetic traps under `TC_DEV_REJECT` or outside the device window), read-only parameters and (in
  `checked_build`) object bounds before modifying anything.
  Unaligned multi-byte accesses read and write bytes in profile endianness
  (§13.2 profile contract), so byte-offset stores and loads reconstruct the
  same value in the VM and on every target.
  Contiguous struct fields are **separate sub-objects**, not an `n`-element
  array of the field type; `memmove T` with `n>1` across sibling fields is
  outside a single live object and therefore UB.  A field one-past pointer is
  still within that sub-object's `[start, one-past]` interval, so `ptradd`
  offset 0, `ptrsub` back into the field and `ptrdiff` against the field start
  are defined; entering the adjacent field through such a pointer is not.
  Out-of-object, one-past dereference and dangling accesses are UB: release may
  continue within the region's host backing, while `checked_build` reports
  `TC_RE_ADDR_UNAVAILABLE` and never changes the defined-program set.
- A checked build also tracks defined bytes for static and frame objects:
  initializer-image bytes start defined, `var` storage starts undefined, and a
  committed scalar/aggregate write marks its range; a pointer-mediated load or
  `memmove` source with undefined bytes reports `TC_RE_ADDR_UNAVAILABLE`.
  Name-based accesses need no runtime fact because static DI already proves
  initialization.  The object checks are address-range based and do not track
  pointer provenance: an access is resolved against the object that fully
  contains its byte range (a containing struct wins over a smaller sibling
  field), so legal accesses into padding or across sibling fields of one object
  are accepted.  UB that happens to remain inside some live object (forged
  provenance, adjacent-object aliasing, a reused frame slot) may go undetected;
  this is permitted for an optional checked build and never relaxes a mandatory
  trap.  This address-range model is the accepted checked-build design for both
  hosted and freestanding targets: AOT emits the object descriptors and shadow
  into the target image, so baremetal does not depend on a host object table.
  Precise pointer provenance is not part of the 0.0.46 checked ABI; adding it
  would change pointer representation or add a side table and is left to a
  future target/profile design.
- Function addresses are unique: `code_base + (func_id + 1) * 16`.
  `funcaddr`/`funceq`/`funcne` use those values; `call_indirect` maps a non-null
  address back to its function.  A typed null `fnptr` reaches the runtime and
  reports `TC_RE_NULL_FUNCTION_POINTER`; an untyped `nullptr` operand remains a
  static `LITERAL_TYPE` because no signature can be inferred.
- `D` counts active function calls, not the top-level program.  A call checks
  `depth == D` before creating a frame and increments on entry; `return`
  decrements.  Tail calls are not optimized and cannot change the count.
- Built-in reference profiles (documented environment definitions, standard
  §13.1; implementations may choose other values): `aarch64-linux-user` is
  W=64, little-endian, device base `0x20000000` size `0x100000` with
  `TC_DEV_REJECT`, D=512; `aarch64-baremetal` is W=64, little-endian, device base
  `0x09000000` size `0x100000` with `TC_DEV_VOLATILE`, D=256;
  `arm32-baremetal` is W=32, little-endian, device base `0x40004000` size
  `0x1000`, D=32; `riscv32-baremetal` is W=32, little-endian, device base
  `0x10000000` size `0x10000000`, D=64; `w32` is W=32, little-endian, device
  base `0x20000000` size `0x100000` with `TC_DEV_REJECT`, D=256.  All use
  `TcExitPolicy` normal 0 / first-trap 1.  Frame storage is host-backed in the
  VM; the AOT target additionally enforces the target stack budget
  (`frame_total * (D+1) <= stack_size`) before emission.  A program with a
  large static frame footprint can therefore execute on the host-backed VM and
  be rejected by AOT with the implementation error (exit 71); this is an
  implementation resource boundary, not language behavior.  The reset entry
  zeroes `__bss_start..__bss_end` before `tc_main`, so ELF and flat images both
  start with zero runtime state.  The trap/exit record buffer is 8192 bytes so
  practical source paths retain the `TC-EXIT` line.  A 900005-byte static object measures 900016 bytes on AArch64 and
  900012 bytes on the 32-bit targets, leaving limited room under the 1 MiB
  static limit; the 1 MiB input image is the semihosting/ARM32 test loader
  seam, while `aarch64-baremetal` and `riscv32-baremetal` use their UART0 byte
  stream for stdin.
  Increasing either limit requires a profile/linker re-layout and a fresh gate.
  If a program's static frame footprints exhaust the profile region before `D`,
  the VM reports `tc_error_internal` (exit 71): physical stack exhaustion is an
  implementation error, never a language diagnostic.
- Runtime trap spans use `TcInstr.span`, the opcode token that identifies the
  triggered instruction (the same anchor used for instruction-level static
  diagnostics); `tc-vm` executes a normal invocation and prints the first trap as
  `<file>:<line>:<column>: error[RT]: <TC_RE_*>: <print-name>` on stderr; the
  exit status is the profile's `trap_status` (1), while normal termination uses
  `normal_status` (0).  `--checked` sets `checked_build`; `--opt` is accepted
  but the zero-pass baseline keeps O0/O2 identical.  `tc-aot` applies the same
  first-diagnostic pipeline and then emits full target assembly.
- I/O uses the `TcIoSink`/`TcIoSrc` seams: one write call per `write`/
  `writeln` record (newline included), no partial record on failure, and the
  hosted VM flushes each record so a closed/full stdout reports `TC_RE_IO` at
  the failing instruction.  LEX invalid UTF-8 carries the 0-based byte offset
  through both frontend and analysis diagnostics and prints it as an additional
  `note: invalid byte offset N` on every diagnostic path.  `read` parses a complete
  whitespace-delimited token before committing its
  target.  Output formatting uses the exact decimal renderer, not host
  `printf`.
- The core creates no heap objects: the allocation API is a runtime-library
  concern.  Until that library lands, heap-region addresses that are not
  language objects are reported as `TC_RE_ADDR_UNAVAILABLE` rather than being
  accessed through the host; this is a safety fallback for UB, not a language
  guarantee.

### AArch64 backend and toolchain conventions

- Scope: W=64 `aarch64-linux-user` on the semihosting qemu-system platform
  instance.  All 51 opcodes and all value types are lowered: integers/bits/
  comparisons, floats, conversions, pointers/objects, `struct`/`make`/`copy`/
  `memmove`, direct and indirect calls, `funcaddr`/`funceq`/`funcne`, `sizeof`,
  `read`/`write`/`writeln`, control flow and `trap`.  A different word size or a
  non-`TC_DEV_REJECT` device policy is a platform-instance capability limit:
  `tc-aot` exits 70 before publishing any artifact.
- ABI: AAPCS64.  Integer/pointer/bool/fnptr arguments use x0-x7, float32/float64
  v0-v7; a homogeneous float aggregate uses one v register per member; other
  composites of at most 16 bytes use consecutive x registers.  A composite
  larger than 16 bytes is replaced by a pointer to a caller-made copy: the
  pointer occupies one x register (or an eight-byte stack slot when x0-x7 are
  exhausted), and the copy lives in the call's outgoing area.  HFAs and
  at-most-16-byte composites that do not fit their register class go by value
  on the stack.  Returns use x0/x1, v0-v3 for HFAs, or an x8 indirect result
  pointer for composites larger than 16 bytes; the callee copies an indirect
  argument into its own parameter object before the body runs.
  Narrow integers are sign- or zero-extended in registers according to their
  signedness and stored at their declared width; bool storage is 0x00/0x01; sp
  stays 16-byte aligned.  x19/x20 hold the
  frame/static bases and x29/x30 form the frame record; every function saves
  them.  A call checks `depth < D` before `bl`/`blr` and increments, then
  decrements after return (the top level is not counted).  Direct-call parameter
  annotations are optimization assumptions and are never checked at runtime.
- ABI reference and scope: the AArch64 mapping follows AAPCS64 (LP64) and the
  ARM32 mapping follows the base AAPCS32 PCS, as published in the ARM ABI
  [2024Q3 release](https://github.com/ARM-software/abi-aa/releases/tag/2024Q3);
  the 2021Q1 Rule C.4 clarification for over-aligned HFAs is part of that
  reference.  The RISC-V 32 mapping follows the base integer calling
  convention of the RISC-V ELF psABI (ILP32, soft-float argument passing).
  The TC type system has a compact layout and no over-aligned types,
  bit-fields, vector types or variadic functions, so C ABI forms that require
  those source constructs are outside the TC ABI mapping; `@align` is a
  pointer-parameter precondition, not a type-alignment attribute.
  `TcABIDesc` fields are validated profile metadata: the AArch64 backend
  requires `composite_max_bytes=16`, `hfa_max_regs=4` and `stack_align=16`,
  the ARM32 backend requires `composite_max_bytes=4`, `hfa=0` and
  `stack_align=8`, and the RISC-V backend requires `composite_max_bytes=8`,
  `hfa=0` and `stack_align=16`, matching the fixed classifiers.
- Layout: each vreg has a stack slot; frame objects keep `TcObject.offset`
  relative to the frame base, static objects keep their in-region offset in one
  emitted byte array, temporaries follow the object area, and frame size is
  rounded to 16 bytes.  Absolute region bases are chosen by the platform/loader
  instance; VM and AOT agree on in-region offsets.  Every frame registers its
  object descriptors (base objects plus field sub-objects, with a deterministic
  type id) with the runtime, so memory helpers can apply the same object rules
  as the VM.  Outgoing argument areas are allocated below the frame base per
  call; a frame's total footprint is checked against the profile stack budget.
- Static data is the concatenation of `TcObject.init` images at their object
  offsets; program `var` storage starts zeroed and is initialized by the
  program.  The `.data` bytes are the same image the VM preloads.  The static
  layout must fit `profile.region[TC_REG_STATIC].size`; exceeding it is an
  implementation error (`tc_error_internal`, exit 71) in both tools, never a
  language diagnostic.  A checked build adds per-frame and static defined-byte
  shadows; the emit-time checked flag is the platform constant
  `__tc_rt_checked`.
- Emitted code calls the freestanding runtime for floating arithmetic,
  conversions, object/pointer memory operations, I/O and first-trap reporting.
  The runtime archive is compiled from the shared semantic core, formatter,
  token reader, decimal converter, arena and a target platform seam; it is
  freestanding (no libc) and contains its own allocator for runtime-private
  buffers only.  The platform seam supplies stdout, a short-read stdin source
  (UART0 framing on `aarch64-baremetal`, loader image on the semihosting/MPS2
  instances), the exit record and termination.  ARM32 adds word-pair scalar wrappers and ARM EABI division/
  memory helpers.  Traps use the span of the triggering instruction and the
  same 15 `TC_RE_*` codes as the VM.
- Toolchain: emission is deterministic; clang (`-target` from
  `tc_aot_target_triple`) assembles, `ld.lld` links with the bundled
  freestanding runtime and linker script, with `--icf=none` so distinct
  function entry addresses stay distinct.  The image is a target ELF with a
  `_start`/reset entry; linker symbols describe code, static and stack regions.
  `--emit=flat` emits raw bytes starting at the linker script's lowest load
  address (AArch64 `0x40000000`, ARM32 `0x0`, RISC-V `0x80000000`); the caller
  supplies the load address for a flat image.
  Platform instances are `aarch64-linux-user` (qemu-system-aarch64 +
  semihosting), `aarch64-baremetal` (PL011 UART0 stdout and framed stdin,
  UART1 exit record, PSCI SYSTEM_OFF), `arm32-baremetal` (qemu-system-arm
  MPS2 AN500, semihosting stdout, CMSDK UART0 exit record) and
  `riscv32-baremetal` (qemu-system-riscv32 virt, NS16550A UART0 stdout and
  framed stdin, semihosting exit record, SiFive test finisher).  There is no
  EL0/Linux-user runtime yet.  `read` consumes the platform stdin seam: on
  `aarch64-baremetal` and `riscv32-baremetal` it is the UART frame (`TCINTCIN`,
  an 8-byte little-endian length and the payload); on the semihosting/ARM32
  instances it is the loader-provided `__tc_input_image` (8-byte little-endian
  length followed by bytes).  Both return short reads of at most four bytes and an
  empty stream is EOF; the loader image window is 1 MiB.  SYS_WRITE0's return value is undefined (qemu fills
  it with a fixed sentinel) and is not interpreted.  These are
  platform-instance facts, not language semantics.  The device/heap region
  constants are emitted from the bound profile; this instance maps only
  `TC_DEV_REJECT` and no heap objects.
- No register allocator: values live in stack slots and x9-x19 are fixed
  temporaries.  Frame size is capped at the profile stack budget; exceeding it
  is an implementation error.  Scalar accesses to struct fields are emitted
  byte-wise, so compact struct layout never depends on target alignment.

### Deferred decisions

- `-0` for an unsigned expected type: treated as an explicit negative literal
  (`LITERAL_TYPE`); the mathematical-zero reading remains an open clarification.
- Heap objects created by the runtime library are not exposed to core programs;
  heap-region addresses that are not language objects report
  `TC_RE_ADDR_UNAVAILABLE`.  The allocator seam stays available for a later
  runtime library.
- Strict `and`/`or` evaluation is auditable only through emitted assembly: the
  VM evaluates both operand loads unconditionally, and TC operands are pure
  (§1 forbids nested expressions), so differential execution cannot observe a
  short-circuiting AOT lowering.  Asm-shape checks for strict evaluation are a
  later-stage verification item, not a 0.0.46 gate.

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
| heap/runtime-library objects | later runtime library |
| AArch64/ARM32/RISC-V lowering, ABI and frame layout | implemented (W=64 and W=32) |
| checked build (VM, AArch64, ARM32 and RISC-V) | implemented |
| freestanding/bare-metal platform instances, 32-bit backends | implemented (S9/S10) |

Changing a frozen structure requires a contract review, updating this file, the
L0 tests and the coverage matrix in the same change.
