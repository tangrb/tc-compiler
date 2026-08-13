/*
 * tc_sem_fp.c — 确定性浮点算术 / 单目 / 比较语义
 *
 * strict 模式检测 IEEE 异常（invalid/overflow/underflow/div0）；
 * ieee 模式按 IEEE 754 返回 ±inf/nan 且不报错。
 */
#include "tc_sem_fp.h"

#include "tc_diagnostic.h"
#include "tc_semantics.h"

#ifdef TC_HAVE_FENV
#include <fenv.h>
#endif
#include <float.h>
#include <math.h>
#include <stdint.h>

static int tc_fp_invalid_operation(TcArithOp op, double lhs, double rhs) {
    if (isnan(lhs) || isnan(rhs)) {
        return 1;
    }
    switch (op) {
    case TC_ADD:
        return isinf(lhs) && isinf(rhs) && signbit(lhs) != signbit(rhs);
    case TC_SUB:
        return isinf(lhs) && isinf(rhs) && signbit(lhs) == signbit(rhs);
    case TC_MUL:
        return (isinf(lhs) && rhs == 0.0) || (lhs == 0.0 && isinf(rhs));
    case TC_DIV:
        return (lhs == 0.0 && rhs == 0.0) || (isinf(lhs) && isinf(rhs));
    case TC_MOD:
        return 0;
    }
    return 0;
}

static int tc_fp_division_by_zero(TcArithOp op, double lhs, double rhs) {
    return op == TC_DIV && rhs == 0.0 && lhs != 0.0 && isfinite(lhs);
}

static uint64_t tc_fp_canonical_nan_bits(TcTypeTag type) {
    return type == TC_FLOAT32 ? UINT64_C(0x7FC00000)
                              : UINT64_C(0x7FF8000000000000);
}

static int tc_fp_set_error(TcErrorKind kind, const char *message,
                           TcDiagnostic *diag, int line) {
    tc_diagnostic_set(diag, kind, line, TC_COLUMN_UNKNOWN, message);
    return -1;
}

#ifdef TC_HAVE_FENV
typedef struct {
    int saved_round;
    int restore_round;
} TcFpEnvironment;

static void tc_fp_environment_begin(TcFpEnvironment *environment) {
    environment->saved_round = fegetround();
    environment->restore_round = environment->saved_round != -1 &&
                                 environment->saved_round != FE_TONEAREST;
    if (environment->restore_round) {
        (void)fesetround(FE_TONEAREST);
    }
    (void)feclearexcept(FE_ALL_EXCEPT);
}

static int tc_fp_environment_end(const TcFpEnvironment *environment) {
    int exceptions = fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
    if (environment->restore_round) {
        (void)fesetround(environment->saved_round);
    }
    return exceptions;
}
#endif

static int tc_fp_compute(TcArithOp op, TcTypeTag type, double lhs, double rhs,
                         double *result, int *exceptions) {
#ifdef TC_HAVE_FENV
    TcFpEnvironment environment;
    tc_fp_environment_begin(&environment);
#endif

    if (type == TC_FLOAT32) {
        volatile float a = (float)lhs;
        volatile float b = (float)rhs;
        volatile float value = 0.0f;

        switch (op) {
        case TC_ADD:
            value = a + b;
            break;
        case TC_SUB:
            value = a - b;
            break;
        case TC_MUL:
            value = a * b;
            break;
        case TC_DIV:
            value = a / b;
            break;
        case TC_MOD:
            return -1;
        }
        *result = (double)value;
    } else {
        volatile double a = lhs;
        volatile double b = rhs;
        volatile double value = 0.0;

        switch (op) {
        case TC_ADD:
            value = a + b;
            break;
        case TC_SUB:
            value = a - b;
            break;
        case TC_MUL:
            value = a * b;
            break;
        case TC_DIV:
            value = a / b;
            break;
        case TC_MOD:
            return -1;
        }
        *result = value;
    }

#ifdef TC_HAVE_FENV
    *exceptions = tc_fp_environment_end(&environment);
#else
    *exceptions = 0;
#endif
    return 0;
}

#ifndef TC_HAVE_FENV
static long double tc_fp_compute_wide(TcArithOp op, double lhs, double rhs) {
    long double a = (long double)lhs;
    long double b = (long double)rhs;

    switch (op) {
    case TC_ADD:
        return a + b;
    case TC_SUB:
        return a - b;
    case TC_MUL:
        return a * b;
    case TC_DIV:
        return a / b;
    case TC_MOD:
        return 0.0L;
    }
    return 0.0L;
}

static int tc_fp_no_fenv_underflow(TcArithOp op, TcTypeTag type,
                                   double lhs, double rhs, double result) {
    long double wide = tc_fp_compute_wide(op, lhs, rhs);
    long double min_normal = type == TC_FLOAT32 ? (long double)FLT_MIN
                                                : (long double)DBL_MIN;

    if (result == 0.0 && isfinite(lhs) && isfinite(rhs)) {
        if (op == TC_MUL) {
            return lhs != 0.0 && rhs != 0.0;
        }
        if (op == TC_DIV) {
            return lhs != 0.0 && rhs != 0.0;
        }
    }
    if (wide == 0.0L || !isfinite(wide) || fabsl((long double)result) >= min_normal) {
        return 0;
    }
    if (type == TC_FLOAT32) {
        return (long double)result != wide;
    }
#if LDBL_MANT_DIG > DBL_MANT_DIG
    return (long double)result != wide;
#else
    if (result == 0.0) {
        return 1;
    }
    if (op == TC_MUL) {
        return lhs == 0.0 || result / lhs != rhs;
    }
    if (op == TC_DIV) {
        return result * rhs != lhs;
    }
    return 0;
#endif
}
#endif

static int tc_fp_check_strict_result(TcArithOp op, TcTypeTag type,
                                     double lhs, double rhs, double result,
                                     int exceptions, TcDiagnostic *diag, int line) {
#ifdef TC_HAVE_FENV
    (void)type;
    (void)result;
#endif
    if (tc_fp_invalid_operation(op, lhs, rhs)) {
        return tc_fp_set_error(TC_RE_FLOAT_INVALID, "float invalid operation", diag, line);
    }
    if (tc_fp_division_by_zero(op, lhs, rhs)) {
        return tc_fp_set_error(TC_RE_DIVISION_BY_ZERO, "division by zero", diag, line);
    }
#ifdef TC_HAVE_FENV
    if ((exceptions & FE_INVALID) != 0) {
        return tc_fp_set_error(TC_RE_FLOAT_INVALID, "float invalid operation", diag, line);
    }
    if ((exceptions & FE_DIVBYZERO) != 0) {
        return tc_fp_set_error(TC_RE_DIVISION_BY_ZERO, "division by zero", diag, line);
    }
    if ((exceptions & FE_OVERFLOW) != 0) {
        return tc_fp_set_error(TC_RE_FLOAT_OVERFLOW, "float overflow", diag, line);
    }
    if ((exceptions & FE_UNDERFLOW) != 0) {
        return tc_fp_set_error(TC_RE_FLOAT_UNDERFLOW, "float underflow", diag, line);
    }
#else
    (void)exceptions;
    if (isfinite(lhs) && isfinite(rhs) && isinf(result)) {
        return tc_fp_set_error(TC_RE_FLOAT_OVERFLOW, "float overflow", diag, line);
    }
    if (tc_fp_no_fenv_underflow(op, type, lhs, rhs, result)) {
        return tc_fp_set_error(TC_RE_FLOAT_UNDERFLOW, "float underflow", diag, line);
    }
#endif
    return 0;
}

int tc_exec_fp_arith(TcArithOp op, TcTypeTag type, TcFloatMode mode,
                     const TcValue *lhs, const TcValue *rhs, TcValue *out,
                     TcDiagnostic *diag, int line) {
    double a = 0.0;
    double b = 0.0;
    double result = 0.0;
    int exceptions = 0;

    if (tc_validate_fp_arith_mode(op, type, mode, diag, line) != 0) {
        return -1;
    }

    a = tc_fp_bits_to_double(type, lhs->bits);
    b = tc_fp_bits_to_double(type, rhs->bits);
    if (tc_fp_compute(op, type, a, b, &result, &exceptions) != 0) {
        return tc_fp_set_error(TC_CE_TYPE_MISMATCH,
                               "unsupported float operation", diag, line);
    }
    if (mode == TC_FLOAT_STRICT &&
        tc_fp_check_strict_result(op, type, a, b, result, exceptions, diag, line) != 0) {
        return -1;
    }
    if (isnan(result)) {
        *out = tc_value_make(type, tc_fp_canonical_nan_bits(type));
    } else {
        *out = tc_value_make(type, tc_fp_double_to_bits(type, result));
    }
    return 0;
}

int tc_exec_fp_unary(TcUnaryOp op, TcTypeTag type, TcFloatMode mode,
                     const TcValue *operand, TcValue *out,
                     TcDiagnostic *diag, int line) {
    uint64_t sign_bit = 0;
    uint64_t bits = 0;

    if (tc_validate_fp_unary_mode(op, type, mode, diag, line) != 0) {
        return -1;
    }
    sign_bit = type == TC_FLOAT32 ? UINT64_C(0x80000000)
                                  : UINT64_C(0x8000000000000000);
    bits = tc_value_to_unsigned(type, operand->bits);
    if (op == TC_UNARY_ABS) {
        bits &= ~sign_bit;
    } else if (op == TC_UNARY_NEG) {
        bits ^= sign_bit;
    } else {
        return tc_fp_set_error(TC_CE_TYPE_MISMATCH,
                               "unsupported float unary operation", diag, line);
    }
    *out = tc_value_make(type, bits);
    return 0;
}

static int tc_fp_compare_result(TcCompareOp op, double lhs, double rhs) {
    if (isnan(lhs) || isnan(rhs)) {
        return op == TC_CMP_NE;
    }
    switch (op) {
    case TC_CMP_EQ:
        return lhs == rhs;
    case TC_CMP_NE:
        return lhs != rhs;
    case TC_CMP_LT:
        return lhs < rhs;
    case TC_CMP_LE:
        return lhs <= rhs;
    case TC_CMP_GT:
        return lhs > rhs;
    case TC_CMP_GE:
        return lhs >= rhs;
    }
    return 0;
}

int tc_exec_fp_compare(TcCompareOp op, TcTypeTag type, TcFloatMode mode,
                       const TcValue *lhs, const TcValue *rhs, TcValue *out,
                       TcDiagnostic *diag, int line) {
    double a = 0.0;
    double b = 0.0;

    if (tc_validate_fp_compare_mode(type, mode, diag, line) != 0) {
        return -1;
    }
    a = tc_fp_bits_to_double(type, lhs->bits);
    b = tc_fp_bits_to_double(type, rhs->bits);
    *out = tc_value_make(TC_BOOL, (uint64_t)tc_fp_compare_result(op, a, b));
    return 0;
}
