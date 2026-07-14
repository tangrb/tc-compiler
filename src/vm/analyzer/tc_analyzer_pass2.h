/*
 * tc_analyzer_pass2.h — Pass2 类型检查、标签收集、goto 合法性
 */
#ifndef TC_ANALYZER_PASS2_H
#define TC_ANALYZER_PASS2_H

#include "tc_analyzer_internal.h"

int tc_pass2_type_check(TcProgram *program, TcSymbolTable *symbols, TcWarningList *warnings,
                        TcDiagnostic *diag);

#endif /* TC_ANALYZER_PASS2_H */
