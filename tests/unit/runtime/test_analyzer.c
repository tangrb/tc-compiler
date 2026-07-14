/*
 * test_analyzer.c — 静态分析器单元测试
 *
 * 覆盖：
 *   - tc_analyze — 合法程序 Pass1/Pass2
 *   - if 条件类型错误、跨块引用、let 编译期求值
 *   - 未初始化变量警告
 */
#include "tc_analyzer.h"

#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_warning.h"

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

static void test_analyze_valid_program(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "var x: int32 = 10\n"
        "writeln(int32, x)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse valid program");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze valid program");
    check(typed.program.count == 2, "typed program stmt count");
    check(typed.symbols.count == 1, "typed program symbol count");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_let_const(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "let N: int32 = 42\n"
        "let M: int32 = add(int32, N, 8)\n"
        "writeln(int32, M)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse let program");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze let program");
    check(typed.symbols.count == 2, "let symbol count");
    check(typed.symbols.symbols[1].has_const_value != 0, "let M has const value");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_if_condition_type(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "if add(int32, 1, 2) then\n"
        "    writeln(int32, 1)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse if arith cond");
    check(tc_analyze(&program, &typed, &diag) != 0, "analyze if arith cond fails");
    check(strstr(diag.message, "if condition must be bool") != NULL, "if cond type message");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_cross_block_reference(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "if true then\n"
        "    var x: int32 = 1\n"
        "end\n"
        "writeln(int32, x)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse cross block");
    check(tc_analyze(&program, &typed, &diag) != 0, "analyze cross block fails");
    check(strstr(diag.message, "cross-block reference") != NULL ||
              strstr(diag.message, "undefined variable") != NULL,
          "cross block error message");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_uninit_error(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "var a: int32\n"
        "writeln(int32, a)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse uninit");
    check(tc_analyze(&program, &typed, &diag) != 0, "analyze uninit fails");
    check(diag.kind == TC_ERR_UNINITIALIZED_VARIABLE, "→ UNINITIALIZED_VARIABLE");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_uninit_if_merge(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "var x: int32\n"
        "if true then\n"
        "    x = 10\n"
        "end\n"
        "var y: int32 = add(int32, x, 1)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse if-path uninit");
    check(tc_analyze(&program, &typed, &diag) != 0, "if-path uninit fails");
    check(diag.kind == TC_ERR_UNINITIALIZED_VARIABLE, "if-path → UNINITIALIZED_VARIABLE");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_uninit_both_paths_ok(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "var x: int32\n"
        "if true then\n"
        "    x = 10\n"
        "else\n"
        "    x = 20\n"
        "end\n"
        "var y: int32 = add(int32, x, 1)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse both-paths");
    check(tc_analyze(&program, &typed, &diag) == 0, "both paths init ok");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_shortcircuit_uninit_ok(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "var flag: bool = false\n"
        "var uninit: bool\n"
        "var result: bool = and(bool, false, uninit)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse shortcircuit");
    check(tc_analyze(&program, &typed, &diag) == 0, "shortcircuit skips uninit rhs");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_if_block_scope(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "var x: int32 = 1\n"
        "if true then\n"
        "    var x: int32 = 2\n"
        "    writeln(int32, x)\n"
        "end\n"
        "writeln(int32, x)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse if shadow");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze if shadow");
    check(typed.symbols.count == 2, "if shadow symbol count");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_const_cyclic(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source = "let A: int32 = add(int32, A, 1)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse cyclic let");
    check(tc_analyze(&program, &typed, &diag) != 0, "analyze cyclic let fails");
    check(strstr(diag.message, "circular dependency") != NULL ||
              strstr(diag.message, "cannot reference itself") != NULL,
          "cyclic let message");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_duplicate_label(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "label dup:\n"
        "label dup:\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse duplicate label");
    check(tc_analyze(&program, &typed, &diag) != 0, "analyze duplicate label fails");
    check(diag.kind == TC_ERR_DUPLICATE_LABEL, "duplicate label → TC_ERR_DUPLICATE_LABEL");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_sibling_label_same_name(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "if true then\n"
        "    label L:\n"
        "else\n"
        "    label L:\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse sibling labels");
    check(tc_analyze(&program, &typed, &diag) == 0, "sibling same-name labels ok");
    check(typed.symbols.label_count == 2, "Pass2 retains both sibling labels");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_goto_ok(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "label start:\n"
        "goto start\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse goto ok");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze goto ok");
    check(typed.symbols.label_count == 1, "goto ok label retained");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_goto_forward(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "goto skip\n"
        "label skip:\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse forward goto");
    check(tc_analyze(&program, &typed, &diag) == 0, "forward goto ok");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_goto_undefined(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source = "goto nonexistent\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse undefined goto");
    check(tc_analyze(&program, &typed, &diag) != 0, "undefined goto fails");
    check(diag.kind == TC_ERR_LABEL_NOT_FOUND, "→ LABEL_NOT_FOUND");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_goto_into_block(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "goto inner\n"
        "if true then\n"
        "    label inner:\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse jump into block");
    check(tc_analyze(&program, &typed, &diag) != 0, "jump into block fails");
    check(diag.kind == TC_ERR_JUMP_INTO_BLOCK, "→ JUMP_INTO_BLOCK");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_goto_sibling(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "if true then\n"
        "    goto else_branch\n"
        "else\n"
        "    label else_branch:\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse sibling jump");
    check(tc_analyze(&program, &typed, &diag) != 0, "sibling jump fails");
    check(diag.kind == TC_ERR_JUMP_TO_SIBLING_BLOCK, "→ JUMP_TO_SIBLING_BLOCK");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_goto_out_of_if(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "label after:\n"
        "if true then\n"
        "    goto after\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse outward goto");
    check(tc_analyze(&program, &typed, &diag) == 0, "outward goto ok");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_analyze_valid_program();
    test_analyze_let_const();
    test_analyze_if_condition_type();
    test_analyze_cross_block_reference();
    test_analyze_uninit_error();
    test_analyze_uninit_if_merge();
    test_analyze_uninit_both_paths_ok();
    test_analyze_shortcircuit_uninit_ok();
    test_analyze_if_block_scope();
    test_analyze_const_cyclic();
    test_analyze_duplicate_label();
    test_analyze_sibling_label_same_name();
    test_analyze_goto_ok();
    test_analyze_goto_forward();
    test_analyze_goto_undefined();
    test_analyze_goto_into_block();
    test_analyze_goto_sibling();
    test_analyze_goto_out_of_if();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
