#include "tc_diagnostic.h"
#include "tc_lexer.h"
#include <stdio.h>

static const char *token_name(TcTokenKind kind) {
    switch (kind) {
    case TC_TOK_EOF: return "EOF";
    case TC_TOK_VAR: return "VAR";
    case TC_TOK_LET: return "LET";
    case TC_TOK_INT_TYPE: return "INT_TYPE";
    case TC_TOK_ARITH_OP: return "ARITH_OP";
    case TC_TOK_UNARY_OP: return "UNARY_OP";
    case TC_TOK_FORMAT_SPEC: return "FORMAT_SPEC";
    case TC_TOK_CAST: return "CAST";
    case TC_TOK_WRAP: return "WRAP";
    case TC_TOK_TRUNCATE: return "TRUNCATE";
    case TC_TOK_WRITE: return "WRITE";
    case TC_TOK_WRITELN: return "WRITELN";
    case TC_TOK_READ: return "READ";
    case TC_TOK_IDENTIFIER: return "IDENTIFIER";
    case TC_TOK_INTEGER: return "INTEGER";
    case TC_TOK_COLON: return "COLON";
    case TC_TOK_EQUAL: return "EQUAL";
    case TC_TOK_COMMA: return "COMMA";
    case TC_TOK_LPAREN: return "LPAREN";
    case TC_TOK_RPAREN: return "RPAREN";
    case TC_TOK_SEMICOLON: return "SEMICOLON";
    default: return "UNKNOWN";
    }
}

static void debug_lex(const char *line) {
    TcTokenList tokens;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    int r = tc_tokenize_line(line, 1, &tokens, &diag);
    printf("line: %s\n", line);
    if (r != 0) {
        printf("  tokenize failed: %s\n", diag.message);
    } else {
        for (size_t i = 0; i < tokens.count; i++) {
            printf("  [%zu] kind=%s", i, token_name(tokens.items[i].kind));
            if (tokens.items[i].kind == TC_TOK_INT_TYPE) {
                printf(" int_type=%d", tokens.items[i].u.int_type);
            }
            if (tokens.items[i].kind == TC_TOK_INTEGER) {
                printf(" magnitude=%llu neg=%d uns=%d",
                    (unsigned long long)tokens.items[i].u.literal.magnitude,
                    tokens.items[i].u.literal.negative,
                    tokens.items[i].u.literal.unsigned_suffix);
            }
            if (tokens.items[i].kind == TC_TOK_FORMAT_SPEC) {
                printf(" fmt=%d", tokens.items[i].u.format_spec);
            }
            printf("\n");
        }
    }
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    debug_lex("writeln(int32, %d, x)");
    debug_lex("let N: int32 = 42");
    debug_lex("cast(int8, truncate, x)");
    debug_lex("add(int8, wrap, x, y)");
    debug_lex("writeln(int32, %d, x)");
    debug_lex("var e: uint64 = 0x1_FFFF_FFFF_FFFF");
    debug_lex("let N: int32 = 42");
    return 0;
}
