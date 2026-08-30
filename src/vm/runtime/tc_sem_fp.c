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
        /* 有限值路径不经硬件 fmod；异常在 tc_fp_mod 中分类 */
        return isnan(lhs) || isnan(rhs) || isinf(lhs) ||
               (lhs == 0.0 && rhs == 0.0);
    }
    return 0;
}

static int tc_fp_division_by_zero(TcArithOp op, double lhs, double rhs) {
    if (op == TC_DIV) {
        return rhs == 0.0 && lhs != 0.0 && isfinite(lhs);
    }
    if (op == TC_MOD) {
        return isfinite(lhs) && lhs != 0.0 && rhs == 0.0;
    }
    return 0;
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
/* B1：无 FENV 位级下溢判定（§6.3.2「微小非精确非规格化」，tininess-after-
 * rounding）。结果（RNE 舍入）指数域全 0（subnormal/zero）且精确值 ≠ 结果
 * → 下溢。精确性用操作数位模式经 __int128 整数运算判定，不依赖宿主浮点
 * 舍入或长双精度，与 fenv 构建在任意平台上结果一致。 */

#if defined(__SIZEOF_INT128__)
typedef unsigned __int128 tc_u128;

/* 值 = mant × 2^e2（mant 含隐位；subnormal 无隐位，e2 为固定底） */
static void tc_fp_unpack_val(TcTypeTag type, uint64_t bits, uint64_t *mant, int *e2) {
    if (type == TC_FLOAT32) {
        uint32_t exp = (uint32_t)((bits >> 23) & 0xffu);
        uint32_t frac = (uint32_t)(bits & 0x7fffffu);

        if (exp == 0) {
            *mant = frac;
            *e2 = -149;
        } else {
            *mant = (1u << 23) | frac;
            *e2 = (int)exp - 150;
        }
    } else {
        uint64_t exp = (bits >> 52) & 0x7ffu;
        uint64_t frac = bits & ((1ull << 52) - 1u);

        if (exp == 0) {
            *mant = frac;
            *e2 = -1074;
        } else {
            *mant = (1ull << 52) | frac;
            *e2 = (int)exp - 1075;
        }
    }
}

static int tc_fp_clz64(uint64_t v) {
    int n = 0;

    if (v == 0) {
        return 64;
    }
    while ((v & (UINT64_C(1) << 63)) == 0) {
        v <<= 1;
        n++;
    }
    return n;
}

/* v 的显著位数（去掉尾零后的位数；0 → 0） */
static int tc_fp_sig64(uint64_t v) {
    int tz = 0;

    if (v == 0) {
        return 0;
    }
    while ((v & 1u) == 0) {
        v >>= 1;
        tz++;
    }
    return 64 - tc_fp_clz64(v) - tz;
}

/* P 的尾零位数 */
static int tc_fp_tz128(tc_u128 P) {
    int tz = 0;

    while (((P >> tz) & 1u) == 0) {
        tz++;
    }
    return tz;
}

/* P 的显著位数（去掉尾零后的位数） */
static int tc_fp_sig128(tc_u128 P) {
    uint64_t hi = (uint64_t)(P >> 64);
    int top = 0;

    if (hi != 0) {
        top = 128 - tc_fp_clz64(hi);
    } else {
        top = 64 - tc_fp_clz64((uint64_t)P);
    }
    return top - tc_fp_tz128(P);
}

/* V = P × 2^e2p（P ≤ 128 位）在目标类型中精确可表示？ */
static int tc_fp_value_exact_fits(TcTypeTag type, tc_u128 P, int e2p) {
    int mant_bits = (type == TC_FLOAT32) ? 24 : 53;
    int min_sub = (type == TC_FLOAT32) ? -149 : -1074;
    int k = 0;

    if (P == 0) {
        return 1;
    }
    /* 值 = P × 2^e2p。目标类型可表示 ⟺ 值是 2^min_sub 网格的整数倍（M ∈ [1, 2^mant_bits-1]）：
     * 即 P × 2^(e2p - min_sub) 为整数且 ≤ 2^mant_bits - 1。 */
    k = min_sub - e2p;
    if (k > 0) {
        /* e2p < min_sub：P 必须是 2^k 的倍数（否则值低于网格粒度，不可表示） */
        if (k >= 128) {
            return 0;
        }
        if (((P >> k) << k) != P) {
            return 0;
        }
        P >>= k;
        return P <= (((tc_u128)1 << mant_bits) - 1u);
    }
    /* e2p ≥ min_sub：值 = P × 2^(e2p - min_sub) × 2^min_sub，P 整数；仅需
     * P × 2^(e2p - min_sub) ≤ 2^mant_bits - 1。调用方保证精确值微小
     * （结果必为 subnormal/zero），故 sh < mant_bits 恒成立，防御性守卫即可。 */
    {
        int sh = e2p - min_sub;

        if (sh >= mant_bits) {
            return P == 0;
        }
        return P <= (((tc_u128)1 << (mant_bits - sh)) - 1u);
    }
}

/* 不相交位区间（|delta| > 75）的和/差精确性：两分量均须对齐 2^min_sub
 * 网格，且总显著位数 ≤ mant_bits（区间不相交 → 无进位/无抵消）。 */
static int tc_fp_exact_neq_disjoint(uint64_t m_hi, int e2_hi, uint64_t m_lo, int e2_lo,
                                    int mant_bits, int min_sub) {
    int k = 0;

    if (m_lo != 0) {
        k = min_sub - e2_lo;
        if (k > 0) {
            if (k >= 64) {
                return 1;
            }
            if (((m_lo >> k) << k) != m_lo) {
                return 1;
            }
        }
    }
    k = min_sub - e2_hi;
    if (k > 0) {
        if (k >= 64) {
            return 1;
        }
        if (((m_hi >> k) << k) != m_hi) {
            return 1;
        }
    }
    if (tc_fp_sig64(m_hi) + tc_fp_sig64(m_lo) > mant_bits) {
        return 1;
    }
    return 0;
}

/* 精确值 |a op b| 不可表示（≠ RNE 舍入结果；result 必为 subnormal/zero）？ */
static int tc_fp_exact_neq(TcArithOp op, TcTypeTag type, uint64_t abits, uint64_t bbits) {
    uint64_t ma = 0;
    uint64_t mb = 0;
    int e2a = 0;
    int e2b = 0;
    int mant_bits = (type == TC_FLOAT32) ? 24 : 53;
    int min_sub = (type == TC_FLOAT32) ? -149 : -1074;

    tc_fp_unpack_val(type, abits, &ma, &e2a);
    tc_fp_unpack_val(type, bbits, &mb, &e2b);

    switch (op) {
    case TC_MUL: {
        /* 精确积 = ma×mb × 2^(e2a+e2b)（≤106/48 位，__int128 精确） */
        return !tc_fp_value_exact_fits(type, (tc_u128)ma * mb, e2a + e2b);
    }
    case TC_DIV: {
        /* 精确商：长除到 mant+2 位判余数；余数非零或不精确位数超限 → 不精确 */
        int extra = (type == TC_FLOAT32) ? 26 : 55;
        tc_u128 num = (tc_u128)ma << extra;
        tc_u128 q = 0;
        tc_u128 rem = 0;

        if (mb == 0) {
            return 0; /* div0 由 tc_fp_division_by_zero 先报 */
        }
        q = num / mb;
        rem = num % mb;
        if (rem != 0) {
            return 1;
        }
        return tc_fp_sig128(q) > mant_bits;
    }
    case TC_ADD:
    case TC_SUB: {
        int delta = e2a - e2b;

        if (delta >= 0) {
            if (delta <= 75) {
                /* 和/差 = (ma×2^delta ± mb) × 2^e2b，≤128 位精确 */
                tc_u128 P = (tc_u128)ma << delta;

                P = (op == TC_ADD) ? (P + mb) : (P - mb);
                return !tc_fp_value_exact_fits(type, P, e2b);
            }
            return tc_fp_exact_neq_disjoint(ma, e2a, mb, e2b, mant_bits, min_sub);
        }
        /* b 指数 ≥ a：对称处理 */
        {
            int d2 = -delta;

            if (d2 <= 75) {
                tc_u128 P = (tc_u128)mb << d2;

                P = (op == TC_ADD) ? (P + ma) : (P - ma);
                return !tc_fp_value_exact_fits(type, P, e2a);
            }
            return tc_fp_exact_neq_disjoint(mb, e2b, ma, e2a, mant_bits, min_sub);
        }
    }
    default:
        return 0;
    }
}

static int tc_fp_no_fenv_underflow(TcArithOp op, TcTypeTag type, uint64_t abits, uint64_t bbits,
                                   uint64_t rbits) {
    int rexp = 0;

    rexp = (type == TC_FLOAT32) ? (int)((rbits >> 23) & 0xffu)
                                : (int)((rbits >> 52) & 0x7ffu);
    if (rexp != 0) {
        return 0; /* 结果为 normal → 非微小 */
    }
    return tc_fp_exact_neq(op, type, abits, bbits);
}
#else
static int tc_fp_no_fenv_underflow(TcArithOp op, TcTypeTag type, uint64_t abits, uint64_t bbits,
                                   uint64_t rbits) {
    /* 无 __int128 平台：保守回退——subnormal/zero 结果即判下溢 */
    int rexp = 0;

    (void)op;
    (void)abits;
    (void)bbits;
    rexp = (type == TC_FLOAT32) ? (int)((rbits >> 23) & 0xffu)
                                : (int)((rbits >> 52) & 0x7ffu);
    return rexp == 0;
}
#endif
#endif

static int tc_fp_check_strict_result(TcArithOp op, TcTypeTag type,
                                     double lhs, double rhs, double result,
                                     uint64_t lhs_bits, uint64_t rhs_bits, uint64_t result_bits,
                                     int exceptions, TcDiagnostic *diag, int line) {
#ifdef TC_HAVE_FENV
    (void)type;
    (void)result;
    (void)lhs_bits;
    (void)rhs_bits;
    (void)result_bits;
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
    if (tc_fp_no_fenv_underflow(op, type, lhs_bits, rhs_bits, result_bits)) {
        return tc_fp_set_error(TC_RE_FLOAT_UNDERFLOW, "float underflow", diag, line);
    }
#endif
    return 0;
}

/*
 * 有限值浮点 mod：对 significand 做整数减法归约，等价于数学域
 * q = trunc(a/b)、r = a - q*b，不经目标类型除法回乘（§6.3.7）。
 * 结果为零时保留 a 的符号。算法移植自 musl fmod/fmodf 的整数归约。
 */
static uint32_t tc_fp_mod_finite_f32(uint32_t ax, uint32_t ay) {
    int ex = (int)((ax >> 23) & 0xff);
    int ey = (int)((ay >> 23) & 0xff);
    uint32_t sx = ax & 0x80000000u;
    uint32_t uxi = ax;
    uint32_t i;

    if ((uxi << 1) <= (ay << 1)) {
        if ((uxi << 1) == (ay << 1)) {
            return sx;
        }
        return ax;
    }
    if (ex == 0) {
        for (i = uxi << 9; (i >> 31) == 0; ex--, i <<= 1) {
        }
        uxi <<= (uint32_t)(-ex + 1);
    } else {
        uxi &= 0x007fffffu;
        uxi |= 1u << 23;
    }
    if (ey == 0) {
        for (i = ay << 9; (i >> 31) == 0; ey--, i <<= 1) {
        }
        ay <<= (uint32_t)(-ey + 1);
    } else {
        ay &= 0x007fffffu;
        ay |= 1u << 23;
    }
    for (; ex > ey; ex--) {
        i = uxi - ay;
        if ((i >> 31) == 0) {
            if (i == 0) {
                return sx;
            }
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - ay;
    if ((i >> 31) == 0) {
        if (i == 0) {
            return sx;
        }
        uxi = i;
    }
    for (; (uxi >> 23) == 0; uxi <<= 1, ex--) {
    }
    if (ex > 0) {
        uxi -= 1u << 23;
        uxi |= (uint32_t)ex << 23;
    } else {
        uxi >>= (uint32_t)(-ex + 1);
    }
    uxi |= sx;
    return uxi;
}

static uint64_t tc_fp_mod_finite_f64(uint64_t ax, uint64_t ay) {
    int ex = (int)((ax >> 52) & 0x7ff);
    int ey = (int)((ay >> 52) & 0x7ff);
    uint64_t sx = ax & UINT64_C(0x8000000000000000);
    uint64_t uxi = ax;
    uint64_t i;

    if ((uxi << 1) <= (ay << 1)) {
        if ((uxi << 1) == (ay << 1)) {
            return sx;
        }
        return ax;
    }
    if (ex == 0) {
        for (i = uxi << 12; (i >> 63) == 0; ex--, i <<= 1) {
        }
        uxi <<= (uint64_t)(-ex + 1);
    } else {
        uxi &= UINT64_C(0x000fffffffffffff);
        uxi |= UINT64_C(1) << 52;
    }
    if (ey == 0) {
        for (i = ay << 12; (i >> 63) == 0; ey--, i <<= 1) {
        }
        ay <<= (uint64_t)(-ey + 1);
    } else {
        ay &= UINT64_C(0x000fffffffffffff);
        ay |= UINT64_C(1) << 52;
    }
    for (; ex > ey; ex--) {
        i = uxi - ay;
        if ((i >> 63) == 0) {
            if (i == 0) {
                return sx;
            }
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - ay;
    if ((i >> 63) == 0) {
        if (i == 0) {
            return sx;
        }
        uxi = i;
    }
    for (; (uxi >> 52) == 0; uxi <<= 1, ex--) {
    }
    if (ex > 0) {
        uxi -= UINT64_C(1) << 52;
        uxi |= (uint64_t)ex << 52;
    } else {
        uxi >>= (uint64_t)(-ex + 1);
    }
    uxi |= sx;
    return uxi;
}

static int tc_fp_mod(TcTypeTag type, TcFloatMode mode, const TcValue *lhs, const TcValue *rhs,
                     TcValue *out, TcDiagnostic *diag, int line) {
    double a = 0.0;
    double b = 0.0;
    int invalid = 0;
    int div0 = 0;

    a = tc_fp_bits_to_double(type, lhs->bits);
    b = tc_fp_bits_to_double(type, rhs->bits);
    invalid = isnan(a) || isnan(b) || isinf(a) || (a == 0.0 && b == 0.0);
    div0 = isfinite(a) && a != 0.0 && b == 0.0;
    if (invalid || div0) {
        if (mode == TC_FLOAT_IEEE) {
            *out = tc_value_make(type, tc_fp_canonical_nan_bits(type));
            return 0;
        }
        if (invalid) {
            return tc_fp_set_error(TC_RE_FLOAT_INVALID, "float invalid operation", diag, line);
        }
        return tc_fp_set_error(TC_RE_DIVISION_BY_ZERO, "division by zero", diag, line);
    }
    if (isinf(b)) {
        /* 有限 a、±inf b：结果按位等于 a */
        *out = tc_value_make(type, lhs->bits);
        return 0;
    }
    if (type == TC_FLOAT32) {
        uint32_t r = tc_fp_mod_finite_f32((uint32_t)(lhs->bits & 0xffffffffu),
                                          (uint32_t)(rhs->bits & 0xffffffffu));
        *out = tc_value_make(type, (uint64_t)r);
        return 0;
    }
    *out = tc_value_make(type, tc_fp_mod_finite_f64(lhs->bits, rhs->bits));
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
    if (op == TC_MOD) {
        return tc_fp_mod(type, mode, lhs, rhs, out, diag, line);
    }

    a = tc_fp_bits_to_double(type, lhs->bits);
    b = tc_fp_bits_to_double(type, rhs->bits);
    if (tc_fp_compute(op, type, a, b, &result, &exceptions) != 0) {
        return tc_fp_set_error(TC_CE_TYPE_MISMATCH,
                               "unsupported float operation", diag, line);
    }
    if (mode == TC_FLOAT_STRICT &&
        tc_fp_check_strict_result(op, type, a, b, result, lhs->bits, rhs->bits,
                                  tc_fp_double_to_bits(type, result), exceptions, diag,
                                  line) != 0) {
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
