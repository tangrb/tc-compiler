/*
 * tc_memblock_exec.h — memblock 堆布局与运行时运算
 */
#ifndef TC_MEMBLOCK_EXEC_H
#define TC_MEMBLOCK_EXEC_H

#include "tc_diagnostic.h"
#include "tc_executor_internal.h"

int tc_exec_memblock_ctor(const TcRhs *rhs, const TcType *expected, TcExecuteCtx *ctx,
                          TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_memblock_clone(const TcType *type, const TcValue *src, TcExecuteCtx *ctx,
                           TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_memblock_load(const TcType *element, const TcOperand *mb_op, const TcOperand *index_op,
                          TcExecuteCtx *ctx, TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_memblock_count(int slot, TcExecuteCtx *ctx, TcValue *out, TcDiagnostic *diag,
                           int line);
int tc_exec_memblock_store_stmt(const TcMemblockStoreStmt *stmt, TcExecuteCtx *ctx,
                                TcDiagnostic *diag);
int tc_exec_memblock_copy_stmt(const TcMemblockCopyStmt *stmt, TcExecuteCtx *ctx,
                               TcDiagnostic *diag);
int tc_exec_memcopy_unsafe_stmt(const TcMemcopyUnsafeStmt *stmt, TcExecuteCtx *ctx,
                                TcDiagnostic *diag);
void tc_exec_memblock_heap_free(TcExecuteCtx *ctx);

#endif /* TC_MEMBLOCK_EXEC_H */
