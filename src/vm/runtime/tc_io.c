/*
 * tc_io.c — TC 统一 I/O 实现
 *
 * 合并 tc_executor.c 和 tc_aot_rt.c 中平行的 read/write 实现，
 * 统一入口供 VM 执行引擎和 AOT 运行时调用。
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

/* ------------------------------------------------------------------ */
/*  格式化输出                                                          */
/* ------------------------------------------------------------------ */

static int tc_io_format_accepts_type(TcTypeKind type, TcFormatSpec fmt) {
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

static int tc_io_write_float(TcTypeKind type, TcFormatSpec fmt, const TcValue *value, FILE *out) {
    char buf[128];
    double number = tc_fp_bits_to_double(type, value->bits);
    const char *format = NULL;
    int written = 0;
#ifdef TC_HAVE_FENV
    int saved_round = fegetround();
#endif

    if (isnan(number)) {
        const char *text = (fmt == TC_FMT_EU || fmt == TC_FMT_GU) ? "NAN" : "nan";
        return fputs(text, out) == EOF ? -1 : 0;
    }
    if (isinf(number)) {
        const int uppercase = fmt == TC_FMT_EU || fmt == TC_FMT_GU;
        const char *text = signbit(number) ? (uppercase ? "-INF" : "-inf")
                                           : (uppercase ? "INF" : "inf");
        return fputs(text, out) == EOF ? -1 : 0;
    }

    switch (fmt) {
    case TC_FMT_F:
        format = "%f";
        break;
    case TC_FMT_E:
        format = "%e";
        break;
    case TC_FMT_EU:
        format = "%E";
        break;
    case TC_FMT_G:
        format = "%g";
        break;
    case TC_FMT_GU:
        format = "%G";
        break;
    default:
        return -1;
    }

#ifdef TC_HAVE_FENV
    if (saved_round != -1 && saved_round != FE_TONEAREST) {
        (void)fesetround(FE_TONEAREST);
    }
#endif
    written = snprintf(buf, sizeof(buf), format, number);
#ifdef TC_HAVE_FENV
    if (saved_round != -1 && saved_round != FE_TONEAREST) {
        (void)fesetround(saved_round);
    }
#endif
    if (written < 0 || (size_t)written >= sizeof(buf)) {
        return -1;
    }
    tc_io_normalize_decimal_point(buf);
    return fputs(buf, out) == EOF ? -1 : 0;
}

int tc_io_write_formatted(TcTypeKind type, TcFormatSpec fmt, const TcValue *value, FILE *out) {
    int n = 0;
    uint64_t mask = 0;
    uint64_t uval = 0;

    if (!value || !out || value->type != type || !tc_io_format_accepts_type(type, fmt)) {
        return -1;
    }
    n = tc_type_bit_width(type);
    mask = tc_mask_bits(n);
    uval = tc_value_to_unsigned(type, value->bits) & mask;

    switch (fmt) {
    case TC_FMT_D:
    case TC_FMT_I:
        if (!tc_type_is_signed(type)) {
            if (fprintf(out, "%llu", (unsigned long long)uval) < 0) {
                return -1;
            }
        } else {
            int64_t sval = tc_bits_to_signed(type, value->bits);
            if (fprintf(out, "%lld", (long long)sval) < 0) {
                return -1;
            }
        }
        break;
    case TC_FMT_U:
        if (fprintf(out, "%llu", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_X:
        if (fprintf(out, "%llx", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_XU:
        if (fprintf(out, "%llX", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_O:
        if (fprintf(out, "%llo", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_B: {
        int i = n - 1;
        int keep_full_width = tc_type_is_signed(type) && tc_bits_to_signed(type, value->bits) < 0;

        while (!keep_full_width && i > 0 && ((uval >> i) & 1U) == 0U) {
            i--;
        }
        for (; i >= 0; i--) {
            if (fputc((uval >> i) & 1 ? '1' : '0', out) == EOF) {
                return -1;
            }
        }
        break;
    }
    case TC_FMT_T:
        if (fprintf(out, "%s", value->bits != 0 ? "true" : "false") < 0) {
            return -1;
        }
        break;
    case TC_FMT_F:
    case TC_FMT_E:
    case TC_FMT_EU:
    case TC_FMT_G:
    case TC_FMT_GU:
        return tc_io_write_float(type, fmt, value, out);
    case TC_FMT_NONE:
        /* 无格式输出由 tc_io_write_value 直接处理；此处防御误传 */
        return -1;
    default:
        return -1;
    }
    return 0;
}

static int tc_io_render_value(const TcValue *value, TcFormatSpec fmt, int newline, FILE *out) {
    if (fmt != TC_FMT_NONE) {
        if (tc_io_write_formatted(value->type, fmt, value, out) != 0) {
            return -1;
        }
    } else if (tc_type_is_bool(value->type)) {
        if (fprintf(out, "%s", value->bits != 0 ? "true" : "false") < 0) {
            return -1;
        }
    } else if (tc_type_is_float(value->type)) {
        if (tc_io_write_float(value->type, TC_FMT_G, value, out) != 0) {
            return -1;
        }
    } else if (tc_type_is_signed(value->type)) {
        int64_t signed_value = tc_bits_to_signed(value->type, value->bits);
        if (fprintf(out, "%" PRId64, signed_value) < 0) {
            return -1;
        }
    } else {
        uint64_t unsigned_value = tc_value_to_unsigned(value->type, value->bits);
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

int tc_io_write_value(const TcValue *value, TcFormatSpec fmt, int newline, FILE *out) {
    char *buf = NULL;
    size_t len = 0;
    FILE *stage = NULL;

    if (!value || !out) {
        return -1;
    }

    /* 先完整渲染到内存缓冲，再一次提交到 out（失败则目标流零字节）。 */
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

static int tc_io_parse_integer(TcTypeKind type, const char *token, uint64_t *out_bits,
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
    }
    if (digits_before == 0 && digits_after == 0) {
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

static int tc_io_parse_float(TcTypeKind type, const char *token, uint64_t *out_bits,
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

int tc_io_read_value(TcTypeKind type, uint64_t *out_bits, TcDiagnostic *diag, int line) {
    char token[256];
    uint64_t bits = 0;

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
