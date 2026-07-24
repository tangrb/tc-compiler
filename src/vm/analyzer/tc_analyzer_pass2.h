/*
 * tc_analyzer_pass2.h — Pass2 类型与语义检查（6c+6d）+ funcall/return
 *
 * 6a 标签收集 → tc_analyze_6a；6e 格式检查 → tc_analyze_6e。
 * 本文件负责 6c（goto 解析+跳转）、6d（类型/mode/RHS）及 funcall/return。
 *
 * @param struct_table Phase 3 已注册的结构体表；不可为 NULL
 * @param func_env     Phase 4 函数检查环境；可为 NULL（则跳过 funcall/return 专用检查）
 */
#ifndef TC_ANALYZER_PASS2_H
#define TC_ANALYZER_PASS2_H

#include "tc_analyzer_internal.h"
#include "tc_func_check.h"
#include "tc_struct_check.h"

int tc_pass2_type_check(TcProgram *program, TcSymbolTable *symbols, TcStructTable *struct_table,
                        TcFuncCheckEnv *func_env, TcWarningList *warnings, TcDiagnostic *diag);

#endif /* TC_ANALYZER_PASS2_H */
