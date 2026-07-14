/*
 * tc_analyzer_repl.c — REPL 增量分析（tc_analyze_statement）
 */
#include "tc_analyzer_repl.h"
#include "tc_analyzer_internal.h"

#include "tc_const_eval.h"
#include "tc_diagnostic.h"
#include "tc_symbol.h"
#include "tc_warning.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  tc_analyze_statement — REPL 增量分析                                 */
/* ------------------------------------------------------------------ */

int tc_analyze_statement(const TcStatement *stmt, TcSymbolTable *symbols,
                         TcReplAnalyzeCtx *repl_ctx, TcWarningList *warnings,
                         TcDiagnostic *diag) {
    TcInitHistory hist;
    size_t stmt_index = repl_ctx->stmt_count;

    memset(&hist, 0, sizeof(hist));
    hist.last_init_stmt_index = repl_ctx->last_init_stmt_index;
    hist.check_init = 1;

    if (stmt->kind == TC_STMT_IF) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, stmt->u.if_stmt.line, TC_COLUMN_UNKNOWN,
                          "if statements are not supported in REPL mode");
        return -1;
    }
    if (stmt->kind == TC_STMT_GOTO) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, stmt->u.goto_stmt.line, TC_COLUMN_UNKNOWN,
                          "goto/label statements are not supported in REPL mode");
        return -1;
    }
    if (stmt->kind == TC_STMT_LABEL_DEF) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, stmt->u.label_def.line, TC_COLUMN_UNKNOWN,
                          "goto/label statements are not supported in REPL mode");
        return -1;
    }

    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, var_def->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (var_def->has_rhs) {
            if (tc_check_rhs(&var_def->rhs, var_def->type, symbols, NULL, &hist, stmt_index,
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

        if (tc_symbol_table_find_in_current_scope(symbols, const_def->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate definition of '%s'", const_def->name);
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
        return tc_check_operand(&io_write->operand, io_write->type, symbols, NULL, &hist,
                                stmt_index, io_write->line, diag, warnings, NULL,
                                TC_ERR_TYPE_MISMATCH);
    }

    if (stmt->kind == TC_STMT_READ) {
        const TcRead *io_read = &stmt->u.io_read;
        const TcSymbol *target = tc_symbol_table_find(symbols, io_read->name);
        char msg[128];

        if (!target) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", io_read->name);
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

    if (stmt->kind == TC_STMT_ASSIGN) {
        const TcAssign *assign = &stmt->u.assign;
        const TcSymbol *target = tc_symbol_table_find(symbols, assign->name);
        char msg[128];

        if (!target) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", assign->name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, assign->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (target->sym_kind == TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, assign->line, TC_COLUMN_UNKNOWN,
                              "cannot assign to constant");
            return -1;
        }
        if (tc_check_rhs(&assign->rhs, target->type, symbols, NULL, &hist, stmt_index,
                         assign->line, diag, warnings, NULL) != 0) {
            return -1;
        }
        return 0;
    }
    return -1;
}

