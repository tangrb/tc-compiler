/*
 * tc_executor.h — 执行引擎接口
 *
 * Executor 消费 Analyzer 产出的 TcTypedProgram，按语句顺序 dispatch：
 * 求值 RHS 并将结果写入对应变量的运行时槽位。
 * TC 源码即「高级字节码」，无需 lowering 为第二套 opcode。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#ifndef TC_EXECUTOR_H
#define TC_EXECUTOR_H

#include "tc_types.h"

/**
 * @brief 执行已类型化的程序
 * @param program 已类型化的程序指针（含语句列表和符号表）
 * @param diag    诊断对象
 * @return 成功返回 0；运行时错误（溢出、除零等）返回 -1 并设置 diag
 */
int tc_execute(const TcTypedProgram *program, TcDiagnostic *diag);

/**
 * @brief 执行单条语句（REPL 会话，使用已有变量槽）
 * @param stmt    待执行的语句
 * @param slots   运行时变量槽位数组（长度至少 symbols->count）
 * @param symbols 全局符号表
 * @param diag    诊断对象
 * @return 成功返回 0；运行时错误返回 -1 并设置 diag
 */
int tc_execute_statement(const TcStatement *stmt, TcValue *slots, const TcSymbolTable *symbols,
                         TcDiagnostic *diag);

#endif
