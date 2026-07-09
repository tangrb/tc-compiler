/*
 * tc_symbol.c — 符号表实现
 *
 * 线性符号数组 + 作用域栈。符号按定义顺序追加；pop_scope 截断当前层符号。
 * find_in_scope 自尾部向前扫描，内层 shadowing 优先。
 */
#include "tc_symbol.h"

#include "tc_diagnostic.h"

#include <stdlib.h>
#include <string.h>

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
    tc_symbol_table_push_global_scope(table);
}

void tc_symbol_table_free(TcSymbolTable *table) {
    size_t i = 0;

    for (i = 0; i < table->count; i++) {
        free(table->symbols[i].name);
    }
    free(table->symbols);
    free(table->scopes);
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
    table->scopes = NULL;
    table->scope_count = 0;
    table->scope_capacity = 0;
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

void tc_symbol_table_pop_scope(TcSymbolTable *table) {
    if (table->scope_count <= 1) {
        return;
    }
    table->scopes[table->scope_count - 1].end_index = table->count;
    table->scope_count--;
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

int tc_symbol_table_add(TcSymbolTable *table, const char *name, TcIntType type, int slot,
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
