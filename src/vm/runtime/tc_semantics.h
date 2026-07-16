/*
 * tc_semantics.h — TC 语言语义运算接口
 *
 * 本模块是 TC-VM 的算术/cast 语义核心，定义整数字面量处理、
 * 位模式变换、算术运算（add/sub/mul/div/mod）和单目运算（abs/neg）
 * 以及类型转换的运行时函数接口。
 * Executor 和 AOT RT 均委托此模块完成语义运算。
 *
 * 子模块：tc_sem_int.h（整数）、tc_sem_fp.h（浮点）、tc_sem_bitwise.h（位运算/移位）。
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

/** 有符号类型的最小值：-(2^(n-1))（子模块共用） */
int64_t tc_type_min_signed(TcType type);

/** 有符号类型的最大值：2^(n-1) - 1（子模块共用） */
int64_t tc_type_max_signed(TcType type);

/** 无符号类型的最大值：2^n - 1（子模块共用） */
uint64_t tc_type_max_unsigned(TcType type);

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
/*  §5.1 操作 × 类型 × 模式矩阵                                        */
/* ------------------------------------------------------------------ */

int tc_validate_arith_mode(TcArithOp op, TcType type, TcWrapMode mode,
                           TcDiagnostic *diag, int line);
int tc_validate_unary_mode(TcUnaryOp op, TcType type, TcWrapMode mode,
                           TcDiagnostic *diag, int line);
int tc_validate_shift_mode(TcShiftOp op, TcType type, TcWrapMode mode,
                           TcDiagnostic *diag, int line);
int tc_validate_fp_arith_mode(TcArithOp op, TcType type, TcFloatMode mode,
                              TcDiagnostic *diag, int line);
int tc_validate_fp_unary_mode(TcUnaryOp op, TcType type, TcFloatMode mode,
                              TcDiagnostic *diag, int line);
int tc_validate_fp_compare_mode(TcType type, TcFloatMode mode,
                                TcDiagnostic *diag, int line);

/* ------------------------------------------------------------------ */
/*  比较 / 逻辑                                                         */
/* ------------------------------------------------------------------ */

int tc_exec_compare(TcCompareOp op, TcType type, const TcValue *lhs, const TcValue *rhs,
                    TcValue *out, TcDiagnostic *diag, int line);
int tc_exec_logic_binary(TcLogicOp op, const TcValue *lhs, const TcValue *rhs, TcValue *out,
                         TcDiagnostic *diag, int line);
int tc_exec_logic_unary(TcLogicOp op, const TcValue *operand, TcValue *out,
                        TcDiagnostic *diag, int line);

#include "tc_sem_int.h"
#include "tc_sem_fp.h"
#include "tc_sem_cast.h"
#include "tc_sem_bitwise.h"

#endif /* TC_SEMANTICS_H */
