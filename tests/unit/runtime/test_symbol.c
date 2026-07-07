/*
 * test_symbol.c — 符号表与块级作用域单元测试
 *
 * 覆盖：
 *   - tc_symbol_table_init / free — 全局作用域初始化
 *   - tc_symbol_table_push_scope / pop_scope — 作用域栈
 *   - tc_symbol_table_find_in_scope — shadowing 与 pop 后不可见
 *   - tc_symbol_table_find_in_current_scope — 同层重复定义检测
 *   - tc_symbol_table_pop_last — REPL 回滚
 */
#include "tc_symbol.h"

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

static void test_init_global_scope(void) {
    TcSymbolTable table;

    tc_symbol_table_init(&table);
    check(tc_symbol_table_current_scope(&table) == 0, "init → global scope level 0");
    check(table.scope_count == 1, "init → one scope frame");
    tc_symbol_table_free(&table);
}

static void test_add_and_find_global(void) {
    TcSymbolTable table;
    TcDiagnostic diag;
    const TcSymbol *sym = NULL;

    tc_diagnostic_init(&diag);
    tc_symbol_table_init(&table);
    check(tc_symbol_table_add(&table, "x", TC_INT32, 0, 1, 0, TC_SYM_VARIABLE, 0, &diag) == 0,
          "add global symbol");
    sym = tc_symbol_table_find_in_scope(&table, "x");
    check(sym != NULL, "find global symbol");
    check(sym != NULL && sym->scope_level == 0, "global symbol scope_level 0");
    check(sym != NULL && sym->slot == 0, "global symbol slot 0");
    tc_symbol_table_free(&table);
    tc_diagnostic_clear(&diag);
}

static void test_shadowing(void) {
    TcSymbolTable table;
    TcDiagnostic diag;
    const TcSymbol *sym = NULL;

    tc_diagnostic_init(&diag);
    tc_symbol_table_init(&table);
    check(tc_symbol_table_add(&table, "x", TC_INT32, 0, 1, 0, TC_SYM_VARIABLE, 0, &diag) == 0,
          "add outer x");
    check(tc_symbol_table_push_scope(&table) == 1, "push block scope → level 1");
    check(tc_symbol_table_add(&table, "x", TC_INT16, 1, 2, 1, TC_SYM_VARIABLE, 0, &diag) == 0,
          "add inner x");
    sym = tc_symbol_table_find_in_scope(&table, "x");
    check(sym != NULL && sym->scope_level == 1 && sym->type == TC_INT16,
          "find_in_scope prefers inner shadow");
    check(tc_symbol_table_find_in_current_scope(&table, "x") != NULL,
          "find_in_current_scope finds inner x");
    tc_symbol_table_pop_scope(&table);
    sym = tc_symbol_table_find_in_scope(&table, "x");
    check(sym != NULL && sym->scope_level == 0 && sym->type == TC_INT32,
          "after pop_scope find outer x again");
    check(table.count == 2, "pop_scope retains block symbols for slot lookup");
    tc_symbol_table_free(&table);
    tc_diagnostic_clear(&diag);
}

static void test_then_else_same_name(void) {
    TcSymbolTable table;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_symbol_table_init(&table);

    check(tc_symbol_table_push_scope(&table) == 1, "then scope");
    check(tc_symbol_table_add(&table, "x", TC_INT32, 0, 1, 0, TC_SYM_VARIABLE, 0, &diag) == 0,
          "then var x");
    tc_symbol_table_pop_scope(&table);

    check(tc_symbol_table_push_scope(&table) == 1, "else scope (same level after pop)");
    check(tc_symbol_table_find_in_current_scope(&table, "x") == NULL,
          "else scope: no then x in current scope");
    check(tc_symbol_table_add(&table, "x", TC_INT32, 1, 3, 2, TC_SYM_VARIABLE, 0, &diag) == 0,
          "else var x (no conflict with then)");
    check(table.count == 2, "then and else x both retained for slot lookup");
    tc_symbol_table_pop_scope(&table);
    check(tc_symbol_table_find_in_scope(&table, "x") == NULL,
          "after both pops x not visible at global scope");
    check(table.count == 2, "symbols remain in table after scope pop");

    tc_symbol_table_free(&table);
    tc_diagnostic_clear(&diag);
}

static void test_cannot_pop_global(void) {
    TcSymbolTable table;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_symbol_table_init(&table);
    check(tc_symbol_table_add(&table, "g", TC_INT32, 0, 1, 0, TC_SYM_VARIABLE, 0, &diag) == 0,
          "add global g");
    tc_symbol_table_pop_scope(&table);
    check(table.count == 1, "pop_scope at global is no-op");
    check(table.scope_count == 1, "global scope frame remains");
    tc_symbol_table_free(&table);
    tc_diagnostic_clear(&diag);
}

static void test_pop_last(void) {
    TcSymbolTable table;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_symbol_table_init(&table);
    check(tc_symbol_table_add(&table, "a", TC_INT32, 0, 1, 0, TC_SYM_VARIABLE, 0, &diag) == 0,
          "add a");
    check(tc_symbol_table_add(&table, "b", TC_INT32, 1, 2, 1, TC_SYM_VARIABLE, 0, &diag) == 0,
          "add b");
    tc_symbol_table_pop_last(&table);
    check(table.count == 1, "pop_last removes one symbol");
    check(tc_symbol_table_find_in_scope(&table, "b") == NULL, "pop_last removed b");
    check(tc_symbol_table_find_in_scope(&table, "a") != NULL, "pop_last kept a");
    tc_symbol_table_free(&table);
    tc_diagnostic_clear(&diag);
}

static void test_nested_scopes(void) {
    TcSymbolTable table;
    TcDiagnostic diag;
    const TcSymbol *sym = NULL;

    tc_diagnostic_init(&diag);
    tc_symbol_table_init(&table);
    check(tc_symbol_table_add(&table, "x", TC_INT32, 0, 1, 0, TC_SYM_VARIABLE, 0, &diag) == 0,
          "global x");
    check(tc_symbol_table_push_scope(&table) == 1, "outer if");
    check(tc_symbol_table_add(&table, "y", TC_INT32, 1, 2, 1, TC_SYM_VARIABLE, 0, &diag) == 0,
          "outer y");
    check(tc_symbol_table_push_scope(&table) == 2, "inner if");
    check(tc_symbol_table_add(&table, "y", TC_INT16, 2, 3, 2, TC_SYM_VARIABLE, 0, &diag) == 0,
          "inner y shadows outer y");
    sym = tc_symbol_table_find_in_scope(&table, "y");
    check(sym != NULL && sym->scope_level == 2, "nested find inner y");
    sym = tc_symbol_table_find_in_scope(&table, "x");
    check(sym != NULL && sym->scope_level == 0, "nested still finds global x");
    tc_symbol_table_pop_scope(&table);
    sym = tc_symbol_table_find_in_scope(&table, "y");
    check(sym != NULL && sym->scope_level == 1, "after inner pop find outer y");
    tc_symbol_table_pop_scope(&table);
    sym = tc_symbol_table_find_in_scope(&table, "y");
    check(sym == NULL, "after outer pop y gone");
    tc_symbol_table_free(&table);
    tc_diagnostic_clear(&diag);
}

static void test_find_mut_shadowing(void) {
    TcSymbolTable table;
    TcDiagnostic diag;
    TcSymbol *mut = NULL;

    tc_diagnostic_init(&diag);
    tc_symbol_table_init(&table);
    check(tc_symbol_table_add(&table, "c", TC_INT32, 0, 1, 0, TC_SYM_CONSTANT, 1, &diag) == 0,
          "global const c");
    check(tc_symbol_table_push_scope(&table) == 1, "block scope");
    check(tc_symbol_table_add(&table, "c", TC_INT32, 1, 2, 1, TC_SYM_VARIABLE, 0, &diag) == 0,
          "block var c");
    mut = tc_symbol_table_find_mut(&table, "c");
    check(mut != NULL && mut->sym_kind == TC_SYM_VARIABLE, "find_mut returns inner symbol");
    tc_symbol_table_free(&table);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_init_global_scope();
    test_add_and_find_global();
    test_shadowing();
    test_then_else_same_name();
    test_cannot_pop_global();
    test_pop_last();
    test_nested_scopes();
    test_find_mut_shadowing();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
