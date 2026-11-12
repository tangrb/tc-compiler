# 0.0.46 implementation skeleton.
#
# No compiler targets yet.  Planned layout (design §14):
#
#   frontend/   lex parse module resolve analyze consteval diagnostic
#   ir/         type instr block cfg region address print
#   runtime/    sem_int sem_fp sem_cast sem_bitwise io sink alloc trap profile
#   vm/         interp memory pointer_state
#   backend/    aarch64 arm32 riscv32 common
#   driver/     tc-vm tc-aot
#
# When implementation starts, add the real subdirectories here and link the
# private `tc_core` target into `tc-vm` / `tc-aot`.  Do not reintroduce
# a C host public library, embedding API, or host ABI.
