/*
 * tc_stmt_index.h — 语句树扁平 stmt_index 分配与跳过
 *
 * Analyzer / Executor / AOT 共用同一套 DFS 先序编号规则：
 *   - 每条语句（含 if 自身）占 1 个 index
 *   - if 的 then/else 块递归编号
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

/** 将游标重置为 0 */
void tc_stmt_index_reset(TcStmtIndexCursor *cursor);

/**
 * 取出当前 index 并将游标推进 1（对应单条语句占用一个序号）。
 * @return 分配前的 index 值
 */
int tc_stmt_index_take(TcStmtIndexCursor *cursor);

/**
 * 语句子树占用的 index 总数（含 stmt 自身）。
 * if 节点：1 + then 块 span + else 块 span。
 */
int tc_stmt_subtree_index_count(const TcStatement *stmt);

/**
 * 语句块占用的 index 总数（块内各语句子树之和，不含块外父节点）。
 */
int tc_stmt_block_index_span(const TcStatement *items, size_t count);

/** 将游标跳过整块语句（未执行分支），推进 block span */
void tc_stmt_index_skip_block(TcStmtIndexCursor *cursor, const TcStatement *items, size_t count);

#endif /* TC_STMT_INDEX_H */
