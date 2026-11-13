# 0.0.46 implementation skeleton

This directory is the **target module layout** from
[TC Native Compiler Design 0.0.46](../docs/TC原生编译器设计说明书-0.0.46.md) §14.
S0 (contract freeze) is implemented: the private `tc_core` library, the
`tc-vm`/`tc-aot` entry points and the frozen headers.  Front-end semantics,
VM and backends start in S1/S6/S7.  Frozen contract ownership is indexed in
[CONTRACTS.md](CONTRACTS.md).

```text
src/
  frontend/   lex parse module resolve analyze consteval diagnostic
  ir/         type instr block cfg region address print
  runtime/    sem_int sem_fp sem_cast sem_bitwise io sink alloc trap profile
  vm/         interp memory pointer_state
  backend/    aarch64 arm32 riscv32 common
  driver/     tc-vm tc-aot
```

## Rules

- Keep the target-independent core (`frontend`, `ir`, `runtime`, `vm`) separate
  from target-specific backends (`backend/<target>`). The first 64-bit target is
  AArch64; the first 32-bit target is still to be selected (arm32 / riscv32).
- The private build target `tc_core` contains the compiler, IR, runtime, and VM
  implementation. It is not installed and exposes no public API/ABI.
- `tc-vm` and `tc-aot` are the only tool entry points.
- Do not reintroduce a C host public library, embedding API, host ABI, or
  AOT embed mode.
- Both 32-bit and 64-bit targets are in scope. `usize`/`isize` are platform-word
  types; `sizeof`, `bitcast`, `trunc`, layout, and diagnostics must handle both.

Implementation starts from the 0.0.46 standard and design document, not from
the historical code (removed from this branch; available only in git tags).
