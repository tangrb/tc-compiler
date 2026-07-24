/*
 * tc_analyze_6a.h — 控制流上下文检查 (goto/label 词法祖先校验)
 *
 * 遍历 AST 收集标签定义、校验 goto/label 的 func/while 词法祖先，
 * 建立标签表供 6c 使用。
 */
#ifndef TC_ANALYZE_6A_H
#define TC_ANALYZE_6A_H

#include "tc_analyzer_internal.h"

/**
 * 6a: 遍历语句树收集所有标签定义，校验:
 *   - label 不在 while 循环内部
 *
 * 返回 0 成功，-1 失败（已通过 diag 设置错误）
 */
int tc_analyze_6a_collect_labels(TcProgram *program, TcSymbolTable *symbols,
                                 TcAnalyzeCtx *ctx, TcDiagnostic *diag);

#endif /* TC_ANALYZE_6A_H */
