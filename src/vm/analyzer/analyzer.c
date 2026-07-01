/*
 * analyzer.c — TC 静态分析器实现
 *
 * 实现「先检后跑」原则：全程序静态分析通过后才允许 Executor 运行。
 *
 * Pass 1（tc_pass1_collect_symbols）：
 *   遍历所有 var 定义，建立符号表并分配 slot（0, 1, 2, ...）。
 *
 * Pass 2（tc_pass2_type_check）：
 *   维护「当前可见符号」表，按源序处理每条语句：
 *   - var 定义：检查 RHS 类型与字面量范围，再将变量加入可见表
 *   - 赋值：检查目标变量已定义，RHS 类型与目标一致
 *
 * v1.0 为单全局作用域，无块级作用域或函数。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_analyzer.h"

#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * @brief 初始化符号表为空状态
 * @param table 待初始化的符号表指针
 */
void tc_symbol_table_init(TcSymbolTable *table) {
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
}

/*
 * @brief 释放符号表中所有符号的动态名称及符号数组
 * @param table 待释放的符号表指针
 */
void tc_symbol_table_free(TcSymbolTable *table) {
    size_t i = 0;
    for (i = 0; i < table->count; i++) {
        free(table->symbols[i].name);
    }
    free(table->symbols);
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
}

/*
 * @brief 在线性符号表中按名称查找符号
 * @param table 符号表指针
 * @param name  要查找的变量名
 * @return 找到返回对应 TcSymbol 指针；未找到返回 NULL
 * @note TC 变量数量少，线性扫描即可，无需哈希
 */
const TcSymbol *tc_symbol_table_find(const TcSymbolTable *table, const char *name) {
    size_t i = 0;
    for (i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) {
            return &table->symbols[i];
        }
    }
    return NULL;
}

/*
 * @brief 向符号表中添加一个新符号
 * @param table    符号表指针
 * @param name     变量名（会被 strdup 复制）
 * @param type     变量的静态类型
 * @param slot     运行时槽位索引
 * @param def_line 定义行号
 * @param diag     诊断对象
 * @return 成功返回 0；重复定义或内存不足返回 -1 并设置 diag
 * @note 不检查重复（重复定义由调用方 tc_pass1_collect_symbols 负责检测）
 */
static int tc_symbol_table_add(TcSymbolTable *table, const char *name, TcIntType type,
                               int slot, int def_line, TcDiagnostic *diag) {
    if (table->count == table->capacity) {
        size_t new_cap = table->capacity == 0 ? 8 : table->capacity * 2;
        TcSymbol *symbols = (TcSymbol *)realloc(table->symbols, new_cap * sizeof(TcSymbol));
        if (!symbols) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, def_line, TC_COLUMN_UNKNOWN, "out of memory");
            return -1;
        }
        table->symbols = symbols;
        table->capacity = new_cap;
    }
    table->symbols[table->count].name = strdup(name);
    if (!table->symbols[table->count].name) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, def_line, TC_COLUMN_UNKNOWN, "out of memory");
        return -1;
    }
    table->symbols[table->count].type = type;
    table->symbols[table->count].slot = slot;
    table->symbols[table->count].def_line = def_line;
    table->count++;
    return 0;
}

/*
 * @brief 初始化已类型化的程序（初始空程序 + 空符号表）
 * @param program 待初始化的 TcTypedProgram 指针
 */
void tc_typed_program_init(TcTypedProgram *program) {
    tc_program_init(&program->program);
    tc_symbol_table_init(&program->symbols);
}

/*
 * @brief 释放已类型化程序（释放程序语句和符号表）
 * @param program 待释放的 TcTypedProgram 指针
 */
void tc_typed_program_free(TcTypedProgram *program) {
    tc_program_free(&program->program);
    tc_symbol_table_free(&program->symbols);
}

/*
 * @brief 推断 RHS 的结果类型（用于赋值类型一致性检查）
 * @param rhs      右值指针
 * @param lhs_type 左值类型（字面量 RHS 无类型信息，借用 lhs_type）
 * @return RHS 的结果类型
 */
static TcIntType tc_rhs_result_type(const TcRhs *rhs, TcIntType lhs_type) {
    if (rhs->kind == TC_RHS_LIT) {
        return lhs_type;
    }
    if (rhs->kind == TC_RHS_ARITH) {
        return rhs->u.arith.type;
    }
    return rhs->u.cast.target;
}

/*
 * @brief 检查单个操作数的类型合法性
 * @param operand  待检查的操作数指针
 * @param expected 期望的上下文类型
 * @param symbols  当前可见符号表
 * @param line     当前行号
 * @param diag     诊断对象
 * @return 检查通过返回 0；字面量超范围或变量未定义/类型不匹配返回 -1 并设置 diag
 * @note 变量须已定义且类型与 expected 一致；字面量须在 expected 类型范围内
 */
static int tc_check_operand(const TcOperand *operand, TcIntType expected, const TcSymbolTable *symbols,
                            int line, TcDiagnostic *diag) {
    char msg[128];

    if (operand->kind == TC_OPERAND_LIT) {
        if (!tc_literal_fits_type(operand->u.lit, expected)) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                              "literal out of range for context type");
            return -1;
        }
        return 0;
    }

    {
        const TcSymbol *symbol = tc_symbol_table_find(symbols, operand->u.name);
        if (!symbol) {
            snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (symbol->type != expected) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "operand type does not match operation type");
            return -1;
        }
    }
    return 0;
}

/*
 * @brief 检查 RHS 与左值（变量）类型的兼容性
 * @param rhs      右值指针
 * @param lhs_type 左值类型（变量声明的类型）
 * @param symbols  当前可见符号表
 * @param line     当前行号
 * @param diag     诊断对象
 * @return 检查通过返回 0；失败返回 -1 并设置 diag
 * @note 额外规则：div/mod 不允许使用 overflow 模式
 * @note 对于 TC_RHS_LIT：检查字面量能否放入 lhs_type
 * @note 对于 TC_RHS_ARITH：校验溢出模式合法性 + 操作数类型 + 结果类型与 lhs_type 一致
 * @note 对于 TC_RHS_CAST：源变量须已定义，目标类型须与 lhs_type 一致
 */
static int tc_check_rhs(const TcRhs *rhs, TcIntType lhs_type, const TcSymbolTable *symbols,
                        int line, TcDiagnostic *diag) {
    char msg[128];

    if (rhs->kind == TC_RHS_LIT) {
        if (!tc_literal_fits_type(rhs->u.lit, lhs_type)) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                              "literal out of range for variable type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        if ((rhs->u.arith.op == TC_DIV || rhs->u.arith.op == TC_MOD) &&
            rhs->u.arith.mode == TC_OVERFLOW) {
            tc_diagnostic_set(diag, TC_ERR_OVERFLOW_MODE, line, TC_COLUMN_UNKNOWN,
                              "div/mod do not support overflow mode");
            return -1;
        }
        if (tc_check_operand(&rhs->u.arith.lhs, rhs->u.arith.type, symbols, line, diag) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.arith.rhs, rhs->u.arith.type, symbols, line, diag) != 0) {
            return -1;
        }
        if (rhs->u.arith.type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    /* cast：源变量须已定义，目标类型须与左值类型一致 */
    {
        const TcSymbol *source = tc_symbol_table_find(symbols, rhs->u.cast.source);
        if (!source) {
            snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.cast.source);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (rhs->u.cast.target != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast target type does not match variable type");
            return -1;
        }
    }
    return 0;
}

/*
 * @brief 第一遍扫描：仅处理 var 定义，建立符号表并分配运行时 slot
 * @param program 源程序指针
 * @param symbols 输出参数，建立的符号表
 * @param diag    诊断对象
 * @return 成功返回 0；重复定义返回 -1 并设置 diag
 * @note slot 顺序即变量首次定义的顺序，Executor 用 slots[slot] 存运行时值
 * @note 赋值语句在第一遍中被忽略
 */
static int tc_pass1_collect_symbols(TcProgram *program, TcSymbolTable *symbols,
                                    TcDiagnostic *diag) {
    size_t i = 0;
    int next_slot = 0;

    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];
        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;
            if (tc_symbol_table_find(symbols, var_def->name)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
                tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line,
                                  TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (tc_symbol_table_add(symbols, var_def->name, var_def->type, next_slot,
                                    var_def->line, diag) != 0) {
                return -1;
            }
            next_slot++;
        }
    }
    return 0;
}

/*
 * @brief 第二遍扫描：按源序模拟执行前的可见性，做类型检查
 * @param program 源程序指针
 * @param diag    诊断对象
 * @return 成功返回 0；类型错误或未定义变量返回 -1 并设置 diag
 * @note var 定义在 RHS 检查通过后才加入 visible 表（故 RHS 不能引用自身）
 * @note 赋值语句只能引用已定义的变量
 * @note 使用独立的 visible 符号表模拟运行时可见性
 */
static int tc_pass2_type_check(TcProgram *program, TcDiagnostic *diag) {
    TcSymbolTable visible;
    size_t i = 0;

    tc_symbol_table_init(&visible);

    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];
        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;
            const TcSymbol *existing = tc_symbol_table_find(&visible, var_def->name);

            if (existing) {
                char msg[128];
                snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
                tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line,
                                  TC_COLUMN_UNKNOWN, msg);
                tc_symbol_table_free(&visible);
                return -1;
            }
            if (tc_check_rhs(&var_def->rhs, var_def->type, &visible, var_def->line, diag) != 0) {
                tc_symbol_table_free(&visible);
                return -1;
            }
            if (tc_symbol_table_add(&visible, var_def->name, var_def->type, (int)i,
                                    var_def->line, diag) != 0) {
                tc_symbol_table_free(&visible);
                return -1;
            }
        } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            const TcIoWrite *io_write = &stmt->u.io_write;
            if (tc_check_operand(&io_write->operand, io_write->type, &visible, io_write->line,
                                 diag) != 0) {
                tc_symbol_table_free(&visible);
                return -1;
            }
        } else if (stmt->kind == TC_STMT_READ) {
            const TcRead *io_read = &stmt->u.io_read;
            const TcSymbol *target = tc_symbol_table_find(&visible, io_read->name);
            char msg[128];

            if (!target) {
                snprintf(msg, sizeof(msg), "undefined variable '%s'", io_read->name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, io_read->line,
                                  TC_COLUMN_UNKNOWN, msg);
                tc_symbol_table_free(&visible);
                return -1;
            }
            if (target->type != io_read->type) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, io_read->line, TC_COLUMN_UNKNOWN,
                                  "read type does not match variable type");
                tc_symbol_table_free(&visible);
                return -1;
            }
        } else {
            const TcAssign *assign = &stmt->u.assign;
            const TcSymbol *target = tc_symbol_table_find(&visible, assign->name);
            char msg[128];

            if (!target) {
                snprintf(msg, sizeof(msg), "undefined variable '%s'", assign->name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, assign->line,
                                  TC_COLUMN_UNKNOWN, msg);
                tc_symbol_table_free(&visible);
                return -1;
            }
            if (tc_check_rhs(&assign->rhs, target->type, &visible, assign->line, diag) != 0) {
                tc_symbol_table_free(&visible);
                return -1;
            }
            if (tc_rhs_result_type(&assign->rhs, target->type) != target->type) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, assign->line, TC_COLUMN_UNKNOWN,
                                  "assignment type does not match rhs result type");
                tc_symbol_table_free(&visible);
                return -1;
            }
        }
    }

    tc_symbol_table_free(&visible);
    return 0;
}

/*
 * @brief 静态分析总入口
 * @param program 待分析的源程序（分析成功后所有权被转移给 out）
 * @param out     输出参数，分析通过后产出 TcTypedProgram（语句列表 + 全局符号表）
 * @param diag    诊断对象
 * @return 分析成功返回 0；失败返回 -1 并设置 diag（此时 out 被释放）
 * @note 将 program 移入 out->program（避免深拷贝），再执行两遍扫描
 */
int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag) {
    tc_typed_program_init(out);
    out->program = *program;
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;

    if (tc_pass1_collect_symbols(&out->program, &out->symbols, diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }
    if (tc_pass2_type_check(&out->program, diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }
    return 0;
}

/*
 * @brief 对单条语句做增量静态分析（REPL 会话）
 * @param stmt    待分析的语句
 * @param symbols 会话符号表（var 定义成功后会追加新符号并分配 slot）
 * @param diag    诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag
 */
int tc_analyze_statement(const TcStatement *stmt, TcSymbolTable *symbols, TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        char msg[128];

        if (tc_symbol_table_find(symbols, var_def->name)) {
            snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (tc_check_rhs(&var_def->rhs, var_def->type, symbols, var_def->line, diag) != 0) {
            return -1;
        }
        return tc_symbol_table_add(symbols, var_def->name, var_def->type, (int)symbols->count,
                                   var_def->line, diag);
    }

    if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        const TcIoWrite *io_write = &stmt->u.io_write;
        return tc_check_operand(&io_write->operand, io_write->type, symbols, io_write->line, diag);
    }

    if (stmt->kind == TC_STMT_READ) {
        const TcRead *io_read = &stmt->u.io_read;
        const TcSymbol *target = tc_symbol_table_find(symbols, io_read->name);
        char msg[128];

        if (!target) {
            snprintf(msg, sizeof(msg), "undefined variable '%s'", io_read->name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, io_read->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (target->type != io_read->type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, io_read->line, TC_COLUMN_UNKNOWN,
                              "read type does not match variable type");
            return -1;
        }
        return 0;
    }

    {
        const TcAssign *assign = &stmt->u.assign;
        const TcSymbol *target = tc_symbol_table_find(symbols, assign->name);
        char msg[128];

        if (!target) {
            snprintf(msg, sizeof(msg), "undefined variable '%s'", assign->name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, assign->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (tc_check_rhs(&assign->rhs, target->type, symbols, assign->line, diag) != 0) {
            return -1;
        }
        if (tc_rhs_result_type(&assign->rhs, target->type) != target->type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, assign->line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
    }
    return 0;
}
