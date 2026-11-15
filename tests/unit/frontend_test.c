/* TC 0.0.46 S1 front-end unit tests (L1).
 *
 * These tests call the private LEX/SYN interfaces directly.  They pin:
 *   - exact decimal -> binary32/64 rounding (standard §2.4/§6.6.3),
 *   - token kinds, values and 1-based line/Unicode-scalar columns (§2/§12.5.1),
 *   - every LEX/SYN code with its primary position,
 *   - flat parse IR shape: field paths, named args, switch targets, sizeof type
 *     arguments, spans, unsafe markers, module regions and all 51 opcodes,
 *   - deterministic repeated parsing.
 *
 * The case ids are referenced by tests/traceability/coverage.generated.json.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "frontend/input.h"
#include "frontend/lex.h"
#include "frontend/num.h"
#include "frontend/parse.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        long long va_ = (long long)(a);                                        \
        long long vb_ = (long long)(b);                                        \
        checks++;                                                              \
        if (va_ != vb_) {                                                      \
            printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, \
                   #a, #b, va_, vb_);                                          \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_STR(a, b)                                                        \
    do {                                                                       \
        const char *sa_ = (a);                                                 \
        const char *sb_ = (b);                                                 \
        checks++;                                                              \
        if (sa_ == NULL || sb_ == NULL || strcmp(sa_, sb_) != 0) {             \
            printf("FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__,    \
                   #a, #b, sa_ ? sa_ : "<null>", sb_ ? sb_ : "<null>");        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Helpers.                                                                  */
/* ------------------------------------------------------------------------- */

static TcParseModule *parse_ok(const char *source, TcFrontendDiag *diag,
                               TcImplError *impl) {
    TcParseModule *module = NULL;
    int rc = tc_parse_source("test.tc", (const uint8_t *)source,
                             strlen(source), &module, diag, impl);
    if (rc != 0) {
        printf("FAIL parse implementation error %d\n", (int)*impl);
        failures++;
        return NULL;
    }
    return module;
}

static void expect_diag_bytes(const uint8_t *bytes, size_t size, TcStage stage,
                              TcDiagCode code, uint32_t line, uint32_t column,
                              const char *label) {
    TcParseModule *module = NULL;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    int rc = tc_parse_source("test.tc", bytes, size, &module, &diag, &impl);
    checks++;
    if (rc != 0) {
        printf("FAIL %s: implementation error %d\n", label, (int)impl);
        failures++;
        return;
    }
    if (module != NULL) {
        printf("FAIL %s: expected diagnostic %s at %u:%u, parsed instead\n",
               label, tc_diag_info(code)->symbol, line, column);
        tc_parse_module_destroy(module);
        failures++;
        return;
    }
    if (diag.code != code || diag.stage != stage || diag.span.line != line ||
        diag.span.column != column) {
        printf("FAIL %s: expected %s/%d at %u:%u, got %s/%d at %u:%u\n", label,
               tc_diag_info(code)->symbol, (int)stage, line, column,
               tc_diag_info(diag.code)->symbol, (int)diag.stage, diag.span.line,
               diag.span.column);
        failures++;
    }
}

static void expect_diag(const char *source, TcStage stage, TcDiagCode code,
                        uint32_t line, uint32_t column, const char *label) {
    expect_diag_bytes((const uint8_t *)source, strlen(source), stage, code,
                      line, column, label);
}

static void expect_ok(const char *source, const char *label) {
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcParseModule *module = parse_ok(source, &diag, &impl);
    checks++;
    if (module == NULL) {
        printf("FAIL %s: expected LEX/SYN acceptance, got %s at %u:%u\n", label,
               tc_diag_info(diag.code)->symbol, diag.span.line,
               diag.span.column);
        failures++;
        return;
    }
    tc_parse_module_destroy(module);
}

static TcLexResult lex_ok(const char *source, TcArena *arena, int *ok) {
    TcLexResult result;
    memset(&result, 0, sizeof(result));
    *ok = tc_lex("test.tc", (const uint8_t *)source, strlen(source), arena,
                 &result) == 0 &&
          !result.has_diag;
    return result;
}

static const char *k_simple_program =
    "#program\n"
    "import util\n"
    "struct S then\n"
    "    readonly x: int32\n"
    "    var y: float64 @padding(4)\n"
    "end\n"
    "const K: int32 = 42\n"
    "var s: S = make S, x: 1, y: 2.5\n"
    "var n: usize = sizeof ptr<int32>\n"
    "label L:\n"
    "s.y = copy float64, 3.5\n"
    "_ = br_if s.x, L\n"
    "unsafe then\n"
    "    _ = trap\n"
    "end\n";

/* ------------------------------------------------------------------------- */
/* TC-UNIT-NUM: exact decimal conversion.                                    */
/* ------------------------------------------------------------------------- */

static void check_float(const char *digits, int exp10, int negative,
                        unsigned width, uint64_t want, TcFloatRange range,
                        const char *label) {
    uint64_t bits = 0;
    TcFloatRange got_range = TC_FLOAT_OK;
    int rc = tc_decimal_to_float(digits, strlen(digits), exp10, negative, width,
                                 &bits, &got_range);
    checks++;
    if (rc != 0 || bits != want || got_range != range) {
        printf("FAIL %s: rc=%d bits=%016llx range=%d want=%016llx/%d\n", label,
               rc, (unsigned long long)bits, (int)got_range,
               (unsigned long long)want, (int)range);
        failures++;
    }
}

static void test_num(void) {
    check_float("0", 0, 0, 64, 0x0000000000000000ull, TC_FLOAT_OK, "zero");
    check_float("0", 0, 1, 64, 0x8000000000000000ull, TC_FLOAT_OK, "neg zero");
    check_float("1", 0, 0, 64, 0x3ff0000000000000ull, TC_FLOAT_OK, "1");
    check_float("15", -1, 0, 64, 0x3ff8000000000000ull, TC_FLOAT_OK, "1.5");
    check_float("1", -1, 0, 64, 0x3fb999999999999aull, TC_FLOAT_OK, "0.1");
    check_float("3", 22, 0, 64, 0x449969368974c05bull, TC_FLOAT_OK, "3e22 exact");
    check_float("1", 308, 0, 64, 0x7fe1ccf385ebc8a0ull, TC_FLOAT_OK, "1e308");
    check_float("1", 309, 0, 64, 0x7ff0000000000000ull,
                TC_FLOAT_ROUNDED_TO_INF, "1e309");
    check_float("5", -324, 0, 64, 0x0000000000000001ull, TC_FLOAT_OK,
                "5e-324 min subnormal");
    check_float("24", -325, 0, 64, 0x0000000000000000ull,
                TC_FLOAT_ROUNDED_TO_ZERO, "2.4e-324 zero");
    check_float("25", -325, 0, 64, 0x0000000000000001ull, TC_FLOAT_OK,
                "2.5e-324 one");
    /* exact midpoint 1 + 2^-53 rounds ties-to-even to 1.0 */
    check_float("100000000000000011102230246251565404236316680908203125", -53,
                0, 64, 0x3ff0000000000000ull, TC_FLOAT_OK, "tie down");
    check_float("100000000000000033306690738754696212708950042724609375", -53,
                0, 64, 0x3ff0000000000002ull, TC_FLOAT_OK, "tie up");
    check_float("1", 0, 0, 32, 0x3f800000ull, TC_FLOAT_OK, "f32 1");
    check_float("1", -1, 0, 32, 0x3dcccccdull, TC_FLOAT_OK, "f32 0.1");
    check_float("34028235", 31, 0, 32, 0x7f7fffffull, TC_FLOAT_OK, "f32 max");
    check_float("34028236", 31, 0, 32, 0x7f800000ull,
                TC_FLOAT_ROUNDED_TO_INF, "f32 inf");
    check_float("14", -46, 0, 32, 0x00000001ull, TC_FLOAT_OK, "f32 min sub");
    check_float("7", -46, 0, 32, 0x00000000ull, TC_FLOAT_ROUNDED_TO_ZERO,
                "f32 below half");
    check_float("8", -46, 0, 32, 0x00000001ull, TC_FLOAT_OK, "f32 above half");
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-LEX: tokens, literal values and positions.                        */
/* ------------------------------------------------------------------------- */

static TcToken *find_token(TcLexResult *result, size_t index) {
    if (index >= result->count) {
        return NULL;
    }
    return &result->tokens[index];
}

static void test_lex_tokens(void) {
    TcArena arena;
    TcLexResult result;
    int ok = 0;
    tc_arena_init(&arena, NULL);
    result = lex_ok(
        "#program\n"
        "var _x: int32 = copy int32, -0x1F\n"
        "var __: uint32 = copy uint32, 1_000u\n"
        "var f: float32 = copy float32, 0.5f\n"
        "var g: float64 = copy float64, -0.0\n"
        "_ = br_if true, L\n"
        "@padding @unsafe @readonly\n",
        &arena, &ok);
    CHECK(ok);
    if (ok) {
        size_t i = 0;
        TcToken *token;
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_PROGRAM);
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_NL);
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_VAR);
        token = find_token(&result, i++);
        CHECK_EQ(token->kind, TC_TOK_IDENT);
        CHECK_STR(token->text, "_x");
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_COLON);
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_INT32);
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_EQ);
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_MNEMONIC);
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_INT32);
        CHECK_EQ(find_token(&result, i++)->kind, TC_TOK_COMMA);
        token = find_token(&result, i++);
        CHECK_EQ(token->kind, TC_TOK_INT);
        CHECK_EQ(token->u.integer.magnitude, 31u);
        CHECK_EQ(token->u.integer.negative, 1);
        CHECK_EQ(token->span.line, 2);
        /* find the 1_000u literal */
        for (; i < result.count; i++) {
            token = find_token(&result, i);
            if (token->kind == TC_TOK_INT && token->span.line == 3) {
                break;
            }
        }
        CHECK(token != NULL && token->kind == TC_TOK_INT);
        if (token != NULL) {
            CHECK_EQ(token->u.integer.magnitude, 1000u);
            CHECK_EQ(token->u.integer.has_unsigned_suffix, 1);
        }
        /* float32 0.5f */
        for (; i < result.count; i++) {
            token = find_token(&result, i);
            if (token->kind == TC_TOK_FLOAT && token->span.line == 4) {
                break;
            }
        }
        CHECK(token != NULL && token->kind == TC_TOK_FLOAT);
        if (token != NULL) {
            CHECK_EQ(token->u.flt.bits, 0x3f000000ull);
            CHECK_EQ(token->u.flt.width, 32);
        }
        /* -0.0 keeps the sign */
        for (; i < result.count; i++) {
            token = find_token(&result, i);
            if (token->kind == TC_TOK_FLOAT && token->span.line == 5) {
                break;
            }
        }
        CHECK(token != NULL && token->kind == TC_TOK_FLOAT);
        if (token != NULL) {
            CHECK_EQ(token->u.flt.bits, 0x8000000000000000ull);
            CHECK_EQ(token->u.flt.width, 64);
        }
        /* annotation names: padding / unsafe / readonly */
        {
            int seen[3] = {0, 0, 0};
            for (i = 0; i < result.count; i++) {
                token = find_token(&result, i);
                if (token->kind != TC_TOK_ANNOTATION) {
                    continue;
                }
                if (strcmp(token->text, "padding") == 0) seen[0] = 1;
                if (strcmp(token->text, "unsafe") == 0) seen[1] = 1;
                if (strcmp(token->text, "readonly") == 0) seen[2] = 1;
            }
            CHECK(seen[0] && seen[1] && seen[2]);
        }
    }
    tc_arena_destroy(&arena);
}

static void test_lex_mnemonics(void) {
    static const struct {
        const char *text;
        TcOpcode op;
        TcOpMode mode;
        int valid;
    } cases[] = {
        {"add", TC_ADD, TC_STRICT, 1},
        {"add.wrap", TC_ADD, TC_WRAP, 1},
        {"add.ieee", TC_ADD, TC_IEEE, 1},
        {"add.nsw", TC_ADD, TC_NSW, 1},
        {"add.nuw", TC_ADD, TC_NUW, 1},
        {"sub.wrap", TC_SUB, TC_WRAP, 1},
        {"sub.ieee", TC_SUB, TC_IEEE, 1},
        {"sub.nsw", TC_SUB, TC_NSW, 1},
        {"sub.nuw", TC_SUB, TC_NUW, 1},
        {"mul.wrap", TC_MUL, TC_WRAP, 1},
        {"mul.ieee", TC_MUL, TC_IEEE, 1},
        {"mul.nsw", TC_MUL, TC_NSW, 1},
        {"mul.nuw", TC_MUL, TC_NUW, 1},
        {"div.ieee", TC_DIV, TC_IEEE, 1},
        {"mod.ieee", TC_MOD, TC_IEEE, 1},
        {"neg.wrap", TC_NEG, TC_WRAP, 1},
        {"shl.wrap", TC_SHL, TC_WRAP, 1},
        {"shl.nsw", TC_SHL, TC_NSW, 1},
        {"shl.nuw", TC_SHL, TC_NUW, 1},
        {"shr", TC_SHR, TC_STRICT, 1},
        {"load.volatile", TC_LOAD, TC_VOLATILE, 1},
        {"store.volatile", TC_STORE, TC_VOLATILE, 1},
        {"call_indirect", TC_CALL_INDIRECT, TC_STRICT, 1},
        {"funcaddr", TC_FUNCADDR, TC_STRICT, 1},
        {"div.wrap", TC_DIV, TC_STRICT, 0},
        {"load.wrap", TC_LOAD, TC_STRICT, 0},
        {"br.wrap", TC_BR, TC_STRICT, 0},
        {"neg.ieee", TC_NEG, TC_STRICT, 0},
        {"add.bogus", TC_ADD, TC_STRICT, 0},
        {"add._x", TC_ADD, TC_STRICT, 0},
        {"add.", TC_ADD, TC_STRICT, 0},
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcArena arena;
        TcLexResult result;
        int ok = 0;
        char source[64];
        snprintf(source, sizeof(source), "%s\n", cases[i].text);
        tc_arena_init(&arena, NULL);
        result = lex_ok(source, &arena, &ok);
        checks++;
        if (!ok || result.count < 1 ||
            result.tokens[0].kind != TC_TOK_MNEMONIC) {
            printf("FAIL mnemonic %s did not lex as a mnemonic\n",
                   cases[i].text);
            failures++;
        } else {
            CHECK_EQ(result.tokens[0].u.mnemonic.op, cases[i].op);
            CHECK_EQ(result.tokens[0].u.mnemonic.mode, cases[i].mode);
            CHECK_EQ(result.tokens[0].u.mnemonic.combo_valid, cases[i].valid);
        }
        tc_arena_destroy(&arena);
    }
}

static void test_lex_positions(void) {
    TcArena arena;
    TcLexResult result;
    int ok = 0;
    tc_arena_init(&arena, NULL);
    result = lex_ok("#program\r\nvar x: int32 = copy int32, 1\r\n"
                    "/// 注释 ✓\r\n_ = trap\r\n",
                    &arena, &ok);
    CHECK(ok);
    if (ok) {
        size_t i;
        int saw_line4 = 0;
        for (i = 0; i < result.count; i++) {
            TcToken *token = find_token(&result, i);
            if (token->kind == TC_TOK_DISCARD && token->span.line == 4) {
                saw_line4 = 1;
                CHECK_EQ(token->span.column, 1);
            }
        }
        CHECK(saw_line4);
        /* the Unicode comment must not produce tokens or diagnostics */
        for (i = 0; i < result.count; i++) {
            CHECK(find_token(&result, i)->span.line != 3);
        }
    }
    tc_arena_destroy(&arena);

    tc_arena_init(&arena, NULL);
    result = lex_ok("#program\nvar x: int32 = copy int32, \\\n    1\n",
                    &arena, &ok);
    CHECK(ok);
    if (ok) {
        int saw_int = 0;
        size_t i;
        for (i = 0; i < result.count; i++) {
            TcToken *token = find_token(&result, i);
            if (token->kind == TC_TOK_INT) {
                saw_int = 1;
                CHECK_EQ(token->span.line, 3);
                CHECK_EQ(token->span.column, 5);
            }
        }
        CHECK(saw_int);
    }
    tc_arena_destroy(&arena);
}

static void test_lex_errors(void) {
    expect_diag_bytes((const uint8_t *)"\xef\xbb\xbf#program\n",
                      sizeof("\xef\xbb\xbf#program\n") - 1, TC_STAGE_LEX,
                      TC_CE_SYNTAX, 1, 1, "BOM");
    expect_diag_bytes((const uint8_t *)"#program\nvar x: int32 = copy int32, 1 \x00\n",
                      sizeof("#program\nvar x: int32 = copy int32, 1 \x00\n") - 1,
                      TC_STAGE_LEX, TC_CE_SYNTAX, 2, 30, "U+0000");
    expect_diag_bytes((const uint8_t *)"#program\nvar x: int32 = 1\n\xff\n",
                      sizeof("#program\nvar x: int32 = 1\n\xff\n") - 1,
                      TC_STAGE_LEX, TC_CE_SYNTAX, 3, 1, "invalid FF");
    expect_diag_bytes((const uint8_t *)"#program\nvar x: int32 = 1\n\xc3\x28\n",
                      sizeof("#program\nvar x: int32 = 1\n\xc3\x28\n") - 1,
                      TC_STAGE_LEX, TC_CE_SYNTAX, 3, 2, "invalid C3 28");
    expect_diag_bytes((const uint8_t *)"#program\nvar x: int32 = 1\n\xe2\x80\n",
                      sizeof("#program\nvar x: int32 = 1\n\xe2\x80\n") - 1,
                      TC_STAGE_LEX, TC_CE_SYNTAX, 3, 3, "truncated E2 80");
    expect_diag("#program\nvar x: int32 = copy int32, 7 // c\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "// outside comment");
    expect_diag("#program\nvar x: int32 = copy int32, 7 ;\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "semicolon");
    expect_diag("#program\nvar x: int32 = copy int32, 7 % 2\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "percent");
    expect_diag("#program\nvar x: int32 = copy int32, 7 / 2\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "single slash");
    expect_diag("#program\nvar x: int32 = copy int32, 7 /* c */\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "block comment");
    expect_diag("#program\nvar x: int32 = copy int32, 7 #\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "plain hash");
    expect_diag("#program\nvar x: int32 = copy int32, 7 \\ 2\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "backslash mid");
    expect_diag("#program\nvar x: int32 = copy int32, 7 \\ \n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "backslash space");
    expect_diag("#program\nvar x: int32 = copy int32, 7 \\", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "backslash eof");
    expect_diag("#program\nvar x: int32 = copy int32, 7 @var\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "at keyword");
    expect_diag("#program\nvar x: int32 = copy int32, 7 +\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "plus");
    expect_diag("#program\nvar x: float64 = copy float64, -nan\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 32, "minus nan");
    expect_diag("#program\nvar x: int32 = copy int32, 007\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 28, "leading zero");
    expect_diag("#program\nvar x: int32 = copy int32, 0_0\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 28, "underscore zero");
    expect_diag("#program\nvar x: float64 = copy float64, 1.\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 32, "trailing dot");
    /* Malformed numeric candidates are one illegal numeric token anchored at
     * the candidate start (documented S1 recovery convention; TC-UNIT-LEX-0005). */
    expect_diag("#program\nvar x: float64 = copy float64, 1.5.5\n",
                TC_STAGE_LEX, TC_CE_SYNTAX, 2, 32, "multi-dot float");
    expect_diag("#program\nvar x: float64 = copy float64, 1..5\n",
                TC_STAGE_LEX, TC_CE_SYNTAX, 2, 32, "empty fraction");
    expect_diag("#program\nvar x: float64 = copy float64, 1.5e5.5\n",
                TC_STAGE_LEX, TC_CE_SYNTAX, 2, 32, "dot after exponent");
    expect_diag("#program\nvar x: uint32 = copy uint32, -42u\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "negative unsigned");
    expect_diag("#program\nvar x: uint64 = copy uint64, 18446744073709551616\n",
                TC_STAGE_LEX, TC_CE_LITERAL_LEX_RANGE, 2, 30, "int range");
    expect_diag("#program\nvar x: int64 = copy int64, -18446744073709551616\n",
                TC_STAGE_LEX, TC_CE_LITERAL_LEX_RANGE, 2, 28, "int range neg");
    expect_diag("#program\nvar x: float64 = copy float64, 1e400\n", TC_STAGE_LEX,
                TC_CE_LITERAL_LEX_RANGE, 2, 32, "float inf");
    expect_diag("#program\nvar x: float64 = copy float64, 1e-400\n", TC_STAGE_LEX,
                TC_CE_LITERAL_LEX_RANGE, 2, 32, "float zero");
    expect_diag("#program\nvar x: float32 = copy float32, 1e40f\n", TC_STAGE_LEX,
                TC_CE_LITERAL_LEX_RANGE, 2, 32, "float32 inf");
    expect_diag("#program\nvar x: int32 = copy int32, 1 é\n", TC_STAGE_LEX,
                TC_CE_SYNTAX, 2, 30, "non-ASCII");
    expect_diag_bytes((const uint8_t *)"#program\n/// c \x00\n",
                      sizeof("#program\n/// c \x00\n") - 1, TC_STAGE_LEX,
                      TC_CE_SYNTAX, 2, 7, "U+0000 in comment");
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-LEX-0004: the LEX stage never reports a SEM/CT code.             */
/* ------------------------------------------------------------------------- */

static void test_lex_code_set(void) {
    static const char *bad[] = {
        "var x: int32 = 1 ;\n",
        "var x: int32 = 1 % 2\n",
        "var x: int32 = 1 // c\n",
        "var x: int32 = 1 \\ 2\n",
        "var x: int32 = 007\n",
        "var x: int32 = 18446744073709551616\n",
        "var x: float64 = 1e400\n",
        "var x: float64 = -nan\n",
        "#program\n\xef\xbb\xbf",
        "#program\nÃ\x28\n",
    };
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TcArena arena;
        TcLexResult result;
        size_t size = strlen(bad[i]);
        tc_arena_init(&arena, NULL);
        if (tc_lex("test.tc", (const uint8_t *)bad[i], size, &arena,
                   &result) != 0) {
            CHECK(0);
        } else {
            CHECK(result.has_diag);
            if (result.has_diag) {
                CHECK_EQ(result.diag.stage, TC_STAGE_LEX);
                CHECK(result.diag.code == TC_CE_SYNTAX ||
                      result.diag.code == TC_CE_LITERAL_LEX_RANGE);
            }
        }
        tc_arena_destroy(&arena);
    }
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-SYN: parse IR shape.                                              */
/* ------------------------------------------------------------------------- */

static TcParseStmt *stmt_at(TcParseModule *module, size_t index) {
    if (module == NULL || index >= module->nbody) {
        return NULL;
    }
    return &module->body[index];
}

static void test_parse_program_shape(void) {
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcParseModule *module = parse_ok(k_simple_program, &diag, &impl);
    CHECK(module != NULL);
    if (module == NULL) {
        return;
    }
    CHECK_EQ(module->is_program, 1);
    CHECK_EQ(module->nimports, 1);
    CHECK_STR(module->imports[0].name, "util");
    CHECK_EQ(module->imports[0].name_span.line, 2);
    CHECK_EQ(module->imports[0].name_span.column, 8);
    CHECK_EQ(module->nstructs, 1);
    CHECK_STR(module->structs[0].name, "S");
    CHECK_EQ(module->structs[0].nfields, 2);
    CHECK_EQ(module->structs[0].fields[0].readonly, 1);
    CHECK_EQ(module->structs[0].fields[0].type.kind, TC_PTYPE_SCALAR);
    CHECK_EQ(module->structs[0].fields[0].type.scalar, TC_TY_I32);
    CHECK_EQ(module->structs[0].fields[1].has_padding, 1);
    CHECK_STR(module->structs[0].fields[1].padding.name, "padding");
    CHECK_EQ(module->structs[0].fields[1].padding.nargs, 1);
    CHECK_EQ(module->structs[0].fields[1].padding.args[0].int_magnitude, 4u);
    CHECK_EQ(module->nbody, 9);
    {
        TcParseStmt *stmt = stmt_at(module, 0); /* const K */
        CHECK(stmt != NULL && stmt->kind == TC_STMT_CONST);
        CHECK_STR(stmt->cnst.name, "K");
        CHECK_EQ(stmt->cnst.init.is_op, 0);
        CHECK_EQ(stmt->cnst.init.value.kind, TC_PVAL_INT);
        CHECK(stmt->cnst.init.value.int_magnitude == 42u);
    }
    {
        TcParseStmt *stmt = stmt_at(module, 1); /* var s = make S, x: 1, y: 2.5 */
        CHECK(stmt != NULL && stmt->kind == TC_STMT_VAR);
        CHECK_STR(stmt->var.name, "s");
        CHECK_EQ(stmt->var.type.kind, TC_PTYPE_NAME);
        CHECK_EQ(stmt->var.init.op, TC_MAKE);
        CHECK_EQ(stmt->var.init.nargs, 3);
        CHECK_STR(stmt->var.init.args[0].value.base, "S");
        CHECK_STR(stmt->var.init.args[1].name, "x");
        CHECK_STR(stmt->var.init.args[2].name, "y");
        CHECK_EQ(stmt->var.init.args[2].value.kind, TC_PVAL_FLOAT);
    }
    {
        TcParseStmt *stmt = stmt_at(module, 2); /* sizeof ptr<int32> */
        CHECK(stmt != NULL && stmt->kind == TC_STMT_VAR);
        CHECK_EQ(stmt->var.init.op, TC_SIZEOF);
        CHECK_EQ(stmt->var.init.nargs, 1);
        CHECK_EQ(stmt->var.init.args[0].kind, TC_PARG_TYPE);
        CHECK_EQ(stmt->var.init.args[0].type.kind, TC_PTYPE_PTR);
        CHECK_EQ(stmt->var.init.args[0].type.pointee->scalar, TC_TY_I32);
    }
    {
        TcParseStmt *stmt = stmt_at(module, 3); /* label L */
        CHECK(stmt != NULL && stmt->kind == TC_STMT_LABEL);
        CHECK_STR(stmt->label.name, "L");
    }
    {
        TcParseStmt *stmt = stmt_at(module, 4); /* s.y = copy float64, 3.5 */
        CHECK(stmt != NULL && stmt->kind == TC_STMT_INSTR);
        CHECK_EQ(stmt->instr.dst_discard, 0);
        CHECK_STR(stmt->instr.dst_base, "s");
        CHECK_EQ(stmt->instr.dst_base_span.line, 11);
        CHECK_EQ(stmt->instr.dst_base_span.column, 1);
        CHECK_EQ(stmt->instr.dst_nfields, 1);
        CHECK_STR(stmt->instr.dst_fields[0], "y");
        CHECK_EQ(stmt->instr.dst_field_spans[0].line, 11);
        CHECK_EQ(stmt->instr.dst_field_spans[0].column, 3);
        CHECK_EQ(stmt->instr.op, TC_COPY);
        CHECK_EQ(stmt->instr.span.line, 11);
        CHECK_EQ(stmt->instr.span.column, 7);
        CHECK_EQ(stmt->instr.end_span.line, 11);
    }
    {
        TcParseStmt *stmt = stmt_at(module, 5); /* br_if s.x, L */
        CHECK(stmt != NULL && stmt->kind == TC_STMT_INSTR);
        CHECK_EQ(stmt->instr.op, TC_BR_IF);
        CHECK_EQ(stmt->instr.args[0].value.kind, TC_PVAL_NAME);
        CHECK_STR(stmt->instr.args[0].value.base, "s");
        CHECK_EQ(stmt->instr.args[0].value.nfields, 1);
        CHECK_STR(stmt->instr.args[0].value.fields[0], "x");
        CHECK_EQ(stmt->instr.args[0].value.field_spans[0].line, 12);
        CHECK_EQ(stmt->instr.args[0].value.field_spans[0].column, 13);
        CHECK_STR(stmt->instr.args[1].value.base, "L");
    }
    tc_parse_module_destroy(module);
}

static void test_unsafe_depth(void) {
    const char *source =
        "#program\n"
        "unsafe then\n"
        "    var x: int32 = copy int32, 1\n"
        "    unsafe then\n"
        "        _ = trap\n"
        "    end\n"
        "end\n";
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcParseModule *module = parse_ok(source, &diag, &impl);
    CHECK(module != NULL);
    if (module == NULL) {
        return;
    }
    CHECK_EQ(module->nbody, 6);
    CHECK_EQ(module->body[0].kind, TC_STMT_UNSAFE_BEGIN);
    CHECK_EQ(module->body[0].unsafe_depth, 0);
    CHECK_EQ(module->body[1].kind, TC_STMT_VAR);
    CHECK_EQ(module->body[1].unsafe_depth, 1);
    CHECK_EQ(module->body[2].kind, TC_STMT_UNSAFE_BEGIN);
    CHECK_EQ(module->body[2].unsafe_depth, 1);
    CHECK_EQ(module->body[3].kind, TC_STMT_INSTR);
    CHECK_EQ(module->body[3].unsafe_depth, 2);
    CHECK_EQ(module->body[4].kind, TC_STMT_UNSAFE_END);
    CHECK_EQ(module->body[4].unsafe_depth, 1);
    CHECK_EQ(module->body[5].kind, TC_STMT_UNSAFE_END);
    CHECK_EQ(module->body[5].unsafe_depth, 0);
    tc_parse_module_destroy(module);
}

static void test_lib_shape(void) {
    const char *source =
        "#lib\n"
        "import util\n"
        "public struct S then\n"
        "    readonly x: int32\n"
        "end\n"
        "private const K: int32 = 1\n"
        "public static var counter: usize = 0\n"
        "public @fast func f(@nonnull p: ptr<int32>, n: usize) int32 then\n"
        "    var v: int32 = load int32, p\n"
        "    _ = return v\n"
        "end\n";
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcParseModule *module = parse_ok(source, &diag, &impl);
    CHECK(module != NULL);
    if (module == NULL) {
        return;
    }
    CHECK_EQ(module->is_program, 0);
    CHECK_EQ(module->nstructs, 1);
    CHECK_EQ(module->nconsts, 2);
    CHECK_EQ(module->nfuncs, 1);
    CHECK_STR(module->imports[0].name, "util");
    CHECK_EQ(module->imports[0].name_span.line, 2);
    CHECK_EQ(module->imports[0].name_span.column, 8);
    CHECK_EQ(module->funcs[0].is_public, 1);
    CHECK_EQ(module->funcs[0].nannotations, 1);
    CHECK_STR(module->funcs[0].annotations[0].name, "fast");
    CHECK_EQ(module->funcs[0].nparams, 2);
    CHECK_STR(module->funcs[0].params[0].name, "p");
    CHECK_EQ(module->funcs[0].params[0].nannotations, 1);
    CHECK_STR(module->funcs[0].params[0].annotations[0].name, "nonnull");
    CHECK_EQ(module->funcs[0].ret.kind, TC_PTYPE_SCALAR);
    CHECK_EQ(module->funcs[0].nbody, 2);
    CHECK_EQ(module->funcs[0].body[0].kind, TC_STMT_VAR);
    CHECK_EQ(module->funcs[0].body[0].var.init.op, TC_LOAD);
    CHECK_EQ(module->funcs[0].body[1].instr.op, TC_RETURN);
    CHECK(module->consts[0].is_static == 0);
    CHECK(module->consts[1].is_static == 1);
    {
        const char *qualified =
            "#lib\n"
            "import util\n"
            "public struct S then\n"
            "    readonly x: util.T\n"
            "end\n";
        TcFrontendDiag qdiag;
        TcImplError qimpl = TC_IMPL_OK;
        TcParseModule *qm = parse_ok(qualified, &qdiag, &qimpl);
        CHECK(qm != NULL);
        if (qm != NULL) {
            CHECK_STR(qm->structs[0].fields[0].type.base, "util");
            CHECK_STR(qm->structs[0].fields[0].type.member, "T");
            CHECK_EQ(qm->structs[0].fields[0].type.member_span.line, 4);
            CHECK_EQ(qm->structs[0].fields[0].type.member_span.column, 22);
            tc_parse_module_destroy(qm);
        }
    }
    tc_parse_module_destroy(module);
}

static void test_switch_targets(void) {
    const char *source =
        "#program\n"
        "var x: int32 = copy int32, 0\n"
        "_ = switch x, L0, L1, L2\n"
        "label L0:\n"
        "label L1:\n"
        "label L2:\n";
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcParseModule *module = parse_ok(source, &diag, &impl);
    CHECK(module != NULL);
    if (module == NULL) {
        return;
    }
    CHECK_EQ(module->nbody, 5);
    CHECK_EQ(module->body[1].kind, TC_STMT_INSTR);
    CHECK_EQ(module->body[1].instr.op, TC_SWITCH);
    CHECK_EQ(module->body[1].instr.nargs, 4);
    /* first operand is the switch value, the rest are labels in order */
    CHECK_STR(module->body[1].instr.args[0].value.base, "x");
    CHECK_STR(module->body[1].instr.args[1].value.base, "L0");
    CHECK_STR(module->body[1].instr.args[3].value.base, "L2");
    CHECK_EQ(module->body[1].instr.ntargets, 3);
    CHECK_STR(module->body[1].instr.switch_targets[0].base, "L0");
    CHECK_STR(module->body[1].instr.switch_targets[1].base, "L1");
    CHECK_STR(module->body[1].instr.switch_targets[2].base, "L2");
    tc_parse_module_destroy(module);
    {
        const char *one = "#program\nvar x: int32 = copy int32, 0\n"
                          "_ = switch x\n";
        TcFrontendDiag odiag;
        TcImplError oimpl = TC_IMPL_OK;
        TcParseModule *om = parse_ok(one, &odiag, &oimpl);
        CHECK(om != NULL);
        if (om != NULL) {
            CHECK_EQ(om->body[1].instr.ntargets, 0);
            CHECK(om->body[1].instr.switch_targets == NULL);
            tc_parse_module_destroy(om);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-OPCODE: all 51 opcodes and every defined suffix.                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *line;
    TcOpcode op;
    TcOpMode mode;
} TcOpcodeCase;

static const TcOpcodeCase k_opcode_cases[] = {
    {"r01 = add int32, a, b", TC_ADD, TC_STRICT},
    {"r02 = add.wrap int32, a, b", TC_ADD, TC_WRAP},
    {"r03 = add.ieee float64, x, y", TC_ADD, TC_IEEE},
    {"r04 = add.nsw int32, a, b", TC_ADD, TC_NSW},
    {"r05 = add.nuw int32, a, b", TC_ADD, TC_NUW},
    {"r06 = sub int32, a, b", TC_SUB, TC_STRICT},
    {"r07 = mul.wrap int32, a, b", TC_MUL, TC_WRAP},
    {"r08 = mul.ieee float64, x, y", TC_MUL, TC_IEEE},
    {"r09 = div int32, a, b", TC_DIV, TC_STRICT},
    {"r10 = div.ieee float64, x, y", TC_DIV, TC_IEEE},
    {"r11 = mod int32, a, b", TC_MOD, TC_STRICT},
    {"r12 = mod.ieee float64, x, y", TC_MOD, TC_IEEE},
    {"r13 = neg int32, a", TC_NEG, TC_STRICT},
    {"r14 = neg.wrap int32, a", TC_NEG, TC_WRAP},
    {"r15 = abs int32, a", TC_ABS, TC_STRICT},
    {"r16 = shl int32, a, b", TC_SHL, TC_STRICT},
    {"r17 = shl.wrap int32, a, b", TC_SHL, TC_WRAP},
    {"r18 = shl.nsw int32, a, b", TC_SHL, TC_NSW},
    {"r19 = shl.nuw int32, a, b", TC_SHL, TC_NUW},
    {"r20 = shr int32, a, b", TC_SHR, TC_STRICT},
    {"r21 = and int32, a, b", TC_AND, TC_STRICT},
    {"r22 = or int32, a, b", TC_OR, TC_STRICT},
    {"r23 = xor int32, a, b", TC_XOR, TC_STRICT},
    {"r24 = not bool, c", TC_NOT, TC_STRICT},
    {"r25 = eq int32, a, b", TC_EQ, TC_STRICT},
    {"r26 = ne int32, a, b", TC_NE, TC_STRICT},
    {"r27 = lt int32, a, b", TC_LT, TC_STRICT},
    {"r28 = le int32, a, b", TC_LE, TC_STRICT},
    {"r29 = gt int32, a, b", TC_GT, TC_STRICT},
    {"r30 = ge int32, a, b", TC_GE, TC_STRICT},
    {"r31 = cast int64, a", TC_CAST, TC_STRICT},
    {"r32 = trunc int8, a", TC_TRUNC, TC_STRICT},
    {"r33 = bitcast uint32, a", TC_BITCAST, TC_STRICT},
    {"r34 = load int32, p", TC_LOAD, TC_STRICT},
    {"r35 = load.volatile int32, p", TC_LOAD, TC_VOLATILE},
    {"_ = store int32, p, a", TC_STORE, TC_STRICT},
    {"_ = store.volatile int32, p, a", TC_STORE, TC_VOLATILE},
    {"_ = memmove int32, p, q, n", TC_MEMMOVE, TC_STRICT},
    {"r36 = addr int32, v", TC_ADDR, TC_STRICT},
    {"r37 = ptradd int32, p, n", TC_PTRADD, TC_STRICT},
    {"r38 = ptrsub int32, p, n", TC_PTRSUB, TC_STRICT},
    {"r39 = ptrdiff int32, p, q", TC_PTRDIFF, TC_STRICT},
    {"r40 = ptreq int32, p, q", TC_PTREQ, TC_STRICT},
    {"r41 = ptrne int32, p, q", TC_PTRNE, TC_STRICT},
    {"r42 = ptrlt int32, p, q", TC_PTRLT, TC_STRICT},
    {"r43 = ptrle int32, p, q", TC_PTRLE, TC_STRICT},
    {"r44 = ptrgt int32, p, q", TC_PTRGT, TC_STRICT},
    {"r45 = ptrge int32, p, q", TC_PTRGE, TC_STRICT},
    {"r46 = sizeof int32", TC_SIZEOF, TC_STRICT},
    {"r47 = funcaddr Self.f", TC_FUNCADDR, TC_STRICT},
    {"r48 = call Self.f, 1", TC_CALL, TC_STRICT},
    {"r49 = call_indirect fp, 1", TC_CALL_INDIRECT, TC_STRICT},
    {"r50 = funceq fnptr<int32>, fp, fq", TC_FUNCEQ, TC_STRICT},
    {"r51 = funcne fnptr<int32>, fp, fq", TC_FUNCNE, TC_STRICT},
    {"r52 = make S, 1", TC_MAKE, TC_STRICT},
    {"r53 = copy int32, a", TC_COPY, TC_STRICT},
    {"_ = br L", TC_BR, TC_STRICT},
    {"_ = br_if c, L", TC_BR_IF, TC_STRICT},
    {"_ = switch x, L0, L1", TC_SWITCH, TC_STRICT},
    {"_ = return", TC_RETURN, TC_STRICT},
    {"_ = trap", TC_TRAP, TC_STRICT},
    {"r54 = read int32", TC_READ, TC_STRICT},
    {"_ = write int32, a", TC_WRITE, TC_STRICT},
    {"_ = writeln int32, a", TC_WRITELN, TC_STRICT},
};

static void test_all_opcodes(void) {
    char source[16384];
    size_t offset = 0;
    size_t i;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcParseModule *module;

    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               "#lib\npublic func all() void then\n");
    for (i = 0; i < sizeof(k_opcode_cases) / sizeof(k_opcode_cases[0]); i++) {
        offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                                   "    %s\n", k_opcode_cases[i].line);
    }
    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               "end\n");
    module = parse_ok(source, &diag, &impl);
    CHECK(module != NULL);
    if (module == NULL) {
        return;
    }
    CHECK_EQ(module->nfuncs, 1);
    CHECK_EQ(module->funcs[0].nbody,
             sizeof(k_opcode_cases) / sizeof(k_opcode_cases[0]));
    if (module->funcs[0].nbody ==
        sizeof(k_opcode_cases) / sizeof(k_opcode_cases[0])) {
        for (i = 0; i < module->funcs[0].nbody; i++) {
            TcParseStmt *stmt = &module->funcs[0].body[i];
            CHECK_EQ(stmt->kind, TC_STMT_INSTR);
            if (stmt->kind == TC_STMT_INSTR) {
                CHECK_EQ(stmt->instr.op, k_opcode_cases[i].op);
                CHECK_EQ(stmt->instr.mode, k_opcode_cases[i].mode);
            }
        }
    }
    tc_parse_module_destroy(module);
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-DIAG: LEX/SYN code coverage and positions.                        */
/* ------------------------------------------------------------------------- */

static void test_diagnostics(void) {
    expect_diag("#program\nvar _: int32 = copy int32, 1\n", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 5, "discard decl");
    expect_diag("#program\n_ = frobnicate int32, 1\n", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 5, "unknown opcode");
    expect_diag("#program\n_ = add.foo int32, 1, 2\n", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 5, "unknown suffix");
    expect_diag("#program\n_ = div.wrap int32, 1, 2\n", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 5, "undefined combo");
    expect_diag("#program\nvar x: int32 = copy int32, add int32, 1, 2\n",
                TC_STAGE_SYN, TC_CE_SYNTAX, 2, 28, "nested op");
    expect_diag("#program\nvar x: int32 = 5\n", TC_STAGE_SYN, TC_CE_SYNTAX, 2,
                16, "bare value init");
    expect_diag("#program\nx = 5\n", TC_STAGE_SYN, TC_CE_SYNTAX, 2, 5,
                "bare value assign");
    expect_diag("#program\nvar x: int32 = copy int32, 1", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 29, "missing final NL");
    expect_diag("#program\n#lib\n", TC_STAGE_SYN, TC_CE_SYNTAX, 2, 1,
                "second pattern");
    expect_diag("var x: int32 = copy int32, 1\n", TC_STAGE_SYN, TC_CE_SYNTAX,
                1, 1, "missing pattern");
    expect_diag("#program\nvar x: int32\n", TC_STAGE_SYN,
                TC_CE_VAR_MISSING_INIT, 2, 5, "var missing init");
    expect_diag("#program\nvar x: int32 =\n", TC_STAGE_SYN,
                TC_CE_VAR_MISSING_INIT, 2, 5, "var missing rhs");
    expect_diag("#lib\npublic static var x: int32\n", TC_STAGE_SYN,
                TC_CE_VAR_MISSING_INIT, 2, 19, "static var missing init");
    expect_diag("#program\nstruct S then\n    readonly x: int32\n",
                TC_STAGE_SYN, TC_CE_MISSING_END, 2, 1, "missing end struct");
    expect_diag("#lib\npublic func f() void then\n    _ = trap\n", TC_STAGE_SYN,
                TC_CE_MISSING_END, 2, 8, "missing end func");
    expect_diag("#program\nunsafe then\n    _ = trap\n", TC_STAGE_SYN,
                TC_CE_MISSING_END, 2, 1, "missing end unsafe");
    expect_diag("#lib\npublic struct S then\n    readonly x: int32\nend\n"
                "import m\n",
                TC_STAGE_SYN, TC_CE_MODULE_LAYER, 5, 1, "layer import");
    expect_diag("#lib\npublic const K: int32 = 1\npublic struct S then\n"
                "    readonly x: int32\nend\n",
                TC_STAGE_SYN, TC_CE_MODULE_LAYER, 3, 1, "layer struct");
    expect_diag("#lib\npublic func f() void then\nend\n"
                "public const K: int32 = 1\n",
                TC_STAGE_SYN, TC_CE_MODULE_LAYER, 4, 1, "layer const");
    expect_diag("#program\nvar x: int32 = copy int32, 1\nstruct S then\n"
                "    readonly y: int32\nend\n",
                TC_STAGE_SYN, TC_CE_MODULE_LAYER, 3, 1, "layer program struct");
    expect_diag("#lib\nstruct S then\n    readonly x: int32\nend\n",
                TC_STAGE_SYN, TC_CE_MISSING_VISIBILITY, 2, 1,
                "missing visibility struct");
    expect_diag("#lib\nfunc f() void then\nend\n", TC_STAGE_SYN,
                TC_CE_MISSING_VISIBILITY, 2, 1, "missing visibility func");
    expect_diag("#lib\n@fast func f() void then\nend\n", TC_STAGE_SYN,
                TC_CE_MISSING_VISIBILITY, 2, 7,
                "missing visibility annotated func");
    expect_diag("#program\nfunc f() void then\nend\n", TC_STAGE_SYN,
                TC_CE_PROGRAM_MODE_MISUSE, 2, 1, "program func");
    expect_diag("#program\nstatic var x: int32 = 1\n", TC_STAGE_SYN,
                TC_CE_PROGRAM_MODE_MISUSE, 2, 1, "program static");
    expect_diag("#program\npublic struct S then\n    readonly x: int32\nend\n",
                TC_STAGE_SYN, TC_CE_PROGRAM_MODE_MISUSE, 2, 1,
                "program public");
    expect_diag("#program\nSelf.x = copy int32, 1\n", TC_STAGE_SYN,
                TC_CE_PROGRAM_MODE_MISUSE, 2, 1, "program Self");
    expect_diag("#program\nvar x: int32 = copy int32, 1\nx = Self.y\n",
                TC_STAGE_SYN, TC_CE_PROGRAM_MODE_MISUSE, 3, 5,
                "program Self bare value");
    expect_diag("#program\nvar x: int32 = Self.y\n", TC_STAGE_SYN,
                TC_CE_PROGRAM_MODE_MISUSE, 2, 16,
                "program Self bare var initializer");
    expect_diag("#lib\nvar x: int32 = copy int32, 1\n", TC_STAGE_SYN,
                TC_CE_PROGRAM_MODE_MISUSE, 2, 1, "lib var");
    expect_diag("#lib\n_ = trap\n", TC_STAGE_SYN, TC_CE_PROGRAM_MODE_MISUSE, 2,
                1, "lib instruction");
    /* Nested block pairing: paired nested blocks are a syntax error at the
     * nested construct, not MISSING_END. */
    expect_diag("#lib\n"
                "public struct S then\n"
                "    readonly x: int32\n"
                "    public func f() void then\n"
                "    end\n"
                "end\n",
                TC_STAGE_SYN, TC_CE_SYNTAX, 4, 5, "paired nested func in struct");
    expect_diag("#lib\n"
                "public func f() void then\n"
                "    public func g() void then\n"
                "    end\n"
                "end\n",
                TC_STAGE_SYN, TC_CE_SYNTAX, 3, 5, "paired nested func in func");
    /* A nested opener consumes the following end; with no further end the
     * enclosing (earlier) opener is the unpaired candidate. */
    expect_diag("#lib\n"
                "public func f() void then\n"
                "    public func g() void then\n"
                "end\n",
                TC_STAGE_SYN, TC_CE_MISSING_END, 2, 8,
                "inner func consumed the only end");
    /* #program Self is mode misuse even in IDENT-only positions. */
    expect_diag("#program\nstruct Self then\n    readonly x: int32\nend\n",
                TC_STAGE_SYN, TC_CE_PROGRAM_MODE_MISUSE, 2, 8,
                "program struct Self");
    expect_diag("#program\nvar Self: int32 = copy int32, 1\n",
                TC_STAGE_SYN, TC_CE_PROGRAM_MODE_MISUSE, 2, 5,
                "program var Self");
    expect_diag("#program\nlabel Self:\n", TC_STAGE_SYN,
                TC_CE_PROGRAM_MODE_MISUSE, 2, 7, "program label Self");
    /* Incomplete/separated suffixes: attached incomplete dot is an invalid
     * mnemonic; whitespace-separated suffix is a token-sequence error at the
     * dot. */
    expect_diag("#program\n_ = add. int32, 1, 2\n", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 5, "incomplete suffix");
    expect_diag("#program\n_ = add .wrap int32, 1, 2\n", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 9, "separated suffix");
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-ORDER: stage priority and source order (standard §12.2 #1/#3).    */
/* ------------------------------------------------------------------------- */

static void test_first_diagnostic_order(void) {
    /* A later lexical error wins over an earlier syntax error: stage priority
     * LEX -> SYN (standard §12.2 #1).  The semicolon is on line 3, after the
     * bare-value initializer on line 2. */
    expect_diag("#program\n"
                "var x: int32 = 5\n"
                "var y: int32 = copy int32, 1 ;\n",
                TC_STAGE_LEX, TC_CE_SYNTAX, 3, 30, "stage priority LEX");
    /* Two syntax errors: the earlier source position wins (§12.2 #3). */
    expect_diag("#program\n"
                "var x: int32 = 5\n"
                "var y: int32\n",
                TC_STAGE_SYN, TC_CE_SYNTAX, 2, 16, "source order SYN");
    /* Two syntax errors on one line: the earlier column wins (§12.2 #3). */
    expect_diag("#program\nvar _: int32 = 5\n", TC_STAGE_SYN,
                TC_CE_SYNTAX, 2, 5, "column order SYN");
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-UTF8: invalid-byte span facts (standard §12.5.1).                 */
/* ------------------------------------------------------------------------- */

static void expect_utf8_span(const uint8_t *bytes, size_t size,
                             uint32_t line, uint32_t column,
                             uint32_t valid_prefix, uint32_t invalid_offset,
                             const char *label) {
    TcParseModule *module = NULL;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    int rc = tc_parse_source("test.tc", bytes, size, &module, &diag, &impl);
    checks++;
    if (rc != 0 || module != NULL) {
        printf("FAIL %s: expected invalid UTF-8 diagnostic\n", label);
        failures++;
        if (module != NULL) {
            tc_parse_module_destroy(module);
        }
        return;
    }
    if (diag.code != TC_CE_SYNTAX || diag.span.line != line ||
        diag.span.column != column ||
        diag.span.valid_prefix_bytes != valid_prefix ||
        diag.span.invalid_byte_offset != invalid_offset) {
        printf("FAIL %s: got %s %u:%u prefix=%u offset=%u\n", label,
               tc_diag_info(diag.code)->symbol, diag.span.line,
               diag.span.column, diag.span.valid_prefix_bytes,
               diag.span.invalid_byte_offset);
        failures++;
    }
}

static void test_utf8_spans(void) {
    static const uint8_t ff_source[] =
        "#program\nvar x: int32 = 1\n\xff\n";
    static const uint8_t c3_source[] =
        "#program\nvar x: int32 = 1\n\xc3\x28\n";
    static const uint8_t trunc_source[] =
        "#program\nvar x: int32 = 1\n\xe2\x80";
    expect_utf8_span(ff_source, sizeof(ff_source) - 1, 3, 1, 26, 26,
                     "invalid lead byte");
    expect_utf8_span(c3_source, sizeof(c3_source) - 1, 3, 2, 27, 27,
                     "invalid continuation");
    expect_utf8_span(trunc_source, sizeof(trunc_source) - 1, 3, 3, 28, 28,
                     "truncated sequence");
    /* Columns are Unicode scalars, not bytes or UTF-16 code units.  Put two
     * non-ASCII scalars in a comment before an invalid byte: the invalid byte
     * is at byte offset 17 but at scalar column 7. */
    {
        static const uint8_t two_scalars[] = "#program\n/// \xc3\xa9\xc3\xa9\xff\n";
        expect_utf8_span(two_scalars, sizeof(two_scalars) - 1, 2, 7, 17, 17,
                         "scalar column after two 2-byte scalars");
    }
    /* A non-BMP scalar is one column (not two UTF-16 code units): invalid byte
     * at byte offset 17 and column 6. */
    {
        static const uint8_t non_bmp[] = "#program\n/// \xf0\x9f\x98\x80\xff\n";
        expect_utf8_span(non_bmp, sizeof(non_bmp) - 1, 2, 6, 17, 17,
                         "scalar column after non-BMP scalar");
    }
    {
        TcParseModule *module = NULL;
        TcFrontendDiag diag;
        TcImplError impl = TC_IMPL_OK;
        static const uint8_t bom[] = "\xef\xbb\xbf#program\n";
        int rc = tc_parse_source("test.tc", bom, sizeof(bom) - 1, &module,
                                 &diag, &impl);
        CHECK(rc == 0 && module == NULL);
        CHECK_EQ(diag.span.invalid_byte_offset, TC_SPAN_NO_INVALID_BYTE);
        CHECK_EQ(diag.span.line, 1);
        CHECK_EQ(diag.span.column, 1);
    }
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-LIMIT: documented nesting limits are implementation errors.       */
/* ------------------------------------------------------------------------- */

static void test_nesting_limit(void) {
    char source[8192];
    size_t offset = 0;
    size_t i;
    TcParseModule *module = NULL;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    int rc;

    /* Deep pointer type: implementation limit, never a language diagnostic. */
    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               "#program\nvar x: ");
    for (i = 0; i < 300; i++) {
        offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                                   "ptr<");
    }
    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               "int32");
    for (i = 0; i < 300; i++) {
        offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                                   ">");
    }
    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               " = copy ptr<int32>, nullptr\n");
    rc = tc_parse_source("test.tc", (const uint8_t *)source, offset, &module,
                         &diag, &impl);
    CHECK(rc != 0);
    CHECK_EQ(impl, TC_IMPL_INTERNAL);
    CHECK(module == NULL);

    /* Deep block nesting uses the same documented limit. */
    offset = 0;
    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               "#program\n");
    for (i = 0; i < 300; i++) {
        offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                                   "unsafe then\n");
    }
    rc = tc_parse_source("test.tc", (const uint8_t *)source, offset, &module,
                         &diag, &impl);
    CHECK(rc != 0);
    CHECK_EQ(impl, TC_IMPL_INTERNAL);
    CHECK(module == NULL);
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-LIMIT-0002: token-size limits vs the lexical integer range.       */
/* ------------------------------------------------------------------------- */

static void test_token_limits(void) {
    char source[40000];
    size_t offset = 0;
    size_t i;
    TcParseModule *module = NULL;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    int rc;

    /* A pathological float token is a documented implementation limit. */
    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               "#program\nvar x: float64 = 1.");
    for (i = 0; i < 17000; i++) {
        source[offset++] = '0';
    }
    source[offset++] = '\n';
    rc = tc_parse_source("test.tc", (const uint8_t *)source, offset, &module,
                         &diag, &impl);
    CHECK(rc != 0);
    CHECK_EQ(impl, TC_IMPL_INTERNAL);
    CHECK(module == NULL);

    /* A huge integer literal is still a language lexical-range diagnostic
     * (no unbounded allocation is needed to decide it). */
    offset = 0;
    offset += (size_t)snprintf(source + offset, sizeof(source) - offset,
                               "#program\nvar x: uint64 = copy uint64, ");
    for (i = 0; i < 17000; i++) {
        source[offset++] = '1';
    }
    source[offset++] = '\n';
    rc = tc_parse_source("test.tc", (const uint8_t *)source, offset, &module,
                         &diag, &impl);
    CHECK(rc == 0);
    CHECK(module == NULL);
    CHECK_EQ(diag.code, TC_CE_LITERAL_LEX_RANGE);
    CHECK_EQ(diag.stage, TC_STAGE_LEX);
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-DET: repeated parse determinism.                                  */
/* ------------------------------------------------------------------------- */

static uint64_t fingerprint(const TcParseModule *module) {
    uint64_t hash = 1469598103934665603ull;
    size_t i;
    size_t j;
    const uint8_t *bytes;
    hash ^= (uint64_t)module->is_program;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)module->nimports;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)module->nstructs;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)module->nconsts;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)module->nfuncs;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)module->nbody;
    hash *= 1099511628211ull;
    for (i = 0; i < module->nbody; i++) {
        hash ^= (uint64_t)module->body[i].kind;
        hash *= 1099511628211ull;
        if (module->body[i].kind == TC_STMT_INSTR) {
            hash ^= (uint64_t)module->body[i].instr.op;
            hash ^= (uint64_t)module->body[i].instr.span.line << 16;
            hash ^= (uint64_t)module->body[i].instr.span.column;
            hash *= 1099511628211ull;
            for (j = 0; j < module->body[i].instr.nargs; j++) {
                hash ^= (uint64_t)module->body[i].instr.args[j].kind;
                hash ^= (uint64_t)module->body[i].instr.args[j].span.column;
                hash *= 1099511628211ull;
            }
        }
    }
    bytes = (const uint8_t *)module->path;
    for (i = 0; bytes != NULL && bytes[i] != 0; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void test_determinism(void) {
    TcFrontendDiag diag1;
    TcFrontendDiag diag2;
    TcImplError impl1 = TC_IMPL_OK;
    TcImplError impl2 = TC_IMPL_OK;
    TcParseModule *a = parse_ok(k_simple_program, &diag1, &impl1);
    TcParseModule *b = parse_ok(k_simple_program, &diag2, &impl2);
    CHECK(a != NULL && b != NULL);
    if (a != NULL && b != NULL) {
        CHECK_EQ(fingerprint(a), fingerprint(b));
    }
    tc_parse_module_destroy(a);
    tc_parse_module_destroy(b);

    {
        TcArena arena1;
        TcArena arena2;
        TcLexResult r1;
        TcLexResult r2;
        int ok1 = 0;
        int ok2 = 0;
        tc_arena_init(&arena1, NULL);
        tc_arena_init(&arena2, NULL);
        r1 = lex_ok("#program\nvar x: int32 = copy int32, -0x1F\n", &arena1,
                    &ok1);
        r2 = lex_ok("#program\nvar x: int32 = copy int32, -0x1F\n", &arena2,
                    &ok2);
        CHECK(ok1 && ok2);
        CHECK_EQ(r1.count, r2.count);
        if (r1.count == r2.count) {
            size_t i;
            for (i = 0; i < r1.count; i++) {
                CHECK_EQ(r1.tokens[i].kind, r2.tokens[i].kind);
                CHECK_EQ(r1.tokens[i].span.line, r2.tokens[i].span.line);
                CHECK_EQ(r1.tokens[i].span.column, r2.tokens[i].span.column);
            }
        }
        tc_arena_destroy(&arena1);
        tc_arena_destroy(&arena2);
    }
}

/* ------------------------------------------------------------------------- */
/* Acceptance of individual EBNF forms.                                      */
/* ------------------------------------------------------------------------- */

static void test_accept_forms(void) {
    expect_ok("#program\n", "empty program");
    expect_ok("#lib\n", "empty library");
    expect_ok("#program\n/// only comments\n\n", "comment-only program");
    expect_ok("#program\r\nvar x: int32 = copy int32, 1\r\n", "CRLF");
    expect_ok("#program\rvar x: int32 = copy int32, 1\r", "CR");
    expect_ok("#program\nvar x: int32 = copy int32, \\\n  1\n",
              "continuation");
    expect_ok("#lib\npublic struct S then\n    readonly x: int32\nend\n"
              "private const K: int32 = 1\n"
              "public static var v: usize = 0\n"
              "public func f() void then\nend\n",
              "library regions");
    expect_ok("#program\nvar x: int32 = copy int32, -inf\n"
              "var y: float64 = copy float64, nan\n"
              "var z: ptr<int32> = copy ptr<int32>, nullptr\n",
              "literal keywords");
    expect_ok("#lib\npublic struct S then\n    readonly x: int32 @padding(4)\n"
              "end\n"
              "public func f(@range(0, 10) n: int32, @align(8) p: ptr<int32>)"
              " int32 then\n    _ = return n\nend\n",
              "annotations");
    expect_ok("#lib\npublic func f(g: fnptr<void, int32, ptr<int32>>) void "
              "then\nend\n",
              "fnptr forms");
    expect_ok("#program\nvar s: S = make S, x: 1, y: 2\n"
              "s.x = copy int32, 3\n"
              "var a: int32 = add int32, s.x, 1\n"
              "label L:\n_ = br_if s.flag, L\n",
              "field paths");
    expect_ok("#program\nvar x: int32 = copy int32, 1\n"
              "_ = switch x, L0, L1, L2\n"
              "label L0:\nlabel L1:\nlabel L2:\n",
              "switch targets");
}

/* ------------------------------------------------------------------------- */
/* TC-UNIT-SYN-0004: field padding annotation shape.                         */
/* ------------------------------------------------------------------------- */

static void test_field_annotation_shape(void) {
    /* SYN accepts any ANNOTATION(INT) in the field_padding shape; the
     * @padding name and N constraints are SEM (S4/S5). */
    expect_ok("#lib\n"
              "public struct S then\n"
              "    readonly a: int32 @align(4)\n"
              "    var b: int32 @fast(1)\n"
              "    var c: int32 @padding(4)\n"
              "    var d: int32 @padding(4u)\n"
              "end\n",
              "field annotation shape");
    /* A bare annotation has no (INT) field_padding shape and is SYNTAX. */
    expect_diag("#lib\n"
                "public struct S then\n"
                "    readonly x: int32 @fast\n"
                "end\n",
                TC_STAGE_SYN, TC_CE_SYNTAX, 3, 28, "bare field annotation");
}

int main(void) {
    test_num();
    test_lex_tokens();
    test_lex_mnemonics();
    test_lex_positions();
    test_lex_errors();
    test_lex_code_set();
    test_parse_program_shape();
    test_unsafe_depth();
    test_lib_shape();
    test_switch_targets();
    test_all_opcodes();
    test_diagnostics();
    test_first_diagnostic_order();
    test_utf8_spans();
    test_nesting_limit();
    test_token_limits();
    test_determinism();
    test_accept_forms();
    test_field_annotation_shape();
    if (failures != 0) {
        printf("front-end unit: %d failure(s) in %d checks\n", failures, checks);
        return 1;
    }
    printf("front-end unit: %d checks passed\n", checks);
    return 0;
}
