/*
 * tc_parser_internal.h — parser 模块内共享辅助（tc_parser.c ↔ tc_parser_rhs.c）
 *
 * 非公共 API；仅 parser 子模块间使用。
 */
#ifndef TC_PARSER_INTERNAL_H
#define TC_PARSER_INTERNAL_H

#include "tc_types.h"
#include "tc_lexer.h"
#include "tc_diagnostic.h"

int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message);
const TcToken *tc_peek(const TcTokenList *tokens, size_t index);
int tc_parse_operand(const TcTokenList *tokens, size_t *index, int line_no,
                     TcOperand *out, TcDiagnostic *diag);
int tc_expect_token(const TcTokenList *tokens, size_t *index, TcTokenKind kind,
                    int line_no, TcDiagnostic *diag);
int tc_token_is_type(const TcToken *tok);

#endif /* TC_PARSER_INTERNAL_H */
