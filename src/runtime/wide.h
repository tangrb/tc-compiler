/* TC 0.0.46 exact wide-integer helpers for the semantic core.
 *
 * The deterministic floating-point core compares exact products and shifted
 * mantissas that need up to 128 bits.  `__int128` is not available on every
 * target (32-bit ARM, for example), so the core uses this dependency-free
 * two-limb type on every build.  The operations are exact: `shl_checked` and
 * `add_checked` report overflow past 128 bits instead of wrapping.
 */
#ifndef TC_RUNTIME_WIDE_H
#define TC_RUNTIME_WIDE_H

#include <stdint.h>

typedef struct {
    uint64_t hi;
    uint64_t lo;
} TcWide;

TcWide tc_wide_from_u64(uint64_t value);
uint64_t tc_wide_low_u64(TcWide value);

/* Three-way comparison: negative, zero or positive when a <, ==, > b. */
int tc_wide_cmp(TcWide a, TcWide b);
int tc_wide_eq_u64(TcWide a, uint64_t b);
int tc_wide_gt_u64(TcWide a, uint64_t b);

/* Exact 64x64 -> 128 product. */
TcWide tc_wide_mul_u64(uint64_t a, uint64_t b);

/* Left shift; returns 0 when a non-zero bit would be shifted past 127. */
int tc_wide_shl_checked(TcWide value, unsigned shift, TcWide *out);

/* Addition with carry out of bit 127 reported as failure. */
int tc_wide_add_checked(TcWide a, TcWide b, TcWide *out);

/* Subtraction; `a` must be at least `b`. */
TcWide tc_wide_sub(TcWide a, TcWide b);

#endif /* TC_RUNTIME_WIDE_H */
