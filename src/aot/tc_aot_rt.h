/*
 * tc_aot_rt.h — AOT 生成 C 代码的运行时辅助接口
 *
 * 提供 tc_aot_arith / tc_aot_cast / tc_aot_write / tc_aot_read 等函数，
 * 内部委托 semantics.c 完成语义运算，保证 AOT 生成代码与 TC-VM 行为一致。
 * 这些函数被 tc-aot 生成的 main.c 调用。
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
int tc_aot_unary(TcUnaryOp op, TcIntType type, TcWrapMode mode, uint64_t *out, uint64_t operand,
                 TcDiagnostic *diag, int line);
int tc_aot_compare(TcCompareOp op, TcIntType type, uint64_t *out, uint64_t lhs, uint64_t rhs,
                   TcDiagnostic *diag, int line);
int tc_aot_logic(TcLogicOp op, uint64_t *out, uint64_t lhs, uint64_t rhs, TcDiagnostic *diag,
                 int line);
int tc_aot_logic_unary(TcLogicOp op, uint64_t *out, uint64_t operand, TcDiagnostic *diag,
                       int line);
int tc_aot_cast(TcIntType target, TcTruncateMode mode, uint64_t src_bits, TcIntType src_type,
                uint64_t *out, TcDiagnostic *diag, int line);
void tc_aot_write(TcIntType type, TcFormatSpec fmt, uint64_t bits, int newline);
int tc_aot_read(TcIntType type, uint64_t *out, TcDiagnostic *diag, int line);
void tc_aot_abort(const TcDiagnostic *diag, int line);

#endif
