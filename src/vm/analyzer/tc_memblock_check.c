/*
 * tc_memblock_check.c — memblock RHS/语句静态验证
 *
 * 下标字面量在编译期做静态越界；变量下标留给运行时。
 * 构造器支持 fill 模式与显式元素列表两种形式。
 */
#include "tc_memblock_check.h"

#include "tc_analyzer_internal.h"
#include "tc_const_eval.h"
#include "tc_ptr_check.h"
#include "tc_semantics.h"   /* tc_bits_to_signed：A-3 编译期负值判定 */
#include "tc_struct_check.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* 前向声明：避免与 tc_type_check.c 循环包含 */
int tc_type_check_literal(const TcLiteral *lit, const TcType *expected, int line,
                          TcDiagnostic *diag);

static const TcSymbol *tc_memblock_resolve(const char *name, const TcSymbolTable *visible,
                                           const TcSymbolTable *global, size_t stmt_index,
                                           int line, TcDiagnostic *diag) {
    return tc_resolve_visible_symbol(visible, global, name, stmt_index, line, diag);
}

/*
 * `usize_operand` 的符号解析（附录 A：`memblock<T, N>` 的 `N` 与构造器
 * `count:` 共用 `usize_operand`）：
 *
 *   usize_operand = integer_literal | identifier | qualified_identifier
 *                 | imported_member_name ;
 *
 * 因此裸 `identifier` 与 `Self.<名>` / `<模块名>.<名>` 均合法，且须解析为
 * 类型是 `usize` 的 `let` / `static let`。
 *
 * `#lib` 的顶层 `static let` 位于**全局**符号表而常不在 `visible` 中（后者
 * 仅承载函数局部绑定），故此处须显式回退到全局表——与表达式操作数不同，
 * 本位置没有「函数内须写 `Self.`」的约束，裸名是规范允许的书写形式。
 * 回退时保留块级作用域上界检查：已退出的块内绑定不得再被引用。
 */
static const TcSymbol *tc_memblock_resolve_usize_operand(const char *name,
                                                         const TcSymbolTable *visible,
                                                         const TcSymbolTable *global,
                                                         size_t stmt_index, int line,
                                                         TcDiagnostic *diag) {
    const TcSymbol *symbol = NULL;
    char msg[128];

    if (!name) {
        return NULL;
    }
    if (strncmp(name, "Self.", 5) == 0 || strchr(name, '.') != NULL) {
        symbol = tc_find_named_binding(visible, global, name);
        if (symbol) {
            return symbol;
        }
        (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
        return NULL;
    }
    if (visible) {
        symbol = tc_symbol_table_find(visible, name);
        if (symbol) {
            return symbol;
        }
    }
    if (global) {
        const TcSymbol *global_sym = tc_symbol_for_assign_target(global, name, (int)stmt_index);

        if (global_sym && global_sym->scope_end_stmt_index < 0) {
            /*
             * 命中模块顶层绑定：函数体内须经 `Self.<名>` 访问（语言标准 §4.3），
             * 此处与表达式操作数同口径报 TC_CE_FUNCTION_SCOPE_ACCESS。
             */
            if (tc_name_scope_check_function_access(name, line, diag)) {
                return NULL;
            }
            return global_sym;
        }
    }
    (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
    tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
    return NULL;
}

static int tc_memblock_resolve_usize_name(const char *name, const TcSymbolTable *visible,
                                          const TcSymbolTable *global, size_t stmt_index, int line,
                                          TcDiagnostic *diag, uint64_t *out_count) {
    const TcSymbol *sym = NULL;
    TcTypeTag tag = TC_VOID;

    if (!name || !out_count) {
        return -1;
    }
    sym = tc_memblock_resolve_usize_operand(name, visible, global, stmt_index, line, diag);
    if (!sym) {
        return -1;
    }
    if (sym->sym_kind != TC_SYM_CONSTANT) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "memblock count must be a compile-time usize constant");
        return -1;
    }
    if (!sym->has_const_value) {
        /* 派生失败：挂起的 CT 类诊断仍是首个规范诊断。 */
        if (tc_const_value_withheld(sym, diag)) {
            return -1;
        }
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                          "constant value is not available by source order");
        return -1;
    }
    tag = tc_type_tag_of(sym->type);
    /*
     * B-5（[语言标准 §3.8.1]/§3.8.3）：`N` / `count:` 只接受**类型为 `usize`**
     * 的 `let` / `static let`。类型不合法（`isize` / `int32` / …）与数学值 < 1
     * 一样，统一报 TC_CE_CONSTANT_EXPRESSION；此前分别错报 TYPE_MISMATCH 与
     * MEMBLOCK_ELEMENT_COUNT_MISMATCH，且额外放行 `isize`。
     */
    if (tag != TC_USIZE) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "memblock count must be a usize constant");
        return -1;
    }
    if (sym->const_value.bits < 1) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "memblock count must be at least 1");
        return -1;
    }
    *out_count = sym->const_value.bits;
    return 0;
}

int tc_memblock_resolve_count_name(const char *name, const TcSymbolTable *visible,
                                   const TcSymbolTable *global, size_t stmt_index, int line,
                                   TcDiagnostic *diag, uint64_t *out_count) {
    return tc_memblock_resolve_usize_name(name, visible, global, stmt_index, line, diag,
                                          out_count);
}

int tc_memblock_resolve_type_counts(TcType *type, const TcSymbolTable *visible,
                                    const TcSymbolTable *global, size_t stmt_index, int line,
                                    TcDiagnostic *diag) {
    uint64_t count = 0;

    if (!type) {
        return 0;
    }
    if (type->tag == TC_PTR) {
        return tc_memblock_resolve_type_counts(type->params.ptr_type.pointee, visible, global,
                                               stmt_index, line, diag);
    }
    if (type->tag != TC_MEMBLOCK) {
        return 0;
    }
    if (tc_memblock_resolve_type_counts(type->params.memblock_type.element, visible, global,
                                        stmt_index, line, diag) != 0) {
        return -1;
    }
    if (!type->params.memblock_type.pending_count_name) {
        return 0;
    }
    if (tc_memblock_resolve_usize_name(type->params.memblock_type.pending_count_name, visible,
                                       global, stmt_index, line, diag, &count) != 0) {
        return -1;
    }
    type->params.memblock_type.count = count;
    free(type->params.memblock_type.pending_count_name);
    type->params.memblock_type.pending_count_name = NULL;
    return 0;
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
        /* §6.7.2.4：index 须为整数类型 operand，宽度/符号性不限（不必等于 T） */
        if (tc_check_integer_operand(&rhs->u.memblock_load.index, visible, global, struct_table,
                                     hist, stmt_index, line, diag, warnings, self_name) != 0) {
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
        if (rhs->u.memblock_ctor.count_name) {
            if (tc_memblock_resolve_usize_name(rhs->u.memblock_ctor.count_name, visible, global,
                                               stmt_index, line, diag, &count) != 0) {
                return -1;
            }
            rhs->u.memblock_ctor.count = count;
            free(rhs->u.memblock_ctor.count_name);
            rhs->u.memblock_ctor.count_name = NULL;
        }
        if (count < 1) {
            /* B-5：`count:` 数学值 < 1 属来源不合法 → TC_CE_CONSTANT_EXPRESSION
             * （ELEMENT_COUNT_MISMATCH 只用于「逐值数量 ≠ count」） */
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line,
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
        /* §3.8.5：`.count` 的结果类型是 usize；isize 不是其等价类型（§5.2.1 严格一致） */
        if (expected && expected->tag != TC_USIZE) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "memblock count result must be usize");
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
    /* §6.7.2.4：index 须为整数类型 operand，宽度/符号性不限（不必等于 T） */
    if (tc_check_integer_operand((TcOperand *)&stmt->index, visible, global, struct_table, hist,
                                 stmt_index, stmt->line, diag, warnings, NULL) != 0) {
        return -1;
    }
    if (tc_memblock_check_index_literal(&stmt->index, tc_type_memblock_count(mb->type),
                                        stmt->line, diag) != 0) {
        return -1;
    }
    /* §6.7.2.2：value 类型须与 T 严格一致（ptr/memblock/struct 比完整类型，不只比 tag） */
    return tc_check_operand_strict((TcOperand *)&stmt->value, &stmt->element_type, visible,
                                   global, struct_table, hist, stmt_index, stmt->line, diag,
                                   warnings, NULL);
}

/**
 * 常量整数操作数取值（§6.7.2.4）：返回 1 = 已确定为非负常量（写入 *out）；
 * 0 = 非常量/非整数字面量；-1 = 已确定为负常量。
 */
static int tc_memblock_const_index_value(const TcOperand *operand, uint64_t *out) {
    if (!operand || operand->kind != TC_OPERAND_LIT) {
        return 0;
    }
    if (operand->u.lit.is_bool || operand->u.lit.is_float || operand->u.lit.is_nullptr) {
        return 0;
    }
    if (operand->u.lit.negative) {
        return -1;
    }
    *out = operand->u.lit.magnitude;
    return 1;
}

int tc_memblock_check_copy(const TcMemblockCopyStmt *stmt, const TcSymbolTable *visible,
                           const TcSymbolTable *global,
                           const TcStructTable *struct_table, TcInitHistory *hist,
                           size_t stmt_index, TcDiagnostic *diag, TcWarningList *warnings) {
    const TcSymbol *dst = NULL;
    const TcSymbol *src = NULL;
    uint64_t dst_count = 0;
    uint64_t src_count = 0;

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

    /*
     * §6.7.2.4：三个整数操作数须为整数类型 operand（宽度/符号性不限，也不必
     * 等于 T）；解析结果写回 binding，供执行期使用。
     */
    if (tc_check_integer_operand((TcOperand *)&stmt->dst_index, visible, global, struct_table,
                                 hist, stmt_index, stmt->line, diag, warnings, NULL) != 0 ||
        tc_check_integer_operand((TcOperand *)&stmt->src_index, visible, global, struct_table,
                                 hist, stmt_index, stmt->line, diag, warnings, NULL) != 0 ||
        tc_check_integer_operand((TcOperand *)&stmt->length, visible, global, struct_table, hist,
                                 stmt_index, stmt->line, diag, warnings, NULL) != 0) {
        return -1;
    }

    /*
     * B-21：count 为编译期常量，故常量下标/length 的区间在编译期完整判定——
     * 负常量下标、常量 length < 0、以及越界区间（空拷贝允许下标等于 count）
     * 一律报静态 TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE，不得留给运行时或静默回绕。
     */
    {
        uint64_t dst_index_v = 0;
        uint64_t src_index_v = 0;
        uint64_t length_v = 0;
        int dst_k = tc_memblock_const_index_value(&stmt->dst_index, &dst_index_v);
        int src_k = tc_memblock_const_index_value(&stmt->src_index, &src_index_v);
        int len_k = tc_memblock_const_index_value(&stmt->length, &length_v);

        dst_count = tc_type_memblock_count(dst->type);
        src_count = tc_type_memblock_count(src->type);

        if (dst_k < 0 || src_k < 0 || len_k < 0) {
            tc_diagnostic_set(diag, TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE, stmt->line,
                              TC_COLUMN_UNKNOWN, "memblock index out of range");
            return -1;
        }
        if (len_k == 1 && length_v > 0) {
            if ((dst_k == 1 && (length_v > dst_count || dst_index_v > dst_count - length_v)) ||
                (src_k == 1 && (length_v > src_count || src_index_v > src_count - length_v))) {
                tc_diagnostic_set(diag, TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE, stmt->line,
                                  TC_COLUMN_UNKNOWN, "memblock index out of range");
                return -1;
            }
        } else {
            /* length 为 0 或不可确定：下标大于 count 必然越界（等于 count 仅空拷贝可） */
            if ((dst_k == 1 && dst_index_v > dst_count) ||
                (src_k == 1 && src_index_v > src_count)) {
                tc_diagnostic_set(diag, TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE, stmt->line,
                                  TC_COLUMN_UNKNOWN, "memblock index out of range");
                return -1;
            }
        }
    }
    return 0;
}

/*
 * A-3（标准 owner 裁决）：`memcopy_unsafe` 的 `length` / `dst_idx` / `src_idx` 是否在
 * **编译期可确定**为负。
 *
 * 可确定来源：① 带负号的整数字面量（`tc_memblock_const_index_value` 返回 -1）；
 * ② 解析到 `let` / `static let` 常量绑定且其常量的有符号数学值为负。运行时绑定
 *（`var` / 形参 / `static var`）不在此判定，仍由运行时 `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`
 * 负责（[语言标准 §6.8.9] 的区间合法性 `length ≥ 0 ∧ dst_idx ≥ 0 ∧ src_idx ≥ 0`）。
 */
static int tc_memcopy_operand_const_negative(const TcOperand *operand) {
    uint64_t value = 0;
    int kind = 0;

    if (!operand) {
        return 0;
    }
    kind = tc_memblock_const_index_value(operand, &value);
    if (kind != 0) {
        return kind < 0;
    }
    if (operand->kind == TC_OPERAND_VAR && operand->binding.resolved &&
        operand->binding.is_const && operand->binding.type &&
        tc_type_is_signed(operand->binding.type->tag)) {
        return tc_bits_to_signed(operand->binding.type->tag, operand->binding.const_bits) < 0;
    }
    return 0;
}

int tc_memblock_check_memcopy_unsafe(const TcMemcopyUnsafeStmt *stmt,
                                     const TcSymbolTable *visible,
                                     const TcSymbolTable *global,
                                     const struct TcStructTable *struct_table,
                                     TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                                     TcWarningList *warnings) {
    /* 静态拒绝 void 元素；所指为形参/let 时拒绝写入。负 length 由运行时检查。 */
    if (stmt->element_type.tag == TC_VOID) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, stmt->line, TC_COLUMN_UNKNOWN,
                          "memcopy_unsafe element type cannot be void");
        return -1;
    }
    if (tc_ptr_operand_target_readonly(&stmt->dst_ptr, visible, global, stmt_index)) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_ASSIGNMENT, stmt->line, TC_COLUMN_UNKNOWN,
                          "cannot store through read-only pointer binding");
        return -1;
    }
    /* `dst` / `src` 须为 `ptr<T>` 类型的 operand（§6.8.9、§6.8.10）；字段读取在此解析。 */
    if (tc_ptr_check_memcopy_unsafe_operands(stmt, visible, global, struct_table, hist,
                                             stmt_index, diag, warnings) != 0) {
        return -1;
    }
    /*
     * A-3：编译期可确定的负 `length` / 负下标按静态语义拒绝（SEM）。此前只在运行时
     * 报告，负字面量下标会被当作巨大 usize 处理（Major 3 回归的根因面）。
     */
    if (tc_memcopy_operand_const_negative(&stmt->length) ||
        tc_memcopy_operand_const_negative(&stmt->dst_index) ||
        tc_memcopy_operand_const_negative(&stmt->src_index)) {
        tc_diagnostic_set(diag, TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE, stmt->line,
                          TC_COLUMN_UNKNOWN, "memcopy_unsafe invalid range");
        return -1;
    }
    return 0;
}
