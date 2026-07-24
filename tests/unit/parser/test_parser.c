/*
 * test_parser.c — Parser 单元测试
 *
 * 覆盖：
 *   - tc_parse_statement — var / let / write / assign / goto / label
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
    check(stmt.u.var_def.rhs.kind == TC_RHS_LIT, "var def initializer rhs");
    check(strcmp(stmt.u.var_def.name, "x") == 0, "var def name");
    check(stmt.u.var_def.type == TC_INT32, "var def type");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_parse_var_requires_initializer(void) {
    static const char *cases[] = {"var x: int32", "var x: int32 ="};
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcTokenList tokens;
        TcStatement stmt;
        TcParserCtx ctx;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_token_list_init(&tokens);
        memset(&ctx, 0, sizeof(ctx));
        memset(&stmt, 0, sizeof(stmt));
        check(tc_tokenize_line(cases[i], (int)i + 1, &tokens, &diag) == 0,
              "tokenize var missing initializer");
        check(tc_parse_statement(&ctx, &tokens, (int)i + 1, &stmt, &diag) != 0,
              "var missing initializer fails in parser");
        check(diag.kind == TC_CE_VAR_MISSING_INIT,
              "var missing initializer uses dedicated error kind");
        check(strcmp(tc_error_kind_name(diag.kind), "VarMissingInitializer") == 0,
              "var missing initializer print name");
        tc_statement_free(&stmt);
        tc_token_list_free(&tokens);
        tc_diagnostic_clear(&diag);
    }
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

#define TC_PROGRAM_HDR "#program\n"

static void test_parse_module_header_required(void) {
    TcProgram program;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program("var x: int32 = 1\n", &program, &diag) != 0,
          "source without module header fails");
    check(diag.kind == TC_CE_SYNTAX, "missing header is syntax error");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(TC_PROGRAM_HDR "var x: int32 = 1\n", &program, &diag) == 0,
          "source with #program header parses");
    check(program.mode == TC_MODULE_PROGRAM, "program mode set");
    check(program.count == 1, "program body stmt count");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_import(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source = TC_PROGRAM_HDR "import mylib\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse import");
    check(program.count == 1, "import stmt count");
    check(program.items[0].kind == TC_STMT_IMPORT, "import kind");
    check(strcmp(program.items[0].u.import_stmt.module_name, "mylib") == 0, "import name");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_program_struct(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "struct Point then\n"
        "    let x: int32 @padding(0)\n"
        "    var y: int32\n"
        "end\n"
        "var p: Point = nullptr\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse program struct");
    check(program.count == 2, "struct program stmt count");
    check(program.items[0].kind == TC_STMT_STRUCT_DEF, "struct def kind");
    check(strcmp(program.items[0].u.struct_def.name, "Point") == 0, "struct name");
    check(program.items[0].u.struct_def.field_count == 2, "struct field count");
    check(program.items[1].kind == TC_STMT_VAR_DEF, "var after struct");
    check(program.items[1].u.var_def.type == TC_STRUCT, "struct typed var");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_lib_func_and_static(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "#lib\n"
        "public struct Box then\n"
        "    var value: int32\n"
        "end\n"
        "private static let K: int32 = 1\n"
        "public func inc ( n: int32 ) int32 then\n"
        "    return n\n"
        "end\n";

    int parsed = 0;

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    parsed = tc_parse_source_to_program(source, &program, &diag);
    check(parsed == 0, "parse lib func/static");
    if (parsed == 0) {
        check(program.mode == TC_MODULE_LIB, "lib mode set");
        check(program.count == 3, "lib decl count");
        if (program.count >= 3 && program.items) {
            check(program.items[0].kind == TC_STMT_STRUCT_DEF, "lib struct");
            check(program.items[0].u.struct_def.visibility == TC_VIS_PUBLIC, "struct visibility");
            check(program.items[1].kind == TC_STMT_STATIC_LET_DEF, "static let");
            check(program.items[2].kind == TC_STMT_FUNC_DEF, "func def");
            check(strcmp(program.items[2].u.func_def.name, "inc") == 0, "func name");
            check(program.items[2].u.func_def.return_type.kind == TC_INT32, "func return type");
        }
    }
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_missing_visibility_lib(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source = "#lib\nstruct Bad then\nend\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0,
          "lib struct without visibility fails");
    check(diag.kind == TC_CE_MISSING_VISIBILITY, "missing visibility error");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_program_mode_misuse(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source = TC_PROGRAM_HDR "public var x: int32 = 0\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0,
          "public in #program fails");
    check(diag.kind == TC_CE_PROGRAM_MODE_MISUSE, "program mode misuse error");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_module_layer_error(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "var x: int32 = 1\n"
        "import bad\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0,
          "import after value decl fails");
    check(diag.kind == TC_CE_MODULE_LAYER, "module layer error");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_if_program(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
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
        TC_PROGRAM_HDR
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
        TC_PROGRAM_HDR
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
        TC_PROGRAM_HDR
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

static void test_parse_goto(void) {
    TcTokenList tokens;
    TcStatement stmt;
    TcParserCtx ctx;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    memset(&ctx, 0, sizeof(ctx));
    check(tc_tokenize_line("goto start", 1, &tokens, &diag) == 0, "tokenize goto");
    check(tc_parse_statement(&ctx, &tokens, 1, &stmt, &diag) == 0, "parse goto");
    check(stmt.kind == TC_STMT_GOTO, "goto kind");
    check(strcmp(stmt.u.goto_stmt.target, "start") == 0, "goto target");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_parse_label(void) {
    TcTokenList tokens;
    TcStatement stmt;
    TcParserCtx ctx;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    memset(&ctx, 0, sizeof(ctx));
    check(tc_tokenize_line("label start:", 1, &tokens, &diag) == 0, "tokenize label");
    check(tc_parse_statement(&ctx, &tokens, 1, &stmt, &diag) == 0, "parse label");
    check(stmt.kind == TC_STMT_LABEL_DEF, "label kind");
    check(strcmp(stmt.u.label_def.name, "start") == 0, "label name");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_parse_label_missing_colon(void) {
    TcTokenList tokens;
    TcStatement stmt;
    TcParserCtx ctx;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_token_list_init(&tokens);
    memset(&ctx, 0, sizeof(ctx));
    check(tc_tokenize_line("label start", 1, &tokens, &diag) == 0, "tokenize label no colon");
    check(tc_parse_statement(&ctx, &tokens, 1, &stmt, &diag) != 0, "parse label missing colon fails");
    check(diag.kind == TC_CE_SYNTAX, "missing colon → SyntaxError");
    tc_statement_free(&stmt);
    tc_token_list_free(&tokens);
    tc_diagnostic_clear(&diag);
}

static void test_parse_goto_in_if_block(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "label done:\n"
        "if true then\n"
        "    goto done\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse goto in if");
    check(program.count == 2, "goto-in-if top count");
    check(program.items[0].kind == TC_STMT_LABEL_DEF, "first stmt label");
    check(program.items[1].kind == TC_STMT_IF, "second stmt if");
    check(program.items[1].u.if_stmt.then_count == 1, "then has goto");
    check(program.items[1].u.if_stmt.then_body[0].kind == TC_STMT_GOTO, "then body is goto");
    check(strcmp(program.items[1].u.if_stmt.then_body[0].u.goto_stmt.target, "done") == 0,
          "then goto target");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_nested_if(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
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

static void test_parse_while_programs(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "while true then\n"
        "    if false then\n"
        "        break\n"
        "    else\n"
        "        continue\n"
        "    end\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(TC_PROGRAM_HDR "while false then\nend\n", &program, &diag) == 0,
          "parse empty while body");
    check(program.count == 1 && program.items && program.items[0].kind == TC_STMT_WHILE,
          "empty while produces while statement");
    if (program.count == 1 && program.items && program.items[0].kind == TC_STMT_WHILE) {
        check(program.items[0].u.while_stmt.body_count == 0, "empty while body count");
        check(program.items[0].u.while_stmt.loop_id == -1,
              "parser leaves while loop id unresolved");
    }
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0,
          "parse while containing if and loop controls");
    check(program.count == 1 && program.items && program.items[0].kind == TC_STMT_WHILE,
          "nested source top-level while");
    if (program.count == 1 && program.items && program.items[0].kind == TC_STMT_WHILE &&
        program.items[0].u.while_stmt.body_count == 1 &&
        program.items[0].u.while_stmt.body[0].kind == TC_STMT_IF) {
        const TcIfStmt *nested_if = &program.items[0].u.while_stmt.body[0].u.if_stmt;

        check(1, "while body contains nested if");
        check(nested_if->then_count == 1 && nested_if->then_body[0].kind == TC_STMT_BREAK,
              "parse break as statement");
        check(nested_if->else_count == 1 && nested_if->else_body[0].kind == TC_STMT_CONTINUE,
              "parse continue as statement");
        if (nested_if->then_count == 1 && nested_if->then_body[0].kind == TC_STMT_BREAK) {
            check(nested_if->then_body[0].u.break_stmt.loop_id == -1,
                  "parser leaves break loop id unresolved");
        }
    } else {
        check(0, "while body contains nested if");
    }
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_while_missing_end(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "while true then\n"
        "    writeln(int32, 1)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0,
          "while missing end fails");
    check(diag.kind == TC_CE_MISSING_END, "while missing end kind");
    check(strstr(diag.message, "missing end for while statement") != NULL,
          "while missing end message");
    tc_diagnostic_clear(&diag);
}

static void test_parse_while_inside_if(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "if true then\n"
        "    while false then\n"
        "    end\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse while inside if");
    check(program.count == 1 && program.items && program.items[0].kind == TC_STMT_IF &&
              program.items[0].u.if_stmt.then_count == 1 &&
              program.items[0].u.if_stmt.then_body[0].kind == TC_STMT_WHILE,
          "if body contains while");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_bitcast_rhs(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "var bits: uint32 = bitcast(uint32, 0x3F800000u)\n"
        "let payload: uint64 = bitcast(uint64, 0x7FF8000000001234u)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse runtime and let bitcast");
    if (program.count == 2) {
        check(program.items[0].u.var_def.rhs.kind == TC_RHS_BITCAST,
              "runtime bitcast rhs kind");
        check(program.items[0].u.var_def.rhs.u.bitcast.target == TC_UINT32,
              "runtime bitcast target");
        check(program.items[0].u.var_def.rhs.u.bitcast.source.kind == TC_OPERAND_LIT,
              "runtime bitcast literal source");
        check(program.items[1].u.const_def.rhs.kind == TC_RHS_BITCAST,
              "let bitcast rhs kind");
    }
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_bitcast_invalid_syntax(void) {
    static const char *sources[] = {
        TC_PROGRAM_HDR "var bits: uint32 = bitcast(uint32)\n",
        TC_PROGRAM_HDR "var bits: uint32 = bitcast(uint32, truncate, 1u)\n",
    };
    size_t i = 0;

    for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        TcProgram program;
        TcDiagnostic diag;

        tc_diagnostic_init(&diag);
        tc_program_init(&program);
        check(tc_parse_source_to_program(sources[i], &program, &diag) != 0,
              "invalid bitcast syntax is rejected by parser");
        check(diag.kind == TC_CE_SYNTAX,
              "invalid bitcast syntax reports SyntaxError");
        tc_program_free(&program);
        tc_diagnostic_clear(&diag);
    }
}

static void test_parse_type_ptr_memblock(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "var p: ptr<int32> = nullptr\n"
        "var mb: memblock<int32, 4> = nullptr\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse ptr/memblock types");
    check(program.count == 2, "two typed vars");
    if (program.count >= 2) {
        check(program.items[0].u.var_def.full_type.kind == TC_PTR, "ptr full_type");
        check(program.items[0].u.var_def.full_type.params.ptr_type.pointee != NULL &&
                  program.items[0].u.var_def.full_type.params.ptr_type.pointee->kind == TC_INT32,
              "ptr pointee int32");
        check(program.items[1].u.var_def.full_type.kind == TC_MEMBLOCK, "memblock full_type");
        check(program.items[1].u.var_def.full_type.params.memblock_type.count == 4,
              "memblock count 4");
    }
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_return_and_funcall(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        "#lib\n"
        "public func bump ( n: int32 ) int32 then\n"
        "    return n\n"
        "end\n"
        "public func noop ( ) void then\n"
        "    return\n"
        "end\n"
        "public func call_it ( ) void then\n"
        "    funcall(Self.bump, n: 1)\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse return/funcall lib");
    check(program.count == 3, "three funcs");
    if (program.count >= 3) {
        TcFuncDef *bump = &program.items[0].u.func_def;
        TcFuncDef *noop = &program.items[1].u.func_def;
        TcFuncDef *call_it = &program.items[2].u.func_def;

        check(bump->body_count == 1 && bump->body[0].kind == TC_STMT_RETURN,
              "bump has return stmt");
        check(bump->body[0].u.return_stmt.has_value, "return with value");
        check(noop->return_type.kind == TC_VOID, "noop returns void");
        check(noop->body_count == 1 && noop->body[0].kind == TC_STMT_RETURN,
              "noop has bare return");
        check(!noop->body[0].u.return_stmt.has_value, "bare return has no value");
        check(call_it->body_count == 1 && call_it->body[0].kind == TC_STMT_FUNCALL,
              "funcall stmt");
        check(call_it->body[0].u.funcall_stmt.is_self, "funcall Self target");
        check(call_it->body[0].u.funcall_stmt.member_name != NULL &&
                  strcmp(call_it->body[0].u.funcall_stmt.member_name, "bump") == 0,
              "funcall member bump");
        check(call_it->body[0].u.funcall_stmt.arg_count == 1, "funcall one named arg");
    }
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_program_rejects_func_static(void) {
    TcProgram program;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(TC_PROGRAM_HDR "func bad ( ) void then\nend\n", &program,
                                     &diag) != 0,
          "func in #program fails");
    check(diag.kind == TC_CE_PROGRAM_MODE_MISUSE, "func misuse kind");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(TC_PROGRAM_HDR "static let K: int32 = 1\n", &program,
                                     &diag) != 0,
          "static in #program fails");
    check(diag.kind == TC_CE_PROGRAM_MODE_MISUSE, "static misuse kind");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_parse_field_assign_and_ptr_store(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source =
        TC_PROGRAM_HDR
        "struct Point then\n"
        "    var x: int32\n"
        "end\n"
        "var p: Point = nullptr\n"
        "p.x = 1\n"
        "var q: ptr<int32> = nullptr\n"
        "ptr_store(int32, q, 2)\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0,
          "parse field assign and ptr_store");
    check(program.count == 5, "struct+vars+stmts");
    if (program.count >= 5) {
        check(program.items[2].kind == TC_STMT_FIELD_ASSIGN, "field assign kind");
        check(program.items[4].kind == TC_STMT_PTR_STORE, "ptr_store kind");
    }
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_parse_module_header_required();
    test_parse_import();
    test_parse_program_struct();
    test_parse_lib_func_and_static();
    test_parse_missing_visibility_lib();
    test_parse_program_mode_misuse();
    test_parse_module_layer_error();
    test_parse_type_ptr_memblock();
    test_parse_return_and_funcall();
    test_parse_program_rejects_func_static();
    test_parse_field_assign_and_ptr_store();
    test_parse_var_def();
    test_parse_var_requires_initializer();
    test_parse_let_const();
    test_parse_write();
    test_parse_goto();
    test_parse_label();
    test_parse_label_missing_colon();
    test_parse_goto_in_if_block();
    test_parse_if_program();
    test_parse_if_else_program();
    test_parse_missing_end();
    test_parse_indent_insufficient();
    test_parse_nested_if();
    test_parse_while_programs();
    test_parse_while_missing_end();
    test_parse_while_inside_if();
    test_parse_bitcast_rhs();
    test_parse_bitcast_invalid_syntax();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
