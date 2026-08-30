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

static int tc_io_normalize_decimal_point(char *buf) {
    const struct lconv *locale = localeconv();
    const char *decimal_point = locale ? locale->decimal_point : NULL;
    char *position = NULL;
    size_t point_len = 0;
    size_t suffix_len = 0;

    if (!decimal_point || decimal_point[0] == '\0' || strcmp(decimal_point, ".") == 0) {
        return 0;
    }
    position = strstr(buf, decimal_point);
    if (!position) {
        return 0;
    }
    point_len = strlen(decimal_point);
    suffix_len = strlen(position + point_len);
    position[0] = '.';
    if (point_len > 1U) {
        memmove(position + 1, position + point_len, suffix_len + 1U);
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

static void tc_io_fix_exp_width(char *buf) {
    char *mark = strpbrk(buf, "eE");
    char *p = NULL;

    if (!mark) {
        return;
    }
    p = mark + 1;
    if (*p == '+' || *p == '-') {
        p++;
    }
    if (p[0] != '\0' && p[1] == '\0') {
        memmove(p + 1, p, 2U);
        *p = '0';
    }
}

static int tc_io_write_float_core(TcTypeTag type, TcFormatSpec spec, const TcValue *value,
                                  FILE *out) {
    TcFormatFullSpec fmt = tc_format_spec_make(spec);

    return tc_io_write_formatted(type, fmt, value, out);
}

static int tc_io_format_float(TcTypeTag type, const TcFormatFullSpec *fmt, const TcValue *value,
                              FILE *out) {
    double number = tc_fp_bits_to_double(type, value->bits);
    int uppercase = fmt->spec == TC_FMT_EU || fmt->spec == TC_FMT_GU;
    int special = 0;
    char prefix[3];
    const char *digits = NULL;
    char *body = NULL;
    int precision = 6;
    int rc = 0;
#ifdef TC_HAVE_FENV
    int saved_round = fegetround();
#endif

    prefix[0] = '\0';
    if (isnan(number)) {
        digits = uppercase ? "NAN" : "nan";
        if (fmt->flag_plus) {
            prefix[0] = '+';
            prefix[1] = '\0';
        }
        special = 1;
    } else if (isinf(number)) {
        if (signbit(number)) {
            prefix[0] = '-';
            prefix[1] = '\0';
        } else if (fmt->flag_plus) {
            prefix[0] = '+';
            prefix[1] = '\0';
        }
        digits = uppercase ? "INF" : "inf";
        special = 1;
    }
    if (special) {
        return tc_io_write_aligned(out, prefix, digits, fmt, 0);
    }

    if (fmt->precision_set) {
        precision = fmt->precision;
        if ((fmt->spec == TC_FMT_G || fmt->spec == TC_FMT_GU) && precision == 0) {
            precision = 1;
        }
    }

    {
        char specfmt[16];
        int need = 0;
        const char *conv_ch = "f";

        switch (fmt->spec) {
        case TC_FMT_F:
            conv_ch = "f";
            break;
        case TC_FMT_E:
            conv_ch = "e";
            break;
        case TC_FMT_EU:
            conv_ch = "E";
            break;
        case TC_FMT_G:
            conv_ch = "g";
            break;
        case TC_FMT_GU:
            conv_ch = "G";
            break;
        default:
            return -1;
        }
        if (fmt->flag_hash) {
            (void)snprintf(specfmt, sizeof(specfmt), "%%#.%d%s", precision, conv_ch);
        } else {
            (void)snprintf(specfmt, sizeof(specfmt), "%%.%d%s", precision, conv_ch);
        }
#ifdef TC_HAVE_FENV
        if (saved_round != -1 && saved_round != FE_TONEAREST) {
            (void)fesetround(FE_TONEAREST);
        }
#endif
        need = snprintf(NULL, 0, specfmt, number);
#ifdef TC_HAVE_FENV
        if (saved_round != -1 && saved_round != FE_TONEAREST) {
            (void)fesetround(saved_round);
        }
#endif
        if (need < 0) {
            return -1;
        }
        body = (char *)malloc((size_t)need + 2U);
        if (!body) {
            return -1;
        }
#ifdef TC_HAVE_FENV
        if (saved_round != -1 && saved_round != FE_TONEAREST) {
            (void)fesetround(FE_TONEAREST);
        }
#endif
        if (snprintf(body, (size_t)need + 2U, specfmt, number) < 0) {
#ifdef TC_HAVE_FENV
            if (saved_round != -1 && saved_round != FE_TONEAREST) {
                (void)fesetround(saved_round);
            }
#endif
            free(body);
            return -1;
        }
#ifdef TC_HAVE_FENV
        if (saved_round != -1 && saved_round != FE_TONEAREST) {
            (void)fesetround(saved_round);
        }
#endif
        tc_io_normalize_decimal_point(body);
        tc_io_fix_exp_width(body);
        prefix[0] = '\0';
        digits = body;
        if (body[0] == '-') {
            prefix[0] = '-';
            prefix[1] = '\0';
            digits = body + 1;
        } else if (fmt->flag_plus) {
            prefix[0] = '+';
            prefix[1] = '\0';
        }
        rc = tc_io_write_aligned(out, prefix, digits, fmt,
                                 fmt->flag_zero && !fmt->flag_minus);
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
