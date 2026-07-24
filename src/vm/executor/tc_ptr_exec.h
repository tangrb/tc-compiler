/*
 * tc_ptr_exec.h — ptr 运行时运算（Phase 5 / 模块 I）
 */
#ifndef TC_PTR_EXEC_H
#define TC_PTR_EXEC_H

#include "tc_diagnostic.h"
#include "tc_executor_internal.h"

int tc_exec_ptr_address(const TcType *pointee, const char *name, TcExecuteCtx *ctx,
                        TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_ptr_load(const TcType *pointee, const TcOperand *ptr_op, TcExecuteCtx *ctx,
                     TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_ptr_store(const TcType *pointee, const TcOperand *ptr_op, const TcOperand *value_op,
                      TcExecuteCtx *ctx, TcDiagnostic *diag, int line);
int tc_exec_ptr_arith(int is_add, const TcType *pointee, const TcOperand *ptr_op,
                      const TcOperand *offset_op, TcExecuteCtx *ctx, TcValue *out,
                      TcDiagnostic *diag, int line);
int tc_exec_ptr_compare(TcCompareOp op, const TcType *pointee, const TcOperand *lhs_op,
                        const TcOperand *rhs_op, TcExecuteCtx *ctx, TcValue *out,
                        TcDiagnostic *diag, int line);
int tc_exec_ptr_size(const TcType *pointee, const TcOperand *ptr_op, TcExecuteCtx *ctx,
                     TcValue *out, TcDiagnostic *diag, int line);

#endif /* TC_PTR_EXEC_H */
