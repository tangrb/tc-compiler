/*
 * test_struct_field_access.c — field_access 作 operand / const-RHS 分析期单元测试
 *
 * 覆盖计划 §5.1 unit 段（test_struct_field_access_*）：
 *   - 类型链校验（a.b.c 嵌套字段读；未知字段；非 struct 中间层）
 *   - .count 语义消歧（memblock 基址 → count；struct 同名字段 → 普通字段读）
 *   - const 上下文（let struct 字段读 ok；var 基址在 let RHS → CONSTANT_EXPRESSION）
 *   - operand 位置字段读（算术 / I/O / return / ptr / memblock 位置）
 *   - hist 注入：直接调用 tc_struct_check_field_access，基址 slot 置
 *     TC_INIT_UNINIT → TC_CE_UNINITIALIZED_VARIABLE（.tc 负例受可达性规则
 *     限制不可构造，见计划 §5.2 备注，改由本测试注入 hist 覆盖）
 */
#include "tc_analyzer.h"
#include "tc_analyzer_internal.h"
#include "tc_diagnostic.h"
#include "tc_parser.h"
#include "tc_struct_check.h"
#include "tc_types.h"

#include <stdio.h>
#include <stdlib.h>
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
     * Do NOT call tc_typed_program_init here — it would double-init and leak. */
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

/* ------------------------------------------------------------------ */
/*  test_struct_field_access_* — 计划 §5.1 unit 段                       */
/* ------------------------------------------------------------------ */

static void test_struct_field_access_chain(void) {
    /* 嵌套字段链 a.b.c 类型校验通过；最终类型匹配 */
    expect_ok("#program\n"
              "struct C then\n    var z: int32\nend\n"
              "struct B then\n    var c: C\nend\n"
              "struct A then\n    var b: B\nend\n"
              "var a: A = A(b: B(c: C(z: 7)))\n"
              "var v: int32 = a.b.c.z\n",
              "nested chain a.b.c ok");
    expect_ok("#program\n"
              "struct C then\n    var z: int32\nend\n"
              "struct B then\n    var c: C\nend\n"
              "struct A then\n    var b: B\nend\n"
              "var a: A = A(b: B(c: C(z: 7)))\n"
              "var v: int32 = mul(int32, a.b.c.z, 2)\n",
              "nested chain as operand ok");
    /* 未知字段 */
    expect_err("#program\n"
               "struct Point then\n    var x: int32\nend\n"
               "var p: Point = Point(x: 1)\n"
               "var v: int32 = p.missing\n",
               TC_CE_STRUCT_UNKNOWN_FIELD, "unknown field");
    expect_err("#program\n"
               "struct Point then\n    var x: int32\nend\n"
               "var p: Point = Point(x: 1)\n"
               "var v: int32 = mul(int32, p.missing, 2)\n",
               TC_CE_STRUCT_UNKNOWN_FIELD, "unknown field in operand");
    /* 非 struct 中间层 */
    expect_err("#program\n"
               "struct Point then\n    var x: int32\nend\n"
               "var p: Point = Point(x: 1)\n"
               "var v: int32 = p.x.y\n",
               TC_CE_TYPE_MISMATCH, "field path through non-struct");
}

static void test_struct_field_access_count_disambiguation(void) {
    /* memblock 基址 .count → count 语义（RHS 与 operand 两个入口） */
    expect_ok("#program\n"
              "var mb: memblock<int32, 2> = memblock(int32, count: 2, 3, 4)\n"
              "var n: usize = mb.count\n",
              "memblock .count RHS ok");
    expect_ok("#program\n"
              "var mb: memblock<int32, 2> = memblock(int32, count: 2, 3, 4)\n"
              "var d: usize = mul(usize, mb.count, 2)\n",
              "memblock .count operand ok");
    /* struct 同名字段 count → 普通字段读（不误报 memblock count） */
    expect_ok("#program\n"
              "struct Stats then\n    var count: int32\nend\n"
              "var s: Stats = Stats(count: 6)\n"
              "var v: int32 = s.count\n",
              "struct field named count read ok");
    expect_ok("#program\n"
              "struct Stats then\n    var count: int32\nend\n"
              "var s: Stats = Stats(count: 6)\n"
              "var d: int32 = mul(int32, s.count, 2)\n",
              "struct field named count as operand ok");
    /* 非 memblock 非 struct 基址 .count → 错误 */
    expect_err("#program\n"
               "var x: int32 = 1\n"
               "var n: usize = x.count\n",
               TC_CE_TYPE_MISMATCH, ".count on non-memblock/non-struct base");
}

static void test_struct_field_access_const(void) {
    /* let struct 字段读在 const operand 与 const-RHS 均可用 */
    expect_ok("#program\n"
              "struct Box then\n    var x: int32\nend\n"
              "let s: Box = Box(x: 4)\n"
              "let y: int32 = add(int32, s.x, 1)\n",
              "let struct field in const operand ok");
    expect_ok("#program\n"
              "struct Box then\n    var x: int32\nend\n"
              "let s: Box = Box(x: 4)\n"
              "let px: int32 = s.x\n",
              "let struct field in const RHS ok");
    /* var 基址在 let 上下文 → TC_CE_CONSTANT_EXPRESSION（两入口） */
    expect_err("#program\n"
               "struct Box then\n    var x: int32\nend\n"
               "var s: Box = Box(x: 4)\n"
               "let y: int32 = add(int32, s.x, 1)\n",
               TC_CE_CONSTANT_EXPRESSION, "var base in const operand");
    expect_err("#program\n"
               "struct Box then\n    var x: int32\nend\n"
               "var s: Box = Box(x: 4)\n"
               "let px: int32 = s.x\n",
               TC_CE_CONSTANT_EXPRESSION, "var base in const RHS");
    /* static var 初始化器中的字段 operand（Self 基址） */
    expect_ok("#lib\n"
              "public struct Box then\n    var x: int32\nend\n"
              "public static let s: Box = Box(x: 9)\n"
              "public static var n: int32 = Self.s.x\n",
              "static var init with Self.field ok");
    /* static let 拓扑：FIELD_READ / 算术 / 比较写在基址之前 */
    expect_ok("#lib\n"
              "public struct Box then\n    var x: int32\nend\n"
              "public static let n: int32 = Self.s.x\n"
              "public static let k: int32 = add(int32, Self.s.x, 1)\n"
              "public static let b: bool = gt(int32, Self.s.x, 0)\n"
              "public static let s: Box = Box(x: 9)\n",
              "static let field topo (read/arith/compare) ok");
    /* static let 拓扑：unary/bitwise/shift/logic/float/cast/bitcast 操作数字段读 */
    expect_ok("#lib\n"
              "public struct Num then\n    var x: int32\nend\n"
              "public struct Flag then\n    var b: bool\n    var f: float64\nend\n"
              "public struct ScalarPair then\n    var a: int32\n    var b: int32\nend\n"
              "public static let nn: int32 = neg(int32, Self.num.x)\n"
              "public static let bw: int32 = or(int32, Self.num.x, 1)\n"
              "public static let sh: int32 = shl(int32, Self.num.x, 1)\n"
              "public static let lb: bool = and(bool, Self.flag.b, Self.flag.b)\n"
              "public static let ln: bool = not(bool, Self.flag.b)\n"
              "public static let fa: float64 = add(float64, Self.flag.f, 1.0)\n"
              "public static let fc: bool = gt(float64, Self.flag.f, 0.0)\n"
              "public static let cc: int8 = cast(int8, Self.num.x)\n"
              "public static let bc: float32 = bitcast(float32, Self.num.x)\n"
              "public static let sp_a: int32 = Self.sp.a\n"
              "public static let num: Num = Num(x: 9)\n"
              "public static let flag: Flag = Flag(b: true, f: 1.5)\n"
              "public static let sp: ScalarPair = ScalarPair(a: Self.num.x, b: 1)\n",
              "static let field topo (unary/bitwise/shift/logic/float/cast/bitcast) ok");
    /* static var 运行期字段读初始化器（提前固化 + 基址为 static let） */
    expect_ok("#lib\n"
              "public struct Box then\n    var x: int32\nend\n"
              "public static let s: Box = Box(x: 9)\n"
              "public static var m: int32 = shl(int32, Self.s.x, 1)\n",
              "static var field operand init ok");
    /* 嵌套字段链作 static let RHS */
    expect_ok("#lib\n"
              "public struct Inner then\n    var x: int32\nend\n"
              "public struct Outer then\n    var inner: Inner\nend\n"
              "public static let o: Outer = Outer(inner: Inner(x: 3))\n"
              "public static let n: int32 = Self.o.inner.x\n",
              "nested static let field ok");
    /* static var 源序之后的 Self.field 非法 */
    expect_err("#lib\n"
               "public struct Box then\n    var x: int32\nend\n"
               "public static var m: int32 = Self.s.x\n"
               "public static let s: Box = Box(x: 9)\n",
               TC_CE_CONSTANT_EXPRESSION, "static var field forward");
}

static void test_struct_field_access_const_composite(void) {
    /* Critical 2 回归（分析侧）：let/static let 基址的 struct/memblock 字段整体
     * 读出（运行期 var 目标；AOT 曾把编译期堆指针嵌入生成 C 段错误）。 */
    expect_ok("#program\n"
              "struct Inner then\n    var v: int32\nend\n"
              "struct Outer then\n    var inner: Inner\nend\n"
              "let o: Outer = Outer(inner: Inner(v: 11))\n"
              "var x: Inner = o.inner\n"
              "var y: int32 = x.v\n",
              "let base struct field whole-read ok");
    expect_ok("#lib\n"
              "public struct Inner then\n    var v: int32\nend\n"
              "public struct Outer then\n    var inner: Inner\nend\n"
              "public static let o: Outer = Outer(inner: Inner(v: 5))\n"
              "public func get() int32 then\n    var x: Inner = Self.o.inner\n"
              "    return x.v\nend\n",
              "static let base struct field whole-read ok");
    /* memblock 字段整体读出（运行期深拷贝路径） */
    expect_ok("#program\n"
              "struct Holder then\n    var data: memblock<int32, 3>\nend\n"
              "let h: Holder = Holder(data: memblock(int32, count: 3, 1, 2, 3))\n"
              "var m: memblock<int32, 3> = h.data\n"
              "var v: int32 = memblock_load(int32, m, 1)\n",
              "let base memblock field whole-read ok");
    /* 嵌套两层复合字段链 */
    expect_ok("#program\n"
              "struct Leaf then\n    var v: int32\nend\n"
              "struct Mid then\n    var leaf: Leaf\nend\n"
              "struct Root then\n    var mid: Mid\nend\n"
              "let r: Root = Root(mid: Mid(leaf: Leaf(v: 7)))\n"
              "var m: Mid = r.mid\n"
              "var l: Leaf = m.leaf\n"
              "var v: int32 = l.v\n",
              "nested const composite field chain ok");
}

static void test_static_let_memblock_count_mismatch(void) {
    /* Critical 1 回归：static let 的 memblock 逐值构造计数不匹配。
     * const 求值先于 pass2 类型检查，须在 tc_eval_const_memblock_ctor 拒绝
     * （曾 value_count > count 堆越界写）。 */
    expect_err("#lib\n"
               "public static let M: memblock<int32, 2> = "
               "memblock(int32, count: 2, 1, 2, 3)\n"
               "public func f() void then\n    return\nend\n",
               TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH, "static let memblock count too many");
    expect_err("#lib\n"
               "public static let M: memblock<int32, 3> = "
               "memblock(int32, count: 3, 1, 2)\n"
               "public func f() void then\n    return\nend\n",
               TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH, "static let memblock count too few");
    /* 对照：计数匹配 → 合法 */
    expect_ok("#lib\n"
              "public static let M: memblock<int32, 3> = "
              "memblock(int32, count: 3, 1, 2, 3)\n"
              "public func f() void then\n    return\nend\n",
              "static let memblock count match ok");
}

static void test_struct_field_access_positions(void) {
    /* operand 位置：算术 / I/O / return / ptr / memblock 基址 */
    expect_ok("#program\n"
              "struct Point then\n    var score: int32\nend\n"
              "var p: Point = Point(score: 9)\n"
              "writeln(int32, p.score)\n",
              "field operand in io");
    expect_ok("#lib\n"
              "public struct Box then\n    var v: int32\nend\n"
              "public func read_score() int32 then\n    var p: Box = Box(v: 17)\n"
              "    return p.v\nend\n",
              "field operand in return");
    expect_ok("#program\n"
              "struct Holder then\n    var mvp: ptr<int32>\nend\n"
              "var x: int32 = 13\n"
              "var px: ptr<int32> = ptr_address(int32, x)\n"
              "var h: Holder = Holder(mvp: px)\n"
              "var v: int32 = ptr_load(int32, h.mvp)\n",
              "field operand in ptr_load");
    expect_ok("#program\n"
              "struct Bag then\n    var items: memblock<int32, 2>\nend\n"
              "var items: memblock<int32, 2> = memblock(int32, count: 2, 10, 20)\n"
              "var bag: Bag = Bag(items: items)\n"
              "var v: int32 = memblock_load(int32, bag.items, 1)\n",
              "field operand as memblock_load base");
}

/* ------------------------------------------------------------------ */
/*  hist 注入：基址 slot 置 UNINIT → 未初始化错误（计划 §5.2 备注）       */
/* ------------------------------------------------------------------ */

static void test_struct_field_access_hist_uninit(void) {
    TcTypedProgram typed = {0};
    TcDiagnostic diag;
    TcSymbol *sym = NULL;
    TcInitState states[1];
    TcInitHistory hist;
    TcFieldAccess access;
    const char *source =
        "#program\n"
        "struct Point then\n    var x: int32\nend\n"
        "var p: Point = Point(x: 1)\n";

    tc_diagnostic_init(&diag);
    if (analyze_source(source, &typed, &diag) != 0) {
        check(0, "hist test: analyze baseline failed");
        tc_diagnostic_clear(&diag);
        return;
    }
    sym = (TcSymbol *)tc_symbol_table_find(&typed.symbols, "p");
    if (!sym || tc_type_tag_of(sym->type) != TC_STRUCT || sym->slot < 0) {
        check(0, "hist test: base symbol not found");
        tc_typed_program_free(&typed);
        tc_diagnostic_clear(&diag);
        return;
    }

    memset(&hist, 0, sizeof(hist));
    hist.init_states = states;
    hist.num_slots = sym->slot + 1;
    hist.check_init = 1;
    hist.defer_to_cfg = 0;
    states[0] = TC_INIT_UNINIT;

    /* 基址未初始化（symbol 标记亦清零，绕过 initialized 快路径） */
    sym->initialized = 0;
    memset(&access, 0, sizeof(access));
    access.base = strdup("p");
    access.fields = (char **)malloc(sizeof(char *));
    access.fields[0] = strdup("x");
    access.field_count = 1;
    check(access.base && access.fields && access.fields[0], "hist test: alloc");
    if (access.base && access.fields && access.fields[0]) {
        memset(&diag, 0, sizeof(diag));
        tc_diagnostic_init(&diag);
        check(tc_struct_check_field_access(&access, NULL, typed.struct_table, &typed.symbols,
                                            &typed.symbols, &hist, 0, 1, &diag, NULL, NULL) != 0 &&
                  diag.kind == TC_CE_UNINITIALIZED_VARIABLE,
              "uninit base field read → UNINITIALIZED_VARIABLE");
        tc_diagnostic_clear(&diag);
        /* 失败路径未固化：parse 期字符串仍归测试所有，须手动释放 */
        free(access.base);
        free(access.fields[0]);
        free(access.fields);
    }

    /* 基址已初始化 → 通过并固化（finalize 内部释放 parse 字符串） */
    states[0] = TC_INIT_INIT;
    memset(&access, 0, sizeof(access));
    access.base = strdup("p");
    access.fields = (char **)malloc(sizeof(char *));
    access.fields[0] = strdup("x");
    access.field_count = 1;
    if (access.base && access.fields && access.fields[0]) {
        memset(&diag, 0, sizeof(diag));
        tc_diagnostic_init(&diag);
        check(tc_struct_check_field_access(&access, NULL, typed.struct_table, &typed.symbols,
                                            &typed.symbols, &hist, 0, 1, &diag, NULL, NULL) == 0,
              "init base field read ok");
        tc_diagnostic_clear(&diag);
        /* 成功路径已固化：仅需释放 offsets 链 */
        tc_resolved_field_access_free(&access.resolved);
    }

    tc_typed_program_free(&typed);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_struct_field_access_chain();
    test_struct_field_access_count_disambiguation();
    test_struct_field_access_const();
    test_struct_field_access_const_composite();
    test_struct_field_access_positions();
    test_struct_field_access_hist_uninit();
    test_static_let_memblock_count_mismatch();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed != 0;
}
