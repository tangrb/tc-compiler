/* TC 0.0.46 AArch64 assembly emitter (design §10/§11).
 *
 * The emitter consumes one finalized TcIR bound to a 64-bit AArch64 profile
 * and writes deterministic GNU-syntax AArch64 assembly for every opcode and
 * value type: integer/bit/comparison/float/conversion operations, control
 * flow, direct and indirect calls, pointer/object memory access, structures,
 * `make`/`copy`/`memmove`, standard I/O and `sizeof`.  Capabilities outside the
 * bound platform instance are rejected with TC_AARCH64_EMIT_UNSUPPORTED before
 * any artifact is published (design §10.3).
 *
 * This is an internal compiler interface, not an installed API.
 */
#ifndef TC_BACKEND_AARCH64_EMIT_H
#define TC_BACKEND_AARCH64_EMIT_H

#include <stddef.h>
#include <stdio.h>

#include "ir/ir.h"
#include "runtime/profile.h"

typedef enum {
    TC_AARCH64_EMIT_OK = 0,
    TC_AARCH64_EMIT_UNSUPPORTED,
    TC_AARCH64_EMIT_INTERNAL,
    TC_AARCH64_EMIT_OOM,
    TC_AARCH64_EMIT_IO
} TcAArch64EmitStatus;

/* Emit assembly for `ir`.  `profile` and `target` must describe the same
 * 64-bit little-endian AArch64 environment.  On failure a human-readable
 * reason is written to `error_message` when it is non-NULL and
 * `error_message_capacity` is non-zero. */
TcAArch64EmitStatus tc_aarch64_emit_asm(
    FILE *out, const TcIR *ir, const TcPlatformProfile *profile,
    const TcTargetDesc *target, char *error_message,
    size_t error_message_capacity);

/* Symbol names used by the emitted assembly and supplied by the AArch64
 * runtime below.  Kept here so the toolchain driver can audit the link. */
#define TC_AARCH64_TOP_LEVEL_SYMBOL "tc_main"
#define TC_AARCH64_STATIC_REGION_SYMBOL "__tc_static_region"
#define TC_AARCH64_CALL_DEPTH_SYMBOL "__tc_call_depth"
#define TC_AARCH64_TRAP_SYMBOL "tc_aarch64_trap"
#define TC_AARCH64_WRITE_SYMBOL "tc_aarch64_write_scalar"
#define TC_AARCH64_READ_SYMBOL "tc_aarch64_read_scalar"

#endif /* TC_BACKEND_AARCH64_EMIT_H */
