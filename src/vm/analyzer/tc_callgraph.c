/*
 * tc_callgraph.c — 函数调用图构建与递归环确定性检查（Phase 4 / 阶段 12）
 *
 * 扫描入口模块函数体（及顶层 funcall 记录）建立 func_id 有向边，
 * 用 Tarjan 求 SCC；自环或 size>1 的 SCC 视为递归，按编译器标准 §8.9 选首边报错。
 */
#include "tc_callgraph.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TC_CALLGRAPH_NO_CALLER (-1) /* 顶层语句中的调用：无调用方函数 */

typedef struct {
    int from; /* 调用方 func_id；顶层为 NO_CALLER */
    int to;   /* 被调方 func_id */
    int line; /* 调用点源行（报错定位） */
} TcCallEdge;

typedef struct {
    TcCallEdge *items;
    size_t count;
    size_t capacity;
} TcCallEdgeList;

typedef struct {
    int *items;
    size_t count;
    size_t capacity;
} TcIntList;

typedef struct {
    int *ids;
    int count;
} TcScc;

typedef struct {
    TcScc *items;
    size_t count;
    size_t capacity;
} TcSccList;

static int tc_callgraph_push_edge(TcCallEdgeList *edges, int from, int to, int line,
                                  TcDiagnostic *diag) {
    TcCallEdge *items;

    if (edges->count == edges->capacity) {
        size_t new_cap = edges->capacity == 0 ? 8 : edges->capacity * 2;
        items = (TcCallEdge *)realloc(edges->items, new_cap * sizeof(TcCallEdge));
        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        edges->items = items;
        edges->capacity = new_cap;
    }
    edges->items[edges->count].from = from;
    edges->items[edges->count].to = to;
    edges->items[edges->count].line = line;
    edges->count++;
    return 0;
}

static const TcFuncSignature *tc_callgraph_sig_for_id(const TcFuncSignatureList *sigs,
                                                      int func_id) {
    size_t i = 0;

    if (!sigs || func_id < 0) {
        return NULL;
    }
    for (i = 0; i < sigs->count; i++) {
        if (sigs->items[i].func_id == func_id) {
            return &sigs->items[i];
        }
    }
    return NULL;
}

static int tc_callgraph_def_line(const TcFuncSignatureList *sigs, int func_id) {
    const TcFuncSignature *sig = tc_callgraph_sig_for_id(sigs, func_id);
    if (!sig) {
        return INT_MAX;
    }
    return sig->def_line;
}

/**
 * 解析调用目标并追加边。解析失败静默跳过：
 * 调用合法性已由 Pass2 保证；此处只关心可解析边的成环。
 */
static int tc_callgraph_record_call(TcCallEdgeList *edges, const TcFuncCheckEnv *env, int caller,
                                    int is_self, const char *qualifier, const char *member_name,
                                    const char *bare_target, int line, TcDiagnostic *diag) {
    TcDiagnostic resolve_diag;
    const TcFuncSignature *sig = NULL;
    int callee_id = 0;

    tc_diagnostic_init(&resolve_diag);
    if (tc_func_resolve_call_target(env, is_self, qualifier, member_name, bare_target, line, &sig,
                                    &resolve_diag) != 0) {
        tc_diagnostic_clear(&resolve_diag);
        return 0;
    }
    tc_diagnostic_clear(&resolve_diag);
    if (!sig) {
        return 0;
    }
    callee_id = sig->func_id;
    if (callee_id < 0) {
        return 0;
    }
    return tc_callgraph_push_edge(edges, caller, callee_id, line, diag);
}

static int tc_callgraph_record_funcall_expr_at(TcCallEdgeList *edges, const TcFuncCheckEnv *env,
                                               int caller, const TcRhs *rhs, int line,
                                               TcDiagnostic *diag) {
    if (!rhs || rhs->kind != TC_RHS_FUNCALL_EXPR) {
        return 0;
    }
    return tc_callgraph_record_call(edges, env, caller, rhs->u.funcall_expr.is_self,
                                    rhs->u.funcall_expr.qualifier, rhs->u.funcall_expr.member_name,
                                    rhs->u.funcall_expr.target, line, diag);
}

static int tc_callgraph_scan_block(const TcStatement *items, size_t count,
                                   const TcFuncCheckEnv *env, int caller,
                                   TcCallEdgeList *edges, TcDiagnostic *diag);

static int tc_callgraph_scan_stmt(const TcStatement *stmt, const TcFuncCheckEnv *env, int caller,
                                  TcCallEdgeList *edges, TcDiagnostic *diag) {
    if (!stmt) {
        return 0;
    }

    /* 收集语句/RHS 中的 funcall；进入 FUNC_DEF 时切换 caller=该函数 func_id */
    switch (stmt->kind) {
    case TC_STMT_FUNCALL:
        return tc_callgraph_record_call(
            edges, env, caller, stmt->u.funcall_stmt.is_self, stmt->u.funcall_stmt.qualifier,
            stmt->u.funcall_stmt.member_name, stmt->u.funcall_stmt.target,
            stmt->u.funcall_stmt.line, diag);
    case TC_STMT_VAR_DEF:
        return tc_callgraph_record_funcall_expr_at(edges, env, caller, &stmt->u.var_def.rhs,
                                                   stmt->u.var_def.line, diag);
    case TC_STMT_ASSIGN:
        return tc_callgraph_record_funcall_expr_at(edges, env, caller, &stmt->u.assign.rhs,
                                                   stmt->u.assign.line, diag);
    case TC_STMT_FIELD_ASSIGN:
        return tc_callgraph_record_funcall_expr_at(edges, env, caller, &stmt->u.field_assign.rhs,
                                                   stmt->u.field_assign.line, diag);
    case TC_STMT_IF:
        if (tc_callgraph_scan_block(stmt->u.if_stmt.then_body, stmt->u.if_stmt.then_count, env,
                                    caller, edges, diag) != 0) {
            return -1;
        }
        return tc_callgraph_scan_block(stmt->u.if_stmt.else_body, stmt->u.if_stmt.else_count, env,
                                       caller, edges, diag);
    case TC_STMT_WHILE:
        return tc_callgraph_scan_block(stmt->u.while_stmt.body, stmt->u.while_stmt.body_count, env,
                                       caller, edges, diag);
    case TC_STMT_FUNC_DEF: {
        const TcFuncDef *fn = &stmt->u.func_def;
        TcFuncCheckEnv body_env = *env;

        body_env.current_func = tc_callgraph_sig_for_id(env->sigs, fn->func_id);
        if (fn->func_id < 0) {
            return 0;
        }
        return tc_callgraph_scan_block(fn->body, fn->body_count, &body_env, fn->func_id, edges,
                                       diag);
    }
    default:
        return 0;
    }
}

static int tc_callgraph_scan_block(const TcStatement *items, size_t count,
                                   const TcFuncCheckEnv *env, int caller,
                                   TcCallEdgeList *edges, TcDiagnostic *diag) {
    size_t i = 0;

    for (i = 0; i < count; i++) {
        if (tc_callgraph_scan_stmt(&items[i], env, caller, edges, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tc_callgraph_collect_edges(const TcFuncCheckEnv *env, TcCallEdgeList *edges,
                                      TcDiagnostic *diag) {
    const TcProgram *program = NULL;
    size_t i = 0;

    /* 仅扫描入口模块 AST；依赖库内调用不单独建边（跨模块递归属后续） */
    if (!env || !env->prog) {
        return 0;
    }
    program = &env->prog->program;
    for (i = 0; i < program->count; i++) {
        if (tc_callgraph_scan_stmt(&program->items[i], env, TC_CALLGRAPH_NO_CALLER, edges,
                                   diag) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tc_callgraph_push_int(TcIntList *list, int value, TcDiagnostic *diag) {
    int *items;

    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        items = (int *)realloc(list->items, new_cap * sizeof(int));
        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        list->items = items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = value;
    return 0;
}

static int tc_callgraph_build_adjacency(const TcCallEdgeList *edges, int func_count,
                                        TcIntList *adj, TcDiagnostic *diag) {
    size_t i = 0;

    for (i = 0; i < edges->count; i++) {
        const TcCallEdge *edge = &edges->items[i];
        if (edge->from < 0 || edge->from >= func_count || edge->to < 0 || edge->to >= func_count) {
            continue;
        }
        if (tc_callgraph_push_int(&adj[edge->from], edge->to, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tc_callgraph_scc_push(TcSccList *sccs, const int *nodes, int count,
                                 TcDiagnostic *diag) {
    TcScc *items;

    if (sccs->count == sccs->capacity) {
        size_t new_cap = sccs->capacity == 0 ? 4 : sccs->capacity * 2;
        items = (TcScc *)realloc(sccs->items, new_cap * sizeof(TcScc));
        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        sccs->items = items;
        sccs->capacity = new_cap;
    }
    sccs->items[sccs->count].ids = (int *)malloc((size_t)count * sizeof(int));
    if (!sccs->items[sccs->count].ids) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    memcpy(sccs->items[sccs->count].ids, nodes, (size_t)count * sizeof(int));
    sccs->items[sccs->count].count = count;
    sccs->count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Tarjan 强连通分量                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    int *index;
    int *lowlink;
    int *on_stack;
    int *stack;
    int stack_top;
    int next_index;
    int *scc_map; /* 顶点 → SCC 下标 */
    TcSccList *sccs;
    TcIntList *adj;
    TcDiagnostic *diag;
} TcTarjanCtx;

static void tc_callgraph_tarjan_push(TcTarjanCtx *ctx, int v) {
    ctx->stack[ctx->stack_top++] = v;
    ctx->on_stack[v] = 1;
}

static int tc_callgraph_tarjan_pop(TcTarjanCtx *ctx, int *out) {
    if (ctx->stack_top <= 0) {
        return -1;
    }
    ctx->stack_top--;
    *out = ctx->stack[ctx->stack_top];
    ctx->on_stack[*out] = 0;
    return 0;
}

static int tc_callgraph_tarjan_strongconnect(TcTarjanCtx *ctx, int v) {
    int i = 0;
    int w = 0;
    int stack_count = 0;
    int component_cap = 4;
    int *component = (int *)malloc((size_t)component_cap * sizeof(int));
    int popped = 0;

    /* 标准 Tarjan：dfs 编号 + lowlink；弹出栈直至 v 形成 SCC */
    if (!component) {
        tc_diagnostic_set(ctx->diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }

    ctx->index[v] = ctx->next_index;
    ctx->lowlink[v] = ctx->next_index;
    ctx->next_index++;
    tc_callgraph_tarjan_push(ctx, v);

    for (i = 0; i < (int)ctx->adj[v].count; i++) {
        w = ctx->adj[v].items[i];
        if (ctx->index[w] == -1) {
            if (tc_callgraph_tarjan_strongconnect(ctx, w) != 0) {
                free(component);
                return -1;
            }
            if (ctx->lowlink[w] < ctx->lowlink[v]) {
                ctx->lowlink[v] = ctx->lowlink[w];
            }
        } else if (ctx->on_stack[w] && ctx->index[w] < ctx->lowlink[v]) {
            ctx->lowlink[v] = ctx->index[w];
        }
    }

    if (ctx->lowlink[v] == ctx->index[v]) {
        stack_count = 0;
        do {
            int *grown = NULL;

            if (tc_callgraph_tarjan_pop(ctx, &popped) != 0) {
                free(component);
                return -1;
            }
            if (stack_count == component_cap) {
                component_cap *= 2;
                grown = (int *)realloc(component, (size_t)component_cap * sizeof(int));
                if (!grown) {
                    free(component);
                    tc_diagnostic_set(ctx->diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                      "memory allocation failed");
                    return -1;
                }
                component = grown;
            }
            component[stack_count++] = popped;
        } while (popped != v);
        if (tc_callgraph_scc_push(ctx->sccs, component, stack_count, ctx->diag) != 0) {
            free(component);
            return -1;
        }
        for (i = 0; i < stack_count; i++) {
            ctx->scc_map[component[i]] = (int)ctx->sccs->count - 1;
        }
    }
    free(component);
    return 0;
}

static int tc_callgraph_tarjan(int func_count, TcIntList *adj, int *scc_map, TcSccList *sccs,
                               TcDiagnostic *diag) {
    TcTarjanCtx ctx;
    int v = 0;
    int rc = 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.adj = adj;
    ctx.sccs = sccs;
    ctx.diag = diag;
    ctx.index = (int *)malloc((size_t)func_count * sizeof(int));
    ctx.lowlink = (int *)malloc((size_t)func_count * sizeof(int));
    ctx.on_stack = (int *)calloc((size_t)func_count, sizeof(int));
    ctx.stack = (int *)malloc((size_t)func_count * sizeof(int));
    ctx.scc_map = scc_map;
    if (!ctx.index || !ctx.lowlink || !ctx.on_stack || !ctx.stack) {
        free(ctx.index);
        free(ctx.lowlink);
        free(ctx.on_stack);
        free(ctx.stack);
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    for (v = 0; v < func_count; v++) {
        ctx.index[v] = -1;
        ctx.scc_map[v] = -1;
    }

    for (v = 0; v < func_count; v++) {
        if (ctx.index[v] == -1) {
            if (tc_callgraph_tarjan_strongconnect(&ctx, v) != 0) {
                rc = -1;
                break;
            }
        }
    }

    free(ctx.index);
    free(ctx.lowlink);
    free(ctx.on_stack);
    free(ctx.stack);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  递归判定与确定性报错选边                                            */
/* ------------------------------------------------------------------ */

/** 单点 SCC 仅当存在自环边才算递归 */
static int tc_callgraph_scc_has_self_loop(const TcScc *scc, const TcIntList *adj) {
    int i = 0;
    int v = 0;

    if (scc->count != 1) {
        return 0;
    }
    v = scc->ids[0];
    for (i = 0; i < (int)adj[v].count; i++) {
        if (adj[v].items[i] == v) {
            return 1;
        }
    }
    return 0;
}

static int tc_callgraph_scc_is_recursive(const TcScc *scc, const TcIntList *adj) {
    /* size>1：互递归；size==1：仅自调用算递归 */
    if (scc->count > 1) {
        return 1;
    }
    return tc_callgraph_scc_has_self_loop(scc, adj);
}

/** SCC 内「定义行最小，同则 func_id 最小」的代表函数（用于 SCC 排序键） */
static int tc_callgraph_scc_min_def(const TcFuncSignatureList *sigs, const TcScc *scc,
                                    int *out_line, int *out_func_id) {
    int i = 0;
    int best_line = INT_MAX;
    int best_id = INT_MAX;

    for (i = 0; i < scc->count; i++) {
        int fid = scc->ids[i];
        int line = tc_callgraph_def_line(sigs, fid);
        if (line < best_line || (line == best_line && fid < best_id)) {
            best_line = line;
            best_id = fid;
        }
    }
    *out_line = best_line;
    *out_func_id = best_id;
    return 0;
}

static int tc_callgraph_pick_recursive_scc(const TcFuncSignatureList *sigs, const TcSccList *sccs,
                                           const TcIntList *adj, int *out_scc_index) {
    size_t i = 0;
    int best_scc = -1;
    int best_line = INT_MAX;
    int best_func_id = INT_MAX;

    /* 多个递归 SCC 时选「代表函数定义行最小」者，保证报错稳定 */
    for (i = 0; i < sccs->count; i++) {
        const TcScc *scc = &sccs->items[i];
        int line = 0;
        int func_id = 0;

        if (!tc_callgraph_scc_is_recursive(scc, adj)) {
            continue;
        }
        tc_callgraph_scc_min_def(sigs, scc, &line, &func_id);
        if (line < best_line || (line == best_line && func_id < best_func_id)) {
            best_line = line;
            best_func_id = func_id;
            best_scc = (int)i;
        }
    }
    *out_scc_index = best_scc;
    return best_scc >= 0 ? 1 : 0;
}

static int tc_callgraph_pick_report_edge(const TcCallEdgeList *edges, int scc_index,
                                         const int *scc_map, int *out_line) {
    size_t i = 0;
    int best_line = INT_MAX;

    /* 在选定 SCC 内部边中取调用行号最小者作为诊断位置 */
    for (i = 0; i < edges->count; i++) {
        const TcCallEdge *edge = &edges->items[i];
        if (edge->from < 0) {
            continue;
        }
        if (scc_map[edge->from] != scc_index || scc_map[edge->to] != scc_index) {
            continue;
        }
        if (edge->line < best_line) {
            best_line = edge->line;
        }
    }
    if (best_line == INT_MAX) {
        return 0;
    }
    *out_line = best_line;
    return 1;
}

static void tc_callgraph_free_int_lists(TcIntList *adj, int func_count) {
    int i = 0;
    if (!adj) {
        return;
    }
    for (i = 0; i < func_count; i++) {
        free(adj[i].items);
    }
    free(adj);
}

static void tc_callgraph_free_sccs(TcSccList *sccs) {
    size_t i = 0;
    if (!sccs) {
        return;
    }
    for (i = 0; i < sccs->count; i++) {
        free(sccs->items[i].ids);
    }
    free(sccs->items);
    sccs->items = NULL;
    sccs->count = 0;
    sccs->capacity = 0;
}

int tc_callgraph_check(const TcFuncCheckEnv *env, TcDiagnostic *diag) {
    TcCallEdgeList edges;
    TcSccList sccs;
    int func_count = 0;
    TcIntList *adj = NULL;
    int *scc_map = NULL;
    int scc_index = 0;
    int report_line = 0;
    int has_cycle = 0;
    int rc = 0;

    /* 流程：收集边 → 邻接表 → Tarjan → 选递归 SCC → 选报错边 */
    if (!env || !env->sigs) {
        return 0;
    }
    func_count = (int)env->sigs->count;
    if (func_count <= 0) {
        return 0;
    }

    memset(&edges, 0, sizeof(edges));
    memset(&sccs, 0, sizeof(sccs));

    if (tc_callgraph_collect_edges(env, &edges, diag) != 0) {
        rc = -1;
        goto done;
    }

    adj = (TcIntList *)calloc((size_t)func_count, sizeof(TcIntList));
    scc_map = (int *)malloc((size_t)func_count * sizeof(int));
    if (!adj || !scc_map) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        rc = -1;
        goto done;
    }

    if (tc_callgraph_build_adjacency(&edges, func_count, adj, diag) != 0) {
        rc = -1;
        goto done;
    }
    if (tc_callgraph_tarjan(func_count, adj, scc_map, &sccs, diag) != 0) {
        rc = -1;
        goto done;
    }

    has_cycle = tc_callgraph_pick_recursive_scc(env->sigs, &sccs, adj, &scc_index);
    if (!has_cycle) {
        rc = 0;
        goto done;
    }
    if (!tc_callgraph_pick_report_edge(&edges, scc_index, scc_map, &report_line)) {
        rc = 0;
        goto done;
    }

    tc_diagnostic_set(diag, TC_CE_RECURSION, report_line, TC_COLUMN_UNKNOWN,
                      "recursive function call");
    rc = -1;

done:
    free(edges.items);
    tc_callgraph_free_sccs(&sccs);
    tc_callgraph_free_int_lists(adj, func_count);
    free(scc_map);
    return rc;
}
