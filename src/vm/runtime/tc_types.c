/*
 * tc_types.c — TC 类型种类、完整类型与运算符工具函数
 *
 * 提供：
 *   - 类型位宽查询（tc_type_bit_width / tc_sizeof_bits）、符号性判定
 *   - 完整类型构造与等价（tc_type_scalar / tc_type_equals）
 *   - 类型名解析（tc_type_parse）与字符串化（tc_type_name）
 *   - 运算符解析与格式说明符
 *   - 错误/警告种类名称字符串化
 */
#include "tc_types.h"

#include <stdlib.h>
#include <string.h>

static TcStructWidthFn g_struct_width_fn = NULL;
static void *g_struct_width_userdata = NULL;

void tc_sizeof_bits_set_struct_width_fn(TcStructWidthFn fn, void *userdata) {
    g_struct_width_fn = fn;
    g_struct_width_userdata = userdata;
}

size_t tc_target_ptr_width_bits(void) {
    return (size_t)(sizeof(void *) * 8U);
}

TcType tc_type_scalar(TcTypeKind kind) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.kind = kind;
    return t;
}

TcType tc_type_make_ptr(TcType *pointee) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.kind = TC_PTR;
    t.params.ptr_type.pointee = pointee;
    return t;
}

TcType tc_type_make_memblock(TcType *element, uint64_t count) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.kind = TC_MEMBLOCK;
    t.params.memblock_type.element = element;
    t.params.memblock_type.count = count;
    return t;
}

TcType tc_type_make_struct(int struct_id) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.kind = TC_STRUCT;
    t.params.struct_type.struct_id = struct_id;
    return t;
}

void tc_type_free(TcType *type) {
    if (!type) {
        return;
    }
    if (type->kind == TC_PTR && type->params.ptr_type.pointee) {
        tc_type_free(type->params.ptr_type.pointee);
        free(type->params.ptr_type.pointee);
        type->params.ptr_type.pointee = NULL;
    } else if (type->kind == TC_MEMBLOCK && type->params.memblock_type.element) {
        tc_type_free(type->params.memblock_type.element);
        free(type->params.memblock_type.element);
        type->params.memblock_type.element = NULL;
    }
    memset(type, 0, sizeof(*type));
}

int tc_type_equals(const TcType *a, const TcType *b) {
    if (!a || !b) {
        return 0;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
    case TC_PTR:
        if (!a->params.ptr_type.pointee || !b->params.ptr_type.pointee) {
            return a->params.ptr_type.pointee == b->params.ptr_type.pointee;
        }
        return tc_type_equals(a->params.ptr_type.pointee, b->params.ptr_type.pointee);
    case TC_MEMBLOCK:
        /* N 不参与等价（语言标准 §3.8 / VM 详设 §8.6） */
        if (!a->params.memblock_type.element || !b->params.memblock_type.element) {
            return a->params.memblock_type.element == b->params.memblock_type.element;
        }
        return tc_type_equals(a->params.memblock_type.element, b->params.memblock_type.element);
    case TC_STRUCT:
        return a->params.struct_type.struct_id == b->params.struct_type.struct_id;
    default:
        return 1;
    }
}

size_t tc_sizeof_bits(const TcType *type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
    case TC_INT8:
    case TC_UINT8:
    case TC_BOOL:
        return 8;
    case TC_INT16:
    case TC_UINT16:
        return 16;
    case TC_INT32:
    case TC_UINT32:
    case TC_FLOAT32:
        return 32;
    case TC_INT64:
    case TC_UINT64:
    case TC_FLOAT64:
        return 64;
    case TC_ISIZE:
    case TC_USIZE:
    case TC_PTR:
        return tc_target_ptr_width_bits();
    case TC_MEMBLOCK: {
        TcType usize_ty = tc_type_scalar(TC_USIZE);
        size_t elem_bits = 0;
        if (type->params.memblock_type.element) {
            elem_bits = tc_sizeof_bits(type->params.memblock_type.element);
        }
        return tc_sizeof_bits(&usize_ty)
             + (size_t)type->params.memblock_type.count * elem_bits;
    }
    case TC_STRUCT:
        if (g_struct_width_fn) {
            return g_struct_width_fn(type->params.struct_type.struct_id, g_struct_width_userdata);
        }
        return 0;
    case TC_VOID:
        return 0;
    }
    return 0;
}

void tc_runtime_slots_init(TcRuntimeSlots *slots) {
    if (!slots) {
        return;
    }
    memset(slots, 0, sizeof(*slots));
}

void tc_runtime_slots_free(TcRuntimeSlots *slots) {
    size_t i;
    if (!slots) {
        return;
    }
    free(slots->toplevel_slots);
    free(slots->static_slots);
    for (i = 0; i < slots->memblock_storage_count; i++) {
        free(slots->memblock_storage[i]);
    }
    free(slots->memblock_storage);
    for (i = 0; i < slots->struct_storage_count; i++) {
        free(slots->struct_storage[i]);
    }
    free(slots->struct_storage);
    memset(slots, 0, sizeof(*slots));
}

/*
 * 返回 TC 整数/浮点/平台字长类型的位宽。
 * TC_BOOL 存储宽度为 8 位；ptr/memblock/struct 请用 tc_sizeof_bits。
 */
int tc_type_bit_width(TcTypeKind type) {
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
    case TC_BOOL:
        return 8;
    case TC_FLOAT32:
        return 32;
    case TC_FLOAT64:
        return 64;
    case TC_ISIZE:
    case TC_USIZE:
        return (int)tc_target_ptr_width_bits();
    case TC_PTR:
        return (int)tc_target_ptr_width_bits();
    case TC_VOID:
    case TC_MEMBLOCK:
    case TC_STRUCT:
        return 0;
    }
    return 0;
}

int tc_type_is_bool(TcTypeKind type) {
    return type == TC_BOOL;
}

int tc_type_is_integer(TcTypeKind type) {
    return (type >= TC_INT8 && type <= TC_UINT64) || type == TC_ISIZE || type == TC_USIZE;
}

int tc_type_is_float(TcTypeKind type) {
    return type == TC_FLOAT32 || type == TC_FLOAT64;
}

int tc_type_is_void(TcTypeKind type) {
    return type == TC_VOID;
}

int tc_type_is_ptr_kind(TcTypeKind type) {
    return type == TC_PTR;
}

int tc_type_is_memblock_kind(TcTypeKind type) {
    return type == TC_MEMBLOCK;
}

int tc_type_is_struct_kind(TcTypeKind type) {
    return type == TC_STRUCT;
}

/*
 * 有符号性判定。
 * TC_BOOL 被视为无符号；isize 为有符号平台字长整数。
 */
int tc_type_is_signed(TcTypeKind type) {
    if (tc_type_is_bool(type)) {
        return 0;
    }
    switch (type) {
    case TC_INT8:
    case TC_INT16:
    case TC_INT32:
    case TC_INT64:
    case TC_ISIZE:
        return 1;
    default:
        return 0;
    }
}

int tc_type_parse(const char *text, TcTypeKind *out) {
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
    } else if (strcmp(text, "bool") == 0) {
        *out = TC_BOOL;
    } else if (strcmp(text, "float32") == 0) {
        *out = TC_FLOAT32;
    } else if (strcmp(text, "float64") == 0) {
        *out = TC_FLOAT64;
    } else if (strcmp(text, "isize") == 0) {
        *out = TC_ISIZE;
    } else if (strcmp(text, "usize") == 0) {
        *out = TC_USIZE;
    } else if (strcmp(text, "void") == 0) {
        *out = TC_VOID;
    } else {
        return 0;
    }
    return 1;
}

int tc_float_mode_parse(const char *text, TcFloatMode *out) {
    if (strcmp(text, "ieee") == 0) {
        *out = TC_FLOAT_IEEE;
    } else if (strcmp(text, "wrap") == 0) {
        *out = TC_FLOAT_WRAP;
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

int tc_compare_op_parse(const char *text, TcCompareOp *out) {
    if (strcmp(text, "eq") == 0) {
        *out = TC_CMP_EQ;
    } else if (strcmp(text, "ne") == 0) {
        *out = TC_CMP_NE;
    } else if (strcmp(text, "lt") == 0) {
        *out = TC_CMP_LT;
    } else if (strcmp(text, "le") == 0) {
        *out = TC_CMP_LE;
    } else if (strcmp(text, "gt") == 0) {
        *out = TC_CMP_GT;
    } else if (strcmp(text, "ge") == 0) {
        *out = TC_CMP_GE;
    } else {
        return 0;
    }
    return 1;
}

int tc_logic_op_parse(const char *text, TcLogicOp *out) {
    if (strcmp(text, "and") == 0) {
        *out = TC_LOGIC_AND;
    } else if (strcmp(text, "or") == 0) {
        *out = TC_LOGIC_OR;
    } else if (strcmp(text, "not") == 0) {
        *out = TC_LOGIC_NOT;
    } else {
        return 0;
    }
    return 1;
}

int tc_bitwise_op_parse(const char *text, TcBitwiseOp *out) {
    if (strcmp(text, "and") == 0) {
        *out = TC_BIT_AND;
    } else if (strcmp(text, "or") == 0) {
        *out = TC_BIT_OR;
    } else if (strcmp(text, "xor") == 0) {
        *out = TC_BIT_XOR;
    } else {
        return 0;
    }
    return 1;
}

int tc_shift_op_parse(const char *text, TcShiftOp *out) {
    if (strcmp(text, "shl") == 0) {
        *out = TC_SHIFT_SHL;
    } else if (strcmp(text, "shr") == 0) {
        *out = TC_SHIFT_SHR;
    } else {
        return 0;
    }
    return 1;
}

const char *tc_bitwise_op_name(TcBitwiseOp op) {
    switch (op) {
    case TC_BIT_AND:
        return "and";
    case TC_BIT_OR:
        return "or";
    case TC_BIT_XOR:
        return "xor";
    }
    return "unknown";
}

const char *tc_shift_op_name(TcShiftOp op) {
    switch (op) {
    case TC_SHIFT_SHL:
        return "shl";
    case TC_SHIFT_SHR:
        return "shr";
    }
    return "unknown";
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
    } else if (strcmp(text, "%t") == 0) {
        *out = TC_FMT_T;
    } else if (strcmp(text, "%f") == 0) {
        *out = TC_FMT_F;
    } else if (strcmp(text, "%e") == 0) {
        *out = TC_FMT_E;
    } else if (strcmp(text, "%E") == 0) {
        *out = TC_FMT_EU;
    } else if (strcmp(text, "%g") == 0) {
        *out = TC_FMT_G;
    } else if (strcmp(text, "%G") == 0) {
        *out = TC_FMT_GU;
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
    case TC_FMT_T:
        return "%t";
    case TC_FMT_F:
        return "%f";
    case TC_FMT_E:
        return "%e";
    case TC_FMT_EU:
        return "%E";
    case TC_FMT_G:
        return "%g";
    case TC_FMT_GU:
        return "%G";
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
    case TC_ERR_CONSTANT_OVERFLOW:
        return "ConstantOverflow";
    case TC_ERR_CONSTANT_DIV_ZERO:
        return "ConstantDivisionByZero";
    case TC_ERR_CONSTANT_CAST_OVERFLOW:
        return "ConstantCastOverflow";
    case TC_ERR_COMPARISON_TYPE_MISMATCH:
        return "ComparisonTypeMismatch";
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
    case TC_ERR_CAST_OVERFLOW:
        return "CastOverflow";
    case TC_ERR_IO:
        return "IOError";
    case TC_ERR_OUT_OF_MEMORY:
        return "OutOfMemory";
    case TC_ERR_INDENT_MIXED:
        return "IndentMixedError";
    case TC_ERR_INDENT_INSUFFICIENT:
        return "IndentInsufficientError";
    case TC_ERR_INDENT_ELSE_END:
        return "IndentElseEndError";
    case TC_ERR_MISSING_END:
        return "MissingEndError";
    case TC_ERR_ELSE_POSITION:
        return "ElsePositionError";
    case TC_ERR_CONDITION_TYPE:
        return "ConditionTypeError";
    case TC_ERR_FLOAT_OVERFLOW:
        return "FloatOverflow";
    case TC_ERR_FLOAT_UNDERFLOW:
        return "FloatUnderflow";
    case TC_ERR_FLOAT_INVALID:
        return "FloatInvalidOperation";
    case TC_ERR_MODE_MISMATCH:
        return "ModeMismatch";
    case TC_ERR_UNINITIALIZED_VARIABLE:
        return "UninitializedVariable";
    case TC_ERR_LABEL_NOT_FOUND:
        return "LabelNotFound";
    case TC_ERR_DUPLICATE_LABEL:
        return "DuplicateLabel";
    case TC_ERR_JUMP_INTO_BLOCK:
        return "JumpIntoBlockError";
    case TC_ERR_JUMP_TO_SIBLING_BLOCK:
        return "JumpToSiblingBlockError";
    case TC_ERR_VAR_MISSING_INIT:
        return "VarMissingInitializer";
    case TC_ERR_BITCAST_WIDTH:
        return "BitcastWidthError";
    case TC_ERR_LABEL_INSIDE_LOOP:
        return "LabelInsideLoop";
    case TC_ERR_GOTO_INSIDE_LOOP:
        return "GotoInsideLoop";
    case TC_ERR_BREAK_OUTSIDE_LOOP:
        return "BreakOutsideLoop";
    case TC_ERR_CONTINUE_OUTSIDE_LOOP:
        return "ContinueOutsideLoop";
    case TC_ERR_GOTO_OUTSIDE_FUNCTION:
        return "GotoOutsideFunction";
    case TC_ERR_LABEL_OUTSIDE_FUNCTION:
        return "LabelOutsideFunction";
    case TC_ERR_JUMP_INCOMPATIBLE_BLOCK:
        return "JumpIncompatibleBlockError";
    case TC_ERR_NEGATIVE_SHIFT_COUNT:
        return "NegativeShiftCount";
    case TC_ERR_FORMAT_SPECIFIER:
        return "FormatSpecifierError";
    case TC_ERR_DUPLICATE_FUNCTION:
        return "DuplicateFunction";
    case TC_ERR_FUNCTION_NAME_CONFLICT:
        return "FunctionNameConflict";
    case TC_ERR_UNDEFINED_FUNCTION:
        return "UndefinedFunction";
    case TC_ERR_DUPLICATE_PARAMETER:
        return "DuplicateParameter";
    case TC_ERR_MISSING_ARGUMENT:
        return "MissingArgument";
    case TC_ERR_DUPLICATE_ARGUMENT:
        return "DuplicateArgument";
    case TC_ERR_UNKNOWN_ARGUMENT:
        return "UnknownArgument";
    case TC_ERR_ARGUMENT_ORDER:
        return "ArgumentOrderError";
    case TC_ERR_ARGUMENT_TYPE:
        return "ArgumentTypeError";
    case TC_ERR_FUNCALL_POSITION:
        return "FunctionCallPositionError";
    case TC_ERR_FUNCALL_RESULT_TYPE:
        return "FunctionCallResultTypeError";
    case TC_ERR_RETURN_OUTSIDE_FUNCTION:
        return "ReturnOutsideFunction";
    case TC_ERR_RETURN_FORM:
        return "ReturnFormError";
    case TC_ERR_RETURN_TYPE:
        return "ReturnTypeError";
    case TC_ERR_MISSING_RETURN:
        return "MissingReturn";
    case TC_ERR_UNREACHABLE_STATEMENT:
        return "UnreachableStatement";
    case TC_ERR_PARAMETER_ASSIGNMENT:
        return "ParameterAssignmentError";
    case TC_ERR_FUNCTION_SCOPE_ACCESS:
        return "FunctionScopeAccessError";
    case TC_ERR_CROSS_CONTROL_FLOW_JUMP:
        return "CrossControlFlowJumpError";
    case TC_ERR_RECURSION:
        return "RecursionError";
    case TC_ERR_MEMBLOCK_INDEX_OUT_OF_RANGE:
    case TC_ERR_MEMBLOCK_INDEX_OUT_OF_RANGE_RT:
        return "MemblockIndexOutOfRange";
    case TC_ERR_MEMBLOCK_ELEMENT_COUNT_MISMATCH:
        return "MemblockElementCountMismatch";
    case TC_ERR_MEMBLOCK_SIZE_MISMATCH:
        return "MemblockSizeMismatch";
    case TC_ERR_STRUCT_MISSING_FIELD:
        return "StructMissingField";
    case TC_ERR_STRUCT_UNKNOWN_FIELD:
        return "StructUnknownField";
    case TC_ERR_STRUCT_DUPLICATE_FIELD:
        return "StructDuplicateField";
    case TC_ERR_STRUCT_FIELD_ORDER:
        return "StructFieldOrderError";
    case TC_ERR_STRUCT_IMMUTABLE_FIELD:
        return "StructImmutableFieldError";
    case TC_ERR_DUPLICATE_STRUCT:
        return "DuplicateStruct";
    case TC_ERR_UNDEFINED_STRUCT:
        return "UndefinedStruct";
    case TC_ERR_MODULE_LAYER:
        return "ModuleLayerError";
    case TC_ERR_MISSING_VISIBILITY:
        return "MissingVisibilityError";
    case TC_ERR_PROGRAM_MODE_MISUSE:
        return "ProgramModeMisuseError";
    case TC_ERR_IMPORT_NOT_FOUND:
        return "ImportNotFound";
    case TC_ERR_IMPORT_NOT_LIB:
        return "ImportNotLib";
    case TC_ERR_IMPORT_AMBIGUOUS:
        return "ImportAmbiguous";
    case TC_ERR_DUPLICATE_IMPORT:
        return "DuplicateImport";
    case TC_ERR_IMPORT_NAME_CONFLICT:
        return "ImportNameConflict";
    case TC_ERR_CIRCULAR_IMPORT:
        return "CircularImport";
    case TC_ERR_PRIVATE_MEMBER_ACCESS:
        return "PrivateMemberAccessError";
    case TC_ERR_MEMCOPY_UNSAFE_INVALID_RANGE:
    case TC_ERR_MEMCOPY_UNSAFE_INVALID_RANGE_RT:
        return "MemcopyUnsafeInvalidRange";
    case TC_ERR_NULL_POINTER_DEREFERENCE:
        return "NullPointerDereference";
    case TC_ERR_NULL_POINTER_ARITHMETIC:
        return "NullPointerArithmetic";
    }
    return "UnknownError";
}

const char *tc_api_error_code_name(TcApiErrorCode code) {
    switch (code) {
    case TC_API_ERR_NONE:
        return "None";
    case TC_API_ERR_INVALID_ARGUMENT:
        return "InvalidArgument";
    case TC_API_ERR_FILE_OPEN:
        return "FileOpen";
    case TC_API_ERR_FILE_READ:
        return "FileRead";
    }
    return "UnknownApiError";
}

const char *tc_warning_kind_name(TcWarningKind kind) {
    switch (kind) {
    case TC_WARN_NONE:
        return "None";
    }
    return "UnknownWarning";
}

const char *tc_type_name(TcTypeKind type) {
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
    case TC_BOOL:
        return "bool";
    case TC_FLOAT32:
        return "float32";
    case TC_FLOAT64:
        return "float64";
    case TC_ISIZE:
        return "isize";
    case TC_USIZE:
        return "usize";
    case TC_VOID:
        return "void";
    case TC_PTR:
        return "ptr";
    case TC_MEMBLOCK:
        return "memblock";
    case TC_STRUCT:
        return "struct";
    }
    return "unknown";
}
