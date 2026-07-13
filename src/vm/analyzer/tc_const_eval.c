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

/*
 * visiting 栈：在编译期求值一个 let 常量 RHS 时，递归地将当前常量名推入栈；
 * 若递归到某个操作数时发现其名称已在栈中，则可判定循环依赖
 * （如 let a = b + 1; let b = a + 1;），立刻报 TC_ERR_CONSTANT_CIRCULAR。
 * 栈深度上限 TC_CONST_VISIT_MAX（64），超过则报表达式过于复杂。
 */
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
    default:
        return -1;
    }
}

/*
 * 编译期 cast 合法性检查：仅允许扩宽 cast（dst_bits >= src_bits）。
 * 窄化 cast（如 int32 → int16）在编译期不被允许，因为可能丢失位模式信息；
 * 运行时通过 tc_exec_cast + TC_TRUNC_STRICT 做严格范围检查，
 * 或者 TC_TRUNC_TRUNCATE 模式显式按位截断。
 * bool↔整数转换允许任意方向（bool 按 0/1 映射到整数，反之亦然）。
 */
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

/*
 * 前向声明，因为 tc_eval_const_rhs 与 tc_eval_const_operand 互相递归：
 *   tc_eval_const_rhs(ARITH) → tc_eval_const_operand(lhs) → 可能调用 tc_resolve_const_value → tc_eval_const_rhs
 *   tc_eval_const_operand(CONST_REF) → 取 const_value（已求值直接返回，无递归）
 *
 * 入口为 tc_resolve_const_value，携带 visiting 栈检测循环依赖。
 */
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

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        if (rhs->u.bitwise_bin.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type, visible,
                                  global, const_name, visiting, visiting_count, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type, visible,
                                  global, const_name, visiting, visiting_count, &rhs_val, line,
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
                                  global, const_name, visiting, visiting_count, &lhs, line,
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
        if (rhs->u.shift.mode == TC_ARITH_WRAP) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "wrap is not allowed in constant expression");
            return -1;
        }
        if (rhs->u.shift.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.value, rhs->u.shift.type, visible, global,
                                  const_name, visiting, visiting_count, &lhs, line, diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.shift.count, rhs->u.shift.type, visible, global,
                                  const_name, visiting, visiting_count, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_shift(rhs->u.shift.op, rhs->u.shift.type, TC_ARITH_STRICT, &lhs, &rhs_val,
                          out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        if (rhs->u.float_arith.mode != TC_FLOAT_STRICT) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "ieee/wrap is not allowed in constant expression");
            return -1;
        }
        if (rhs->u.float_arith.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.lhs, rhs->u.float_arith.type, visible,
                                  global, const_name, visiting, visiting_count, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_arith.rhs, rhs->u.float_arith.type, visible,
                                  global, const_name, visiting, visiting_count, &rhs_val, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_arith(rhs->u.float_arith.op, rhs->u.float_arith.type, TC_FLOAT_STRICT,
                             &lhs, &rhs_val, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        if (rhs->u.float_unary.mode != TC_FLOAT_STRICT) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "ieee/wrap is not allowed in constant expression");
            return -1;
        }
        if (rhs->u.float_unary.type != expected_type) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_unary.operand, rhs->u.float_unary.type, visible,
                                  global, const_name, visiting, visiting_count, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        tc_diagnostic_init(&tmp_diag);
        if (tc_exec_fp_unary(rhs->u.float_unary.op, rhs->u.float_unary.type, TC_FLOAT_STRICT,
                             &lhs, out, &tmp_diag, line) != 0) {
            tc_const_map_runtime_error(tmp_diag.kind, diag, line);
            tc_diagnostic_clear(&tmp_diag);
            return -1;
        }
        tc_diagnostic_clear(&tmp_diag);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        if (rhs->u.float_compare.mode != TC_FLOAT_STRICT &&
            rhs->u.float_compare.mode != TC_FLOAT_IEEE) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "wrap is not allowed in constant expression");
            return -1;
        }
        if (!tc_type_is_bool(expected_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "constant expression type mismatch");
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.lhs, rhs->u.float_compare.type, visible,
                                  global, const_name, visiting, visiting_count, &lhs, line,
                                  diag) != 0) {
            return -1;
        }
        if (tc_eval_const_operand(&rhs->u.float_compare.rhs, rhs->u.float_compare.type, visible,
                                  global, const_name, visiting, visiting_count, &rhs_val, line,
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

    if (rhs->kind == TC_RHS_FLOAT_CAST) {
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "float cast is not allowed in constant expression");
        return -1;
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
