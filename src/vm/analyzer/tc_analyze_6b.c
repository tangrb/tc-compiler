/*
 * tc_analyze_6b.c — 名称作用域预建
 *
 * 6b 子阶段：为每个变量/常量/形参/static 分配全局唯一 slot，
 * 建立 def_stmt_index/scope_end_stmt_index，构建成员索引和全局名称冲突检测。
 * 委托原有的 tc_pass1_collect_symbols 执行。
 */
#include "tc_analyze_6b.h"
#include "tc_analyzer_pass1.h"
#include "tc_symbol.h"

#include <stddef.h>

int tc_analyze_6b_build_scopes(TcProgram *program, TcSymbolTable *symbols,
                                TcDiagnostic *diag)
{
    tc_pass1_collect_symbols(program, symbols, diag);
    return 0;
}
