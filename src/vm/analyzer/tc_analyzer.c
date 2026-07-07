/*
 * tc_analyzer.c — TC 静态分析器实现
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
                            TcDiagnostic *diag, TcErrorKind literal_type_err) {
    TcErrorKind err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;
    if (!tc_literal_fits_context(lit, expected, &err_kind)) {
        if (err_kind == TC_ERR_LITERAL_TYPE) {
            tc_diagnostic_set(diag, literal_type_err, line, TC_COLUMN_UNKNOWN,
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

/** Pass2 与预扫描共享的全局语句序号上下文 */
typedef struct {
    TcProgram *program;
    int *last_init;
    int next_stmt_index;
} TcAnalyzeCtx;

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

/*
 * @brief 查找 stmt_index 之前最近定义的同名符号
 */
static const TcSymbol *tc_symbol_for_assign_target(const TcSymbolTable *symbols, const char *name,
                                                   int stmt_index) {
    size_t i = 0;
    const TcSymbol *best = NULL;

    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (strcmp(sym->name, name) != 0) {
            continue;
        }
        if (sym->def_stmt_index >= stmt_index) {
            continue;
        }
        if (!best || sym->def_stmt_index > best->def_stmt_index) {
            best = sym;
        }
    }
    return best;
}

/*
 * @brief 在 visible 中解析操作数符号；不可见时区分跨块引用与未定义
 * @param global Pass2 全局符号表；REPL 模式传 NULL（无块作用域）
 */
static const TcSymbol *tc_resolve_visible_symbol(const TcSymbolTable *visible,
                                                 const TcSymbolTable *global, const char *name,
                                                 size_t stmt_index, int line,
                                                 TcDiagnostic *diag) {
    const TcSymbol *symbol = tc_symbol_table_find(visible, name);
    char msg[128];

    if (symbol) {
        return symbol;
    }
    if (global) {
        const TcSymbol *block_sym =
            tc_symbol_for_assign_target(global, name, (int)stmt_index);

        if (block_sym && block_sym->scope_end_stmt_index >= 0) {
            snprintf(msg, sizeof(msg), "cross-block reference to variable '%s'", name);
            tc_diagnostic_set(diag, TC_ERR_CROSS_BLOCK_REFERENCE, line, TC_COLUMN_UNKNOWN, msg);
            return NULL;
        }
    }
    snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
    tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  操作数与 RHS 检查                                                    */
/* ------------------------------------------------------------------ */

/*
 * @brief 检查操作数的类型兼容性与变量定义存在性
 * @param self_name 若非 NULL，表示当前定义中的变量名（用于自引用检测）
 */
static int tc_check_operand(const TcOperand *operand, TcIntType expected,
                            const TcSymbolTable *visible, const TcSymbolTable *global,
                            const TcInitHistory *hist, size_t stmt_index, int line,
                            TcDiagnostic *diag, TcWarningList *warnings, const char *self_name,
                            TcErrorKind type_err) {
    char msg[128];

    if (operand->kind == TC_OPERAND_LIT) {
        return tc_check_literal(&operand->u.lit, expected, line, diag, type_err);
    }

    {
        const TcSymbol *symbol = NULL;

        if (self_name && strcmp(operand->u.name, self_name) == 0) {
            snprintf(msg, sizeof(msg),
                     "variable '%s' cannot reference itself in its initializer", self_name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        symbol = tc_resolve_visible_symbol(visible, global, operand->u.name, stmt_index, line,
                                           diag);
        if (!symbol) {
            return -1;
        }
        if (symbol->type != expected) {
            tc_diagnostic_set(diag, type_err, line, TC_COLUMN_UNKNOWN,
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
static int tc_check_rhs(const TcRhs *rhs, TcIntType lhs_type, const TcSymbolTable *visible,
                        const TcSymbolTable *global, const TcInitHistory *hist, size_t stmt_index,
                        int line, TcDiagnostic *diag, TcWarningList *warnings,
                        const char *self_name) {
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
        if (tc_check_operand(&rhs->u.arith.lhs, rhs->u.arith.type, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.arith.rhs, rhs->u.arith.type, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
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
        if (tc_check_operand(&rhs->u.unary.operand, rhs->u.unary.type, visible, global, hist, stmt_index,
                             line, diag, warnings, self_name, TC_ERR_TYPE_MISMATCH) != 0) {
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
        if (tc_check_operand(&rhs->u.compare.lhs, rhs->u.compare.type, visible, global, hist, stmt_index,
                             line, diag, warnings, self_name,
                             TC_ERR_COMPARISON_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.compare.rhs, rhs->u.compare.type, visible, global, hist, stmt_index,
                             line, diag, warnings, self_name,
                             TC_ERR_COMPARISON_TYPE_MISMATCH) != 0) {
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
        if (tc_check_operand(&rhs->u.logic_bin.lhs, TC_BOOL, visible, global, hist, stmt_index, line, diag,
                             warnings, self_name, TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        /*
         * 逻辑短路 Analyzer 行为（与 Executor 的运行时短路对称）：
         *   lhs 为 false 字面量 → and 短路：不检查 rhs 的未初始化警告
         *     （将 warnings 指针传 NULL 抑制警告，但保留类型/存在性检查）
         *   lhs 为 true 字面量  → or  短路：同上
         * 语言标准 §7.4.4 规定这种静态分析层面的短路抑制。
         */
        if (rhs->u.logic_bin.op == TC_LOGIC_AND) {
            if (rhs->u.logic_bin.lhs.kind == TC_OPERAND_LIT &&
                rhs->u.logic_bin.lhs.u.lit.is_bool &&
                rhs->u.logic_bin.lhs.u.lit.magnitude == 0) {
                /* 短路：false && rhs 不求值 rhs；仍校验 rhs 存在性与类型 */
                if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, hist,
                                     stmt_index, line, diag, NULL, self_name,
                                     TC_ERR_TYPE_MISMATCH) != 0) {
                    return -1;
                }
            } else if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, hist,
                                        stmt_index, line, diag, warnings, self_name,
                                        TC_ERR_TYPE_MISMATCH) != 0) {
                return -1;
            }
        } else if (rhs->u.logic_bin.lhs.kind == TC_OPERAND_LIT &&
                   rhs->u.logic_bin.lhs.u.lit.is_bool &&
                   rhs->u.logic_bin.lhs.u.lit.magnitude != 0) {
            /* 短路：true || rhs 不求值 rhs；仍校验 rhs 存在性与类型 */
            if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, hist,
                                 stmt_index, line, diag, NULL, self_name,
                                 TC_ERR_TYPE_MISMATCH) != 0) {
                return -1;
            }
        } else if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, hist, stmt_index,
                                     line, diag, warnings, self_name, TC_ERR_TYPE_MISMATCH) != 0) {
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
        if (tc_check_operand(&rhs->u.logic_un.operand, TC_BOOL, visible, global, hist, stmt_index, line,
                             diag, warnings, self_name, TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(lhs_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        if (tc_type_is_bool(rhs->u.bitwise_bin.type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitwise operation requires integer type");
            return -1;
        }
        if (tc_check_operand(&rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.bitwise_bin.type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        if (tc_type_is_bool(rhs->u.bitwise_un.type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitwise operation requires integer type");
            return -1;
        }
        if (tc_check_operand(&rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.bitwise_un.type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        if (tc_type_is_bool(rhs->u.shift.type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "shift operation requires integer type");
            return -1;
        }
        if (rhs->u.shift.op == TC_SHIFT_SHR && rhs->u.shift.mode == TC_ARITH_WRAP) {
            tc_diagnostic_set(diag, TC_ERR_OVERFLOW_MODE, line, TC_COLUMN_UNKNOWN,
                              "shift right does not support wrap mode");
            return -1;
        }
        if (tc_check_operand(&rhs->u.shift.value, rhs->u.shift.type, visible, global, hist, stmt_index,
                             line, diag, warnings, self_name, TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.shift.count, rhs->u.shift.type, visible, global, hist, stmt_index,
                             line, diag, warnings, self_name, TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.shift.type != lhs_type) {
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

    if (rhs->kind != TC_RHS_CAST) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, TC_COLUMN_UNKNOWN, "unsupported rhs kind");
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
        source = tc_resolve_visible_symbol(visible, global, rhs->u.cast.source, stmt_index, line,
                                           diag);
        if (!source) {
            return -1;
        }
        tc_maybe_warn_uninitialized(hist, source, stmt_index, line, warnings);
        if (rhs->u.cast.mode == TC_TRUNC_TRUNCATE &&
            (tc_type_is_bool(rhs->u.cast.target) || tc_type_is_bool(source->type))) {
            tc_diagnostic_set(diag, TC_ERR_KEYWORD, line, TC_COLUMN_UNKNOWN,
                              "truncate is only allowed for integer to integer conversion");
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
 * @brief 检查 if 条件表达式，结果须为 bool
 */
static int tc_check_if_condition(const TcRhs *rhs, const TcSymbolTable *visible,
                                 const TcSymbolTable *global, const TcInitHistory *hist,
                                 size_t stmt_index, int line, TcDiagnostic *diag,
                                 TcWarningList *warnings) {
    if (tc_check_rhs(rhs, TC_BOOL, visible, global, hist, stmt_index, line, diag, warnings,
                     NULL) != 0) {
        if (diag->kind == TC_ERR_TYPE_MISMATCH) {
            tc_diagnostic_set(diag, TC_ERR_CONDITION_TYPE, line, TC_COLUMN_UNKNOWN,
                              "if condition must be bool");
        }
        return -1;
    }
    return 0;
}

/*
 * @brief 复制 visible 符号表（含 let 编译期值），用于 if 块独立可见性帧
 */
static int tc_visible_copy_from(const TcSymbolTable *src, TcSymbolTable *dst,
                                TcDiagnostic *diag) {
    size_t i = 0;

    tc_symbol_table_init(dst);
    for (i = 0; i < src->count; i++) {
        const TcSymbol *sym = &src->symbols[i];
        TcSymbol *mut = NULL;

        if (tc_symbol_table_add(dst, sym->name, sym->type, sym->slot, sym->def_line,
                                sym->def_stmt_index, sym->sym_kind, sym->initialized,
                                diag) != 0) {
            return -1;
        }
        if (sym->has_const_value) {
            mut = tc_symbol_table_find_mut(dst, sym->name);
            if (mut) {
                mut->has_const_value = 1;
                mut->const_value = sym->const_value;
            }
        }
    }
    return 0;
}

/*
 * @brief 按定义语句序号查找 Pass1 符号（then/else 同名局部变量各唯一）
 */
static const TcSymbol *tc_find_symbol_by_def_index(const TcSymbolTable *global, const char *name,
                                                  int def_stmt_index) {
    size_t i = 0;

    for (i = 0; i < global->count; i++) {
        const TcSymbol *sym = &global->symbols[i];

        if (sym->def_stmt_index == def_stmt_index && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

/*
 * @brief 将 global 符号表中的定义加入 visible（Pass2 源序可见性）
 */
static int tc_visible_add_from_global(const TcSymbolTable *global, const char *name,
                                      int def_stmt_index, TcSymbolTable *visible,
                                      TcDiagnostic *diag) {
    const TcSymbol *sym = tc_find_symbol_by_def_index(global, name, def_stmt_index);

    if (!sym) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "internal analyzer error");
        return -1;
    }
    return tc_symbol_table_add(visible, sym->name, sym->type, sym->slot, sym->def_line,
                               sym->def_stmt_index, sym->sym_kind, sym->initialized, diag);
}

/* ------------------------------------------------------------------ */
/*  Pass 1 — 符号收集（DFS 递归）                                        */
/* ------------------------------------------------------------------ */

static void tc_mark_if_scope_end(TcSymbolTable *symbols, int if_stmt_index, int if_end_index) {
    size_t i = 0;

    for (i = 0; i < symbols->count; i++) {
        TcSymbol *sym = &symbols->symbols[i];

        if (sym->scope_end_stmt_index >= 0) {
            continue;
        }
        if (sym->def_stmt_index > if_stmt_index && sym->def_stmt_index < if_end_index) {
            sym->scope_end_stmt_index = if_end_index;
        }
    }
}

static int tc_pass1_collect_stmt(const TcStatement *stmt, TcSymbolTable *symbols, int *next_slot,
                                 TcAnalyzeCtx *ctx, TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        size_t i = 0;
        int if_stmt_index = ctx->next_stmt_index;

        ctx->next_stmt_index++;
        if (tc_symbol_table_push_scope(symbols) < 0) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, if_stmt->line, TC_COLUMN_UNKNOWN,
                              "out of memory");
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_pass1_collect_stmt(&if_stmt->then_body[i], symbols, next_slot, ctx, diag) !=
                0) {
                tc_symbol_table_pop_scope(symbols);
                return -1;
            }
        }
        tc_symbol_table_pop_scope(symbols);

        if (if_stmt->else_count > 0) {
            if (tc_symbol_table_push_scope(symbols) < 0) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, if_stmt->line, TC_COLUMN_UNKNOWN,
                                  "out of memory");
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_pass1_collect_stmt(&if_stmt->else_body[i], symbols, next_slot, ctx,
                                           diag) != 0) {
                    tc_symbol_table_pop_scope(symbols);
                    return -1;
                }
            }
            tc_symbol_table_pop_scope(symbols);
        }
        tc_mark_if_scope_end(symbols, if_stmt_index, ctx->next_stmt_index);
        return 0;
    }

    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, var_def->name)) {
            snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, var_def->line,
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (tc_symbol_table_add(symbols, var_def->name, var_def->type, *next_slot, var_def->line,
                                ctx->next_stmt_index, TC_SYM_VARIABLE, var_def->has_rhs,
                                diag) != 0) {
            return -1;
        }
        (*next_slot)++;
        ctx->next_stmt_index++;
        return 0;
    }

    if (stmt->kind == TC_STMT_CONST_DEF) {
        const TcConstDef *const_def = &stmt->u.const_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, const_def->name)) {
            snprintf(msg, sizeof(msg), "duplicate definition of '%s'", const_def->name);
            tc_diagnostic_set(diag, TC_ERR_DUPLICATE_DEFINITION, const_def->line,
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (tc_symbol_table_add(symbols, const_def->name, const_def->type, *next_slot,
                                const_def->line, ctx->next_stmt_index, TC_SYM_CONSTANT, 1,
                                diag) != 0) {
            return -1;
        }
        (*next_slot)++;
        ctx->next_stmt_index++;
        return 0;
    }

    ctx->next_stmt_index++;
    return 0;
}

static int tc_pass1_collect_symbols(TcProgram *program, TcSymbolTable *symbols,
                                    TcDiagnostic *diag) {
    TcAnalyzeCtx ctx;
    size_t i = 0;
    int next_slot = 0;

    ctx.program = program;
    ctx.last_init = NULL;
    ctx.next_stmt_index = 0;

    for (i = 0; i < program->count; i++) {
        if (tc_pass1_collect_stmt(&program->items[i], symbols, &next_slot, &ctx, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Pass 2 — 预扫描初始化历史（DFS）                                    */
/* ------------------------------------------------------------------ */

static void tc_prescan_init_history_stmt(const TcStatement *stmt, TcAnalyzeCtx *ctx,
                                         const TcSymbolTable *symbols) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        size_t i = 0;

        ctx->next_stmt_index++;
        for (i = 0; i < if_stmt->then_count; i++) {
            tc_prescan_init_history_stmt(&if_stmt->then_body[i], ctx, symbols);
        }
        for (i = 0; i < if_stmt->else_count; i++) {
            tc_prescan_init_history_stmt(&if_stmt->else_body[i], ctx, symbols);
        }
        return;
    }

    if (stmt->kind == TC_STMT_ASSIGN) {
        const TcSymbol *sym =
            tc_symbol_for_assign_target(symbols, stmt->u.assign.name, ctx->next_stmt_index);
        if (sym) {
            ctx->last_init[sym->slot] = ctx->next_stmt_index;
        }
    } else if (stmt->kind == TC_STMT_READ) {
        const TcSymbol *sym =
            tc_symbol_for_assign_target(symbols, stmt->u.io_read.name, ctx->next_stmt_index);
        if (sym) {
            ctx->last_init[sym->slot] = ctx->next_stmt_index;
        }
    }
    ctx->next_stmt_index++;
}

static void tc_prescan_init_history(TcProgram *program, TcSymbolTable *symbols,
                                    TcAnalyzeCtx *ctx) {
    size_t i = 0;

    ctx->next_stmt_index = 0;
    for (i = 0; i < program->count; i++) {
        tc_prescan_init_history_stmt(&program->items[i], ctx, symbols);
    }
}

/* ------------------------------------------------------------------ */
/*  Pass 2 — 类型与语义检查（DFS 递归）                                 */
/* ------------------------------------------------------------------ */

static int tc_pass2_check_stmt(const TcStatement *stmt, TcSymbolTable *symbols,
                               TcSymbolTable *visible, TcAnalyzeCtx *ctx, TcInitHistory *hist,
                               TcWarningList *warnings, TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        TcSymbolTable visible_then;
        size_t i = 0;
        size_t stmt_index = (size_t)ctx->next_stmt_index;

        ctx->next_stmt_index++;

        if (tc_check_if_condition(&if_stmt->condition, visible, symbols, hist, stmt_index,
                                  if_stmt->line, diag, warnings) != 0) {
            return -1;
        }

        if (tc_visible_copy_from(visible, &visible_then, diag) != 0) {
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_pass2_check_stmt(&if_stmt->then_body[i], symbols, &visible_then, ctx, hist,
                                    warnings, diag) != 0) {
                tc_symbol_table_free(&visible_then);
                return -1;
            }
        }
        tc_symbol_table_free(&visible_then);

        if (if_stmt->else_count > 0) {
            TcSymbolTable visible_else;

            if (tc_visible_copy_from(visible, &visible_else, diag) != 0) {
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_pass2_check_stmt(&if_stmt->else_body[i], symbols, &visible_else, ctx, hist,
                                        warnings, diag) != 0) {
                    tc_symbol_table_free(&visible_else);
                    return -1;
                }
            }
            tc_symbol_table_free(&visible_else);
        }
        return 0;
    }

    {
        size_t stmt_index = (size_t)ctx->next_stmt_index;
        ctx->next_stmt_index++;

        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;

            if (var_def->has_rhs) {
                if (tc_check_rhs(&var_def->rhs, var_def->type, visible, symbols, hist, stmt_index,
                                 var_def->line, diag, warnings, var_def->name) != 0) {
                    return -1;
                }
            }
            return tc_visible_add_from_global(symbols, var_def->name, (int)stmt_index, visible,
                                              diag);
        }

        if (stmt->kind == TC_STMT_CONST_DEF) {
            const TcConstDef *const_def = &stmt->u.const_def;
            TcSymbol *global_sym =
                (TcSymbol *)tc_find_symbol_by_def_index(symbols, const_def->name, (int)stmt_index);

            if (const_def->rhs.kind == TC_RHS_CAST) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, const_def->line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant initializer must be a constant expression");
                return -1;
            }
            if (global_sym == NULL) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, const_def->line, TC_COLUMN_UNKNOWN,
                                  "internal analyzer error");
                return -1;
            }
            if (tc_resolve_const_value(global_sym, &const_def->rhs, visible, symbols,
                                       const_def->line, diag) != 0) {
                return -1;
            }
            return tc_visible_add_from_global(symbols, const_def->name, (int)stmt_index, visible,
                                             diag);
        }

        if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            const TcIoWrite *io_write = &stmt->u.io_write;

            if (tc_check_io_format(io_write->type, io_write->fmt, io_write->line, diag) != 0) {
                return -1;
            }
            return tc_check_operand(&io_write->operand, io_write->type, visible, symbols, hist,
                                    stmt_index, io_write->line, diag, warnings, NULL,
                                    TC_ERR_TYPE_MISMATCH);
        }

        if (stmt->kind == TC_STMT_READ) {
            const TcRead *io_read = &stmt->u.io_read;
            const TcSymbol *target = tc_resolve_visible_symbol(visible, symbols, io_read->name,
                                                               stmt_index, io_read->line, diag);

            if (!target) {
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
            const TcSymbol *target = tc_resolve_visible_symbol(visible, symbols, assign->name,
                                                               stmt_index, assign->line, diag);

            if (!target) {
                return -1;
            }
            if (target->sym_kind == TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, assign->line,
                                  TC_COLUMN_UNKNOWN, "cannot assign to constant");
                return -1;
            }
            return tc_check_rhs(&assign->rhs, target->type, visible, symbols, hist, stmt_index,
                                assign->line, diag, warnings, NULL);
        }
    }

    return 0;
}

static int tc_pass2_type_check(TcProgram *program, TcSymbolTable *symbols, TcWarningList *warnings,
                               TcDiagnostic *diag) {
    TcSymbolTable visible;
    TcAnalyzeCtx ctx;
    TcInitHistory hist;
    int *last_init = NULL;
    size_t i = 0;

    tc_symbol_table_init(&visible);
    ctx.program = program;
    ctx.next_stmt_index = 0;

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
    ctx.last_init = last_init;
    tc_prescan_init_history(program, symbols, &ctx);

    ctx.next_stmt_index = 0;
    hist.program = program;
    hist.last_init_stmt_index = last_init;

    for (i = 0; i < program->count; i++) {
        if (tc_pass2_check_stmt(&program->items[i], symbols, &visible, &ctx, &hist, warnings,
                                diag) != 0) {
            tc_symbol_table_free(&visible);
            free(last_init);
            return -1;
        }
    }

    tc_symbol_table_free(&visible);
    free(last_init);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  tc_analyze — 两遍分析入口                                            */
/* ------------------------------------------------------------------ */

int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag) {
    tc_typed_program_init(out);
    /*
     * 通过 struct 浅拷贝转移 program 的 items 所有权给 out->program。
     * 然后将 program 清零，避免调用方二次 free（所有权转移模式）。
     * 后续 Pass1/Pass2 失败时 tc_typed_program_free 统一回收所有资源。
     */
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

    if (stmt->kind == TC_STMT_IF) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, stmt->u.if_stmt.line, TC_COLUMN_UNKNOWN,
                          "if statements are not supported in REPL mode");
        return -1;
    }

    if (stmt->kind == TC_STMT_VAR_DEF) {
        const TcVarDef *var_def = &stmt->u.var_def;
        char msg[128];

        if (tc_symbol_table_find_in_current_scope(symbols, var_def->name)) {
            snprintf(msg, sizeof(msg), "duplicate definition of '%s'", var_def->name);
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
        return tc_check_operand(&io_write->operand, io_write->type, symbols, NULL, &hist,
                                stmt_index, io_write->line, diag, warnings, NULL,
                                TC_ERR_TYPE_MISMATCH);
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
        if (tc_check_rhs(&assign->rhs, target->type, symbols, NULL, &hist, stmt_index,
                         assign->line, diag, warnings, NULL) != 0) {
            return -1;
        }
    }
    return 0;
}
