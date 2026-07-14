/*
 * tc_symbol.c — 符号表实现
 *
 * 线性符号数组 + 作用域栈。符号按定义顺序追加；pop_scope 关闭当前层可见性（符号保留）。
 * find_in_scope 自尾部向前扫描，内层 shadowing 优先。
 * 标签表：同深度禁止重名；pop_scope 时删除当前深度标签（兄弟块可复用同名）。
 */
#include "tc_symbol.h"

#include "tc_diagnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tc_symbol_name_hash(const char *name) {
    unsigned hash = 5381u;

    while (*name != '\0') {
        hash = ((hash << 5) + hash) + (unsigned char)*name;
        name++;
    }
    return hash % TC_SYM_NAME_BUCKETS;
}

void tc_symbol_name_index_init(TcSymbolNameIndex *index) {
    size_t i = 0;

    for (i = 0; i < TC_SYM_NAME_BUCKETS; i++) {
        index->buckets[i] = NULL;
    }
    index->nodes = NULL;
    index->node_count = 0;
}

void tc_symbol_name_index_free(TcSymbolNameIndex *index) {
    free(index->nodes);
    tc_symbol_name_index_init(index);
}

int tc_symbol_name_index_build(const TcSymbolTable *table, TcSymbolNameIndex *index,
                               TcDiagnostic *diag) {
    size_t i = 0;

    tc_symbol_name_index_free(index);
    if (table->count == 0) {
        return 0;
    }

    index->nodes = (TcSymNameIndexNode *)malloc(table->count * sizeof(TcSymNameIndexNode));
    if (!index->nodes) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
        return -1;
    }
    index->node_count = table->count;

    for (i = 0; i < table->count; i++) {
        unsigned bucket = tc_symbol_name_hash(table->symbols[i].name);
        TcSymNameIndexNode *node = &index->nodes[i];

        node->sym_index = i;
        node->next = index->buckets[bucket];
        index->buckets[bucket] = node;
    }
    return 0;
}

const TcSymbol *tc_symbol_table_find_visible(const TcSymbolTable *table, const char *name,
                                             int stmt_index, const TcSymbolNameIndex *index) {
    const TcSymbol *best = NULL;

    if (index && index->nodes) {
        unsigned bucket = tc_symbol_name_hash(name);
        const TcSymNameIndexNode *node = index->buckets[bucket];

        while (node) {
            const TcSymbol *sym = &table->symbols[node->sym_index];

            if (strcmp(sym->name, name) == 0) {
                if (sym->def_stmt_index < stmt_index &&
                    (sym->scope_end_stmt_index < 0 || stmt_index < sym->scope_end_stmt_index) &&
                    (!best || sym->def_stmt_index > best->def_stmt_index)) {
                    best = sym;
                }
            }
            node = node->next;
        }
        if (best) {
            return best;
        }
        return tc_symbol_table_find(table, name);
    }

    {
        size_t i = 0;

        for (i = 0; i < table->count; i++) {
            const TcSymbol *sym = &table->symbols[i];

            if (strcmp(sym->name, name) != 0) {
                continue;
            }
            if (sym->def_stmt_index >= stmt_index) {
                continue;
            }
            if (sym->scope_end_stmt_index >= 0 && stmt_index >= sym->scope_end_stmt_index) {
                continue;
            }
            if (!best || sym->def_stmt_index > best->def_stmt_index) {
                best = sym;
            }
        }
        if (best) {
            return best;
        }
        return tc_symbol_table_find(table, name);
    }
}

static int tc_symbol_table_ensure_scope_capacity(TcSymbolTable *table, size_t need) {
    if (table->scope_capacity >= need) {
        return 0;
    }
    {
        size_t new_cap = table->scope_capacity == 0 ? 4 : table->scope_capacity * 2;
        TcScope *scopes = (TcScope *)realloc(table->scopes, new_cap * sizeof(TcScope));

        if (!scopes) {
            return -1;
        }
        table->scopes = scopes;
        table->scope_capacity = new_cap;
    }
    return 0;
}

static void tc_symbol_table_push_global_scope(TcSymbolTable *table) {
    if (table->scope_count > 0) {
        return;
    }
    if (tc_symbol_table_ensure_scope_capacity(table, 1) != 0) {
        return;
    }
    table->scopes[0].start_index = 0;
    table->scopes[0].end_index = TC_SCOPE_END_OPEN;
    table->scopes[0].level = 0;
    table->scope_count = 1;
}

void tc_symbol_table_init(TcSymbolTable *table) {
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
    table->scopes = NULL;
    table->scope_count = 0;
    table->scope_capacity = 0;
    table->labels = NULL;
    table->label_count = 0;
    table->label_capacity = 0;
    tc_symbol_table_push_global_scope(table);
}

void tc_symbol_table_clear_labels(TcSymbolTable *table) {
    size_t i = 0;

    for (i = 0; i < table->label_count; i++) {
        free(table->labels[i].name);
        free(table->labels[i].block_path);
        table->labels[i].name = NULL;
        table->labels[i].block_path = NULL;
    }
    table->label_count = 0;
}

void tc_symbol_table_free(TcSymbolTable *table) {
    size_t i = 0;

    for (i = 0; i < table->count; i++) {
        free(table->symbols[i].name);
    }
    free(table->symbols);
    free(table->scopes);
    tc_symbol_table_clear_labels(table);
    free(table->labels);
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
    table->scopes = NULL;
    table->scope_count = 0;
    table->scope_capacity = 0;
    table->labels = NULL;
    table->label_count = 0;
    table->label_capacity = 0;
}

int tc_symbol_table_current_scope(const TcSymbolTable *table) {
    if (table->scope_count == 0) {
        return 0;
    }
    return table->scopes[table->scope_count - 1].level;
}

int tc_symbol_table_push_scope(TcSymbolTable *table) {
    int new_level = 0;

    if (table->scope_count == 0) {
        tc_symbol_table_push_global_scope(table);
    }
    if (table->scope_count == 0) {
        return -1;
    }
    new_level = table->scopes[table->scope_count - 1].level + 1;
    if (tc_symbol_table_ensure_scope_capacity(table, table->scope_count + 1) != 0) {
        return -1;
    }
    table->scopes[table->scope_count].start_index = table->count;
    table->scopes[table->scope_count].end_index = TC_SCOPE_END_OPEN;
    table->scopes[table->scope_count].level = new_level;
    table->scope_count++;
    return new_level;
}

static int tc_label_paths_equal(const int *a, const int *b, int depth) {
    int i = 0;

    if (depth == 0) {
        return 1;
    }
    if (!a || !b) {
        return a == b;
    }
    for (i = 0; i < depth; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

void tc_symbol_table_pop_labels(TcSymbolTable *table) {
    int depth = tc_symbol_table_current_scope(table);

    while (table->label_count > 0) {
        TcLabelEntry *entry = &table->labels[table->label_count - 1];

        if (entry->block_depth != depth) {
            break;
        }
        free(entry->name);
        free(entry->block_path);
        entry->name = NULL;
        entry->block_path = NULL;
        table->label_count--;
    }
}

void tc_symbol_table_pop_scope(TcSymbolTable *table) {
    if (table->scope_count <= 1) {
        return;
    }
    /* 先按退出深度清理标签，再关闭作用域帧 */
    tc_symbol_table_pop_labels(table);
    table->scopes[table->scope_count - 1].end_index = table->count;
    table->scope_count--;
}

int tc_symbol_table_add_label(TcSymbolTable *table, const char *name, int stmt_index, int line,
                              const int *block_path, int block_depth, TcDiagnostic *diag) {
    size_t i = 0;
    char msg[128];
    int *path_copy = NULL;

    for (i = 0; i < table->label_count; i++) {
        const TcLabelEntry *entry = &table->labels[i];

        if (strcmp(entry->name, name) != 0 || entry->block_depth != block_depth) {
            continue;
        }
        if (block_path != NULL) {
            if (!tc_label_paths_equal(entry->block_path, block_path, block_depth)) {
                continue;
            }
        }
        (void)snprintf(msg, sizeof(msg), "duplicate label '%s'", name);
        tc_diagnostic_set(diag, TC_ERR_DUPLICATE_LABEL, line, TC_COLUMN_UNKNOWN, msg);
        return -1;
    }

    if (block_depth > 0 && block_path != NULL) {
        path_copy = (int *)malloc((size_t)block_depth * sizeof(int));
        if (!path_copy) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        memcpy(path_copy, block_path, (size_t)block_depth * sizeof(int));
    }

    if (table->label_count == table->label_capacity) {
        size_t new_cap = table->label_capacity == 0 ? 8 : table->label_capacity * 2;
        TcLabelEntry *labels =
            (TcLabelEntry *)realloc(table->labels, new_cap * sizeof(TcLabelEntry));

        if (!labels) {
            free(path_copy);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        table->labels = labels;
        table->label_capacity = new_cap;
    }

    table->labels[table->label_count].name = strdup(name);
    if (!table->labels[table->label_count].name) {
        free(path_copy);
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    table->labels[table->label_count].stmt_index = stmt_index;
    table->labels[table->label_count].block_depth = block_depth;
    table->labels[table->label_count].block_path = path_copy;
    table->labels[table->label_count].def_line = line;
    table->label_count++;
    return 0;
}

const TcLabelEntry *tc_symbol_table_find_label(const TcSymbolTable *table, const char *name) {
    size_t i = 0;

    if (!table || !name) {
        return NULL;
    }
    for (i = table->label_count; i > 0; i--) {
        const TcLabelEntry *entry = &table->labels[i - 1];

        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

const TcSymbol *tc_symbol_table_find_in_scope(const TcSymbolTable *table, const char *name) {
    int s = (int)table->scope_count - 1;

    while (s >= 0) {
        const TcScope *frame = &table->scopes[s];
        size_t end = frame->end_index;
        size_t i = 0;

        if (end == TC_SCOPE_END_OPEN) {
            end = table->count;
        }
        for (i = end; i > frame->start_index; i--) {
            const TcSymbol *sym = &table->symbols[i - 1];

            if (sym->scope_level != frame->level) {
                continue;
            }
            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
        }
        s--;
    }
    return NULL;
}

const TcSymbol *tc_symbol_table_find_in_current_scope(const TcSymbolTable *table,
                                                      const char *name) {
    const TcScope *frame = NULL;
    size_t end = 0;
    size_t i = 0;

    if (table->scope_count == 0) {
        return NULL;
    }
    frame = &table->scopes[table->scope_count - 1];
    end = frame->end_index;
    if (end == TC_SCOPE_END_OPEN) {
        end = table->count;
    }
    for (i = end; i > frame->start_index; i--) {
        const TcSymbol *sym = &table->symbols[i - 1];

        if (sym->scope_level != frame->level) {
            continue;
        }
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

const TcSymbol *tc_symbol_table_find(const TcSymbolTable *table, const char *name) {
    return tc_symbol_table_find_in_scope(table, name);
}

int tc_symbol_table_add(TcSymbolTable *table, const char *name, TcType type, int slot,
                        int def_line, int def_stmt_index, TcSymKind sym_kind, int initialized,
                        TcDiagnostic *diag) {
    if (table->scope_count == 0) {
        tc_symbol_table_push_global_scope(table);
    }
    if (table->count == table->capacity) {
        size_t new_cap = table->capacity == 0 ? 8 : table->capacity * 2;
        TcSymbol *symbols = (TcSymbol *)realloc(table->symbols, new_cap * sizeof(TcSymbol));

        if (!symbols) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, def_line, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        table->symbols = symbols;
        table->capacity = new_cap;
    }
    table->symbols[table->count].name = strdup(name);
    if (!table->symbols[table->count].name) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, def_line, TC_COLUMN_UNKNOWN, "memory allocation failed");
        return -1;
    }
    table->symbols[table->count].type = type;
    table->symbols[table->count].slot = slot;
    table->symbols[table->count].def_line = def_line;
    table->symbols[table->count].def_stmt_index = def_stmt_index;
    table->symbols[table->count].sym_kind = sym_kind;
    table->symbols[table->count].initialized = initialized;
    table->symbols[table->count].has_const_value = 0;
    table->symbols[table->count].const_value.type = type;
    table->symbols[table->count].const_value.bits = 0;
    table->symbols[table->count].scope_level = tc_symbol_table_current_scope(table);
    table->symbols[table->count].scope_end_stmt_index = -1;
    table->count++;
    return 0;
}

void tc_symbol_table_pop_last(TcSymbolTable *table) {
    if (table->count == 0) {
        return;
    }
    table->count--;
    free(table->symbols[table->count].name);
    table->symbols[table->count].name = NULL;
}

TcSymbol *tc_symbol_table_find_mut(TcSymbolTable *table, const char *name) {
    const TcSymbol *found = tc_symbol_table_find_in_scope(table, name);

    if (!found) {
        return NULL;
    }
    return &table->symbols[(size_t)(found - table->symbols)];
}
