/*
 * tc_aot_rt.h — AOT 生成 C 代码的运行时辅助（复用 semantics.c 语义）
 */
#ifndef TC_AOT_RT_H
#define TC_AOT_RT_H

#include <stdint.h>

#include "tc_diagnostic.h"
#include "tc_types.h"

void tc_aot_diag_init(TcDiagnostic *diag);
uint64_t tc_aot_lit(TcIntType type, uint64_t magnitude, int negative, int unsigned_suffix);
int tc_aot_arith(TcArithOp op, TcIntType type, TcWrapMode mode, uint64_t *out, uint64_t lhs,
                 uint64_t rhs, TcDiagnostic *diag, int line);
int tc_aot_cast(TcIntType target, TcTruncateMode mode, uint64_t src_bits, TcIntType src_type,
                uint64_t *out, TcDiagnostic *diag, int line);
void tc_aot_write(TcIntType type, uint64_t bits, int newline);
int tc_aot_read(TcIntType type, uint64_t *out, TcDiagnostic *diag, int line);
void tc_aot_abort(const TcDiagnostic *diag, int line);

#endif
