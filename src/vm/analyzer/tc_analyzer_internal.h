/*
 * tc_analyzer_internal.h — analyzer 子模块共享类型与交叉声明
 *
 * 非公共 API；仅 analyzer 子模块间使用。
 */
#ifndef TC_ANALYZER_INTERNAL_H
#define TC_ANALYZER_INTERNAL_H

#include "tc_symbol.h"
#include "tc_types.h"
#include "tc_stmt_index.h"
#include "tc_warning.h"
#include "tc_diagnostic.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  共享类型                                                            */
/* ------------------------------------------------------------------ */

/**
 * 块路径：每层编码 if 的 then/else 分支。
 * then = if_stmt_index * 2；else = if_stmt_index * 2 + 1（区分兄弟块）。
 */
typedef struct {
    TcBlockId *path;
    int depth;
    int capacity;
} TcBlockPath;

/** 每个 slot 的初始化状态（路径敏感数据流） */
typedef enum {
    TC_INIT_UNKNOWN = 0,
    TC_INIT_UNINIT,
    TC_INIT_INIT
} TcInitState;

/** Pass2 与预扫描共享的全局语句序号上下文 */
typedef struct {
    TcProgram *program;
    int *last_init;              /* 预扫描缓存（REPL / 兼容）；Pass2 主路径用 init_states */
    TcStmtIndexCursor index;
    TcBlockPath block_path;
    TcInitState *init_states;    /* slot → 当前路径初始化状态 */
    int num_slots;
    int path_reachable;          /* 0：goto 后至下一 label 之前，跳过 init 副作用 */
    int next_loop_id;            /* Pass1 源序分配稳定 loop id */
    int current_loop_id;         /* Pass2 当前最内层 while；-1 表示无 */
    int loop_depth;              /* 词法祖先 while 数，用于范式隔离 */
} TcAnalyzeCtx;

/** 初始化历史 / 数据流上下文，供未初始化变量检查使用 */
typedef struct {
    const TcProgram *program;               /* 完整程序（文件模式）；REPL 模式下为 NULL */
    const int *last_init_stmt_index;        /* REPL：slot → 最后初始化语句序号 */
    TcInitState *init_states;               /* Pass2 路径敏感状态；NULL 则回退 last_init */
    int num_slots;
    int check_init;                         /* 0：短路或不可达路径，跳过未初始化错误 */
    int defer_to_cfg;                       /* 文件模式由完整 CFG 统一检查 */
} TcInitHistory;

/* ------------------------------------------------------------------ */
/*  字面量（实现于 tc_analyzer.c）                                        */
/* ------------------------------------------------------------------ */

int tc_check_literal(const TcLiteral *lit, TcType expected, int line,
                     TcDiagnostic *diag, TcErrorKind literal_type_err);

/* ------------------------------------------------------------------ */
/*  DFA（实现于 tc_analyzer_dfa.c）                                       */
/* ------------------------------------------------------------------ */

void tc_init_states_reset(TcInitState *states, int num_slots, TcInitState s);
void tc_init_states_copy(TcInitState *dst, const TcInitState *src, int num_slots);
void tc_init_states_merge(TcInitState *merged, const TcInitState *a, const TcInitState *b,
                          int num_slots);

void tc_block_path_init(TcBlockPath *bp);
void tc_block_path_free(TcBlockPath *bp);
int tc_block_path_push(TcBlockPath *bp, TcBlockId block_id, TcDiagnostic *diag);
void tc_block_path_pop(TcBlockPath *bp);
TcBlockId tc_block_id_then(int if_stmt_index);
TcBlockId tc_block_id_else(int if_stmt_index);
TcBlockId tc_block_id_while(int while_stmt_index);
int tc_paths_equal_prefix(const TcBlockId *a, const TcBlockId *b, int depth);

int tc_check_operand_init(TcInitHistory *hist, const TcSymbol *sym, size_t stmt_index,
                          int line, TcDiagnostic *diag);
const TcSymbol *tc_symbol_for_assign_target(const TcSymbolTable *symbols, const char *name,
                                            int stmt_index);

void tc_prescan_init_history(TcProgram *program, TcSymbolTable *symbols, TcAnalyzeCtx *ctx);

/* ------------------------------------------------------------------ */
/*  Pass2 类型检查辅助（实现于 tc_analyzer_pass2.c；REPL 亦用）              */
/* ------------------------------------------------------------------ */

int tc_check_operand(TcOperand *operand, TcType expected,
                     const TcSymbolTable *visible, const TcSymbolTable *global,
                     TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                     TcWarningList *warnings, const char *self_name, TcErrorKind type_err);
int tc_check_io_format(TcType type, TcFormatSpec fmt, int line, TcDiagnostic *diag);
int tc_check_rhs(TcRhs *rhs, TcType lhs_type, const TcSymbolTable *visible,
                 const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                 int line, TcDiagnostic *diag, TcWarningList *warnings, const char *self_name);

#endif /* TC_ANALYZER_INTERNAL_H */
