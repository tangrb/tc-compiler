/* tc_sem_fp.h — deterministic float arithmetic, unary, and comparison interface */
#ifndef TC_SEM_FP_H
#define TC_SEM_FP_H

#include "tc_types.h"

/**
 * 浮点算术运算入口：add/sub/mul/div（strict/ieee；wrap 非法）。
 * @param type  TC_FLOAT32 或 TC_FLOAT64
 * @param mode  浮点运算模式
 */
int tc_exec_fp_arith(TcArithOp op, TcTypeKind type, TcFloatMode mode,
                     const TcValue *lhs, const TcValue *rhs, TcValue *out,
                     TcDiagnostic *diag, int line);

/**
 * 浮点单目运算入口：abs / neg（纯符号位操作；wrap 非法）。
 * @param type  TC_FLOAT32 或 TC_FLOAT64
 * @param mode  浮点运算模式
 */
int tc_exec_fp_unary(TcUnaryOp op, TcTypeKind type, TcFloatMode mode,
                     const TcValue *operand, TcValue *out,
                     TcDiagnostic *diag, int line);

/**
 * 浮点比较运算入口：eq/ne/lt/le/gt/ge（strict/ieee；wrap 不支持）。
 * @param type  TC_FLOAT32 或 TC_FLOAT64
 * @param mode  浮点运算模式
 */
int tc_exec_fp_compare(TcCompareOp op, TcTypeKind type, TcFloatMode mode,
                       const TcValue *lhs, const TcValue *rhs, TcValue *out,
                       TcDiagnostic *diag, int line);

#endif /* TC_SEM_FP_H */
