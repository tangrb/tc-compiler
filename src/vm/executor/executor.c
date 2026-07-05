/*
 * executor.c — TC 执行引擎实现
 *
 * 采用「变量槽位」模型：分配 symbols.count 个 TcValue 槽，按 TcSymbol.slot 索引。
 * 执行流程：
 *   1. 分配 slots[] 并填充未初始化哨兵（tc_slots_init_uninitialized）
 *   2. 顺序遍历语句列表，对每条语句 dispatch 到对应的处理逻辑
 *   3. var/let 定义：求值 RHS → 写入新槽；赋值：求值 RHS → 覆盖已有槽
 *   4. write/writeln：格式化输出到 stdout；read：从 stdin 解析十进制整数
 *
 * 算术/cast 语义委托给 semantics.c，保证与 AOT 行为一致。
 */
#include "tc_executor.h"

#include "tc_diagnostic.h"
#include "tc_io.h"
#include "tc_semantics.h"
#include "tc_symbol.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  格式化输出辅助（委托 tc_io.c）                                       */
/* ------------------------------------------------------------------ */

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

    if (tc_io_write_value(&value, io_write->fmt, newline, stdout) != 0) {
        tc_diagnostic_set(diag, TC_ERR_IO, io_write->line, TC_COLUMN_UNKNOWN, "output failed");
        return -1;
    }
    return 0;
}

/* 委托 tc_io.c 处理 stdin 输入 */

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
    const TcSymbol *symbol = NULL;
    uint64_t bits = 0;

    if (tc_io_read_value(io_read->type, &bits, diag, io_read->line) != 0) {
        return -1;
    }

    symbol = tc_symbol_table_find(symbols, io_read->name);
    assert(symbol != NULL);
    slots[symbol->slot] = tc_value_make(io_read->type, bits);
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

    if (rhs->kind == TC_RHS_COMPARE) {
        TcValue lhs =
            tc_eval_operand(&rhs->u.compare.lhs, rhs->u.compare.type, slots, symbols);
        TcValue rhs_value =
            tc_eval_operand(&rhs->u.compare.rhs, rhs->u.compare.type, slots, symbols);
        return tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type, &lhs, &rhs_value, out, diag,
                               line);
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        TcValue lhs = tc_eval_operand(&rhs->u.logic_bin.lhs, TC_BOOL, slots, symbols);
        if (rhs->u.logic_bin.op == TC_LOGIC_AND && lhs.bits == 0) {
            *out = tc_value_make(TC_BOOL, 0);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs.bits != 0) {
            *out = tc_value_make(TC_BOOL, 1);
            return 0;
        }
        {
            TcValue rhs_value =
                tc_eval_operand(&rhs->u.logic_bin.rhs, TC_BOOL, slots, symbols);
            return tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_value, out, diag, line);
        }
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        TcValue operand =
            tc_eval_operand(&rhs->u.logic_un.operand, TC_BOOL, slots, symbols);
        return tc_exec_logic_unary(rhs->u.logic_un.op, &operand, out, diag, line);
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
            return 0;  /* 无初始化表达式的 var，槽位保持未初始化哨兵 */
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
        tc_slots_init_uninitialized(slots, program->symbols.count);
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
