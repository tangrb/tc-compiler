/*
 * tc_struct_exec.h — struct 运行时：构造、字段读写、深拷贝（模块 I）
 */
#ifndef TC_STRUCT_EXEC_H
#define TC_STRUCT_EXEC_H

#include "tc_diagnostic.h"
#include "tc_executor_internal.h"
#include "tc_struct_check.h"

int tc_exec_struct_ctor(const TcRhs *rhs, TcExecuteCtx *ctx, TcValue *out, TcDiagnostic *diag,
                        int line);
int tc_exec_struct_field_read(const TcRhs *rhs, TcTypeTag expected_type, TcExecuteCtx *ctx,
                              TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_struct_field_assign(const TcFieldAssign *assign, TcExecuteCtx *ctx,
                                TcDiagnostic *diag);
int tc_exec_struct_clone(const TcValue *src, size_t width_bits, TcExecuteCtx *ctx, TcValue *out,
                         TcDiagnostic *diag, int line);
/** 按字节数分配并追踪一个 struct 堆块（memblock 的 struct 元素抽出等复用） */
int tc_exec_struct_alloc(size_t bytes, TcExecuteCtx *ctx, void **out_block,
                         TcDiagnostic *diag, int line);
int tc_exec_struct_store_value(TcValue *dst_slot, const TcValue *src, int struct_id,
                               TcExecuteCtx *ctx, TcDiagnostic *diag, int line);
void tc_exec_struct_heap_free(TcExecuteCtx *ctx);

const TcStructTable *tc_exec_struct_table(const TcExecuteCtx *ctx);

#endif /* TC_STRUCT_EXEC_H */
