/* TC 0.0.46 AArch64 subset assembly emitter (design §10/§11).
 *
 * The emitter consumes one finalized TcIR bound to a 64-bit AArch64 profile
 * and writes deterministic GNU-syntax AArch64 assembly for the implemented
 * subset: scalar integer/bit/comparison operations, control flow, direct
 * calls, read/write/writeln, sizeof and copy.  It never silently lowers an
 * unsupported opcode: the caller receives TC_A64_UNSUPPORTED and no artifact
 * should be published (design §10.3).
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
    TC_A64_OK = 0,
    TC_A64_UNSUPPORTED,
    TC_A64_INTERNAL,
    TC_A64_IO
} TcA64Status;

/* Emit assembly for `ir`.  `profile` and `target` must describe the same
 * 64-bit little-endian AArch64 environment.  On failure a human-readable
 * reason is written to `err` (when err != NULL and errcap != 0). */
TcA64Status tc_a64_emit_asm(FILE *out, const TcIR *ir,
                            const TcPlatformProfile *profile,
                            const TcTargetDesc *target, char *err,
                            size_t errcap);

/* Symbol names used by the emitted assembly and supplied by the AArch64
 * runtime below.  Kept here so the toolchain driver can audit the link. */
#define TC_A64_TOP_SYMBOL "tc_main"
#define TC_A64_STATIC_SYMBOL "__tc_static"
#define TC_A64_DEPTH_SYMBOL "__tc_depth"
#define TC_A64_TRAP_SYMBOL "tc_trap"
#define TC_A64_WRITE_SYMBOL "tc_a64_write"
#define TC_A64_READ_SYMBOL "tc_a64_read"

#endif /* TC_BACKEND_AARCH64_EMIT_H */
