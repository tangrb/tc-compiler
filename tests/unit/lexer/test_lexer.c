/*
 * test_lexer.c — Lexer 模块单元测试
 *
 * 验证 token 序列与 union 字段（int_type / arith_op / literal）的正确性，
 * 防止 union 别名覆写类低级错误再次引入。
 */
#include "tc_diagnostic.h"
#include "tc_lexer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

static void check(int condition, const char *message) {
    if (condition) {
        g_passed++;
    } else {
        g_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static int tokenize_line_ok(const char *line, TcTokenList *tokens, TcDiagnostic *diag) {
    tc_token_list_init(tokens);
    tc_diagnostic_clear(diag);
    if (tc_tokenize_line(line, 1, tokens, diag) != 0) {
        tc_token_list_free(tokens);
        return 0;
    }
    return 1;
}

static const TcToken *find_token(const TcTokenList *tokens, TcTokenKind kind, size_t *index) {
    size_t i;
    for (i = *index; i < tokens->count; i++) {
        if (tokens->items[i].kind == kind) {
            *index = i + 1;
            return &tokens->items[i];
        }
    }
    return NULL;
}

static const TcToken *token_at(const TcTokenList *tokens, size_t index) {
    if (index >= tokens->count) {
        return NULL;
    }
    return &tokens->items[index];
}

static void test_uint8_type_token(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: uint8 = 250", &tokens, &diag), "tokenize var a: uint8 = 250");

    tok = find_token(&tokens, TC_TOK_INT_TYPE, &idx);
    check(tok != NULL && tok->u.int_type == TC_UINT8, "uint8 → TC_TOK_INT_TYPE with TC_UINT8");

    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 250 && tok->u.literal.negative == 0,
          "uint8 = 250 → integer literal magnitude 250");

    check(token_at(&tokens, tokens.count - 1)->kind == TC_TOK_EOF, "line ends with EOF");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_unsigned_suffix_literal(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: uint8 = 10u", &tokens, &diag), "tokenize var a: uint8 = 10u");

    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 10 && tok->u.literal.unsigned_suffix == 1,
          "10u → unsigned_suffix == 1");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_all_int_types(void) {
    static const struct {
        const char *name;
        TcTypeKind expected;
    } cases[] = {
        {"int8", TC_INT8},     {"uint8", TC_UINT8},     {"int16", TC_INT16},
        {"uint16", TC_UINT16}, {"int32", TC_INT32},     {"uint32", TC_UINT32},
        {"int64", TC_INT64},   {"uint64", TC_UINT64},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcTokenList tokens;
        TcDiagnostic diag;
        char line[64];
        size_t idx = 0;
        const TcToken *tok;

        snprintf(line, sizeof(line), "var x: %s = 0", cases[i].name);
        tc_diagnostic_init(&diag);
        check(tokenize_line_ok(line, &tokens, &diag), line);

        tok = find_token(&tokens, TC_TOK_INT_TYPE, &idx);
        check(tok != NULL && tok->u.int_type == cases[i].expected,
              "int type token preserves enum value");

        tc_token_list_free(&tokens);
        tc_diagnostic_clear(&diag);
    }
}

static void test_arith_op_add(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("add(int32, wrap, x, y)", &tokens, &diag),
          "tokenize add(int32, wrap, x, y)");

    tok = find_token(&tokens, TC_TOK_ARITH_OP, &idx);
    check(tok != NULL && tok->u.arith_op == TC_ADD, "add → TC_TOK_ARITH_OP with TC_ADD");

    tok = find_token(&tokens, TC_TOK_WRAP, &idx);
    check(tok != NULL, "wrap keyword present");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_all_arith_ops(void) {
    static const struct {
        const char *name;
        TcArithOp expected;
    } cases[] = {
        {"add", TC_ADD}, {"sub", TC_SUB}, {"mul", TC_MUL}, {"div", TC_DIV}, {"mod", TC_MOD},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcTokenList tokens;
        TcDiagnostic diag;
        char line[64];
        size_t idx = 0;
        const TcToken *tok;

        snprintf(line, sizeof(line), "%s(int32, x, y)", cases[i].name);
        tc_diagnostic_init(&diag);
        check(tokenize_line_ok(line, &tokens, &diag), line);

        tok = find_token(&tokens, TC_TOK_ARITH_OP, &idx);
        check(tok != NULL && tok->u.arith_op == cases[i].expected,
              "arith op token preserves enum value");

        tc_token_list_free(&tokens);
        tc_diagnostic_clear(&diag);
    }
}

static void test_hex_literal_with_separator_and_suffix(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var x: uint16 = 0xFF_00u", &tokens, &diag),
          "tokenize 0xFF_00u hex literal");

    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 0xFF00 && tok->u.literal.unsigned_suffix == 1,
          "0xFF_00u → magnitude 0xFF00, unsigned suffix");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_oct_and_bin_literals(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: int32 = 0o777", &tokens, &diag), "tokenize 0o777");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 511, "0o777 → magnitude 511");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("var b: int32 = 0b1010", &tokens, &diag), "tokenize 0b1010");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 10, "0b1010 → magnitude 10");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_semicolon_comment_truncates_line(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("; this is a comment", &tokens, &diag), "tokenize ; comment");

    check(tokens.count == 2, "; comment → SEMICOLON + EOF only");
    check(tokens.items[0].kind == TC_TOK_SEMICOLON, "first token is SEMICOLON");
    check(tokens.items[1].kind == TC_TOK_EOF, "second token is EOF");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_empty_line_eof_only(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("", &tokens, &diag), "tokenize empty line");
    check(tokens.count == 1 && tokens.items[0].kind == TC_TOK_EOF, "empty line → EOF only");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_negative_decimal_literal(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var x: int32 = -42", &tokens, &diag), "tokenize -42");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 42 && tok->u.literal.negative == 1,
          "-42 → negative literal");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_uint16_max_literal(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var c: uint16 = 65535", &tokens, &diag), "tokenize uint16 = 65535");

    tok = find_token(&tokens, TC_TOK_INT_TYPE, &idx);
    check(tok != NULL && tok->u.int_type == TC_UINT16, "uint16 type token");

    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 65535, "65535 literal magnitude");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_statement_keywords(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("let N: int32 = 1", &tokens, &diag), "tokenize let");
    check(token_at(&tokens, 0)->kind == TC_TOK_LET, "let → TC_TOK_LET");
    tc_token_list_free(&tokens);

    check(tokenize_line_ok("cast(int8, x)", &tokens, &diag), "tokenize cast");
    check(token_at(&tokens, 0)->kind == TC_TOK_CAST, "cast → TC_TOK_CAST");
    tc_token_list_free(&tokens);

    check(tokenize_line_ok("writeln(int32, x)", &tokens, &diag), "tokenize writeln");
    check(token_at(&tokens, 0)->kind == TC_TOK_WRITELN, "writeln → TC_TOK_WRITELN");
    tc_token_list_free(&tokens);

    tc_diagnostic_clear(&diag);
}

static void test_float_type_tokens(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var x: float32 = 0.0f", &tokens, &diag), "tokenize float32 type");
    tok = find_token(&tokens, TC_TOK_FLOAT_TYPE, &idx);
    check(tok != NULL && tok->u.int_type == TC_FLOAT32, "float32 → TC_TOK_FLOAT_TYPE");

    idx = 0;
    tc_token_list_free(&tokens);
    check(tokenize_line_ok("var y: float64 = 1.0", &tokens, &diag), "tokenize float64 type");
    tok = find_token(&tokens, TC_TOK_FLOAT_TYPE, &idx);
    check(tok != NULL && tok->u.int_type == TC_FLOAT64, "float64 → TC_TOK_FLOAT_TYPE");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_float_literal_tokens(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var x: float64 = 3.14", &tokens, &diag), "tokenize 3.14");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && tok->u.literal.is_float == 1 && tok->u.literal.float_value == 3.14,
          "3.14 → FLOAT_LIT is_float=1");

    idx = 0;
    tc_token_list_free(&tokens);
    check(tokenize_line_ok("var y: float32 = 1.0f", &tokens, &diag), "tokenize 1.0f");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && tok->u.literal.float32_suffix == 1, "1.0f → float32_suffix");

    idx = 0;
    tc_token_list_free(&tokens);
    check(tokenize_line_ok("var z: float64 = 1e5", &tokens, &diag), "tokenize 1e5");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && tok->u.literal.float_value == 100000.0, "1e5 → 100000.0");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_float_special_literals(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: float64 = inf", &tokens, &diag), "tokenize inf");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && isinf(tok->u.literal.float_value) &&
              tok->u.literal.float_value > 0,
          "inf → +inf FLOAT_LIT");

    idx = 0;
    tc_token_list_free(&tokens);
    check(tokenize_line_ok("var b: float64 = -inf", &tokens, &diag), "tokenize -inf");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && isinf(tok->u.literal.float_value) &&
              tok->u.literal.float_value < 0,
          "-inf → -inf FLOAT_LIT");

    idx = 0;
    tc_token_list_free(&tokens);
    check(tokenize_line_ok("var c: float64 = nan", &tokens, &diag), "tokenize nan");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && isnan(tok->u.literal.float_value), "nan → NAN FLOAT_LIT");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_ieee_keyword(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("add(float64, ieee, x, y)", &tokens, &diag), "tokenize ieee");
    tok = find_token(&tokens, TC_TOK_IEEE, &idx);
    check(tok != NULL, "ieee → TC_TOK_IEEE");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_uint8_type_token();
    test_unsigned_suffix_literal();
    test_all_int_types();
    test_arith_op_add();
    test_all_arith_ops();
    test_hex_literal_with_separator_and_suffix();
    test_oct_and_bin_literals();
    test_semicolon_comment_truncates_line();
    test_empty_line_eof_only();
    test_negative_decimal_literal();
    test_uint16_max_literal();
    test_statement_keywords();
    test_float_type_tokens();
    test_float_literal_tokens();
    test_float_special_literals();
    test_ieee_keyword();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
