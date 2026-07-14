/*
 * test_types.c — 类型工具函数模块单元测试
 *
 * 覆盖 tc_types.c 中所有公开函数：
 *   - tc_type_bit_width — 9 种类型的位宽查询
 *   - tc_type_is_bool / tc_type_is_integer / tc_type_is_signed — 符号/种类判定
 *   - tc_type_parse — 类型名字符串→枚举
 *   - tc_arith_op_parse / tc_unary_op_parse — 运算符字符串→枚举
 *   - tc_compare_op_parse / tc_logic_op_parse — 比较/逻辑运算符解析
 *   - tc_format_spec_parse / tc_format_spec_name — 格式说明符
 *   - tc_error_kind_name / tc_warning_kind_name — 错误/警告名称
 *   - tc_type_name — 类型枚举→字符串
 *
 * 防止回归：类型映射遗漏、未定义行为返回空串或 "unknown"
 */

#include "tc_types.h"

#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

static void check(int condition, const char *message) {
    if (condition) {
        g_passed++;
    } else {
        g_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

/* ================================================================== */
/*  tc_type_bit_width                                                   */
/* ================================================================== */

static void test_type_bit_width(void) {
    check(tc_type_bit_width(TC_INT8) == 8, "TC_INT8 bit width = 8");
    check(tc_type_bit_width(TC_UINT8) == 8, "TC_UINT8 bit width = 8");
    check(tc_type_bit_width(TC_INT16) == 16, "TC_INT16 bit width = 16");
    check(tc_type_bit_width(TC_UINT16) == 16, "TC_UINT16 bit width = 16");
    check(tc_type_bit_width(TC_INT32) == 32, "TC_INT32 bit width = 32");
    check(tc_type_bit_width(TC_UINT32) == 32, "TC_UINT32 bit width = 32");
    check(tc_type_bit_width(TC_INT64) == 64, "TC_INT64 bit width = 64");
    check(tc_type_bit_width(TC_UINT64) == 64, "TC_UINT64 bit width = 64");
    check(tc_type_bit_width(TC_BOOL) == 8, "TC_BOOL bit width = 8");
    check(tc_type_bit_width(TC_FLOAT32) == 32, "TC_FLOAT32 bit width = 32");
    check(tc_type_bit_width(TC_FLOAT64) == 64, "TC_FLOAT64 bit width = 64");
}

/* ================================================================== */
/*  tc_type_is_bool / tc_type_is_integer / tc_type_is_signed            */
/* ================================================================== */

static void test_type_is_bool(void) {
    check(tc_type_is_bool(TC_BOOL) == 1, "TC_BOOL is bool");
    check(tc_type_is_bool(TC_INT32) == 0, "TC_INT32 is not bool");
    check(tc_type_is_bool(TC_UINT64) == 0, "TC_UINT64 is not bool");
}

static void test_type_is_integer(void) {
    check(tc_type_is_integer(TC_INT8) == 1, "TC_INT8 is integer");
    check(tc_type_is_integer(TC_UINT32) == 1, "TC_UINT32 is integer");
    check(tc_type_is_integer(TC_BOOL) == 0, "TC_BOOL is not integer");
    check(tc_type_is_integer(TC_FLOAT32) == 0, "TC_FLOAT32 is not integer");
    check(tc_type_is_integer(TC_FLOAT64) == 0, "TC_FLOAT64 is not integer");
}

static void test_type_is_float(void) {
    check(tc_type_is_float(TC_FLOAT32) == 1, "TC_FLOAT32 is float");
    check(tc_type_is_float(TC_FLOAT64) == 1, "TC_FLOAT64 is float");
    check(tc_type_is_float(TC_INT32) == 0, "TC_INT32 is not float");
    check(tc_type_is_float(TC_BOOL) == 0, "TC_BOOL is not float");
}

static void test_type_is_signed(void) {
    check(tc_type_is_signed(TC_INT8) == 1, "TC_INT8 is signed");
    check(tc_type_is_signed(TC_INT16) == 1, "TC_INT16 is signed");
    check(tc_type_is_signed(TC_INT32) == 1, "TC_INT32 is signed");
    check(tc_type_is_signed(TC_INT64) == 1, "TC_INT64 is signed");
    check(tc_type_is_signed(TC_UINT8) == 0, "TC_UINT8 is not signed");
    check(tc_type_is_signed(TC_UINT16) == 0, "TC_UINT16 is not signed");
    check(tc_type_is_signed(TC_UINT32) == 0, "TC_UINT32 is not signed");
    check(tc_type_is_signed(TC_UINT64) == 0, "TC_UINT64 is not signed");
    check(tc_type_is_signed(TC_BOOL) == 0, "TC_BOOL is not signed");
}

/* ================================================================== */
/*  tc_type_parse                                                       */
/* ================================================================== */

static void test_type_parse(void) {
    TcType out = TC_INT8;

    check(tc_type_parse("int8", &out) == 1 && out == TC_INT8, "parse 'int8' → TC_INT8");
    check(tc_type_parse("uint8", &out) == 1 && out == TC_UINT8, "parse 'uint8' → TC_UINT8");
    check(tc_type_parse("int16", &out) == 1 && out == TC_INT16, "parse 'int16' → TC_INT16");
    check(tc_type_parse("uint16", &out) == 1 && out == TC_UINT16, "parse 'uint16' → TC_UINT16");
    check(tc_type_parse("int32", &out) == 1 && out == TC_INT32, "parse 'int32' → TC_INT32");
    check(tc_type_parse("uint32", &out) == 1 && out == TC_UINT32, "parse 'uint32' → TC_UINT32");
    check(tc_type_parse("int64", &out) == 1 && out == TC_INT64, "parse 'int64' → TC_INT64");
    check(tc_type_parse("uint64", &out) == 1 && out == TC_UINT64, "parse 'uint64' → TC_UINT64");
    check(tc_type_parse("bool", &out) == 1 && out == TC_BOOL, "parse 'bool' → TC_BOOL");
    check(tc_type_parse("float32", &out) == 1 && out == TC_FLOAT32, "parse 'float32' → TC_FLOAT32");
    check(tc_type_parse("float64", &out) == 1 && out == TC_FLOAT64, "parse 'float64' → TC_FLOAT64");
    check(tc_type_parse("unknown", &out) == 0, "parse 'unknown' → 0 (not found)");
    check(tc_type_parse("INT32", &out) == 0, "parse 'INT32' → 0 (case sensitive)");
}

/* ================================================================== */
/*  tc_arith_op_parse                                                   */
/* ================================================================== */

static void test_arith_op_parse(void) {
    TcArithOp out = TC_ADD;

    check(tc_arith_op_parse("add", &out) == 1 && out == TC_ADD, "parse 'add' → TC_ADD");
    check(tc_arith_op_parse("sub", &out) == 1 && out == TC_SUB, "parse 'sub' → TC_SUB");
    check(tc_arith_op_parse("mul", &out) == 1 && out == TC_MUL, "parse 'mul' → TC_MUL");
    check(tc_arith_op_parse("div", &out) == 1 && out == TC_DIV, "parse 'div' → TC_DIV");
    check(tc_arith_op_parse("mod", &out) == 1 && out == TC_MOD, "parse 'mod' → TC_MOD");
    check(tc_arith_op_parse("addd", &out) == 0, "parse 'addd' → 0 (not found)");
}

/* ================================================================== */
/*  tc_unary_op_parse                                                   */
/* ================================================================== */

static void test_unary_op_parse(void) {
    TcUnaryOp out = TC_UNARY_ABS;

    check(tc_unary_op_parse("abs", &out) == 1 && out == TC_UNARY_ABS,
          "parse 'abs' → TC_UNARY_ABS");
    check(tc_unary_op_parse("neg", &out) == 1 && out == TC_UNARY_NEG,
          "parse 'neg' → TC_UNARY_NEG");
    check(tc_unary_op_parse("abs ", &out) == 0, "parse 'abs ' → 0 (trailing space)");
}

/* ================================================================== */
/*  tc_compare_op_parse                                                 */
/* ================================================================== */

static void test_compare_op_parse(void) {
    TcCompareOp out = TC_CMP_EQ;

    check(tc_compare_op_parse("eq", &out) == 1 && out == TC_CMP_EQ, "parse 'eq' → TC_CMP_EQ");
    check(tc_compare_op_parse("ne", &out) == 1 && out == TC_CMP_NE, "parse 'ne' → TC_CMP_NE");
    check(tc_compare_op_parse("lt", &out) == 1 && out == TC_CMP_LT, "parse 'lt' → TC_CMP_LT");
    check(tc_compare_op_parse("le", &out) == 1 && out == TC_CMP_LE, "parse 'le' → TC_CMP_LE");
    check(tc_compare_op_parse("gt", &out) == 1 && out == TC_CMP_GT, "parse 'gt' → TC_CMP_GT");
    check(tc_compare_op_parse("ge", &out) == 1 && out == TC_CMP_GE, "parse 'ge' → TC_CMP_GE");
    check(tc_compare_op_parse("==", &out) == 0, "parse '==' → 0 (not found)");
}

/* ================================================================== */
/*  tc_logic_op_parse                                                   */
/* ================================================================== */

static void test_logic_op_parse(void) {
    TcLogicOp out = TC_LOGIC_AND;

    check(tc_logic_op_parse("and", &out) == 1 && out == TC_LOGIC_AND,
          "parse 'and' → TC_LOGIC_AND");
    check(tc_logic_op_parse("or", &out) == 1 && out == TC_LOGIC_OR,
          "parse 'or' → TC_LOGIC_OR");
    check(tc_logic_op_parse("not", &out) == 1 && out == TC_LOGIC_NOT,
          "parse 'not' → TC_LOGIC_NOT");
    check(tc_logic_op_parse("xor", &out) == 0, "parse 'xor' → 0 (not logic op)");
}

/* ================================================================== */
/*  tc_bitwise_op_parse / tc_bitwise_op_name                           */
/* ================================================================== */

static void test_bitwise_op_parse(void) {
    TcBitwiseOp out = TC_BIT_AND;

    check(tc_bitwise_op_parse("and", &out) == 1 && out == TC_BIT_AND,
          "parse 'and' → TC_BIT_AND");
    check(tc_bitwise_op_parse("or", &out) == 1 && out == TC_BIT_OR,
          "parse 'or' → TC_BIT_OR");
    check(tc_bitwise_op_parse("xor", &out) == 1 && out == TC_BIT_XOR,
          "parse 'xor' → TC_BIT_XOR");
    check(tc_bitwise_op_parse("not", &out) == 0, "parse 'not' → 0 (not bitwise bin op)");
}

static void test_bitwise_op_name(void) {
    check(strcmp(tc_bitwise_op_name(TC_BIT_AND), "and") == 0, "TC_BIT_AND → 'and'");
    check(strcmp(tc_bitwise_op_name(TC_BIT_OR), "or") == 0, "TC_BIT_OR → 'or'");
    check(strcmp(tc_bitwise_op_name(TC_BIT_XOR), "xor") == 0, "TC_BIT_XOR → 'xor'");
}

/* ================================================================== */
/*  tc_shift_op_parse / tc_shift_op_name                               */
/* ================================================================== */

static void test_shift_op_parse(void) {
    TcShiftOp out = TC_SHIFT_SHL;

    check(tc_shift_op_parse("shl", &out) == 1 && out == TC_SHIFT_SHL,
          "parse 'shl' → TC_SHIFT_SHL");
    check(tc_shift_op_parse("shr", &out) == 1 && out == TC_SHIFT_SHR,
          "parse 'shr' → TC_SHIFT_SHR");
    check(tc_shift_op_parse("shift", &out) == 0, "parse 'shift' → 0 (not found)");
}

static void test_shift_op_name(void) {
    check(strcmp(tc_shift_op_name(TC_SHIFT_SHL), "shl") == 0, "TC_SHIFT_SHL → 'shl'");
    check(strcmp(tc_shift_op_name(TC_SHIFT_SHR), "shr") == 0, "TC_SHIFT_SHR → 'shr'");
}

/* ================================================================== */
/*  tc_format_spec_parse                                                */
/* ================================================================== */

static void test_format_spec_parse(void) {
    TcFormatSpec out = TC_FMT_NONE;

    check(tc_format_spec_parse("%d", &out) == 1 && out == TC_FMT_D, "parse '%%d' → TC_FMT_D");
    check(tc_format_spec_parse("%i", &out) == 1 && out == TC_FMT_I, "parse '%%i' → TC_FMT_I");
    check(tc_format_spec_parse("%u", &out) == 1 && out == TC_FMT_U, "parse '%%u' → TC_FMT_U");
    check(tc_format_spec_parse("%x", &out) == 1 && out == TC_FMT_X, "parse '%%x' → TC_FMT_X");
    check(tc_format_spec_parse("%X", &out) == 1 && out == TC_FMT_XU, "parse '%%X' → TC_FMT_XU");
    check(tc_format_spec_parse("%o", &out) == 1 && out == TC_FMT_O, "parse '%%o' → TC_FMT_O");
    check(tc_format_spec_parse("%b", &out) == 1 && out == TC_FMT_B, "parse '%%b' → TC_FMT_B");
    check(tc_format_spec_parse("%t", &out) == 1 && out == TC_FMT_T, "parse '%%t' → TC_FMT_T");
    check(tc_format_spec_parse("%f", &out) == 1 && out == TC_FMT_F, "parse '%%f' → TC_FMT_F");
    check(tc_format_spec_parse("%e", &out) == 1 && out == TC_FMT_E, "parse '%%e' → TC_FMT_E");
    check(tc_format_spec_parse("%E", &out) == 1 && out == TC_FMT_EU, "parse '%%E' → TC_FMT_EU");
    check(tc_format_spec_parse("%g", &out) == 1 && out == TC_FMT_G, "parse '%%g' → TC_FMT_G");
    check(tc_format_spec_parse("%G", &out) == 1 && out == TC_FMT_GU, "parse '%%G' → TC_FMT_GU");
    check(tc_format_spec_parse("%s", &out) == 0, "parse '%%s' → 0 (not found)");
}

/* ================================================================== */
/*  tc_format_spec_name                                                 */
/* ================================================================== */

static void test_format_spec_name(void) {
    check(strcmp(tc_format_spec_name(TC_FMT_D), "%d") == 0, "TC_FMT_D → '%%d'");
    check(strcmp(tc_format_spec_name(TC_FMT_I), "%i") == 0, "TC_FMT_I → '%%i'");
    check(strcmp(tc_format_spec_name(TC_FMT_U), "%u") == 0, "TC_FMT_U → '%%u'");
    check(strcmp(tc_format_spec_name(TC_FMT_X), "%x") == 0, "TC_FMT_X → '%%x'");
    check(strcmp(tc_format_spec_name(TC_FMT_XU), "%X") == 0, "TC_FMT_XU → '%%X'");
    check(strcmp(tc_format_spec_name(TC_FMT_O), "%o") == 0, "TC_FMT_O → '%%o'");
    check(strcmp(tc_format_spec_name(TC_FMT_B), "%b") == 0, "TC_FMT_B → '%%b'");
    check(strcmp(tc_format_spec_name(TC_FMT_T), "%t") == 0, "TC_FMT_T → '%%t'");
    check(strcmp(tc_format_spec_name(TC_FMT_F), "%f") == 0, "TC_FMT_F → '%%f'");
    check(strcmp(tc_format_spec_name(TC_FMT_E), "%e") == 0, "TC_FMT_E → '%%e'");
    check(strcmp(tc_format_spec_name(TC_FMT_EU), "%E") == 0, "TC_FMT_EU → '%%E'");
    check(strcmp(tc_format_spec_name(TC_FMT_G), "%g") == 0, "TC_FMT_G → '%%g'");
    check(strcmp(tc_format_spec_name(TC_FMT_GU), "%G") == 0, "TC_FMT_GU → '%%G'");
    check(strcmp(tc_format_spec_name(TC_FMT_NONE), "") == 0, "TC_FMT_NONE → ''");
}

/* ================================================================== */
/*  tc_error_kind_name                                                  */
/* ================================================================== */

static void test_error_kind_name(void) {
    /* 验证全部错误种类都有对应的名称 */
    check(strcmp(tc_error_kind_name(TC_ERR_SYNTAX), "SyntaxError") == 0,
          "TC_ERR_SYNTAX → SyntaxError");
    check(strcmp(tc_error_kind_name(TC_ERR_UNDEFINED_VARIABLE), "UndefinedVariable") == 0,
          "TC_ERR_UNDEFINED_VARIABLE → UndefinedVariable");
    check(strcmp(tc_error_kind_name(TC_ERR_DUPLICATE_DEFINITION), "DuplicateDefinition") == 0,
          "TC_ERR_DUPLICATE_DEFINITION → DuplicateDefinition");
    check(strcmp(tc_error_kind_name(TC_ERR_TYPE_MISMATCH), "TypeMismatch") == 0,
          "TC_ERR_TYPE_MISMATCH → TypeMismatch");
    check(strcmp(tc_error_kind_name(TC_ERR_LITERAL_OUT_OF_RANGE), "LiteralOutOfRange") == 0,
          "TC_ERR_LITERAL_OUT_OF_RANGE → LiteralOutOfRange");
    check(strcmp(tc_error_kind_name(TC_ERR_LITERAL_TYPE), "LiteralTypeError") == 0,
          "TC_ERR_LITERAL_TYPE → LiteralTypeError");
    check(strcmp(tc_error_kind_name(TC_ERR_KEYWORD), "KeywordError") == 0,
          "TC_ERR_KEYWORD → KeywordError");
    check(strcmp(tc_error_kind_name(TC_ERR_CONSTANT_ASSIGNMENT), "ConstantAssignmentError") == 0,
          "TC_ERR_CONSTANT_ASSIGNMENT → ConstantAssignmentError");
    check(strcmp(tc_error_kind_name(TC_ERR_CONSTANT_EXPRESSION), "ConstantExpressionError") == 0,
          "TC_ERR_CONSTANT_EXPRESSION → ConstantExpressionError");
    check(strcmp(tc_error_kind_name(TC_ERR_CONSTANT_CIRCULAR), "ConstantCircularDependency") == 0,
          "TC_ERR_CONSTANT_CIRCULAR → ConstantCircularDependency");
    check(strcmp(tc_error_kind_name(TC_ERR_CONSTANT_OVERFLOW), "ConstantOverflow") == 0,
          "TC_ERR_CONSTANT_OVERFLOW → ConstantOverflow");
    check(strcmp(tc_error_kind_name(TC_ERR_CONSTANT_DIV_ZERO), "ConstantDivisionByZero") == 0,
          "TC_ERR_CONSTANT_DIV_ZERO → ConstantDivisionByZero");
    check(strcmp(tc_error_kind_name(TC_ERR_CONSTANT_CAST_OVERFLOW), "ConstantCastOverflow") == 0,
          "TC_ERR_CONSTANT_CAST_OVERFLOW → ConstantCastOverflow");
    check(strcmp(tc_error_kind_name(TC_ERR_COMPARISON_TYPE_MISMATCH), "ComparisonTypeMismatch") == 0,
          "TC_ERR_COMPARISON_TYPE_MISMATCH → ComparisonTypeMismatch");
    check(strcmp(tc_error_kind_name(TC_ERR_FORMAT_STRING), "FormatStringError") == 0,
          "TC_ERR_FORMAT_STRING → FormatStringError");
    check(strcmp(tc_error_kind_name(TC_ERR_FORMAT_TYPE_MISMATCH), "FormatTypeMismatch") == 0,
          "TC_ERR_FORMAT_TYPE_MISMATCH → FormatTypeMismatch");
    check(strcmp(tc_error_kind_name(TC_ERR_OPERAND_COUNT), "OperandCountError") == 0,
          "TC_ERR_OPERAND_COUNT → OperandCountError");
    check(strcmp(tc_error_kind_name(TC_ERR_DIVISION_BY_ZERO), "DivisionByZero") == 0,
          "TC_ERR_DIVISION_BY_ZERO → DivisionByZero");
    check(strcmp(tc_error_kind_name(TC_ERR_INTEGER_OVERFLOW), "IntegerOverflow") == 0,
          "TC_ERR_INTEGER_OVERFLOW → IntegerOverflow");
    check(strcmp(tc_error_kind_name(TC_ERR_OVERFLOW_MODE), "OverflowModeError") == 0,
          "TC_ERR_OVERFLOW_MODE → OverflowModeError");
    check(strcmp(tc_error_kind_name(TC_ERR_CAST_OVERFLOW), "CastOverflow") == 0,
          "TC_ERR_CAST_OVERFLOW → CastOverflow");
    check(strcmp(tc_error_kind_name(TC_ERR_IO), "IOError") == 0,
          "TC_ERR_IO → IOError");
    check(strcmp(tc_error_kind_name(TC_ERR_OUT_OF_MEMORY), "OutOfMemory") == 0,
          "TC_ERR_OUT_OF_MEMORY → OutOfMemory");
    check(strcmp(tc_error_kind_name(TC_ERR_INDENT_MIXED), "IndentMixedError") == 0,
          "TC_ERR_INDENT_MIXED → IndentMixedError");
    check(strcmp(tc_error_kind_name(TC_ERR_INDENT_INSUFFICIENT), "IndentInsufficientError") == 0,
          "TC_ERR_INDENT_INSUFFICIENT → IndentInsufficientError");
    check(strcmp(tc_error_kind_name(TC_ERR_INDENT_ELSE_END), "IndentElseEndError") == 0,
          "TC_ERR_INDENT_ELSE_END → IndentElseEndError");
    check(strcmp(tc_error_kind_name(TC_ERR_MISSING_END), "MissingEndError") == 0,
          "TC_ERR_MISSING_END → MissingEndError");
    check(strcmp(tc_error_kind_name(TC_ERR_ELSE_POSITION), "ElsePositionError") == 0,
          "TC_ERR_ELSE_POSITION → ElsePositionError");
    check(strcmp(tc_error_kind_name(TC_ERR_CONDITION_TYPE), "ConditionTypeError") == 0,
          "TC_ERR_CONDITION_TYPE → ConditionTypeError");
    check(strcmp(tc_error_kind_name(TC_ERR_CROSS_BLOCK_REFERENCE), "CrossBlockReferenceError") == 0,
          "TC_ERR_CROSS_BLOCK_REFERENCE → CrossBlockReferenceError");
    check(strcmp(tc_error_kind_name(TC_ERR_FLOAT_OVERFLOW), "FloatOverflow") == 0,
          "TC_ERR_FLOAT_OVERFLOW → FloatOverflow");
    check(strcmp(tc_error_kind_name(TC_ERR_FLOAT_UNDERFLOW), "FloatUnderflow") == 0,
          "TC_ERR_FLOAT_UNDERFLOW → FloatUnderflow");
    check(strcmp(tc_error_kind_name(TC_ERR_FLOAT_INVALID), "FloatInvalidOperation") == 0,
          "TC_ERR_FLOAT_INVALID → FloatInvalidOperation");
    check(strcmp(tc_error_kind_name(TC_ERR_FLOAT_CAST_OVERFLOW), "FloatCastOverflow") == 0,
          "TC_ERR_FLOAT_CAST_OVERFLOW → FloatCastOverflow");
    check(strcmp(tc_error_kind_name(TC_ERR_MODE_MISMATCH), "ModeMismatch") == 0,
          "TC_ERR_MODE_MISMATCH → ModeMismatch");
    check(strcmp(tc_error_kind_name(TC_ERR_UNINITIALIZED_VARIABLE), "UninitializedVariable") == 0,
          "TC_ERR_UNINITIALIZED_VARIABLE → UninitializedVariable");
    check(strcmp(tc_error_kind_name(TC_ERR_LABEL_NOT_FOUND), "LabelNotFound") == 0,
          "TC_ERR_LABEL_NOT_FOUND → LabelNotFound");
    check(strcmp(tc_error_kind_name(TC_ERR_DUPLICATE_LABEL), "DuplicateLabel") == 0,
          "TC_ERR_DUPLICATE_LABEL → DuplicateLabel");
    check(strcmp(tc_error_kind_name(TC_ERR_JUMP_INTO_BLOCK), "JumpIntoBlockError") == 0,
          "TC_ERR_JUMP_INTO_BLOCK → JumpIntoBlockError");
    check(strcmp(tc_error_kind_name(TC_ERR_JUMP_TO_SIBLING_BLOCK), "JumpToSiblingBlockError") == 0,
          "TC_ERR_JUMP_TO_SIBLING_BLOCK → JumpToSiblingBlockError");

    /* 未知错误种类 → UnknownError */
    check(strcmp(tc_error_kind_name((TcErrorKind)999), "UnknownError") == 0,
          "unknown error kind → UnknownError");
}

/* ================================================================== */
/*  tc_warning_kind_name                                                */
/* ================================================================== */

static void test_warning_kind_name(void) {
    check(strcmp(tc_warning_kind_name(TC_WARN_NONE), "None") == 0,
          "TC_WARN_NONE → None");

    /* 未知警告种类 → UnknownWarning */
    check(strcmp(tc_warning_kind_name((TcWarningKind)999), "UnknownWarning") == 0,
          "unknown warning kind → UnknownWarning");
}

/* ================================================================== */
/*  tc_type_name                                                    */
/* ================================================================== */

static void test_int_type_name(void) {
    check(strcmp(tc_type_name(TC_INT8), "int8") == 0, "TC_INT8 → 'int8'");
    check(strcmp(tc_type_name(TC_UINT8), "uint8") == 0, "TC_UINT8 → 'uint8'");
    check(strcmp(tc_type_name(TC_INT16), "int16") == 0, "TC_INT16 → 'int16'");
    check(strcmp(tc_type_name(TC_UINT16), "uint16") == 0, "TC_UINT16 → 'uint16'");
    check(strcmp(tc_type_name(TC_INT32), "int32") == 0, "TC_INT32 → 'int32'");
    check(strcmp(tc_type_name(TC_UINT32), "uint32") == 0, "TC_UINT32 → 'uint32'");
    check(strcmp(tc_type_name(TC_INT64), "int64") == 0, "TC_INT64 → 'int64'");
    check(strcmp(tc_type_name(TC_UINT64), "uint64") == 0, "TC_UINT64 → 'uint64'");
    check(strcmp(tc_type_name(TC_BOOL), "bool") == 0, "TC_BOOL → 'bool'");
    check(strcmp(tc_type_name(TC_FLOAT32), "float32") == 0, "TC_FLOAT32 → 'float32'");
    check(strcmp(tc_type_name(TC_FLOAT64), "float64") == 0, "TC_FLOAT64 → 'float64'");

    /* 未知类型 → unknown */
    check(strcmp(tc_type_name((TcType)999), "unknown") == 0,
          "unknown type → 'unknown'");
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void) {
    test_type_bit_width();
    test_type_is_bool();
    test_type_is_integer();
    test_type_is_float();
    test_type_is_signed();
    test_type_parse();
    test_arith_op_parse();
    test_unary_op_parse();
    test_compare_op_parse();
    test_logic_op_parse();
    test_bitwise_op_parse();
    test_bitwise_op_name();
    test_shift_op_parse();
    test_shift_op_name();
    test_format_spec_parse();
    test_format_spec_name();
    test_error_kind_name();
    test_warning_kind_name();
    test_int_type_name();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
