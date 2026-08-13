/*
 * tc_sem_bitwise.h — 按位运算与移位语义接口
 */
#ifndef TC_SEM_BITWISE_H
#define TC_SEM_BITWISE_H

#include "tc_types.h"

/**
 * 按位双目运算：and / or / xor。
 * 操作数按无符号位模式处理，结果按目标类型 T 解释；不溢出、不支持 wrap/truncate。
 */
int tc_exec_bitwise_binary(TcBitwiseOp op, TcTypeTag type,
                           const TcValue *lhs, const TcValue *rhs, TcValue *out,
                           TcDiagnostic *diag, int line);

/** 按位单目 not：操作数位模式取反后按 T 解释 */
int tc_exec_bitwise_unary(TcTypeTag type, const TcValue *operand, TcValue *out,
                          TcDiagnostic *diag, int line);

/**
 * 移位运算：shl（可选 wrap）/ shr（恒 strict，永不溢出）。
 * 计数 k 取无符号数学值，不掩码；被移位数与计数须与 type 一致。
 */
int tc_exec_shift(TcShiftOp op, TcTypeTag type, TcWrapMode mode,
                  const TcValue *value, const TcValue *count, TcValue *out,
                  TcDiagnostic *diag, int line);

#endif /* TC_SEM_BITWISE_H */
