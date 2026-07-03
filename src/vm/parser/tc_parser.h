/*
 * tc_parser.h — 语法分析器接口
 *
 * 将一行 Token 流解析为一条 TcStatement（变量/常量定义、赋值、I/O 等）。
 * 同时提供 TcProgram 动态数组管理及 AST 节点内存释放函数。
 *
 * TC 语言语法概要（一行一语句，分号可选）：
 *   var id: type [= rhs]        变量定义
 *   let id: type = literal      常量定义
 *   id = rhs                    赋值
 *   write/writeln(type [,fmt,] operand)  输出
 *   read(type, id)              输入
 *
 * rhs = literal | op(type [,wrap,] lhs, rhs) | cast(type [,truncate,] operand)
 */
#ifndef TC_PARSER_H
#define TC_PARSER_H

#include "tc_types.h"
#include "tc_lexer.h"

/**
 * 初始化 TcProgram 为空状态。
 * @param program 待初始化的程序指针
 */
void tc_program_init(TcProgram *program);

/**
 * 释放整个 TcProgram 包含的所有语句及其动态内存。
 * @param program 待释放的程序指针
 */
void tc_program_free(TcProgram *program);

/**
 * 向程序末尾追加一条语句。
 * @param program 程序指针
 * @param stmt    待追加的语句（内容被浅拷贝，调用方可复用 stmt 空间）
 * @return 成功返回 0；内存不足返回 -1
 */
int tc_program_push(TcProgram *program, const TcStatement *stmt);

/**
 * 释放 TcRhs 中的动态内存（name / source 等堆分配的字段）。
 * @param rhs 待释放的 RHS 指针
 */
void tc_rhs_free(TcRhs *rhs);

/**
 * 释放单条语句中的动态内存，与 tc_rhs_free 协同使用。
 * @param stmt 待释放的语句指针
 */
void tc_statement_free(TcStatement *stmt);

/**
 * 解析单条语句。
 * @param tokens  Token 列表（来自 tc_tokenize_line，含行尾 TC_TOK_EOF）
 * @param line_no 当前行号
 * @param out     输出参数，解析后的 TcStatement
 * @param diag    诊断对象
 * @return 成功返回 0；语法错误返回 -1 并设置 diag
 */
int tc_parse_statement(const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag);

#endif
