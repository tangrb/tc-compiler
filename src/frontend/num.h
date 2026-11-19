/* TC 0.0.46 exact decimal -> IEEE-754 binary32/binary64 conversion.
 *
 * Standard §2.4: the exact decimal value is rounded directly to the target
 * format with roundTiesToEven; no intermediate floating type may be used.
 * This module implements the conversion with exact integer arithmetic only,
 * so LEX range checks (non-zero finite value rounding to zero, finite value
 * rounding to infinity -> TC_CE_LITERAL_LEX_RANGE) are deterministic and
 * independent of the host floating-point environment.
 *
 * The same routine is used by later stages for literal bit patterns.
 */
#ifndef TC_FRONTEND_NUM_H
#define TC_FRONTEND_NUM_H

#include <stddef.h>
#include <stdint.h>

#include "frontend/input.h"

typedef enum {
    TC_FLOAT_OK = 0,
    TC_FLOAT_ROUNDED_TO_ZERO, /* non-zero finite value rounded to zero */
    TC_FLOAT_ROUNDED_TO_INF   /* finite value rounded to infinity */
} TcFloatRange;

/* Convert digits[0..ndigits) * 10^exp10 (digits are ASCII '0'..'9', no
 * separators) with the given sign.  bit_width is 32 or 64.  On TC_FLOAT_OK
 * *out_bits holds the IEEE bit pattern (sign included).  Returns 0 on success
 * and -1 on allocation failure; a contract violation (bad digit/width) also
 * returns -1. */
int tc_decimal_to_float(const char *digits, size_t ndigits, int exp10,
                        int negative, unsigned bit_width, uint64_t *out_bits,
                        TcFloatRange *out_range);

#endif /* TC_FRONTEND_NUM_H */
