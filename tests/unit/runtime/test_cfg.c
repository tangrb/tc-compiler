/* test_cfg.c — explicit CFG construction and reachability contracts */
#include "tc_analyzer.h"
#include "tc_cfg.h"
#include "tc_diagnostic.h"
#include "tc_parser.h"

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

static int compile_source(const char *source, TcTypedProgram *typed, TcDiagnostic *diag) {
    TcProgram program;

    tc_program_init(&program);
    tc_typed_program_init(typed);
    if (tc_parse_source_to_program(source, &program, diag) != 0) {
        return -1;
    }
    return tc_analyze(&program, typed, diag);
}

static size_t edge_count(const TcCfg *cfg, TcCfgEdgeKind kind, int enabled_only) {
    size_t count = 0;
    size_t i = 0;

    for (i = 0; i < cfg->edge_count; i++) {
        if (cfg->edges[i].kind == kind && (!enabled_only || cfg->edges[i].enabled)) {
            count++;
        }
    }
    return count;
}

static size_t node_count(const TcCfg *cfg, TcCfgNodeKind kind) {
    size_t count = 0;
    size_t i = 0;

    for (i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i].kind == kind) {
            count++;
        }
    }
    return count;
}

static int find_node_for_stmt(const TcCfg *cfg, TcCfgNodeKind kind, int stmt_index) {
    size_t i = 0;

    for (i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i].kind == kind && cfg->nodes[i].stmt_index == stmt_index) {
            return (int)i;
        }
    }
    return -1;
}

static int find_first_node(const TcCfg *cfg, TcCfgNodeKind kind) {
    size_t i = 0;

    for (i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i].kind == kind) {
            return (int)i;
        }
    }
    return -1;
}

static size_t enabled_edges_from(const TcCfg *cfg, int from, TcCfgEdgeKind kind) {
    size_t count = 0;
    size_t i = 0;

    for (i = 0; i < cfg->edge_count; i++) {
        if (cfg->edges[i].from == from && cfg->edges[i].kind == kind &&
            cfg->edges[i].enabled) {
            count++;
        }
    }
    return count;
}

static void test_sequential_cfg(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(compile_source("var x: int32 = 1\nwriteln(int32, x)\n", &typed, &diag) == 0,
          "compile sequential cfg");
    if (typed.cfg) {
        check(typed.cfg->node_count == 4, "entry + two statements + exit");
        check(typed.cfg->edge_count == 3, "three sequential fallthrough edges");
        check(typed.cfg->nodes[typed.cfg->entry_id].kind == TC_CFG_ENTRY, "entry node kind");
        check(typed.cfg->nodes[typed.cfg->exit_id].kind == TC_CFG_EXIT, "exit node kind");
        check(node_count(typed.cfg, TC_CFG_STATEMENT) == 2, "two statement nodes");
    } else {
        check(0, "typed program owns cfg");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_structured_edges(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "while false then\n"
        "    if true then\n"
        "        continue\n"
        "    else\n"
        "        break\n"
        "    end\n"
        "end\n";

    tc_diagnostic_init(&diag);
    check(compile_source(source, &typed, &diag) == 0, "compile structured cfg");
    if (typed.cfg) {
        check(node_count(typed.cfg, TC_CFG_LOOP_CONDITION) == 1, "one loop condition node");
        check(node_count(typed.cfg, TC_CFG_LOOP_EXIT) == 1, "one loop exit node");
        check(node_count(typed.cfg, TC_CFG_BRANCH) == 1, "one if branch node");
        check(node_count(typed.cfg, TC_CFG_MERGE) == 1, "one if merge node");
        check(edge_count(typed.cfg, TC_CFG_TRUE, 0) >= 2, "while and if true edges exist");
        check(edge_count(typed.cfg, TC_CFG_FALSE, 0) >= 2, "while and if false edges exist");
        check(edge_count(typed.cfg, TC_CFG_BREAK, 0) == 1, "break edge exists");
        check(edge_count(typed.cfg, TC_CFG_CONTINUE, 0) == 1, "continue edge exists");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_goto_edge(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source = "goto done\nvar x: int32 = 1\nlabel done:\n";

    tc_diagnostic_init(&diag);
    check(compile_source(source, &typed, &diag) == 0, "compile goto cfg");
    if (typed.cfg) {
        check(edge_count(typed.cfg, TC_CFG_GOTO, 1) == 1, "resolved goto edge exists");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_constant_loop_pruning(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "while false then\n"
        "    writeln(int32, 1)\n"
        "end\n"
        "writeln(int32, 2)\n";
    size_t i = 0;
    int body_reachable = 1;

    tc_diagnostic_init(&diag);
    check(compile_source(source, &typed, &diag) == 0, "compile constant false loop");
    if (typed.cfg) {
        check(edge_count(typed.cfg, TC_CFG_TRUE, 1) == 0,
              "constant false loop disables true edge");
        check(edge_count(typed.cfg, TC_CFG_FALSE, 1) == 1,
              "constant false loop retains false edge");
        for (i = 0; i < typed.cfg->node_count; i++) {
            if (typed.cfg->nodes[i].stmt_index == 1) {
                body_reachable = typed.cfg->nodes[i].reachable;
            }
        }
        check(body_reachable == 0, "constant false body is unreachable");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void check_let_branch_pruning(const char *source, int expected,
                                     const char *compile_message) {
    TcTypedProgram typed;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(compile_source(source, &typed, &diag) == 0, compile_message);
    if (typed.cfg) {
        int branch = find_node_for_stmt(typed.cfg, TC_CFG_BRANCH, 1);

        check(branch >= 0, "find branch controlled by let");
        if (branch >= 0) {
            check(enabled_edges_from(typed.cfg, branch, TC_CFG_TRUE) ==
                      (expected ? 1u : 0u),
                  "let value controls enabled true edge");
            check(enabled_edges_from(typed.cfg, branch, TC_CFG_FALSE) ==
                      (expected ? 0u : 1u),
                  "let value controls enabled false edge");
        }
    } else {
        check(0, "let branch compile owns cfg");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_let_constant_branch_pruning(void) {
    check_let_branch_pruning(
        "let FLAG: bool = true\n"
        "if FLAG then\n"
        "    writeln(int32, 1)\n"
        "else\n"
        "    writeln(int32, 2)\n"
        "end\n",
        1, "compile let true branch");
    check_let_branch_pruning(
        "let FLAG: bool = false\n"
        "if FLAG then\n"
        "    writeln(int32, 1)\n"
        "else\n"
        "    writeln(int32, 2)\n"
        "end\n",
        0, "compile let false branch");
    check_let_branch_pruning(
        "let FLAG: bool = eq(int32, 7, 7)\n"
        "if FLAG then\n"
        "    writeln(int32, 1)\n"
        "else\n"
        "    writeln(int32, 2)\n"
        "end\n",
        1, "compile let comparison branch");
}

static void test_local_let_shadow_branch_pruning(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "let FLAG: bool = false\n"
        "if true then\n"
        "    let FLAG: bool = true\n"
        "    if FLAG then\n"
        "        writeln(int32, 1)\n"
        "    else\n"
        "        writeln(int32, 2)\n"
        "    end\n"
        "end\n";
    int branch = -1;

    tc_diagnostic_init(&diag);
    check(compile_source(source, &typed, &diag) == 0,
          "compile local let shadow branch");
    if (typed.cfg) {
        branch = find_node_for_stmt(typed.cfg, TC_CFG_BRANCH, 3);
        check(branch >= 0, "find branch controlled by shadowing let");
        if (branch >= 0) {
            check(enabled_edges_from(typed.cfg, branch, TC_CFG_TRUE) == 1u,
                  "shadowing let enables true edge");
            check(enabled_edges_from(typed.cfg, branch, TC_CFG_FALSE) == 0u,
                  "shadowing let disables false edge");
        }
    } else {
        check(0, "local let shadow compile owns cfg");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_static_bool_rhs_pruning(void) {
    static const struct {
        const char *source;
        TcCfgNodeKind kind;
        int expected;
        const char *message;
    } cases[] = {
        {
            "if true then\n    writeln(bool, true)\nend\n",
            TC_CFG_BRANCH,
            1,
            "literal true branch",
        },
        {
            "let FLAG: bool = false\n"
            "if FLAG then\n    writeln(bool, true)\nend\n",
            TC_CFG_BRANCH,
            0,
            "const-ref false branch",
        },
        {
            "let A: int32 = 10\nlet B: int32 = 5\n"
            "if gt(int32, A, B) then\n    writeln(bool, true)\nend\n",
            TC_CFG_BRANCH,
            1,
            "integer comparison branch",
        },
        {
            "let A: float64 = 1.0\nlet B: float64 = 2.0\n"
            "if lt(float64, A, B) then\n    writeln(bool, true)\nend\n",
            TC_CFG_BRANCH,
            1,
            "float comparison branch",
        },
        {
            "let A: bool = true\nlet B: bool = false\n"
            "if and(bool, A, B) then\n    writeln(bool, true)\nend\n",
            TC_CFG_BRANCH,
            0,
            "logic binary branch",
        },
        {
            "let A: bool = false\n"
            "if not(bool, A) then\n    writeln(bool, true)\nend\n",
            TC_CFG_BRANCH,
            1,
            "logic unary branch",
        },
        {
            "let ZERO: int32 = 0\n"
            "if cast(bool, ZERO) then\n    writeln(bool, true)\nend\n",
            TC_CFG_BRANCH,
            0,
            "strict bool cast branch",
        },
        {
            "var runtime: bool = true\n"
            "while runtime then\n    runtime = false\nend\n",
            TC_CFG_LOOP_CONDITION,
            -1,
            "var loop condition remains unknown",
        },
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcTypedProgram typed;
        TcDiagnostic diag;
        int node = -1;

        tc_diagnostic_init(&diag);
        check(compile_source(cases[i].source, &typed, &diag) == 0, cases[i].message);
        if (typed.cfg) {
            node = find_first_node(typed.cfg, cases[i].kind);
            check(node >= 0, "static bool case owns condition node");
            if (node >= 0) {
                check(typed.cfg->nodes[node].constant_condition == cases[i].expected,
                      "static bool RHS produces expected tri-state value");
                check(enabled_edges_from(typed.cfg, node, TC_CFG_TRUE) ==
                          (cases[i].expected == 0 ? 0u : 1u),
                      "static bool RHS controls true edge");
                check(enabled_edges_from(typed.cfg, node, TC_CFG_FALSE) ==
                          (cases[i].expected == 1 ? 0u : 1u),
                      "static bool RHS controls false edge");
            }
        } else {
            check(0, "static bool case owns cfg");
        }
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_short_circuit_read_sets(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "let FALSE: bool = false\n"
        "let TRUE: bool = true\n"
        "var rhs: bool = true\n"
        "var runtime_false: bool = false\n"
        "var literal_and: bool = and(bool, false, rhs)\n"
        "var literal_or: bool = or(bool, true, rhs)\n"
        "var const_and: bool = and(bool, FALSE, rhs)\n"
        "var const_or: bool = or(bool, TRUE, rhs)\n"
        "var runtime_and: bool = and(bool, runtime_false, rhs)\n";
    static const int pruned_stmt_indices[] = {4, 5, 6, 7};
    size_t i = 0;
    int node = -1;

    tc_diagnostic_init(&diag);
    check(compile_source(source, &typed, &diag) == 0,
          "compile logic RHS read-set matrix");
    if (typed.cfg) {
        for (i = 0; i < sizeof(pruned_stmt_indices) / sizeof(pruned_stmt_indices[0]); i++) {
            node = find_node_for_stmt(typed.cfg, TC_CFG_STATEMENT, pruned_stmt_indices[i]);
            check(node >= 0, "find pruned logic statement node");
            if (node >= 0) {
                check(typed.cfg->nodes[node].read_count == 0,
                      "literal or let bool short circuit omits RHS read slot");
            }
        }
        node = find_node_for_stmt(typed.cfg, TC_CFG_STATEMENT, 8);
        check(node >= 0, "find runtime logic statement node");
        if (node >= 0) {
            check(typed.cfg->nodes[node].read_count == 2,
                  "var lhs keeps both logic operand read slots");
        }
    } else {
        check(0, "logic RHS read-set compile owns cfg");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_cfg_size_is_linear(void) {
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "var a: int32 = 1\n"
        "if true then\n"
        "    var b: int32 = 2\n"
        "else\n"
        "    var c: int32 = 3\n"
        "end\n"
        "while false then\n"
        "    var d: int32 = 4\n"
        "end\n";

    tc_diagnostic_init(&diag);
    check(compile_source(source, &typed, &diag) == 0, "compile cfg size sample");
    if (typed.cfg) {
        check(typed.cfg->node_count <= 12, "cfg nodes remain linear in source statements");
        check(typed.cfg->edge_count <= 16, "cfg edges remain linear in source statements");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_sequential_cfg();
    test_structured_edges();
    test_goto_edge();
    test_constant_loop_pruning();
    test_let_constant_branch_pruning();
    test_local_let_shadow_branch_pruning();
    test_static_bool_rhs_pruning();
    test_short_circuit_read_sets();
    test_cfg_size_is_linear();
    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
