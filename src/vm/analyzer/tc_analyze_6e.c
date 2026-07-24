/*
 * tc_analyze_6e.c — I/O 格式检查
 *
 * 6e 子阶段：校验 write/writeln 语句的格式说明符与操作数类型兼容性。
 * 包括标志字符、宽度、精度与转换符的语义约束。
 */
#include "tc_analyze_6e.h"
#include "tc_analyzer_internal.h"
#include "tc_analyzer_pass2.h"
#include "tc_types.h"
#include "tc_symbol.h"
#include "tc_diagnostic.h"

#include <stddef.h>
#include <string.h>

/*
 * @brief 校验格式说明符与操作数类型的匹配关系
 *
 * %d/%i 要求有符号类型；%u 要求无符号类型；%x/%X/%o/%b 无限制
 * 同时校验标志/宽度/精度与转换符的兼容性。
 */
int tc_check_io_format(TcTypeKind type, const TcFormatFullSpec *spec, int line,
                       TcDiagnostic *diag)
{
    TcFormatSpec fmt = spec->spec;

    if (fmt == TC_FMT_NONE) {
        return 0;
    }

    /* 格式控制项标志兼容性 */

    /* '+': 强制符号，不适用于无符号专用格式符 */
    if (spec->flag_plus) {
        if (fmt == TC_FMT_U || fmt == TC_FMT_X || fmt == TC_FMT_XU ||
            fmt == TC_FMT_O || fmt == TC_FMT_B || fmt == TC_FMT_T) {
            tc_diagnostic_set(diag, TC_CE_FORMAT_SPECIFIER, line, TC_COLUMN_UNKNOWN,
                              "'+' flag not supported for this format specifier");
            return -1;
        }
    }

    /* '#' 备用形式：整数/浮点格式符可用 */
    if (spec->flag_hash) {
        if (fmt == TC_FMT_T) {
            tc_diagnostic_set(diag, TC_CE_FORMAT_SPECIFIER, line, TC_COLUMN_UNKNOWN,
                              "'#' flag not supported for %%t");
            return -1;
        }
    }

    /* '0' 零填充与 '-' 左对齐互斥 */
    if (spec->flag_zero && spec->flag_minus) {
        tc_diagnostic_set(diag, TC_CE_FORMAT_SPECIFIER, line, TC_COLUMN_UNKNOWN,
                          "'0' and '-' flags are mutually exclusive");
        return -1;
    }

    /* %t 不支持宽度/精度/任何标志 */
    if (fmt == TC_FMT_T) {
        if (spec->flag_minus || spec->flag_plus || spec->flag_hash || spec->flag_zero ||
            spec->width || spec->precision_set) {
            tc_diagnostic_set(diag, TC_CE_FORMAT_SPECIFIER, line, TC_COLUMN_UNKNOWN,
                              "%%t does not support flags, width, or precision");
            return -1;
        }
    }

    /* 浮点格式符：仅用于浮点类型 */
    if (fmt == TC_FMT_F || fmt == TC_FMT_E || fmt == TC_FMT_EU ||
        fmt == TC_FMT_G || fmt == TC_FMT_GU) {
        if (!tc_type_is_float(type)) {
            tc_diagnostic_set(diag, TC_CE_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "float format specifier requires float type");
            return -1;
        }
        return 0;
    }

    /* 浮点类型不允许整数格式符 */
    if (tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_CE_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "float type requires float format specifier");
        return -1;
    }

    if (fmt == TC_FMT_T) {
        if (!tc_type_is_bool(type)) {
            tc_diagnostic_set(diag, TC_CE_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "%t requires bool type");
            return -1;
        }
        return 0;
    }
    if (tc_type_is_bool(type)) {
        tc_diagnostic_set(diag, TC_CE_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "bool type requires %t format specifier");
        return -1;
    }
    if (fmt == TC_FMT_D || fmt == TC_FMT_I) {
        if (!tc_type_is_signed(type)) {
            tc_diagnostic_set(diag, TC_CE_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "%d requires signed type");
            return -1;
        }
        return 0;
    }
    if (fmt == TC_FMT_U) {
        if (tc_type_is_signed(type)) {
            tc_diagnostic_set(diag, TC_CE_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "%u requires unsigned type");
            return -1;
        }
        return 0;
    }
    if (fmt == TC_FMT_X || fmt == TC_FMT_XU || fmt == TC_FMT_O || fmt == TC_FMT_B) {
        return 0;
    }
    return 0;
}
