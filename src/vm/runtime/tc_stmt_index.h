/*
 * tc_stmt_index.h — 语句树扁平 stmt_index 分配与跳过（header-only）
 *
 * Analyzer / Executor / AOT 共用同一套 DFS 先序编号规则：
 *   - 每条语句（含 if 自身）占 1 个 index
 *   - if 的 then/else 块和 while body 递归编号
 *   - Executor 跳过未执行分支时须按子树 span 推进，而非仅 then_count/else_count
 */
#ifndef TC_STMT_INDEX_H
#define TC_STMT_INDEX_H

#include "tc_types.h"

#include <stddef.h>

/** 扁平 stmt_index 游标（next 为下一条待分配/期望的序号） */
typedef struct {
    int next;
} TcStmtIndexCursor;

static inline int tc_stmt_subtree_index_count(const TcStatement *stmt);
static inline int tc_stmt_block_index_span(const TcStatement *items, size_t count);

/** 将游标重置为 0 */
static inline void tc_stmt_index_reset(TcStmtIndexCursor *cursor) {
    if (cursor) {
        cursor->next = 0;
    }
}

/**
 * 取出当前 index 并将游标推进 1（对应单条语句占用一个序号）。
 * @return 分配前的 index 值
 */
static inline int tc_stmt_index_take(TcStmtIndexCursor *cursor) {
    int index = cursor->next;

    cursor->next++;
    return index;
}

/**
 * 语句块占用的 index 总数（块内各语句子树之和，不含块外父节点）。
 */
static inline int tc_stmt_block_index_span(const TcStatement *items, size_t count) {
    size_t i = 0;
    int span = 0;

    for (i = 0; i < count; i++) {
        span += tc_stmt_subtree_index_count(&items[i]);
    }
    return span;
}

/**
 * 语句子树占用的 index 总数（含 stmt 自身）。
 * if 节点：1 + then 块 span + else 块 span。
 * while：1 + body span。
 * func：1 + body span（0.0.38 多域；与 Pass1 一致）。
 */
static inline int tc_stmt_subtree_index_count(const TcStatement *stmt) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        int span = 1;

        span += tc_stmt_block_index_span(if_stmt->then_body, if_stmt->then_count);
        span += tc_stmt_block_index_span(if_stmt->else_body, if_stmt->else_count);
        return span;
    }
    if (stmt->kind == TC_STMT_WHILE) {
        const TcWhileStmt *while_stmt = &stmt->u.while_stmt;

        return 1 + tc_stmt_block_index_span(while_stmt->body, while_stmt->body_count);
    }
    if (stmt->kind == TC_STMT_FUNC_DEF) {
        const TcFuncDef *func = &stmt->u.func_def;

        return 1 + tc_stmt_block_index_span(func->body, func->body_count);
    }
    return 1;
}

/** 将游标跳过整块语句（未执行分支），推进 block span */
static inline void tc_stmt_index_skip_block(TcStmtIndexCursor *cursor, const TcStatement *items,
                                           size_t count) {
    cursor->next += tc_stmt_block_index_span(items, count);
}

#endif /* TC_STMT_INDEX_H */
