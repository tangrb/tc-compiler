/*
 * types.c — TC 整数类型与运算符的工具函数
 *
 * 提供类型位宽查询、有符号/无符号判定、类型名/运算符名解析，
 * 以及错误种类与类型名的字符串化，供 Lexer、Analyzer 和诊断输出使用。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_types.h"

#include <string.h>

/*
 * @brief 返回整数类型的位宽
 * @param type  整数类型枚举值（TcIntType）
 * @return 位宽值：8 / 16 / 32 / 64；非法类型返回 0
 */
int tc_type_bit_width(TcIntType type) {
    switch (type) {
    case TC_INT8:
    case TC_UINT8:
        return 8;
    case TC_INT16:
    case TC_UINT16:
        return 16;
    case TC_INT32:
    case TC_UINT32:
        return 32;
    case TC_INT64:
    case TC_UINT64:
        return 64;
    }
    return 0;
}

/*
 * @brief 判断类型是否为有符号整数
 * @param type  整数类型枚举值
 * @return 有符号类型（int8~int64）返回 1；无符号类型（uint*）返回 0
 */
int tc_type_is_signed(TcIntType type) {
    switch (type) {
    case TC_INT8:
    case TC_INT16:
    case TC_INT32:
    case TC_INT64:
        return 1;
    default:
        return 0;
    }
}

/*
 * @brief 将字符串解析为 TcIntType 枚举值
 * @param text  类型名字符串（如 "int8"、"uint32" 等）
 * @param out   输出参数，解析成功时写入对应的 TcIntType 枚举值
 * @return 解析成功返回 1；无法识别返回 0
 */
int tc_type_parse(const char *text, TcIntType *out) {
    if (strcmp(text, "int8") == 0) {
        *out = TC_INT8;
    } else if (strcmp(text, "uint8") == 0) {
        *out = TC_UINT8;
    } else if (strcmp(text, "int16") == 0) {
        *out = TC_INT16;
    } else if (strcmp(text, "uint16") == 0) {
        *out = TC_UINT16;
    } else if (strcmp(text, "int32") == 0) {
        *out = TC_INT32;
    } else if (strcmp(text, "uint32") == 0) {
        *out = TC_UINT32;
    } else if (strcmp(text, "int64") == 0) {
        *out = TC_INT64;
    } else if (strcmp(text, "uint64") == 0) {
        *out = TC_UINT64;
    } else {
        return 0;
    }
    return 1;
}

/*
 * @brief 将算术运算符关键字解析为 TcArithOp 枚举值
 * @param text  运算符关键字（"add" / "sub" / "mul" / "div" / "mod"）
 * @param out   输出参数，解析成功时写入对应的 TcArithOp 枚举值
 * @return 解析成功返回 1；无法识别返回 0
 */
int tc_arith_op_parse(const char *text, TcArithOp *out) {
    if (strcmp(text, "add") == 0) {
        *out = TC_ADD;
    } else if (strcmp(text, "sub") == 0) {
        *out = TC_SUB;
    } else if (strcmp(text, "mul") == 0) {
        *out = TC_MUL;
    } else if (strcmp(text, "div") == 0) {
        *out = TC_DIV;
    } else if (strcmp(text, "mod") == 0) {
        *out = TC_MOD;
    } else {
        return 0;
    }
    return 1;
}

/*
 * @brief 将错误种类枚举转为对外展示的名称
 * @param kind  错误种类枚举值
 * @return 错误名称字符串（如 "SyntaxError"、"DivisionByZero" 等）；
 *         无法识别的种类返回 "UnknownError"
 */
const char *tc_error_kind_name(TcErrorKind kind) {
    switch (kind) {
    case TC_ERR_SYNTAX:
        return "SyntaxError";
    case TC_ERR_UNDEFINED_VARIABLE:
        return "UndefinedVariable";
    case TC_ERR_DUPLICATE_DEFINITION:
        return "DuplicateDefinition";
    case TC_ERR_TYPE_MISMATCH:
        return "TypeMismatch";
    case TC_ERR_LITERAL_OUT_OF_RANGE:
        return "LiteralOutOfRange";
    case TC_ERR_DIVISION_BY_ZERO:
        return "DivisionByZero";
    case TC_ERR_INTEGER_OVERFLOW:
        return "IntegerOverflow";
    case TC_ERR_OVERFLOW_MODE:
        return "OverflowModeError";
    case TC_ERR_CAST_OVERFLOW:
        return "CastOverflow";
    case TC_ERR_IO:
        return "IOError";
    }
    return "UnknownError";
}

/*
 * @brief 将 TcIntType 转为源码中的类型名字符串
 * @param type  整数类型枚举值
 * @return 类型名字符串（如 "int8"、"uint32" 等）；非法类型返回 "unknown"
 */
const char *tc_int_type_name(TcIntType type) {
    switch (type) {
    case TC_INT8:
        return "int8";
    case TC_UINT8:
        return "uint8";
    case TC_INT16:
        return "int16";
    case TC_UINT16:
        return "uint16";
    case TC_INT32:
        return "int32";
    case TC_UINT32:
        return "uint32";
    case TC_INT64:
        return "int64";
    case TC_UINT64:
        return "uint64";
    }
    return "unknown";
}
