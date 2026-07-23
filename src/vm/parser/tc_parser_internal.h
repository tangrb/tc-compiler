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

/** 堆分配复制 Token 文本；失败设置 OOM 并返回 NULL */
char *tc_token_strdup(const TcToken *tok, int line_no, TcDiagnostic *diag);

/** 标识符 Token 是否与 name 完全匹配 */
int tc_token_is_ident_named(const TcToken *tok, const char *name);

/**
 * 解析完整类型语法（标量 / void / ptr / memblock / struct 名）。
 * @param allow_void  1 允许 void（函数返回类型）
 * @param out_type    输出 TcType（含堆分配嵌套 ptr/memblock）
 * @param out_struct_name  若 kind==TC_STRUCT 则输出结构体名（堆分配）；否则 NULL
 */
int tc_parse_type_syntax(const TcTokenList *tokens, size_t *index, int line_no,
                         int allow_void, TcType *out_type, char **out_struct_name,
                         TcDiagnostic *diag);

#endif /* TC_PARSER_INTERNAL_H */
