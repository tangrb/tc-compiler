/*
 * tc_analyzer_pass2.c — Pass2 类型检查、标签收集、goto 跳转合法性
 *
 * Phase 3：复合类型经 tc_type_check_rhs / tc_ptr_check_* / tc_memblock_check_* /
 * tc_struct_check_* 分派；label/goto 仅允许出现在函数体内。
 */
#include "tc_analyzer_pass2.h"
#include "tc_type_check.h"
#include "tc_memblock_check.h"
#include "tc_ptr_check.h"
#include "tc_struct_check.h"
#include "tc_func_check.h"
#include "tc_analyzer_dfa.h"

#include "tc_const_eval.h"
#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_semantics.h"
#include "tc_stmt_index.h"
#include "tc_symbol.h"
#include "tc_warning.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  goto 解析与跳转合法性                                                */
/* ------------------------------------------------------------------ */

static const TcLabelEntry *tc_resolve_goto_label(const TcSymbolTable *table, const char *name,
                                                 const TcBlockPath *goto_path) {
    const TcLabelEntry *best_same = NULL;
    const TcLabelEntry *best_ancestor = NULL;
    const TcLabelEntry *any = NULL;
    size_t i = 0;

    for (i = 0; i < table->label_count; i++) {
        const TcLabelEntry *entry = &table->labels[i];

        if (strcmp(entry->name, name) != 0) {
            continue;
        }
        any = entry;
        if (entry->block_depth == goto_path->depth &&
            tc_paths_equal_prefix(entry->block_path, goto_path->path, entry->block_depth)) {
            best_same = entry;
        } else if (entry->block_depth < goto_path->depth &&
                   tc_paths_equal_prefix(entry->block_path, goto_path->path,
                                         entry->block_depth)) {
            if (!best_ancestor || entry->block_depth > best_ancestor->block_depth) {
                best_ancestor = entry;
            }
        }
    }
    if (best_same) {
        return best_same;
    }
    if (best_ancestor) {
        return best_ancestor;
    }
    return any;
}

static int tc_check_goto_jump(const TcBlockPath *goto_path, const TcLabelEntry *label,
                              int line, TcDiagnostic *diag) {
    TcBlockPath label_path;

    label_path.path = label->block_path;
    label_path.depth = label->block_depth;
    label_path.capacity = label->block_depth;

    if (goto_path->depth == label_path.depth) {
        if (tc_paths_equal_prefix(goto_path->path, label_path.path, goto_path->depth)) {
            return 0; /* 平级跳转 */
        }
        tc_diagnostic_set(diag, TC_ERR_JUMP_TO_SIBLING_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into sibling block");
        return -1;
    }

    if (goto_path->depth > label_path.depth) {
        if (tc_paths_equal_prefix(goto_path->path, label_path.path, label_path.depth)) {
            return 0; /* 向外跳转（祖先） */
        }
        tc_diagnostic_set(diag, TC_ERR_JUMP_TO_SIBLING_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into sibling block");
        return -1;
    }

    /* goto 更浅：若 label 在 goto 的子路径上 → 跳入子块，否则兄弟 */
    if (tc_paths_equal_prefix(label_path.path, goto_path->path, goto_path->depth)) {
        tc_diagnostic_set(diag, TC_ERR_JUMP_INTO_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into inner block");
        return -1;
    }
    tc_diagnostic_set(diag, TC_ERR_JUMP_TO_SIBLING_BLOCK, line, TC_COLUMN_UNKNOWN,
                      "cannot jump into sibling block");
    return -1;
}


/* ------------------------------------------------------------------ */
/*  操作数与 RHS 检查                                                    */
/* ------------------------------------------------------------------ */

void tc_resolved_binding_set(TcResolvedBinding *binding, const TcSymbol *symbol) {
    binding->resolved = 1;
    binding->slot = symbol->sym_kind == TC_SYM_VARIABLE ? symbol->slot : -1;
    binding->is_const = symbol->sym_kind == TC_SYM_CONSTANT;
    binding->type = symbol->type;
    binding->const_bits = symbol->has_const_value ? symbol->const_value.bits : 0;
}

const TcSymbol *tc_resolve_visible_symbol(const TcSymbolTable *visible,
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
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return NULL;
        }
    }
    (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
    tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
    return NULL;
}

/** 带本库成员索引的名称解析（函数体内裸名命中成员 → FUNCTION_SCOPE_ACCESS） */
static const TcSymbol *tc_resolve_visible_symbol_scoped(const TcSymbolTable *visible,
                                                        const TcSymbolTable *global,
                                                        const char *name, size_t stmt_index,
                                                        int line, TcDiagnostic *diag,
                                                        const TcMemberIndex *members,
                                                        int in_function) {
    const TcSymbol *symbol = tc_symbol_table_find(visible, name);
    char msg[128];

    if (symbol) {
        return symbol;
    }
    if (global) {
        const TcSymbol *block_sym =
            tc_symbol_for_assign_target(global, name, (int)stmt_index);

        if (block_sym && block_sym->scope_end_stmt_index >= 0) {
            if (in_function && members &&
                tc_func_try_function_scope_access(members, name, line, diag)) {
                return NULL;
            }
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return NULL;
        }
    }
    if (in_function && members &&
        tc_func_try_function_scope_access(members, name, line, diag)) {
        return NULL;
    }
    (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
    tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
    return NULL;
}

static int tc_precheck_name_binding(const char *name, TcResolvedBinding *binding,
                                    const TcSymbolTable *visible,
                                    const TcSymbolTable *global, size_t stmt_index, int line,
                                    TcDiagnostic *diag, const char *self_name) {
    const TcSymbol *symbol = NULL;
    char msg[128];

    if (self_name && strcmp(name, self_name) == 0) {
        (void)snprintf(msg, sizeof(msg),
                       "variable '%s' cannot reference itself in its initializer", self_name);
        tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
        return -1;
    }
    symbol = tc_resolve_visible_symbol(visible, global, name, stmt_index, line, diag);
    if (!symbol) {
        return -1;
    }
    tc_resolved_binding_set(binding, symbol);
    return 0;
}

static int tc_precheck_operand_name(TcOperand *operand, const TcSymbolTable *visible,
                                    const TcSymbolTable *global, size_t stmt_index, int line,
                                    TcDiagnostic *diag, const char *self_name) {
    if (operand->kind == TC_OPERAND_LIT) {
        return 0;
    }
    return tc_precheck_name_binding(operand->u.name, &operand->binding, visible, global,
                                    stmt_index, line, diag, self_name);
}

/* §11.0：同一 RHS 先完成名称解析，再进入类型、模式与常量阶段。 */
static int tc_precheck_rhs_names(TcRhs *rhs, const TcSymbolTable *visible,
                                 const TcSymbolTable *global, size_t stmt_index, int line,
                                 TcDiagnostic *diag, const char *self_name) {
    switch (rhs->kind) {
    case TC_RHS_ARITH:
        if (tc_precheck_operand_name(&rhs->u.arith.lhs, visible, global, stmt_index, line, diag,
                                     self_name) != 0) {
            return -1;
        }
        return tc_precheck_operand_name(&rhs->u.arith.rhs, visible, global, stmt_index, line,
                                        diag, self_name);
    case TC_RHS_UNARY:
        return tc_precheck_operand_name(&rhs->u.unary.operand, visible, global, stmt_index, line,
                                        diag, self_name);
    case TC_RHS_COMPARE:
        if (tc_precheck_operand_name(&rhs->u.compare.lhs, visible, global, stmt_index, line,
                                     diag, self_name) != 0) {
            return -1;
        }
        return tc_precheck_operand_name(&rhs->u.compare.rhs, visible, global, stmt_index, line,
                                        diag, self_name);
    case TC_RHS_LOGIC_BIN:
        if (tc_precheck_operand_name(&rhs->u.logic_bin.lhs, visible, global, stmt_index, line,
                                     diag, self_name) != 0) {
            return -1;
        }
        return tc_precheck_operand_name(&rhs->u.logic_bin.rhs, visible, global, stmt_index, line,
                                        diag, self_name);
    case TC_RHS_LOGIC_UN:
        return tc_precheck_operand_name(&rhs->u.logic_un.operand, visible, global, stmt_index,
                                        line, diag, self_name);
    case TC_RHS_BITWISE_BIN:
        if (tc_precheck_operand_name(&rhs->u.bitwise_bin.lhs, visible, global, stmt_index, line,
                                     diag, self_name) != 0) {
            return -1;
        }
        return tc_precheck_operand_name(&rhs->u.bitwise_bin.rhs, visible, global, stmt_index,
                                        line, diag, self_name);
    case TC_RHS_BITWISE_UN:
        return tc_precheck_operand_name(&rhs->u.bitwise_un.operand, visible, global, stmt_index,
                                        line, diag, self_name);
    case TC_RHS_SHIFT:
        if (tc_precheck_operand_name(&rhs->u.shift.value, visible, global, stmt_index, line, diag,
                                     self_name) != 0) {
            return -1;
        }
        return tc_precheck_operand_name(&rhs->u.shift.count, visible, global, stmt_index, line,
                                        diag, self_name);
    case TC_RHS_CAST:
        return tc_precheck_operand_name(&rhs->u.cast.source, visible, global, stmt_index, line,
                                        diag, self_name);
    case TC_RHS_CONST_CAST:
        return tc_precheck_operand_name(&rhs->u.const_cast.source, visible, global, stmt_index,
                                        line, diag, self_name);
    case TC_RHS_FLOAT_ARITH:
        if (tc_precheck_operand_name(&rhs->u.float_arith.lhs, visible, global, stmt_index, line,
                                     diag, self_name) != 0) {
            return -1;
        }
        return tc_precheck_operand_name(&rhs->u.float_arith.rhs, visible, global, stmt_index,
                                        line, diag, self_name);
    case TC_RHS_FLOAT_UNARY:
        return tc_precheck_operand_name(&rhs->u.float_unary.operand, visible, global, stmt_index,
                                        line, diag, self_name);
    case TC_RHS_FLOAT_COMPARE:
        if (tc_precheck_operand_name(&rhs->u.float_compare.lhs, visible, global, stmt_index, line,
                                     diag, self_name) != 0) {
            return -1;
        }
        return tc_precheck_operand_name(&rhs->u.float_compare.rhs, visible, global, stmt_index,
                                        line, diag, self_name);
    case TC_RHS_BITCAST:
        return tc_precheck_operand_name(&rhs->u.bitcast.source, visible, global, stmt_index, line,
                                        diag, self_name);
    case TC_RHS_CONST_REF:
        return tc_precheck_name_binding(rhs->u.const_ref.name, &rhs->u.const_ref.binding,
                                        visible, global, stmt_index, line, diag, self_name);
    case TC_RHS_LIT:
        return 0;
    case TC_RHS_MEMBLOCK_LOAD:
    case TC_RHS_MEMBLOCK_CONSTRUCTOR:
    case TC_RHS_MEMBLOCK_COUNT:
    case TC_RHS_STRUCT_CONSTRUCTOR:
    case TC_RHS_FIELD_READ:
    case TC_RHS_PTR_LOAD:
    case TC_RHS_PTR_ADDRESS:
    case TC_RHS_PTR_ADD:
    case TC_RHS_PTR_SUB:
    case TC_RHS_PTR_EQ:
    case TC_RHS_PTR_NE:
    case TC_RHS_PTR_LT:
    case TC_RHS_PTR_LE:
    case TC_RHS_PTR_GT:
    case TC_RHS_PTR_GE:
    case TC_RHS_PTR_SIZE:
    case TC_RHS_FUNCALL_EXPR:
    case TC_RHS_SELF_MEMBER:
        /* 0.0.35 Phase 2：可解析；深度类型/执行在后续阶段 */
        return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  操作数与 RHS 检查                                                    */
/* ------------------------------------------------------------------ */

/*
 * @brief 检查操作数的类型兼容性与变量定义存在性
 * @param self_name 若非 NULL，表示当前定义中的变量名（用于自引用检测）
 */
int tc_check_operand(TcOperand *operand, TcTypeKind expected,
                            const TcSymbolTable *visible, const TcSymbolTable *global,
                            TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                            TcWarningList *warnings, const char *self_name, TcErrorKind type_err) {
    char msg[128];

    (void)warnings;
    if (operand->kind == TC_OPERAND_LIT) {
        return tc_check_literal(&operand->u.lit, expected, line, diag, type_err);
    }

    {
        const TcSymbol *symbol = NULL;

        if (self_name && strcmp(operand->u.name, self_name) == 0) {
            (void)snprintf(msg, sizeof(msg),
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
        if (tc_check_operand_init(hist, symbol, stmt_index, line, diag) != 0) {
            return -1;
        }
        tc_resolved_binding_set(&operand->binding, symbol);
    }
    return 0;
}

/*
 * @brief 校验格式说明符与操作数类型的匹配关系
 *
 * %d/%i 要求有符号类型；%u 要求无符号类型；%x/%X/%o/%b 无限制
 */
int tc_check_io_format(TcTypeKind type, TcFormatSpec fmt, int line, TcDiagnostic *diag) {
    if (fmt == TC_FMT_NONE) {
        return 0;
    }

    /* 浮点格式符：仅用于浮点类型 */
    if (fmt == TC_FMT_F || fmt == TC_FMT_E || fmt == TC_FMT_EU ||
        fmt == TC_FMT_G || fmt == TC_FMT_GU) {
        if (!tc_type_is_float(type)) {
            tc_diagnostic_set(diag, TC_ERR_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "float format specifier requires float type");
            return -1;
        }
        return 0;
    }

    /* 浮点类型不允许整数格式符 */
    if (tc_type_is_float(type)) {
        tc_diagnostic_set(diag, TC_ERR_FORMAT_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "float type requires float format specifier");
        return -1;
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
int tc_check_rhs(TcRhs *rhs, TcTypeKind lhs_type, const TcSymbolTable *visible,
                        const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                        int line, TcDiagnostic *diag, TcWarningList *warnings,
                        const char *self_name) {
    char msg[128];

    if (tc_precheck_rhs_names(rhs, visible, global, stmt_index, line, diag, self_name) != 0) {
        return -1;
    }

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
        if (tc_validate_arith_mode(rhs->u.arith.op, rhs->u.arith.type,
                                   rhs->u.arith.mode, diag, line) != 0) {
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
        if (tc_validate_unary_mode(rhs->u.unary.op, rhs->u.unary.type,
                                   rhs->u.unary.mode, diag, line) != 0) {
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
        int saved_check_init = hist ? hist->check_init : 1;
        TcStaticBoolResult lhs_value = TC_STATIC_BOOL_UNKNOWN;

        if (tc_check_operand(&rhs->u.logic_bin.lhs, TC_BOOL, visible, global, hist, stmt_index, line, diag,
                             warnings, self_name, TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        /*
         * 逻辑短路（与 Executor / §7.2.2 对称）：
         *   and + 静态 false / or + 静态 true → 不检查 rhs 未初始化
         *   仍校验 rhs 存在性与类型（临时关闭 check_init）。
         */
        tc_try_eval_static_bool_operand(&rhs->u.logic_bin.lhs, &lhs_value);
        if ((rhs->u.logic_bin.op == TC_LOGIC_AND && lhs_value == TC_STATIC_BOOL_FALSE) ||
            (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs_value == TC_STATIC_BOOL_TRUE)) {
            if (hist) {
                hist->check_init = 0;
            }
            if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, hist,
                                 stmt_index, line, diag, warnings, self_name,
                                 TC_ERR_TYPE_MISMATCH) != 0) {
                if (hist) {
                    hist->check_init = saved_check_init;
                }
                return -1;
            }
            if (hist) {
                hist->check_init = saved_check_init;
            }
        } else if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, hist,
                                    stmt_index, line, diag, warnings, self_name,
                                    TC_ERR_TYPE_MISMATCH) != 0) {
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
        if (tc_type_is_bool(rhs->u.bitwise_bin.type) || tc_type_is_float(rhs->u.bitwise_bin.type)) {
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
        if (tc_type_is_bool(rhs->u.bitwise_un.type) || tc_type_is_float(rhs->u.bitwise_un.type)) {
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
        if (tc_validate_shift_mode(rhs->u.shift.op, rhs->u.shift.type,
                                   rhs->u.shift.mode, diag, line) != 0) {
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

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        if (tc_validate_fp_arith_mode(rhs->u.float_arith.op, rhs->u.float_arith.type,
                                      rhs->u.float_arith.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_arith.lhs, rhs->u.float_arith.type, visible, global,
                             hist, stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_arith.rhs, rhs->u.float_arith.type, visible, global,
                             hist, stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.float_arith.type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        if (tc_validate_fp_unary_mode(rhs->u.float_unary.op, rhs->u.float_unary.type,
                                      rhs->u.float_unary.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_unary.operand, rhs->u.float_unary.type, visible,
                             global, hist, stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.float_unary.type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        if (tc_validate_fp_compare_mode(rhs->u.float_compare.type,
                                        rhs->u.float_compare.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_compare.lhs, rhs->u.float_compare.type, visible,
                             global, hist, stmt_index, line, diag, warnings, self_name,
                             TC_ERR_COMPARISON_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_compare.rhs, rhs->u.float_compare.type, visible,
                             global, hist, stmt_index, line, diag, warnings, self_name,
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

    if (rhs->kind == TC_RHS_BITCAST) {
        TcBitcastRhs *bitcast = &rhs->u.bitcast;
        TcTypeKind source_type = TC_INT32;
        const TcSymbol *source = NULL;
        int width = tc_type_bit_width(bitcast->target);

        if (tc_type_is_bool(bitcast->target) ||
            (!tc_type_is_integer(bitcast->target) && !tc_type_is_float(bitcast->target))) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitcast target must be a non-bool integer or float type");
            return -1;
        }
        if (bitcast->source.kind == TC_OPERAND_VAR) {
            if (self_name && strcmp(bitcast->source.u.name, self_name) == 0) {
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "variable cannot reference itself in its initializer");
                return -1;
            }
            source = tc_resolve_visible_symbol(visible, global, bitcast->source.u.name,
                                               stmt_index, line, diag);
            if (!source) {
                return -1;
            }
            source_type = source->type;
            if (tc_check_operand_init(hist, source, stmt_index, line, diag) != 0) {
                return -1;
            }
            tc_resolved_binding_set(&bitcast->source.binding, source);
        } else if (bitcast->source.u.lit.is_bool) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        } else if (bitcast->source.u.lit.is_float) {
            if (!isfinite(bitcast->source.u.lit.float_value) &&
                !bitcast->source.u.lit.float32_suffix) {
                source_type = width == 32 ? TC_FLOAT32 : TC_FLOAT64;
            } else {
                source_type = bitcast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
            }
        } else if (width == 32) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT32 : TC_INT32;
        } else if (width == 64) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
        } else if (width == 16) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT16 : TC_INT16;
        } else {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT8 : TC_INT8;
        }
        if (tc_type_is_bool(source_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        }
        if (tc_type_bit_width(source_type) != width) {
            tc_diagnostic_set(diag, TC_ERR_BITCAST_WIDTH, line, TC_COLUMN_UNKNOWN,
                              "bitcast source and target widths must match");
            return -1;
        }
        if (bitcast->source.kind == TC_OPERAND_LIT &&
            tc_check_literal(&bitcast->source.u.lit, source_type, line, diag,
                             TC_ERR_LITERAL_TYPE) != 0) {
            return -1;
        }
        if (bitcast->target != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitcast target type does not match variable type");
            return -1;
        }
        bitcast->source_type = source_type;
        bitcast->source_type_resolved = 1;
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        const TcSymbol *source = NULL;

        if (self_name && strcmp(rhs->u.const_ref.name, self_name) == 0) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", self_name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        source = tc_resolve_visible_symbol(visible, global, rhs->u.const_ref.name, stmt_index,
                                           line, diag);
        if (!source) {
            return -1;
        }
        if (source->type != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "identifier type does not match destination type");
            return -1;
        }
        if (source->sym_kind == TC_SYM_VARIABLE &&
            tc_check_operand_init(hist, source, stmt_index, line, diag) != 0) {
            return -1;
        }
        tc_resolved_binding_set(&rhs->u.const_ref.binding, source);
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant cast is only allowed in let initializer");
        return -1;
    }

    if (rhs->kind != TC_RHS_CAST) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, TC_COLUMN_UNKNOWN, "unsupported rhs kind");
        return -1;
    }

    {
        TcCastRhs *cast = &rhs->u.cast;
        const TcSymbol *source = NULL;
        TcTypeKind source_type = TC_INT64;

        if (cast->source.kind == TC_OPERAND_VAR) {
            if (self_name && strcmp(cast->source.u.name, self_name) == 0) {
                (void)snprintf(msg, sizeof(msg),
                         "variable '%s' cannot reference itself in its initializer", self_name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            source = tc_resolve_visible_symbol(visible, global, cast->source.u.name, stmt_index,
                                               line, diag);
            if (!source) {
                return -1;
            }
            if (tc_check_operand_init(hist, source, stmt_index, line, diag) != 0) {
                return -1;
            }
            source_type = source->type;
            tc_resolved_binding_set(&cast->source.binding, source);
        } else if (cast->source.u.lit.is_bool) {
            source_type = TC_BOOL;
        } else if (cast->source.u.lit.is_float) {
            source_type = cast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
        } else {
            source_type = cast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
        }
        if (cast->source.kind == TC_OPERAND_LIT &&
            tc_check_literal(&cast->source.u.lit, source_type, line, diag,
                             TC_ERR_LITERAL_TYPE) != 0) {
            return -1;
        }
        if (cast->mode == TC_TRUNC_TRUNCATE &&
            (!tc_type_is_integer(cast->target) || !tc_type_is_integer(source_type) ||
             tc_type_bit_width(cast->target) >= tc_type_bit_width(source_type))) {
            tc_diagnostic_set(diag, TC_ERR_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "truncate requires an integer target narrower than the source");
            return -1;
        }
        if (cast->target != lhs_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast target type does not match variable type");
            return -1;
        }
        cast->source_type = source_type;
        cast->source_type_resolved = 1;
    }
    return 0;
}

/*
 * @brief 检查 if 条件表达式，结果须为 bool
 */
static int tc_check_condition(TcRhs *rhs, const TcSymbolTable *visible,
                              const TcSymbolTable *global, TcInitHistory *hist,
                              size_t stmt_index, int line, const char *owner,
                              TcDiagnostic *diag, TcWarningList *warnings) {
    if (tc_check_rhs(rhs, TC_BOOL, visible, global, hist, stmt_index, line, diag, warnings,
                     NULL) != 0) {
        if (diag->kind == TC_ERR_TYPE_MISMATCH) {
            char msg[64];

            (void)snprintf(msg, sizeof(msg), "%s condition must be bool", owner);
            tc_diagnostic_set(diag, TC_ERR_CONDITION_TYPE, line, TC_COLUMN_UNKNOWN,
                              msg);
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
    TcSymbol *added = NULL;

    if (!sym) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "internal analyzer error");
        return -1;
    }
    if (tc_symbol_table_add_ex(visible, sym->name, sym->type, &sym->full_type,
                               sym->memblock_count, sym->struct_id, sym->slot, sym->slot_domain,
                               sym->def_line, sym->def_stmt_index, sym->sym_kind,
                               sym->initialized, diag) != 0) {
        return -1;
    }
    added = &visible->symbols[visible->count - 1];
    added->has_const_value = sym->has_const_value;
    added->const_value = sym->const_value;
    added->scope_end_stmt_index = sym->scope_end_stmt_index;
    return 0;
}


/* ------------------------------------------------------------------ */
/*  Pass 2a — 收集全部标签（带块路径，支持前向 goto）                    */
/* ------------------------------------------------------------------ */

static int tc_pass2_collect_labels_stmt(TcStatement *stmt, TcSymbolTable *symbols,
                                        TcAnalyzeCtx *ctx, TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_IF) {
        TcIfStmt *if_stmt = &stmt->u.if_stmt;
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);

        if (tc_block_path_push(&ctx->block_path, tc_block_id_then(if_stmt_index), diag) != 0) {
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_pass2_collect_labels_stmt(&if_stmt->then_body[i], symbols, ctx, diag) != 0) {
                return -1;
            }
        }
        tc_block_path_pop(&ctx->block_path);

        if (if_stmt->else_count > 0) {
            if (tc_block_path_push(&ctx->block_path, tc_block_id_else(if_stmt_index), diag) !=
                0) {
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_pass2_collect_labels_stmt(&if_stmt->else_body[i], symbols, ctx, diag) !=
                    0) {
                    return -1;
                }
            }
            tc_block_path_pop(&ctx->block_path);
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_WHILE) {
        TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);

        if (tc_block_path_push(&ctx->block_path, tc_block_id_while(while_stmt_index), diag) != 0) {
            return -1;
        }
        ctx->loop_depth++;
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_pass2_collect_labels_stmt(&while_stmt->body[i], symbols, ctx, diag) != 0) {
                ctx->loop_depth--;
                tc_block_path_pop(&ctx->block_path);
                return -1;
            }
        }
        ctx->loop_depth--;
        tc_block_path_pop(&ctx->block_path);
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        const TcLabelDef *label_def = &stmt->u.label_def;
        int stmt_index = tc_stmt_index_take(&ctx->index);

        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_ERR_LABEL_OUTSIDE_FUNCTION, label_def->line,
                              TC_COLUMN_UNKNOWN, "label is only allowed inside a function");
            return -1;
        }
        if (ctx->loop_depth > 0) {
            tc_diagnostic_set(diag, TC_ERR_LABEL_INSIDE_LOOP, label_def->line,
                              TC_COLUMN_UNKNOWN, "label is not allowed inside while");
            return -1;
        }

        return tc_symbol_table_add_label(symbols, label_def->name, stmt_index, label_def->line,
                                         ctx->block_path.path, ctx->block_path.depth, diag);
    }

    if (stmt->kind == TC_STMT_GOTO) {
        tc_stmt_index_take(&ctx->index);
        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_ERR_GOTO_OUTSIDE_FUNCTION, stmt->u.goto_stmt.line,
                              TC_COLUMN_UNKNOWN, "goto is only allowed inside a function");
            return -1;
        }
        if (ctx->loop_depth > 0) {
            tc_diagnostic_set(diag, TC_ERR_GOTO_INSIDE_LOOP, stmt->u.goto_stmt.line,
                              TC_COLUMN_UNKNOWN, "goto is not allowed inside while");
            return -1;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNC_DEF) {
        TcFuncDef *func = &stmt->u.func_def;
        size_t i = 0;

        (void)tc_stmt_index_take(&ctx->index);
        ctx->func_depth++;
        for (i = 0; i < func->body_count; i++) {
            if (tc_pass2_collect_labels_stmt(&func->body[i], symbols, ctx, diag) != 0) {
                ctx->func_depth--;
                return -1;
            }
        }
        ctx->func_depth--;
        return 0;
    }

    tc_stmt_index_take(&ctx->index);
    return 0;
}

static int tc_pass2_collect_labels(TcProgram *program, TcSymbolTable *symbols,
                                   TcAnalyzeCtx *ctx, TcDiagnostic *diag) {
    size_t i = 0;

    tc_symbol_table_clear_labels(symbols);
    tc_stmt_index_reset(&ctx->index);
    ctx->block_path.depth = 0;
    ctx->loop_depth = 0;
    ctx->func_depth = 0;
    for (i = 0; i < program->count; i++) {
        if (tc_pass2_collect_labels_stmt(&program->items[i], symbols, ctx, diag) != 0) {
            return -1;
        }
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/*  Pass 2b — 类型与语义检查（DFS 递归）+ goto 跳转合法性                */
/* ------------------------------------------------------------------ */

static int tc_pass2_check_funcall_rhs(TcRhs *rhs, const TcType *expected, int position,
                                      TcAnalyzeCtx *ctx, const TcSymbolTable *visible,
                                      const TcSymbolTable *symbols, TcInitHistory *hist,
                                      size_t stmt_index, int line, TcWarningList *warnings,
                                      TcDiagnostic *diag) {
    TcNamedArg *args = NULL;
    size_t i = 0;
    int rc = 0;

    if (!ctx->func_env || rhs->kind != TC_RHS_FUNCALL_EXPR) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, TC_COLUMN_UNKNOWN, "expected funcall");
        return -1;
    }
    if (rhs->u.funcall_expr.arg_count > 0) {
        args = (TcNamedArg *)calloc(rhs->u.funcall_expr.arg_count, sizeof(TcNamedArg));
        if (!args) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < rhs->u.funcall_expr.arg_count; i++) {
            args[i].param_name = rhs->u.funcall_expr.args[i].param_name;
            if (rhs->u.funcall_expr.args[i].value) {
                memcpy(&args[i].value, rhs->u.funcall_expr.args[i].value, sizeof(TcRhs));
            }
        }
    }
    rc = tc_func_check_funcall(ctx->func_env, rhs->u.funcall_expr.is_self,
                               rhs->u.funcall_expr.qualifier, rhs->u.funcall_expr.member_name,
                               rhs->u.funcall_expr.target, args, rhs->u.funcall_expr.arg_count,
                               position, expected, line, visible, symbols, hist, stmt_index,
                               warnings, diag);
    free(args);
    return rc;
}

static int tc_pass2_check_stmt(TcStatement *stmt, TcSymbolTable *symbols,
                               TcSymbolTable *visible, TcStructTable *struct_table,
                               TcAnalyzeCtx *ctx, TcInitHistory *hist, TcWarningList *warnings,
                               TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_WHILE) {
        TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        TcSymbolTable visible_body;
        TcInitState *before_loop = NULL;
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);
        int saved_loop_id = ctx->current_loop_id;
        int saved_reachable = ctx->path_reachable;

        if (ctx->num_slots > 0) {
            before_loop = (TcInitState *)malloc((size_t)ctx->num_slots * sizeof(TcInitState));
            if (!before_loop) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, while_stmt->line,
                                  TC_COLUMN_UNKNOWN, "memory allocation failed");
                return -1;
            }
            tc_init_states_copy(before_loop, ctx->init_states, ctx->num_slots);
        }
        if (tc_check_condition(&while_stmt->condition, visible, symbols, hist,
                               (size_t)while_stmt_index, while_stmt->line, "while", diag,
                               warnings) != 0) {
            free(before_loop);
            return -1;
        }
        if (tc_block_path_push(&ctx->block_path, tc_block_id_while(while_stmt_index), diag) != 0 ||
            tc_visible_copy_from(visible, &visible_body, diag) != 0) {
            free(before_loop);
            return -1;
        }
        ctx->loop_depth++;
        ctx->current_loop_id = while_stmt->loop_id;
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_pass2_check_stmt(&while_stmt->body[i], symbols, &visible_body, struct_table,
                                    ctx, hist, warnings, diag) != 0) {
                ctx->current_loop_id = saved_loop_id;
                ctx->loop_depth--;
                tc_symbol_table_free(&visible_body);
                tc_block_path_pop(&ctx->block_path);
                free(before_loop);
                return -1;
            }
        }
        ctx->current_loop_id = saved_loop_id;
        ctx->loop_depth--;
        tc_symbol_table_free(&visible_body);
        tc_block_path_pop(&ctx->block_path);
        if (ctx->num_slots > 0) {
            tc_init_states_copy(ctx->init_states, before_loop, ctx->num_slots);
        }
        ctx->path_reachable = saved_reachable;
        if (hist) {
            hist->check_init = saved_reachable;
        }
        free(before_loop);
        return 0;
    }

    if (stmt->kind == TC_STMT_IF) {
        TcIfStmt *if_stmt = &stmt->u.if_stmt;
        TcSymbolTable visible_then;
        TcInitState *before_then = NULL;
        TcInitState *after_then = NULL;
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);
        size_t stmt_index = (size_t)if_stmt_index;
        int then_reachable = 1;
        int else_reachable = 1;
        int saved_reachable = ctx->path_reachable;

        if (ctx->num_slots > 0) {
            before_then = (TcInitState *)malloc((size_t)ctx->num_slots * sizeof(TcInitState));
            after_then = (TcInitState *)malloc((size_t)ctx->num_slots * sizeof(TcInitState));
            if (!before_then || !after_then) {
                free(before_then);
                free(after_then);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, if_stmt->line, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            tc_init_states_copy(before_then, ctx->init_states, ctx->num_slots);
        }

        if (tc_check_condition(&if_stmt->condition, visible, symbols, hist, stmt_index,
                               if_stmt->line, "if", diag, warnings) != 0) {
            free(before_then);
            free(after_then);
            return -1;
        }

        if (tc_block_path_push(&ctx->block_path, tc_block_id_then(if_stmt_index), diag) != 0) {
            free(before_then);
            free(after_then);
            return -1;
        }
        if (tc_visible_copy_from(visible, &visible_then, diag) != 0) {
            free(before_then);
            free(after_then);
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_pass2_check_stmt(&if_stmt->then_body[i], symbols, &visible_then, struct_table,
                                    ctx, hist, warnings, diag) != 0) {
                tc_symbol_table_free(&visible_then);
                free(before_then);
                free(after_then);
                return -1;
            }
        }
        tc_symbol_table_free(&visible_then);
        tc_block_path_pop(&ctx->block_path);
        then_reachable = ctx->path_reachable;
        if (ctx->num_slots > 0) {
            tc_init_states_copy(after_then, ctx->init_states, ctx->num_slots);
            tc_init_states_copy(ctx->init_states, before_then, ctx->num_slots);
        }
        ctx->path_reachable = saved_reachable;
        if (hist) {
            hist->check_init = saved_reachable;
        }

        if (if_stmt->else_count > 0) {
            TcSymbolTable visible_else;

            if (tc_block_path_push(&ctx->block_path, tc_block_id_else(if_stmt_index), diag) !=
                0) {
                free(before_then);
                free(after_then);
                return -1;
            }
            if (tc_visible_copy_from(visible, &visible_else, diag) != 0) {
                free(before_then);
                free(after_then);
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_pass2_check_stmt(&if_stmt->else_body[i], symbols, &visible_else,
                                        struct_table, ctx, hist, warnings, diag) != 0) {
                    tc_symbol_table_free(&visible_else);
                    free(before_then);
                    free(after_then);
                    return -1;
                }
            }
            tc_symbol_table_free(&visible_else);
            tc_block_path_pop(&ctx->block_path);
            else_reachable = ctx->path_reachable;
            if (ctx->num_slots > 0) {
                if (then_reachable && else_reachable) {
                    tc_init_states_merge(ctx->init_states, after_then, ctx->init_states,
                                         ctx->num_slots);
                } else if (then_reachable) {
                    tc_init_states_copy(ctx->init_states, after_then, ctx->num_slots);
                }
                /* 仅 else 可达：ctx 已是 else 结束状态；均不可达：保留 else 快照 */
            }
            ctx->path_reachable =
                saved_reachable ? (then_reachable || else_reachable) : 0;
        } else {
            if (ctx->num_slots > 0) {
                if (then_reachable) {
                    tc_init_states_merge(ctx->init_states, after_then, before_then,
                                         ctx->num_slots);
                } else {
                    tc_init_states_copy(ctx->init_states, before_then, ctx->num_slots);
                }
            }
            /* 无 else 时可跳过 then，合流点仍可达 */
            ctx->path_reachable = saved_reachable;
        }
        if (hist) {
            hist->check_init = ctx->path_reachable;
        }
        free(before_then);
        free(after_then);
        return 0;
    }

    if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        TcLoopControlStmt *control = stmt->kind == TC_STMT_BREAK ? &stmt->u.break_stmt
                                                                 : &stmt->u.continue_stmt;

        tc_stmt_index_take(&ctx->index);
        if (ctx->current_loop_id < 0) {
            TcErrorKind kind = stmt->kind == TC_STMT_BREAK ? TC_ERR_BREAK_OUTSIDE_LOOP
                                                            : TC_ERR_CONTINUE_OUTSIDE_LOOP;
            const char *message = stmt->kind == TC_STMT_BREAK ? "break used outside while"
                                                               : "continue used outside while";

            tc_diagnostic_set(diag, kind, control->line, TC_COLUMN_UNKNOWN, message);
            return -1;
        }
        control->loop_id = ctx->current_loop_id;
        ctx->path_reachable = 0;
        if (hist) {
            hist->check_init = 0;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_GOTO) {
        TcGoto *goto_stmt = &stmt->u.goto_stmt;
        const TcLabelEntry *entry = NULL;
        char msg[128];

        tc_stmt_index_take(&ctx->index);
        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_ERR_GOTO_OUTSIDE_FUNCTION, goto_stmt->line,
                              TC_COLUMN_UNKNOWN, "goto is only allowed inside a function");
            return -1;
        }
        entry = tc_resolve_goto_label(symbols, goto_stmt->target, &ctx->block_path);
        if (!entry) {
            (void)snprintf(msg, sizeof(msg), "label '%s' not found", goto_stmt->target);
            tc_diagnostic_set(diag, TC_ERR_LABEL_NOT_FOUND, goto_stmt->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (tc_check_goto_jump(&ctx->block_path, entry, goto_stmt->line, diag) != 0) {
            return -1;
        }
        goto_stmt->resolved_target_stmt_index = entry->stmt_index;
        goto_stmt->resolved = 1;
        /* 终止当前顺序路径：其后赋值不更新 init（前向跳过初始化） */
        ctx->path_reachable = 0;
        if (hist) {
            hist->check_init = 0;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNC_DEF) {
        TcFuncDef *func = &stmt->u.func_def;
        TcSymbolTable visible_body;
        size_t i = 0;
        const TcFuncSignature *saved_func = NULL;

        (void)tc_stmt_index_take(&ctx->index);
        tc_symbol_table_init(&visible_body);
        ctx->func_depth++;
        if (ctx->func_env) {
            saved_func = ctx->func_env->current_func;
            ctx->func_env->current_func = NULL;
            if (ctx->func_env->sigs) {
                size_t si = 0;
                for (si = 0; si < ctx->func_env->sigs->count; si++) {
                    if (ctx->func_env->sigs->items[si].func_id == func->func_id) {
                        ctx->func_env->current_func = &ctx->func_env->sigs->items[si];
                        break;
                    }
                }
            }
        }
        for (i = 0; i < func->param_count; i++) {
            const TcFuncParam *param = &func->params[i];
            const TcSymbol *sym = NULL;
            size_t si = 0;

            /* Pass1 已 pop 函数作用域；按 def_line + PARAM 域找回形参符号 */
            for (si = 0; si < symbols->count; si++) {
                const TcSymbol *cand = &symbols->symbols[si];

                if (cand->slot_domain == TC_SLOT_PARAM && cand->def_line == func->line &&
                    cand->name && param->name && strcmp(cand->name, param->name) == 0) {
                    sym = cand;
                    break;
                }
            }

            if (sym && tc_visible_add_from_global(symbols, param->name, sym->def_stmt_index,
                                                    &visible_body, diag) != 0) {
                if (ctx->func_env) {
                    ctx->func_env->current_func = saved_func;
                }
                ctx->func_depth--;
                tc_symbol_table_free(&visible_body);
                return -1;
            }
            /* 形参入口已初始化 */
            if (sym && ctx->init_states && sym->slot >= 0 && sym->slot < ctx->num_slots) {
                ctx->init_states[sym->slot] = TC_INIT_INIT;
            }
        }
        for (i = 0; i < func->body_count; i++) {
            if (tc_pass2_check_stmt(&func->body[i], symbols, &visible_body, struct_table, ctx,
                                    hist, warnings, diag) != 0) {
                if (ctx->func_env) {
                    ctx->func_env->current_func = saved_func;
                }
                ctx->func_depth--;
                tc_symbol_table_free(&visible_body);
                return -1;
            }
        }
        if (ctx->func_env) {
            ctx->func_env->current_func = saved_func;
        }
        ctx->func_depth--;
        tc_symbol_table_free(&visible_body);
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        tc_stmt_index_take(&ctx->index);
        if (ctx->func_depth == 0) {
            tc_diagnostic_set(diag, TC_ERR_LABEL_OUTSIDE_FUNCTION, stmt->u.label_def.line,
                              TC_COLUMN_UNKNOWN, "label is only allowed inside a function");
            return -1;
        }
        /* 标签合流点：恢复可达（简化：不合并多入边状态） */
        ctx->path_reachable = 1;
        if (hist) {
            hist->check_init = 1;
        }
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNCALL) {
        TcFuncallStmt *call = &stmt->u.funcall_stmt;
        size_t stmt_index = (size_t)tc_stmt_index_take(&ctx->index);

        if (!ctx->func_env) {
            return 0;
        }
        return tc_func_check_funcall(ctx->func_env, call->is_self, call->qualifier,
                                     call->member_name, call->target, call->args, call->arg_count,
                                     0, NULL, call->line, visible, symbols, hist, stmt_index,
                                     warnings, diag);
    }

    if (stmt->kind == TC_STMT_RETURN) {
        TcReturnStmt *ret = &stmt->u.return_stmt;
        size_t stmt_index = (size_t)tc_stmt_index_take(&ctx->index);

        if (!ctx->func_env) {
            return 0;
        }
        if (tc_func_check_return(ctx->func_env, ret, visible, symbols, hist, stmt_index, warnings,
                                 diag) != 0) {
            return -1;
        }
        ctx->path_reachable = 0;
        if (hist) {
            hist->check_init = 0;
        }
        return 0;
    }

    {
        size_t stmt_index = (size_t)tc_stmt_index_take(&ctx->index);

        if (stmt->kind == TC_STMT_VAR_DEF) {
            TcVarDef *var_def = &stmt->u.var_def;
            const TcSymbol *sym = NULL;

            if (var_def->rhs.kind == TC_RHS_FUNCALL_EXPR && ctx->func_env) {
                if (tc_pass2_check_funcall_rhs(&var_def->rhs, &var_def->full_type, 1, ctx,
                                               visible, symbols, hist, stmt_index, var_def->line,
                                               warnings, diag) != 0) {
                    return -1;
                }
            } else if (tc_type_check_rhs(&var_def->rhs, &var_def->full_type, visible, symbols,
                                         struct_table, hist, stmt_index, var_def->line, diag,
                                         warnings, var_def->name) != 0) {
                return -1;
            }
            if (tc_visible_add_from_global(symbols, var_def->name, (int)stmt_index, visible,
                                           diag) != 0) {
                return -1;
            }
            sym = tc_find_symbol_by_def_index(symbols, var_def->name, (int)stmt_index);
            if (sym && ctx->init_states && ctx->path_reachable && sym->slot >= 0 &&
                sym->slot < ctx->num_slots) {
                ctx->init_states[sym->slot] = TC_INIT_INIT;
            }
            return 0;
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
            if (tc_visible_add_from_global(symbols, const_def->name, (int)stmt_index, visible,
                                           diag) != 0) {
                return -1;
            }
            if (global_sym && ctx->init_states && global_sym->slot >= 0 &&
                global_sym->slot < ctx->num_slots) {
                ctx->init_states[global_sym->slot] = TC_INIT_INIT;
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            TcIoWrite *io_write = &stmt->u.io_write;

            if (tc_check_io_format(io_write->type, io_write->fmt, io_write->line, diag) != 0) {
                return -1;
            }
            return tc_check_operand(&io_write->operand, io_write->type, visible, symbols, hist,
                                    stmt_index, io_write->line, diag, warnings, NULL,
                                    TC_ERR_TYPE_MISMATCH);
        }

        if (stmt->kind == TC_STMT_READ) {
            TcRead *io_read = &stmt->u.io_read;
            const TcSymbol *target =
                tc_resolve_visible_symbol_scoped(visible, symbols, io_read->name, stmt_index,
                                                 io_read->line, diag,
                                                 ctx->func_env ? ctx->func_env->members : NULL,
                                                 ctx->func_depth > 0);

            if (!target) {
                return -1;
            }
            if (tc_func_check_writable_target(target, io_read->line, diag) != 0) {
                return -1;
            }
            if (target->type != io_read->type) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, io_read->line, TC_COLUMN_UNKNOWN,
                                  "read type does not match variable type");
                return -1;
            }
            tc_resolved_binding_set(&io_read->binding, target);
            if (ctx->init_states && ctx->path_reachable && target->slot >= 0 &&
                target->slot < ctx->num_slots) {
                ctx->init_states[target->slot] = TC_INIT_INIT;
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_ASSIGN) {
            TcAssign *assign = &stmt->u.assign;
            const TcSymbol *target =
                tc_resolve_visible_symbol_scoped(visible, symbols, assign->name, stmt_index,
                                                 assign->line, diag,
                                                 ctx->func_env ? ctx->func_env->members : NULL,
                                                 ctx->func_depth > 0);

            if (!target) {
                return -1;
            }
            if (target->sym_kind == TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, assign->line,
                                  TC_COLUMN_UNKNOWN, "cannot assign to constant");
                return -1;
            }
            if (tc_func_check_writable_target(target, assign->line, diag) != 0) {
                return -1;
            }
            tc_resolved_binding_set(&assign->binding, target);
            if (assign->rhs.kind == TC_RHS_FUNCALL_EXPR && ctx->func_env) {
                if (tc_pass2_check_funcall_rhs(&assign->rhs, &target->full_type, 1, ctx, visible,
                                               symbols, hist, stmt_index, assign->line, warnings,
                                               diag) != 0) {
                    return -1;
                }
            } else if (tc_type_check_rhs(&assign->rhs, &target->full_type, visible, symbols,
                                         struct_table, hist, stmt_index, assign->line, diag,
                                         warnings, NULL) != 0) {
                return -1;
            }
            if (ctx->init_states && ctx->path_reachable && target->slot >= 0 &&
                target->slot < ctx->num_slots) {
                ctx->init_states[target->slot] = TC_INIT_INIT;
            }
            return 0;
        }

        if (stmt->kind == TC_STMT_FIELD_ASSIGN) {
            return tc_struct_check_field_assign(&stmt->u.field_assign, struct_table, visible,
                                                symbols, hist, stmt_index, diag, warnings);
        }

        /* Phase 3：指针 / memblock 语句侧检查 */
        if (stmt->kind == TC_STMT_PTR_STORE) {
            return tc_ptr_check_store(&stmt->u.ptr_store, visible, symbols, hist, stmt_index, diag,
                                      warnings);
        }

        if (stmt->kind == TC_STMT_MEMBLOCK_STORE) {
            return tc_memblock_check_store(&stmt->u.memblock_store, visible, symbols, hist,
                                           stmt_index, diag, warnings);
        }

        if (stmt->kind == TC_STMT_MEMBLOCK_COPY) {
            return tc_memblock_check_copy(&stmt->u.memblock_copy, visible, symbols, hist,
                                          stmt_index, diag, warnings);
        }

        if (stmt->kind == TC_STMT_MEMCOPY_UNSAFE) {
            return tc_memblock_check_memcopy_unsafe(&stmt->u.memcopy_unsafe, visible, symbols,
                                                    hist, stmt_index, diag, warnings);
        }
    }

    return 0;
}

int tc_pass2_type_check(TcProgram *program, TcSymbolTable *symbols, TcStructTable *struct_table,
                        TcFuncCheckEnv *func_env, TcWarningList *warnings, TcDiagnostic *diag) {
    TcSymbolTable visible;
    TcAnalyzeCtx ctx;
    TcInitHistory hist;
    int *last_init = NULL;
    TcInitState *init_states = NULL;
    size_t i = 0;

    tc_symbol_table_init(&visible);
    memset(&ctx, 0, sizeof(ctx));
    ctx.program = program;
    ctx.func_env = func_env;
    tc_stmt_index_reset(&ctx.index);
    tc_block_path_init(&ctx.block_path);
    ctx.path_reachable = 1;
    ctx.current_loop_id = -1;
    ctx.loop_depth = 0;
    ctx.func_depth = 0;
    ctx.num_slots = (int)tc_symbol_table_runtime_slot_count(symbols);

    if (ctx.num_slots > 0) {
        last_init = (int *)malloc((size_t)ctx.num_slots * sizeof(int));
        init_states = (TcInitState *)malloc((size_t)ctx.num_slots * sizeof(TcInitState));
        if (!last_init || !init_states) {
            free(last_init);
            free(init_states);
            tc_symbol_table_free(&visible);
            tc_block_path_free(&ctx.block_path);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < (size_t)ctx.num_slots; i++) {
            last_init[i] = -1;
        }
        tc_init_states_reset(init_states, ctx.num_slots, TC_INIT_UNINIT);
        for (i = 0; i < symbols->count; i++) {
            const TcSymbol *sym = &symbols->symbols[i];

            if (sym->slot >= 0 && sym->slot < ctx.num_slots) {
                if (sym->sym_kind == TC_SYM_CONSTANT || sym->initialized) {
                    init_states[sym->slot] = TC_INIT_INIT;
                }
            }
        }
    }
    ctx.last_init = last_init;
    ctx.init_states = init_states;
    tc_prescan_init_history(program, symbols, &ctx);

    if (tc_pass2_collect_labels(program, symbols, &ctx, diag) != 0) {
        tc_symbol_table_free(&visible);
        tc_block_path_free(&ctx.block_path);
        free(last_init);
        free(init_states);
        return -1;
    }

    tc_stmt_index_reset(&ctx.index);
    ctx.block_path.depth = 0;
    ctx.path_reachable = 1;
    ctx.current_loop_id = -1;
    ctx.loop_depth = 0;
    hist.program = program;
    hist.last_init_stmt_index = NULL; /* 文件模式走路径敏感 init_states */
    hist.init_states = init_states;
    hist.num_slots = ctx.num_slots;
    hist.check_init = 1;
    hist.defer_to_cfg = 1;

    for (i = 0; i < program->count; i++) {
        if (tc_pass2_check_stmt(&program->items[i], symbols, &visible, struct_table, &ctx, &hist,
                                warnings, diag) != 0) {
            tc_symbol_table_free(&visible);
            tc_block_path_free(&ctx.block_path);
            free(last_init);
            free(init_states);
            return -1;
        }
    }

    tc_symbol_table_free(&visible);
    tc_block_path_free(&ctx.block_path);
    free(last_init);
    free(init_states);
    return 0;
}
