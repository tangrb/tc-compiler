/*
 * semantics.c — TC 整数语义运算的实现
 *
 * 本模块是 TC-VM 的语义核心，严格遵循 TC 语言标准：
 *   - 有符号 strict 模式：溢出检测，溢出时报 TC_ERR_INTEGER_OVERFLOW
 *   - 有符号 overflow 模式：按位宽做二进制环绕
 *   - 无符号运算：始终按位宽截断（div/mod 不支持 overflow 关键字）
 *   - cast strict：检查值能否表示为目标类型
 *   - cast overflow：截断或符号/零扩展
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_semantics.h"

#include "tc_diagnostic.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * @brief 生成 n 位全 1 掩码，用于位宽截断
 * @param bit_width  掩码位数
 * @return n 位全 1 的 uint64_t 值；n >= 64 时返回 UINT64_MAX
 * @note 例如 bit_width=8 返回 0xFF，bit_width=16 返回 0xFFFF
 */
uint64_t tc_mask_bits(int bit_width) {
    if (bit_width >= 64) {
        return UINT64_MAX;
    }
    return (1ULL << (unsigned)bit_width) - 1ULL;
}

/*
 * @brief 将无符号位模式按目标有符号类型解释为有符号整数
 * @param type  目标有符号整数类型
 * @param bits  无符号位模式
 * @return 有符号整数值；若最高位为 1，按二补数规则减去 2^n 得到负值
 */
int64_t tc_bits_to_signed(TcIntType type, uint64_t bits) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t masked = bits & mask;
    uint64_t sign_bit = 1ULL << (unsigned)(n - 1);

    if (masked & sign_bit) {
        if (n == 64) {
            return (int64_t)masked;
        }
        /* 非 64 位：手动做符号扩展减法 */
        return (int64_t)(masked - (1ULL << (unsigned)n));
    }
    return (int64_t)masked;
}

/*
 * @brief 将有符号整数编码为目标类型的 n 位无符号位模式
 * @param type  目标整数类型
 * @param value 有符号整数值
 * @return 截断高位后的无符号位模式（保留低 n 位）
 */
uint64_t tc_signed_to_bits(TcIntType type, int64_t value) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    return ((uint64_t)value) & mask;
}

/*
 * @brief 将位模式归一化到目标类型的位宽（无符号视角）
 * @param type  目标整数类型
 * @param bits  原始位模式
 * @return 经位宽掩码掩码后的无符号值，高位被截断
 */
uint64_t tc_value_to_unsigned(TcIntType type, uint64_t bits) {
    return bits & tc_mask_bits(tc_type_bit_width(type));
}

/*
 * @brief 构造运行时值
 * @param type  值的目标整数类型
 * @param bits  位模式值（会自动归一化到目标类型位宽）
 * @return 包含类型和归一化后 bits 的 TcValue 结构体
 */
TcValue tc_value_make(TcIntType type, uint64_t bits) {
    TcValue value;
    value.type = type;
    value.bits = tc_value_to_unsigned(type, bits);
    return value;
}

/*
 * @brief 获取有符号类型的最小可表示值
 * @param type  有符号整数类型
 * @return 最小值：-(2^(n-1))，n 为位宽
 * @note 内部辅助函数，调用方应确保 type 为有符号类型
 */
static int64_t tc_type_min_signed(TcIntType type) {
    int n = tc_type_bit_width(type);
    return -(1LL << (n - 1));
}

/*
 * @brief 获取有符号类型的最大可表示值
 * @param type  有符号整数类型
 * @return 最大值：2^(n-1) - 1，n 为位宽
 * @note 内部辅助函数，调用方应确保 type 为有符号类型
 */
static int64_t tc_type_max_signed(TcIntType type) {
    int n = tc_type_bit_width(type);
    return (1LL << (n - 1)) - 1LL;
}

/*
 * @brief 获取无符号类型的最大可表示值
 * @param type  无符号整数类型
 * @return 最大值：2^n - 1，n 为位宽
 * @note 内部辅助函数，调用方应确保 type 为无符号类型
 */
static uint64_t tc_type_max_unsigned(TcIntType type) {
    return tc_mask_bits(tc_type_bit_width(type));
}

/*
 * @brief 检查无符号字面量 value 能否放入 type 类型
 * @param value 无符号字面量值
 * @param type  目标整数类型
 * @return 可放入返回 1；超出范围返回 0
 * @note 有符号类型时先将 value 转为 int64_t 再检查有符号范围
 */
int tc_literal_fits_type(uint64_t value, TcIntType type) {
    if (tc_type_is_signed(type)) {
        if (value > (uint64_t)INT64_MAX) {
            return 0;
        }
        return tc_signed_in_range((int64_t)value, type);
    }
    return tc_unsigned_in_range(value, type);
}

/*
 * @brief 检查有符号整数值是否在指定类型的可表示范围内
 * @param value 有符号整数值
 * @param type  目标整数类型
 * @return 在范围内返回 1；超出返回 0
 */
int tc_signed_in_range(int64_t value, TcIntType type) {
    return value >= tc_type_min_signed(type) && value <= tc_type_max_signed(type);
}

/*
 * @brief 检查无符号整数值是否在指定类型的可表示范围内
 * @param value 无符号整数值
 * @param type  目标整数类型
 * @return 在范围内返回 1；超出返回 0
 */
int tc_unsigned_in_range(uint64_t value, TcIntType type) {
    return value <= tc_type_max_unsigned(type);
}

/*
 * @brief 有符号加法溢出检测（在 int64 范围内运算）
 * @param a      加数
 * @param b      加数
 * @param result 输出参数，无溢出时写入加法和
 * @return 溢出返回 1；无溢出返回 0
 */
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

/*
 * @brief 有符号减法溢出检测
 * @param a      被减数
 * @param b      减数
 * @param result 输出参数，无溢出时写入减法差
 * @return 溢出返回 1；无溢出返回 0
 */
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

/*
 * @brief 有符号乘法溢出检测
 * @param a      乘数
 * @param b      乘数
 * @param result 输出参数，无溢出时写入乘积
 * @return 溢出返回 1；无溢出返回 0
 * @note 先检查边界条件避免溢出，再进行乘法运算
 */
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

/*
 * @brief 64 位无符号乘法，返回 128 位结果的高/低 64 位
 * @param a  乘数
 * @param b  乘数
 * @param hi 输出参数，乘积的高 64 位
 * @param lo 输出参数，乘积的低 64 位
 * @note 采用 32 位分块乘法（a*b = (a_hi*2^32 + a_lo)*(b_hi*2^32 + b_lo)）
 *       避免直接 64×64 溢出。用于 int64/uint64 乘法时的位宽截断（取低 64 位）
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

/*
 * @brief overflow 模式下将 bits 截断到目标类型位宽
 * @param type  目标整数类型
 * @param bits  原始位模式
 * @return 截断后的位模式（保留低 n 位）
 */
static uint64_t tc_wrap_bits(TcIntType type, uint64_t bits) {
    return tc_value_to_unsigned(type, bits);
}

/*
 * @brief 有符号算术运算核心实现
 * @param op    算术运算符（add/sub/mul/div/mod）
 * @param type  运算的目标整数类型
 * @param mode  溢出处理模式（strict / overflow）
 * @param lhs   左操作数
 * @param rhs   右操作数
 * @param out   输出参数，写入运算结果 TcValue
 * @param diag  诊断对象，出错时填写错误信息
 * @param line  当前语句行号，用于错误定位
 * @return 成功返回 0；失败（除零/溢出）返回 -1
 * @note div/mod 不支持 overflow 模式（由 Analyzer 静态拒绝）
 * @note overflow 模式下 add/sub/mul 在无符号位模式上做环绕运算
 */
static int tc_exec_signed_arith(TcArithOp op, TcIntType type, TcOverflowMode mode,
                                const TcValue *lhs, const TcValue *rhs, TcValue *out,
                                TcDiagnostic *diag, int line) {
    int64_t a = tc_bits_to_signed(type, lhs->bits);
    int64_t b = tc_bits_to_signed(type, rhs->bits);
    int64_t result = 0;
    char msg[128];

    /* 除法与取模：div/mod 始终精确运算，仅检查除零 */
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
        *out = tc_value_make(type, tc_signed_to_bits(type, result));
        return 0;
    }

    /* overflow 模式：在无符号位域上做环绕 */
    if (mode == TC_OVERFLOW) {
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
            /* 乘法：64 位用 128 位乘法取低字；较窄类型直接乘后截断 */
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

/*
 * @brief 无符号算术运算核心实现
 * @param op    算术运算符（add/sub/mul/div/mod）
 * @param type  运算的目标整数类型
 * @param mode  溢出处理模式（无符号运算忽略此参数）
 * @param lhs   左操作数
 * @param rhs   右操作数
 * @param out   输出参数，写入运算结果 TcValue
 * @param diag  诊断对象，出错时填写错误信息
 * @param line  当前语句行号，用于错误定位
 * @return 成功返回 0；失败（除零）返回 -1
 * @note 无符号类型不支持 strict/overflow 区分，始终对结果做位宽掩码截断
 */
static int tc_exec_unsigned_arith(TcArithOp op, TcIntType type, TcOverflowMode mode,
                                    const TcValue *lhs, const TcValue *rhs, TcValue *out,
                                    TcDiagnostic *diag, int line) {
    (void)mode;  /* 无符号运算忽略 overflow 模式参数 */
    uint64_t a = tc_value_to_unsigned(type, lhs->bits);
    uint64_t b = tc_value_to_unsigned(type, rhs->bits);
    uint64_t mask = tc_mask_bits(tc_type_bit_width(type));
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
        result = (a + b) & mask;
    } else if (op == TC_SUB) {
        result = (a - b) & mask;
    } else {
        if (tc_type_bit_width(type) == 64) {
            uint64_t hi = 0;
            uint64_t lo = 0;
            tc_umul64(a, b, &hi, &lo);
            result = lo & mask;
        } else {
            result = (a * b) & mask;
        }
    }

    *out = tc_value_make(type, result);
    return 0;
}

/*
 * @brief 算术运算入口：按有符号/无符号分派到对应实现
 * @param op    算术运算符
 * @param type  运算的目标整数类型
 * @param mode  溢出处理模式
 * @param lhs   左操作数
 * @param rhs   右操作数
 * @param out   输出参数，写入运算结果 TcValue
 * @param diag  诊断对象
 * @param line  当前语句行号
 * @return 成功返回 0；失败返回 -1 并设置 diag
 */
int tc_exec_arith(TcArithOp op, TcIntType type, TcOverflowMode mode,
                  const TcValue *lhs, const TcValue *rhs, TcValue *out,
                  TcDiagnostic *diag, int line) {
    if (tc_type_is_signed(type)) {
        return tc_exec_signed_arith(op, type, mode, lhs, rhs, out, diag, line);
    }
    return tc_exec_unsigned_arith(op, type, mode, lhs, rhs, out, diag, line);
}

/*
 * @brief strict 模式类型转换（带范围检查）
 * @param target 目标整数类型
 * @param source 源运行时值
 * @param out    输出参数，写入转换后的 TcValue
 * @param diag   诊断对象，转换失败时填写错误信息
 * @param line   当前语句行号
 * @return 成功返回 0；值超出目标范围返回 -1
 * @note 根据源/目标类型的有符号性、位宽关系，分别处理：
 *       - 同类型：直接复制
 *       - 扩展（dst > src）：有符号做符号扩展，无符号零扩展
 *       - 同宽变符号性：检查负值或超范围
 *       - 窄化：检查值是否在目标范围内
 */
static int tc_cast_strict(TcIntType target, const TcValue *source, TcValue *out,
                          TcDiagnostic *diag, int line) {
    TcIntType src_type = source->type;
    int src_bits = tc_type_bit_width(src_type);
    int dst_bits = tc_type_bit_width(target);
    int src_signed = tc_type_is_signed(src_type);
    int dst_signed = tc_type_is_signed(target);
    char msg[128];

    if (src_type == target) {
        *out = *source;
        return 0;
    }

    /* 目标位宽更大：扩展 */
    if (dst_bits > src_bits) {
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
        /* 无符号源 → 有符号目标（更宽） */
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

    /* 同位宽但符号性不同 */
    if (dst_bits == src_bits) {
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

    /* 窄化：有符号 → 有符号 */
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

    /* 窄化：无符号 → 无符号（直接截断，strict 下仍合法） */
    if (!src_signed && !dst_signed) {
        uint64_t value = tc_value_to_unsigned(src_type, source->bits);
        *out = tc_value_make(target, tc_wrap_bits(target, value));
        return 0;
    }

    /* 窄化：有符号 → 无符号 */
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

    /* 窄化：无符号 → 有符号 */
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

/*
 * @brief 位扩展辅助：将 src_bits 宽的位模式扩展到 dst_bits
 * @param bits        原始位模式
 * @param src_bits    源位宽
 * @param dst_bits    目标位宽（必须 >= src_bits 才实际扩展）
 * @param sign_extend 符号扩展标志：1 表示符号扩展，0 表示零扩展
 * @return 扩展后的位模式
 * @note sign_extend=1 时若源最高位为 1 则高位填 1（符号扩展），否则零扩展
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
    if (bits & (1ULL << (unsigned)(src_bits - 1))) {
        uint64_t high_mask = ~tc_mask_bits(dst_bits);
        high_mask |= mask;
        return bits | high_mask;
    }
    return bits;
}

/*
 * @brief overflow 模式类型转换（不做范围检查）
 * @param target 目标整数类型
 * @param source 源运行时值
 * @param out    输出参数，写入转换后的 TcValue
 * @return 始终返回 0（overflow 模式下永远不会失败）
 * @note 窄化直接截低 n 位；扩展时根据目标/源符号性选择零扩展或符号扩展
 */
static int tc_cast_overflow(TcIntType target, const TcValue *source, TcValue *out) {
    TcIntType src_type = source->type;
    int src_bits = tc_type_bit_width(src_type);
    int dst_bits = tc_type_bit_width(target);
    uint64_t bits = tc_value_to_unsigned(src_type, source->bits);
    uint64_t result_bits = 0;

    if (dst_bits <= src_bits) {
        result_bits = bits & tc_mask_bits(dst_bits);
    } else if (!tc_type_is_signed(target) || !tc_type_is_signed(src_type)) {
        result_bits = tc_extend_bits(bits, src_bits, dst_bits, 0);
    } else if (bits & (1ULL << (unsigned)(src_bits - 1))) {
        result_bits = tc_extend_bits(bits, src_bits, dst_bits, 1);
    } else {
        result_bits = tc_extend_bits(bits, src_bits, dst_bits, 0);
    }

    *out = tc_value_make(target, result_bits);
    return 0;
}

/*
 * @brief cast 运算入口：按 strict / overflow 模式分派
 * @param target 目标整数类型
 * @param mode   转换模式（strict 或 overflow）
 * @param source 源运行时值
 * @param out    输出参数，写入转换后的 TcValue
 * @param diag   诊断对象（strict 模式出错时使用）
 * @param line   当前语句行号
 * @return 成功返回 0；strict 模式下值溢出返回 -1
 */
int tc_exec_cast(TcIntType target, TcOverflowMode mode, const TcValue *source,
                 TcValue *out, TcDiagnostic *diag, int line) {
    if (mode == TC_STRICT) {
        return tc_cast_strict(target, source, out, diag, line);
    }
    return tc_cast_overflow(target, source, out);
}
