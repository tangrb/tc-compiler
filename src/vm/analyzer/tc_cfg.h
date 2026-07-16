/* tc_cfg.h — explicit control-flow graph and definite-initialization analysis */
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
    TC_CFG_SHORT_CIRCUIT
} TcCfgEdgeKind;

typedef struct {
    int id;
    TcCfgNodeKind kind;
    int stmt_index;
    int line;
    int scope_id;
    TcStmtKind stmt_kind;
    int reachable;
    int constant_condition; /* -1: nonconstant, 0: false, 1: true */
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
};

void tc_cfg_init(TcCfg *cfg);
void tc_cfg_free(TcCfg *cfg);
int tc_cfg_build(const TcProgram *program, const TcSymbolTable *symbols, TcCfg *out,
                 TcDiagnostic *diag);
int tc_analyze_definite_init(const TcCfg *cfg, size_t slot_count, TcDiagnostic *diag);

#endif /* TC_CFG_H */
