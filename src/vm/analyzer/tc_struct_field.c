/*
 * tc_struct_field.c — 结构体字段读/写、基址分类与 offset 固化
 */
#include "tc_analyzer_pass2_rhs.h"
#include "tc_struct_field.h"
#include "tc_type_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 若基址 `X.y` 的前缀 `X` 能解析为可见绑定，则把 `y` 移回字段链首位。
 * 解析器会把 `<模块>.<成员>.<字段>` 的前两段收成基址；附录 A 无大小写规则，
 * 大写变量（`A.i`）同样合法。限定名基址（前缀不是绑定）保持不变。
 *
 * @return 1 已重分类；0 无需改动；-1 内存不足
 */
static int tc_field_split_variable_base(char **base, char ***fields, size_t *field_count,
                                        const TcSymbolTable *visible,
                                        const TcSymbolTable *global,
                                        const TcMemberIndex *members) {
    const char *dot = NULL;
    const TcSymbol *sym = NULL;
    char *prefix = NULL;
    char *rest = NULL;
    char **grown = NULL;
    char *old_base = NULL;
    size_t i = 0;

    if (!base || !*base || !fields || !field_count) {
        return 0;
    }
    if (strncmp(*base, "Self.", 5) == 0) {
        return 0;
    }
    dot = strchr(*base, '.');
    if (!dot || dot == *base || dot[1] == '\0' || strchr(dot + 1, '.') != NULL) {
        return 0;
    }
    prefix = (char *)malloc((size_t)(dot - *base) + 1U);
    if (!prefix) {
        return -1;
    }
    memcpy(prefix, *base, (size_t)(dot - *base));
    prefix[dot - *base] = '\0';
    sym = tc_find_named_binding(visible, global, prefix, members);
    if (!sym) {
        free(prefix);
        return 0;
    }
    rest = strdup(dot + 1);
    if (!rest) {
        free(prefix);
        return -1;
    }
    grown = (char **)realloc(*fields, (*field_count + 1U) * sizeof(char *));
    if (!grown) {
        free(prefix);
        free(rest);
        return -1;
    }
    for (i = *field_count; i > 0; i--) {
        grown[i] = grown[i - 1];
    }
    grown[0] = rest;
    *fields = grown;
    *field_count += 1U;
    old_base = *base;
    *base = prefix;
    free(old_base);
    return 1;
}

static const TcSymbol *tc_struct_resolve_base(const char *base, const TcSymbolTable *visible,
                                              const TcSymbolTable *global, size_t stmt_index,
                                              int line, TcDiagnostic *diag,
                                              const TcMemberIndex *members, int in_function) {
    const char *member = NULL;
    char msg[128];

    if (strncmp(base, "Self.", 5) == 0) {
        member = base + 5;
        /*
         * `Self.<名>` 只解析本模块顶层成员（§4.3、§4.4）。统一走名称解析，由其按
         * 当前模块的成员索引判定归属，避免命中其它模块（含 private）的同名成员。
         */
        {
            const TcSymbol *source = tc_resolve_self_member(member, global, members);

            if (source && source->slot_domain == TC_SLOT_STATIC) {
                return source;
            }
        }
        (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", member);
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
        return NULL;
    }

    {
        const char *dot = strchr(base, '.');

        if (dot && dot != base && dot[1] != '\0') {
            /*
             * `<模块名>.<成员>` 作字段链基址（解析器只把「首字母大写 + 两点」形态归入
             * 基址，故此处只可能是模块限定或 `Self.` 成员）。统一走名称解析，以便
             * 施加 §4.4 的两条约束：所属模块须等于限定前缀、成员不得为 private。
             */
            member = dot + 1;
            if (tc_reject_private_member_access(base, global, line, diag)) {
                return NULL;
            }
            {
                const TcSymbol *source = tc_find_named_binding(visible, global, base, members);

                if (source) {
                    return source;
                }
            }
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", member);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
            return NULL;
        }
    }

    return tc_resolve_visible_symbol(visible, global, base, stmt_index, line, diag, members,
                                     in_function);
}

void tc_resolved_field_access_free(TcResolvedFieldAccess *access) {
    if (!access) {
        return;
    }
    free(access->offsets);
    access->offsets = NULL;
    access->field_count = 0;
    access->resolved = 0;
}

static void tc_field_access_free_parse_strings(TcFieldAccess *access) {
    if (!access) {
        return;
    }
    free(access->base);
    access->base = NULL;
    if (access->fields) {
        size_t i = 0;
        for (i = 0; i < access->field_count; i++) {
            free(access->fields[i]);
        }
        free(access->fields);
        access->fields = NULL;
    }
    access->field_count = 0;
}

static int tc_struct_finalize_field_access(TcFieldAccess *access, const TcSymbol *base_sym,
                                           const TcType *final_type, uint32_t *offsets,
                                           size_t field_count, int is_memblock_count) {
    int is_const_base = (base_sym->sym_kind == TC_SYM_CONSTANT ||
                         base_sym->sym_kind == TC_SYM_STATIC_LET);

    access->resolved.resolved = 1;
    /* let / static let 基址槽为 -1（与 tc_resolved_binding_set 一致）；运行时基址保留 slot */
    access->resolved.base_slot = is_const_base ? -1 : base_sym->slot;
    access->resolved.const_bits =
        base_sym->has_const_value ? base_sym->const_value.bits : 0ULL;
    access->resolved.field_type = final_type;
    access->resolved.offsets = offsets;
    access->resolved.field_count = field_count;
    access->resolved.is_memblock_count = is_memblock_count;
    if (is_const_base && !base_sym->has_const_value) {
        /* static let 拓扑求值前保留 base 名，供 const_eval 延迟取 const_bits */
        if (access->fields) {
            size_t i = 0;
            for (i = 0; i < access->field_count; i++) {
                free(access->fields[i]);
            }
            free(access->fields);
            access->fields = NULL;
        }
        access->field_count = 0;
    } else {
        tc_field_access_free_parse_strings(access);
    }
    return 0;
}

static int tc_struct_compute_field_offsets(const TcStructTable *table, int struct_id,
                                           char *const *fields, size_t field_count,
                                           const TcType **out_final_type, uint32_t **out_offsets,
                                           int *out_is_memblock_count, TcDiagnostic *diag,
                                           int line) {
    uint32_t *offsets = NULL;
    size_t bit_off = 0;
    size_t i = 0;
    int cursor_sid = struct_id;
    const TcType *field_type = NULL;

    *out_is_memblock_count = 0;
    offsets = (uint32_t *)calloc(field_count, sizeof(uint32_t));
    if (!offsets) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }

    for (i = 0; i < field_count; i++) {
        const TcStructEntry *entry = tc_struct_table_get(table, cursor_sid);
        size_t j = 0;
        const TcStructField *field = NULL;
        size_t before = 0;

        if (!entry) {
            free(offsets);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN,
                              "undefined struct type for field path");
            return -1;
        }
        for (j = 0; j < entry->field_count; j++) {
            if (strcmp(entry->fields[j].name, fields[i]) == 0) {
                field = &entry->fields[j];
                break;
            }
            before += tc_struct_field_width_bits(&entry->fields[j], table);
        }
        if (!field) {
            char fmsg[128];
            free(offsets);
            (void)snprintf(fmsg, sizeof(fmsg), "unknown struct field '%s'", fields[i]);
            tc_diagnostic_set(diag, TC_CE_STRUCT_UNKNOWN_FIELD, line, TC_COLUMN_UNKNOWN, fmsg);
            return -1;
        }
        bit_off += before;
        offsets[i] = (uint32_t)(bit_off / 8U);
        field_type = &field->type;
        if (i + 1 < field_count) {
            if (field->type.tag != TC_STRUCT) {
                free(offsets);
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "field path requires struct base");
                return -1;
            }
            cursor_sid = field->type.params.struct_type.struct_id;
        }
    }

    *out_final_type = field_type;
    *out_offsets = offsets;
    return 0;
}

int tc_struct_check_field_access(TcFieldAccess *access, const TcType *expected,
                                 const TcStructTable *table, const TcSymbolTable *visible,
                                 const TcSymbolTable *global, TcInitHistory *hist,
                                 size_t stmt_index, int line, TcDiagnostic *diag,
                                 TcWarningList *warnings, const char *self_name) {
    const TcSymbol *base_sym = NULL;
    const TcType *cursor_type = NULL;
    int is_memblock_count = 0;
    uint32_t *offsets = NULL;
    const TcType *final_type = NULL;
    size_t i = 0;

    (void)warnings;
    if (!access || !access->base || access->field_count == 0) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid struct field read");
        return -1;
    }

    if (self_name && strcmp(access->base, self_name) == 0) {
        char msg[128];
        (void)snprintf(msg, sizeof(msg),
                       "variable '%s' cannot reference itself in its initializer", self_name);
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
        return -1;
    }

    /* 先按名称解析纠正基址分类（首字母大写的变量同样是合法基址） */
    if (tc_field_split_variable_base(&access->base, &access->fields, &access->field_count,
                                     visible, global, tc_hist_name_members(hist)) < 0) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    /*
     * `<模块名>.<成员>` 作普通 RHS 操作数的**整体读取**。
     *
     * 解析器只在 `X.y.`（两点）形态下把 `X.y` 归入基址，故单点限定名
     * `ImpLib.K` 到达这里是「基址 `ImpLib` + 字段链 `["K"]`」。若基址不是可见
     * 绑定、而 `"<基址>.<首个字段>"` 经名称解析命中限定成员（附录 A 的 `operand`
     * 含 `imported_member_name`；[语言标准 §4.3、§6.1.2] 允许经导入限定解析到
     * 公开 `static let` / `static var`），则该操作数就是该**绑定自身**：按无字段
     * 的绑定读取定型（`resolved.field_count = 0`），执行器 / AOT / 常量求值按
     * 绑定读取处理（与 `read(int32, <模块>.<名>)` 的表示一致）。
     */
    if (strchr(access->base, '.') == NULL &&
        tc_find_named_binding(visible, global, access->base, tc_hist_name_members(hist)) == NULL) {
        size_t need = strlen(access->base) + 1U + strlen(access->fields[0]) + 1U;
        char *combined = (char *)malloc(need);
        const TcSymbol *qualified = NULL;

        if (!combined) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        snprintf(combined, need, "%s.%s", access->base, access->fields[0]);
        /*
         * [语言标准 §4.4]：`<模块名>.<private 成员>` 报专用码（须先于按名解析，否则
         * 只会得到笼统的 undefined variable）。
         */
        if (tc_reject_private_member_access(combined, global, line, diag)) {
            free(combined);
            return -1;
        }
        qualified = tc_find_named_binding(visible, global, combined, tc_hist_name_members(hist));
        free(combined);
        /*
         * 限定名必须真的是「该模块的成员」：解析到的符号的所属模块须与限定前缀一致
         * （符号模块标记，已由 tc_find_named_binding 统一过滤）。否则
         * `NoSuchLib.K` 会因按裸成员名回退而静默命中任一可见 `K`。
         */
        if (qualified) {
            if (expected && !tc_type_equals(qualified->type, expected)) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "field read result type does not match expected type");
                return -1;
            }
            if (qualified->sym_kind != TC_SYM_CONSTANT &&
                qualified->sym_kind != TC_SYM_STATIC_LET &&
                tc_check_operand_init(hist, qualified, stmt_index, line, diag) != 0) {
                return -1;
            }
            return tc_struct_finalize_field_access(access, qualified, qualified->type, NULL, 0, 0);
        }
    }

    base_sym = tc_struct_resolve_base(access->base, visible, global, stmt_index, line, diag,
                                      tc_hist_name_members(hist), tc_hist_name_in_function(hist));
    if (!base_sym) {
        return -1;
    }

    if (access->field_count == 1 && strcmp(access->fields[0], "count") == 0) {
        if (tc_type_tag_of(base_sym->type) == TC_MEMBLOCK) {
            is_memblock_count = 1;
            final_type = tc_type_tag_singleton(TC_USIZE);
            /* §3.8.5：`.count` 的结果类型是 usize；isize 不是其等价类型 */
            if (expected && expected->tag != TC_USIZE) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "memblock count result must be usize");
                return -1;
            }
            if (base_sym->sym_kind != TC_SYM_CONSTANT && base_sym->sym_kind != TC_SYM_STATIC_LET) {
                if (tc_check_operand_init(hist, base_sym, stmt_index, line, diag) != 0) {
                    return -1;
                }
            }
            if (tc_struct_finalize_field_access(access, base_sym, final_type, NULL, 0,
                                                is_memblock_count) != 0) {
                return -1;
            }
            access->resolved.const_bits = tc_type_memblock_count(base_sym->type);
            return 0;
        }
    }

    if (tc_type_tag_of(base_sym->type) != TC_STRUCT) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "field read requires struct base");
        return -1;
    }

    cursor_type = base_sym->type;
    for (i = 0; i < access->field_count; i++) {
        const TcStructEntry *cur_def = NULL;
        const TcStructField *field = NULL;

        if (cursor_type->tag != TC_STRUCT) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "field read requires struct base");
            return -1;
        }
        cur_def = (cursor_type->params.struct_type.struct_id >= 0 &&
                   (size_t)cursor_type->params.struct_type.struct_id < table->count)
                      ? &table->items[(size_t)cursor_type->params.struct_type.struct_id]
                      : NULL;
        if (!cur_def) {
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN,
                              "undefined struct type for field read");
            return -1;
        }
        field = tc_struct_find_field(cur_def, access->fields[i]);
        if (!field) {
            char fmsg[128];
            (void)snprintf(fmsg, sizeof(fmsg), "unknown struct field '%s'", access->fields[i]);
            tc_diagnostic_set(diag, TC_CE_STRUCT_UNKNOWN_FIELD, line, TC_COLUMN_UNKNOWN, fmsg);
            return -1;
        }
        cursor_type = &field->type;
    }
    final_type = cursor_type;

    if (expected && !tc_type_equals(final_type, expected)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "field read result type does not match expected type");
        return -1;
    }
    if (expected && tc_type_memblock_count_mismatch(final_type, expected)) {
        tc_diagnostic_set(diag, TC_CE_MEMBLOCK_SIZE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "memblock size mismatch in field read result");
        return -1;
    }

    if (base_sym->sym_kind != TC_SYM_CONSTANT && base_sym->sym_kind != TC_SYM_STATIC_LET) {
        if (tc_check_operand_init(hist, base_sym, stmt_index, line, diag) != 0) {
            return -1;
        }
    }

    if (tc_struct_compute_field_offsets(table, tc_type_struct_id(base_sym->type), access->fields,
                                        access->field_count, &final_type, &offsets,
                                        &is_memblock_count, diag, line) != 0) {
        return -1;
    }

    return tc_struct_finalize_field_access(access, base_sym, final_type, offsets,
                                           access->field_count, is_memblock_count);
}

int tc_struct_check_field_read(TcRhs *rhs, const TcType *expected,
                               const TcStructTable *table, const TcSymbolTable *visible,
                               const TcSymbolTable *global, TcInitHistory *hist,
                               size_t stmt_index, int line, TcDiagnostic *diag,
                               TcWarningList *warnings, const char *self_name) {
    TcFieldAccess access;

    if (rhs->kind != TC_RHS_FIELD_READ) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid struct field read");
        return -1;
    }
    /* static let 求值可能已提前固化（字段名已释放） */
    if (rhs->u.field_read.resolved.resolved) {
        if (expected && rhs->u.field_read.resolved.field_type &&
            !tc_type_equals(rhs->u.field_read.resolved.field_type, expected)) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                              "field read result type does not match expected type");
            return -1;
        }
        return 0;
    }
    if (rhs->u.field_read.field_count == 0) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid struct field read");
        return -1;
    }
    memset(&access, 0, sizeof(access));
    access.base = rhs->u.field_read.base;
    access.fields = rhs->u.field_read.fields;
    access.field_count = rhs->u.field_read.field_count;
    access.resolved = rhs->u.field_read.resolved;
    if (tc_struct_check_field_access(&access, expected, table, visible, global, hist, stmt_index,
                                     line, diag, warnings, self_name) != 0) {
        return -1;
    }
    rhs->u.field_read.resolved = access.resolved;
    rhs->u.field_read.base = access.base;
    rhs->u.field_read.fields = access.fields;
    rhs->u.field_read.field_count = access.field_count;
    return 0;
}

int tc_struct_check_field_assign(TcFieldAssign *assign, TcAnalyzeCtx *ctx,
                                 const TcStructTable *table,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                                 TcWarningList *warnings) {
    const TcSymbol *base_sym = NULL;
    const TcType *cursor_type = NULL;
    const TcStructField *field = NULL;
    size_t i = 0;
    char msg[128];

    /* 与字段读同口径，先按名称解析纠正基址分类 */
    if (tc_field_split_variable_base(&assign->base, &assign->fields, &assign->field_count,
                                     visible, global, tc_hist_name_members(hist)) < 0) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, assign->line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }

    /* 基对象不可为 let；路径上每个字段须为 var（is_var）；RHS 匹配末字段类型 */
    base_sym = tc_struct_resolve_base(assign->base, visible, global, stmt_index, assign->line,
                                      diag, tc_hist_name_members(hist),
                                      tc_hist_name_in_function(hist));
    if (!base_sym) {
        return -1;
    }
    if (base_sym->sym_kind == TC_SYM_CONSTANT || base_sym->sym_kind == TC_SYM_STATIC_LET) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_ASSIGNMENT, assign->line, TC_COLUMN_UNKNOWN,
                          "cannot assign to constant binding");
        return -1;
    }
    if (base_sym->sym_kind == TC_SYM_PARAMETER ||
        base_sym->slot_domain == TC_SLOT_PARAM) {
        tc_diagnostic_set(diag, TC_CE_PARAMETER_ASSIGNMENT, assign->line, TC_COLUMN_UNKNOWN,
                          "cannot assign to function parameter");
        return -1;
    }
    if (tc_type_tag_of(base_sym->type) != TC_STRUCT) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, assign->line, TC_COLUMN_UNKNOWN,
                          "field assignment requires struct base");
        return -1;
    }
    /* 固化基绑定：执行期与 AOT 直接取槽（基址可为 `Self.<名>` / 导入限定名）。 */
    tc_resolved_binding_set(&assign->base_binding, base_sym);
    cursor_type = base_sym->type;
    for (i = 0; i < assign->field_count; i++) {
        const TcStructEntry *cur_def = NULL;

        if (cursor_type->tag != TC_STRUCT) {
            tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, assign->line, TC_COLUMN_UNKNOWN,
                              "field assignment requires struct base");
            return -1;
        }
        cur_def = (cursor_type->params.struct_type.struct_id >= 0 &&
                   (size_t)cursor_type->params.struct_type.struct_id < table->count)
                      ? &table->items[(size_t)cursor_type->params.struct_type.struct_id]
                      : NULL;
        if (!cur_def) {
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, assign->line, TC_COLUMN_UNKNOWN,
                              "undefined struct type for field assignment");
            return -1;
        }
        field = tc_struct_find_field(cur_def, assign->fields[i]);
        if (!field) {
            (void)snprintf(msg, sizeof(msg), "unknown struct field '%s'", assign->fields[i]);
            tc_diagnostic_set(diag, TC_CE_STRUCT_UNKNOWN_FIELD, assign->line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        if (!field->is_var) {
            (void)snprintf(msg, sizeof(msg), "cannot assign to immutable struct field '%s'",
                           field->name);
            tc_diagnostic_set(diag, TC_CE_STRUCT_IMMUTABLE_FIELD, assign->line,
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        cursor_type = &field->type;
    }
    /*
     * 字段赋值的 RHS 为 funcall 时走专用检查（与整绑定赋值一致），
     * 否则 tc_type_check_rhs 不认 TC_RHS_FUNCALL_EXPR。
     */
    if (assign->rhs.kind == TC_RHS_FUNCALL_EXPR && ctx && ctx->func_env) {
        return tc_pass2_check_funcall_rhs(&assign->rhs, field ? &field->type : base_sym->type, 1,
                                          ctx, visible, global, hist, stmt_index, assign->line,
                                          warnings, diag);
    }
    return tc_type_check_rhs((TcRhs *)&assign->rhs, field ? &field->type : base_sym->type,
                             visible,
                             global, table, hist, stmt_index, assign->line, diag, warnings, NULL);
}
