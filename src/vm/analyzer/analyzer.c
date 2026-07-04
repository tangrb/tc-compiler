/*
 * analyzer.c — TC 静态分析器实现
 *
 * 两遍扫描架构：
 *   Pass 1 — 符号收集：扫描所有 var/let 定义，分配运行时 slot，检测重复定义
 *   Pass 2 — 类型与语义检查：按语句顺序校验类型兼容性、字面量范围、overflow 模式合法性、
 *            let 常量编译期求值、未初始化变量警告
 *
 * 分析通过后产出 TcTypedProgram，Executor 可直接消费。
 */
#include "tc_analyzer.h"

#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_semantics.h"
#include "tc_symbol.h"
#include "tc_warning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_const_eval.h"

/* ------------------------------------------------------------------ */
/*  TcTypedProgram 生命周期管理                                          */
/* ------------------------------------------------------------------ */

void tc_typed_program_init(TcTypedProgram *program) {
    tc_program_init(&program->program);
    tc_symbol_table_init(&program->symbols);
    tc_warning_list_init(&program->warnings);
}

void tc_typed_program_free(TcTypedProgram *program) {
    tc_program_free(&program->program);
    tc_symbol_table_free(&program->symbols);
    tc_warning_list_free(&program->warnings);
}

/* ------------------------------------------------------------------ */
/*  字面量检查辅助                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 检查 TcLiteral 能否放入目标类型
 * @return 检查通过返回 0；失败返回 -1 并设置 diag
 */
static int tc_check_literal(const TcLiteral *lit, TcIntType expected, int line,
                            TcDiagnostic *diag) {
    TcErrorKind err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
    if (!tc_literal_fits_context(lit, expected, &err_kind)) {
        if (err_kind == TC_ERR_LITERAL_TYPE) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                              "literal type does not match context");
        } else {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                              "literal out of range for context type");
        }
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  初始化追踪 & 未初始化变量警告                                         */
/* ------------------------------------------------------------------ */

/** 初始化历史上下文，供未初始化变量检查使用 */
typedef struct {
    const TcProgram *program;               /* 完整程序（文件模式）；REPL 模式下为 NULL */
    const int *last_init_stmt_index;        /* slot → 最后初始化语句序号，-1 表示从未 */
} TcInitHistory;

/*
 * @brief 判断变量在 stmt_index 之前是否已被初始化
 *
 * 查询顺序：sym->initialized（定义时有值）→ last_init_stmt_index 缓存
 * → 遍历 program 中从 def_stmt_index+1 到 before_index-1 的 ASSIGN/READ 语句
 */
static int tc_variable_is_initialized_before(const TcInitHistory *hist, const TcSymbol *sym,
                                             size_t before_index) {
    if (sym->initialized) {
        return 1;
    }
    if (hist->last_init_stmt_index != NULL) {
        int last = hist->last_init_stmt_index[sym->slot];
        return last >= 0 && (size_t)last > (size_t)sym->def_stmt_index &&
               (size_t)last < before_index;
    }
    if (hist->program != NULL) {
        size_t i = 0;
        for (i = (size_t)sym->def_stmt_index + 1; i < before_index; i++) {
            const TcStatement *stmt = &hist->program->items[i];
            if (stmt->kind == TC_STMT_ASSIGN && strcmp(stmt->u.assign.name, sym->name) == 0) {
                return 1;
            }
            if (stmt->kind == TC_STMT_READ && strcmp(stmt->u.io_read.name, sym->name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * @brief 对可能未初始化的变量引用发出警告
 */
static void tc_maybe_warn_uninitialized(const TcInitHistory *hist, const TcSymbol *sym,
                                        size_t stmt_index, int line, TcWarningList *warnings) {
    char msg[128];
    if (!warnings) {
        return;
    }
    if (sym->sym_kind == TC_SYM_CONSTANT) {
        return;  /* let 常量始终有编译期值，不需要警告 */
    }
    if (tc_variable_is_initialized_before(hist, sym, stmt_index)) {
        return;
    }
    snprintf(msg, sizeof(msg), "use of possibly uninitialized variable '%s'", sym->name);
    tc_warning_list_add(warnings, TC_WARN_UNINITIALIZED_VARIABLE, line, msg);
}

/* ------------------------------------------------------------------ */
/*  操作数与 RHS 检查                                                    */
/* ------------------------------------------------------------------ */

/*
 * @brief 检查操作数的类型兼容性与变量定义存在性
 * @param self_name 若非 NULL，表示当前定义中的变量名（用于自引用检测）
 */
static int tc_check_operand(const TcOperand *operand, TcIntType expected,
                            const TcSymbolTable *symbols, const TcInitHistory *hist,
                            size_t stmt_index, int line, TcDiagnostic *diag,
                            TcWarningList *warnings, const char *self_name) {
    char msg[128];

    if (operand->kind == TC_OPERAND_LIT) {
        return tc_check_literal(&operand->u.lit, expected, line, diag);
    }

    {
        const TcSymbol *symbol = NULL;

        if (self_name && strcmp(operand->u.name, self_name) == 0) {
            snprintf(msg, sizeof(msg),
                     "variable '%s' cannot reference itself in its initializer", self_name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        symbol = tc_symbol_table_find(symbols, operand->u.name);
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
        tc_maybe_warn_uninitialized(hist, symbol, stmt_index, line, warnings);
    }
    return 0;
}

/*
 * @brief 校验格式说明符与操作数类型的匹配关系
 *
 * %d/%i 要求有符号类型；%u 要求无符号类型；%x/%X/%o/%b 无限制
 */
static int tc_check_io_format(TcIntType type, TcFormatSpec fmt, int line, TcDiagnostic *diag) {
    if (fmt == TC_FMT_NONE) {
        return 0;
    }
    if (fmt == TC_FMT_T) {
        if (!tc_type_is_bool(type)) {
            tc_diagnostic_set(diag, TC_ERR_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "%t requires bool type");
            return -1;
        }
        return 0;
    }
    if (tc_type_is_bool(type)) {
        tc_diagnostic_set(diag, TC_ERR_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "bool type requires %t format specifier");
        return -1;
    }
    if (fmt == TC_FMT_D || fmt == TC_FMT_I) {
        if (!tc_type_is_signed(type)) {
            tc_diagnostic_set(diag, TC_ERR_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "%d requires signed type");
            return -1;
        }
        return 0;
    }
    if (fmt == TC_FMT_U) {
        if (tc_type_is_signed(type)) {
            tc_diagnostic_set(diag, TC_ERR_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "%u requires unsigned type");
            return -1;
        }
        return 0;
    }
    if (fmt == TC_FMT_X || fmt == TC_FMT_XU || fmt == TC_FMT_O || fmt == TC_FMT_B) {
        return 0;
    }
    return 0;
}

/*
 * @brief 对 RHS 进行类型检查
 * @param lhs_type  赋值目标的类型
 * @param self_name 自引用检测（用于 var 初始化器）
 */
static int tc_check_rhs(const TcRhs *rhs, TcIntType lhs_type, const TcSymbolTable *symbols,
                        const TcInitHistory *hist, size_t stmt_index, int line,
                        TcDiagnostic *diag, TcWarningList *warnings, const char *self_name) {
    char msg[128];

    if (rhs->kind == TC_RHS_LIT) {
        if (!tc_literal_fits_context(&rhs->u.lit, lhs_type, NULL)) {
            TcErrorKind err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
            tc_literal_fits_context(&rhs->u.lit, lhs_type, &err_kind);
            if (err_kind == TC_ERR_LITERAL_TYPE) {
                tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                                  "literal type does not match variable type");
            } else {
                tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                                  "literal out of range for variable type");
            }
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        /* div/mod 不支持 wrap 模式（TC 语言标准规定） */
        if ((rhs->u.arith.op == TC_DIV || rhs->u.arith.op == TC_MOD) &&
            rhs->u.arith.mode == TC_ARITH_WRAP) {
            tc_diagnostic_set(diag, TC_ERR_OVERFLOW_MODE, line, TC_COLUMN_UNKNOWN,
                              "div/mod do not support wrap mode");
            return -1;
        }
        if (tc_check_operand(&rhs->u.arith.lhs, rhs->u.arith.type, symbols, hist, stmt_index,
                             line, diag, warnings, self_name) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.arith.rhs, rhs->u.arith.type, symbols, hist, stmt_index,
                             line, diag, warnings, self_name) != 0) {
            return -1;
        }
        if (rhs->u.arith.type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        /* abs 不支持 wrap 模式 */
        if (rhs->u.unary.op == TC_UNARY_ABS && rhs->u.unary.mode == TC_ARITH_WRAP) {
            tc_diagnostic_set(diag, TC_ERR_OVERFLOW_MODE, line, TC_COLUMN_UNKNOWN,
                              "abs does not support wrap");
            return -1;
        }
        if (tc_check_operand(&rhs->u.unary.operand, rhs->u.unary.type, symbols, hist, stmt_index,
                             line, diag, warnings, self_name) != 0) {
            return -1;
        }
        if (rhs->u.unary.type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        if (tc_check_operand(&rhs->u.compare.lhs, rhs->u.compare.type, symbols, hist, stmt_index,
                             line, diag, warnings, self_name) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.compare.rhs, rhs->u.compare.type, symbols, hist, stmt_index,
                             line, diag, warnings, self_name) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(lhs_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        if (tc_check_operand(&rhs->u.logic_bin.lhs, TC_BOOL, symbols, hist, stmt_index, line, diag,
                             warnings, self_name) != 0) {
            return -1;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_AND) {
            if (rhs->u.logic_bin.lhs.kind == TC_OPERAND_LIT &&
                rhs->u.logic_bin.lhs.u.lit.is_bool &&
                rhs->u.logic_bin.lhs.u.lit.magnitude == 0) {
                /* 短路：false && rhs 不求值 rhs；仍校验 rhs 存在性与类型 */
                if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, symbols, hist,
                                     stmt_index, line, diag, NULL, self_name) != 0) {
                    return -1;
                }
            } else if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, symbols, hist,
                                        stmt_index, line, diag, warnings, self_name) != 0) {
                return -1;
            }
        } else if (rhs->u.logic_bin.lhs.kind == TC_OPERAND_LIT &&
                   rhs->u.logic_bin.lhs.u.lit.is_bool &&
                   rhs->u.logic_bin.lhs.u.lit.magnitude != 0) {
            /* 短路：true || rhs 不求值 rhs；仍校验 rhs 存在性与类型 */
            if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, symbols, hist,
                                 stmt_index, line, diag, NULL, self_name) != 0) {
                return -1;
            }
        } else if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, symbols, hist, stmt_index,
                                     line, diag, warnings, self_name) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(lhs_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        if (tc_check_operand(&rhs->u.logic_un.operand, TC_BOOL, symbols, hist, stmt_index, line,
                             diag, warnings, self_name) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(lhs_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_REF || rhs->kind == TC_RHS_CONST_CAST) {
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant reference is only allowed in let initializer");
        return -1;
    }

    {
        const TcSymbol *source = NULL;

        if (self_name && strcmp(rhs->u.cast.source, self_name) == 0) {
            snprintf(msg, sizeof(msg),
                     "variable '%s' cannot reference itself in its initializer", self_name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        source = tc_symbol_table_find(symbols, rhs->u.cast.source);
        if (!source) {
            snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.cast.source);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        tc_maybe_warn_uninitialized(hist, source, stmt_index, line, warnings);
        if (rhs->u.cast.target != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast target type does not match variable type");
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Pass 1 — 符号收集                                                   */
/* ------------------------------------------------------------------ */

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
                                    var_def->line, (int)i, TC_SYM_VARIABLE, var_def->has_rhs,
                                    diag) != 0) {
                return -1;
            }
            next_slot++;
        } else if (stmt->kind == TC_STMT_CONST_DEF) {
            const TcConstDef *const_def = &stmt->u.const_def;
            if (tc_symbol_table_find(symbols, const_def->name)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "duplicate definition of '%s'", const_def->name);
                tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, const_def->line,
                                  TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (tc_symbol_table_add(symbols, const_def->name, const_def->type, next_slot,
                                    const_def->line, (int)i, TC_SYM_CONSTANT, 1, diag) != 0) {
                return -1;
            }
            next_slot++;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Pass 2 — 类型与语义检查                                              */
/* ------------------------------------------------------------------ */

static int tc_pass2_type_check(TcProgram *program, TcSymbolTable *symbols, TcWarningList *warnings,
                               TcDiagnostic *diag) {
    TcSymbolTable visible;
    int *last_init = NULL;
    size_t i = 0;

    tc_symbol_table_init(&visible);

    /* 分配 last_init_stmt_index 缓存，初始化为 -1 */
    if (symbols->count > 0) {
        last_init = (int *)malloc(symbols->count * sizeof(int));
        if (!last_init) {
            tc_symbol_table_free(&visible);
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "out of memory");
            return -1;
        }
        for (i = 0; i < symbols->count; i++) {
            last_init[i] = -1;
        }
    }

    /* 预计算：扫描所有 ASSIGN/READ 语句，缓存每个符号的最后初始化位置 */
    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];
        if (stmt->kind == TC_STMT_ASSIGN) {
            const TcSymbol *sym = tc_symbol_table_find(symbols, stmt->u.assign.name);
            if (sym) {
                last_init[sym->slot] = (int)i;
            }
        } else if (stmt->kind == TC_STMT_READ) {
            const TcSymbol *sym = tc_symbol_table_find(symbols, stmt->u.io_read.name);
            if (sym) {
                last_init[sym->slot] = (int)i;
            }
        }
    }

    {
        TcInitHistory hist = {program, last_init};
    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];

        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;
            if (var_def->has_rhs) {
                if (tc_check_rhs(&var_def->rhs, var_def->type, &visible, &hist, i,
                                 var_def->line, diag, warnings, var_def->name) != 0) {
                    goto cleanup;
                }
            }
            /* 将 var 加入 visible 表，供后续语句引用 */
            if (tc_symbol_table_add(&visible, var_def->name, var_def->type, (int)i,
                                    var_def->line, (int)i, TC_SYM_VARIABLE, var_def->has_rhs,
                                    diag) != 0) {
                goto cleanup;
            }
        } else if (stmt->kind == TC_STMT_CONST_DEF) {
            const TcConstDef *const_def = &stmt->u.const_def;
            TcSymbol *global_sym = tc_symbol_table_find_mut(symbols, const_def->name);

            if (const_def->rhs.kind == TC_RHS_CAST) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, const_def->line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant initializer must be a constant expression");
                goto cleanup;
            }
            if (tc_resolve_const_value(global_sym, &const_def->rhs, &visible, symbols,
                                       const_def->line, diag) != 0) {
                goto cleanup;
            }
            /* 将 let 加入 visible 表 */
            if (tc_symbol_table_add(&visible, const_def->name, const_def->type, (int)i,
                                    const_def->line, (int)i, TC_SYM_CONSTANT, 1, diag) != 0) {
                goto cleanup;
            }
        } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            const TcIoWrite *io_write = &stmt->u.io_write;
            if (tc_check_io_format(io_write->type, io_write->fmt, io_write->line, diag) != 0) {
                goto cleanup;
            }
            if (tc_check_operand(&io_write->operand, io_write->type, &visible, &hist, i,
                                 io_write->line, diag, warnings, NULL) != 0) {
                goto cleanup;
            }
        } else if (stmt->kind == TC_STMT_READ) {
            const TcRead *io_read = &stmt->u.io_read;
            const TcSymbol *target = tc_symbol_table_find(&visible, io_read->name);
            char msg[128];

            if (!target) {
                snprintf(msg, sizeof(msg), "undefined variable '%s'", io_read->name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, io_read->line,
                                  TC_COLUMN_UNKNOWN, msg);
                goto cleanup;
            }
            if (target->type != io_read->type) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, io_read->line, TC_COLUMN_UNKNOWN,
                                  "read type does not match variable type");
                goto cleanup;
            }
        } else {
            const TcAssign *assign = &stmt->u.assign;
            const TcSymbol *target = tc_symbol_table_find(&visible, assign->name);
            const TcSymbol *global_target = tc_symbol_table_find(symbols, assign->name);
            char msg[128];

            if (!target) {
                snprintf(msg, sizeof(msg), "undefined variable '%s'", assign->name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, assign->line,
                                  TC_COLUMN_UNKNOWN, msg);
                goto cleanup;
            }
            if (global_target && global_target->sym_kind == TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, assign->line,
                                  TC_COLUMN_UNKNOWN, "cannot assign to constant");
                goto cleanup;
            }
            if (tc_check_rhs(&assign->rhs, target->type, &visible, &hist, i, assign->line, diag,
                             warnings, NULL) != 0) {
                goto cleanup;
            }
        }
    } /* end for(i) */
    } /* end block scope for hist */

    tc_symbol_table_free(&visible);
    free(last_init);
    return 0;

cleanup:
    tc_symbol_table_free(&visible);
    free(last_init);
    return -1;
}

/* ------------------------------------------------------------------ */
/*  tc_analyze — 两遍分析入口                                            */
/* ------------------------------------------------------------------ */

int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag) {
    tc_typed_program_init(out);
    /* 通过 struct 浅拷贝转移 program->items 所有权 */
    out->program = *program;
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;

    if (tc_pass1_collect_symbols(&out->program, &out->symbols, diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }
    if (tc_pass2_type_check(&out->program, &out->symbols, &out->warnings, diag) != 0) {
        tc_typed_program_free(out);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  tc_analyze_statement — REPL 增量分析                                 */
/* ------------------------------------------------------------------ */

int tc_analyze_statement(const TcStatement *stmt, TcSymbolTable *symbols,
                         TcReplAnalyzeCtx *repl_ctx, TcWarningList *warnings,
                         TcDiagnostic *diag) {
    TcInitHistory hist = {NULL, repl_ctx->last_init_stmt_index};
    size_t stmt_index = repl_ctx->stmt_count;

    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        char msg[128];

        if (tc_symbol_table_find(symbols, var_def->name)) {
            snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (var_def->has_rhs) {
            if (tc_check_rhs(&var_def->rhs, var_def->type, symbols, &hist, stmt_index,
                             var_def->line, diag, warnings, var_def->name) != 0) {
                return -1;
            }
        }
        return tc_symbol_table_add(symbols, var_def->name, var_def->type, (int)symbols->count,
                                   var_def->line, (int)stmt_index, TC_SYM_VARIABLE,
                                   var_def->has_rhs, diag);
    }

    if (stmt->kind == TC_STMT_CONST_DEF) {
        const TcConstDef *const_def = &stmt->u.const_def;
        TcSymbol temp_sym;
        TcSymbol *sym = NULL;
        char msg[128];

        if (tc_symbol_table_find(symbols, const_def->name)) {
            snprintf(msg, sizeof(msg), "duplicate definition of '%s'", const_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, const_def->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (const_def->rhs.kind == TC_RHS_CAST) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, const_def->line,
                              TC_COLUMN_UNKNOWN, "constant initializer must be a constant expression");
            return -1;
        }
        memset(&temp_sym, 0, sizeof(temp_sym));
        temp_sym.type = const_def->type;
        temp_sym.name = const_def->name;
        if (tc_resolve_const_value(&temp_sym, &const_def->rhs, symbols, symbols, const_def->line,
                                   diag) != 0) {
            return -1;
        }
        if (tc_symbol_table_add(symbols, const_def->name, const_def->type, (int)symbols->count,
                                const_def->line, (int)stmt_index, TC_SYM_CONSTANT, 1,
                                diag) != 0) {
            return -1;
        }
        sym = tc_symbol_table_find_mut(symbols, const_def->name);
        sym->const_value = temp_sym.const_value;
        sym->has_const_value = 1;
        return 0;
    }

    if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        const TcIoWrite *io_write = &stmt->u.io_write;
        if (tc_check_io_format(io_write->type, io_write->fmt, io_write->line, diag) != 0) {
            return -1;
        }
        return tc_check_operand(&io_write->operand, io_write->type, symbols, &hist, stmt_index,
                                io_write->line, diag, warnings, NULL);
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
        if (target->sym_kind == TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, assign->line, TC_COLUMN_UNKNOWN,
                              "cannot assign to constant");
            return -1;
        }
        if (tc_check_rhs(&assign->rhs, target->type, symbols, &hist, stmt_index, assign->line, diag,
                         warnings, NULL) != 0) {
            return -1;
        }
    }
    return 0;
}
