/* TC 0.0.46 contract: trap seam (design §7.2).
 *
 * The handler receives only the first trap's runtime error kind and the line of
 * the triggering instruction.  It must not be used for implementation-level
 * diagnostics; TC_ERR_OUT_OF_MEMORY has a separate channel.
 */
#ifndef TC_RUNTIME_TRAP_H
#define TC_RUNTIME_TRAP_H

#include "frontend/diagnostic_codes.h"

/* Only the 15 TC_RE_* codes are valid error kinds.  TcErrorKind is a distinct
 * name so runtime-facing code never accepts a TC_CE_* by mistake in review. */
typedef enum {
    TC_EK_INTEGER_OVERFLOW = TC_RE_INTEGER_OVERFLOW,
    TC_EK_DIV_ZERO = TC_RE_DIV_ZERO,
    TC_EK_NEGATIVE_SHIFT = TC_RE_NEGATIVE_SHIFT,
    TC_EK_FLOAT_OVERFLOW = TC_RE_FLOAT_OVERFLOW,
    TC_EK_FLOAT_UNDERFLOW = TC_RE_FLOAT_UNDERFLOW,
    TC_EK_FLOAT_INVALID = TC_RE_FLOAT_INVALID,
    TC_EK_CAST_OVERFLOW = TC_RE_CAST_OVERFLOW,
    TC_EK_NULL_POINTER = TC_RE_NULL_POINTER,
    TC_EK_PTRDIFF_OVERFLOW = TC_RE_PTRDIFF_OVERFLOW,
    TC_EK_WRITE_PROTECTED = TC_RE_WRITE_PROTECTED,
    TC_EK_ADDR_UNAVAILABLE = TC_RE_ADDR_UNAVAILABLE,
    TC_EK_STACK_OVERFLOW = TC_RE_STACK_OVERFLOW,
    TC_EK_NULL_FUNCTION_POINTER = TC_RE_NULL_FUNCTION_POINTER,
    TC_EK_IO = TC_RE_IO,
    TC_EK_TRAP = TC_RE_TRAP
} TcErrorKind;

typedef struct TcTrapHandler {
    void (*trap)(void *ctx, TcErrorKind kind, int line);
    void *ctx;
} TcTrapHandler;

#endif /* TC_RUNTIME_TRAP_H */
