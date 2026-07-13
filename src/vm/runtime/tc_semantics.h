/*
 * tc_semantics.h — TC 语言语义运算接口
 *
 * 本模块是 TC-VM 的算术/cast 语义核心，定义整数字面量处理、
 * 位模式变换、算术运算（add/sub/mul/div/mod）和单目运算（abs/neg）
 * 以及类型转换的运行时函数接口。
 * Executor 和 AOT RT 均委托此模块完成语义运算。
 */
#ifndef TC_SEMANTICS_H
#define TC_SEMANTICS_H

#include "tc_types.h"

/* ------------------------------------------------------------------ */
/*  字面量检查与转换                                                     */
/* ------------------------------------------------------------------ */

/** 检查无符号字面量量值能否放入目标类型的表示范围 */
int tc_literal_fits_type(uint64_t value, TcType type);

/**
 * 按上下文类型检查 TcLiteral 合法性（考虑负号、u 后缀和类型符号性）。
 * @param lit      字面量
 * @param type     上下文类型
 * @param err_kind 失败时写入具体错误种类（LiteralOutOfRange 或 LiteralTypeError）
 * @return 合法返回 1；非法返回 0
 */
int tc_literal_fits_context(const TcLiteral *lit, TcType type, TcErrorKind *err_kind);

/** 将字面量编码为运行时值（Analyzer 已校验范围，此函数假定合法） */
TcValue tc_literal_to_value(const TcLiteral *lit, TcType type);

/* ------------------------------------------------------------------ */
/*  范围检查                                                           */
/* ------------------------------------------------------------------ */

int tc_signed_in_range(int64_t value, TcType type);
int tc_unsigned_in_range(uint64_t value, TcType type);

/* ------------------------------------------------------------------ */
/*  位模式变换工具函数                                                    */
/* ------------------------------------------------------------------ */

/** 生成 n 位全 1 掩码，n >= 64 时返回 UINT64_MAX */
uint64_t tc_mask_bits(int bit_width);

/**
 * 将 IEEE 754 位模式转为 double（float32 先转为 float 再提升到 double）。
 * 供 tc_semantics.c 内部及 tc_io.c 等外部模块共用。
 */
double tc_fp_bits_to_double(TcType type, uint64_t bits);

/**
 * 将 double 编码为 IEEE 754 位模式（float32 先截断再编码）。
 * 供 tc_semantics.c 内部及 tc_io.c 等外部模块共用。
 */
uint64_t tc_fp_double_to_bits(TcType type, double value);

/** 将无符号位模式按目标有符号类型解释为 int64（二补数） */
int64_t tc_bits_to_signed(TcType type, uint64_t bits);

/** 将 int64 编码为目标类型位宽的无符号位模式 */
uint64_t tc_signed_to_bits(TcType type, int64_t value);

/** 将位模式用目标类型的位宽掩码归一化（无符号视角） */
uint64_t tc_value_to_unsigned(TcType type, uint64_t bits);

/** 构造运行时值（自动归一化 bits 到目标类型位宽） */
TcValue tc_value_make(TcType type, uint64_t bits);

/* ------------------------------------------------------------------ */
/*  未初始化变量槽位哨兵                                                  */
/* ------------------------------------------------------------------ */

/** 未初始化槽位填充字节（VM TcValue 槽 memset 用） */
#define TC_UNINITIALIZED_SLOT_BYTE 0xFE

/** 未初始化槽位位模式（AOT uint64_t 槽与 TcValue.bits 一致） */
#define TC_UNINITIALIZED_SLOT_BITS 0xFEFEFEFEFEFEFEFEULL

/** 将 TcValue 槽数组填充为未初始化哨兵值 */
void tc_slots_init_uninitialized(TcValue *slots, size_t count);

/** 将 uint64_t 槽数组填充为未初始化哨兵值 */
void tc_slot_bits_init_uninitialized(uint64_t *slots, size_t count);

/* ------------------------------------------------------------------ */
/*  算术 / 单目 / cast 运行时运算                                         */
/* ------------------------------------------------------------------ */

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
int tc_exec_compare(TcCompareOp op, TcType type, const TcValue *lhs, const TcValue *rhs,
                    TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_logic_binary(TcLogicOp op, const TcValue *lhs, const TcValue *rhs, TcValue *out,
                         TcDiagnostic *diag, int line);
int tc_exec_logic_unary(TcLogicOp op, const TcValue *operand, TcValue *out,
                        TcDiagnostic *diag, int line);

/**
 * 按位双目运算：and / or / xor。
 * 操作数按无符号位模式处理，结果按目标类型 T 解释；不溢出、不支持 wrap/truncate。
 */
int tc_exec_bitwise_binary(TcBitwiseOp op, TcType type,
                           const TcValue *lhs, const TcValue *rhs, TcValue *out,
                           TcDiagnostic *diag, int line);

/** 按位单目 not：操作数位模式取反后按 T 解释 */
int tc_exec_bitwise_unary(TcType type, const TcValue *operand, TcValue *out,
                          TcDiagnostic *diag, int line);

/**
 * 移位运算：shl（可选 wrap）/ shr（恒 strict，永不溢出）。
 * 计数 k 取无符号数学值，不掩码；被移位数与计数须与 type 一致。
 */
int tc_exec_shift(TcShiftOp op, TcType type, TcWrapMode mode,
                  const TcValue *value, const TcValue *count, TcValue *out,
                  TcDiagnostic *diag, int line);

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

#endif
