/*
 * tc_io.c — TC 统一 I/O 实现
 *
 * 合并 VM 与 AOT 的 read/write 实现；二者只委托本模块。
 *
 * write 函数使用 FILE *out 参数支持灵活输出，并检查 I/O 错误；
 * read 函数从 stdin 解析十进制整数或 bool 文本，含范围检查。
 *
 * write/writeln：先在内存中生成完整字节串，再一次 fwrite 提交到目标流
 * （语言标准 §10 / 编译器标准 §10.5 原子逻辑提交）。
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tc_io.h"

#include "tc_semantics.h"

#include <errno.h>
#ifdef TC_HAVE_FENV
#include <fenv.h>
#endif
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef _WIN32
static FILE *tc_io_open_stage_file(char *path_buf, size_t path_cap) {
    const char *dir;
    int n;

    dir = getenv("TEMP");
    if (dir == NULL || dir[0] == '\0') {
        dir = getenv("TMP");
    }
    if (dir == NULL || dir[0] == '\0') {
        dir = ".";
    }
    n = snprintf(path_buf, path_cap, "%s\\tc-io-stage-%p.tmp", dir, (void *)path_buf);
    if (n < 0 || (size_t)n >= path_cap) {
        return NULL;
    }
    return fopen(path_buf, "w+b");
}

static void tc_io_ensure_binary_stdio(void) {
    static int done = 0;

    if (done) {
        return;
    }
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);
    done = 1;
}
#endif

void tc_io_init(void) {
#ifdef _WIN32
    tc_io_ensure_binary_stdio();
#endif
}

/* ------------------------------------------------------------------ */
/*  格式化输出                                                          */
/* ------------------------------------------------------------------ */

static int tc_io_format_accepts_type(TcTypeTag type, TcFormatSpec fmt) {
    switch (fmt) {
    case TC_FMT_D:
    case TC_FMT_I:
        return tc_type_is_signed(type);
    case TC_FMT_U:
        return tc_type_is_integer(type) && !tc_type_is_signed(type);
    case TC_FMT_X:
    case TC_FMT_XU:
    case TC_FMT_O:
    case TC_FMT_B:
        return tc_type_is_integer(type);
    case TC_FMT_T:
        return tc_type_is_bool(type);
    case TC_FMT_F:
    case TC_FMT_E:
    case TC_FMT_EU:
    case TC_FMT_G:
    case TC_FMT_GU:
        return tc_type_is_float(type);
    case TC_FMT_NONE:
        return 0;
    }
    return 0;
}

static int tc_io_write_repeat(FILE *out, char ch, int count) {
    int i = 0;

    for (i = 0; i < count; i++) {
        if (fputc(ch, out) == EOF) {
            return -1;
        }
    }
    return 0;
}

static int tc_io_write_aligned(FILE *out, const char *prefix, const char *digits,
                               const TcFormatFullSpec *fmt, int zero_pad) {
    int prefix_len = (int)strlen(prefix);
    int digits_len = (int)strlen(digits);
    int content = prefix_len + digits_len;
    int pad = (fmt->width > content) ? fmt->width - content : 0;

    if (fmt->flag_minus) {
        if (fputs(prefix, out) == EOF || fputs(digits, out) == EOF) {
            return -1;
        }
        return tc_io_write_repeat(out, ' ', pad);
    }
    if (zero_pad && pad > 0) {
        if (fputs(prefix, out) == EOF) {
            return -1;
        }
        if (tc_io_write_repeat(out, '0', pad) != 0) {
            return -1;
        }
        return fputs(digits, out) == EOF ? -1 : 0;
    }
    if (tc_io_write_repeat(out, ' ', pad) != 0) {
        return -1;
    }
    if (fputs(prefix, out) == EOF || fputs(digits, out) == EOF) {
        return -1;
    }
    return 0;
}

static void tc_io_u64_to_base(uint64_t value, int base, int uppercase, char *out, size_t out_size) {
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[80];
    int n = 0;
    int i = 0;

    if (out_size == 0) {
        return;
    }
    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = alphabet[value % (unsigned)base];
        value /= (unsigned)base;
    }
    if ((size_t)n + 1U > out_size) {
        n = (int)out_size - 1;
    }
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
}

static void tc_io_strip_leading_zeros(char *digits) {
    size_t i = 0;

    while (digits[i] == '0' && digits[i + 1] != '\0') {
        i++;
    }
    if (i > 0) {
        memmove(digits, digits + i, strlen(digits + i) + 1U);
    }
}

static int tc_io_prec_pad(char *digits, size_t cap, int min_digits) {
    int len = (int)strlen(digits);
    int need = min_digits - len;

    if (need <= 0) {
        return 0;
    }
    if ((size_t)min_digits + 1U > cap) {
        return -1;
    }
    memmove(digits + need, digits, (size_t)len + 1U);
    memset(digits, '0', (size_t)need);
    return 0;
}

static int tc_io_full_digit_width(TcTypeTag type, TcFormatSpec spec) {
    int bits = tc_type_bit_width(type);

    if (spec == TC_FMT_X || spec == TC_FMT_XU) {
        return (bits + 3) / 4;
    }
    if (spec == TC_FMT_O) {
        return (bits + 2) / 3;
    }
    if (spec == TC_FMT_B) {
        return bits;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  A2：自实现浮点十进制输出（§10.4，位模式精确，roundTiesToEven）        */
/* ------------------------------------------------------------------ */

/* 十进制大整数（limbs 基 1e9，limbs[0] 最低位）。96 limbs ≈ 864 十进制位，
 * 覆盖 f64 最小次正规（2^-1074 ≈ 4.94e-324）的精确展开（约 751 位）。 */
#define TC_FP_BIG_LIMBS 96

static int tc_fp_big_from_u64(uint32_t *limbs, uint64_t v) {
    int n = 0;

    while (v > 0 && n < TC_FP_BIG_LIMBS) {
        limbs[n++] = (uint32_t)(v % 1000000000u);
        v /= 1000000000u;
    }
    if (n == 0) {
        limbs[n++] = 0;
    }
    return n;
}

static void tc_fp_big_mul_small(uint32_t *limbs, int *nlimbs, uint32_t m) {
    uint64_t carry = 0;
    int i = 0;

    for (i = 0; i < *nlimbs; i++) {
        uint64_t prod = (uint64_t)limbs[i] * m + carry;
        limbs[i] = (uint32_t)(prod % 1000000000u);
        carry = prod / 1000000000u;
    }
    while (carry > 0 && *nlimbs < TC_FP_BIG_LIMBS) {
        limbs[(*nlimbs)++] = (uint32_t)(carry % 1000000000u);
        carry /= 1000000000u;
    }
}

/* limbs → 十进制数字串（MSB first，无前导零） */
static int tc_fp_big_to_digits(const uint32_t *limbs, int nlimbs, uint8_t *digits) {
    int n = 0;
    int i = 0;

    for (i = nlimbs - 1; i >= 0; i--) {
        char buf[12];
        int len = 0;
        int j = 0;

        tc_io_u64_to_base(limbs[i], 10, 0, buf, sizeof(buf));
        len = (int)strlen(buf);
        if (i == nlimbs - 1) {
            for (j = 0; j < len; j++) {
                digits[n++] = (uint8_t)(buf[j] - '0');
            }
        } else {
            for (j = 0; j < 9 - len; j++) {
                digits[n++] = 0;
            }
            for (j = 0; j < len; j++) {
                digits[n++] = (uint8_t)(buf[j] - '0');
            }
        }
    }
    return n;
}

/* 精确十进制：value = mantissa × 2^e2（IEEE 位模式展开） */
typedef struct {
    uint8_t digits[800];
    int ndigits;
    int exp10; /* value = digits[0].digits[1]… × 10^exp10；零值 digits={0}, exp10=0 */
} TcFpExact;

static void tc_fp_exact_from_binary(TcFpExact *d, uint64_t mantissa, int e2) {
    uint32_t limbs[TC_FP_BIG_LIMBS];
    int nlimbs = 0;
    int i = 0;

    if (mantissa == 0) {
        d->digits[0] = 0;
        d->ndigits = 1;
        d->exp10 = 0;
        return;
    }
    nlimbs = tc_fp_big_from_u64(limbs, mantissa);
    if (e2 >= 0) {
        for (i = 0; i < e2; i++) {
            tc_fp_big_mul_small(limbs, &nlimbs, 2);
        }
        d->ndigits = tc_fp_big_to_digits(limbs, nlimbs, d->digits);
        d->exp10 = d->ndigits - 1;
    } else {
        int k = -e2;

        for (i = 0; i < k; i++) {
            tc_fp_big_mul_small(limbs, &nlimbs, 5);
        }
        d->ndigits = tc_fp_big_to_digits(limbs, nlimbs, d->digits);
        d->exp10 = d->ndigits - 1 - k;
    }
}

/* roundTiesToEven：保留前 keep 位有效数字；keep ≤ 0 时全部舍弃，进位落到
 * 10^carry0_exp10（%f 的 keep≤0 情形，如 9e-7 舍到 6 位小数 → 1e-6）。 */
static void tc_fp_round(TcFpExact *d, int keep, int carry0_exp10) {
    int up = 0;
    int j = 0;

    if (keep >= d->ndigits) {
        return;
    }
    if (keep <= 0) {
        uint8_t r = d->digits[0];

        if (r > 5) {
            up = 1;
        } else if (r == 5) {
            int has = 0;
            for (j = 1; j < d->ndigits; j++) {
                if (d->digits[j] != 0) {
                    has = 1;
                    break;
                }
            }
            if (has) {
                up = 1;
            }
        }
        if (up) {
            d->digits[0] = 1;
            d->ndigits = 1;
            d->exp10 = carry0_exp10;
        } else {
            d->digits[0] = 0;
            d->ndigits = 1;
            d->exp10 = 0;
        }
        return;
    }
    {
        uint8_t r = d->digits[keep];

        if (r > 5) {
            up = 1;
        } else if (r == 5) {
            int has = 0;
            for (j = keep + 1; j < d->ndigits; j++) {
                if (d->digits[j] != 0) {
                    has = 1;
                    break;
                }
            }
            if (has) {
                up = 1;
            } else if ((d->digits[keep - 1] & 1)) {
                up = 1;
            }
        }
        d->ndigits = keep;
        if (up) {
            int p = keep - 1;

            while (p >= 0 && d->digits[p] == 9) {
                d->digits[p] = 0;
                p--;
            }
            if (p < 0) {
                memmove(d->digits + 1, d->digits, (size_t)keep);
                d->digits[0] = 1;
                d->ndigits = keep + 1;
                d->exp10++;
            } else {
                d->digits[p]++;
            }
        }
    }
}

static uint8_t tc_fp_digit_at(const TcFpExact *d, int idx) {
    if (idx >= 0 && idx < d->ndigits) {
        return d->digits[idx];
    }
    return 0;
}

static void tc_fp_strip_zeros(TcFpExact *d) {
    while (d->ndigits > 1 && d->digits[d->ndigits - 1] == 0) {
        d->ndigits--;
    }
}

static void tc_fp_append_exp(char *out, size_t *n, int exp, int uppercase) {
    char tmp[8];
    int t = 0;
    int a = exp < 0 ? -exp : exp;

    out[(*n)++] = uppercase ? 'E' : 'e';
    out[(*n)++] = exp < 0 ? '-' : '+';
    do {
        tmp[t++] = (char)('0' + (a % 10));
        a /= 10;
    } while (a > 0);
    while (t < 2) {
        tmp[t++] = '0';
    }
    while (t > 0) {
        out[(*n)++] = tmp[--t];
    }
}

/* %e/%E：digits[0].digits[1..p] e±exp（指数至少两位；§10.4；精度 0 且非 # 无小数点） */
static void tc_fp_render_exp(const TcFpExact *d, int precision, int uppercase, int hash, char *out,
                             size_t *n) {
    int i = 0;

    out[(*n)++] = (char)('0' + d->digits[0]);
    if (precision > 0 || hash) {
        out[(*n)++] = '.';
        for (i = 1; i <= precision; i++) {
            out[(*n)++] = (char)('0' + tc_fp_digit_at(d, i));
        }
    }
    tc_fp_append_exp(out, n, d->exp10, uppercase);
}

/* %f：整数位 + precision 位小数（§10.4；精度 0 且非 # 无小数点） */
static void tc_fp_render_fixed(const TcFpExact *d, int precision, int hash, char *out, size_t *n) {
    int i = 0;

    if (d->exp10 >= 0) {
        for (i = 0; i <= d->exp10; i++) {
            out[(*n)++] = (char)('0' + tc_fp_digit_at(d, i));
        }
    } else {
        out[(*n)++] = '0';
    }
    if (precision > 0 || hash) {
        out[(*n)++] = '.';
        for (i = d->exp10 + 1; i <= d->exp10 + precision; i++) {
            out[(*n)++] = (char)('0' + tc_fp_digit_at(d, i));
        }
    }
}

/* %g/%G：round/strip 已在调用前完成；按 e 阈值判形（§10.4） */
static void tc_fp_render_general(const TcFpExact *d, int precision, int uppercase, int hash,
                                 char *out, size_t *n) {
    int i = 0;
    int use_exp = (d->exp10 < -4 || d->exp10 >= precision);

    if (use_exp) {
        out[(*n)++] = (char)('0' + d->digits[0]);
        if (hash) {
            int f = precision - 1;

            out[(*n)++] = '.';
            for (i = 1; i <= f; i++) {
                out[(*n)++] = (char)('0' + tc_fp_digit_at(d, i));
            }
        } else if (d->ndigits > 1) {
            out[(*n)++] = '.';
            for (i = 1; i < d->ndigits; i++) {
                out[(*n)++] = (char)('0' + d->digits[i]);
            }
        }
        tc_fp_append_exp(out, n, d->exp10, uppercase);
        return;
    }
    if (d->exp10 >= 0) {
        for (i = 0; i <= d->exp10; i++) {
            out[(*n)++] = (char)('0' + tc_fp_digit_at(d, i));
        }
    } else {
        out[(*n)++] = '0';
    }
    if (hash) {
        int frac = precision - (d->exp10 + 1);

        out[(*n)++] = '.';
        if (frac > 0) {
            for (i = d->exp10 + 1; i <= d->exp10 + frac; i++) {
                out[(*n)++] = (char)('0' + tc_fp_digit_at(d, i));
            }
        }
    } else if (d->exp10 + 1 < d->ndigits) {
        out[(*n)++] = '.';
        for (i = d->exp10 + 1; i < d->ndigits; i++) {
            out[(*n)++] = (char)('0' + tc_fp_digit_at(d, i));
        }
    }
}

static int tc_io_write_float_core(TcTypeTag type, TcFormatSpec spec, const TcValue *value,
                                  FILE *out) {
    TcFormatFullSpec fmt = tc_format_spec_make(spec);

    return tc_io_write_formatted(type, fmt, value, out);
}

/* IEEE 位模式分解：特殊值/符号/（mantissa, e2）——value = mantissa × 2^e2 */
static void tc_io_fp_unpack(TcTypeTag type, uint64_t bits, uint64_t *mantissa, int *e2,
                            int *is_nan, int *is_inf, int *negative) {
    if (type == TC_FLOAT32) {
        uint32_t exp = (uint32_t)((bits >> 23) & 0xffu);
        uint32_t frac = (uint32_t)(bits & 0x7fffffu);

        *negative = (int)((bits >> 31) & 1u);
        if (exp == 0xffu) {
            *is_nan = frac != 0;
            *is_inf = frac == 0;
            *mantissa = 0;
            *e2 = 0;
            return;
        }
        *is_nan = 0;
        *is_inf = 0;
        if (exp == 0) {
            *mantissa = frac;
            *e2 = -149; /* 次正规：frac × 2^-149 */
        } else {
            *mantissa = (1u << 23) | frac;
            *e2 = (int)exp - 150; /* 正规：(1|frac) × 2^(exp-127-23) */
        }
    } else {
        uint64_t exp = (bits >> 52) & 0x7ffu;
        uint64_t frac = bits & ((1ull << 52) - 1u);

        *negative = (int)((bits >> 63) & 1u);
        if (exp == 0x7ffu) {
            *is_nan = frac != 0;
            *is_inf = frac == 0;
            *mantissa = 0;
            *e2 = 0;
            return;
        }
        *is_nan = 0;
        *is_inf = 0;
        if (exp == 0) {
            *mantissa = frac;
            *e2 = -1074; /* 次正规：frac × 2^-1074 */
        } else {
            *mantissa = (1ull << 52) | frac;
            *e2 = (int)exp - 1075; /* 正规：(1|frac) × 2^(exp-1023-52) */
        }
    }
}

static int tc_io_format_float(TcTypeTag type, const TcFormatFullSpec *fmt, const TcValue *value,
                              FILE *out) {
    uint64_t mantissa = 0;
    int e2 = 0;
    int is_nan = 0;
    int is_inf = 0;
    int negative = 0;
    int uppercase = (fmt->spec == TC_FMT_EU || fmt->spec == TC_FMT_GU);
    char prefix[3];
    char *body = NULL;
    int precision = 6;
    int rc = 0;

    prefix[0] = '\0';
    tc_io_fp_unpack(type, value->bits, &mantissa, &e2, &is_nan, &is_inf, &negative);
    if (is_nan) {
        /* §10.4：NaN 符号位不外露，仅 + 标志添加 + */
        if (fmt->flag_plus) {
            prefix[0] = '+';
            prefix[1] = '\0';
        }
        return tc_io_write_aligned(out, prefix, uppercase ? "NAN" : "nan", fmt, 0);
    }
    if (is_inf) {
        if (negative) {
            prefix[0] = '-';
            prefix[1] = '\0';
        } else if (fmt->flag_plus) {
            prefix[0] = '+';
            prefix[1] = '\0';
        }
        return tc_io_write_aligned(out, prefix, uppercase ? "INF" : "inf", fmt, 0);
    }
    if (negative) {
        prefix[0] = '-';
        prefix[1] = '\0';
    } else if (fmt->flag_plus) {
        prefix[0] = '+';
        prefix[1] = '\0';
    }

    if (fmt->precision_set) {
        precision = fmt->precision;
        if ((fmt->spec == TC_FMT_G || fmt->spec == TC_FMT_GU) && precision == 0) {
            precision = 1;
        }
    }

    {
        TcFpExact d;
        size_t n = 0;
        size_t body_cap = 0;

        tc_fp_exact_from_binary(&d, mantissa, e2);
        /* 缓冲：整数位（f64 ≤ 310）+ 精度（≤ 65535）+ 符号/点/指数 */
        body_cap = 340u + (size_t)precision + 16u;
        body = (char *)malloc(body_cap);
        if (!body) {
            return -1;
        }
        switch (fmt->spec) {
        case TC_FMT_E:
        case TC_FMT_EU:
            tc_fp_round(&d, precision + 1, 0);
            tc_fp_render_exp(&d, precision, uppercase, fmt->flag_hash, body, &n);
            break;
        case TC_FMT_F:
            tc_fp_round(&d, d.exp10 + precision + 1, -precision);
            tc_fp_render_fixed(&d, precision, fmt->flag_hash, body, &n);
            break;
        case TC_FMT_G:
        case TC_FMT_GU:
            tc_fp_round(&d, precision, 0);
            if (!fmt->flag_hash) {
                tc_fp_strip_zeros(&d);
            }
            tc_fp_render_general(&d, precision, uppercase, fmt->flag_hash, body, &n);
            break;
        default:
            free(body);
            return -1;
        }
        body[n] = '\0';
        rc = tc_io_write_aligned(out, prefix, body, fmt, fmt->flag_zero && !fmt->flag_minus);
        free(body);
        return rc;
    }
}

int tc_io_write_formatted(TcTypeTag type, TcFormatFullSpec fmt, const TcValue *value, FILE *out) {
    int n = 0;
    uint64_t mask = 0;
    uint64_t uval = 0;
    int64_t sval = 0;
    int negative = 0;
    char digits[80];
    char prefix[8];
    int min_digits = 1;
    int empty_ok = 0;
    int int_zero_pad = 0;

    if (!value || !out || !value->type || value->type->tag != type ||
        !tc_io_format_accepts_type(type, fmt.spec)) {
        return -1;
    }
    if (fmt.spec == TC_FMT_F || fmt.spec == TC_FMT_E || fmt.spec == TC_FMT_EU ||
        fmt.spec == TC_FMT_G || fmt.spec == TC_FMT_GU) {
        return tc_io_format_float(type, &fmt, value, out);
    }

    n = tc_type_bit_width(type);
    mask = tc_mask_bits(n);
    uval = tc_value_to_unsigned(type, value->bits) & mask;
    prefix[0] = '\0';
    digits[0] = '\0';

    if (fmt.spec == TC_FMT_T) {
        return tc_io_write_aligned(out, "", value->bits != 0 ? "true" : "false", &fmt, 0);
    }

    if (fmt.spec == TC_FMT_D || fmt.spec == TC_FMT_I) {
        sval = tc_bits_to_signed(type, value->bits);
        negative = sval < 0;
        if (sval == INT64_MIN) {
            (void)strcpy(digits, "9223372036854775808");
        } else {
            uint64_t mag = negative ? (uint64_t)(-sval) : (uint64_t)sval;
            tc_io_u64_to_base(mag, 10, 0, digits, sizeof(digits));
        }
        if (negative) {
            prefix[0] = '-';
            prefix[1] = '\0';
        } else if (fmt.flag_plus) {
            prefix[0] = '+';
            prefix[1] = '\0';
        }
    } else if (fmt.spec == TC_FMT_U) {
        tc_io_u64_to_base(uval, 10, 0, digits, sizeof(digits));
    } else if (fmt.spec == TC_FMT_X || fmt.spec == TC_FMT_XU) {
        tc_io_u64_to_base(uval, 16, fmt.spec == TC_FMT_XU, digits, sizeof(digits));
        if (!(tc_type_is_signed(type) && tc_bits_to_signed(type, value->bits) < 0)) {
            tc_io_strip_leading_zeros(digits);
        } else if (tc_io_prec_pad(digits, sizeof(digits),
                                  tc_io_full_digit_width(type, fmt.spec)) != 0) {
            return -1;
        }
        if (fmt.flag_hash && uval != 0) {
            prefix[0] = '0';
            prefix[1] = (char)(fmt.spec == TC_FMT_XU ? 'X' : 'x');
            prefix[2] = '\0';
        }
    } else if (fmt.spec == TC_FMT_O) {
        tc_io_u64_to_base(uval, 8, 0, digits, sizeof(digits));
        if (!(tc_type_is_signed(type) && tc_bits_to_signed(type, value->bits) < 0)) {
            tc_io_strip_leading_zeros(digits);
        } else if (tc_io_prec_pad(digits, sizeof(digits),
                                  tc_io_full_digit_width(type, fmt.spec)) != 0) {
            return -1;
        }
    } else if (fmt.spec == TC_FMT_B) {
        tc_io_u64_to_base(uval, 2, 0, digits, sizeof(digits));
        if (!(tc_type_is_signed(type) && tc_bits_to_signed(type, value->bits) < 0)) {
            tc_io_strip_leading_zeros(digits);
        } else if (tc_io_prec_pad(digits, sizeof(digits),
                                  tc_io_full_digit_width(type, fmt.spec)) != 0) {
            return -1;
        }
        if (fmt.flag_hash && uval != 0) {
            prefix[0] = '0';
            prefix[1] = 'b';
            prefix[2] = '\0';
        }
    } else {
        return -1;
    }

    empty_ok = fmt.precision_set && fmt.precision == 0 && uval == 0 &&
               !(fmt.spec == TC_FMT_D || fmt.spec == TC_FMT_I);
    if (fmt.spec == TC_FMT_D || fmt.spec == TC_FMT_I) {
        empty_ok = fmt.precision_set && fmt.precision == 0 && sval == 0;
    }
    if (empty_ok) {
        if (fmt.spec == TC_FMT_O && fmt.flag_hash) {
            digits[0] = '0';
            digits[1] = '\0';
        } else {
            digits[0] = '\0';
        }
    }

    if (fmt.precision_set) {
        min_digits = fmt.precision;
        if (digits[0] != '\0' && tc_io_prec_pad(digits, sizeof(digits), min_digits) != 0) {
            return -1;
        }
        if (digits[0] == '\0' && min_digits > 0 &&
            tc_io_prec_pad(digits, sizeof(digits), min_digits) != 0) {
            return -1;
        }
    }

    if (fmt.spec == TC_FMT_O && fmt.flag_hash && digits[0] != '0') {
        if (tc_io_prec_pad(digits, sizeof(digits), (int)strlen(digits) + 1) != 0) {
            return -1;
        }
        digits[0] = '0';
    }

    int_zero_pad = fmt.flag_zero && !fmt.flag_minus && !fmt.precision_set;
    return tc_io_write_aligned(out, prefix, digits, &fmt, int_zero_pad);
}

static int tc_io_render_value(const TcValue *value, TcFormatFullSpec fmt, int newline, FILE *out) {
    if (fmt.spec != TC_FMT_NONE) {
        if (tc_io_write_formatted(value->type->tag, fmt, value, out) != 0) {
            return -1;
        }
    } else if (tc_type_is_bool(value->type->tag)) {
        if (fprintf(out, "%s", value->bits != 0 ? "true" : "false") < 0) {
            return -1;
        }
    } else if (tc_type_is_float(value->type->tag)) {
        if (tc_io_write_float_core(value->type->tag, TC_FMT_G, value, out) != 0) {
            return -1;
        }
    } else if (tc_type_is_signed(value->type->tag)) {
        int64_t signed_value = tc_bits_to_signed(value->type->tag, value->bits);
        if (fprintf(out, "%" PRId64, signed_value) < 0) {
            return -1;
        }
    } else {
        uint64_t unsigned_value = tc_value_to_unsigned(value->type->tag, value->bits);
        if (fprintf(out, "%" PRIu64, unsigned_value) < 0) {
            return -1;
        }
    }
    if (newline) {
        if (fputc('\n', out) == EOF) {
            return -1;
        }
    }
    return 0;
}

int tc_io_write_value(const TcValue *value, TcFormatFullSpec fmt, int newline, FILE *out) {
    char *buf = NULL;
    size_t len = 0;
    FILE *stage = NULL;

    if (!value || !out) {
        return -1;
    }
#ifdef _WIN32
    tc_io_ensure_binary_stdio();
#endif

    /* 先完整渲染到内存缓冲，再一次提交到 out（失败则目标流零字节）。 */
#ifdef _WIN32
    {
        char stage_path[4096];

        stage = tc_io_open_stage_file(stage_path, sizeof(stage_path));
        if (!stage) {
            return -1;
        }
        if (tc_io_render_value(value, fmt, newline, stage) != 0) {
            fclose(stage);
            (void)remove(stage_path);
            return -1;
        }
        if (fflush(stage) != 0 || fseek(stage, 0L, SEEK_END) != 0) {
            fclose(stage);
            (void)remove(stage_path);
            return -1;
        }
        {
            long end_pos = ftell(stage);

            if (end_pos < 0) {
                fclose(stage);
                (void)remove(stage_path);
                return -1;
            }
            len = (size_t)end_pos;
        }
        if (fseek(stage, 0L, SEEK_SET) != 0) {
            fclose(stage);
            (void)remove(stage_path);
            return -1;
        }
        if (len > 0U) {
            buf = (char *)malloc(len);
            if (!buf) {
                fclose(stage);
                (void)remove(stage_path);
                return -1;
            }
            if (fread(buf, 1U, len, stage) != len) {
                free(buf);
                fclose(stage);
                (void)remove(stage_path);
                return -1;
            }
        }
        fclose(stage);
        (void)remove(stage_path);
        stage = NULL;
    }
#else
    stage = open_memstream(&buf, &len);
    if (!stage) {
        return -1;
    }
    if (tc_io_render_value(value, fmt, newline, stage) != 0) {
        fclose(stage);
        free(buf);
        return -1;
    }
    if (fclose(stage) != 0) {
        free(buf);
        return -1;
    }
    stage = NULL;
#endif

    if (len > 0U) {
        if (fwrite(buf, 1U, len, out) != len) {
            free(buf);
            return -1;
        }
    }
    free(buf);
    if (fflush(out) != 0) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  stdin 输入辅助                                                      */
/* ------------------------------------------------------------------ */

static int tc_io_is_ascii_space(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/* 由 tc_io_read_value 调用；对外暴露供单元测试直接验证。 */
void tc_io_skip_whitespace(void) {
    int c = 0;

    for (;;) {
        c = fgetc(stdin);
        if (c == EOF) {
            return;
        }
        if (tc_io_is_ascii_space(c)) {
            continue;
        }
        (void)ungetc(c, stdin);
        return;
    }
}

static int tc_io_read_token(char *buf, size_t buf_size, TcDiagnostic *diag, int line) {
    size_t i = 0;
    int c = 0;

    tc_io_skip_whitespace();
    c = fgetc(stdin);
    if (c == EOF) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN,
                          ferror(stdin) ? "input failed" : "unexpected end of input");
        return -1;
    }

    while (c != EOF && !tc_io_is_ascii_space(c)) {
        if (i + 1U >= buf_size) {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
        buf[i++] = (char)c;
        c = fgetc(stdin);
    }
    if (c == EOF && ferror(stdin)) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "input failed");
        return -1;
    }
    buf[i] = '\0';
    return 0;
}

static int tc_io_parse_abs_digits(const char *digits, uint64_t *out_abs) {
    uint64_t value = 0;
    const unsigned char *p = (const unsigned char *)digits;

    if (*p == '\0') {
        return -1;
    }
    while (*p != '\0') {
        unsigned int digit = 0;

        if (*p < '0' || *p > '9') {
            return -1;
        }
        digit = (unsigned int)(*p - '0');
        if (value > UINT64_MAX / UINT64_C(10) ||
            (value == UINT64_MAX / UINT64_C(10) &&
             (uint64_t)digit > UINT64_MAX % UINT64_C(10))) {
            return 1;
        }
        value = value * UINT64_C(10) + (uint64_t)digit;
        p++;
    }
    *out_abs = value;
    return 0;
}

int tc_io_read_digits(int c, int line, TcDiagnostic *diag,
                      uint64_t *out_abs, int *out_sign) {
    char token[256];
    size_t i = 0;
    int sign = 1;
    int parse_rc = 0;

    if (c == '-') {
        sign = -1;
    } else {
        token[i++] = (char)c;
    }
    c = fgetc(stdin);
    while (c != EOF && !tc_io_is_ascii_space(c)) {
        if (i + 1U >= sizeof(token)) {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
        token[i++] = (char)c;
        c = fgetc(stdin);
    }
    if (c == EOF && ferror(stdin)) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "input failed");
        return -1;
    }
    token[i] = '\0';
    parse_rc = tc_io_parse_abs_digits(token, out_abs);
    if (parse_rc != 0) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN,
                          parse_rc > 0 ? "input value out of range" : "invalid input");
        return -1;
    }
    *out_sign = sign;
    return 0;
}

static int tc_io_parse_integer(TcTypeTag type, const char *token, uint64_t *out_bits,
                               TcDiagnostic *diag, int line) {
    const char *digits = token;
    uint64_t abs_value = 0;
    uint64_t limit = 0;
    int negative = 0;
    int parse_rc = 0;
    int width = tc_type_bit_width(type);

    if (*digits == '-') {
        negative = 1;
        digits++;
    }
    parse_rc = tc_io_parse_abs_digits(digits, &abs_value);
    if (parse_rc != 0) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN,
                          parse_rc > 0 ? "input value out of range" : "invalid input");
        return -1;
    }
    if (!tc_type_is_signed(type)) {
        if (negative) {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
        limit = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
        if (abs_value > limit) {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN,
                              "input value out of range");
            return -1;
        }
        *out_bits = abs_value;
        return 0;
    }

    limit = UINT64_C(1) << (width - 1);
    if ((!negative && abs_value >= limit) || (negative && abs_value > limit)) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN,
                          "input value out of range");
        return -1;
    }
    if (negative && abs_value == TC_INT64_MIN_ABS_MAGNITUDE) {
        *out_bits = tc_signed_to_bits(type, INT64_MIN);
    } else {
        int64_t signed_value = (int64_t)abs_value;
        if (negative) {
            signed_value = -signed_value;
        }
        *out_bits = tc_signed_to_bits(type, signed_value);
    }
    return 0;
}

static int tc_io_float_token_is_decimal(const char *token) {
    const unsigned char *p = (const unsigned char *)token;
    int digits_before = 0;
    int digits_after = 0;

    if (*p == '-') {
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        digits_before++;
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            digits_after++;
            p++;
        }
        /* §10.4：小数点前后均须有数字（拒绝 1. / .5） */
        if (digits_before < 1 || digits_after < 1) {
            return 0;
        }
    }
    if (digits_before == 0) {
        return 0;
    }
    if (*p == 'e' || *p == 'E') {
        int exponent_digits = 0;

        p++;
        if (*p == '+' || *p == '-') {
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            exponent_digits++;
            p++;
        }
        if (exponent_digits == 0) {
            return 0;
        }
    }
    return *p == '\0';
}

static int tc_io_float_token_is_nonzero(const char *token) {
    const unsigned char *p = (const unsigned char *)token;

    while (*p != '\0' && *p != 'e' && *p != 'E') {
        if (*p >= '1' && *p <= '9') {
            return 1;
        }
        p++;
    }
    return 0;
}

static int tc_io_localize_decimal_token(const char *token, char *buf, size_t buf_size) {
    const struct lconv *locale = localeconv();
    const char *decimal_point = locale ? locale->decimal_point : NULL;
    const char *dot = strchr(token, '.');
    size_t prefix_len = 0;
    size_t point_len = 0;
    size_t suffix_len = 0;

    if (!dot || !decimal_point || decimal_point[0] == '\0' ||
        strcmp(decimal_point, ".") == 0) {
        if (strlen(token) + 1U > buf_size) {
            return -1;
        }
        strcpy(buf, token);
        return 0;
    }
    prefix_len = (size_t)(dot - token);
    point_len = strlen(decimal_point);
    suffix_len = strlen(dot + 1);
    if (prefix_len + point_len + suffix_len + 1U > buf_size) {
        return -1;
    }
    memcpy(buf, token, prefix_len);
    memcpy(buf + prefix_len, decimal_point, point_len);
    memcpy(buf + prefix_len + point_len, dot + 1, suffix_len + 1U);
    return 0;
}

static float tc_io_strtof_nearest(const char *text, char **end) {
#ifdef TC_HAVE_FENV
    int saved_round = fegetround();
    float value = 0.0f;

    if (saved_round != -1 && saved_round != FE_TONEAREST) {
        (void)fesetround(FE_TONEAREST);
    }
    value = strtof(text, end);
    if (saved_round != -1 && saved_round != FE_TONEAREST) {
        (void)fesetround(saved_round);
    }
    return value;
#else
    return strtof(text, end);
#endif
}

static double tc_io_strtod_nearest(const char *text, char **end) {
#ifdef TC_HAVE_FENV
    int saved_round = fegetround();
    double value = 0.0;

    if (saved_round != -1 && saved_round != FE_TONEAREST) {
        (void)fesetround(FE_TONEAREST);
    }
    value = strtod(text, end);
    if (saved_round != -1 && saved_round != FE_TONEAREST) {
        (void)fesetround(saved_round);
    }
    return value;
#else
    return strtod(text, end);
#endif
}

static int tc_io_parse_float(TcTypeTag type, const char *token, uint64_t *out_bits,
                             TcDiagnostic *diag, int line) {
    char localized[512];
    char *end = NULL;

    if (strcmp(token, "inf") == 0) {
        *out_bits = type == TC_FLOAT32 ? UINT64_C(0x7F800000)
                                       : UINT64_C(0x7FF0000000000000);
        return 0;
    }
    if (strcmp(token, "-inf") == 0) {
        *out_bits = type == TC_FLOAT32 ? UINT64_C(0xFF800000)
                                       : UINT64_C(0xFFF0000000000000);
        return 0;
    }
    if (strcmp(token, "nan") == 0) {
        *out_bits = type == TC_FLOAT32 ? UINT64_C(0x7FC00000)
                                       : UINT64_C(0x7FF8000000000000);
        return 0;
    }
    if (!tc_io_float_token_is_decimal(token) ||
        tc_io_localize_decimal_token(token, localized, sizeof(localized)) != 0) {
        tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
        return -1;
    }

    errno = 0;
    if (type == TC_FLOAT32) {
        float value = tc_io_strtof_nearest(localized, &end);
        uint32_t bits = 0;

        if (end == localized || *end != '\0') {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
        if (isinf((double)value) ||
            (value == 0.0f && tc_io_float_token_is_nonzero(token))) {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN,
                              "input value out of range");
            return -1;
        }
        memcpy(&bits, &value, sizeof(bits));
        *out_bits = (uint64_t)bits;
        return 0;
    }

    {
        double value = tc_io_strtod_nearest(localized, &end);
        uint64_t bits = 0;

        if (end == localized || *end != '\0') {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
        if (isinf(value) || (value == 0.0 && tc_io_float_token_is_nonzero(token))) {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN,
                              "input value out of range");
            return -1;
        }
        memcpy(&bits, &value, sizeof(bits));
        *out_bits = bits;
        return 0;
    }
}

int tc_io_read_value(TcTypeTag type, uint64_t *out_bits, TcDiagnostic *diag, int line) {
    char token[256];
    uint64_t bits = 0;

#ifdef _WIN32
    tc_io_ensure_binary_stdio();
#endif
    if (!out_bits || !diag ||
        (!tc_type_is_integer(type) && !tc_type_is_bool(type) && !tc_type_is_float(type))) {
        return -1;
    }
    if (tc_io_read_token(token, sizeof(token), diag, line) != 0) {
        return -1;
    }
    if (tc_type_is_bool(type)) {
        if (strcmp(token, "true") == 0) {
            bits = UINT64_C(1);
        } else if (strcmp(token, "false") != 0) {
            tc_diagnostic_set(diag, TC_RE_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
    } else if (tc_type_is_float(type)) {
        if (tc_io_parse_float(type, token, &bits, diag, line) != 0) {
            return -1;
        }
    } else if (tc_io_parse_integer(type, token, &bits, diag, line) != 0) {
        return -1;
    }
    *out_bits = bits;
    return 0;
}
