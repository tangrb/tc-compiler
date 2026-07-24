/*
 * tc_type_check.c — 完整类型 RHS/字面量检查入口（Phase 3）
 *
 * 分派策略：
 *   LIT / CONST_REF → 本文件直接处理（含 nullptr、memblock 尺寸）
 *   指针族 RHS     → tc_ptr_check_rhs
 *   memblock 族    → tc_memblock_check_rhs
 *   struct 构造/读 → tc_struct_check_*
 *   其它标量 RHS   → 既有 tc_check_rhs（期望 kind）
 */
#include "tc_type_check.h"

#include "tc_memblock_check.h"
#include "tc_ptr_check.h"
#include "tc_semantics.h"

#include <stdio.h>
#include <string.h>

int tc_type_check_literal(const TcLiteral *lit, const TcType *expected, int line,
                          TcDiagnostic *diag) {
    TcErrorKind err_kind = TC_ERR_LITERAL_OUT_OF_RANGE;

    if (!expected) {
        return -1;
    }
    /* nullptr 仅可出现在 ptr 期望上下文 */
    if (lit->is_nullptr) {
        if (expected->kind != TC_PTR) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                              "nullptr is only allowed in pointer context");
            return -1;
        }
        return 0;
    }
    if (lit->is_float_special || lit->is_float) {
        if (!tc_type_is_float(expected->kind)) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                              "float literal type does not match context");
            return -1;
        }
        /* f/F 后缀强制 float32 上下文 */
        if (lit->float32_suffix && expected->kind != TC_FLOAT32) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                              "float32 suffix requires float32 context");
            return -1;
        }
        return tc_check_literal(lit, expected->kind, line, diag, TC_ERR_LITERAL_TYPE);
    }
    if (lit->is_bool) {
        if (!tc_type_is_bool(expected->kind)) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                              "bool literal requires bool context");
            return -1;
        }
        return 0;
    }
    if (lit->unsigned_suffix && tc_type_is_signed(expected->kind)) {
        tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, TC_COLUMN_UNKNOWN,
                          "unsigned suffix literal cannot be used in signed context");
        return -1;
    }
    if (!tc_literal_fits_context(lit, expected->kind, &err_kind)) {
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

int tc_type_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                      const TcSymbolTable *global, const TcStructTable *struct_table,
                      TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                      TcWarningList *warnings, const char *self_name) {
    if (!expected) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, TC_COLUMN_UNKNOWN, "missing expected type");
        return -1;
    }

    if (rhs->kind == TC_RHS_LIT) {
        return tc_type_check_literal(&rhs->u.lit, expected, line, diag);
    }

    /* 标识符作为 RHS：解析绑定、完整类型匹配、未初始化检查 */
    if (rhs->kind == TC_RHS_CONST_REF) {
        const TcSymbol *source = NULL;
        char msg[128];

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
        if (expected->kind == TC_MEMBLOCK) {
            /* memblock：元素类型相等，且声明 N 在目标指定时必须一致 */
            if (source->type != TC_MEMBLOCK ||
                !tc_type_equals(&source->full_type, expected)) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "identifier type does not match destination type");
                return -1;
            }
            if (source->memblock_count != expected->params.memblock_type.count &&
                expected->params.memblock_type.count != 0) {
                tc_diagnostic_set(diag, TC_ERR_MEMBLOCK_SIZE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "memblock size mismatch");
                return -1;
            }
        } else if (expected->kind == TC_PTR || expected->kind == TC_STRUCT) {
            if (!tc_type_equals(&source->full_type, expected)) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "identifier type does not match destination type");
                return -1;
            }
        } else if (source->type != expected->kind) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "identifier type does not match destination type");
            return -1;
        }
        if (source->sym_kind == TC_SYM_VARIABLE &&
            tc_check_operand_init(hist, source, stmt_index, line, diag) != 0) {
            return -1;
        }
        tc_resolved_binding_set(&rhs->u.const_ref.binding, source);
        (void)struct_table;
        (void)warnings;
        return 0;
    }

    /* 复合类型专用分派；标量回退 */
    switch (rhs->kind) {
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
        return tc_ptr_check_rhs(rhs, expected, visible, global, hist, stmt_index, line, diag,
                                warnings, self_name);
    case TC_RHS_MEMBLOCK_LOAD:
    case TC_RHS_MEMBLOCK_CONSTRUCTOR:
    case TC_RHS_MEMBLOCK_COUNT:
        return tc_memblock_check_rhs(rhs, expected, visible, global, hist, stmt_index, line, diag,
                                     warnings, self_name);
    case TC_RHS_STRUCT_CONSTRUCTOR:
        return tc_struct_check_constructor(rhs, expected, struct_table, visible, global, hist,
                                           stmt_index, line, diag, warnings, self_name);
    case TC_RHS_FIELD_READ:
        return tc_struct_check_field_read(rhs, expected, struct_table, visible, global, hist,
                                          stmt_index, line, diag, warnings, self_name);
    default:
        if (expected->kind == TC_PTR || expected->kind == TC_MEMBLOCK ||
            expected->kind == TC_STRUCT) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "rhs kind does not match composite destination type");
            return -1;
        }
        return tc_check_rhs(rhs, expected->kind, visible, global, hist, stmt_index, line, diag,
                            warnings, self_name);
    }
}
