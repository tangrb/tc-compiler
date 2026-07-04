/*
 * tc_aot_rt.c — AOT 运行时辅助实现
 *
 * 将 AOT 生成的 C 代码中的 uint64_t 槽位操作委托给 semantics.c，
 * 包括：字面量求值（tc_aot_lit）、算术运算（tc_aot_arith）、
 * 单目运算（tc_aot_unary）、类型转换（tc_aot_cast）、
 * 格式化输出（tc_aot_write）、输入（tc_aot_read）及错误中止（tc_aot_abort）。
 */
#include "tc_aot_rt.h"

#include "tc_io.h"
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
    tc_io_write_value(&value, fmt, newline, stdout);
}

/* ------------------------------------------------------------------ */
/*  输入                                                               */
/* ------------------------------------------------------------------ */

int tc_aot_read(TcIntType type, uint64_t *out, TcDiagnostic *diag, int line) {
    return tc_io_read_value(type, out, diag, line);
}

/* ------------------------------------------------------------------ */
/*  错误中止                                                           */
/* ------------------------------------------------------------------ */

void tc_aot_abort(const TcDiagnostic *diag, int line) {
    (void)line;
    tc_diagnostic_print(diag, stderr);
    exit(1);
}
