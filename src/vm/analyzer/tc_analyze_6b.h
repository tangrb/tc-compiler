/*
 * tc_analyze_6b.h — 名称作用域预建
 *
 * 建立本库成员索引、全局函数名冲突检查、标签表和符号槽分配。
 * 当前包装 Pass1 的 tc_pass1_collect_symbols，后续扩展成员索引和函数名冲突。
 */
#ifndef TC_ANALYZE_6B_H
#define TC_ANALYZE_6B_H

#include "tc_analyzer_internal.h"

/**
 * 6b: 名称作用域预建 — 委托 tc_pass1_collect_symbols 建立符号可见性表、
 * 分配栈槽位、构建标签表。
 *
 * 返回 0 成功，-1 失败
 */
int tc_analyze_6b_build_scopes(
    TcProgram *program,
    TcSymbolTable *symbols,
    TcDiagnostic *diag);

#endif /* TC_ANALYZE_6B_H */
