/*
 * tc_semantics.c — 语义共享工具、字面量检查、比较/逻辑与未初始化哨兵
 *
 * 整数算术见 tc_sem_int.c；浮点见 tc_sem_fp.c；位运算/移位见 tc_sem_bitwise.c。
 * Executor 和 AOT RT 均经 tc_semantics.h 委托，保证行为一致。
 */
#include "tc_semantics.h"

#include "tc_diagnostic.h"

#ifdef TC_HAVE_FENV
#include <fenv.h>
#endif
#include <float.h>
#include <limits.h>
#include <math.h>
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

int64_t tc_bits_to_signed(TcTypeTag type, uint64_t bits) {
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

uint64_t tc_signed_to_bits(TcTypeTag type, int64_t value) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    return ((uint64_t)value) & mask;
}

uint64_t tc_value_to_unsigned(TcTypeTag type, uint64_t bits) {
    return bits & tc_mask_bits(tc_type_bit_width(type));
}

TcValue tc_value_make(TcTypeTag type, uint64_t bits) {
    TcValue value;
    /* 自动归一化 bits 到目标类型位宽：窄类型的高位被掩码清零，
     * 保证 TcValue 中 bits 的"脏高位"不会影响后续运算。 */
    value.type = tc_type_tag_singleton(type);
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
/*  §5.1 操作 × 类型 × 模式矩阵                                        */
/* ------------------------------------------------------------------ */

static int tc_sem_mode_error(TcDiagnostic *diag, int line, const char *message) {
    tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN, message);
    return -1;
}

int tc_validate_arith_mode(TcArithOp op, TcTypeTag type, TcWrapMode mode,
                           TcDiagnostic *diag, int line) {
    if (tc_type_is_bool(type) || tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "integer arithmetic requires integer type");
        return -1;
    }
    if (mode != TC_ARITH_WRAP) {
        return 0;
    }
    if (!tc_type_is_signed(type)) {
        return tc_sem_mode_error(diag, line,
                                 "unsigned arithmetic does not accept wrap mode");
    }
    if (op == TC_DIV || op == TC_MOD) {
        return tc_sem_mode_error(diag, line, "div/mod do not support wrap mode");
    }
    return 0;
}

int tc_validate_unary_mode(TcUnaryOp op, TcTypeTag type, TcWrapMode mode,
                           TcDiagnostic *diag, int line) {
    if (tc_type_is_bool(type) || tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "integer unary operation requires integer type");
        return -1;
    }
    if (mode != TC_ARITH_WRAP) {
        return 0;
    }
    if (op != TC_UNARY_NEG) {
        return tc_sem_mode_error(diag, line, "abs does not support wrap mode");
    }
    if (!tc_type_is_signed(type)) {
        return tc_sem_mode_error(diag, line,
                                 "unsigned unary operation does not accept wrap mode");
    }
    return 0;
}

int tc_validate_shift_mode(TcShiftOp op, TcTypeTag type, TcWrapMode mode,
                           TcDiagnostic *diag, int line) {
    if (tc_type_is_bool(type) || tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "shift operation requires integer type");
        return -1;
    }
    if (mode == TC_ARITH_WRAP && op != TC_SHIFT_SHL) {
        return tc_sem_mode_error(diag, line, "shift right does not support wrap mode");
    }
    return 0;
}

int tc_validate_fp_arith_mode(TcArithOp op, TcTypeTag type, TcFloatMode mode,
                              TcDiagnostic *diag, int line) {
    if (!tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "expected float type");
        return -1;
    }
    if (op == TC_MOD) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "mod not supported for float types");
        return -1;
    }
    if (mode == TC_FLOAT_WRAP) {
        return tc_sem_mode_error(diag, line,
                                 "wrap mode is not allowed for float arithmetic");
    }
    return 0;
}

int tc_validate_fp_unary_mode(TcUnaryOp op, TcTypeTag type, TcFloatMode mode,
                              TcDiagnostic *diag, int line) {
    (void)op;
    if (!tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "expected float type");
        return -1;
    }
    if (mode != TC_FLOAT_STRICT) {
        return tc_sem_mode_error(diag, line,
                                 "float unary operations do not accept mode keywords");
    }
    return 0;
}

int tc_validate_fp_compare_mode(TcTypeTag type, TcFloatMode mode,
                                TcDiagnostic *diag, int line) {
    if (!tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "expected float type");
        return -1;
    }
    if (mode != TC_FLOAT_STRICT) {
        return tc_sem_mode_error(diag, line,
                                 "float comparisons do not accept mode keywords");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  类型范围辅助函数（内部）                                              */
/* ------------------------------------------------------------------ */

/** 有符号类型的最小值：-(2^(n-1)) */
int64_t tc_type_min_signed(TcTypeTag type) {
    int n = tc_type_bit_width(type);
    if (n == 64) {
        return INT64_MIN;
    }
    return -(1LL << (n - 1));
}

/** 有符号类型的最大值：2^(n-1) - 1 */
int64_t tc_type_max_signed(TcTypeTag type) {
    int n = tc_type_bit_width(type);
    if (n == 64) {
        return INT64_MAX;
    }
    return (1LL << (n - 1)) - 1LL;
}

/** 无符号类型的最大值：2^n - 1 */
uint64_t tc_type_max_unsigned(TcTypeTag type) {
    return tc_mask_bits(tc_type_bit_width(type));
}

/* ------------------------------------------------------------------ */
/*  字面量检查                                                           */
/* ------------------------------------------------------------------ */

int tc_literal_fits_type(uint64_t value, TcTypeTag type) {
    if (tc_type_is_signed(type)) {
        if (value > (uint64_t)INT64_MAX) {
            return 0;
        }
        return tc_signed_in_range((int64_t)value, type);
    }
    return tc_unsigned_in_range(value, type);
}

int tc_literal_fits_context(const TcLiteral *lit, TcTypeTag type, TcErrorKind *err_kind) {
    if (lit->is_float) {
        float f32 = 0.0f;

        if (!tc_type_is_float(type)) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_TYPE;
            }
            return 0;
        }
        if (lit->float32_suffix && type != TC_FLOAT32) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_TYPE;
            }
            return 0;
        }
        if (type == TC_FLOAT32) {
            f32 = (float)lit->float_value;
            if (isfinite(lit->float_value) &&
                (lit->float_value > (double)FLT_MAX ||
                 lit->float_value < -(double)FLT_MAX)) {
                if (err_kind) {
                    *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
                }
                return 0;
            }
            if (isinf(lit->float_value) && !isinf((double)f32)) {
                if (err_kind) {
                    *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
                }
                return 0;
            }
        }
        return 1;
    }

    if (lit->is_bool) {
        if (!tc_type_is_bool(type)) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_TYPE;
            }
            return 0;
        }
        return 1;
    }

    if (tc_type_is_bool(type)) {
        if (err_kind) {
            *err_kind = TC_CE_LITERAL_TYPE;
        }
        return 0;
    }

    if (tc_type_is_float(type)) {
        if (err_kind) {
            *err_kind = TC_CE_LITERAL_TYPE;
        }
        return 0;
    }

    if (lit->unsigned_suffix) {
        /* u 后缀的字面量不能用于有符号上下文 */
        if (tc_type_is_signed(type)) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_TYPE;
            }
            return 0;
        }
        if (!tc_unsigned_in_range(lit->magnitude, type)) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        return 1;
    }

    if (lit->negative) {
        /* 有负号的字面量不能用于无符号上下文 */
        if (!tc_type_is_signed(type)) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        if (lit->magnitude == TC_INT64_MIN_ABS_MAGNITUDE) {
            if (!tc_signed_in_range(INT64_MIN, type)) {
                if (err_kind) {
                    *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
                }
                return 0;
            }
            return 1;
        }
        if (lit->magnitude > (uint64_t)INT64_MAX) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        if (!tc_signed_in_range(-(int64_t)lit->magnitude, type)) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        return 1;
    }

    if (tc_type_is_signed(type)) {
        if (lit->magnitude > (uint64_t)INT64_MAX) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        if (!tc_signed_in_range((int64_t)lit->magnitude, type)) {
            if (err_kind) {
                *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
            }
            return 0;
        }
        return 1;
    }

    if (!tc_unsigned_in_range(lit->magnitude, type)) {
        if (err_kind) {
            *err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
        }
        return 0;
    }
    return 1;
}

TcValue tc_literal_to_value(const TcLiteral *lit, TcTypeTag type) {
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

int tc_signed_in_range(int64_t value, TcTypeTag type) {
    return value >= tc_type_min_signed(type) && value <= tc_type_max_signed(type);
}

int tc_unsigned_in_range(uint64_t value, TcTypeTag type) {
    return value <= tc_type_max_unsigned(type);
}

/* ------------------------------------------------------------------ */
/*  浮点位模式转换                                                       */
/* ------------------------------------------------------------------ */

double tc_fp_bits_to_double(TcTypeTag type, uint64_t bits) {
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

uint64_t tc_fp_double_to_bits(TcTypeTag type, double value) {
    if (type == TC_FLOAT32) {
        float f = 0.0f;
        uint32_t b32 = 0;
#ifdef TC_HAVE_FENV
        int saved_round = fegetround();
        int restore_round = saved_round != -1 && saved_round != FE_TONEAREST;
        volatile double source = value;

        if (restore_round) {
            (void)fesetround(FE_TONEAREST);
        }
        f = (float)source;
        if (restore_round) {
            (void)fesetround(saved_round);
        }
#else
        f = (float)value;
#endif
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

/* ------------------------------------------------------------------ */
/*  整数比较与逻辑运算                                                    */
/* ------------------------------------------------------------------ */

int tc_exec_compare(TcCompareOp op, TcTypeTag type, const TcValue *lhs, const TcValue *rhs,
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
    int rhs_true = rhs->bits != 0;

    (void)diag;
    (void)line;

    if (op == TC_LOGIC_AND) {
        if (!lhs_true) {
            *out = tc_value_make(TC_BOOL, 0);
            return 0;
        }
        *out = tc_value_make(TC_BOOL, rhs_true ? 1ULL : 0ULL);
        return 0;
    }

    if (op == TC_LOGIC_XOR) {
        *out = tc_value_make(TC_BOOL, (lhs_true != rhs_true) ? 1ULL : 0ULL);
        return 0;
    }

    /* TC_LOGIC_OR */
    if (lhs_true) {
        *out = tc_value_make(TC_BOOL, 1);
        return 0;
    }
    *out = tc_value_make(TC_BOOL, rhs_true ? 1ULL : 0ULL);
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
