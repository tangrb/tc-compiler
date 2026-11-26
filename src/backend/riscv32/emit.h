/* TC 0.0.46 RISC-V 32 (RV32ILP32) assembly emitter (design §10/§11).
 *
 * The emitter consumes one finalized TcIR bound to the riscv32-baremetal
 * profile and writes deterministic GNU-syntax RISC-V assembly.  It is an
 * independent backend: its argument classification, frame layout and
 * instruction selection are specific to the RISC-V 32-bit psABI.  Arithmetic,
 * conversions, floating point, memory checks and I/O call the shared runtime
 * core so the reference VM and the native target execute one implementation
 * of the standard's rules; control flow, calls, frames and static data are
 * emitted natively with a fixed-slot, zero-pass model.
 *
 * This is an internal compiler interface, not an installed API.
 */
#ifndef TC_BACKEND_RISCV32_EMIT_H
#define TC_BACKEND_RISCV32_EMIT_H

#include <stddef.h>
#include <stdio.h>

#include "ir/ir.h"
#include "ir/linemap.h"
#include "runtime/profile.h"

typedef enum {
    TC_RISCV32_EMIT_OK = 0,
    TC_RISCV32_EMIT_UNSUPPORTED,
    TC_RISCV32_EMIT_INTERNAL,
    TC_RISCV32_EMIT_OOM,
    TC_RISCV32_EMIT_IO
} TcRiscv32EmitStatus;

/* Emit assembly for `ir`.  `profile` and `target` must describe the same
 * 32-bit little-endian ILP32 environment. */
TcRiscv32EmitStatus tc_riscv32_emit_asm(FILE *out, const TcIR *ir,
                                        const TcPlatformProfile *profile,
                                        const TcTargetDesc *target,
                                        const TcLineMap *linemap,
                                        char *error_message,
                                        size_t error_message_capacity);

#define TC_RISCV32_TOP_LEVEL_SYMBOL "tc_main"

#endif /* TC_BACKEND_RISCV32_EMIT_H */
