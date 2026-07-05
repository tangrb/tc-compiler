/*
 * tc_const_eval.c — let 常量编译期求值实现
 *
 * 从 tc_analyzer.c 中拆分出的独立模块，仅负责 let 定义的编译期常量计算。
 * 包含循环依赖检测（visiting 栈）、运行时错误到常量错误的映射、
 * 常量 cast 合法性检查，以及所有 RHS 种类的常量求值。
 */
#include "tc_const_eval.h"

#include "tc_diagnostic.h"
#include "tc_semantics.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  常量求值辅助                                                         */
/* ------------------------------------------------------------------ */

#define TC_CONST_VISIT_MAX 64

static int tc_const_visit_contains(const char *const *visiting, size_t count, const char *name) {
    size_t i = 0;
    for (i = 0; i < count; i++) {
        if (strcmp(visiting[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int tc_const_map_runtime_error(TcErrorKind kind, TcDiagnostic *diag, int line) {
    switch (kind) {
    case TC_ERR_INTEGER_OVERFLOW:
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "constant overflow");
        return -1;
    case TC_ERR_DIVISION_BY_ZERO:
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_DIV_ZERO, line, TC_COLUMN_UNKNOWN,
                          "constant division by zero");
        return -1;
    case TC_ERR_CAST_OVERFLOW:
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "constant cast overflow");
        return -1;
    default:
        return -1;
    }
}

static int tc_const_cast_allowed(TcIntType target, const TcValue *source) {
    TcIntType src_type = source->type;
    int src_bits = tc_type_bit_width(src_type);
    int dst_bits = tc_type_bit_width(target);

    if (tc_type_is_bool(src_type) || tc_type_is_bool(target)) {
        return 1;
    }
    if (dst_bits < src_bits) {
        return 0;
    }
    return 1;
}

static int tc_eval_const_operand(const TcOperand *operand, TcIntType expected,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 const char *const_name, const char *const *visiting,
                                 size_t visiting_count, TcValue *out, int line,
                                 TcDiagnostic *diag) {
    char msg[128];

    if (operand->kind == TC_OPERAND_LIT) {
        if (!tc_literal_fits_context(&operand->u.lit, expected, NULL)) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid literal in constant expression");
            return -1;
        }
        *out = tc_literal_to_value(&operand->u.lit, expected);
        return 0;
    }

    {
        const TcSymbol *symbol = NULL;

        if (const_name && strcmp(operand->u.name, const_name) == 0) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_CIRCULAR, line, TC_COLUMN_UNKNOWN,
                              "circular dependency in constant expression");
            return -1;
        }
        if (tc_const_visit_contains(visiting, visiting_count, operand->u.name)) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_CIRCULAR, line, TC_COLUMN_UNKNOWN,
                              "circular dependency in constant expression");
            return -1;
        }
        symbol = tc_symbol_table_find(visible, operand->u.name);
        if (!symbol) {
            snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (symbol->sym_kind != TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        if (!symbol->has_const_value) {
            const TcSymbol *global_sym = tc_symbol_table_find(global, operand->u.name);
            if (!global_sym || !global_sym->has_const_value) {
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "forward reference to constant");
                return -1;
            }
            if (global_sym->type != expected) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "operand type does not match operation type");
                return -1;
            }
            *out = global_sym->const_value;
            return 0;
        }
        if (symbol->type != expected) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "operand type does not match operation type");
            return -1;
        }
        *out = symbol->const_value;
        return 0;
    }
}

static int tc_eval_const_rhs(const TcRhs *rhs, TcIntType expected_type,
                             const TcSymbolTable *visible, const TcSymbolTable *global,
                             const char *const_name, const char *const *visiting,
                             size_t visiting_count, TcValue *out, int line, TcDiagnostic *diag);

static int tc_eval_const_rhs(const TcRhs *rhs, TcIntType expected_type,
                             const TcSymbolTable *visible, const TcSymbolTable *global,
                             const char *const_name, const char *const *visiting,
                             size_t visiting_count, TcValue *out, int line, TcDiagnostic *diag) {
    TcDiagnostic tmp_diag;
    TcValue lhs = {0};
    TcValue rhs_val = {0};

    if (rhs->kind == TC_RHS_LIT) {
        if (!tc_literal_fits_context(&rhs->u.lit, expected_type, NULL)) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid literal in constant expression");
            return -1;
        }
        *out = tc_literal_to_value(&rhs->u.lit, expected_type);
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        if (const_name && strcmp(rhs->u.const_ref.name, const_name) == 0) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_CIRCULAR, line, TC_COLUMN_UNKNOWN,
                              "circular dependency in constant expression");
            return -1;
        }
        if (tc_const_visit_contains(visiting, visiting_count, rhs->u.const_ref.name)) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_CIRCULAR, line, TC_COLUMN_UNKNOWN,
                              "circular dependency in constant expression");
            return -1;
        }
        {
            const TcSymbol *symbol = tc_symbol_table_find(visible, rhs->u.const_ref.name);
            char msg[128];
            if (!symbol) {
                snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.const_ref.name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            if (!symbol->has_const_value) {
                const TcSymbol *global_sym =
                    tc_symbol_table_find(global, rhs->u.const_ref.name);
                if (!global_sym || !global_sym->has_const_value) {
                    tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                      "forward reference to constant");
                    return -1;
                }
                if (global_sym->type != expected_type) {
                    tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                      "constant type does not match expected type");
                    return -1;
                }
                *out = global_sym->const_value;
                return 0;
            }
            if (symbol->type != expected_type) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "constant type does not match expected type");
                return -1;
            }
            *out = symbol->const_value;
            return 0;
        }
    }

    if (rhs->kind == TC_RHS_ARITH) {
        if (rhs->u.arith.mode == TC_ARITH_WRAP) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "wrap is not allowed in constant expression");
            return -1;
        }
        if (rhs->u.arith.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.lhs, rhs->u.arith.type, visible, global,
                                  const_name, visiting, visiting_count, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.rhs, rhs->u.arith.type, visible, global,
                                  const_name, visiting, visiting_count, &rhs_val, line, diag) !=
            0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_arith(rhs->u.arith.op, rhs->u.arith.type, TC_ARITH_STRICT, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        if (rhs->u.unary.mode == TC_ARITH_WRAP) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "wrap is not allowed in constant expression");
            return -1;
        }
        if (rhs->u.unary.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.unary.operand, rhs->u.unary.type, visible, global,
                                  const_name, visiting, visiting_count, &lhs, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_unary(rhs->u.unary.op, rhs->u.unary.type, TC_ARITH_STRICT, &lhs, out,
                          &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.compare.lhs, rhs->u.compare.type, visible, global,
                                  const_name, visiting, visiting_count, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.compare.rhs, rhs->u.compare.type, visible, global,
                                  const_name, visiting, visiting_count, &rhs_val, line, diag) !=
            0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type, &lhs, &rhs_val, out, &tmp_diag,
                            line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_bin.lhs, TC_BOOL, visible, global, const_name,
                                  visiting, visiting_count, &lhs, line, diag) != 0) {
            return -1;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_AND && lhs.bits == 0) {
            *out = tc_value_make(TC_BOOL, 0);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs.bits != 0) {
            *out = tc_value_make(TC_BOOL, 1);
            return 0;
        }
        if (tc_eval_const_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, const_name,
                                  visiting, visiting_count, &rhs_val, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_un.operand, TC_BOOL, visible, global, const_name,
                                  visiting, visiting_count, &lhs, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_logic_unary(rhs->u.logic_un.op, &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        TcValue src_val = {0};

        if (rhs->u.const_cast.target != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant cast target type mismatch");
            return -1;
        }
        if (rhs->u.const_cast.source.kind == TC_OPERAND_LIT) {
            if (rhs->u.const_cast.source.u.lit.is_bool) {
                src_val = tc_literal_to_value(&rhs->u.const_cast.source.u.lit, TC_BOOL);
            } else {
                if (!tc_literal_fits_context(&rhs->u.const_cast.source.u.lit,
                                             rhs->u.const_cast.target, NULL)) {
                    tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                      "invalid literal in constant cast");
                    return -1;
                }
                src_val = tc_literal_to_value(&rhs->u.const_cast.source.u.lit,
                                              rhs->u.const_cast.target);
            }
        } else if (rhs->u.const_cast.source.kind == TC_OPERAND_VAR) {
            const TcSymbol *symbol =
                tc_symbol_table_find(visible, rhs->u.const_cast.source.u.name);
            char msg[128];

            if (const_name && strcmp(rhs->u.const_cast.source.u.name, const_name) == 0) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_CIRCULAR, line, TC_COLUMN_UNKNOWN,
                                  "circular dependency in constant expression");
                return -1;
            }
            if (!symbol || symbol->sym_kind != TC_SYM_CONSTANT) {
                snprintf(msg, sizeof(msg), "undefined variable '%s'",
                         rhs->u.const_cast.source.u.name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            {
                const TcSymbol *global_sym =
                    tc_symbol_table_find(global, rhs->u.const_cast.source.u.name);
                if (!global_sym || !global_sym->has_const_value) {
                    tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                      "forward reference to constant");
                    return -1;
                }
                src_val = global_sym->const_value;
            }
        } else {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid constant cast source");
            return -1;
        }
        if (!tc_const_cast_allowed(rhs->u.const_cast.target, &src_val)) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_CAST_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                              "constant cast overflow");
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_cast(rhs->u.const_cast.target, TC_TRUNC_STRICT, &src_val, out, &tmp_diag,
                         line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                      "invalid constant expression");
    return -1;
}

/* ------------------------------------------------------------------ */
/*  公开接口 — 编译期求值 let RHS                                        */
/* ------------------------------------------------------------------ */

int tc_resolve_const_value(TcSymbol *sym, const TcRhs *rhs, const TcSymbolTable *visible,
                           const TcSymbolTable *global, int line, TcDiagnostic *diag) {
    TcValue value = {0};
    const char *visiting[TC_CONST_VISIT_MAX];
    size_t visiting_count = 0;

    if (visiting_count < TC_CONST_VISIT_MAX) {
        visiting[visiting_count++] = sym->name;
    }

    if (tc_eval_const_rhs(rhs, sym->type, visible, global, sym->name, visiting, visiting_count,
                          &value, line, diag) != 0) {
        return -1;
    }
    sym->const_value = value;
    sym->has_const_value = 1;
    return 0;
}
