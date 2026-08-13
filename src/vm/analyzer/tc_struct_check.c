/*
 * tc_struct_check.c — 结构体定义表与构造/字段访问验证（Phase 3）
 *
 * 注册顺序：先入表并校验字段（禁止同类型自引用 / 前向嵌套）→
 * 第二遍把嵌套 struct 名解析为 struct_id 并累加 width_bits →
 * 最后把程序中其它声明上的 struct 名解析掉。
 */
#include "tc_struct_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 前向声明：避免与 tc_type_check.c 循环包含 */
int tc_type_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                      const TcSymbolTable *global, const TcStructTable *struct_table,
                      TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                      TcWarningList *warnings, const char *self_name);

static int tc_struct_entry_push(TcStructTable *table, const TcStructEntry *entry,
                                TcDiagnostic *diag, int line) {
    if (table->count == table->capacity) {
        size_t new_cap = table->capacity == 0 ? 4 : table->capacity * 2;
        TcStructEntry *new_items =
            (TcStructEntry *)realloc(table->items, new_cap * sizeof(TcStructEntry));
        if (!new_items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        table->items = new_items;
        table->capacity = new_cap;
    }
    table->items[table->count] = *entry;
    table->count++;
    return 0;
}

/** 单字段位宽 = sizeof(type) + 8×@padding(N 字节)；嵌套 struct 查表 width_bits。 */
static size_t tc_struct_field_width_bits(const TcStructField *field,
                                         const TcStructTable *table) {
    size_t field_bits = 0;

    if (field->type.tag == TC_STRUCT) {
        int sid = field->type.params.struct_type.struct_id;
        if (sid >= 0 && (size_t)sid < table->count) {
            field_bits = table->items[(size_t)sid].width_bits;
        }
    } else {
        field_bits = tc_sizeof_bits(&field->type);
    }
    return field_bits + (size_t)field->padding * 8U;
}

/**
 * 字段类型合法性：禁 void；禁字段类型等于当前正在定义的 struct；
 * 嵌套 struct 必须已存在且 struct_id < self_id（仅允许「更早」定义）。
 */
static int tc_struct_validate_field_type(const char *struct_name, const TcStructField *field,
                                         int self_id, const TcStructTable *table, int line,
                                         TcDiagnostic *diag) {
    char msg[160];

    if (field->type.tag == TC_VOID) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "struct field type cannot be void");
        return -1;
    }
    if (field->struct_type_name &&
        strcmp(field->struct_type_name, struct_name) == 0) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "struct field cannot have the same type as enclosing struct");
        return -1;
    }
    /*
     * 解析期嵌套 struct 常为 make_struct(-1) + struct_type_name；
     * sid<0 时交给下方按名查找，勿在此误报 undefined。
     */
    if (field->type.tag == TC_STRUCT) {
        int sid = field->type.params.struct_type.struct_id;
        if (sid >= 0 && sid >= self_id) {
            (void)snprintf(msg, sizeof(msg), "undefined struct type in field '%s'",
                           field->name ? field->name : "");
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (sid < 0 && !field->struct_type_name) {
            (void)snprintf(msg, sizeof(msg), "undefined struct type in field '%s'",
                           field->name ? field->name : "");
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }
    if (field->struct_type_name) {
        const TcStructEntry *nested = tc_struct_table_find(table, field->struct_type_name);
        if (!nested) {
            (void)snprintf(msg, sizeof(msg), "undefined struct '%s'", field->struct_type_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (nested->struct_id >= self_id) {
            (void)snprintf(msg, sizeof(msg),
                           "struct field type '%s' must refer to an earlier struct definition",
                           field->struct_type_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }
    return 0;
}

/** 将声明上的未解析 struct 名替换为 make_struct(struct_id)，并释放名字字符串。 */
static int tc_struct_resolve_type(TcType *type, char **struct_type_name, const TcStructTable *table,
                                  int line, TcDiagnostic *diag) {
    char msg[128];

    if (!struct_type_name || !*struct_type_name) {
        return 0;
    }
    {
        const TcStructEntry *entry = tc_struct_table_find(table, *struct_type_name);
        if (!entry) {
            (void)snprintf(msg, sizeof(msg), "undefined struct '%s'", *struct_type_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        tc_type_free(type);
        *type = tc_type_make_struct(entry->struct_id);
        free(*struct_type_name);
        *struct_type_name = NULL;
    }
    return 0;
}

/** 解析单条语句（及嵌套块 / 函数体）上的 struct 类型名。 */
static int tc_struct_resolve_stmt_types(TcStatement *stmt, TcStructTable *table,
                                        TcDiagnostic *diag) {
    size_t i = 0;

    if (!stmt) {
        return 0;
    }
    if (stmt->kind == TC_STMT_VAR_DEF) {
        TcVarDef *def = &stmt->u.var_def;
        if (tc_struct_resolve_type(&def->full_type, &def->struct_type_name, table, def->line,
                                   diag) != 0) {
            return -1;
        }
        return 0;
    }
    if (stmt->kind == TC_STMT_CONST_DEF) {
        TcConstDef *def = &stmt->u.const_def;
        if (tc_struct_resolve_type(&def->full_type, &def->struct_type_name, table, def->line,
                                   diag) != 0) {
            return -1;
        }
        return 0;
    }
    if (stmt->kind == TC_STMT_STATIC_VAR_DEF) {
        TcStaticVarDef *def = &stmt->u.static_var_def;
        return tc_struct_resolve_type(&def->type, &def->struct_type_name, table, def->line, diag);
    }
    if (stmt->kind == TC_STMT_STATIC_LET_DEF) {
        TcStaticLetDef *def = &stmt->u.static_let_def;
        return tc_struct_resolve_type(&def->type, &def->struct_type_name, table, def->line, diag);
    }
    if (stmt->kind == TC_STMT_FUNC_DEF) {
        TcFuncDef *func = &stmt->u.func_def;
        size_t j = 0;

        if (tc_struct_resolve_type(&func->return_type, &func->return_struct_name, table, func->line,
                                   diag) != 0) {
            return -1;
        }
        for (j = 0; j < func->param_count; j++) {
            TcFuncParam *param = &func->params[j];
            if (tc_struct_resolve_type(&param->type, &param->struct_type_name, table, func->line,
                                       diag) != 0) {
                return -1;
            }
        }
        for (i = 0; i < func->body_count; i++) {
            if (tc_struct_resolve_stmt_types(&func->body[i], table, diag) != 0) {
                return -1;
            }
        }
        return 0;
    }
    if (stmt->kind == TC_STMT_IF) {
        for (i = 0; i < stmt->u.if_stmt.then_count; i++) {
            if (tc_struct_resolve_stmt_types(&stmt->u.if_stmt.then_body[i], table, diag) != 0) {
                return -1;
            }
        }
        for (i = 0; i < stmt->u.if_stmt.else_count; i++) {
            if (tc_struct_resolve_stmt_types(&stmt->u.if_stmt.else_body[i], table, diag) != 0) {
                return -1;
            }
        }
        return 0;
    }
    if (stmt->kind == TC_STMT_WHILE) {
        for (i = 0; i < stmt->u.while_stmt.body_count; i++) {
            if (tc_struct_resolve_stmt_types(&stmt->u.while_stmt.body[i], table, diag) != 0) {
                return -1;
            }
        }
        return 0;
    }
    return 0;
}

/** 扫程序中的 var/let/static/func（含函数体与块），解析 struct 类型名。 */
static int tc_struct_resolve_var_types(TcProgram *program, TcStructTable *table,
                                       TcDiagnostic *diag) {
    size_t i = 0;

    for (i = 0; i < program->count; i++) {
        if (tc_struct_resolve_stmt_types(&program->items[i], table, diag) != 0) {
            return -1;
        }
    }
    return 0;
}

void tc_struct_table_init(TcStructTable *table) {
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

void tc_struct_table_free(TcStructTable *table) {
    size_t i = 0;
    size_t j = 0;

    if (!table) {
        return;
    }
    for (i = 0; i < table->count; i++) {
        TcStructEntry *entry = &table->items[i];
        free(entry->name);
        for (j = 0; j < entry->field_count; j++) {
            free(entry->fields[j].name);
            free(entry->fields[j].struct_type_name);
            tc_type_free(&entry->fields[j].type);
        }
        free(entry->fields);
    }
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

const TcStructEntry *tc_struct_table_find(const TcStructTable *table, const char *name) {
    size_t i = 0;

    if (!table || !name) {
        return NULL;
    }
    for (i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) {
            return &table->items[i];
        }
    }
    return NULL;
}

const TcStructEntry *tc_struct_table_get(const TcStructTable *table, int struct_id) {
    if (!table || struct_id < 0 || (size_t)struct_id >= table->count) {
        return NULL;
    }
    return &table->items[(size_t)struct_id];
}

int tc_struct_path_offset_bytes(const TcStructTable *table, int struct_id, char *const *fields,
                                size_t field_count, size_t *out_offset_bytes,
                                const TcType **out_field_type, TcDiagnostic *diag, int line) {
    const TcStructEntry *entry = NULL;
    size_t bit_off = 0;
    size_t i = 0;
    int cursor_sid = struct_id;
    const TcType *final_type = NULL;

    entry = tc_struct_table_get(table, struct_id);
    if (!entry || !fields || field_count == 0) {
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN,
                          "undefined struct type for field path");
        return -1;
    }

    for (i = 0; i < field_count; i++) {
        size_t j = 0;
        const TcStructField *field = NULL;
        size_t before = 0;

        entry = tc_struct_table_get(table, cursor_sid);
        if (!entry) {
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
            char msg[128];
            (void)snprintf(msg, sizeof(msg), "unknown struct field '%s'", fields[i]);
            tc_diagnostic_set(diag, TC_CE_STRUCT_UNKNOWN_FIELD, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        bit_off += before;
        final_type = &field->type;
        if (i + 1 < field_count) {
            if (field->type.tag != TC_STRUCT) {
                tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                                  "field path requires struct base");
                return -1;
            }
            cursor_sid = field->type.params.struct_type.struct_id;
        }
    }
    *out_offset_bytes = bit_off / 8U;
    if (out_field_type) {
        *out_field_type = final_type;
    }
    return 0;
}

size_t tc_struct_table_width_bits(int struct_id, void *userdata) {
    const TcStructTable *table = (const TcStructTable *)userdata;

    if (!table || struct_id < 0 || (size_t)struct_id >= table->count) {
        return 0;
    }
    return table->items[(size_t)struct_id].width_bits;
}

int tc_struct_table_register_program(TcProgram *program, TcStructTable *table,
                                     TcDiagnostic *diag) {
    size_t i = 0;
    size_t j = 0;
    size_t k = 0;

    /* 第一遍：注册每个 STRUCT_DEF */
    for (i = 0; i < program->count; i++) {
        TcStatement *stmt = &program->items[i];
        TcStructDef *def = NULL;
        TcStructEntry entry;
        char msg[128];

        if (stmt->kind != TC_STMT_STRUCT_DEF) {
            continue;
        }
        def = &stmt->u.struct_def;
        if (tc_struct_table_find(table, def->name)) {
            (void)snprintf(msg, sizeof(msg), "duplicate struct definition '%s'", def->name);
            tc_diagnostic_set(diag, TC_CE_DUPLICATE_STRUCT, def->line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (def->field_count == 0) {
            tc_diagnostic_set(diag, TC_CE_SYNTAX, def->line, TC_COLUMN_UNKNOWN,
                              "struct must have at least one field");
            return -1;
        }
        memset(&entry, 0, sizeof(entry));
        entry.name = strdup(def->name);
        if (!entry.name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, def->line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        entry.visibility = def->visibility;
        entry.struct_id = (int)table->count;
        entry.field_count = def->field_count;
        entry.fields = (TcStructField *)calloc(def->field_count, sizeof(TcStructField));
        if (!entry.fields) {
            free(entry.name);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, def->line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (j = 0; j < def->field_count; j++) {
            TcStructField *dst = &entry.fields[j];
            TcStructField *src = &def->fields[j];

            dst->name = strdup(src->name);
            if (!dst->name) {
                tc_struct_table_free(table);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, def->line, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            dst->is_var = src->is_var;
            dst->padding = src->padding;
            if (tc_type_copy(&src->type, &dst->type, diag) != 0) {
                tc_struct_table_free(table);
                return -1;
            }
            if (src->struct_type_name) {
                dst->struct_type_name = strdup(src->struct_type_name);
                if (!dst->struct_type_name) {
                    tc_struct_table_free(table);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, def->line, TC_COLUMN_UNKNOWN,
                                      "memory allocation failed");
                    return -1;
                }
            }
            if (tc_struct_validate_field_type(def->name, dst, entry.struct_id, table, def->line,
                                              diag) != 0) {
                tc_struct_table_free(table);
                free(entry.name);
                for (k = 0; k < entry.field_count; k++) {
                    free(entry.fields[k].name);
                    free(entry.fields[k].struct_type_name);
                    tc_type_free(&entry.fields[k].type);
                }
                free(entry.fields);
                return -1;
            }
        }
        def->struct_id = entry.struct_id;
        if (tc_struct_entry_push(table, &entry, diag, def->line) != 0) {
            free(entry.name);
            for (k = 0; k < entry.field_count; k++) {
                free(entry.fields[k].name);
                free(entry.fields[k].struct_type_name);
                tc_type_free(&entry.fields[k].type);
            }
            free(entry.fields);
            return -1;
        }
    }

    /* 第二遍：解析嵌套字段类型名并计算布局位宽 */
    for (i = 0; i < table->count; i++) {
        TcStructEntry *entry = &table->items[i];
        size_t w = 0;

        for (j = 0; j < entry->field_count; j++) {
            if (entry->fields[j].struct_type_name) {
                const TcStructEntry *nested =
                    tc_struct_table_find(table, entry->fields[j].struct_type_name);
                if (nested) {
                    tc_type_free(&entry->fields[j].type);
                    entry->fields[j].type = tc_type_make_struct(nested->struct_id);
                    free(entry->fields[j].struct_type_name);
                    entry->fields[j].struct_type_name = NULL;
                }
            }
            w += tc_struct_field_width_bits(&entry->fields[j], table);
        }
        entry->width_bits = w;
    }

    return tc_struct_resolve_var_types(program, table, diag);
}

static const TcStructField *tc_struct_find_field(const TcStructEntry *entry, const char *name) {
    size_t i = 0;

    for (i = 0; i < entry->field_count; i++) {
        if (strcmp(entry->fields[i].name, name) == 0) {
            return &entry->fields[i];
        }
    }
    return NULL;
}

static const TcSymbol *tc_struct_resolve_base(const char *base, const TcSymbolTable *visible,
                                              const TcSymbolTable *global, size_t stmt_index,
                                              int line, TcDiagnostic *diag) {
    return tc_resolve_visible_symbol(visible, global, base, stmt_index, line, diag);
}

int tc_struct_check_constructor(const TcRhs *rhs, const TcType *expected,
                                const TcStructTable *table, const TcSymbolTable *visible,
                                const TcSymbolTable *global, TcInitHistory *hist,
                                size_t stmt_index, int line, TcDiagnostic *diag,
                                TcWarningList *warnings, const char *self_name) {
    const TcStructEntry *def = NULL;
    size_t i = 0;
    size_t fi = 0;
    char msg[128];

    (void)warnings;
    /* 目标必须是 struct，且构造器名解析出的 id 与期望一致 */
    if (!expected || expected->tag != TC_STRUCT) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "struct constructor requires struct destination type");
        return -1;
    }
    if (rhs->kind != TC_RHS_STRUCT_CONSTRUCTOR) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "expected struct constructor expression");
        return -1;
    }
    def = tc_struct_table_find(table, rhs->u.struct_ctor.struct_name);
    if (!def) {
        (void)snprintf(msg, sizeof(msg), "undefined struct '%s'",
                       rhs->u.struct_ctor.struct_name);
        tc_diagnostic_set(diag, TC_CE_UNDEFINED_STRUCT, line, TC_COLUMN_UNKNOWN, msg);
        return -1;
    }
    if (def->struct_id != expected->params.struct_type.struct_id) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "struct constructor type does not match destination");
        return -1;
    }
    /* 声明中每个字段都必须出现；随后再验未知/重复/顺序/类型 */
    for (i = 0; i < def->field_count; i++) {
        size_t found = 0;
        for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
            if (strcmp(def->fields[i].name, rhs->u.struct_ctor.fields[fi].param_name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            (void)snprintf(msg, sizeof(msg), "missing field '%s' in struct constructor",
                           def->fields[i].name);
            tc_diagnostic_set(diag, TC_CE_STRUCT_MISSING_FIELD, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }
    for (i = 0; i < rhs->u.struct_ctor.field_count; i++) {
        const char *pname = rhs->u.struct_ctor.fields[i].param_name;
        const TcStructField *field_def = tc_struct_find_field(def, pname);
        size_t expected_index = 0;
        size_t j = 0;

        if (!field_def) {
            (void)snprintf(msg, sizeof(msg), "unknown struct field '%s'", pname);
            tc_diagnostic_set(diag, TC_CE_STRUCT_UNKNOWN_FIELD, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(rhs->u.struct_ctor.fields[j].param_name, pname) == 0) {
                (void)snprintf(msg, sizeof(msg), "duplicate struct field '%s'", pname);
                tc_diagnostic_set(diag, TC_CE_STRUCT_DUPLICATE_FIELD, line, TC_COLUMN_UNKNOWN,
                                  msg);
                return -1;
            }
        }
        for (j = 0; j < def->field_count; j++) {
            if (&def->fields[j] == field_def) {
                expected_index = j;
                break;
            }
        }
        if (i != expected_index) {
            tc_diagnostic_set(diag, TC_CE_STRUCT_FIELD_ORDER, line, TC_COLUMN_UNKNOWN,
                              "struct constructor fields must follow declaration order");
            return -1;
        }
        if (rhs->u.struct_ctor.fields[i].has_rhs) {
            if (tc_type_check_rhs((TcRhs *)rhs->u.struct_ctor.fields[i].value_rhs,
                                  &field_def->type,
                                  visible, global, table, hist, stmt_index, line, diag, warnings,
                                  self_name) != 0) {
                return -1;
            }
        } else if (tc_check_operand((TcOperand *)&rhs->u.struct_ctor.fields[i].value_op,
                                    field_def->type.tag, visible, global, hist, stmt_index, line,
                                    diag, warnings, self_name, TC_CE_TYPE_MISMATCH) != 0) {
            return -1;
        }
    }
    return 0;
}

int tc_struct_check_field_read(const TcRhs *rhs, const TcType *expected,
                               const TcStructTable *table, const TcSymbolTable *visible,
                               const TcSymbolTable *global, TcInitHistory *hist,
                               size_t stmt_index, int line, TcDiagnostic *diag,
                               TcWarningList *warnings, const char *self_name) {
    const TcSymbol *base_sym = NULL;
    const TcType *cursor_type = NULL;
    size_t i = 0;

    /* 沿 a.b.c 逐级下降：每步要求当前类型为 struct，并解析字段 */
    (void)warnings;
    (void)hist;
    (void)self_name;
    if (rhs->kind != TC_RHS_FIELD_READ || rhs->u.field_read.field_count == 0) {
        tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                          "invalid struct field read");
        return -1;
    }
    base_sym = tc_struct_resolve_base(rhs->u.field_read.base, visible, global, stmt_index, line,
                                      diag);
    if (!base_sym) {
        return -1;
    }
    if (tc_type_tag_of(base_sym->type) != TC_STRUCT) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "field read requires struct base");
        return -1;
    }
    cursor_type = base_sym->type;
    for (i = 0; i < rhs->u.field_read.field_count; i++) {
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
        field = tc_struct_find_field(cur_def, rhs->u.field_read.fields[i]);
        if (!field) {
            char msg[128];
            (void)snprintf(msg, sizeof(msg), "unknown struct field '%s'",
                           rhs->u.field_read.fields[i]);
            tc_diagnostic_set(diag, TC_CE_STRUCT_UNKNOWN_FIELD, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        cursor_type = &field->type;
    }
    if (expected && !tc_type_equals(cursor_type, expected)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "field read result type does not match expected type");
        return -1;
    }
    return 0;
}

int tc_struct_check_field_assign(const TcFieldAssign *assign, const TcStructTable *table,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                                 TcWarningList *warnings) {
    const TcSymbol *base_sym = NULL;
    const TcType *cursor_type = NULL;
    const TcStructField *field = NULL;
    size_t i = 0;
    char msg[128];

    /* 基对象不可为 let；路径上每个字段须为 var（is_var）；RHS 匹配末字段类型 */
    base_sym = tc_struct_resolve_base(assign->base, visible, global, stmt_index, assign->line,
                                      diag);
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
    return tc_type_check_rhs((TcRhs *)&assign->rhs, field ? &field->type : base_sym->type,
                             visible,
                             global, table, hist, stmt_index, assign->line, diag, warnings, NULL);
}
