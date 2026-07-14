/*
 * tc_io.h — TC 统一 I/O 接口
 *
 * 提供 write/read 的共享实现，消除 tc_executor.c 和 tc_aot_rt.c 间的代码重复。
 * 支持格式化输出（%d/%i/%u/%x/%X/%o/%b/%t/%f/%e/%E/%g/%G）和 stdin 输入（十进制整数/bool 文本/浮点数）。
 */
#ifndef TC_IO_H
#define TC_IO_H

#include <stdio.h>
#include <stdint.h>

#include "tc_diagnostic.h"
#include "tc_types.h"

/**
 * 按格式符号将 TcValue 写入指定输出流。
 * @param type  值的整数类型
 * @param fmt   格式说明符
 * @param value 待输出的运行时值
 * @param out   输出流
 * @return 成功返回 0；I/O 错误返回 -1
 */
int tc_io_write_formatted(TcType type, TcFormatSpec fmt, const TcValue *value, FILE *out);

/**
 * 将 TcValue 写入指定输出流，带可选格式和换行。
 * @param value   待输出的运行时值
 * @param fmt     格式说明符（TC_FMT_NONE 时按类型默认输出）
 * @param newline 是否追加换行符
 * @param out     输出流
 * @return 成功返回 0；I/O 错误返回 -1
 */
int tc_io_write_value(const TcValue *value, TcFormatSpec fmt, int newline, FILE *out);

/**
 * 跳过 stdin 前导空白字符。
 */
void tc_io_skip_whitespace(void);

/**
 * 从 stdin 读取十进制数字字符序列，计算其绝对值和符号。
 * @param c        当前已读取的首个字符
 * @param line     当前行号（错误定位）
 * @param diag     诊断对象
 * @param out_abs  输出：数字序列的绝对值
 * @param out_sign 输出：符号（1 或 -1）
 * @return 成功返回 0；输入非法或超出 uint64 范围返回 -1
 */
int tc_io_read_digits(int c, int line, TcDiagnostic *diag,
                      uint64_t *out_abs, int *out_sign);

/**
 * 从 stdin 读取一个类型化的值（bool 文本或十进制整数）。
 * @param type     期望的目标类型
 * @param out_bits 输出：读取值的位模式
 * @param diag     诊断对象
 * @param line     当前行号（错误定位）
 * @return 成功返回 0；输入非法或超出目标类型范围返回 -1
 */
int tc_io_read_value(TcType type, uint64_t *out_bits, TcDiagnostic *diag, int line);

#endif /* TC_IO_H */
