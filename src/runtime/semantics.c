/* TC 0.0.46 runtime semantic core.
 *
 * Integer operations follow standard §6.3 (strict/wrap, division and shift
 * boundaries); floating operations follow §6.3.1-§6.3.3 and §10.6 (binary32 /
 * binary64, roundTiesToEven, exception priority INVALID -> DIV_ZERO ->
 * OVERFLOW -> UNDERFLOW).  The build disables FMA contraction and rejects
 * hosts that evaluate floating expressions in a wider precision, so one
 * operation rounds once in the target format.
 */
#include "runtime/semantics.h"

#include "ir/type_width.h"

#include <float.h>

#include "runtime/rt_memory.h"
#include "runtime/wide.h"

#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "TC floating semantics require FLT_EVAL_METHOD == 0"
#endif

static const TcErrorKind TC_SEM_NO_TRAP = (TcErrorKind)0;

/* ------------------------------------------------------------------------- */
/* Width and bit helpers.                                                    */
/* ------------------------------------------------------------------------- */

unsigned tc_sem_type_width(const TcType *type, unsigned W) {
    if (W != 32u && W != 64u) {
        return 0;
    }
    return tc_type_width_of_tag(type->tag, W);
}

uint64_t tc_sem_mask(unsigned width) {
    if (width == 0) {
        return 0;
    }
    if (width >= 64u) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << width) - 1u;
}

static uint64_t low_mask(unsigned width) {
    return tc_sem_mask(width);
}

static int64_t sign_extend(uint64_t bits, unsigned width) {
    uint64_t mask;
    uint64_t sign;
    if (width == 0) {
        return 0;
    }
    if (width >= 64u) {
        return (int64_t)bits;
    }
    mask = low_mask(width);
    sign = UINT64_C(1) << (width - 1u);
    bits &= mask;
    return (int64_t)((bits ^ sign) - sign);
}

static int signed_fits(int64_t value, unsigned width) {
    int64_t min;
    int64_t max;
    if (width == 0) {
        return 0;
    }
    if (width >= 64u) {
        return 1;
    }
    min = -(int64_t)(UINT64_C(1) << (width - 1u));
    max = (int64_t)((UINT64_C(1) << (width - 1u)) - 1u);
    return value >= min && value <= max;
}

static uint64_t int_result(int64_t value, unsigned width) {
    return (uint64_t)value & low_mask(width);
}

static int is_integer_type(const TcType *type) {
    return type != NULL && tc_type_is_integer(type);
}

/* ------------------------------------------------------------------------- */
/* Integer operations.                                                       */
/* ------------------------------------------------------------------------- */

static int integer_binop(unsigned W, const TcType *type, TcOpcode op,
                         TcOpMode mode, uint64_t a, uint64_t b, uint64_t *out,
                         TcErrorKind *trap) {
    unsigned width = tc_sem_type_width(type, W);
    uint64_t mask = low_mask(width);
    int is_signed = tc_type_is_signed_integer(type);
    uint64_t ua = a & mask;
    uint64_t ub = b & mask;
    int64_t sa = sign_extend(ua, width);
    int64_t sb = sign_extend(ub, width);

    switch (op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL: {
        int wrap = mode == TC_WRAP || !is_signed;
        if (is_signed && !wrap) {
            int64_t result = 0;
            int overflow;
            if (op == TC_ADD) {
                overflow = __builtin_add_overflow(sa, sb, &result);
            } else if (op == TC_SUB) {
                overflow = __builtin_sub_overflow(sa, sb, &result);
            } else {
                overflow = __builtin_mul_overflow(sa, sb, &result);
            }
            if (overflow || !signed_fits(result, width)) {
                *trap = TC_EK_INTEGER_OVERFLOW;
                return 0;
            }
            *out = int_result(result, width);
            return 1;
        }
        if (op == TC_ADD) {
            *out = (ua + ub) & mask;
        } else if (op == TC_SUB) {
            *out = (ua - ub) & mask;
        } else {
            *out = (ua * ub) & mask;
        }
        return 1;
    }
    case TC_DIV:
    case TC_MOD:
        if (ub == 0) {
            *trap = TC_EK_DIV_ZERO;
            return 0;
        }
        if (is_signed) {
            if (ua == (UINT64_C(1) << (width - 1u)) && ub == mask) {
                if (op == TC_DIV) {
                    *trap = TC_EK_INTEGER_OVERFLOW;
                    return 0;
                }
                *out = 0;
                return 1;
            }
            *out = int_result(op == TC_DIV ? sa / sb : sa % sb, width);
        } else {
            *out = op == TC_DIV ? ua / ub : ua % ub;
        }
        return 1;
    case TC_SHL:
    case TC_SHR: {
        uint64_t count;
        int negative;
        if (is_signed) {
            negative = sb < 0;
            count = negative ? 0 : (uint64_t)sb;
        } else {
            negative = 0;
            count = ub;
        }
        if (negative) {
            *trap = TC_EK_NEGATIVE_SHIFT;
            return 0;
        }
        if (op == TC_SHR) {
            if (count >= width) {
                *out = 0;
                return 1;
            }
            if (is_signed) {
                uint64_t result = ua >> count;
                if (count != 0 && (ua & (UINT64_C(1) << (width - 1u))) != 0) {
                    result |= mask ^ (mask >> count);
                }
                *out = result & mask;
            } else {
                *out = ua >> count;
            }
            return 1;
        }
        if (mode == TC_WRAP) {
            *out = count >= width ? 0 : (ua << count) & mask;
            return 1;
        }
        /* strict shl: mathematical value a * 2^k must be representable. */
        if (count >= width) {
            if (ua != 0) {
                *trap = TC_EK_INTEGER_OVERFLOW;
                return 0;
            }
            *out = 0;
            return 1;
        }
        {
            uint64_t magnitude;
            uint64_t limit;
            if (is_signed) {
                magnitude = sa < 0 ? (UINT64_C(0) - (uint64_t)sa)
                                   : (uint64_t)sa;
                limit = width >= 64u
                            ? (UINT64_C(1) << 63)
                            : (UINT64_C(1) << (width - 1u));
                if (count != 0 && magnitude > (limit >> count)) {
                    *trap = TC_EK_INTEGER_OVERFLOW;
                    return 0;
                }
                if (sa >= 0 && count != 0 &&
                    magnitude > (((limit - 1u) >> count))) {
                    *trap = TC_EK_INTEGER_OVERFLOW;
                    return 0;
                }
                *out = (((uint64_t)sa) << count) & mask;
            } else {
                TcWide uproduct;
                if (!tc_wide_shl_checked(tc_wide_from_u64(ua), count,
                                         &uproduct) ||
                    tc_wide_gt_u64(uproduct, mask)) {
                    *trap = TC_EK_INTEGER_OVERFLOW;
                    return 0;
                }
                *out = tc_wide_low_u64(uproduct) & mask;
            }
        }
        return 1;
    }
    case TC_AND:
        *out = ua & ub;
        return 1;
    case TC_OR:
        *out = ua | ub;
        return 1;
    case TC_XOR:
        *out = ua ^ ub;
        return 1;
    default:
        *trap = TC_SEM_NO_TRAP;
        return 0;
    }
}

static int integer_unop(unsigned W, const TcType *type, TcOpcode op,
                        TcOpMode mode, uint64_t a, uint64_t *out,
                        TcErrorKind *trap) {
    unsigned width = tc_sem_type_width(type, W);
    uint64_t mask = low_mask(width);
    int is_signed = tc_type_is_signed_integer(type);
    uint64_t ua = a & mask;
    int64_t sa = sign_extend(ua, width);

    switch (op) {
    case TC_NEG:
        if (is_signed && mode != TC_WRAP) {
            int64_t result;
            if (ua == (UINT64_C(1) << (width - 1u))) {
                *trap = TC_EK_INTEGER_OVERFLOW;
                return 0;
            }
            result = -sa;
            if (!signed_fits(result, width)) {
                *trap = TC_EK_INTEGER_OVERFLOW;
                return 0;
            }
            *out = int_result(result, width);
            return 1;
        }
        *out = (UINT64_C(0) - ua) & mask;
        return 1;
    case TC_ABS:
        if (is_signed) {
            if (ua == (UINT64_C(1) << (width - 1u))) {
                *trap = TC_EK_INTEGER_OVERFLOW;
                return 0;
            }
            if (sa < 0) {
                *out = int_result(-sa, width);
            } else {
                *out = ua;
            }
            return 1;
        }
        *out = ua;
        return 1;
    case TC_NOT:
        if (type != NULL && type->tag == TC_TY_BOOL) {
            *out = ua != 0 ? 0u : 1u;
        } else {
            *out = (~ua) & mask;
        }
        return 1;
    default:
        *trap = TC_SEM_NO_TRAP;
        return 0;
    }
}

int tc_sem_int_binop(unsigned W, const TcType *type, TcOpcode op, TcOpMode mode,
                     uint64_t a, uint64_t b, uint64_t *out,
                     TcErrorKind *trap) {
    if (trap != NULL) {
        *trap = TC_SEM_NO_TRAP;
    }
    if (type == NULL || out == NULL) {
        return 0;
    }
    if (type->tag == TC_TY_BOOL) {
        /* bool and/or/xor are logical and always produce 0/1. */
        int ba = a != 0;
        int bb = b != 0;
        if (op == TC_AND) {
            *out = (ba && bb) ? 1u : 0u;
        } else if (op == TC_OR) {
            *out = (ba || bb) ? 1u : 0u;
        } else if (op == TC_XOR) {
            *out = (ba != bb) ? 1u : 0u;
        } else {
            return 0;
        }
        return 1;
    }
    if (!is_integer_type(type)) {
        return 0;
    }
    return integer_binop(W, type, op, mode, a, b, out, trap);
}

int tc_sem_int_unop(unsigned W, const TcType *type, TcOpcode op, TcOpMode mode,
                    uint64_t a, uint64_t *out, TcErrorKind *trap) {
    if (trap != NULL) {
        *trap = TC_SEM_NO_TRAP;
    }
    if (out == NULL || type == NULL) {
        return 0;
    }
    if (type->tag == TC_TY_BOOL) {
        if (op != TC_NOT) {
            return 0;
        }
        *out = a != 0 ? 0u : 1u;
        return 1;
    }
    if (!is_integer_type(type)) {
        return 0;
    }
    return integer_unop(W, type, op, mode, a, out, trap);
}

/* ------------------------------------------------------------------------- */
/* IEEE-754 helpers.                                                         */
/* ------------------------------------------------------------------------- */

static int tc_bit_length64(uint64_t value);

static float f32_from_bits(uint64_t bits) {
    uint32_t raw = (uint32_t)bits;
    float value;
    tc_rt_memcpy(&value, &raw, sizeof(value));
    return value;
}

static uint64_t f32_to_bits(float value) {
    uint32_t raw;
    tc_rt_memcpy(&raw, &value, sizeof(raw));
    return raw;
}

/* Round `value / 2^shift` to an integer, ties to even.  Used by the exact
 * integer-to-floating conversions so a 32-bit target never needs a compiler
 * runtime helper for 64-bit integer conversion. */
static uint64_t round_shift_ties_even(uint64_t value, unsigned shift) {
    uint64_t quotient;
    uint64_t remainder;
    uint64_t half;
    if (shift == 0u) {
        return value;
    }
    if (shift >= 64u) {
        if (shift == 64u && value > (UINT64_C(1) << 63)) {
            return 1;
        }
        return 0;
    }
    quotient = value >> shift;
    remainder = value & ((UINT64_C(1) << shift) - 1u);
    half = UINT64_C(1) << (shift - 1u);
    if (remainder > half || (remainder == half && (quotient & 1u) != 0u)) {
        quotient++;
    }
    return quotient;
}

/* Exact integer-to-binary64 conversion.  All uint64 magnitudes convert to a
 * normal binary64 value, so only the significand needs rounding. */
static uint64_t int_to_f64_bits(uint64_t magnitude, int negative) {
    uint64_t sign = negative ? (UINT64_C(1) << 63) : 0;
    unsigned bits;
    int exponent;
    uint64_t significand;
    if (magnitude == 0) {
        return sign;
    }
    bits = (unsigned)tc_bit_length64(magnitude);
    exponent = (int)bits - 1;
    if (bits <= 53u) {
        significand = magnitude << (53u - bits);
    } else {
        unsigned shift = bits - 53u;
        significand = round_shift_ties_even(magnitude, shift);
        if (significand == (UINT64_C(1) << 53)) {
            significand >>= 1;
            exponent++;
        }
    }
    return sign | ((uint64_t)(exponent + 1023) << 52) |
           (significand & UINT64_C(0xfffffffffffff));
}

/* Exact integer-to-binary32 conversion (uint64 magnitudes stay far below the
 * binary32 overflow threshold). */
static uint64_t int_to_f32_bits(uint64_t magnitude, int negative) {
    uint64_t sign = negative ? (UINT64_C(1) << 31) : 0;
    unsigned bits;
    int exponent;
    uint64_t significand;
    if (magnitude == 0) {
        return sign;
    }
    bits = (unsigned)tc_bit_length64(magnitude);
    exponent = (int)bits - 1;
    if (bits <= 24u) {
        significand = magnitude << (24u - bits);
    } else {
        unsigned shift = bits - 24u;
        significand = round_shift_ties_even(magnitude, shift);
        if (significand == (UINT64_C(1) << 24)) {
            significand >>= 1;
            exponent++;
        }
    }
    return sign | ((uint64_t)(exponent + 127) << 23) |
           (significand & UINT64_C(0x7fffff));
}

uint64_t tc_sem_nan_bits(unsigned width) {
    if (width == 32u) {
        return UINT64_C(0x7fc00000);
    }
    return UINT64_C(0x7ff8000000000000);
}

uint64_t tc_sem_inf_bits(unsigned width, int negative) {
    uint64_t bits = width == 32u ? UINT64_C(0x7f800000)
                                 : UINT64_C(0x7ff0000000000000);
    if (width == 32u) {
        bits |= negative ? UINT64_C(0x80000000) : 0;
    } else {
        bits |= negative ? UINT64_C(0x8000000000000000) : 0;
    }
    return bits;
}

static int tc_bit_length64(uint64_t value) {
    return value == 0 ? 0 : 64 - __builtin_clzll(value);
}

static int tc_bit_length32(uint32_t value) {
    return value == 0 ? 0 : 32 - __builtin_clz(value);
}

/* Exact floating-point remainder of finite operands (standard §6.3.3: the
 * quotient is the exact rational quotient truncated toward zero, so the
 * remainder is x - trunc(x/y)*y).  Both results are exactly representable in
 * their format, so no rounding happens here and no host libm is involved.
 *
 * With x = Ma * 2^Ea and y = Mb * 2^Eb, the magnitude of the remainder is
 * (Ma * 2^(Ea-Eb) mod Mb) * 2^Eb.  When |x| >= |y| the exponent difference is
 * non-negative and the residue is computed by repeated doubling modulo Mb;
 * both mantissas fit in 53/24 bits, so the intermediate stays in 64 bits. */
static uint64_t tc_f64_fmod_bits(uint64_t a, uint64_t b) {
    uint64_t sign = a & (UINT64_C(1) << 63);
    uint64_t ax = a & ~(UINT64_C(1) << 63);
    uint64_t bx = b & ~(UINT64_C(1) << 63);
    unsigned exponent_a = (unsigned)(ax >> 52) & 0x7ffu;
    unsigned exponent_b = (unsigned)(bx >> 52) & 0x7ffu;
    uint64_t mantissa_a = ax & ((UINT64_C(1) << 52) - 1u);
    uint64_t mantissa_b = bx & ((UINT64_C(1) << 52) - 1u);
    int scale_a;
    int scale_b;
    long shift;
    uint64_t residue;
    if (exponent_a == 0) {
        scale_a = -1074;
    } else {
        mantissa_a |= UINT64_C(1) << 52;
        scale_a = (int)exponent_a - 1075;
    }
    if (exponent_b == 0) {
        scale_b = -1074;
    } else {
        mantissa_b |= UINT64_C(1) << 52;
        scale_b = (int)exponent_b - 1075;
    }
    if (mantissa_b == 0) {
        return sign;
    }
    if (mantissa_a == 0 ||
        tc_bit_length64(mantissa_a) - 1 + scale_a <
            tc_bit_length64(mantissa_b) - 1 + scale_b) {
        return a;
    }
    shift = (long)scale_a - (long)scale_b;
    residue = mantissa_a % mantissa_b;
    while (shift-- > 0) {
        residue <<= 1;
        if (residue >= mantissa_b) {
            residue -= mantissa_b;
        }
    }
    if (residue == 0) {
        return sign;
    }
    {
        int exponent = scale_b + tc_bit_length64(residue) - 1;
        if (exponent >= -1022) {
            int bits = tc_bit_length64(residue);
            uint64_t mantissa = residue << (53 - bits);
            return sign | ((uint64_t)(exponent + 1023) << 52) |
                   (mantissa & ((UINT64_C(1) << 52) - 1u));
        }
        /* The subnormal unit is 2^-1074 and scale_b >= -1074. */
        return sign | (residue << (scale_b + 1074));
    }
}

static uint32_t tc_f32_fmod_bits(uint32_t a, uint32_t b) {
    uint32_t sign = a & UINT32_C(0x80000000);
    uint32_t ax = a & UINT32_C(0x7fffffff);
    uint32_t bx = b & UINT32_C(0x7fffffff);
    unsigned exponent_a = (ax >> 23) & 0xffu;
    unsigned exponent_b = (bx >> 23) & 0xffu;
    uint32_t mantissa_a = ax & UINT32_C(0x7fffff);
    uint32_t mantissa_b = bx & UINT32_C(0x7fffff);
    int scale_a;
    int scale_b;
    long shift;
    uint32_t residue;
    if (exponent_a == 0) {
        scale_a = -149;
    } else {
        mantissa_a |= UINT32_C(1) << 23;
        scale_a = (int)exponent_a - 150;
    }
    if (exponent_b == 0) {
        scale_b = -149;
    } else {
        mantissa_b |= UINT32_C(1) << 23;
        scale_b = (int)exponent_b - 150;
    }
    if (mantissa_b == 0) {
        return sign;
    }
    if (mantissa_a == 0 ||
        tc_bit_length32(mantissa_a) - 1 + scale_a <
            tc_bit_length32(mantissa_b) - 1 + scale_b) {
        return a;
    }
    shift = (long)scale_a - (long)scale_b;
    residue = mantissa_a % mantissa_b;
    while (shift-- > 0) {
        residue <<= 1;
        if (residue >= mantissa_b) {
            residue -= mantissa_b;
        }
    }
    if (residue == 0) {
        return sign;
    }
    {
        int exponent = scale_b + tc_bit_length32(residue) - 1;
        if (exponent >= -126) {
            int bits = tc_bit_length32(residue);
            uint32_t mantissa = residue << (24 - bits);
            return sign | ((uint32_t)(exponent + 127) << 23) |
                   (mantissa & UINT32_C(0x7fffff));
        }
        return sign | (residue << (scale_b + 149));
    }
}

typedef struct {
    uint64_t mantissa; /* without implicit hidden bit materialization issues */
    int scale;         /* value = mantissa * 2^scale */
    int sign;
    int is_nan;
    int is_inf;
    int is_zero;
    int is_subnormal;
} TcFpParts;

static int fp_parts(uint64_t bits, unsigned width, TcFpParts *out) {
    tc_rt_memset(out, 0, sizeof(*out));
    if (width == 32u) {
        uint32_t raw = (uint32_t)bits;
        unsigned exponent = (raw >> 23) & 0xffu;
        uint32_t fraction = raw & 0x7fffffu;
        out->sign = (raw >> 31) != 0;
        if (exponent == 0xffu) {
            if (fraction != 0) {
                out->is_nan = 1;
            } else {
                out->is_inf = 1;
            }
            return 1;
        }
        if (exponent == 0) {
            out->mantissa = fraction;
            out->scale = 1 - 127 - 23;
            out->is_zero = fraction == 0;
            out->is_subnormal = fraction != 0;
        } else {
            out->mantissa = (UINT64_C(1) << 23) | fraction;
            out->scale = (int)exponent - 127 - 23;
        }
        return 1;
    }
    if (width == 64u) {
        unsigned exponent = (unsigned)((bits >> 52) & 0x7ffu);
        uint64_t fraction = bits & UINT64_C(0xfffffffffffff);
        out->sign = (bits >> 63) != 0;
        if (exponent == 0x7ffu) {
            if (fraction != 0) {
                out->is_nan = 1;
            } else {
                out->is_inf = 1;
            }
            return 1;
        }
        if (exponent == 0) {
            out->mantissa = fraction;
            out->scale = 1 - 1023 - 52;
            out->is_zero = fraction == 0;
            out->is_subnormal = fraction != 0;
        } else {
            out->mantissa = (UINT64_C(1) << 52) | fraction;
            out->scale = (int)exponent - 1023 - 52;
        }
        return 1;
    }
    return 0;
}

static int wide_shl_checked(TcWide value, unsigned shift,
                                 TcWide *out) {
    return tc_wide_shl_checked(value, shift, out);
}

/* Whether subnormal result r of mul(a,b) is the exact product. */
static int mul_subnormal_exact(const TcFpParts *a, const TcFpParts *b,
                               const TcFpParts *r) {
    TcWide product;
    TcWide shifted;
    long diff;
    if (a->is_nan || b->is_nan || a->is_inf || b->is_inf || a->is_zero ||
        b->is_zero || !r->is_subnormal) {
        return 0;
    }
    product = tc_wide_mul_u64(a->mantissa, b->mantissa);
    diff = (long)a->scale + (long)b->scale - (long)r->scale;
    if (diff >= 0) {
        if (!wide_shl_checked(product, (unsigned)diff, &shifted)) {
            return 0;
        }
        return tc_wide_eq_u64(shifted, r->mantissa);
    }
    if (!wide_shl_checked(tc_wide_from_u64(r->mantissa),
                               (unsigned)(-diff), &shifted)) {
        return 0;
    }
    return tc_wide_cmp(product, shifted) == 0;
}

/* Whether subnormal result r of div(a,b) is the exact quotient. */
static int div_subnormal_exact(const TcFpParts *a, const TcFpParts *b,
                               const TcFpParts *r) {
    TcWide product;
    TcWide shifted;
    long diff;
    if (a->is_nan || b->is_nan || a->is_inf || b->is_inf || a->is_zero ||
        b->is_zero || !r->is_subnormal) {
        return 0;
    }
    product = tc_wide_mul_u64(r->mantissa, b->mantissa);
    diff = (long)r->scale + (long)b->scale - (long)a->scale;
    if (diff >= 0) {
        if (!wide_shl_checked(product, (unsigned)diff, &shifted)) {
            return 0;
        }
        return tc_wide_eq_u64(shifted, a->mantissa);
    }
    if (!wide_shl_checked(tc_wide_from_u64(a->mantissa),
                               (unsigned)(-diff), &shifted)) {
        return 0;
    }
    return tc_wide_cmp(product, shifted) == 0;
}

/* Whether result r of add/sub is the exact mathematical sum.  The exact result
 * is compared as mantissa * 2^scale against r, using 128-bit integers.
 * Operands are finite; r is subnormal or zero. */
static int add_sub_exact(const TcFpParts *a, const TcFpParts *b, int subtract,
                         const TcFpParts *r) {
    int ea;
    int eb;
    long common;
    unsigned da;
    unsigned db;
    TcWide ma;
    TcWide mb;
    TcWide magnitude;
    long diff;

    if (a->is_nan || b->is_nan || a->is_inf || b->is_inf) {
        return 0;
    }
    if (a->is_zero && b->is_zero) {
        return r->mantissa == 0;
    }
    if (a->is_zero) {
        /* Exact result is +/-b. */
        int sign_b = subtract ? !b->sign : b->sign;
        return b->mantissa == r->mantissa && b->scale == r->scale &&
               (b->is_zero ? r->mantissa == 0 : sign_b == r->sign);
    }
    if (b->is_zero) {
        return a->mantissa == r->mantissa && a->scale == r->scale &&
               a->sign == r->sign;
    }
    ea = a->scale;
    eb = b->scale;
    common = ea < eb ? ea : eb;
    da = (unsigned)(ea - common);
    db = (unsigned)(eb - common);
    if (!wide_shl_checked(tc_wide_from_u64(a->mantissa), da, &ma) ||
        !wide_shl_checked(tc_wide_from_u64(b->mantissa), db, &mb)) {
        return 0;
    }
    {
        int sign_b = subtract ? !b->sign : b->sign;
        if (a->sign == sign_b) {
            if (!tc_wide_add_checked(ma, mb, &magnitude)) {
                return 0;
            }
            if (r->mantissa == 0 || r->sign != a->sign) {
                return 0;
            }
        } else if (tc_wide_cmp(ma, mb) == 0) {
            return r->mantissa == 0;
        } else if (tc_wide_cmp(ma, mb) > 0) {
            magnitude = tc_wide_sub(ma, mb);
            if (r->mantissa == 0 || r->sign != a->sign) {
                return 0;
            }
        } else {
            magnitude = tc_wide_sub(mb, ma);
            if (r->mantissa == 0 || r->sign != sign_b) {
                return 0;
            }
        }
    }
    diff = (long)common - (long)r->scale;
    if (diff >= 0) {
        TcWide shifted;
        if (!wide_shl_checked(magnitude, (unsigned)diff, &shifted)) {
            return 0;
        }
        return tc_wide_eq_u64(shifted, r->mantissa);
    }
    {
        TcWide shifted;
        if (!wide_shl_checked(tc_wide_from_u64(r->mantissa),
                                   (unsigned)(-diff), &shifted)) {
            return 0;
        }
        return tc_wide_cmp(magnitude, shifted) == 0;
    }
}

static int fp_is_tiny(const TcFpParts *r) {
    return r->is_subnormal || r->is_zero;
}

/* ------------------------------------------------------------------------- */
/* Software binary64.                                                        */
/*                                                                           */
/* The 32-bit target must not require an FPU for float64: the standard's     */
/* soft-float requirement is implemented with integer arithmetic.  The       */
/* algorithms are the classic IEEE-754 pack/unpack, align, round-ties-to-even */
/* form; binary32 still uses the target FPU under the softfp ABI.            */
/* ------------------------------------------------------------------------- */

#define SF_MANTBITS64 52u
#define SF_EXPBITS64 11u
#define SF_BIAS64 (-1023)
#define SF_MANTBITS32 23u
#define SF_EXPBITS32 8u
#define SF_BIAS32 (-127)

static uint64_t sf_nan64(void) {
    return UINT64_C(0x7ff8000000000000);
}

static uint32_t sf_nan32(void) {
    return UINT32_C(0x7fc00000);
}

static uint64_t sf_inf64(void) {
    return UINT64_C(0x7ff0000000000000);
}

static uint64_t sf_neg64(void) {
    return UINT64_C(0x8000000000000000);
}

static uint64_t sf_unpack64(uint64_t f, uint64_t *mantissa, int *exponent,
                            int *is_inf, int *is_nan) {
    uint64_t sign = f & sf_neg64();
    *mantissa = f & ((UINT64_C(1) << SF_MANTBITS64) - 1u);
    *exponent = (int)((f >> SF_MANTBITS64) & 0x7ffu);
    *is_inf = 0;
    *is_nan = 0;
    if (*exponent == 0x7ff) {
        if (*mantissa != 0) {
            *is_nan = 1;
        } else {
            *is_inf = 1;
        }
        return sign;
    }
    if (*exponent == 0) {
        if (*mantissa != 0) {
            *exponent += SF_BIAS64 + 1;
            while (*mantissa < (UINT64_C(1) << SF_MANTBITS64)) {
                *mantissa <<= 1;
                (*exponent)--;
            }
        }
    } else {
        *mantissa |= UINT64_C(1) << SF_MANTBITS64;
        *exponent += SF_BIAS64;
    }
    return sign;
}

static uint32_t sf_unpack32(uint32_t f, uint32_t *mantissa, int *exponent,
                            int *is_inf, int *is_nan) {
    uint32_t sign = f & UINT32_C(0x80000000);
    *mantissa = f & ((UINT32_C(1) << SF_MANTBITS32) - 1u);
    *exponent = (int)((f >> SF_MANTBITS32) & 0xffu);
    *is_inf = 0;
    *is_nan = 0;
    if (*exponent == 0xffu) {
        if (*mantissa != 0) {
            *is_nan = 1;
        } else {
            *is_inf = 1;
        }
        return sign;
    }
    if (*exponent == 0) {
        if (*mantissa != 0) {
            *exponent += SF_BIAS32 + 1;
            while (*mantissa < (UINT32_C(1) << SF_MANTBITS32)) {
                *mantissa <<= 1;
                (*exponent)--;
            }
        }
    } else {
        *mantissa |= UINT32_C(1) << SF_MANTBITS32;
        *exponent += SF_BIAS32;
    }
    return sign;
}

static uint64_t sf_pack64(uint64_t sign, uint64_t mantissa, int exponent,
                          uint64_t trunc) {
    uint64_t mantissa0 = mantissa;
    uint64_t trunc0 = trunc;
    int exponent0 = exponent;
    if (mantissa == 0) {
        return sign;
    }
    while (mantissa < (UINT64_C(1) << SF_MANTBITS64)) {
        mantissa <<= 1;
        exponent--;
    }
    while (mantissa >= (UINT64_C(4) << SF_MANTBITS64)) {
        trunc |= mantissa & 1u;
        mantissa >>= 1;
        exponent++;
    }
    if (mantissa >= (UINT64_C(2) << SF_MANTBITS64)) {
        if ((mantissa & 1u) != 0 && (trunc != 0 || (mantissa & 2u) != 0)) {
            mantissa++;
            if (mantissa >= (UINT64_C(4) << SF_MANTBITS64)) {
                mantissa >>= 1;
                exponent++;
            }
        }
        mantissa >>= 1;
        exponent++;
    }
    if (exponent >= 1024) {
        return sign ^ sf_inf64();
    }
    if (exponent < SF_BIAS64 + 1) {
        if (exponent < SF_BIAS64 - (int)SF_MANTBITS64) {
            return sign;
        }
        mantissa = mantissa0;
        exponent = exponent0;
        trunc = trunc0;
        while (exponent < SF_BIAS64) {
            trunc |= mantissa & 1u;
            mantissa >>= 1;
            exponent++;
        }
        if ((mantissa & 1u) != 0 && (trunc != 0 || (mantissa & 2u) != 0)) {
            mantissa++;
        }
        mantissa >>= 1;
        exponent++;
        if (mantissa < (UINT64_C(1) << SF_MANTBITS64)) {
            return sign | mantissa;
        }
    }
    return sign | ((uint64_t)(exponent - SF_BIAS64) << SF_MANTBITS64) |
           (mantissa & ((UINT64_C(1) << SF_MANTBITS64) - 1u));
}

static uint32_t sf_pack32(uint32_t sign, uint32_t mantissa, int exponent,
                          uint32_t trunc) {
    uint32_t mantissa0 = mantissa;
    uint32_t trunc0 = trunc;
    int exponent0 = exponent;
    if (mantissa == 0) {
        return sign;
    }
    while (mantissa < (UINT32_C(1) << SF_MANTBITS32)) {
        mantissa <<= 1;
        exponent--;
    }
    while (mantissa >= (UINT32_C(4) << SF_MANTBITS32)) {
        trunc |= mantissa & 1u;
        mantissa >>= 1;
        exponent++;
    }
    if (mantissa >= (UINT32_C(2) << SF_MANTBITS32)) {
        if ((mantissa & 1u) != 0 && (trunc != 0 || (mantissa & 2u) != 0)) {
            mantissa++;
            if (mantissa >= (UINT32_C(4) << SF_MANTBITS32)) {
                mantissa >>= 1;
                exponent++;
            }
        }
        mantissa >>= 1;
        exponent++;
    }
    if (exponent >= 128) {
        return sign ^ UINT32_C(0x7f800000);
    }
    if (exponent < SF_BIAS32 + 1) {
        if (exponent < SF_BIAS32 - (int)SF_MANTBITS32) {
            return sign;
        }
        mantissa = mantissa0;
        exponent = exponent0;
        trunc = trunc0;
        while (exponent < SF_BIAS32) {
            trunc |= mantissa & 1u;
            mantissa >>= 1;
            exponent++;
        }
        if ((mantissa & 1u) != 0 && (trunc != 0 || (mantissa & 2u) != 0)) {
            mantissa++;
        }
        mantissa >>= 1;
        exponent++;
        if (mantissa < (UINT32_C(1) << SF_MANTBITS32)) {
            return sign | mantissa;
        }
    }
    return sign | ((uint32_t)(exponent - SF_BIAS32) << SF_MANTBITS32) |
           (mantissa & ((UINT32_C(1) << SF_MANTBITS32) - 1u));
}

static uint64_t sf_add64(uint64_t f, uint64_t g) {
    uint64_t fs, fm, gs, gm;
    uint64_t trunc;
    int fe, ge, fi, fn, gi, gn;
    uint32_t shift;
    fs = sf_unpack64(f, &fm, &fe, &fi, &fn);
    gs = sf_unpack64(g, &gm, &ge, &gi, &gn);
    if (fn || gn) {
        return sf_nan64();
    }
    if (fi && gi && fs != gs) {
        return sf_nan64();
    }
    if (fi) {
        return f;
    }
    if (gi) {
        return g;
    }
    if (fm == 0 && gm == 0 && fs != 0 && gs != 0) {
        return f;
    }
    if (fm == 0) {
        if (gm == 0) {
            g ^= gs;
        }
        return g;
    }
    if (gm == 0) {
        return f;
    }
    if (fe < ge || (fe == ge && fm < gm)) {
        uint64_t tf = f, tm = fm, ts = fs;
        int te = fe;
        f = g; fm = gm; fs = gs; fe = ge;
        g = tf; gm = tm; gs = ts; ge = te;
    }
    shift = (uint32_t)(fe - ge);
    fm <<= 2;
    gm <<= 2;
    if (shift >= 64u) {
        trunc = gm != 0;
        gm = 0;
    } else {
        trunc = gm & ((UINT64_C(1) << shift) - 1u);
        gm >>= shift;
    }
    if (fs == gs) {
        fm += gm;
    } else {
        fm -= gm;
        if (trunc != 0) {
            fm--;
        }
    }
    if (fm == 0) {
        fs = 0;
    }
    return sf_pack64(fs, fm, fe - 2, trunc);
}

static void sf_mullu(uint64_t u, uint64_t v, uint64_t *lo, uint64_t *hi) {
    uint64_t u0 = u & UINT64_C(0xffffffff);
    uint64_t u1 = u >> 32;
    uint64_t v0 = v & UINT64_C(0xffffffff);
    uint64_t v1 = v >> 32;
    uint64_t w0 = u0 * v0;
    uint64_t t = u1 * v0 + (w0 >> 32);
    uint64_t w1 = t & UINT64_C(0xffffffff);
    uint64_t w2 = t >> 32;
    w1 += u0 * v1;
    *lo = u * v;
    *hi = u1 * v1 + w2 + (w1 >> 32);
}

static uint64_t sf_mul64(uint64_t f, uint64_t g) {
    uint64_t fs, fm, gs, gm, lo, hi, trunc, mantissa;
    int fe, ge, fi, fn, gi, gn;
    fs = sf_unpack64(f, &fm, &fe, &fi, &fn);
    gs = sf_unpack64(g, &gm, &ge, &gi, &gn);
    if (fn || gn) {
        return sf_nan64();
    }
    if (fi && gi) {
        return f ^ gs;
    }
    if ((fi && gm == 0) || (fm == 0 && gi)) {
        return sf_nan64();
    }
    if (fm == 0) {
        return f ^ gs;
    }
    if (gm == 0) {
        return g ^ fs;
    }
    sf_mullu(fm, gm, &lo, &hi);
    trunc = lo & ((UINT64_C(1) << (SF_MANTBITS64 - 1u)) - 1u);
    mantissa = (hi << (64u - (SF_MANTBITS64 - 1u))) |
               (lo >> (SF_MANTBITS64 - 1u));
    return sf_pack64(fs ^ gs, mantissa, fe + ge - 1, trunc);
}

static void sf_divlu(uint64_t u1, uint64_t u0, uint64_t v, uint64_t *q_out,
                     uint64_t *r_out) {
    const uint64_t b = UINT64_C(1) << 32;
    uint64_t s = 0;
    uint64_t vn1, vn0, un32, un10, un1, un0, q1, rhat, un21, q0;
    if (u1 >= v) {
        *q_out = UINT64_MAX;
        *r_out = UINT64_MAX;
        return;
    }
    while ((v & (UINT64_C(1) << 63)) == 0) {
        s++;
        v <<= 1;
    }
    vn1 = v >> 32;
    vn0 = v & UINT64_C(0xffffffff);
    un32 = (u1 << s) | (s != 0 ? u0 >> (64u - s) : 0);
    un10 = u0 << s;
    un1 = un10 >> 32;
    un0 = un10 & UINT64_C(0xffffffff);
    q1 = un32 / vn1;
    rhat = un32 - q1 * vn1;
    while (q1 >= b || q1 * vn0 > b * rhat + un1) {
        q1--;
        rhat += vn1;
        if (rhat >= b) {
            break;
        }
    }
    un21 = un32 * b + un1 - q1 * v;
    q0 = un21 / vn1;
    rhat = un21 - q0 * vn1;
    while (q0 >= b || q0 * vn0 > b * rhat + un0) {
        q0--;
        rhat += vn1;
        if (rhat >= b) {
            break;
        }
    }
    *q_out = q1 * b + q0;
    *r_out = (un21 * b + un0 - q0 * v) >> s;
}

static uint64_t sf_div64(uint64_t f, uint64_t g) {
    uint64_t fs, fm, gs, gm, q, r;
    int fe, ge, fi, fn, gi, gn;
    fs = sf_unpack64(f, &fm, &fe, &fi, &fn);
    gs = sf_unpack64(g, &gm, &ge, &gi, &gn);
    if (fn || gn) {
        return sf_nan64();
    }
    if (fi && gi) {
        return sf_nan64();
    }
    if (!fi && !gi && fm == 0 && gm == 0) {
        return sf_nan64();
    }
    if (fi) {
        return fs ^ gs ^ sf_inf64();
    }
    if (!gi && gm == 0) {
        return fs ^ gs ^ sf_inf64();
    }
    if (gi || fm == 0) {
        return fs ^ gs;
    }
    sf_divlu(fm >> (64u - (SF_MANTBITS64 + 2u)),
             fm << (SF_MANTBITS64 + 2u), gm, &q, &r);
    return sf_pack64(fs ^ gs, q, fe - ge - 2, r);
}

static uint32_t sf_f64to32(uint64_t f) {
    uint64_t fs, fm;
    int fe, fi, fn;
    uint32_t fs32;
    unsigned d = SF_MANTBITS64 - SF_MANTBITS32 - 1u;
    fs = sf_unpack64(f, &fm, &fe, &fi, &fn);
    if (fn) {
        return sf_nan32();
    }
    fs32 = (uint32_t)(fs >> 32);
    if (fi) {
        return fs32 ^ UINT32_C(0x7f800000);
    }
    return sf_pack32(fs32, (uint32_t)(fm >> d), fe - 1,
                     (uint32_t)(fm & ((UINT64_C(1) << d) - 1u)));
}

static uint64_t sf_f32to64(uint32_t f) {
    uint64_t fs64, fm;
    uint32_t fm32, fs;
    int fe, fi, fn;
    unsigned d = SF_MANTBITS64 - SF_MANTBITS32;
    fs = sf_unpack32(f, &fm32, &fe, &fi, &fn);
    if (fn) {
        return sf_nan64();
    }
    fs64 = (uint64_t)fs << 32;
    fm = (uint64_t)fm32 << d;
    if (fi) {
        return fs64 ^ sf_inf64();
    }
    return sf_pack64(fs64, fm, fe, 0);
}

static int sf_cmp64(uint64_t f, uint64_t g, int *is_nan) {
    uint64_t fs, fm, gs, gm;
    int fe, ge, fi, fn, gi, gn;
    fs = sf_unpack64(f, &fm, &fe, &fi, &fn);
    gs = sf_unpack64(g, &gm, &ge, &gi, &gn);
    (void)fe;
    (void)ge;
    if (fn || gn) {
        *is_nan = 1;
        return 0;
    }
    *is_nan = 0;
    if (!fi && !gi && fm == 0 && gm == 0) {
        return 0;
    }
    if (fs != gs) {
        return fs != 0 ? -1 : 1;
    }
    if (f == g) {
        return 0;
    }
    if (fs != 0) {
        return f > g ? -1 : 1;
    }
    return f < g ? -1 : 1;
}

static int float_binop(unsigned W, const TcType *type, TcOpcode op,
                       TcOpMode mode, uint64_t a_bits, uint64_t b_bits,
                       uint64_t *out, TcErrorKind *trap) {
    unsigned width = tc_sem_type_width(type, W);
    int ieee = mode == TC_IEEE;
    TcFpParts a;
    TcFpParts b;
    TcFpParts r;
    uint64_t result = 0;

    if (width != 32u && width != 64u) {
        *trap = TC_SEM_NO_TRAP;
        return 0;
    }
    if (!fp_parts(a_bits, width, &a) || !fp_parts(b_bits, width, &b)) {
        *trap = TC_SEM_NO_TRAP;
        return 0;
    }
    /* INVALID first (standard §6.3 exception priority). */
    if (a.is_nan || b.is_nan) {
        if (ieee) {
            *out = tc_sem_nan_bits(width);
            return 1;
        }
        *trap = TC_EK_FLOAT_INVALID;
        return 0;
    }
    switch (op) {
    case TC_ADD:
        if (a.is_inf && b.is_inf && a.sign != b.sign) {
            if (ieee) {
                *out = tc_sem_nan_bits(width);
                return 1;
            }
            *trap = TC_EK_FLOAT_INVALID;
            return 0;
        }
        if (width == 32u) {
            result = f32_to_bits(f32_from_bits(a_bits) + f32_from_bits(b_bits));
        } else {
            result = sf_add64(a_bits, b_bits);
        }
        break;
    case TC_SUB:
        if (a.is_inf && b.is_inf && a.sign == b.sign) {
            if (ieee) {
                *out = tc_sem_nan_bits(width);
                return 1;
            }
            *trap = TC_EK_FLOAT_INVALID;
            return 0;
        }
        if (width == 32u) {
            result = f32_to_bits(f32_from_bits(a_bits) - f32_from_bits(b_bits));
        } else {
            result = sf_add64(a_bits, b_bits ^ sf_neg64());
        }
        break;
    case TC_MUL:
        if ((a.is_inf && b.is_zero) || (b.is_inf && a.is_zero)) {
            if (ieee) {
                *out = tc_sem_nan_bits(width);
                return 1;
            }
            *trap = TC_EK_FLOAT_INVALID;
            return 0;
        }
        if (width == 32u) {
            result = f32_to_bits(f32_from_bits(a_bits) * f32_from_bits(b_bits));
        } else {
            result = sf_mul64(a_bits, b_bits);
        }
        break;
    case TC_DIV:
        if ((a.is_inf && b.is_inf) || (a.is_zero && b.is_zero)) {
            if (ieee) {
                *out = tc_sem_nan_bits(width);
                return 1;
            }
            *trap = TC_EK_FLOAT_INVALID;
            return 0;
        }
        if (b.is_zero && !a.is_inf) {
            if (ieee) {
                result = a.sign != b.sign ? tc_sem_inf_bits(width, 1)
                                          : tc_sem_inf_bits(width, 0);
                *out = result;
                return 1;
            }
            *trap = TC_EK_DIV_ZERO;
            return 0;
        }
        if (width == 32u) {
            result = f32_to_bits(f32_from_bits(a_bits) / f32_from_bits(b_bits));
        } else {
            result = sf_div64(a_bits, b_bits);
        }
        break;
    case TC_MOD:
        if (a.is_inf || (a.is_zero && b.is_zero)) {
            if (ieee) {
                *out = tc_sem_nan_bits(width);
                return 1;
            }
            *trap = TC_EK_FLOAT_INVALID;
            return 0;
        }
        if (b.is_zero) {
            if (ieee) {
                *out = tc_sem_nan_bits(width);
                return 1;
            }
            *trap = TC_EK_DIV_ZERO;
            return 0;
        }
        if (b.is_inf) {
            *out = a_bits;
            return 1;
        }
        result = width == 32u ? tc_f32_fmod_bits((uint32_t)a_bits, (uint32_t)b_bits)
                              : tc_f64_fmod_bits(a_bits, b_bits);
        break;
    default:
        *trap = TC_SEM_NO_TRAP;
        return 0;
    }
    if (!fp_parts(result, width, &r)) {
        *trap = TC_SEM_NO_TRAP;
        return 0;
    }
    if (r.is_nan) {
        if (ieee) {
            *out = tc_sem_nan_bits(width);
            return 1;
        }
        *trap = TC_EK_FLOAT_INVALID;
        return 0;
    }
    if (ieee) {
        *out = result;
        return 1;
    }
    if (r.is_inf && !a.is_inf && !b.is_inf) {
        *trap = TC_EK_FLOAT_OVERFLOW;
        return 0;
    }
    /* UNDERFLOW: tiny after rounding and inexact (standard §6.3.2). */
    if ((op == TC_ADD || op == TC_SUB || op == TC_MUL || op == TC_DIV) &&
        fp_is_tiny(&r)) {
        int exact;
        if ((op == TC_MUL || op == TC_DIV) &&
            (a.is_zero || b.is_zero ||
             (op == TC_DIV && b.is_inf))) {
            /* A zero operand, or a finite value divided by infinity, makes
             * the result exactly zero. */
            exact = r.is_zero;
        } else if (op == TC_MUL) {
            exact = mul_subnormal_exact(&a, &b, &r);
        } else if (op == TC_DIV) {
            exact = div_subnormal_exact(&a, &b, &r);
        } else {
            exact = add_sub_exact(&a, &b, op == TC_SUB, &r);
        }
        if (!exact) {
            *trap = TC_EK_FLOAT_UNDERFLOW;
            return 0;
        }
    }
    *out = result;
    return 1;
}

static int float_unop(unsigned W, const TcType *type, TcOpcode op, uint64_t a,
                      uint64_t *out) {
    unsigned width = tc_sem_type_width(type, W);
    uint64_t sign = width == 32u ? UINT64_C(0x80000000)
                                 : UINT64_C(0x8000000000000000);
    if (width != 32u && width != 64u) {
        return 0;
    }
    if (op == TC_ABS) {
        *out = a & ~sign;
        return 1;
    }
    if (op == TC_NEG) {
        *out = a ^ sign;
        return 1;
    }
    return 0;
}

int tc_sem_float_binop(unsigned W, const TcType *type, TcOpcode op,
                       TcOpMode mode, uint64_t a, uint64_t b, uint64_t *out,
                       TcErrorKind *trap) {
    if (trap != NULL) {
        *trap = TC_SEM_NO_TRAP;
    }
    if (type == NULL || !tc_type_is_float(type) || out == NULL) {
        return 0;
    }
    return float_binop(W, type, op, mode, a, b, out, trap);
}

int tc_sem_float_unop(unsigned W, const TcType *type, TcOpcode op, uint64_t a,
                      uint64_t *out) {
    if (type == NULL || !tc_type_is_float(type) || out == NULL) {
        return 0;
    }
    return float_unop(W, type, op, a, out);
}

/* ------------------------------------------------------------------------- */
/* Comparison.                                                               */
/* ------------------------------------------------------------------------- */

int tc_sem_compare(unsigned W, const TcType *type, TcOpcode op, uint64_t a,
                   uint64_t b, uint64_t *out_bool) {
    int result = 0;
    if (type == NULL || out_bool == NULL) {
        return 0;
    }
    if (type->tag == TC_TY_BOOL) {
        if (op == TC_EQ) {
            result = (a != 0) == (b != 0);
        } else if (op == TC_NE) {
            result = (a != 0) != (b != 0);
        } else {
            return 0;
        }
        *out_bool = result ? 1u : 0u;
        return 1;
    }
    if (tc_type_is_integer(type)) {
        unsigned width = tc_sem_type_width(type, W);
        uint64_t mask = low_mask(width);
        if (tc_type_is_signed_integer(type)) {
            int64_t sa = sign_extend(a, width);
            int64_t sb = sign_extend(b, width);
            switch (op) {
            case TC_EQ: result = sa == sb; break;
            case TC_NE: result = sa != sb; break;
            case TC_LT: result = sa < sb; break;
            case TC_LE: result = sa <= sb; break;
            case TC_GT: result = sa > sb; break;
            case TC_GE: result = sa >= sb; break;
            default: return 0;
            }
        } else {
            uint64_t ua = a & mask;
            uint64_t ub = b & mask;
            switch (op) {
            case TC_EQ: result = ua == ub; break;
            case TC_NE: result = ua != ub; break;
            case TC_LT: result = ua < ub; break;
            case TC_LE: result = ua <= ub; break;
            case TC_GT: result = ua > ub; break;
            case TC_GE: result = ua >= ub; break;
            default: return 0;
            }
        }
        *out_bool = result ? 1u : 0u;
        return 1;
    }
    if (tc_type_is_float(type)) {
        unsigned width = tc_sem_type_width(type, W);
        if (width != 32u && width != 64u) {
            return 0;
        }
        if (width == 32u) {
            float fa = f32_from_bits(a);
            float fb = f32_from_bits(b);
            switch (op) {
            case TC_EQ: result = fa == fb; break;
            case TC_NE: result = fa != fb; break;
            case TC_LT: result = fa < fb; break;
            case TC_LE: result = fa <= fb; break;
            case TC_GT: result = fa > fb; break;
            case TC_GE: result = fa >= fb; break;
            default: return 0;
            }
        } else {
            int nan = 0;
            int cmp = sf_cmp64(a, b, &nan);
            switch (op) {
            case TC_EQ: result = !nan && cmp == 0; break;
            case TC_NE: result = nan || cmp != 0; break;
            case TC_LT: result = !nan && cmp < 0; break;
            case TC_LE: result = !nan && cmp <= 0; break;
            case TC_GT: result = !nan && cmp > 0; break;
            case TC_GE: result = !nan && cmp >= 0; break;
            default: return 0;
            }
        }
        *out_bool = result ? 1u : 0u;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Conversions.                                                              */
/* ------------------------------------------------------------------------- */

static int float_to_int(unsigned W, uint64_t bits, unsigned source_width,
                        const TcType *target, uint64_t *out) {
    TcFpParts parts;
    unsigned width = tc_sem_type_width(target, W);
    TcWide magnitude;
    TcWide limit;

    if (!fp_parts(bits, source_width, &parts) || parts.is_nan || parts.is_inf ||
        width == 0 || width > 64u) {
        return 0;
    }
    if (parts.is_zero) {
        *out = 0;
        return 1;
    }
    if (parts.scale >= 0) {
        unsigned shift = (unsigned)parts.scale;
        if (!tc_wide_shl_checked(tc_wide_from_u64(parts.mantissa), shift,
                                 &magnitude)) {
            return 0;
        }
    } else {
        unsigned shift = (unsigned)(-parts.scale);
        magnitude = tc_wide_from_u64(shift >= 64u
                                         ? 0
                                         : (parts.mantissa >> shift));
    }
    if (tc_type_is_signed_integer(target)) {
        limit = tc_wide_from_u64(UINT64_C(1) << (width - 1u));
        if (parts.sign) {
            if (tc_wide_cmp(magnitude, limit) > 0) {
                return 0;
            }
        } else if (tc_wide_cmp(magnitude, limit) >= 0) {
            return 0;
        }
        *out = tc_wide_low_u64(magnitude) & low_mask(width);
        if (parts.sign && tc_wide_cmp(magnitude, tc_wide_from_u64(0)) != 0) {
            *out = (UINT64_C(0) - *out) & low_mask(width);
        }
        return 1;
    }
    if (parts.sign && tc_wide_cmp(magnitude, tc_wide_from_u64(0)) != 0) {
        return 0;
    }
    limit = tc_wide_from_u64(width >= 64u ? UINT64_MAX : low_mask(width));
    if (tc_wide_cmp(magnitude, limit) > 0) {
        return 0;
    }
    *out = tc_wide_low_u64(magnitude) & low_mask(width);
    return 1;
}

int tc_sem_cast(unsigned W, const TcType *src, const TcType *dst, uint64_t bits,
                uint64_t *out, TcErrorKind *trap) {
    if (trap != NULL) {
        *trap = TC_SEM_NO_TRAP;
    }
    if (src == NULL || dst == NULL || out == NULL) {
        return 0;
    }
    if (tc_type_equal(src, dst)) {
        *out = bits & low_mask(tc_sem_type_width(dst, W));
        return 1;
    }
    if (dst->tag == TC_TY_BOOL) {
        int truth;
        if (tc_type_is_float(src)) {
            unsigned sw = tc_sem_type_width(src, W);
            uint64_t magnitude = sw == 32u ? (bits & UINT64_C(0x7fffffff))
                                           : (bits & ~sf_neg64());
            truth = magnitude != 0;
        } else {
            truth = (bits & low_mask(tc_sem_type_width(src, W))) != 0;
        }
        *out = truth ? 1u : 0u;
        return 1;
    }
    if (tc_type_is_integer(dst)) {
        if (tc_type_is_float(src)) {
            if (!float_to_int(W, bits, tc_sem_type_width(src, W), dst, out)) {
                *trap = TC_EK_CAST_OVERFLOW;
                return 0;
            }
            return 1;
        }
        if (src->tag == TC_TY_BOOL) {
            *out = bits != 0 ? 1u : 0u;
            return 1;
        }
        if (tc_type_is_integer(src)) {
            unsigned sw = tc_sem_type_width(src, W);
            unsigned tw = tc_sem_type_width(dst, W);
            if (sw == 0 || tw == 0) {
                return 0;
            }
            if (tc_type_is_signed_integer(src)) {
                int64_t value = sign_extend(bits, sw);
                if (tc_type_is_signed_integer(dst)) {
                    if (!signed_fits(value, tw)) {
                        *trap = TC_EK_CAST_OVERFLOW;
                        return 0;
                    }
                } else if (value < 0 ||
                           (uint64_t)value > low_mask(tw)) {
                    *trap = TC_EK_CAST_OVERFLOW;
                    return 0;
                }
                *out = ((uint64_t)value) & low_mask(tw);
            } else {
                uint64_t value = bits & low_mask(sw);
                if (tc_type_is_signed_integer(dst)) {
                    uint64_t max = tw >= 64u ? (uint64_t)INT64_MAX
                                             : (low_mask(tw) >> 1u);
                    if (value > max) {
                        *trap = TC_EK_CAST_OVERFLOW;
                        return 0;
                    }
                } else if (tw < 64u && value > low_mask(tw)) {
                    *trap = TC_EK_CAST_OVERFLOW;
                    return 0;
                }
                *out = value & low_mask(tw);
            }
            return 1;
        }
        return 0;
    }
    if (tc_type_is_float(dst)) {
        unsigned tw = tc_sem_type_width(dst, W);
        if (tc_type_is_float(src)) {
            unsigned sw = tc_sem_type_width(src, W);
            TcFpParts parts;
            if (sw == tw) {
                *out = bits;
                return 1;
            }
            if (!fp_parts(bits, sw, &parts)) {
                return 0;
            }
            if (parts.is_nan) {
                *out = tc_sem_nan_bits(tw);
                return 1;
            }
            if (parts.is_inf) {
                *out = tc_sem_inf_bits(tw, parts.sign);
                return 1;
            }
            if (sw == 32u && tw == 64u) {
                *out = sf_f32to64((uint32_t)bits);
                return 1;
            }
            if (sw == 64u && tw == 32u) {
                /* The input is finite here (NaN and infinity were handled
                 * above), so a float32 result with the infinity exponent is a
                 * magnitude overflow of the narrowing cast. */
                uint64_t narrowed_bits = sf_f64to32(bits);
                if ((narrowed_bits & UINT64_C(0x7fffffff)) ==
                    UINT64_C(0x7f800000)) {
                    *trap = TC_EK_CAST_OVERFLOW;
                    return 0;
                }
                *out = narrowed_bits;
                return 1;
            }
            return 0;
        }
        if (src->tag == TC_TY_BOOL || tc_type_is_integer(src)) {
            unsigned sw = tc_sem_type_width(src, W);
            uint64_t magnitude;
            int negative;
            if (sw == 0) {
                return 0;
            }
            if (tc_type_is_signed_integer(src)) {
                int64_t value = sign_extend(bits, sw);
                negative = value < 0;
                magnitude = negative ? (uint64_t)(-(value + 1)) + 1u
                                     : (uint64_t)value;
            } else {
                negative = 0;
                magnitude = bits & low_mask(sw);
            }
            *out = tw == 32u ? int_to_f32_bits(magnitude, negative)
                             : int_to_f64_bits(magnitude, negative);
            return 1;
        }
        return 0;
    }
    return 0;
}

int tc_sem_trunc(unsigned W, const TcType *src, const TcType *dst,
                 uint64_t bits, uint64_t *out) {
    if (src == NULL || dst == NULL || out == NULL ||
        !tc_type_is_integer(src) || !tc_type_is_integer(dst)) {
        return 0;
    }
    *out = bits & low_mask(tc_sem_type_width(dst, W));
    return 1;
}

int tc_sem_bitcast(unsigned W, const TcType *dst, uint64_t bits,
                   uint64_t *out) {
    unsigned width;
    if (dst == NULL || out == NULL) {
        return 0;
    }
    width = tc_sem_type_width(dst, W);
    if (width == 0) {
        return 0;
    }
    *out = bits & low_mask(width);
    return 1;
}

uint64_t tc_sem_bool_from_byte(uint8_t byte) {
    return byte != 0 ? 1u : 0u;
}
