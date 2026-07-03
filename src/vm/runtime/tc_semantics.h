/*
 * tc_semantics.h — TC 语言语义运算接口
 */
#ifndef TC_SEMANTICS_H
#define TC_SEMANTICS_H

#include "tc_types.h"

int tc_literal_fits_type(uint64_t value, TcIntType type);

/**
 * @brief 按上下文类型检查 TcLiteral 合法性
 * @param lit      字面量
 * @param type     上下文类型
 * @param err_kind 失败时写入错误种类（LiteralOutOfRange 或 LiteralTypeError）
 * @return 合法返回 1；非法返回 0
 */
int tc_literal_fits_context(const TcLiteral *lit, TcIntType type, TcErrorKind *err_kind);

/**
 * @brief 将字面量编码为运行时值（Analyzer 已校验范围）
 */
TcValue tc_literal_to_value(const TcLiteral *lit, TcIntType type);

int tc_signed_in_range(int64_t value, TcIntType type);
int tc_unsigned_in_range(uint64_t value, TcIntType type);

uint64_t tc_mask_bits(int bit_width);
int64_t tc_bits_to_signed(TcIntType type, uint64_t bits);
uint64_t tc_signed_to_bits(TcIntType type, int64_t value);
uint64_t tc_value_to_unsigned(TcIntType type, uint64_t bits);
TcValue tc_value_make(TcIntType type, uint64_t bits);

int tc_exec_arith(TcArithOp op, TcIntType type, TcWrapMode mode,
                  const TcValue *lhs, const TcValue *rhs, TcValue *out,
                  TcDiagnostic *diag, int line);

int tc_exec_cast(TcIntType target, TcTruncateMode mode, const TcValue *source,
                 TcValue *out, TcDiagnostic *diag, int line);

#endif
