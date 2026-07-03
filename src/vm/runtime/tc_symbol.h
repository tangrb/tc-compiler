/*
 * tc_symbol.h — 符号表运行时接口
 *
 * 符号表的创建、查找与释放；供 Analyzer、Executor、REPL 共享。
 */
#ifndef TC_SYMBOL_H
#define TC_SYMBOL_H

#include "tc_types.h"

#include "tc_diagnostic.h"

void tc_symbol_table_init(TcSymbolTable *table);
void tc_symbol_table_free(TcSymbolTable *table);
const TcSymbol *tc_symbol_table_find(const TcSymbolTable *table, const char *name);

int tc_symbol_table_add(TcSymbolTable *table, const char *name, TcIntType type, int slot,
                        int def_line, int def_stmt_index, TcSymKind sym_kind, int initialized,
                        TcDiagnostic *diag);
void tc_symbol_table_pop_last(TcSymbolTable *table);
TcSymbol *tc_symbol_table_find_mut(TcSymbolTable *table, const char *name);

#endif
