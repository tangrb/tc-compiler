/*
 * test_types.c — 类型工具函数模块单元测试
 *
 * 覆盖 tc_types.c 公开函数，以及 0.0.35 Phase 1（模块 A）验收：
 *   - TcTypeKind / TcType 编码（A-1/A-2）
 *   - tc_type_equals 全部等价组合（A-3）
 *   - tc_sizeof_bits 各宽度（A-4）
 *   - TcStmtKind / TcRhsKind 枚举计数（A-5/A-6）
 *   - tc_error_kind_name 全表命名 + 白名单唯一性（A-7）
 *   - TcRuntimeSlots / TcSlotDomain 槽位骨架（A-8）
 */

#include "tc_types.h"

#include <stdio.h>
#include <stdlib.h>
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
    TcTypeKind out = TC_INT8;

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
    check(tc_logic_op_parse("xor", &out) == 1 && out == TC_LOGIC_XOR,
          "parse 'xor' → TC_LOGIC_XOR");
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
    TcFormatFullSpec out;

    check(tc_format_spec_parse("%d", &out) == 1 && out.spec == TC_FMT_D, "parse '%%d' → TC_FMT_D");
    check(tc_format_spec_parse("%i", &out) == 1 && out.spec == TC_FMT_I, "parse '%%i' → TC_FMT_I");
    check(tc_format_spec_parse("%u", &out) == 1 && out.spec == TC_FMT_U, "parse '%%u' → TC_FMT_U");
    check(tc_format_spec_parse("%x", &out) == 1 && out.spec == TC_FMT_X, "parse '%%x' → TC_FMT_X");
    check(tc_format_spec_parse("%X", &out) == 1 && out.spec == TC_FMT_XU, "parse '%%X' → TC_FMT_XU");
    check(tc_format_spec_parse("%o", &out) == 1 && out.spec == TC_FMT_O, "parse '%%o' → TC_FMT_O");
    check(tc_format_spec_parse("%b", &out) == 1 && out.spec == TC_FMT_B, "parse '%%b' → TC_FMT_B");
    check(tc_format_spec_parse("%t", &out) == 1 && out.spec == TC_FMT_T, "parse '%%t' → TC_FMT_T");
    check(tc_format_spec_parse("%f", &out) == 1 && out.spec == TC_FMT_F, "parse '%%f' → TC_FMT_F");
    check(tc_format_spec_parse("%e", &out) == 1 && out.spec == TC_FMT_E, "parse '%%e' → TC_FMT_E");
    check(tc_format_spec_parse("%E", &out) == 1 && out.spec == TC_FMT_EU, "parse '%%E' → TC_FMT_EU");
    check(tc_format_spec_parse("%g", &out) == 1 && out.spec == TC_FMT_G, "parse '%%g' → TC_FMT_G");
    check(tc_format_spec_parse("%G", &out) == 1 && out.spec == TC_FMT_GU, "parse '%%G' → TC_FMT_GU");
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
/*  Phase 1 / A-7: tc_error_kind_name                                   */
/* ================================================================== */

/* CE/RE 越界对按设计共享打印名（编译器标准 §11.4.3 / §11.4.6） */
static int error_names_allowed_duplicate(TcErrorKind a, TcErrorKind b) {
    if ((a == TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE && b == TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE) ||
        (b == TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE && a == TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE)) {
        return 1;
    }
    if ((a == TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE &&
         b == TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE) ||
        (b == TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE &&
         a == TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE)) {
        return 1;
    }
    return 0;
}

static void test_error_kind_name(void) {
    int all_named = 1;
    int all_unique = 1;
    size_t i;
    size_t j;
    const size_t error_kind_count = (size_t)TC_ERR_OUT_OF_MEMORY + 1U;

    check(strcmp(tc_error_kind_name(TC_CE_SYNTAX), "SyntaxError") == 0,
          "TC_CE_SYNTAX → SyntaxError");
    check(strcmp(tc_error_kind_name(TC_CE_TYPE_MISMATCH), "TypeMismatch") == 0,
          "TC_CE_TYPE_MISMATCH → TypeMismatch");
    check(strcmp(tc_error_kind_name(TC_ERR_OUT_OF_MEMORY), "OutOfMemory") == 0,
          "TC_ERR_OUT_OF_MEMORY → OutOfMemory");
    check(strcmp(tc_error_kind_name(TC_CE_CONTINUE_OUTSIDE_LOOP), "ContinueOutsideLoop") == 0,
          "TC_CE_CONTINUE_OUTSIDE_LOOP → ContinueOutsideLoop");
    check(strcmp(tc_error_kind_name(TC_CE_GOTO_OUTSIDE_FUNCTION), "GotoOutsideFunction") == 0,
          "TC_CE_GOTO_OUTSIDE_FUNCTION → GotoOutsideFunction");
    check(strcmp(tc_error_kind_name(TC_CE_LABEL_OUTSIDE_FUNCTION), "LabelOutsideFunction") == 0,
          "TC_CE_LABEL_OUTSIDE_FUNCTION → LabelOutsideFunction");
    check(strcmp(tc_error_kind_name(TC_RE_NEGATIVE_SHIFT_COUNT), "NegativeShiftCount") == 0,
          "TC_RE_NEGATIVE_SHIFT_COUNT → NegativeShiftCount");
    check(strcmp(tc_error_kind_name(TC_CE_FORMAT_SPECIFIER), "FormatSpecifierError") == 0,
          "TC_CE_FORMAT_SPECIFIER → FormatSpecifierError");
    check(strcmp(tc_error_kind_name(TC_CE_DUPLICATE_FUNCTION), "DuplicateFunction") == 0,
          "TC_CE_DUPLICATE_FUNCTION → DuplicateFunction");
    check(strcmp(tc_error_kind_name(TC_CE_UNDEFINED_FUNCTION), "UndefinedFunction") == 0,
          "TC_CE_UNDEFINED_FUNCTION → UndefinedFunction");
    check(strcmp(tc_error_kind_name(TC_CE_MISSING_ARGUMENT), "MissingArgument") == 0,
          "TC_CE_MISSING_ARGUMENT → MissingArgument");
    check(strcmp(tc_error_kind_name(TC_CE_FUNCALL_POSITION), "FunctionCallPositionError") == 0,
          "TC_CE_FUNCALL_POSITION → FunctionCallPositionError");
    check(strcmp(tc_error_kind_name(TC_CE_RETURN_OUTSIDE_FUNCTION), "ReturnOutsideFunction") == 0,
          "TC_CE_RETURN_OUTSIDE_FUNCTION → ReturnOutsideFunction");
    check(strcmp(tc_error_kind_name(TC_CE_MISSING_RETURN), "MissingReturn") == 0,
          "TC_CE_MISSING_RETURN → MissingReturn");
    check(strcmp(tc_error_kind_name(TC_CE_PARAMETER_ASSIGNMENT), "ParameterAssignmentError") == 0,
          "TC_CE_PARAMETER_ASSIGNMENT → ParameterAssignmentError");
    check(strcmp(tc_error_kind_name(TC_CE_RECURSION), "RecursionError") == 0,
          "TC_CE_RECURSION → RecursionError");
    check(strcmp(tc_error_kind_name(TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH),
                 "MemblockElementCountMismatch") == 0,
          "TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH");
    check(strcmp(tc_error_kind_name(TC_CE_MEMBLOCK_SIZE_MISMATCH), "MemblockSizeMismatch") == 0,
          "TC_CE_MEMBLOCK_SIZE_MISMATCH → MemblockSizeMismatch");
    check(strcmp(tc_error_kind_name(TC_CE_STRUCT_MISSING_FIELD), "StructMissingField") == 0,
          "TC_CE_STRUCT_MISSING_FIELD → StructMissingField");
    check(strcmp(tc_error_kind_name(TC_CE_STRUCT_FIELD_ORDER), "StructFieldOrderError") == 0,
          "TC_CE_STRUCT_FIELD_ORDER → StructFieldOrderError");
    check(strcmp(tc_error_kind_name(TC_CE_DUPLICATE_STRUCT), "DuplicateStruct") == 0,
          "TC_CE_DUPLICATE_STRUCT → DuplicateStruct");
    check(strcmp(tc_error_kind_name(TC_CE_UNDEFINED_STRUCT), "UndefinedStruct") == 0,
          "TC_CE_UNDEFINED_STRUCT → UndefinedStruct");
    check(strcmp(tc_error_kind_name(TC_CE_MODULE_LAYER), "ModuleLayerError") == 0,
          "TC_CE_MODULE_LAYER → ModuleLayerError");
    check(strcmp(tc_error_kind_name(TC_CE_MISSING_VISIBILITY), "MissingVisibilityError") == 0,
          "TC_CE_MISSING_VISIBILITY → MissingVisibilityError");
    check(strcmp(tc_error_kind_name(TC_CE_PROGRAM_MODE_MISUSE), "ProgramModeMisuseError") == 0,
          "TC_CE_PROGRAM_MODE_MISUSE → ProgramModeMisuseError");
    check(strcmp(tc_error_kind_name(TC_CE_IMPORT_NOT_FOUND), "ImportNotFound") == 0,
          "TC_CE_IMPORT_NOT_FOUND → ImportNotFound");
    check(strcmp(tc_error_kind_name(TC_CE_CIRCULAR_IMPORT), "CircularImport") == 0,
          "TC_CE_CIRCULAR_IMPORT → CircularImport");
    check(strcmp(tc_error_kind_name(TC_CE_PRIVATE_MEMBER_ACCESS), "PrivateMemberAccessError") == 0,
          "TC_CE_PRIVATE_MEMBER_ACCESS → PrivateMemberAccessError");
    check(strcmp(tc_error_kind_name(TC_RE_NULL_POINTER_DEREFERENCE), "NullPointerDereference") == 0,
          "TC_RE_NULL_POINTER_DEREFERENCE → NullPointerDereference");
    check(strcmp(tc_error_kind_name(TC_RE_NULL_POINTER_ARITHMETIC), "NullPointerArithmetic") == 0,
          "TC_RE_NULL_POINTER_ARITHMETIC → NullPointerArithmetic");
    check(strcmp(tc_error_kind_name(TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE),
                 tc_error_kind_name(TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE)) == 0,
          "memblock index CE/RE share print name");
    check(strcmp(tc_error_kind_name(TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE),
                 tc_error_kind_name(TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE)) == 0,
          "memcopy unsafe CE/RE share print name");

    check(error_kind_count == 90U, "0.0.35 error kind table has 90 entries");
    for (i = 0; i < error_kind_count; i++) {
        const char *name = tc_error_kind_name((TcErrorKind)i);

        if (strcmp(name, "UnknownError") == 0) {
            all_named = 0;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(name, tc_error_kind_name((TcErrorKind)j)) == 0 &&
                !error_names_allowed_duplicate((TcErrorKind)i, (TcErrorKind)j)) {
                all_unique = 0;
            }
        }
    }
    check(all_named, "every error kind has a name");
    check(all_unique, "error kind names unique except CE/RE allowlist pairs");

    check(strcmp(tc_error_kind_name((TcErrorKind)999), "UnknownError") == 0,
          "unknown error kind → UnknownError");
}

/* ================================================================== */
/*  Phase 1 / A-1～A-4: TcTypeKind + TcType + equals + sizeof_bits      */
/* ================================================================== */

static size_t test_struct_width_cb(int struct_id, void *userdata) {
    size_t *table = (size_t *)userdata;

    if (!table) {
        return 0;
    }
    if (struct_id == 1) {
        return table[0];
    }
    if (struct_id == 2) {
        return table[1];
    }
    return 0;
}

static void test_type_kind_inventory(void) {
    check((int)TC_STRUCT - (int)TC_INT8 + 1 == 17, "TcTypeKind has 17 entries");
    check(tc_type_is_ptr_kind(TC_PTR) == 1, "TC_PTR is ptr kind");
    check(tc_type_is_memblock_kind(TC_MEMBLOCK) == 1, "TC_MEMBLOCK is memblock kind");
    check(tc_type_is_struct_kind(TC_STRUCT) == 1, "TC_STRUCT is struct kind");
    check(tc_type_is_ptr_kind(TC_INT32) == 0, "int32 is not ptr kind");
    check(tc_type_bit_width(TC_VOID) == 0, "void bit_width = 0");
    check(tc_type_bit_width(TC_MEMBLOCK) == 0, "memblock bit_width via kind API = 0");
    check(tc_type_bit_width(TC_STRUCT) == 0, "struct bit_width via kind API = 0");
}

static void test_type_encoding(void) {
    TcType i32 = tc_type_scalar(TC_INT32);
    TcType elem = tc_type_scalar(TC_FLOAT64);
    TcType ptr_t = tc_type_make_ptr(&elem);
    TcType mb = tc_type_make_memblock(&elem, 7);
    TcType st = tc_type_make_struct(42);

    check(i32.kind == TC_INT32 && i32.params.ptr_type.pointee == NULL,
          "scalar encodes kind only");
    check(ptr_t.kind == TC_PTR && ptr_t.params.ptr_type.pointee == &elem,
          "ptr encodes pointee pointer");
    check(mb.kind == TC_MEMBLOCK && mb.params.memblock_type.element == &elem &&
              mb.params.memblock_type.count == 7,
          "memblock encodes element and count");
    check(st.kind == TC_STRUCT && st.params.struct_type.struct_id == 42,
          "struct encodes struct_id");
}

static void test_type_equals_matrix(void) {
    TcType i32 = tc_type_scalar(TC_INT32);
    TcType i32b = tc_type_scalar(TC_INT32);
    TcType u32 = tc_type_scalar(TC_UINT32);
    TcType vvoid = tc_type_scalar(TC_VOID);
    TcType vvoid2 = tc_type_scalar(TC_VOID);
    TcType f32 = tc_type_scalar(TC_FLOAT32);
    TcType elem_i32 = tc_type_scalar(TC_INT32);
    TcType elem_u32 = tc_type_scalar(TC_UINT32);
    TcType mb10 = tc_type_make_memblock(&elem_i32, 10);
    TcType mb20 = tc_type_make_memblock(&elem_i32, 20);
    TcType mb_u = tc_type_make_memblock(&elem_u32, 10);
    TcType ptr_i = tc_type_make_ptr(&elem_i32);
    TcType ptr_u = tc_type_make_ptr(&elem_u32);
    TcType ptr_null_a = tc_type_make_ptr(NULL);
    TcType ptr_null_b = tc_type_make_ptr(NULL);
    TcType inner = tc_type_scalar(TC_INT32);
    TcType mid = tc_type_make_ptr(&inner);
    TcType nested_a = tc_type_make_ptr(&mid);
    TcType nested_b = tc_type_make_ptr(&mid);
    TcType nested_diff = tc_type_make_ptr(&ptr_u);
    TcType st1 = tc_type_make_struct(1);
    TcType st1b = tc_type_make_struct(1);
    TcType st2 = tc_type_make_struct(2);

    check(tc_type_equals(&i32, &i32b) == 1, "scalar same kind equals");
    check(tc_type_equals(&i32, &u32) == 0, "scalar different kinds unequal");
    check(tc_type_equals(&vvoid, &vvoid2) == 1, "void equals void");
    check(tc_type_equals(&vvoid, &i32) == 0, "void != int32");
    check(tc_type_equals(&f32, &i32) == 0, "float32 != int32");

    check(tc_type_equals(&mb10, &mb20) == 1, "memblock equals ignores N");
    check(tc_type_equals(&mb10, &mb_u) == 0, "memblock different T unequal");
    check(tc_type_equals(&ptr_i, &ptr_i) == 1, "ptr same pointee equals");
    check(tc_type_equals(&ptr_i, &ptr_u) == 0, "ptr different pointee unequal");
    check(tc_type_equals(&ptr_null_a, &ptr_null_b) == 1, "ptr NULL pointee equals");
    check(tc_type_equals(&ptr_null_a, &ptr_i) == 0, "ptr NULL != ptr with pointee");

    check(tc_type_equals(&nested_a, &nested_b) == 1, "nested ptr equals");
    check(tc_type_equals(&nested_a, &nested_diff) == 0, "nested ptr different leaf unequal");

    check(tc_type_equals(&st1, &st1b) == 1, "struct same id equals");
    check(tc_type_equals(&st1, &st2) == 0, "struct different id unequal");
    check(tc_type_equals(&st1, &i32) == 0, "struct != scalar");
    check(tc_type_equals(&mb10, &ptr_i) == 0, "memblock != ptr");

    check(tc_type_equals(NULL, &i32) == 0, "NULL lhs equals → 0");
    check(tc_type_equals(&i32, NULL) == 0, "NULL rhs equals → 0");
    check(tc_type_equals(NULL, NULL) == 0, "NULL NULL equals → 0");
}

static void test_sizeof_bits_matrix(void) {
    TcType i8 = tc_type_scalar(TC_INT8);
    TcType u8 = tc_type_scalar(TC_UINT8);
    TcType i16 = tc_type_scalar(TC_INT16);
    TcType u16 = tc_type_scalar(TC_UINT16);
    TcType i32 = tc_type_scalar(TC_INT32);
    TcType u32 = tc_type_scalar(TC_UINT32);
    TcType i64 = tc_type_scalar(TC_INT64);
    TcType u64 = tc_type_scalar(TC_UINT64);
    TcType b = tc_type_scalar(TC_BOOL);
    TcType f32 = tc_type_scalar(TC_FLOAT32);
    TcType f64 = tc_type_scalar(TC_FLOAT64);
    TcType isize = tc_type_scalar(TC_ISIZE);
    TcType usize = tc_type_scalar(TC_USIZE);
    TcType vvoid = tc_type_scalar(TC_VOID);
    TcType elem = tc_type_scalar(TC_INT32);
    TcType ptr_t = tc_type_make_ptr(&elem);
    TcType mb3 = tc_type_make_memblock(&elem, 3);
    TcType inner_elem = tc_type_scalar(TC_UINT8);
    TcType inner_mb = tc_type_make_memblock(&inner_elem, 4);
    TcType outer_mb = tc_type_make_memblock(&inner_mb, 2);
    TcType st = tc_type_make_struct(1);
    TcType st2 = tc_type_make_struct(2);
    size_t ptr_w = tc_target_ptr_width_bits();
    size_t widths[2];

    check(ptr_w == 32 || ptr_w == 64, "target ptr width is 32 or 64");
    check(tc_sizeof_bits(&i8) == 8, "sizeof_bits int8");
    check(tc_sizeof_bits(&u8) == 8, "sizeof_bits uint8");
    check(tc_sizeof_bits(&i16) == 16, "sizeof_bits int16");
    check(tc_sizeof_bits(&u16) == 16, "sizeof_bits uint16");
    check(tc_sizeof_bits(&i32) == 32, "sizeof_bits int32");
    check(tc_sizeof_bits(&u32) == 32, "sizeof_bits uint32");
    check(tc_sizeof_bits(&i64) == 64, "sizeof_bits int64");
    check(tc_sizeof_bits(&u64) == 64, "sizeof_bits uint64");
    check(tc_sizeof_bits(&b) == 8, "sizeof_bits bool");
    check(tc_sizeof_bits(&f32) == 32, "sizeof_bits float32");
    check(tc_sizeof_bits(&f64) == 64, "sizeof_bits float64");
    check(tc_sizeof_bits(&isize) == ptr_w, "sizeof_bits isize = ptr width");
    check(tc_sizeof_bits(&usize) == ptr_w, "sizeof_bits usize = ptr width");
    check(tc_sizeof_bits(&vvoid) == 0, "sizeof_bits void = 0");
    check(tc_sizeof_bits(&ptr_t) == ptr_w, "sizeof_bits ptr = ptr width");
    check(tc_sizeof_bits(&mb3) == ptr_w + 3U * 32U, "sizeof_bits memblock<int32,3>");
    check(tc_sizeof_bits(&outer_mb) == ptr_w + 2U * (ptr_w + 4U * 8U),
          "sizeof_bits nested memblock");

    widths[0] = 96;
    widths[1] = 128;
    tc_sizeof_bits_set_struct_width_fn(test_struct_width_cb, widths);
    check(tc_sizeof_bits(&st) == 96, "sizeof_bits struct id1 via callback");
    check(tc_sizeof_bits(&st2) == 128, "sizeof_bits struct id2 via callback");
    tc_sizeof_bits_set_struct_width_fn(NULL, NULL);
    check(tc_sizeof_bits(&st) == 0, "sizeof_bits struct without callback = 0");
    check(tc_sizeof_bits(NULL) == 0, "sizeof_bits NULL = 0");
}

static void test_new_scalar_kinds(void) {
    TcTypeKind out = TC_INT8;
    size_t ptr_w = tc_target_ptr_width_bits();

    check(tc_type_bit_width(TC_ISIZE) == (int)ptr_w, "isize bit width = ptr width");
    check(tc_type_bit_width(TC_USIZE) == (int)ptr_w, "usize bit width = ptr width");
    check(tc_type_bit_width(TC_PTR) == (int)ptr_w, "ptr kind bit_width = ptr width");
    check(tc_type_is_integer(TC_ISIZE) == 1, "isize is integer");
    check(tc_type_is_integer(TC_USIZE) == 1, "usize is integer");
    check(tc_type_is_integer(TC_PTR) == 0, "ptr is not integer");
    check(tc_type_is_signed(TC_ISIZE) == 1, "isize is signed");
    check(tc_type_is_signed(TC_USIZE) == 0, "usize is unsigned");
    check(tc_type_is_void(TC_VOID) == 1, "void is void");
    check(tc_type_is_void(TC_INT32) == 0, "int32 is not void");
    check(tc_type_parse("isize", &out) == 1 && out == TC_ISIZE, "parse isize");
    check(tc_type_parse("usize", &out) == 1 && out == TC_USIZE, "parse usize");
    check(tc_type_parse("void", &out) == 1 && out == TC_VOID, "parse void");
    check(tc_type_parse("ptr", &out) == 0, "parse 'ptr' alone fails (needs <T>)");
    check(strcmp(tc_type_name(TC_ISIZE), "isize") == 0, "name isize");
    check(strcmp(tc_type_name(TC_USIZE), "usize") == 0, "name usize");
    check(strcmp(tc_type_name(TC_VOID), "void") == 0, "name void");
    check(strcmp(tc_type_name(TC_PTR), "ptr") == 0, "name ptr");
    check(strcmp(tc_type_name(TC_MEMBLOCK), "memblock") == 0, "name memblock");
    check(strcmp(tc_type_name(TC_STRUCT), "struct") == 0, "name struct");
}

/* ================================================================== */
/*  Phase 1 / A-5～A-6: STMT / RHS 枚举库存                             */
/* ================================================================== */

static void test_stmt_rhs_kind_inventory(void) {
    check((int)TC_STMT_IMPORT - (int)TC_STMT_VAR_DEF + 1 == 24,
          "TcStmtKind has 24 entries (12 legacy + 12 new)");
    check((int)TC_RHS_SELF_MEMBER - (int)TC_RHS_LIT + 1 == 34,
          "TcRhsKind has 34 entries (16 legacy + 18 new)");
    check(TC_STMT_FUNC_DEF > TC_STMT_CONTINUE, "FUNC_DEF after legacy stmts");
    check(TC_STMT_FIELD_ASSIGN > TC_STMT_CONTINUE, "FIELD_ASSIGN after legacy stmts");
    check(TC_RHS_MEMBLOCK_LOAD > TC_RHS_BITCAST, "MEMBLOCK_LOAD after legacy rhs");
    check(TC_RHS_FUNCALL_EXPR > TC_RHS_BITCAST, "FUNCALL_EXPR after legacy rhs");
    check(TC_SYM_PARAMETER > TC_SYM_CONSTANT, "SYM_PARAMETER added");
    check(TC_SYM_STATIC_VAR > TC_SYM_CONSTANT, "SYM_STATIC_VAR added");
    check(TC_SYM_STATIC_LET > TC_SYM_CONSTANT, "SYM_STATIC_LET added");
}

/* ================================================================== */
/*  Phase 1 / A-8: 槽位模型                                             */
/* ================================================================== */

static void test_runtime_slots(void) {
    TcRuntimeSlots slots;
    TcValue *vals;
    void *block;

    tc_runtime_slots_init(&slots);
    check(slots.toplevel_slots == NULL && slots.toplevel_count == 0, "slots init toplevel empty");
    check(slots.static_slots == NULL && slots.static_count == 0, "slots init static empty");
    check(slots.memblock_storage == NULL && slots.memblock_storage_count == 0,
          "slots init memblock storage empty");
    check(slots.struct_storage == NULL && slots.struct_storage_count == 0,
          "slots init struct storage empty");

    vals = (TcValue *)calloc(2, sizeof(TcValue));
    check(vals != NULL, "allocate toplevel slot array");
    if (vals) {
        vals[0].type = TC_INT32;
        vals[0].bits = 7;
        vals[1].type = TC_PTR;
        vals[1].bits = 0;
        slots.toplevel_slots = vals;
        slots.toplevel_count = 2;
    }

    slots.static_slots = (TcValue *)calloc(1, sizeof(TcValue));
    check(slots.static_slots != NULL, "allocate static slot array");
    if (slots.static_slots) {
        slots.static_slots[0].type = TC_USIZE;
        slots.static_count = 1;
    }

    block = malloc(16);
    check(block != NULL, "allocate memblock storage blob");
    slots.memblock_storage = (void **)malloc(sizeof(void *));
    check(slots.memblock_storage != NULL, "allocate memblock_storage array");
    if (slots.memblock_storage && block) {
        slots.memblock_storage[0] = block;
        slots.memblock_storage_count = 1;
        slots.memblock_storage_capacity = 1;
    } else {
        free(block);
    }

    check(TC_SLOT_TOPLEVEL == 0, "TC_SLOT_TOPLEVEL = 0");
    check(TC_SLOT_STATIC == 1, "TC_SLOT_STATIC = 1");
    check(TC_SLOT_PARAM == 2, "TC_SLOT_PARAM = 2");
    check(TC_SLOT_LOCAL == 3, "TC_SLOT_LOCAL = 3");

    tc_runtime_slots_free(&slots);
    check(slots.toplevel_slots == NULL && slots.static_slots == NULL,
          "slots free clears slot arrays");
    check(slots.memblock_storage == NULL && slots.struct_storage == NULL,
          "slots free clears storage arrays");
    check(slots.toplevel_count == 0 && slots.static_count == 0, "slots free clears counts");

    tc_runtime_slots_init(NULL);
    tc_runtime_slots_free(NULL);
    check(1, "runtime_slots NULL args are no-ops");
}

static void test_loop_statement_contracts(void) {
    TcStatement while_stmt;
    TcStatement break_stmt;
    TcStatement continue_stmt;
    TcStatement import_stmt;

    memset(&while_stmt, 0, sizeof(while_stmt));
    memset(&break_stmt, 0, sizeof(break_stmt));
    memset(&continue_stmt, 0, sizeof(continue_stmt));
    memset(&import_stmt, 0, sizeof(import_stmt));

    while_stmt.kind = TC_STMT_WHILE;
    while_stmt.u.while_stmt.line = 11;
    while_stmt.u.while_stmt.loop_id = -1;
    break_stmt.kind = TC_STMT_BREAK;
    break_stmt.u.break_stmt.line = 12;
    break_stmt.u.break_stmt.loop_id = -1;
    continue_stmt.kind = TC_STMT_CONTINUE;
    continue_stmt.u.continue_stmt.line = 13;
    continue_stmt.u.continue_stmt.loop_id = -1;
    import_stmt.kind = TC_STMT_IMPORT;

    check(while_stmt.u.while_stmt.line == 11, "while statement stores source line");
    check(while_stmt.u.while_stmt.loop_id == -1, "while loop id unresolved before analysis");
    check(while_stmt.u.while_stmt.body == NULL && while_stmt.u.while_stmt.body_count == 0,
          "while body starts empty");
    check(break_stmt.u.break_stmt.line == 12 && break_stmt.u.break_stmt.loop_id == -1,
          "break control contract");
    check(continue_stmt.u.continue_stmt.line == 13 &&
              continue_stmt.u.continue_stmt.loop_id == -1,
          "continue control contract");
    check(import_stmt.kind == TC_STMT_IMPORT, "IMPORT stmt kind assignable");
}

/* ================================================================== */
/*  tc_warning_kind_name                                                */
/* ================================================================== */

static void test_warning_kind_name(void) {
    check(strcmp(tc_warning_kind_name(TC_WARN_NONE), "None") == 0,
          "TC_WARN_NONE → None");

    check(strcmp(tc_warning_kind_name((TcWarningKind)999), "UnknownWarning") == 0,
          "unknown warning kind → UnknownWarning");
}

/* ================================================================== */
/*  tc_type_name                                                        */
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

    check(strcmp(tc_type_name((TcTypeKind)999), "unknown") == 0,
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
    test_type_kind_inventory();
    test_type_encoding();
    test_type_equals_matrix();
    test_sizeof_bits_matrix();
    test_new_scalar_kinds();
    test_stmt_rhs_kind_inventory();
    test_runtime_slots();
    test_loop_statement_contracts();
    test_warning_kind_name();
    test_int_type_name();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
