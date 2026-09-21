/*
 * tc_executor_internal.h — Executor 内部上下文（executor / ptr / memblock / struct 共享）
 *
 * internal error 假定入口已通过 --check（Pass2 + CFG）：未解析元数据、
 * 缺失 return、未知 stmt/RHS kind 为防御路径；损坏的复合值 / 非法指针比较
 * 为形状检查。
 */
#ifndef TC_EXECUTOR_INTERNAL_H
#define TC_EXECUTOR_INTERNAL_H

#include "tc_call_frame.h"
#include "tc_stmt_index.h"
#include "tc_types.h"

typedef struct TcExecuteCtx {
    TcStmtIndexCursor index;
    TcValue *slots;
    size_t slot_capacity; /* slots 数组容量（指针解引用须校验槽索引上界；
                           * 嵌入模式含临时槽位区容量，见 tc_embed_slots.h） */
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
int tc_exec_load_binding(const TcResolvedBinding *binding, TcTypeTag type,
                         const TcValue *slots, TcValue *out, TcDiagnostic *diag, int line);
int tc_eval_operand(const TcOperand *operand, TcTypeTag expected_type, TcExecuteCtx *ctx,
                    TcValue *out, TcDiagnostic *diag, int line);
int tc_eval_rhs(const TcRhs *rhs, TcTypeTag expected_type, TcExecuteCtx *ctx, TcValue *out,
                TcDiagnostic *diag, int line);
const TcSymbol *tc_exec_find_symbol(const TcSymbolTable *symbols, const char *name);

#endif /* TC_EXECUTOR_INTERNAL_H */
