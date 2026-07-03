/*
 * tc_symbol.h — 符号表运行时接口
 *
 * 符号表的创建、查找与释放，供 Analyzer、Executor、REPL 共享。
 * TcSymbolTable 管理 TcSymbol 动态数组，按 name 线性查找。
 */
#ifndef TC_SYMBOL_H
#define TC_SYMBOL_H

#include "tc_types.h"

#include "tc_diagnostic.h"

/** 初始化空符号表 */
void tc_symbol_table_init(TcSymbolTable *table);

/** 释放符号表全部动态内存（包括每个 symbol->name） */
void tc_symbol_table_free(TcSymbolTable *table);

/** 按 name 查找符号（只读），未找到返回 NULL */
const TcSymbol *tc_symbol_table_find(const TcSymbolTable *table, const char *name);

/**
 * 向符号表追加一个符号。
 * @param table           符号表
 * @param name            符号名（内部 strdup 复制）
 * @param type            整数类型
 * @param slot            运行时槽位索引
 * @param def_line        定义行号
 * @param def_stmt_index  定义语句序号
 * @param sym_kind        符号种类（variable / constant）
 * @param initialized     定义时是否有初始化值
 * @param diag            诊断对象（内存不足时设置）
 * @return 成功返回 0；内存不足返回 -1
 */
int tc_symbol_table_add(TcSymbolTable *table, const char *name, TcIntType type, int slot,
                        int def_line, int def_stmt_index, TcSymKind sym_kind, int initialized,
                        TcDiagnostic *diag);

/** 移除最后一个符号（REPL 分析失败时回滚） */
void tc_symbol_table_pop_last(TcSymbolTable *table);

/** 按 name 查找符号（可变），用于修改符号的字段（如 const_value） */
TcSymbol *tc_symbol_table_find_mut(TcSymbolTable *table, const char *name);

#endif
