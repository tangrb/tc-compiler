/*
 * tc_io.c — TC 统一 I/O 实现
 *
 * 合并 tc_executor.c 和 tc_aot_rt.c 中平行的 read/write 实现，
 * 统一入口供 VM 执行引擎和 AOT 运行时调用。
 *
 * write 函数使用 FILE *out 参数支持灵活输出，并检查 I/O 错误；
 * read 函数从 stdin 解析十进制整数或 bool 文本，含范围检查。
 */
#include "tc_io.h"

#include "tc_semantics.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  格式化输出                                                          */
/* ------------------------------------------------------------------ */

int tc_io_write_formatted(TcIntType type, TcFormatSpec fmt, const TcValue *value, FILE *out) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t uval = tc_value_to_unsigned(type, value->bits) & mask;

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
        int i = 0;
        for (i = n - 1; i >= 0; i--) {
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
    case TC_FMT_NONE:
        /* 无格式输出由 tc_io_write_value 直接处理；此处防御误传 */
        return -1;
    default:
        return -1;
    }
    return 0;
}

int tc_io_write_value(const TcValue *value, TcFormatSpec fmt, int newline, FILE *out) {
    if (fmt != TC_FMT_NONE) {
        if (tc_io_write_formatted(value->type, fmt, value, out) != 0) {
            return -1;
        }
    } else if (tc_type_is_bool(value->type)) {
        if (fprintf(out, "%s", value->bits != 0 ? "true" : "false") < 0) {
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
    if (fflush(out) != 0) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  stdin 输入辅助                                                      */
/* ------------------------------------------------------------------ */

/* 由 tc_io_read_value 调用；对外暴露供单元测试直接验证 */
void tc_io_skip_whitespace(void) {
    int c = 0;
    for (;;) {
        c = fgetc(stdin);
        if (c == EOF) {
            return;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        ungetc(c, stdin);
        return;
    }
}

int tc_io_read_digits(int c, int line, TcDiagnostic *diag,
                      uint64_t *out_abs, int *out_sign) {
    int sign = 1;
    int digit_count = 0;
    uint64_t abs_value = 0;

    if (c == '-') {
        sign = -1;
        c = fgetc(stdin);
        if (c == EOF) {
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
    }
    if (!isdigit((unsigned char)c)) {
        tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
        return -1;
    }
    do {
        int digit = c - '0';
        if (abs_value > UINT64_MAX / 10ULL ||
            (abs_value == UINT64_MAX / 10ULL &&
             (uint64_t)digit > UINT64_MAX % 10ULL)) {
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN,
                              "input value out of range");
            return -1;
        }
        abs_value = abs_value * 10ULL + (uint64_t)digit;
        digit_count++;
        c = fgetc(stdin);
    } while (c != EOF && isdigit((unsigned char)c));
    if (c != EOF) {
        ungetc(c, stdin);
    }
    if (digit_count == 0) {
        tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
        return -1;
    }

    *out_abs = abs_value;
    *out_sign = sign;
    return 0;
}

int tc_io_read_value(TcIntType type, uint64_t *out_bits, TcDiagnostic *diag, int line) {
    int c = 0;
    TcValue value;

    tc_io_skip_whitespace();

    if (tc_type_is_bool(type)) {
        char word[8];
        size_t i = 0;

        c = fgetc(stdin);
        while (c != EOF && i + 1 < sizeof(word) &&
               (c == 't' || c == 'r' || c == 'u' || c == 'e' || c == 'f' || c == 'a' ||
                c == 'l' || c == 's')) {
            word[i++] = (char)c;
            c = fgetc(stdin);
        }
        if (c != EOF) {
            ungetc(c, stdin);
        }
        word[i] = '\0';
        if (strcmp(word, "true") == 0) {
            value = tc_value_make(TC_BOOL, 1);
        } else if (strcmp(word, "false") == 0) {
            value = tc_value_make(TC_BOOL, 0);
        } else {
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
        *out_bits = value.bits;
        return 0;
    }

    {
        int sign = 1;
        uint64_t abs_value = 0;

        c = fgetc(stdin);
        if (c == EOF) {
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "unexpected end of input");
            return -1;
        }

        if (tc_io_read_digits(c, line, diag, &abs_value, &sign) != 0) {
            return -1;
        }

        /* 按目标类型的有符号性做范围检查并构造 TcValue */
        if (tc_type_is_signed(type)) {
            if (sign == -1 && abs_value == TC_INT64_MIN_ABS_MAGNITUDE) {
                /* INT64_MIN 的特殊情况：abs_value == 2^63 需要单独处理 */
                value = tc_value_make(type, tc_signed_to_bits(type, INT64_MIN));
            } else {
                int64_t signed_value = (int64_t)abs_value;
                signed_value *= sign;
                if (!tc_signed_in_range(signed_value, type)) {
                    tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN,
                                      "input value out of range");
                    return -1;
                }
                value = tc_value_make(type, tc_signed_to_bits(type, signed_value));
            }
        } else {
            if (sign == -1) {
                tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
                return -1;
            }
            if (!tc_unsigned_in_range(abs_value, type)) {
                tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN,
                                  "input value out of range");
                return -1;
            }
            value = tc_value_make(type, abs_value);
        }

        *out_bits = value.bits;
        return 0;
    }
}
