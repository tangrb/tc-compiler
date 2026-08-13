/*
 * tc_sem_int.c — 整数算术（add/sub/mul/div/mod）与单目运算（abs/neg）
 *
 * 有符号严格模式使用两步溢出检测：先验证 int64 运算不溢出，再检查窄类型范围。
 * 无符号算术的截断语义等价于 wrap，mode 参数仅用于签名兼容。
 */
#include "tc_semantics.h"

#include "tc_diagnostic.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  诊断消息格式化（本模块内部）                                          */
/* ------------------------------------------------------------------ */

static int tc_semantics_format_msg(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= size) {
        return -1;
    }
    return 0;
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
/*  有符号算术运算                                                       */
/* ------------------------------------------------------------------ */

static int tc_exec_signed_arith(TcArithOp op, TcTypeTag type, TcWrapMode mode,
                                const TcValue *lhs, const TcValue *rhs, TcValue *out,
                                TcDiagnostic *diag, int line) {
    int64_t a = tc_bits_to_signed(type, lhs->bits);
    int64_t b = tc_bits_to_signed(type, rhs->bits);
    int64_t result = 0;
    char msg[128];

    /* div/mod：除零检查；INT_MIN/-1 时商不可表示（div 报错，§5.2 mod 仍为 0） */
    if (op == TC_DIV || op == TC_MOD) {
        if (b == 0) {
            tc_diagnostic_set(diag, TC_RE_DIVISION_BY_ZERO, line, TC_COLUMN_UNKNOWN,
                              "division by zero");
            return -1;
        }
        if (a == tc_type_min_signed(type) && b == -1) {
            if (op == TC_MOD) {
                *out = tc_value_make(type, 0);
                return 0;
            }
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
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
            if (tc_semantics_format_msg(msg, sizeof(msg), "result out of range for %s",
                                        tc_type_name(type)) != 0) {
                tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                                  "result out of range");
            } else {
                tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN, msg);
            }
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
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "signed addition overflow");
            return -1;
        }
    } else if (op == TC_SUB) {
        if (tc_ssub_overflow(a, b, &result)) {
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "signed subtraction overflow");
            return -1;
        }
    } else if (tc_smul_overflow(a, b, &result)) {
        tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "signed multiplication overflow");
        return -1;
    }

    if (!tc_signed_in_range(result, type)) {
        if (tc_semantics_format_msg(msg, sizeof(msg), "result out of range for %s",
                                    tc_type_name(type)) != 0) {
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "result out of range");
        } else {
            tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN, msg);
        }
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
static int tc_exec_unsigned_arith(TcArithOp op, TcTypeTag type, TcWrapMode mode,
                                    const TcValue *lhs, const TcValue *rhs, TcValue *out,
                                    TcDiagnostic *diag, int line) {
    (void)mode;
    uint64_t a = tc_value_to_unsigned(type, lhs->bits);
    uint64_t b = tc_value_to_unsigned(type, rhs->bits);
    uint64_t result = 0;

    if (op == TC_DIV || op == TC_MOD) {
        if (b == 0) {
            tc_diagnostic_set(diag, TC_RE_DIVISION_BY_ZERO, line, TC_COLUMN_UNKNOWN,
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

int tc_exec_arith(TcArithOp op, TcTypeTag type, TcWrapMode mode,
                  const TcValue *lhs, const TcValue *rhs, TcValue *out,
                  TcDiagnostic *diag, int line) {
    if (tc_validate_arith_mode(op, type, mode, diag, line) != 0) {
        return -1;
    }
    if (tc_type_is_signed(type)) {
        return tc_exec_signed_arith(op, type, mode, lhs, rhs, out, diag, line);
    }
    return tc_exec_unsigned_arith(op, type, mode, lhs, rhs, out, diag, line);
}

/* ------------------------------------------------------------------ */
/*  单目运算：abs / neg                                                  */
/* ------------------------------------------------------------------ */

int tc_exec_unary(TcUnaryOp op, TcTypeTag type, TcWrapMode mode,
                  const TcValue *operand, TcValue *out,
                  TcDiagnostic *diag, int line) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t bits = tc_value_to_unsigned(type, operand->bits);

    if (tc_validate_unary_mode(op, type, mode, diag, line) != 0) {
        return -1;
    }

    if (op == TC_UNARY_ABS) {
        if (tc_type_is_signed(type)) {
            int64_t val = tc_bits_to_signed(type, bits);
            if (val == tc_type_min_signed(type)) {
                tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
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
                tc_diagnostic_set(diag, TC_RE_INTEGER_OVERFLOW, line, TC_COLUMN_UNKNOWN,
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
