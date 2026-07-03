/*
 * executor.c — TC 执行引擎实现
 *
 * 采用「变量槽位」模型：分配 symbols.count 个 TcValue 槽，按 TcSymbol.slot 索引。
 * 执行流程：
 *   1. 分配 slots[] 并用 0xFE 填充（便于调试时识别未初始化的槽）
 *   2. 顺序遍历语句列表，对每条语句 dispatch 到对应的处理逻辑
 *   3. var/let 定义：求值 RHS → 写入新槽；赋值：求值 RHS → 覆盖已有槽
 *   4. write/writeln：格式化输出到 stdout；read：从 stdin 解析十进制整数
 *
 * 算术/cast 语义委托给 semantics.c，保证与 AOT 行为一致。
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

/* ------------------------------------------------------------------ */
/*  格式化输出辅助                                                       */
/* ------------------------------------------------------------------ */

/*
 * @brief 按格式符号将 TcValue 写入 stdout
 * @param type  值的整数类型
 * @param fmt   格式说明符
 * @param value 待输出的运行时值
 * @param out   输出流（stdout）
 * @return 成功返回 0；I/O 错误返回 -1
 *
 * 支持的格式：
 *   %d/%i — 有符号十进制；%u — 无符号十进制；%x — 小写十六进制；
 *   %X — 大写十六进制；%o — 八进制；%b — 二进制
 */
static int tc_write_formatted(TcIntType type, TcFormatSpec fmt, const TcValue *value, FILE *out) {
    int n = tc_type_bit_width(type);
    uint64_t mask = tc_mask_bits(n);
    uint64_t uval = tc_value_to_unsigned(type, value->bits) & mask;

    switch (fmt) {
    case TC_FMT_D:
    case TC_FMT_I:
        if (!tc_type_is_signed(type)) {
            if (fprintf(out, "%llu", (unsigned long long)uval) < 0) {
                return -1;
            }
        } else {
            int64_t sval = tc_bits_to_signed(type, value->bits);
            if (fprintf(out, "%lld", (long long)sval) < 0) {
                return -1;
            }
        }
        break;
    case TC_FMT_U:
        if (fprintf(out, "%llu", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_X:
        if (fprintf(out, "%llx", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_XU:
        if (fprintf(out, "%llX", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_O:
        if (fprintf(out, "%llo", (unsigned long long)uval) < 0) {
            return -1;
        }
        break;
    case TC_FMT_B: {
        int i = 0;
        /* 从最高位到最低位逐位输出 */
        for (i = n - 1; i >= 0; i--) {
            if (fputc((uval >> i) & 1 ? '1' : '0', out) == EOF) {
                return -1;
            }
        }
        break;
    }
    default:
        return -1;
    }
    return 0;
}

/*
 * @brief 将 TcValue 输出到 stdout
 * @param value   待输出的运行时值
 * @param fmt     格式说明符（TC_FMT_NONE 时按类型默认输出）
 * @param newline 是否追加换行符
 * @return 成功返回 0；I/O 错误返回 -1
 */
static int tc_exec_write_value(const TcValue *value, TcFormatSpec fmt, int newline) {
    if (fmt != TC_FMT_NONE) {
        if (tc_write_formatted(value->type, fmt, value, stdout) != 0) {
            return -1;
        }
    } else if (tc_type_is_signed(value->type)) {
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

/* ------------------------------------------------------------------ */
/*  I/O 语句实现                                                        */
/* ------------------------------------------------------------------ */

/*
 * @brief 执行 write / writeln 语句
 * @param io_write 输出语句结构
 * @param slots    运行时变量槽位数组
 * @param symbols  全局符号表（用于变量查找）
 * @param newline  writeln 时为 1
 * @param diag     诊断对象
 * @return 成功返回 0；I/O 错误返回 -1
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

    if (tc_exec_write_value(&value, io_write->fmt, newline) != 0) {
        tc_diagnostic_set(diag, TC_ERR_IO, io_write->line, TC_COLUMN_UNKNOWN, "output failed");
        return -1;
    }
    return 0;
}

/* 跳过 stdin 前导空白字符 */
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
 * @param c        当前已读取的首个字符（必须是 '-' 或数字才合法）
 * @param line     当前行号（错误定位）
 * @param diag     诊断对象
 * @param out_abs  输出：数字序列的绝对值
 * @param out_sign 输出：符号（1 或 -1）
 * @return 成功返回 0；输入非法或超出 uint64 范围返回 -1
 *
 * @note 提取 signed/unsigned 输入的公共数字读取逻辑。不负责值域检查，
 *       调用方按 signed/unsigned 类型自行判断。
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
        /* abs_value * 10 + digit <= UINT64_MAX */
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
 * @brief 执行 read 语句：从 stdin 读取十进制整数并存入目标变量槽
 * @param io_read  read 语句结构
 * @param slots    运行时变量槽位数组（可写）
 * @param symbols  全局符号表
 * @param diag     诊断对象
 * @return 成功返回 0；输入非法或超范围返回 -1
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

    /* 按目标类型的有符号性做范围检查并构造 TcValue */
    if (tc_type_is_signed(io_read->type)) {
        if (sign == -1 && abs_value == TC_INT64_MIN_ABS_MAGNITUDE) {
            /* INT64_MIN 的特殊情况：abs_value == 2^63 需要单独处理 */
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

/* ------------------------------------------------------------------ */
/*  RHS 求值                                                           */
/* ------------------------------------------------------------------ */

/** 求值算术操作数：字面量按期望类型构造 TcValue；变量从 slots 中按 symbol->slot 读取 */
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
 * @param line          当前语句行号（错误定位）
 * @return 成功返回 0；运行时错误（除零/溢出）返回 -1 并设置 diag
 *
 * 四种形式：
 *   TC_RHS_LIT    → tc_literal_to_value
 *   TC_RHS_ARITH  → tc_exec_arith（委托 semantics.c）
 *   TC_RHS_UNARY  → tc_exec_unary（委托 semantics.c）
 *   TC_RHS_CAST   → tc_exec_cast（委托 semantics.c）
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

    if (rhs->kind == TC_RHS_UNARY) {
        TcValue operand =
            tc_eval_operand(&rhs->u.unary.operand, rhs->u.unary.type, slots, symbols);
        return tc_exec_unary(rhs->u.unary.op, rhs->u.unary.type, rhs->u.unary.mode, &operand,
                             out, diag, line);
    }

    {
        const TcSymbol *source = tc_symbol_table_find(symbols, rhs->u.cast.source);
        assert(source != NULL);
        const TcValue *src_value = &slots[source->slot];
        return tc_exec_cast(rhs->u.cast.target, rhs->u.cast.mode, src_value, out, diag, line);
    }
}

/* ------------------------------------------------------------------ */
/*  语句执行入口                                                        */
/* ------------------------------------------------------------------ */

int tc_execute_statement(const TcStatement *stmt, TcValue *slots, const TcSymbolTable *symbols,
                         TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        const TcSymbol *symbol = tc_symbol_table_find(symbols, var_def->name);
        TcValue value;

        if (!var_def->has_rhs) {
            return 0;  /* 无初始化表达式的 var，槽位保持 0xFE */
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
            /* let 常量使用编译期求值结果 */
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

/* ------------------------------------------------------------------ */
/*  程序执行主循环                                                       */
/* ------------------------------------------------------------------ */

int tc_execute(const TcTypedProgram *program, TcDiagnostic *diag) {
    TcValue *slots = NULL;
    size_t i = 0;

    if (program->symbols.count > 0) {
        slots = (TcValue *)malloc(program->symbols.count * sizeof(TcValue));
        if (!slots) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "out of memory");
            return -1;
        }
        /* 用 0xFE 填充初始化，便于调试时识别未初始化的槽位 */
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
