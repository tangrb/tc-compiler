/* test_embed.c — TC-Embed 嵌入式运行时单元测试（v0.0.41） */
#include "tc_embed.h"
#include "tc_value_bridge.h"
#include "tc_lib.h"

#include <stdint.h>
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

/* ── 辅助：编译 #lib TC 源码 ── */
static int compile_lib(const char *source, const char *name,
                        TcTypedProgram *out, TcDiagnostic *diag) {
    char full[2048];
    (void)snprintf(full, sizeof(full), "#lib\n%s", source);
    return tc_compile_source(full, name, out, diag);
}

/* ── 测试：创建/销毁生命周期 ── */
static void test_embed_create_destroy(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;

    tc_diagnostic_init(&diag);
    check(compile_lib("", "empty", &prog, &diag) == 0,
          "compile empty lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "tc_embed_create with empty lib");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：NULL program 拒绝 ── */
static void test_embed_null_program(void) {
    TcDiagnostic diag;
    TcEmbedCtx *ctx;

    tc_diagnostic_init(&diag);
    ctx = tc_embed_create(NULL, &diag);
    check(ctx == NULL, "tc_embed_create with NULL returns NULL");
    tc_diagnostic_clear(&diag);
}

/* ── 测试：无参 void 函数调用 ── */
static void test_embed_call_no_args_void_return(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func noop() void then\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile noop lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    check(tc_embed_call(ctx, NULL, "noop", 0, NULL, NULL) == 0,
          "call noop");
    check(!tc_embed_had_error(ctx), "no error after call");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：int32 标量参数与返回值 ── */
static void test_embed_call_scalar_args(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue args[2];
    TcValue result;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func plus(a: int32, b: int32) int32 then\n"
        "    var sum: int32 = add(int32, a, b)\n"
        "    return sum\n"
        "end\n", "math", &prog, &diag) == 0,
          "compile math lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    args[0] = tc_value_from_int32(3);
    args[1] = tc_value_from_int32(4);
    check(tc_embed_call(ctx, NULL, "plus", 2, args, &result) == 0,
          "call plus(3, 4)");
    tc_value_to_int64(result, &val);
    check(val == 7, "plus(3,4) == 7");

    /* 再次调用 */
    args[0] = tc_value_from_int32(10);
    args[1] = tc_value_from_int32(-3);
    check(tc_embed_call(ctx, NULL, "plus", 2, args, &result) == 0,
          "call plus(10, -3)");
    tc_value_to_int64(result, &val);
    check(val == 7, "plus(10,-3) == 7");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：float64 参数与返回值 ── */
static void test_embed_call_float64(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue args[2];
    TcValue result;
    double val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func mult(a: float64, b: float64) float64 then\n"
        "    var prod: float64 = mul(float64, a, b)\n"
        "    return prod\n"
        "end\n", "fp", &prog, &diag) == 0,
          "compile float64 lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    args[0] = tc_value_from_double(2.5);
    args[1] = tc_value_from_double(4.0);
    check(tc_embed_call(ctx, NULL, "mult", 2, args, &result) == 0,
          "call mult");
    tc_value_to_double(result, &val);
    check(val == 10.0, "mult(2.5, 4.0) == 10.0");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：bool 参数 ── */
static void test_embed_call_bool(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue args[1];
    TcValue result;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func ident(x: bool) bool then\n"
        "    return x\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile bool lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    args[0] = tc_value_from_bool(1);
    check(tc_embed_call(ctx, NULL, "ident", 1, args, &result) == 0,
          "call ident(true)");
    check(tc_value_to_bool(result) != 0, "ident(true) == true");

    args[0] = tc_value_from_bool(0);
    check(tc_embed_call(ctx, NULL, "ident", 1, args, &result) == 0,
          "call ident(false)");
    check(tc_value_to_bool(result) == 0, "ident(false) == false");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：参数数量不匹配 ── */
static void test_embed_call_wrong_arg_count(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue args[1];

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func plus(a: int32, b: int32) int32 then\n"
        "    var sum: int32 = add(int32, a, b)\n"
        "    return sum\n"
        "end\n", "math", &prog, &diag) == 0,
          "compile math lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    args[0] = tc_value_from_int32(42);
    check(tc_embed_call(ctx, NULL, "plus", 1, args, NULL) != 0,
          "wrong arg count returns error");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：函数不存在 ── */
static void test_embed_call_func_not_found(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func foo() void then\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    check(tc_embed_call(ctx, NULL, "nonexistent", 0, NULL, NULL) != 0,
          "nonexistent func returns error");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：ptr_load 读取 C 平铺数据（无循环，常量偏移） ── */
static void test_embed_ptr_load_sum(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;
    TcValue args[1];
    TcValue result;
    int64_t total;
    size_t slot_count;
    int data_base;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func sum(data: ptr<int32>) int32 then\n"
        "    var a: int32 = ptr_load(int32, data)\n"
        "    var p1: ptr<int32> = ptr_add(int32, data, 1)\n"
        "    var b: int32 = ptr_load(int32, p1)\n"
        "    var total: int32 = add(int32, a, b)\n"
        "    return total\n"
        "end\n", "stats", &prog, &diag) == 0,
          "compile ptr load sum lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    info = tc_embed_func_info(ctx, NULL, "sum");
    check(info != NULL, "find sum");
    check(info->param_count == 1, "sum has 1 param");

    /* 数据区域：放在 slot 末尾安全区（不与 param slot 重叠） */
    slot_count = tc_embed_slot_count(ctx);
    check(slot_count >= 3, "at least 3 slots");
    data_base = (int)slot_count - 3;

    check(tc_embed_slot_write(ctx, data_base,
                               tc_value_from_int32(10)) == 0,
          "write slot[data_base] = 10");
    check(tc_embed_slot_write(ctx, data_base + 1,
                               tc_value_from_int32(20)) == 0,
          "write slot[data_base+1] = 20");

    /* 构造 ptr<int32> 参数指向 slot data_base */
    args[0] = tc_embed_ptr_encode(data_base);

    check(tc_embed_call(ctx, NULL, "sum", 1, args, &result) == 0,
          "call sum");
    tc_value_to_int64(result, &total);
    check(total == 30, "sum(slot[0]=10, slot[1]=20) == 30");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：C 写 slot → TC ptr_store（常量偏移）→ C 读回 ── */
static void test_embed_ptr_store_offset(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;
    TcValue args[2];
    TcValue stored;
    int data_base;
    size_t slot_count;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func store_at_one(data: ptr<int32>, val: int32) void then\n"
        "    var p1: ptr<int32> = ptr_add(int32, data, 1)\n"
        "    ptr_store(int32, p1, val)\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile ptr store lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    info = tc_embed_func_info(ctx, NULL, "store_at_one");
    check(info != NULL, "find store_at_one");

    /* 数据区域：放在 slot 末尾安全区 */
    slot_count = tc_embed_slot_count(ctx);
    check(slot_count >= 3, "at least 3 slots");
    data_base = (int)slot_count - 3;

    check(tc_embed_slot_write(ctx, data_base, tc_value_from_int32(99)) == 0,
          "write slot[data_base] = 99");

    args[0] = tc_embed_ptr_encode(data_base);
    args[1] = tc_value_from_int32(42);

    check(tc_embed_call(ctx, NULL, "store_at_one", 2, args, NULL) == 0,
          "call store_at_one");

    /* 读回 slot[data_base + 1] 验证 TC 写入了值 */
    check(tc_embed_slot_read(ctx, data_base + 1, &stored) == 0,
          "read slot[data_base+1]");
    tc_value_to_int64(stored, &val);
    check(val == 42, "store_at_one wrote 42 to slot[data_base+1]");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：重复调用保持 static var 值 ── */
static void test_embed_static_var_persist(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue result;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public static var value: int32 = 42\n"
        "public func get_value() int32 then\n"
        "    var v: int32 = Self.value\n"
        "    return v\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile get_value lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    /* 第一次调用 */
    check(tc_embed_call(ctx, NULL, "get_value", 0, NULL, &result) == 0,
          "call get_value #1");
    tc_value_to_int64(result, &val);
    check(val == 42, "get_value() == 42");

    /* 第二次调用 — 值应保持不变 */
    check(tc_embed_call(ctx, NULL, "get_value", 0, NULL, &result) == 0,
          "call get_value #2");
    tc_value_to_int64(result, &val);
    check(val == 42, "get_value() == 42 (persistent)");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：槽位读写往返 ── */
static void test_embed_slot_write_read(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue readback;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(
        "#program\nvar x: int32 = 0\n", "<test>", &prog, &diag) == 0,
          "compile program with var");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    /* #program 的 var x 分配了 slot 0，写入并读回 */
    check(tc_embed_slot_write(ctx, 0, tc_value_from_int32(42)) == 0,
          "write slot 0");
    check(tc_embed_slot_read(ctx, 0, &readback) == 0, "read slot 0");
    tc_value_to_int64(readback, &val);
    check(val == 42, "slot readback == 42");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

static void test_embed_bool_normalize(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue dirty;
    TcValue readback;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(
        "#program\nvar flag: bool = false\n", "<test>", &prog, &diag) == 0,
          "compile program with bool var");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    dirty.type = tc_type_tag_singleton(TC_BOOL);
    dirty.bits = UINT64_C(0xFF);
    check(tc_embed_slot_write(ctx, 0, dirty) == 0, "write dirty bool bits");
    check(tc_embed_slot_read(ctx, 0, &readback) == 0, "read bool slot");
    check(readback.bits == UINT64_C(1), "dirty bool normalized to 0x01");
    check(readback.type && readback.type->tag == TC_BOOL, "readback type is bool");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：越界槽位拒绝 ── */
static void test_embed_slot_out_of_range(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue v = tc_value_from_int32(0);

    tc_diagnostic_init(&diag);
    check(tc_compile_source(
        "#program\nvar x: int32 = 0\n", "<test>", &prog, &diag) == 0,
          "compile program with var");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    check(tc_embed_slot_write(ctx, -1, v) != 0, "negative slot rejected");
    check(tc_embed_slot_write(ctx, 9999, v) != 0, "large slot rejected");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：错误消息可读 ── */
static void test_embed_error_message(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const char *err1, *err2;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func foo() void then\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    /* 函数不存在 */
    tc_embed_call(ctx, NULL, "bar", 0, NULL, NULL);
    err1 = tc_embed_get_error(ctx);
    check(err1 != NULL && strlen(err1) > 0, "error message non-empty");
    check(tc_embed_had_error(ctx) != 0, "error flag set");

    /* 成功的调用应清除错误 */
    tc_embed_call(ctx, NULL, "foo", 0, NULL, NULL);
    check(!tc_embed_had_error(ctx), "error flag cleared after success");

    /* 再次失败 */
    tc_embed_call(ctx, NULL, "bar", 0, NULL, NULL);
    err2 = tc_embed_get_error(ctx);
    check(err2 != NULL && strlen(err2) > 0, "error message after re-failure");
    check(tc_embed_had_error(ctx) != 0, "error flag re-set");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：func_info 查询 ── */
static void test_embed_func_info_query(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func plus(a: int32, b: int32) int32 then\n"
        "    var sum: int32 = add(int32, a, b)\n"
        "    return sum\n"
        "end\n"
        "public func greet(x: bool) void then\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    /* 查询 plus */
    info = tc_embed_func_info(ctx, NULL, "plus");
    check(info != NULL, "find plus");
    check(info->has_return != 0, "plus has return");
    check(info->param_count == 2, "plus has 2 params");

    /* 查询 greet */
    info = tc_embed_func_info(ctx, NULL, "greet");
    check(info != NULL, "find greet");
    check(info->has_return == 0, "greet has no return");
    check(info->param_count == 1, "greet has 1 param");

    /* 不存在的函数 */
    info = tc_embed_func_info(ctx, NULL, "nonexistent");
    check(info == NULL, "nonexistent returns NULL");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：top_var_slot 和 self_var_slot ── */
static void test_embed_var_slot_query(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    int slot;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(
        "#program\n"
        "var global_x: int32 = 10\n"
        "var global_y: bool = true\n", "<test>", &prog, &diag) == 0,
          "compile program with top-level vars");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    slot = tc_embed_top_var_slot(ctx, "global_x");
    check(slot >= 0, "top_var_slot global_x >= 0");

    slot = tc_embed_top_var_slot(ctx, "global_y");
    check(slot >= 0, "top_var_slot global_y >= 0");

    slot = tc_embed_top_var_slot(ctx, "nonexistent");
    check(slot == -1, "top_var_slot nonexistent == -1");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：ptr<T> encode/decode/null API ── */
static void test_embed_ptr_encode_decode(void) {
    TcValue v;
    int slot;
    int i;

    /* nullptr 检测 */
    v.bits = 0;
    v.type = tc_type_tag_singleton(TC_PTR);
    check(tc_embed_ptr_is_null(v) != 0, "ptr_is_null(0) == true");

    /* 合法 ptr 检测 */
    v = tc_embed_ptr_encode(5);
    check(tc_embed_ptr_is_null(v) == 0, "ptr_is_null(encode(5)) == false");

    /* encode ↔ decode 往返 */
    for (i = 0; i < 100; i++) {
        v = tc_embed_ptr_encode(i);
        check(v.type->tag == TC_PTR, "ptr_encode sets type to TC_PTR");
        check(v.bits == (((uint64_t)(uint32_t)i << 1) | 1ULL),
              "ptr_encode bit pattern correct");
        check(tc_embed_ptr_decode_slot(v, &slot) == 0,
              "ptr_decode returns 0 for valid ptr");
        check(slot == i, "ptr_decode returns correct slot");
    }

    /* 非法 ptr 拒绝 */
    v.bits = 2;  /* LSB=0, 非零 → 无效 */
    v.type = tc_type_tag_singleton(TC_PTR);
    check(tc_embed_ptr_decode_slot(v, &slot) != 0,
          "ptr_decode rejects invalid ptr (bits=2)");

    v.bits = 4;
    check(tc_embed_ptr_decode_slot(v, &slot) != 0,
          "ptr_decode rejects invalid ptr (bits=4)");
}

/* ── 测试：static var 递增（C 侧写 slot，TC 侧读回） ── */
static void test_embed_static_var_increment(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    int slot;
    TcValue result;
    int64_t val;
    int call;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public static var counter: int32 = 0\n"
        "public func get_counter() int32 then\n"
        "    var v: int32 = Self.counter\n"
        "    return v\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile counter lib");
    if (diag.domain != TC_DIAG_NONE) {
        tc_diagnostic_clear(&diag);
        return;
    }

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");
    if (!ctx) { tc_typed_program_free(&prog); tc_diagnostic_clear(&diag); return; }

    slot = tc_embed_self_var_slot(ctx, "counter");
    check(slot >= 0, "self_var_slot returns valid slot");

    for (call = 1; call <= 5; call++) {
        /* C 侧写入递增的 counter 值 */
        check(tc_embed_slot_write(ctx, slot, tc_value_from_int32(call * 10)) == 0,
              "write slot = call * 10");
        check(tc_embed_call(ctx, NULL, "get_counter", 0, NULL, &result) == 0,
              "call get_counter");
        tc_value_to_int64(result, &val);
        check(val == (int64_t)(call * 10), "get_counter == call * 10");
    }

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：嵌套 funcall（outer → inner） ── */
static void test_embed_nested_funcall(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue args[1];
    TcValue result;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "private func inner(x: int32) int32 then\n"
        "    var r: int32 = add(int32, x, 1)\n"
        "    return r\n"
        "end\n"
        "public func outer(x: int32) int32 then\n"
        "    var r: int32 = funcall(Self.inner, x: x)\n"
        "    return r\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile nested funcall lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    args[0] = tc_value_from_int32(41);
    check(tc_embed_call(ctx, NULL, "outer", 1, args, &result) == 0,
          "call outer(41)");
    tc_value_to_int64(result, &val);
    check(val == 42, "outer(41) == 42 via inner(x+1)");

    args[0] = tc_value_from_int32(0);
    check(tc_embed_call(ctx, NULL, "outer", 1, args, &result) == 0,
          "call outer(0)");
    tc_value_to_int64(result, &val);
    check(val == 1, "outer(0) == 1");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：int64 / int32 / float64 多类型参数与返回值 ── */
static void test_embed_call_mixed_types(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue args[2];
    TcValue result;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func add_int64(a: int64, b: int64) int64 then\n"
        "    var sum: int64 = add(int64, a, b)\n"
        "    return sum\n"
        "end\n",
        "types", &prog, &diag) == 0,
          "compile int64 lib");
    if (diag.domain != TC_DIAG_NONE) {
        tc_diagnostic_clear(&diag);
        return;
    }

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");
    if (!ctx) { tc_typed_program_free(&prog); tc_diagnostic_clear(&diag); return; }

    /* int64: 1000000 + 2000000 = 3000000 */
    args[0] = tc_value_from_int64(1000000);
    args[1] = tc_value_from_int64(2000000);
    check(tc_embed_call(ctx, NULL, "add_int64", 2, args, &result) == 0,
          "call add_int64(1M, 2M)");
    tc_value_to_int64(result, &val);
    check(val == 3000000, "add_int64(1M, 2M) == 3000000");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：self_var_slot 读写 ── */
static void test_embed_self_var_slot_readwrite(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    int slot;
    TcValue readback;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public static var value: int32 = 42\n"
        "public func get_value() int32 then\n"
        "    var v: int32 = Self.value\n"
        "    return v\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile self var lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    /* 查询 static var slot */
    slot = tc_embed_self_var_slot(ctx, "value");
    check(slot >= 0, "self_var_slot returns valid slot");

    /* 读取验证初始值 */
    check(tc_embed_slot_read(ctx, slot, &readback) == 0, "read self var slot");
    tc_value_to_int64(readback, &val);
    check(val == 42, "self var initial value == 42");

    /* 写入新值 */
    check(tc_embed_slot_write(ctx, slot, tc_value_from_int32(99)) == 0,
          "write self var slot = 99");

    /* 通过 TC 函数验证写入生效 */
    {
        TcValue result;
        int64_t v2;
        check(tc_embed_call(ctx, NULL, "get_value", 0, NULL, &result) == 0,
              "call get_value after slot write");
        tc_value_to_int64(result, &v2);
        check(v2 == 99, "get_value() == 99 after slot write");
    }

    /* 不存在的 static var */
    check(tc_embed_self_var_slot(ctx, "nonexistent") == -1,
          "self_var_slot returns -1 for nonexistent");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：slot_count API ── */
static void test_embed_slot_count_api(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    size_t count;

    tc_diagnostic_init(&diag);
    check(tc_compile_source(
        "#program\nvar x: int32 = 0\nvar y: int32 = 0\n", "<test>", &prog, &diag) == 0, "compile two-vars program");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    count = tc_embed_slot_count(ctx);
    check(count >= 2, "slot_count >= 2 for two top-level vars");

    /* NULL 安全 */
    check(tc_embed_slot_count(NULL) == 0, "slot_count(NULL) == 0");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：ptr_store 写入多个偏移位置后 C 侧读回（常量偏移） ── */
static void test_embed_ptr_store_multiple(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;
    TcValue args[2];
    int data_base;
    size_t slot_count;
    int i;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public static var _pad: int32 = 0\n"
        "public func fill_three(data: ptr<int32>, v: int32) void then\n"
        "    var p1: ptr<int32> = ptr_add(int32, data, 1)\n"
        "    var p2: ptr<int32> = ptr_add(int32, data, 2)\n"
        "    ptr_store(int32, data, v)\n"
        "    ptr_store(int32, p1, v)\n"
        "    ptr_store(int32, p2, v)\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile ptr store multiple lib");
    if (diag.domain != TC_DIAG_NONE) {
        tc_diagnostic_clear(&diag);
        return;
    }

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");
    if (!ctx) { tc_typed_program_free(&prog); tc_diagnostic_clear(&diag); return; }

    info = tc_embed_func_info(ctx, NULL, "fill_three");
    check(info != NULL, "find fill_three");

    slot_count = tc_embed_slot_count(ctx);
    check(slot_count > 3, "enough slots for data");
    data_base = (int)slot_count - 3;

    /* 初始化为 0 */
    for (i = 0; i < 3; i++) {
        check(tc_embed_slot_write(ctx, data_base + i,
                                   tc_value_from_int32(0)) == 0,
              "init slot to 0");
    }

    /* 调用 fill_three(data, 7) */
    args[0] = tc_embed_ptr_encode(data_base);
    args[1] = tc_value_from_int32(7);
    check(tc_embed_call(ctx, NULL, "fill_three", 2, args, NULL) == 0,
          "call fill_three(data, 7)");

    /* 验证所有 3 个位置均为 7 */
    for (i = 0; i < 3; i++) {
        TcValue stored;
        check(tc_embed_slot_read(ctx, data_base + i, &stored) == 0,
              "read back slot after fill_three");
        tc_value_to_int64(stored, &val);
        check(val == 7, "fill_three wrote 7 to all slots");
    }

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：临时槽位区分配/释放往返 ── */
static void test_embed_tmp_begin_end(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    size_t slot_count;
    int base = -1;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func calc(a: int32, b: int32, c: int32, d: int32) int32 then\n"
        "    var e: int32 = add(int32, a, b)\n"
        "    var f: int32 = add(int32, e, c)\n"
        "    var g: int32 = add(int32, f, d)\n"
        "    return g\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile tmp lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    slot_count = tc_embed_slot_count(ctx);

    check(tc_embed_tmp_begin(ctx, 3, &base) == 0, "tmp_begin 3 slots");
    check(base >= 0 && (size_t)base + 3 <= slot_count, "tmp region within slots");
    check((size_t)base == slot_count - 3, "tmp base at end of slots");

    {
        int base2 = -1;
        check(tc_embed_tmp_begin(ctx, 2, &base2) == 0, "nested tmp_begin 2 slots");
        check(base2 == base - 2, "nested base below previous");
        tc_embed_tmp_end(ctx);
    }
    tc_embed_tmp_end(ctx);

    check(tc_embed_tmp_begin(ctx, 4, &base) == 0, "tmp_begin after end");
    check((size_t)base == slot_count - 4, "reuse freed region");
    tc_embed_tmp_end(ctx);

    check(tc_embed_tmp_begin(ctx, (size_t)slot_count + 1, &base) != 0,
          "oversized tmp_begin rejected");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：make_ptr 一键平铺 + TC ptr_load 遍历 ── */
static void test_embed_make_ptr_sum(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    int32_t input[5] = {1, 2, 3, 4, 5};
    TcValue data_ptr;
    TcValue result;
    int64_t total;
    int slot;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func sum(data: ptr<int32>) int32 then\n"
        "    var a: int32 = ptr_load(int32, data)\n"
        "    var p1: ptr<int32> = ptr_add(int32, data, 1)\n"
        "    var b: int32 = ptr_load(int32, p1)\n"
        "    var p2: ptr<int32> = ptr_add(int32, data, 2)\n"
        "    var c: int32 = ptr_load(int32, p2)\n"
        "    var p3: ptr<int32> = ptr_add(int32, data, 3)\n"
        "    var d: int32 = ptr_load(int32, p3)\n"
        "    var p4: ptr<int32> = ptr_add(int32, data, 4)\n"
        "    var e: int32 = ptr_load(int32, p4)\n"
        "    var s1: int32 = add(int32, a, b)\n"
        "    var s2: int32 = add(int32, s1, c)\n"
        "    var s3: int32 = add(int32, s2, d)\n"
        "    var total: int32 = add(int32, s3, e)\n"
        "    return total\n"
        "end\n", "stats", &prog, &diag) == 0,
          "compile make_ptr sum lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    check(tc_embed_make_ptr(ctx, TC_INT32, input, 5, &data_ptr) == 0,
          "make_ptr int32[5]");
    check(tc_embed_ptr_is_null(data_ptr) == 0, "make_ptr returns non-null ptr");
    check(tc_embed_ptr_decode_slot(data_ptr, &slot) == 0 && slot >= 0,
          "make_ptr ptr decodes to slot");

    check(tc_embed_call_typed(ctx, tc_embed_func_info(ctx, NULL, "sum"),
                              TC_EMBED_ARGS(tc_embed_arg_value(data_ptr)),
                              &result) == 0,
          "call sum via typed");
    tc_value_to_int64(result, &total);
    check(total == 15, "sum(1..5) == 15");

    tc_embed_tmp_end(ctx);
    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：make_ptr count=0 返回 nullptr / 不支持类型拒绝 ── */
static void test_embed_make_ptr_zero_count(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    TcValue p;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func noop() void then\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    check(tc_embed_make_ptr(ctx, TC_INT32, NULL, 0, &p) == 0,
          "make_ptr zero count ok");
    check(tc_embed_ptr_is_null(p) != 0, "zero count -> nullptr");

    check(tc_embed_make_ptr(ctx, TC_STRUCT, NULL, 0, &p) != 0,
          "make_ptr unsupported type rejected");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：call_typed 标量参数 + 宏自动 nargs ── */
static void test_embed_call_typed_args(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;
    TcValue result;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func plus(a: int32, b: int32) int32 then\n"
        "    var sum: int32 = add(int32, a, b)\n"
        "    return sum\n"
        "end\n", "math", &prog, &diag) == 0,
          "compile math lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    info = tc_embed_func_info(ctx, NULL, "plus");
    check(info != NULL, "find plus");

    check(tc_embed_call_typed(ctx, info,
                              TC_EMBED_ARGS(tc_embed_arg_i32(3),
                                            tc_embed_arg_i32(4)),
                              &result) == 0,
          "typed call plus(3,4)");
    tc_value_to_int64(result, &val);
    check(val == 7, "plus(3,4) == 7");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：call_typed 类型不匹配拒绝 ── */
static void test_embed_call_typed_type_mismatch(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func plus(a: int32, b: int32) int32 then\n"
        "    var sum: int32 = add(int32, a, b)\n"
        "    return sum\n"
        "end\n", "math", &prog, &diag) == 0,
          "compile math lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    info = tc_embed_func_info(ctx, NULL, "plus");
    check(info != NULL, "find plus");

    check(tc_embed_call_typed(ctx, info,
                              TC_EMBED_ARGS(tc_embed_arg_f64(1.0),
                                            tc_embed_arg_i32(4)),
                              NULL) != 0,
          "type mismatch rejected");
    check(tc_embed_had_error(ctx) != 0, "error flag set on mismatch");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：call_typed 参数个数不符拒绝 ── */
static void test_embed_call_typed_wrong_count(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func plus(a: int32, b: int32) int32 then\n"
        "    var sum: int32 = add(int32, a, b)\n"
        "    return sum\n"
        "end\n", "math", &prog, &diag) == 0,
          "compile math lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    info = tc_embed_func_info(ctx, NULL, "plus");
    check(info != NULL, "find plus");

    check(tc_embed_call_typed(ctx, info,
                              TC_EMBED_ARGS(tc_embed_arg_i32(1)),
                              NULL) != 0,
          "wrong count rejected");

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

/* ── 测试：make_ptr + ptr_store 原地修改 + C 读回 ── */
static void test_embed_make_ptr_store_readback(void) {
    TcTypedProgram prog;
    TcDiagnostic diag;
    TcEmbedCtx *ctx;
    int32_t data_src[3] = {99, 0, 0};
    TcValue data_ptr;
    TcValue result;
    TcValue stored;
    int base;
    int64_t val;

    tc_diagnostic_init(&diag);
    check(compile_lib(
        "public func store_at_one(data: ptr<int32>, val: int32) void then\n"
        "    var p1: ptr<int32> = ptr_add(int32, data, 1)\n"
        "    ptr_store(int32, p1, val)\n"
        "end\n", "test", &prog, &diag) == 0,
          "compile store lib");

    ctx = tc_embed_create(&prog, &diag);
    check(ctx != NULL, "create ctx");

    check(tc_embed_make_ptr(ctx, TC_INT32, data_src, 3, &data_ptr) == 0,
          "make_ptr int32[3]");
    check(tc_embed_ptr_decode_slot(data_ptr, &base) == 0, "decode data ptr");

    check(tc_embed_call_typed(ctx, tc_embed_func_info(ctx, NULL, "store_at_one"),
                              TC_EMBED_ARGS(tc_embed_arg_value(data_ptr),
                                            tc_embed_arg_i32(42)),
                              &result) == 0,
          "call store_at_one via typed");

    check(tc_embed_slot_read(ctx, base + 1, &stored) == 0, "read back slot[base+1]");
    tc_value_to_int64(stored, &val);
    check(val == 42, "store wrote 42 to slot[base+1]");

    tc_embed_tmp_end(ctx);
    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_embed_create_destroy();
    test_embed_null_program();
    test_embed_call_no_args_void_return();
    test_embed_call_scalar_args();
    test_embed_call_float64();
    test_embed_call_bool();
    test_embed_call_wrong_arg_count();
    test_embed_call_func_not_found();
    test_embed_ptr_load_sum();
    test_embed_ptr_store_offset();
    test_embed_static_var_persist();
    test_embed_slot_write_read();
    test_embed_bool_normalize();
    test_embed_slot_out_of_range();
    test_embed_error_message();
    test_embed_func_info_query();
    test_embed_var_slot_query();
    test_embed_ptr_encode_decode();
    test_embed_static_var_increment();
    test_embed_nested_funcall();
    test_embed_call_mixed_types();
    test_embed_self_var_slot_readwrite();
    test_embed_slot_count_api();
    test_embed_ptr_store_multiple();
    test_embed_tmp_begin_end();
    test_embed_make_ptr_sum();
    test_embed_make_ptr_zero_count();
    test_embed_call_typed_args();
    test_embed_call_typed_type_mismatch();
    test_embed_call_typed_wrong_count();
    test_embed_make_ptr_store_readback();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
