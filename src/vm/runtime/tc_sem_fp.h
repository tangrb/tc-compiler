/*
 * tc_sem_fp.h — 浮点算术、比较与 cast 语义接口
 */
#ifndef TC_SEM_FP_H
#define TC_SEM_FP_H

#include "tc_types.h"

/**
 * 浮点算术运算入口：add/sub/mul/div（strict/ieee/wrap）。
 * @param type  TC_FLOAT32 或 TC_FLOAT64
 * @param mode  浮点运算模式
 */
int tc_exec_fp_arith(TcArithOp op, TcType type, TcFloatMode mode,
                     const TcValue *lhs, const TcValue *rhs, TcValue *out,
                     TcDiagnostic *diag, int line);

/**
 * 浮点单目运算入口：abs / neg（strict/ieee/wrap）。
 * @param type  TC_FLOAT32 或 TC_FLOAT64
 * @param mode  浮点运算模式
 */
int tc_exec_fp_unary(TcUnaryOp op, TcType type, TcFloatMode mode,
                     const TcValue *operand, TcValue *out,
                     TcDiagnostic *diag, int line);

/**
 * 浮点比较运算入口：eq/ne/lt/le/gt/ge（strict/ieee；wrap 不支持）。
 * @param type  TC_FLOAT32 或 TC_FLOAT64
 * @param mode  浮点运算模式
 */
int tc_exec_fp_compare(TcCompareOp op, TcType type, TcFloatMode mode,
                       const TcValue *lhs, const TcValue *rhs, TcValue *out,
                       TcDiagnostic *diag, int line);

/**
 * 浮点类型转换入口（strict / truncate）。
 * @param target  TC_FLOAT32 或 TC_FLOAT64
 */
int tc_exec_fp_cast(TcType target, TcTruncateMode mode, const TcValue *source,
                    TcValue *out, TcDiagnostic *diag, int line);

#endif /* TC_SEM_FP_H */
