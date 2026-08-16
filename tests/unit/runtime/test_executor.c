/* test_executor.c — structured control propagation and repeatable execution */
#include "tc_lib.h"
#include "tc_executor.h"
#include "tc_semantics.h"

#include <stdio.h>

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

static void test_nested_loop_control_and_repeat(void) {
    static const char *source =
        "#program\nvar outer: int32 = 0\n"
        "while lt(int32, outer, 3) then\n"
        "    var inner: int32 = 0\n"
        "    while true then\n"
        "        inner = add(int32, inner, 1)\n"
        "        if eq(int32, inner, 2) then\n"
        "            continue\n"
        "        end\n"
        "        if eq(int32, inner, 4) then\n"
        "            break\n"
        "        end\n"
        "    end\n"
        "    outer = add(int32, outer, 1)\n"
        "end\n";
    TcTypedProgram typed;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(source, "<test>", &typed, &diag) == 0,
          "compile nested loop control program");
    check(tc_run_program(&typed, &diag) == 0, "first structured execution succeeds");
    check(tc_run_program(&typed, &diag) == 0,
          "second execution uses fresh fixed slots and succeeds");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_zero_iteration(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tc_compile_source("#program\nvar x: int32 = 0\nwhile false then\n    x = 1\nend\n", "<test>", &typed, &diag) == 0,
          "compile zero-iteration loop");
    check(tc_run_program(&typed, &diag) == 0, "zero-iteration loop executes");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_execution_uses_resolved_slots(void) {
    static const char *source =
        "#program\nvar counter: int32 = 0\n"
        "while lt(int32, counter, 2) then\n"
        "    counter = add(int32, counter, 1)\n"
        "end\n";
    TcTypedProgram typed;
    TcDiagnostic diag;
    TcStatement *var_stmt = NULL;
    TcStatement *while_stmt = NULL;
    TcStatement *assign_stmt = NULL;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(source, "<test>", &typed, &diag) == 0,
          "compile resolved-slot execution program");

    var_stmt = &typed.program.items[0];
    while_stmt = &typed.program.items[1];
    assign_stmt = &while_stmt->u.while_stmt.body[0];
    var_stmt->u.var_def.name[0] = 'x';
    while_stmt->u.while_stmt.condition.u.compare.lhs.u.name[0] = 'x';
    assign_stmt->u.assign.name[0] = 'x';
    assign_stmt->u.assign.rhs.u.arith.lhs.u.name[0] = 'x';

    check(tc_run_program(&typed, &diag) == 0,
          "executor consumes Analyzer-resolved slots instead of AST names");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_var_reexecution_overwrites_fixed_slot(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;
    TcValue slot;
    int resolved_slot = -1;

    tc_diagnostic_init(&diag);
    check(tc_compile_source("#program\nvar value: int32 = 7\n", "<test>", &typed, &diag) == 0,
          "compile fixed-slot reexecution program");
    resolved_slot = typed.program.items[0].u.var_def.binding.slot;
    check(resolved_slot == 0, "first lexical var has stable slot zero");

    slot = tc_value_make(TC_INT32, 99);
    check(tc_execute_statement(&typed.program.items[0], &slot, &typed.symbols, &diag) == 0,
          "first var execution succeeds");
    check(slot.type->tag == TC_INT32 && slot.bits == 7,
          "first var execution initializes resolved slot");

    slot = tc_value_make(TC_INT32, 123);
    check(tc_execute_statement(&typed.program.items[0], &slot, &typed.symbols, &diag) == 0,
          "second var execution succeeds");
    check(slot.type->tag == TC_INT32 && slot.bits == 7,
          "second var execution overwrites the same fixed slot");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_goto_outside_function_rejected(void) {
    static const char *source =
        "#program\ngoto done\n"
        "var skipped: int32 = 1\n"
        "label done:\n";
    TcTypedProgram typed;
    tc_typed_program_init(&typed);
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(source, "<test>", &typed, &diag) != 0,
          "top-level goto rejected at compile time");
    check(diag.kind == TC_CE_GOTO_OUTSIDE_FUNCTION, "→ GOTO_OUTSIDE_FUNCTION");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_nested_loop_control_and_repeat();
    test_zero_iteration();
    test_execution_uses_resolved_slots();
    test_var_reexecution_overwrites_fixed_slot();
    test_goto_outside_function_rejected();
    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
