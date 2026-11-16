/* TC 0.0.46 deterministic scalar output formatting.
 *
 * The floating renderer derives the exact decimal expansion of the binary
 * value (mantissa * 2^e2, using 5^k for negative exponents) and then rounds
 * the first six significant digits with roundTiesToEven.  This is the §11.2
 * `%g`-style rule: scientific notation when the decimal exponent is < -4 or
 * >= 6, at least two exponent digits, trailing fractional zeros removed, and
 * a preserved negative zero.
 */
#include "runtime/format.h"

#include "runtime/semantics.h"

#include <string.h>

#define DEC_BASE UINT32_C(1000000000)
#define DEC_MAX_LIMBS 192u
#define DEC_MAX_DIGITS (DEC_MAX_LIMBS * 9u)

typedef struct {
    uint32_t limb[DEC_MAX_LIMBS]; /* base 1e9, least significant first */
    size_t n;
} TcDecBig;

static void dec_set_u64(TcDecBig *a, uint64_t value) {
    a->n = 0;
    if (value == 0) {
        return;
    }
    while (value != 0) {
        a->limb[a->n++] = (uint32_t)(value % DEC_BASE);
        value /= DEC_BASE;
    }
}

static int dec_mul_small(TcDecBig *a, uint32_t factor) {
    uint64_t carry = 0;
    size_t i;
    if (a->n == 0) {
        return 1;
    }
    for (i = 0; i < a->n; i++) {
        uint64_t product = (uint64_t)a->limb[i] * factor + carry;
        a->limb[i] = (uint32_t)(product % DEC_BASE);
        carry = product / DEC_BASE;
    }
    while (carry != 0) {
        if (a->n >= DEC_MAX_LIMBS) {
            return 0;
        }
        a->limb[a->n++] = (uint32_t)(carry % DEC_BASE);
        carry /= DEC_BASE;
    }
    return 1;
}

static uint32_t dec_div_small(TcDecBig *a, uint32_t divisor) {
    uint64_t remainder = 0;
    size_t i = a->n;
    while (i > 0) {
        uint64_t current;
        i--;
        current = remainder * DEC_BASE + a->limb[i];
        a->limb[i] = (uint32_t)(current / divisor);
        remainder = current % divisor;
    }
    while (a->n > 0 && a->limb[a->n - 1u] == 0) {
        a->n--;
    }
    return (uint32_t)remainder;
}

/* Extract the exact decimal digits of `a` in most-significant-first order. */
static size_t dec_digits(const TcDecBig *a, uint8_t *digits) {
    TcDecBig copy = *a;
    uint8_t reversed[DEC_MAX_DIGITS];
    size_t count = 0;
    size_t i;
    while (copy.n != 0) {
        if (count >= DEC_MAX_DIGITS) {
            return 0;
        }
        reversed[count++] = (uint8_t)dec_div_small(&copy, 10u);
    }
    for (i = 0; i < count; i++) {
        digits[i] = reversed[count - 1u - i];
    }
    return count;
}

static void append_char(char *out, size_t cap, size_t *len, char c) {
    if (*len + 1u < cap) {
        out[*len] = c;
    }
    (*len)++;
}

static int finish(char *out, size_t cap, size_t len) {
    if (len >= cap) {
        return 0;
    }
    out[len] = '\0';
    return 1;
}

int tc_format_float(unsigned width, uint64_t bits, char *out, size_t cap) {
    uint64_t fraction_mask;
    unsigned exponent_bits;
    unsigned bias;
    unsigned fraction_bits;
    unsigned exponent;
    uint64_t fraction;
    uint64_t mantissa;
    int value_scale;
    int negative = 0;
    int is_nan = 0;
    int is_inf = 0;
    int is_zero = 0;
    TcDecBig big;
    uint8_t digits[DEC_MAX_DIGITS];
    size_t ndigits;
    uint8_t head[6];
    long exp10;
    size_t len = 0;
    size_t i;
    int k = 0;

    if (out == NULL || cap == 0) {
        return 0;
    }
    if (width == 32u) {
        exponent_bits = 8;
        bias = 127;
        fraction_bits = 23;
        fraction_mask = UINT64_C(0x7fffff);
        negative = ((bits >> 31) & 1u) != 0;
        bits &= UINT64_C(0xffffffff);
    } else if (width == 64u) {
        exponent_bits = 11;
        bias = 1023;
        fraction_bits = 52;
        fraction_mask = UINT64_C(0xfffffffffffff);
        negative = (bits >> 63) != 0;
    } else {
        return 0;
    }
    exponent = (unsigned)((bits >> fraction_bits) &
                          ((UINT64_C(1) << exponent_bits) - 1u));
    fraction = bits & fraction_mask;
    if (exponent == (unsigned)((UINT64_C(1) << exponent_bits) - 1u)) {
        if (fraction != 0) {
            is_nan = 1;
        } else {
            is_inf = 1;
        }
    } else if (exponent == 0) {
        is_zero = fraction == 0;
        mantissa = fraction;
        value_scale = 1 - (int)bias - (int)fraction_bits;
    } else {
        mantissa = (UINT64_C(1) << fraction_bits) | fraction;
        value_scale = (int)exponent - (int)bias - (int)fraction_bits;
    }
    if (is_nan) {
        if (cap < 4u) {
            return 0;
        }
        memcpy(out, "nan", 4u);
        return 1;
    }
    if (is_inf) {
        if (negative) {
            append_char(out, cap, &len, '-');
        }
        append_char(out, cap, &len, 'i');
        append_char(out, cap, &len, 'n');
        append_char(out, cap, &len, 'f');
        return finish(out, cap, len);
    }
    if (is_zero) {
        if (negative) {
            append_char(out, cap, &len, '-');
        }
        append_char(out, cap, &len, '0');
        return finish(out, cap, len);
    }
    dec_set_u64(&big, mantissa);
    if (value_scale >= 0) {
        for (i = 0; i < (size_t)value_scale; i++) {
            if (!dec_mul_small(&big, 2u)) {
                return 0;
            }
        }
    } else {
        k = -value_scale;
        for (i = 0; i < (size_t)k; i++) {
            if (!dec_mul_small(&big, 5u)) {
                return 0;
            }
        }
    }
    ndigits = dec_digits(&big, digits);
    if (ndigits == 0) {
        return 0;
    }
    /* value = digits * 10^-k: exponent of the leading digit. */
    exp10 = (long)ndigits - 1 - (long)k;
    memset(head, 0, sizeof(head));
    for (i = 0; i < 6u && i < ndigits; i++) {
        head[i] = digits[i];
    }
    if (ndigits > 6u) {
        int round_up = 0;
        if (digits[6] > 5u) {
            round_up = 1;
        } else if (digits[6] == 5u) {
            int tail = 0;
            for (i = 7u; i < ndigits; i++) {
                if (digits[i] != 0) {
                    tail = 1;
                    break;
                }
            }
            round_up = tail || (head[5] & 1u) != 0;
        }
        if (round_up) {
            int carry = 1;
            int index = 5;
            while (index >= 0 && carry) {
                int sum = (int)head[index] + carry;
                head[index] = (uint8_t)(sum % 10);
                carry = sum / 10;
                index--;
            }
            if (carry) {
                head[0] = 1;
                head[1] = 0;
                head[2] = 0;
                head[3] = 0;
                head[4] = 0;
                head[5] = 0;
                exp10++;
            }
        }
    }
    if (negative) {
        append_char(out, cap, &len, '-');
    }
    if (exp10 < -4 || exp10 >= 6) {
        size_t last = 5;
        int exponent_value;
        char exponent_digits[8];
        size_t nexponent = 0;
        append_char(out, cap, &len, (char)('0' + head[0]));
        while (last > 0 && head[last] == 0) {
            last--;
        }
        if (last > 0) {
            append_char(out, cap, &len, '.');
            for (i = 1; i <= last; i++) {
                append_char(out, cap, &len, (char)('0' + head[i]));
            }
        }
        append_char(out, cap, &len, 'e');
        exponent_value = (int)exp10;
        append_char(out, cap, &len, exponent_value < 0 ? '-' : '+');
        if (exponent_value < 0) {
            exponent_value = -exponent_value;
        }
        do {
            exponent_digits[nexponent++] =
                (char)('0' + (exponent_value % 10));
            exponent_value /= 10;
        } while (exponent_value != 0 && nexponent < sizeof(exponent_digits));
        while (nexponent < 2u) {
            exponent_digits[nexponent++] = '0';
        }
        while (nexponent > 0) {
            append_char(out, cap, &len, exponent_digits[--nexponent]);
        }
    } else if (exp10 >= 0) {
        size_t integer_digits = (size_t)exp10 + 1u;
        size_t last = 5;
        if (integer_digits >= 6u) {
            for (i = 0; i < 6u; i++) {
                append_char(out, cap, &len, (char)('0' + head[i]));
            }
            for (i = 6u; i < integer_digits; i++) {
                append_char(out, cap, &len, '0');
            }
        } else {
            for (i = 0; i < integer_digits; i++) {
                append_char(out, cap, &len, (char)('0' + head[i]));
            }
            while (last >= integer_digits && head[last] == 0) {
                if (last == integer_digits) {
                    last = 0;
                    break;
                }
                last--;
            }
            if (last >= integer_digits) {
                append_char(out, cap, &len, '.');
                for (i = integer_digits; i <= last; i++) {
                    append_char(out, cap, &len, (char)('0' + head[i]));
                }
            }
        }
    } else {
        size_t last = 5;
        size_t zeros = (size_t)(-exp10 - 1);
        while (last > 0 && head[last] == 0) {
            last--;
        }
        append_char(out, cap, &len, '0');
        append_char(out, cap, &len, '.');
        for (i = 0; i < zeros; i++) {
            append_char(out, cap, &len, '0');
        }
        for (i = 0; i <= last; i++) {
            append_char(out, cap, &len, (char)('0' + head[i]));
        }
    }
    return finish(out, cap, len);
}

static size_t format_u64(uint64_t value, char *out) {
    char reversed[24];
    size_t n = 0;
    size_t i;
    if (value == 0) {
        out[0] = '0';
        return 1;
    }
    while (value != 0) {
        reversed[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    for (i = 0; i < n; i++) {
        out[i] = reversed[n - 1u - i];
    }
    return n;
}

int tc_format_scalar(unsigned W, const TcType *type, uint64_t bits, char *out,
                     size_t cap) {
    if (type == NULL || out == NULL || cap == 0) {
        return 0;
    }
    if (type->tag == TC_TY_BOOL) {
        const char *text = (bits & 0xffu) != 0 ? "true" : "false";
        size_t len = strlen(text);
        if (len >= cap) {
            return 0;
        }
        memcpy(out, text, len + 1u);
        return 1;
    }
    if (type->tag == TC_TY_F32) {
        return tc_format_float(32u, bits, out, cap);
    }
    if (type->tag == TC_TY_F64) {
        return tc_format_float(64u, bits, out, cap);
    }
    if (tc_type_is_integer(type)) {
        unsigned width = tc_sem_type_width(type, W);
        uint64_t mask = width >= 64u ? UINT64_MAX
                                     : ((UINT64_C(1) << width) - 1u);
        uint64_t value = bits & mask;
        char digits[24];
        size_t len;
        if (tc_type_is_signed_integer(type)) {
            uint64_t sign = UINT64_C(1) << (width - 1u);
            if ((value & sign) != 0) {
                uint64_t magnitude = (UINT64_C(0) - value) & mask;
                digits[0] = '-';
                len = 1u + format_u64(magnitude, digits + 1);
            } else {
                len = format_u64(value, digits);
            }
        } else {
            len = format_u64(value, digits);
        }
        if (len >= cap) {
            return 0;
        }
        memcpy(out, digits, len);
        out[len] = '\0';
        return 1;
    }
    return 0;
}
