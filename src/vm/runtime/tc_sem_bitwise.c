/*
 * tc_sem_bitwise.c — 按位运算（and/or/xor/not）与移位（shl/shr）
 *
 * 所有按位运算统一采用 uint64 无符号算术 + 位宽掩码，确保符号无关的确定性结果。
 * Bool 类型的值在计算后额外归一化为 0x00/0x01，防止位操作产生非规范位模式
 * （例如 true & 0xFF 得到 0xFF 而非 0x01），保证后续逻辑/I/O 不受污染。
 */
#include "tc_semantics.h"

#include "tc_diagnostic.h"

/* ------------------------------------------------------------------ */
/*  bool 值规范化                                                     */
/* ------------------------------------------------------------------ */

/*
 * TC_BOOL 的规范位模式为 0x00（假）或 0x01（真）。
 * 位运算直接操作底层比特时可能产生非规范值，归一化可消除这一隐患。
 */
static void tc_normalize_bool(TcTypeTag type, TcValue *out) {
    if (type == TC_BOOL) {
        out->bits = out->bits ? 1ULL : 0ULL;
    }
}

/* ------------------------------------------------------------------ */
/*  按位运算                                                            */
/* ------------------------------------------------------------------ */

/*
 * 校验操作数的运行时类型是否与声明的运算类型一致。
 * b 可为 NULL（单目运算时省略第二个操作数），此时只检查 a。
 */
static int tc_check_operand_types(TcTypeTag type, const TcValue *a, const TcValue *b,
                                  TcDiagnostic *diag, int line) {
    if (a->type->tag != type || (b != NULL && b->type->tag != type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "operand type does not match operation type");
        return -1;
    }
    return 0;
}

int tc_exec_bitwise_binary(TcBitwiseOp op, TcTypeTag type,
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
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN, "unknown bitwise operator");
        return -1;
    }

    *out = tc_value_make(type, result & mask);
    tc_normalize_bool(type, out);
    return 0;
}

int tc_exec_bitwise_unary(TcTypeTag type, const TcValue *operand, TcValue *out,
                          TcDiagnostic *diag, int line) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t bits = 0;

    if (tc_check_operand_types(type, operand, NULL, diag, line) != 0) {
        return -1;
    }

    bits = tc_value_to_unsigned(type, operand->bits);
    *out = tc_value_make(type, (~bits) & mask);
    tc_normalize_bool(type, out);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  移位运算辅助                                                         */
/* ------------------------------------------------------------------ */

/*
 * 检测有符号 val * 2^k 是否溢出 int64 范围。
 * 使用无符号除法做边界检查以避免有符号溢出未定义行为。
 * k >= 64 时 val 非零即溢出（移位超越 int64 表示范围）。
 * 返回 1 表示溢出，0 表示结果存于 *result。
 * 注：k 在进入本函数前已被校验为非负（负计数已在 tc_exec_shift 中报错）。
 */
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
        /* 负边界：数学结果 -2^63 可表示。INT64_MAX/pow2 会把该点误判为溢出。 */
        uint64_t abs_val = (uint64_t)(-val);
        uint64_t limit = (1ULL << 63) / pow2;
        uint64_t mag = 0;

        if (abs_val > limit) {
            return 1;
        }
        mag = abs_val * pow2;
        /* mag ∈ [1, 2^63]；2^63 即 INT64_MIN，用无符号环绕避免有符号乘法 UB */
        *result = (int64_t)(0ULL - mag);
        return 0;
    }
}

/*
 * 将移位计数字段的位模式按类型 T 的数值语义解码为数学值 k（规范 §6.4.2）：
 *   - 有符号 T：按二进制补码解码为有符号值，k < 0 时触发 TC_RE_NEGATIVE_SHIFT_COUNT；
 *   - 无符号 T：按非负整数解码，恒非负，不会触发本错误。
 * 返回 0 表示成功（k 存于 *k_out），-1 表示负计数错误（diag 已设置）。
 */
static int tc_shift_count(TcTypeTag type, const TcValue *count, uint64_t *k_out,
                          TcDiagnostic *diag, int line) {
    if (tc_type_is_signed(type)) {
        int64_t k = tc_bits_to_signed(type, count->bits);

        if (k < 0) {
            tc_diagnostic_set(diag, TC_RE_NEGATIVE_SHIFT_COUNT, line, TC_COLUMN_UNKNOWN,
                              "negative shift count");
            return -1;
        }
        *k_out = (uint64_t)k;
        return 0;
    }

    *k_out = tc_value_to_unsigned(type, count->bits);
    return 0;
}

/*
 * shl 左移：对无符号/有符号整数做 val << k。
 * strict 模式：有符号检测乘法溢出（tc_smul_pow2_overflow + 窄类型范围），
 *   无符号检查 val > max >> k；wrap 模式：直接截断到 mask。
 * k >= 位宽时 wrap 返回 0，strict 报 overflow。
 * 注：负移位计数已在 tc_exec_shift 中先行拦截（TC_RE_NEGATIVE_SHIFT_COUNT），
 * 此处 k 恒为非负，k >= n 判定不受影响。
 */
static int tc_exec_shl(TcTypeTag type, TcWrapMode mode, const TcValue *value,
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
        tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
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
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "shift left overflow");
            return -1;
        }
        if (!tc_signed_in_range(wide, type)) {
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "shift left overflow");
            return -1;
        }
        *out = tc_value_make(type, tc_signed_to_bits(type, wide));
        return 0;
    }

    {
        uint64_t max = tc_type_max_unsigned(type);
        if (k > 0 && val_bits > (max >> (unsigned)k)) {
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
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
 * 注：负移位计数已在 tc_exec_shift 中先行拦截（TC_RE_NEGATIVE_SHIFT_COUNT）。
 */
static int tc_exec_shr(TcTypeTag type, const TcValue *value, uint64_t k, TcValue *out) {
    int n = tc_type_bit_width(type);
    uint64_t val_bits = tc_value_to_unsigned(type, value->bits);

    if (k >= (uint64_t)n) {
        *out = tc_value_make(type, 0);
        return 0;
    }

    if (tc_type_is_signed(type)) {
        /* 显式算术右移：高位补符号位。不使用宿主有符号 `>>`（C99 实现定义）。 */
        uint64_t mask = tc_mask_bits(n);
        uint64_t bits = val_bits & mask;
        uint64_t shifted = bits >> (unsigned)k;

        if ((bits >> (unsigned)(n - 1)) & 1ULL) {
            shifted |= mask ^ (mask >> (unsigned)k);
        }
        *out = tc_value_make(type, shifted);
        return 0;
    }

    *out = tc_value_make(type, val_bits >> (unsigned)k);
    return 0;
}

/*
 * tc_exec_shift — 移位分派入口
 *
 * 先校验操作模式（shl 支持 strict/wrap；shr 仅 strict），
 * 再校验操作数类型一致性（value 和 count 须同类型），
 * 最后按 shl/shr 分派各自的执行函数。
 * Bool 移位结果归一化以防构造非规范位模式。
 */
int tc_exec_shift(TcShiftOp op, TcTypeTag type, TcWrapMode mode,
                  const TcValue *value, const TcValue *count, TcValue *out,
                  TcDiagnostic *diag, int line) {
    uint64_t k = 0;

    if (tc_validate_shift_mode(op, type, mode, diag, line) != 0) {
        return -1;
    }

    if (value->type->tag != type || count->type->tag != type) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "operand type does not match operation type");
        return -1;
    }

    /* 先解码移位计数：负计数（TC_RE_NEGATIVE_SHIFT_COUNT）优先于 k >= n 判定（规范 §6.4.2） */
    if (tc_shift_count(type, count, &k, diag, line) != 0) {
        return -1;
    }

    if (op == TC_SHIFT_SHL) {
        if (tc_exec_shl(type, mode, value, k, out, diag, line) != 0) {
            return -1;
        }
    } else {
        if (tc_exec_shr(type, value, k, out) != 0) {
            return -1;
        }
    }

    tc_normalize_bool(type, out);
    return 0;
}
