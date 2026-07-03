/*
 * tc_analyzer.h — 静态分析器接口
 *
 * Analyzer 在 Parser 产出 TcProgram 后执行两遍扫描：
 *   Pass 1 — 收集所有 var 定义，分配运行时槽位，检测重复定义
 *   Pass 2 — 按语句顺序做类型检查、字面量范围校验、overflow 合法性检查
 *
 * 分析通过后产出 TcTypedProgram（语句列表 + 全局符号表），供 Executor 使用。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_ANALYZER_H
#define TC_ANALYZER_H

#include "tc_symbol.h"
#include "tc_types.h"

/**
 * @brief 初始化已类型化的程序
 * @param program 待初始化的 TcTypedProgram 指针
 */
void tc_typed_program_init(TcTypedProgram *program);

/**
 * @brief 释放已类型化程序（释放程序语句和符号表）
 * @param program 待释放的 TcTypedProgram 指针
 */
void tc_typed_program_free(TcTypedProgram *program);

/**
 * @brief 对 program 做两遍静态分析，结果写入 out
 * @param program 待分析的源程序（成功后所有权被转移）
 * @param out     输出参数，分析通过后的 TcTypedProgram
 * @param diag    诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag（此时 out 被释放）
 */
int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag);

/**
 * @brief REPL 增量分析上下文（轻量初始化历史，替代完整语句列表）
 */
typedef struct {
    size_t stmt_count;
    int *last_init_stmt_index;
    size_t last_init_capacity;
} TcReplAnalyzeCtx;

/**
 * @brief 对单条语句做增量静态分析（REPL 会话）
 * @param stmt      待分析的语句
 * @param symbols   会话符号表（var 定义成功后会追加新符号并分配 slot）
 * @param repl_ctx  REPL 分析上下文（stmt_count 为当前语句索引）
 * @param warnings  警告输出列表
 * @param diag      诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag（symbols 不变）
 */
int tc_analyze_statement(const TcStatement *stmt, TcSymbolTable *symbols,
                         TcReplAnalyzeCtx *repl_ctx, TcWarningList *warnings,
                         TcDiagnostic *diag);

#endif
