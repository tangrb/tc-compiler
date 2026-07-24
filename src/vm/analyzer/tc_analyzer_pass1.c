/*
 * tc_analyzer_pass1.c — Pass1 符号收集与 slot 分配
 *
 * DFS 遍历语句树，按 stmt_index 顺序登记符号。
 * Phase 3：写入 full_type / memblock_count / struct_id（经 tc_symbol_table_add_ex）。
 */
#include "tc_analyzer_pass1.h"

#include "tc_diagnostic.h"
#include "tc_stmt_index.h"
#include "tc_symbol.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Pass 1 — 符号收集（DFS 递归）                                        */
/* ------------------------------------------------------------------ */

static void tc_mark_block_scope_end(TcSymbolTable *symbols, int owner_stmt_index,
                                    int block_end_index) {
    size_t i = 0;

    for (i = 0; i < symbols->count; i++) {
        TcSymbol *sym = &symbols->symbols[i];

        if (sym->scope_end_stmt_index >= 0) {
            continue;
        }
        if (sym->def_stmt_index > owner_stmt_index && sym->def_stmt_index < block_end_index) {
            sym->scope_end_stmt_index = block_end_index;
        }
    }
}

static uint64_t tc_memblock_n_from_type(const TcType *type) {
    if (!type || type->kind != TC_MEMBLOCK) {
        return 0;
    }
    return type->params.memblock_type.count;
}

static int tc_struct_id_from_type(const TcType *type) {
    if (!type || type->kind != TC_STRUCT) {
        return -1;
    }
    return type->params.struct_type.struct_id;
}

static int tc_pass1_collect_stmt(TcStatement *stmt, TcSymbolTable *symbols, int *next_slot,
                                 TcSlotDomain slot_domain, TcAnalyzeCtx *ctx,
                                 TcDiagnostic *diag);

static int tc_pass1_collect_block(TcStatement *items, size_t count, TcSymbolTable *symbols,
                                  int *next_slot, TcSlotDomain slot_domain, TcAnalyzeCtx *ctx,
                                  TcDiagnostic *diag) {
    size_t i = 0;

    for (i = 0; i < count; i++) {
        if (tc_pass1_collect_stmt(&items[i], symbols, next_slot, slot_domain, ctx, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tc_pass1_collect_stmt(TcStatement *stmt, TcSymbolTable *symbols, int *next_slot,
                                 TcSlotDomain slot_domain, TcAnalyzeCtx *ctx,
                                 TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);
        if (tc_symbol_table_push_scope(symbols) < 0) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, if_stmt->line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_pass1_collect_stmt(&if_stmt->then_body[i], symbols, next_slot, slot_domain,
                                       ctx, diag) != 0) {
                tc_symbol_table_pop_scope(symbols);
                return -1;
            }
        }
        tc_symbol_table_pop_scope(symbols);

        if (if_stmt->else_count > 0) {
            if (tc_symbol_table_push_scope(symbols) < 0) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, if_stmt->line, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_pass1_collect_stmt(&if_stmt->else_body[i], symbols, next_slot, slot_domain,
                                           ctx, diag) != 0) {
                    tc_symbol_table_pop_scope(symbols);
                    return -1;
                }
            }
            tc_symbol_table_pop_scope(symbols);
        }
        tc_mark_block_scope_end(symbols, if_stmt_index, ctx->index.next);
        return 0;
    }

    if (stmt->kind == TC_STMT_WHILE) {
        TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);

        while_stmt->loop_id = ctx->next_loop_id++;
        if (tc_symbol_table_push_scope(symbols) < 0) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, while_stmt->line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_pass1_collect_stmt(&while_stmt->body[i], symbols, next_slot, slot_domain, ctx,
                                       diag) != 0) {
                tc_symbol_table_pop_scope(symbols);
                return -1;
            }
        }
        tc_symbol_table_pop_scope(symbols);
        tc_mark_block_scope_end(symbols, while_stmt_index, ctx->index.next);
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNC_DEF) {
        TcFuncDef *func = &stmt->u.func_def;
        size_t i = 0;

        (void)tc_stmt_index_take(&ctx->index);
        if (tc_symbol_table_push_scope(symbols) < 0) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, func->line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < func->param_count; i++) {
            TcFuncParam *param = &func->params[i];
            char msg[128];

            if (tc_symbol_table_find_in_current_scope(symbols, param->name)) {
                (void)snprintf(msg, sizeof(msg), "duplicate parameter '%s'", param->name);
                tc_diagnostic_set(diag, TC_ERR_DUPLICATE_PARAMETER, func->line,
                                  TC_COLUMN_UNKNOWN, msg);
                tc_symbol_table_pop_scope(symbols);
                return -1;
            }
            /* 全局唯一 slot，避免多域 CFG 位集冲突；域由 slot_domain 区分 */
            if (tc_symbol_table_add_ex(symbols, param->name, param->type.kind, &param->type,
                                       tc_memblock_n_from_type(&param->type),
                                       tc_struct_id_from_type(&param->type), *next_slot,
                                       TC_SLOT_PARAM, func->line, ctx->index.next,
                                       TC_SYM_VARIABLE, 1, diag) != 0) {
                tc_symbol_table_pop_scope(symbols);
                return -1;
            }
            (*next_slot)++;
        }
        if (tc_pass1_collect_block(func->body, func->body_count, symbols, next_slot,
                                   TC_SLOT_LOCAL, ctx, diag) != 0) {
            tc_symbol_table_pop_scope(symbols);
            return -1;
        }
        tc_symbol_table_pop_scope(symbols);
        return 0;
    }

    if (stmt->kind == TC_STMT_VAR_DEF) {
        TcVarDef *var_def = &stmt->u.var_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, var_def->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line,
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (tc_symbol_table_add_ex(symbols, var_def->name, var_def->type, &var_def->full_type,
                                   tc_memblock_n_from_type(&var_def->full_type),
                                   tc_struct_id_from_type(&var_def->full_type), *next_slot,
                                   slot_domain, var_def->line, tc_stmt_index_take(&ctx->index),
                                   TC_SYM_VARIABLE, 1, diag) != 0) {
            return -1;
        }
        var_def->binding.resolved = 1;
        var_def->binding.slot = *next_slot;
        var_def->binding.is_const = 0;
        var_def->binding.type = var_def->type;
        var_def->binding.const_bits = 0;
        (*next_slot)++;
        return 0;
    }

    if (stmt->kind == TC_STMT_CONST_DEF) {
        const TcConstDef *const_def = &stmt->u.const_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, const_def->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate definition of '%s'", const_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, const_def->line,
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (tc_symbol_table_add_ex(symbols, const_def->name, const_def->type,
                                   &const_def->full_type,
                                   tc_memblock_n_from_type(&const_def->full_type),
                                   tc_struct_id_from_type(&const_def->full_type), -1,
                                   slot_domain, const_def->line, tc_stmt_index_take(&ctx->index),
                                   TC_SYM_CONSTANT, 1, diag) != 0) {
            return -1;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_STATIC_VAR_DEF) {
        TcStaticVarDef *sv = &stmt->u.static_var_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, sv->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate definition of '%s'", sv->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, sv->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (tc_symbol_table_add_ex(symbols, sv->name, sv->type.kind, &sv->type,
                                   tc_memblock_n_from_type(&sv->type),
                                   tc_struct_id_from_type(&sv->type), *next_slot, TC_SLOT_STATIC,
                                   sv->line, tc_stmt_index_take(&ctx->index), TC_SYM_VARIABLE, 1,
                                   diag) != 0) {
            return -1;
        }
        sv->static_slot = *next_slot;
        (*next_slot)++;
        return 0;
    }

    if (stmt->kind == TC_STMT_STATIC_LET_DEF) {
        const TcStaticLetDef *sl = &stmt->u.static_let_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, sl->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate definition of '%s'", sl->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, sl->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (tc_symbol_table_add_ex(symbols, sl->name, sl->type.kind, &sl->type,
                                   tc_memblock_n_from_type(&sl->type),
                                   tc_struct_id_from_type(&sl->type), -1, TC_SLOT_STATIC,
                                   sl->line, tc_stmt_index_take(&ctx->index), TC_SYM_CONSTANT, 1,
                                   diag) != 0) {
            return -1;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        const TcLabelDef *label_def = &stmt->u.label_def;
        int stmt_index = tc_stmt_index_take(&ctx->index);

        return tc_symbol_table_add_label(symbols, label_def->name, stmt_index, label_def->line,
                                         NULL, tc_symbol_table_current_scope(symbols), diag);
    }

    if (stmt->kind == TC_STMT_GOTO) {
        tc_stmt_index_take(&ctx->index);
        return 0;
    }

    tc_stmt_index_take(&ctx->index);
    return 0;
}

int tc_pass1_collect_symbols(TcProgram *program, TcSymbolTable *symbols,
                                    TcDiagnostic *diag) {
    TcAnalyzeCtx ctx;
    size_t i = 0;
    int next_slot = (int)tc_symbol_table_runtime_slot_count(symbols);

    ctx.program = program;
    ctx.last_init = NULL;
    ctx.next_loop_id = 0;
    tc_stmt_index_reset(&ctx.index);

    for (i = 0; i < program->count; i++) {
        if (tc_pass1_collect_stmt(&program->items[i], symbols, &next_slot, TC_SLOT_TOPLEVEL,
                                   &ctx, diag) != 0) {
            return -1;
        }
    }
    return 0;
}
