/*
 * tc_sem_bitwise.c — 按位运算（and/or/xor/not）与移位（shl/shr）
 */
#include "tc_semantics.h"

#include "tc_diagnostic.h"

/* ------------------------------------------------------------------ */
/*  按位运算                                                            */
/* ------------------------------------------------------------------ */

static int tc_check_operand_types(TcTypeKind type, const TcValue *a, const TcValue *b,
                                  TcDiagnostic *diag, int line) {
    if (a->type != type || (b != NULL && b->type != type)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "operand type does not match operation type");
        return -1;
    }
    return 0;
}

int tc_exec_bitwise_binary(TcBitwiseOp op, TcTypeKind type,
                           const TcValue *lhs, const TcValue *rhs, TcValue *out,
                           TcDiagnostic *diag, int line) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t result = 0;

    if (tc_check_operand_types(type, lhs, rhs, diag, line) != 0) {
        return -1;
    }

    a = tc_value_to_unsigned(type, lhs->bits);
    b = tc_value_to_unsigned(type, rhs->bits);

    switch (op) {
    case TC_BIT_AND:
        result = a & b;
        break;
    case TC_BIT_OR:
        result = a | b;
        break;
    case TC_BIT_XOR:
        result = a ^ b;
        break;
    default:
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, TC_COLUMN_UNKNOWN, "unknown bitwise operator");
        return -1;
    }

    *out = tc_value_make(type, result & mask);
    return 0;
}

int tc_exec_bitwise_unary(TcTypeKind type, const TcValue *operand, TcValue *out,
                          TcDiagnostic *diag, int line) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t bits = 0;

    if (tc_check_operand_types(type, operand, NULL, diag, line) != 0) {
        return -1;
    }

    bits = tc_value_to_unsigned(type, operand->bits);
    *out = tc_value_make(type, (~bits) & mask);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  移位运算辅助                                                         */
/* ------------------------------------------------------------------ */

/** 有符号 val * 2^k，检测 int64 乘法溢出 */
static int tc_smul_pow2_overflow(int64_t val, unsigned k, int64_t *result) {
    uint64_t pow2 = 0;

    if (k == 0) {
        *result = val;
        return 0;
    }
    if (k >= 64) {
        if (val != 0) {
            return 1;
        }
        *result = 0;
        return 0;
    }

    pow2 = 1ULL << k;
    if (val > 0) {
        if ((uint64_t)val > (uint64_t)INT64_MAX / pow2) {
            return 1;
        }
        *result = val * (int64_t)pow2;
        return 0;
    }
    if (val == 0) {
        *result = 0;
        return 0;
    }
    if (val == INT64_MIN) {
        return 1;
    }
    {
        int64_t abs_val = -val;
        if ((uint64_t)abs_val > (uint64_t)INT64_MAX / pow2) {
            return 1;
        }
        *result = val * (int64_t)pow2;
        return 0;
    }
}

static int tc_shift_count(TcTypeKind type, const TcValue *count, uint64_t *k_out) {
    *k_out = tc_value_to_unsigned(type, count->bits);
    return 0;
}

static int tc_exec_shl(TcTypeKind type, TcWrapMode mode, const TcValue *value,
                       uint64_t k, TcValue *out, TcDiagnostic *diag, int line) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t val_bits = tc_value_to_unsigned(type, value->bits);

    if (val_bits == 0) {
        *out = tc_value_make(type, 0);
        return 0;
    }

    if (k >= (uint64_t)n) {
        if (mode == TC_ARITH_WRAP) {
            *out = tc_value_make(type, 0);
            return 0;
        }
        tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "shift left overflow");
        return -1;
    }

    if (mode == TC_ARITH_WRAP) {
        *out = tc_value_make(type, (val_bits << (unsigned)k) & mask);
        return 0;
    }

    if (tc_type_is_signed(type)) {
        int64_t val = tc_bits_to_signed(type, val_bits);
        int64_t wide = 0;

        if (tc_smul_pow2_overflow(val, (unsigned)k, &wide)) {
            tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "shift left overflow");
            return -1;
        }
        if (!tc_signed_in_range(wide, type)) {
            tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "shift left overflow");
            return -1;
        }
        *out = tc_value_make(type, tc_signed_to_bits(type, wide));
        return 0;
    }

    {
        uint64_t max = tc_type_max_unsigned(type);
        if (k > 0 && val_bits > (max >> (unsigned)k)) {
            tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "shift left overflow");
            return -1;
        }
        *out = tc_value_make(type, val_bits << (unsigned)k);
        return 0;
    }
}

/*
 * 右移永不溢出（shr 恒为 strict 模式，不设 wrap 参数）：
 *   - 有符号：算术右移（高位复制符号位）
 *   - 无符号：逻辑右移（高位补 0）
 * k >= n 时直接返回 0。
 */
static int tc_exec_shr(TcTypeKind type, const TcValue *value, uint64_t k, TcValue *out) {
    int n = tc_type_bit_width(type);
    uint64_t val_bits = tc_value_to_unsigned(type, value->bits);

    if (k >= (uint64_t)n) {
        *out = tc_value_make(type, 0);
        return 0;
    }

    if (tc_type_is_signed(type)) {
        int64_t val = tc_bits_to_signed(type, val_bits);
        *out = tc_value_make(type, tc_signed_to_bits(type, val >> (unsigned)k));
        return 0;
    }

    *out = tc_value_make(type, val_bits >> (unsigned)k);
    return 0;
}

int tc_exec_shift(TcShiftOp op, TcTypeKind type, TcWrapMode mode,
                  const TcValue *value, const TcValue *count, TcValue *out,
                  TcDiagnostic *diag, int line) {
    uint64_t k = 0;

    if (tc_validate_shift_mode(op, type, mode, diag, line) != 0) {
        return -1;
    }

    if (value->type != type || count->type != type) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "operand type does not match operation type");
        return -1;
    }

    tc_shift_count(type, count, &k);

    if (op == TC_SHIFT_SHL) {
        return tc_exec_shl(type, mode, value, k, out, diag, line);
    }
    return tc_exec_shr(type, value, k, out);
}
