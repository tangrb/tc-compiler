/*
 * tc_executor_internal.h — Executor 内部上下文（tc_executor / ptr / memblock 共享）
 */
#ifndef TC_EXECUTOR_INTERNAL_H
#define TC_EXECUTOR_INTERNAL_H

#include "tc_call_frame.h"
#include "tc_stmt_index.h"
#include "tc_types.h"

typedef struct TcExecuteCtx {
    TcStmtIndexCursor index;
    TcValue *slots;
    const TcSymbolTable *symbols;
    const TcTypedProgram *program;
    TcCallFrame *call_frame;
    int current_func_id;
    void **memblock_heap;
    size_t memblock_heap_count;
    size_t memblock_heap_capacity;
    void **struct_heap;
    size_t struct_heap_count;
    size_t struct_heap_capacity;
} TcExecuteCtx;

const TcFuncDef *tc_find_func_def(const TcTypedProgram *prog, int func_id,
                                  const TcProgram **out_module);
int tc_func_body_index_range(const TcProgram *module, int func_id, int *out_body_start,
                             int *out_body_end);
int tc_exec_param_slot(const TcSymbolTable *symbols, const TcFuncDef *func,
                       const char *param_name, int *out_slot);
void tc_exec_set_internal_error(TcDiagnostic *diag, int line, const char *message);
int tc_exec_load_binding(const TcResolvedBinding *binding, TcTypeKind type,
                         const TcValue *slots, TcValue *out, TcDiagnostic *diag, int line);
int tc_eval_operand(const TcOperand *operand, TcTypeKind expected_type, TcExecuteCtx *ctx,
                    TcValue *out, TcDiagnostic *diag, int line);
int tc_eval_rhs(const TcRhs *rhs, TcTypeKind expected_type, TcExecuteCtx *ctx, TcValue *out,
                TcDiagnostic *diag, int line);
const TcSymbol *tc_exec_find_symbol(const TcSymbolTable *symbols, const char *name);

#endif /* TC_EXECUTOR_INTERNAL_H */
