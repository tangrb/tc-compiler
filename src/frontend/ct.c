/* TC 0.0.46 compile-time (CT) evaluation (standard §3.10/§4.10, §13.2.2).
 *
 * The evaluator runs after SEM accepted the program: name, type, conversion,
 * layout and const_expr form errors are already reported, so an unreachable
 * case here is an internal inconsistency, not a language diagnostic.
 * Evaluation is demand driven in `module_order`/module source order; every
 * declaration is evaluated at most once and success/failure is cached.  The
 * first encountered failure is reported at the target declaration and stops
 * CT (standard §4.10/§12.5.1).
 *
 * Strict arithmetic follows standard §6.3: signed integer overflow and strict
 * `shl` overflow are CONST_OVERFLOW, integer/strict float division by zero is
 * CONST_DIV_ZERO, negative shift counts are CONST_SHIFT, strict floating
 * invalid operations are CONST_FLOAT_INVALID, strict floating underflow is
 * CONST_FLOAT_UNDERFLOW, and a non-representable `cast` is CONST_CAST.
 */
#include "frontend/ct.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* CT floating evaluation must round once per instruction in the target
 * binary32/binary64 format (standard §6.3.2/§10.6).  The build disables FMA
 * contraction; hosts that evaluate in wider precision are rejected rather than
 * silently producing profile-dependent results. */
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "CT floating evaluation requires FLT_EVAL_METHOD == 0"
#endif

#define CT_NO_ERROR ((TcDiagCode)0)

/* ------------------------------------------------------------------------- */
/* Table state and scopes.                                                   */
/* ------------------------------------------------------------------------- */

typedef enum {
    TC_CT_EMPTY = 0,
    TC_CT_EVALUATING,
    TC_CT_SUCCESS,
    TC_CT_FAILURE
} TcCtState;

typedef struct {
    uint32_t module;
    size_t decl_index;
    TcMember *member;
    TcCtState state;
    const TcCtValue *value;
    TcCtDiagnostic diag;
} TcCtDecl;

typedef struct {
    uint32_t module;
    size_t func_member; /* SIZE_MAX == #program body */
    size_t stmt_index;
    const TcParseStmt *stmt;
    const TcType *type;
    TcCtState state;
    const TcCtValue *value;
    TcCtDiagnostic diag;
} TcCtLocal;

typedef struct {
    TcAnalysis *an;
    TcCtDecl *decls;
    size_t ndecls;
    TcCtLocal *locals;
    size_t nlocals;
    size_t eval_count;
    int has_failure;
    TcCtDiagnostic failure;
    int internal;
} TcCt;

typedef struct {
    TcCt *ct;
    TcAnalysis *an;
    uint32_t module;
    const TcParseStmt *body; /* NULL at module level */
    size_t nbody;
    size_t func_member;
    size_t stmt_index;
    TcSpan decl_span;
    unsigned depth;
    int failed;
} CtScope;

static void *ct_alloc(TcCt *ct, size_t size) {
    void *p = tc_arena_alloc0(&ct->an->arena, size == 0 ? 1u : size);
    if (p == NULL) {
        ct->an->impl = TC_IMPL_OOM;
        longjmp(ct->an->env, 1);
    }
    return p;
}

static const TcType *scalar_type(TcAnalysis *an, TcTypeTag tag) {
    return tc_type_scalar(an->types, tag);
}

static unsigned ct_bits(TcAnalysis *an, const TcType *type) {
    return tc_sem_type_bits(an, type);
}

static int ct_type_is_bool(const TcType *type) {
    return type != NULL && type->tag == TC_TY_BOOL;
}

static int ct_type_is_ptr(const TcType *type) {
    return type != NULL && type->tag == TC_TY_PTR;
}

static int ct_type_is_fnptr(const TcType *type) {
    return type != NULL && type->tag == TC_TY_FNPTR;
}

static int ct_type_is_struct(const TcType *type) {
    return type != NULL && type->tag == TC_TY_STRUCT;
}

static int ct_type_is_void(const TcType *type) {
    return type != NULL && type->tag == TC_TY_VOID;
}

static uint64_t ct_mask(unsigned bits) {
    return bits >= 64u ? ~(uint64_t)0 : ((UINT64_C(1) << bits) - 1u);
}

static uint64_t ct_sign_extend(uint64_t bits, unsigned width) {
    uint64_t mask = ct_mask(width);
    if (width < 64u && (bits & (UINT64_C(1) << (width - 1u))) != 0) {
        return bits | ~mask;
    }
    return bits & mask;
}

/* ------------------------------------------------------------------------- */
/* Values.                                                                   */
/* ------------------------------------------------------------------------- */

static TcCtValue *ct_value_alloc(TcCt *ct, const TcType *type) {
    TcCtValue *value = (TcCtValue *)ct_alloc(ct, sizeof(TcCtValue));
    value->type = type;
    value->kind = TC_CT_SCALAR;
    return value;
}

static const TcCtValue *ct_scalar(TcCt *ct, const TcType *type, uint64_t bits) {
    TcCtValue *value = ct_value_alloc(ct, type);
    value->kind = TC_CT_SCALAR;
    value->bits = type != NULL ? bits & ct_mask(ct_bits(ct->an, type)) : bits;
    return value;
}

/* ------------------------------------------------------------------------- */
/* Floating point helpers.                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    int sign;
    int is_nan;
    int is_inf;
    int is_zero;
    int is_subnormal;
    int scale;
    uint64_t mantissa;
} CtFParts;

static int ct_fp_parts(uint64_t bits, unsigned width, CtFParts *out) {
    memset(out, 0, sizeof(*out));
    if (width == 32) {
        unsigned exp = (unsigned)((bits >> 23) & 0xffu);
        uint64_t frac = bits & UINT64_C(0x7fffff);
        out->sign = (int)((bits >> 31) & 1u);
        if (exp == 0xffu) {
            out->is_inf = frac == 0;
            out->is_nan = frac != 0;
            out->mantissa = frac;
            return 1;
        }
        if (exp == 0) {
            out->mantissa = frac;
            out->scale = -149;
            out->is_zero = frac == 0;
            out->is_subnormal = frac != 0;
        } else {
            out->mantissa = frac | (UINT64_C(1) << 23);
            out->scale = (int)exp - 127 - 23;
        }
        return 1;
    }
    if (width == 64) {
        unsigned exp = (unsigned)((bits >> 52) & 0x7ffu);
        uint64_t frac = bits & UINT64_C(0xfffffffffffff);
        out->sign = (int)((bits >> 63) & 1u);
        if (exp == 0x7ffu) {
            out->is_inf = frac == 0;
            out->is_nan = frac != 0;
            out->mantissa = frac;
            return 1;
        }
        if (exp == 0) {
            out->mantissa = frac;
            out->scale = -1074;
            out->is_zero = frac == 0;
            out->is_subnormal = frac != 0;
        } else {
            out->mantissa = frac | (UINT64_C(1) << 52);
            out->scale = (int)exp - 1023 - 52;
        }
        return 1;
    }
    return 0;
}

static float ct_bits_to_f32(uint64_t bits) {
    uint32_t raw = (uint32_t)bits;
    float value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static uint64_t ct_f32_to_bits(float value) {
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    return raw;
}

static double ct_bits_to_f64(uint64_t bits) {
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t ct_f64_to_bits(double value) {
    uint64_t raw;
    memcpy(&raw, &value, sizeof(raw));
    return raw;
}

static uint64_t ct_nan_bits(unsigned width) {
    return width == 32 ? UINT64_C(0x7fc00000)
                       : UINT64_C(0x7ff8000000000000);
}

static uint64_t ct_inf_bits(unsigned width, int sign) {
    if (width == 32) {
        return sign != 0 ? UINT64_C(0xff800000) : UINT64_C(0x7f800000);
    }
    return sign != 0 ? UINT64_C(0xfff0000000000000)
                     : UINT64_C(0x7ff0000000000000);
}

typedef struct {
    uint64_t hi;
    uint64_t lo;
} CtU128;

static CtU128 ct_u128_mul(uint64_t a, uint64_t b) {
    uint64_t a0 = a & UINT64_C(0xffffffff);
    uint64_t a1 = a >> 32;
    uint64_t b0 = b & UINT64_C(0xffffffff);
    uint64_t b1 = b >> 32;
    uint64_t p0 = a0 * b0;
    uint64_t p1 = a0 * b1;
    uint64_t p2 = a1 * b0;
    uint64_t p3 = a1 * b1;
    uint64_t mid = (p0 >> 32) + (p1 & UINT64_C(0xffffffff)) +
                   (p2 & UINT64_C(0xffffffff));
    CtU128 out;
    out.lo = (p0 & UINT64_C(0xffffffff)) | (mid << 32);
    out.hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    return out;
}

static CtU128 ct_u128_from_u64(uint64_t value) {
    CtU128 out;
    out.hi = 0;
    out.lo = value;
    return out;
}

/* 0 on overflow (nonzero bits shifted out), 1 otherwise. */
static int ct_u128_shl_checked(CtU128 value, unsigned shift, CtU128 *out) {
    if (shift == 0) {
        *out = value;
        return 1;
    }
    if (shift >= 128) {
        if (value.hi == 0 && value.lo == 0) {
            *out = ct_u128_from_u64(0);
            return 1;
        }
        return 0;
    }
    if (shift >= 64) {
        if (value.hi != 0) {
            return 0;
        }
        out->hi = value.lo << (shift - 64u);
        out->lo = 0;
        return 1;
    }
    if ((value.hi >> (64u - shift)) != 0) {
        return 0;
    }
    out->hi = (value.hi << shift) | (value.lo >> (64u - shift));
    out->lo = value.lo << shift;
    return 1;
}

static int ct_u128_equal(CtU128 a, CtU128 b) {
    return a.hi == b.hi && a.lo == b.lo;
}

static int ct_u128_equal_u64(CtU128 a, uint64_t b) {
    return a.hi == 0 && a.lo == b;
}

static int ct_fp_special_pair(const CtFParts *a, const CtFParts *b,
                              const CtFParts *r) {
    return a->is_nan || b->is_nan || a->is_inf || b->is_inf || a->is_zero ||
           b->is_zero || !r->is_subnormal;
}

/* Whether the subnormal result `r` of mul(a,b) is the exact product. */
static int ct_mul_subnormal_exact(const CtFParts *a, const CtFParts *b,
                                  const CtFParts *r) {
    CtU128 product;
    CtU128 shifted;
    long diff;
    if (ct_fp_special_pair(a, b, r)) {
        return 0;
    }
    product = ct_u128_mul(a->mantissa, b->mantissa);
    diff = (long)a->scale + (long)b->scale - (long)r->scale;
    if (diff >= 0) {
        if (!ct_u128_shl_checked(product, (unsigned)diff, &shifted)) {
            return 0;
        }
        return ct_u128_equal_u64(shifted, r->mantissa);
    }
    if (!ct_u128_shl_checked(ct_u128_from_u64(r->mantissa),
                             (unsigned)(-diff), &shifted)) {
        return 0;
    }
    return ct_u128_equal(product, shifted);
}

/* Whether the subnormal result `r` of div(a,b) is the exact quotient. */
static int ct_div_subnormal_exact(const CtFParts *a, const CtFParts *b,
                                  const CtFParts *r) {
    CtU128 product;
    CtU128 shifted;
    long diff;
    if (ct_fp_special_pair(a, b, r)) {
        return 0;
    }
    product = ct_u128_mul(r->mantissa, b->mantissa);
    diff = (long)r->scale + (long)b->scale - (long)a->scale;
    if (diff >= 0) {
        if (!ct_u128_shl_checked(product, (unsigned)diff, &shifted)) {
            return 0;
        }
        return ct_u128_equal_u64(shifted, a->mantissa);
    }
    if (!ct_u128_shl_checked(ct_u128_from_u64(a->mantissa),
                             (unsigned)(-diff), &shifted)) {
        return 0;
    }
    return ct_u128_equal(product, shifted);
}

/* Exactness of a subnormal mul/div result decides tininess-after-rounding:
 * an exact subnormal result is not an underflow (standard §6.3.2). */
static int ct_float_op_exact(unsigned width, TcOpcode op, uint64_t a_bits,
                             uint64_t b_bits, uint64_t r_bits) {
    CtFParts a;
    CtFParts b;
    CtFParts r;
    if (!ct_fp_parts(a_bits, width, &a) || !ct_fp_parts(b_bits, width, &b) ||
        !ct_fp_parts(r_bits, width, &r)) {
        return 0;
    }
    if (op == TC_MUL) {
        return ct_mul_subnormal_exact(&a, &b, &r);
    }
    if (op == TC_DIV) {
        return ct_div_subnormal_exact(&a, &b, &r);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Integer operations.                                                       */
/* ------------------------------------------------------------------------- */

static int ct_signed_fits(int64_t value, unsigned width) {
    if (width >= 64u) {
        return 1;
    }
    return value >= -(int64_t)(UINT64_C(1) << (width - 1u)) &&
           value <= (int64_t)((UINT64_C(1) << (width - 1u)) - 1u);
}

static int64_t ct_signed_value(uint64_t bits, unsigned width) {
    return (int64_t)ct_sign_extend(bits, width);
}

/* Returns 1 with *out, or 0 with *error set. */
static int ct_int_binop(TcAnalysis *an, TcOpcode op, const TcType *type,
                        uint64_t a_bits, uint64_t b_bits, uint64_t *out,
                        TcDiagCode *error) {
    unsigned width = ct_bits(an, type);
    uint64_t mask = ct_mask(width);
    uint64_t a = a_bits & mask;
    uint64_t b = b_bits & mask;
    int is_signed = tc_type_is_signed_integer(type);
    switch (op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
        if (is_signed) {
            int64_t sa = ct_signed_value(a, width);
            int64_t sb = ct_signed_value(b, width);
            int64_t result = 0;
            int overflow;
            if (op == TC_ADD) {
                overflow = __builtin_add_overflow(sa, sb, &result);
            } else if (op == TC_SUB) {
                overflow = __builtin_sub_overflow(sa, sb, &result);
            } else {
                overflow = __builtin_mul_overflow(sa, sb, &result);
            }
            if (overflow || !ct_signed_fits(result, width)) {
                *error = TC_CE_CONST_OVERFLOW;
                return 0;
            }
            *out = (uint64_t)result & mask;
            return 1;
        }
        if (op == TC_ADD) {
            *out = (a + b) & mask;
        } else if (op == TC_SUB) {
            *out = (a - b) & mask;
        } else {
            *out = (a * b) & mask;
        }
        return 1;
    case TC_DIV:
    case TC_MOD:
        if (b == 0) {
            *error = TC_CE_CONST_DIV_ZERO;
            return 0;
        }
        if (is_signed) {
            int64_t sa = ct_signed_value(a, width);
            int64_t sb = ct_signed_value(b, width);
            if (a == (UINT64_C(1) << (width - 1u)) && b == mask) {
                if (op == TC_DIV) {
                    *error = TC_CE_CONST_OVERFLOW;
                    return 0;
                }
                *out = 0;
                return 1;
            }
            *out = op == TC_DIV ? ((uint64_t)(sa / sb) & mask)
                                : ((uint64_t)(sa % sb) & mask);
        } else {
            *out = op == TC_DIV ? a / b : a % b;
        }
        return 1;
    case TC_SHL:
    case TC_SHR: {
        uint64_t count;
        if (is_signed) {
            int64_t sk = ct_signed_value(b, width);
            if (sk < 0) {
                *error = TC_CE_CONST_SHIFT;
                return 0;
            }
            count = (uint64_t)sk;
        } else {
            count = b;
        }
        if (op == TC_SHL) {
            if (count >= width) {
                if (a != 0) {
                    *error = TC_CE_CONST_OVERFLOW;
                    return 0;
                }
                *out = 0;
                return 1;
            }
            if (is_signed) {
                int64_t sa = ct_signed_value(a, width);
                int64_t smax;
                int64_t smin;
                if (width >= 64u) {
                    smax = INT64_MAX;
                    smin = INT64_MIN;
                } else {
                    smax = (int64_t)((UINT64_C(1) << (width - 1u)) - 1u);
                    smin = -(int64_t)(UINT64_C(1) << (width - 1u));
                }
                if (count != 0 &&
                    (sa > (smax >> count) || sa < (smin >> count))) {
                    *error = TC_CE_CONST_OVERFLOW;
                    return 0;
                }
                *out = (a << count) & mask;
            } else {
                if (count != 0 && (a >> (width - count)) != 0) {
                    *error = TC_CE_CONST_OVERFLOW;
                    return 0;
                }
                *out = (a << count) & mask;
            }
            return 1;
        }
        if (count >= width) {
            *out = 0;
        } else if (is_signed) {
            int64_t sa = ct_signed_value(a, width);
            *out = (uint64_t)(sa >> count) & mask;
        } else {
            *out = a >> count;
        }
        return 1;
    }
    case TC_AND:
        *out = a & b;
        return 1;
    case TC_OR:
        *out = a | b;
        return 1;
    case TC_XOR:
        *out = a ^ b;
        return 1;
    default:
        *error = CT_NO_ERROR;
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* IEEE arithmetic (strict; standard §6.3/§6.3.1-§6.3.3).                     */
/* ------------------------------------------------------------------------- */
/* Returns 1 with *out, or 0 with *error set. */
static int ct_float_binop(TcAnalysis *an, TcOpcode op, const TcType *type,
                          uint64_t a_bits, uint64_t b_bits, uint64_t *out,
                          TcDiagCode *error) {
    unsigned width = ct_bits(an, type);
    CtFParts a;
    CtFParts b;
    CtFParts r;
    uint64_t result = 0;
    if (!ct_fp_parts(a_bits, width, &a) || !ct_fp_parts(b_bits, width, &b)) {
        *error = TC_CE_CONST_FLOAT_INVALID;
        return 0;
    }
    if (a.is_nan || b.is_nan) {
        *error = TC_CE_CONST_FLOAT_INVALID;
        return 0;
    }
    switch (op) {
    case TC_ADD:
        if (a.is_inf && b.is_inf && a.sign != b.sign) {
            *error = TC_CE_CONST_FLOAT_INVALID;
            return 0;
        }
        if (width == 32) {
            result = ct_f32_to_bits(ct_bits_to_f32(a_bits) +
                                    ct_bits_to_f32(b_bits));
        } else {
            result = ct_f64_to_bits(ct_bits_to_f64(a_bits) +
                                    ct_bits_to_f64(b_bits));
        }
        break;
    case TC_SUB:
        if (a.is_inf && b.is_inf && a.sign == b.sign) {
            *error = TC_CE_CONST_FLOAT_INVALID;
            return 0;
        }
        if (width == 32) {
            result = ct_f32_to_bits(ct_bits_to_f32(a_bits) -
                                    ct_bits_to_f32(b_bits));
        } else {
            result = ct_f64_to_bits(ct_bits_to_f64(a_bits) -
                                    ct_bits_to_f64(b_bits));
        }
        break;
    case TC_MUL:
        if ((a.is_inf && b.is_zero) || (b.is_inf && a.is_zero)) {
            *error = TC_CE_CONST_FLOAT_INVALID;
            return 0;
        }
        if (width == 32) {
            result = ct_f32_to_bits(ct_bits_to_f32(a_bits) *
                                    ct_bits_to_f32(b_bits));
        } else {
            result = ct_f64_to_bits(ct_bits_to_f64(a_bits) *
                                    ct_bits_to_f64(b_bits));
        }
        break;
    case TC_DIV:
        if ((a.is_inf && b.is_inf) || (a.is_zero && b.is_zero)) {
            *error = TC_CE_CONST_FLOAT_INVALID;
            return 0;
        }
        if (b.is_zero && !a.is_inf) {
            *error = TC_CE_CONST_DIV_ZERO;
            return 0;
        }
        if (width == 32) {
            result = ct_f32_to_bits(ct_bits_to_f32(a_bits) /
                                    ct_bits_to_f32(b_bits));
        } else {
            result = ct_f64_to_bits(ct_bits_to_f64(a_bits) /
                                    ct_bits_to_f64(b_bits));
        }
        break;
    case TC_MOD:
        if (a.is_inf) {
            *error = TC_CE_CONST_FLOAT_INVALID;
            return 0;
        }
        if (b.is_zero) {
            if (a.is_zero) {
                *error = TC_CE_CONST_FLOAT_INVALID;
                return 0;
            }
            *error = TC_CE_CONST_DIV_ZERO;
            return 0;
        }
        if (b.is_inf) {
            *out = a_bits;
            return 1;
        }
        if (width == 32) {
            result = ct_f32_to_bits(fmodf(ct_bits_to_f32(a_bits),
                                          ct_bits_to_f32(b_bits)));
        } else {
            result = ct_f64_to_bits(fmod(ct_bits_to_f64(a_bits),
                                         ct_bits_to_f64(b_bits)));
        }
        break;
    default:
        *error = CT_NO_ERROR;
        return 0;
    }
    if (!ct_fp_parts(result, width, &r)) {
        *error = CT_NO_ERROR;
        return 0;
    }
    if (r.is_nan) {
        *error = TC_CE_CONST_FLOAT_INVALID;
        return 0;
    }
    if (r.is_inf && !a.is_inf && !b.is_inf) {
        *error = TC_CE_CONST_OVERFLOW;
        return 0;
    }
    if ((op == TC_MUL || op == TC_DIV) && r.is_subnormal) {
        if (!ct_float_op_exact(width, op, a_bits, b_bits, result)) {
            *error = TC_CE_CONST_FLOAT_UNDERFLOW;
            return 0;
        }
    } else if ((op == TC_MUL || op == TC_DIV) && r.is_zero && !a.is_zero &&
               !b.is_zero && !a.is_inf && !b.is_inf) {
        *error = TC_CE_CONST_FLOAT_UNDERFLOW;
        return 0;
    }
    *out = result;
    return 1;
}

static int ct_float_to_int(uint64_t bits, unsigned source_width,
                           const TcType *target, TcAnalysis *an,
                           uint64_t *out) {
    CtFParts parts;
    unsigned width = ct_bits(an, target);
    uint64_t magnitude;
    if (!ct_fp_parts(bits, source_width, &parts) || parts.is_nan ||
        parts.is_inf || width == 0 || width > 64u) {
        return 0;
    }
    if (parts.is_zero) {
        *out = 0;
        return 1;
    }
    if (parts.scale >= 0) {
        unsigned shift = (unsigned)parts.scale;
        if (shift >= 64u || parts.mantissa > (UINT64_MAX >> shift)) {
            return 0;
        }
        magnitude = parts.mantissa << shift;
    } else {
        unsigned shift = (unsigned)(-parts.scale);
        magnitude = shift >= 64u ? 0 : parts.mantissa >> shift;
    }
    if (tc_type_is_signed_integer(target)) {
        uint64_t limit = width >= 64u ? (UINT64_C(1) << 63)
                                      : (UINT64_C(1) << (width - 1u));
        if (parts.sign != 0) {
            if (magnitude > limit) {
                return 0;
            }
        } else if (magnitude >= limit) {
            return 0;
        }
        *out = parts.sign != 0 ? (UINT64_C(0) - magnitude) : magnitude;
    } else {
        if (parts.sign != 0 && magnitude != 0) {
            return 0;
        }
        if (width < 64u && magnitude > ct_mask(width)) {
            return 0;
        }
        *out = magnitude;
    }
    *out &= ct_mask(width);
    return 1;
}

static int ct_int_to_float(uint64_t bits, const TcType *source,
                           const TcType *target, TcAnalysis *an,
                           uint64_t *out) {
    unsigned width = ct_bits(an, target);
    if (width != 32u && width != 64u) {
        return 0;
    }
    if (ct_type_is_bool(source) || tc_type_is_integer(source)) {
        unsigned sw = ct_bits(an, source);
        if (sw == 0 || sw > 64u) {
            return 0;
        }
        if (tc_type_is_signed_integer(source)) {
            int64_t value = ct_signed_value(bits, sw);
            *out = width == 32 ? ct_f32_to_bits((float)value)
                               : ct_f64_to_bits((double)value);
        } else {
            uint64_t value = bits & ct_mask(sw);
            *out = width == 32 ? ct_f32_to_bits((float)value)
                               : ct_f64_to_bits((double)value);
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Evaluation context helpers.                                               */
/* ------------------------------------------------------------------------- */

static int ct_fail(CtScope *sc, TcDiagCode code) {
    sc->failed = 1;
    if (!sc->ct->has_failure) {
        sc->ct->has_failure = 1;
        sc->ct->failure.code = code;
        sc->ct->failure.span = sc->decl_span;
    }
    return 0;
}

static int ct_internal(CtScope *sc) {
    sc->ct->internal = 1;
    return 0;
}

static const TcType *ct_type_arg(CtScope *sc, const TcParseInstr *instr,
                                 size_t index) {
    if (index >= instr->nargs) {
        return NULL;
    }
    return tc_sem_resolve_type_arg(sc->an, sc->module,
                                   (const TcParseArg *)&instr->args[index]);
}

static int ct_eval_decl_slot(CtScope *sc, size_t slot,
                             const TcCtValue **out);
static int ct_eval_local_slot(CtScope *sc, size_t slot,
                              const TcCtValue **out);

static int ct_select_fields(CtScope *sc, const TcCtValue *base,
                            const char **fields, size_t nfields,
                            const TcCtValue **out) {
    const TcCtValue *cur = base;
    size_t i;
    for (i = 0; i < nfields; i++) {
        TcStructDef *def;
        const TcField *field;
        size_t index;
        if (cur == NULL || cur->kind != TC_CT_STRUCT) {
            return ct_internal(sc);
        }
        def = tc_sem_struct_for_type(sc->an, cur->type);
        if (def == NULL) {
            return ct_internal(sc);
        }
        field = tc_sem_find_field(def, fields[i]);
        if (field == NULL) {
            return ct_internal(sc);
        }
        index = (size_t)(field - def->fields);
        if (index >= cur->nfields || cur->fields[index] == NULL) {
            return ct_internal(sc);
        }
        cur = cur->fields[index];
    }
    *out = cur;
    return 1;
}

/* Find the local const named `name` visible at the current statement. */
static int ct_find_local_entry(CtScope *sc, const char *name) {
    size_t i;
    for (i = 0; i < sc->nbody; i++) {
        const TcParseStmt *stmt = &sc->body[i];
        size_t j;
        if (stmt->kind != TC_STMT_CONST ||
            strcmp(stmt->cnst.name, name) != 0) {
            continue;
        }
        if (i >= sc->stmt_index) {
            return -1;
        }
        for (j = 0; j < sc->ct->nlocals; j++) {
            TcCtLocal *local = &sc->ct->locals[j];
            if (local->module == sc->module &&
                local->func_member == sc->func_member &&
                local->stmt_index == i) {
                return (int)j;
            }
        }
        return -1;
    }
    return -1;
}

static int ct_eval_name(CtScope *sc, const TcParseValue *v,
                        const TcCtValue **out) {
    const TcCtValue *base = NULL;
    if (sc->body != NULL) {
        int local = ct_find_local_entry(sc, v->base);
        if (local >= 0) {
            if (!ct_eval_local_slot(sc, (size_t)local, &base)) {
                return 0;
            }
            return ct_select_fields(sc, base, v->fields, v->nfields, out);
        }
    }
    {
        TcMember *member = NULL;
        uint32_t module = 0;
        const char **fields = NULL;
        size_t nfields = 0;
        size_t slot;
        if (!tc_sem_resolve_const_path(sc->an, sc->module, v, &member,
                                       &module, &fields, &nfields)) {
            return ct_internal(sc);
        }
        for (slot = 0; slot < sc->ct->ndecls; slot++) {
            if (sc->ct->decls[slot].module == module &&
                sc->ct->decls[slot].decl_index == member->decl_index) {
                break;
            }
        }
        if (slot >= sc->ct->ndecls) {
            return ct_internal(sc);
        }
        if (!ct_eval_decl_slot(sc, slot, &base)) {
            return 0;
        }
        return ct_select_fields(sc, base, fields, nfields, out);
    }
}

static int ct_eval_value(CtScope *sc, const TcParseValue *v,
                         const TcType *expected, const TcCtValue **out) {
    switch (v->kind) {
    case TC_PVAL_INT: {
        unsigned width;
        uint64_t bits;
        if (expected == NULL || !tc_type_is_integer(expected)) {
            return ct_internal(sc);
        }
        width = ct_bits(sc->an, expected);
        bits = v->int_magnitude & ct_mask(width);
        if (v->int_negative && bits != 0) {
            bits = (UINT64_C(0) - bits) & ct_mask(width);
        }
        *out = ct_scalar(sc->ct, expected, bits);
        return 1;
    }
    case TC_PVAL_BOOL:
        if (expected == NULL || !ct_type_is_bool(expected)) {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, expected, v->bool_value != 0 ? 1u : 0u);
        return 1;
    case TC_PVAL_FLOAT: {
        unsigned width;
        if (expected == NULL || !tc_type_is_float(expected)) {
            return ct_internal(sc);
        }
        width = ct_bits(sc->an, expected);
        if (width != v->float_width) {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, expected, v->float_bits);
        return 1;
    }
    case TC_PVAL_INF:
        if (expected == NULL || !tc_type_is_float(expected)) {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, expected,
                         ct_inf_bits(ct_bits(sc->an, expected),
                                     v->inf_negative));
        return 1;
    case TC_PVAL_NAN:
        if (expected == NULL || !tc_type_is_float(expected)) {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, expected,
                         ct_nan_bits(ct_bits(sc->an, expected)));
        return 1;
    case TC_PVAL_NULLPTR:
        if (expected == NULL ||
            (!ct_type_is_ptr(expected) && !ct_type_is_fnptr(expected))) {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, expected, 0);
        return 1;
    case TC_PVAL_NAME:
        return ct_eval_name(sc, v, out);
    default:
        return ct_internal(sc);
    }
}

static int ct_eval_operand(CtScope *sc, const TcParseInstr *instr,
                           size_t index, const TcType *type,
                           const TcCtValue **out) {
    if (index >= instr->nargs || instr->args[index].kind != TC_PARG_VALUE) {
        return ct_internal(sc);
    }
    return ct_eval_value(sc, &instr->args[index].value, type, out);
}

/* Typed source operand for cast/trunc/bitcast (standard §6.6.3). */
static int ct_eval_source(CtScope *sc, const TcParseInstr *instr,
                          const TcType *target, const TcCtValue **out) {
    const TcParseValue *v;
    const TcType *source = NULL;
    if (instr->nargs < 2 || instr->args[1].kind != TC_PARG_VALUE) {
        return ct_internal(sc);
    }
    v = &instr->args[1].value;
    if (v->kind == TC_PVAL_NAME) {
        return ct_eval_name(sc, v, out);
    }
    switch (v->kind) {
    case TC_PVAL_INT: {
        TcTypeTag tag;
        unsigned width = ct_bits(sc->an, target);
        if (instr->op == TC_BITCAST) {
            switch (width) {
            case 8:
                tag = v->int_unsigned_suffix ? TC_TY_U8 : TC_TY_I8;
                break;
            case 16:
                tag = v->int_unsigned_suffix ? TC_TY_U16 : TC_TY_I16;
                break;
            case 32:
                tag = v->int_unsigned_suffix ? TC_TY_U32 : TC_TY_I32;
                break;
            default:
                tag = v->int_unsigned_suffix ? TC_TY_U64 : TC_TY_I64;
                break;
            }
        } else {
            tag = v->int_unsigned_suffix ? TC_TY_U64 : TC_TY_I64;
        }
        source = scalar_type(sc->an, tag);
        break;
    }
    case TC_PVAL_FLOAT:
        source = scalar_type(sc->an,
                             v->float_width == 32 ? TC_TY_F32 : TC_TY_F64);
        break;
    case TC_PVAL_BOOL:
        source = scalar_type(sc->an, TC_TY_BOOL);
        break;
    case TC_PVAL_INF:
    case TC_PVAL_NAN:
        source = scalar_type(sc->an,
                             ct_bits(sc->an, target) == 32 ? TC_TY_F32
                                                           : TC_TY_F64);
        break;
    default:
        return ct_internal(sc);
    }
    if (source == NULL) {
        return ct_internal(sc);
    }
    return ct_eval_value(sc, v, source, out);
}

static int ct_cast_value(CtScope *sc, const TcType *target,
                         const TcCtValue *source, const TcCtValue **out) {
    uint64_t bits = 0;
    unsigned tw = ct_bits(sc->an, target);
    if (source == NULL || ct_type_is_void(source->type)) {
        return ct_internal(sc);
    }
    if (ct_type_is_bool(target)) {
        if (tc_type_is_float(source->type)) {
            unsigned sw = ct_bits(sc->an, source->type);
            bits = (sw == 32 ? (ct_bits_to_f32(source->bits) != 0.0f)
                             : (ct_bits_to_f64(source->bits) != 0.0))
                       ? 1u
                       : 0u;
        } else {
            bits = source->bits != 0 ? 1u : 0u;
        }
    } else if (tc_type_is_integer(target)) {
        if (tc_type_is_float(source->type)) {
            if (!ct_float_to_int(source->bits,
                                 ct_bits(sc->an, source->type), target,
                                 sc->an, &bits)) {
                return ct_fail(sc, TC_CE_CONST_CAST);
            }
        } else if (ct_type_is_bool(source->type)) {
            bits = source->bits != 0 ? 1u : 0u;
        } else if (tc_type_is_integer(source->type)) {
            unsigned sw = ct_bits(sc->an, source->type);
            if (sw == 0 || sw > 64u || tw == 0 || tw > 64u) {
                return ct_internal(sc);
            }
            if (tc_type_is_signed_integer(source->type)) {
                int64_t value = ct_signed_value(source->bits, sw);
                if (tc_type_is_signed_integer(target)) {
                    if (!ct_signed_fits(value, tw)) {
                        return ct_fail(sc, TC_CE_CONST_CAST);
                    }
                } else if (value < 0 ||
                           (uint64_t)value > ct_mask(tw)) {
                    return ct_fail(sc, TC_CE_CONST_CAST);
                }
                bits = (uint64_t)value;
            } else {
                uint64_t value = source->bits & ct_mask(sw);
                if (tc_type_is_signed_integer(target)) {
                    uint64_t max = tw >= 64u ? (uint64_t)INT64_MAX
                                             : (ct_mask(tw) >> 1u);
                    if (value > max) {
                        return ct_fail(sc, TC_CE_CONST_CAST);
                    }
                } else if (tw < 64u && value > ct_mask(tw)) {
                    return ct_fail(sc, TC_CE_CONST_CAST);
                }
                bits = value;
            }
        } else {
            return ct_internal(sc);
        }
    } else if (tc_type_is_float(target)) {
        if (tc_type_is_float(source->type)) {
            unsigned sw = ct_bits(sc->an, source->type);
            CtFParts parts;
            if (sw == tw) {
                bits = source->bits;
            } else if (sw == 32 && tw == 64) {
                ct_fp_parts(source->bits, 32, &parts);
                if (parts.is_nan) {
                    bits = ct_nan_bits(64);
                } else if (parts.is_inf) {
                    bits = ct_inf_bits(64, parts.sign);
                } else {
                    bits = ct_f64_to_bits((double)ct_bits_to_f32(
                        source->bits));
                }
            } else if (sw == 64 && tw == 32) {
                ct_fp_parts(source->bits, 64, &parts);
                if (parts.is_nan) {
                    bits = ct_nan_bits(32);
                } else if (parts.is_inf) {
                    bits = ct_inf_bits(32, parts.sign);
                } else {
                    double original = ct_bits_to_f64(source->bits);
                    float narrowed = (float)original;
                    if (isinf(narrowed) && !isinf(original)) {
                        return ct_fail(sc, TC_CE_CONST_CAST);
                    }
                    bits = ct_f32_to_bits(narrowed);
                }
            } else {
                return ct_internal(sc);
            }
        } else if (ct_type_is_bool(source->type) ||
                   tc_type_is_integer(source->type)) {
            if (!ct_int_to_float(source->bits, source->type, target, sc->an,
                                 &bits)) {
                return ct_internal(sc);
            }
        } else {
            return ct_internal(sc);
        }
    } else {
        return ct_internal(sc);
    }
    *out = ct_scalar(sc->ct, target, bits);
    return 1;
}

static int ct_trunc_value(CtScope *sc, const TcType *target,
                          const TcCtValue *source, const TcCtValue **out) {
    unsigned tw = ct_bits(sc->an, target);
    if (!tc_type_is_integer(target) || source == NULL ||
        !tc_type_is_integer(source->type)) {
        return ct_internal(sc);
    }
    *out = ct_scalar(sc->ct, target, source->bits & ct_mask(tw));
    return 1;
}

static int ct_bitcast_value(CtScope *sc, const TcType *target,
                            const TcCtValue *source, const TcCtValue **out) {
    unsigned tw = ct_bits(sc->an, target);
    unsigned sw;
    if (source == NULL) {
        return ct_internal(sc);
    }
    sw = ct_bits(sc->an, source->type);
    if (tw == 0 || tw != sw) {
        return ct_internal(sc);
    }
    *out = ct_scalar(sc->ct, target, source->bits);
    return 1;
}

static int ct_eval_make(CtScope *sc, const TcParseInstr *instr,
                        const TcType *expected, const TcCtValue **out) {
    const TcType *type = ct_type_arg(sc, instr, 0);
    TcStructDef *def;
    TcCtValue *value;
    size_t i;
    int named;
    if (type == NULL || !ct_type_is_struct(type)) {
        return ct_internal(sc);
    }
    def = tc_sem_struct_for_type(sc->an, type);
    if (def == NULL) {
        return ct_internal(sc);
    }
    if (expected != NULL && !tc_type_equal(expected, type)) {
        return ct_internal(sc);
    }
    value = ct_value_alloc(sc->ct, type);
    value->kind = TC_CT_STRUCT;
    value->nfields = def->nfields;
    value->fields = (TcCtValue **)ct_alloc(
        sc->ct, (def->nfields == 0 ? 1u : def->nfields) *
                    sizeof(TcCtValue *));
    named = instr->nargs > 1 && instr->args[1].name != NULL;
    if (named) {
        for (i = 1; i < instr->nargs; i++) {
            const TcParseArg *arg = &instr->args[i];
            const TcField *field;
            size_t index;
            if (arg->kind != TC_PARG_VALUE || arg->name == NULL) {
                return ct_internal(sc);
            }
            field = tc_sem_find_field(def, arg->name);
            if (field == NULL) {
                return ct_internal(sc);
            }
            index = (size_t)(field - def->fields);
            if (index >= def->nfields) {
                return ct_internal(sc);
            }
            {
                const TcCtValue *field_value = NULL;
                if (!ct_eval_value(sc, &arg->value, field->type,
                                   &field_value)) {
                    return 0;
                }
                value->fields[index] = (TcCtValue *)field_value;
            }
        }
    } else {
        if (instr->nargs - 1u != def->nfields) {
            return ct_internal(sc);
        }
        for (i = 0; i < def->nfields; i++) {
            const TcParseArg *arg = &instr->args[i + 1u];
            if (arg->kind != TC_PARG_VALUE || arg->name != NULL) {
                return ct_internal(sc);
            }
            {
                const TcCtValue *field_value = NULL;
                if (!ct_eval_value(sc, &arg->value, def->fields[i].type,
                                   &field_value)) {
                    return 0;
                }
                value->fields[i] = (TcCtValue *)field_value;
            }
        }
    }
    for (i = 0; i < def->nfields; i++) {
        if (value->fields[i] == NULL) {
            return ct_internal(sc);
        }
    }
    *out = value;
    return 1;
}

static int ct_eval_op(CtScope *sc, const TcParseInstr *instr,
                      const TcType *expected, const TcCtValue **out) {
    const TcType *type;
    const TcCtValue *a = NULL;
    const TcCtValue *b = NULL;
    uint64_t bits = 0;
    TcDiagCode error = CT_NO_ERROR;
    switch (instr->op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
        type = ct_type_arg(sc, instr, 0);
        if (type == NULL) {
            return ct_internal(sc);
        }
        if (!ct_eval_operand(sc, instr, 1, type, &a)) {
            return 0;
        }
        if (!ct_eval_operand(sc, instr, 2, type, &b)) {
            return 0;
        }
        if (tc_type_is_float(type)) {
            if (!ct_float_binop(sc->an, instr->op, type, a->bits, b->bits,
                                &bits, &error)) {
                return error != CT_NO_ERROR ? ct_fail(sc, error)
                                            : ct_internal(sc);
            }
        } else if (ct_type_is_bool(type)) {
            uint64_t x = a->bits != 0 ? 1u : 0u;
            uint64_t y = b->bits != 0 ? 1u : 0u;
            if (instr->op == TC_AND) {
                bits = x & y;
            } else if (instr->op == TC_OR) {
                bits = x | y;
            } else if (instr->op == TC_XOR) {
                bits = x ^ y;
            } else {
                return ct_internal(sc);
            }
        } else if (tc_type_is_integer(type)) {
            if (!ct_int_binop(sc->an, instr->op, type, a->bits, b->bits,
                              &bits, &error)) {
                return error != CT_NO_ERROR ? ct_fail(sc, error)
                                            : ct_internal(sc);
            }
        } else {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, type, bits);
        return 1;
    case TC_NEG:
    case TC_ABS:
    case TC_NOT:
        type = ct_type_arg(sc, instr, 0);
        if (type == NULL) {
            return ct_internal(sc);
        }
        if (!ct_eval_operand(sc, instr, 1, type, &a)) {
            return 0;
        }
        if (tc_type_is_float(type)) {
            unsigned width = ct_bits(sc->an, type);
            uint64_t sign = UINT64_C(1) << (width - 1u);
            if (instr->op == TC_NOT) {
                return ct_internal(sc);
            }
            bits = instr->op == TC_NEG ? (a->bits ^ sign) : (a->bits & ~sign);
        } else if (ct_type_is_bool(type)) {
            if (instr->op != TC_NOT) {
                return ct_internal(sc);
            }
            bits = a->bits != 0 ? 0u : 1u;
        } else if (tc_type_is_integer(type)) {
            unsigned width = ct_bits(sc->an, type);
            uint64_t mask = ct_mask(width);
            if (instr->op == TC_NOT) {
                bits = (~a->bits) & mask;
            } else if (!tc_type_is_signed_integer(type)) {
                bits = instr->op == TC_NEG ? ((UINT64_C(0) - a->bits) & mask)
                                           : (a->bits & mask);
            } else {
                int64_t sv = ct_signed_value(a->bits, width);
                uint64_t min_bits = UINT64_C(1) << (width - 1u);
                if ((a->bits & mask) == min_bits) {
                    return ct_fail(sc, TC_CE_CONST_OVERFLOW);
                }
                bits = instr->op == TC_NEG
                           ? ((UINT64_C(0) - a->bits) & mask)
                           : ((uint64_t)(sv < 0 ? -sv : sv) & mask);
            }
        } else {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, type, bits);
        return 1;
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE: {
        int result = 0;
        type = ct_type_arg(sc, instr, 0);
        if (type == NULL) {
            return ct_internal(sc);
        }
        if (!ct_eval_operand(sc, instr, 1, type, &a)) {
            return 0;
        }
        if (!ct_eval_operand(sc, instr, 2, type, &b)) {
            return 0;
        }
        if (tc_type_is_float(type)) {
            unsigned width = ct_bits(sc->an, type);
            if (width == 32) {
                float fa = ct_bits_to_f32(a->bits);
                float fb = ct_bits_to_f32(b->bits);
                switch (instr->op) {
                case TC_EQ: result = fa == fb; break;
                case TC_NE: result = fa != fb; break;
                case TC_LT: result = fa < fb; break;
                case TC_LE: result = fa <= fb; break;
                case TC_GT: result = fa > fb; break;
                default: result = fa >= fb; break;
                }
            } else {
                double da = ct_bits_to_f64(a->bits);
                double db = ct_bits_to_f64(b->bits);
                switch (instr->op) {
                case TC_EQ: result = da == db; break;
                case TC_NE: result = da != db; break;
                case TC_LT: result = da < db; break;
                case TC_LE: result = da <= db; break;
                case TC_GT: result = da > db; break;
                default: result = da >= db; break;
                }
            }
        } else if (ct_type_is_bool(type)) {
            if (instr->op != TC_EQ && instr->op != TC_NE) {
                return ct_internal(sc);
            }
            result = instr->op == TC_EQ ? (a->bits == b->bits)
                                        : (a->bits != b->bits);
        } else if (tc_type_is_integer(type)) {
            unsigned width = ct_bits(sc->an, type);
            if (tc_type_is_signed_integer(type)) {
                int64_t sa = ct_signed_value(a->bits, width);
                int64_t sb = ct_signed_value(b->bits, width);
                switch (instr->op) {
                case TC_EQ: result = sa == sb; break;
                case TC_NE: result = sa != sb; break;
                case TC_LT: result = sa < sb; break;
                case TC_LE: result = sa <= sb; break;
                case TC_GT: result = sa > sb; break;
                default: result = sa >= sb; break;
                }
            } else {
                uint64_t ua = a->bits & ct_mask(width);
                uint64_t ub = b->bits & ct_mask(width);
                switch (instr->op) {
                case TC_EQ: result = ua == ub; break;
                case TC_NE: result = ua != ub; break;
                case TC_LT: result = ua < ub; break;
                case TC_LE: result = ua <= ub; break;
                case TC_GT: result = ua > ub; break;
                default: result = ua >= ub; break;
                }
            }
        } else {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, scalar_type(sc->an, TC_TY_BOOL),
                         result != 0 ? 1u : 0u);
        return 1;
    }
    case TC_CAST:
    case TC_TRUNC:
    case TC_BITCAST: {
        const TcCtValue *source = NULL;
        type = ct_type_arg(sc, instr, 0);
        if (type == NULL) {
            return ct_internal(sc);
        }
        if (!ct_eval_source(sc, instr, type, &source)) {
            return 0;
        }
        if (instr->op == TC_CAST) {
            return ct_cast_value(sc, type, source, out);
        }
        if (instr->op == TC_TRUNC) {
            return ct_trunc_value(sc, type, source, out);
        }
        return ct_bitcast_value(sc, type, source, out);
    }
    case TC_SIZEOF: {
        int ok = 0;
        uint64_t size;
        type = ct_type_arg(sc, instr, 0);
        if (type == NULL) {
            return ct_internal(sc);
        }
        size = tc_sem_type_size(sc->an, type, &ok);
        if (!ok) {
            return ct_internal(sc);
        }
        if (expected != NULL &&
            !tc_type_equal(expected, scalar_type(sc->an, TC_TY_USIZE))) {
            return ct_internal(sc);
        }
        *out = ct_scalar(sc->ct, scalar_type(sc->an, TC_TY_USIZE), size);
        return 1;
    }
    case TC_MAKE:
        return ct_eval_make(sc, instr, expected, out);
    default:
        return ct_internal(sc);
    }
}

/* ------------------------------------------------------------------------- */
/* Declaration and local evaluation.                                         */
/* ------------------------------------------------------------------------- */

static int ct_eval_init(CtScope *sc, const TcParseConstInit *init,
                        const TcType *expected, const TcCtValue **out) {
    if (init->is_op) {
        return ct_eval_op(sc, &init->op, expected, out);
    }
    return ct_eval_value(sc, &init->value, expected, out);
}

static int ct_eval_decl_slot(CtScope *sc, size_t slot,
                             const TcCtValue **out) {
    TcCt *ct = sc->ct;
    TcCtDecl *decl = &ct->decls[slot];
    CtScope inner;
    const TcCtValue *value = NULL;
    if (decl->state == TC_CT_SUCCESS) {
        *out = decl->value;
        return 1;
    }
    if (decl->state == TC_CT_FAILURE) {
        if (!ct->has_failure) {
            ct->has_failure = 1;
            ct->failure = decl->diag;
        }
        return 0;
    }
    if (decl->state == TC_CT_EVALUATING) {
        return ct_internal(sc);
    }
    if (sc->depth >= 256u) {
        return ct_internal(sc);
    }
    if (decl->member == NULL || decl->member->value_decl == NULL ||
        decl->member->type == NULL) {
        return ct_internal(sc);
    }
    memset(&inner, 0, sizeof(inner));
    inner.ct = ct;
    inner.an = sc->an;
    inner.module = decl->module;
    inner.func_member = SIZE_MAX;
    inner.decl_span = decl->member->value_decl->name_span;
    inner.depth = sc->depth + 1u;
    decl->state = TC_CT_EVALUATING;
    ct->eval_count++;
    if (!ct_eval_init(&inner, &decl->member->value_decl->init,
                      decl->member->type, &value)) {
        decl->state = TC_CT_FAILURE;
        if (!ct->has_failure) {
            return ct_internal(&inner);
        }
        decl->diag = ct->failure;
        return 0;
    }
    decl->state = TC_CT_SUCCESS;
    decl->value = value;
    *out = value;
    return 1;
}

static int ct_eval_local_slot(CtScope *sc, size_t slot,
                              const TcCtValue **out) {
    TcCt *ct = sc->ct;
    TcCtLocal *local = &ct->locals[slot];
    CtScope inner;
    const TcCtValue *value = NULL;
    if (local->state == TC_CT_SUCCESS) {
        *out = local->value;
        return 1;
    }
    if (local->state == TC_CT_FAILURE) {
        if (!ct->has_failure) {
            ct->has_failure = 1;
            ct->failure = local->diag;
        }
        return 0;
    }
    if (local->state == TC_CT_EVALUATING) {
        return ct_internal(sc);
    }
    if (sc->depth >= 256u) {
        return ct_internal(sc);
    }
    if (local->stmt == NULL || local->type == NULL) {
        return ct_internal(sc);
    }
    memset(&inner, 0, sizeof(inner));
    inner.ct = ct;
    inner.an = sc->an;
    inner.module = local->module;
    inner.body = sc->body;
    inner.nbody = sc->nbody;
    inner.func_member = local->func_member;
    inner.stmt_index = local->stmt_index;
    inner.decl_span = local->stmt->cnst.name_span;
    inner.depth = sc->depth + 1u;
    local->state = TC_CT_EVALUATING;
    ct->eval_count++;
    if (!ct_eval_init(&inner, &local->stmt->cnst.init, local->type, &value)) {
        local->state = TC_CT_FAILURE;
        if (!ct->has_failure) {
            return ct_internal(&inner);
        }
        local->diag = ct->failure;
        return 0;
    }
    local->state = TC_CT_SUCCESS;
    local->value = value;
    *out = value;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Start-order tables and the driver.                                        */
/* ------------------------------------------------------------------------- */

static TcMember *value_member(TcModuleSem *module, size_t decl_index) {
    size_t i;
    for (i = 0; i < module->nmembers; i++) {
        TcMember *member = &module->members[i];
        if ((member->kind == TC_MEM_CONST || member->kind == TC_MEM_STATIC) &&
            member->decl_index == decl_index) {
            return member;
        }
    }
    return NULL;
}

static size_t func_member_index(TcModuleSem *module, const TcParseFunc *func) {
    size_t i;
    for (i = 0; i < module->nmembers; i++) {
        if (module->members[i].kind == TC_MEM_FUNC &&
            module->members[i].func == func) {
            return i;
        }
    }
    return SIZE_MAX;
}

static const TcType *local_const_type(TcAnalysis *an, uint32_t module,
                                      const TcParseStmt *stmt) {
    TcParseArg arg;
    memset(&arg, 0, sizeof(arg));
    arg.kind = TC_PARG_TYPE;
    arg.type = stmt->cnst.type;
    return tc_sem_resolve_type_arg(an, module, &arg);
}

static int count_locals(TcAnalysis *an) {
    size_t total = 0;
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcParseModule *parse = an->modules[mi].info->parse;
        size_t i;
        if (parse->is_program) {
            for (i = 0; i < parse->nbody; i++) {
                if (parse->body[i].kind == TC_STMT_CONST) {
                    total++;
                }
            }
        } else {
            size_t f;
            for (f = 0; f < parse->nfuncs; f++) {
                for (i = 0; i < parse->funcs[f].nbody; i++) {
                    if (parse->funcs[f].body[i].kind == TC_STMT_CONST) {
                        total++;
                    }
                }
            }
        }
    }
    return total;
}

static void add_locals(TcCt *ct, uint32_t module, const TcParseStmt *body,
                       size_t nbody, size_t func_member, size_t *li) {
    size_t i;
    for (i = 0; i < nbody; i++) {
        const TcParseStmt *stmt = &body[i];
        TcCtLocal *local;
        if (stmt->kind != TC_STMT_CONST) {
            continue;
        }
        local = &ct->locals[(*li)++];
        local->module = module;
        local->func_member = func_member;
        local->stmt_index = i;
        local->stmt = stmt;
        local->type = local_const_type(ct->an, module, stmt);
        local->state = TC_CT_EMPTY;
    }
}

static void build_tables(TcCt *ct) {
    TcAnalysis *an = ct->an;
    size_t ndecls = 0;
    size_t nlocals = count_locals(an);
    size_t mi;
    size_t di = 0;
    size_t li = 0;
    for (mi = 0; mi < an->nmodules; mi++) {
        ndecls += an->modules[mi].info->parse->nconsts;
    }
    ct->decls = (TcCtDecl *)ct_alloc(
        ct, (ndecls == 0 ? 1u : ndecls) * sizeof(TcCtDecl));
    ct->ndecls = ndecls;
    ct->locals = (TcCtLocal *)ct_alloc(
        ct, (nlocals == 0 ? 1u : nlocals) * sizeof(TcCtLocal));
    ct->nlocals = nlocals;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        TcParseModule *parse = module->info->parse;
        size_t i;
        for (i = 0; i < parse->nconsts; i++) {
            ct->decls[di].module = (uint32_t)mi;
            ct->decls[di].decl_index = i;
            ct->decls[di].member = value_member(module, i);
            ct->decls[di].state = TC_CT_EMPTY;
            di++;
        }
        if (parse->is_program) {
            add_locals(ct, (uint32_t)mi, parse->body, parse->nbody, SIZE_MAX,
                       &li);
        } else {
            size_t f;
            for (f = 0; f < parse->nfuncs; f++) {
                add_locals(ct, (uint32_t)mi, parse->funcs[f].body,
                           parse->funcs[f].nbody,
                           func_member_index(module, &parse->funcs[f]), &li);
            }
        }
    }
}

static int eval_all_decls(TcCt *ct) {
    size_t i;
    for (i = 0; i < ct->ndecls; i++) {
        CtScope sc;
        const TcCtValue *value = NULL;
        if (ct->decls[i].state == TC_CT_SUCCESS ||
            ct->decls[i].state == TC_CT_FAILURE) {
            continue;
        }
        memset(&sc, 0, sizeof(sc));
        sc.ct = ct;
        sc.an = ct->an;
        sc.module = ct->decls[i].module;
        sc.func_member = SIZE_MAX;
        if (!ct_eval_decl_slot(&sc, i, &value)) {
            return 0;
        }
    }
    return 1;
}

static int eval_all_locals(TcCt *ct) {
    size_t i;
    for (i = 0; i < ct->nlocals; i++) {
        CtScope sc;
        const TcCtValue *value = NULL;
        TcModuleSem *module;
        TcParseStmt *body = NULL;
        size_t nbody = 0;
        if (ct->locals[i].state != TC_CT_EMPTY) {
            continue;
        }
        module = &ct->an->modules[ct->locals[i].module];
        if (module->info->parse->is_program) {
            body = module->info->parse->body;
            nbody = module->info->parse->nbody;
        } else {
            size_t f;
            for (f = 0; f < module->info->parse->nfuncs; f++) {
                if (func_member_index(module,
                                      &module->info->parse->funcs[f]) ==
                    ct->locals[i].func_member) {
                    body = module->info->parse->funcs[f].body;
                    nbody = module->info->parse->funcs[f].nbody;
                    break;
                }
            }
        }
        if (body == NULL) {
            ct->internal = 1;
            return 0;
        }
        memset(&sc, 0, sizeof(sc));
        sc.ct = ct;
        sc.an = ct->an;
        sc.module = ct->locals[i].module;
        sc.body = body;
        sc.nbody = nbody;
        sc.func_member = ct->locals[i].func_member;
        sc.stmt_index = ct->locals[i].stmt_index;
        if (!ct_eval_local_slot(&sc, i, &value)) {
            return 0;
        }
    }
    return 1;
}

int tc_ct_run(TcAnalysis *an, TcCtDiagnostic *out_diag) {
    TcCt *ct;
    if (an == NULL || an->ct != NULL) {
        return -1;
    }
    ct = (TcCt *)tc_arena_alloc0(&an->arena, sizeof(TcCt));
    if (ct == NULL) {
        an->impl = TC_IMPL_OOM;
        return -1;
    }
    memset(ct, 0, sizeof(*ct));
    ct->an = an;
    an->ct = ct;
    build_tables(ct);
    if (!eval_all_decls(ct) || !eval_all_locals(ct) || ct->internal) {
        if (ct->internal || !ct->has_failure) {
            if (an->impl == TC_IMPL_OK) {
                an->impl = TC_IMPL_INTERNAL;
            }
            return -1;
        }
        if (out_diag != NULL) {
            *out_diag = ct->failure;
        }
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Result accessors and byte images.                                         */
/* ------------------------------------------------------------------------- */

const TcCtValue *tc_ct_module_value(const TcAnalysis *an, uint32_t module,
                                    size_t decl_index) {
    const TcCt *ct = an != NULL ? (const TcCt *)an->ct : NULL;
    size_t i;
    if (ct == NULL) {
        return NULL;
    }
    for (i = 0; i < ct->ndecls; i++) {
        if (ct->decls[i].module == module &&
            ct->decls[i].decl_index == decl_index) {
            return ct->decls[i].state == TC_CT_SUCCESS ? ct->decls[i].value
                                                       : NULL;
        }
    }
    return NULL;
}

const TcCtValue *tc_ct_module_value_by_member(const TcAnalysis *an,
                                              uint32_t module,
                                              const TcMember *member) {
    if (member == NULL) {
        return NULL;
    }
    return tc_ct_module_value(an, module, member->decl_index);
}

const TcCtValue *tc_ct_local_value(const TcAnalysis *an, uint32_t module,
                                   size_t func_member, size_t stmt_index) {
    const TcCt *ct = an != NULL ? (const TcCt *)an->ct : NULL;
    size_t i;
    if (ct == NULL) {
        return NULL;
    }
    for (i = 0; i < ct->nlocals; i++) {
        if (ct->locals[i].module == module &&
            ct->locals[i].func_member == func_member &&
            ct->locals[i].stmt_index == stmt_index) {
            return ct->locals[i].state == TC_CT_SUCCESS
                       ? ct->locals[i].value
                       : NULL;
        }
    }
    return NULL;
}

size_t tc_ct_eval_count(const TcAnalysis *an) {
    const TcCt *ct = an != NULL ? (const TcCt *)an->ct : NULL;
    return ct != NULL ? ct->eval_count : 0;
}

int tc_ct_decl_is_static(const TcAnalysis *an, uint32_t module,
                         size_t decl_index) {
    const TcCt *ct = an != NULL ? (const TcCt *)an->ct : NULL;
    size_t i;
    if (ct == NULL) {
        return -1;
    }
    for (i = 0; i < ct->ndecls; i++) {
        if (ct->decls[i].module == module &&
            ct->decls[i].decl_index == decl_index) {
            return ct->decls[i].member != NULL &&
                           ct->decls[i].member->kind == TC_MEM_STATIC
                       ? 1
                       : 0;
        }
    }
    return -1;
}

static void write_scalar_image(uint8_t *bytes, uint64_t bits, unsigned width,
                               int little_endian) {
    unsigned size = width / 8u;
    unsigned i;
    for (i = 0; i < size; i++) {
        unsigned shift = little_endian ? i * 8u : (size - 1u - i) * 8u;
        bytes[i] = (uint8_t)((bits >> shift) & 0xffu);
    }
}

static int fill_image(TcAnalysis *an, const TcCtValue *value, uint8_t *bytes,
                      size_t size, size_t offset) {
    size_t value_size;
    int ok = 0;
    unsigned width;
    int little_endian = an->profile != NULL ? an->profile->little_endian : 1;
    value_size = (size_t)tc_sem_type_size(an, value->type, &ok);
    if (!ok || offset > size || value_size > size - offset) {
        return 0;
    }
    if (value->kind == TC_CT_STRUCT) {
        TcStructDef *def = tc_sem_struct_for_type(an, value->type);
        size_t i;
        if (def == NULL || def->nfields != value->nfields) {
            return 0;
        }
        for (i = 0; i < def->nfields; i++) {
            if (value->fields[i] == NULL ||
                !fill_image(an, value->fields[i], bytes, size,
                            offset + (size_t)def->fields[i].offset)) {
                return 0;
            }
        }
        return 1;
    }
    width = ct_bits(an, value->type);
    if (width == 0 || width != value_size * 8u) {
        return 0;
    }
    write_scalar_image(bytes + offset, value->bits, width, little_endian);
    return 1;
}

const TcInitImage *tc_ct_value_image(TcAnalysis *an, const TcCtValue *value,
                                     int *ok) {
    TcCtValue *mutable_value = (TcCtValue *)value;
    TcInitImage *image;
    uint8_t *bytes;
    size_t size;
    int size_ok = 0;
    if (ok != NULL) {
        *ok = 0;
    }
    if (an == NULL || value == NULL) {
        return NULL;
    }
    if (value->image != NULL) {
        if (ok != NULL) {
            *ok = 1;
        }
        return value->image;
    }
    size = (size_t)tc_sem_type_size(an, value->type, &size_ok);
    if (!size_ok || size == 0) {
        return NULL;
    }
    image = (TcInitImage *)tc_arena_alloc0(&an->arena, sizeof(TcInitImage));
    bytes = (uint8_t *)tc_arena_alloc0(&an->arena, size);
    if (image == NULL || bytes == NULL) {
        /* Image materialization is also a read-only query after Analyze; do
         * not jump into a completed analysis frame. */
        an->impl = TC_IMPL_OOM;
        return NULL;
    }
    if (!fill_image(an, value, bytes, size, 0)) {
        return NULL;
    }
    image->bytes = bytes;
    image->size = size;
    mutable_value->image = image;
    if (ok != NULL) {
        *ok = 1;
    }
    return image;
}
