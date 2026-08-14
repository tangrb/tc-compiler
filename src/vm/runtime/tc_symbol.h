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

#define TC_SYM_NAME_BUCKETS 64

typedef struct TcSymNameIndexNode {
    size_t sym_index;
    struct TcSymNameIndexNode *next;
} TcSymNameIndexNode;

/** 按符号名哈希的只读索引，用于 stmt_index 可见性查询 */
typedef struct {
    TcSymNameIndexNode *buckets[TC_SYM_NAME_BUCKETS];
    TcSymNameIndexNode *nodes;
    size_t node_count;
} TcSymbolNameIndex;

/** 初始化空索引 */
void tc_symbol_name_index_init(TcSymbolNameIndex *index);

/** 释放索引内存 */
void tc_symbol_name_index_free(TcSymbolNameIndex *index);

/**
 * 为符号表构建按名哈希索引。
 * @return 成功 0；内存不足 -1
 */
int tc_symbol_name_index_build(const TcSymbolTable *table, TcSymbolNameIndex *index,
                               TcDiagnostic *diag);

/**
 * 按 stmt_index 可见性查找变量（索引加速；index 为 NULL 时线性扫描）。
 */
const TcSymbol *tc_symbol_table_find_visible(const TcSymbolTable *table, const char *name,
                                             int stmt_index, const TcSymbolNameIndex *index);

/** 返回变量所需的运行时槽数；let 常量不占槽。 */
size_t tc_symbol_table_runtime_slot_count(const TcSymbolTable *table);

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
 * 向符号表追加一个符号（完整类型版本）。
 * @param type  已 intern / 单例的稳定类型指针；禁止传临时栈上 TcType 地址
 * @param slot_domain 槽域
 */
int tc_symbol_table_add_ex(TcSymbolTable *table, const char *name, const TcType *type,
                           int slot, TcSlotDomain slot_domain, int def_line, int def_stmt_index,
                           TcSymKind sym_kind, int initialized, TcDiagnostic *diag);

/**
 * 向符号表追加一个符号（scope_level 取当前作用域层级；标量快捷入口）。
 * @param table           符号表
 * @param name            符号名（内部 strdup 复制）
 * @param type_tag        标量标签（内部转为单例指针）
 * @param slot            运行时槽位索引
 * @param def_line        定义行号
 * @param def_stmt_index  定义语句序号
 * @param sym_kind        符号种类（variable / constant）
 * @param initialized     定义时是否有初始化值
 * @param diag            诊断对象（内存不足时设置）
 * @return 成功返回 0；内存不足返回 -1
 */
int tc_symbol_table_add(TcSymbolTable *table, const char *name, TcTypeTag type_tag, int slot,
                        int def_line, int def_stmt_index, TcSymKind sym_kind, int initialized,
                        TcDiagnostic *diag);

/** 进入新作用域，返回新 level；内存不足返回 -1 */
int tc_symbol_table_push_scope(TcSymbolTable *table);

/** 退出当前作用域（移除该 level 全部符号）；不可弹出全局作用域 */
void tc_symbol_table_pop_scope(TcSymbolTable *table);

/** 获取当前作用域层级；无栈帧时返回 0 */
int tc_symbol_table_current_scope(const TcSymbolTable *table);

/** 移除最后一个符号（分析失败回滚） */
void tc_symbol_table_pop_last(TcSymbolTable *table);

/** 按 name 查找符号（可变，内层作用域优先），用于修改符号字段（如 const_value） */
TcSymbol *tc_symbol_table_find_mut(TcSymbolTable *table, const char *name);

/**
 * 添加标签。
 * @param func_id     所属函数 func_id（4d 稳定分配；顶层传 -1）
 * @param block_path  块路径（长度 block_depth）；NULL 表示仅按 depth 查重（Pass1）
 * @param block_depth 路径深度；Pass1 传当前作用域层级
 * 同函数同作用域重名 → TC_CE_DUPLICATE_LABEL；跨函数同名合法（各自独立标签表），
 * 不同块路径允许同名。
 * @return 成功 0；重复标签或 OOM 返回 -1
 */
int tc_symbol_table_add_label(TcSymbolTable *table, const char *name, int func_id,
                              int stmt_index, int line, const TcBlockId *block_path,
                              int block_depth, TcDiagnostic *diag);

/**
 * 自表尾向前按名查找标签（不区分块；跳转解析见 Analyzer）。
 * @return TcLabelEntry* 或 NULL（未找到）
 */
const TcLabelEntry *tc_symbol_table_find_label(const TcSymbolTable *table, const char *name);

/**
 * 移除当前块深度内的所有标签（作用域退出时由 pop_scope 自动调用）。
 */
void tc_symbol_table_pop_labels(TcSymbolTable *table);

/** 清空全部标签（Pass2 重新收集前调用） */
void tc_symbol_table_clear_labels(TcSymbolTable *table);

#endif
