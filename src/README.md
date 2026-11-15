# 0.0.46 implementation layout

This directory is the **target module layout** from
[TC Native Compiler Design 0.0.46](../docs/TC原生编译器设计说明书-0.0.46.md) §14.
S0 (contract freeze), S1 (LEX/SYN + flat parse IR), S2 (module discovery and
IMPORT) and S3 (names, types, conversions and layout) are implemented: the
private `tc_core` library, the `tc-vm`/`tc-aot` entry points, the frozen headers,
and the `frontend/` reader/lexer/parser/module-loader/analyzer.  CT, CFG/DI,
safety/annotations, the VM and backends are later work.  Frozen contract
ownership is indexed in [CONTRACTS.md](CONTRACTS.md).

```text
src/
  base/         assert version (shared foundations, no language semantics)
  contract/     S0 frozen-contract check + zero-pass optimization policy
  diagnostic/   closed 80-code set, generated table, first-diagnostic selection
  frontend/     input num lex parse; module (discovery/IMPORT); sem (names/types/
                conversions/layout/instruction shape)
  ir/           type instr block operand span region object address structs pointer_state
  runtime/      alloc io trap profile module_loader (sem_* from S6)
  vm/           interp memory pointer_state (S6)
  backend/      aarch64 arm32 riscv32 common (S7+)
  driver/       tc-vm tc-aot + shared CLI/profile selection
```

`base/`, `contract/` and `diagnostic/` are S0 **infrastructure**, not language
pipeline stages: their facts are consumed by every phase (and by the runtime
trap seam), so they must not be buried inside `frontend/`.  `frontend/` owns the
LEX/SYN/IMPORT/SEM/CT pipeline; S1 provides `input` (arena/source/decode),
`num` (exact decimal -> binary32/64), `lex` (tokens/spans/LEX diagnostics) and
`parse` (EBNF + flat name-carrying parse IR).  `ir/`, `runtime/`, `vm/`,
`backend/` and `driver/` follow design §14.

## Rules

- Source comments explain the code itself, cite the standard/design where
  useful, or state a self-contained fact.  Do not cite phase numbers (S0/S1...),
  gate reports, the test plan or audit findings in implementation comments;
  test-case ids belong to tests and the coverage matrix.
- Keep the target-independent core (`frontend`, `ir`, `runtime`, `vm`) separate
  from target-specific backends (`backend/<target>`). The first 64-bit target is
  AArch64; the first 32-bit target is still to be selected (arm32 / riscv32).
- The private build target `tc_core` contains the compiler, IR, runtime, and VM
  implementation. It is not installed and exposes no public API/ABI.
- Each source file has one module owner: `base/` + `contract/` are frozen S0
  infrastructure; `diagnostic/` owns the single diagnostic fact source;
  `ir/` owns representation; `runtime/` owns platform seams/profiles;
  `driver/` owns only CLI parsing and tool policy.  Do not create a second
  table, profile instance or diagnostic code set in another directory.
- `tc-vm` and `tc-aot` are the only tool entry points.  `--syntax-only` and
  `--static-only` are partial checks: exit 0 never claims a valid program.  A
  normal invocation still exits 70 after the implemented static stages.
- Parse IR (`frontend/parse.h`) is intentionally not the frozen final IR:
  source names/type syntax stay until SEM, and field paths keep the
  `TcFieldRef` shape.  SEM resolves and validates them and owns the frozen type
  table; materializing `TcInstr`/`TcIR` belongs to IR finalization.  Do not add
  source names to the frozen `TcOperand`/`TcInstr`.
- Documented S1 limits: 4 MiB source, 16 KiB token (identifier/annotation/float), 256 type/block nesting
  levels.  Exceeding them returns `tc_error_internal` (exit 71); it is never a
  language diagnostic.
- Do not reintroduce a C host public library, embedding API, host ABI, or
  AOT embed mode.
- Both 32-bit and 64-bit targets are in scope. `usize`/`isize` are platform-word
  types; `sizeof`, `bitcast`, `trunc`, layout, and diagnostics must handle both.

Implementation starts from the 0.0.46 standard and design document, not from
the historical code (removed from this branch; available only in git tags).
