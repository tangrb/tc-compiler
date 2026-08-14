/*
 * tc_analyze_6a.c — goto/label 控制流上下文检查
 *
 * 6a 子阶段：单次遍历 AST，收集全部 label 定义并校验 goto/label 的
 * 词法祖先（func/while）合法性。建立标签表供 6c 查询。
 */
#include "tc_analyze_6a.h"
#include "tc_analyzer_pass2.h"
#include "tc_types.h"
#include "tc_symbol.h"
#include "tc_diagnostic.h"

#include <stddef.h>
#include <stdio.h>

static int tc_6a_collect_labels_stmt(TcStatement *stmt, TcSymbolTable *symbols,
                                     TcAnalyzeCtx *ctx, TcDiagnostic *diag)
{
    if (stmt->kind == TC_STMT_IF) {
        TcIfStmt *if_stmt = &stmt->u.if_stmt;
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);

        if (tc_block_path_push(&ctx->block_path, tc_block_id_then(if_stmt_index), diag) != 0) {
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_6a_collect_labels_stmt(&if_stmt->then_body[i], symbols, ctx, diag) != 0) {
                return -1;
            }
        }
        tc_block_path_pop(&ctx->block_path);

        if (if_stmt->else_count > 0) {
            if (tc_block_path_push(&ctx->block_path, tc_block_id_else(if_stmt_index), diag) != 0) {
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_6a_collect_labels_stmt(&if_stmt->else_body[i], symbols, ctx, diag) != 0) {
                    return -1;
                }
            }
            tc_block_path_pop(&ctx->block_path);
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_WHILE) {
        TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);

        if (tc_block_path_push(&ctx->block_path, tc_block_id_while(while_stmt_index), diag) != 0) {
            return -1;
        }
        ctx->loop_depth++;
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_6a_collect_labels_stmt(&while_stmt->body[i], symbols, ctx, diag) != 0) {
                ctx->loop_depth--;
                tc_block_path_pop(&ctx->block_path);
                return -1;
            }
        }
        ctx->loop_depth--;
        tc_block_path_pop(&ctx->block_path);
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        const TcLabelDef *label_def = &stmt->u.label_def;
        int stmt_index = tc_stmt_index_take(&ctx->index);

        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_CE_LABEL_OUTSIDE_FUNCTION, label_def->line,
                              TC_COLUMN_UNKNOWN, "label is only allowed inside a function");
            return -1;
        }
        if (ctx->loop_depth > 0) {
            tc_diagnostic_set(diag, TC_CE_LABEL_INSIDE_LOOP, label_def->line,
                              TC_COLUMN_UNKNOWN, "label is not allowed inside while");
            return -1;
        }

        return tc_symbol_table_add_label(symbols, label_def->name, ctx->current_func_id,
                                         stmt_index, label_def->line, ctx->block_path.path,
                                         ctx->block_path.depth, diag);
    }

    if (stmt->kind == TC_STMT_GOTO) {
        tc_stmt_index_take(&ctx->index);
        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_CE_GOTO_OUTSIDE_FUNCTION, stmt->u.goto_stmt.line,
                              TC_COLUMN_UNKNOWN, "goto is only allowed inside a function");
            return -1;
        }
        if (ctx->loop_depth > 0) {
            tc_diagnostic_set(diag, TC_CE_GOTO_INSIDE_LOOP, stmt->u.goto_stmt.line,
                              TC_COLUMN_UNKNOWN, "goto is not allowed inside while");
            return -1;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNC_DEF) {
        TcFuncDef *func = &stmt->u.func_def;
        size_t i = 0;
        int saved_func_id = ctx->current_func_id;

        (void)tc_stmt_index_take(&ctx->index);
        ctx->func_depth++;
        ctx->current_func_id = func->func_id;
        for (i = 0; i < func->body_count; i++) {
            if (tc_6a_collect_labels_stmt(&func->body[i], symbols, ctx, diag) != 0) {
                ctx->func_depth--;
                ctx->current_func_id = saved_func_id;
                return -1;
            }
        }
        ctx->func_depth--;
        ctx->current_func_id = saved_func_id;
        return 0;
    }

    tc_stmt_index_take(&ctx->index);
    return 0;
}

int tc_analyze_6a_collect_labels(TcProgram *program, TcSymbolTable *symbols,
                                 TcAnalyzeCtx *ctx, TcDiagnostic *diag)
{
    size_t i = 0;

    tc_symbol_table_clear_labels(symbols);
    tc_stmt_index_reset(&ctx->index);
    ctx->block_path.depth = 0;
    ctx->loop_depth = 0;
    ctx->func_depth = 0;
    ctx->current_func_id = -1;
    for (i = 0; i < program->count; i++) {
        if (tc_6a_collect_labels_stmt(&program->items[i], symbols, ctx, diag) != 0) {
            return -1;
        }
    }
    return 0;
}
