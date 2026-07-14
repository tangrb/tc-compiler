/*
 * tc_sem_fp.c — 浮点算术 / 比较 / cast（strict / ieee / wrap）
 */
#include "tc_semantics.h"

#include "tc_diagnostic.h"

#ifdef TC_HAVE_FENV
#include <fenv.h>
#endif
#include <float.h>
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  浮点算术（strict / ieee / wrap）                                    */
/* ------------------------------------------------------------------ */

static int tc_fp_strict_check_nan_operand(double value, TcDiagnostic *diag, int line) {
    if (isnan(value)) {
        tc_diagnostic_set(diag, TC_ERR_FLOAT_INVALID, line, TC_COLUMN_UNKNOWN,
                          "float invalid operation");
        return -1;
    }
    return 0;
}

#ifdef TC_HAVE_FENV

/*
 * 浮点 strict 模式通过 fenv.h 检测 IEEE 754 异常标志。
 *
 * 线程安全：feclearexcept / fetestexcept 读写进程级（C 实现中通常为
 * 线程局部，但标准不保证）浮点异常标志；TC-VM 为单线程解释执行，
 * 无并发浮点运算，当前用法安全。若将来嵌入多线程宿主（如并行 REPL
 * 会话共享进程），须在每次 tc_exec_fp_* 调用前后加锁或改用 per-thread
 * fenv，参见 TC-VM 详设 §10.9「浮点 strict 模式与 fenv 线程安全」。
 */

static void tc_fp_clear_exceptions(void) {
    feclearexcept(FE_ALL_EXCEPT);
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

#else /* !TC_HAVE_FENV */

static void tc_fp_clear_exceptions(void) {
}

/*
 * 无 fenv 平台按语言标准检测非零非规格化结果（VM 详设 §10.9）。
 * 与 fenv 路径在 DBL_MIN×0.5 等边界情形可能略有差异（硬件未置 FE_UNDERFLOW）。
 */
static int tc_fp_strict_check_result_no_fenv(double result, TcDiagnostic *diag, int line) {
    if (isnan(result)) {
        tc_diagnostic_set(diag, TC_ERR_FLOAT_INVALID, line, TC_COLUMN_UNKNOWN,
                          "float invalid operation");
        return -1;
    }
    if (result != 0.0 && isfinite(result) && fabs(result) < DBL_MIN) {
        tc_diagnostic_set(diag, TC_ERR_FLOAT_UNDERFLOW, line, TC_COLUMN_UNKNOWN,
                          "float underflow");
        return -1;
    }
    return 0;
}

static int tc_fp_strict_check_exceptions_no_fenv(TcArithOp op, double lhs, double rhs,
                                                 double result, TcDiagnostic *diag, int line) {
    if (op == TC_DIV && rhs == 0.0) {
        tc_diagnostic_set(diag, TC_ERR_DIVISION_BY_ZERO, line, TC_COLUMN_UNKNOWN,
                          "division by zero");
        return -1;
    }
    if (isfinite(lhs) && isfinite(rhs) && isinf(result)) {
        tc_diagnostic_set(diag, TC_ERR_FLOAT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "float overflow");
        return -1;
    }
    return tc_fp_strict_check_result_no_fenv(result, diag, line);
}

#endif /* TC_HAVE_FENV */

static int tc_exec_fp_arith_strict(TcArithOp op, TcType type,
                                   double lhs, double rhs, double *result,
                                   TcDiagnostic *diag, int line) {
    (void)type;
    if (tc_fp_strict_check_nan_operand(lhs, diag, line) != 0) {
        return -1;
    }
    if (tc_fp_strict_check_nan_operand(rhs, diag, line) != 0) {
        return -1;
    }

    tc_fp_clear_exceptions();
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
#ifdef TC_HAVE_FENV
    return tc_fp_strict_check_exceptions(*result, diag, line);
#else
    return tc_fp_strict_check_exceptions_no_fenv(op, lhs, rhs, *result, diag, line);
#endif
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

static int tc_exec_fp_arith_wrap(TcArithOp op, TcType type, uint64_t lhs_bits, uint64_t rhs_bits,
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

static int tc_exec_fp_unary_wrap(TcUnaryOp op, TcType type, uint64_t operand_bits,
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

static int tc_fp_cast_strict(TcType target, const TcValue *source, TcValue *out,
                             TcDiagnostic *diag, int line) {
    TcType src_type = source->type;

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

static int tc_fp_cast_truncate(TcType target, const TcValue *source, TcValue *out,
                               TcDiagnostic *diag, int line) {
    TcType src_type = source->type;
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

int tc_exec_fp_arith(TcArithOp op, TcType type, TcFloatMode mode,
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
    tc_fp_clear_exceptions();
    switch (op) {
    case TC_UNARY_ABS:
        *result = fabs(operand);
        break;
    case TC_UNARY_NEG:
        *result = -operand;
        break;
    }
#ifdef TC_HAVE_FENV
    return tc_fp_strict_check_exceptions(*result, diag, line);
#else
    return tc_fp_strict_check_result_no_fenv(*result, diag, line);
#endif
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

int tc_exec_fp_unary(TcUnaryOp op, TcType type, TcFloatMode mode,
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

int tc_exec_fp_compare(TcCompareOp op, TcType type, TcFloatMode mode,
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

int tc_exec_fp_cast(TcType target, TcTruncateMode mode, const TcValue *source,
                    TcValue *out, TcDiagnostic *diag, int line) {
    if (mode == TC_TRUNC_STRICT) {
        return tc_fp_cast_strict(target, source, out, diag, line);
    }
    return tc_fp_cast_truncate(target, source, out, diag, line);
}

