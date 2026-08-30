/*
 * tc_ptr_check.c — 指针 RHS/语句静态验证
 *
 * 临时构造 ptr<T> 时可能堆分配 pointee；各分支在返回前释放，
 * 避免把临时类型泄漏进 AST（AST 自带 pointee_type 字段）。
 */
#include "tc_ptr_check.h"

#include "tc_struct_check.h"

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

    /* 总是深拷贝 pointee（含嵌套 ptr/memblock/struct 参数），返回独立可释放的
     * ptr<pointee>；嵌套指针（ptr<ptr<T>>）不再短路共享 AST 节点（§3.10）。 */
    heap_pointee = (TcType *)malloc(sizeof(TcType));
    if (!heap_pointee) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        memset(&ptr, 0, sizeof(ptr));
        ptr.tag = TC_VOID;
        return ptr;
    }
    if (tc_type_copy(pointee, heap_pointee, diag) != 0) {
        free(heap_pointee);
        memset(&ptr, 0, sizeof(ptr));
        ptr.tag = TC_VOID;
        return ptr;
    }
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
                                const TcStructTable *struct_table, TcInitHistory *hist,
                                size_t stmt_index, int line, TcDiagnostic *diag,
                                TcWarningList *warnings, const char *self_name) {
    const TcSymbol *sym = NULL;

    if (operand->kind == TC_OPERAND_LIT) {
        if (operand->u.lit.is_nullptr) {
            if (!expected_ptr || expected_ptr->tag != TC_PTR) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "nullptr requires pointer context");
                return -1;
            }
            return 0;
        }
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "pointer operand must be variable or nullptr");
        return -1;
    }

    if (operand->kind == TC_OPERAND_FIELD_READ) {
        if (!expected_ptr || expected_ptr->tag != TC_PTR) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer operand type does not match");
            return -1;
        }
        return tc_struct_check_field_access(&operand->u.field_read, expected_ptr, struct_table,
                                            visible, global, hist, stmt_index, line, diag,
                                            warnings, self_name);
    }

    if (self_name && operand->u.name && strcmp(operand->u.name, self_name) == 0) {
        char msg[128];
        (void)snprintf(msg, sizeof(msg),
                       "variable '%s' cannot reference itself in its initializer", self_name);
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
        return -1;
    }
    sym = tc_ptr_resolve_var(operand->u.name, visible, global, stmt_index, line, diag);
    if (!sym) {
        return -1;
    }
    if (!tc_type_equals(sym->type, expected_ptr)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
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

/** 偏移量：严格 usize（§3.10.8：ptr_add/ptr_sub 偏移须为 usize）。 */
static int tc_ptr_check_usize_operand(TcOperand *operand, const TcSymbolTable *visible,
                                      const TcSymbolTable *global,
                                      const TcStructTable *struct_table, TcInitHistory *hist,
                                      size_t stmt_index, int line, TcDiagnostic *diag,
                                      TcWarningList *warnings, const char *self_name) {
    return tc_check_operand(operand, TC_USIZE, visible, global, struct_table, hist, stmt_index,
                            line, diag, warnings, self_name, TC_CE_TYPE_MISMATCH);
}

int tc_ptr_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                     const TcSymbolTable *global, const TcStructTable *struct_table,
                     TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                     TcWarningList *warnings, const char *self_name) {
    TcType ptr_ty;
    const TcType *pointee = NULL;

    switch (rhs->kind) {
    case TC_RHS_PTR_LOAD:
        /* ptr_load(T, p) → T；p 须为 ptr<T> */
        pointee = &rhs->u.ptr_load.pointee_type;
        ptr_ty = tc_ptr_make_from_pointee(pointee, diag, line);
        if (ptr_ty.tag == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_load.ptr, &ptr_ty, visible, global, struct_table, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && !tc_type_equals(pointee, expected)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_load result type does not match destination");
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        /* 所指为 memblock 时：N 规划个数必须与接收类型一致，
         * tc_type_equals 忽略 N，需在此补充检查 */
        if (expected && tc_type_memblock_count_mismatch(pointee, expected)) {
            tc_diagnostic_set(diag, TC_CE_MEMBLOCK_SIZE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock size mismatch in ptr_load result");
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
            tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;

    case TC_RHS_PTR_ADDRESS: {
        /* ptr_address(T, name) → ptr<T>；禁止对 let/static let 取址 */
        const TcSymbol *target = NULL;

        if (rhs->u.ptr_address.pointee_type.tag == TC_VOID) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_address pointee type cannot be void");
            return -1;
        }
        target = tc_ptr_resolve_var(rhs->u.ptr_address.name, visible, global, stmt_index, line,
                                    diag);
        if (!target) {
            return -1;
        }
        if (target->sym_kind == TC_SYM_CONSTANT || target->sym_kind == TC_SYM_STATIC_LET) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_ASSIGNMENT, line, TC_COLUMN_UNKNOWN,
                              "cannot take address of constant binding");
            return -1;
        }
        if (!tc_type_equals(target->type, &rhs->u.ptr_address.pointee_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_address pointee type does not match variable type");
            return -1;
        }
        ptr_ty = tc_ptr_make_from_pointee(&rhs->u.ptr_address.pointee_type, diag, line);
        if (ptr_ty.tag == TC_VOID) {
            return -1;
        }
        if (expected && !tc_type_equals(&ptr_ty, expected)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_address result type does not match destination");
            /* 嵌套指针 pointee 时 tc_ptr_make_from_pointee 返回浅拷贝（共享
             * AST pointee 节点，见 tc_ptr_make_from_pointee），不得释放；
             * 与 ptr_load/arith/compare/size/store 分支的守卫一致。 */
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
            tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;
    }

    case TC_RHS_PTR_ADD:
    case TC_RHS_PTR_SUB:
        /* ptr_add/sub(T, p, off) → ptr<T>；off 严格为 usize（§3.10.8） */
        pointee = &rhs->u.ptr_arith.pointee_type;
        ptr_ty = tc_ptr_make_from_pointee(pointee, diag, line);
        if (ptr_ty.tag == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_arith.ptr, &ptr_ty, visible, global, struct_table, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (tc_ptr_check_usize_operand(&rhs->u.ptr_arith.offset, visible, global, struct_table, hist,
                                       stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && !tc_type_equals(&ptr_ty, expected)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer arithmetic result type does not match destination");
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
            tc_type_free(ptr_ty.params.ptr_type.pointee);
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
        if (ptr_ty.tag == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_compare.lhs, &ptr_ty, visible, global, struct_table, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0 ||
            tc_ptr_check_operand(&rhs->u.ptr_compare.rhs, &ptr_ty, visible, global, struct_table, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && !tc_type_is_bool(expected->tag)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "pointer comparison result must be bool");
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
            tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;

    case TC_RHS_PTR_SIZE:
        /* ptr_size(T, p) → usize/isize */
        pointee = &rhs->u.ptr_size.pointee_type;
        ptr_ty = tc_ptr_make_from_pointee(pointee, diag, line);
        if (ptr_ty.tag == TC_VOID) {
            return -1;
        }
        if (tc_ptr_check_operand(&rhs->u.ptr_size.ptr, &ptr_ty, visible, global, struct_table, hist,
                                 stmt_index, line, diag, warnings, self_name) != 0) {
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (expected && expected->tag != TC_USIZE && expected->tag != TC_ISIZE) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "ptr_size result must be usize/isize");
            if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (ptr_ty.tag == TC_PTR && ptr_ty.params.ptr_type.pointee) {
            tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
        }
        return 0;

    default:
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid pointer rhs kind");
        return -1;
    }
}

static int tc_ptr_sym_is_param(const TcSymbol *sym) {
    return sym && (sym->sym_kind == TC_SYM_PARAMETER || sym->slot_domain == TC_SLOT_PARAM);
}

static const TcSymbol *tc_ptr_lookup_optional(const char *name, const TcSymbolTable *visible,
                                               const TcSymbolTable *global) {
    const TcSymbol *sym = NULL;

    if (!name) {
        return NULL;
    }
    if (visible) {
        sym = tc_symbol_table_find(visible, name);
        if (sym) {
            return sym;
        }
    }
    if (global) {
        return tc_symbol_table_find(global, name);
    }
    return NULL;
}

int tc_ptr_operand_target_readonly(const TcOperand *operand, const TcSymbolTable *visible,
                                   const TcSymbolTable *global, size_t stmt_index) {
    const TcSymbol *sym = NULL;

    (void)stmt_index;
    if (!operand || operand->kind != TC_OPERAND_VAR || !operand->u.name) {
        return 0;
    }
    sym = tc_ptr_lookup_optional(operand->u.name, visible, global);
    if (!sym || !sym->type || sym->type->tag != TC_PTR) {
        return 0;
    }
    if (sym->sym_kind == TC_SYM_CONSTANT || sym->sym_kind == TC_SYM_STATIC_LET) {
        return 1;
    }
    return sym->ptr_target_readonly ? 1 : 0;
}

int tc_ptr_rhs_target_readonly(const TcRhs *rhs, const TcSymbolTable *visible,
                               const TcSymbolTable *global, size_t stmt_index) {
    const TcSymbol *target = NULL;

    (void)stmt_index;
    if (!rhs) {
        return 0;
    }
    switch (rhs->kind) {
    case TC_RHS_PTR_ADDRESS:
        target = tc_ptr_lookup_optional(rhs->u.ptr_address.name, visible, global);
        if (!target) {
            return 0;
        }
        return (tc_ptr_sym_is_param(target) || target->sym_kind == TC_SYM_CONSTANT ||
                target->sym_kind == TC_SYM_STATIC_LET)
                   ? 1
                   : 0;
    case TC_RHS_CONST_REF:
        target = tc_ptr_lookup_optional(rhs->u.const_ref.name, visible, global);
        if (!target || !target->type || target->type->tag != TC_PTR) {
            return 0;
        }
        if (target->sym_kind == TC_SYM_CONSTANT || target->sym_kind == TC_SYM_STATIC_LET) {
            return 1;
        }
        return target->ptr_target_readonly ? 1 : 0;
    case TC_RHS_PTR_ADD:
    case TC_RHS_PTR_SUB:
        return tc_ptr_operand_target_readonly(&rhs->u.ptr_arith.ptr, visible, global, stmt_index);
    default:
        return 0;
    }
}

int tc_ptr_check_store(const TcPtrStoreStmt *stmt, const TcSymbolTable *visible,
                       const TcSymbolTable *global, const TcStructTable *struct_table,
                       TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                       TcWarningList *warnings) {
    TcType ptr_ty;

    /* ptr_store(T, p, v)：经只读绑定（let/形参）间接写入一律拒绝 */
    if (stmt->pointee_type.tag == TC_VOID) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "ptr_store pointee type cannot be void");
        return -1;
    }
    ptr_ty = tc_ptr_make_from_pointee(&stmt->pointee_type, diag, stmt->line);
    if (ptr_ty.tag == TC_VOID) {
        return -1;
    }
    if (stmt->ptr.kind == TC_OPERAND_VAR) {
        const TcSymbol *holder = tc_ptr_resolve_var(stmt->ptr.u.name, visible, global,
                                                     stmt_index, stmt->line, diag);
        if (!holder) {
            if (ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
        if (holder->sym_kind == TC_SYM_CONSTANT || holder->sym_kind == TC_SYM_STATIC_LET ||
            holder->ptr_target_readonly) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_ASSIGNMENT, stmt->line, TC_COLUMN_UNKNOWN,
                              "cannot store through read-only pointer binding");
            if (ptr_ty.params.ptr_type.pointee) {
                tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
            }
            return -1;
        }
    }
    if (tc_ptr_check_operand((TcOperand *)&stmt->ptr, &ptr_ty, visible, global, struct_table, hist,
                             stmt_index, stmt->line, diag, warnings, NULL) != 0) {
        if (ptr_ty.params.ptr_type.pointee) {
            tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
        }
        return -1;
    }
    if (tc_check_operand((TcOperand *)&stmt->value, stmt->pointee_type.tag, visible, global,
                         struct_table, hist, stmt_index, stmt->line, diag, warnings, NULL,
                         TC_CE_TYPE_MISMATCH) != 0) {
        if (ptr_ty.params.ptr_type.pointee) {
            tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
        }
        return -1;
    }
    if (ptr_ty.params.ptr_type.pointee) {
        tc_type_free(ptr_ty.params.ptr_type.pointee);
                free(ptr_ty.params.ptr_type.pointee);
    }
    return 0;
}
