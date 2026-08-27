/*
 * tc_memblock_check.c — memblock RHS/语句静态验证
 *
 * 下标字面量在编译期做静态越界；变量下标留给运行时。
 * 构造器支持 fill 模式与显式元素列表两种形式。
 */
#include "tc_memblock_check.h"

#include "tc_struct_check.h"

#include <stdlib.h>
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
        tc_diagnostic_set(diag, TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    if (index->u.lit.magnitude >= count) {
        tc_diagnostic_set(diag, TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE, line, TC_COLUMN_UNKNOWN,
                          "memblock index out of range");
        return -1;
    }
    return 0;
}

/**
 * 元素操作数须匹配 element 完整类型。
 * 复合元素走 type 指针比较；标量委托 tc_check_operand。
 */
static int tc_memblock_operand_matches_element(TcOperand *operand, const TcType *element,
                                               const TcSymbolTable *visible,
                                               const TcSymbolTable *global,
                                               const TcStructTable *struct_table,
                                               TcInitHistory *hist, size_t stmt_index, int line,
                                               TcDiagnostic *diag, TcWarningList *warnings,
                                               const char *self_name) {
    if (operand->kind == TC_OPERAND_LIT) {
        return tc_type_check_literal(&operand->u.lit, element, line, diag);
    }
    if (operand->kind == TC_OPERAND_FIELD_READ) {
        return tc_struct_check_field_access(&operand->u.field_read, element, struct_table, visible,
                                            global, hist, stmt_index, line, diag, warnings,
                                            self_name);
    }
    if (element->tag == TC_STRUCT || element->tag == TC_PTR || element->tag == TC_MEMBLOCK) {
        if (operand->kind != TC_OPERAND_VAR) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "operand type does not match memblock element type");
            return -1;
        }
        {
            const TcSymbol *sym =
                tc_memblock_resolve(operand->u.name, visible, global, stmt_index, line, diag);
            if (!sym) {
                return -1;
            }
            if (!tc_type_equals(sym->type, element)) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
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
    return tc_check_operand(operand, element->tag, visible, global, struct_table, hist, stmt_index, line, diag,
                            warnings, self_name, TC_CE_TYPE_MISMATCH);
}

int tc_memblock_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                          const TcSymbolTable *global, const TcStructTable *struct_table,
                          TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                          TcWarningList *warnings, const char *self_name) {
    switch (rhs->kind) {
    case TC_RHS_MEMBLOCK_LOAD: {
        /* memblock_load(T, mb, i) → T */
        const TcType *mb_type = NULL;
        uint64_t mb_count = 0;

        if (!expected) {
            return -1;
        }
        if (rhs->u.memblock_load.memblock.kind != TC_OPERAND_VAR &&
            rhs->u.memblock_load.memblock.kind != TC_OPERAND_FIELD_READ) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock_load requires memblock variable");
            return -1;
        }
        if (rhs->u.memblock_load.memblock.kind == TC_OPERAND_FIELD_READ) {
            if (tc_struct_check_field_access(&rhs->u.memblock_load.memblock.u.field_read, NULL,
                                              struct_table, visible, global, hist, stmt_index,
                                              line, diag, warnings, self_name) != 0) {
                return -1;
            }
            mb_type = rhs->u.memblock_load.memblock.u.field_read.resolved.field_type;
            if (!mb_type || mb_type->tag != TC_MEMBLOCK) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "memblock_load requires memblock variable");
                return -1;
            }
        } else if (tc_check_operand(&rhs->u.memblock_load.memblock, TC_MEMBLOCK, visible, global,
                                    struct_table, hist, stmt_index, line, diag, warnings,
                                    self_name, TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (rhs->u.memblock_load.memblock.kind == TC_OPERAND_VAR) {
            const TcSymbol *mb =
                tc_memblock_resolve(rhs->u.memblock_load.memblock.u.name, visible, global,
                                    stmt_index, line, diag);
            if (!mb) {
                return -1;
            }
            mb_type = mb->type;
        } else if (!mb_type) {
            mb_type = rhs->u.memblock_load.memblock.u.field_read.resolved.field_type;
        }
        if (!tc_type_equals(mb_type->params.memblock_type.element,
                            &rhs->u.memblock_load.element_type)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock_load element type does not match");
            return -1;
        }
        mb_count = tc_type_memblock_count(mb_type);
        if (tc_check_operand(&rhs->u.memblock_load.index, TC_USIZE, visible, global, struct_table, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0 &&
            tc_check_operand(&rhs->u.memblock_load.index, TC_ISIZE, visible, global, struct_table, hist,
                             stmt_index, line, diag, warnings, self_name,
                             TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
        if (tc_memblock_check_index_literal(&rhs->u.memblock_load.index, mb_count, line, diag) !=
            0) {
            return -1;
        }
        if (!tc_type_equals(&rhs->u.memblock_load.element_type, expected)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock_load result type does not match destination");
            return -1;
        }
        return 0;
    }
    case TC_RHS_MEMBLOCK_CONSTRUCTOR: {
        /* memblock(T, N, ...) 或 fill；N≥1，列表长度须等于 N */
        uint64_t count = rhs->u.memblock_ctor.count;
        size_t i = 0;

        if (!expected || expected->tag != TC_MEMBLOCK) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor requires memblock destination");
            return -1;
        }
        if (count < 1) {
            tc_diagnostic_set(diag, TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH, line,
                              TC_COLUMN_UNKNOWN, "memblock count must be at least 1");
            return -1;
        }
        if (!tc_type_equals(&rhs->u.memblock_ctor.element_type,
                            expected->params.memblock_type.element)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor element type does not match");
            return -1;
        }
        if (expected->params.memblock_type.count != 0 &&
            expected->params.memblock_type.count != count) {
            tc_diagnostic_set(diag, TC_CE_MEMBLOCK_SIZE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock constructor count does not match destination size");
            return -1;
        }
        if (rhs->u.memblock_ctor.is_fill) {
            if (tc_memblock_operand_matches_element(&rhs->u.memblock_ctor.fill_value,
                                                    &rhs->u.memblock_ctor.element_type, visible,
                                                    global, struct_table, hist, stmt_index, line,
                                                    diag, warnings, self_name) != 0) {
                return -1;
            }
            return 0;
        }
        if (rhs->u.memblock_ctor.value_count != count) {
            tc_diagnostic_set(diag, TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH, line,
                              TC_COLUMN_UNKNOWN,
                              "memblock constructor value count does not match count");
            return -1;
        }
        for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
            if (tc_memblock_operand_matches_element(&rhs->u.memblock_ctor.values[i],
                                                    &rhs->u.memblock_ctor.element_type, visible,
                                                    global, struct_table, hist, stmt_index, line,
                                                    diag, warnings, self_name) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case TC_RHS_MEMBLOCK_COUNT: {
        const TcSymbol *base_sym =
            tc_memblock_resolve(rhs->u.memblock_count.memblock_name, visible, global, stmt_index,
                                line, diag);

        if (!base_sym) {
            return -1;
        }
        if (tc_type_tag_of(base_sym->type) == TC_STRUCT) {
            char *count_field = strdup("count");
            char **fields = NULL;

            if (!count_field) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            fields = (char **)malloc(sizeof(char *));
            if (!fields) {
                free(count_field);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            fields[0] = count_field;
            /* 注意：memblock_name 与 field_read.base 是同一 union 存储，
             * 转移所有权后不得再对 memblock_name 置 NULL（会清掉 base）。 */
            rhs->kind = TC_RHS_FIELD_READ;
            rhs->u.field_read.base = rhs->u.memblock_count.memblock_name;
            rhs->u.field_read.fields = fields;
            rhs->u.field_read.field_count = 1;
            memset(&rhs->u.field_read.resolved, 0, sizeof(rhs->u.field_read.resolved));
            return tc_struct_check_field_read(rhs, expected, struct_table, visible, global, hist,
                                            stmt_index, line, diag, warnings, self_name);
        }
        if (tc_type_tag_of(base_sym->type) != TC_MEMBLOCK) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock count requires memblock variable");
            return -1;
        }
        tc_resolved_binding_set((TcResolvedBinding *)&rhs->u.memblock_count.binding, base_sym);
        if (expected && expected->tag != TC_USIZE && expected->tag != TC_ISIZE) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock count result must be usize/isize");
            return -1;
        }
        return 0;
    }
    default:
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid memblock rhs kind");
        return -1;
    }
}

int tc_memblock_check_store(const TcMemblockStoreStmt *stmt, const TcSymbolTable *visible,
                            const TcSymbolTable *global, const TcStructTable *struct_table,
                            TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                            TcWarningList *warnings) {
    const TcSymbol *mb = NULL;

    mb = tc_memblock_resolve(stmt->memblock_name, visible, global, stmt_index, stmt->line, diag);
    if (!mb) {
        return -1;
    }
    tc_resolved_binding_set((TcResolvedBinding *)&stmt->binding, mb);
    if (mb->sym_kind == TC_SYM_CONSTANT || mb->sym_kind == TC_SYM_STATIC_LET) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_ASSIGNMENT, stmt->line, TC_COLUMN_UNKNOWN,
                          "cannot store into constant memblock");
        return -1;
    }
    if (tc_type_tag_of(mb->type) != TC_MEMBLOCK) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock_store requires memblock variable");
        return -1;
    }
    if (!tc_type_equals(mb->type->params.memblock_type.element, &stmt->element_type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock_store element type does not match");
        return -1;
    }
    if (tc_check_operand((TcOperand *)&stmt->index, TC_USIZE, visible, global, struct_table, hist,
                         stmt_index, stmt->line, diag, warnings, NULL, TC_CE_TYPE_MISMATCH) != 0 &&
        tc_check_operand((TcOperand *)&stmt->index, TC_ISIZE, visible, global, struct_table, hist,
                         stmt_index, stmt->line, diag, warnings, NULL, TC_CE_TYPE_MISMATCH) != 0) {
        return -1;
    }
    if (tc_memblock_check_index_literal(&stmt->index, tc_type_memblock_count(mb->type),
                                        stmt->line, diag) != 0) {
        return -1;
    }
    return tc_check_operand((TcOperand *)&stmt->value, stmt->element_type.tag, visible, global,
                            struct_table, hist, stmt_index, stmt->line, diag, warnings, NULL,
                            TC_CE_TYPE_MISMATCH);
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
    tc_resolved_binding_set((TcResolvedBinding *)&stmt->dst_binding, dst);
    tc_resolved_binding_set((TcResolvedBinding *)&stmt->src_binding, src);
    if (dst->sym_kind == TC_SYM_CONSTANT || dst->sym_kind == TC_SYM_STATIC_LET) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_ASSIGNMENT, stmt->line, TC_COLUMN_UNKNOWN,
                          "cannot copy into constant memblock");
        return -1;
    }
    if (tc_type_tag_of(dst->type) != TC_MEMBLOCK || tc_type_tag_of(src->type) != TC_MEMBLOCK) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock_copy requires memblock variables");
        return -1;
    }
    if (tc_type_memblock_count(dst->type) != tc_type_memblock_count(src->type)) {
        tc_diagnostic_set(diag, TC_CE_MEMBLOCK_SIZE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memblock copy size mismatch");
        return -1;
    }
    if (!tc_type_equals(dst->type->params.memblock_type.element, &stmt->element_type)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
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
    /* 静态只拒绝 void 元素；负 length 由 VM/AOT 报运行时区间错误 */
    (void)visible;
    (void)global;
    (void)hist;
    (void)stmt_index;
    (void)warnings;
    if (stmt->element_type.tag == TC_VOID) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memcopy_unsafe element type cannot be void");
        return -1;
    }
    return 0;
}
