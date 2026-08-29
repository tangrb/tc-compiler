/*
 * tc_analyzer_pass2_rhs.c — Pass2 RHS 语义检查（tc_check_rhs 分发）
 *
 * 从 tc_analyzer_pass2.c 拆出：operand/RHS 名字预检、tc_check_rhs 巨型分发、
 * 条件检查、funcall RHS 与可见性拷贝辅助。
 */
#include "tc_analyzer_pass2_rhs.h"

#include "tc_analyzer_internal.h"
#include "tc_analyze_6a.h"
#include "tc_analyze_6e.h"
#include "tc_type_check.h"
#include "tc_memblock_check.h"
#include "tc_ptr_check.h"
#include "tc_struct_check.h"
#include "tc_func_check.h"
#include "tc_const_eval.h"
#include "tc_scope.h"
#include "tc_semantics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const TcSymbol *tc_resolve_visible_symbol_scoped(const TcSymbolTable *visible,
                                                        const TcSymbolTable *global,
                                                        const char *name, size_t stmt_index,
                                                        int line, TcDiagnostic *diag,
                                                        const TcMemberIndex *members,
                                                        int in_function) {
    const TcSymbol *symbol = NULL;
    char msg[128];

    if (name && (strncmp(name, "Self.", 5) == 0 || strchr(name, '.') != NULL)) {
        symbol = tc_find_named_binding(visible, global, name);
        if (symbol) {
            return symbol;
        }
        (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
        return NULL;
    }

    symbol = tc_symbol_table_find(visible, name);

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
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return NULL;
        }
    }
    if (in_function && members &&
        tc_func_try_function_scope_access(members, name, line, diag)) {
        return NULL;
    }
    (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
    tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
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
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
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
    if (operand->kind == TC_OPERAND_FIELD_READ) {
        /* 基址解析（含 Self / static let）在 tc_struct_check_field_access 完成 */
        return 0;
    }
    return tc_precheck_name_binding(operand->u.name, &operand->binding, visible, global,
                                    stmt_index, line, diag, self_name);
}

int tc_precheck_rhs_names(TcRhs *rhs, const TcSymbolTable *visible,
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
        /* 复合/调用 RHS 的名字与类型由 tc_type_check_rhs / tc_*_check 负责 */
        return 0;
    }
    return 0;
}

int tc_check_operand(TcOperand *operand, TcTypeTag expected,
                            const TcSymbolTable *visible, const TcSymbolTable *global,
                            const TcStructTable *struct_table, TcInitHistory *hist,
                            size_t stmt_index, int line, TcDiagnostic *diag,
                            TcWarningList *warnings, const char *self_name, TcErrorKind type_err) {
    char msg[128];

    (void)warnings;
    if (operand->kind == TC_OPERAND_LIT) {
        return tc_check_literal(&operand->u.lit, expected, line, diag, type_err);
    }

    if (operand->kind == TC_OPERAND_FIELD_READ) {
        return tc_struct_check_field_access(&operand->u.field_read,
                                            tc_type_tag_singleton(expected), struct_table,
                                            visible, global, hist, stmt_index, line, diag,
                                            warnings, self_name);
    }

    {
        const TcSymbol *symbol = NULL;

        if (self_name && operand->u.name && strcmp(operand->u.name, self_name) == 0) {
            (void)snprintf(msg, sizeof(msg),
                     "variable '%s' cannot reference itself in its initializer", self_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        symbol = tc_resolve_visible_symbol(visible, global, operand->u.name, stmt_index, line,
                                           diag);
        if (!symbol) {
            return -1;
        }
        if (tc_type_tag_of(symbol->type) != expected) {
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

int tc_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                        const TcSymbolTable *global, const TcStructTable *struct_table,
                        TcInitHistory *hist, size_t stmt_index,
                        int line, TcDiagnostic *diag, TcWarningList *warnings,
                        const char *self_name) {
    char msg[128];

    if (!expected) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "missing expected type for rhs check");
        return -1;
    }

    if (tc_precheck_rhs_names(rhs, visible, global, stmt_index, line, diag, self_name) != 0) {
        return -1;
    }

    if (rhs->kind == TC_RHS_LIT) {
        if (!tc_literal_fits_context(&rhs->u.lit, expected->tag, NULL)) {
            TcErrorKind err_kind = TC_CE_LITERAL_OUT_OF_RANGE;
            tc_literal_fits_context(&rhs->u.lit, expected->tag, &err_kind);
            if (err_kind == TC_CE_LITERAL_TYPE) {
                tc_diagnostic_set(diag, TC_CE_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                                  "literal type does not match variable type");
            } else {
                tc_diagnostic_set(diag, TC_CE_LITERAL_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                                  "literal out of range for variable type");
            }
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        if (tc_validate_arith_mode(rhs->u.arith.op, rhs->u.arith.type->tag,
                                   rhs->u.arith.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.arith.lhs, rhs->u.arith.type->tag, visible, global, struct_table, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.arith.rhs, rhs->u.arith.type->tag, visible, global, struct_table, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.arith.type->tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        if (tc_validate_unary_mode(rhs->u.unary.op, rhs->u.unary.type->tag,
                                   rhs->u.unary.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.unary.operand, rhs->u.unary.type->tag, visible, global, struct_table, hist, stmt_index,
                             line, diag, warnings, self_name, TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.unary.type->tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        if (tc_check_operand(&rhs->u.compare.lhs, rhs->u.compare.type->tag, visible, global, struct_table, hist, stmt_index,
                             line, diag, warnings, self_name,
                             TC_CE_COMPARISON_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.compare.rhs, rhs->u.compare.type->tag, visible, global, struct_table, hist, stmt_index,
                             line, diag, warnings, self_name,
                             TC_CE_COMPARISON_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(expected->tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        int saved_check_init = hist ? hist->check_init : 1;
        TcStaticBoolResult lhs_value = TC_STATIC_BOOL_UNKNOWN;

        if (tc_check_operand(&rhs->u.logic_bin.lhs, TC_BOOL, visible, global, struct_table, hist, stmt_index, line, diag,
                             warnings, self_name, TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        /*
         * 逻辑短路（与 Executor / 语言标准对称）：
         *   and + 静态 false / or + 静态 true → 不检查 rhs 未初始化，
         *   仍校验 rhs 存在性与类型（check_init = 0）。
         */
        tc_try_eval_static_bool_operand(&rhs->u.logic_bin.lhs, &lhs_value);
        if ((rhs->u.logic_bin.op == TC_LOGIC_AND && lhs_value == TC_STATIC_BOOL_FALSE) ||
            (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs_value == TC_STATIC_BOOL_TRUE)) {
            if (hist) {
                hist->check_init = 0;
            }
            if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, struct_table, hist,
                                 stmt_index, line, diag, warnings, self_name,
                                 TC_CE_TYPE_MISMATCH) != 0) {
                if (hist) {
                    hist->check_init = saved_check_init;
                }
                return -1;
            }
            if (hist) {
                hist->check_init = saved_check_init;
            }
        } else if (tc_check_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, struct_table, hist,
                                    stmt_index, line, diag, warnings, self_name,
                                    TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(expected->tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        if (tc_check_operand(&rhs->u.logic_un.operand, TC_BOOL, visible, global, struct_table, hist, stmt_index, line,
                             diag, warnings, self_name, TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(expected->tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        if (tc_type_is_bool(rhs->u.bitwise_bin.type->tag) || tc_type_is_float(rhs->u.bitwise_bin.type->tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitwise operation requires integer type");
            return -1;
        }
        if (tc_check_operand(&rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type->tag, visible, global, struct_table, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type->tag, visible, global, struct_table, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.bitwise_bin.type->tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        if (tc_type_is_bool(rhs->u.bitwise_un.type->tag) || tc_type_is_float(rhs->u.bitwise_un.type->tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitwise operation requires integer type");
            return -1;
        }
        if (tc_check_operand(&rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type->tag, visible, global, struct_table, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.bitwise_un.type->tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        if (tc_validate_shift_mode(rhs->u.shift.op, rhs->u.shift.type->tag,
                                   rhs->u.shift.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.shift.value, rhs->u.shift.type->tag, visible, global, struct_table, hist, stmt_index,
                             line, diag, warnings, self_name, TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.shift.count, rhs->u.shift.type->tag, visible, global, struct_table, hist, stmt_index,
                             line, diag, warnings, self_name, TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.shift.type->tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        if (tc_validate_fp_arith_mode(rhs->u.float_arith.op, rhs->u.float_arith.type->tag,
                                      rhs->u.float_arith.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_arith.lhs, rhs->u.float_arith.type->tag, visible, global,
                             struct_table, hist, stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_arith.rhs, rhs->u.float_arith.type->tag, visible, global,
                             struct_table, hist, stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.float_arith.type->tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        if (tc_validate_fp_unary_mode(rhs->u.float_unary.op, rhs->u.float_unary.type->tag,
                                      rhs->u.float_unary.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_unary.operand, rhs->u.float_unary.type->tag, visible,
                             global, struct_table, hist, stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.float_unary.type->tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        if (tc_validate_fp_compare_mode(rhs->u.float_compare.type->tag,
                                        rhs->u.float_compare.mode, diag, line) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_compare.lhs, rhs->u.float_compare.type->tag, visible,
                             global, struct_table, hist, stmt_index, line, diag, warnings, self_name,
                             TC_CE_COMPARISON_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.float_compare.rhs, rhs->u.float_compare.type->tag, visible,
                             global, struct_table, hist, stmt_index, line, diag, warnings, self_name,
                             TC_CE_COMPARISON_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(expected->tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "assignment type does not match rhs result type");
            return -1;
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_BITCAST) {
        TcBitcastRhs *bitcast = &rhs->u.bitcast;
        TcTypeTag source_tag = TC_INT32;
        const TcType *source_full = NULL;
        const TcSymbol *source = NULL;
        size_t target_width = 0;
        size_t source_width = 0;

        if (tc_type_is_bool(bitcast->target.tag) ||
            (!tc_type_is_integer(bitcast->target.tag) && !tc_type_is_float(bitcast->target.tag) &&
             bitcast->target.tag != TC_PTR)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitcast target must be a non-bool integer, float, or ptr type");
            return -1;
        }
        target_width = tc_sizeof_bits(&bitcast->target);
        if (bitcast->source.kind == TC_OPERAND_VAR) {
            if (self_name && strcmp(bitcast->source.u.name, self_name) == 0) {
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "variable cannot reference itself in its initializer");
                return -1;
            }
            source = tc_resolve_visible_symbol(visible, global, bitcast->source.u.name,
                                               stmt_index, line, diag);
            if (!source) {
                return -1;
            }
            source_full = source->type;
            source_tag = tc_type_tag_of(source_full);
            if (tc_check_operand_init(hist, source, stmt_index, line, diag) != 0) {
                return -1;
            }
            tc_resolved_binding_set(&bitcast->source.binding, source);
            source_width = tc_sizeof_bits(source_full);
        } else if (bitcast->source.kind == TC_OPERAND_FIELD_READ) {
            if (tc_struct_check_field_access(&bitcast->source.u.field_read, NULL, struct_table,
                                              visible, global, hist, stmt_index, line, diag,
                                              warnings, self_name) != 0) {
                return -1;
            }
            source_full = bitcast->source.u.field_read.resolved.field_type;
            source_tag = tc_type_tag_of(source_full);
            source_width = tc_sizeof_bits(source_full);
        } else if (bitcast->source.kind == TC_OPERAND_LIT &&
                   bitcast->source.u.lit.is_bool) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        } else if (bitcast->source.kind != TC_OPERAND_LIT) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitcast source must be a variable, field, or literal");
            return -1;
        } else if (bitcast->source.u.lit.is_float) {
            if (!isfinite(bitcast->source.u.lit.float_value) &&
                !bitcast->source.u.lit.float32_suffix) {
                source_tag = target_width == 32 ? TC_FLOAT32 : TC_FLOAT64;
            } else {
                source_tag = bitcast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
            }
            source_width = (size_t)tc_type_bit_width(source_tag);
        } else if (target_width == 32) {
            source_tag = bitcast->source.u.lit.unsigned_suffix ? TC_UINT32 : TC_INT32;
            source_width = 32;
        } else if (target_width == 64) {
            source_tag = bitcast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
            source_width = 64;
        } else if (target_width == 16) {
            source_tag = bitcast->source.u.lit.unsigned_suffix ? TC_UINT16 : TC_INT16;
            source_width = 16;
        } else {
            source_tag = bitcast->source.u.lit.unsigned_suffix ? TC_UINT8 : TC_INT8;
            source_width = 8;
        }
        if (tc_type_is_bool(source_tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        }
        if (source_width != target_width) {
            tc_diagnostic_set(diag, TC_CE_BITCAST_WIDTH, line, TC_COLUMN_UNKNOWN,
                              "bitcast source and target widths must match");
            return -1;
        }
        if (bitcast->source.kind == TC_OPERAND_LIT &&
            tc_check_literal(&bitcast->source.u.lit, source_tag, line, diag,
                             TC_CE_LITERAL_TYPE) != 0) {
            return -1;
        }
        if (bitcast->target.tag == TC_PTR) {
            if (!tc_type_equals(&bitcast->target, expected)) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "bitcast target type does not match variable type");
                return -1;
            }
        } else if (bitcast->target.tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bitcast target type does not match variable type");
            return -1;
        }
        if (tc_pass2_resolve_target_type(hist, &bitcast->target, &bitcast->target_type, line,
                                         diag) != 0) {
            return -1;
        }
        bitcast->target_type_resolved = 1;
        bitcast->source_type =
            source_full ? source_full : tc_type_tag_singleton(source_tag);
        bitcast->source_type_resolved = 1;
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        const TcSymbol *source = NULL;

        if (self_name && strcmp(rhs->u.const_ref.name, self_name) == 0) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", self_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        source = tc_resolve_visible_symbol(visible, global, rhs->u.const_ref.name, stmt_index,
                                           line, diag);
        if (!source) {
            return -1;
        }
        if (tc_type_tag_of(source->type) != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
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
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant cast is only allowed in let initializer");
        return -1;
    }

    if (rhs->kind != TC_RHS_CAST) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN, "unsupported rhs kind");
        return -1;
    }

    {
        TcCastRhs *cast = &rhs->u.cast;
        const TcSymbol *source = NULL;
        TcTypeTag source_tag = TC_INT64;
        const TcType *source_full = NULL;

        if (cast->target.tag == TC_STRUCT || cast->target.tag == TC_MEMBLOCK ||
            cast->target.tag == TC_VOID) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast target must be scalar or ptr type");
            return -1;
        }

        if (cast->source.kind == TC_OPERAND_VAR) {
            if (self_name && strcmp(cast->source.u.name, self_name) == 0) {
                (void)snprintf(msg, sizeof(msg),
                         "variable '%s' cannot reference itself in its initializer", self_name);
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
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
            source_full = source->type;
            source_tag = tc_type_tag_of(source_full);
            tc_resolved_binding_set(&cast->source.binding, source);
        } else if (cast->source.kind == TC_OPERAND_FIELD_READ) {
            if (tc_struct_check_field_access(&cast->source.u.field_read, NULL, struct_table,
                                              visible, global, hist, stmt_index, line, diag,
                                              warnings, self_name) != 0) {
                return -1;
            }
            source_full = cast->source.u.field_read.resolved.field_type;
            source_tag = tc_type_tag_of(source_full);
        } else if (cast->source.kind == TC_OPERAND_LIT &&
                   cast->source.u.lit.is_nullptr) {
            source_tag = TC_PTR;
        } else if (cast->source.kind == TC_OPERAND_LIT &&
                   cast->source.u.lit.is_bool) {
            source_tag = TC_BOOL;
        } else if (cast->source.kind == TC_OPERAND_LIT &&
                   cast->source.u.lit.is_float) {
            source_tag = cast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
        } else if (cast->source.kind == TC_OPERAND_LIT) {
            source_tag = cast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
        } else {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast source must be a variable, field, or literal");
            return -1;
        }

        /* ptr<U> → ptr<T>：等宽所指类型；不可 cast 到整数/浮点 */
        if (cast->target.tag == TC_PTR) {
            if (cast->mode != TC_TRUNC_STRICT) {
                tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "truncate cannot be used with pointer cast");
                return -1;
            }
            if (source_tag != TC_PTR) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "pointer cast requires a pointer source");
                return -1;
            }
            if (source_full && source_full->params.ptr_type.pointee &&
                cast->target.params.ptr_type.pointee) {
                size_t tw = tc_sizeof_bits(cast->target.params.ptr_type.pointee);
                size_t sw = tc_sizeof_bits(source_full->params.ptr_type.pointee);

                if (tw == 0 || sw == 0 || tw != sw) {
                    tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                      "pointer cast requires equal-width pointee types");
                    return -1;
                }
            }
            if (!tc_type_equals(&cast->target, expected)) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "cast target type does not match variable type");
                return -1;
            }
            if (tc_pass2_resolve_target_type(hist, &cast->target, &cast->target_type, line,
                                             diag) != 0) {
                return -1;
            }
            cast->target_type_resolved = 1;
            /*
             * nullptr 源无所指类型 U：跳过等宽检查（上文 source_full 为空时已跳过），
             * 由目标期望 ptr<T> 定型即可。
             */
            cast->source_type =
                source_full ? source_full : tc_type_tag_singleton(TC_PTR);
            cast->source_type_resolved = 1;
            return 0;
        }

        if (source_tag == TC_PTR) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr cannot cast to integer or float type");
            return -1;
        }
        if (cast->source.kind == TC_OPERAND_LIT &&
            tc_check_literal(&cast->source.u.lit, source_tag, line, diag,
                             TC_CE_LITERAL_TYPE) != 0) {
            return -1;
        }
        if (cast->mode == TC_TRUNC_TRUNCATE &&
            (!tc_type_is_integer(cast->target.tag) || !tc_type_is_integer(source_tag) ||
             tc_type_bit_width(cast->target.tag) >= tc_type_bit_width(source_tag))) {
            tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "truncate requires an integer target narrower than the source");
            return -1;
        }
        if (cast->target.tag != expected->tag) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "cast target type does not match variable type");
            return -1;
        }
        if (tc_pass2_resolve_target_type(hist, &cast->target, &cast->target_type, line,
                                         diag) != 0) {
            return -1;
        }
        cast->target_type_resolved = 1;
        cast->source_type = tc_type_tag_singleton(source_tag);
        cast->source_type_resolved = 1;
    }
    return 0;
}

int tc_check_condition(TcRhs *rhs, const TcSymbolTable *visible,
                              const TcSymbolTable *global, const TcStructTable *struct_table,
                              TcInitHistory *hist, size_t stmt_index, int line, const char *owner,
                              TcDiagnostic *diag, TcWarningList *warnings) {
    if (tc_check_rhs(rhs, tc_type_tag_singleton(TC_BOOL), visible, global, struct_table, hist,
                     stmt_index, line, diag, warnings, NULL) != 0) {
        if (diag->kind == TC_CE_TYPE_MISMATCH) {
            char msg[64];

            (void)snprintf(msg, sizeof(msg), "%s condition must be bool", owner);
            tc_diagnostic_set(diag, TC_CE_CONDITION_TYPE, line, TC_COLUMN_UNKNOWN,
                              msg);
        }
        return -1;
    }
    return 0;
}

int tc_visible_copy_from(const TcSymbolTable *src, TcSymbolTable *dst,
                                TcDiagnostic *diag) {
    size_t i = 0;

    tc_symbol_table_init(dst);
    for (i = 0; i < src->count; i++) {
        const TcSymbol *sym = &src->symbols[i];
        TcSymbol *mut = NULL;

        /* 指针拷贝：type 指向程序 type_table / 单例 */
        if (tc_symbol_table_add_ex(dst, sym->name, sym->type, sym->slot, sym->slot_domain,
                                   sym->def_line, sym->def_stmt_index, sym->sym_kind,
                                   sym->initialized, diag) != 0) {
            return -1;
        }
        mut = &dst->symbols[dst->count - 1];
        mut->scope_end_stmt_index = sym->scope_end_stmt_index;
        mut->ptr_target_readonly = sym->ptr_target_readonly;
        if (sym->has_const_value) {
            mut->has_const_value = 1;
            mut->const_value = sym->const_value;
        }
    }
    return 0;
}

const TcSymbol *tc_find_symbol_by_def_index(const TcSymbolTable *global, const char *name,
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

int tc_visible_add_from_global(const TcSymbolTable *global, const char *name,
                                      int def_stmt_index, TcSymbolTable *visible,
                                      TcDiagnostic *diag) {
    const TcSymbol *sym = tc_find_symbol_by_def_index(global, name, def_stmt_index);
    TcSymbol *added = NULL;

    if (!sym) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, 0, TC_COLUMN_UNKNOWN, "internal analyzer error");
        return -1;
    }
    if (tc_symbol_table_add_ex(visible, sym->name, sym->type, sym->slot, sym->slot_domain,
                               sym->def_line, sym->def_stmt_index, sym->sym_kind,
                               sym->initialized, diag) != 0) {
        return -1;
    }
    added = &visible->symbols[visible->count - 1];
    added->has_const_value = sym->has_const_value;
    added->const_value = sym->const_value;
    added->scope_end_stmt_index = sym->scope_end_stmt_index;
    added->ptr_target_readonly = sym->ptr_target_readonly;
    return 0;
}

int tc_pass2_check_funcall_rhs(TcRhs *rhs, const TcType *expected, int position,
                                      TcAnalyzeCtx *ctx, const TcSymbolTable *visible,
                                      const TcSymbolTable *symbols, TcInitHistory *hist,
                                      size_t stmt_index, int line, TcWarningList *warnings,
                                      TcDiagnostic *diag) {
    TcNamedArg *args = NULL;
    size_t i = 0;
    int rc = 0;

    if (!ctx->func_env || rhs->kind != TC_RHS_FUNCALL_EXPR) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN, "expected funcall");
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
                               warnings, &rhs->u.funcall_expr.resolved_func_id, diag);
    if (rc == 0 && args) {
        /* type_check 写在副本上的 binding 须写回 AST，供 Executor 消费 */
        for (i = 0; i < rhs->u.funcall_expr.arg_count; i++) {
            if (rhs->u.funcall_expr.args[i].value) {
                memcpy(rhs->u.funcall_expr.args[i].value, &args[i].value, sizeof(TcRhs));
            }
        }
    }
    free(args);
    return rc;
}
