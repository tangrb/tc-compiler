/*
 * tc_memblock_check.c — memblock RHS/语句验证（Phase 3）
 *
 * 下标字面量在编译期做静态越界；变量下标留给运行时。
 * 构造器支持 fill 模式与显式元素列表两种形式。
 */
#include "tc_memblock_check.h"

#include <stdio.h>
#include <string.h>

/* 前向声明：避免与 tc_type_check.c 循环包含 */
int tc_type_check_literal(const TcLiteral *lit, const TcType *expected, int line,
                          TcDiagnostic *diag);

static const TcSymbol *tc_memblock_resolve(const char *name, const TcSymbolTable *visible,
                                           const TcSymbolTable *global, size_t stmt_index,
                                           int line, TcDiagnostic *diag) {
    return tc_resolve_visible_symbol(visible, global, name, stmt_index, line, diag);
}

/**
 * 编译期下标越界：仅当 index 为非负整数字面量时检查 magnitude < count。
 * 变量下标返回 0，交由运行时。
 */
static int tc_memblock_check_index_literal(const TcOperand *index, uint64_t count, int line,
                                           TcDiagnostic *diag) {
    if (index->kind != TC_OPERAND_LIT || index->u.lit.is_bool || index->u.lit.is_float) {
        return 0;
    }
    if (index->u.lit.negative) {
        tc_diagnostic_set(diag, TC_ERR_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    if (index->u.lit.magnitude >= count) {
        tc_diagnostic_set(diag, TC_ERR_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    return 0;
}

/**
 * 元素操作数须匹配 element 完整类型。
 * 复合元素走 full_type 比较；标量委托 tc_check_operand。
 */
static int tc_memblock_operand_matches_element(TcOperand *operand, const TcType *element,
                                               const TcSymbolTable *visible,
                                               const TcSymbolTable *global, TcInitHistory *hist,
                                               size_t stmt_index, int line, TcDiagnostic *diag,
                                               TcWarningList *warnings, const char *self_name) {
    if (operand->kind == TC_OPERAND_LIT) {
        return tc_type_check_literal(&operand->u.lit, element, line, diag);
    }
    if (element->kind == TC_STRUCT || element->kind == TC_PTR || element->kind == TC_MEMBLOCK) {
        if (operand->kind != TC_OPERAND_VAR) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "operand type does not match memblock element type");
            return -1;
        }
        {
            const TcSymbol *sym =
                tc_memblock_resolve(operand->u.name, visible, global, stmt_index, line, diag);
            if (!sym) {
                return -1;
            }
            if (!tc_type_equals(&sym->full_type, element)) {
                tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "operand type does not match memblock element type");
                return -1;
            }
            if (tc_check_operand_init(hist, sym, stmt_index, line, diag) != 0) {
                return -1;
            }
            tc_resolved_binding_set(&operand->binding, sym);
            return 0;
        }
    }
    return tc_check_operand(operand, element->kind, visible, global, hist, stmt_index, line, diag,
                            warnings, self_name, TC_ERR_TYPE_MISMATCH);
}

int tc_memblock_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                          const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                          int line, TcDiagnostic *diag, TcWarningList *warnings,
                          const char *self_name) {
    switch (rhs->kind) {
    case TC_RHS_MEMBLOCK_LOAD: {
        /* memblock_load(T, mb, i) → T */
        const TcSymbol *mb = NULL;

        if (!expected) {
            return -1;
        }
        if (rhs->u.memblock_load.memblock.kind != TC_OPERAND_VAR) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock_load requires memblock variable");
            return -1;
        }
        mb = tc_memblock_resolve(rhs->u.memblock_load.memblock.u.name, visible, global,
                                 stmt_index, line, diag);
        if (!mb) {
            return -1;
        }
        if (mb->type != TC_MEMBLOCK) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock_load requires memblock variable");
            return -1;
        }
        if (!tc_type_equals(mb->full_type.params.memblock_type.element,
                            &rhs->u.memblock_load.element_type)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock_load element type does not match");
            return -1;
        }
        if (tc_check_operand(&rhs->u.memblock_load.memblock, TC_MEMBLOCK, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_check_operand(&rhs->u.memblock_load.index, TC_USIZE, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0 &&
            tc_check_operand(&rhs->u.memblock_load.index, TC_ISIZE, visible, global, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_ERR_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_memblock_check_index_literal(&rhs->u.memblock_load.index, mb->memblock_count, line,
                                            diag) != 0) {
            return -1;
        }
        if (!tc_type_equals(&rhs->u.memblock_load.element_type, expected)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock_load result type does not match destination");
            return -1;
        }
        return 0;
    }
    case TC_RHS_MEMBLOCK_CONSTRUCTOR: {
        /* memblock(T, N, ...) 或 fill；N≥1，列表长度须等于 N */
        uint64_t count = rhs->u.memblock_ctor.count;
        size_t i = 0;

        if (!expected || expected->kind != TC_MEMBLOCK) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor requires memblock destination");
            return -1;
        }
        if (count < 1) {
            tc_diagnostic_set(diag, TC_ERR_MEMBLOCK_ELEMENT_COUNT_MISMATCH, line,
                              TC_COLUMN_UNKNOWN, "memblock count must be at least 1");
            return -1;
        }
        if (!tc_type_equals(&rhs->u.memblock_ctor.element_type,
                            expected->params.memblock_type.element)) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor element type does not match");
            return -1;
        }
        if (expected->params.memblock_type.count != 0 &&
            expected->params.memblock_type.count != count) {
            tc_diagnostic_set(diag, TC_ERR_MEMBLOCK_SIZE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor count does not match destination size");
            return -1;
        }
        if (rhs->u.memblock_ctor.is_fill) {
            if (tc_memblock_operand_matches_element(&rhs->u.memblock_ctor.fill_value,
                                                    &rhs->u.memblock_ctor.element_type, visible,
                                                    global, hist, stmt_index, line, diag, warnings,
                                                    self_name) != 0) {
                return -1;
            }
            return 0;
        }
        if (rhs->u.memblock_ctor.value_count != count) {
            tc_diagnostic_set(diag, TC_ERR_MEMBLOCK_ELEMENT_COUNT_MISMATCH, line,
                              TC_COLUMN_UNKNOWN,
                              "memblock constructor value count does not match count");
            return -1;
        }
        for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
            if (tc_memblock_operand_matches_element(&rhs->u.memblock_ctor.values[i],
                                                    &rhs->u.memblock_ctor.element_type, visible,
                                                    global, hist, stmt_index, line, diag, warnings,
                                                    self_name) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case TC_RHS_MEMBLOCK_COUNT: {
        /* mb.count → usize/isize（编译期可知声明 N，此处只验类型） */
        const TcSymbol *mb = NULL;

        mb = tc_memblock_resolve(rhs->u.memblock_count.memblock_name, visible, global, stmt_index,
                                 line, diag);
        if (!mb) {
            return -1;
        }
        if (mb->type != TC_MEMBLOCK) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock count requires memblock variable");
            return -1;
        }
        if (expected && expected->kind != TC_USIZE && expected->kind != TC_ISIZE) {
            tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock count result must be usize/isize");
            return -1;
        }
        return 0;
    }
    default:
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid memblock rhs kind");
        return -1;
    }
}

int tc_memblock_check_store(const TcMemblockStoreStmt *stmt, const TcSymbolTable *visible,
                            const TcSymbolTable *global, TcInitHistory *hist,
                            size_t stmt_index, TcDiagnostic *diag, TcWarningList *warnings) {
    const TcSymbol *mb = NULL;

    mb = tc_memblock_resolve(stmt->memblock_name, visible, global, stmt_index, stmt->line, diag);
    if (!mb) {
        return -1;
    }
    if (mb->sym_kind == TC_SYM_CONSTANT || mb->sym_kind == TC_SYM_STATIC_LET) {
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, stmt->line, TC_COLUMN_UNKNOWN,
                          "cannot store into constant memblock");
        return -1;
    }
    if (mb->type != TC_MEMBLOCK) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock_store requires memblock variable");
        return -1;
    }
    if (!tc_type_equals(mb->full_type.params.memblock_type.element, &stmt->element_type)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock_store element type does not match");
        return -1;
    }
    if (tc_check_operand((TcOperand *)&stmt->index, TC_USIZE, visible, global, hist, stmt_index,
                         stmt->line, diag, warnings, NULL, TC_ERR_TYPE_MISMATCH) != 0 &&
        tc_check_operand((TcOperand *)&stmt->index, TC_ISIZE, visible, global, hist, stmt_index,
                         stmt->line, diag, warnings, NULL, TC_ERR_TYPE_MISMATCH) != 0) {
        return -1;
    }
    if (tc_memblock_check_index_literal(&stmt->index, mb->memblock_count, stmt->line, diag) !=
        0) {
        return -1;
    }
    return tc_check_operand((TcOperand *)&stmt->value, stmt->element_type.kind, visible, global,
                            hist, stmt_index, stmt->line, diag, warnings, NULL,
                            TC_ERR_TYPE_MISMATCH);
}

int tc_memblock_check_copy(const TcMemblockCopyStmt *stmt, const TcSymbolTable *visible,
                           const TcSymbolTable *global, TcInitHistory *hist,
                           size_t stmt_index, TcDiagnostic *diag, TcWarningList *warnings) {
    const TcSymbol *dst = NULL;
    const TcSymbol *src = NULL;

    /* 整块拷贝要求两端声明长度 N 相同（元素类型由语句注解约束） */
    dst = tc_memblock_resolve(stmt->dst_name, visible, global, stmt_index, stmt->line, diag);
    if (!dst) {
        return -1;
    }
    src = tc_memblock_resolve(stmt->src_name, visible, global, stmt_index, stmt->line, diag);
    if (!src) {
        return -1;
    }
    if (dst->sym_kind == TC_SYM_CONSTANT || dst->sym_kind == TC_SYM_STATIC_LET) {
        tc_diagnostic_set(diag, TC_ERR_CONSTANT_ASSIGNMENT, stmt->line, TC_COLUMN_UNKNOWN,
                          "cannot copy into constant memblock");
        return -1;
    }
    if (dst->type != TC_MEMBLOCK || src->type != TC_MEMBLOCK) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock_copy requires memblock variables");
        return -1;
    }
    if (dst->memblock_count != src->memblock_count) {
        tc_diagnostic_set(diag, TC_ERR_MEMBLOCK_SIZE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock copy size mismatch");
        return -1;
    }
    if (!tc_type_equals(dst->full_type.params.memblock_type.element,
                        &stmt->element_type)) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock_copy element type does not match");
        return -1;
    }
    (void)hist;
    (void)warnings;
    (void)stmt_index;
    return 0;
}

int tc_memblock_check_memcopy_unsafe(const TcMemcopyUnsafeStmt *stmt,
                                     const TcSymbolTable *visible,
                                     const TcSymbolTable *global, TcInitHistory *hist,
                                     size_t stmt_index, TcDiagnostic *diag,
                                     TcWarningList *warnings) {
    /* Phase 3 最小校验：元素不可为 void；区间检查留给后续阶段 */
    (void)visible;
    (void)global;
    (void)hist;
    (void)stmt_index;
    (void)warnings;
    if (stmt->element_type.kind == TC_VOID) {
        tc_diagnostic_set(diag, TC_ERR_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memcopy_unsafe element type cannot be void");
        return -1;
    }
    return 0;
}
