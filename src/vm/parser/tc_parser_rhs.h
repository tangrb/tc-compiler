/*
 * tc_parser_rhs.h — RHS / const-RHS 解析接口
 */
#ifndef TC_PARSER_RHS_H
#define TC_PARSER_RHS_H

#include "tc_parser.h"
#include "tc_types.h"
#include "tc_lexer.h"
#include "tc_diagnostic.h"

/**
 * 解析运行时 RHS（34 种 TcRhsKind：标量 16 + 复合/调用 18）。
 * FUNCALL_EXPR 的 kind 由语句解析赋值；其余多在本模块完成。
 * @return 成功 0；失败 -1 并设置 diag
 */
int tc_parse_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index, int line_no,
                 TcRhs *out, TcDiagnostic *diag);

/**
 * 解析 let 常量 RHS（禁 wrap/truncate/ieee 等）。
 * @return 成功 0；失败 -1 并设置 diag
 */
int tc_parse_const_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index, int line_no,
                       TcRhs *out, TcDiagnostic *diag);

#endif /* TC_PARSER_RHS_H */
