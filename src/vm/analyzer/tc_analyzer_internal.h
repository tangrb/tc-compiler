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

/* 完整定义在 tc_func_check.h */
typedef struct TcFuncCheckEnv TcFuncCheckEnv;

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
    int *last_init;              /* 预扫描缓存；文件模式 uninit 走 CFG，本字段作回退 */
    TcStmtIndexCursor index;
    TcBlockPath block_path;
    TcInitState *init_states;    /* slot → 当前路径初始化状态 */
    int num_slots;
    int path_reachable;          /* 0：goto 后至下一 label 之前，跳过 init 副作用 */
    int next_loop_id;            /* Pass1 源序分配稳定 loop id */
    int current_loop_id;         /* Pass2 当前最内层 while；-1 表示无 */
    int loop_depth;              /* 词法祖先 while 数，用于范式隔离 */
    int func_depth;              /* 词法祖先 func 数；0 表示顶层 */
    int current_func_id;         /* 当前函数 func_id（4d 稳定分配）；顶层为 -1 */
    TcFuncCheckEnv *func_env;    /* 函数检查环境；可为 NULL（跳过 funcall/return 专用检查） */
    TcTypeTable *type_table;     /* 分析期类型池；Pass1 intern 用 */
} TcAnalyzeCtx;

/** 初始化历史 / 数据流上下文，供未初始化变量检查使用 */
typedef struct {
    const TcProgram *program;               /* 当前分析的程序；可为 NULL 则走 last_init 回退 */
    const int *last_init_stmt_index;        /* slot → 最后初始化语句序号（无 init_states 时） */
    TcInitState *init_states;               /* Pass2 路径敏感状态；NULL 则回退 last_init */
    int num_slots;
    int check_init;                         /* 0：短路或不可达路径，跳过未初始化错误 */
    int defer_to_cfg;                       /* 文件模式由完整 CFG 统一检查 */
    TcTypeTable *type_table;                /* Pass2：cast/bitcast 目标 intern；执行期只读 */
} TcInitHistory;

/* ------------------------------------------------------------------ */
/*  字面量（实现于 tc_analyzer.c）                                        */
/* ------------------------------------------------------------------ */

int tc_check_literal(const TcLiteral *lit, TcTypeTag expected, int line,
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
/*  Pass2 类型检查辅助（实现于 tc_analyzer_pass2.c / tc_analyzer_pass2_rhs.c） */
/* ------------------------------------------------------------------ */

void tc_resolved_binding_set(TcResolvedBinding *binding, const TcSymbol *symbol);

const TcSymbol *tc_resolve_visible_symbol(const TcSymbolTable *visible,
                                          const TcSymbolTable *global, const char *name,
                                          size_t stmt_index, int line, TcDiagnostic *diag);

/*
 * tc_check_operand / tc_check_rhs / tc_check_io_format 的权威声明在模块头中：
 *   tc_analyzer_pass2_rhs.h、tc_analyze_6e.h
 * 此处再导出，避免 analyzer 子模块重复手写原型；include 守卫防止循环。
 */
#include "tc_analyze_6e.h"
#include "tc_analyzer_pass2_rhs.h"

#endif /* TC_ANALYZER_INTERNAL_H */
