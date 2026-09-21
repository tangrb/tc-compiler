/*
 * tc_const_aggregate.c — 常量 struct/memblock 构造与堆所有权
 */
#include "tc_const_aggregate.h"

#include "tc_struct_check.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 端序契约（§3.5）：const 复合值头部/标量字段/元素按固定 LE（低字节在前）
 * 序列化，与 VM/AOT 运行时的 tc_mb_/tc_st_ 系列一致，不依赖宿主字节序。 */
static void tc_ce_store_bits(uint8_t *dst, size_t nbytes, uint64_t bits) {
    size_t i = 0;

    for (i = 0; i < nbytes; i++) {
        dst[i] = (uint8_t)(bits >> (8U * i));
    }
}

int tc_const_heap_named(uint64_t bits, const TcSymbolTable *table) {
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

void tc_const_drop_temp_heap(TcValue *value, const TcSymbolTable *visible,
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
        tc_ce_store_bits(dst, nbytes, bits);
    }
    return 0;
}

static int tc_eval_const_ctor_field(const TcRhs *rhs_field, int has_rhs, const TcOperand *value_op,
                                    const TcType *field_type, const TcSymbolTable *visible,
                                    const TcSymbolTable *global, const TcStructTable *struct_table,
                                    const char *const_name, const TcMemberIndex *members,
                                    TcValue *out, int line, TcDiagnostic *diag) {
    if (has_rhs && rhs_field) {
        return tc_eval_const_rhs(rhs_field, field_type->tag, visible, global, struct_table,
                                 const_name, members, out, line, diag);
    }
    return tc_eval_const_operand(value_op, field_type->tag, visible, global, members, const_name,
                                 out, line, diag);
}

int tc_eval_const_struct_ctor(const TcRhs *rhs, const TcStructTable *table,
                                     const TcSymbolTable *visible, const TcSymbolTable *global,
                                     const char *const_name, const TcMemberIndex *members,
                                     TcValue *out, int line, TcDiagnostic *diag) {
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
                        table, const_name, members, &field_value, line, diag) != 0) {
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

int tc_eval_const_memblock_ctor(const TcRhs *rhs, const TcStructTable *struct_table,
                                       const TcSymbolTable *visible, const TcSymbolTable *global,
                                       const char *const_name, const TcMemberIndex *members,
                                       TcValue *out, int line, TcDiagnostic *diag) {
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
    tc_ce_store_bits(block, sizeof(uint64_t), count);
    cursor = (uint8_t *)block + sizeof(uint64_t);
    if (rhs->u.memblock_ctor.is_fill) {
        TcValue fill_value = {0};

        if (tc_eval_const_operand(&rhs->u.memblock_ctor.fill_value, elem->tag, visible, global, members,
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
                tc_ce_store_bits(cursor + i * element_bytes, element_bytes, fill_value.bits);
            }
        }
        tc_const_drop_temp_heap(&fill_value, visible, global);
    } else {
        for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
            TcValue elem_val = {0};

            if (tc_eval_const_operand(&rhs->u.memblock_ctor.values[i], elem->tag, visible, global, members,
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
                tc_ce_store_bits(cursor + i * element_bytes, element_bytes, elem_val.bits);
            }
            tc_const_drop_temp_heap(&elem_val, visible, global);
        }
    }
    out->type = tc_type_tag_singleton(TC_MEMBLOCK);
    out->bits = (uint64_t)(uintptr_t)block;
    return 0;
}
