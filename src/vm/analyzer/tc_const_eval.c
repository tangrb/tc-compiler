/*
 * tc_const_eval.c — let 常量编译期求值实现
 *
 * 从 tc_analyzer.c 拆出：源序求值 let；映射运行时错误为编译期常量错误；
 * 与 Executor 共用 tc_sem_*。复合/funcall RHS 在 let 中拒绝（defer）。
 */
#include "tc_const_eval.h"

#include "tc_diagnostic.h"
#include "tc_semantics.h"
#include "tc_struct_check.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tc_const_heap_named(uint64_t bits, const TcSymbolTable *table) {
    size_t i = 0;

    if (!table || bits == 0) {
        return 0;
    }
    for (i = 0; i < table->count; i++) {
        /* 仅复合类型（struct/memblock）的 const_value.bits 才是堆地址；
         * 标量 const 的位模式可能恰巧等于某次分配地址，不得误判为别名。 */
        if (table->symbols[i].has_const_value && table->symbols[i].const_value.type &&
            (table->symbols[i].const_value.type->tag == TC_STRUCT ||
             table->symbols[i].const_value.type->tag == TC_MEMBLOCK) &&
            table->symbols[i].const_value.bits == bits) {
            return 1;
        }
    }
    return 0;
}

static void tc_const_drop_temp_heap(TcValue *value, const TcSymbolTable *visible,
                                    const TcSymbolTable *global) {
    if (!value || value->bits == 0 || !value->type) {
        return;
    }
    if (value->type->tag != TC_STRUCT && value->type->tag != TC_MEMBLOCK) {
        return;
    }
    if (tc_const_heap_named(value->bits, visible) || tc_const_heap_named(value->bits, global)) {
        return;
    }
    free((void *)(uintptr_t)value->bits);
    value->bits = 0;
}

/* ------------------------------------------------------------------ */
/*  常量求值辅助                                                         */
/* ------------------------------------------------------------------ */

/*
 * 将运行时语义错误（tc_exec_* 产生的 TC_RE_*）映射为对应的
 * 编译期常量错误（TC_CE_CONSTANT_*）。
 *
 * 运行时错误类型与常量错误类型一一对应，这种区分使 TC 语言的
 * 错误报告能精确区分"运行时溢出"和"编译期常量溢出"，
 * 便于测试断言和用户定位。
 */
static int tc_const_map_runtime_error(TcErrorKind kind, TcDiagnostic *diag, int line) {
    switch (kind) {
    case TC_RE_INTEGER_OVERFLOW:
        tc_diagnostic_set(diag, TC_CE_CONSTANT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "constant overflow");
        return -1;
    case TC_RE_DIVISION_BY_ZERO:
        tc_diagnostic_set(diag, TC_CE_CONSTANT_DIV_ZERO, line, TC_COLUMN_UNKNOWN,
                          "constant division by zero");
        return -1;
    case TC_RE_NEGATIVE_SHIFT_COUNT:
        /* 常量移位负计数报常量表达式错误，不映射为常量溢出（规范 §6.4.3） */
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "negative shift count");
        return -1;
    case TC_RE_CAST_OVERFLOW:
        tc_diagnostic_set(diag, TC_CE_CONSTANT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "constant cast overflow");
        return -1;
    case TC_RE_FLOAT_OVERFLOW:
    case TC_RE_FLOAT_UNDERFLOW:
        tc_diagnostic_set(diag, TC_CE_CONSTANT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "constant overflow");
        return -1;
    case TC_RE_FLOAT_INVALID:
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid floating-point constant expression");
        return -1;
    default:
        return -1;
    }
}

static int tc_try_eval_bound_operand(const TcOperand *operand, TcTypeTag expected, TcValue *out) {
    if (operand->kind == TC_OPERAND_LIT) {
        if (!tc_literal_fits_context(&operand->u.lit, expected, NULL)) {
            return 0;
        }
        *out = tc_literal_to_value(&operand->u.lit, expected);
        return 1;
    }
    if (!operand->binding.resolved || !operand->binding.is_const ||
        operand->binding.type->tag != expected) {
        return 0;
    }
    *out = tc_value_make(expected, operand->binding.const_bits);
    return 1;
}

static int tc_try_eval_bound_const_ref(const TcRhs *rhs, TcValue *out) {
    const TcResolvedBinding *binding = &rhs->u.const_ref.binding;

    if (!binding->resolved || !binding->is_const || binding->type->tag != TC_BOOL) {
        return 0;
    }
    *out = tc_value_make(TC_BOOL, binding->const_bits);
    return 1;
}

void tc_try_eval_static_bool_operand(const TcOperand *operand, TcStaticBoolResult *result) {
    TcValue value = {0};

    *result = TC_STATIC_BOOL_UNKNOWN;
    if (tc_try_eval_bound_operand(operand, TC_BOOL, &value)) {
        *result = value.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
    }
}

int tc_try_eval_static_bool(const TcRhs *rhs, int line, TcStaticBoolResult *result,
                            TcDiagnostic *diag) {
    TcDiagnostic tmp_diag;
    TcValue lhs = {0};
    TcValue rhs_value = {0};
    TcValue out = {0};
    int status = 0;

    *result = TC_STATIC_BOOL_UNKNOWN;
    if (rhs->kind == TC_RHS_LIT) {
        if (rhs->u.lit.is_bool) {
            *result = rhs->u.lit.magnitude == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
        }
        return 0;
    }
    if (rhs->kind == TC_RHS_CONST_REF) {
        if (tc_try_eval_bound_const_ref(rhs, &out)) {
            *result = out.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
        }
        return 0;
    }

    switch (rhs->kind) {
    case TC_RHS_COMPARE:
        if (!tc_try_eval_bound_operand(&rhs->u.compare.lhs, rhs->u.compare.type->tag, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.compare.rhs, rhs->u.compare.type->tag, &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type->tag, &lhs, &rhs_value, &out,
                                 &tmp_diag, line);
        break;
    case TC_RHS_FLOAT_COMPARE:
        if (!tc_try_eval_bound_operand(&rhs->u.float_compare.lhs,
                                       rhs->u.float_compare.type->tag, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.float_compare.rhs,
                                       rhs->u.float_compare.type->tag, &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_fp_compare(rhs->u.float_compare.op, rhs->u.float_compare.type->tag,
                                    rhs->u.float_compare.mode, &lhs, &rhs_value, &out,
                                    &tmp_diag, line);
        break;
    case TC_RHS_LOGIC_BIN:
        if (!tc_try_eval_bound_operand(&rhs->u.logic_bin.lhs, TC_BOOL, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.logic_bin.rhs, TC_BOOL, &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_value, &out, &tmp_diag,
                                      line);
        break;
    case TC_RHS_LOGIC_UN:
        if (!tc_try_eval_bound_operand(&rhs->u.logic_un.operand, TC_BOOL, &lhs)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_logic_unary(rhs->u.logic_un.op, &lhs, &out, &tmp_diag, line);
        break;
    case TC_RHS_CAST:
        if (rhs->u.cast.target.tag != TC_BOOL || rhs->u.cast.mode != TC_TRUNC_STRICT ||
            !rhs->u.cast.source_type_resolved ||
            !tc_try_eval_bound_operand(&rhs->u.cast.source, rhs->u.cast.source_type->tag, &lhs)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_cast(TC_BOOL, &lhs, &out, &tmp_diag, line);
        break;
    default:
        return 0;
    }

    if (status != 0) {
        (void)tc_const_map_runtime_error(tmp_diag.kind, diag, line);
        tc_diagnostic_clear(&tmp_diag);
        return -1;
    }
    tc_diagnostic_clear(&tmp_diag);
    *result = out.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
    return 0;
}

static int tc_const_read_resolved_field(const TcResolvedFieldAccess *access,
                                        const TcSymbol *base_sym, TcTypeTag expected,
                                        TcValue *out, int line, TcDiagnostic *diag) {
    const uint8_t *data = NULL;
    size_t offset = 0;
    size_t nbytes = 0;
    const TcType *field_type = NULL;
    uint64_t bits = 0;

    if (!access || !access->resolved) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    if (access->base_slot >= 0) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant expression cannot reference var variable");
        return -1;
    }
    if (access->is_memblock_count) {
        if (base_sym && base_sym->type) {
            *out = tc_value_make(TC_USIZE, tc_type_memblock_count(base_sym->type));
        } else {
            *out = tc_value_make(TC_USIZE, 0);
        }
        return 0;
    }
    if (!access->field_type || access->field_count == 0 || !access->offsets) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    field_type = access->field_type;
    if (tc_type_tag_of(field_type) != expected) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "constant type does not match expected type");
        return -1;
    }
    if (field_type->tag == TC_STRUCT || field_type->tag == TC_MEMBLOCK) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    /* 优先用基址符号当前 const_value（static let 拓扑求值后才就绪） */
    if (base_sym && base_sym->has_const_value) {
        bits = base_sym->const_value.bits;
    } else {
        bits = access->const_bits;
    }
    data = (const uint8_t *)(uintptr_t)bits;
    if (!data) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant value is not available by source order");
        return -1;
    }
    offset = access->offsets[access->field_count - 1];
    nbytes = (tc_sizeof_bits(field_type) + 7U) / 8U;
    out->type = field_type;
    out->bits = 0;
    memcpy(&out->bits, data + offset, nbytes <= sizeof(out->bits) ? nbytes : sizeof(out->bits));
    if (field_type->tag == TC_BOOL) {
        out->bits = out->bits ? 1ULL : 0ULL;
    }
    return 0;
}

static const TcSymbol *tc_const_lookup_field_base(const char *base, const TcSymbolTable *visible,
                                                  const TcSymbolTable *global) {
    const char *member = NULL;

    if (!base) {
        return NULL;
    }
    if (strncmp(base, "Self.", 5) == 0) {
        member = base + 5;
        if (global) {
            return tc_symbol_table_find(global, member);
        }
        return NULL;
    }
    {
        const char *dot = strchr(base, '.');

        if (dot && dot != base && dot[1] != '\0') {
            member = dot + 1;
            if (global) {
                return tc_symbol_table_find(global, member);
            }
            return NULL;
        }
    }
    if (visible) {
        const TcSymbol *sym = tc_symbol_table_find(visible, base);

        if (sym) {
            return sym;
        }
    }
    if (global) {
        return tc_symbol_table_find(global, base);
    }
    return NULL;
}

static int tc_eval_const_operand(const TcOperand *operand, TcTypeTag expected,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 const char *const_name, TcValue *out, int line,
                                 TcDiagnostic *diag) {
    char msg[128];

    if (operand->kind == TC_OPERAND_LIT) {
        if (!tc_literal_fits_context(&operand->u.lit, expected, NULL)) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid literal in constant expression");
            return -1;
        }
        *out = tc_literal_to_value(&operand->u.lit, expected);
        return 0;
    }

    if (operand->kind == TC_OPERAND_FIELD_READ) {
        const TcSymbol *base_sym = NULL;

        if (!operand->u.field_read.resolved.resolved) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid constant expression");
            return -1;
        }
        base_sym = tc_const_lookup_field_base(operand->u.field_read.base, visible, global);
        return tc_const_read_resolved_field(&operand->u.field_read.resolved, base_sym, expected,
                                            out, line, diag);
    }

    {
        const TcSymbol *symbol = NULL;

        if (const_name && operand->u.name && strcmp(operand->u.name, const_name) == 0) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        symbol = tc_symbol_table_find(visible, operand->u.name);
        if (!symbol) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (symbol->sym_kind != TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        (void)global;
        if (!symbol->has_const_value) {
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                              "constant value is not available by source order");
            return -1;
        }
        if (tc_type_tag_of(symbol->type) != expected) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "operand type does not match operation type");
            return -1;
        }
        *out = symbol->const_value;
        return 0;
    }
}

static size_t tc_const_type_payload_bytes(const TcType *type, const TcStructTable *table) {
    if (!type) {
        return 0;
    }
    if (type->tag == TC_STRUCT) {
        const TcStructEntry *entry =
            tc_struct_table_get(table, type->params.struct_type.struct_id);
        if (!entry) {
            return 0;
        }
        return (entry->width_bits + 7U) / 8U;
    }
    return (tc_sizeof_bits_ex(type, tc_struct_table_width_bits, table) + 7U) / 8U;
}

static int tc_const_write_field_bytes(uint8_t *base, size_t offset, const TcType *field_type,
                                      const TcValue *value, const TcStructTable *table) {
    size_t nbytes = tc_const_type_payload_bytes(field_type, table);
    uint8_t *dst = base + offset;

    if (field_type->tag == TC_STRUCT) {
        if (!value || value->bits == 0 || nbytes == 0) {
            return -1;
        }
        memcpy(dst, (const void *)(uintptr_t)value->bits, nbytes);
        return 0;
    }
    if (field_type->tag == TC_MEMBLOCK) {
        if (!value || value->bits == 0 || nbytes == 0) {
            return -1;
        }
        memcpy(dst, (const void *)(uintptr_t)value->bits, nbytes);
        return 0;
    }
    {
        uint64_t bits = value->bits;
        if (field_type->tag == TC_BOOL) {
            bits = bits ? 1ULL : 0ULL;
        }
        memset(dst, 0, nbytes);
        memcpy(dst, &bits, nbytes <= sizeof(bits) ? nbytes : sizeof(bits));
    }
    return 0;
}

static int tc_eval_const_rhs(const TcRhs *rhs, TcTypeTag expected_type,
                             const TcSymbolTable *visible, const TcSymbolTable *global,
                             const TcStructTable *struct_table, const char *const_name,
                             TcValue *out, int line, TcDiagnostic *diag);

static int tc_eval_const_ctor_field(const TcRhs *rhs_field, int has_rhs, const TcOperand *value_op,
                                    const TcType *field_type, const TcSymbolTable *visible,
                                    const TcSymbolTable *global, const TcStructTable *struct_table,
                                    const char *const_name, TcValue *out, int line,
                                    TcDiagnostic *diag) {
    if (has_rhs && rhs_field) {
        return tc_eval_const_rhs(rhs_field, field_type->tag, visible, global, struct_table,
                                 const_name, out, line, diag);
    }
    return tc_eval_const_operand(value_op, field_type->tag, visible, global, const_name, out, line,
                                 diag);
}

static int tc_eval_const_struct_ctor(const TcRhs *rhs, const TcStructTable *table,
                                     const TcSymbolTable *visible, const TcSymbolTable *global,
                                     const char *const_name, TcValue *out, int line,
                                     TcDiagnostic *diag) {
    const TcStructEntry *entry = NULL;
    void *block = NULL;
    size_t bit_off = 0;
    size_t i = 0;

    if (!rhs || rhs->kind != TC_RHS_STRUCT_CONSTRUCTOR || !table) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    entry = tc_struct_table_find(table, rhs->u.struct_ctor.struct_name);
    if (!entry) {
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN,
                          "undefined struct type for constant constructor");
        return -1;
    }
    block = calloc(1, (entry->width_bits + 7U) / 8U);
    if (!block) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }

    for (i = 0; i < entry->field_count; i++) {
        const TcStructField *field = &entry->fields[i];
        size_t field_bits = 0;
        size_t offset_bytes = bit_off / 8U;
        TcValue field_value = {0};
        size_t fi = 0;
        int found = 0;

        for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
            if (strcmp(rhs->u.struct_ctor.fields[fi].param_name, field->name) == 0) {
                if (tc_eval_const_ctor_field(
                        (const TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs,
                        rhs->u.struct_ctor.fields[fi].has_rhs,
                        &rhs->u.struct_ctor.fields[fi].value_op, &field->type, visible, global,
                        table, const_name, &field_value, line, diag) != 0) {
                    free(block);
                    return -1;
                }
                found = 1;
                break;
            }
        }
        if (!found) {
            free(block);
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "missing field in constant struct constructor");
            return -1;
        }
        if (tc_const_write_field_bytes((uint8_t *)block, offset_bytes, &field->type, &field_value,
                                       table) != 0) {
            tc_const_drop_temp_heap(&field_value, visible, global);
            free(block);
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid constant struct field value");
            return -1;
        }
        tc_const_drop_temp_heap(&field_value, visible, global);
        field_bits = tc_sizeof_bits_ex(&field->type, tc_struct_table_width_bits, table);
        bit_off += field_bits + (size_t)field->padding * 8U;
    }

    out->type = tc_type_tag_singleton(TC_STRUCT);
    out->bits = (uint64_t)(uintptr_t)block;
    return 0;
}

static int tc_eval_const_memblock_ctor(const TcRhs *rhs, const TcStructTable *struct_table,
                                       const TcSymbolTable *visible, const TcSymbolTable *global,
                                       const char *const_name, TcValue *out, int line,
                                       TcDiagnostic *diag) {
    uint64_t count = 0;
    size_t element_bits = 0;
    size_t element_bytes = 0;
    size_t payload_bytes = 0;
    void *block = NULL;
    uint8_t *cursor = NULL;
    const TcType *elem = NULL;
    size_t i = 0;

    if (!rhs || rhs->kind != TC_RHS_MEMBLOCK_CONSTRUCTOR || !out) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid constant expression");
        return -1;
    }
    elem = &rhs->u.memblock_ctor.element_type;
    count = rhs->u.memblock_ctor.count;
    if (count < 1) {
        tc_diagnostic_set(diag, TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "memblock count must be at least 1");
        return -1;
    }
    /* 逐值构造必须恰好 count 个元素；static let 在 pass2 类型检查之前求值，
     * 此处是计数校验的最后防线（与 tc_memblock_check_rhs 的 value_count != count 一致）。 */
    if (!rhs->u.memblock_ctor.is_fill && rhs->u.memblock_ctor.value_count != count) {
        tc_diagnostic_set(diag, TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "memblock element count mismatch");
        return -1;
    }
    element_bits = tc_sizeof_bits_ex(elem, tc_struct_table_width_bits, struct_table);
    element_bytes = (element_bits + 7U) / 8U;
    if (element_bytes > 0 && count > (SIZE_MAX - sizeof(uint64_t)) / element_bytes) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    payload_bytes = (size_t)count * element_bytes;
    /* calloc：即使未来出现 value_count < count 的漏网路径，尾部也保持零初始化 */
    block = calloc(1, sizeof(uint64_t) + payload_bytes);
    if (!block) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    memcpy(block, &count, sizeof(uint64_t));
    cursor = (uint8_t *)block + sizeof(uint64_t);
    if (rhs->u.memblock_ctor.is_fill) {
        TcValue fill_value = {0};

        if (tc_eval_const_operand(&rhs->u.memblock_ctor.fill_value, elem->tag, visible, global,
                                  const_name, &fill_value, line, diag) != 0) {
            free(block);
            return -1;
        }
        if (elem->tag == TC_BOOL) {
            fill_value.bits = fill_value.bits ? 1ULL : 0ULL;
        }
        for (i = 0; i < count; i++) {
            if (elem->tag == TC_STRUCT) {
                memcpy(cursor + i * element_bytes, (void *)(uintptr_t)fill_value.bits,
                       element_bytes);
            } else {
                memcpy(cursor + i * element_bytes, &fill_value.bits, element_bytes);
            }
        }
        tc_const_drop_temp_heap(&fill_value, visible, global);
    } else {
        for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
            TcValue elem_val = {0};

            if (tc_eval_const_operand(&rhs->u.memblock_ctor.values[i], elem->tag, visible, global,
                                      const_name, &elem_val, line, diag) != 0) {
                free(block);
                return -1;
            }
            if (elem->tag == TC_BOOL) {
                elem_val.bits = elem_val.bits ? 1ULL : 0ULL;
            }
            if (elem->tag == TC_STRUCT) {
                memcpy(cursor + i * element_bytes, (void *)(uintptr_t)elem_val.bits, element_bytes);
            } else {
                memcpy(cursor + i * element_bytes, &elem_val.bits, element_bytes);
            }
            tc_const_drop_temp_heap(&elem_val, visible, global);
        }
    }
    out->type = tc_type_tag_singleton(TC_MEMBLOCK);
    out->bits = (uint64_t)(uintptr_t)block;
    return 0;
}

static int tc_eval_const_rhs(const TcRhs *rhs, TcTypeTag expected_type,
                             const TcSymbolTable *visible, const TcSymbolTable *global,
                             const TcStructTable *struct_table, const char *const_name,
                             TcValue *out, int line, TcDiagnostic *diag) {
    TcDiagnostic tmp_diag;
    TcValue lhs = {0};
    TcValue rhs_val = {0};

    if (rhs->kind == TC_RHS_LIT) {
        if (!tc_literal_fits_context(&rhs->u.lit, expected_type, NULL)) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid literal in constant expression");
            return -1;
        }
        *out = tc_literal_to_value(&rhs->u.lit, expected_type);
        return 0;
    }

    if (rhs->kind == TC_RHS_FIELD_READ && rhs->u.field_read.resolved.resolved) {
        const TcSymbol *base_sym =
            tc_const_lookup_field_base(rhs->u.field_read.base, visible, global);

        return tc_const_read_resolved_field(&rhs->u.field_read.resolved, base_sym, expected_type,
                                            out, line, diag);
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        if (const_name && strcmp(rhs->u.const_ref.name, const_name) == 0) {
            char msg[128];

            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.const_ref.name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        {
            const TcSymbol *symbol = tc_symbol_table_find(visible, rhs->u.const_ref.name);
            char msg[128];
            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.const_ref.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            if (!symbol->has_const_value) {
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "constant value is not available by source order");
                return -1;
            }
            if (tc_type_tag_of(symbol->type) != expected_type) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "constant type does not match expected type");
                return -1;
            }
            *out = symbol->const_value;
            return 0;
        }
    }

    if (rhs->kind == TC_RHS_ARITH) {
        if (tc_validate_arith_mode(rhs->u.arith.op, rhs->u.arith.type->tag,
                                   rhs->u.arith.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.arith.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.lhs, rhs->u.arith.type->tag, visible, global,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.rhs, rhs->u.arith.type->tag, visible, global,
                                  const_name, &rhs_val, line, diag) !=
            0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_arith(rhs->u.arith.op, rhs->u.arith.type->tag, rhs->u.arith.mode, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        if (tc_validate_unary_mode(rhs->u.unary.op, rhs->u.unary.type->tag,
                                   rhs->u.unary.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.unary.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.unary.operand, rhs->u.unary.type->tag, visible, global,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_unary(rhs->u.unary.op, rhs->u.unary.type->tag, rhs->u.unary.mode, &lhs, out,
                          &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.compare.lhs, rhs->u.compare.type->tag, visible, global,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.compare.rhs, rhs->u.compare.type->tag, visible, global,
                                  const_name, &rhs_val, line, diag) !=
            0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type->tag, &lhs, &rhs_val, out, &tmp_diag,
                            line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_bin.lhs, TC_BOOL, visible, global, const_name,
                                  &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, const_name,
                                  &rhs_val, line, diag) != 0) {
            return -1;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_AND && lhs.bits == 0) {
            *out = tc_value_make(TC_BOOL, 0);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs.bits != 0) {
            *out = tc_value_make(TC_BOOL, 1);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_XOR) {
            /* xor 不短路：两侧均需求值 */
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_un.operand, TC_BOOL, visible, global, const_name,
                                  &lhs, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_logic_unary(rhs->u.logic_un.op, &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        if (rhs->u.bitwise_bin.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type->tag, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type->tag, visible,
                                  global, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_bitwise_binary(rhs->u.bitwise_bin.op, rhs->u.bitwise_bin.type->tag, &lhs,
                                   &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        if (rhs->u.bitwise_un.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type->tag, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_bitwise_unary(rhs->u.bitwise_un.type->tag, &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        if (tc_validate_shift_mode(rhs->u.shift.op, rhs->u.shift.type->tag,
                                   rhs->u.shift.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.shift.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.value, rhs->u.shift.type->tag, visible, global,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.count, rhs->u.shift.type->tag, visible, global,
                                  const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_shift(rhs->u.shift.op, rhs->u.shift.type->tag, rhs->u.shift.mode, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        if (tc_validate_fp_arith_mode(rhs->u.float_arith.op, rhs->u.float_arith.type->tag,
                                      rhs->u.float_arith.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.float_arith.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.lhs, rhs->u.float_arith.type->tag, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.rhs, rhs->u.float_arith.type->tag, visible,
                                  global, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_arith(rhs->u.float_arith.op, rhs->u.float_arith.type->tag,
                             rhs->u.float_arith.mode,
                             &lhs, &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        if (tc_validate_fp_unary_mode(rhs->u.float_unary.op, rhs->u.float_unary.type->tag,
                                      rhs->u.float_unary.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.float_unary.type->tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_unary.operand, rhs->u.float_unary.type->tag, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_unary(rhs->u.float_unary.op, rhs->u.float_unary.type->tag,
                             rhs->u.float_unary.mode,
                             &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        if (tc_validate_fp_compare_mode(rhs->u.float_compare.type->tag,
                                        rhs->u.float_compare.mode, diag, line) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.lhs, rhs->u.float_compare.type->tag, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.rhs, rhs->u.float_compare.type->tag, visible,
                                  global, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_compare(rhs->u.float_compare.op, rhs->u.float_compare.type->tag,
                               rhs->u.float_compare.mode, &lhs, &rhs_val, out, &tmp_diag,
                               line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITCAST) {
        TcBitcastRhs *bitcast = (TcBitcastRhs *)&rhs->u.bitcast;
        TcValue source = {0};
        TcTypeTag source_type = TC_INT32;
        int width = tc_type_bit_width(bitcast->target.tag);

        if (bitcast->target.tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant bitcast type mismatch");
            return -1;
        }
        if (bitcast->source.kind == TC_OPERAND_VAR) {
            const TcSymbol *symbol = tc_symbol_table_find(visible, bitcast->source.u.name);
            char msg[128];

            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               bitcast->source.u.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line,
                                  TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            source_type = tc_type_tag_of(symbol->type);
        } else if (bitcast->source.kind == TC_OPERAND_FIELD_READ) {
            if (!bitcast->source.u.field_read.resolved.resolved ||
                !bitcast->source.u.field_read.resolved.field_type) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "invalid constant bitcast source");
                return -1;
            }
            source_type = tc_type_tag_of(bitcast->source.u.field_read.resolved.field_type);
        } else if (bitcast->source.u.lit.is_bool) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        } else if (bitcast->source.u.lit.is_float) {
            if (!isfinite(bitcast->source.u.lit.float_value) &&
                !bitcast->source.u.lit.float32_suffix) {
                source_type = width == 32 ? TC_FLOAT32 : TC_FLOAT64;
            } else {
                source_type = bitcast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
            }
        } else if (width == 64) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
        } else if (width == 32) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT32 : TC_INT32;
        } else if (width == 16) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT16 : TC_INT16;
        } else {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT8 : TC_INT8;
        }
        if (tc_type_is_bool(bitcast->target.tag) || tc_type_is_bool(source_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        }
        if (tc_type_bit_width(source_type) != width) {
            tc_diagnostic_set(diag, TC_CE_BITCAST_WIDTH, line, TC_COLUMN_UNKNOWN,
                              "bitcast source and target widths must match");
            return -1;
        }
        bitcast->source_type = tc_type_tag_singleton(source_type);
        bitcast->source_type_resolved = 1;
        if (tc_eval_const_operand(&bitcast->source, source_type, visible,
                                  global, const_name, &source, line,
                                  diag) != 0) {
            return -1;
        }
        return tc_exec_bitcast(bitcast->target.tag, &source, out, diag, line);
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        TcCastRhs *cast = (TcCastRhs *)&rhs->u.const_cast;
        TcValue src_val = {0};
        TcTypeTag source_type = TC_INT64;

        if (cast->target.tag != expected_type) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant cast target type mismatch");
            return -1;
        }
        if (cast->target.tag == TC_STRUCT || cast->target.tag == TC_MEMBLOCK ||
            cast->target.tag == TC_VOID) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast target must be scalar or ptr type");
            return -1;
        }
        if (cast->source.kind == TC_OPERAND_LIT && cast->source.u.lit.is_nullptr) {
            if (cast->target.tag != TC_PTR) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "pointer cast requires a pointer source");
                return -1;
            }
            *out = tc_value_make(TC_PTR, 0);
            return 0;
        }
        if (cast->target.tag == TC_PTR) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer cast requires a pointer source");
            return -1;
        }
        if (cast->source.kind == TC_OPERAND_LIT) {
            if (cast->source.u.lit.is_bool) {
                source_type = TC_BOOL;
            } else if (cast->source.u.lit.is_float) {
                source_type = cast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
            } else {
                source_type = cast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
            }
            if (!tc_literal_fits_context(&cast->source.u.lit, source_type, NULL)) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "invalid literal in constant cast");
                return -1;
            }
            src_val = tc_literal_to_value(&cast->source.u.lit, source_type);
        } else if (cast->source.kind == TC_OPERAND_VAR) {
            const TcSymbol *symbol =
                tc_symbol_table_find(visible, cast->source.u.name);
            char msg[128];

            if (const_name && strcmp(cast->source.u.name, const_name) == 0) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               cast->source.u.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               cast->source.u.name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            if (!symbol->has_const_value) {
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "constant value is not available by source order");
                return -1;
            }
            src_val = symbol->const_value;
            source_type = tc_type_tag_of(symbol->type);
        } else if (cast->source.kind == TC_OPERAND_FIELD_READ) {
            if (!cast->source.u.field_read.resolved.resolved ||
                !cast->source.u.field_read.resolved.field_type) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "invalid constant cast source");
                return -1;
            }
            source_type = tc_type_tag_of(cast->source.u.field_read.resolved.field_type);
            if (tc_eval_const_operand(&cast->source, source_type, visible, global, const_name,
                                      &src_val, line, diag) != 0) {
                return -1;
            }
        } else {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid constant cast source");
            return -1;
        }
        cast->source_type = tc_type_tag_singleton(source_type);
        cast->source_type_resolved = 1;
        tc_diagnostic_init(&tmp_diag);
        if ((cast->mode == TC_TRUNC_TRUNCATE
                 ? tc_exec_truncate(cast->target.tag, &src_val, out, &tmp_diag, line)
                 : tc_exec_cast(cast->target.tag, &src_val, out, &tmp_diag, line)) != 0) {
            if (tmp_diag.kind == TC_CE_MODE_MISMATCH) {
                tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "truncate requires an integer target narrower than the source");
            } else {
                tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            }
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_COUNT) {
        const TcResolvedBinding *binding = &rhs->u.memblock_count.binding;

        if (!binding->resolved || !binding->is_const) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        if (expected_type != TC_USIZE && expected_type != TC_ISIZE) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant type does not match expected type");
            return -1;
        }
        *out = tc_value_make(TC_USIZE, tc_type_memblock_count(binding->type));
        return 0;
    }

    if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        if (expected_type != TC_STRUCT) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "struct constructor type mismatch in constant expression");
            return -1;
        }
        return tc_eval_const_struct_ctor(rhs, struct_table, visible, global, const_name, out, line,
                                         diag);
    }

    if (rhs->kind == TC_RHS_PTR_SIZE) {
        size_t bits = 0;

        if (expected_type != TC_USIZE && expected_type != TC_ISIZE) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        bits = tc_sizeof_bits_ex(&rhs->u.ptr_size.pointee_type, tc_struct_table_width_bits,
                                 struct_table);
        *out = tc_value_make(TC_USIZE, (uint64_t)bits);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_CONSTRUCTOR) {
        if (expected_type != TC_MEMBLOCK) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor type mismatch in constant expression");
            return -1;
        }
        return tc_eval_const_memblock_ctor(rhs, struct_table, visible, global, const_name, out, line,
                                           diag);
    }

    tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                      "invalid constant expression");
    return -1;
}

/* ------------------------------------------------------------------ */
/*  公开接口 — 编译期求值 let RHS                                        */
/* ------------------------------------------------------------------ */

int tc_resolve_const_value(TcSymbol *sym, const TcRhs *rhs, const TcSymbolTable *visible,
                           const TcSymbolTable *global, const TcStructTable *struct_table,
                           int line, TcDiagnostic *diag) {
    TcValue value = {0};

    if (tc_eval_const_rhs(rhs, tc_type_tag_of(sym->type), visible, global, struct_table, sym->name,
                          &value, line, diag) != 0) {
        return -1;
    }
    {
        int aliased = tc_const_heap_named(value.bits, visible) ||
                      tc_const_heap_named(value.bits, global);

        /* 先判断是否别名，再写入本符号，避免 named 扫到自己 */
        sym->const_value = value;
        sym->has_const_value = 1;
        sym->owns_const_heap = 0;
        if (value.type && (value.type->tag == TC_STRUCT || value.type->tag == TC_MEMBLOCK) &&
            value.bits != 0 && !aliased) {
            sym->owns_const_heap = 1;
        }
    }
    return 0;
}
