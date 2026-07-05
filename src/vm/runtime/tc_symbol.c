/*
 * tc_symbol.c — 符号表实现
 *
 * 简单的线性查找符号表。TC v1.0 为单全局作用域，暂无哈希表需求。
 * 符号的 name 字段由符号表负责 strdup 并在释放时 free。
 */
#include "tc_symbol.h"

#include "tc_diagnostic.h"

#include <stdlib.h>
#include <string.h>

void tc_symbol_table_init(TcSymbolTable *table) {
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
}

void tc_symbol_table_free(TcSymbolTable *table) {
    size_t i = 0;
    for (i = 0; i < table->count; i++) {
        free(table->symbols[i].name);
    }
    free(table->symbols);
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
}

const TcSymbol *tc_symbol_table_find(const TcSymbolTable *table, const char *name) {
    size_t i = 0;
    for (i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) {
            return &table->symbols[i];
        }
    }
    return NULL;
}

int tc_symbol_table_add(TcSymbolTable *table, const char *name, TcIntType type, int slot,
                        int def_line, int def_stmt_index, TcSymKind sym_kind, int initialized,
                        TcDiagnostic *diag) {
    if (table->count == table->capacity) {
        size_t new_cap = table->capacity == 0 ? 8 : table->capacity * 2;
        TcSymbol *symbols = (TcSymbol *)realloc(table->symbols, new_cap * sizeof(TcSymbol));
        if (!symbols) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, def_line, TC_COLUMN_UNKNOWN, "out of memory");
            return -1;
        }
        table->symbols = symbols;
        table->capacity = new_cap;
    }
    table->symbols[table->count].name = strdup(name);
    if (!table->symbols[table->count].name) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, def_line, TC_COLUMN_UNKNOWN, "out of memory");
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
    size_t i = 0;
    for (i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) {
            return &table->symbols[i];
        }
    }
    return NULL;
}
