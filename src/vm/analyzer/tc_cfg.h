/*
 * tc_cfg.h — 显式控制流图与确定初始化分析
 *
 * 单域 TcCfg：入口/出口/语句/分支/合流/循环节点 + 边（fallthrough/true/false/
 * break/continue/goto/短路/return）。
 * 多域 TcCfgSet：顶层与各函数体各自独立建图、独立做 definite init（状态不跨域）。
 */
#ifndef TC_CFG_H
#define TC_CFG_H

#include "tc_diagnostic.h"
#include "tc_symbol.h"
#include "tc_types.h"

#include <stddef.h>

typedef enum {
    TC_CFG_ENTRY,
    TC_CFG_EXIT,
    TC_CFG_STATEMENT,
    TC_CFG_BRANCH,
    TC_CFG_MERGE,
    TC_CFG_LOOP_CONDITION,
    TC_CFG_LOOP_EXIT
} TcCfgNodeKind;

typedef enum {
    TC_CFG_FALLTHROUGH,
    TC_CFG_TRUE,
    TC_CFG_FALSE,
    TC_CFG_BREAK,
    TC_CFG_CONTINUE,
    TC_CFG_GOTO,
    TC_CFG_SHORT_CIRCUIT,
    TC_CFG_RETURN
} TcCfgEdgeKind;

typedef struct {
    int id;
    TcCfgNodeKind kind;
    int stmt_index;
    int line;
    int scope_id;
    TcStmtKind stmt_kind;
    int reachable;
    int constant_condition; /* -1：非常量；0：恒假；1：恒真 */
    int *read_slots;
    size_t read_count;
    size_t read_capacity;
    int write_slot;
} TcCfgNode;

typedef struct {
    int from;
    int to;
    TcCfgEdgeKind kind;
    int enabled;
} TcCfgEdge;

struct TcCfg {
    TcCfgNode *nodes;
    size_t node_count;
    size_t node_capacity;
    TcCfgEdge *edges;
    size_t edge_count;
    size_t edge_capacity;
    int entry_id;
    int exit_id;
    size_t slot_count;
    int is_function_domain; /* 1：函数域 CFG；0：顶层 */
    int func_id;            /* 函数域时的 func_id；顶层为 -1 */
    int *entry_init_slots;  /* 入口已初始化槽（形参）；可为 NULL */
    size_t entry_init_count;
};

/** 多域 CFG 集合：顶层 + 各函数独立 CFG（互不拼接状态） */
struct TcCfgSet {
    TcCfg toplevel;
    TcCfg *funcs;
    size_t func_count;
    size_t func_capacity;
};

void tc_cfg_init(TcCfg *cfg);
void tc_cfg_free(TcCfg *cfg);
void tc_cfg_set_init(TcCfgSet *set);
void tc_cfg_set_free(TcCfgSet *set);

/** 构建单域 CFG（通常为顶层可执行语句序列） */
int tc_cfg_build(const TcProgram *program, const TcSymbolTable *symbols, TcCfg *out,
                 TcDiagnostic *diag);

/** 构建顶层 CFG，并对每个 TC_STMT_FUNC_DEF 构建独立函数域 CFG */
int tc_cfg_build_all(const TcProgram *program, const TcSymbolTable *symbols, TcCfgSet *out,
                     TcDiagnostic *diag);

int tc_analyze_definite_init(const TcCfg *cfg, size_t slot_count, TcDiagnostic *diag);

/** 多域确定初始化 + UNREACHABLE + MISSING_RETURN */
int tc_analyze_definite_init_all(const TcCfgSet *set, const TcProgram *program,
                                 size_t slot_count, TcDiagnostic *diag);

#endif /* TC_CFG_H */
