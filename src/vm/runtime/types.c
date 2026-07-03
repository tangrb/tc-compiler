/*
 * types.c — TC 整数类型与运算符的工具函数
 *
 * 提供：
 *   - 类型位宽查询（tc_type_bit_width）、符号性判定（tc_type_is_signed）
 *   - 类型名解析（tc_type_parse）与字符串化（tc_int_type_name）
 *   - 运算符解析（tc_arith_op_parse / tc_unary_op_parse）
 *   - 格式说明符解析（tc_format_spec_parse）与字符串化（tc_format_spec_name）
 *   - 错误/警告种类名称字符串化（tc_error_kind_name / tc_warning_kind_name）
 *
 * 供 Lexer（类型/运算符关键字识别）、Analyzer（类型比较）、Executor/AOT（格式化输出）使用。
 */
#include "tc_types.h"

#include <string.h>

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

int tc_unary_op_parse(const char *text, TcUnaryOp *out) {
    if (strcmp(text, "abs") == 0) {
        *out = TC_UNARY_ABS;
    } else if (strcmp(text, "neg") == 0) {
        *out = TC_UNARY_NEG;
    } else {
        return 0;
    }
    return 1;
}

int tc_format_spec_parse(const char *text, TcFormatSpec *out) {
    if (strcmp(text, "%d") == 0) {
        *out = TC_FMT_D;
    } else if (strcmp(text, "%i") == 0) {
        *out = TC_FMT_I;
    } else if (strcmp(text, "%u") == 0) {
        *out = TC_FMT_U;
    } else if (strcmp(text, "%x") == 0) {
        *out = TC_FMT_X;
    } else if (strcmp(text, "%X") == 0) {
        *out = TC_FMT_XU;
    } else if (strcmp(text, "%o") == 0) {
        *out = TC_FMT_O;
    } else if (strcmp(text, "%b") == 0) {
        *out = TC_FMT_B;
    } else {
        return 0;
    }
    return 1;
}

const char *tc_format_spec_name(TcFormatSpec fmt) {
    switch (fmt) {
    case TC_FMT_D:
        return "%d";
    case TC_FMT_I:
        return "%i";
    case TC_FMT_U:
        return "%u";
    case TC_FMT_X:
        return "%x";
    case TC_FMT_XU:
        return "%X";
    case TC_FMT_O:
        return "%o";
    case TC_FMT_B:
        return "%b";
    default:
        return "";
    }
}

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
    case TC_ERR_LITERAL_TYPE:
        return "LiteralTypeError";
    case TC_ERR_KEYWORD:
        return "KeywordError";
    case TC_ERR_CONSTANT_ASSIGNMENT:
        return "ConstantAssignmentError";
    case TC_ERR_CONSTANT_EXPRESSION:
        return "ConstantExpressionError";
    case TC_ERR_FORMAT_STRING:
        return "FormatStringError";
    case TC_ERR_FORMAT_TYPE_MISMATCH:
        return "FormatTypeMismatch";
    case TC_ERR_OPERAND_COUNT:
        return "OperandCountError";
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

const char *tc_warning_kind_name(TcWarningKind kind) {
    switch (kind) {
    case TC_WARN_UNINITIALIZED_VARIABLE:
        return "UninitializedVariable";
    }
    return "UnknownWarning";
}

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
