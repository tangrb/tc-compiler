/*
 * tc_types.c — TC 类型标签、完整类型与运算符工具函数
 *
 * 提供：
 *   - 类型位宽查询（tc_type_bit_width / tc_sizeof_bits）、符号性判定
 *   - 完整类型构造 / 深拷贝 / 等价（tc_type_scalar / tc_type_copy / tc_type_equals）
 *   - struct 布局位宽显式回调（tc_sizeof_bits_ex）
 *   - 类型名解析与字符串化、运算符/格式说明符、错误种类名
 */
#include "tc_types.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

size_t tc_target_ptr_width_bits(void) {
    return (size_t)(sizeof(void *) * 8U);
}

TcType tc_type_from_tag(TcTypeTag tag) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.tag = tag;
    return t;
}

TcType tc_type_scalar(TcTypeTag tag) {
    return tc_type_from_tag(tag);
}

TcTypeTag tc_type_scalar_tag(const TcType *type) {
    assert(type != NULL);
    return type->tag;
}

const TcType tc_type_singletons[TC_TYPE_TAG_COUNT] = {
    [TC_INT8] = {.tag = TC_INT8},
    [TC_UINT8] = {.tag = TC_UINT8},
    [TC_INT16] = {.tag = TC_INT16},
    [TC_UINT16] = {.tag = TC_UINT16},
    [TC_INT32] = {.tag = TC_INT32},
    [TC_UINT32] = {.tag = TC_UINT32},
    [TC_INT64] = {.tag = TC_INT64},
    [TC_UINT64] = {.tag = TC_UINT64},
    [TC_BOOL] = {.tag = TC_BOOL},
    [TC_FLOAT32] = {.tag = TC_FLOAT32},
    [TC_FLOAT64] = {.tag = TC_FLOAT64},
    [TC_ISIZE] = {.tag = TC_ISIZE},
    [TC_USIZE] = {.tag = TC_USIZE},
    [TC_VOID] = {.tag = TC_VOID},
    [TC_PTR] = {.tag = TC_PTR},
    [TC_MEMBLOCK] = {.tag = TC_MEMBLOCK},
    [TC_STRUCT] = {.tag = TC_STRUCT},
};

const TcType *tc_type_tag_singleton(TcTypeTag tag) {
    assert((int)tag >= 0 && (int)tag < TC_TYPE_TAG_COUNT);
    return &tc_type_singletons[tag];
}

void tc_type_table_init(TcTypeTable *table) {
    if (!table) {
        return;
    }
    memset(table, 0, sizeof(*table));
}

void tc_type_table_free(TcTypeTable *table) {
    size_t i;

    if (!table) {
        return;
    }
    for (i = 0; i < table->count; i++) {
        if (table->nodes[i]) {
            /* 子指针指向池内其它节点或单例，不可 tc_type_free 递归 */
            free(table->nodes[i]);
            table->nodes[i] = NULL;
        }
    }
    free(table->nodes);
    memset(table, 0, sizeof(*table));
}

/** memblock 入池键：equals 忽略 N，但池内节点按 T+N 区分以便 sizeof */
static int tc_type_pool_same(const TcType *a, const TcType *b) {
    if (!a || !b) {
        return a == b;
    }
    if (a->tag != b->tag) {
        return 0;
    }
    switch (a->tag) {
    case TC_PTR:
        return tc_type_pool_same(a->params.ptr_type.pointee, b->params.ptr_type.pointee);
    case TC_MEMBLOCK:
        if (a->params.memblock_type.count != b->params.memblock_type.count) {
            return 0;
        }
        return tc_type_pool_same(a->params.memblock_type.element,
                                 b->params.memblock_type.element);
    case TC_STRUCT:
        return a->params.struct_type.struct_id == b->params.struct_type.struct_id;
    default:
        return 1;
    }
}

const TcType *tc_type_intern(TcTypeTable *table, const TcType *type, TcDiagnostic *diag) {
    TcType *node = NULL;
    TcType built;
    const TcType *child = NULL;
    size_t i;

    (void)diag;
    if (!type) {
        return NULL;
    }
    if (type->tag != TC_PTR && type->tag != TC_MEMBLOCK && type->tag != TC_STRUCT) {
        return tc_type_tag_singleton(type->tag);
    }
    if (!table) {
        return NULL;
    }

    memset(&built, 0, sizeof(built));
    built.tag = type->tag;
    if (type->tag == TC_PTR) {
        if (type->params.ptr_type.pointee) {
            child = tc_type_intern(table, type->params.ptr_type.pointee, diag);
            if (!child) {
                return NULL;
            }
            /* 池内节点持有「非拥有」子指针：子节点已在池/单例中，free 时不可递归释放 */
            built.params.ptr_type.pointee = (TcType *)(uintptr_t)child;
        }
    } else if (type->tag == TC_MEMBLOCK) {
        if (type->params.memblock_type.element) {
            child = tc_type_intern(table, type->params.memblock_type.element, diag);
            if (!child) {
                return NULL;
            }
            built.params.memblock_type.element = (TcType *)(uintptr_t)child;
        }
        built.params.memblock_type.count = type->params.memblock_type.count;
    } else {
        built.params.struct_type.struct_id = type->params.struct_type.struct_id;
    }

    for (i = 0; i < table->count; i++) {
        if (tc_type_pool_same(table->nodes[i], &built)) {
            return table->nodes[i];
        }
    }

    node = (TcType *)malloc(sizeof(TcType));
    if (!node) {
        return NULL;
    }
    *node = built;

    if (table->count >= table->capacity) {
        size_t new_cap = table->capacity == 0 ? 16 : table->capacity * 2;
        TcType **grown = (TcType **)realloc(table->nodes, new_cap * sizeof(TcType *));
        if (!grown) {
            free(node);
            return NULL;
        }
        table->nodes = grown;
        table->capacity = new_cap;
    }
    table->nodes[table->count++] = node;
    return node;
}

TcType tc_type_make_ptr(TcType *pointee) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.tag = TC_PTR;
    t.params.ptr_type.pointee = pointee;
    return t;
}

TcType tc_type_make_memblock(TcType *element, uint64_t count) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.tag = TC_MEMBLOCK;
    t.params.memblock_type.element = element;
    t.params.memblock_type.count = count;
    return t;
}

TcType tc_type_make_struct(int struct_id) {
    TcType t;
    memset(&t, 0, sizeof(t));
    t.tag = TC_STRUCT;
    t.params.struct_type.struct_id = struct_id;
    return t;
}

void tc_type_free(TcType *type) {
    if (!type) {
        return;
    }
    if (type->tag == TC_PTR && type->params.ptr_type.pointee) {
        tc_type_free(type->params.ptr_type.pointee);
        free(type->params.ptr_type.pointee);
        type->params.ptr_type.pointee = NULL;
    } else if (type->tag == TC_MEMBLOCK && type->params.memblock_type.element) {
        tc_type_free(type->params.memblock_type.element);
        free(type->params.memblock_type.element);
        type->params.memblock_type.element = NULL;
    }
    memset(type, 0, sizeof(*type));
}

int tc_type_copy(const TcType *src, TcType *out, TcDiagnostic *diag) {
    TcType copy;

    (void)diag;
    if (!src || !out) {
        return -1;
    }
    memset(&copy, 0, sizeof(copy));
    copy.tag = src->tag;
    if (src->tag == TC_PTR) {
        TcType *pointee = NULL;

        if (src->params.ptr_type.pointee) {
            pointee = (TcType *)malloc(sizeof(TcType));
            if (!pointee) {
                return -1;
            }
            if (tc_type_copy(src->params.ptr_type.pointee, pointee, diag) != 0) {
                free(pointee);
                return -1;
            }
        }
        copy.params.ptr_type.pointee = pointee;
    } else if (src->tag == TC_MEMBLOCK) {
        TcType *element = NULL;

        if (src->params.memblock_type.element) {
            element = (TcType *)malloc(sizeof(TcType));
            if (!element) {
                return -1;
            }
            if (tc_type_copy(src->params.memblock_type.element, element, diag) != 0) {
                free(element);
                return -1;
            }
        }
        copy.params.memblock_type.element = element;
        copy.params.memblock_type.count = src->params.memblock_type.count;
    } else if (src->tag == TC_STRUCT) {
        copy.params.struct_type.struct_id = src->params.struct_type.struct_id;
    }
    *out = copy;
    return 0;
}

int tc_type_equals(const TcType *a, const TcType *b) {
    if (!a || !b) {
        return 0;
    }
    if (a == b) {
        return 1;
    }
    assert(tc_type_scalar_tag(a) == a->tag);
    assert(tc_type_scalar_tag(b) == b->tag);
    if (a->tag != b->tag) {
        return 0;
    }
    switch (a->tag) {
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

size_t tc_sizeof_bits_ex(const TcType *type, TcStructWidthFn fn, void *userdata) {
    if (!type) {
        return 0;
    }
    assert(tc_type_scalar_tag(type) == type->tag);
    switch (type->tag) {
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
            elem_bits = tc_sizeof_bits_ex(type->params.memblock_type.element, fn, userdata);
        }
        return tc_sizeof_bits_ex(&usize_ty, fn, userdata)
             + (size_t)type->params.memblock_type.count * elem_bits;
    }
    case TC_STRUCT:
        if (fn) {
            return fn(type->params.struct_type.struct_id, userdata);
        }
        return 0;
    case TC_VOID:
        return 0;
    }
    return 0;
}

size_t tc_sizeof_bits(const TcType *type) {
    return tc_sizeof_bits_ex(type, NULL, NULL);
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
int tc_type_bit_width(TcTypeTag type) {
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

int tc_type_is_bool(TcTypeTag type) {
    return type == TC_BOOL;
}

int tc_type_is_integer(TcTypeTag type) {
    return (type >= TC_INT8 && type <= TC_UINT64) || type == TC_ISIZE || type == TC_USIZE;
}

int tc_type_is_float(TcTypeTag type) {
    return type == TC_FLOAT32 || type == TC_FLOAT64;
}

int tc_type_is_void(TcTypeTag type) {
    return type == TC_VOID;
}

int tc_type_is_ptr_tag(TcTypeTag type) {
    return type == TC_PTR;
}

int tc_type_is_memblock_tag(TcTypeTag type) {
    return type == TC_MEMBLOCK;
}

int tc_type_is_struct_tag(TcTypeTag type) {
    return type == TC_STRUCT;
}

/*
 * 有符号性判定。
 * TC_BOOL 被视为无符号；isize 为有符号平台字长整数。
 */
int tc_type_is_signed(TcTypeTag type) {
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

int tc_type_parse(const char *text, TcTypeTag *out) {
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
    } else if (strcmp(text, "xor") == 0) {
        *out = TC_LOGIC_XOR;
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

int tc_format_spec_parse(const char *text, TcFormatFullSpec *out) {
    const char *p;

    if (!text || !out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->spec = TC_FMT_NONE;

    if (text[0] != '%') {
        return 0;
    }
    p = text + 1;

    /* 标志字符：- + # 0，逐个读取，重复 → 非法 */
    for (;;) {
        char c = *p;
        if (c == '-') {
            if (out->flag_minus) return 0;
            out->flag_minus = 1;
        } else if (c == '+') {
            if (out->flag_plus) return 0;
            out->flag_plus = 1;
        } else if (c == '#') {
            if (out->flag_hash) return 0;
            out->flag_hash = 1;
        } else if (c == '0') {
            if (out->flag_zero) return 0;
            out->flag_zero = 1;
        } else {
            break;
        }
        p++;
    }

    /* 宽度：1–65535，前导不可为 0 */
    if (isdigit((unsigned char)*p) && *p != '0') {
        int w = 0;
        while (isdigit((unsigned char)*p)) {
            w = w * 10 + (*p - '0');
            if (w > 65535) return 0;
            p++;
        }
        out->width = w;
    }

    /* 精度：.N */
    if (*p == '.') {
        p++;
        {
            int prec = 0;
            while (isdigit((unsigned char)*p)) {
                prec = prec * 10 + (*p - '0');
                if (prec > 65535) return 0;
                p++;
            }
            out->precision_set = 1;
            out->precision = prec;
        }
    }

    /* 转换字符 */
    switch (*p) {
    case 'd': out->spec = TC_FMT_D; break;
    case 'i': out->spec = TC_FMT_I; break;
    case 'u': out->spec = TC_FMT_U; break;
    case 'x': out->spec = TC_FMT_X; break;
    case 'X': out->spec = TC_FMT_XU; break;
    case 'o': out->spec = TC_FMT_O; break;
    case 'b': out->spec = TC_FMT_B; break;
    case 't': out->spec = TC_FMT_T; break;
    case 'f': out->spec = TC_FMT_F; break;
    case 'e': out->spec = TC_FMT_E; break;
    case 'E': out->spec = TC_FMT_EU; break;
    case 'g': out->spec = TC_FMT_G; break;
    case 'G': out->spec = TC_FMT_GU; break;
    default:  return 0;
    }
    p++;

    /* 不允许尾随字符 */
    if (*p != '\0') return 0;

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
    case TC_CE_SYNTAX:
        return "SyntaxError";
    case TC_CE_UNDEFINED_VARIABLE:
        return "UndefinedVariable";
    case TC_CE_DUPLICATE_DEFINITION:
        return "DuplicateDefinition";
    case TC_CE_TYPE_MISMATCH:
        return "TypeMismatch";
    case TC_CE_LITERAL_OUT_OF_RANGE:
        return "LiteralOutOfRange";
    case TC_CE_LITERAL_TYPE:
        return "LiteralTypeError";
    case TC_CE_KEYWORD:
        return "KeywordError";
    case TC_CE_CONSTANT_ASSIGNMENT:
        return "ConstantAssignmentError";
    case TC_CE_CONSTANT_EXPRESSION:
        return "ConstantExpressionError";
    case TC_CE_CONSTANT_OVERFLOW:
        return "ConstantOverflow";
    case TC_CE_CONSTANT_DIV_ZERO:
        return "ConstantDivisionByZero";
    case TC_CE_CONSTANT_CAST_OVERFLOW:
        return "ConstantCastOverflow";
    case TC_CE_COMPARISON_TYPE_MISMATCH:
        return "ComparisonTypeMismatch";
    case TC_CE_FORMAT_TYPE_MISMATCH:
        return "FormatTypeMismatch";
    case TC_CE_OPERAND_COUNT:
        return "OperandCountError";
    case TC_RE_DIVISION_BY_ZERO:
        return "DivisionByZero";
    case TC_RE_INTEGER_OVERFLOW:
        return "IntegerOverflow";
    case TC_RE_CAST_OVERFLOW:
        return "CastOverflow";
    case TC_RE_IO:
        return "IOError";
    case TC_ERR_OUT_OF_MEMORY:
        return "OutOfMemory";
    case TC_CE_INDENT_MIXED:
        return "IndentMixedError";
    case TC_CE_INDENT_INSUFFICIENT:
        return "IndentInsufficientError";
    case TC_CE_INDENT_ELSE_END:
        return "IndentElseEndError";
    case TC_CE_MISSING_END:
        return "MissingEndError";
    case TC_CE_ELSE_POSITION:
        return "ElsePositionError";
    case TC_CE_CONDITION_TYPE:
        return "ConditionTypeError";
    case TC_RE_FLOAT_OVERFLOW:
        return "FloatOverflow";
    case TC_RE_FLOAT_UNDERFLOW:
        return "FloatUnderflow";
    case TC_RE_FLOAT_INVALID:
        return "FloatInvalidOperation";
    case TC_CE_MODE_MISMATCH:
        return "ModeMismatch";
    case TC_CE_UNINITIALIZED_VARIABLE:
        return "UninitializedVariable";
    case TC_CE_LABEL_NOT_FOUND:
        return "LabelNotFound";
    case TC_CE_DUPLICATE_LABEL:
        return "DuplicateLabel";
    case TC_CE_JUMP_INTO_BLOCK:
        return "JumpIntoBlockError";
    case TC_CE_JUMP_INCOMPATIBLE_BLOCK:
        return "JumpIncompatibleBlockError";
    case TC_CE_VAR_MISSING_INIT:
        return "VarMissingInitializer";
    case TC_CE_BITCAST_WIDTH:
        return "BitcastWidthError";
    case TC_CE_LABEL_INSIDE_LOOP:
        return "LabelInsideLoop";
    case TC_CE_GOTO_INSIDE_LOOP:
        return "GotoInsideLoop";
    case TC_CE_BREAK_OUTSIDE_LOOP:
        return "BreakOutsideLoop";
    case TC_CE_CONTINUE_OUTSIDE_LOOP:
        return "ContinueOutsideLoop";
    case TC_CE_GOTO_OUTSIDE_FUNCTION:
        return "GotoOutsideFunction";
    case TC_CE_LABEL_OUTSIDE_FUNCTION:
        return "LabelOutsideFunction";
    case TC_RE_NEGATIVE_SHIFT_COUNT:
        return "NegativeShiftCount";
    case TC_CE_FORMAT_SPECIFIER:
        return "FormatSpecifierError";
    case TC_CE_DUPLICATE_FUNCTION:
        return "DuplicateFunction";
    case TC_CE_FUNCTION_NAME_CONFLICT:
        return "FunctionNameConflict";
    case TC_CE_UNDEFINED_FUNCTION:
        return "UndefinedFunction";
    case TC_CE_DUPLICATE_PARAMETER:
        return "DuplicateParameter";
    case TC_CE_MISSING_ARGUMENT:
        return "MissingArgument";
    case TC_CE_DUPLICATE_ARGUMENT:
        return "DuplicateArgument";
    case TC_CE_UNKNOWN_ARGUMENT:
        return "UnknownArgument";
    case TC_CE_ARGUMENT_ORDER:
        return "ArgumentOrderError";
    case TC_CE_ARGUMENT_TYPE:
        return "ArgumentTypeError";
    case TC_CE_FUNCALL_POSITION:
        return "FunctionCallPositionError";
    case TC_CE_FUNCALL_RESULT_TYPE:
        return "FunctionCallResultTypeError";
    case TC_CE_RETURN_OUTSIDE_FUNCTION:
        return "ReturnOutsideFunction";
    case TC_CE_RETURN_FORM:
        return "ReturnFormError";
    case TC_CE_RETURN_TYPE:
        return "ReturnTypeError";
    case TC_CE_MISSING_RETURN:
        return "MissingReturn";
    case TC_CE_UNREACHABLE_STATEMENT:
        return "UnreachableStatement";
    case TC_CE_PARAMETER_ASSIGNMENT:
        return "ParameterAssignmentError";
    case TC_CE_FUNCTION_SCOPE_ACCESS:
        return "FunctionScopeAccessError";
    case TC_CE_CROSS_CONTROL_FLOW_JUMP:
        return "CrossControlFlowJumpError";
    case TC_CE_RECURSION:
        return "RecursionError";
    case TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE:
    case TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE:
        return "MemblockIndexOutOfRange";
    case TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH:
        return "MemblockElementCountMismatch";
    case TC_CE_MEMBLOCK_SIZE_MISMATCH:
        return "MemblockSizeMismatch";
    case TC_CE_STRUCT_MISSING_FIELD:
        return "StructMissingField";
    case TC_CE_STRUCT_UNKNOWN_FIELD:
        return "StructUnknownField";
    case TC_CE_STRUCT_DUPLICATE_FIELD:
        return "StructDuplicateField";
    case TC_CE_STRUCT_FIELD_ORDER:
        return "StructFieldOrderError";
    case TC_CE_STRUCT_IMMUTABLE_FIELD:
        return "StructImmutableFieldError";
    case TC_CE_DUPLICATE_STRUCT:
        return "DuplicateStruct";
    case TC_CE_UNDEFINED_STRUCT:
        return "UndefinedStruct";
    case TC_CE_MODULE_LAYER:
        return "ModuleLayerError";
    case TC_CE_MISSING_VISIBILITY:
        return "MissingVisibilityError";
    case TC_CE_PROGRAM_MODE_MISUSE:
        return "ProgramModeMisuseError";
    case TC_CE_IMPORT_NOT_FOUND:
        return "ImportNotFound";
    case TC_CE_IMPORT_NOT_LIB:
        return "ImportNotLib";
    case TC_CE_IMPORT_AMBIGUOUS:
        return "ImportAmbiguous";
    case TC_CE_DUPLICATE_IMPORT:
        return "DuplicateImport";
    case TC_CE_IMPORT_NAME_CONFLICT:
        return "ImportNameConflict";
    case TC_CE_CIRCULAR_IMPORT:
        return "CircularImport";
    case TC_CE_PRIVATE_MEMBER_ACCESS:
        return "PrivateMemberAccessError";
    case TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE:
    case TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE:
        return "MemcopyUnsafeInvalidRange";
    case TC_RE_NULL_POINTER_DEREFERENCE:
        return "NullPointerDereference";
    case TC_RE_NULL_POINTER_ARITHMETIC:
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

const char *tc_type_name(TcTypeTag type) {
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
