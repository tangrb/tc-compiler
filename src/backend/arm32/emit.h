/* TC 0.0.46 ARM32 (Cortex-M7 / AAPCS32) assembly emitter (design §10/§11).
 *
 * The emitter consumes one finalized TcIR bound to the arm32-baremetal
 * profile and writes deterministic GNU-syntax Thumb-2 assembly.  It is an
 * independent backend: the AArch64 lowering, register use and AAPCS32 call
 * classification are not shared with AArch64.  Arithmetic, conversions,
 * floating point, memory checks and I/O call the shared runtime core so the
 * reference VM and the 32-bit backend execute one implementation of the
 * standard's rules; control flow, calls, frames and static data are emitted
 * natively with a fixed-slot, zero-pass model.
 *
 * This is an internal compiler interface, not an installed API.
 */
#ifndef TC_BACKEND_ARM32_EMIT_H
#define TC_BACKEND_ARM32_EMIT_H

#include <stddef.h>
#include <stdio.h>

#include "ir/ir.h"
#include "ir/linemap.h"
#include "runtime/profile.h"

typedef enum {
    TC_ARM32_EMIT_OK = 0,
    TC_ARM32_EMIT_UNSUPPORTED,
    TC_ARM32_EMIT_INTERNAL,
    TC_ARM32_EMIT_OOM,
    TC_ARM32_EMIT_IO
} TcArm32EmitStatus;

/* Emit assembly for `ir`.  `profile` and `target` must describe the same
 * 32-bit little-endian AAPCS32 environment. */
TcArm32EmitStatus tc_arm32_emit_asm(FILE *out, const TcIR *ir,
                                    const TcPlatformProfile *profile,
                                    const TcTargetDesc *target,
                                    const TcLineMap *linemap,
                                    char *error_message,
                                    size_t error_message_capacity);

#define TC_ARM32_TOP_LEVEL_SYMBOL "tc_main"

#endif /* TC_BACKEND_ARM32_EMIT_H */
