/*
 * test_type_check.c — Phase 3 完整类型检查（模块 E/G）单元验收
 *
 * 覆盖 nullptr、struct/memblock/ptr 相关静态错误路径与基本通过用例。
 * 端到端见 tests/valid/phase3_*.tc 与 tests/errors/static/*。
 */
#include "tc_analyzer.h"
#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_type_check.h"
#include "tc_types.h"

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

static int analyze_source(const char *source, TcTypedProgram *typed, TcDiagnostic *diag) {
    TcProgram program;

    tc_program_init(&program);
    /* NOTE: tc_analyze (→ tc_analyze_ex) calls tc_typed_program_init internally.
     * Do NOT call tc_typed_program_init here — it would double‑init and leak. */
    if (tc_parse_source_to_program(source, &program, diag) != 0) {
        return -1;
    }
    return tc_analyze(&program, typed, diag);
}

static void expect_err(const char *source, TcErrorKind kind, const char *label) {
    TcTypedProgram typed = {0};
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(analyze_source(source, &typed, &diag) != 0 && diag.kind == kind, label);
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void expect_ok(const char *source, const char *label) {
    TcTypedProgram typed = {0};
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(analyze_source(source, &typed, &diag) == 0, label);
    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

static void test_literal_e4_direct(void) {
    TcLiteral lit;
    TcType expected;
    TcDiagnostic diag;

    memset(&lit, 0, sizeof(lit));
    memset(&expected, 0, sizeof(expected));
    tc_diagnostic_init(&diag);

    lit.is_nullptr = 1;
    expected.kind = TC_INT32;
    check(tc_type_check_literal(&lit, &expected, 1, &diag) != 0 &&
              diag.kind == TC_CE_LITERAL_TYPE,
          "nullptr rejected in int context");
    tc_diagnostic_clear(&diag);

    expected.kind = TC_PTR;
    check(tc_type_check_literal(&lit, &expected, 1, &diag) == 0, "nullptr accepted in ptr context");
    tc_diagnostic_clear(&diag);

    lit.is_nullptr = 0;
    lit.unsigned_suffix = 1;
    lit.magnitude = 42;
    expected.kind = TC_INT32;
    check(tc_type_check_literal(&lit, &expected, 1, &diag) != 0 &&
              diag.kind == TC_CE_LITERAL_TYPE,
          "unsigned suffix rejected in signed context");
    tc_diagnostic_clear(&diag);

    lit.unsigned_suffix = 0;
    lit.is_float_special = 1;
    expected.kind = TC_INT32;
    check(tc_type_check_literal(&lit, &expected, 1, &diag) != 0 &&
              diag.kind == TC_CE_LITERAL_TYPE,
          "float special rejected in int context");
    tc_diagnostic_clear(&diag);
}

static void test_goto_outside_function(void) {
    expect_err("#program\ngoto t\nlabel t:\n", TC_CE_GOTO_OUTSIDE_FUNCTION,
               "toplevel goto → GOTO_OUTSIDE_FUNCTION");
}

static void test_missing_return(void) {
    expect_err("#lib\npublic func f() int32 then\n    var x: int32 = 1\nend\n",
               TC_CE_MISSING_RETURN, "non-void fallthrough → MISSING_RETURN");
}

static void test_unreachable_after_return(void) {
    expect_err("#lib\npublic func f() void then\n    return\n    var x: int32 = 1\nend\n",
               TC_CE_UNREACHABLE_STATEMENT, "code after return → UNREACHABLE");
}

static void test_duplicate_struct(void) {
    expect_err("#program\nstruct Point then\n    var x: int32\nend\n"
               "struct Point then\n    var y: int32\nend\n",
               TC_CE_DUPLICATE_STRUCT, "duplicate struct name");
}

static void test_struct_constructor_errors(void) {
    expect_err("#program\nstruct Point then\n    var x: int32\n    var y: int32\nend\n"
               "var p: Point = Point(x: 1)\n",
               TC_CE_STRUCT_MISSING_FIELD, "struct missing field");
    expect_err("#program\nstruct Point then\n    var x: int32\nend\n"
               "var p: Point = Point(x: 1, z: 2)\n",
               TC_CE_STRUCT_UNKNOWN_FIELD, "struct unknown field");
    expect_err("#program\nstruct Point then\n    var x: int32\n    var y: int32\nend\n"
               "var p: Point = Point(y: 2, x: 1)\n",
               TC_CE_STRUCT_FIELD_ORDER, "struct field order");
}

static void test_memblock_errors(void) {
    expect_err("#program\nvar a: memblock<int32, 2> = memblock(int32, count: 3, 1, 2, 3)\n",
               TC_CE_MEMBLOCK_SIZE_MISMATCH, "memblock ctor size mismatch");
    expect_err("#program\nvar mb: memblock<int32, 2> = memblock(int32, count: 2, 1, 2)\n"
               "var x: int32 = memblock_load(int32, mb, 2)\n",
               TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE, "memblock index oob");
    expect_err("#program\nvar a: memblock<int32, 2> = memblock(int32, count: 2, 1, 2)\n"
               "var b: memblock<int32, 3> = a\n",
               TC_CE_MEMBLOCK_SIZE_MISMATCH, "memblock assign size mismatch");
    expect_err("#program\nvar mb: memblock<int32, 2> = memblock(int32, count: 0, fill: 0)\n",
               TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH, "memblock count zero");
    expect_err("#program\nvar mb: memblock<int32, 2> = memblock(int32, count: 2, fill: true)\n",
               TC_CE_LITERAL_TYPE, "memblock fill type");
    expect_err("#program\nvar mb: memblock<int32, 2> = memblock(int32, count: 2, 1, 2)\n"
               "memblock_store(int32, mb, 2, 9)\n",
               TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE, "memblock store oob");
}

static void test_ptr_errors(void) {
    expect_err("#program\nlet a: int32 = 1\nvar p: ptr<int32> = ptr_address(int32, a)\n",
               TC_CE_CONSTANT_ASSIGNMENT, "ptr_address of let");
    expect_err("#program\nlet p: ptr<int32> = nullptr\nptr_store(int32, p, 2)\n",
               TC_CE_CONSTANT_ASSIGNMENT, "ptr_store through let ptr");
    expect_err("#program\nvar a: int32 = 1\nvar p: ptr<int32> = ptr_address(int32, a)\n"
               "var x: int32 = add(int32, p, 1)\n",
               TC_CE_TYPE_MISMATCH, "ptr scalar arith rejected");
    expect_err("#program\nvar a: int32 = 1\nvar p: ptr<int32> = ptr_address(int32, a)\n"
               "var q: ptr<float64> = ptr_add(int32, p, 1)\n",
               TC_CE_TYPE_MISMATCH, "ptr_add result type");
}

static void test_struct_nested_and_mutability(void) {
    expect_ok("#program\nstruct Inner then\n    var x: int32\nend\n"
              "struct Outer then\n    var inner: Inner\nend\n"
              "var o: Outer = Outer(inner: Inner(x: 7))\n"
              "var v: int32 = o.inner.x\n",
              "nested struct field read ok");
    expect_err("#program\nstruct Point then\n    var x: int32\nend\n"
               "var p: Point = Point(x: 1)\nvar v: int32 = p.x.y\n",
               TC_CE_TYPE_MISMATCH, "nested field through non-struct");
    expect_ok("#program\nstruct Point then\n    var x: int32\nend\n"
              "var p: Point = Point(x: 1)\np.x = 2\n",
              "var×var field assign ok");
    expect_err("#lib\npublic struct Point then\n    var x: int32\nend\n"
               "public func f(p: Point) void then\n    p.x = 2\n    return\nend\n",
               TC_CE_PARAMETER_ASSIGNMENT, "param×var field assign");
}

static void test_phase3_ok_paths(void) {
    expect_ok("#program\nvar p: ptr<int32> = nullptr\n", "nullptr ok");
    expect_ok("#program\nstruct Point then\n    var x: int32\nend\n"
              "var p: Point = Point(x: 1)\nvar v: int32 = p.x\n",
              "struct ctor/field read ok");
    expect_ok("#program\nvar mb: memblock<int32, 2> = memblock(int32, count: 2, 1, 2)\n"
              "var n: usize = mb.count\n",
              "memblock ctor/count ok");
    expect_ok("#program\nvar mb: memblock<int32, 3> = memblock(int32, count: 3, fill: 0)\n",
              "memblock fill ctor ok");
    expect_ok("#program\nvar a: int32 = 1\nvar p: ptr<int32> = ptr_address(int32, a)\n"
              "var q: ptr<int32> = ptr_add(int32, p, 1)\n",
              "ptr_address/add ok");
    expect_ok("#program\nvar a: int32 = 1\nvar p: ptr<int32> = ptr_address(int32, a)\n"
              "ptr_store(int32, p, 2)\nvar b: int32 = ptr_load(int32, p)\n",
              "ptr_load/store ok");
}

static void test_self_member_rhs(void) {
    expect_ok("#lib\npublic static var C: int32 = 7\n"
              "public func f() int32 then\n    var v: int32 = Self.C\n    return v\nend\n",
              "Self.static var RHS ok");
    expect_ok("#lib\npublic static let K: int32 = 3\n"
              "public func f() int32 then\n    var v: int32 = Self.K\n    return v\nend\n",
              "Self.static let RHS ok");
    expect_err("#lib\npublic static var C: int32 = 1\n"
               "public func f() void then\n    var x: int32 = Self.missing\n    return\nend\n",
               TC_CE_UNDEFINED_VARIABLE, "Self.undefined member");
    expect_err("#lib\npublic static var C: int32 = 1\n"
               "public func f() void then\n    var x: bool = Self.C\n    return\nend\n",
               TC_CE_TYPE_MISMATCH, "Self.member type mismatch");
    expect_err("#lib\npublic static var C: int32 = 1\n"
               "public func f() void then\n    var x: int32 = C\n    return\nend\n",
               TC_CE_UNDEFINED_VARIABLE, "bare static name in function");
    expect_err("#program\nstruct Point then\n    let x: int32\nend\n"
               "let p: Point = Point(x: 1)\np.x = 2\n",
               TC_CE_CONSTANT_ASSIGNMENT, "let×let field assign");
    expect_err("#program\nstruct Point then\n    var x: int32\nend\n"
               "let p: Point = Point(x: 1)\np.x = 2\n",
               TC_CE_CONSTANT_ASSIGNMENT, "let×var field assign");
}

static void test_phase4_func_extras(void) {
    expect_err("#lib\npublic func g() void then\n    return\nend\n"
               "public func f(g: int32) void then\n    return\nend\n",
               TC_CE_FUNCTION_NAME_CONFLICT, "param name vs function");
    expect_err("#lib\npublic func f(a: int32) void then\n    var a: int32 = 1\n    return\nend\n",
               TC_CE_DUPLICATE_DEFINITION, "param shadow local");
    expect_ok("#lib\npublic func f() void then\n    goto done\n    label done:\n    return\nend\n",
              "func-local goto ok");
}

int main(void) {
    test_literal_e4_direct();
    test_goto_outside_function();
    test_missing_return();
    test_unreachable_after_return();
    test_duplicate_struct();
    test_struct_constructor_errors();
    test_memblock_errors();
    test_ptr_errors();
    test_struct_nested_and_mutability();
    test_phase3_ok_paths();
    test_self_member_rhs();
    test_phase4_func_extras();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed != 0;
}
