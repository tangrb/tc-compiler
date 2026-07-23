/*
 * test_analyzer.c — 静态分析器单元测试
 *
 * 覆盖：
 *   - tc_analyze — 合法程序 Pass1/Pass2
 *   - if 条件类型错误、跨块引用、let 编译期求值
 *   - 未初始化变量警告
 */
#include "tc_analyzer.h"

#include "tc_cfg.h"
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
        "#program\nvar x: int32 = 10\n"
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
        "#program\nlet N: int32 = 42\n"
        "let M: int32 = add(int32, N, 8)\n"
        "writeln(int32, M)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse let program");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze let program");
    check(typed.symbols.count == 2, "let symbol count");
    check(typed.symbols.symbols[0].slot == -1, "let N has no runtime slot");
    check(typed.symbols.symbols[1].slot == -1, "let M has no runtime slot");
    check(typed.symbols.symbols[1].has_const_value != 0, "let M has const value");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_let_does_not_consume_var_slots(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nlet A: int32 = 1\n"
        "var x: int32 = A\n"
        "let B: int32 = 2\n"
        "var y: int32 = B\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0,
          "parse interleaved let and var slots");
    check(tc_analyze(&program, &typed, &diag) == 0,
          "analyze interleaved let and var slots");
    if (typed.symbols.count == 4) {
        check(typed.symbols.symbols[0].slot == -1,
              "first let has no runtime slot");
        check(typed.symbols.symbols[1].slot == 0,
              "first var owns runtime slot zero");
        check(typed.symbols.symbols[2].slot == -1,
              "second let has no runtime slot");
        check(typed.symbols.symbols[3].slot == 1,
              "second var owns contiguous runtime slot one");
    } else {
        check(0, "interleaved declarations produce four symbols");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_let_allowed_forms(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nlet LITERAL: int32 = 42\n"
        "let EARLIER: int32 = add(int32, LITERAL, 8)\n"
        "let WRAP: int8 = add(int8, wrap, 120, 20)\n"
        "let IEEE: float32 = div(float32, ieee, 1.0f, 0.0f)\n"
        "let TRUNCATED: int8 = cast(int8, truncate, 1000)\n"
        "let BITS: uint32 = bitcast(uint32, 1.0f)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0,
          "parse all permitted let forms");
    check(tc_analyze(&program, &typed, &diag) == 0,
          "analyze all permitted let forms");
    if (typed.symbols.count == 6) {
        check(typed.symbols.symbols[1].const_value.bits == 50,
              "earlier let feeds one non-nested call");
        check(typed.symbols.symbols[2].const_value.bits == UINT8_C(0x8c),
              "integer wrap is permitted in let");
        check(typed.symbols.symbols[3].const_value.bits == UINT32_C(0x7f800000),
              "float ieee is permitted in let");
        check(typed.symbols.symbols[4].const_value.bits == UINT8_C(0xe8),
              "integer truncate is permitted in let");
        check(typed.symbols.symbols[5].const_value.bits == UINT32_C(0x3f800000),
              "equal-width bitcast is permitted in let");
    } else {
        check(0, "all permitted let forms produce symbols");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_let_nested_call_error(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "#program\nlet BAD: int32 = add(int32, mul(int32, 2, 3), 4)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0,
          "nested let call is rejected while parsing");
    check(diag.kind == TC_ERR_CONSTANT_EXPRESSION,
          "nested let call uses constant-expression error");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_let_reference_errors(void) {
    static const struct {
        const char *source;
        TcErrorKind kind;
        const char *message;
    } cases[] = {
        {
            "#program\nvar runtime: int32 = 1\n"
            "let BAD: int32 = add(int32, runtime, 1)\n",
            TC_ERR_CONSTANT_EXPRESSION,
            "#program\nlet rejects var reference",
        },
        {
            "#program\nvar runtime: int64 = 1\n"
            "let BAD: int32 = cast(int32, runtime)\n",
            TC_ERR_CONSTANT_EXPRESSION,
            "#program\nlet cast rejects var reference",
        },
        {
            "#program\nvar runtime: uint32 = 0x3F800000u\n"
            "let BAD: float32 = bitcast(float32, runtime)\n",
            TC_ERR_CONSTANT_EXPRESSION,
            "#program\nlet bitcast rejects var reference",
        },
        {
            "#program\nlet SELF: int32 = SELF\n",
            TC_ERR_UNDEFINED_VARIABLE,
            "#program\nlet self-reference is undefined by source order",
        },
        {
            "#program\nlet FIRST: int32 = add(int32, LATER, 1)\n"
            "let LATER: int32 = 1\n",
            TC_ERR_UNDEFINED_VARIABLE,
            "#program\nlet forward reference is undefined by source order",
        },
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(cases[i].source, &program, &diag) == 0,
              "parse let reference error case");
        check(tc_analyze(&program, &typed, &diag) != 0, cases[i].message);
        check(diag.kind == cases[i].kind, "let reference error kind");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_block_local_let_chain(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nif true then\n"
        "    let A: int32 = 1\n"
        "    let B: int32 = A\n"
        "    let C: int32 = add(int32, B, 1)\n"
        "    let D: int16 = cast(int16, truncate, C)\n"
        "    let E: uint16 = bitcast(uint16, D)\n"
        "    writeln(uint16, E)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0,
          "parse block-local let chain");
    check(tc_analyze(&program, &typed, &diag) == 0,
          "analyze block-local let chain");
    if (typed.symbols.count == 5) {
        check(typed.symbols.symbols[1].const_value.bits == 1,
              "local plain let reference propagates value");
        check(typed.symbols.symbols[2].const_value.bits == 2,
              "local arithmetic let reference propagates value");
        check(typed.symbols.symbols[3].const_value.bits == 2,
              "local cast let reference propagates value");
        check(typed.symbols.symbols[4].const_value.bits == 2,
              "local bitcast let reference propagates value");
    } else {
        check(0, "block-local let chain produces five symbols");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_short_circuit_still_validates_rhs(void) {
    static const struct {
        const char *source;
        TcErrorKind kind;
        const char *message;
    } cases[] = {
        {
            "#program\nlet BAD: bool = and(bool, false, missing)\n",
            TC_ERR_UNDEFINED_VARIABLE,
            "and false still validates missing rhs",
        },
        {
            "#program\nlet BAD: bool = or(bool, true, missing)\n",
            TC_ERR_UNDEFINED_VARIABLE,
            "or true still validates missing rhs",
        },
        {
            "#program\nlet BAD: bool = and(bool, false, LATER)\n"
            "let LATER: bool = true\n",
            TC_ERR_UNDEFINED_VARIABLE,
            "and false still validates forward rhs",
        },
        {
            "#program\nlet BAD: bool = or(bool, true, LATER)\n"
            "let LATER: bool = false\n",
            TC_ERR_UNDEFINED_VARIABLE,
            "or true still validates forward rhs",
        },
        {
            "#program\nvar runtime: bool = true\n"
            "let BAD: bool = and(bool, false, runtime)\n",
            TC_ERR_CONSTANT_EXPRESSION,
            "and false still rejects var rhs",
        },
        {
            "#program\nvar runtime: bool = false\n"
            "let BAD: bool = or(bool, true, runtime)\n",
            TC_ERR_CONSTANT_EXPRESSION,
            "or true still rejects var rhs",
        },
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(cases[i].source, &program, &diag) == 0,
              "parse short-circuit validation case");
        check(tc_analyze(&program, &typed, &diag) != 0, cases[i].message);
        check(diag.kind == cases[i].kind, "short-circuit rhs diagnostic kind");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_let_mode_matrix(void) {
    static const char *cases[] = {
        "#program\nlet BAD: uint8 = add(uint8, wrap, 255u, 1u)\n",
        "#program\nlet BAD: uint8 = neg(uint8, wrap, 1u)\n",
        "#program\nlet BAD: int32 = div(int32, wrap, 4, 2)\n",
        "#program\nlet BAD: int32 = mod(int32, wrap, 4, 2)\n",
        "#program\nlet BAD: int32 = abs(int32, wrap, -1)\n",
        "#program\nlet BAD: float64 = neg(float64, ieee, 1.0)\n",
        "#program\nlet BAD: bool = eq(float64, ieee, 1.0, 1.0)\n",
        "#program\nvar BAD: uint8 = add(uint8, wrap, 255u, 1u)\n",
        "#program\nvar BAD: uint8 = neg(uint8, wrap, 1u)\n",
        "#program\nvar BAD: float64 = neg(float64, ieee, 1.0)\n",
        "#program\nvar BAD: bool = eq(float64, ieee, 1.0, 1.0)\n",
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(cases[i], &program, &diag) == 0,
              "parse forbidden mode matrix case");
        check(tc_analyze(&program, &typed, &diag) != 0,
              "reject forbidden operation/type/mode combination");
        check(diag.kind == TC_ERR_MODE_MISMATCH,
              "forbidden combination uses mode mismatch");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_unreachable_let_branch_still_checks_names(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nlet NEVER: bool = false\n"
        "if NEVER then\n"
        "    writeln(int32, missing)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0,
          "parse statically unreachable let branch");
    check(tc_analyze(&program, &typed, &diag) != 0,
          "unreachable let branch still checks names");
    check(diag.kind == TC_ERR_UNDEFINED_VARIABLE,
          "unreachable let branch preserves undefined-variable error");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_diagnostic_priority_matrix(void) {
    static const struct {
        const char *source;
        int parse_failure;
        TcErrorKind kind;
        const char *message;
    } cases[] = {
        {
            "#program\nvar value: int32 = @\n"
            "writeln(int32, missing)\n",
            1,
            TC_ERR_SYNTAX,
            "lexical error precedes later name error",
        },
        {
            "#program\nvar result: bool = and(bool, missing)\n",
            1,
            TC_ERR_SYNTAX,
            "syntax error precedes missing operand name resolution",
        },
        {
            "#program\nvar result: bool = and(bool, 1, missing)\n",
            0,
            TC_ERR_UNDEFINED_VARIABLE,
            "name resolution precedes operand type checking in one RHS",
        },
        {
            "#program\nvar bad: float32 = add(float32, wrap, 1.0, 2.0)\n",
            0,
            TC_ERR_MODE_MISMATCH,
            "mode legality precedes literal context typing",
        },
        {
            "#program\nlet BAD: int32 = div(int32, 1, 0)\n"
            "goto use\n"
            "var pending: int32 = 1\n"
            "label use:\n"
            "writeln(int32, pending)\n",
            0,
            TC_ERR_CONSTANT_DIV_ZERO,
            "constant evaluation precedes CFG definite-init failure",
        },
        {
            "#program\nlet NEVER: bool = false\n"
            "if NEVER then\n"
            "    writeln(int32, missing)\n"
            "end\n",
            0,
            TC_ERR_UNDEFINED_VARIABLE,
            "unreachable branch still reports name error before CFG",
        },
        {
            "#program\nlet NEVER: bool = false\n"
            "var number: int32 = 1\n"
            "if NEVER then\n"
            "    var bad: bool = and(bool, false, number)\n"
            "end\n",
            0,
            TC_ERR_TYPE_MISMATCH,
            "unreachable branch still reports type error before CFG",
        },
        {
            "#program\nvar runtime: bool = false\n"
            "goto use\n"
            "var pending: bool = true\n"
            "label use:\n"
            "var result: bool = and(bool, runtime, pending)\n",
            0,
            TC_ERR_UNINITIALIZED_VARIABLE,
            "DFA reports uninitialized read after all earlier phases pass",
        },
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;
        int parse_status = 0;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        parse_status = tc_parse_source_to_program(cases[i].source, &program, &diag);
        if (cases[i].parse_failure) {
            check(parse_status != 0, cases[i].message);
        } else {
            check(parse_status == 0, "parse diagnostic-priority analyzer case");
            if (parse_status == 0) {
                check(tc_analyze(&program, &typed, &diag) != 0, cases[i].message);
            }
        }
        check(diag.kind == cases[i].kind, "diagnostic-priority matrix error kind");
        if (parse_status != 0) {
            tc_program_free(&program);
        } else {
            tc_typed_program_free(&typed);
        }
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_let_runtime_error_mapping(void) {
    static const struct {
        const char *source;
        TcErrorKind kind;
    } cases[] = {
        {"#program\nlet BAD: int8 = add(int8, 127, 1)\n", TC_ERR_CONSTANT_OVERFLOW},
        {"#program\nlet BAD: int32 = div(int32, 1, 0)\n", TC_ERR_CONSTANT_DIV_ZERO},
        {"#program\nlet BAD: int8 = cast(int8, 128)\n", TC_ERR_CONSTANT_CAST_OVERFLOW},
        {"#program\nlet BAD: float32 = div(float32, 0.0f, 0.0f)\n",
         TC_ERR_CONSTANT_EXPRESSION},
        {"#program\nlet BAD: float32 = mul(float32, 3.4e38f, 2.0f)\n",
         TC_ERR_CONSTANT_OVERFLOW},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(cases[i].source, &program, &diag) == 0,
              "parse let runtime-error mapping case");
        check(tc_analyze(&program, &typed, &diag) != 0,
              "let runtime semantic error fails analysis");
        check(diag.kind == cases[i].kind, "let runtime error maps to constant error");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_if_condition_type(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nif add(int32, 1, 2) then\n"
        "    writeln(int32, 1)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse if arith cond");
    check(tc_analyze(&program, &typed, &diag) != 0, "analyze if arith cond fails");
    check(strstr(diag.message, "if condition must be bool") != NULL, "#program\nif cond type message");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_cross_block_reference(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nif true then\n"
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
        "#program\ngoto use\n"
        "var a: int32 = 1\n"
        "label use:\n"
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
        "#program\nvar condition: int32 = 1\n"
        "goto branch\n"
        "var x: int32 = 0\n"
        "label branch:\n"
        "if eq(int32, condition, 1) then\n"
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
        "#program\ngoto branch\n"
        "var x: int32 = 0\n"
        "label branch:\n"
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
        "#program\nvar flag: bool = false\n"
        "goto use\n"
        "var uninit: bool = true\n"
        "label use:\n"
        "var result: bool = and(bool, false, uninit)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse shortcircuit");
    check(tc_analyze(&program, &typed, &diag) == 0, "shortcircuit skips uninit rhs");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static int first_condition_value(const TcCfg *cfg) {
    size_t i = 0;

    if (!cfg) {
        return -2;
    }
    for (i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i].kind == TC_CFG_BRANCH ||
            cfg->nodes[i].kind == TC_CFG_LOOP_CONDITION) {
            return cfg->nodes[i].constant_condition;
        }
    }
    return -2;
}

static void test_analyze_static_bool_operand_matrix(void) {
    static const struct {
        const char *source;
        int expected;
        const char *message;
    } valid_cases[] = {
        {
            "#program\nif true then\n"
            "    writeln(bool, true)\n"
            "end\n",
            1,
            "bool literal is a static condition",
        },
        {
            "#program\nlet FLAG: bool = false\n"
            "if FLAG then\n"
            "    writeln(bool, true)\n"
            "end\n",
            0,
            "earlier visible let bool is a static condition",
        },
        {
            "#program\nvar flag: bool = false\n"
            "if flag then\n"
            "    writeln(bool, true)\n"
            "end\n",
            -1,
            "#program\nvar bool remains an unknown static condition",
        },
        {
            "#program\nlet TEN: int32 = 10\n"
            "let FIVE: int32 = 5\n"
            "if gt(int32, TEN, FIVE) then\n"
            "    writeln(bool, true)\n"
            "end\n",
            1,
            "single-level comparison is a static condition",
        },
        {
            "#program\nlet LOW: float64 = 1.0\n"
            "let HIGH: float64 = 2.0\n"
            "if lt(float64, LOW, HIGH) then\n"
            "    writeln(bool, true)\n"
            "end\n",
            1,
            "single-level float comparison is a static condition",
        },
        {
            "#program\nlet A: bool = true\n"
            "let B: bool = false\n"
            "if and(bool, A, B) then\n"
            "    writeln(bool, true)\n"
            "end\n",
            0,
            "single-level logic binary RHS is a static condition",
        },
        {
            "#program\nlet FLAG: bool = false\n"
            "if not(bool, FLAG) then\n"
            "    writeln(bool, true)\n"
            "end\n",
            1,
            "single-level logic unary RHS is a static condition",
        },
        {
            "#program\nlet ZERO: int32 = 0\n"
            "if cast(bool, ZERO) then\n"
            "    writeln(bool, true)\n"
            "end\n",
            0,
            "legal strict bool cast is a static condition",
        },
    };
    static const char *invalid_cases[] = {
        "#program\nif LATER then\n"
        "    writeln(bool, true)\n"
        "end\n"
        "let LATER: bool = false\n",
        "#program\nif true then\n"
        "    let LOCAL: bool = false\n"
        "end\n"
        "if LOCAL then\n"
        "    writeln(bool, true)\n"
        "end\n",
    };
    size_t i = 0;

    for (i = 0; i < sizeof(valid_cases) / sizeof(valid_cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(valid_cases[i].source, &program, &diag) == 0,
              "parse static bool operand case");
        check(tc_analyze(&program, &typed, &diag) == 0, valid_cases[i].message);
        check(first_condition_value(typed.cfg) == valid_cases[i].expected,
              "static bool operand produces expected tri-state value");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }

    for (i = 0; i < sizeof(invalid_cases) / sizeof(invalid_cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(invalid_cases[i], &program, &diag) == 0,
              "parse unavailable let condition case");
        check(tc_analyze(&program, &typed, &diag) != 0,
              "forward or invisible let condition is rejected before CFG");
        check(diag.kind == TC_ERR_UNDEFINED_VARIABLE,
              "unavailable let condition uses name-stage diagnostic");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_static_bool_dfa_pruning(void) {
    static const char *cases[] = {
        "#program\nlet TEN: int32 = 10\n"
        "let FIVE: int32 = 5\n"
        "goto branch\n"
        "var value: int32 = 0\n"
        "label branch:\n"
        "if gt(int32, TEN, FIVE) then\n"
        "    value = 10\n"
        "end\n"
        "writeln(int32, value)\n",
        "#program\nlet FALSE: bool = false\n"
        "goto use\n"
        "var pending: bool = true\n"
        "label use:\n"
        "var result: bool = and(bool, FALSE, pending)\n",
        "#program\nlet TEN: int32 = 10\n"
        "let FIVE: int32 = 5\n"
        "goto use\n"
        "var pending: int32 = 1\n"
        "label use:\n"
        "while lt(int32, TEN, FIVE) then\n"
        "    writeln(int32, pending)\n"
        "end\n",
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(cases[i], &program, &diag) == 0,
              "parse static bool DFA case");
        check(tc_analyze(&program, &typed, &diag) == 0,
              "pruned predecessors do not enter definite-init intersection");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_if_block_scope(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nvar x: int32 = 1\n"
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
    const char *source = "#program\nlet A: int32 = add(int32, A, 1)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse cyclic let");
    check(tc_analyze(&program, &typed, &diag) != 0, "analyze cyclic let fails");
    check(diag.kind == TC_ERR_UNDEFINED_VARIABLE,
          "self-reference is undefined, never ConstantCircular");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_duplicate_label(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nlabel dup:\n"
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
        "#program\nif true then\n"
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
        "#program\nlabel start:\n"
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
        "#program\ngoto skip\n"
        "label skip:\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse forward goto");
    check(tc_analyze(&program, &typed, &diag) == 0, "forward goto ok");
    check(typed.program.items[0].u.goto_stmt.resolved != 0, "forward goto is resolved");
    check(typed.program.items[0].u.goto_stmt.resolved_target_stmt_index == 1,
          "forward goto stores target stmt index");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_goto_undefined(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source = "#program\ngoto nonexistent\n";

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
        "#program\ngoto inner\n"
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
        "#program\nif true then\n"
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
        "#program\nlabel after:\n"
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

static void test_analyze_while_scope_and_slots(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nvar x: int32 = 1\n"
        "while false then\n"
        "    var x: int32 = 2\n"
        "    writeln(int32, x)\n"
        "end\n"
        "writeln(int32, x)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse while scope");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze while scope");
    if (typed.program.count == 3 && typed.program.items[1].kind == TC_STMT_WHILE) {
        check(typed.symbols.count == 2, "while shadow creates two fixed slots");
        check(typed.symbols.symbols[0].slot != typed.symbols.symbols[1].slot,
              "while shadow slots are unique");
        check(typed.program.items[1].u.while_stmt.loop_id == 0, "outer while gets loop id zero");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_nested_loop_targets(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source =
        "#program\nwhile false then\n"
        "    while false then\n"
        "        continue\n"
        "    end\n"
        "    break\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse nested loops");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze nested loops");
    if (typed.program.count == 1 && typed.program.items[0].kind == TC_STMT_WHILE) {
        TcWhileStmt *outer = &typed.program.items[0].u.while_stmt;

        check(outer->loop_id == 0, "outer loop id zero");
        check(outer->body_count == 2 && outer->body[0].kind == TC_STMT_WHILE,
              "outer body retains nested loop");
        if (outer->body_count == 2 && outer->body[0].kind == TC_STMT_WHILE) {
            TcWhileStmt *inner = &outer->body[0].u.while_stmt;

            check(inner->loop_id == 1, "inner loop id one");
            check(inner->body[0].u.continue_stmt.loop_id == 1,
                  "continue binds innermost loop");
            check(outer->body[1].u.break_stmt.loop_id == 0, "break binds outer loop");
        }
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_loop_context_errors(void) {
    static const struct {
        const char *source;
        TcErrorKind kind;
    } cases[] = {
        {"#program\nbreak\n", TC_ERR_BREAK_OUTSIDE_LOOP},
        {"#program\ncontinue\n", TC_ERR_CONTINUE_OUTSIDE_LOOP},
        {"#program\nwhile false then\n    goto out\nend\nlabel out:\n", TC_ERR_GOTO_INSIDE_LOOP},
        {"#program\nwhile false then\n    label inner:\nend\n", TC_ERR_LABEL_INSIDE_LOOP},
        {"#program\nwhile false then\n    if true then\n        goto out\n    end\nend\nlabel out:\n",
         TC_ERR_GOTO_INSIDE_LOOP},
        {"#program\nwhile false then\n    if true then\n        label inner:\n    end\nend\n",
         TC_ERR_LABEL_INSIDE_LOOP},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(cases[i].source, &program, &diag) == 0,
              "parse loop context error case");
        check(tc_analyze(&program, &typed, &diag) != 0, "loop context case fails analysis");
        check(diag.kind == cases[i].kind, "loop context uses dedicated error kind");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_while_condition_type(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *source = "#program\nwhile add(int32, 1, 2) then\nend\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse while bad condition");
    check(tc_analyze(&program, &typed, &diag) != 0, "while bad condition fails");
    check(diag.kind == TC_ERR_CONDITION_TYPE, "while condition uses condition type error");
    check(diag.message && strstr(diag.message, "while condition must be bool") != NULL,
          "while condition message");
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_bitcast(void) {
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    const char *valid =
        "#program\nvar f: float32 = 1.0f\n"
        "var bits: uint32 = bitcast(uint32, f)\n"
        "var f2: float32 = bitcast(float32, 0x3F800000u)\n"
        "var inf_bits: uint32 = bitcast(uint32, inf)\n"
        "var nan_bits: uint64 = bitcast(uint64, nan)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(valid, &program, &diag) == 0, "parse valid bitcast");
    check(tc_analyze(&program, &typed, &diag) == 0, "analyze valid bitcast");
    if (typed.program.count == 5) {
        TcBitcastRhs *from_var = &typed.program.items[1].u.var_def.rhs.u.bitcast;
        TcBitcastRhs *from_lit = &typed.program.items[2].u.var_def.rhs.u.bitcast;
        TcBitcastRhs *inf_lit = &typed.program.items[3].u.var_def.rhs.u.bitcast;
        TcBitcastRhs *nan_lit = &typed.program.items[4].u.var_def.rhs.u.bitcast;

        check(from_var->source_type_resolved && from_var->source_type == TC_FLOAT32,
              "bitcast variable source type resolved");
        check(from_lit->source_type_resolved && from_lit->source_type == TC_UINT32,
              "bitcast literal source type resolved from target width");
        check(inf_lit->source_type_resolved && inf_lit->source_type == TC_FLOAT32,
              "bitcast inf source type follows 32-bit target width");
        check(nan_lit->source_type_resolved && nan_lit->source_type == TC_FLOAT64,
              "bitcast nan source type follows 64-bit target width");
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_analyze_bitcast_errors(void) {
    static const struct {
        const char *source;
        TcErrorKind kind;
    } cases[] = {
        {"#program\nvar f: float32 = 1.0f\nvar bad: uint64 = bitcast(uint64, f)\n",
         TC_ERR_BITCAST_WIDTH},
        {"#program\nvar b: bool = true\nvar bad: uint8 = bitcast(uint8, b)\n", TC_ERR_TYPE_MISMATCH},
        {"#program\nvar bad: bool = bitcast(bool, 1u)\n", TC_ERR_TYPE_MISMATCH},
        {"#program\nvar bad: float32 = bitcast(float32, 1.0)\n", TC_ERR_BITCAST_WIDTH},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcProgram program;
        TcTypedProgram typed;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        tc_typed_program_init(&typed);
        check(tc_parse_source_to_program(cases[i].source, &program, &diag) == 0,
              "parse bitcast error case");
        check(tc_analyze(&program, &typed, &diag) != 0, "bitcast error fails analysis");
        check(diag.kind == cases[i].kind, "bitcast dedicated static error kind");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
    }
}

static void test_analyze_cast_literal_source_types(void) {
    static const TcTypeKind expected[] = {
        TC_INT64, TC_UINT64, TC_FLOAT32, TC_FLOAT64,
        TC_FLOAT64, TC_FLOAT64, TC_BOOL,
    };
    const char *source =
        "#program\nvar a: int64 = cast(int64, 1)\n"
        "var b: uint64 = cast(uint64, 1u)\n"
        "var c: float32 = cast(float32, 1.0f)\n"
        "var d: float64 = cast(float64, 1.0)\n"
        "var e: float64 = cast(float64, inf)\n"
        "var f: float64 = cast(float64, nan)\n"
        "var g: bool = cast(bool, true)\n";
    TcProgram program;
    TcTypedProgram typed;
    TcDiagnostic diag;
    size_t i = 0;

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_typed_program_init(&typed);
    check(tc_parse_source_to_program(source, &program, &diag) == 0,
          "parse cast literal source-type matrix");
    check(tc_analyze(&program, &typed, &diag) == 0,
          "analyze cast literal source-type matrix");
    if (typed.program.count == sizeof(expected) / sizeof(expected[0])) {
        for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            const TcCastRhs *cast = &typed.program.items[i].u.var_def.rhs.u.cast;
            check(cast->source_type_resolved && cast->source_type == expected[i],
                  "cast literal source type follows standard 8.1.1");
        }
    }
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_analyze_valid_program();
    test_analyze_let_const();
    test_analyze_let_does_not_consume_var_slots();
    test_analyze_let_allowed_forms();
    test_analyze_let_nested_call_error();
    test_analyze_let_reference_errors();
    test_analyze_block_local_let_chain();
    test_analyze_short_circuit_still_validates_rhs();
    test_analyze_let_mode_matrix();
    test_analyze_unreachable_let_branch_still_checks_names();
    test_analyze_diagnostic_priority_matrix();
    test_analyze_let_runtime_error_mapping();
    test_analyze_if_condition_type();
    test_analyze_cross_block_reference();
    test_analyze_uninit_error();
    test_analyze_uninit_if_merge();
    test_analyze_uninit_both_paths_ok();
    test_analyze_shortcircuit_uninit_ok();
    test_analyze_static_bool_operand_matrix();
    test_analyze_static_bool_dfa_pruning();
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
    test_analyze_while_scope_and_slots();
    test_analyze_nested_loop_targets();
    test_analyze_loop_context_errors();
    test_analyze_while_condition_type();
    test_analyze_bitcast();
    test_analyze_bitcast_errors();
    test_analyze_cast_literal_source_types();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
