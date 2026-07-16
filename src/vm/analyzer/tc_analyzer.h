/*
 * tc_analyzer.h — 静态分析器接口
 *
 * Analyzer 在 Parser 产出 TcProgram 后执行两遍扫描：
 *   Pass 1 — 收集所有 var/let 定义，分配运行时槽位，检测重复定义
 *   Pass 2 — 按语句顺序做类型检查、字面量范围校验、overflow 合法性检查、常量编译期求值
 *
 * 分析通过后产出 TcTypedProgram（语句、符号表、完整 CFG、空警告列表），供 VM/AOT 使用。
 */
#ifndef TC_ANALYZER_H
#define TC_ANALYZER_H

#include "tc_symbol.h"
#include "tc_types.h"

/**
 * 初始化已类型化程序为空状态。
 * @param program 待初始化的 TcTypedProgram 指针
 */
void tc_typed_program_init(TcTypedProgram *program);

/**
 * 释放已类型化程序（释放语句列表、符号表和警告列表）。
 * @param program 待释放的 TcTypedProgram 指针
 */
void tc_typed_program_free(TcTypedProgram *program);

/**
 * 对 program 做两遍静态分析，结果写入 out。
 * @param program 待分析的源程序（成功后所有权被转移至 out，调用方不得再使用）
 * @param out     输出参数，分析通过后的 TcTypedProgram
 * @param diag    诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag（out 已被清空，无需 tc_typed_program_free）
 *
 * @note 所有权转移策略：通过 struct 浅拷贝转移 program->items 的所有权，
 *       然后将原始 program 清零。此模式避免了深拷贝开销，但要求调用方
 *       在 tc_analyze 返回后不再使用原始的 program 变量。
 */
int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag);

/**
 * REPL 增量分析上下文。
 * 轻量初始化历史（替代完整语句列表），跟踪每个变量的最后赋值位置。
 */
typedef struct {
    size_t stmt_count;
    int *last_init_stmt_index;  /* 按 slot 索引的最后初始化语句序号，-1 表示未初始化 */
    size_t last_init_capacity;
} TcReplAnalyzeCtx;

/**
 * 对单条语句做增量静态分析（REPL 会话使用）。
 * @param stmt      待分析的语句
 * @param symbols   会话符号表（var/let 定义成功后自动追加新符号并分配 slot）
 * @param repl_ctx  REPL 分析上下文（stmt_count 为当前语句索引）
 * @param warnings  警告输出列表
 * @param diag      诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag（symbols 不变）
 */
int tc_analyze_statement(TcStatement *stmt, TcSymbolTable *symbols,
                         TcReplAnalyzeCtx *repl_ctx, TcWarningList *warnings,
                         TcDiagnostic *diag);

#endif
