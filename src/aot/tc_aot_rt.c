/*
 * tc_aot_rt.c — AOT 运行时辅助实现
 *
 * 将 AOT 生成的 C 代码中的 uint64_t 槽位操作委托给 tc_semantics.c，
 * 包括：字面量求值（tc_aot_lit）、算术运算（tc_aot_arith）、
 * 单目运算（tc_aot_unary）、按位运算（tc_aot_bitwise_*）、移位（tc_aot_shift）、
 * 类型转换（tc_aot_cast）、
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

/*
 * AOT 槽位初始化：用未初始化哨兵值填充所有槽位，
 * 与 VM 的 tc_slots_init_uninitialized 使用一致的哨兵值
 * （TC_UNINITIALIZED_SLOT_BITS），保障"使用未初始化变量"检测
 * 在 VM 和 AOT 下的行为一致。
 */
void tc_aot_init_slots(uint64_t *slots, size_t count) {
    tc_slot_bits_init_uninitialized(slots, count);
}

/* ------------------------------------------------------------------ */
/*  字面量 & 算术 & 单目 & cast 委托                                      */
/* ------------------------------------------------------------------ */

uint64_t tc_aot_lit(TcType type, uint64_t magnitude, int negative, int unsigned_suffix) {
    TcLiteral lit;
    TcValue value;

    if (tc_type_is_float(type)) {
        return magnitude;
    }

    memset(&lit, 0, sizeof(lit));
    lit.magnitude = magnitude;
    lit.negative = negative;
    lit.unsigned_suffix = unsigned_suffix;
    lit.is_bool = tc_type_is_bool(type) ? 1 : 0;
    value = tc_literal_to_value(&lit, type);
    return value.bits;
}

int tc_aot_compare(TcCompareOp op, TcType type, uint64_t *out, uint64_t lhs, uint64_t rhs,
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

int tc_aot_arith(TcArithOp op, TcType type, TcWrapMode mode, uint64_t *out, uint64_t lhs,
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

int tc_aot_unary(TcUnaryOp op, TcType type, TcWrapMode mode, uint64_t *out, uint64_t operand,
                 TcDiagnostic *diag, int line) {
    TcValue operand_value = tc_value_make(type, operand);
    TcValue result;

    if (tc_exec_unary(op, type, mode, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_bitwise_binary(TcBitwiseOp op, TcType type, uint64_t *out, uint64_t lhs,
                          uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_bitwise_binary(op, type, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_bitwise_unary(TcType type, uint64_t *out, uint64_t operand, TcDiagnostic *diag,
                         int line) {
    TcValue operand_value = tc_value_make(type, operand);
    TcValue result;

    if (tc_exec_bitwise_unary(type, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_shift(TcShiftOp op, TcType type, TcWrapMode mode, uint64_t *out, uint64_t value,
                 uint64_t count, TcDiagnostic *diag, int line) {
    TcValue value_v = tc_value_make(type, value);
    TcValue count_v = tc_value_make(type, count);
    TcValue result;

    if (tc_exec_shift(op, type, mode, &value_v, &count_v, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_cast(TcType target, TcTruncateMode mode, uint64_t src_bits, TcType src_type,
                uint64_t *out, TcDiagnostic *diag, int line) {
    TcValue src = tc_value_make(src_type, src_bits);
    TcValue result;

    if ((mode == TC_TRUNC_TRUNCATE
             ? tc_exec_truncate(target, &src, &result, diag, line)
             : tc_exec_cast(target, &src, &result, diag, line)) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_bitcast(TcType target, TcType source_type, uint64_t *out, uint64_t source_bits,
                   TcDiagnostic *diag, int line) {
    TcValue source = tc_value_make(source_type, source_bits);
    TcValue result;

    if (tc_exec_bitcast(target, &source, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_arith(TcArithOp op, TcType type, TcFloatMode mode, uint64_t *out, uint64_t lhs,
                    uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_fp_arith(op, type, mode, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_unary(TcUnaryOp op, TcType type, TcFloatMode mode, uint64_t *out,
                    uint64_t operand, TcDiagnostic *diag, int line) {
    TcValue operand_value = tc_value_make(type, operand);
    TcValue result;

    if (tc_exec_fp_unary(op, type, mode, &operand_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_compare(TcCompareOp op, TcType type, TcFloatMode mode, uint64_t *out,
                      uint64_t lhs, uint64_t rhs, TcDiagnostic *diag, int line) {
    TcValue lhs_value = tc_value_make(type, lhs);
    TcValue rhs_value = tc_value_make(type, rhs);
    TcValue result;

    if (tc_exec_fp_compare(op, type, mode, &lhs_value, &rhs_value, &result, diag, line) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

int tc_aot_fp_cast(TcType target, TcTruncateMode mode, uint64_t src_bits, TcType src_type,
                   uint64_t *out, TcDiagnostic *diag, int line) {
    TcValue src = tc_value_make(src_type, src_bits);
    TcValue result;

    if ((mode == TC_TRUNC_TRUNCATE
             ? tc_exec_truncate(target, &src, &result, diag, line)
             : tc_exec_cast(target, &src, &result, diag, line)) != 0) {
        return -1;
    }
    *out = result.bits;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  格式化输出                                                          */
/* ------------------------------------------------------------------ */

int tc_aot_write(TcType type, TcFormatSpec fmt, uint64_t bits, int newline,
                 TcDiagnostic *diag, int line) {
    TcValue value = tc_value_make(type, bits);

    if (tc_io_write_value(&value, fmt, newline, stdout) != 0) {
        tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "output failed");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  输入                                                               */
/* ------------------------------------------------------------------ */

int tc_aot_read(TcType type, uint64_t *out, TcDiagnostic *diag, int line) {
    return tc_io_read_value(type, out, diag, line);
}

/* ------------------------------------------------------------------ */
/*  错误中止                                                           */
/* ------------------------------------------------------------------ */

/*
 * AOT 生成代码的错误回调：打印诊断信息后以 exit(1) 终止。
 * 与 VM 的 fail-fast 一致：遇到首个运行时错误即中止执行。
 * line 参数保留供扩展使用（如精确指示生成代码中的出错位置）。
 */
void tc_aot_abort(const TcDiagnostic *diag, int line) {
    (void)line;
    tc_diagnostic_print(diag, stderr);
    exit(1);
}
