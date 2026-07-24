/*
 * tc_ptr_check.c — 指针 RHS/语句验证（Phase 3）
 *
 * 临时构造 ptr<T> 时可能堆分配 pointee；各分支在返回前释放，
 * 避免把临时类型泄漏进 AST（AST 自带 pointee_type 字段）。
 */
#include "tc_ptr_check.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * 由 pointee 构造临时 ptr 类型。
 * 若 pointee 本身已是 PTR，直接按值返回（不额外分配）；
 * 否则堆拷贝 pointee 后 make_ptr。失败时 kind=VOID 且已写 diag。
 */
static TcType tc_ptr_make_from_pointee(const TcType *pointee, TcDiagnostic *diag, int line) {
    TcType *heap_pointee = NULL;
    TcType ptr;

    if (pointee->kind == TC_PTR) {
        return *pointee;
    }
    heap_pointee = (TcType *)malloc(sizeof(TcType));
    if (!heap_pointee) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        memset(&ptr, 0, sizeof(ptr));
        ptr.kind = TC_VOID;
        return ptr;
    }
    *heap_pointee = *pointee;
    return tc_type_make_ptr(heap_pointee);
}

static const TcSymbol *tc_ptr_resolve_var(const char *name, const TcSymbolTable *visible,
                                            const TcSymbolTable *global, size_t stmt_index,
                                            int line, TcDiagnostic *diag) {
    return tc_resolve_visible_symbol(visible, global, name, stmt_index, line, diag);
}

/**
 * 指针操作数：允许 nullptr，或类型完全匹配的已初始化变量。
 * 禁止初始化式中的自引用（self_name）。
 */
static int tc_ptr_check_operand(TcOperand *operand, const TcType *expected_ptr,
                                const TcSymbolTable *visible, const TcSymbolTable *global,
                                TcInitHistory *hist, size_t stmt_index, int line,
                                TcDiagnostic *diag, TcWarningList *warnings,
                                const char *self_name) {
    const TcSymbol *sym = NULL;

    if (operand->kind == TC_OPERAND_LIT) {
        if (operand->u.lit.is_nullptr) {
            if (!expected_ptr || expected_ptr->kind != TC_PTR) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "nullptr requires pointer context");
                return -1;
            }
            return 0;
        }
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "pointer operand must be variable or nullptr");
        return -1;
    }

    if (self_name && strcmp(operand->u.name, self_name) == 0) {
        char msg[128];
        (void)snprintf(msg, sizeof(msg),
                       "variable '%s' cannot reference itself in its initializer", self_name);
        tc_diagnostic_set(diag, TC_ERR_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
        return -1;
    }
    sym = tc_ptr_resolve_var(operand->u.name, visible, global, stmt_index, line, diag);
    if (!sym) {
        return -1;
    }
    if (!tc_type_equals(&sym->full_type, expected_ptr)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "pointer operand type does not match");
        return -1;
    }
    if (tc_check_operand_init(hist, sym, stmt_index, line, diag) != 0) {
        return -1;
    }
    tc_resolved_binding_set(&operand->binding, sym);
    (void)warnings;
    return 0;
}

/** 偏移量：优先 usize，失败再试 isize（指针算术索引约定）。 */
static int tc_ptr_check_usize_operand(TcOperand *operand, const TcSymbolTable *visible,
                                      const TcSymbolTable *global, TcInitHistory *hist,
                                      size_t stmt_index, int line, TcDiagnostic *diag,
                                      TcWarningList *warnings, const char *self_name) {
    if (tc_check_operand(operand, TC_USIZE, visible, global, hist, stmt_index, line, diag,
                         warnings, self_name, TC_ERR_TYPE_MISMATCH) == 0) {
        return 0;
    }
    return tc_check_operand(operand, TC_ISIZE, visible, global, hist, stmt_index, line, diag,
                            warnings, self_name, TC_ERR_TYPE_MISMATCH);
}

int tc_ptr_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                     const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                     int line, TcDiagnostic *diag, TcWarningList *warnings,
                     const char *self_name) {
    TcType ptr_ty;
    const TcType *pointee = NULL;

    switch (rhs->kind) {
    case TC_RHS_PTR_LOAD:
        /* ptr_load(T, p) → T；p 须为 ptr<T> */
        pointee = &rhs->u.ptr_load.pointee_type;
        ptr_ty = tc_ptr_make_from_pointee(pointee, diag, line);
        if (ptr_ty.kind == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_load.ptr, &ptr_ty, visible, global, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && !tc_type_equals(pointee, expected)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_load result type does not match destination");
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee && pointee->kind != TC_PTR) {
            free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;

    case TC_RHS_PTR_ADDRESS: {
        /* ptr_address(T, name) → ptr<T>；禁止对 let/static let 取址 */
        const TcSymbol *target = NULL;

        if (rhs->u.ptr_address.pointee_type.kind == TC_VOID) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_address pointee type cannot be void");
            return -1;
        }
        target = tc_ptr_resolve_var(rhs->u.ptr_address.name, visible, global, stmt_index, line,
                                    diag);
        if (!target) {
            return -1;
        }
        if (target->sym_kind == TC_SYM_CONSTANT || target->sym_kind == TC_SYM_STATIC_LET) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, line, TC_COLUMN_UNKNOWN,
                              "cannot take address of constant binding");
            return -1;
        }
        if (!tc_type_equals(&target->full_type, &rhs->u.ptr_address.pointee_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_address pointee type does not match variable type");
            return -1;
        }
        ptr_ty = tc_ptr_make_from_pointee(&rhs->u.ptr_address.pointee_type, diag, line);
        if (ptr_ty.kind == TC_VOID) {
            return -1;
        }
        if (expected && !tc_type_equals(&ptr_ty, expected)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_address result type does not match destination");
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee) {
            free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;
    }

    case TC_RHS_PTR_ADD:
    case TC_RHS_PTR_SUB:
        /* ptr_add/sub(T, p, off) → ptr<T>；off 为 usize/isize */
        pointee = &rhs->u.ptr_arith.pointee_type;
        ptr_ty = tc_ptr_make_from_pointee(pointee, diag, line);
        if (ptr_ty.kind == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_arith.ptr, &ptr_ty, visible, global, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (tc_ptr_check_usize_operand(&rhs->u.ptr_arith.offset, visible, global, hist,
                                       stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && !tc_type_equals(&ptr_ty, expected)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer arithmetic result type does not match destination");
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee && pointee->kind != TC_PTR) {
            free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;

    case TC_RHS_PTR_EQ:
    case TC_RHS_PTR_NE:
    case TC_RHS_PTR_LT:
    case TC_RHS_PTR_LE:
    case TC_RHS_PTR_GT:
    case TC_RHS_PTR_GE:
        /* 同 pointee 类型的两指针比较 → bool */
        pointee = &rhs->u.ptr_compare.pointee_type;
        ptr_ty = tc_ptr_make_from_pointee(pointee, diag, line);
        if (ptr_ty.kind == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_compare.lhs, &ptr_ty, visible, global, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0 ||
            tc_ptr_check_operand(&rhs->u.ptr_compare.rhs, &ptr_ty, visible, global, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && !tc_type_is_bool(expected->kind)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer comparison result must be bool");
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee && pointee->kind != TC_PTR) {
            free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;

    case TC_RHS_PTR_SIZE:
        /* ptr_size(T, p) → usize/isize */
        pointee = &rhs->u.ptr_size.pointee_type;
        ptr_ty = tc_ptr_make_from_pointee(pointee, diag, line);
        if (ptr_ty.kind == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_size.ptr, &ptr_ty, visible, global, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && expected->kind != TC_USIZE && expected->kind != TC_ISIZE) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_size result must be usize/isize");
            if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee &&
                pointee->kind != TC_PTR) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.kind == TC_PTR && ptr_ty.params.ptr_type.pointee && pointee->kind != TC_PTR) {
            free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;

    default:
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid pointer rhs kind");
        return -1;
    }
}

int tc_ptr_check_store(const TcPtrStoreStmt *stmt, const TcSymbolTable *visible,
                       const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                       TcDiagnostic *diag, TcWarningList *warnings) {
    TcType ptr_ty;

    /* ptr_store(T, p, v)：经只读绑定（let/形参）间接写入一律拒绝 */
    if (stmt->pointee_type.kind == TC_VOID) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "ptr_store pointee type cannot be void");
        return -1;
    }
    ptr_ty = tc_ptr_make_from_pointee(&stmt->pointee_type, diag, stmt->line);
    if (ptr_ty.kind == TC_VOID) {
        return -1;
    }
    if (stmt->ptr.kind == TC_OPERAND_VAR) {
        const TcSymbol *holder = tc_ptr_resolve_var(stmt->ptr.u.name, visible, global,
                                                     stmt_index, stmt->line, diag);
        if (!holder) {
            if (ptr_ty.params.ptr_type.pointee) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (holder->sym_kind == TC_SYM_CONSTANT || holder->sym_kind == TC_SYM_STATIC_LET ||
            holder->sym_kind == TC_SYM_PARAMETER) {
            tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, stmt->line, TC_COLUMN_UNKNOWN,
                              "cannot store through read-only pointer binding");
            if (ptr_ty.params.ptr_type.pointee) {
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
    }
    if (tc_ptr_check_operand((TcOperand *)&stmt->ptr, &ptr_ty, visible, global, hist,
                             stmt_index, stmt->line, diag, warnings, NULL) != 0) {
        if (ptr_ty.params.ptr_type.pointee) {
            free(ptr_ty.params.ptr_type.pointee);
        }
        return -1;
    }
    if (tc_check_operand((TcOperand *)&stmt->value, stmt->pointee_type.kind, visible, global,
                         hist, stmt_index, stmt->line, diag, warnings, NULL,
                         TC_ERR_TYPE_MISMATCH) != 0) {
        if (ptr_ty.params.ptr_type.pointee) {
            free(ptr_ty.params.ptr_type.pointee);
        }
        return -1;
    }
    if (ptr_ty.params.ptr_type.pointee) {
        free(ptr_ty.params.ptr_type.pointee);
    }
    return 0;
}
