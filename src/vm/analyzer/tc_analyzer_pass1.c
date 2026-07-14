/*
 * tc_analyzer_pass1.c — Pass1 符号收集与 slot 分配
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

static void tc_mark_if_scope_end(TcSymbolTable *symbols, int if_stmt_index, int if_end_index) {
    size_t i = 0;

    for (i = 0; i < symbols->count; i++) {
        TcSymbol *sym = &symbols->symbols[i];

        if (sym->scope_end_stmt_index >= 0) {
            continue;
        }
        if (sym->def_stmt_index > if_stmt_index && sym->def_stmt_index < if_end_index) {
            sym->scope_end_stmt_index = if_end_index;
        }
    }
}

static int tc_pass1_collect_stmt(const TcStatement *stmt, TcSymbolTable *symbols, int *next_slot,
                                 TcAnalyzeCtx *ctx, TcDiagnostic *diag) {
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
            if (tc_pass1_collect_stmt(&if_stmt->then_body[i], symbols, next_slot, ctx, diag) !=
                0) {
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
                if (tc_pass1_collect_stmt(&if_stmt->else_body[i], symbols, next_slot, ctx,
                                           diag) != 0) {
                    tc_symbol_table_pop_scope(symbols);
                    return -1;
                }
            }
            tc_symbol_table_pop_scope(symbols);
        }
        tc_mark_if_scope_end(symbols, if_stmt_index, ctx->index.next);
        return 0;
    }

    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, var_def->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line,
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (tc_symbol_table_add(symbols, var_def->name, var_def->type, *next_slot, var_def->line,
                                tc_stmt_index_take(&ctx->index), TC_SYM_VARIABLE, var_def->has_rhs,
                                diag) != 0) {
            return -1;
        }
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
        if (tc_symbol_table_add(symbols, const_def->name, const_def->type, *next_slot,
                                const_def->line, tc_stmt_index_take(&ctx->index), TC_SYM_CONSTANT, 1,
                                diag) != 0) {
            return -1;
        }
        (*next_slot)++;
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        const TcLabelDef *label_def = &stmt->u.label_def;
        int stmt_index = tc_stmt_index_take(&ctx->index);

        /* 标签仅注册到标签表，不分配 slot；随 pop_scope 清理块内标签 */
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
    int next_slot = 0;

    ctx.program = program;
    ctx.last_init = NULL;
    tc_stmt_index_reset(&ctx.index);

    for (i = 0; i < program->count; i++) {
        if (tc_pass1_collect_stmt(&program->items[i], symbols, &next_slot, &ctx, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

