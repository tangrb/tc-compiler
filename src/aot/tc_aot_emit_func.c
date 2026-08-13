/*
 * tc_aot_emit_func.c — AOT Codegen 函数与 static var 发射
 *
 * 生成 tc_func_<id> 定义、前向声明、static var 拓扑初始化与嵌入函数表。
 */
#include "tc_aot_codegen.h"
#include "tc_aot_codegen_internal.h"

#include <stdio.h>

int tc_aot_emit_static_vars_program(FILE *out, const TcProgram *program, TcAotEmitCtx *ctx) {
    size_t i = 0;

    for (i = 0; i < program->count; i++) {
        if (program->items[i].kind == TC_STMT_STATIC_VAR_DEF) {
            const TcStaticVarDef *sv = &program->items[i].u.static_var_def;
            TcTypeTag rhs_type = sv->type.tag;

            if (sv->type.tag == TC_MEMBLOCK) {
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

int tc_aot_emit_function(FILE *out, const TcFuncDef *func, const TcProgram *module,
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
    ctx->current_return_type = func->return_type.tag;
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

int tc_aot_emit_functions_program(FILE *out, const TcProgram *program, TcAotEmitCtx *ctx) {
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

void tc_aot_emit_func_decls(FILE *out, const TcTypedProgram *program, TcAotEmitCtx *ctx) {
    size_t di = 0;
    size_t i = 0;
    int wrote = 0;
    const char *qual = ctx->embed_mode ? "" : "static ";

    for (di = 0; di < program->dep_count; di++) {
        for (i = 0; i < program->deps[di].count; i++) {
            if (program->deps[di].items[i].kind == TC_STMT_FUNC_DEF) {
                const TcFuncDef *func = &program->deps[di].items[i].u.func_def;

                if (func->return_type.tag != TC_VOID) {
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

            if (func->return_type.tag != TC_VOID) {
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
int tc_aot_emit_func_table(FILE *out, const TcTypedProgram *program) {
    size_t di = 0;
    size_t i = 0;

    fprintf(out, "/* ── 函数表 ── */\n");
    fprintf(out, "const tc_aot_func_entry tc_aot_func_table[] = {\n");

    for (di = 0; di < program->dep_count; di++) {
        for (i = 0; i < program->deps[di].count; i++) {
            if (program->deps[di].items[i].kind == TC_STMT_FUNC_DEF) {
                const TcFuncDef *func = &program->deps[di].items[i].u.func_def;
                if (func->return_type.tag != TC_VOID) {
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
            if (func->return_type.tag != TC_VOID) {
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
