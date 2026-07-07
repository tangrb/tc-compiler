/*
 * tc_symbol.h — 符号表运行时接口
 *
 * 符号表的创建、查找与释放，供 Analyzer、Executor、REPL 共享。
 * TcSymbolTable 管理 TcSymbol 动态数组与作用域栈，支持块级作用域与 shadowing。
 */
#ifndef TC_SYMBOL_H
#define TC_SYMBOL_H

#include "tc_types.h"

#include "tc_diagnostic.h"

/** 初始化空符号表（含全局作用域 level 0） */
void tc_symbol_table_init(TcSymbolTable *table);

/** 释放符号表全部动态内存（包括每个 symbol->name 与作用域栈） */
void tc_symbol_table_free(TcSymbolTable *table);

/**
 * 按 name 查找符号（只读，内层作用域优先）。
 * 等价于 tc_symbol_table_find_in_scope()，保留以兼容现有调用点。
 */
const TcSymbol *tc_symbol_table_find(const TcSymbolTable *table, const char *name);

/**
 * 在当前活跃作用域链内按 name 查找（内层优先，支持 shadowing）。
 * 仅返回 scope_level <= current_scope 的符号；未找到返回 NULL。
 */
const TcSymbol *tc_symbol_table_find_in_scope(const TcSymbolTable *table, const char *name);

/**
 * 当前作用域内是否已有同名定义（用于 Pass1 重复定义检测）。
 * 未找到返回 NULL。
 */
const TcSymbol *tc_symbol_table_find_in_current_scope(const TcSymbolTable *table,
                                                      const char *name);

/**
 * 向符号表追加一个符号（scope_level 取当前作用域层级）。
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

/** 进入新作用域，返回新 level；内存不足返回 -1 */
int tc_symbol_table_push_scope(TcSymbolTable *table);

/** 退出当前作用域（移除该 level 全部符号）；不可弹出全局作用域 */
void tc_symbol_table_pop_scope(TcSymbolTable *table);

/** 获取当前作用域层级；无栈帧时返回 0 */
int tc_symbol_table_current_scope(const TcSymbolTable *table);

/** 移除最后一个符号（REPL 分析失败时回滚） */
void tc_symbol_table_pop_last(TcSymbolTable *table);

/** 按 name 查找符号（可变，内层作用域优先），用于修改符号字段（如 const_value） */
TcSymbol *tc_symbol_table_find_mut(TcSymbolTable *table, const char *name);

#endif
