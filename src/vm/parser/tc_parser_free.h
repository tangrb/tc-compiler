/*
 * tc_parser_free.h — AST / TcProgram 内存释放接口
 */
#ifndef TC_PARSER_FREE_H
#define TC_PARSER_FREE_H

#include "tc_types.h"
#include "tc_diagnostic.h"

/** 释放 TcOperand 中的动态内存（变量名） */
void tc_operand_free(TcOperand *operand);

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
 * @param diag    诊断对象
 * @return 成功返回 0；内存不足返回 -1
 */
int tc_program_push(TcProgram *program, const TcStatement *stmt, TcDiagnostic *diag);

#endif /* TC_PARSER_FREE_H */
