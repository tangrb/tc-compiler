/*
 * tc_embed.c — TC 嵌入式运行时实现（v0.0.37）
 *
 * 双模式：VM 路径（Executor）+ AOT 路径（直调生成代码）。
 * AOT 桥接函数见 tc_embed_aot.c。
 */
#include "tc_embed_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_executor_internal.h"   /* tc_find_func_def, tc_exec_param_slot */
#include "tc_semantics.h"           /* tc_slots_init_uninitialized */
#include "tc_memblock_exec.h"       /* tc_exec_memblock_heap_free */
#include "tc_struct_exec.h"         /* tc_exec_struct_heap_free */
#include "tc_call_frame.h"          /* tc_call_frame_push/pop */
#include "tc_symbol.h"              /* tc_symbol_table_runtime_slot_count */

/* ── 内部辅助 ── */

void tc_embed_set_error(TcEmbedCtx *ctx, const char *msg) {
    ctx->error_flag = 1;
    if (msg) {
        (void)snprintf(ctx->error_message, sizeof(ctx->error_message), "%s", msg);
    }
}

/* 在 AOT 函数表中查找 func_id 对应的 entry */
static const tc_aot_func_entry *tc_embed_find_aot_entry(
        const TcEmbedCtx *ctx, int func_id) {
    const tc_aot_func_entry *entry = NULL;
    size_t i = 0;

    if (!ctx->aot_func_table) return NULL;
    for (i = 0; ; i++) {
        entry = &ctx->aot_func_table[i];
        if (entry->func_id < 0) break;    /* sentinel */
        if (entry->func_id == func_id) return entry;
    }
    return NULL;
}

/*
 * 从 TcProgram 中收集函数定义，构建 TcEmbedFuncInfo 条目。
 */
static int tc_embed_build_func_index_program(TcEmbedCtx *ctx,
                                              const TcProgram *module,
                                              const char *module_name) {
    size_t i = 0;
    size_t j = 0;
    size_t old_count = ctx->func_count;
    int count_added = 0;

    for (i = 0; i < module->count; i++) {
        if (module->items[i].kind == TC_STMT_FUNC_DEF) {
            count_added++;
        }
    }
    if (count_added == 0) return 0;

    {
        TcEmbedFuncInfo *new_funcs = (TcEmbedFuncInfo *)realloc(
            ctx->funcs, (ctx->func_count + (size_t)count_added) * sizeof(TcEmbedFuncInfo));
        if (!new_funcs) {
            tc_embed_set_error(ctx, "memory allocation failed");
            return -1;
        }
        ctx->funcs = new_funcs;
    }
    memset(&ctx->funcs[old_count], 0, (size_t)count_added * sizeof(TcEmbedFuncInfo));

    for (i = 0; i < module->count; i++) {
        if (module->items[i].kind != TC_STMT_FUNC_DEF) continue;

        const TcFuncDef *func = &module->items[i].u.func_def;
        TcEmbedFuncInfo *info = &ctx->funcs[ctx->func_count];

        info->module_name = func->name;
        info->func_name = func->name;
        info->func_id = func->func_id;
        info->has_return = (func->return_type.tag != TC_VOID);
        info->return_type = (int)func->return_type.tag;
        info->param_count = func->param_count;

        if (func->param_count > 0) {
            info->param_slots = (int *)malloc(func->param_count * sizeof(int));
            info->param_types = (int *)malloc(func->param_count * sizeof(int));
            if (!info->param_slots || !info->param_types) {
                free(info->param_slots);
                free(info->param_types);
                info->param_slots = NULL;
                info->param_types = NULL;
                tc_embed_set_error(ctx, "memory allocation failed");
                return -1;
            }
            for (j = 0; j < func->param_count; j++) {
                int slot = -1;
                if (tc_exec_param_slot(&ctx->program->symbols, func,
                                        func->params[j].name, &slot) != 0 || slot < 0) {
                    tc_embed_set_error(ctx, "internal error: unresolved parameter slot");
                    return -1;
                }
                info->param_slots[j] = slot;
                info->param_types[j] = (int)func->params[j].type.tag;
            }
        } else {
            info->param_slots = NULL;
            info->param_types = NULL;
        }

        ctx->func_count++;
    }

    for (i = old_count; i < ctx->func_count; i++) {
        ctx->funcs[i].module_name = module_name;
    }

    return 0;
}

int tc_embed_build_func_index(TcEmbedCtx *ctx) {
    size_t i = 0;
    const char *entry_name = NULL;

    entry_name = ctx->program->program.module_name;
    if (!entry_name) entry_name = "";

    if (tc_embed_build_func_index_program(ctx, &ctx->program->program, entry_name) != 0) {
        return -1;
    }
    for (i = 0; i < ctx->program->dep_count; i++) {
        const char *dep_name = ctx->program->deps[i].module_name;
        if (!dep_name) dep_name = "";
        if (tc_embed_build_func_index_program(ctx, &ctx->program->deps[i], dep_name) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ── 生命周期 ── */

TcEmbedCtx *tc_embed_create(const TcTypedProgram *program, TcDiagnostic *diag) {
    TcEmbedCtx *ctx = NULL;
    size_t slot_count = 0;

    if (!program) {
        tc_diagnostic_set_api(diag, TC_API_ERR_INVALID_ARGUMENT,
                              "invalid argument: program is NULL");
        return NULL;
    }

    ctx = (TcEmbedCtx *)calloc(1, sizeof(TcEmbedCtx));
    if (!ctx) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return NULL;
    }

    ctx->is_aot = 0;
    ctx->program = program;
    tc_diagnostic_init(&ctx->diag);

    slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);
    if (slot_count > 0) {
        ctx->exec_ctx.slots = (TcValue *)malloc(slot_count * sizeof(TcValue));
        if (!ctx->exec_ctx.slots) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            tc_embed_destroy(ctx);
            return NULL;
        }
        tc_slots_init_uninitialized(ctx->exec_ctx.slots, slot_count);
    }

    ctx->exec_ctx.symbols = &program->symbols;
    ctx->exec_ctx.program = program;
    ctx->exec_ctx.current_func_id = -1;
    ctx->exec_ctx.call_frame = NULL;
    ctx->exec_ctx.memblock_heap = NULL;
    ctx->exec_ctx.memblock_heap_count = 0;
    ctx->exec_ctx.memblock_heap_capacity = 0;
    ctx->exec_ctx.struct_heap = NULL;
    ctx->exec_ctx.struct_heap_count = 0;
    ctx->exec_ctx.struct_heap_capacity = 0;
    ctx->tmp_top = (int)slot_count;
    tc_stmt_index_reset(&ctx->exec_ctx.index);

    if (tc_exec_init_all_static_vars(program, &ctx->exec_ctx, &ctx->diag) != 0) {
        tc_diagnostic_set(diag, ctx->diag.kind, ctx->diag.line, ctx->diag.column,
                          ctx->diag.message);
        tc_embed_destroy(ctx);
        return NULL;
    }

    if (tc_embed_build_func_index(ctx) != 0) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          ctx->error_message);
        tc_embed_destroy(ctx);
        return NULL;
    }

    return ctx;
}

void tc_embed_destroy(TcEmbedCtx *ctx) {
    size_t i = 0;

    if (!ctx) return;

    for (i = 0; i < ctx->func_count; i++) {
        free(ctx->funcs[i].param_slots);
        free(ctx->funcs[i].param_types);
    }
    free(ctx->funcs);

    if (ctx->is_aot) {
        /* AOT 模式：slots 由 AOT 生成的全局数据管理，不释放 */
        tc_diagnostic_clear(&ctx->diag);
        free(ctx);
        return;
    }

    /* VM 模式：释放 executor 拥有的资源 */
    tc_exec_memblock_heap_free(&ctx->exec_ctx);
    tc_exec_struct_heap_free(&ctx->exec_ctx);
    free(ctx->exec_ctx.slots);
    tc_diagnostic_clear(&ctx->diag);
    free(ctx);
}

/* ── 符号查询 ── */

const TcEmbedFuncInfo *tc_embed_func_info(const TcEmbedCtx *ctx,
                                           const char *module,
                                           const char *func_name) {
    size_t i = 0;
    int module_empty = (!module || module[0] == '\0');

    if (!ctx || !func_name) return NULL;

    for (i = 0; i < ctx->func_count; i++) {
        const TcEmbedFuncInfo *info = &ctx->funcs[i];
        int info_module_empty = (!info->module_name || info->module_name[0] == '\0');

        if (module_empty) {
            if (!info_module_empty) continue;
        } else {
            if (info_module_empty) continue;
            if (strcmp(info->module_name, module) != 0) continue;
        }

        if (strcmp(info->func_name, func_name) == 0) {
            return info;
        }
    }

    if (!module_empty) {
        for (i = 0; i < ctx->func_count; i++) {
            const TcEmbedFuncInfo *info = &ctx->funcs[i];
            int info_module_empty = (!info->module_name || info->module_name[0] == '\0');

            if (!info_module_empty) continue;
            if (strcmp(info->func_name, func_name) == 0) {
                return info;
            }
        }
    }

    return NULL;
}

int tc_embed_top_var_slot(const TcEmbedCtx *ctx, const char *name) {
    size_t i = 0;

    if (!ctx || !name) return -1;

    for (i = 0; i < ctx->program->symbols.count; i++) {
        const TcSymbol *sym = &ctx->program->symbols.symbols[i];

        if (sym->sym_kind == TC_SYM_VARIABLE && sym->slot_domain == TC_SLOT_TOPLEVEL) {
            if (sym->name && strcmp(sym->name, name) == 0) {
                return sym->slot;
            }
        }
    }
    return -1;
}

int tc_embed_self_var_slot(const TcEmbedCtx *ctx, const char *name) {
    size_t i = 0;

    if (!ctx || !name) return -1;

    for (i = 0; i < ctx->program->symbols.count; i++) {
        const TcSymbol *sym = &ctx->program->symbols.symbols[i];

        /* static var 实际使用 TC_SYM_VARIABLE + TC_SLOT_STATIC（而非 TC_SYM_STATIC_VAR） */
        if ((sym->sym_kind == TC_SYM_VARIABLE && sym->slot_domain == TC_SLOT_STATIC &&
             sym->slot >= 0) ||
            sym->sym_kind == TC_SYM_STATIC_VAR ||
            sym->sym_kind == TC_SYM_STATIC_LET) {
            if (sym->name && strcmp(sym->name, name) == 0) {
                return sym->slot;
            }
        }
    }
    return -1;
}

/* ── 槽位信息与读写 ── */

size_t tc_embed_slot_count(const TcEmbedCtx *ctx) {
    if (!ctx) return 0;
    if (ctx->is_aot) return ctx->aot_slot_count;
    return tc_symbol_table_runtime_slot_count(&ctx->program->symbols);
}

int tc_embed_slot_write(TcEmbedCtx *ctx, int slot, TcValue value) {
    size_t count = tc_embed_slot_count(ctx);

    if (slot < 0 || (size_t)slot >= count) {
        char msg[128];
        (void)snprintf(msg, sizeof(msg), "slot index %d out of range [0, %zu)", slot, count);
        tc_embed_set_error(ctx, msg);
        return -1;
    }

    if (ctx->is_aot) {
        ctx->aot_slots[slot] = value.bits;
    } else {
        ctx->exec_ctx.slots[slot] = value;
    }
    ctx->error_flag = 0;
    return 0;
}

int tc_embed_slot_read(const TcEmbedCtx *ctx, int slot, TcValue *out) {
    size_t count = tc_embed_slot_count(ctx);

    if (!out) return -1;
    if (slot < 0 || (size_t)slot >= count) {
        return -1;
    }

    if (ctx->is_aot) {
        out->bits = ctx->aot_slots[slot];
        out->type = tc_type_tag_singleton(TC_INT64);  /* AOT 模式下不追踪类型，调用方自行保证 */
    } else {
        *out = ctx->exec_ctx.slots[slot];
    }
    return 0;
}

/* ── 临时槽位区（运行时便捷层） ── */

int tc_embed_tmp_begin(TcEmbedCtx *ctx, size_t n, int *base_slot_out) {
    if (!ctx || !base_slot_out) return -1;

    if (ctx->tmp_depth >= TC_EMBED_TMP_MAX_DEPTH) {
        tc_embed_set_error(ctx, "temporary slot region: max nesting depth exceeded");
        return -1;
    }
    if ((size_t)ctx->tmp_top < n) {
        tc_embed_set_error(ctx, "temporary slot region exhausted");
        return -1;
    }

    ctx->tmp_marks[ctx->tmp_depth] = ctx->tmp_top;
    ctx->tmp_top = (int)((size_t)ctx->tmp_top - n);
    *base_slot_out = ctx->tmp_top;
    ctx->tmp_depth++;
    return 0;
}

void tc_embed_tmp_end(TcEmbedCtx *ctx) {
    if (!ctx || ctx->tmp_depth <= 0) return;
    ctx->tmp_depth--;
    ctx->tmp_top = ctx->tmp_marks[ctx->tmp_depth];
}

/*
 * 将 C 侧内存中的标量值按 TC 类型读入 TcValue。
 * 使用 memcpy 读取，避免未对齐访问；位模式与 tc_value_from_* 一致。
 */
static int tc_embed_raw_to_value(TcTypeTag type, const void *src, TcValue *out) {
    const char *p = (const char *)src;

    if (!src || !out) return -1;

    switch (type) {
    case TC_INT8: {
        int8_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_int8(t);
        return 0;
    }
    case TC_UINT8: {
        uint8_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_uint8(t);
        return 0;
    }
    case TC_INT16: {
        int16_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_int16(t);
        return 0;
    }
    case TC_UINT16: {
        uint16_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_uint16(t);
        return 0;
    }
    case TC_INT32: {
        int32_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_int32(t);
        return 0;
    }
    case TC_UINT32: {
        uint32_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_uint32(t);
        return 0;
    }
    case TC_INT64: {
        int64_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_int64(t);
        return 0;
    }
    case TC_UINT64: {
        uint64_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_uint64(t);
        return 0;
    }
    case TC_BOOL: {
        uint8_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_bool(t != 0);
        return 0;
    }
    case TC_FLOAT32: {
        float t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_float(t);
        return 0;
    }
    case TC_FLOAT64: {
        double t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_double(t);
        return 0;
    }
    case TC_ISIZE: {
        intptr_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_int64((int64_t)t);
        out->type = tc_type_tag_singleton(TC_ISIZE);
        return 0;
    }
    case TC_USIZE: {
        uintptr_t t;
        memcpy(&t, p, sizeof(t));
        *out = tc_value_from_uint64((uint64_t)t);
        out->type = tc_type_tag_singleton(TC_USIZE);
        return 0;
    }
    default:
        return -1;
    }
}

int tc_embed_make_ptr(TcEmbedCtx *ctx, TcTypeTag elem_type,
                      const void *data, size_t count, TcValue *out) {
    size_t elem_bytes = (size_t)(tc_type_bit_width(elem_type) / 8);
    int base = 0;
    size_t i = 0;

    if (!ctx || !out) return -1;

    if (elem_bytes == 0 ||
        (!tc_type_is_integer(elem_type) && !tc_type_is_float(elem_type) &&
         !tc_type_is_bool(elem_type))) {
        tc_embed_set_error(ctx, "unsupported element type for tc_embed_make_ptr");
        return -1;
    }

    if (!data || count == 0) {
        out->type = tc_type_tag_singleton(TC_PTR);
        out->bits = 0;   /* nullptr */
        return 0;
    }

    if (tc_embed_tmp_begin(ctx, count, &base) != 0) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        TcValue v;
        const char *elem = (const char *)data + i * elem_bytes;

        if (tc_embed_raw_to_value(elem_type, elem, &v) != 0) {
            tc_embed_set_error(ctx, "unsupported element type for tc_embed_make_ptr");
            return -1;
        }
        if (tc_embed_slot_write(ctx, base + (int)i, v) != 0) {
            return -1;
        }
    }

    *out = tc_embed_ptr_encode(base);
    return 0;
}

/* ── 函数调用 ── */

int tc_embed_call(TcEmbedCtx *ctx, const char *module, const char *func,
                  int nargs, const TcValue *args, TcValue *result) {
    const TcEmbedFuncInfo *info = NULL;
    int rc = 0;
    TcValue ret;
    int i = 0;

    if (!ctx || !func) {
        return -1;
    }

    ctx->error_flag = 0;
    ctx->error_message[0] = '\0';

    info = tc_embed_func_info(ctx, module, func);
    if (!info) {
        char msg[256];
        const char *mod = module ? module : "";
        (void)snprintf(msg, sizeof(msg), "function not found: %s::%s", mod, func);
        tc_embed_set_error(ctx, msg);
        return -1;
    }

    if ((size_t)nargs != info->param_count) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg),
                       "wrong argument count for %s: expected %zu, got %d",
                       func, info->param_count, nargs);
        tc_embed_set_error(ctx, msg);
        return -1;
    }

    /* 将实参写入形参 slot */
    for (i = 0; i < nargs; i++) {
        tc_embed_slot_write(ctx, info->param_slots[i], args[i]);
    }

    /* ── AOT 路径 ── */
    if (ctx->is_aot) {
        const tc_aot_func_entry *entry = tc_embed_find_aot_entry(ctx, info->func_id);

        if (!entry) {
            tc_embed_set_error(ctx, "internal error: AOT function entry not found");
            return -1;
        }

        tc_diagnostic_init(&ctx->diag);
        entry->entry(&ctx->diag);

        if (ctx->diag.domain != TC_DIAG_NONE) {
            tc_embed_set_error(ctx, ctx->diag.message && ctx->diag.message[0]
                                     ? ctx->diag.message
                                     : "runtime error in AOT function");
            return -1;
        }

        if (info->has_return && result && entry->ret_ptr) {
            result->bits = *entry->ret_ptr;
            result->type = tc_type_tag_singleton((TcTypeTag)info->return_type);
        }
        return 0;
    }

    /* ── VM 路径 ── */
    tc_diagnostic_init(&ctx->diag);
    rc = tc_exec_call_function_public(info->func_id, &ctx->exec_ctx,
                                      &ret, info->has_return,
                                      &ctx->diag, 0);
    if (rc != 0) {
        tc_embed_set_error(ctx, ctx->diag.message);
        return -1;
    }

    if (info->has_return && result) {
        *result = ret;
    }

    return 0;
}

int tc_embed_call_typed(TcEmbedCtx *ctx, const TcEmbedFuncInfo *info,
                        const TcEmbedArg *args, size_t nargs,
                        TcValue *result) {
    TcValue stack_values[TC_EMBED_TYPED_MAX_ARGS];
    TcValue *values = stack_values;
    size_t i = 0;
    int rc = 0;

    if (!ctx || !info) {
        if (ctx) tc_embed_set_error(ctx, "function not found");
        return -1;
    }
    if (nargs != info->param_count) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg),
                       "wrong argument count for %s: expected %zu, got %zu",
                       info->func_name, info->param_count, nargs);
        tc_embed_set_error(ctx, msg);
        return -1;
    }
    for (i = 0; i < nargs; i++) {
        if (args[i].type != (TcTypeTag)info->param_types[i]) {
            char msg[256];
            (void)snprintf(msg, sizeof(msg),
                           "argument %zu type mismatch for %s: expected %d, got %d",
                           i, info->func_name, info->param_types[i],
                           (int)args[i].type);
            tc_embed_set_error(ctx, msg);
            return -1;
        }
    }

    if (nargs > TC_EMBED_TYPED_MAX_ARGS) {
        values = (TcValue *)malloc(nargs * sizeof(TcValue));
        if (!values) {
            tc_embed_set_error(ctx, "memory allocation failed");
            return -1;
        }
    }
    for (i = 0; i < nargs; i++) {
        values[i].type = tc_type_tag_singleton(args[i].type);
        values[i].bits = args[i].bits;
    }

    rc = tc_embed_call(ctx, info->module_name, info->func_name,
                       (int)nargs, values, result);
    if (values != stack_values) {
        free(values);
    }
    return rc;
}

/* ── 错误查询 ── */

const char *tc_embed_get_error(const TcEmbedCtx *ctx) {
    if (!ctx) return "context is null";
    return ctx->error_message;
}

int tc_embed_had_error(const TcEmbedCtx *ctx) {
    if (!ctx) return 1;
    return ctx->error_flag;
}
