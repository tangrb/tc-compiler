/* TC 0.0.46 exact decimal -> binary floating-point conversion. */
#include "frontend/num.h"

#include "runtime/rt_memory.h"

/* ------------------------------------------------------------------------- */
/* Minimal unsigned big integer, base 2^32, arena allocated.                 */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint32_t *limbs;
    size_t n;
    size_t cap;
    TcArena *arena;
} TcBig;

static int big_init(TcBig *a, TcArena *arena) {
    a->limbs = NULL;
    a->n = 0;
    a->cap = 0;
    a->arena = arena;
    return 0;
}

static int big_reserve(TcBig *a, size_t cap) {
    uint32_t *fresh;
    if (cap <= a->cap) {
        return 0;
    }
    fresh = (uint32_t *)tc_arena_alloc(a->arena, cap * sizeof(uint32_t));
    if (fresh == NULL) {
        return -1;
    }
    if (a->n != 0) {
        tc_rt_memcpy(fresh, a->limbs, a->n * sizeof(uint32_t));
    }
    a->limbs = fresh;
    a->cap = cap;
    return 0;
}

static int big_set_u64(TcBig *a, uint64_t value) {
    if (value == 0) {
        a->n = 0;
        return 0;
    }
    if (big_reserve(a, 2) != 0) {
        return -1;
    }
    a->limbs[0] = (uint32_t)(value & 0xffffffffu);
    a->limbs[1] = (uint32_t)(value >> 32);
    a->n = a->limbs[1] != 0 ? 2u : 1u;
    return 0;
}

static int big_copy(TcBig *dst, const TcBig *src) {
    if (src->n != 0) {
        if (big_reserve(dst, src->n) != 0) {
            return -1;
        }
        tc_rt_memcpy(dst->limbs, src->limbs, src->n * sizeof(uint32_t));
    }
    dst->n = src->n;
    return 0;
}

static int big_mul_small(TcBig *a, uint32_t factor) {
    uint64_t carry = 0;
    size_t i;
    if (a->n == 0) {
        return 0;
    }
    for (i = 0; i < a->n; i++) {
        uint64_t product = (uint64_t)a->limbs[i] * factor + carry;
        a->limbs[i] = (uint32_t)(product & 0xffffffffu);
        carry = product >> 32;
    }
    if (carry != 0) {
        if (big_reserve(a, a->n + 1) != 0) {
            return -1;
        }
        a->limbs[a->n] = (uint32_t)carry;
        a->n++;
    }
    return 0;
}

static int big_add_small(TcBig *a, uint32_t addend) {
    uint64_t carry = addend;
    size_t i = 0;
    while (carry != 0) {
        uint64_t sum;
        if (i == a->n) {
            if (big_reserve(a, a->n + 1) != 0) {
                return -1;
            }
            a->limbs[a->n] = 0;
            a->n++;
        }
        sum = (uint64_t)a->limbs[i] + carry;
        a->limbs[i] = (uint32_t)(sum & 0xffffffffu);
        carry = sum >> 32;
        i++;
    }
    return 0;
}

static int big_add(TcBig *a, const TcBig *b) {
    size_t i;
    uint64_t carry = 0;
    size_t n = a->n > b->n ? a->n : b->n;
    if (big_reserve(a, n + 1) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        uint64_t sum = carry;
        if (i < a->n) {
            sum += a->limbs[i];
        }
        if (i < b->n) {
            sum += b->limbs[i];
        }
        a->limbs[i] = (uint32_t)(sum & 0xffffffffu);
        carry = sum >> 32;
    }
    a->n = n;
    if (carry != 0) {
        a->limbs[a->n++] = (uint32_t)carry;
    }
    while (a->n > 0 && a->limbs[a->n - 1] == 0) {
        a->n--;
    }
    return 0;
}

static int big_cmp(const TcBig *a, const TcBig *b) {
    size_t i;
    if (a->n != b->n) {
        return a->n < b->n ? -1 : 1;
    }
    for (i = a->n; i > 0; i--) {
        if (a->limbs[i - 1] != b->limbs[i - 1]) {
            return a->limbs[i - 1] < b->limbs[i - 1] ? -1 : 1;
        }
    }
    return 0;
}

static unsigned clz32(uint32_t value) {
    unsigned count = 0;
    uint32_t mask = 0x80000000u;
    if (value == 0) {
        return 32u;
    }
    while ((value & mask) == 0) {
        count++;
        mask >>= 1;
    }
    return count;
}

static size_t big_bitlen(const TcBig *a) {
    if (a->n == 0) {
        return 0;
    }
    return (a->n - 1u) * 32u + (32u - clz32(a->limbs[a->n - 1u]));
}

static int big_shl(TcBig *a, size_t bits) {
    size_t words;
    unsigned offset;
    size_t new_n;
    size_t i;
    uint32_t *fresh;
    if (bits == 0 || a->n == 0) {
        return 0;
    }
    words = bits / 32u;
    offset = (unsigned)(bits % 32u);
    new_n = a->n + words + (offset != 0 ? 1u : 0u);
    fresh = (uint32_t *)tc_arena_alloc(a->arena, new_n * sizeof(uint32_t));
    if (fresh == NULL) {
        return -1;
    }
    tc_rt_memset(fresh, 0, new_n * sizeof(uint32_t));
    for (i = 0; i < a->n; i++) {
        uint64_t value = (uint64_t)a->limbs[i] << offset;
        fresh[i + words] |= (uint32_t)(value & 0xffffffffu);
        if (offset != 0) {
            fresh[i + words + 1] |= (uint32_t)(value >> 32);
        }
    }
    a->limbs = fresh;
    a->cap = new_n;
    a->n = new_n;
    while (a->n > 0 && a->limbs[a->n - 1] == 0) {
        a->n--;
    }
    return 0;
}

static int big_shr_copy(const TcBig *a, size_t bits, TcBig *out) {
    size_t words = bits / 32u;
    unsigned offset = (unsigned)(bits % 32u);
    size_t n;
    size_t i;
    if (words >= a->n) {
        out->n = 0;
        return 0;
    }
    n = a->n - words;
    if (big_reserve(out, n) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        uint64_t value = a->limbs[i + words];
        if (offset != 0) {
            value >>= offset;
            if (i + words + 1 < a->n) {
                value |= (uint64_t)a->limbs[i + words + 1] << (32 - offset);
            }
        }
        out->limbs[i] = (uint32_t)(value & 0xffffffffu);
    }
    out->n = n;
    while (out->n > 0 && out->limbs[out->n - 1] == 0) {
        out->n--;
    }
    return 0;
}

static int big_low_bits(const TcBig *a, size_t bits, TcBig *out) {
    size_t words;
    unsigned offset;
    size_t n;
    size_t i;
    if (bits == 0 || a->n == 0) {
        out->n = 0;
        return 0;
    }
    words = bits / 32u;
    offset = (unsigned)(bits % 32u);
    n = words < a->n ? words : a->n;
    if (big_reserve(out, (n + 1u)) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        out->limbs[i] = a->limbs[i];
    }
    out->n = n;
    if (offset != 0 && words < a->n) {
        out->limbs[n] = a->limbs[words] & ((1u << offset) - 1u);
        out->n = n + 1u;
    }
    while (out->n > 0 && out->limbs[out->n - 1] == 0) {
        out->n--;
    }
    return 0;
}

/* Compare a*2^ashift with b*2^bshift.  Returns -1, 0 or 1 in *out and 0 on
 * success, -1 on allocation failure. */
static int big_cmp_scaled(const TcBig *a, size_t ashift, const TcBig *b,
                          size_t bshift, int *out) {
    size_t abits = big_bitlen(a);
    size_t bbits = big_bitlen(b);
    size_t ab = abits == 0 ? 0 : abits + ashift;
    size_t bb = bbits == 0 ? 0 : bbits + bshift;
    TcBig shifted;
    int result;
    if (ab != bb) {
        *out = ab < bb ? -1 : 1;
        return 0;
    }
    if (ab == 0) {
        *out = 0;
        return 0;
    }
    big_init(&shifted, a->arena);
    if (ashift >= bshift) {
        if (big_copy(&shifted, a) != 0 || big_shl(&shifted, ashift - bshift) != 0) {
            return -1;
        }
        result = big_cmp(&shifted, b);
    } else {
        if (big_copy(&shifted, b) != 0 || big_shl(&shifted, bshift - ashift) != 0) {
            return -1;
        }
        result = big_cmp(a, &shifted);
    }
    *out = result;
    return 0;
}

/* Compare the low `bits` bits of a (the remainder of a >> bits) with
 * 2^(bits-1): returns -1 below half, 0 exactly half, 1 above half. */
static int low_bits_cmp_half(const TcBig *a, size_t bits) {
    size_t bit = bits - 1u;
    size_t limb = bit / 32u;
    unsigned offset = (unsigned)(bit % 32u);
    uint32_t lower_mask;
    size_t i;
    if (limb >= a->n) {
        return -1;
    }
    if (((a->limbs[limb] >> offset) & 1u) == 0) {
        return -1;
    }
    lower_mask = offset == 0 ? 0u : ((1u << offset) - 1u);
    if ((a->limbs[limb] & lower_mask) != 0) {
        return 1;
    }
    for (i = 0; i < limb; i++) {
        if (a->limbs[i] != 0) {
            return 1;
        }
    }
    return 0;
}

static int big_is_odd(const TcBig *a) {
    return a->n != 0 && (a->limbs[0] & 1u) != 0;
}

static uint32_t big_divmod_small(TcBig *a, uint32_t divisor) {
    uint64_t remainder = 0;
    size_t i;
    for (i = a->n; i > 0; i--) {
        uint64_t current = (remainder << 32) | a->limbs[i - 1u];
        a->limbs[i - 1u] = (uint32_t)(current / divisor);
        remainder = current % divisor;
    }
    while (a->n > 0 && a->limbs[a->n - 1] == 0) {
        a->n--;
    }
    return (uint32_t)remainder;
}

/* a = 5^k. */
static int big_pow5(TcBig *a, unsigned k) {
    unsigned i;
    if (big_set_u64(a, 1) != 0) {
        return -1;
    }
    for (i = 0; i < k; i++) {
        if (big_mul_small(a, 5u) != 0) {
            return -1;
        }
    }
    return 0;
}

/* q = n / 5^k, r = n mod 5^k.  q and r must be initialized. */
static int big_divmod_pow5(const TcBig *n, unsigned k, TcBig *q, TcBig *r) {
    TcBig work;
    uint32_t *digits;
    unsigned i;
    big_init(&work, n->arena);
    if (big_copy(&work, n) != 0) {
        return -1;
    }
    if (k == 0) {
        if (big_copy(q, &work) != 0 || big_set_u64(r, 0) != 0) {
            return -1;
        }
        return 0;
    }
    digits = (uint32_t *)tc_arena_alloc(n->arena, k * sizeof(uint32_t));
    if (digits == NULL) {
        return -1;
    }
    for (i = 0; i < k; i++) {
        digits[i] = big_divmod_small(&work, 5u);
    }
    if (big_copy(q, &work) != 0 || big_set_u64(r, 0) != 0) {
        return -1;
    }
    for (i = k; i > 0; i--) {
        if (big_mul_small(r, 5u) != 0 || big_add_small(r, digits[i - 1u]) != 0) {
            return -1;
        }
    }
    return 0;
}

static uint64_t big_to_u64(const TcBig *a) {
    uint64_t value = 0;
    if (a->n > 0) {
        value = a->limbs[0];
    }
    if (a->n > 1) {
        value |= (uint64_t)a->limbs[1] << 32;
    }
    return value;
}

static unsigned bitlen64(uint64_t value) {
    unsigned count = 0;
    while (value != 0) {
        count++;
        value >>= 1;
    }
    return count;
}

/* ------------------------------------------------------------------------- */
/* Conversion.                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    unsigned p;
    int emax;
    int emin;
    unsigned width;
    unsigned exp_bits;
    int bias;
} TcFloatFormat;

static int format_init(unsigned width, TcFloatFormat *fmt) {
    if (width == 32) {
        fmt->p = 24;
        fmt->emax = 127;
        fmt->emin = -126;
        fmt->width = 32;
        fmt->exp_bits = 8;
        fmt->bias = 127;
        return 0;
    }
    if (width == 64) {
        fmt->p = 53;
        fmt->emax = 1023;
        fmt->emin = -1022;
        fmt->width = 64;
        fmt->exp_bits = 11;
        fmt->bias = 1023;
        return 0;
    }
    return -1;
}

/* Early decimal-exponent bounds that are certainly out of range.  The values
 * are derived from the binary format parameters; the exact path handles the
 * boundary decimal exponents. */
static int early_range(int dec_exp, const TcFloatFormat *fmt,
                       TcFloatRange *range) {
    /* max finite value: (2 - 2^(1-p)) * 2^emax
     * its decimal exponent is floor(log10(max_finite)).  A value whose
     * decimal exponent exceeds that by one is certainly > max finite. */
    int max_dec;
    int min_dec;
    if (fmt->width == 64) {
        max_dec = 308;
        min_dec = -324;
    } else {
        max_dec = 38;
        min_dec = -46;
    }
    if (dec_exp > max_dec) {
        *range = TC_FLOAT_ROUNDED_TO_INF;
        return 1;
    }
    if (dec_exp < min_dec) {
        *range = TC_FLOAT_ROUNDED_TO_ZERO;
        return 1;
    }
    *range = TC_FLOAT_OK;
    return 0;
}

static int decimal_to_float(const char *digits, size_t ndigits, int exp10,
                            int negative, const TcFloatFormat *fmt,
                            uint64_t *out_bits, TcFloatRange *out_range) {
    TcArena arena;
    TcBig d;
    TcBig t;
    TcBig q;
    TcBig r;
    TcBig n;
    TcBig q5;
    TcBig rem2;
    size_t first = 0;
    size_t last;
    size_t significant;
    int dec_exp;
    int e10;
    int e_value;
    int qexp;
    int round_extra = -1;
    uint64_t qv;
    uint64_t sign_bit;
    int i;
    uint64_t bits = 0;

    *out_range = TC_FLOAT_OK;
    *out_bits = 0;
    sign_bit = (uint64_t)1u << (fmt->width - 1u);

    while (first < ndigits && digits[first] == '0') {
        first++;
    }
    last = ndigits;
    while (last > first && digits[last - 1u] == '0') {
        last--;
        exp10++;
    }
    if (first == last) {
        *out_bits = negative ? sign_bit : 0;
        return 0;
    }
    for (i = (int)first; i < (int)last; i++) {
        if (digits[i] < '0' || digits[i] > '9') {
            return -1;
        }
    }
    significant = last - first;
    dec_exp = (int)significant - 1 + exp10;
    if (early_range(dec_exp, fmt, out_range)) {
        /* out_bits stays zero; caller treats range status as a LEX error. */
        if (*out_range == TC_FLOAT_ROUNDED_TO_INF) {
            *out_bits = (negative ? sign_bit : 0) |
                        (((uint64_t)((1u << fmt->exp_bits) - 1u))
                         << (fmt->p - 1u));
        } else {
            *out_bits = negative ? sign_bit : 0;
        }
        return 0;
    }

    tc_arena_init(&arena, NULL);
    big_init(&d, &arena);
    big_init(&t, &arena);
    big_init(&q, &arena);
    big_init(&r, &arena);
    big_init(&n, &arena);
    big_init(&q5, &arena);
    big_init(&rem2, &arena);

    for (i = (int)first; i < (int)last; i++) {
        if (big_mul_small(&d, 10u) != 0 ||
            big_add_small(&d, (uint32_t)(digits[i] - '0')) != 0) {
            tc_arena_destroy(&arena);
            return -1;
        }
    }

    e10 = exp10;
    if (e10 >= 0) {
        TcBig d5;
        size_t bitlen;
        big_init(&d5, &arena);
        if (big_copy(&d5, &d) != 0) {
            tc_arena_destroy(&arena);
            return -1;
        }
        for (i = 0; i < e10; i++) {
            if (big_mul_small(&d5, 5u) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
        }
        bitlen = big_bitlen(&d5);
        e_value = e10 + (int)bitlen - 1;
        qexp = e_value - (int)(fmt->p - 1u);
        if (qexp < fmt->emin - (int)(fmt->p - 1u)) {
            qexp = fmt->emin - (int)(fmt->p - 1u);
        }
        if (e10 - qexp >= 0) {
            if (big_copy(&q, &d5) != 0 ||
                big_shl(&q, (size_t)(e10 - qexp)) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
        } else {
            size_t a = (size_t)(qexp - e10);
            if (big_shr_copy(&d5, a, &q) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            round_extra = low_bits_cmp_half(&d5, a);
        }
    } else {
        unsigned k = (unsigned)(-e10);
        size_t bitlen_d;
        size_t bitlen_t;
        int e0;
        int m;
        if (big_pow5(&t, k) != 0) {
            tc_arena_destroy(&arena);
            return -1;
        }
        bitlen_d = big_bitlen(&d);
        bitlen_t = big_bitlen(&t);
        e0 = (int)bitlen_d - (int)bitlen_t - (int)k;
        m = e0 + (int)k;
        if (m >= 0) {
            int cmp = 0;
            if (big_cmp_scaled(&d, 0, &t, (size_t)m, &cmp) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            e_value = cmp >= 0 ? e0 : e0 - 1;
        } else {
            int cmp = 0;
            if (big_cmp_scaled(&d, (size_t)(-m), &t, 0, &cmp) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            e_value = cmp >= 0 ? e0 : e0 - 1;
        }
        qexp = e_value - (int)(fmt->p - 1u);
        if (qexp < fmt->emin - (int)(fmt->p - 1u)) {
            qexp = fmt->emin - (int)(fmt->p - 1u);
        }
        int s2 = -(int)k - qexp;
        if (s2 >= 0) {
            if (big_copy(&n, &d) != 0 || big_shl(&n, (size_t)s2) != 0 ||
                big_divmod_pow5(&n, k, &q, &r) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            if (big_shl(&r, 1) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            round_extra = big_cmp(&r, &t);
        } else {
            size_t a = (size_t)(-s2);
            if (big_divmod_pow5(&d, k, &q5, &r) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            if (big_shr_copy(&q5, a, &q) != 0 ||
                big_low_bits(&q5, a, &rem2) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            for (i = 0; i < (int)k; i++) {
                if (big_mul_small(&rem2, 5u) != 0) {
                    tc_arena_destroy(&arena);
                    return -1;
                }
            }
            if (big_add(&rem2, &r) != 0 || big_shl(&rem2, 1) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            /* compare 2*(rem2*5^k + r) with 5^k * 2^a */
            if (big_shl(&t, a) != 0) {
                tc_arena_destroy(&arena);
                return -1;
            }
            round_extra = big_cmp(&rem2, &t);
        }
    }

    if (round_extra > 0 || (round_extra == 0 && big_is_odd(&q))) {
        if (big_add_small(&q, 1u) != 0) {
            tc_arena_destroy(&arena);
            return -1;
        }
    }
    if (big_bitlen(&q) > 64u) {
        tc_arena_destroy(&arena);
        return -1;
    }
    qv = big_to_u64(&q);
    if (qv == 0) {
        *out_bits = negative ? sign_bit : 0;
        *out_range = TC_FLOAT_ROUNDED_TO_ZERO;
        tc_arena_destroy(&arena);
        return 0;
    }

    {
        unsigned bl = bitlen64(qv);
        int result_exp = qexp + (int)bl - 1;
        if (bl > fmt->p) {
            /* Rounding carried into a new binade.  result_exp is the exponent
             * of the leading bit and must not change when the significand is
             * shifted down to p bits. */
            qv >>= 1;
        }
        if (result_exp > fmt->emax) {
            *out_bits = (negative ? sign_bit : 0) |
                        (((uint64_t)((1u << fmt->exp_bits) - 1u))
                         << (fmt->p - 1u));
            *out_range = TC_FLOAT_ROUNDED_TO_INF;
            tc_arena_destroy(&arena);
            return 0;
        }
        if (result_exp >= fmt->emin) {
            uint64_t fraction_mask =
                ((uint64_t)1u << (fmt->p - 1u)) - 1u;
            uint64_t exponent_field =
                (uint64_t)(result_exp + fmt->bias) << (fmt->p - 1u);
            uint64_t fraction = qv & fraction_mask;
            bits = exponent_field | fraction;
        } else {
            bits = qv;
        }
        if (negative) {
            bits |= sign_bit;
        }
    }
    *out_bits = bits;
    *out_range = TC_FLOAT_OK;
    tc_arena_destroy(&arena);
    return 0;
}

int tc_decimal_to_float(const char *digits, size_t ndigits, int exp10,
                        int negative, unsigned bit_width, uint64_t *out_bits,
                        TcFloatRange *out_range) {
    TcFloatFormat fmt;
    if (digits == NULL || out_bits == NULL || out_range == NULL) {
        return -1;
    }
    if (format_init(bit_width, &fmt) != 0) {
        return -1;
    }
    return decimal_to_float(digits, ndigits, exp10, negative, &fmt, out_bits,
                            out_range);
}
