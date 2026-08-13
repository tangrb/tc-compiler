/*
 * tc_memblock_exec.c — memblock 堆布局与运行时运算
 *
 * 布局：[uint64_t count][element bits...]
 */
#include "tc_memblock_exec.h"

#include "tc_semantics.h"
#include "tc_struct_check.h"
#include "tc_symbol.h"

#include <stdlib.h>
#include <string.h>

static int tc_exec_memblock_track(TcExecuteCtx *ctx, void *block, TcDiagnostic *diag) {
    void **items = NULL;

    if (!ctx || !block) {
        return -1;
    }
    if (ctx->memblock_heap_count == ctx->memblock_heap_capacity) {
        size_t new_cap = ctx->memblock_heap_capacity == 0 ? 8 : ctx->memblock_heap_capacity * 2;
        items = (void **)realloc(ctx->memblock_heap, new_cap * sizeof(void *));
        if (!items) {
            free(block);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        ctx->memblock_heap = items;
        ctx->memblock_heap_capacity = new_cap;
    }
    ctx->memblock_heap[ctx->memblock_heap_count++] = block;
    return 0;
}

static void *tc_memblock_data(const TcValue *mb_value) {
    if (!mb_value || mb_value->type->tag != TC_MEMBLOCK || mb_value->bits == 0) {
        return NULL;
    }
    return (void *)(uintptr_t)mb_value->bits;
}

static uint64_t tc_memblock_declared_count(const TcSymbol *sym) {
    if (!sym) {
        return 0;
    }
    return tc_type_memblock_count(sym->type);
}

static size_t tc_memblock_element_bytes(const TcType *element, const TcExecuteCtx *ctx) {
    size_t bits = 0;

    if (!element) {
        return 0;
    }
    bits = tc_sizeof_bits_ex(element, tc_struct_table_width_bits, ctx->program->struct_table);
    return (bits + 7U) / 8U;
}

static int tc_memblock_read_index(const TcOperand *index_op, TcExecuteCtx *ctx, uint64_t *out,
                                  TcDiagnostic *diag, int line) {
    TcValue index_value;

    if (tc_eval_operand(index_op, TC_USIZE, ctx, &index_value, diag, line) != 0 &&
        tc_eval_operand(index_op, TC_ISIZE, ctx, &index_value, diag, line) != 0) {
        return -1;
    }
    if (index_value.type->tag == TC_ISIZE && (int64_t)tc_bits_to_signed(TC_ISIZE, index_value.bits) < 0) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    *out = index_value.bits;
    return 0;
}

static int tc_memblock_bounds_check(uint64_t index, uint64_t count, int line,
                                    TcDiagnostic *diag) {
    if (index >= count) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    return 0;
}

static uint8_t *tc_memblock_element_ptr(void *block, size_t element_bytes, uint64_t index) {
    return (uint8_t *)block + sizeof(uint64_t) + index * element_bytes;
}

int tc_exec_memblock_ctor(const TcRhs *rhs, const TcType *expected, TcExecuteCtx *ctx,
                          TcValue *out, TcDiagnostic *diag, int line) {
    uint64_t count = 0;
    size_t element_bytes = 0;
    size_t payload_bytes = 0;
    void *block = NULL;
    uint8_t *cursor = NULL;
    size_t i = 0;

    if (!rhs || rhs->kind != TC_RHS_MEMBLOCK_CONSTRUCTOR || !expected || !ctx || !out || !diag) {
        return -1;
    }
    count = rhs->u.memblock_ctor.count;
    element_bytes = tc_memblock_element_bytes(expected->params.memblock_type.element, ctx);
    payload_bytes = (size_t)count * element_bytes;
    block = malloc(sizeof(uint64_t) + payload_bytes);
    if (!block) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    memcpy(block, &count, sizeof(uint64_t));
    cursor = (uint8_t *)block + sizeof(uint64_t);
    if (rhs->u.memblock_ctor.is_fill) {
        TcValue fill_value;
        if (tc_eval_operand(&rhs->u.memblock_ctor.fill_value,
                            expected->params.memblock_type.element->tag, ctx, &fill_value, diag,
                            line) != 0) {
            free(block);
            return -1;
        }
        if (expected->params.memblock_type.element->tag == TC_BOOL) {
            fill_value.bits = fill_value.bits ? 1ULL : 0ULL;
        }
        for (i = 0; i < count; i++) {
            memcpy(cursor + i * element_bytes, &fill_value.bits, element_bytes);
        }
    } else {
        for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
            TcValue elem;
            if (tc_eval_operand(&rhs->u.memblock_ctor.values[i],
                                expected->params.memblock_type.element->tag, ctx, &elem, diag,
                                line) != 0) {
                free(block);
                return -1;
            }
            if (expected->params.memblock_type.element->tag == TC_BOOL) {
                elem.bits = elem.bits ? 1ULL : 0ULL;
            }
            memcpy(cursor + i * element_bytes, &elem.bits, element_bytes);
        }
    }
    if (tc_exec_memblock_track(ctx, block, diag) != 0) {
        return -1;
    }
    out->type = tc_type_tag_singleton(TC_MEMBLOCK);
    out->bits = (uint64_t)(uintptr_t)block;
    return 0;
}

int tc_exec_memblock_load(const TcType *element, const TcOperand *mb_op, const TcOperand *index_op,
                          TcExecuteCtx *ctx, TcValue *out, TcDiagnostic *diag, int line) {
    TcValue mb_value;
    void *block = NULL;
    uint64_t index = 0;
    uint64_t count = 0;
    size_t element_bytes = 0;
    const uint8_t *src = NULL;

    if (tc_eval_operand(mb_op, TC_MEMBLOCK, ctx, &mb_value, diag, line) != 0) {
        return -1;
    }
    block = tc_memblock_data(&mb_value);
    if (!block) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid memblock value");
        return -1;
    }
    memcpy(&count, block, sizeof(uint64_t));
    if (tc_memblock_read_index(index_op, ctx, &index, diag, line) != 0) {
        return -1;
    }
    if (tc_memblock_bounds_check(index, count, line, diag) != 0) {
        return -1;
    }
    element_bytes = tc_memblock_element_bytes(element, ctx);
    src = tc_memblock_element_ptr(block, element_bytes, index);
    out->type = element;
    memcpy(&out->bits, src, element_bytes);
    return 0;
}

int tc_exec_memblock_count(const char *memblock_name, TcExecuteCtx *ctx, TcValue *out,
                           TcDiagnostic *diag, int line) {
    const TcSymbol *sym = NULL;
    void *block = NULL;
    uint64_t count = 0;

    sym = tc_exec_find_symbol(ctx->symbols, memblock_name);
    if (!sym || sym->slot < 0 || tc_type_tag_of(sym->type) != TC_MEMBLOCK) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved memblock for count");
        return -1;
    }
    if (ctx->slots[sym->slot].bits != 0) {
        block = tc_memblock_data(&ctx->slots[sym->slot]);
        if (block) {
            memcpy(&count, block, sizeof(uint64_t));
        }
    }
    if (count == 0) {
        count = tc_memblock_declared_count(sym);
    }
    out->type = tc_type_tag_singleton(TC_USIZE);
    out->bits = count;
    return 0;
}

int tc_exec_memblock_store_stmt(const TcMemblockStoreStmt *stmt, TcExecuteCtx *ctx,
                                TcDiagnostic *diag) {
    const TcSymbol *sym = NULL;
    TcValue mb_value;
    void *block = NULL;
    uint64_t index = 0;
    uint64_t count = 0;
    size_t element_bytes = 0;
    TcValue value;
    uint8_t *dst = NULL;

    sym = tc_exec_find_symbol(ctx->symbols, stmt->memblock_name);
    if (!sym || sym->slot < 0) {
        tc_exec_set_internal_error(diag, stmt->line,
                                   "internal error: unresolved memblock store target");
        return -1;
    }
    mb_value = ctx->slots[sym->slot];
    block = tc_memblock_data(&mb_value);
    if (!block) {
        tc_exec_set_internal_error(diag, stmt->line, "internal error: invalid memblock value");
        return -1;
    }
    memcpy(&count, block, sizeof(uint64_t));
    if (tc_memblock_read_index(&stmt->index, ctx, &index, diag, stmt->line) != 0) {
        return -1;
    }
    if (tc_memblock_bounds_check(index, count, stmt->line, diag) != 0) {
        return -1;
    }
    if (tc_eval_operand(&stmt->value, stmt->element_type.tag, ctx, &value, diag, stmt->line) !=
        0) {
        return -1;
    }
    if (stmt->element_type.tag == TC_BOOL) {
        value.bits = value.bits ? 1ULL : 0ULL;
        value.type = tc_type_tag_singleton(TC_BOOL);
    }
    element_bytes =
        tc_memblock_element_bytes(sym->type->params.memblock_type.element, ctx);
    dst = tc_memblock_element_ptr(block, element_bytes, index);
    memcpy(dst, &value.bits, element_bytes);
    return 0;
}

int tc_exec_memblock_copy_stmt(const TcMemblockCopyStmt *stmt, TcExecuteCtx *ctx,
                               TcDiagnostic *diag) {
    const TcSymbol *dst_sym = NULL;
    const TcSymbol *src_sym = NULL;
    TcValue dst_mb;
    TcValue src_mb;
    void *dst_block = NULL;
    void *src_block = NULL;
    uint64_t dst_index = 0;
    uint64_t src_index = 0;
    uint64_t length = 0;
    uint64_t dst_count = 0;
    uint64_t src_count = 0;
    size_t element_bytes = 0;
    TcType element_type;

    dst_sym = tc_exec_find_symbol(ctx->symbols, stmt->dst_name);
    src_sym = tc_exec_find_symbol(ctx->symbols, stmt->src_name);
    if (!dst_sym || !src_sym || dst_sym->slot < 0 || src_sym->slot < 0) {
        tc_exec_set_internal_error(diag, stmt->line, "internal error: unresolved memblock copy");
        return -1;
    }
    dst_mb = ctx->slots[dst_sym->slot];
    src_mb = ctx->slots[src_sym->slot];
    dst_block = tc_memblock_data(&dst_mb);
    src_block = tc_memblock_data(&src_mb);
    if (!dst_block || !src_block) {
        tc_exec_set_internal_error(diag, stmt->line, "internal error: invalid memblock value");
        return -1;
    }
    element_type = tc_type_scalar(stmt->element_type.tag);
    element_bytes = tc_memblock_element_bytes(&element_type, ctx);
    memcpy(&dst_count, dst_block, sizeof(uint64_t));
    memcpy(&src_count, src_block, sizeof(uint64_t));
    if (tc_memblock_read_index(&stmt->dst_index, ctx, &dst_index, diag, stmt->line) != 0 ||
        tc_memblock_read_index(&stmt->src_index, ctx, &src_index, diag, stmt->line) != 0 ||
        tc_memblock_read_index(&stmt->length, ctx, &length, diag, stmt->line) != 0) {
        return -1;
    }
    if (length > 0 && (dst_index + length > dst_count || src_index + length > src_count)) {
        tc_diagnostic_set(diag, TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE, stmt->line,
                          TC_COLUMN_UNKNOWN, "memblock index out of range");
        return -1;
    }
    if (length > 0) {
        size_t nbytes = (size_t)length * element_bytes;
        void *temp = malloc(nbytes);

        if (!temp) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, stmt->line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        memcpy(temp, tc_memblock_element_ptr(src_block, element_bytes, src_index), nbytes);
        memcpy(tc_memblock_element_ptr(dst_block, element_bytes, dst_index), temp, nbytes);
        free(temp);
    }
    return 0;
}

int tc_exec_memcopy_unsafe_stmt(const TcMemcopyUnsafeStmt *stmt, TcExecuteCtx *ctx,
                                TcDiagnostic *diag) {
    TcValue dst_ptr;
    TcValue src_ptr;
    int dst_slot = 0;
    int src_slot = 0;
    uint64_t dst_index = 0;
    uint64_t src_index = 0;
    int64_t length_signed = 0;
    uint64_t length = 0;
    size_t element_bytes = 0;
    TcType element_type;
    void *temp = NULL;

    if (tc_eval_operand(&stmt->dst_ptr, TC_PTR, ctx, &dst_ptr, diag, stmt->line) != 0 ||
        tc_eval_operand(&stmt->src_ptr, TC_PTR, ctx, &src_ptr, diag, stmt->line) != 0) {
        return -1;
    }
    if (dst_ptr.bits == 0 || src_ptr.bits == 0) {
        tc_diagnostic_set(diag, TC_RE_NULL_POINTER_DEREFERENCE, stmt->line, TC_COLUMN_UNKNOWN,
                          "null pointer dereference");
        return -1;
    }
    if ((dst_ptr.bits & 1ULL) == 0 || (src_ptr.bits & 1ULL) == 0) {
        tc_exec_set_internal_error(diag, stmt->line, "internal error: invalid pointer value");
        return -1;
    }
    dst_slot = (int)(dst_ptr.bits >> 1);
    src_slot = (int)(src_ptr.bits >> 1);
    if (tc_memblock_read_index(&stmt->dst_index, ctx, &dst_index, diag, stmt->line) != 0 ||
        tc_memblock_read_index(&stmt->src_index, ctx, &src_index, diag, stmt->line) != 0) {
        return -1;
    }
    {
        TcValue length_value;
        if (tc_eval_operand(&stmt->length, TC_USIZE, ctx, &length_value, diag, stmt->line) != 0 &&
            tc_eval_operand(&stmt->length, TC_ISIZE, ctx, &length_value, diag, stmt->line) != 0) {
            return -1;
        }
        if (length_value.type->tag == TC_ISIZE) {
            length_signed = tc_bits_to_signed(TC_ISIZE, length_value.bits);
        } else {
            length_signed = (int64_t)length_value.bits;
        }
    }
    if (length_signed < 0) {
        tc_diagnostic_set(diag, TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE, stmt->line,
                          TC_COLUMN_UNKNOWN, "memcopy_unsafe invalid range");
        return -1;
    }
    length = (uint64_t)length_signed;
    element_type = tc_type_scalar(stmt->element_type.tag);
    element_bytes = tc_memblock_element_bytes(&element_type, ctx);
    if (length == 0) {
        return 0;
    }
    temp = malloc((size_t)length * element_bytes);
    if (!temp) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, stmt->line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    {
        void *dst_block = tc_memblock_data(&ctx->slots[dst_slot]);
        void *src_block = tc_memblock_data(&ctx->slots[src_slot]);
        if (!dst_block || !src_block) {
            free(temp);
            tc_exec_set_internal_error(diag, stmt->line, "internal error: memcopy_unsafe target");
            return -1;
        }
        memcpy(temp, tc_memblock_element_ptr(src_block, element_bytes, src_index),
               (size_t)length * element_bytes);
        memcpy(tc_memblock_element_ptr(dst_block, element_bytes, dst_index), temp,
               (size_t)length * element_bytes);
    }
    free(temp);
    return 0;
}

void tc_exec_memblock_heap_free(TcExecuteCtx *ctx) {
    size_t i = 0;

    if (!ctx) {
        return;
    }
    for (i = 0; i < ctx->memblock_heap_count; i++) {
        free(ctx->memblock_heap[i]);
    }
    free(ctx->memblock_heap);
    ctx->memblock_heap = NULL;
    ctx->memblock_heap_count = 0;
    ctx->memblock_heap_capacity = 0;
}
