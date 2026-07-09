/*
 * tc_stmt_index.c — 语句树扁平 stmt_index 分配与跳过（实现）
 */
#include "tc_stmt_index.h"

void tc_stmt_index_reset(TcStmtIndexCursor *cursor) {
    if (cursor) {
        cursor->next = 0;
    }
}

int tc_stmt_index_take(TcStmtIndexCursor *cursor) {
    int index = cursor->next;

    cursor->next++;
    return index;
}

int tc_stmt_block_index_span(const TcStatement *items, size_t count) {
    size_t i = 0;
    int span = 0;

    for (i = 0; i < count; i++) {
        span += tc_stmt_subtree_index_count(&items[i]);
    }
    return span;
}

int tc_stmt_subtree_index_count(const TcStatement *stmt) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        int span = 1;

        span += tc_stmt_block_index_span(if_stmt->then_body, if_stmt->then_count);
        span += tc_stmt_block_index_span(if_stmt->else_body, if_stmt->else_count);
        return span;
    }
    return 1;
}

void tc_stmt_index_skip_block(TcStmtIndexCursor *cursor, const TcStatement *items, size_t count) {
    cursor->next += tc_stmt_block_index_span(items, count);
}
