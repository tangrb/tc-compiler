/*
 * executor.c — TC 执行引擎实现
 *
 * 采用「变量槽位」模型：symbols.count 个 TcValue 槽，按 TcSymbol.slot 索引。
 * 对每条语句：
 *   - var 定义：求值 RHS，写入新变量槽
 *   - 赋值：求值 RHS，覆盖已有槽
 *
 * RHS 求值递归委托给 tc_eval_rhs，算术/cast 语义由 semantics.c 实现。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_executor.h"

#include "tc_diagnostic.h"
#include "tc_semantics.h"
#include "tc_symbol.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * @brief 将 TcValue 格式化为十进制文本并写入 stdout
 * @param value   待输出的运行时值
 * @param newline 是否在末尾追加换行符
 * @return 成功返回 0；输出失败返回 -1
 */
static int tc_exec_write_value(const TcValue *value, int newline) {
    if (tc_type_is_signed(value->type)) {
        int64_t signed_value = tc_bits_to_signed(value->type, value->bits);
        if (fprintf(stdout, "%" PRId64, signed_value) < 0) {
            return -1;
        }
    } else {
        uint64_t unsigned_value = tc_value_to_unsigned(value->type, value->bits);
        if (fprintf(stdout, "%" PRIu64, unsigned_value) < 0) {
            return -1;
        }
    }
    if (newline) {
        if (fputc('\n', stdout) == EOF) {
            return -1;
        }
    }
    if (fflush(stdout) != 0) {
        return -1;
    }
    return 0;
}

/*
 * @brief 执行 write / writeln 语句
 * @param io_write  I/O 输出语句
 * @param slots     运行时变量槽位数组
 * @param symbols   全局符号表
 * @param newline   writeln 时为 1
 * @param diag      诊断对象
 * @return 成功返回 0；失败返回 -1
 */
static int tc_exec_io_write(const TcIoWrite *io_write, const TcValue *slots,
                            const TcSymbolTable *symbols, int newline, TcDiagnostic *diag) {
    TcValue value;

    if (io_write->operand.kind == TC_OPERAND_LIT) {
        value = tc_literal_to_value(&io_write->operand.u.lit, io_write->type);
    } else {
        const TcSymbol *symbol = tc_symbol_table_find(symbols, io_write->operand.u.name);
        assert(symbol != NULL);
        value = slots[symbol->slot];
    }

    if (tc_exec_write_value(&value, newline) != 0) {
        tc_diagnostic_set(diag, TC_ERR_IO, io_write->line, TC_COLUMN_UNKNOWN, "output failed");
        return -1;
    }
    return 0;
}

/*
 * @brief 跳过 stdin 前导空白
 */
static void tc_io_skip_whitespace(void) {
    int c = 0;
    for (;;) {
        c = fgetc(stdin);
        if (c == EOF) {
            return;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        ungetc(c, stdin);
        return;
    }
}

/*
 * @brief 从 stdin 读取十进制数字字符序列，计算其绝对值
 * @param c         当前已读取的首个字符（必须是 '-' 或数字才合法，否则返回 -1）
 * @param line      当前行号
 * @param diag      诊断对象
 * @param out_abs   输出：数字序列的绝对值（无符号）
 * @param out_sign  输出：符号（1 或 -1）
 * @return 成功返回 0；输入非法或超出 uint64 范围返回 -1
 *
 * @note 提取 signed/unsigned 公共数字读取逻辑，消除重复代码。
 *       此函数不负责判断值与目标类型的兼容性——由调用方根据 signed/unsigned
 *       类型进行范围检查。
 */
static int tc_read_decimal_digits(int c, int line, TcDiagnostic *diag,
                                  uint64_t *out_abs, int *out_sign) {
    int sign = 1;
    int digit_count = 0;
    uint64_t abs_value = 0;

    if (c == '-') {
        sign = -1;
        c = fgetc(stdin);
        if (c == EOF) {
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
    }
    if (!isdigit((unsigned char)c)) {
        tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
        return -1;
    }
    do {
        int digit = c - '0';
        if (abs_value > UINT64_MAX / 10ULL ||
            (abs_value == UINT64_MAX / 10ULL &&
             (uint64_t)digit > UINT64_MAX % 10ULL)) {
            tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN,
                              "input value out of range");
            return -1;
        }
        abs_value = abs_value * 10ULL + (uint64_t)digit;
        digit_count++;
        c = fgetc(stdin);
    } while (c != EOF && isdigit((unsigned char)c));
    if (c != EOF) {
        ungetc(c, stdin);
    }
    if (digit_count == 0) {
        tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "invalid input");
        return -1;
    }

    *out_abs = abs_value;
    *out_sign = sign;
    return 0;
}

/*
 * @brief 从 stdin 读取一个十进制整数并存入目标变量槽
 * @param io_read  read 语句
 * @param slots    运行时变量槽位数组（可写）
 * @param symbols  全局符号表
 * @param diag     诊断对象
 * @return 成功返回 0；输入非法返回 -1 并设置 TC_ERR_IO
 */
static int tc_exec_io_read(const TcRead *io_read, TcValue *slots, const TcSymbolTable *symbols,
                           TcDiagnostic *diag) {
    int c = 0;
    int sign = 1;
    uint64_t abs_value = 0;
    const TcSymbol *symbol = NULL;
    TcValue value;

    tc_io_skip_whitespace();

    c = fgetc(stdin);
    if (c == EOF) {
        tc_diagnostic_set(diag, TC_ERR_IO, io_read->line, TC_COLUMN_UNKNOWN, "unexpected end of input");
        return -1;
    }

    if (tc_read_decimal_digits(c, io_read->line, diag, &abs_value, &sign) != 0) {
        return -1;
    }

    if (tc_type_is_signed(io_read->type)) {
        if (sign == -1 && abs_value == TC_INT64_MIN_ABS_MAGNITUDE) {
            value = tc_value_make(io_read->type, tc_signed_to_bits(io_read->type, INT64_MIN));
        } else {
            int64_t signed_value = (int64_t)abs_value;
            signed_value *= sign;
            if (!tc_signed_in_range(signed_value, io_read->type)) {
                tc_diagnostic_set(diag, TC_ERR_IO, io_read->line, TC_COLUMN_UNKNOWN,
                                  "input value out of range");
                return -1;
            }
            value = tc_value_make(io_read->type, tc_signed_to_bits(io_read->type, signed_value));
        }
    } else {
        if (sign == -1) {
            tc_diagnostic_set(diag, TC_ERR_IO, io_read->line, TC_COLUMN_UNKNOWN, "invalid input");
            return -1;
        }
        if (!tc_unsigned_in_range(abs_value, io_read->type)) {
            tc_diagnostic_set(diag, TC_ERR_IO, io_read->line, TC_COLUMN_UNKNOWN,
                              "input value out of range");
            return -1;
        }
        value = tc_value_make(io_read->type, abs_value);
    }

    symbol = tc_symbol_table_find(symbols, io_read->name);
    assert(symbol != NULL);
    slots[symbol->slot] = value;
    return 0;
}

/*
 * @brief 求值算术操作数
 * @param operand       操作数指针
 * @param expected_type 期望的上下文类型（字面量按此类型构造 TcValue）
 * @param slots         运行时变量槽位数组
 * @param symbols       全局符号表（用于变量查找）
 * @return 操作数的 TcValue
 * @note 字面量按 expected_type 构造 TcValue；变量从 slots 中按 symbol->slot 读取
 */
static TcValue tc_eval_operand(const TcOperand *operand, TcIntType expected_type,
                               const TcValue *slots, const TcSymbolTable *symbols) {
    if (operand->kind == TC_OPERAND_LIT) {
        return tc_literal_to_value(&operand->u.lit, expected_type);
    }
    {
        const TcSymbol *symbol = tc_symbol_table_find(symbols, operand->u.name);
        assert(symbol != NULL);
        return slots[symbol->slot];
    }
}

/*
 * @brief 求值 RHS 表达式，结果写入 *out
 * @param rhs           右值指针
 * @param expected_type 期望的结果类型
 * @param slots         运行时变量槽位数组
 * @param symbols       全局符号表
 * @param out           输出参数，求值结果 TcValue
 * @param diag          诊断对象
 * @param line          当前语句行号
 * @return 成功返回 0；运行时错误（除零/溢出）返回 -1 并设置 diag
 * @note 三种形式：字面量、算术运算、cast
 */
static int tc_eval_rhs(const TcRhs *rhs, TcIntType expected_type, const TcValue *slots,
                       const TcSymbolTable *symbols, TcValue *out, TcDiagnostic *diag,
                       int line) {
    if (rhs->kind == TC_RHS_LIT) {
        *out = tc_literal_to_value(&rhs->u.lit, expected_type);
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        TcValue lhs =
            tc_eval_operand(&rhs->u.arith.lhs, rhs->u.arith.type, slots, symbols);
        TcValue rhs_value =
            tc_eval_operand(&rhs->u.arith.rhs, rhs->u.arith.type, slots, symbols);
        return tc_exec_arith(rhs->u.arith.op, rhs->u.arith.type, rhs->u.arith.mode, &lhs,
                             &rhs_value, out, diag, line);
    }

    {
        const TcSymbol *source = tc_symbol_table_find(symbols, rhs->u.cast.source);
        assert(source != NULL);
        const TcValue *src_value = &slots[source->slot];
        return tc_exec_cast(rhs->u.cast.target, rhs->u.cast.mode, src_value, out, diag, line);
    }
}

/*
 * @brief 执行单条语句
 * @param stmt    待执行的语句
 * @param slots   运行时变量槽位数组
 * @param symbols 全局符号表
 * @param diag    诊断对象
 * @return 成功返回 0；运行时错误返回 -1 并设置 diag
 */
int tc_execute_statement(const TcStatement *stmt, TcValue *slots, const TcSymbolTable *symbols,
                         TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        const TcSymbol *symbol = tc_symbol_table_find(symbols, var_def->name);
        TcValue value;

        if (!var_def->has_rhs) {
            return 0;
        }
        if (tc_eval_rhs(&var_def->rhs, var_def->type, slots, symbols, &value, diag,
                        var_def->line) != 0) {
            return -1;
        }
        slots[symbol->slot] = value;
    } else if (stmt->kind == TC_STMT_CONST_DEF) {
        const TcConstDef *const_def = &stmt->u.const_def;
        const TcSymbol *symbol = tc_symbol_table_find(symbols, const_def->name);
        if (symbol->has_const_value) {
            slots[symbol->slot] = symbol->const_value;
        }
    } else if (stmt->kind == TC_STMT_ASSIGN) {
        const TcAssign *assign = &stmt->u.assign;
        const TcSymbol *symbol = tc_symbol_table_find(symbols, assign->name);
        TcValue value;

        if (tc_eval_rhs(&assign->rhs, symbol->type, slots, symbols, &value, diag, assign->line) !=
            0) {
            return -1;
        }
        slots[symbol->slot] = value;
    } else if (stmt->kind == TC_STMT_WRITE) {
        if (tc_exec_io_write(&stmt->u.io_write, slots, symbols, 0, diag) != 0) {
            return -1;
        }
    } else if (stmt->kind == TC_STMT_WRITELN) {
        if (tc_exec_io_write(&stmt->u.io_write, slots, symbols, 1, diag) != 0) {
            return -1;
        }
    } else if (stmt->kind == TC_STMT_READ) {
        if (tc_exec_io_read(&stmt->u.io_read, slots, symbols, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * @brief 程序执行主循环：顺序遍历语句列表，更新变量槽
 * @param program 已类型化的程序（含语句列表和符号表）
 * @param diag    诊断对象
 * @return 成功返回 0；运行时错误返回 -1 并设置 diag
 * @note 采用"变量槽位"模型：分配 symbols.count 个 TcValue 槽，按 TcSymbol.slot 索引
 * @note 无变量时直接返回成功（空程序）
 */
int tc_execute(const TcTypedProgram *program, TcDiagnostic *diag) {
    TcValue *slots = NULL;
    size_t i = 0;

    if (program->symbols.count > 0) {
        slots = (TcValue *)malloc(program->symbols.count * sizeof(TcValue));
        if (!slots) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "out of memory");
            return -1;
        }
        memset(slots, 0xFE, program->symbols.count * sizeof(TcValue));
    }

    for (i = 0; i < program->program.count; i++) {
        const TcStatement *stmt = &program->program.items[i];
        if (tc_execute_statement(stmt, slots, &program->symbols, diag) != 0) {
            free(slots);
            return -1;
        }
    }

    free(slots);
    return 0;
}
