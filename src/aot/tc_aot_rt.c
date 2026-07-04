/*
 * tc_aot_rt.c — AOT 运行时辅助实现
 *
 * 将 AOT 生成的 C 代码中的 uint64_t 槽位操作委托给 semantics.c，
 * 包括：字面量求值（tc_aot_lit）、算术运算（tc_aot_arith）、
 * 单目运算（tc_aot_unary）、类型转换（tc_aot_cast）、
 * 格式化输出（tc_aot_write）、输入（tc_aot_read）及错误中止（tc_aot_abort）。
 */
#include "tc_aot_rt.h"

#include "tc_semantics.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tc_aot_diag_init(TcDiagnostic *diag) {
    tc_diagnostic_init(diag);
}

/* ------------------------------------------------------------------ */
/*  字面量 & 算术 & 单目 & cast 委托                                      */
/* ------------------------------------------------------------------ */

uint64_t tc_aot_lit(TcIntType type, uint64_t magnitude, int negative, int unsigned_suffix) {
    TcLiteral lit;
    TcValue value;

    lit.magnitude = magnitude;
    lit.negative = negative;
    lit.unsigned_suffix = unsigned_suffix;
    lit.is_bool = tc_type_is_bool(type) ? 1 : 0;
    value = tc_literal_to_value(&lit, type);
    return value.bits;
}

int tc_aot_compare(TcCompareOp op, TcIntType type, uint64_t *out, uint64_t lhs, uint64_t rhs,
                   TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_compare(op, type, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_logic(TcLogicOp op, uint64_t *out, uint64_t lhs, uint64_t rhs, TcDiagnostic *diag,
                 int line) {
    TcValue lhs_value = tc_value_make(TC_BOOL, lhs);
    TcValue rhs_value = tc_value_make(TC_BOOL, rhs);
    TcValue result;

    if (op == TC_LOGIC_NOT) {
        if (tc_exec_logic_unary(op, &lhs_value, &result, diag, line) != 0) {
            return -1;
        }
    } else if (tc_exec_logic_binary(op, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_logic_unary(TcLogicOp op, uint64_t *out, uint64_t operand, TcDiagnostic *diag,
                       int line) {
    TcValue operand_value = tc_value_make(TC_BOOL, operand);
    TcValue result;

    if (tc_exec_logic_unary(op, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_arith(TcArithOp op, TcIntType type, TcWrapMode mode, uint64_t *out, uint64_t lhs,
                 uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_arith(op, type, mode, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_unary(TcUnaryOp op, TcIntType type, TcWrapMode mode, uint64_t *out, uint64_t operand,
                 TcDiagnostic *diag, int line) {
    TcValue operand_value = tc_value_make(type, operand);
    TcValue result;

    if (tc_exec_unary(op, type, mode, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_cast(TcIntType target, TcTruncateMode mode, uint64_t src_bits, TcIntType src_type,
                uint64_t *out, TcDiagnostic *diag, int line) {
    TcValue src = tc_value_make(src_type, src_bits);
    TcValue result;

    if (tc_exec_cast(target, mode, &src, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  格式化输出                                                          */
/* ------------------------------------------------------------------ */

void tc_aot_write(TcIntType type, TcFormatSpec fmt, uint64_t bits, int newline) {
    TcValue value = tc_value_make(type, bits);
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t uval = tc_value_to_unsigned(type, bits) & mask;

    if (fmt != TC_FMT_NONE) {
        switch (fmt) {
        case TC_FMT_D:
        case TC_FMT_I:
            if (!tc_type_is_signed(type)) {
                fprintf(stdout, "%llu", (unsigned long long)uval);
            } else {
                fprintf(stdout, "%lld", (long long)tc_bits_to_signed(type, bits));
            }
            break;
        case TC_FMT_U:
            fprintf(stdout, "%llu", (unsigned long long)uval);
            break;
        case TC_FMT_X:
            fprintf(stdout, "%llx", (unsigned long long)uval);
            break;
        case TC_FMT_XU:
            fprintf(stdout, "%llX", (unsigned long long)uval);
            break;
        case TC_FMT_O:
            fprintf(stdout, "%llo", (unsigned long long)uval);
            break;
        case TC_FMT_B: {
            int i = 0;
            for (i = n - 1; i >= 0; i--) {
                fputc((uval >> i) & 1 ? '1' : '0', stdout);
            }
            break;
        }
        case TC_FMT_T:
            fprintf(stdout, "%s", value.bits != 0 ? "true" : "false");
            break;
        default:
            break;
        }
    } else if (tc_type_is_bool(type)) {
        fprintf(stdout, "%s", value.bits != 0 ? "true" : "false");
    } else if (tc_type_is_signed(type)) {
        int64_t signed_value = tc_bits_to_signed(type, bits);
        fprintf(stdout, "%" PRId64, signed_value);
    } else {
        uint64_t unsigned_value = tc_value_to_unsigned(type, bits);
        fprintf(stdout, "%" PRIu64, unsigned_value);
    }
    (void)value;
    if (newline) {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  输入                                                               */
/* ------------------------------------------------------------------ */

/** 跳过 stdin 前导空白 */
static void tc_aot_skip_whitespace(void) {
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

/*
 * @brief 从 stdin 读取十进制数字字符序列，计算其绝对值
 * @param c        当前已读取的首个字符
 * @param line     当前行号
 * @param diag     诊断对象
 * @param out_abs  输出：绝对值
 * @param out_sign 输出：符号（1 或 -1）
 * @return 成功 0；非法或超出范围 -1
 *
 * 提取 signed/unsigned 输入的公共数字读取逻辑，与 executor.c 中的
 * tc_read_decimal_digits 功能相同（因 AOT 独立编译，无法共享）。
 */
static int tc_aot_read_decimal_digits(int c, int line, TcDiagnostic *diag, uint64_t *out_abs,
                                      int *out_sign) {
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
            (abs_value == UINT64_MAX / 10ULL && (uint64_t)digit > UINT64_MAX % 10ULL)) {
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "input value out of range");
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

int tc_aot_read(TcIntType type, uint64_t *out, TcDiagnostic *diag, int line) {
    int c = 0;
    TcValue value;

    tc_aot_skip_whitespace();

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
        *out = value.bits;
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
    if (tc_aot_read_decimal_digits(c, line, diag, &abs_value, &sign) != 0) {
        return -1;
    }

    if (tc_type_is_signed(type)) {
        if (sign == -1 && abs_value == TC_INT64_MIN_ABS_MAGNITUDE) {
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
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "input value out of range");
            return -1;
        }
        value = tc_value_make(type, abs_value);
    }

    *out = value.bits;
    return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  错误中止                                                           */
/* ------------------------------------------------------------------ */

void tc_aot_abort(const TcDiagnostic *diag, int line) {
    (void)line;
    tc_diagnostic_print(diag, stderr);
    exit(1);
}
