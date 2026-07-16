/*
 * test_lexer_extended.c — Lexer 模块扩展单元测试
 *
 * 补充现有词法测试未覆盖的边界场景。
 */
#include "tc_diagnostic.h"
#include "tc_lexer.h"

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

/* 测试所有一元运算符 token */
static void test_all_unary_ops(void) {
    static const struct {
        const char *text;
        TcUnaryOp expected;
    } cases[] = {
        {"abs", TC_UNARY_ABS},
        {"neg", TC_UNARY_NEG},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcTokenList tokens;
        TcDiagnostic diag;
        char line[64];
        size_t idx = 0;
        const TcToken *tok;

        snprintf(line, sizeof(line), "%s(int32, wrap, x)", cases[i].text);
        tc_diagnostic_init(&diag);
        check(tokenize_line_ok(line, &tokens, &diag), line);

        tok = find_token(&tokens, TC_TOK_UNARY_OP, &idx);
        check(tok != NULL && tok->u.unary_op == cases[i].expected,
              "unary op token preserves enum value");

        tc_token_list_free(&tokens);
        tc_diagnostic_clear(&diag);
    }
}

/* 测试 cast 关键字 */
static void test_cast_keyword(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("cast(int8, truncate, x)", &tokens, &diag), "tokenize cast with truncate");

    check(token_at(&tokens, 0)->kind == TC_TOK_CAST, "cast → TC_TOK_CAST");
    /* tokens: CAST LPAREN INT_TYPE COMMA TRUNCATE COMMA IDENTIFIER RPAREN EOF */
    check(token_at(&tokens, 4)->kind == TC_TOK_TRUNCATE, "truncate → TC_TOK_TRUNCATE");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_bitcast_keyword(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("bitcast(float32, bits)", &tokens, &diag),
          "tokenize bitcast expression");
    check(token_at(&tokens, 0)->kind == TC_TOK_BITCAST,
          "bitcast -> TC_TOK_BITCAST");
    tc_token_list_free(&tokens);

    check(tokenize_line_ok("var bitcast_value: uint32 = 1u", &tokens, &diag),
          "tokenize identifier beginning with bitcast");
    check(token_at(&tokens, 1)->kind == TC_TOK_IDENTIFIER,
          "bitcast prefix does not consume a longer identifier");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

/* 测试 write/writeln/read 关键字 */
static void test_io_keywords(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("write(int32, x)", &tokens, &diag), "tokenize write");
    check(token_at(&tokens, 0)->kind == TC_TOK_WRITE, "write → TC_TOK_WRITE");
    tc_token_list_free(&tokens);

    /* tokens: WRITELN LPAREN INT_TYPE COMMA FORMAT_SPEC COMMA IDENTIFIER RPAREN EOF */
    check(tokenize_line_ok("writeln(int32, %d, x)", &tokens, &diag), "tokenize writeln with fmt");
    check(token_at(&tokens, 0)->kind == TC_TOK_WRITELN, "writeln → TC_TOK_WRITELN");
    check(token_at(&tokens, 4)->kind == TC_TOK_FORMAT_SPEC, "format spec → TC_TOK_FORMAT_SPEC");
    tc_token_list_free(&tokens);

    check(tokenize_line_ok("read(int32, x)", &tokens, &diag), "tokenize read");
    check(token_at(&tokens, 0)->kind == TC_TOK_READ, "read → TC_TOK_READ");
    tc_token_list_free(&tokens);

    tc_diagnostic_clear(&diag);
}

/* 测试各种进制字面量的 token 值 */
static void test_binary_literals(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: uint8 = 0b10101010", &tokens, &diag), "tokenize 0b10101010");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 170, "0b10101010 → magnitude 170");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_hex_literals(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: uint64 = 0xDEAD_BEEF", &tokens, &diag), "tokenize 0xDEAD_BEEF");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 0xDEADBEEFULL, "0xDEAD_BEEF → correct magnitude");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_oct_literals(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: uint32 = 0o7777_7777", &tokens, &diag), "tokenize 0o7777_7777");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    /* 0o77777777 = 16777215 */
    check(tok != NULL && tok->u.literal.magnitude == 16777215ULL, "0o7777_7777 → correct magnitude");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

/* 测试最大 uint64 字面量 */
static void test_max_uint64_literal(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: uint64 = 18446744073709551615", &tokens, &diag),
          "tokenize uint64 max literal");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 18446744073709551615ULL,
          "uint64 max → correct magnitude");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

/* 测试多种格式说明符 */
static void test_format_specifiers(void) {
    static const char *specs[] = {"%d", "%i", "%u", "%x", "%X", "%o", "%b"};
    size_t i;

    for (i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
        TcTokenList tokens;
        TcDiagnostic diag;
        char line[64];
        size_t idx = 0;
        const TcToken *tok;

        snprintf(line, sizeof(line), "writeln(int32, %s, x)", specs[i]);
        tc_diagnostic_init(&diag);
        check(tokenize_line_ok(line, &tokens, &diag), line);
        tok = find_token(&tokens, TC_TOK_FORMAT_SPEC, &idx);
        check(tok != NULL, "format spec token present");
        tc_token_list_free(&tokens);
        tc_diagnostic_clear(&diag);
    }
}

/* 测试 let 关键字 */
static void test_let_keyword(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    /* tokens: LET IDENTIFIER COLON INT_TYPE EQUAL INTEGER EOF */
    check(tokenize_line_ok("let N: int32 = 42", &tokens, &diag), "tokenize let N");
    check(token_at(&tokens, 0)->kind == TC_TOK_LET, "let → TC_TOK_LET");
    check(token_at(&tokens, 3)->kind == TC_TOK_INT_TYPE, "int32 type at index 3");
    check(token_at(&tokens, 5)->kind == TC_TOK_INTEGER, "literal 42 at index 5");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

/* 测试带下划线的多种分隔符字面量 */
static void test_underscore_literals(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: int32 = 1_2_3_4_5", &tokens, &diag), "tokenize 1_2_3_4_5");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 12345, "1_2_3_4_5 → 12345");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("var b: uint64 = 1_000_000_000_000", &tokens, &diag),
          "tokenize 1_000_000_000_000");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 1000000000000ULL,
          "1_000_000_000_000 → correct");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("var c: uint8 = 0xFF_AA", &tokens, &diag), "tokenize 0xFF_AA");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 0xFFAA, "0xFF_AA → 0xFFAA");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("var d: uint16 = 0b1010_0101", &tokens, &diag), "tokenize 0b1010_0101");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 0xA5, "0b1010_0101 → 0xA5");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("var e: uint64 = 0x1_FFFF_FFFF_FFFF", &tokens, &diag),
          "tokenize 0x1_FFFF_FFFF_FFFF");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 562949953421311ULL,
          "0x1_FFFF_FFFF_FFFF → 562949953421311");
    tc_token_list_free(&tokens);

    tc_diagnostic_clear(&diag);
}

/* 测试 wrap 和 truncate 关键字 */
static void test_wrap_truncate_keywords(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    /* tokens: ARITH_OP LPAREN INT_TYPE COMMA WRAP COMMA IDENTIFIER COMMA IDENTIFIER RPAREN EOF */
    check(tokenize_line_ok("add(int8, wrap, x, y)", &tokens, &diag), "tokenize add with wrap");
    check(token_at(&tokens, 4)->kind == TC_TOK_WRAP, "wrap at index 4 → TC_TOK_WRAP");
    tc_token_list_free(&tokens);

    check(tokenize_line_ok("cast(int8, truncate, x)", &tokens, &diag), "tokenize cast with truncate");
    /* tokens: CAST LPAREN INT_TYPE COMMA TRUNCATE COMMA IDENTIFIER RPAREN EOF */
    check(token_at(&tokens, 4)->kind == TC_TOK_TRUNCATE, "truncate at index 4 → TC_TOK_TRUNCATE");
    tc_token_list_free(&tokens);

    tc_diagnostic_clear(&diag);
}

/* 测试空白行和纯注释行 */
static void test_whitespace_and_comments(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("   ; indented comment", &tokens, &diag), "tokenize indented comment");
    check(tokens.count == 2, "indented comment → SEMICOLON + EOF");
    check(tokens.items[0].kind == TC_TOK_SEMICOLON, "first token is SEMICOLON");
    tc_token_list_free(&tokens);

    check(tokenize_line_ok("   \t  ", &tokens, &diag), "tokenize whitespace only");
    check(tokens.count == 1 && tokens.items[0].kind == TC_TOK_EOF, "whitespace → EOF only");
    tc_token_list_free(&tokens);

    tc_diagnostic_clear(&diag);
}

/* 测试位运算与移位关键字 token */
static void test_bitwise_shift_tokens(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);

    check(tokenize_line_ok("xor(int8, a, b)", &tokens, &diag), "tokenize xor(int8, a, b)");
    tok = find_token(&tokens, TC_TOK_BITWISE_OP, &idx);
    check(tok != NULL && tok->u.bitwise_op == TC_BIT_XOR, "xor → TC_TOK_BITWISE_OP with TC_BIT_XOR");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("shl(int32, a, 2)", &tokens, &diag), "tokenize shl(int32, a, 2)");
    tok = find_token(&tokens, TC_TOK_SHIFT_OP, &idx);
    check(tok != NULL && tok->u.shift_op == TC_SHIFT_SHL, "shl → TC_TOK_SHIFT_OP with TC_SHIFT_SHL");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("shr(uint8, a, 1)", &tokens, &diag), "tokenize shr(uint8, a, 1)");
    tok = find_token(&tokens, TC_TOK_SHIFT_OP, &idx);
    check(tok != NULL && tok->u.shift_op == TC_SHIFT_SHR, "shr → TC_TOK_SHIFT_OP with TC_SHIFT_SHR");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("and(bool, x, y)", &tokens, &diag), "tokenize and(bool, x, y)");
    tok = find_token(&tokens, TC_TOK_LOGIC_OP, &idx);
    check(tok != NULL && tok->u.logic_op == TC_LOGIC_AND,
          "and(bool, ...) → TC_TOK_LOGIC_OP (parser 按类型分派位运算)");
    tc_token_list_free(&tokens);

    idx = 0;
    check(tokenize_line_ok("var a: uint8 = 0b1010_1010u", &tokens, &diag),
          "tokenize 0b1010_1010u binary literal with suffix");
    tok = find_token(&tokens, TC_TOK_INTEGER, &idx);
    check(tok != NULL && tok->u.literal.magnitude == 0xAA && tok->u.literal.unsigned_suffix == 1,
          "0b1010_1010u → magnitude 0xAA with unsigned suffix");
    tc_token_list_free(&tokens);

    check(strcmp(tc_token_kind_name(TC_TOK_BITWISE_OP), "BITWISE_OP") == 0,
          "tc_token_kind_name(TC_TOK_BITWISE_OP) → BITWISE_OP");
    check(strcmp(tc_token_kind_name(TC_TOK_SHIFT_OP), "SHIFT_OP") == 0,
          "tc_token_kind_name(TC_TOK_SHIFT_OP) → SHIFT_OP");

    tc_diagnostic_clear(&diag);
}

static void test_control_flow_keywords(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);

    check(tc_tokenize_line("if x then", 1, &tokens, &diag) == 0, "tokenize if line");
    check(token_at(&tokens, 0)->kind == TC_TOK_IF, "if → TC_TOK_IF");
    check(token_at(&tokens, 2)->kind == TC_TOK_THEN, "then → TC_TOK_THEN");

    tc_token_list_free(&tokens);
    tc_token_list_init(&tokens);
    check(tc_tokenize_line("else", 2, &tokens, &diag) == 0, "tokenize else");
    check(token_at(&tokens, 0)->kind == TC_TOK_ELSE, "else → TC_TOK_ELSE");

    tc_token_list_free(&tokens);
    tc_token_list_init(&tokens);
    check(tc_tokenize_line("end", 3, &tokens, &diag) == 0, "tokenize end");
    check(token_at(&tokens, 0)->kind == TC_TOK_END, "end → TC_TOK_END");

    tc_token_list_free(&tokens);
    tc_token_list_init(&tokens);
    check(tc_tokenize_line("goto start", 4, &tokens, &diag) == 0, "tokenize goto");
    check(token_at(&tokens, 0)->kind == TC_TOK_GOTO, "goto → TC_TOK_GOTO");
    check(token_at(&tokens, 1)->kind == TC_TOK_IDENTIFIER, "goto target → IDENTIFIER");

    tc_token_list_free(&tokens);
    tc_token_list_init(&tokens);
    check(tc_tokenize_line("label start:", 5, &tokens, &diag) == 0, "tokenize label");
    check(token_at(&tokens, 0)->kind == TC_TOK_LABEL, "label → TC_TOK_LABEL");
    check(token_at(&tokens, 1)->kind == TC_TOK_IDENTIFIER, "label name → IDENTIFIER");
    check(token_at(&tokens, 2)->kind == TC_TOK_COLON, "label colon → TC_TOK_COLON");

    tc_token_list_free(&tokens);
    tc_token_list_init(&tokens);
    check(tc_tokenize_line(
              "while break continue whilex breakfast continued While BREAK Continue", 6,
              &tokens, &diag) == 0,
          "tokenize loop keywords and identifier boundaries");
    check(token_at(&tokens, 0)->kind == TC_TOK_WHILE, "while → TC_TOK_WHILE");
    check(token_at(&tokens, 1)->kind == TC_TOK_BREAK, "break → TC_TOK_BREAK");
    check(token_at(&tokens, 2)->kind == TC_TOK_CONTINUE, "continue → TC_TOK_CONTINUE");
    check(token_at(&tokens, 3)->kind == TC_TOK_IDENTIFIER, "whilex remains identifier");
    check(token_at(&tokens, 4)->kind == TC_TOK_IDENTIFIER, "breakfast remains identifier");
    check(token_at(&tokens, 5)->kind == TC_TOK_IDENTIFIER, "continued remains identifier");
    check(token_at(&tokens, 6)->kind == TC_TOK_IDENTIFIER, "While remains identifier");
    check(token_at(&tokens, 7)->kind == TC_TOK_IDENTIFIER, "BREAK remains identifier");
    check(token_at(&tokens, 8)->kind == TC_TOK_IDENTIFIER, "Continue remains identifier");

    check(strcmp(tc_token_kind_name(TC_TOK_IF), "IF") == 0,
          "tc_token_kind_name(TC_TOK_IF) → IF");
    check(strcmp(tc_token_kind_name(TC_TOK_THEN), "THEN") == 0,
          "tc_token_kind_name(TC_TOK_THEN) → THEN");
    check(strcmp(tc_token_kind_name(TC_TOK_ELSE), "ELSE") == 0,
          "tc_token_kind_name(TC_TOK_ELSE) → ELSE");
    check(strcmp(tc_token_kind_name(TC_TOK_END), "END") == 0,
          "tc_token_kind_name(TC_TOK_END) → END");
    check(strcmp(tc_token_kind_name(TC_TOK_GOTO), "GOTO") == 0,
          "tc_token_kind_name(TC_TOK_GOTO) → GOTO");
    check(strcmp(tc_token_kind_name(TC_TOK_LABEL), "LABEL") == 0,
          "tc_token_kind_name(TC_TOK_LABEL) → LABEL");
    check(strcmp(tc_token_kind_name(TC_TOK_WHILE), "WHILE") == 0,
          "tc_token_kind_name(TC_TOK_WHILE) → WHILE");
    check(strcmp(tc_token_kind_name(TC_TOK_BREAK), "BREAK") == 0,
          "tc_token_kind_name(TC_TOK_BREAK) → BREAK");
    check(strcmp(tc_token_kind_name(TC_TOK_CONTINUE), "CONTINUE") == 0,
          "tc_token_kind_name(TC_TOK_CONTINUE) → CONTINUE");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_float_format_specifiers(void) {
    static const struct {
        const char *spec;
        TcFormatSpec expected;
    } cases[] = {
        {"%f", TC_FMT_F}, {"%e", TC_FMT_E}, {"%E", TC_FMT_EU},
        {"%g", TC_FMT_G}, {"%G", TC_FMT_GU},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcTokenList tokens;
        TcDiagnostic diag;
        char line[64];
        size_t idx = 0;
        const TcToken *tok;

        snprintf(line, sizeof(line), "writeln(float64, %s, x)", cases[i].spec);
        tc_diagnostic_init(&diag);
        check(tokenize_line_ok(line, &tokens, &diag), line);
        tok = find_token(&tokens, TC_TOK_FORMAT_SPEC, &idx);
        check(tok != NULL && tok->u.format_spec == cases[i].expected,
              "float format spec token");
        tc_token_list_free(&tokens);
        tc_diagnostic_clear(&diag);
    }
}

static void test_float_literal_lex_errors(void) {
    TcTokenList tokens;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    check(tc_tokenize_line("var x: float64 = 3.14u", 1, &tokens, &diag) != 0,
          "3.14u → literal type error");
    check(diag.kind == TC_ERR_LITERAL_TYPE, "3.14u error kind");
    tc_token_list_free(&tokens);

    tc_token_list_init(&tokens);
    tc_diagnostic_clear(&diag);
    check(tc_tokenize_line("var x: float64 = .5", 1, &tokens, &diag) != 0,
          ".5 → syntax error");
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_scientific_float_literals(void) {
    TcTokenList tokens;
    TcDiagnostic diag;
    size_t idx = 0;
    const TcToken *tok;

    tc_diagnostic_init(&diag);
    check(tokenize_line_ok("var a: float64 = -3.14e-2", &tokens, &diag), "tokenize -3.14e-2");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && tok->u.literal.float_value == -0.0314, "-3.14e-2 value");

    idx = 0;
    tc_token_list_free(&tokens);
    check(tokenize_line_ok("var b: float64 = 1.2E+5", &tokens, &diag), "tokenize 1.2E+5");
    tok = find_token(&tokens, TC_TOK_FLOAT_LIT, &idx);
    check(tok != NULL && tok->u.literal.float_value == 120000.0, "1.2E+5 value");

    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_all_unary_ops();
    test_cast_keyword();
    test_bitcast_keyword();
    test_io_keywords();
    test_binary_literals();
    test_hex_literals();
    test_oct_literals();
    test_max_uint64_literal();
    test_format_specifiers();
    test_let_keyword();
    test_underscore_literals();
    test_wrap_truncate_keywords();
    test_bitwise_shift_tokens();
    test_control_flow_keywords();
    test_whitespace_and_comments();
    test_float_format_specifiers();
    test_float_literal_lex_errors();
    test_scientific_float_literals();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
