/*
 * tc_sem_int.h — 整数算术、单目与 cast 语义接口
 */
#ifndef TC_SEM_INT_H
#define TC_SEM_INT_H

#include "tc_types.h"

/**
 * 算术运算入口：按有符号/无符号分派到对应实现。
 * @param op    运算符（add/sub/mul/div/mod）
 * @param type  运算类型
 * @param mode  溢出模式
 * @param lhs   左操作数
 * @param rhs   右操作数
 * @param out   输出结果
 * @param diag  诊断对象
 * @param line  当前行号
 * @return 成功 0；失败（除零/溢出）-1
 */
int tc_exec_arith(TcArithOp op, TcTypeTag type, TcWrapMode mode,
                  const TcValue *lhs, const TcValue *rhs, TcValue *out,
                  TcDiagnostic *diag, int line);

/**
 * 单目运算入口：abs / neg。
 * @param op      运算符
 * @param type    运算类型
 * @param mode    溢出模式
 * @param operand 操作数
 * @param out     输出结果
 * @param diag    诊断对象
 * @param line    当前行号
 * @return 成功 0；失败（abs/neg(INT_MIN) 溢出）-1
 */
int tc_exec_unary(TcUnaryOp op, TcTypeTag type, TcWrapMode mode,
                  const TcValue *operand, TcValue *out,
                  TcDiagnostic *diag, int line);

/**
 * 64×64 → 128 位无符号乘法（C99 分块，不依赖 __int128）。
 * hi 为高 64 位，lo 为低 64 位。wrap 乘法消费 lo；hi 供溢出/测试使用。
 */
void tc_umul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo);

#endif /* TC_SEM_INT_H */
