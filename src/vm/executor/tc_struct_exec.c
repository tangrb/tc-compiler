/*
 * tc_struct_exec.c — struct 堆布局与运行时运算
 *
 * 布局：按声明源序紧密排列字段字节，其后跟 @padding(N) 个 0x00 字节。
 * TcValue.bits 存堆块指针（与 memblock 一致）。
 *
 * 字节约定（A1 端序契约，§3.5）：标量字段字节区间内按固定 LE（低字节在前）
 * 序列化，显式位组装读写，不依赖宿主字节序；结构体/memblock 字段为不透明
 * 字节块，整块 memcpy。
 */
#include "tc_struct_exec.h"

#include "tc_memblock_exec.h"
#include "tc_symbol.h"

#include <stdlib.h>
#include <string.h>

/** 抽象位串 → LE 字节（dst 长 nbytes，低字节在前；nbytes ≤ 8）。 */
static void tc_st_store_bits(uint8_t *dst, size_t nbytes, uint64_t bits) {
    size_t i = 0;

    for (i = 0; i < nbytes; i++) {
        dst[i] = (uint8_t)(bits >> (8U * i));
    }
}

/** LE 字节 → 抽象位串（src 长 nbytes；nbytes ≤ 8）。 */
static uint64_t tc_st_load_bits(const uint8_t *src, size_t nbytes) {
    uint64_t bits = 0;
    size_t i = 0;

    for (i = 0; i < nbytes && i < sizeof(bits); i++) {
        bits |= ((uint64_t)src[i]) << (8U * i);
    }
    return bits;
}

const TcStructTable *tc_exec_struct_table(const TcExecuteCtx *ctx) {
    if (!ctx || !ctx->program) {
        return NULL;
    }
    return ctx->program->struct_table;
}

static int tc_exec_struct_track(TcExecuteCtx *ctx, void *block, TcDiagnostic *diag, int line) {
    void **items = NULL;

    if (!ctx || !block) {
        return -1;
    }
    if (ctx->struct_heap_count == ctx->struct_heap_capacity) {
        size_t new_cap = ctx->struct_heap_capacity == 0 ? 8 : ctx->struct_heap_capacity * 2;
        items = (void **)realloc(ctx->struct_heap, new_cap * sizeof(void *));
        if (!items) {
            free(block);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        ctx->struct_heap = items;
        ctx->struct_heap_capacity = new_cap;
    }
    ctx->struct_heap[ctx->struct_heap_count++] = block;
    return 0;
}

static void *tc_struct_data(const TcValue *value) {
    if (!value || !value->type || value->type->tag != TC_STRUCT || value->bits == 0) {
        return NULL;
    }
    return (void *)(uintptr_t)value->bits;
}

static size_t tc_struct_bytes_of(const TcStructEntry *entry) {
    if (!entry) {
        return 0;
    }
    return (entry->width_bits + 7U) / 8U;
}

static size_t tc_type_payload_bytes(const TcType *type, const TcStructTable *table) {
    if (!type) {
        return 0;
    }
    if (type->tag == TC_STRUCT) {
        const TcStructEntry *e = tc_struct_table_get(table, type->params.struct_type.struct_id);
        return tc_struct_bytes_of(e);
    }
    /* memblock 元素 / ptr 所指可为结构体，须经宽度表回调（§3.9.3） */
    return (tc_sizeof_bits_ex(type, tc_struct_table_width_bits, (void *)table) + 7U) / 8U;
}

int tc_exec_struct_alloc(size_t bytes, TcExecuteCtx *ctx, void **out_block,
                         TcDiagnostic *diag, int line) {
    void *block = NULL;

    if (bytes == 0) {
        tc_exec_set_internal_error(diag, line, "internal error: zero-sized struct");
        return -1;
    }
    block = calloc(1, bytes);
    if (!block) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    if (tc_exec_struct_track(ctx, block, diag, line) != 0) {
        return -1;
    }
    *out_block = block;
    return 0;
}

int tc_exec_struct_clone(const TcValue *src, size_t width_bits, TcExecuteCtx *ctx, TcValue *out,
                         TcDiagnostic *diag, int line) {
    void *src_data = NULL;
    void *block = NULL;
    size_t bytes = (width_bits + 7U) / 8U;

    src_data = tc_struct_data(src);
    if (!src_data || bytes == 0) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid struct value");
        return -1;
    }
    if (tc_exec_struct_alloc(bytes, ctx, &block, diag, line) != 0) {
        return -1;
    }
    memcpy(block, src_data, bytes);
    out->type = tc_type_tag_singleton(TC_STRUCT);
    out->bits = (uint64_t)(uintptr_t)block;
    return 0;
}

int tc_exec_struct_store_value(TcValue *dst_slot, const TcValue *src, int struct_id,
                               TcExecuteCtx *ctx, TcDiagnostic *diag, int line) {
    const TcStructTable *table = tc_exec_struct_table(ctx);
    const TcStructEntry *entry = NULL;
    TcValue cloned;

    entry = tc_struct_table_get(table, struct_id);
    if (!entry) {
        tc_exec_set_internal_error(diag, line, "internal error: unknown struct id");
        return -1;
    }
    if (tc_exec_struct_clone(src, entry->width_bits, ctx, &cloned, diag, line) != 0) {
        return -1;
    }
    *dst_slot = cloned;
    return 0;
}

static int tc_exec_write_field_bytes(uint8_t *base, size_t offset, const TcType *field_type,
                                     const TcValue *value, const TcStructTable *table,
                                     TcDiagnostic *diag, int line) {
    size_t nbytes = tc_type_payload_bytes(field_type, table);
    uint8_t *dst = base + offset;

    if (field_type->tag == TC_STRUCT) {
        void *src = tc_struct_data(value);
        if (!src || nbytes == 0) {
            tc_exec_set_internal_error(diag, line, "internal error: nested struct field value");
            return -1;
        }
        memcpy(dst, src, nbytes);
        return 0;
    }
    if (field_type->tag == TC_MEMBLOCK) {
        void *src = NULL;
        if (!value || !value->type || value->type->tag != TC_MEMBLOCK || value->bits == 0) {
            tc_exec_set_internal_error(diag, line, "internal error: memblock field value");
            return -1;
        }
        src = (void *)(uintptr_t)value->bits;
        memcpy(dst, src, nbytes);
        return 0;
    }
    {
        uint64_t bits = value->bits;
        if (field_type->tag == TC_BOOL) {
            bits = bits ? 1ULL : 0ULL;
        }
        tc_st_store_bits(dst, nbytes, bits);
    }
    return 0;
}

static int tc_exec_read_field_bytes(const uint8_t *base, size_t offset, const TcType *field_type,
                                    const TcStructTable *table, TcExecuteCtx *ctx, TcValue *out,
                                    TcDiagnostic *diag, int line) {
    size_t nbytes = tc_type_payload_bytes(field_type, table);
    const uint8_t *src = base + offset;

    if (field_type->tag == TC_STRUCT) {
        void *block = NULL;
        if (tc_exec_struct_alloc(nbytes, ctx, &block, diag, line) != 0) {
            return -1;
        }
        memcpy(block, src, nbytes);
        out->type = tc_type_tag_singleton(TC_STRUCT);
        out->bits = (uint64_t)(uintptr_t)block;
        return 0;
    }
    if (field_type->tag == TC_MEMBLOCK) {
        void *block = malloc(nbytes);
        if (!block) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        memcpy(block, src, nbytes);
        /* 复用 memblock 堆跟踪：写入后由调用方走 memblock 路径；此处挂到 struct 堆亦可 */
        if (tc_exec_struct_track(ctx, block, diag, line) != 0) {
            return -1;
        }
        out->type = tc_type_tag_singleton(TC_MEMBLOCK);
        out->bits = (uint64_t)(uintptr_t)block;
        return 0;
    }
    out->type = field_type;
    out->bits = tc_st_load_bits(src, nbytes);
    if (field_type->tag == TC_BOOL) {
        out->bits = out->bits ? 1ULL : 0ULL;
    }
    return 0;
}

static int tc_exec_eval_ctor_field(const TcRhs *rhs_field_holder, int has_rhs,
                                   const TcOperand *value_op, const TcType *field_type,
                                   TcExecuteCtx *ctx, TcValue *out, TcDiagnostic *diag, int line) {
    if (has_rhs && rhs_field_holder) {
        return tc_eval_rhs(rhs_field_holder, field_type->tag, ctx, out, diag, line);
    }
    return tc_eval_operand(value_op, field_type->tag, ctx, out, diag, line);
}

int tc_exec_struct_ctor(const TcRhs *rhs, TcExecuteCtx *ctx, TcValue *out, TcDiagnostic *diag,
                        int line) {
    const TcStructTable *table = tc_exec_struct_table(ctx);
    const TcStructEntry *entry = NULL;
    void *block = NULL;
    size_t bit_off = 0;
    size_t i = 0;

    if (!rhs || rhs->kind != TC_RHS_STRUCT_CONSTRUCTOR || !ctx || !out || !diag) {
        return -1;
    }
    entry = tc_struct_table_find(table, rhs->u.struct_ctor.struct_name);
    if (!entry) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved struct constructor");
        return -1;
    }
    if (tc_exec_struct_alloc(tc_struct_bytes_of(entry), ctx, &block, diag, line) != 0) {
        return -1;
    }

    for (i = 0; i < entry->field_count; i++) {
        const TcStructField *field = &entry->fields[i];
        size_t field_bits = 0;
        size_t offset_bytes = bit_off / 8U;
        TcValue field_value;
        size_t fi = 0;
        int found = 0;

        for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
            if (strcmp(rhs->u.struct_ctor.fields[fi].param_name, field->name) == 0) {
                if (tc_exec_eval_ctor_field((const TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs,
                                            rhs->u.struct_ctor.fields[fi].has_rhs,
                                            &rhs->u.struct_ctor.fields[fi].value_op, &field->type,
                                            ctx, &field_value, diag, line) != 0) {
                    return -1;
                }
                found = 1;
                break;
            }
        }
        if (!found) {
            tc_exec_set_internal_error(diag, line, "internal error: missing ctor field at runtime");
            return -1;
        }
        if (tc_exec_write_field_bytes((uint8_t *)block, offset_bytes, &field->type, &field_value,
                                      table, diag, line) != 0) {
            return -1;
        }
        /* memblock 元素 / ptr 所指可为结构体，须经宽度表回调（§3.9.3） */
        field_bits = tc_sizeof_bits_ex(&field->type, tc_struct_table_width_bits, (void *)table);
        bit_off += field_bits + (size_t)field->padding * 8U;
    }

    out->type = tc_type_tag_singleton(TC_STRUCT);
    out->bits = (uint64_t)(uintptr_t)block;
    return 0;
}

static int tc_exec_load_struct_base(const char *base_name, TcExecuteCtx *ctx, TcValue *out,
                                    int *out_struct_id, TcDiagnostic *diag, int line) {
    const TcSymbol *sym = tc_exec_find_symbol(ctx->symbols, base_name);

    if (!sym || tc_type_tag_of(sym->type) != TC_STRUCT || sym->slot < 0 ||
        !ctx->slots) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved struct base");
        return -1;
    }
    *out = ctx->slots[sym->slot];
    *out_struct_id = tc_type_struct_id(sym->type);
    if (*out_struct_id < 0) {
        tc_exec_set_internal_error(diag, line, "internal error: missing struct id on base");
        return -1;
    }
    return 0;
}

static int tc_exec_load_struct_base_resolved(const TcResolvedFieldAccess *access, TcExecuteCtx *ctx,
                                             TcValue *out, TcDiagnostic *diag, int line) {
    if (!access || !access->resolved) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved field access");
        return -1;
    }
    if (access->base_slot >= 0) {
        if (!ctx->slots) {
            tc_exec_set_internal_error(diag, line, "internal error: missing runtime slots");
            return -1;
        }
        *out = ctx->slots[access->base_slot];
        return 0;
    }
    if (access->const_bits != 0 || access->field_type) {
        out->type = tc_type_tag_singleton(TC_STRUCT);
        out->bits = access->const_bits;
        return 0;
    }
    tc_exec_set_internal_error(diag, line, "internal error: unresolved struct base");
    return -1;
}

int tc_exec_eval_field_access(const TcResolvedFieldAccess *access, TcExecuteCtx *ctx,
                              TcValue *out, TcDiagnostic *diag, int line) {
    const TcStructTable *table = tc_exec_struct_table(ctx);
    TcValue base;
    void *data = NULL;
    size_t offset = 0;
    const TcType *field_type = NULL;

    if (!access || !access->resolved || !access->field_type) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid field access");
        return -1;
    }
    if (access->is_memblock_count) {
        /* 分析器已将声明 count 存入 const_bits（tc_struct_check.c） */
        return tc_exec_memblock_count(access->base_slot, (uint64_t)access->const_bits, ctx, out,
                                      diag, line);
    }
    if (access->field_count == 0 || !access->offsets) {
        tc_exec_set_internal_error(diag, line, "internal error: missing field offsets");
        return -1;
    }
    if (tc_exec_load_struct_base_resolved(access, ctx, &base, diag, line) != 0) {
        return -1;
    }
    data = tc_struct_data(&base);
    if (!data) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid struct field read");
        return -1;
    }
    offset = access->offsets[access->field_count - 1];
    field_type = access->field_type;
    return tc_exec_read_field_bytes((const uint8_t *)data, offset, field_type, table, ctx, out,
                                    diag, line);
}

int tc_exec_struct_field_read(const TcRhs *rhs, TcTypeTag expected_type, TcExecuteCtx *ctx,
                              TcValue *out, TcDiagnostic *diag, int line) {
    (void)expected_type;
    if (!rhs || rhs->kind != TC_RHS_FIELD_READ) {
        return -1;
    }
    if (rhs->u.field_read.resolved.resolved) {
        return tc_exec_eval_field_access(&rhs->u.field_read.resolved, ctx, out, diag, line);
    }
    {
        const TcStructTable *table = tc_exec_struct_table(ctx);
        TcValue base;
        int struct_id = -1;
        size_t offset = 0;
        const TcType *field_type = NULL;
        void *data = NULL;

        if (tc_exec_load_struct_base(rhs->u.field_read.base, ctx, &base, &struct_id, diag, line) !=
            0) {
            return -1;
        }
        if (tc_struct_path_offset_bytes(table, struct_id, rhs->u.field_read.fields,
                                        rhs->u.field_read.field_count, &offset, &field_type, diag,
                                        line) != 0) {
            return -1;
        }
        data = tc_struct_data(&base);
        if (!data || !field_type) {
            tc_exec_set_internal_error(diag, line, "internal error: invalid struct field read");
            return -1;
        }
        return tc_exec_read_field_bytes((const uint8_t *)data, offset, field_type, table, ctx, out,
                                        diag, line);
    }
}

int tc_exec_struct_field_assign(const TcFieldAssign *assign, TcExecuteCtx *ctx,
                                TcDiagnostic *diag) {
    const TcStructTable *table = tc_exec_struct_table(ctx);
    TcValue base;
    int struct_id = -1;
    size_t offset = 0;
    const TcType *field_type = NULL;
    void *data = NULL;
    TcValue rhs_value;

    if (!assign) {
        return -1;
    }
    if (tc_exec_load_struct_base(assign->base, ctx, &base, &struct_id, diag, assign->line) != 0) {
        return -1;
    }
    if (tc_struct_path_offset_bytes(table, struct_id, assign->fields, assign->field_count, &offset,
                                    &field_type, diag, assign->line) != 0) {
        return -1;
    }
    data = tc_struct_data(&base);
    if (!data || !field_type) {
        tc_exec_set_internal_error(diag, assign->line, "internal error: invalid field assign");
        return -1;
    }
    if (tc_eval_rhs(&assign->rhs, field_type->tag, ctx, &rhs_value, diag, assign->line) != 0) {
        return -1;
    }
    return tc_exec_write_field_bytes((uint8_t *)data, offset, field_type, &rhs_value, table, diag,
                                     assign->line);
}

void tc_exec_struct_heap_free(TcExecuteCtx *ctx) {
    size_t i = 0;

    if (!ctx) {
        return;
    }
    for (i = 0; i < ctx->struct_heap_count; i++) {
        free(ctx->struct_heap[i]);
    }
    free(ctx->struct_heap);
    ctx->struct_heap = NULL;
    ctx->struct_heap_count = 0;
    ctx->struct_heap_capacity = 0;
}
