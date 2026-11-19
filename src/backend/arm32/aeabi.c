/* TC 0.0.46 ARM32 compiler-runtime emulation (AAPCS32/ILP32).
 *
 * The 32-bit target has no 64-bit divide instruction and clang lowers
 * `uint64_t`/`int64_t` division to the ARM EABI helpers.  The algorithms here
 * use only 64-bit shifts, compares and subtracts so they never call
 * themselves; the assembly glue in aeabi_glue.S returns the AAPCS32
 * quotient/remainder register pairs.
 */
#include <stdint.h>

/* Unsigned 64/64 division.  `remainder` receives the remainder; the quotient
 * is the return value.  The caller (the semantic core) rejects a zero divisor
 * before trapping; a zero divisor here yields zero rather than faulting. */
uint64_t tc_arm32_uldivmod64(uint64_t dividend, uint64_t divisor,
                             uint64_t *remainder) {
    uint64_t quotient = 0;
    uint64_t residue = 0;
    int bit;
    if (divisor == 0) {
        *remainder = 0;
        return 0;
    }
    for (bit = 63; bit >= 0; bit--) {
        uint64_t carry = residue >> 63;
        residue = (residue << 1) | ((dividend >> bit) & 1u);
        if (carry != 0 || residue >= divisor) {
            residue -= divisor;
            quotient |= (uint64_t)1 << bit;
        }
    }
    *remainder = residue;
    return quotient;
}

/* Signed 64/64 division with C/AAPCS32 truncation semantics. */
int64_t tc_arm32_ldivmod64(int64_t dividend, int64_t divisor,
                           uint64_t *remainder) {
    int dividend_negative = dividend < 0;
    int divisor_negative = divisor < 0;
    uint64_t magnitude_dividend =
        dividend_negative ? (uint64_t)(-(dividend + 1)) + 1u
                          : (uint64_t)dividend;
    uint64_t magnitude_divisor =
        divisor_negative ? (uint64_t)(-(divisor + 1)) + 1u
                         : (uint64_t)divisor;
    uint64_t magnitude_remainder = 0;
    uint64_t magnitude_quotient =
        tc_arm32_uldivmod64(magnitude_dividend, magnitude_divisor,
                            &magnitude_remainder);
    if (dividend_negative) {
        magnitude_remainder = (uint64_t)0 - magnitude_remainder;
    }
    *remainder = magnitude_remainder;
    if (dividend_negative != divisor_negative) {
        return (int64_t)((uint64_t)0 - magnitude_quotient);
    }
    return (int64_t)magnitude_quotient;
}
