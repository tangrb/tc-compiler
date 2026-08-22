/*
 * tc_analyzer_pass1.h — Pass1 符号收集与槽分配
 *
 * DFS 登记 var/let/static/形参，intern 类型，分配 slot 与 loop id。
 */
 */
#ifndef TC_ANALYZER_PASS1_H
#define TC_ANALYZER_PASS1_H

#include "tc_analyzer_internal.h"

int tc_pass1_collect_symbols(TcProgram *program, TcSymbolTable *symbols, TcTypeTable *types,
                             TcDiagnostic *diag);

#endif /* TC_ANALYZER_PASS1_H */
