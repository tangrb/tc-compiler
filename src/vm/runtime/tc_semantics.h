/*
 * tc_semantics.h — TC 语言语义运算接口
 *
 * 实现 TC 语言标准定义的整数运算语义：字面量范围检查、位模式转换、
 * 算术运算（add/sub/mul/div/mod）和类型转换（cast），支持 strict 与 overflow 两种模式。
 * Analyzer 用于静态字面量校验，Executor 用于运行时求值。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_SEMANTICS_H
#define TC_SEMANTICS_H

#include "tc_types.h"

/* 字面量/数值范围检查 */

/**
 * @brief 检查无符号字面量能否放入指定类型
 * @param value 无符号字面量值
 * @param type  目标整数类型
 * @return 可放入返回 1；超出范围返回 0
 */
int tc_literal_fits_type(uint64_t value, TcIntType type);

/**
 * @brief 检查有符号整数是否在指定类型范围内
 * @param value 有符号整数值
 * @param type  目标整数类型
 * @return 在范围内返回 1；超出返回 0
 */
int tc_signed_in_range(int64_t value, TcIntType type);

/**
 * @brief 检查无符号整数是否在指定类型范围内
 * @param value 无符号整数值
 * @param type  目标整数类型
 * @return 在范围内返回 1；超出返回 0
 */
int tc_unsigned_in_range(uint64_t value, TcIntType type);

/* 位模式工具：掩码、有符号/无符号互转、值归一化 */
uint64_t tc_mask_bits(int bit_width);
int64_t tc_bits_to_signed(TcIntType type, uint64_t bits);
uint64_t tc_signed_to_bits(TcIntType type, int64_t value);
uint64_t tc_value_to_unsigned(TcIntType type, uint64_t bits);

/**
 * @brief 构造运行时值
 * @param type 值的整数类型
 * @param bits 位模式（自动归一化到目标类型位宽）
 * @return 包含类型和归一化后 bits 的 TcValue 结构体
 */
TcValue tc_value_make(TcIntType type, uint64_t bits);

/**
 * @brief 执行算术运算
 * @param op    算术运算符
 * @param type  运算的目标整数类型
 * @param mode  溢出处理模式（strict / overflow）
 * @param lhs   左操作数
 * @param rhs   右操作数
 * @param out   输出参数，运算结果
 * @param diag  诊断对象
 * @param line  当前语句行号
 * @return 成功返回 0；失败返回 -1 并通过 diag 报告错误
 */
int tc_exec_arith(TcArithOp op, TcIntType type, TcOverflowMode mode,
                  const TcValue *lhs, const TcValue *rhs, TcValue *out,
                  TcDiagnostic *diag, int line);

/**
 * @brief 执行类型转换
 * @param target 目标整数类型
 * @param mode   转换模式（strict / overflow）
 * @param source 源运行时值
 * @param out    输出参数，转换后的值
 * @param diag   诊断对象
 * @param line   当前语句行号
 * @return 成功返回 0；strict 模式下范围不合法返回 -1
 * @note strict 模式下范围不合法则报错；overflow 模式下按位宽截断/符号扩展
 */
int tc_exec_cast(TcIntType target, TcOverflowMode mode, const TcValue *source,
                 TcValue *out, TcDiagnostic *diag, int line);

#endif
