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
int tc_exec_arith(TcArithOp op, TcType type, TcWrapMode mode,
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
int tc_exec_unary(TcUnaryOp op, TcType type, TcWrapMode mode,
                  const TcValue *operand, TcValue *out,
                  TcDiagnostic *diag, int line);

/**
 * cast 运算入口：按 strict / truncate 模式分派。
 * @param target 目标类型
 * @param mode   转换模式
 * @param source 源运行时值
 * @param out    输出结果
 * @param diag   诊断对象
 * @param line   当前行号
 * @return 成功 0；strict 模式值溢出 -1
 */
int tc_exec_cast(TcType target, TcTruncateMode mode, const TcValue *source,
                 TcValue *out, TcDiagnostic *diag, int line);

#endif /* TC_SEM_INT_H */
