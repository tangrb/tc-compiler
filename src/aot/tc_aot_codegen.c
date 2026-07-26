/*
 * tc_aot_codegen.c — TC → C99 转译（AOT Codegen）
 *
 * 消费 Analyzer 产出的 TcTypedProgram，逐语句生成等价的 C99 代码。
 * 生成的代码由统一全局 slots[]、tc_func_<id> 函数、tc_init_static_vars 与 main() 组成。
 *
 * 设计原则：
 *   - 算术、cast、比较、逻辑、位运算、I/O 均通过 tc_aot_rt.h 中的 shim 函数
 *     委托 tc_semantics.c / tc_io.c，保证与 TC-VM 行为完全一致。
 *   - let 常量编译器已求值，Codegen 直接将 const_value.bits 写为字面量。
 *   - CONST_REF / CONST_CAST 在 Analyzer 阶段应已被折叠，Codegen 发现则报错。
 *   - if → 原生 C if-else；label/goto → tc_label_<stmt_index>（无 shim）。
 */
#include "tc_aot_codegen.h"

#include "tc_stmt_index.h"
#include "tc_struct_check.h"
#include "tc_symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/** 块路径深度上限（与 Analyzer / Executor 同构：then=if*2，else=if*2+1） */
#define TC_AOT_BLOCK_DEPTH_MAX 64
#define TC_AOT_LOOP_DEPTH_MAX 64

typedef struct {
    TcBlockId path[TC_AOT_BLOCK_DEPTH_MAX];
    int depth;
} TcAotBlockPath;

typedef struct {
    int loop_ids[TC_AOT_LOOP_DEPTH_MAX];
    int depth;
} TcAotLoopStack;

/** Codegen 阶段 DFS 语句序号 + 块路径（与 Analyzer / Executor 一致） */
typedef struct {
    TcStmtIndexCursor index;
    TcSymbolNameIndex sym_index;
    TcAotBlockPath block_path;
    TcAotLoopStack loops;
    const TcTypedProgram *program;
    int current_func_id; /* -1 = 顶层 */
    TcTypeKind current_return_type;
    int tmp_seq; /* 嵌套 RHS 临时变量唯一序号 */
    int embed_mode; /* 1 = 嵌入库模式 */
} TcAotEmitCtx;

static int tc_aot_paths_equal_prefix(const TcBlockId *a, const TcBlockId *b, int depth) {
    int i = 0;

    for (i = 0; i < depth; i++) {
        if (a[i].owner_stmt_index != b[i].owner_stmt_index || a[i].kind != b[i].kind) {
            return 0;
        }
    }
    return 1;
}

static int tc_aot_block_path_push(TcAotBlockPath *bp, TcBlockId block_id) {
    if (bp->depth >= TC_AOT_BLOCK_DEPTH_MAX) {
        return -1;
    }
    bp->path[bp->depth++] = block_id;
    return 0;
}

static void tc_aot_block_path_pop(TcAotBlockPath *bp) {
    if (bp->depth > 0) {
        bp->depth--;
    }
}

static int tc_aot_loop_stack_push(TcAotLoopStack *loops, int loop_id) {
    if (loop_id < 0 || loops->depth >= TC_AOT_LOOP_DEPTH_MAX) {
        return -1;
    }
    loops->loop_ids[loops->depth++] = loop_id;
    return 0;
}

static void tc_aot_loop_stack_pop(TcAotLoopStack *loops) {
    if (loops->depth > 0) {
        loops->depth--;
    }
}

static const TcLabelEntry *tc_aot_resolve_goto_label(const TcSymbolTable *table, const char *name,
                                                     const TcAotBlockPath *goto_path) {
    const TcLabelEntry *best_same = NULL;
    const TcLabelEntry *best_ancestor = NULL;
    const TcLabelEntry *any = NULL;
    size_t i = 0;

    for (i = 0; i < table->label_count; i++) {
        const TcLabelEntry *entry = &table->labels[i];

        if (strcmp(entry->name, name) != 0) {
            continue;
        }
        any = entry;
        if (entry->block_depth == goto_path->depth &&
            tc_aot_paths_equal_prefix(entry->block_path, goto_path->path, entry->block_depth)) {
            best_same = entry;
        } else if (entry->block_depth < goto_path->depth &&
                   tc_aot_paths_equal_prefix(entry->block_path, goto_path->path,
                                              entry->block_depth)) {
            if (!best_ancestor || entry->block_depth > best_ancestor->block_depth) {
                best_ancestor = entry;
            }
        }
    }
    if (best_same) {
        return best_same;
    }
    if (best_ancestor) {
        return best_ancestor;
    }
    return any;
}

static const TcSymbol *tc_aot_find_def_symbol(const TcSymbolTable *symbols, const char *name,
                                              int def_line) {
    size_t i = 0;

    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (sym->def_line == def_line && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static const TcFuncDef *tc_aot_find_func_def(const TcTypedProgram *prog, int func_id,
                                             const TcProgram **out_module) {
    size_t i = 0;
    size_t j = 0;

    if (!prog || func_id < 0) {
        return NULL;
    }
    for (i = 0; i < prog->program.count; i++) {
        if (prog->program.items[i].kind == TC_STMT_FUNC_DEF &&
            prog->program.items[i].u.func_def.func_id == func_id) {
            if (out_module) {
                *out_module = &prog->program;
            }
            return &prog->program.items[i].u.func_def;
        }
    }
    for (i = 0; i < prog->dep_count; i++) {
        for (j = 0; j < prog->deps[i].count; j++) {
            if (prog->deps[i].items[j].kind == TC_STMT_FUNC_DEF &&
                prog->deps[i].items[j].u.func_def.func_id == func_id) {
                if (out_module) {
                    *out_module = &prog->deps[i];
                }
                return &prog->deps[i].items[j].u.func_def;
            }
        }
    }
    return NULL;
}

static int tc_aot_func_body_index_range(const TcProgram *module, int func_id, int *out_body_start,
                                        int *out_body_end) {
    size_t i = 0;
    int cursor = 0;

    if (!module || !out_body_start || !out_body_end) {
        return -1;
    }
    for (i = 0; i < module->count; i++) {
        const TcStatement *stmt = &module->items[i];
        int span = tc_stmt_subtree_index_count(stmt);

        if (stmt->kind == TC_STMT_FUNC_DEF && stmt->u.func_def.func_id == func_id) {
            *out_body_start = cursor + 1;
            *out_body_end = cursor + span;
            return 0;
        }
        cursor += span;
    }
    return -1;
}

static int tc_aot_param_slot(const TcSymbolTable *symbols, const TcFuncDef *func,
                             const char *param_name, int *out_slot) {
    size_t i = 0;
    size_t pi = 0;

    if (!symbols || !func || !param_name || !out_slot) {
        return -1;
    }
    for (pi = 0; pi < func->param_count; pi++) {
        if (func->params[pi].name && strcmp(func->params[pi].name, param_name) == 0) {
            size_t seen = 0;
            for (i = 0; i < symbols->count; i++) {
                const TcSymbol *sym = &symbols->symbols[i];

                if (sym->slot_domain != TC_SLOT_PARAM || sym->def_line != func->line) {
                    continue;
                }
                if (seen == pi) {
                    *out_slot = sym->slot;
                    return 0;
                }
                seen++;
            }
            break;
        }
    }
    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (sym->slot_domain == TC_SLOT_PARAM && sym->def_line == func->line && sym->name &&
            strcmp(sym->name, param_name) == 0) {
            *out_slot = sym->slot;
            return 0;
        }
    }
    return -1;
}

static const TcRhs *tc_aot_find_named_arg_rhs(const char *param_name, const TcNamedArg *args,
                                              size_t arg_count) {
    size_t i = 0;

    for (i = 0; i < arg_count; i++) {
        if (args[i].param_name && param_name && strcmp(args[i].param_name, param_name) == 0) {
            return &args[i].value;
        }
    }
    return NULL;
}

typedef struct {
    char *param_name;
    TcRhs *value;
} TcAotFuncallExprArg;

static const TcRhs *tc_aot_find_expr_arg_rhs(const char *param_name,
                                             const TcAotFuncallExprArg *args, size_t arg_count) {
    size_t i = 0;

    for (i = 0; i < arg_count; i++) {
        if (args[i].param_name && param_name && strcmp(args[i].param_name, param_name) == 0) {
            return args[i].value;
        }
    }
    return NULL;
}

static const TcSymbol *tc_aot_find_symbol_by_name(const TcSymbolTable *symbols,
                                                  const char *name) {
    size_t i = 0;
    const TcSymbol *best = NULL;

    if (!symbols || !name) {
        return NULL;
    }
    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (!sym->name || strcmp(sym->name, name) != 0) {
            continue;
        }
        if (!best || sym->def_stmt_index > best->def_stmt_index) {
            best = sym;
        }
    }
    return best;
}

static int tc_aot_resolve_var_slot(const TcSymbolTable *symbols, const TcSymbolNameIndex *sym_index,
                                   const char *name, int stmt_index, int *out_slot) {
    const TcSymbol *sym = tc_symbol_table_find_visible(symbols, name, stmt_index, sym_index);

    if (!sym || sym->slot < 0) {
        return -1;
    }
    *out_slot = sym->slot;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  辅助函数                                                           */
/* ------------------------------------------------------------------ */

static const char *tc_aot_type_enum(TcTypeKind type) {
    switch (type) {
    case TC_INT8:
        return "TC_INT8";
    case TC_UINT8:
        return "TC_UINT8";
    case TC_INT16:
        return "TC_INT16";
    case TC_UINT16:
        return "TC_UINT16";
    case TC_INT32:
        return "TC_INT32";
    case TC_UINT32:
        return "TC_UINT32";
    case TC_INT64:
        return "TC_INT64";
    case TC_UINT64:
        return "TC_UINT64";
    case TC_BOOL:
        return "TC_BOOL";
    case TC_FLOAT32:
        return "TC_FLOAT32";
    case TC_FLOAT64:
        return "TC_FLOAT64";
    case TC_ISIZE:
        return "TC_ISIZE";
    case TC_USIZE:
        return "TC_USIZE";
    case TC_PTR:
        return "TC_PTR";
    case TC_MEMBLOCK:
        return "TC_MEMBLOCK";
    case TC_VOID:
        return "TC_VOID";
    case TC_STRUCT:
        return "TC_STRUCT";
    }
    return "TC_INT32";
}

static const char *tc_aot_format_enum(TcFormatSpec fmt) {
    switch (fmt) {
    case TC_FMT_NONE:
        return "TC_FMT_NONE";
    case TC_FMT_D:
        return "TC_FMT_D";
    case TC_FMT_I:
        return "TC_FMT_I";
    case TC_FMT_U:
        return "TC_FMT_U";
    case TC_FMT_X:
        return "TC_FMT_X";
    case TC_FMT_XU:
        return "TC_FMT_XU";
    case TC_FMT_O:
        return "TC_FMT_O";
    case TC_FMT_B:
        return "TC_FMT_B";
    case TC_FMT_T:
        return "TC_FMT_T";
    case TC_FMT_F:
        return "TC_FMT_F";
    case TC_FMT_E:
        return "TC_FMT_E";
    case TC_FMT_EU:
        return "TC_FMT_EU";
    case TC_FMT_G:
        return "TC_FMT_G";
    case TC_FMT_GU:
        return "TC_FMT_GU";
    }
    return "TC_FMT_NONE";
}

static void tc_aot_sub_indent(char *out, size_t out_size, const char *base, int levels) {
    size_t len = strlen(base);
    size_t extra = (size_t)levels * 4U;

    if (len + extra + 1U > out_size) {
        out[0] = '\0';
        return;
    }
    memcpy(out, base, len);
    memset(out + len, ' ', extra);
    out[len + extra] = '\0';
}

static void tc_aot_emit_c_string(FILE *out, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "<source>");

    fputc('"', out);
    while (*p != '\0') {
        switch (*p) {
        case '\\':
            fputs("\\\\", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20U || *p >= 0x7fU) {
                fprintf(out, "\\%03o", (unsigned int)*p);
            } else {
                fputc((int)*p, out);
            }
            break;
        }
        p++;
    }
    fputc('"', out);
}

static const char *tc_aot_ptr_compare_op(TcCompareOp op) {
    switch (op) {
    case TC_CMP_EQ:
        return "TC_CMP_EQ";
    case TC_CMP_NE:
        return "TC_CMP_NE";
    case TC_CMP_LT:
        return "TC_CMP_LT";
    case TC_CMP_LE:
        return "TC_CMP_LE";
    case TC_CMP_GT:
        return "TC_CMP_GT";
    case TC_CMP_GE:
        return "TC_CMP_GE";
    }
    return "TC_CMP_EQ";
}

/* ------------------------------------------------------------------ */
/*  表达式发射                                                          */
/* ------------------------------------------------------------------ */

static void tc_aot_emit_literal_expr(FILE *out, TcTypeKind type, const TcLiteral *lit) {
    if (lit->is_nullptr) {
        fprintf(out, "0ULL");
        return;
    }
    if (lit->is_bool) {
        fprintf(out, "tc_aot_lit(%s, %lluULL, 0, 0)", tc_aot_type_enum(TC_BOOL),
                lit->magnitude ? 1ULL : 0ULL);
        return;
    }
    if (lit->is_float) {
        double d = lit->float_value;
        if (type == TC_FLOAT32) {
            float f = (float)d;
            uint32_t b32 = 0;
            memcpy(&b32, &f, sizeof(b32));
            fprintf(out, "tc_aot_lit(%s, 0x%xULL, 0, 0)", tc_aot_type_enum(type), b32);
        } else {
            uint64_t b64 = 0;
            memcpy(&b64, &d, sizeof(b64));
            fprintf(out, "tc_aot_lit(%s, 0x%" PRIx64 "ULL, 0, 0)", tc_aot_type_enum(type), b64);
        }
        return;
    }
    fprintf(out, "tc_aot_lit(%s, %" PRIu64 "ULL, %d, %d)", tc_aot_type_enum(type), lit->magnitude,
            lit->negative, lit->unsigned_suffix);
}

static void tc_aot_emit_operand_expr(FILE *out, const TcOperand *operand, TcTypeKind type,
                                     const TcAotEmitCtx *ctx, int stmt_index) {
    const TcSymbolTable *symbols = &ctx->program->symbols;

    if (operand->kind == TC_OPERAND_LIT) {
        tc_aot_emit_literal_expr(out, type, &operand->u.lit);
        return;
    }
    if (operand->binding.resolved) {
        if (operand->binding.is_const) {
            fprintf(out, "0x%016" PRIx64 "ULL", operand->binding.const_bits);
        } else if (operand->binding.slot >= 0) {
            fprintf(out, "slots[%d]", operand->binding.slot);
        }
        return;
    }
    {
        const TcSymbol *symbol =
            tc_symbol_table_find_visible(symbols, operand->u.name, stmt_index, &ctx->sym_index);

        if (!symbol) {
            symbol = tc_aot_find_symbol_by_name(symbols, operand->u.name);
        }
        if (!symbol) {
            return;
        }
        if (symbol->sym_kind == TC_SYM_CONSTANT && symbol->has_const_value) {
            fprintf(out, "0x%016" PRIx64 "ULL", symbol->const_value.bits);
        } else {
            fprintf(out, "slots[%d]", symbol->slot);
        }
    }
}

static int tc_aot_emit_operand_assign(FILE *out, const TcOperand *operand, TcTypeKind type,
                                      const char *dst_expr, const char *indent,
                                      const TcAotEmitCtx *ctx, int stmt_index) {
    fprintf(out, "%s%s = ", indent, dst_expr);
    tc_aot_emit_operand_expr(out, operand, type, ctx, stmt_index);
    fprintf(out, ";\n");
    return 0;
}

static int tc_aot_emit_rhs(FILE *out, const TcRhs *rhs, TcTypeKind expected_type,
                           const char *dst_expr, const char *indent, TcAotEmitCtx *ctx,
                           int stmt_index, int line);

static int tc_aot_emit_rhs_slot(FILE *out, const TcRhs *rhs, TcTypeKind expected_type, int dst_slot,
                                const char *indent, TcAotEmitCtx *ctx, int stmt_index, int line) {
    char dst_expr[32];

    snprintf(dst_expr, sizeof(dst_expr), "slots[%d]", dst_slot);
    return tc_aot_emit_rhs(out, rhs, expected_type, dst_expr, indent, ctx, stmt_index, line);
}

static int tc_aot_emit_funcall(FILE *out, int func_id, const TcNamedArg *stmt_args,
                               size_t stmt_arg_count, const TcAotFuncallExprArg *expr_args,
                               size_t expr_arg_count, int use_expr_args, const char *indent,
                               TcAotEmitCtx *ctx, int stmt_index, int line, int want_result,
                               const char *dst_expr) {
    const TcProgram *module = NULL;
    const TcFuncDef *func = NULL;
    char abort_indent[64];
    size_t pi = 0;

    func = tc_aot_find_func_def(ctx->program, func_id, &module);
    if (!func) {
        return -1;
    }
    tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);

    for (pi = 0; pi < func->param_count; pi++) {
        const TcFuncParam *param = &func->params[pi];
        const TcRhs *arg_rhs = NULL;
        int param_slot = -1;

        if (use_expr_args) {
            arg_rhs = tc_aot_find_expr_arg_rhs(param->name, expr_args, expr_arg_count);
        } else {
            arg_rhs = tc_aot_find_named_arg_rhs(param->name, stmt_args, stmt_arg_count);
        }
        if (!arg_rhs) {
            return -1;
        }
        if (tc_aot_param_slot(&ctx->program->symbols, func, param->name, &param_slot) != 0 ||
            param_slot < 0) {
            return -1;
        }
        if (tc_aot_emit_rhs_slot(out, arg_rhs, param->type.kind, param_slot, indent, ctx,
                                 stmt_index, line) != 0) {
            return -1;
        }
    }

    fprintf(out, "%stc_aot_func_%d(tc_aot_cur_diag);\n", indent, func_id);
    fprintf(out, "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, %d);\n",
            abort_indent, line);
    if (want_result && dst_expr) {
        fprintf(out, "%s%s = tc_aot_ret_%d;\n", indent, dst_expr, func_id);
    }
    return 0;
}

static int tc_aot_emit_rhs(FILE *out, const TcRhs *rhs, TcTypeKind expected_type,
                           const char *dst_expr, const char *indent, TcAotEmitCtx *ctx,
                           int stmt_index, int line) {
    const TcSymbolTable *symbols = &ctx->program->symbols;
    char abort_indent[64];

    tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);

    if (rhs->kind == TC_RHS_LIT) {
        fprintf(out, "%s%s = ", indent, dst_expr);
        tc_aot_emit_literal_expr(out, expected_type, &rhs->u.lit);
        fprintf(out, ";\n");
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        if (rhs->u.const_ref.binding.resolved) {
            if (rhs->u.const_ref.binding.is_const) {
                fprintf(out, "%s%s = 0x%016" PRIx64 "ULL;\n", indent, dst_expr,
                        rhs->u.const_ref.binding.const_bits);
            } else if (rhs->u.const_ref.binding.slot >= 0) {
                if (expected_type == TC_STRUCT) {
                    const TcSymbol *sym = tc_symbol_table_find_visible(
                        symbols, rhs->u.const_ref.name, stmt_index, &ctx->sym_index);
                    size_t bytes = 0;

                    if (sym && sym->struct_id >= 0 && ctx->program->struct_table) {
                        const TcStructEntry *e =
                            tc_struct_table_get(ctx->program->struct_table, sym->struct_id);
                        if (e) {
                            bytes = (e->width_bits + 7U) / 8U;
                        }
                    }
                    if (bytes == 0) {
                        return -1;
                    }
                    fprintf(out,
                            "%s%s = tc_aot_struct_clone(slots[%d], %zu, tc_aot_cur_diag, %d);\n",
                            indent, dst_expr, rhs->u.const_ref.binding.slot, bytes, line);
                    fprintf(out,
                            "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                            "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                            abort_indent, line);
                } else {
                    fprintf(out, "%s%s = slots[%d];\n", indent, dst_expr,
                            rhs->u.const_ref.binding.slot);
                }
            } else {
                return -1;
            }
            return 0;
        }
        {
            const TcSymbol *symbol = tc_symbol_table_find_visible(
                symbols, rhs->u.const_ref.name, stmt_index, &ctx->sym_index);

            if (!symbol) {
                return -1;
            }
            if (symbol->sym_kind == TC_SYM_CONSTANT && symbol->has_const_value) {
                fprintf(out, "%s%s = 0x%016" PRIx64 "ULL;\n", indent, dst_expr,
                        symbol->const_value.bits);
            } else if (expected_type == TC_STRUCT) {
                size_t bytes = 0;
                if (symbol->struct_id >= 0 && ctx->program->struct_table) {
                    const TcStructEntry *e =
                        tc_struct_table_get(ctx->program->struct_table, symbol->struct_id);
                    if (e) {
                        bytes = (e->width_bits + 7U) / 8U;
                    }
                }
                if (bytes == 0) {
                    return -1;
                }
                fprintf(out, "%s%s = tc_aot_struct_clone(slots[%d], %zu, tc_aot_cur_diag, %d);\n",
                        indent, dst_expr, symbol->slot, bytes, line);
                fprintf(out,
                        "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                        "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                        abort_indent, line);
            } else {
                fprintf(out, "%s%s = slots[%d];\n", indent, dst_expr, symbol->slot);
            }
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        const char *mode =
            rhs->u.arith.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";
        const char *op_name = NULL;

        switch (rhs->u.arith.op) {
        case TC_ADD:
            op_name = "TC_ADD";
            break;
        case TC_SUB:
            op_name = "TC_SUB";
            break;
        case TC_MUL:
            op_name = "TC_MUL";
            break;
        case TC_DIV:
            op_name = "TC_DIV";
            break;
        case TC_MOD:
            op_name = "TC_MOD";
            break;
        }

        fprintf(out, "%sif (tc_aot_arith(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.arith.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.arith.lhs, rhs->u.arith.type, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.arith.rhs, rhs->u.arith.type, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        const char *mode =
            rhs->u.unary.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";
        const char *op_name = rhs->u.unary.op == TC_UNARY_ABS ? "TC_UNARY_ABS" : "TC_UNARY_NEG";

        fprintf(out, "%sif (tc_aot_unary(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.unary.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.unary.operand, rhs->u.unary.type, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        const char *op_name = "TC_CMP_EQ";
        switch (rhs->u.compare.op) {
        case TC_CMP_EQ:
            op_name = "TC_CMP_EQ";
            break;
        case TC_CMP_NE:
            op_name = "TC_CMP_NE";
            break;
        case TC_CMP_LT:
            op_name = "TC_CMP_LT";
            break;
        case TC_CMP_LE:
            op_name = "TC_CMP_LE";
            break;
        case TC_CMP_GT:
            op_name = "TC_CMP_GT";
            break;
        case TC_CMP_GE:
            op_name = "TC_CMP_GE";
            break;
        }
        fprintf(out, "%sif (tc_aot_compare(%s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.compare.type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.compare.lhs, rhs->u.compare.type, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.compare.rhs, rhs->u.compare.type, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        fprintf(out, "%sif (tc_aot_logic_unary(TC_LOGIC_NOT, &%s, ", indent, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_un.operand, TC_BOOL, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        const char *op_name;
        if (rhs->u.logic_bin.op == TC_LOGIC_AND) {
            op_name = "TC_LOGIC_AND";
        } else if (rhs->u.logic_bin.op == TC_LOGIC_XOR) {
            op_name = "TC_LOGIC_XOR";
        } else {
            op_name = "TC_LOGIC_OR";
        }
        fprintf(out, "%sif (tc_aot_logic(%s, &%s, ", indent, op_name, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.lhs, TC_BOOL, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.rhs, TC_BOOL, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        const char *op_name = "TC_BIT_AND";

        switch (rhs->u.bitwise_bin.op) {
        case TC_BIT_AND:
            op_name = "TC_BIT_AND";
            break;
        case TC_BIT_OR:
            op_name = "TC_BIT_OR";
            break;
        case TC_BIT_XOR:
            op_name = "TC_BIT_XOR";
            break;
        }

        fprintf(out, "%sif (tc_aot_bitwise_binary(%s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.bitwise_bin.type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type, ctx,
                                 stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        fprintf(out, "%sif (tc_aot_bitwise_unary(%s, &%s, ", indent,
                tc_aot_type_enum(rhs->u.bitwise_un.type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        const char *op_name =
            rhs->u.shift.op == TC_SHIFT_SHL ? "TC_SHIFT_SHL" : "TC_SHIFT_SHR";
        const char *mode =
            rhs->u.shift.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";

        fprintf(out, "%sif (tc_aot_shift(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.shift.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.shift.value, rhs->u.shift.type, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.shift.count, rhs->u.shift.type, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITCAST) {
        fprintf(out, "%sif (tc_aot_bitcast(%s, %s, &%s, ", indent,
                tc_aot_type_enum(rhs->u.bitcast.target),
                tc_aot_type_enum(rhs->u.bitcast.source_type), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitcast.source, rhs->u.bitcast.source_type, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        return -1;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        const char *mode = rhs->u.float_arith.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                    : "TC_FLOAT_STRICT";
        const char *op_name = "TC_ADD";

        switch (rhs->u.float_arith.op) {
        case TC_ADD:
            op_name = "TC_ADD";
            break;
        case TC_SUB:
            op_name = "TC_SUB";
            break;
        case TC_MUL:
            op_name = "TC_MUL";
            break;
        case TC_DIV:
            op_name = "TC_DIV";
            break;
        case TC_MOD:
            op_name = "TC_MOD";
            break;
        }
        fprintf(out, "%sif (tc_aot_fp_arith(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_arith.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_arith.lhs, rhs->u.float_arith.type, ctx,
                                 stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.float_arith.rhs, rhs->u.float_arith.type, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        const char *mode = rhs->u.float_unary.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                    : "TC_FLOAT_STRICT";
        const char *op_name =
            rhs->u.float_unary.op == TC_UNARY_ABS ? "TC_UNARY_ABS" : "TC_UNARY_NEG";

        fprintf(out, "%sif (tc_aot_fp_unary(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_unary.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_unary.operand, rhs->u.float_unary.type, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        const char *mode = rhs->u.float_compare.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                      : "TC_FLOAT_STRICT";
        const char *op_name = "TC_CMP_EQ";

        switch (rhs->u.float_compare.op) {
        case TC_CMP_EQ:
            op_name = "TC_CMP_EQ";
            break;
        case TC_CMP_NE:
            op_name = "TC_CMP_NE";
            break;
        case TC_CMP_LT:
            op_name = "TC_CMP_LT";
            break;
        case TC_CMP_LE:
            op_name = "TC_CMP_LE";
            break;
        case TC_CMP_GT:
            op_name = "TC_CMP_GT";
            break;
        case TC_CMP_GE:
            op_name = "TC_CMP_GE";
            break;
        }
        fprintf(out, "%sif (tc_aot_fp_compare(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_compare.type), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_compare.lhs, rhs->u.float_compare.type, ctx,
                                 stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.float_compare.rhs, rhs->u.float_compare.type, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_ADDRESS) {
        int slot = -1;

        if (rhs->u.ptr_address.name &&
            tc_aot_resolve_var_slot(symbols, &ctx->sym_index, rhs->u.ptr_address.name, stmt_index,
                                    &slot) == 0) {
            fprintf(out, "%s%s = tc_aot_ptr_address(%d);\n", indent, dst_expr, slot);
            return 0;
        }
        return -1;
    }

    if (rhs->kind == TC_RHS_PTR_LOAD) {
        fprintf(out, "%sif (tc_aot_ptr_load(slots, ", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_load.ptr, TC_PTR, ctx, stmt_index);
        fprintf(out, ", &%s, tc_aot_cur_diag, %d) != 0)\n", dst_expr, line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_ADD || rhs->kind == TC_RHS_PTR_SUB) {
        int is_add = rhs->kind == TC_RHS_PTR_ADD ? 1 : 0;

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _off;\n", indent);
        fprintf(out, "%s    _off = ", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_arith.offset, TC_USIZE, ctx, stmt_index);
        fprintf(out, ";\n");
        fprintf(out, "%s    if (tc_aot_ptr_arith(%d, ", indent, is_add);
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_arith.ptr, TC_PTR, ctx, stmt_index);
        fprintf(out, ", (int64_t)_off, &%s, tc_aot_cur_diag, %d) != 0)\n", dst_expr, line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_EQ || rhs->kind == TC_RHS_PTR_NE || rhs->kind == TC_RHS_PTR_LT ||
        rhs->kind == TC_RHS_PTR_LE || rhs->kind == TC_RHS_PTR_GT || rhs->kind == TC_RHS_PTR_GE) {
        TcCompareOp op = TC_CMP_EQ;

        switch (rhs->kind) {
        case TC_RHS_PTR_EQ:
            op = TC_CMP_EQ;
            break;
        case TC_RHS_PTR_NE:
            op = TC_CMP_NE;
            break;
        case TC_RHS_PTR_LT:
            op = TC_CMP_LT;
            break;
        case TC_RHS_PTR_LE:
            op = TC_CMP_LE;
            break;
        case TC_RHS_PTR_GT:
            op = TC_CMP_GT;
            break;
        case TC_RHS_PTR_GE:
            op = TC_CMP_GE;
            break;
        default:
            break;
        }
        fprintf(out, "%sif (tc_aot_ptr_compare(%s, ", indent, tc_aot_ptr_compare_op(op));
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_compare.lhs, TC_PTR, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_compare.rhs, TC_PTR, ctx, stmt_index);
        fprintf(out, ", &%s, tc_aot_cur_diag, %d) != 0)\n", dst_expr, line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_SIZE) {
        TcType pointee = rhs->u.ptr_size.pointee_type;
        size_t sizeof_bits = tc_sizeof_bits(&pointee);

        (void)rhs->u.ptr_size.ptr;
        fprintf(out, "%s%s = tc_aot_ptr_size(%zu);\n", indent, dst_expr, sizeof_bits);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_CONSTRUCTOR) {
        TcType element = tc_type_scalar(rhs->u.memblock_ctor.element_type.kind);
        size_t elem_bits = tc_sizeof_bits(&element);
        size_t elem_bytes = (elem_bits + 7U) / 8U;
        size_t i = 0;

        fprintf(out, "%s%s = tc_aot_memblock_alloc(%" PRIu64 "ULL, %zu, tc_aot_cur_diag, %d);\n",
                indent, dst_expr, rhs->u.memblock_ctor.count, elem_bytes, line);
        fprintf(out, "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent,
                line);
        if (rhs->u.memblock_ctor.is_fill) {
            char fill_tmp[32];
            snprintf(fill_tmp, sizeof(fill_tmp), "_tc_fill_%d", stmt_index);
            fprintf(out, "%s{\n", indent);
            fprintf(out, "%s    uint64_t %s;\n", indent, fill_tmp);
            if (tc_aot_emit_operand_assign(out, &rhs->u.memblock_ctor.fill_value, element.kind,
                                           fill_tmp, abort_indent, ctx, stmt_index) != 0) {
                return -1;
            }
            for (i = 0; i < rhs->u.memblock_ctor.count; i++) {
                fprintf(out, "%s    tc_aot_memblock_set_elem(%s, %zu, %zu, %s);\n", abort_indent,
                        dst_expr, elem_bytes, i, fill_tmp);
            }
            fprintf(out, "%s}\n", indent);
        } else {
            for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
                char elem_tmp[32];
                snprintf(elem_tmp, sizeof(elem_tmp), "_tc_mb_%d_%zu", stmt_index, i);
                fprintf(out, "%s{\n", indent);
                fprintf(out, "%s    uint64_t %s;\n", indent, elem_tmp);
                if (tc_aot_emit_operand_assign(out, &rhs->u.memblock_ctor.values[i], element.kind,
                                               elem_tmp, abort_indent, ctx, stmt_index) != 0) {
                    return -1;
                }
                fprintf(out, "%s    tc_aot_memblock_set_elem(%s, %zu, %zu, %s);\n", abort_indent,
                        dst_expr, elem_bytes, i, elem_tmp);
                fprintf(out, "%s}\n", indent);
            }
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_LOAD) {
        TcType element = tc_type_scalar(rhs->u.memblock_load.element_type.kind);
        size_t elem_bytes = (tc_sizeof_bits(&element) + 7U) / 8U;

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _mb_idx;\n", indent);
        fprintf(out, "%s    _mb_idx = ", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.memblock_load.index, TC_USIZE, ctx, stmt_index);
        fprintf(out, ";\n");
        fprintf(out, "%s    if (tc_aot_memblock_load(", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.memblock_load.memblock, TC_MEMBLOCK, ctx, stmt_index);
        fprintf(out, ", %zu, _mb_idx, %s, &%s, tc_aot_cur_diag, %d) != 0)\n", elem_bytes,
                tc_aot_type_enum(element.kind), dst_expr, line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_COUNT) {
        const TcSymbol *sym = tc_symbol_table_find_visible(
            symbols, rhs->u.memblock_count.memblock_name, stmt_index, &ctx->sym_index);

        if (sym && sym->slot >= 0) {
            fprintf(out, "%s{\n", indent);
            fprintf(out, "%s    uint64_t _mb_cnt = tc_aot_memblock_get_count(slots[%d]);\n", indent,
                    sym->slot);
            if (sym->memblock_count > 0) {
                fprintf(out, "%s    if (_mb_cnt == 0) _mb_cnt = %" PRIu64 "ULL;\n", indent,
                        sym->memblock_count);
            }
            fprintf(out, "%s    %s = _mb_cnt;\n", indent, dst_expr);
            fprintf(out, "%s}\n", indent);
            return 0;
        }
        if (sym && sym->memblock_count > 0) {
            fprintf(out, "%s%s = %" PRIu64 "ULL;\n", indent, dst_expr, sym->memblock_count);
            return 0;
        }
        return -1;
    }

    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        const TcSymbol *sym = tc_aot_find_symbol_by_name(symbols, rhs->u.self_member.member_name);

        if (!sym) {
            return -1;
        }
        if (sym->sym_kind == TC_SYM_CONSTANT && sym->has_const_value) {
            fprintf(out, "%s%s = 0x%016" PRIx64 "ULL;\n", indent, dst_expr,
                    sym->const_value.bits);
            return 0;
        }
        if (sym->slot >= 0) {
            fprintf(out, "%s%s = slots[%d];\n", indent, dst_expr, sym->slot);
            return 0;
        }
        return -1;
    }

    if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        const TcStructTable *table = ctx->program->struct_table;
        const TcStructEntry *entry = NULL;
        size_t bytes = 0;
        size_t bit_off = 0;
        size_t i = 0;
        TcDiagnostic local_diag;

        entry = tc_struct_table_find(table, rhs->u.struct_ctor.struct_name);
        if (!entry) {
            return -1;
        }
        bytes = (entry->width_bits + 7U) / 8U;
        fprintf(out, "%s%s = tc_aot_struct_alloc(%zu, tc_aot_cur_diag, %d);\n", indent, dst_expr,
                bytes, line);
        fprintf(out,
                "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, %d);\n",
                abort_indent, line);
        tc_diagnostic_init(&local_diag);
        for (i = 0; i < entry->field_count; i++) {
            const TcStructField *field = &entry->fields[i];
            size_t offset = bit_off / 8U;
            size_t nbytes = 0;
            size_t fi = 0;
            char tmp[48];
            int found = 0;
            int tmp_id = ctx->tmp_seq++;

            if (field->type.kind == TC_STRUCT) {
                const TcStructEntry *nested =
                    tc_struct_table_get(table, field->type.params.struct_type.struct_id);
                nbytes = nested ? (nested->width_bits + 7U) / 8U : 0;
            } else {
                nbytes = (tc_sizeof_bits(&field->type) + 7U) / 8U;
            }
            snprintf(tmp, sizeof(tmp), "_tc_sf_%d_%d", stmt_index, tmp_id);
            for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
                if (strcmp(rhs->u.struct_ctor.fields[fi].param_name, field->name) != 0) {
                    continue;
                }
                found = 1;
                fprintf(out, "%s{\n", indent);
                fprintf(out, "%s    uint64_t %s;\n", indent, tmp);
                if (rhs->u.struct_ctor.fields[fi].has_rhs) {
                    if (tc_aot_emit_rhs(out, (const TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs,
                                        field->type.kind, tmp, abort_indent, ctx, stmt_index,
                                        line) != 0) {
                        return -1;
                    }
                } else if (tc_aot_emit_operand_assign(out, &rhs->u.struct_ctor.fields[fi].value_op,
                                                      field->type.kind, tmp, abort_indent, ctx,
                                                      stmt_index) != 0) {
                    return -1;
                }
                if (field->type.kind == TC_STRUCT) {
                    fprintf(out, "%s    tc_aot_struct_memcpy_field(%s, %zu, %zu, %s);\n",
                            abort_indent, dst_expr, offset, nbytes, tmp);
                } else {
                    fprintf(out, "%s    tc_aot_struct_store_bits(%s, %zu, %zu, %s);\n", abort_indent,
                            dst_expr, offset, nbytes, tmp);
                }
                fprintf(out, "%s}\n", indent);
                break;
            }
            if (!found) {
                return -1;
            }
            if (field->type.kind == TC_STRUCT) {
                const TcStructEntry *nested =
                    tc_struct_table_get(table, field->type.params.struct_type.struct_id);
                bit_off += nested ? nested->width_bits : 0;
            } else {
                bit_off += tc_sizeof_bits(&field->type);
            }
            bit_off += (size_t)field->padding * 8U;
        }
        (void)local_diag;
        return 0;
    }

    if (rhs->kind == TC_RHS_FIELD_READ) {
        const TcStructTable *table = ctx->program->struct_table;
        const TcSymbol *base_sym = tc_symbol_table_find_visible(
            symbols, rhs->u.field_read.base, stmt_index, &ctx->sym_index);
        size_t offset = 0;
        const TcType *field_type = NULL;
        size_t nbytes = 0;
        TcDiagnostic local_diag;

        if (!base_sym || base_sym->slot < 0 || base_sym->struct_id < 0) {
            return -1;
        }
        tc_diagnostic_init(&local_diag);
        if (tc_struct_path_offset_bytes(table, base_sym->struct_id, rhs->u.field_read.fields,
                                        rhs->u.field_read.field_count, &offset, &field_type,
                                        &local_diag, line) != 0) {
            return -1;
        }
        if (field_type->kind == TC_STRUCT) {
            const TcStructEntry *nested =
                tc_struct_table_get(table, field_type->params.struct_type.struct_id);
            nbytes = nested ? (nested->width_bits + 7U) / 8U : 0;
            fprintf(out,
                    "%s%s = tc_aot_struct_extract(slots[%d], %zu, %zu, tc_aot_cur_diag, %d);\n",
                    indent, dst_expr, base_sym->slot, offset, nbytes, line);
            fprintf(out,
                    "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                    "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                    abort_indent, line);
        } else {
            nbytes = (tc_sizeof_bits(field_type) + 7U) / 8U;
            fprintf(out, "%stc_aot_struct_load_bits(slots[%d], %zu, %zu, &%s);\n", indent,
                    base_sym->slot, offset, nbytes, dst_expr);
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_FUNCALL_EXPR) {
        if (rhs->u.funcall_expr.resolved_func_id < 0) {
            return -1;
        }
        return tc_aot_emit_funcall(out, rhs->u.funcall_expr.resolved_func_id, NULL, 0,
                                   (const TcAotFuncallExprArg *)rhs->u.funcall_expr.args,
                                   rhs->u.funcall_expr.arg_count, 1, indent, ctx, stmt_index, line,
                                   1, dst_expr);
    }

    if (rhs->kind != TC_RHS_CAST) {
        return -1;
    }

    {
        const char *mode =
            rhs->u.cast.mode == TC_TRUNC_TRUNCATE ? "TC_TRUNC_TRUNCATE" : "TC_TRUNC_STRICT";

        fprintf(out, "%sif (tc_aot_cast(%s, %s, ", indent,
                tc_aot_type_enum(rhs->u.cast.target), mode);
        tc_aot_emit_operand_expr(out, &rhs->u.cast.source, rhs->u.cast.source_type, ctx, stmt_index);
        fprintf(out, ", %s, &%s, tc_aot_cur_diag, %d) != 0)\n",
                tc_aot_type_enum(rhs->u.cast.source_type), dst_expr, line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  语句发射                                                            */
/* ------------------------------------------------------------------ */

static int tc_aot_emit_statement_impl(FILE *out, const TcStatement *stmt, TcAotEmitCtx *ctx,
                                      const char *indent) {
    const TcSymbolTable *symbols = &ctx->program->symbols;

    if (stmt->kind == TC_STMT_WHILE) {
        const TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        char loop_indent[64];
        char body_indent[64];
        char control_indent[64];
        char cond_name[32];
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(loop_indent, sizeof(loop_indent), indent, 1);
        tc_aot_sub_indent(body_indent, sizeof(body_indent), loop_indent, 1);
        tc_aot_sub_indent(control_indent, sizeof(control_indent), body_indent, 1);
        if (loop_indent[0] == '\0' || body_indent[0] == '\0' || control_indent[0] == '\0') {
            return -1;
        }
        if (snprintf(cond_name, sizeof(cond_name), "tc_cond_%d", while_stmt_index) < 0) {
            return -1;
        }

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%sfor (;;) {\n", loop_indent);
        fprintf(out, "%suint64_t %s;\n", body_indent, cond_name);
        if (tc_aot_emit_rhs(out, &while_stmt->condition, TC_BOOL, cond_name, body_indent, ctx,
                            while_stmt_index, while_stmt->line) != 0) {
            return -1;
        }
        fprintf(out, "%sif (%s == 0) {\n", body_indent, cond_name);
        fprintf(out, "%sbreak;\n", control_indent);
        fprintf(out, "%s}\n", body_indent);

        if (tc_aot_block_path_push(
                &ctx->block_path,
                (TcBlockId){while_stmt_index, TC_BLOCK_WHILE}) != 0 ||
            tc_aot_loop_stack_push(&ctx->loops, while_stmt->loop_id) != 0) {
            return -1;
        }
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_aot_emit_statement_impl(out, &while_stmt->body[i], ctx, body_indent) != 0) {
                return -1;
            }
        }
        tc_aot_loop_stack_pop(&ctx->loops);
        tc_aot_block_path_pop(&ctx->block_path);

        fprintf(out, "%s}\n", loop_indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        char block_indent[64];
        char branch_indent[64];
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(block_indent, sizeof(block_indent), indent, 1);
        tc_aot_sub_indent(branch_indent, sizeof(branch_indent), block_indent, 1);

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%suint64_t _cond;\n", block_indent);

        if (tc_aot_emit_rhs(out, &if_stmt->condition, TC_BOOL, "_cond", block_indent, ctx,
                            if_stmt_index, if_stmt->line) != 0) {
            return -1;
        }

        fprintf(out, "%sif (_cond != 0) {\n", block_indent);
        if (tc_aot_block_path_push(
                &ctx->block_path,
                (TcBlockId){if_stmt_index, TC_BLOCK_IF_THEN}) != 0) {
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_aot_emit_statement_impl(out, &if_stmt->then_body[i], ctx, branch_indent) != 0) {
                return -1;
            }
        }
        tc_aot_block_path_pop(&ctx->block_path);

        if (if_stmt->else_count > 0) {
            fprintf(out, "%s} else {\n", block_indent);
            if (tc_aot_block_path_push(
                    &ctx->block_path,
                    (TcBlockId){if_stmt_index, TC_BLOCK_IF_ELSE}) != 0) {
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_aot_emit_statement_impl(out, &if_stmt->else_body[i], ctx,
                                               branch_indent) != 0) {
                    return -1;
                }
            }
            tc_aot_block_path_pop(&ctx->block_path);
        }

        fprintf(out, "%s}\n", block_indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        int stmt_index_val = tc_stmt_index_take(&ctx->index);

        fprintf(out, "%sif (0) goto tc_label_%d;\n", indent, stmt_index_val);
        fprintf(out, "%stc_label_%d: ;\n", indent, stmt_index_val);
        return 0;
    }

    if (stmt->kind == TC_STMT_GOTO) {
        const TcLabelEntry *entry = NULL;

        tc_stmt_index_take(&ctx->index);
        entry = tc_aot_resolve_goto_label(symbols, stmt->u.goto_stmt.target, &ctx->block_path);
        if (!entry) {
            return -1;
        }
        fprintf(out, "%sgoto tc_label_%d;\n", indent, entry->stmt_index);
        return 0;
    }

    if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        const TcLoopControlStmt *loop_control =
            stmt->kind == TC_STMT_BREAK ? &stmt->u.break_stmt : &stmt->u.continue_stmt;

        tc_stmt_index_take(&ctx->index);
        if (ctx->loops.depth == 0 ||
            ctx->loops.loop_ids[ctx->loops.depth - 1] != loop_control->loop_id) {
            return -1;
        }
        fprintf(out, "%s%s;\n", indent,
                stmt->kind == TC_STMT_BREAK ? "break" : "continue");
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNC_DEF || stmt->kind == TC_STMT_STRUCT_DEF ||
        stmt->kind == TC_STMT_IMPORT || stmt->kind == TC_STMT_STATIC_LET_DEF ||
        stmt->kind == TC_STMT_STATIC_VAR_DEF) {
        tc_stmt_index_take(&ctx->index);
        return 0;
    }

    if (stmt->kind == TC_STMT_RETURN) {
        const TcReturnStmt *ret = &stmt->u.return_stmt;
        char abort_indent[64];

        tc_stmt_index_take(&ctx->index);
        if (ctx->current_func_id < 0) {
            return -1;
        }
        if (!ret->has_value) {
            fprintf(out, "%sreturn;\n", indent);
            return 0;
        }
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        if (ret->value.kind == TC_OPERAND_LIT) {
            fprintf(out, "%s    tc_aot_ret_%d = ", indent, ctx->current_func_id);
            tc_aot_emit_literal_expr(out, ctx->current_return_type, &ret->value.u.lit);
            fprintf(out, ";\n");
        } else {
            char ret_tmp[32];
            snprintf(ret_tmp, sizeof(ret_tmp), "tc_aot_ret_%d", ctx->current_func_id);
            if (tc_aot_emit_operand_assign(out, &ret->value, ctx->current_return_type, ret_tmp,
                                           abort_indent, ctx, ctx->index.next - 1) != 0) {
                return -1;
            }
        }
        fprintf(out, "%s    return;\n", indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNCALL) {
        const TcFuncallStmt *call = &stmt->u.funcall_stmt;
        int stmt_index = tc_stmt_index_take(&ctx->index);

        if (call->resolved_func_id < 0) {
            return -1;
        }
        return tc_aot_emit_funcall(out, call->resolved_func_id, call->args, call->arg_count, NULL, 0,
                                   0, indent, ctx, stmt_index, call->line, 0, NULL);
    }

    if (stmt->kind == TC_STMT_PTR_STORE) {
        const TcPtrStoreStmt *store = &stmt->u.ptr_store;
        char abort_indent[64];
        int stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _ptr, _val;\n", indent);
        if (tc_aot_emit_operand_assign(out, &store->ptr, TC_PTR, "_ptr", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &store->value, store->pointee_type.kind, "_val",
                                       abort_indent, ctx, stmt_index) != 0) {
            return -1;
        }
        fprintf(out, "%s    if (tc_aot_ptr_store(slots, _ptr, _val, %s, tc_aot_cur_diag, %d) != 0)\n",
                abort_indent, tc_aot_type_enum(store->pointee_type.kind), store->line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, store->line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_MEMBLOCK_STORE) {
        const TcMemblockStoreStmt *store = &stmt->u.memblock_store;
        char abort_indent[64];
        int stmt_index = tc_stmt_index_take(&ctx->index);
        int mb_slot = -1;
        TcType element = tc_type_scalar(store->element_type.kind);
        size_t elem_bytes = (tc_sizeof_bits(&element) + 7U) / 8U;

        if (tc_aot_resolve_var_slot(symbols, &ctx->sym_index, store->memblock_name, stmt_index,
                                    &mb_slot) != 0) {
            return -1;
        }
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _idx, _val;\n", indent);
        if (tc_aot_emit_operand_assign(out, &store->index, TC_USIZE, "_idx", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &store->value, element.kind, "_val", abort_indent, ctx,
                                       stmt_index) != 0) {
            return -1;
        }
        fprintf(out,
                "%s    if (tc_aot_memblock_store(slots[%d], %zu, _idx, _val, %s, tc_aot_cur_diag, %d) != 0)\n",
                abort_indent, mb_slot, elem_bytes, tc_aot_type_enum(element.kind), store->line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, store->line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_MEMBLOCK_COPY) {
        const TcMemblockCopyStmt *copy = &stmt->u.memblock_copy;
        char abort_indent[64];
        int stmt_index = tc_stmt_index_take(&ctx->index);
        int dst_slot = -1;
        int src_slot = -1;
        TcType element = tc_type_scalar(copy->element_type.kind);
        size_t elem_bytes = (tc_sizeof_bits(&element) + 7U) / 8U;

        if (tc_aot_resolve_var_slot(symbols, &ctx->sym_index, copy->dst_name, stmt_index,
                                    &dst_slot) != 0 ||
            tc_aot_resolve_var_slot(symbols, &ctx->sym_index, copy->src_name, stmt_index,
                                    &src_slot) != 0) {
            return -1;
        }
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _dst_idx, _src_idx, _len;\n", indent);
        if (tc_aot_emit_operand_assign(out, &copy->dst_index, TC_USIZE, "_dst_idx", abort_indent,
                                       ctx, stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &copy->src_index, TC_USIZE, "_src_idx", abort_indent,
                                       ctx, stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &copy->length, TC_USIZE, "_len", abort_indent, ctx,
                                       stmt_index) != 0) {
            return -1;
        }
        fprintf(out,
                "%s    if (tc_aot_memblock_copy(slots[%d], _dst_idx, slots[%d], _src_idx, _len, "
                "%zu, tc_aot_cur_diag, %d) != 0)\n",
                abort_indent, dst_slot, src_slot, elem_bytes, copy->line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, copy->line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_MEMCOPY_UNSAFE) {
        tc_stmt_index_take(&ctx->index);
        return -1;
    }

    if (stmt->kind == TC_STMT_FIELD_ASSIGN) {
        const TcFieldAssign *assign = &stmt->u.field_assign;
        const TcStructTable *table = ctx->program->struct_table;
        const TcSymbol *base_sym = NULL;
        size_t offset = 0;
        const TcType *field_type = NULL;
        size_t nbytes = 0;
        char tmp[48];
        char abort_indent[64];
        int stmt_index = 0;
        TcDiagnostic local_diag;

        stmt_index = tc_stmt_index_take(&ctx->index);
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        base_sym = tc_symbol_table_find_visible(symbols, assign->base, stmt_index, &ctx->sym_index);
        if (!base_sym || base_sym->slot < 0 || base_sym->struct_id < 0) {
            return -1;
        }
        tc_diagnostic_init(&local_diag);
        if (tc_struct_path_offset_bytes(table, base_sym->struct_id, assign->fields,
                                        assign->field_count, &offset, &field_type, &local_diag,
                                        assign->line) != 0) {
            return -1;
        }
        snprintf(tmp, sizeof(tmp), "_tc_fa_%d", stmt_index);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t %s;\n", indent, tmp);
        if (tc_aot_emit_rhs(out, &assign->rhs, field_type->kind, tmp, abort_indent, ctx, stmt_index,
                            assign->line) != 0) {
            return -1;
        }
        if (field_type->kind == TC_STRUCT) {
            const TcStructEntry *nested =
                tc_struct_table_get(table, field_type->params.struct_type.struct_id);
            nbytes = nested ? (nested->width_bits + 7U) / 8U : 0;
            fprintf(out, "%s    tc_aot_struct_memcpy_field(slots[%d], %zu, %zu, %s);\n", indent,
                    base_sym->slot, offset, nbytes, tmp);
        } else {
            nbytes = (tc_sizeof_bits(field_type) + 7U) / 8U;
            fprintf(out, "%s    tc_aot_struct_store_bits(slots[%d], %zu, %zu, %s);\n", indent,
                    base_sym->slot, offset, nbytes, tmp);
        }
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    {
        int stmt_index = tc_stmt_index_take(&ctx->index);
        const TcSymbol *symbol = NULL;

        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;
            TcTypeKind expected_type = var_def->type;
            int slot = -1;

            if (var_def->full_type.kind == TC_MEMBLOCK) {
                expected_type = TC_MEMBLOCK;
            } else if (var_def->full_type.kind == TC_PTR) {
                expected_type = TC_PTR;
            } else if (var_def->full_type.kind == TC_STRUCT) {
                expected_type = TC_STRUCT;
            }
            if (var_def->binding.resolved && !var_def->binding.is_const &&
                var_def->binding.slot >= 0) {
                slot = var_def->binding.slot;
            } else {
                symbol = tc_aot_find_def_symbol(symbols, var_def->name, var_def->line);
                if (!symbol) {
                    return -1;
                }
                slot = symbol->slot;
            }
            return tc_aot_emit_rhs_slot(out, &var_def->rhs, expected_type, slot, indent, ctx,
                                        stmt_index, var_def->line);
        }

        if (stmt->kind == TC_STMT_CONST_DEF) {
            return 0;
        }

        if (stmt->kind == TC_STMT_ASSIGN) {
            const TcAssign *assign = &stmt->u.assign;
            int slot = -1;
            TcTypeKind assign_type = TC_INT32;

            if (assign->binding.resolved && assign->binding.slot >= 0) {
                slot = assign->binding.slot;
                assign_type = assign->binding.type;
            } else {
                symbol = tc_symbol_table_find_visible(symbols, assign->name, stmt_index,
                                                        &ctx->sym_index);
                if (!symbol) {
                    return -1;
                }
                slot = symbol->slot;
                assign_type = symbol->type;
            }
            return tc_aot_emit_rhs_slot(out, &assign->rhs, assign_type, slot, indent, ctx, stmt_index,
                                        assign->line);
        }

        if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            const TcIoWrite *io = &stmt->u.io_write;
            int newline = stmt->kind == TC_STMT_WRITELN ? 1 : 0;
            char abort_indent[64];

            tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
            fprintf(out, "%sif (tc_aot_write(%s, %s, ", indent, tc_aot_type_enum(io->type),
                    tc_aot_format_enum(io->fmt.spec));
            tc_aot_emit_operand_expr(out, &io->operand, io->type, ctx, stmt_index);
            fprintf(out, ", %d, tc_aot_cur_diag, %d) != 0)\n", newline, io->line);
            fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, io->line);
            return 0;
        }

        if (stmt->kind == TC_STMT_READ) {
            const TcRead *io_read = &stmt->u.io_read;
            char abort_indent[64];
            int slot = -1;

            if (io_read->binding.resolved && !io_read->binding.is_const &&
                io_read->binding.slot >= 0) {
                slot = io_read->binding.slot;
            } else {
                symbol = tc_symbol_table_find_visible(symbols, io_read->name, stmt_index,
                                                      &ctx->sym_index);
                if (!symbol) {
                    return -1;
                }
                slot = symbol->slot;
            }
            tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
            fprintf(out, "%sif (tc_aot_read(%s, &slots[%d], tc_aot_cur_diag, %d) != 0)\n", indent,
                    tc_aot_type_enum(io_read->type), slot, io_read->line);
            fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, io_read->line);
            return 0;
        }
    }

    return -1;
}

static int tc_aot_emit_static_vars_program(FILE *out, const TcProgram *program, TcAotEmitCtx *ctx) {
    size_t i = 0;

    for (i = 0; i < program->count; i++) {
        if (program->items[i].kind == TC_STMT_STATIC_VAR_DEF) {
            const TcStaticVarDef *sv = &program->items[i].u.static_var_def;
            TcTypeKind rhs_type = sv->type.kind;

            if (sv->type.kind == TC_MEMBLOCK) {
                rhs_type = TC_MEMBLOCK;
            }
            if (sv->static_slot < 0) {
                return -1;
            }
            if (tc_aot_emit_rhs_slot(out, &sv->rhs, rhs_type, sv->static_slot, "    ", ctx, 0,
                                     sv->line) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int tc_aot_emit_function(FILE *out, const TcFuncDef *func, const TcProgram *module,
                                TcAotEmitCtx *ctx) {
    int body_start = 0;
    int body_end = 0;
    size_t i = 0;
    const char *qual = ctx->embed_mode ? "" : "static ";

    (void)body_end;
    if (tc_aot_func_body_index_range(module, func->func_id, &body_start, &body_end) != 0) {
        return -1;
    }
    fprintf(out, "%svoid tc_aot_func_%d(TcDiagnostic *diag) {\n    tc_aot_cur_diag = diag;\n",
            qual, func->func_id);
    ctx->current_func_id = func->func_id;
    ctx->current_return_type = func->return_type.kind;
    ctx->block_path.depth = 0;
    ctx->loops.depth = 0;
    tc_stmt_index_reset(&ctx->index);
    ctx->index.next = body_start;
    for (i = 0; i < func->body_count; i++) {
        if (tc_aot_emit_statement_impl(out, &func->body[i], ctx, "    ") != 0) {
            ctx->current_func_id = -1;
            return -1;
        }
    }
    ctx->current_func_id = -1;
    fprintf(out, "}\n\n");
    return 0;
}

static int tc_aot_emit_functions_program(FILE *out, const TcProgram *program, TcAotEmitCtx *ctx) {
    size_t i = 0;

    for (i = 0; i < program->count; i++) {
        if (program->items[i].kind == TC_STMT_FUNC_DEF) {
            if (tc_aot_emit_function(out, &program->items[i].u.func_def, program, ctx) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static void tc_aot_emit_func_decls(FILE *out, const TcTypedProgram *program,
                                    TcAotEmitCtx *ctx) {
    size_t di = 0;
    size_t i = 0;
    int wrote = 0;
    const char *qual = ctx->embed_mode ? "" : "static ";

    for (di = 0; di < program->dep_count; di++) {
        for (i = 0; i < program->deps[di].count; i++) {
            if (program->deps[di].items[i].kind == TC_STMT_FUNC_DEF) {
                const TcFuncDef *func = &program->deps[di].items[i].u.func_def;

                if (func->return_type.kind != TC_VOID) {
                    fprintf(out, "%suint64_t tc_aot_ret_%d;\n", qual, func->func_id);
                }
                fprintf(out, "%svoid tc_aot_func_%d(TcDiagnostic *diag);\n", qual, func->func_id);
                wrote = 1;
            }
        }
    }
    for (i = 0; i < program->program.count; i++) {
        if (program->program.items[i].kind == TC_STMT_FUNC_DEF) {
            const TcFuncDef *func = &program->program.items[i].u.func_def;

            if (func->return_type.kind != TC_VOID) {
                fprintf(out, "%suint64_t tc_aot_ret_%d;\n", qual, func->func_id);
            }
            fprintf(out, "%svoid tc_aot_func_%d(TcDiagnostic *diag);\n", qual, func->func_id);
            wrote = 1;
        }
    }
    if (wrote) {
        fputc('\n', out);
    }
}

/* ── 嵌入模式：生成函数表 ── */
static int tc_aot_emit_func_table(FILE *out, const TcTypedProgram *program) {
    size_t di = 0;
    size_t i = 0;

    fprintf(out, "/* ── 函数表 ── */\n");
    fprintf(out, "const tc_aot_func_entry tc_aot_func_table[] = {\n");

    for (di = 0; di < program->dep_count; di++) {
        for (i = 0; i < program->deps[di].count; i++) {
            if (program->deps[di].items[i].kind == TC_STMT_FUNC_DEF) {
                const TcFuncDef *func = &program->deps[di].items[i].u.func_def;
                if (func->return_type.kind != TC_VOID) {
                    fprintf(out, "    { %d, tc_aot_func_%d, &tc_aot_ret_%d },\n",
                            func->func_id, func->func_id, func->func_id);
                } else {
                    fprintf(out, "    { %d, tc_aot_func_%d, NULL },\n",
                            func->func_id, func->func_id);
                }
            }
        }
    }
    for (i = 0; i < program->program.count; i++) {
        if (program->program.items[i].kind == TC_STMT_FUNC_DEF) {
            const TcFuncDef *func = &program->program.items[i].u.func_def;
            if (func->return_type.kind != TC_VOID) {
                fprintf(out, "    { %d, tc_aot_func_%d, &tc_aot_ret_%d },\n",
                        func->func_id, func->func_id, func->func_id);
            } else {
                fprintf(out, "    { %d, tc_aot_func_%d, NULL },\n",
                        func->func_id, func->func_id);
            }
        }
    }

    fprintf(out, "    { -1, NULL, NULL }\n");
    fprintf(out, "};\n\n");
    return 0;
}

/* ── 嵌入模式：生成头文件 ── */
int tc_aot_emit_embed_header(FILE *out, const TcTypedProgram *program,
                              const char *source_name) {
    size_t slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);

    (void)source_name;
    fprintf(out, "/* Auto-generated header by tc-aot --embed. Do not edit. */\n");
    fprintf(out, "#ifndef TC_AOT_EMBED_H\n");
    fprintf(out, "#define TC_AOT_EMBED_H\n\n");
    fprintf(out, "#include <stddef.h>\n");
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include \"tc_diagnostic.h\"\n");
    fprintf(out, "#include \"tc_aot_embed_rt.h\"\n\n");
    if (slot_count > 0) {
        fprintf(out, "#define TC_AOT_SLOT_COUNT %zu\n\n", slot_count);
        fprintf(out, "extern uint64_t slots[%zu];\n\n", slot_count);
    } else {
        fprintf(out, "#define TC_AOT_SLOT_COUNT 1\n\n");
        fprintf(out, "extern uint64_t slots[1];\n\n");
    }
    fprintf(out, "int tc_aot_init(TcDiagnostic *diag);\n");
    fprintf(out, "void tc_aot_cleanup(void);\n\n");

    /* 函数表类型 */
    fprintf(out, "typedef void (*tc_aot_func_entry_t)(TcDiagnostic *diag);\n\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int func_id;\n");
    fprintf(out, "    tc_aot_func_entry_t entry;\n");
    fprintf(out, "    uint64_t *ret_ptr;\n");
    fprintf(out, "} tc_aot_func_entry;\n\n");
    fprintf(out, "extern const tc_aot_func_entry tc_aot_func_table[];\n\n");

    fprintf(out, "#endif /* TC_AOT_EMBED_H */\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  C 文件生成入口                                                       */
/* ------------------------------------------------------------------ */

int tc_aot_emit_c(FILE *out, const TcTypedProgram *program, const char *source_name,
                  int embed_mode) {
    size_t i = 0;
    size_t di = 0;
    size_t slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);
    TcAotEmitCtx ctx;
    TcDiagnostic diag;
    int rc = 0;

    tc_stmt_index_reset(&ctx.index);
    tc_symbol_name_index_init(&ctx.sym_index);
    ctx.block_path.depth = 0;
    ctx.loops.depth = 0;
    ctx.program = program;
    ctx.current_func_id = -1;
    ctx.current_return_type = TC_VOID;
    ctx.tmp_seq = 0;
    ctx.embed_mode = embed_mode;
    tc_diagnostic_init(&diag);
    if (tc_symbol_name_index_build(&program->symbols, &ctx.sym_index, &diag) != 0) {
        tc_symbol_name_index_free(&ctx.sym_index);
        return -1;
    }

    fprintf(out, "/* Auto-generated by tc-aot. Do not edit. */\n");
    fprintf(out, "#include <stdint.h>\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include \"tc_aot_rt.h\"\n");
    if (embed_mode) {
        fprintf(out, "#include \"tc_aot_embed_rt.h\"\n");
        /* 宏替换：将 tc_aot_abort 重定向为非致命 + return */
        fprintf(out, "#define tc_aot_abort(diag, line) do { \\\n");
        fprintf(out, "    tc_aot_embed_abort(diag, line); \\\n");
        fprintf(out, "    return; \\\n");
        fprintf(out, "} while (0)\n");
    }
    fputc('\n', out);

    {
        size_t count = slot_count > 0 ? slot_count : 1;
        const char *qual = embed_mode ? "" : "static ";
        fprintf(out, "%suint64_t slots[%zu];\n\n", qual, count);
    }

    if (embed_mode) {
        fprintf(out, "TcDiagnostic *tc_aot_cur_diag;\n");
        fprintf(out, "int tc_aot_embed_error_flag;\n\n");
    } else {
        fprintf(out, "static TcDiagnostic *tc_aot_cur_diag;\n\n");
    }

    tc_aot_emit_func_decls(out, program, &ctx);

    /* ── 静态初始化 ── */
    if (embed_mode) {
        fprintf(out, "int tc_aot_init(TcDiagnostic *diag) {\n");
    } else {
        fprintf(out, "static void tc_init_static_vars(TcDiagnostic *diag) {\n");
    }
    fprintf(out, "    tc_aot_cur_diag = diag;\n");
    for (di = 0; di < program->dep_count; di++) {
        if (tc_aot_emit_static_vars_program(out, &program->deps[di], &ctx) != 0) {
            rc = -1;
            break;
        }
    }
    if (rc == 0) {
        if (tc_aot_emit_static_vars_program(out, &program->program, &ctx) != 0) {
            rc = -1;
        }
    }
    if (embed_mode) {
        fprintf(out, "    return 0;\n");
    }
    fprintf(out, "}\n\n");

    /* ── 函数定义 ── */
    if (rc == 0) {
        for (di = 0; di < program->dep_count; di++) {
            if (tc_aot_emit_functions_program(out, &program->deps[di], &ctx) != 0) {
                rc = -1;
                break;
            }
        }
    }
    if (rc == 0) {
        if (tc_aot_emit_functions_program(out, &program->program, &ctx) != 0) {
            rc = -1;
        }
    }

    if (rc != 0) {
        tc_symbol_name_index_free(&ctx.sym_index);
        tc_diagnostic_clear(&diag);
        return rc;
    }

    /* ── 嵌入模式：函数表 + 清理函数 ── */
    if (embed_mode) {
        if (tc_aot_emit_func_table(out, program) != 0) {
            tc_symbol_name_index_free(&ctx.sym_index);
            tc_diagnostic_clear(&diag);
            return -1;
        }
        fprintf(out, "void tc_aot_cleanup(void) {\n");
        fprintf(out, "    tc_aot_memblock_heap_free_all();\n");
        fprintf(out, "    tc_aot_struct_heap_free_all();\n");
        fprintf(out, "}\n");
        tc_symbol_name_index_free(&ctx.sym_index);
        tc_diagnostic_clear(&diag);
        return 0;
    }

    /* ── 独立程序模式：生成 main() ── */
    fprintf(out, "int main(void) {\n");
    fprintf(out, "    TcDiagnostic diag;\n");
    fprintf(out, "    tc_aot_diag_init(&diag);\n");
    fprintf(out, "    tc_aot_cur_diag = &diag;\n");
    fprintf(out, "    if (tc_diagnostic_set_source(&diag, ");
    tc_aot_emit_c_string(out, source_name);
    fprintf(out, ", NULL) != 0) {\n");
    fprintf(out, "        tc_aot_abort(&diag, 0);\n");
    fprintf(out, "    }\n");
    if (slot_count > 0) {
        fprintf(out, "    tc_aot_init_slots(slots, %zu);\n", slot_count);
    }
    fprintf(out, "    tc_init_static_vars(&diag);\n");
    fprintf(out, "    if (diag.domain != TC_DIAG_NONE) tc_aot_abort(&diag, 0);\n");
    fprintf(out, "\n");

    ctx.current_func_id = -1;
    ctx.block_path.depth = 0;
    ctx.loops.depth = 0;
    tc_stmt_index_reset(&ctx.index);

    for (i = 0; i < program->program.count; i++) {
        if (tc_aot_emit_statement_impl(out, &program->program.items[i], &ctx, "    ") != 0) {
            rc = -1;
            break;
        }
    }

    fprintf(out, "\n    tc_aot_memblock_heap_free_all();\n");
    fprintf(out, "    tc_aot_struct_heap_free_all();\n");
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    tc_symbol_name_index_free(&ctx.sym_index);
    tc_diagnostic_clear(&diag);
    return rc;
}
