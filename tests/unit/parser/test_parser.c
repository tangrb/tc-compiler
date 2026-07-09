/*
 * test_parser.c — Parser 单元测试
 *
 * 覆盖：
 *   - tc_parse_statement — var / let / write / assign
 *   - tc_parse_source_to_program — 多行 if 与缩进
 *   - 静态错误 — 缺少 end、缩进不足
 */
#include "tc_parser.h"

#include "tc_diagnostic.h"
#include "tc_lexer.h"

#include <stdio.h>
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

static void test_parse_var_def(void) {
    TcTokenList tokens;
    TcStatement stmt;
    TcParserCtx ctx;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    memset(&ctx, 0, sizeof(ctx));
    check(tc_tokenize_line("var x: int32 = 42", 1, &tokens, &diag) == 0, "tokenize var def");
    check(tc_parse_statement(&ctx, &tokens, 1, &stmt, &diag) == 0, "parse var def");
    check(stmt.kind == TC_STMT_VAR_DEF, "var def kind");
    check(stmt.u.var_def.has_rhs != 0, "var def has rhs");
    check(strcmp(stmt.u.var_def.name, "x") == 0, "var def name");
    check(stmt.u.var_def.type == TC_INT32, "var def type");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_parse_let_const(void) {
    TcTokenList tokens;
    TcStatement stmt;
    TcParserCtx ctx;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    memset(&ctx, 0, sizeof(ctx));
    check(tc_tokenize_line("let N: int32 = 100", 1, &tokens, &diag) == 0, "tokenize let");
    check(tc_parse_statement(&ctx, &tokens, 1, &stmt, &diag) == 0, "parse let");
    check(stmt.kind == TC_STMT_CONST_DEF, "let kind CONST_DEF");
    check(strcmp(stmt.u.const_def.name, "N") == 0, "let name");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_parse_write(void) {
    TcTokenList tokens;
    TcStatement stmt;
    TcParserCtx ctx;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    memset(&ctx, 0, sizeof(ctx));
    check(tc_tokenize_line("writeln(int32, x)", 1, &tokens, &diag) == 0, "tokenize writeln");
    check(tc_parse_statement(&ctx, &tokens, 1, &stmt, &diag) == 0, "parse writeln");
    check(stmt.kind == TC_STMT_WRITELN, "writeln kind");
    check(stmt.u.io_write.type == TC_INT32, "writeln type");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_parse_if_program(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "var x: int32 = 1\n"
        "if eq(int32, x, 1) then\n"
        "    writeln(int32, x)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse if program");
    check(program.count == 2, "if program stmt count");
    check(program.items[0].kind == TC_STMT_VAR_DEF, "if program first stmt var");
    check(program.items[1].kind == TC_STMT_IF, "if program second stmt if");
    check(program.items[1].u.if_stmt.then_count == 1, "if then body count");
    check(program.items[1].u.if_stmt.else_count == 0, "if no else");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_if_else_program(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "if false then\n"
        "    writeln(int32, 1)\n"
        "else\n"
        "    writeln(int32, 2)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse if-else program");
    check(program.count == 1, "if-else program stmt count");
    check(program.items[0].kind == TC_STMT_IF, "if-else kind");
    check(program.items[0].u.if_stmt.then_count == 1, "if-else then count");
    check(program.items[0].u.if_stmt.else_count == 1, "if-else else count");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_missing_end(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "if true then\n"
        "    writeln(int32, 1)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0, "missing end fails");
    check(strstr(diag.message, "missing end") != NULL, "missing end message");
    tc_diagnostic_clear(&diag);
}

static void test_parse_indent_insufficient(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "if true then\n"
        "    writeln(int32, 1)\n"
        "  writeln(int32, 2)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0, "indent insufficient fails");
    check(strstr(diag.message, "insufficient indentation") != NULL, "indent insufficient message");
    tc_diagnostic_clear(&diag);
}

static void test_parse_nested_if(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "if true then\n"
        "    if false then\n"
        "        writeln(int32, 1)\n"
        "    end\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse nested if");
    check(program.count == 1, "nested if top count");
    check(program.items[0].u.if_stmt.then_count == 1, "nested if then count");
    check(program.items[0].u.if_stmt.then_body[0].kind == TC_STMT_IF, "nested if inner kind");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_parse_var_def();
    test_parse_let_const();
    test_parse_write();
    test_parse_if_program();
    test_parse_if_else_program();
    test_parse_missing_end();
    test_parse_indent_insufficient();
    test_parse_nested_if();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
