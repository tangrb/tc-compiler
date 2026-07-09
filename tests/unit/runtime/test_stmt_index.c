/*
 * test_stmt_index.c — stmt_index 子树 span 与游标单元测试
 */
#include "tc_parser.h"
#include "tc_stmt_index.h"

#include "tc_diagnostic.h"

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

static void test_leaf_count(void) {
    TcTokenList tokens;
    TcStatement stmt;
    TcParserCtx ctx;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    memset(&ctx, 0, sizeof(ctx));
    check(tc_tokenize_line("writeln(int32, x)", 1, &tokens, &diag) == 0, "tokenize leaf");
    check(tc_parse_statement(&ctx, &tokens, 1, &stmt, &diag) == 0, "parse leaf");
    check(tc_stmt_subtree_index_count(&stmt) == 1, "leaf subtree count 1");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_nested_if_subtree_count(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "if false then\n"
        "    if true then\n"
        "        writeln(int32, 1)\n"
        "    end\n"
        "    var x: int32 = 2\n"
        "else\n"
        "    writeln(int32, 3)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse nested if program");
    check(program.count == 1, "one top-level stmt");
    check(tc_stmt_subtree_index_count(&program.items[0]) == 5, "nested if subtree count 5");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_skip_block_matches_dfs(void) {
    TcProgram program;
    TcDiagnostic diag;
    TcStmtIndexCursor cursor;
    const char *source =
        "var a: int32 = 1\n"
        "if false then\n"
        "    if true then\n"
        "        var b: int32 = 2\n"
        "    end\n"
        "else\n"
        "    writeln(int32, a)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse skip sample");

    tc_stmt_index_reset(&cursor);
    check(tc_stmt_index_take(&cursor) == 0, "var a index 0");
    check(tc_stmt_index_take(&cursor) == 1, "outer if index 1");
    tc_stmt_index_skip_block(&cursor, program.items[1].u.if_stmt.then_body,
                            program.items[1].u.if_stmt.then_count);
    check(cursor.next == 4, "skip then subtree advances to else index");
    check(tc_stmt_index_take(&cursor) == 4, "else writeln index 4");
    check(cursor.next == 5, "cursor after program");

    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_block_span(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "if true then\n"
        "    writeln(int32, 1)\n"
        "    writeln(int32, 2)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse block span");
    check(tc_stmt_block_index_span(program.items[0].u.if_stmt.then_body,
                                   program.items[0].u.if_stmt.then_count) == 2,
          "two writeln block span 2");
    check(tc_stmt_subtree_index_count(&program.items[0]) == 3, "if + 2 writeln = 3");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_leaf_count();
    test_nested_if_subtree_count();
    test_skip_block_matches_dfs();
    test_block_span();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
