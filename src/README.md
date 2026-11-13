# 0.0.46 implementation layout

This directory is the **target module layout** from
[TC Native Compiler Design 0.0.46](../docs/TC原生编译器设计说明书-0.0.46.md) §14.
S0 (contract freeze) is implemented: the private `tc_core` library, the
`tc-vm`/`tc-aot` entry points and the frozen headers.  Front-end semantics,
VM and backends start in S1/S6/S7.  Frozen contract ownership is indexed in
[CONTRACTS.md](CONTRACTS.md).

```text
src/
  base/         assert version (shared foundations, no language semantics)
  contract/     S0 frozen-contract check + zero-pass optimization policy
  diagnostic/   closed 80-code set, generated table, first-diagnostic selection
  frontend/     lex parse module resolve analyze consteval ebnf (S1+)
  ir/           type instr block operand span region object address structs
  runtime/      alloc io trap profile module_loader (sem_* from S6)
  vm/           interp memory pointer_state (S6)
  backend/      aarch64 arm32 riscv32 common (S7+)
  driver/       tc-vm tc-aot + shared CLI/profile selection
```

`base/`, `contract/` and `diagnostic/` are S0 **infrastructure**, not language
pipeline stages: their facts are consumed by every phase (and by the runtime
trap seam), so they must not be buried inside `frontend/`.  `frontend/` itself
stays reserved for the LEX/SYN/IMPORT/SEM/CT pipeline that starts in S1.
`ir/`, `runtime/`, `vm/`, `backend/` and `driver/` follow design §14.

## Rules

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
- `tc-vm` and `tc-aot` are the only tool entry points.
- Do not reintroduce a C host public library, embedding API, host ABI, or
  AOT embed mode.
- Both 32-bit and 64-bit targets are in scope. `usize`/`isize` are platform-word
  types; `sizeof`, `bitcast`, `trunc`, layout, and diagnostics must handle both.

Implementation starts from the 0.0.46 standard and design document, not from
the historical code (removed from this branch; available only in git tags).
