/*
 * tc_parser.h — 语法分析器接口
 *
 * 将一行 Token 流解析为一条 TcStatement（变量定义或赋值）。
 * 同时提供 TcProgram 动态数组的管理及 AST 节点内存释放函数。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_PARSER_H
#define TC_PARSER_H

#include "tc_types.h"
#include "tc_lexer.h"

/**
 * @brief 初始化 TcProgram 为空状态
 * @param program 待初始化的程序指针
 */
void tc_program_init(TcProgram *program);

/**
 * @brief 释放整个 TcProgram 包含的所有语句及其动态内存
 * @param program 待释放的程序指针
 */
void tc_program_free(TcProgram *program);

/**
 * @brief 向程序末尾追加一条语句
 * @param program 程序指针
 * @param stmt    待追加的语句指针
 * @return 成功返回 0；内存不足返回 -1
 */
int tc_program_push(TcProgram *program, const TcStatement *stmt);

/**
 * @brief 释放 TcRhs 中的动态内存
 * @param rhs 待释放的 RHS 指针
 */
void tc_rhs_free(TcRhs *rhs);

/**
 * @brief 释放单条语句中的动态内存
 * @param stmt 待释放的语句指针
 */
void tc_statement_free(TcStatement *stmt);

/**
 * @brief 解析单条语句
 * @param tokens  Token 列表（来自 tc_tokenize_line，含行尾 EOF）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcStatement
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1 并设置 diag
 */
int tc_parse_statement(const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag);

#endif
