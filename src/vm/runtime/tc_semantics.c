/*
 * tc_semantics.c — TC 整数语义运算的实现
 *
 * 本模块是 TC-VM 的语义核心，严格遵循 TC 语言标准：
 *   - 有符号 strict 模式：溢出检测，溢出时报 TC_ERR_INTEGER_OVERFLOW
 *   - 有符号 wrap 模式：按目标类型位宽做二进制环绕
 *   - 无符号运算：始终按位宽截断（div/mod 不支持 wrap 关键字，由 Analyzer 拒绝）
 *   - cast strict：检查值能否在目标类型中精确表示
 *   - cast truncate：按位模式截断、符号扩展或零扩展，不报错
 *
 * Executor 和 AOT RT 均委托此模块完成语义运算，保证行为一致。
 */
#include "tc_semantics.h"

#include "tc_diagnostic.h"

#include <fenv.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  位模式工具函数                                                       */
/* ------------------------------------------------------------------ */

uint64_t tc_mask_bits(int bit_width) {
    if (bit_width >= 64) {
        return UINT64_MAX;
    }
    return (1ULL << (unsigned)bit_width) - 1ULL;
}

int64_t tc_bits_to_signed(TcIntType type, uint64_t bits) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t masked = bits & mask;
    uint64_t sign_bit = 1ULL << (unsigned)(n - 1);

    if (masked & sign_bit) {
        if (n == 64) {
            return (int64_t)masked;
        }
        /* 非 64 位：手动做二补数符号扩展（减法） */
        return (int64_t)(masked - (1ULL << (unsigned)n));
    }
    return (int64_t)masked;
}

uint64_t tc_signed_to_bits(TcIntType type, int64_t value) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    return ((uint64_t)value) & mask;
}

uint64_t tc_value_to_unsigned(TcIntType type, uint64_t bits) {
    return bits & tc_mask_bits(tc_type_bit_width(type));
}

TcValue tc_value_make(TcIntType type, uint64_t bits) {
    TcValue value;
    /* 自动归一化 bits 到目标类型位宽：窄类型的高位被掩码清零，
     * 保证 TcValue 中 bits 的"脏高位"不会影响后续运算。 */
    value.type = type;
    value.bits = tc_value_to_unsigned(type, bits);
    return value;
}

void tc_slots_init_uninitialized(TcValue *slots, size_t count) {
    if (slots != NULL && count > 0) {
        memset(slots, TC_UNINITIALIZED_SLOT_BYTE, count * sizeof(TcValue));
    }
}

void tc_slot_bits_init_uninitialized(uint64_t *slots, size_t count) {
    size_t i = 0;
    if (slots == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        slots[i] = TC_UNINITIALIZED_SLOT_BITS;
    }
}

/* ------------------------------------------------------------------ */
/*  类型范围辅助函数（内部）                                              */
/* ------------------------------------------------------------------ */

/** 有符号类型的最小值：-(2^(n-1)) */
static int64_t tc_type_min_signed(TcIntType type) {
    int n = tc_type_bit_width(type);
    if (n == 64) {
        return INT64_MIN;
    }
    return -(1LL << (n - 1));
}

/** 有符号类型的最大值：2^(n-1) - 1 */
static int64_t tc_type_max_signed(TcIntType type) {
    int n = tc_type_bit_width(type);
    if (n == 64) {
        return INT64_MAX;
    }
    return (1LL << (n - 1)) - 1LL;
}

/** 无符号类型的最大值：2^n - 1 */
static uint64_t tc_type_max_unsigned(TcIntType type) {
    return tc_mask_bits(tc_type_bit_width(type));
}

/* ------------------------------------------------------------------ */
/*  字面量检查                                                           */
/* ------------------------------------------------------------------ */

int tc_literal_fits_type(uint64_t value, TcIntType type) {
    if (tc_type_is_signed(type)) {
        if (value > (uint64_t)INT64_MAX) {
            return 0;
        }
        return tc_signed_in_range((int64_t)value, type);
    }
    return tc_unsigned_in_range(value, type);
}

int tc_literal_fits_context(const TcLiteral *lit, TcIntType type, TcErrorKind *err_kind) {
    if (lit->is_float) {
        float f32 = 0.0f;

        if (!tc_type_is_float(type)) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_TYPE;
            }
            return 0;
        }
        if (lit->float32_suffix && type != TC_FLOAT32) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_TYPE;
            }
            return 0;
        }
        if (type == TC_FLOAT32) {
            f32 = (float)lit->float_value;
            if (lit->float_value > (double)FLT_MAX || lit->float_value < -(double)FLT_MAX) {
                if (err_kind) {
                    *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
                }
                return 0;
            }
            if (isinf(lit->float_value) && !isinf((double)f32)) {
                if (err_kind) {
                    *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
                }
                return 0;
            }
        }
        return 1;
    }

    if (lit->is_bool) {
        if (!tc_type_is_bool(type)) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_TYPE;
            }
            return 0;
        }
        return 1;
    }

    if (tc_type_is_bool(type)) {
        if (err_kind) {
            *err_kind = TC_ERR_LITERAL_TYPE;
        }
        return 0;
    }

    if (tc_type_is_float(type)) {
        if (err_kind) {
            *err_kind = TC_ERR_LITERAL_TYPE;
        }
        return 0;
    }

    if (lit->unsigned_suffix) {
        /* u 后缀的字面量不能用于有符号上下文 */
        if (tc_type_is_signed(type)) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_TYPE;
            }
            return 0;
        }
        if (!tc_unsigned_in_range(lit->magnitude, type)) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        return 1;
    }

    if (lit->negative) {
        /* 有负号的字面量不能用于无符号上下文 */
        if (!tc_type_is_signed(type)) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        if (lit->magnitude == TC_INT64_MIN_ABS_MAGNITUDE) {
            if (!tc_signed_in_range(INT64_MIN, type)) {
                if (err_kind) {
                    *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
                }
                return 0;
            }
            return 1;
        }
        if (lit->magnitude > (uint64_t)INT64_MAX) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        if (!tc_signed_in_range(-(int64_t)lit->magnitude, type)) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        return 1;
    }

    if (tc_type_is_signed(type)) {
        if (lit->magnitude > (uint64_t)INT64_MAX) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        if (!tc_signed_in_range((int64_t)lit->magnitude, type)) {
            if (err_kind) {
                *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        return 1;
    }

    if (!tc_unsigned_in_range(lit->magnitude, type)) {
        if (err_kind) {
            *err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
        }
        return 0;
    }
    return 1;
}

TcValue tc_literal_to_value(const TcLiteral *lit, TcIntType type) {
    if (lit->is_float) {
        if (type == TC_FLOAT32) {
            float f = (float)lit->float_value;
            uint32_t bits = 0;
            memcpy(&bits, &f, sizeof(f));
            return tc_value_make(TC_FLOAT32, (uint64_t)bits);
        }
        {
            double d = lit->float_value;
            uint64_t bits = 0;
            memcpy(&bits, &d, sizeof(d));
            return tc_value_make(TC_FLOAT64, bits);
        }
    }
    if (lit->is_bool) {
        return tc_value_make(TC_BOOL, lit->magnitude ? 1ULL : 0ULL);
    }
    if (lit->unsigned_suffix) {
        return tc_value_make(type, lit->magnitude);
    }
    if (lit->negative) {
        if (lit->magnitude == TC_INT64_MIN_ABS_MAGNITUDE) {
            return tc_value_make(type, tc_signed_to_bits(type, INT64_MIN));
        }
        return tc_value_make(type, tc_signed_to_bits(type, -(int64_t)lit->magnitude));
    }
    return tc_value_make(type, lit->magnitude);
}

/* ------------------------------------------------------------------ */
/*  范围检查                                                           */
/* ------------------------------------------------------------------ */

int tc_signed_in_range(int64_t value, TcIntType type) {
    return value >= tc_type_min_signed(type) && value <= tc_type_max_signed(type);
}

int tc_unsigned_in_range(uint64_t value, TcIntType type) {
    return value <= tc_type_max_unsigned(type);
}

/* ------------------------------------------------------------------ */
/*  int64 溢出检测辅助                                                   */
/* ------------------------------------------------------------------ */

/** 有符号加法溢出检测：若溢出返回 1，否则 *result = a + b */
static int tc_sadd_overflow(int64_t a, int64_t b, int64_t *result) {
    if (b > 0 && a > INT64_MAX - b) {
        return 1;
    }
    if (b < 0 && a < INT64_MIN - b) {
        return 1;
    }
    *result = a + b;
    return 0;
}

/** 有符号减法溢出检测：若溢出返回 1，否则 *result = a - b */
static int tc_ssub_overflow(int64_t a, int64_t b, int64_t *result) {
    if (b < 0 && a > INT64_MAX + b) {
        return 1;
    }
    if (b > 0 && a < INT64_MIN + b) {
        return 1;
    }
    *result = a - b;
    return 0;
}

/** 有符号乘法溢出检测：若溢出返回 1，否则 *result = a * b */
static int tc_smul_overflow(int64_t a, int64_t b, int64_t *result) {
    if (a == 0 || b == 0) {
        *result = 0;
        return 0;
    }
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) {
                return 1;
            }
        } else if (b < INT64_MIN / a) {
            return 1;
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) {
                return 1;
            }
        } else if (b != 0 && a < INT64_MAX / b) {
            return 1;
        }
    }
    *result = a * b;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  64 × 64 → 128 位分块乘法                                            */
/* ------------------------------------------------------------------ */

/*
 * 64 位无符号乘法，返回 128 位结果的 hi（高 64 位）和 lo（低 64 位）。
 *
 * 采用 32 位分块法：
 *   a * b = (a_hi·2^32 + a_lo)(b_hi·2^32 + b_lo)
 *          = a_lo·b_lo + (a_lo·b_hi + a_hi·b_lo)·2^32 + a_hi·b_hi·2^64
 *          = p0          + mid·2^32                      + p3·2^64
 *   lo = p0_lo + (mid_lo << 32)
 *   hi = p3 + mid_hi + p1_hi + p2_hi
 *
 * 用纯 C99 避免 __int128 编译器扩展，用于 int64/uint64 乘法位宽截断
 * 时只需 lo（wrap 模式）或 hi（移位溢出检测）。
 */
static void tc_umul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
    const uint64_t mask32 = 0xFFFFFFFFULL;
    uint64_t a_lo = a & mask32;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & mask32;
    uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t mid = p1 + p2 + (p0 >> 32);
    *lo = (p0 & mask32) | ((mid & mask32) << 32);
    *hi = p3 + (mid >> 32) + (p1 >> 32) + (p2 >> 32);
}

/* ------------------------------------------------------------------ */
/*  wrap 模式位宽截断辅助                                                */
/* ------------------------------------------------------------------ */

/** 将 bits 按目标类型位宽截断（wrap 模式下使用） */
static uint64_t tc_wrap_bits(TcIntType type, uint64_t bits) {
    return tc_value_to_unsigned(type, bits);
}

/* ------------------------------------------------------------------ */
/*  有符号算术运算                                                       */
/* ------------------------------------------------------------------ */

static int tc_exec_signed_arith(TcArithOp op, TcIntType type, TcWrapMode mode,
                                const TcValue *lhs, const TcValue *rhs, TcValue *out,
                                TcDiagnostic *diag, int line) {
    int64_t a = tc_bits_to_signed(type, lhs->bits);
    int64_t b = tc_bits_to_signed(type, rhs->bits);
    int64_t result = 0;
    char msg[128];

    /* div/mod：除零检查；INT_MIN/-1 时商不可表示（div 报错，§5.2 mod 仍为 0） */
    if (op == TC_DIV || op == TC_MOD) {
        if (b == 0) {
            tc_diagnostic_set(diag, TC_ERR_DIVISION_BY_ZERO, line, TC_COLUMN_UNKNOWN,
                              "division by zero");
            return -1;
        }
        if (a == tc_type_min_signed(type) && b == -1) {
            if (op == TC_MOD) {
                *out = tc_value_make(type, 0);
                return 0;
            }
            tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "signed division overflow");
            return -1;
        }
        if (op == TC_DIV) {
            result = a / b;
        } else {
            result = a % b;
        }
        /* div/mod 结果也可能超出窄类型范围（如 int8(-128) / int8(-1) = 128） */
        if (!tc_signed_in_range(result, type)) {
            snprintf(msg, sizeof(msg), "result out of range for %s", tc_int_type_name(type));
            tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        *out = tc_value_make(type, tc_signed_to_bits(type, result));
        return 0;
    }

    /* overflow 模式：在无符号位域上做环绕运算 */
    if (mode == TC_ARITH_WRAP) {
        int n = tc_type_bit_width(type);
        uint64_t mask = tc_mask_bits(n);
        uint64_t wrapped = 0;

        if (op == TC_ADD) {
            wrapped = (tc_value_to_unsigned(type, (uint64_t)a) +
                       tc_value_to_unsigned(type, (uint64_t)b)) &
                      mask;
        } else if (op == TC_SUB) {
            wrapped = (tc_value_to_unsigned(type, (uint64_t)a) -
                       tc_value_to_unsigned(type, (uint64_t)b)) &
                      mask;
        } else {
            /* 有符号乘法 wrap：64 位用 128 位乘法取低字；较窄类型直接乘后截断 */
            if (n == 64) {
                uint64_t hi = 0;
                uint64_t lo = 0;
                uint64_t ua = tc_value_to_unsigned(type, (uint64_t)a);
                uint64_t ub = tc_value_to_unsigned(type, (uint64_t)b);
                tc_umul64(ua, ub, &hi, &lo);
                wrapped = lo & mask;
            } else {
                int64_t wide = a * b;
                wrapped = tc_signed_to_bits(type, wide);
            }
        }
        *out = tc_value_make(type, wrapped);
        return 0;
    }

    /* strict 模式：检测 int64 运算溢出，再检查结果是否落在目标类型范围内 */
    if (op == TC_ADD) {
        if (tc_sadd_overflow(a, b, &result)) {
            tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "signed addition overflow");
            return -1;
        }
    } else if (op == TC_SUB) {
        if (tc_ssub_overflow(a, b, &result)) {
            tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "signed subtraction overflow");
            return -1;
        }
    } else if (tc_smul_overflow(a, b, &result)) {
        tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "signed multiplication overflow");
        return -1;
    }

    if (!tc_signed_in_range(result, type)) {
        snprintf(msg, sizeof(msg), "result out of range for %s", tc_int_type_name(type));
        tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN, msg);
        return -1;
    }

    *out = tc_value_make(type, tc_signed_to_bits(type, result));
    return 0;
}

/* ------------------------------------------------------------------ */
/*  无符号算术运算                                                       */
/* ------------------------------------------------------------------ */

/*
 * 无符号算术运算。
 * mode 参数仅用于签名兼容——无符号运算的截断语义等价于 wrap，
 * 无论传入什么模式都按位宽截断，所以入参被忽略（§5.1 语言标准）。
 */
static int tc_exec_unsigned_arith(TcArithOp op, TcIntType type, TcWrapMode mode,
                                    const TcValue *lhs, const TcValue *rhs, TcValue *out,
                                    TcDiagnostic *diag, int line) {
    (void)mode;
    uint64_t a = tc_value_to_unsigned(type, lhs->bits);
    uint64_t b = tc_value_to_unsigned(type, rhs->bits);
    uint64_t result = 0;

    if (op == TC_DIV || op == TC_MOD) {
        if (b == 0) {
            tc_diagnostic_set(diag, TC_ERR_DIVISION_BY_ZERO, line, TC_COLUMN_UNKNOWN,
                              "division by zero");
            return -1;
        }
        if (op == TC_DIV) {
            result = a / b;
        } else {
            result = a % b;
        }
        *out = tc_value_make(type, result);
        return 0;
    }

    if (op == TC_ADD) {
        result = a + b;
    } else if (op == TC_SUB) {
        result = a - b;
    } else {
        /* 无符号乘法：64 位用 128 位乘法取低字；较窄类型直接乘 */
        if (tc_type_bit_width(type) == 64) {
            uint64_t hi = 0;
            uint64_t lo = 0;
            tc_umul64(a, b, &hi, &lo);
            result = lo;
        } else {
            result = a * b;
        }
    }

    *out = tc_value_make(type, result);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  算术运算入口                                                        */
/* ------------------------------------------------------------------ */

int tc_exec_arith(TcArithOp op, TcIntType type, TcWrapMode mode,
                  const TcValue *lhs, const TcValue *rhs, TcValue *out,
                  TcDiagnostic *diag, int line) {
    if (tc_type_is_signed(type)) {
        return tc_exec_signed_arith(op, type, mode, lhs, rhs, out, diag, line);
    }
    return tc_exec_unsigned_arith(op, type, mode, lhs, rhs, out, diag, line);
}

/* ------------------------------------------------------------------ */
/*  单目运算：abs / neg                                                  */
/* ------------------------------------------------------------------ */

int tc_exec_unary(TcUnaryOp op, TcIntType type, TcWrapMode mode,
                  const TcValue *operand, TcValue *out,
                  TcDiagnostic *diag, int line) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t bits = tc_value_to_unsigned(type, operand->bits);

    if (op == TC_UNARY_ABS) {
        if (tc_type_is_signed(type)) {
            int64_t val = tc_bits_to_signed(type, bits);
            if (val == tc_type_min_signed(type)) {
                tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "abs(INT_MIN) overflow");
                return -1;
            }
            if (val < 0) {
                val = -val;
            }
            out->bits = (uint64_t)val & mask;
        } else {
            out->bits = bits;  /* 无符号 abs 无操作 */
        }
    } else {
        /* NEG */
        if (tc_type_is_signed(type) && mode == TC_ARITH_STRICT) {
            int64_t val = tc_bits_to_signed(type, bits);
            if (val == tc_type_min_signed(type)) {
                tc_diagnostic_set(diag, TC_ERR_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "neg(INT_MIN) overflow");
                return -1;
            }
            val = -val;
            out->bits = (uint64_t)val & mask;
        } else {
            /* wrap 模式或无符号：用二补数求反（~x + 1） */
            out->bits = (bits == 0) ? 0 : (mask ^ bits) + 1;
            out->bits &= mask;
        }
    }

    out->type = type;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  strict cast 子函数                                                   */
/* ------------------------------------------------------------------ */

/** cast 扩展：符号扩展或零扩展，带范围检查 */
static int tc_cast_widen(TcIntType target, TcIntType src_type, const TcValue *source,
                         TcValue *out, TcDiagnostic *diag, int line) {
    int src_signed = tc_type_is_signed(src_type);
    int dst_signed = tc_type_is_signed(target);

    if (src_signed && dst_signed) {
        int64_t value = tc_bits_to_signed(src_type, source->bits);
        *out = tc_value_make(target, tc_signed_to_bits(target, value));
        return 0;
    }
    if (!src_signed && !dst_signed) {
        uint64_t value = tc_value_to_unsigned(src_type, source->bits);
        *out = tc_value_make(target, value);
        return 0;
    }
    if (src_signed && !dst_signed) {
        int64_t value = tc_bits_to_signed(src_type, source->bits);
        if (value < 0) {
            tc_diagnostic_set(diag, TC_ERR_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "cannot cast negative signed value to unsigned");
            return -1;
        }
        *out = tc_value_make(target, (uint64_t)value);
        return 0;
    }
    /* 无符号 → 有符号（更宽） */
    {
        uint64_t value = tc_value_to_unsigned(src_type, source->bits);
        if (value > (uint64_t)tc_type_max_signed(target)) {
            tc_diagnostic_set(diag, TC_ERR_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "unsigned value out of signed target range");
            return -1;
        }
        *out = tc_value_make(target, tc_signed_to_bits(target, (int64_t)value));
        return 0;
    }
}

/** cast 同位宽转换（bit width 相同但符号性不同）：检查负值或超范围 */
static int tc_cast_same_width_diff_sign(TcIntType target, TcIntType src_type,
                                        const TcValue *source, TcValue *out,
                                        TcDiagnostic *diag, int line) {
    int src_signed = tc_type_is_signed(src_type);

    if (src_signed) {
        int64_t value = tc_bits_to_signed(src_type, source->bits);
        if (value < 0) {
            tc_diagnostic_set(diag, TC_ERR_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "cannot cast negative signed value to unsigned");
            return -1;
        }
        *out = tc_value_make(target, (uint64_t)value);
        return 0;
    }
    /* 无符号 → 有符号（同宽） */
    {
        uint64_t value = tc_value_to_unsigned(src_type, source->bits);
        if (value > (uint64_t)tc_type_max_signed(target)) {
            tc_diagnostic_set(diag, TC_ERR_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "unsigned value out of signed target range");
            return -1;
        }
        *out = tc_value_make(target, tc_signed_to_bits(target, (int64_t)value));
        return 0;
    }
}

/** cast 窄化：检查值能否在更窄的类型中精确表示 */
static int tc_cast_narrow(TcIntType target, TcIntType src_type, const TcValue *source,
                          TcValue *out, TcDiagnostic *diag, int line) {
    int src_signed = tc_type_is_signed(src_type);
    int dst_signed = tc_type_is_signed(target);
    char msg[128];

    /* 有符号 → 有符号 */
    if (src_signed && dst_signed) {
        int64_t value = tc_bits_to_signed(src_type, source->bits);
        if (!tc_signed_in_range(value, target)) {
            snprintf(msg, sizeof(msg), "value out of range for %s", tc_int_type_name(target));
            tc_diagnostic_set(diag, TC_ERR_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        *out = tc_value_make(target, tc_signed_to_bits(target, value));
        return 0;
    }

    /* 无符号 → 无符号（截断后在 strict 模式下仍合法） */
    if (!src_signed && !dst_signed) {
        uint64_t value = tc_value_to_unsigned(src_type, source->bits);
        *out = tc_value_make(target, tc_wrap_bits(target, value));
        return 0;
    }

    /* 有符号 → 无符号 */
    if (src_signed && !dst_signed) {
        int64_t value = tc_bits_to_signed(src_type, source->bits);
        if (value < 0 || (uint64_t)value > tc_type_max_unsigned(target)) {
            tc_diagnostic_set(diag, TC_ERR_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "signed value cannot be represented as unsigned target");
            return -1;
        }
        *out = tc_value_make(target, (uint64_t)value);
        return 0;
    }

    /* 无符号 → 有符号 */
    {
        uint64_t value = tc_value_to_unsigned(src_type, source->bits);
        if (value > (uint64_t)tc_type_max_signed(target)) {
            tc_diagnostic_set(diag, TC_ERR_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "unsigned value out of signed target range");
            return -1;
        }
        *out = tc_value_make(target, tc_signed_to_bits(target, (int64_t)value));
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  strict cast 入口：按位宽和符号性分派子函数                             */
/* ------------------------------------------------------------------ */

static int tc_cast_strict(TcIntType target, const TcValue *source, TcValue *out,
                          TcDiagnostic *diag, int line) {
    TcIntType src_type = source->type;

    if (tc_type_is_bool(src_type) && tc_type_is_bool(target)) {
        *out = *source;
        return 0;
    }
    if (tc_type_is_bool(src_type) && tc_type_is_integer(target)) {
        *out = tc_value_make(target, source->bits != 0 ? 1ULL : 0ULL);
        return 0;
    }
    if (tc_type_is_integer(src_type) && tc_type_is_bool(target)) {
        *out = tc_value_make(TC_BOOL, source->bits != 0 ? 1ULL : 0ULL);
        return 0;
    }

    {
    TcIntType src_type_inner = source->type;
    int src_bits = tc_type_bit_width(src_type_inner);
    int dst_bits = tc_type_bit_width(target);

    if (src_type_inner == target) {
        *out = *source;
        return 0;
    }

    if (dst_bits > src_bits) {
        return tc_cast_widen(target, src_type_inner, source, out, diag, line);
    }

    if (dst_bits == src_bits) {
        return tc_cast_same_width_diff_sign(target, src_type_inner, source, out, diag, line);
    }

    return tc_cast_narrow(target, src_type_inner, source, out, diag, line);
    }
}

/* ------------------------------------------------------------------ */
/*  truncate cast 辅助: 位扩展                                           */
/* ------------------------------------------------------------------ */

/*
 * @brief 位扩展：将 src_bits 宽的位模式扩展到 dst_bits
 * @param bits         原始位模式
 * @param src_bits     源位宽
 * @param dst_bits     目标位宽（必须 >= src_bits 才实际扩展）
 * @param sign_extend  符号扩展标志：1 符号扩展，0 零扩展
 * @return 扩展后的位模式
 */
static uint64_t tc_extend_bits(uint64_t bits, int src_bits, int dst_bits, int sign_extend) {
    uint64_t mask = tc_mask_bits(src_bits);
    bits &= mask;
    if (dst_bits <= src_bits) {
        return bits;
    }
    if (!sign_extend) {
        return bits;
    }
    /* 若源符号位为 1，高位全填 1（从 src_bits 到 dst_bits-1） */
    if (bits & (1ULL << (unsigned)(src_bits - 1))) {
        uint64_t extend_mask = (~tc_mask_bits(src_bits)) & tc_mask_bits(dst_bits);
        return bits | extend_mask;
    }
    return bits;
}

/* ------------------------------------------------------------------ */
/*  truncate cast：不做范围检查，直接按位模式转换                           */
/* ------------------------------------------------------------------ */

static int tc_cast_truncate(TcIntType target, const TcValue *source, TcValue *out) {
    TcIntType src_type = source->type;

    if (tc_type_is_bool(src_type) || tc_type_is_bool(target)) {
        if (tc_type_is_bool(target)) {
            *out = tc_value_make(TC_BOOL, source->bits != 0 ? 1ULL : 0ULL);
        } else {
            *out = tc_value_make(target, source->bits != 0 ? 1ULL : 0ULL);
        }
        return 0;
    }

    {
    int src_bits = tc_type_bit_width(src_type);
    int dst_bits = tc_type_bit_width(target);
    uint64_t bits = tc_value_to_unsigned(src_type, source->bits);
    uint64_t result_bits = 0;

    if (dst_bits <= src_bits) {
        /* 窄化：直接截低 n 位 */
        result_bits = bits & tc_mask_bits(dst_bits);
    } else if (!tc_type_is_signed(target) || !tc_type_is_signed(src_type)) {
        /* 目标或源为无符号 → 零扩展 */
        result_bits = tc_extend_bits(bits, src_bits, dst_bits, 0);
    } else if (bits & (1ULL << (unsigned)(src_bits - 1))) {
        /* 有符号 → 有符号，源符号位为 1 → 符号扩展 */
        result_bits = tc_extend_bits(bits, src_bits, dst_bits, 1);
    } else {
        result_bits = tc_extend_bits(bits, src_bits, dst_bits, 0);
    }

    *out = tc_value_make(target, result_bits);
    return 0;
    }
}

int tc_exec_compare(TcCompareOp op, TcIntType type, const TcValue *lhs, const TcValue *rhs,
                    TcValue *out, TcDiagnostic *diag, int line) {
    int n = tc_type_bit_width(type);
    int result = 0;

    (void)diag;
    (void)line;

    if (tc_type_is_signed(type)) {
        int64_t a = tc_bits_to_signed(type, lhs->bits & tc_mask_bits(n));
        int64_t b = tc_bits_to_signed(type, rhs->bits & tc_mask_bits(n));
        switch (op) {
        case TC_CMP_EQ:
            result = (a == b);
            break;
        case TC_CMP_NE:
            result = (a != b);
            break;
        case TC_CMP_LT:
            result = (a < b);
            break;
        case TC_CMP_LE:
            result = (a <= b);
            break;
        case TC_CMP_GT:
            result = (a > b);
            break;
        case TC_CMP_GE:
            result = (a >= b);
            break;
        }
    } else {
        uint64_t a = lhs->bits & tc_mask_bits(n);
        uint64_t b = rhs->bits & tc_mask_bits(n);
        switch (op) {
        case TC_CMP_EQ:
            result = (a == b);
            break;
        case TC_CMP_NE:
            result = (a != b);
            break;
        case TC_CMP_LT:
            result = (a < b);
            break;
        case TC_CMP_LE:
            result = (a <= b);
            break;
        case TC_CMP_GT:
            result = (a > b);
            break;
        case TC_CMP_GE:
            result = (a >= b);
            break;
        }
    }

    *out = tc_value_make(TC_BOOL, result ? 1ULL : 0ULL);
    return 0;
}

int tc_exec_logic_binary(TcLogicOp op, const TcValue *lhs, const TcValue *rhs, TcValue *out,
                         TcDiagnostic *diag, int line) {
    int lhs_true = lhs->bits != 0;

    (void)diag;
    (void)line;

    if (op == TC_LOGIC_AND) {
        if (!lhs_true) {
            *out = tc_value_make(TC_BOOL, 0);
            return 0;
        }
        *out = tc_value_make(TC_BOOL, rhs->bits != 0 ? 1ULL : 0ULL);
        return 0;
    }

    if (lhs_true) {
        *out = tc_value_make(TC_BOOL, 1);
        return 0;
    }
    *out = tc_value_make(TC_BOOL, rhs->bits != 0 ? 1ULL : 0ULL);
    return 0;
}

int tc_exec_logic_unary(TcLogicOp op, const TcValue *operand, TcValue *out,
                        TcDiagnostic *diag, int line) {
    (void)op;
    (void)diag;
    (void)line;
    *out = tc_value_make(TC_BOOL, operand->bits == 0 ? 1ULL : 0ULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  按位运算                                                            */
/* ------------------------------------------------------------------ */

static int tc_check_operand_types(TcIntType type, const TcValue *a, const TcValue *b,
                                  TcDiagnostic *diag, int line) {
    if (a->type != type || (b != NULL && b->type != type)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "operand type does not match operation type");
        return -1;
    }
    return 0;
}

int tc_exec_bitwise_binary(TcBitwiseOp op, TcIntType type,
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

int tc_exec_bitwise_unary(TcIntType type, const TcValue *operand, TcValue *out,
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

static int tc_shift_count(TcIntType type, const TcValue *count, uint64_t *k_out) {
    *k_out = tc_value_to_unsigned(type, count->bits);
    return 0;
}

static int tc_exec_shl(TcIntType type, TcWrapMode mode, const TcValue *value,
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
static int tc_exec_shr(TcIntType type, const TcValue *value, uint64_t k, TcValue *out) {
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

int tc_exec_shift(TcShiftOp op, TcIntType type, TcWrapMode mode,
                  const TcValue *value, const TcValue *count, TcValue *out,
                  TcDiagnostic *diag, int line) {
    uint64_t k = 0;

    if (value->type != type || count->type != type) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "operand type does not match operation type");
        return -1;
    }

    if (op == TC_SHIFT_SHR) {
        (void)mode;  /* shr 恒为 strict，传参仅签名对齐 */
    }

    tc_shift_count(type, count, &k);

    if (op == TC_SHIFT_SHL) {
        return tc_exec_shl(type, mode, value, k, out, diag, line);
    }
    return tc_exec_shr(type, value, k, out);
}

/* ------------------------------------------------------------------ */
/*  浮点算术（strict / ieee 骨架）                                      */
/* ------------------------------------------------------------------ */

static double tc_fp_bits_to_double(TcIntType type, uint64_t bits) {
    if (type == TC_FLOAT32) {
        float f = 0.0f;
        uint32_t b32 = (uint32_t)(bits & 0xFFFFFFFFu);
        memcpy(&f, &b32, sizeof(f));
        return (double)f;
    }
    {
        double d = 0.0;
        memcpy(&d, &bits, sizeof(d));
        return d;
    }
}

static uint64_t tc_fp_double_to_bits(TcIntType type, double value) {
    if (type == TC_FLOAT32) {
        float f = (float)value;
        uint32_t b32 = 0;
        memcpy(&b32, &f, sizeof(b32));
        return (uint64_t)b32;
    }
    {
        double d = value;
        uint64_t b64 = 0;
        memcpy(&b64, &d, sizeof(b64));
        return b64;
    }
}

static int tc_fp_strict_check_nan_operand(double value, TcDiagnostic *diag, int line) {
    if (isnan(value)) {
        tc_diagnostic_set(diag, TC_ERR_FLOAT_INVALID, line, TC_COLUMN_UNKNOWN,
                          "float invalid operation");
        return -1;
    }
    return 0;
}

static int tc_fp_strict_check_exceptions(double result, TcDiagnostic *diag, int line) {
    if (fetestexcept(FE_INVALID)) {
        tc_diagnostic_set(diag, TC_ERR_FLOAT_INVALID, line, TC_COLUMN_UNKNOWN,
                          "float invalid operation");
        return -1;
    }
    if (fetestexcept(FE_DIVBYZERO)) {
        tc_diagnostic_set(diag, TC_ERR_DIVISION_BY_ZERO, line, TC_COLUMN_UNKNOWN,
                          "division by zero");
        return -1;
    }
    if (fetestexcept(FE_OVERFLOW)) {
        tc_diagnostic_set(diag, TC_ERR_FLOAT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "float overflow");
        return -1;
    }
    if (fetestexcept(FE_UNDERFLOW)) {
        if (result != 0.0 && !isnan(result)) {
            tc_diagnostic_set(diag, TC_ERR_FLOAT_UNDERFLOW, line, TC_COLUMN_UNKNOWN,
                              "float underflow");
            return -1;
        }
    }
    return 0;
}

static int tc_exec_fp_arith_strict(TcArithOp op, TcIntType type,
                                   double lhs, double rhs, double *result,
                                   TcDiagnostic *diag, int line) {
    (void)type;
    if (tc_fp_strict_check_nan_operand(lhs, diag, line) != 0) {
        return -1;
    }
    if (tc_fp_strict_check_nan_operand(rhs, diag, line) != 0) {
        return -1;
    }

    feclearexcept(FE_ALL_EXCEPT);
    switch (op) {
    case TC_ADD:
        *result = lhs + rhs;
        break;
    case TC_SUB:
        *result = lhs - rhs;
        break;
    case TC_MUL:
        *result = lhs * rhs;
        break;
    case TC_DIV:
        *result = lhs / rhs;
        break;
    case TC_MOD:
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "mod not supported for float types");
        return -1;
    }
    return tc_fp_strict_check_exceptions(*result, diag, line);
}

static int tc_exec_fp_arith_ieee(TcArithOp op, double lhs, double rhs, double *result,
                                 TcDiagnostic *diag, int line) {
    (void)diag;
    (void)line;
    switch (op) {
    case TC_ADD:
        *result = lhs + rhs;
        break;
    case TC_SUB:
        *result = lhs - rhs;
        break;
    case TC_MUL:
        *result = lhs * rhs;
        break;
    case TC_DIV:
        *result = lhs / rhs;
        break;
    case TC_MOD:
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "mod not supported for float types");
        return -1;
    }
    return 0;
}

static int tc_exec_fp_arith_wrap(TcArithOp op, TcIntType type, uint64_t lhs_bits, uint64_t rhs_bits,
                                 uint64_t *result_bits, TcDiagnostic *diag, int line) {
    int width = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(width);
    uint64_t a = lhs_bits & mask;
    uint64_t b = rhs_bits & mask;
    uint64_t result = 0;

    (void)diag;
    (void)line;
    switch (op) {
    case TC_ADD:
        result = (a + b) & mask;
        break;
    case TC_SUB:
        result = (a - b) & mask;
        break;
    case TC_MUL:
        if (width >= 64) {
            result = a * b;
        } else {
            result = (a * b) & mask;
        }
        break;
    case TC_DIV:
        if (b == 0) {
            tc_diagnostic_set(diag, TC_ERR_DIVISION_BY_ZERO, line, TC_COLUMN_UNKNOWN,
                              "division by zero");
            return -1;
        }
        result = a / b;
        break;
    case TC_MOD:
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "mod not supported for float types");
        return -1;
    }
    *result_bits = result & mask;
    return 0;
}

static int tc_exec_fp_unary_wrap(TcUnaryOp op, TcIntType type, uint64_t operand_bits,
                                 uint64_t *result_bits, TcDiagnostic *diag, int line) {
    int width = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(width);
    uint64_t bits = operand_bits & mask;
    uint64_t sign_mask = (width == 32) ? 0x7FFFFFFFu : 0x7FFFFFFFFFFFFFFFULL;

    (void)diag;
    (void)line;
    switch (op) {
    case TC_UNARY_ABS:
        *result_bits = bits & sign_mask;
        return 0;
    case TC_UNARY_NEG:
        *result_bits = (0 - bits) & mask;
        return 0;
    }
    return -1;
}

static int tc_fp_compare_result(TcCompareOp op, double lhs, double rhs, int *result) {
    if (isnan(lhs) || isnan(rhs)) {
        if (op == TC_CMP_NE) {
            *result = 1;
        } else {
            *result = 0;
        }
        return 0;
    }
    switch (op) {
    case TC_CMP_EQ:
        *result = (lhs == rhs);
        break;
    case TC_CMP_NE:
        *result = (lhs != rhs);
        break;
    case TC_CMP_LT:
        *result = (lhs < rhs);
        break;
    case TC_CMP_LE:
        *result = (lhs <= rhs);
        break;
    case TC_CMP_GT:
        *result = (lhs > rhs);
        break;
    case TC_CMP_GE:
        *result = (lhs >= rhs);
        break;
    }
    return 0;
}

static int tc_fp_cast_strict(TcIntType target, const TcValue *source, TcValue *out,
                             TcDiagnostic *diag, int line) {
    TcIntType src_type = source->type;

    if (tc_type_is_bool(src_type) && tc_type_is_bool(target)) {
        *out = *source;
        return 0;
    }
    if (tc_type_is_bool(src_type)) {
        double val = source->bits != 0 ? 1.0 : 0.0;
        if (tc_type_is_float(target)) {
            out->type = target;
            out->bits = tc_fp_double_to_bits(target, val);
            return 0;
        }
        if (tc_type_is_integer(target)) {
            *out = tc_value_make(target, source->bits != 0 ? 1ULL : 0ULL);
            return 0;
        }
    }
    if (tc_type_is_bool(target)) {
        if (tc_type_is_float(src_type)) {
            double val = tc_fp_bits_to_double(src_type, source->bits);
            *out = tc_value_make(TC_BOOL, (val != 0.0 || isnan(val)) ? 1ULL : 0ULL);
            return 0;
        }
        *out = tc_value_make(TC_BOOL, source->bits != 0 ? 1ULL : 0ULL);
        return 0;
    }

    if (tc_type_is_integer(src_type) && tc_type_is_float(target)) {
        double val = 0.0;
        if (tc_type_is_signed(src_type)) {
            val = (double)tc_bits_to_signed(src_type, source->bits);
        } else {
            val = (double)tc_value_to_unsigned(src_type, source->bits);
        }
        if (target == TC_FLOAT32) {
            float f = (float)val;
            if (isinf(val) && !isinf((double)f)) {
                tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "float cast overflow");
                return -1;
            }
            if ((double)f != val && !isinf(val) && val != 0.0) {
                /* 整数在 float32 可精确表示范围内则 OK */
                if (val > (double)FLT_MAX || val < -(double)FLT_MAX) {
                    tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                      "float cast overflow");
                    return -1;
                }
            }
        } else if (val > (double)DBL_MAX || val < -(double)DBL_MAX) {
            tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "float cast overflow");
            return -1;
        }
        out->type = target;
        out->bits = tc_fp_double_to_bits(target, val);
        return 0;
    }

    if (tc_type_is_float(src_type) && tc_type_is_integer(target)) {
        double val = tc_fp_bits_to_double(src_type, source->bits);
        if (isnan(val) || isinf(val)) {
            tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "float cast overflow");
            return -1;
        }
        if (tc_type_is_signed(target)) {
            int64_t ival = (int64_t)val;
            if ((double)ival != val) {
                tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "float cast overflow");
                return -1;
            }
            if (!tc_signed_in_range(ival, target)) {
                tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "float cast overflow");
                return -1;
            }
            *out = tc_value_make(target, tc_signed_to_bits(target, ival));
            return 0;
        }
        {
            uint64_t ival = (uint64_t)val;
            if ((double)ival != val || val < 0.0) {
                tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "float cast overflow");
                return -1;
            }
            if (!tc_unsigned_in_range(ival, target)) {
                tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "float cast overflow");
                return -1;
            }
            *out = tc_value_make(target, ival);
            return 0;
        }
    }

    if (tc_type_is_float(src_type) && tc_type_is_float(target)) {
        double val = tc_fp_bits_to_double(src_type, source->bits);
        if (target == TC_FLOAT32 && src_type == TC_FLOAT64) {
            float f = (float)val;
            if (isinf(val) && !isinf((double)f)) {
                tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "float cast overflow");
                return -1;
            }
            if (val > (double)FLT_MAX || val < -(double)FLT_MAX) {
                tc_diagnostic_set(diag, TC_ERR_FLOAT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "float cast overflow");
                return -1;
            }
        }
        out->type = target;
        out->bits = tc_fp_double_to_bits(target, val);
        return 0;
    }

    tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN, "incompatible cast types");
    return -1;
}

static int tc_fp_cast_truncate(TcIntType target, const TcValue *source, TcValue *out,
                               TcDiagnostic *diag, int line) {
    TcIntType src_type = source->type;
    int src_bits = tc_type_bit_width(src_type);
    int dst_bits = tc_type_bit_width(target);
    uint64_t bits = source->bits;

    if (tc_type_is_bool(src_type) || tc_type_is_bool(target)) {
        TcDiagnostic tmp;
        tc_diagnostic_init(&tmp);
        if (tc_fp_cast_strict(target, source, out, &tmp, 0) != 0) {
            tc_diagnostic_clear(&tmp);
            if (diag) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "incompatible cast types for truncate mode");
            }
            return -1;
        }
        tc_diagnostic_clear(&tmp);
        return 0;
    }

    if (src_bits == dst_bits && tc_type_is_integer(src_type) && tc_type_is_float(target)) {
        out->type = target;
        out->bits = bits & tc_mask_bits(dst_bits);
        return 0;
    }
    if (src_bits == dst_bits && tc_type_is_float(src_type) && tc_type_is_integer(target)) {
        out->type = target;
        out->bits = bits & tc_mask_bits(dst_bits);
        return 0;
    }
    if (src_bits == dst_bits && tc_type_is_float(src_type) && tc_type_is_float(target)) {
        out->type = target;
        out->bits = bits & tc_mask_bits(dst_bits);
        return 0;
    }
    if (src_type == TC_FLOAT64 && target == TC_FLOAT32) {
        out->type = TC_FLOAT32;
        out->bits = bits & 0xFFFFFFFFu;
        return 0;
    }
    if (src_type == TC_FLOAT32 && target == TC_FLOAT64) {
        out->type = TC_FLOAT64;
        out->bits = bits & 0xFFFFFFFFu;
        return 0;
    }

    if (diag) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "incompatible cast types for truncate mode");
    }
    return -1;
}

int tc_exec_fp_arith(TcArithOp op, TcIntType type, TcFloatMode mode,
                     const TcValue *lhs, const TcValue *rhs, TcValue *out,
                     TcDiagnostic *diag, int line) {
    double a = 0.0;
    double b = 0.0;
    double result = 0.0;
    int rc = 0;

    if (!tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "expected float type");
        return -1;
    }

    a = tc_fp_bits_to_double(type, lhs->bits);
    b = tc_fp_bits_to_double(type, rhs->bits);

    if (mode == TC_FLOAT_STRICT) {
        rc = tc_exec_fp_arith_strict(op, type, a, b, &result, diag, line);
    } else if (mode == TC_FLOAT_IEEE) {
        rc = tc_exec_fp_arith_ieee(op, a, b, &result, diag, line);
    } else {
        uint64_t result_bits = 0;
        rc = tc_exec_fp_arith_wrap(op, type, lhs->bits, rhs->bits, &result_bits, diag, line);
        if (rc == 0) {
            out->type = type;
            out->bits = result_bits;
            return 0;
        }
        return -1;
    }

    if (rc != 0) {
        return -1;
    }

    out->type = type;
    out->bits = tc_fp_double_to_bits(type, result);
    return 0;
}

static int tc_exec_fp_unary_strict(TcUnaryOp op, double operand, double *result,
                                   TcDiagnostic *diag, int line) {
    if (tc_fp_strict_check_nan_operand(operand, diag, line) != 0) {
        return -1;
    }
    feclearexcept(FE_ALL_EXCEPT);
    switch (op) {
    case TC_UNARY_ABS:
        *result = fabs(operand);
        break;
    case TC_UNARY_NEG:
        *result = -operand;
        break;
    }
    return tc_fp_strict_check_exceptions(*result, diag, line);
}

static int tc_exec_fp_unary_ieee(TcUnaryOp op, double operand, double *result) {
    switch (op) {
    case TC_UNARY_ABS:
        *result = fabs(operand);
        break;
    case TC_UNARY_NEG:
        *result = -operand;
        break;
    }
    return 0;
}

int tc_exec_fp_unary(TcUnaryOp op, TcIntType type, TcFloatMode mode,
                     const TcValue *operand, TcValue *out,
                     TcDiagnostic *diag, int line) {
    double val = 0.0;
    double result = 0.0;
    int rc = 0;

    if (!tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "expected float type");
        return -1;
    }

    val = tc_fp_bits_to_double(type, operand->bits);

    if (mode == TC_FLOAT_STRICT) {
        rc = tc_exec_fp_unary_strict(op, val, &result, diag, line);
    } else if (mode == TC_FLOAT_IEEE) {
        rc = tc_exec_fp_unary_ieee(op, val, &result);
    } else {
        uint64_t result_bits = 0;
        rc = tc_exec_fp_unary_wrap(op, type, operand->bits, &result_bits, diag, line);
        if (rc == 0) {
            out->type = type;
            out->bits = result_bits;
            return 0;
        }
        return -1;
    }

    if (rc != 0) {
        return -1;
    }

    out->type = type;
    out->bits = tc_fp_double_to_bits(type, result);
    return 0;
}

int tc_exec_fp_compare(TcCompareOp op, TcIntType type, TcFloatMode mode,
                       const TcValue *lhs, const TcValue *rhs, TcValue *out,
                       TcDiagnostic *diag, int line) {
    double a = 0.0;
    double b = 0.0;
    int cmp_result = 0;

    (void)mode;
    if (!tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "expected float type");
        return -1;
    }

    a = tc_fp_bits_to_double(type, lhs->bits);
    b = tc_fp_bits_to_double(type, rhs->bits);

    if (tc_fp_compare_result(op, a, b, &cmp_result) != 0) {
        return -1;
    }

    out->type = TC_BOOL;
    out->bits = cmp_result ? 1ULL : 0ULL;
    return 0;
}

int tc_exec_fp_cast(TcIntType target, TcTruncateMode mode, const TcValue *source,
                    TcValue *out, TcDiagnostic *diag, int line) {
    if (mode == TC_TRUNC_STRICT) {
        return tc_fp_cast_strict(target, source, out, diag, line);
    }
    return tc_fp_cast_truncate(target, source, out, diag, line);
}

/* ------------------------------------------------------------------ */
/*  cast 运算入口                                                        */
/* ------------------------------------------------------------------ */

int tc_exec_cast(TcIntType target, TcTruncateMode mode, const TcValue *source,
                 TcValue *out, TcDiagnostic *diag, int line) {
    if (mode == TC_TRUNC_STRICT) {
        return tc_cast_strict(target, source, out, diag, line);
    }
    return tc_cast_truncate(target, source, out);
}
