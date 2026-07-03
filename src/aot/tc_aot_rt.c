/*
 * tc_aot_rt.c — AOT 运行时：将 uint64_t 槽位操作委托给 semantics.c
 */
#include "tc_aot_rt.h"

#include "tc_semantics.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

void tc_aot_diag_init(TcDiagnostic *diag) {
    tc_diagnostic_init(diag);
}

uint64_t tc_aot_lit(TcIntType type, uint64_t magnitude, int negative, int unsigned_suffix) {
    TcLiteral lit;
    TcValue value;

    lit.magnitude = magnitude;
    lit.negative = negative;
    lit.unsigned_suffix = unsigned_suffix;
    value = tc_literal_to_value(&lit, type);
    return value.bits;
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

void tc_aot_write(TcIntType type, uint64_t bits, int newline) {
    if (tc_type_is_signed(type)) {
        int64_t signed_value = tc_bits_to_signed(type, bits);
        fprintf(stdout, "%" PRId64, signed_value);
    } else {
        uint64_t unsigned_value = tc_value_to_unsigned(type, bits);
        fprintf(stdout, "%" PRIu64, unsigned_value);
    }
    if (newline) {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

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
    int sign = 1;
    uint64_t abs_value = 0;
    TcValue value;

    tc_aot_skip_whitespace();
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

void tc_aot_abort(const TcDiagnostic *diag, int line) {
    (void)line;
    tc_diagnostic_print(diag, stderr);
    exit(1);
}
