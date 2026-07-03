/*
 * tc_lexer.h — 词法分析器接口
 */
#ifndef TC_LEXER_H
#define TC_LEXER_H

#include "tc_types.h"

typedef enum {
    TC_TOK_EOF,
    TC_TOK_VAR,
    TC_TOK_LET,
    TC_TOK_INT_TYPE,
    TC_TOK_ARITH_OP,
    TC_TOK_CAST,
    TC_TOK_WRAP,
    TC_TOK_TRUNCATE,
    TC_TOK_WRITE,
    TC_TOK_WRITELN,
    TC_TOK_READ,
    TC_TOK_IDENTIFIER,
    TC_TOK_INTEGER,
    TC_TOK_COLON,
    TC_TOK_EQUAL,
    TC_TOK_COMMA,
    TC_TOK_LPAREN,
    TC_TOK_RPAREN,
    TC_TOK_SEMICOLON
} TcTokenKind;

typedef struct {
    TcTokenKind kind;
    const char *start;
    size_t length;
    int line;
    int column;
    union {
        TcIntType int_type;
        TcArithOp arith_op;
        TcLiteral literal;
    } u;
} TcToken;

typedef struct {
    TcToken *items;
    size_t count;
    size_t capacity;
} TcTokenList;

void tc_token_list_init(TcTokenList *list);
void tc_token_list_free(TcTokenList *list);

int tc_tokenize_line(const char *line, int line_no, TcTokenList *out,
                     TcDiagnostic *diag);

#endif
