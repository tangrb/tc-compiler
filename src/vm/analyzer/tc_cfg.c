/* tc_cfg.c — 显式 CFG 构建与确定初始化不动点分析
 *
 * 线性扫描语句序列建图；支持 if/while/goto/短路边。
 * tc_cfg_build_all 为顶层与每个函数体单独建域；
 * definite init 在各域独立运行，并报告不可达与缺 return。
 */
#include "tc_cfg.h"

#include "tc_const_eval.h"
#include "tc_stmt_index.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int loop_id;
    int condition_node;
    int exit_node;
} TcCfgLoopFrame;

typedef struct {
    int node;
    int target_stmt_index;
} TcCfgPendingGoto;

typedef struct {
    TcCfg *cfg;
    const TcSymbolTable *symbols;
    TcDiagnostic *diag;
    TcStmtIndexCursor index;
    int scope_depth;
    int *stmt_nodes;
    size_t stmt_node_count;
    TcCfgLoopFrame *loops;
    size_t loop_count;
    size_t loop_capacity;
    TcCfgPendingGoto *gotos;
    size_t goto_count;
    size_t goto_capacity;
    int *pending_returns;
    size_t return_count;
    size_t return_capacity;
} TcCfgBuildCtx;

void tc_cfg_init(TcCfg *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->entry_id = -1;
    cfg->exit_id = -1;
    cfg->func_id = -1;
}

void tc_cfg_free(TcCfg *cfg) {
    size_t i = 0;

    if (!cfg) {
        return;
    }
    for (i = 0; i < cfg->node_count; i++) {
        free(cfg->nodes[i].read_slots);
    }
    free(cfg->nodes);
    free(cfg->edges);
    free(cfg->entry_init_slots);
    tc_cfg_init(cfg);
}

static int tc_cfg_oom(TcDiagnostic *diag, int line) {
    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                      "memory allocation failed");
    return -1;
}

static int tc_cfg_add_node(TcCfgBuildCtx *ctx, TcCfgNodeKind kind, int stmt_index, int line,
                           TcStmtKind stmt_kind) {
    TcCfg *cfg = ctx->cfg;
    TcCfgNode *node = NULL;

    if (cfg->node_count == cfg->node_capacity) {
        size_t capacity = cfg->node_capacity == 0 ? 16 : cfg->node_capacity * 2;
        TcCfgNode *nodes =
            (TcCfgNode *)realloc(cfg->nodes, capacity * sizeof(TcCfgNode));

        if (!nodes) {
            return tc_cfg_oom(ctx->diag, line);
        }
        cfg->nodes = nodes;
        cfg->node_capacity = capacity;
    }
    node = &cfg->nodes[cfg->node_count];
    memset(node, 0, sizeof(*node));
    node->id = (int)cfg->node_count;
    node->kind = kind;
    node->stmt_index = stmt_index;
    node->line = line;
    node->scope_id = ctx->scope_depth;
    node->stmt_kind = stmt_kind;
    node->constant_condition = -1;
    node->write_slot = -1;
    cfg->node_count++;
    if (stmt_index >= 0 && (size_t)stmt_index < ctx->stmt_node_count) {
        ctx->stmt_nodes[stmt_index] = node->id;
    }
    return node->id;
}

static int tc_cfg_add_edge(TcCfgBuildCtx *ctx, int from, int to, TcCfgEdgeKind kind) {
    TcCfg *cfg = ctx->cfg;
    TcCfgEdge *edge = NULL;

    if (from < 0 || to < 0) {
        return 0;
    }
    if (cfg->edge_count == cfg->edge_capacity) {
        size_t capacity = cfg->edge_capacity == 0 ? 24 : cfg->edge_capacity * 2;
        TcCfgEdge *edges =
            (TcCfgEdge *)realloc(cfg->edges, capacity * sizeof(TcCfgEdge));

        if (!edges) {
            return tc_cfg_oom(ctx->diag, 0);
        }
        cfg->edges = edges;
        cfg->edge_capacity = capacity;
    }
    edge = &cfg->edges[cfg->edge_count++];
    edge->from = from;
    edge->to = to;
    edge->kind = kind;
    edge->enabled = 1;
    return 0;
}

static const TcSymbol *tc_cfg_find_def(const TcSymbolTable *symbols, const char *name,
                                       int stmt_index) {
    size_t i = 0;

    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (sym->def_stmt_index == stmt_index && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static const TcSymbol *tc_cfg_find_visible(const TcSymbolTable *symbols, const char *name,
                                           int stmt_index) {
    return tc_symbol_table_find_visible(symbols, name, stmt_index, NULL);
}

static int tc_cfg_node_add_read(TcCfgBuildCtx *ctx, int node_id, const char *name,
                                int stmt_index) {
    const TcSymbol *sym = tc_cfg_find_visible(ctx->symbols, name, stmt_index);
    TcCfgNode *node = &ctx->cfg->nodes[node_id];
    size_t i = 0;

    if (!sym || sym->sym_kind != TC_SYM_VARIABLE) {
        return 0;
    }
    for (i = 0; i < node->read_count; i++) {
        if (node->read_slots[i] == sym->slot) {
            return 0;
        }
    }
    if (node->read_count == node->read_capacity) {
        size_t capacity = node->read_capacity == 0 ? 2 : node->read_capacity * 2;
        int *slots = (int *)realloc(node->read_slots, capacity * sizeof(int));

        if (!slots) {
            return tc_cfg_oom(ctx->diag, node->line);
        }
        node->read_slots = slots;
        node->read_capacity = capacity;
    }
    node->read_slots[node->read_count++] = sym->slot;
    return 0;
}

static int tc_cfg_node_add_read_slot(TcCfgBuildCtx *ctx, int node_id, int slot) {
    TcCfgNode *node = &ctx->cfg->nodes[node_id];
    size_t i = 0;

    if (slot < 0) {
        return 0;
    }
    for (i = 0; i < node->read_count; i++) {
        if (node->read_slots[i] == slot) {
            return 0;
        }
    }
    if (node->read_count == node->read_capacity) {
        size_t capacity = node->read_capacity == 0 ? 2 : node->read_capacity * 2;
        int *slots = (int *)realloc(node->read_slots, capacity * sizeof(int));

        if (!slots) {
            return tc_cfg_oom(ctx->diag, node->line);
        }
        node->read_slots = slots;
        node->read_capacity = capacity;
    }
    node->read_slots[node->read_count++] = slot;
    return 0;
}

static int tc_cfg_add_operand_read(TcCfgBuildCtx *ctx, int node_id, const TcOperand *operand,
                                   int stmt_index) {
    if (operand->kind == TC_OPERAND_VAR) {
        return tc_cfg_node_add_read(ctx, node_id, operand->u.name, stmt_index);
    }
    if (operand->kind == TC_OPERAND_FIELD_READ) {
        if (operand->u.field_read.resolved.resolved &&
            operand->u.field_read.resolved.base_slot >= 0) {
            return tc_cfg_node_add_read_slot(ctx, node_id,
                                             operand->u.field_read.resolved.base_slot);
        }
        if (operand->u.field_read.base) {
            return tc_cfg_node_add_read(ctx, node_id, operand->u.field_read.base, stmt_index);
        }
    }
    return 0;
}

static int tc_cfg_add_rhs_reads(TcCfgBuildCtx *ctx, int node_id, const TcRhs *rhs,
                                int stmt_index) {
    switch (rhs->kind) {
    case TC_RHS_ARITH:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.arith.lhs, stmt_index) != 0 ||
                       tc_cfg_add_operand_read(ctx, node_id, &rhs->u.arith.rhs, stmt_index) != 0
                   ? -1
                   : 0;
    case TC_RHS_UNARY:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.unary.operand, stmt_index);
    case TC_RHS_COMPARE:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.compare.lhs, stmt_index) != 0 ||
                       tc_cfg_add_operand_read(ctx, node_id, &rhs->u.compare.rhs, stmt_index) != 0
                   ? -1
                   : 0;
    case TC_RHS_LOGIC_BIN:
    {
        TcStaticBoolResult lhs_value = TC_STATIC_BOOL_UNKNOWN;

        if (tc_cfg_add_operand_read(ctx, node_id, &rhs->u.logic_bin.lhs, stmt_index) != 0) {
            return -1;
        }
        tc_try_eval_static_bool_operand(&rhs->u.logic_bin.lhs, &lhs_value);
        if ((rhs->u.logic_bin.op == TC_LOGIC_AND && lhs_value == TC_STATIC_BOOL_FALSE) ||
            (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs_value == TC_STATIC_BOOL_TRUE)) {
            return 0;
        }
        /* xor 不短路：始终读取 rhs */
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.logic_bin.rhs, stmt_index);
    }
    case TC_RHS_LOGIC_UN:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.logic_un.operand, stmt_index);
    case TC_RHS_BITWISE_BIN:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.bitwise_bin.lhs, stmt_index) != 0 ||
                       tc_cfg_add_operand_read(ctx, node_id, &rhs->u.bitwise_bin.rhs, stmt_index) != 0
                   ? -1
                   : 0;
    case TC_RHS_BITWISE_UN:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.bitwise_un.operand, stmt_index);
    case TC_RHS_SHIFT:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.shift.value, stmt_index) != 0 ||
                       tc_cfg_add_operand_read(ctx, node_id, &rhs->u.shift.count, stmt_index) != 0
                   ? -1
                   : 0;
    case TC_RHS_CAST:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.cast.source, stmt_index);
    case TC_RHS_CONST_CAST:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.const_cast.source, stmt_index);
    case TC_RHS_FLOAT_ARITH:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.float_arith.lhs, stmt_index) != 0 ||
                       tc_cfg_add_operand_read(ctx, node_id, &rhs->u.float_arith.rhs, stmt_index) != 0
                   ? -1
                   : 0;
    case TC_RHS_FLOAT_UNARY:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.float_unary.operand, stmt_index);
    case TC_RHS_FLOAT_COMPARE:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.float_compare.lhs, stmt_index) != 0 ||
                       tc_cfg_add_operand_read(ctx, node_id, &rhs->u.float_compare.rhs, stmt_index) != 0
                   ? -1
                   : 0;
    case TC_RHS_BITCAST:
        return tc_cfg_add_operand_read(ctx, node_id, &rhs->u.bitcast.source, stmt_index);
    case TC_RHS_CONST_REF:
        return tc_cfg_node_add_read(ctx, node_id, rhs->u.const_ref.name, stmt_index);
    case TC_RHS_LIT:
        return 0;
    case TC_RHS_MEMBLOCK_LOAD:
    case TC_RHS_MEMBLOCK_CONSTRUCTOR:
    case TC_RHS_MEMBLOCK_COUNT:
    case TC_RHS_STRUCT_CONSTRUCTOR:
    case TC_RHS_PTR_LOAD:
    case TC_RHS_PTR_ADDRESS:
    case TC_RHS_PTR_ADD:
    case TC_RHS_PTR_SUB:
    case TC_RHS_PTR_EQ:
    case TC_RHS_PTR_NE:
    case TC_RHS_PTR_LT:
    case TC_RHS_PTR_LE:
    case TC_RHS_PTR_GT:
    case TC_RHS_PTR_GE:
    case TC_RHS_PTR_SIZE:
    case TC_RHS_FUNCALL_EXPR:
    case TC_RHS_SELF_MEMBER:
        /* 复合/调用 RHS：读集不在此展开；操作数名由 Pass2 检查。
         * CFG 读边用于标量条件与简单赋值的短路剪枝。 */
        return 0;
    case TC_RHS_FIELD_READ:
        if (rhs->u.field_read.resolved.resolved && rhs->u.field_read.resolved.base_slot >= 0) {
            return tc_cfg_node_add_read_slot(ctx, node_id, rhs->u.field_read.resolved.base_slot);
        }
        if (rhs->u.field_read.base) {
            return tc_cfg_node_add_read(ctx, node_id, rhs->u.field_read.base, stmt_index);
        }
        return 0;
    }
    return 0;
}

static int tc_cfg_push_loop(TcCfgBuildCtx *ctx, int loop_id, int condition, int exit_node) {
    if (ctx->loop_count == ctx->loop_capacity) {
        size_t capacity = ctx->loop_capacity == 0 ? 4 : ctx->loop_capacity * 2;
        TcCfgLoopFrame *loops =
            (TcCfgLoopFrame *)realloc(ctx->loops, capacity * sizeof(TcCfgLoopFrame));

        if (!loops) {
            return tc_cfg_oom(ctx->diag, 0);
        }
        ctx->loops = loops;
        ctx->loop_capacity = capacity;
    }
    ctx->loops[ctx->loop_count].loop_id = loop_id;
    ctx->loops[ctx->loop_count].condition_node = condition;
    ctx->loops[ctx->loop_count].exit_node = exit_node;
    ctx->loop_count++;
    return 0;
}

static const TcCfgLoopFrame *tc_cfg_find_loop(const TcCfgBuildCtx *ctx, int loop_id) {
    size_t i = ctx->loop_count;

    while (i > 0) {
        i--;
        if (ctx->loops[i].loop_id == loop_id) {
            return &ctx->loops[i];
        }
    }
    return NULL;
}

static int tc_cfg_add_pending_goto(TcCfgBuildCtx *ctx, int node, int target_stmt_index) {
    if (ctx->goto_count == ctx->goto_capacity) {
        size_t capacity = ctx->goto_capacity == 0 ? 4 : ctx->goto_capacity * 2;
        TcCfgPendingGoto *gotos =
            (TcCfgPendingGoto *)realloc(ctx->gotos, capacity * sizeof(TcCfgPendingGoto));

        if (!gotos) {
            return tc_cfg_oom(ctx->diag, 0);
        }
        ctx->gotos = gotos;
        ctx->goto_capacity = capacity;
    }
    ctx->gotos[ctx->goto_count].node = node;
    ctx->gotos[ctx->goto_count].target_stmt_index = target_stmt_index;
    ctx->goto_count++;
    return 0;
}

static int tc_cfg_add_pending_return(TcCfgBuildCtx *ctx, int node) {
    if (ctx->return_count == ctx->return_capacity) {
        size_t capacity = ctx->return_capacity == 0 ? 4 : ctx->return_capacity * 2;
        int *rets = (int *)realloc(ctx->pending_returns, capacity * sizeof(int));

        if (!rets) {
            return tc_cfg_oom(ctx->diag, 0);
        }
        ctx->pending_returns = rets;
        ctx->return_capacity = capacity;
    }
    ctx->pending_returns[ctx->return_count++] = node;
    return 0;
}

static int tc_cfg_build_block(TcCfgBuildCtx *ctx, const TcStatement *items, size_t count,
                              int predecessor, TcCfgEdgeKind incoming);

static int tc_cfg_build_stmt(TcCfgBuildCtx *ctx, const TcStatement *stmt, int predecessor,
                             TcCfgEdgeKind incoming) {
    int stmt_index = tc_stmt_index_take(&ctx->index);
    int node = -1;

    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        int merge = -1;
        int then_end = -1;
        int else_end = -1;
        TcStaticBoolResult value = TC_STATIC_BOOL_UNKNOWN;

        node = tc_cfg_add_node(ctx, TC_CFG_BRANCH, stmt_index, if_stmt->line, stmt->kind);
        if (node < 0 || tc_cfg_add_edge(ctx, predecessor, node, incoming) != 0 ||
            tc_cfg_add_rhs_reads(ctx, node, &if_stmt->condition, stmt_index) != 0) {
            return -2;
        }
        if (tc_try_eval_static_bool(&if_stmt->condition, if_stmt->line, &value,
                                    ctx->diag) != 0) {
            return -2;
        }
        if (value != TC_STATIC_BOOL_UNKNOWN) {
            ctx->cfg->nodes[node].constant_condition = (int)value;
        }
        merge = tc_cfg_add_node(ctx, TC_CFG_MERGE, -1, if_stmt->line, stmt->kind);
        if (merge < 0) {
            return -2;
        }
        ctx->scope_depth++;
        then_end = tc_cfg_build_block(ctx, if_stmt->then_body, if_stmt->then_count, node,
                                      TC_CFG_TRUE);
        ctx->scope_depth--;
        if (then_end == -2 || tc_cfg_add_edge(ctx, then_end, merge, TC_CFG_FALLTHROUGH) != 0) {
            return -2;
        }
        if (if_stmt->else_count > 0) {
            ctx->scope_depth++;
            else_end = tc_cfg_build_block(ctx, if_stmt->else_body, if_stmt->else_count, node,
                                          TC_CFG_FALSE);
            ctx->scope_depth--;
            if (else_end == -2 || tc_cfg_add_edge(ctx, else_end, merge, TC_CFG_FALLTHROUGH) != 0) {
                return -2;
            }
        } else if (tc_cfg_add_edge(ctx, node, merge, TC_CFG_FALSE) != 0) {
            return -2;
        }
        return merge;
    }

    if (stmt->kind == TC_STMT_WHILE) {
        const TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        int loop_exit = -1;
        int body_end = -1;
        TcStaticBoolResult value = TC_STATIC_BOOL_UNKNOWN;

        node = tc_cfg_add_node(ctx, TC_CFG_LOOP_CONDITION, stmt_index, while_stmt->line,
                               stmt->kind);
        if (node < 0 || tc_cfg_add_edge(ctx, predecessor, node, incoming) != 0 ||
            tc_cfg_add_rhs_reads(ctx, node, &while_stmt->condition, stmt_index) != 0) {
            return -2;
        }
        if (tc_try_eval_static_bool(&while_stmt->condition, while_stmt->line, &value,
                                    ctx->diag) != 0) {
            return -2;
        }
        if (value != TC_STATIC_BOOL_UNKNOWN) {
            ctx->cfg->nodes[node].constant_condition = (int)value;
        }
        loop_exit =
            tc_cfg_add_node(ctx, TC_CFG_LOOP_EXIT, -1, while_stmt->line, stmt->kind);
        if (loop_exit < 0 || tc_cfg_add_edge(ctx, node, loop_exit, TC_CFG_FALSE) != 0 ||
            tc_cfg_push_loop(ctx, while_stmt->loop_id, node, loop_exit) != 0) {
            return -2;
        }
        ctx->scope_depth++;
        body_end = tc_cfg_build_block(ctx, while_stmt->body, while_stmt->body_count, node,
                                      TC_CFG_TRUE);
        ctx->scope_depth--;
        ctx->loop_count--;
        if (body_end == -2 || tc_cfg_add_edge(ctx, body_end, node, TC_CFG_FALLTHROUGH) != 0) {
            return -2;
        }
        return loop_exit;
    }

    node = tc_cfg_add_node(ctx, TC_CFG_STATEMENT, stmt_index,
                           stmt->kind == TC_STMT_VAR_DEF       ? stmt->u.var_def.line
                           : stmt->kind == TC_STMT_CONST_DEF   ? stmt->u.const_def.line
                           : stmt->kind == TC_STMT_ASSIGN      ? stmt->u.assign.line
                           : stmt->kind == TC_STMT_READ        ? stmt->u.io_read.line
                           : stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN
                               ? stmt->u.io_write.line
                           : stmt->kind == TC_STMT_LABEL_DEF ? stmt->u.label_def.line
                           : stmt->kind == TC_STMT_GOTO       ? stmt->u.goto_stmt.line
                           : stmt->kind == TC_STMT_BREAK      ? stmt->u.break_stmt.line
                           : stmt->kind == TC_STMT_CONTINUE   ? stmt->u.continue_stmt.line
                           : stmt->kind == TC_STMT_IMPORT     ? stmt->u.import_stmt.line
                           : stmt->kind == TC_STMT_STRUCT_DEF ? stmt->u.struct_def.line
                           : stmt->kind == TC_STMT_FUNC_DEF   ? stmt->u.func_def.line
                           : stmt->kind == TC_STMT_STATIC_VAR_DEF ? stmt->u.static_var_def.line
                           : stmt->kind == TC_STMT_STATIC_LET_DEF ? stmt->u.static_let_def.line
                                                              : 0,
                           stmt->kind);
    if (node < 0 || tc_cfg_add_edge(ctx, predecessor, node, incoming) != 0) {
        return -2;
    }

    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcSymbol *sym = tc_cfg_find_def(ctx->symbols, stmt->u.var_def.name, stmt_index);

        if (tc_cfg_add_rhs_reads(ctx, node, &stmt->u.var_def.rhs, stmt_index) != 0) {
            return -2;
        }
        if (sym) {
            ctx->cfg->nodes[node].write_slot = sym->slot;
        }
    } else if (stmt->kind == TC_STMT_CONST_DEF) {
        if (tc_cfg_add_rhs_reads(ctx, node, &stmt->u.const_def.rhs, stmt_index) != 0) {
            return -2;
        }
    } else if (stmt->kind == TC_STMT_ASSIGN) {
        const TcSymbol *sym =
            tc_cfg_find_visible(ctx->symbols, stmt->u.assign.name, stmt_index);

        if (tc_cfg_add_rhs_reads(ctx, node, &stmt->u.assign.rhs, stmt_index) != 0) {
            return -2;
        }
        if (sym) {
            ctx->cfg->nodes[node].write_slot = sym->slot;
        }
    } else if (stmt->kind == TC_STMT_READ) {
        const TcSymbol *sym =
            tc_cfg_find_visible(ctx->symbols, stmt->u.io_read.name, stmt_index);

        if (sym) {
            ctx->cfg->nodes[node].write_slot = sym->slot;
        }
    } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        if (tc_cfg_add_operand_read(ctx, node, &stmt->u.io_write.operand, stmt_index) != 0) {
            return -2;
        }
    } else if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        const TcLoopControlStmt *control = stmt->kind == TC_STMT_BREAK ? &stmt->u.break_stmt
                                                                       : &stmt->u.continue_stmt;
        const TcCfgLoopFrame *loop = tc_cfg_find_loop(ctx, control->loop_id);

        if (!loop || tc_cfg_add_edge(ctx, node,
                                     stmt->kind == TC_STMT_BREAK ? loop->exit_node
                                                                 : loop->condition_node,
                                     stmt->kind == TC_STMT_BREAK ? TC_CFG_BREAK
                                                                 : TC_CFG_CONTINUE) != 0) {
            return -2;
        }
        return -1;
    } else if (stmt->kind == TC_STMT_GOTO) {
        if (!stmt->u.goto_stmt.resolved ||
            tc_cfg_add_pending_goto(ctx, node,
                                    stmt->u.goto_stmt.resolved_target_stmt_index) != 0) {
            tc_diagnostic_set(ctx->diag, TC_CE_SYNTAX, stmt->u.goto_stmt.line,
                              TC_COLUMN_UNKNOWN, "internal unresolved goto");
            return -2;
        }
        return -1;
    } else if (stmt->kind == TC_STMT_RETURN) {
        if (tc_cfg_add_pending_return(ctx, node) != 0) {
            return -2;
        }
        return -1;
    } else if (stmt->kind == TC_STMT_FUNC_DEF) {
        /* 顶层域：不展开函数体；推进 stmt_index 与 Pass1 对齐 */
        int body_span =
            tc_stmt_block_index_span(stmt->u.func_def.body, stmt->u.func_def.body_count);
        ctx->index.next += body_span;
        return node;
    }
    return node;
}

static int tc_cfg_build_block(TcCfgBuildCtx *ctx, const TcStatement *items, size_t count,
                              int predecessor, TcCfgEdgeKind incoming) {
    size_t i = 0;
    int current = predecessor;
    TcCfgEdgeKind edge_kind = incoming;

    for (i = 0; i < count; i++) {
        current = tc_cfg_build_stmt(ctx, &items[i], current, edge_kind);
        if (current == -2) {
            return -2;
        }
        edge_kind = TC_CFG_FALLTHROUGH;
    }
    return current;
}

static void tc_cfg_prune_constant_edges(TcCfg *cfg) {
    size_t i = 0;

    for (i = 0; i < cfg->edge_count; i++) {
        TcCfgEdge *edge = &cfg->edges[i];
        const TcCfgNode *from = &cfg->nodes[edge->from];

        if (from->constant_condition < 0) {
            continue;
        }
        if ((edge->kind == TC_CFG_TRUE && from->constant_condition == 0) ||
            (edge->kind == TC_CFG_FALSE && from->constant_condition == 1)) {
            edge->enabled = 0;
        }
    }
}

static int tc_cfg_mark_reachable(TcCfg *cfg, TcDiagnostic *diag) {
    int *queue = NULL;
    size_t head = 0;
    size_t tail = 0;
    size_t i = 0;

    if (cfg->node_count == 0) {
        return 0;
    }
    queue = (int *)malloc(cfg->node_count * sizeof(int));
    if (!queue) {
        return tc_cfg_oom(diag, 0);
    }
    cfg->nodes[cfg->entry_id].reachable = 1;
    queue[tail++] = cfg->entry_id;
    while (head < tail) {
        int from = queue[head++];

        for (i = 0; i < cfg->edge_count; i++) {
            TcCfgEdge *edge = &cfg->edges[i];

            if (!edge->enabled || edge->from != from || cfg->nodes[edge->to].reachable) {
                continue;
            }
            cfg->nodes[edge->to].reachable = 1;
            queue[tail++] = edge->to;
        }
    }
    free(queue);
    return 0;
}

int tc_cfg_build(const TcProgram *program, const TcSymbolTable *symbols, TcCfg *out,
                 TcDiagnostic *diag) {
    TcCfgBuildCtx ctx;
    int last = -1;
    int stmt_count = tc_stmt_block_index_span(program->items, program->count);
    size_t i = 0;

    tc_cfg_init(out);
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = out;
    ctx.symbols = symbols;
    ctx.diag = diag;
    ctx.stmt_node_count = stmt_count > 0 ? (size_t)stmt_count : 1;
    ctx.stmt_nodes = (int *)malloc(ctx.stmt_node_count * sizeof(int));
    if (!ctx.stmt_nodes) {
        return tc_cfg_oom(diag, 0);
    }
    for (i = 0; i < ctx.stmt_node_count; i++) {
        ctx.stmt_nodes[i] = -1;
    }
    tc_stmt_index_reset(&ctx.index);
    out->slot_count = tc_symbol_table_runtime_slot_count(symbols);
    out->entry_id = tc_cfg_add_node(&ctx, TC_CFG_ENTRY, -1, 0, TC_STMT_VAR_DEF);
    if (out->entry_id < 0) {
        goto fail;
    }
    last = tc_cfg_build_block(&ctx, program->items, program->count, out->entry_id,
                              TC_CFG_FALLTHROUGH);
    if (last == -2) {
        goto fail;
    }
    out->exit_id = tc_cfg_add_node(&ctx, TC_CFG_EXIT, -1, 0, TC_STMT_VAR_DEF);
    if (out->exit_id < 0 || tc_cfg_add_edge(&ctx, last, out->exit_id, TC_CFG_FALLTHROUGH) != 0) {
        goto fail;
    }
    for (i = 0; i < ctx.goto_count; i++) {
        int target_index = ctx.gotos[i].target_stmt_index;
        int target = target_index >= 0 && (size_t)target_index < ctx.stmt_node_count
                         ? ctx.stmt_nodes[target_index]
                         : -1;

        if (target < 0 || tc_cfg_add_edge(&ctx, ctx.gotos[i].node, target, TC_CFG_GOTO) != 0) {
            tc_diagnostic_set(diag, TC_CE_SYNTAX, 0, TC_COLUMN_UNKNOWN,
                              "internal invalid goto target");
            goto fail;
        }
    }
    for (i = 0; i < ctx.return_count; i++) {
        if (tc_cfg_add_edge(&ctx, ctx.pending_returns[i], out->exit_id, TC_CFG_RETURN) != 0) {
            goto fail;
        }
    }
    tc_cfg_prune_constant_edges(out);
    if (tc_cfg_mark_reachable(out, diag) != 0) {
        goto fail;
    }
    free(ctx.stmt_nodes);
    free(ctx.loops);
    free(ctx.gotos);
    free(ctx.pending_returns);
    return 0;

fail:
    free(ctx.stmt_nodes);
    free(ctx.loops);
    free(ctx.gotos);
    free(ctx.pending_returns);
    tc_cfg_free(out);
    return -1;
}

static void tc_bitset_fill(uint64_t *bits, size_t words, size_t slot_count) {
    size_t i = 0;

    for (i = 0; i < words; i++) {
        bits[i] = UINT64_MAX;
    }
    if (words > 0 && slot_count % 64u != 0u) {
        bits[words - 1] = (UINT64_C(1) << (slot_count % 64u)) - UINT64_C(1);
    }
}

int tc_analyze_definite_init(const TcCfg *cfg, size_t slot_count, TcDiagnostic *diag) {
    size_t words = (slot_count + 63u) / 64u;
    uint64_t *in_sets = NULL;
    uint64_t *out_sets = NULL;
    uint64_t *next = NULL;
    int changed = 1;
    size_t i = 0;

    if (words == 0 || cfg->node_count == 0) {
        return 0;
    }
    in_sets = (uint64_t *)calloc(cfg->node_count * words, sizeof(uint64_t));
    out_sets = (uint64_t *)calloc(cfg->node_count * words, sizeof(uint64_t));
    next = (uint64_t *)malloc(words * sizeof(uint64_t));
    if (!in_sets || !out_sets || !next) {
        free(in_sets);
        free(out_sets);
        free(next);
        return tc_cfg_oom(diag, 0);
    }
    for (i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i].reachable && (int)i != cfg->entry_id) {
            tc_bitset_fill(&in_sets[i * words], words, slot_count);
            tc_bitset_fill(&out_sets[i * words], words, slot_count);
        }
    }
    /* 函数域：入口 OUT 含全部形参已初始化 */
    if (cfg->entry_id >= 0 && cfg->entry_init_slots && cfg->entry_init_count > 0) {
        size_t s = 0;

        for (s = 0; s < cfg->entry_init_count; s++) {
            int slot = cfg->entry_init_slots[s];

            if (slot >= 0 && (size_t)slot < slot_count) {
                out_sets[(size_t)cfg->entry_id * words + (size_t)slot / 64u] |=
                    UINT64_C(1) << ((size_t)slot % 64u);
            }
        }
    }

    while (changed) {
        changed = 0;
        for (i = 0; i < cfg->node_count; i++) {
            const TcCfgNode *node = &cfg->nodes[i];
            size_t e = 0;
            int have_predecessor = 0;

            if (!node->reachable || (int)i == cfg->entry_id) {
                continue;
            }
            tc_bitset_fill(next, words, slot_count);
            for (e = 0; e < cfg->edge_count; e++) {
                const TcCfgEdge *edge = &cfg->edges[e];
                size_t w = 0;

                if (!edge->enabled || edge->to != (int)i ||
                    !cfg->nodes[edge->from].reachable) {
                    continue;
                }
                if (!have_predecessor) {
                    memcpy(next, &out_sets[(size_t)edge->from * words], words * sizeof(uint64_t));
                    have_predecessor = 1;
                } else {
                    for (w = 0; w < words; w++) {
                        next[w] &= out_sets[(size_t)edge->from * words + w];
                    }
                }
            }
            if (!have_predecessor) {
                memset(next, 0, words * sizeof(uint64_t));
            }
            if (memcmp(&in_sets[i * words], next, words * sizeof(uint64_t)) != 0) {
                memcpy(&in_sets[i * words], next, words * sizeof(uint64_t));
                changed = 1;
            }
            if (node->write_slot >= 0 && (size_t)node->write_slot < slot_count) {
                next[(size_t)node->write_slot / 64u] |=
                    UINT64_C(1) << ((size_t)node->write_slot % 64u);
            }
            if (memcmp(&out_sets[i * words], next, words * sizeof(uint64_t)) != 0) {
                memcpy(&out_sets[i * words], next, words * sizeof(uint64_t));
                changed = 1;
            }
        }
    }

    for (i = 0; i < cfg->node_count; i++) {
        const TcCfgNode *node = &cfg->nodes[i];
        size_t r = 0;

        if (!node->reachable) {
            continue;
        }
        for (r = 0; r < node->read_count; r++) {
            size_t slot = (size_t)node->read_slots[r];

            if (slot < slot_count &&
                (in_sets[i * words + slot / 64u] & (UINT64_C(1) << (slot % 64u))) == 0) {
                tc_diagnostic_set(diag, TC_CE_UNINITIALIZED_VARIABLE, node->line,
                                  TC_COLUMN_UNKNOWN, "use of uninitialized variable");
                free(in_sets);
                free(out_sets);
                free(next);
                return -1;
            }
        }
    }

    free(in_sets);
    free(out_sets);
    free(next);
    return 0;
}

void tc_cfg_set_init(TcCfgSet *set) {
    memset(set, 0, sizeof(*set));
    tc_cfg_init(&set->toplevel);
}

void tc_cfg_set_free(TcCfgSet *set) {
    size_t i = 0;

    if (!set) {
        return;
    }
    tc_cfg_free(&set->toplevel);
    for (i = 0; i < set->func_count; i++) {
        tc_cfg_free(&set->funcs[i]);
    }
    free(set->funcs);
    set->funcs = NULL;
    set->func_count = 0;
    set->func_capacity = 0;
}

static int tc_cfg_build_items(const TcStatement *items, size_t count, int start_index,
                              const TcSymbolTable *symbols, TcCfg *out, TcDiagnostic *diag) {
    TcCfgBuildCtx ctx;
    int last = -1;
    int stmt_count = start_index + tc_stmt_block_index_span(items, count);
    size_t i = 0;

    tc_cfg_init(out);
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = out;
    ctx.symbols = symbols;
    ctx.diag = diag;
    ctx.stmt_node_count = stmt_count > 0 ? (size_t)stmt_count : 1;
    ctx.stmt_nodes = (int *)malloc(ctx.stmt_node_count * sizeof(int));
    if (!ctx.stmt_nodes) {
        return tc_cfg_oom(diag, 0);
    }
    for (i = 0; i < ctx.stmt_node_count; i++) {
        ctx.stmt_nodes[i] = -1;
    }
    tc_stmt_index_reset(&ctx.index);
    ctx.index.next = start_index;
    out->slot_count = tc_symbol_table_runtime_slot_count(symbols);
    out->entry_id = tc_cfg_add_node(&ctx, TC_CFG_ENTRY, -1, 0, TC_STMT_VAR_DEF);
    if (out->entry_id < 0) {
        goto fail;
    }
    last = tc_cfg_build_block(&ctx, items, count, out->entry_id, TC_CFG_FALLTHROUGH);
    if (last == -2) {
        goto fail;
    }
    out->exit_id = tc_cfg_add_node(&ctx, TC_CFG_EXIT, -1, 0, TC_STMT_VAR_DEF);
    if (out->exit_id < 0 || tc_cfg_add_edge(&ctx, last, out->exit_id, TC_CFG_FALLTHROUGH) != 0) {
        goto fail;
    }
    for (i = 0; i < ctx.goto_count; i++) {
        int target_index = ctx.gotos[i].target_stmt_index;
        int target = target_index >= 0 && (size_t)target_index < ctx.stmt_node_count
                         ? ctx.stmt_nodes[target_index]
                         : -1;

        if (target < 0 || tc_cfg_add_edge(&ctx, ctx.gotos[i].node, target, TC_CFG_GOTO) != 0) {
            tc_diagnostic_set(diag, TC_CE_SYNTAX, 0, TC_COLUMN_UNKNOWN,
                              "internal invalid goto target");
            goto fail;
        }
    }
    for (i = 0; i < ctx.return_count; i++) {
        if (tc_cfg_add_edge(&ctx, ctx.pending_returns[i], out->exit_id, TC_CFG_RETURN) != 0) {
            goto fail;
        }
    }
    tc_cfg_prune_constant_edges(out);
    if (tc_cfg_mark_reachable(out, diag) != 0) {
        goto fail;
    }
    free(ctx.stmt_nodes);
    free(ctx.loops);
    free(ctx.gotos);
    free(ctx.pending_returns);
    return 0;

fail:
    free(ctx.stmt_nodes);
    free(ctx.loops);
    free(ctx.gotos);
    free(ctx.pending_returns);
    tc_cfg_free(out);
    return -1;
}

static int tc_cfg_collect_param_slots(const TcSymbolTable *symbols, int body_start,
                                      int **out_slots, size_t *out_count,
                                      TcDiagnostic *diag) {
    size_t i = 0;
    size_t count = 0;
    size_t cap = 0;
    int *slots = NULL;

    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (sym->slot_domain != TC_SLOT_PARAM || sym->slot < 0) {
            continue;
        }
        if (sym->def_stmt_index != body_start) {
            continue;
        }
        if (count == cap) {
            size_t new_cap = cap == 0 ? 4 : cap * 2;
            int *grown = (int *)realloc(slots, new_cap * sizeof(int));

            if (!grown) {
                free(slots);
                return tc_cfg_oom(diag, 0);
            }
            slots = grown;
            cap = new_cap;
        }
        slots[count++] = sym->slot;
    }
    *out_slots = slots;
    *out_count = count;
    return 0;
}

int tc_cfg_build_all(const TcProgram *program, const TcSymbolTable *symbols, TcCfgSet *out,
                     TcDiagnostic *diag) {
    size_t i = 0;
    int cursor = 0;

    tc_cfg_set_init(out);
    if (tc_cfg_build(program, symbols, &out->toplevel, diag) != 0) {
        tc_cfg_set_free(out);
        return -1;
    }
    out->toplevel.is_function_domain = 0;
    out->toplevel.func_id = -1;

    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];

        if (stmt->kind != TC_STMT_FUNC_DEF) {
            cursor += tc_stmt_subtree_index_count(stmt);
            continue;
        }
        {
            const TcFuncDef *func = &stmt->u.func_def;
            int func_index = cursor;
            int body_start = func_index + 1;
            TcCfg *fcfg = NULL;

            if (out->func_count == out->func_capacity) {
                size_t cap = out->func_capacity == 0 ? 4 : out->func_capacity * 2;
                TcCfg *grown = (TcCfg *)realloc(out->funcs, cap * sizeof(TcCfg));

                if (!grown) {
                    tc_cfg_set_free(out);
                    return tc_cfg_oom(diag, func->line);
                }
                out->funcs = grown;
                out->func_capacity = cap;
            }
            fcfg = &out->funcs[out->func_count];
            tc_cfg_init(fcfg);
            if (tc_cfg_build_items(func->body, func->body_count, body_start, symbols, fcfg,
                                   diag) != 0) {
                tc_cfg_set_free(out);
                return -1;
            }
            fcfg->is_function_domain = 1;
            fcfg->func_id = func->func_id >= 0 ? func->func_id : (int)out->func_count;
            if (tc_cfg_collect_param_slots(symbols, body_start, &fcfg->entry_init_slots,
                                           &fcfg->entry_init_count, diag) != 0) {
                tc_cfg_set_free(out);
                return -1;
            }
            out->func_count++;
            cursor += tc_stmt_subtree_index_count(stmt);
        }
    }
    return 0;
}

static int tc_cfg_diagnose_unreachable(const TcCfg *cfg, TcDiagnostic *diag) {
    int *queue = NULL;
    int *structural = NULL;
    size_t head = 0;
    size_t tail = 0;
    size_t i = 0;

    if (cfg->node_count == 0 || cfg->entry_id < 0) {
        return 0;
    }
    queue = (int *)malloc(cfg->node_count * sizeof(int));
    structural = (int *)calloc(cfg->node_count, sizeof(int));
    if (!queue || !structural) {
        free(queue);
        free(structural);
        return tc_cfg_oom(diag, 0);
    }
    /* 结构可达：忽略常量边剪枝，仅捕捉 return/goto 后无标签汇合的死码 */
    structural[cfg->entry_id] = 1;
    queue[tail++] = cfg->entry_id;
    while (head < tail) {
        int from = queue[head++];

        for (i = 0; i < cfg->edge_count; i++) {
            const TcCfgEdge *edge = &cfg->edges[i];

            if (edge->from != from || structural[edge->to]) {
                continue;
            }
            structural[edge->to] = 1;
            queue[tail++] = edge->to;
        }
    }
    for (i = 0; i < cfg->node_count; i++) {
        const TcCfgNode *node = &cfg->nodes[i];

        if (node->kind != TC_CFG_STATEMENT || node->stmt_index < 0) {
            continue;
        }
        if (!structural[i]) {
            tc_diagnostic_set(diag, TC_CE_UNREACHABLE_STATEMENT, node->line, TC_COLUMN_UNKNOWN,
                              "unreachable statement");
            free(queue);
            free(structural);
            return -1;
        }
    }
    free(queue);
    free(structural);
    return 0;
}

static int tc_cfg_diagnose_missing_return(const TcCfg *cfg, const TcFuncDef *func,
                                          TcDiagnostic *diag) {
    size_t e = 0;

        if (!func || func->return_type.tag == TC_VOID) {
        return 0;
    }
    if (cfg->exit_id < 0 || !cfg->nodes[cfg->exit_id].reachable) {
        return 0;
    }
    for (e = 0; e < cfg->edge_count; e++) {
        const TcCfgEdge *edge = &cfg->edges[e];

        if (!edge->enabled || edge->to != cfg->exit_id) {
            continue;
        }
        if (edge->kind == TC_CFG_FALLTHROUGH && cfg->nodes[edge->from].reachable) {
            tc_diagnostic_set(diag, TC_CE_MISSING_RETURN, func->line, TC_COLUMN_UNKNOWN,
                              "missing return on reachable path");
            return -1;
        }
    }
    return 0;
}

int tc_analyze_definite_init_all(const TcCfgSet *set, const TcProgram *program,
                                 size_t slot_count, TcDiagnostic *diag) {
    size_t i = 0;
    size_t fi = 0;

    if (tc_analyze_definite_init(&set->toplevel, slot_count, diag) != 0) {
        return -1;
    }
    if (tc_cfg_diagnose_unreachable(&set->toplevel, diag) != 0) {
        return -1;
    }
    for (i = 0; i < set->func_count; i++) {
        const TcFuncDef *func = NULL;

        if (tc_analyze_definite_init(&set->funcs[i], slot_count, diag) != 0) {
            return -1;
        }
        if (tc_cfg_diagnose_unreachable(&set->funcs[i], diag) != 0) {
            return -1;
        }
        /* 按源序匹配第 fi 个 FUNC_DEF */
        while (fi < program->count && program->items[fi].kind != TC_STMT_FUNC_DEF) {
            fi++;
        }
        if (fi < program->count) {
            func = &program->items[fi].u.func_def;
            if (tc_cfg_diagnose_missing_return(&set->funcs[i], func, diag) != 0) {
                return -1;
            }
            fi++;
        }
    }
    return 0;
}

