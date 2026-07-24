/*
 * tc_const_eval.c — let 常量编译期求值实现
 *
 * 从 tc_analyzer.c 中拆分出的独立模块，仅负责 let 定义的编译期常量计算。
 * 依靠源序可见性阻止自引用/前向引用，并负责运行时错误映射、共享
 * cast 语义以及所有合法 RHS 种类的常量求值。
 */
#include "tc_const_eval.h"

#include "tc_diagnostic.h"
#include "tc_semantics.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  常量求值辅助                                                         */
/* ------------------------------------------------------------------ */

/*
 * 将运行时语义错误（tc_exec_* 产生的 TC_ERR_*）映射为对应的
 * 编译期常量错误（TC_ERR_CONSTANT_*）。
 *
 * 运行时错误类型与常量错误类型一一对应，这种区分使 TC 语言的
 * 错误报告能精确区分"运行时溢出"和"编译期常量溢出"，
 * 便于测试断言和用户定位。
 */
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
    case TC_ERR_FLOAT_OVERFLOW:
    case TC_ERR_FLOAT_UNDERFLOW:
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_OVERFLOW, line, TC_COLUMN_UNKNOWN,
                          "constant overflow");
        return -1;
    case TC_ERR_FLOAT_INVALID:
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "invalid floating-point constant expression");
        return -1;
    default:
        return -1;
    }
}

static int tc_try_eval_bound_operand(const TcOperand *operand, TcTypeKind expected, TcValue *out) {
    if (operand->kind == TC_OPERAND_LIT) {
        if (!tc_literal_fits_context(&operand->u.lit, expected, NULL)) {
            return 0;
        }
        *out = tc_literal_to_value(&operand->u.lit, expected);
        return 1;
    }
    if (!operand->binding.resolved || !operand->binding.is_const ||
        operand->binding.type != expected) {
        return 0;
    }
    *out = tc_value_make(expected, operand->binding.const_bits);
    return 1;
}

static int tc_try_eval_bound_const_ref(const TcRhs *rhs, TcValue *out) {
    const TcResolvedBinding *binding = &rhs->u.const_ref.binding;

    if (!binding->resolved || !binding->is_const || binding->type != TC_BOOL) {
        return 0;
    }
    *out = tc_value_make(TC_BOOL, binding->const_bits);
    return 1;
}

void tc_try_eval_static_bool_operand(const TcOperand *operand, TcStaticBoolResult *result) {
    TcValue value = {0};

    *result = TC_STATIC_BOOL_UNKNOWN;
    if (tc_try_eval_bound_operand(operand, TC_BOOL, &value)) {
        *result = value.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
    }
}

int tc_try_eval_static_bool(const TcRhs *rhs, int line, TcStaticBoolResult *result,
                            TcDiagnostic *diag) {
    TcDiagnostic tmp_diag;
    TcValue lhs = {0};
    TcValue rhs_value = {0};
    TcValue out = {0};
    int status = 0;

    *result = TC_STATIC_BOOL_UNKNOWN;
    if (rhs->kind == TC_RHS_LIT) {
        if (rhs->u.lit.is_bool) {
            *result = rhs->u.lit.magnitude == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
        }
        return 0;
    }
    if (rhs->kind == TC_RHS_CONST_REF) {
        if (tc_try_eval_bound_const_ref(rhs, &out)) {
            *result = out.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
        }
        return 0;
    }

    switch (rhs->kind) {
    case TC_RHS_COMPARE:
        if (!tc_try_eval_bound_operand(&rhs->u.compare.lhs, rhs->u.compare.type, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.compare.rhs, rhs->u.compare.type, &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type, &lhs, &rhs_value, &out,
                                 &tmp_diag, line);
        break;
    case TC_RHS_FLOAT_COMPARE:
        if (!tc_try_eval_bound_operand(&rhs->u.float_compare.lhs,
                                       rhs->u.float_compare.type, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.float_compare.rhs,
                                       rhs->u.float_compare.type, &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_fp_compare(rhs->u.float_compare.op, rhs->u.float_compare.type,
                                    rhs->u.float_compare.mode, &lhs, &rhs_value, &out,
                                    &tmp_diag, line);
        break;
    case TC_RHS_LOGIC_BIN:
        if (!tc_try_eval_bound_operand(&rhs->u.logic_bin.lhs, TC_BOOL, &lhs) ||
            !tc_try_eval_bound_operand(&rhs->u.logic_bin.rhs, TC_BOOL, &rhs_value)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_value, &out, &tmp_diag,
                                      line);
        break;
    case TC_RHS_LOGIC_UN:
        if (!tc_try_eval_bound_operand(&rhs->u.logic_un.operand, TC_BOOL, &lhs)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_logic_unary(rhs->u.logic_un.op, &lhs, &out, &tmp_diag, line);
        break;
    case TC_RHS_CAST:
        if (rhs->u.cast.target != TC_BOOL || rhs->u.cast.mode != TC_TRUNC_STRICT ||
            !rhs->u.cast.source_type_resolved ||
            !tc_try_eval_bound_operand(&rhs->u.cast.source, rhs->u.cast.source_type, &lhs)) {
            return 0;
        }
        tc_diagnostic_init(&tmp_diag);
        status = tc_exec_cast(TC_BOOL, &lhs, &out, &tmp_diag, line);
        break;
    default:
        return 0;
    }

    if (status != 0) {
        (void)tc_const_map_runtime_error(tmp_diag.kind, diag, line);
        tc_diagnostic_clear(&tmp_diag);
        return -1;
    }
    tc_diagnostic_clear(&tmp_diag);
    *result = out.bits == 0 ? TC_STATIC_BOOL_FALSE : TC_STATIC_BOOL_TRUE;
    return 0;
}

static int tc_eval_const_operand(const TcOperand *operand, TcTypeKind expected,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 const char *const_name, TcValue *out, int line,
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
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        symbol = tc_symbol_table_find(visible, operand->u.name);
        if (!symbol) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", operand->u.name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (symbol->sym_kind != TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        (void)global;
        if (!symbol->has_const_value) {
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                              "constant value is not available by source order");
            return -1;
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

static int tc_eval_const_rhs(const TcRhs *rhs, TcTypeKind expected_type,
                             const TcSymbolTable *visible, const TcSymbolTable *global,
                             const char *const_name, TcValue *out, int line,
                             TcDiagnostic *diag);

static int tc_eval_const_rhs(const TcRhs *rhs, TcTypeKind expected_type,
                             const TcSymbolTable *visible, const TcSymbolTable *global,
                             const char *const_name, TcValue *out, int line,
                             TcDiagnostic *diag) {
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
            char msg[128];

            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.const_ref.name);
            tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        {
            const TcSymbol *symbol = tc_symbol_table_find(visible, rhs->u.const_ref.name);
            char msg[128];
            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", rhs->u.const_ref.name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            if (!symbol->has_const_value) {
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "constant value is not available by source order");
                return -1;
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
        if (tc_validate_arith_mode(rhs->u.arith.op, rhs->u.arith.type,
                                   rhs->u.arith.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.arith.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.lhs, rhs->u.arith.type, visible, global,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.arith.rhs, rhs->u.arith.type, visible, global,
                                  const_name, &rhs_val, line, diag) !=
            0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_arith(rhs->u.arith.op, rhs->u.arith.type, rhs->u.arith.mode, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        if (tc_validate_unary_mode(rhs->u.unary.op, rhs->u.unary.type,
                                   rhs->u.unary.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.unary.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.unary.operand, rhs->u.unary.type, visible, global,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_unary(rhs->u.unary.op, rhs->u.unary.type, rhs->u.unary.mode, &lhs, out,
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
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.compare.rhs, rhs->u.compare.type, visible, global,
                                  const_name, &rhs_val, line, diag) !=
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
                                  &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.logic_bin.rhs, TC_BOOL, visible, global, const_name,
                                  &rhs_val, line, diag) != 0) {
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
                                  &lhs, line, diag) != 0) {
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

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        if (rhs->u.bitwise_bin.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type, visible,
                                  global, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_bitwise_binary(rhs->u.bitwise_bin.op, rhs->u.bitwise_bin.type, &lhs,
                                   &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        if (rhs->u.bitwise_un.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_bitwise_unary(rhs->u.bitwise_un.type, &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        if (tc_validate_shift_mode(rhs->u.shift.op, rhs->u.shift.type,
                                   rhs->u.shift.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.shift.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.value, rhs->u.shift.type, visible, global,
                                  const_name, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.count, rhs->u.shift.type, visible, global,
                                  const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_shift(rhs->u.shift.op, rhs->u.shift.type, rhs->u.shift.mode, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        if (tc_validate_fp_arith_mode(rhs->u.float_arith.op, rhs->u.float_arith.type,
                                      rhs->u.float_arith.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.float_arith.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.lhs, rhs->u.float_arith.type, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.rhs, rhs->u.float_arith.type, visible,
                                  global, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_arith(rhs->u.float_arith.op, rhs->u.float_arith.type,
                             rhs->u.float_arith.mode,
                             &lhs, &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        if (tc_validate_fp_unary_mode(rhs->u.float_unary.op, rhs->u.float_unary.type,
                                      rhs->u.float_unary.mode, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.float_unary.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_unary.operand, rhs->u.float_unary.type, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_unary(rhs->u.float_unary.op, rhs->u.float_unary.type,
                             rhs->u.float_unary.mode,
                             &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        if (tc_validate_fp_compare_mode(rhs->u.float_compare.type,
                                        rhs->u.float_compare.mode, diag, line) != 0) {
            return -1;
        }
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.lhs, rhs->u.float_compare.type, visible,
                                  global, const_name, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.rhs, rhs->u.float_compare.type, visible,
                                  global, const_name, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_compare(rhs->u.float_compare.op, rhs->u.float_compare.type,
                               rhs->u.float_compare.mode, &lhs, &rhs_val, out, &tmp_diag,
                               line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITCAST) {
        TcBitcastRhs *bitcast = (TcBitcastRhs *)&rhs->u.bitcast;
        TcValue source = {0};
        TcTypeKind source_type = TC_INT32;
        int width = tc_type_bit_width(bitcast->target);

        if (bitcast->target != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant bitcast type mismatch");
            return -1;
        }
        if (bitcast->source.kind == TC_OPERAND_VAR) {
            const TcSymbol *symbol = tc_symbol_table_find(visible, bitcast->source.u.name);
            char msg[128];

            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               bitcast->source.u.name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line,
                                  TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            source_type = symbol->type;
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
        } else if (width == 64) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
        } else if (width == 32) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT32 : TC_INT32;
        } else if (width == 16) {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT16 : TC_INT16;
        } else {
            source_type = bitcast->source.u.lit.unsigned_suffix ? TC_UINT8 : TC_INT8;
        }
        if (tc_type_is_bool(bitcast->target) || tc_type_is_bool(source_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "bool does not participate in bitcast");
            return -1;
        }
        if (tc_type_bit_width(source_type) != width) {
            tc_diagnostic_set(diag, TC_ERR_BITCAST_WIDTH, line, TC_COLUMN_UNKNOWN,
                              "bitcast source and target widths must match");
            return -1;
        }
        bitcast->source_type = source_type;
        bitcast->source_type_resolved = 1;
        if (tc_eval_const_operand(&bitcast->source, source_type, visible,
                                  global, const_name, &source, line,
                                  diag) != 0) {
            return -1;
        }
        return tc_exec_bitcast(bitcast->target, &source, out, diag, line);
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        TcCastRhs *cast = (TcCastRhs *)&rhs->u.const_cast;
        TcValue src_val = {0};
        TcTypeKind source_type = TC_INT64;

        if (cast->target != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant cast target type mismatch");
            return -1;
        }
        if (cast->source.kind == TC_OPERAND_LIT) {
            if (cast->source.u.lit.is_bool) {
                source_type = TC_BOOL;
            } else if (cast->source.u.lit.is_float) {
                source_type = cast->source.u.lit.float32_suffix ? TC_FLOAT32 : TC_FLOAT64;
            } else {
                source_type = cast->source.u.lit.unsigned_suffix ? TC_UINT64 : TC_INT64;
            }
            if (!tc_literal_fits_context(&cast->source.u.lit, source_type, NULL)) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "invalid literal in constant cast");
                return -1;
            }
            src_val = tc_literal_to_value(&cast->source.u.lit, source_type);
        } else if (cast->source.kind == TC_OPERAND_VAR) {
            const TcSymbol *symbol =
                tc_symbol_table_find(visible, cast->source.u.name);
            char msg[128];

            if (const_name && strcmp(cast->source.u.name, const_name) == 0) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               cast->source.u.name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (!symbol) {
                (void)snprintf(msg, sizeof(msg), "undefined variable '%s'",
                               cast->source.u.name);
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
            if (symbol->sym_kind != TC_SYM_CONSTANT) {
                tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line,
                                  TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            if (!symbol->has_const_value) {
                tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                                  "constant value is not available by source order");
                return -1;
            }
            src_val = symbol->const_value;
            source_type = symbol->type;
        } else {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "invalid constant cast source");
            return -1;
        }
        cast->source_type = source_type;
        cast->source_type_resolved = 1;
        tc_diagnostic_init(&tmp_diag);
        if ((cast->mode == TC_TRUNC_TRUNCATE
                 ? tc_exec_truncate(cast->target, &src_val, out, &tmp_diag, line)
                 : tc_exec_cast(cast->target, &src_val, out, &tmp_diag, line)) != 0) {
            if (tmp_diag.kind == TC_ERR_MODE_MISMATCH) {
                tc_diagnostic_set(diag, TC_ERR_MODE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "truncate requires an integer target narrower than the source");
            } else {
                tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            }
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        if (expected_type != TC_STRUCT) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "struct constructor type mismatch in constant expression");
            return -1;
        }
        /* 字段合法性由类型检查保证；编译期 struct 值占位（bits=0），供 let 绑定注册 */
        out->type = TC_STRUCT;
        out->bits = 0;
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

    if (tc_eval_const_rhs(rhs, sym->type, visible, global, sym->name, &value, line, diag) != 0) {
        return -1;
    }
    sym->const_value = value;
    sym->has_const_value = 1;
    return 0;
}
