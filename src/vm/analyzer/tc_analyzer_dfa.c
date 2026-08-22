/*
 * tc_analyzer_dfa.c — 路径敏感初始化数据流（TcInitState / 块路径 / 预扫描）
 */
#include "tc_analyzer_dfa.h"

#include "tc_diagnostic.h"
#include "tc_stmt_index.h"
#include "tc_symbol.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  初始化追踪 & 未初始化变量静态错误                                      */
/* ------------------------------------------------------------------ */

void tc_init_states_reset(TcInitState *states, int num_slots, TcInitState s) {
    int i = 0;

    for (i = 0; i < num_slots; i++) {
        states[i] = s;
    }
}

void tc_init_states_copy(TcInitState *dst, const TcInitState *src, int num_slots) {
    if (num_slots > 0) {
        memcpy(dst, src, (size_t)num_slots * sizeof(TcInitState));
    }
}

void tc_init_states_merge(TcInitState *merged, const TcInitState *a, const TcInitState *b,
                                 int num_slots) {
    int i = 0;

    for (i = 0; i < num_slots; i++) {
        merged[i] = (a[i] == TC_INIT_INIT && b[i] == TC_INIT_INIT) ? TC_INIT_INIT : TC_INIT_UNINIT;
    }
}

void tc_block_path_init(TcBlockPath *bp) {
    bp->path = NULL;
    bp->depth = 0;
    bp->capacity = 0;
}

void tc_block_path_free(TcBlockPath *bp) {
    free(bp->path);
    tc_block_path_init(bp);
}

int tc_block_path_push(TcBlockPath *bp, TcBlockId block_id, TcDiagnostic *diag) {
    if (bp->depth == bp->capacity) {
        int new_cap = bp->capacity == 0 ? 8 : bp->capacity * 2;
        TcBlockId *path =
            (TcBlockId *)realloc(bp->path, (size_t)new_cap * sizeof(TcBlockId));

        if (!path) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        bp->path = path;
        bp->capacity = new_cap;
    }
    bp->path[bp->depth++] = block_id;
    return 0;
}

void tc_block_path_pop(TcBlockPath *bp) {
    if (bp->depth > 0) {
        bp->depth--;
    }
}

TcBlockId tc_block_id_then(int if_stmt_index) {
    TcBlockId id = {if_stmt_index, TC_BLOCK_IF_THEN};

    return id;
}

TcBlockId tc_block_id_else(int if_stmt_index) {
    TcBlockId id = {if_stmt_index, TC_BLOCK_IF_ELSE};

    return id;
}

TcBlockId tc_block_id_while(int while_stmt_index) {
    TcBlockId id = {while_stmt_index, TC_BLOCK_WHILE};

    return id;
}

int tc_paths_equal_prefix(const TcBlockId *a, const TcBlockId *b, int depth) {
    int i = 0;

    for (i = 0; i < depth; i++) {
        if (a[i].owner_stmt_index != b[i].owner_stmt_index || a[i].kind != b[i].kind) {
            return 0;
        }
    }
    return 1;
}

/*
 * @brief 判断变量在 stmt_index 之前是否已被初始化（无 init_states 时的回退）
 */
static int tc_variable_is_initialized_before(const TcInitHistory *hist, const TcSymbol *sym,
                                             size_t before_index) {
    if (sym->initialized) {
        return 1;
    }
    if (hist->last_init_stmt_index != NULL) {
        int last = hist->last_init_stmt_index[sym->slot];
        return last >= 0 && (size_t)last > (size_t)sym->def_stmt_index &&
               (size_t)last < before_index;
    }
    if (hist->program != NULL) {
        size_t i = 0;
        for (i = (size_t)sym->def_stmt_index + 1; i < before_index; i++) {
            const TcStatement *stmt = &hist->program->items[i];
            if (stmt->kind == TC_STMT_ASSIGN && strcmp(stmt->u.assign.name, sym->name) == 0) {
                return 1;
            }
            if (stmt->kind == TC_STMT_READ && strcmp(stmt->u.io_read.name, sym->name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * @brief 读取未初始化变量 → TC_CE_UNINITIALIZED_VARIABLE
 */
int tc_check_operand_init(TcInitHistory *hist, const TcSymbol *sym, size_t stmt_index,
                                 int line, TcDiagnostic *diag) {
    char msg[128];

    if (!hist || !hist->check_init || hist->defer_to_cfg) {
        return 0;
    }
    if (sym->sym_kind == TC_SYM_CONSTANT) {
        return 0;
    }
    if (sym->initialized) {
        return 0;
    }
    if (hist->init_states != NULL) {
        if (sym->slot >= 0 && sym->slot < hist->num_slots &&
            hist->init_states[sym->slot] == TC_INIT_INIT) {
            return 0;
        }
    } else if (tc_variable_is_initialized_before(hist, sym, stmt_index)) {
        return 0;
    }
    (void)snprintf(msg, sizeof(msg), "use of uninitialized variable '%s'", sym->name);
    tc_diagnostic_set(diag, TC_CE_UNINITIALIZED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
    return -1;
}

/*
 * @brief 查找 stmt_index 之前最近定义的同名符号
 */
const TcSymbol *tc_symbol_for_assign_target(const TcSymbolTable *symbols, const char *name,
                                                   int stmt_index) {
    size_t i = 0;
    const TcSymbol *best = NULL;

    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (strcmp(sym->name, name) != 0) {
            continue;
        }
        if (sym->def_stmt_index >= stmt_index) {
            continue;
        }
        if (!best || sym->def_stmt_index > best->def_stmt_index) {
            best = sym;
        }
    }
    return best;
}

static void tc_prescan_init_history_stmt(const TcStatement *stmt, TcAnalyzeCtx *ctx,
                                         const TcSymbolTable *symbols) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        size_t i = 0;

        tc_stmt_index_take(&ctx->index);
        for (i = 0; i < if_stmt->then_count; i++) {
            tc_prescan_init_history_stmt(&if_stmt->then_body[i], ctx, symbols);
        }
        for (i = 0; i < if_stmt->else_count; i++) {
            tc_prescan_init_history_stmt(&if_stmt->else_body[i], ctx, symbols);
        }
        return;
    }

    if (stmt->kind == TC_STMT_WHILE) {
        const TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        size_t i = 0;

        tc_stmt_index_take(&ctx->index);
        for (i = 0; i < while_stmt->body_count; i++) {
            tc_prescan_init_history_stmt(&while_stmt->body[i], ctx, symbols);
        }
        return;
    }

    if (stmt->kind == TC_STMT_ASSIGN) {
        const TcSymbol *sym =
            tc_symbol_for_assign_target(symbols, stmt->u.assign.name, ctx->index.next);
        if (sym && sym->sym_kind == TC_SYM_VARIABLE && sym->slot >= 0 &&
            sym->slot < ctx->num_slots) {
            ctx->last_init[sym->slot] = ctx->index.next;
        }
    } else if (stmt->kind == TC_STMT_READ) {
        const TcSymbol *sym =
            tc_symbol_for_assign_target(symbols, stmt->u.io_read.name, ctx->index.next);
        if (sym && sym->sym_kind == TC_SYM_VARIABLE && sym->slot >= 0 &&
            sym->slot < ctx->num_slots) {
            ctx->last_init[sym->slot] = ctx->index.next;
        }
    }
    tc_stmt_index_take(&ctx->index);
}

void tc_prescan_init_history(TcProgram *program, TcSymbolTable *symbols,
                                    TcAnalyzeCtx *ctx) {
    size_t i = 0;

    tc_stmt_index_reset(&ctx->index);
    for (i = 0; i < program->count; i++) {
        tc_prescan_init_history_stmt(&program->items[i], ctx, symbols);
    }
}
