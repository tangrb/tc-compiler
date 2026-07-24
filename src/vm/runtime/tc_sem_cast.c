/*
 * tc_sem_cast.c — 共享数值转换语义（基于规范化 TcValue 位模式）
 *
 * Analyzer / const_eval / Executor / AOT 均委托此处，保证 strict/truncate 行为一致。
 * 覆盖整数↔整数、整数↔浮点、浮点↔浮点；溢出走 TC_RE_CAST_OVERFLOW。
 */
#include "tc_sem_cast.h"

#include "tc_diagnostic.h"
#include "tc_semantics.h"

#ifdef TC_HAVE_FENV
#include <fenv.h>
#endif
#include <float.h>
#include <math.h>

static int tc_cast_overflow(TcDiagnostic *diag, int line) {
    tc_diagnostic_set(diag, TC_RE_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                      "cast result out of range for target type");
    return -1;
}

static int tc_cast_integer_to_integer(TcTypeKind target, const TcValue *source,
                                      TcValue *out, TcDiagnostic *diag, int line) {
    if (tc_type_is_signed(source->type)) {
        int64_t value = tc_bits_to_signed(source->type, source->bits);
        if (tc_type_is_signed(target)) {
            if (!tc_signed_in_range(value, target)) {
                return tc_cast_overflow(diag, line);
            }
            *out = tc_value_make(target, tc_signed_to_bits(target, value));
            return 0;
        }
        if (value < 0 || (uint64_t)value > tc_type_max_unsigned(target)) {
            return tc_cast_overflow(diag, line);
        }
        *out = tc_value_make(target, (uint64_t)value);
        return 0;
    }

    {
        uint64_t value = tc_value_to_unsigned(source->type, source->bits);
        if (tc_type_is_signed(target)) {
            if (value > (uint64_t)tc_type_max_signed(target)) {
                return tc_cast_overflow(diag, line);
            }
            *out = tc_value_make(target, tc_signed_to_bits(target, (int64_t)value));
            return 0;
        }
        if (!tc_unsigned_in_range(value, target)) {
            return tc_cast_overflow(diag, line);
        }
        *out = tc_value_make(target, value);
        return 0;
    }
}

static double tc_cast_integer_to_double(const TcValue *source) {
    double result = 0.0;
#ifdef TC_HAVE_FENV
    int saved_round = fegetround();
    int restore_round = saved_round != -1 && saved_round != FE_TONEAREST;

    if (restore_round) {
        (void)fesetround(FE_TONEAREST);
    }
#endif
    if (tc_type_is_signed(source->type)) {
        volatile int64_t value = tc_bits_to_signed(source->type, source->bits);
        result = (double)value;
    } else {
        volatile uint64_t value = tc_value_to_unsigned(source->type, source->bits);
        result = (double)value;
    }
#ifdef TC_HAVE_FENV
    if (restore_round) {
        (void)fesetround(saved_round);
    }
#endif
    return result;
}

static int tc_cast_float_to_integer(TcTypeKind target, double value, TcValue *out,
                                    TcDiagnostic *diag, int line) {
    double truncated = 0.0;
    int width = tc_type_bit_width(target);

    if (!isfinite(value)) {
        return tc_cast_overflow(diag, line);
    }
    truncated = trunc(value);
    if (tc_type_is_signed(target)) {
        double lower = -ldexp(1.0, width - 1);
        double upper = ldexp(1.0, width - 1);
        int64_t converted = 0;
        if (truncated < lower || truncated >= upper) {
            return tc_cast_overflow(diag, line);
        }
        converted = (int64_t)truncated;
        *out = tc_value_make(target, tc_signed_to_bits(target, converted));
        return 0;
    }
    {
        double upper = ldexp(1.0, width);
        uint64_t converted = 0;
        if (truncated < 0.0 || truncated >= upper) {
            return tc_cast_overflow(diag, line);
        }
        converted = (uint64_t)truncated;
        *out = tc_value_make(target, converted);
        return 0;
    }
}

static uint64_t tc_canonical_nan_bits(TcTypeKind target) {
    return target == TC_FLOAT32 ? UINT64_C(0x7FC00000)
                                : UINT64_C(0x7FF8000000000000);
}

int tc_exec_cast(TcTypeKind target, const TcValue *source, TcValue *out,
                 TcDiagnostic *diag, int line) {
    TcTypeKind source_type = source->type;

    if (source_type == target) {
        *out = *source;
        return 0;
    }
    if (tc_type_is_bool(source_type)) {
        uint64_t value = source->bits != 0 ? 1ULL : 0ULL;
        if (tc_type_is_bool(target) || tc_type_is_integer(target)) {
            *out = tc_value_make(target, value);
            return 0;
        }
        if (tc_type_is_float(target)) {
            *out = tc_value_make(target, tc_fp_double_to_bits(target, value ? 1.0 : 0.0));
            return 0;
        }
    }
    if (tc_type_is_bool(target)) {
        if (tc_type_is_integer(source_type)) {
            *out = tc_value_make(TC_BOOL,
                                 tc_value_to_unsigned(source_type, source->bits) != 0);
            return 0;
        }
        if (tc_type_is_float(source_type)) {
            double value = tc_fp_bits_to_double(source_type, source->bits);
            *out = tc_value_make(TC_BOOL, value != 0.0);
            return 0;
        }
    }
    if (tc_type_is_integer(source_type) && tc_type_is_integer(target)) {
        return tc_cast_integer_to_integer(target, source, out, diag, line);
    }
    if (tc_type_is_integer(source_type) && tc_type_is_float(target)) {
        double value = tc_cast_integer_to_double(source);
        uint64_t bits = tc_fp_double_to_bits(target, value);
        if (target == TC_FLOAT32 && isinf(tc_fp_bits_to_double(target, bits))) {
            return tc_cast_overflow(diag, line);
        }
        *out = tc_value_make(target, bits);
        return 0;
    }
    if (tc_type_is_float(source_type) && tc_type_is_integer(target)) {
        return tc_cast_float_to_integer(target,
                                        tc_fp_bits_to_double(source_type, source->bits),
                                        out, diag, line);
    }
    if (tc_type_is_float(source_type) && tc_type_is_float(target)) {
        double value = tc_fp_bits_to_double(source_type, source->bits);
        uint64_t bits = 0;
        if (isnan(value)) {
            *out = tc_value_make(target, tc_canonical_nan_bits(target));
            return 0;
        }
        bits = tc_fp_double_to_bits(target, value);
        if (isfinite(value) && isinf(tc_fp_bits_to_double(target, bits))) {
            return tc_cast_overflow(diag, line);
        }
        *out = tc_value_make(target, bits);
        return 0;
    }

    tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                      "incompatible cast types");
    return -1;
}

int tc_exec_truncate(TcTypeKind target, const TcValue *source, TcValue *out,
                     TcDiagnostic *diag, int line) {
    if (!tc_type_is_integer(source->type) || !tc_type_is_integer(target) ||
        tc_type_bit_width(target) >= tc_type_bit_width(source->type)) {
        tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "truncate requires an integer target narrower than the source");
        return -1;
    }
    *out = tc_value_make(target, source->bits);
    return 0;
}

int tc_exec_bitcast(TcTypeKind target, const TcValue *source, TcValue *out,
                    TcDiagnostic *diag, int line) {
    int target_width = tc_type_bit_width(target);
    int source_width = tc_type_bit_width(source->type);

    if (tc_type_is_bool(target) || tc_type_is_bool(source->type) ||
        (!tc_type_is_integer(target) && !tc_type_is_float(target)) ||
        (!tc_type_is_integer(source->type) && !tc_type_is_float(source->type))) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "bitcast requires non-bool integer or float types");
        return -1;
    }
    if (target_width != source_width) {
        tc_diagnostic_set(diag, TC_CE_BITCAST_WIDTH, line, TC_COLUMN_UNKNOWN,
                          "bitcast source and target widths must match");
        return -1;
    }
    *out = tc_value_make(target, source->bits);
    return 0;
}
