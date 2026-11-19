/* TC 0.0.46 RISC-V 32 compiler runtime helpers (RV32ILP32, freestanding).
 *
 * The RV32 M extension has no 64-bit divide/remainder instructions, so clang
 * lowers `uint64_t`/`int64_t` division to these compiler-runtime entry points.
 * The algorithms use only 64-bit shifts, comparisons and subtraction and are
 * self-contained; the runtime never links a hosted compiler-rt or libgcc.
 */
#include <stdint.h>

static uint64_t udivmod64(uint64_t dividend, uint64_t divisor,
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

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {
    uint64_t remainder = 0;
    return udivmod64(dividend, divisor, &remainder);
}

uint64_t __umoddi3(uint64_t dividend, uint64_t divisor) {
    uint64_t remainder = 0;
    (void)udivmod64(dividend, divisor, &remainder);
    return remainder;
}

int64_t __divdi3(int64_t dividend, int64_t divisor) {
    int dividend_negative = dividend < 0;
    int divisor_negative = divisor < 0;
    uint64_t magnitude_dividend =
        dividend_negative ? (uint64_t)(-(dividend + 1)) + 1u
                          : (uint64_t)dividend;
    uint64_t magnitude_divisor =
        divisor_negative ? (uint64_t)(-(divisor + 1)) + 1u
                         : (uint64_t)divisor;
    uint64_t remainder = 0;
    uint64_t quotient =
        udivmod64(magnitude_dividend, magnitude_divisor, &remainder);
    if (dividend_negative != divisor_negative) {
        return (int64_t)((uint64_t)0 - quotient);
    }
    return (int64_t)quotient;
}

int64_t __moddi3(int64_t dividend, int64_t divisor) {
    int dividend_negative = dividend < 0;
    int divisor_negative = divisor < 0;
    uint64_t magnitude_dividend =
        dividend_negative ? (uint64_t)(-(dividend + 1)) + 1u
                          : (uint64_t)dividend;
    uint64_t magnitude_divisor =
        divisor_negative ? (uint64_t)(-(divisor + 1)) + 1u
                         : (uint64_t)divisor;
    uint64_t remainder = 0;
    (void)udivmod64(magnitude_dividend, magnitude_divisor, &remainder);
    if (dividend_negative) {
        remainder = (uint64_t)0 - remainder;
    }
    return (int64_t)remainder;
}
