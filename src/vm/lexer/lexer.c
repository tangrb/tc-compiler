/*
 * lexer.c — TC 词法分析器实现（v0.0.14）
 */
#include "tc_lexer.h"

#include "tc_diagnostic.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int tc_token_list_push(TcTokenList *list, const TcToken *token) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        TcToken *items = (TcToken *)realloc(list->items, new_cap * sizeof(TcToken));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = *token;
    return 0;
}

void tc_token_list_init(TcTokenList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void tc_token_list_free(TcTokenList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int tc_is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int tc_is_identifier_start(char c) {
    return tc_is_letter(c);
}

static int tc_is_identifier_part(char c) {
    return tc_is_letter(c) || (c >= '0' && c <= '9');
}

static void tc_skip_ws(const char **p, int *column) {
    while (**p == ' ' || **p == '\t') {
        (*p)++;
        if (column) {
            (*column)++;
        }
    }
}

static int tc_digit_value(char c, int base) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (base == 16) {
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
    }
    return -1;
}

static int tc_mul_u64_overflow(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0 && b > UINT64_MAX / a) {
        return 1;
    }
    *out = a * b;
    return 0;
}

static int tc_add_u64_overflow(uint64_t a, uint64_t b, uint64_t *out) {
    if (a > UINT64_MAX - b) {
        return 1;
    }
    *out = a + b;
    return 0;
}

static int tc_parse_radix_digits(const char **p, int base, int allow_underscore,
                                 uint64_t *value, TcDiagnostic *diag, int line, int column) {
    int has_digit = 0;
    int prev_underscore = 0;

    while (**p != '\0') {
        if (allow_underscore && **p == '_') {
            if (!has_digit || prev_underscore) {
                tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid integer literal");
                return -1;
            }
            prev_underscore = 1;
            (*p)++;
            continue;
        }
        prev_underscore = 0;
        {
            int digit = tc_digit_value(**p, base);
            if (digit < 0 || (base == 8 && digit > 7) || (base == 2 && digit > 1)) {
                break;
            }
            has_digit = 1;
            {
                uint64_t next = 0;
                if (tc_mul_u64_overflow(*value, (uint64_t)base, value) ||
                    tc_add_u64_overflow(*value, (uint64_t)digit, &next)) {
                    tc_diagnostic_set(diag, TC_ERR_LITERAL_OUT_OF_RANGE, line, column,
                                      "integer literal too large");
                    return -1;
                }
                *value = next;
            }
            (*p)++;
        }
    }

    if (!has_digit || prev_underscore) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid integer literal");
        return -1;
    }
    return 0;
}

static int tc_parse_integer_literal(const char *start, const char **end, TcLiteral *lit,
                                    TcDiagnostic *diag, int line, int column) {
    const char *p = start;
    int negative = 0;

    lit->magnitude = 0;
    lit->negative = 0;
    lit->unsigned_suffix = 0;

    if (*p == '-') {
        negative = 1;
        p++;
        if (*p == '\0') {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "expected integer literal");
            return -1;
        }
    }

    if (*p == '0') {
        if (p[1] == 'x' || p[1] == 'X') {
            p += 2;
            if (tc_parse_radix_digits(&p, 16, 1, &lit->magnitude, diag, line, column) != 0) {
                return -1;
            }
        } else if (p[1] == 'b' || p[1] == 'B') {
            p += 2;
            if (tc_parse_radix_digits(&p, 2, 1, &lit->magnitude, diag, line, column) != 0) {
                return -1;
            }
        } else if (p[1] == 'o' || p[1] == 'O') {
            p += 2;
            if (tc_parse_radix_digits(&p, 8, 1, &lit->magnitude, diag, line, column) != 0) {
                return -1;
            }
        } else if (p[1] >= '0' && p[1] <= '9') {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "invalid integer literal");
            return -1;
        } else {
            lit->magnitude = 0;
            p++;
        }
    } else if (isdigit((unsigned char)*p)) {
        if (tc_parse_radix_digits(&p, 10, 1, &lit->magnitude, diag, line, column) != 0) {
            return -1;
        }
    } else {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, "expected integer literal");
        return -1;
    }

    if (*p == 'u' || *p == 'U') {
        if (negative) {
            tc_diagnostic_set(diag, TC_ERR_LITERAL_TYPE, line, column,
                              "negative value cannot use unsigned suffix");
            return -1;
        }
        lit->unsigned_suffix = 1;
        p++;
    }

    if (negative && !lit->unsigned_suffix) {
        lit->negative = 1;
    }

    *end = p;
    return 0;
}

static int tc_keyword_token(const char *text, size_t len, TcToken *token) {
    char buf[32];
    if (len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, text, len);
    buf[len] = '\0';

    if (strcmp(buf, "var") == 0) {
        token->kind = TC_TOK_VAR;
        return 1;
    }
    if (strcmp(buf, "let") == 0) {
        token->kind = TC_TOK_LET;
        return 1;
    }
    if (strcmp(buf, "cast") == 0) {
        token->kind = TC_TOK_CAST;
        return 1;
    }
    if (strcmp(buf, "wrap") == 0) {
        token->kind = TC_TOK_WRAP;
        return 1;
    }
    if (strcmp(buf, "truncate") == 0) {
        token->kind = TC_TOK_TRUNCATE;
        return 1;
    }
    if (strcmp(buf, "write") == 0) {
        token->kind = TC_TOK_WRITE;
        return 1;
    }
    if (strcmp(buf, "writeln") == 0) {
        token->kind = TC_TOK_WRITELN;
        return 1;
    }
    if (strcmp(buf, "read") == 0) {
        token->kind = TC_TOK_READ;
        return 1;
    }
    if (tc_type_parse(buf, &token->u.int_type)) {
        token->kind = TC_TOK_INT_TYPE;
        return 1;
    }
    if (tc_arith_op_parse(buf, &token->u.arith_op)) {
        token->kind = TC_TOK_ARITH_OP;
        return 1;
    }
    return 0;
}

static int tc_emit_token(TcTokenList *out, TcTokenKind kind, const char *start, size_t len,
                         int line, int column, TcIntType int_type, TcArithOp arith_op,
                         const TcLiteral *literal) {
    TcToken token;
    token.kind = kind;
    token.start = start;
    token.length = len;
    token.line = line;
    token.column = column;
    if (literal) {
        token.u.literal = *literal;
    } else {
        token.u.int_type = int_type;
        token.u.arith_op = arith_op;
    }
    return tc_token_list_push(out, &token);
}

int tc_tokenize_line(const char *line, int line_no, TcTokenList *out, TcDiagnostic *diag) {
    const char *p = line;
    int column = 1;

    tc_skip_ws(&p, &column);

    while (*p != '\0' && *p != '\r' && *p != '\n') {
        const char *start = p;
        int tok_column = column;

        if (*p == ':') {
            /* int_type 和 arith_op 对单字符 token 无意义，传入默认值占位 */
            if (tc_emit_token(out, TC_TOK_COLON, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '=') {
            if (tc_emit_token(out, TC_TOK_EQUAL, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == ',') {
            if (tc_emit_token(out, TC_TOK_COMMA, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == '(') {
            if (tc_emit_token(out, TC_TOK_LPAREN, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == ')') {
            if (tc_emit_token(out, TC_TOK_RPAREN, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL) != 0) {
                return -1;
            }
            p++;
            column++;
            tc_skip_ws(&p, &column);
            continue;
        }
        if (*p == ';') {
            if (tc_emit_token(out, TC_TOK_SEMICOLON, start, 1, line_no, tok_column, TC_INT32, TC_ADD,
                              NULL) != 0) {
                return -1;
            }
            /* ; 起语句结束与注释作用，忽略同行后续字符（语言标准 stmt_terminator） */
            break;
        }

        if (*p == '-' || isdigit((unsigned char)*p)) {
            TcLiteral lit;
            const char *end = NULL;
            if (tc_parse_integer_literal(p, &end, &lit, diag, line_no, tok_column) != 0) {
                return -1;
            }
            if (tc_emit_token(out, TC_TOK_INTEGER, start, (size_t)(end - start), line_no, tok_column,
                              TC_INT32, TC_ADD, &lit) != 0) {
                return -1;
            }
            column += (int)(end - p);
            p = end;
            tc_skip_ws(&p, &column);
            continue;
        }

        if (tc_is_identifier_start(*p)) {
            p++;
            column++;
            while (tc_is_identifier_part(*p)) {
                p++;
                column++;
            }
            {
                TcToken token;
                size_t len = (size_t)(p - start);
                if (tc_keyword_token(start, len, &token)) {
                    token.start = start;
                    token.length = len;
                    token.line = line_no;
                    token.column = tok_column;
                    if (tc_token_list_push(out, &token) != 0) {
                        return -1;
                    }
                } else if (tc_emit_token(out, TC_TOK_IDENTIFIER, start, len, line_no, tok_column,
                                         TC_INT32, TC_ADD, NULL) != 0) {
                    return -1;
                }
            }
            tc_skip_ws(&p, &column);
            continue;
        }

        tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok_column, "unexpected character");
        return -1;
    }

    {
        TcToken eof;
        eof.kind = TC_TOK_EOF;
        eof.start = p;
        eof.length = 0;
        eof.line = line_no;
        eof.column = column;
        eof.u.int_type = TC_INT32;
        if (tc_token_list_push(out, &eof) != 0) {
            return -1;
        }
    }
    return 0;
}
