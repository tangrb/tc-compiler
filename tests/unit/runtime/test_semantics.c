/*
 * test_semantics.c — 语义核心模块单元测试
 *
 * 覆盖 tc_semantics.c 中所有公开函数：
 *   - 位模式工具函数 (mask_bits / bits_to_signed / signed_to_bits / ...)
 *   - 范围检查 (signed_in_range / unsigned_in_range)
 *   - 字面量检查与转换 (literal_fits_type / literal_fits_context / literal_to_value)
 *   - 算术运算 (tc_exec_arith) — 有符号/无符号 × strict/wrap × 5 种 op
 *   - 单目运算 (tc_exec_unary) — abs/neg, strict/wrap, INT_MIN 边界
 *   - 比较运算 (tc_exec_compare) — 6 种 op, 有符号/无符号
 *   - 逻辑运算 (tc_exec_logic_binary / tc_exec_logic_unary)
 *   - cast 运算 (tc_exec_cast) — strict/truncate, widen/narrow/same-width/bool
 *
 * 防止回归：位模式别名、溢出检测遗漏、短路逻辑错误、cast 范围检查遗漏
 */

#include "tc_semantics.h"
#include "tc_diagnostic.h"

#ifdef TC_HAVE_FENV
#include <fenv.h>
#endif
#include <float.h>
#include <math.h>
#include <stdint.h>
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

/* ================================================================== */
/*  位模式工具函数                                                       */
/* ================================================================== */

static void test_mask_bits(void) {
    check(tc_mask_bits(1) == 1ULL, "mask_bits(1) == 0x1");
    check(tc_mask_bits(8) == 0xFFULL, "mask_bits(8) == 0xFF");
    check(tc_mask_bits(16) == 0xFFFFULL, "mask_bits(16) == 0xFFFF");
    check(tc_mask_bits(32) == 0xFFFFFFFFULL, "mask_bits(32) == 0xFFFFFFFF");
    check(tc_mask_bits(64) == UINT64_MAX, "mask_bits(64) == UINT64_MAX");
    check(tc_mask_bits(0) == 0ULL, "mask_bits(0) == 0");
}

static void test_bits_to_signed(void) {
    /* int8: 0xFF = -1 */
    check(tc_bits_to_signed(TC_INT8, 0xFF) == -1, "int8 0xFF → -1");
    /* int8: 0x80 = -128 */
    check(tc_bits_to_signed(TC_INT8, 0x80) == -128, "int8 0x80 → -128");
    /* int8: 0x7F = 127 */
    check(tc_bits_to_signed(TC_INT8, 0x7F) == 127, "int8 0x7F → 127");
    /* int16: 0xFFFF = -1 */
    check(tc_bits_to_signed(TC_INT16, 0xFFFF) == -1, "int16 0xFFFF → -1");
    /* int32: 0xFFFFFFFF = -1 */
    check(tc_bits_to_signed(TC_INT32, 0xFFFFFFFFULL) == -1, "int32 0xFFFFFFFF → -1");
    /* int64: 0xFFFFFFFFFFFFFFFF = -1 */
    check(tc_bits_to_signed(TC_INT64, UINT64_MAX) == -1, "int64 UINT64_MAX → -1");
    /* int64: INT64_MIN */
    check(tc_bits_to_signed(TC_INT64, (uint64_t)INT64_MIN) == INT64_MIN,
          "int64 0x8000... → INT64_MIN");
    /* int64: 正数 */
    check(tc_bits_to_signed(TC_INT64, 42) == 42, "int64 42 → 42");
}

static void test_signed_to_bits(void) {
    check(tc_signed_to_bits(TC_INT8, -1) == 0xFF, "int8 -1 → 0xFF");
    check(tc_signed_to_bits(TC_INT8, -128) == 0x80, "int8 -128 → 0x80");
    check(tc_signed_to_bits(TC_INT8, 127) == 0x7F, "int8 127 → 0x7F");
    check(tc_signed_to_bits(TC_INT32, -1) == 0xFFFFFFFFULL, "int32 -1 → 0xFFFFFFFF");
    check(tc_signed_to_bits(TC_INT64, INT64_MIN) == (uint64_t)INT64_MIN,
          "int64 INT64_MIN → 0x8000...");
}

static void test_value_to_unsigned(void) {
    /* int8, bits=0xFF → 归一化到 8 位 = 0xFF */
    check(tc_value_to_unsigned(TC_INT8, 0xFF) == 0xFF, "int8 value_to_unsigned 0xFF");
    /* int8, bits=0x1FF → 高位被掩码 = 0xFF */
    check(tc_value_to_unsigned(TC_INT8, 0x1FF) == 0xFF,
          "int8 value_to_unsigned 0x1FF → 0xFF");
    /* uint32, 完整值 */
    check(tc_value_to_unsigned(TC_UINT32, 0xDEADBEEFULL) == 0xDEADBEEFULL,
          "uint32 value_to_unsigned 0xDEADBEEF");
    /* uint64, 不截断 */
    check(tc_value_to_unsigned(TC_UINT64, UINT64_MAX) == UINT64_MAX,
          "uint64 value_to_unsigned UINT64_MAX");
}

static void test_value_make(void) {
    TcValue v;

    v = tc_value_make(TC_INT8, 0xFF);
    check(v.type->tag == TC_INT8 && v.bits == 0xFF, "value_make int8 0xFF → type=int8, bits=0xFF");

    v = tc_value_make(TC_INT32, 0x1FFFFFFFFULL);
    check(v.type->tag == TC_INT32 && v.bits == 0xFFFFFFFFULL,
          "value_make int32 0x1FFFFFFFF → bits=0xFFFFFFFF");

    v = tc_value_make(TC_BOOL, 1);
    check(v.type->tag == TC_BOOL && v.bits == 1, "value_make bool 1 → type=bool, bits=1");

    v = tc_value_make(TC_UINT64, 42);
    check(v.type->tag == TC_UINT64 && v.bits == 42, "value_make uint64 42");

    v = tc_value_make(TC_MEMBLOCK, UINT64_C(0x00007FFF12345678));
    check(v.type->tag == TC_MEMBLOCK && v.bits == UINT64_C(0x00007FFF12345678),
          "value_make memblock preserves heap pointer bits");

    v = tc_value_make(TC_STRUCT, UINT64_C(0x00007FFF12345678));
    check(v.type->tag == TC_STRUCT && v.bits == UINT64_C(0x00007FFF12345678),
          "value_make struct preserves heap pointer bits");
}

static void test_uninitialized_slot_sentinel(void) {
    TcValue vm_slots[2];
    uint64_t aot_slots[2];

    tc_slots_init_uninitialized(vm_slots, 2);
    tc_slot_bits_init_uninitialized(aot_slots, 2);

    check(vm_slots[0].bits == TC_UNINITIALIZED_SLOT_BITS,
          "tc_slots_init_uninitialized sets bits sentinel");
    check(aot_slots[0] == TC_UNINITIALIZED_SLOT_BITS,
          "tc_slot_bits_init_uninitialized sets bits sentinel");
    check(aot_slots[0] == vm_slots[0].bits, "VM/AOT uninitialized bits match");
}

/* ================================================================== */
/*  范围检查                                                           */
/* ================================================================== */

static void test_signed_in_range(void) {
    check(tc_signed_in_range(0, TC_INT8), "signed_in_range int8 0");
    check(tc_signed_in_range(-128, TC_INT8), "signed_in_range int8 -128");
    check(tc_signed_in_range(127, TC_INT8), "signed_in_range int8 127");
    check(!tc_signed_in_range(-129, TC_INT8), "signed_in_range int8 -129 out");
    check(!tc_signed_in_range(128, TC_INT8), "signed_in_range int8 128 out");

    check(tc_signed_in_range(0, TC_INT32), "signed_in_range int32 0");
    check(tc_signed_in_range(INT32_MAX, TC_INT32), "signed_in_range int32 INT32_MAX");
    check(tc_signed_in_range(INT32_MIN, TC_INT32), "signed_in_range int32 INT32_MIN");
    check(!tc_signed_in_range((int64_t)INT32_MAX + 1, TC_INT32),
          "signed_in_range int32 INT32_MAX+1 out");

    check(tc_signed_in_range(INT64_MIN, TC_INT64), "signed_in_range int64 INT64_MIN");
    check(tc_signed_in_range(INT64_MAX, TC_INT64), "signed_in_range int64 INT64_MAX");
}

static void test_unsigned_in_range(void) {
    check(tc_unsigned_in_range(0, TC_UINT8), "unsigned_in_range uint8 0");
    check(tc_unsigned_in_range(255, TC_UINT8), "unsigned_in_range uint8 255");
    check(!tc_unsigned_in_range(256, TC_UINT8), "unsigned_in_range uint8 256 out");

    check(tc_unsigned_in_range(UINT32_MAX, TC_UINT32), "unsigned_in_range uint32 UINT32_MAX");
    check(!tc_unsigned_in_range((uint64_t)UINT32_MAX + 1, TC_UINT32),
          "unsigned_in_range uint32 UINT32_MAX+1 out");

    check(tc_unsigned_in_range(UINT64_MAX, TC_UINT64), "unsigned_in_range uint64 UINT64_MAX");
}

/* ================================================================== */
/*  字面量检查与转换                                                     */
/* ================================================================== */

static void test_literal_fits_type(void) {
    /* 有符号类型 */
    check(tc_literal_fits_type(0, TC_INT8), "literal_fits_type int8 0");
    check(tc_literal_fits_type(127, TC_INT8), "literal_fits_type int8 127");
    check(!tc_literal_fits_type(128, TC_INT8), "literal_fits_type int8 128 out");
    check(!tc_literal_fits_type((uint64_t)INT64_MAX + 1, TC_INT64),
          "literal_fits_type int64 >INT64_MAX out");

    /* 无符号类型 */
    check(tc_literal_fits_type(0, TC_UINT8), "literal_fits_type uint8 0");
    check(tc_literal_fits_type(255, TC_UINT8), "literal_fits_type uint8 255");
    check(!tc_literal_fits_type(256, TC_UINT8), "literal_fits_type uint8 256 out");
}

static void test_literal_fits_context(void) {
    TcLiteral lit;
    TcErrorKind err = TC_CE_SYNTAX;

    /* bool 字面量 → bool 类型：合法 */
    memset(&lit, 0, sizeof(lit));
    lit.is_bool = 1;
    lit.magnitude = 1;
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_BOOL, &err) == 1, "bool literal → bool type ok");
    /* bool 字面量 → int 类型：非法 → LiteralType */
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_INT32, &err) == 0, "bool literal → int32 fails");
    check(err == TC_CE_LITERAL_TYPE, "bool→int32 err_kind = LITERAL_TYPE");

    /* int 字面量 → bool 类型：非法 */
    memset(&lit, 0, sizeof(lit));
    lit.magnitude = 42;
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_BOOL, &err) == 0, "int literal → bool fails");
    check(err == TC_CE_LITERAL_TYPE, "int→bool err_kind = LITERAL_TYPE");

    /* unsigned suffix → unsigned type: 合法 */
    memset(&lit, 0, sizeof(lit));
    lit.magnitude = 100;
    lit.unsigned_suffix = 1;
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_UINT32, &err) == 1,
          "u-suffix literal → uint32 ok");
    /* unsigned suffix → signed type: 非法 */
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_INT32, &err) == 0,
          "u-suffix literal → int32 fails");
    check(err == TC_CE_LITERAL_TYPE, "u-suffix→int32 err_kind = LITERAL_TYPE");

    /* 负数字面量 → signed type: 合法 */
    memset(&lit, 0, sizeof(lit));
    lit.magnitude = 42;
    lit.negative = 1;
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_INT32, &err) == 1,
          "negative literal → int32 ok");
    /* 负数字面量 → unsigned type: 非法 */
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_UINT32, &err) == 0,
          "negative literal → uint32 fails");
    check(err == TC_CE_LITERAL_OUT_OF_RANGE, "negative→uint32 err_kind = OUT_OF_RANGE");

    /* INT64_MIN 绝对值（2^63）→ int64: 合法 */
    memset(&lit, 0, sizeof(lit));
    lit.magnitude = TC_INT64_MIN_ABS_MAGNITUDE;
    lit.negative = 1;
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_INT64, &err) == 1,
          "INT64_MIN magnitude → int64 ok");

    /* 字面量超出范围 */
    memset(&lit, 0, sizeof(lit));
    lit.magnitude = 256;
    err = TC_CE_SYNTAX;
    check(tc_literal_fits_context(&lit, TC_INT8, &err) == 0,
          "256 literal → int8 out of range");
    check(err == TC_CE_LITERAL_OUT_OF_RANGE, "256→int8 err_kind = OUT_OF_RANGE");
}

static void test_literal_to_value(void) {
    TcValue v;

    /* bool 字面量 */
    {
    TcLiteral bool_true = {1, 0, 0, 1, 0, 0.0, 0, 0, 0, 0};
    v = tc_literal_to_value(&bool_true, TC_BOOL);
    check(v.type->tag == TC_BOOL && v.bits == 1, "literal_to_value bool true → bits=1");
    }

    {
    TcLiteral bool_false = {0, 0, 0, 1, 0, 0.0, 0, 0, 0, 0};
    v = tc_literal_to_value(&bool_false, TC_BOOL);
    check(v.type->tag == TC_BOOL && v.bits == 0, "literal_to_value bool false → bits=0");
    }

    /* 无符号字面量 */
    {
    TcLiteral u_lit = {255, 0, 1, 0, 0, 0.0, 0, 0, 0, 0};
    v = tc_literal_to_value(&u_lit, TC_UINT8);
    check(v.type->tag == TC_UINT8 && v.bits == 255, "literal_to_value uint8 255");
    }

    /* 负数字面量 */
    {
    TcLiteral neg_lit = {42, 1, 0, 0, 0, 0.0, 0, 0, 0, 0};
    v = tc_literal_to_value(&neg_lit, TC_INT8);
    check(v.type->tag == TC_INT8 && v.bits == 0xD6, "literal_to_value int8 -42 → 0xD6");
    check(tc_bits_to_signed(TC_INT8, v.bits) == -42, "int8 -42 value check");
    }

    /* INT64_MIN 绝对值 */
    {
    TcLiteral min_lit = {TC_INT64_MIN_ABS_MAGNITUDE, 1, 0, 0, 0, 0.0, 0, 0, 0, 0};
    v = tc_literal_to_value(&min_lit, TC_INT64);
    check(v.type->tag == TC_INT64 && v.bits == (uint64_t)INT64_MIN,
          "literal_to_value int64 INT64_MIN");
    }

    /* 正数字面量 */
    {
    TcLiteral pos_lit = {123, 0, 0, 0, 0, 0.0, 0, 0, 0, 0};
    v = tc_literal_to_value(&pos_lit, TC_INT32);
    check(v.type->tag == TC_INT32 && v.bits == 123, "literal_to_value int32 123");
    }

    /* nan / ±inf 特殊字面量：硬编码 canonical 位，不经宿主 NAN */
    {
    TcLiteral special;
    memset(&special, 0, sizeof(special));
    special.is_float = 1;
    special.is_float_special = 1;
    special.float_special = 0;
    v = tc_literal_to_value(&special, TC_FLOAT32);
    check(v.type->tag == TC_FLOAT32 && v.bits == TC_FLOAT32_CANONICAL_NAN_BITS,
          "literal_to_value nan → float32 canonical quiet NaN");
    v = tc_literal_to_value(&special, TC_FLOAT64);
    check(v.type->tag == TC_FLOAT64 && v.bits == TC_FLOAT64_CANONICAL_NAN_BITS,
          "literal_to_value nan → float64 canonical quiet NaN");

    special.float_special = 1;
    v = tc_literal_to_value(&special, TC_FLOAT32);
    check(v.bits == TC_FLOAT32_POS_INF_BITS, "literal_to_value +inf → float32");
    v = tc_literal_to_value(&special, TC_FLOAT64);
    check(v.bits == TC_FLOAT64_POS_INF_BITS, "literal_to_value +inf → float64");

    special.float_special = -1;
    v = tc_literal_to_value(&special, TC_FLOAT32);
    check(v.bits == TC_FLOAT32_NEG_INF_BITS, "literal_to_value -inf → float32");
    v = tc_literal_to_value(&special, TC_FLOAT64);
    check(v.bits == TC_FLOAT64_NEG_INF_BITS, "literal_to_value -inf → float64");
    }
}

/* ================================================================== */
/*  算术运算                                                           */
/* ================================================================== */

static void test_arith_signed_add_strict(void) {
    TcValue out;
    TcDiagnostic diag;
    TcValue a = tc_value_make(TC_INT32, 100);
    TcValue b = tc_value_make(TC_INT32, 200);
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_arith(TC_ADD, TC_INT32, TC_ARITH_STRICT, &a, &b, &out, &diag, 1);
    check(rc == 0, "signed add strict 100+200 ok");
    check(out.type->tag == TC_INT32 && out.bits == 300, "100+200=300");

    /* 正溢出 */
    TcValue max = tc_value_make(TC_INT32, INT32_MAX);
    TcValue one = tc_value_make(TC_INT32, 1);
    rc = tc_exec_arith(TC_ADD, TC_INT32, TC_ARITH_STRICT, &max, &one, &out, &diag, 1);
    check(rc == -1, "signed add strict INT32_MAX+1 overflow");
    check(diag.kind == TC_RE_INTEGER_OVERFLOW, "overflow kind check");

    /* 负溢出 */
    TcValue min = tc_value_make(TC_INT32, (uint64_t)(int64_t)INT32_MIN);
    TcValue neg_one = tc_value_make(TC_INT32, (uint64_t)(int64_t)-1);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_arith(TC_ADD, TC_INT32, TC_ARITH_STRICT, &min, &neg_one, &out, &diag, 1);
    check(rc == -1, "signed add strict INT32_MIN+(-1) overflow");

    tc_diagnostic_clear(&diag);
}

static void test_arith_signed_sub_strict(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    TcValue a = tc_value_make(TC_INT32, 100);
    TcValue b = tc_value_make(TC_INT32, 50);
    int rc = tc_exec_arith(TC_SUB, TC_INT32, TC_ARITH_STRICT, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 50, "signed sub strict 100-50=50");

    /* INT32_MIN - 1 */
    TcValue min = tc_value_make(TC_INT32, (uint64_t)(int64_t)INT32_MIN);
    TcValue one = tc_value_make(TC_INT32, 1);
    rc = tc_exec_arith(TC_SUB, TC_INT32, TC_ARITH_STRICT, &min, &one, &out, &diag, 1);
    check(rc == -1, "signed sub strict INT32_MIN-1 overflow");

    tc_diagnostic_clear(&diag);
}

static void test_arith_signed_mul_strict(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    TcValue a = tc_value_make(TC_INT32, 1000);
    TcValue b = tc_value_make(TC_INT32, 2000);
    int rc = tc_exec_arith(TC_MUL, TC_INT32, TC_ARITH_STRICT, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 2000000, "signed mul strict 1000*2000=2000000");

    /* 溢出 */
    TcValue big = tc_value_make(TC_INT32, 50000);
    rc = tc_exec_arith(TC_MUL, TC_INT32, TC_ARITH_STRICT, &big, &big, &out, &diag, 1);
    check(rc == -1, "signed mul strict 50000*50000 overflow");

    /* 零乘 */
    tc_diagnostic_clear(&diag);
    TcValue zero = tc_value_make(TC_INT32, 0);
    rc = tc_exec_arith(TC_MUL, TC_INT32, TC_ARITH_STRICT, &big, &zero, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "signed mul strict 50000*0=0");

    tc_diagnostic_clear(&diag);
}

static void test_arith_signed_div_mod(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    TcValue a = tc_value_make(TC_INT32, 100);
    TcValue b = tc_value_make(TC_INT32, 7);
    int rc = tc_exec_arith(TC_DIV, TC_INT32, TC_ARITH_STRICT, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 14, "signed div strict 100/7=14");

    rc = tc_exec_arith(TC_MOD, TC_INT32, TC_ARITH_STRICT, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 2, "signed mod strict 100%7=2");

    /* 除零 */
    TcValue zero = tc_value_make(TC_INT32, 0);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_arith(TC_DIV, TC_INT32, TC_ARITH_STRICT, &a, &zero, &out, &diag, 1);
    check(rc == -1, "signed div strict 100/0 div by zero");
    check(diag.kind == TC_RE_DIVISION_BY_ZERO, "div by zero kind");

    /* INT32_MIN / -1 → overflow */
    TcValue min = tc_value_make(TC_INT32, (uint64_t)(int64_t)INT32_MIN);
    TcValue neg_one = tc_value_make(TC_INT32, (uint64_t)(int64_t)-1);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_arith(TC_DIV, TC_INT32, TC_ARITH_STRICT, &min, &neg_one, &out, &diag, 1);
    check(rc == -1, "INT32_MIN/-1 overflow");

    /* INT32_MIN % -1 → 0 (合法) */
    tc_diagnostic_clear(&diag);
    rc = tc_exec_arith(TC_MOD, TC_INT32, TC_ARITH_STRICT, &min, &neg_one, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "INT32_MIN%-1 = 0");

    tc_diagnostic_clear(&diag);
}

static void test_arith_signed_wrap(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* int8 wrap: 127 + 1 = -128 */
    TcValue max = tc_value_make(TC_INT8, 127);
    TcValue one = tc_value_make(TC_INT8, 1);
    int rc = tc_exec_arith(TC_ADD, TC_INT8, TC_ARITH_WRAP, &max, &one, &out, &diag, 1);
    check(rc == 0, "signed wrap int8 127+1 ok");
    check(tc_bits_to_signed(TC_INT8, out.bits) == -128, "signed wrap int8 127+1 = -128");

    /* int8 wrap: -128 - 1 = 127 */
    TcValue min = tc_value_make(TC_INT8, 0x80);
    rc = tc_exec_arith(TC_SUB, TC_INT8, TC_ARITH_WRAP, &min, &one, &out, &diag, 1);
    check(rc == 0, "signed wrap int8 -128-1 ok");
    check(tc_bits_to_signed(TC_INT8, out.bits) == 127, "signed wrap int8 -128-1 = 127");

    /* int16 wrap mul */
    TcValue a = tc_value_make(TC_INT16, 256);
    TcValue b = tc_value_make(TC_INT16, 256);
    rc = tc_exec_arith(TC_MUL, TC_INT16, TC_ARITH_WRAP, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "signed wrap int16 256*256 = 0 (low 16 bits)");

    /* int64 wrap mul: use 128-bit multiplication path */
    TcValue big_a = tc_value_make(TC_INT64, (uint64_t)INT64_MAX);
    TcValue big_b = tc_value_make(TC_INT64, 2);
    rc = tc_exec_arith(TC_MUL, TC_INT64, TC_ARITH_WRAP, &big_a, &big_b, &out, &diag, 1);
    check(rc == 0, "signed wrap int64 INT64_MAX*2 ok (wrap)");

    tc_diagnostic_clear(&diag);
}

static void test_umul64(void) {
    uint64_t hi = 0;
    uint64_t lo = 0;

    tc_umul64(0, 0, &hi, &lo);
    check(hi == 0 && lo == 0, "umul64 0*0 = 0");

    tc_umul64(1, UINT64_MAX, &hi, &lo);
    check(hi == 0 && lo == UINT64_MAX, "umul64 1*max = max");

    tc_umul64(UINT64_C(0x100000000), UINT64_C(0x100000000), &hi, &lo);
    check(hi == 1 && lo == 0, "umul64 2^32*2^32 → hi=1 lo=0");

    tc_umul64(UINT64_MAX, UINT64_MAX, &hi, &lo);
    check(hi == UINT64_C(0xFFFFFFFFFFFFFFFE) && lo == 1ULL,
          "umul64 max*max → hi=2^64-2 lo=1");

    tc_umul64(UINT64_C(0xFFFFFFFF), UINT64_C(0xFFFFFFFF), &hi, &lo);
    check(hi == 0 && lo == UINT64_C(0xFFFFFFFE00000001),
          "umul64 (2^32-1)^2");
}

static void test_arith_unsigned(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    TcValue a = tc_value_make(TC_UINT32, 3000000000ULL);
    TcValue b = tc_value_make(TC_UINT32, 1000000000ULL);
    int rc = tc_exec_arith(TC_ADD, TC_UINT32, TC_ARITH_STRICT, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 4000000000ULL, "unsigned add 3B+1B=4B");

    /* 无符号减法，不会借位 */
    TcValue small = tc_value_make(TC_UINT32, 5);
    TcValue big = tc_value_make(TC_UINT32, 10);
    rc = tc_exec_arith(TC_SUB, TC_UINT32, TC_ARITH_STRICT, &small, &big, &out, &diag, 1);
    /* 5 - 10 underflows to large number in unsigned */
    check(rc == 0 && out.bits == 0xFFFFFFFBULL, "unsigned sub 5-10 → wrap around");

    /* 无符号乘 */
    TcValue x = tc_value_make(TC_UINT8, 200);
    TcValue y = tc_value_make(TC_UINT8, 2);
    rc = tc_exec_arith(TC_MUL, TC_UINT8, TC_ARITH_STRICT, &x, &y, &out, &diag, 1);
    check(rc == 0 && out.bits == 144, "unsigned mul uint8 200*2 → 144 (0x90)");

    /* 无符号乘 uint64 (128-bit path) */
    TcValue u64_a = tc_value_make(TC_UINT64, 0x100000000ULL);
    TcValue u64_b = tc_value_make(TC_UINT64, 0x100000000ULL);
    rc = tc_exec_arith(TC_MUL, TC_UINT64, TC_ARITH_STRICT, &u64_a, &u64_b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "unsigned mul uint64 2^32*2^32 → low 64 bits = 0");

    /* 无符号乘 uint64：max*max 低 64 位为 1（验证 umul64 lo） */
    {
        TcValue ua = tc_value_make(TC_UINT64, UINT64_MAX);
        TcValue ub = tc_value_make(TC_UINT64, UINT64_MAX);
        rc = tc_exec_arith(TC_MUL, TC_UINT64, TC_ARITH_STRICT, &ua, &ub, &out, &diag, 1);
        check(rc == 0 && out.bits == 1ULL, "unsigned mul uint64 max*max → 1");
    }

    /* 除零 */
    TcValue zero = tc_value_make(TC_UINT32, 0);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_arith(TC_DIV, TC_UINT32, TC_ARITH_STRICT, &a, &zero, &out, &diag, 1);
    check(rc == -1, "unsigned div by zero");
    check(diag.kind == TC_RE_DIVISION_BY_ZERO, "unsigned div zero kind");

    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  单目运算                                                           */
/* ================================================================== */

static void test_unary_abs(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* int32 abs(42) = 42 */
    TcValue pos = tc_value_make(TC_INT32, 42);
    int rc = tc_exec_unary(TC_UNARY_ABS, TC_INT32, TC_ARITH_STRICT, &pos, &out, &diag, 1);
    check(rc == 0 && out.bits == 42, "abs(42) = 42");

    /* int32 abs(-42) = 42 */
    TcValue neg = tc_value_make(TC_INT32, tc_signed_to_bits(TC_INT32, -42));
    tc_diagnostic_clear(&diag);
    rc = tc_exec_unary(TC_UNARY_ABS, TC_INT32, TC_ARITH_STRICT, &neg, &out, &diag, 1);
    check(rc == 0 && tc_bits_to_signed(TC_INT32, out.bits) == 42, "abs(-42) = 42");

    /* int32 abs(INT32_MIN) → overflow */
    TcValue min = tc_value_make(TC_INT32, (uint64_t)(int64_t)INT32_MIN);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_unary(TC_UNARY_ABS, TC_INT32, TC_ARITH_STRICT, &min, &out, &diag, 1);
    check(rc == -1, "abs(INT32_MIN) overflow");
    check(diag.kind == TC_RE_INTEGER_OVERFLOW, "abs overflow kind");

    /* uint32 abs(100) = 100 (no-op for unsigned) */
    TcValue u = tc_value_make(TC_UINT32, 100);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_unary(TC_UNARY_ABS, TC_UINT32, TC_ARITH_STRICT, &u, &out, &diag, 1);
    check(rc == 0 && out.bits == 100, "unsigned abs(100) = 100");

    tc_diagnostic_clear(&diag);
}

static void test_unary_neg(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* int32 strict neg(42) = -42 */
    TcValue pos = tc_value_make(TC_INT32, 42);
    int rc = tc_exec_unary(TC_UNARY_NEG, TC_INT32, TC_ARITH_STRICT, &pos, &out, &diag, 1);
    check(rc == 0, "neg(42) ok");
    check(tc_bits_to_signed(TC_INT32, out.bits) == -42, "neg(42) = -42");

    /* int32 strict neg(INT32_MIN) → overflow */
    TcValue min = tc_value_make(TC_INT32, (uint64_t)(int64_t)INT32_MIN);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_unary(TC_UNARY_NEG, TC_INT32, TC_ARITH_STRICT, &min, &out, &diag, 1);
    check(rc == -1, "neg(INT32_MIN) overflow (strict)");
    check(diag.kind == TC_RE_INTEGER_OVERFLOW, "neg overflow kind");

    /* int32 wrap neg(INT32_MIN) = INT32_MIN (2's complement wraps back) */
    tc_diagnostic_clear(&diag);
    rc = tc_exec_unary(TC_UNARY_NEG, TC_INT32, TC_ARITH_WRAP, &min, &out, &diag, 1);
    check(rc == 0, "neg(INT32_MIN) wrap ok");
    check(out.bits == ((uint64_t)(int64_t)INT32_MIN & 0xFFFFFFFFULL),
          "neg(INT32_MIN) wrap = INT32_MIN (self)");

    /* uint32 neg(0) = 0 */
    TcValue zero = tc_value_make(TC_UINT32, 0);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_unary(TC_UNARY_NEG, TC_UINT32, TC_ARITH_STRICT, &zero, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "unsigned neg(0) = 0");

    /* uint32 neg(1) = 0xFFFFFFFF */
    TcValue one = tc_value_make(TC_UINT32, 1);
    tc_diagnostic_clear(&diag);
    rc = tc_exec_unary(TC_UNARY_NEG, TC_UINT32, TC_ARITH_STRICT, &one, &out, &diag, 1);
    check(rc == 0 && out.bits == 0xFFFFFFFFULL, "unsigned neg(1) = 0xFFFFFFFF");

    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  比较运算                                                           */
/* ================================================================== */

static void test_compare_signed(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);
    TcValue a = tc_value_make(TC_INT32, 10);
    TcValue b = tc_value_make(TC_INT32, 20);
    TcValue neg_a = tc_value_make(TC_INT32, tc_signed_to_bits(TC_INT32, -10));
    int rc;

    rc = tc_exec_compare(TC_CMP_EQ, TC_INT32, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_BOOL && out.bits == 0, "int32 10 eq 20 → false");

    rc = tc_exec_compare(TC_CMP_NE, TC_INT32, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "int32 10 ne 20 → true");

    rc = tc_exec_compare(TC_CMP_LT, TC_INT32, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "int32 10 lt 20 → true");

    rc = tc_exec_compare(TC_CMP_LE, TC_INT32, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "int32 10 le 20 → true");

    rc = tc_exec_compare(TC_CMP_GT, TC_INT32, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "int32 10 gt 20 → false");

    rc = tc_exec_compare(TC_CMP_GE, TC_INT32, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "int32 10 ge 20 → false");

    /* 负值比较：-10 < 20 */
    rc = tc_exec_compare(TC_CMP_LT, TC_INT32, &neg_a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "int32 -10 lt 20 → true");

    /* 相等 */
    rc = tc_exec_compare(TC_CMP_EQ, TC_INT32, &a, &a, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "int32 10 eq 10 → true");

    tc_diagnostic_clear(&diag);
}

static void test_compare_unsigned(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);
    TcValue small = tc_value_make(TC_UINT32, 5);
    TcValue big = tc_value_make(TC_UINT32, 4000000000ULL);
    int rc;

    rc = tc_exec_compare(TC_CMP_LT, TC_UINT32, &small, &big, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "uint32 5 lt 4B → true");

    rc = tc_exec_compare(TC_CMP_GT, TC_UINT32, &small, &big, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "uint32 5 gt 4B → false");

    rc = tc_exec_compare(TC_CMP_EQ, TC_UINT32, &big, &big, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "uint32 big eq big → true");

    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  逻辑运算                                                           */
/* ================================================================== */

static void test_logic_binary(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    TcValue t = tc_value_make(TC_BOOL, 1);
    TcValue f = tc_value_make(TC_BOOL, 0);

    /* AND: t && t = t */
    int rc = tc_exec_logic_binary(TC_LOGIC_AND, &t, &t, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_BOOL && out.bits == 1, "and(true,true) = true");

    /* AND: t && f = f */
    rc = tc_exec_logic_binary(TC_LOGIC_AND, &t, &f, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "and(true,false) = false");

    /* AND: f && anything = f (short-circuit) */
    rc = tc_exec_logic_binary(TC_LOGIC_AND, &f, &t, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "and(false,true) = false (short-circuit)");

    /* OR: t || anything = t (short-circuit) */
    rc = tc_exec_logic_binary(TC_LOGIC_OR, &t, &f, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "or(true,false) = true (short-circuit)");

    /* OR: f || t = t */
    rc = tc_exec_logic_binary(TC_LOGIC_OR, &f, &t, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "or(false,true) = true");

    /* OR: f || f = f */
    rc = tc_exec_logic_binary(TC_LOGIC_OR, &f, &f, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "or(false,false) = false");

    tc_diagnostic_clear(&diag);
}

static void test_logic_unary(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    TcValue t = tc_value_make(TC_BOOL, 1);
    TcValue f = tc_value_make(TC_BOOL, 0);

    int rc = tc_exec_logic_unary(TC_LOGIC_NOT, &t, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_BOOL && out.bits == 0, "not(true) = false");

    rc = tc_exec_logic_unary(TC_LOGIC_NOT, &f, &out, &diag, 1);
    check(rc == 0 && out.bits == 1, "not(false) = true");

    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  cast 运算 — strict mode                                            */
/* ================================================================== */

static void test_cast_strict_widen_signed_to_signed(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);
    TcValue src = tc_value_make(TC_INT8, tc_signed_to_bits(TC_INT8, -42));

    int rc = tc_exec_cast(TC_INT32, &src, &out, &diag, 1);
    check(rc == 0, "cast strict int8(-42) → int32 ok");
    check(tc_bits_to_signed(TC_INT32, out.bits) == -42, "int8(-42) → int32 = -42");

    tc_diagnostic_clear(&diag);
}

static void test_cast_strict_widen_unsigned_to_unsigned(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);
    TcValue src = tc_value_make(TC_UINT8, 200);

    int rc = tc_exec_cast(TC_UINT32, &src, &out, &diag, 1);
    check(rc == 0 && out.bits == 200, "cast strict uint8(200) → uint32 = 200");

    tc_diagnostic_clear(&diag);
}

static void test_cast_strict_widen_signed_to_unsigned(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* 负值 → 无符号：拒绝 */
    TcValue neg = tc_value_make(TC_INT8, tc_signed_to_bits(TC_INT8, -1));
    int rc = tc_exec_cast(TC_UINT32, &neg, &out, &diag, 1);
    check(rc == -1, "cast strict int8(-1) → uint32 fails");
    check(diag.kind == TC_RE_CAST_OVERFLOW, "neg→unsigned overflow kind");

    /* 正值 → 无符号：OK */
    tc_diagnostic_clear(&diag);
    TcValue pos = tc_value_make(TC_INT8, 100);
    rc = tc_exec_cast(TC_UINT32, &pos, &out, &diag, 1);
    check(rc == 0 && out.bits == 100, "cast strict int8(100) → uint32 = 100");

    tc_diagnostic_clear(&diag);
}

static void test_cast_strict_widen_unsigned_to_signed(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* uint8 200 → int8 窄化（同宽），不触发 widen */
    /* 测真正 widen: uint8 → int32 */
    TcValue src = tc_value_make(TC_UINT8, 200);
    int rc = tc_exec_cast(TC_INT32, &src, &out, &diag, 1);
    check(rc == 0 && out.bits == 200, "cast strict uint8(200) → int32 = 200");

    tc_diagnostic_clear(&diag);
}

static void test_cast_strict_same_width_diff_sign(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* int8(-1) → uint8: 拒绝（负值 → 无符号） */
    TcValue neg = tc_value_make(TC_INT8, tc_signed_to_bits(TC_INT8, -1));
    int rc = tc_exec_cast(TC_UINT8, &neg, &out, &diag, 1);
    check(rc == -1, "cast strict int8(-1) → uint8 fails");

    /* uint8(200) → int8: 200 > 127, 拒绝（超有符号范围） */
    tc_diagnostic_clear(&diag);
    TcValue big = tc_value_make(TC_UINT8, 200);
    rc = tc_exec_cast(TC_INT8, &big, &out, &diag, 1);
    check(rc == -1, "cast strict uint8(200) → int8 fails (out of range)");

    /* uint8(100) → int8: OK */
    tc_diagnostic_clear(&diag);
    TcValue pos = tc_value_make(TC_UINT8, 100);
    rc = tc_exec_cast(TC_INT8, &pos, &out, &diag, 1);
    check(rc == 0 && tc_bits_to_signed(TC_INT8, out.bits) == 100,
          "cast strict uint8(100) → int8 = 100");

    tc_diagnostic_clear(&diag);
}

static void test_cast_strict_narrow(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* int32(1000) → int8: 1000 > 127, 拒绝 */
    TcValue big = tc_value_make(TC_INT32, 1000);
    int rc = tc_exec_cast(TC_INT8, &big, &out, &diag, 1);
    check(rc == -1, "cast strict int32(1000) → int8 fails (out of range)");

    /* int32(42) → int8: OK */
    tc_diagnostic_clear(&diag);
    TcValue small = tc_value_make(TC_INT32, 42);
    rc = tc_exec_cast(TC_INT8, &small, &out, &diag, 1);
    check(rc == 0 && tc_bits_to_signed(TC_INT8, out.bits) == 42,
          "cast strict int32(42) → int8 = 42");

    /* uint32(500) → uint8: strict conversion checks mathematical range. */
    tc_diagnostic_clear(&diag);
    TcValue u_big = tc_value_make(TC_UINT32, 500);
    rc = tc_exec_cast(TC_UINT8, &u_big, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_CAST_OVERFLOW,
          "cast strict uint32(500) → uint8 fails");

    /* int32(-1) → uint8: 拒绝（负值 → 无符号） */
    tc_diagnostic_clear(&diag);
    TcValue neg = tc_value_make(TC_INT32, (uint64_t)(int64_t)-1);
    rc = tc_exec_cast(TC_UINT8, &neg, &out, &diag, 1);
    check(rc == -1, "cast strict int32(-1) → uint8 fails");

    tc_diagnostic_clear(&diag);
}

static void test_cast_strict_bool(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* bool → bool */
    TcValue b_true = tc_value_make(TC_BOOL, 1);
    int rc = tc_exec_cast(TC_BOOL, &b_true, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_BOOL && out.bits == 1, "cast strict bool→bool ok");

    /* bool → int32 */
    rc = tc_exec_cast(TC_INT32, &b_true, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_INT32 && out.bits == 1, "cast strict bool→int32 = 1");

    /* int32 → bool */
    TcValue i42 = tc_value_make(TC_INT32, 42);
    rc = tc_exec_cast(TC_BOOL, &i42, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_BOOL && out.bits == 1, "cast strict int32(42)→bool = true");

    TcValue i0 = tc_value_make(TC_INT32, 0);
    rc = tc_exec_cast(TC_BOOL, &i0, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "cast strict int32(0)→bool = false");

    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  cast 运算 — truncate mode                                          */
/* ================================================================== */

static void test_cast_truncate(void) {
    TcValue out;
    TcDiagnostic diag;
    tc_diagnostic_init(&diag);

    /* int16(-128) → int8: 截断低 8 位 = 0x80 = -128 */
    TcValue v = tc_value_make(TC_INT16, tc_signed_to_bits(TC_INT16, -128));
    int rc = tc_exec_truncate(TC_INT8, &v, &out, &diag, 1);
    check(rc == 0, "cast truncate int16(-128) → int8 ok");
    check(tc_bits_to_signed(TC_INT8, out.bits) == -128, "int16(-128) trunc→int8 = -128");

    /* int16(511) → int8: 截断低 8 位 = 0xFF = -1 */
    v = tc_value_make(TC_INT16, 511);
    rc = tc_exec_truncate(TC_INT8, &v, &out, &diag, 1);
    check(rc == 0, "cast truncate int16(511) → int8 ok");
    check(tc_bits_to_signed(TC_INT8, out.bits) == -1, "int16(511) trunc→int8 = -1");

    /* truncate rejects widening. */
    v = tc_value_make(TC_INT8, tc_signed_to_bits(TC_INT8, -1));
    rc = tc_exec_truncate(TC_INT16, &v, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "cast truncate int8(-1) → int16 rejects widening");

    /* uint8(200) → int16: 零扩展 */
    v = tc_value_make(TC_UINT8, 200);
    rc = tc_exec_truncate(TC_INT16, &v, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "cast truncate uint8(200) → int16 rejects widening");

    /* bool → int32 */
    TcValue b = tc_value_make(TC_BOOL, 1);
    rc = tc_exec_truncate(TC_INT32, &b, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "cast truncate rejects bool source");

    /* int32 → bool */
    v = tc_value_make(TC_INT32, 42);
    rc = tc_exec_truncate(TC_BOOL, &v, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "cast truncate rejects bool target");

    v = tc_value_make(TC_INT16, UINT64_C(0xFF7F));
    rc = tc_exec_truncate(TC_UINT8, &v, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_UINT8 && out.bits == UINT64_C(0x7F),
          "truncate signed source to unsigned target preserves low bits");

    v = tc_value_make(TC_UINT16, UINT64_C(0xABCD));
    rc = tc_exec_truncate(TC_INT8, &v, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_INT8 && out.bits == UINT64_C(0xCD),
          "truncate unsigned source to signed target preserves low bits");

    v = tc_value_make(TC_INT8, UINT64_C(0xFF));
    rc = tc_exec_truncate(TC_UINT8, &v, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects equal-width integer conversion");

    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  浮点算术（tc_exec_fp_arith strict 骨架）                             */
/* ================================================================== */

static TcValue fp64_from_double(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return tc_value_make(TC_FLOAT64, bits);
}

static int fp64_approx_equal(uint64_t bits, double expected) {
    double actual = 0.0;
    memcpy(&actual, &bits, sizeof(actual));
    return fabs(actual - expected) < 1e-9;
}

static TcValue fp32_from_double(double value) {
    float f = (float)value;
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return tc_value_make(TC_FLOAT32, (uint64_t)bits);
}

static int fp32_approx_equal(uint64_t bits, double expected) {
    uint32_t b32 = (uint32_t)bits;
    float actual_float = 0.0f;
    memcpy(&actual_float, &b32, sizeof(actual_float));
    return fabs((double)actual_float - expected) < 1e-6;
}

static void test_cast_strict_matrix(void) {
    static const TcValue sources[] = {
        {TC_TYPE_PTR(TC_INT8), UINT64_C(0xFF)},
        {TC_TYPE_PTR(TC_UINT8), UINT64_C(255)},
        {TC_TYPE_PTR(TC_INT16), UINT64_C(0xFF7F)},
        {TC_TYPE_PTR(TC_UINT16), UINT64_C(65535)},
        {TC_TYPE_PTR(TC_INT32), UINT64_C(0xFFFF7FFF)},
        {TC_TYPE_PTR(TC_UINT32), UINT64_C(0xFFFFFFFF)},
        {TC_TYPE_PTR(TC_INT64), UINT64_C(0x8000000000000000)},
        {TC_TYPE_PTR(TC_UINT64), UINT64_MAX},
        {TC_TYPE_PTR(TC_BOOL), UINT64_C(1)},
        {TC_TYPE_PTR(TC_FLOAT32), UINT64_C(0x437FC000)},
        {TC_TYPE_PTR(TC_FLOAT64), UINT64_C(0xC060380000000000)},
    };
    static const int succeeds[11][11] = {
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1},
        {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1},
        {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1},
    };
    size_t source_index = 0;

    for (source_index = 0; source_index < 11; source_index++) {
        TcTypeTag target = TC_INT8;
        for (target = TC_INT8; target <= TC_FLOAT64; target = (TcTypeTag)(target + 1)) {
            TcDiagnostic diag;
            TcValue out = {0};
            char message[128];
            int rc = 0;

            tc_diagnostic_init(&diag);
            rc = tc_exec_cast(target, &sources[source_index], &out, &diag, 1);
            (void)snprintf(message, sizeof(message), "strict cast matrix %s -> %s status",
                           tc_type_name(sources[source_index].type->tag), tc_type_name(target));
            check((rc == 0) == succeeds[source_index][target], message);
            if (succeeds[source_index][target]) {
                (void)snprintf(message, sizeof(message), "strict cast matrix %s -> %s value type",
                               tc_type_name(sources[source_index].type->tag), tc_type_name(target));
                check(out.type->tag == target, message);
                if (target == sources[source_index].type->tag) {
                    (void)snprintf(message, sizeof(message),
                                   "strict same-type cast %s preserves complete bits",
                                   tc_type_name(target));
                    check(out.bits == sources[source_index].bits, message);
                }
            } else {
                (void)snprintf(message, sizeof(message), "strict cast matrix %s -> %s error kind",
                               tc_type_name(sources[source_index].type->tag), tc_type_name(target));
                check(diag.kind == TC_RE_CAST_OVERFLOW, message);
            }
            tc_diagnostic_clear(&diag);
        }
    }
}

static void test_cast_same_type_float_bit_identity(void) {
    static const TcValue sources[] = {
        {TC_TYPE_PTR(TC_FLOAT32), UINT64_C(0x80000000)},
        {TC_TYPE_PTR(TC_FLOAT32), UINT64_C(0x7FC12345)},
        {TC_TYPE_PTR(TC_FLOAT64), UINT64_C(0x8000000000000000)},
        {TC_TYPE_PTR(TC_FLOAT64), UINT64_C(0x7FF8000000001234)},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        TcDiagnostic diag;
        TcValue out = {0};

        tc_diagnostic_init(&diag);
        check(tc_exec_cast(sources[i].type->tag, &sources[i], &out, &diag, 1) == 0 &&
                  out.type->tag == sources[i].type->tag && out.bits == sources[i].bits,
              "same-type float cast preserves negative zero or complete NaN payload");
        tc_diagnostic_clear(&diag);
    }
}

static void test_fp_arith_add_strict(void) {
    TcValue lhs = fp64_from_double(1.5);
    TcValue rhs = fp64_from_double(2.5);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_ADD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_FLOAT64 && fp64_approx_equal(out.bits, 4.0),
          "fp64 add strict 1.5+2.5=4.0");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_div_strict(void) {
    TcValue lhs = fp64_from_double(10.0);
    TcValue rhs = fp64_from_double(2.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_DIV, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp64_approx_equal(out.bits, 5.0), "fp64 div strict 10/2=5.0");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_div_zero_strict(void) {
    TcValue lhs = fp64_from_double(1.0);
    TcValue rhs = fp64_from_double(0.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_DIV, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc != 0 && diag.kind == TC_RE_DIVISION_BY_ZERO,
          "fp64 div strict 1/0 → DIVISION_BY_ZERO");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_nan_operand_strict(void) {
    TcValue lhs = fp64_from_double(NAN);
    TcValue rhs = fp64_from_double(1.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_ADD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc != 0 && diag.kind == TC_RE_FLOAT_INVALID,
          "fp64 add strict nan operand → FLOAT_INVALID");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_sub_strict(void) {
    TcValue lhs = fp64_from_double(5.0);
    TcValue rhs = fp64_from_double(3.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_SUB, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_FLOAT64 && fp64_approx_equal(out.bits, 2.0),
          "fp64 sub strict 5.0-3.0=2.0");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_mul_strict(void) {
    TcValue lhs = fp64_from_double(1.5);
    TcValue rhs = fp64_from_double(4.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_MUL, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp64_approx_equal(out.bits, 6.0), "fp64 mul strict 1.5*4.0=6.0");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_mul_underflow_flush_to_zero_strict(void) {
    TcValue lhs = fp64_from_double(1e-200);
    TcValue rhs = fp64_from_double(1e-200);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_MUL, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc != 0 && diag.kind == TC_RE_FLOAT_UNDERFLOW,
          "fp64 mul strict inexact tiny result rounded to zero -> FLOAT_UNDERFLOW");
    tc_diagnostic_clear(&diag);
}

static void test_fp_strict_exception_priority(void) {
    static const struct {
        TcTypeTag type;
        TcArithOp op;
        uint64_t lhs_bits;
        uint64_t rhs_bits;
        TcErrorKind expected;
        const char *message;
    } cases[] = {
        {TC_FLOAT64, TC_DIV, UINT64_C(0x0000000000000000),
         UINT64_C(0x0000000000000000), TC_RE_FLOAT_INVALID,
         "strict 0/0 reports invalid before division by zero"},
        {TC_FLOAT64, TC_DIV, UINT64_C(0x7FF8000000001234),
         UINT64_C(0x0000000000000000), TC_RE_FLOAT_INVALID,
         "strict NaN/0 reports invalid before division by zero"},
        {TC_FLOAT64, TC_DIV, UINT64_C(0x3FF0000000000000),
         UINT64_C(0x0000000000000000), TC_RE_DIVISION_BY_ZERO,
         "strict 1/0 reports division by zero"},
        {TC_FLOAT32, TC_MUL, UINT64_C(0x7149F2CA), UINT64_C(0x7149F2CA),
         TC_RE_FLOAT_OVERFLOW, "strict float32 overflow is detected at binary32 precision"},
        {TC_FLOAT32, TC_MUL, UINT64_C(0x0DA24260), UINT64_C(0x1E3CE508),
         TC_RE_FLOAT_UNDERFLOW,
         "strict float32 inexact tiny result rounded to zero reports underflow"},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcDiagnostic diag;
        TcValue lhs = tc_value_make(cases[i].type, cases[i].lhs_bits);
        TcValue rhs = tc_value_make(cases[i].type, cases[i].rhs_bits);
        TcValue out = {0};
        int rc = 0;

        tc_diagnostic_init(&diag);
        rc = tc_exec_fp_arith(cases[i].op, cases[i].type, TC_FLOAT_STRICT,
                              &lhs, &rhs, &out, &diag, 1);
        check(rc != 0 && diag.kind == cases[i].expected, cases[i].message);
        tc_diagnostic_clear(&diag);
    }
}

static void test_fp_ieee_canonical_nan(void) {
    static const struct {
        TcTypeTag type;
        uint64_t lhs_bits;
        uint64_t rhs_bits;
        uint64_t expected_bits;
        const char *message;
    } cases[] = {
        {TC_FLOAT32, UINT64_C(0), UINT64_C(0), UINT64_C(0x7FC00000),
         "ieee float32 0/0 returns canonical quiet NaN"},
        {TC_FLOAT64, UINT64_C(0), UINT64_C(0), UINT64_C(0x7FF8000000000000),
         "ieee float64 0/0 returns canonical quiet NaN"},
        {TC_FLOAT32, UINT64_C(0x7FC12345), UINT64_C(0x3F800000),
         UINT64_C(0x7FC00000), "ieee float32 NaN payload input is canonicalized"},
        {TC_FLOAT64, UINT64_C(0x7FF8000000001234), UINT64_C(0x3FF0000000000000),
         UINT64_C(0x7FF8000000000000), "ieee float64 NaN payload input is canonicalized"},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcDiagnostic diag;
        TcValue lhs = tc_value_make(cases[i].type, cases[i].lhs_bits);
        TcValue rhs = tc_value_make(cases[i].type, cases[i].rhs_bits);
        TcValue out = {0};
        TcArithOp op = i < 2 ? TC_DIV : TC_ADD;

        tc_diagnostic_init(&diag);
        check(tc_exec_fp_arith(op, cases[i].type, TC_FLOAT_IEEE,
                               &lhs, &rhs, &out, &diag, 1) == 0 &&
                  out.type->tag == cases[i].type && out.bits == cases[i].expected_bits,
              cases[i].message);
        tc_diagnostic_clear(&diag);
    }
}

static void test_fp_round_ties_to_even(void) {
    TcDiagnostic diag;
    TcValue lhs = tc_value_make(TC_FLOAT32, UINT64_C(0x3F800000));
    TcValue rhs = tc_value_make(TC_FLOAT32, UINT64_C(0x33800000));
    TcValue out = {0};
#ifdef TC_HAVE_FENV
    int saved_round = fegetround();
    (void)fesetround(FE_UPWARD);
#endif

    tc_diagnostic_init(&diag);
    check(tc_exec_fp_arith(TC_ADD, TC_FLOAT32, TC_FLOAT_IEEE,
                           &lhs, &rhs, &out, &diag, 1) == 0 &&
              out.bits == UINT64_C(0x3F800000),
          "float32 halfway addition uses roundTiesToEven independent of host rounding mode");
    tc_diagnostic_clear(&diag);
#ifdef TC_HAVE_FENV
    (void)fesetround(saved_round);
#endif
}

static void test_cast_round_ties_to_even(void) {
    TcDiagnostic diag;
    TcValue float_midpoint = tc_value_make(TC_FLOAT64, UINT64_C(0x3FF0000010000000));
    TcValue integer_midpoint = tc_value_make(TC_INT64, UINT64_C(16777217));
    TcValue uint64_max = tc_value_make(TC_UINT64, UINT64_MAX);
    TcValue out = {0};
#ifdef TC_HAVE_FENV
    int saved_round = fegetround();
    (void)fesetround(FE_UPWARD);
#endif

    tc_diagnostic_init(&diag);
    check(tc_exec_cast(TC_FLOAT32, &float_midpoint, &out, &diag, 1) == 0 &&
              out.bits == UINT64_C(0x3F800000),
          "float64 to float32 cast midpoint uses roundTiesToEven");
    check(tc_exec_cast(TC_FLOAT32, &integer_midpoint, &out, &diag, 1) == 0 &&
              out.bits == UINT64_C(0x4B800000),
          "integer to float32 cast midpoint uses roundTiesToEven");
#ifdef TC_HAVE_FENV
    (void)fesetround(FE_DOWNWARD);
#endif
    check(tc_exec_cast(TC_FLOAT64, &uint64_max, &out, &diag, 1) == 0 &&
              out.bits == UINT64_C(0x43F0000000000000),
          "uint64 to float64 cast uses roundTiesToEven");
    tc_diagnostic_clear(&diag);
#ifdef TC_HAVE_FENV
    (void)fesetround(saved_round);
#endif
}

static void test_fp_nan_compare_matrix(void) {
    static const uint64_t nan_bits[] = {UINT64_C(0x7FC12345),
                                        UINT64_C(0x7FF8000000001234)};
    static const uint64_t one_bits[] = {UINT64_C(0x3F800000),
                                        UINT64_C(0x3FF0000000000000)};
    static const int expected[] = {0, 1, 0, 0, 0, 0};
    TcTypeTag type = TC_FLOAT32;

    for (type = TC_FLOAT32; type <= TC_FLOAT64; type = (TcTypeTag)(type + 1)) {
        size_t type_index = (size_t)(type - TC_FLOAT32);
        TcCompareOp op = TC_CMP_EQ;
        for (op = TC_CMP_EQ; op <= TC_CMP_GE; op = (TcCompareOp)(op + 1)) {
            TcDiagnostic diag;
            TcValue nan = tc_value_make(type, nan_bits[type_index]);
            TcValue one = tc_value_make(type, one_bits[type_index]);
            TcValue out = {0};
            char message[128];

            tc_diagnostic_init(&diag);
            (void)snprintf(message, sizeof(message), "NaN compare matrix %s op=%d",
                           tc_type_name(type), (int)op);
            check(tc_exec_fp_compare(op, type, TC_FLOAT_STRICT, &nan, &one, &out, &diag, 1) == 0 &&
                      out.type->tag == TC_BOOL && out.bits == (uint64_t)expected[op],
                  message);
            tc_diagnostic_clear(&diag);
        }
        {
            TcDiagnostic diag;
            TcValue nan = tc_value_make(type, nan_bits[type_index]);
            TcValue one = tc_value_make(type, one_bits[type_index]);
            TcValue out = {0};

            tc_diagnostic_init(&diag);
            check(tc_exec_fp_compare(TC_CMP_EQ, type, TC_FLOAT_IEEE, &nan, &one,
                                     &out, &diag, 1) != 0 &&
                      diag.kind == TC_CE_MODE_MISMATCH,
                  "float compare rejects ieee mode keyword");
            tc_diagnostic_clear(&diag);
        }
    }
}

static void test_fp_unary_preserves_payload_bits(void) {
    static const struct {
        TcTypeTag type;
        uint64_t source;
        uint64_t negated;
        uint64_t absolute;
    } cases[] = {
        {TC_FLOAT32, UINT64_C(0x7FC12345), UINT64_C(0xFFC12345), UINT64_C(0x7FC12345)},
        {TC_FLOAT64, UINT64_C(0xFFF8000000001234), UINT64_C(0x7FF8000000001234),
         UINT64_C(0x7FF8000000001234)},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcDiagnostic diag;
        TcValue source = tc_value_make(cases[i].type, cases[i].source);
        TcValue out = {0};

        tc_diagnostic_init(&diag);
        check(tc_exec_fp_unary(TC_UNARY_NEG, cases[i].type, TC_FLOAT_STRICT,
                               &source, &out, &diag, 1) == 0 && out.bits == cases[i].negated,
              "float neg flips only the sign bit and preserves NaN payload");
        check(tc_exec_fp_unary(TC_UNARY_ABS, cases[i].type, TC_FLOAT_STRICT,
                               &source, &out, &diag, 1) == 0 && out.bits == cases[i].absolute,
              "float abs clears only the sign bit and preserves NaN payload");
        tc_diagnostic_clear(&diag);
    }
}

static void test_fp_compare_rejects_wrap(void) {
    TcDiagnostic diag;
    TcValue lhs = fp32_from_double(1.0);
    TcValue rhs = fp32_from_double(1.0);
    TcValue out = {0};

    tc_diagnostic_init(&diag);
    check(tc_exec_fp_compare(TC_CMP_EQ, TC_FLOAT32, TC_FLOAT_WRAP,
                             &lhs, &rhs, &out, &diag, 1) != 0 &&
              diag.kind == TC_CE_MODE_MISMATCH,
          "float comparison rejects wrap mode at shared semantics boundary");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_mul_underflow_subnormal_strict(void) {
    TcValue lhs = fp64_from_double(1e-300);
    TcValue rhs = fp64_from_double(1e-10);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_MUL, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc != 0 && diag.kind == TC_RE_FLOAT_UNDERFLOW,
          "fp64 mul strict 1e-300*1e-10 → FLOAT_UNDERFLOW");
    tc_diagnostic_clear(&diag);
}

static void test_fp_exact_subnormal_does_not_underflow(void) {
    static const struct {
        TcTypeTag type;
        uint64_t lhs_bits;
        uint64_t rhs_bits;
        uint64_t expected_bits;
        const char *message;
    } cases[] = {
        {TC_FLOAT32, UINT64_C(0x00800000), UINT64_C(0x3F000000),
         UINT64_C(0x00400000), "exact float32 subnormal result is allowed"},
        {TC_FLOAT64, UINT64_C(0x0010000000000000), UINT64_C(0x3FE0000000000000),
         UINT64_C(0x0008000000000000), "exact float64 subnormal result is allowed"},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TcDiagnostic diag;
        TcValue lhs = tc_value_make(cases[i].type, cases[i].lhs_bits);
        TcValue rhs = tc_value_make(cases[i].type, cases[i].rhs_bits);
        TcValue out = {0};

        tc_diagnostic_init(&diag);
        check(tc_exec_fp_arith(TC_MUL, cases[i].type, TC_FLOAT_STRICT,
                               &lhs, &rhs, &out, &diag, 1) == 0 &&
                  out.bits == cases[i].expected_bits,
              cases[i].message);
        tc_diagnostic_clear(&diag);
    }
}

static void test_fp_arith_add_float32_strict(void) {
    TcValue lhs = fp32_from_double(1.5);
    TcValue rhs = fp32_from_double(2.5);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_ADD, TC_FLOAT32, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_FLOAT32 && fp32_approx_equal(out.bits, 4.0),
          "fp32 add strict 1.5+2.5=4.0");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_sub_float32_strict(void) {
    TcValue lhs = fp32_from_double(10.0);
    TcValue rhs = fp32_from_double(3.5);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_SUB, TC_FLOAT32, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp32_approx_equal(out.bits, 6.5), "fp32 sub strict 10.0-3.5=6.5");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_mul_float32_strict(void) {
    TcValue lhs = fp32_from_double(2.5);
    TcValue rhs = fp32_from_double(3.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_MUL, TC_FLOAT32, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp32_approx_equal(out.bits, 7.5), "fp32 mul strict 2.5*3.0=7.5");
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_div_float32_strict(void) {
    TcValue lhs = fp32_from_double(7.0);
    TcValue rhs = fp32_from_double(2.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_arith(TC_DIV, TC_FLOAT32, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp32_approx_equal(out.bits, 3.5), "fp32 div strict 7.0/2.0=3.5");
    tc_diagnostic_clear(&diag);
}

static void test_fp_unary_neg_strict(void) {
    TcValue operand = fp64_from_double(3.5);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_unary(TC_UNARY_NEG, TC_FLOAT64, TC_FLOAT_STRICT, &operand, &out, &diag, 1);
    check(rc == 0 && fp64_approx_equal(out.bits, -3.5), "fp64 neg strict -3.5");
    tc_diagnostic_clear(&diag);
}

static void test_fp_unary_abs_mode_matrix(void) {
    TcValue operand = fp64_from_double(-2.5);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_unary(TC_UNARY_ABS, TC_FLOAT64, TC_FLOAT_STRICT, &operand, &out, &diag, 1);
    check(rc == 0 && fp64_approx_equal(out.bits, 2.5), "fp64 abs strict 2.5");
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_unary(TC_UNARY_ABS, TC_FLOAT64, TC_FLOAT_IEEE, &operand, &out, &diag, 1);
    check(rc != 0 && diag.kind == TC_CE_MODE_MISMATCH,
          "fp64 abs rejects ieee mode keyword");
    tc_diagnostic_clear(&diag);
}

static void test_fp_compare_nan_ne(void) {
    TcValue lhs = fp64_from_double(1.0);
    TcValue rhs = fp64_from_double(1.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;
    uint64_t nan_bits = 0;

    memcpy(&nan_bits, &(double){NAN}, sizeof(nan_bits));
    lhs.bits = nan_bits;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_compare(TC_CMP_NE, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_BOOL && out.bits == 1, "fp64 ne(nan,1.0)=true");
    tc_diagnostic_clear(&diag);
}

static void test_fp_compare_eq_nan(void) {
    TcValue lhs = fp64_from_double(1.0);
    TcValue rhs = fp64_from_double(1.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;
    uint64_t nan_bits = 0;

    memcpy(&nan_bits, &(double){NAN}, sizeof(nan_bits));
    lhs.bits = nan_bits;

    tc_diagnostic_init(&diag);
    rc = tc_exec_fp_compare(TC_CMP_EQ, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_BOOL && out.bits == 0, "fp64 eq(nan,1.0)=false");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_int_to_float64(void) {
    TcValue src = tc_value_make(TC_INT32, 42);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_cast(TC_FLOAT64, &src, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_FLOAT64 && fp64_approx_equal(out.bits, 42.0),
          "cast int32(42) → float64");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_float64_to_int32(void) {
    TcValue src = fp64_from_double(7.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_cast(TC_INT32, &src, &out, &diag, 1);
    check(rc == 0 && out.type->tag == TC_INT32 && out.bits == 7, "cast float64(7.0) → int32");
    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  浮点 truncate cast 边界                                              */
/* ================================================================== */

static void test_fp_cast_truncate_f32_to_i32(void) {
    TcValue src = fp32_from_double(1.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_INT32, &src, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects float32 → int32");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_truncate_i32_to_f32(void) {
    TcValue src = tc_value_make(TC_INT32, 1065353216u);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_FLOAT32, &src, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects int32 → float32");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_truncate_f64_to_i64(void) {
    TcValue src = fp64_from_double(1.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_INT64, &src, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects float64 → int64");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_truncate_i64_to_f64(void) {
    TcValue src = tc_value_make(TC_INT64, 4607182418800017408ULL);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_FLOAT64, &src, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects int64 → float64");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_truncate_f64_to_f32_mask(void) {
    TcValue src = fp64_from_double(1.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_FLOAT32, &src, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects float64 → float32");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_truncate_f32_to_f64_mask(void) {
    TcValue src = fp32_from_double(3.14159);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_FLOAT64, &src, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects float32 → float64");
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_truncate_f64_to_f32_roundtrip(void) {
    TcValue src = fp32_from_double(3.14159);
    TcValue mid;
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_FLOAT64, &src, &mid, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate float roundtrip is rejected at first step");
    (void)out;
    tc_diagnostic_clear(&diag);
}

static void test_fp_cast_truncate_neg_zero_bits(void) {
    TcValue src = tc_value_make(TC_INT32, (uint64_t)(uint32_t)0x80000000u);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    tc_diagnostic_init(&diag);
    rc = tc_exec_truncate(TC_FLOAT32, &src, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "truncate rejects integer → float negative-zero bit pattern");
    (void)out;
    tc_diagnostic_clear(&diag);
}

static void test_fp_arith_wrap_add(void) {
    TcValue lhs = fp64_from_double(1.0);
    TcValue rhs = fp64_from_double(2.0);
    TcValue out;
    TcDiagnostic diag;
    int rc;

    (void)lhs;
    (void)rhs;
    tc_diagnostic_init(&diag);
    lhs = fp64_from_double(1.0);
    rhs = fp64_from_double(2.0);
    rc = tc_exec_fp_arith(TC_ADD, TC_FLOAT64, TC_FLOAT_WRAP, &lhs, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_CE_MODE_MISMATCH,
          "fp64 add wrap is rejected");
    tc_diagnostic_clear(&diag);
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

static void test_bitcast_semantics(void) {
    TcDiagnostic diag;
    TcValue f32 = {TC_TYPE_PTR(TC_FLOAT32), UINT64_C(0xBF800000)};
    TcValue u32 = {TC_TYPE_PTR(TC_UINT32), UINT64_C(0x7FC12345)};
    TcValue u64 = {TC_TYPE_PTR(TC_UINT64), UINT64_C(0x7FF8000000001234)};
    TcValue boolean = {TC_TYPE_PTR(TC_BOOL), 1};
    TcValue out = {0};

    tc_diagnostic_init(&diag);
    check(tc_exec_bitcast(TC_UINT32, &f32, &out, &diag, 1) == 0,
          "bitcast float32 to uint32");
    check(out.type->tag == TC_UINT32 && out.bits == UINT64_C(0xBF800000),
          "bitcast32 preserves all bits");
    check(tc_exec_bitcast(TC_FLOAT32, &u32, &out, &diag, 1) == 0,
          "bitcast uint32 NaN payload to float32");
    check(out.type->tag == TC_FLOAT32 && out.bits == UINT64_C(0x7FC12345),
          "bitcast32 preserves NaN payload");
    check(tc_exec_bitcast(TC_FLOAT64, &u64, &out, &diag, 1) == 0,
          "bitcast uint64 NaN payload to float64");
    check(out.type->tag == TC_FLOAT64 && out.bits == UINT64_C(0x7FF8000000001234),
          "bitcast64 preserves NaN payload");
    check(tc_exec_bitcast(TC_UINT32, &u32, &out, &diag, 1) == 0 &&
              out.type->tag == TC_UINT32 && out.bits == u32.bits,
          "same-type bitcast is a bit-preserving identity");
    check(tc_exec_bitcast(TC_UINT64, &f32, &out, &diag, 1) != 0,
          "bitcast rejects unequal widths");
    check(diag.kind == TC_CE_BITCAST_WIDTH, "bitcast unequal width error kind");
    check(tc_exec_bitcast(TC_UINT8, &boolean, &out, &diag, 1) != 0,
          "bitcast rejects bool source");
    check(diag.kind == TC_CE_TYPE_MISMATCH, "bitcast bool error kind");
    tc_diagnostic_clear(&diag);
}

int main(void) {
    /* Bit tool functions */
    test_mask_bits();
    test_bits_to_signed();
    test_signed_to_bits();
    test_value_to_unsigned();
    test_value_make();
    test_uninitialized_slot_sentinel();

    /* Range checks */
    test_signed_in_range();
    test_unsigned_in_range();

    /* Literal */
    test_literal_fits_type();
    test_literal_fits_context();
    test_literal_to_value();

    /* Arithmetic */
    test_arith_signed_add_strict();
    test_arith_signed_sub_strict();
    test_arith_signed_mul_strict();
    test_arith_signed_div_mod();
    test_arith_signed_wrap();
    test_umul64();
    test_arith_unsigned();

    /* Unary */
    test_unary_abs();
    test_unary_neg();

    /* Compare */
    test_compare_signed();
    test_compare_unsigned();

    /* Logic */
    test_logic_binary();
    test_logic_unary();

    /* Cast strict */
    test_cast_strict_widen_signed_to_signed();
    test_cast_strict_widen_unsigned_to_unsigned();
    test_cast_strict_widen_signed_to_unsigned();
    test_cast_strict_widen_unsigned_to_signed();
    test_cast_strict_same_width_diff_sign();
    test_cast_strict_narrow();
    test_cast_strict_bool();

    /* Cast truncate */
    test_cast_truncate();
    test_cast_strict_matrix();
    test_cast_same_type_float_bit_identity();
    test_bitcast_semantics();

    /* Float arithmetic */
    test_fp_arith_add_strict();
    test_fp_arith_sub_strict();
    test_fp_arith_mul_strict();
    test_fp_arith_mul_underflow_flush_to_zero_strict();
    test_fp_arith_mul_underflow_subnormal_strict();
    test_fp_exact_subnormal_does_not_underflow();
    test_fp_arith_div_strict();
    test_fp_arith_div_zero_strict();
    test_fp_arith_nan_operand_strict();
    test_fp_strict_exception_priority();
    test_fp_ieee_canonical_nan();
    test_fp_round_ties_to_even();
    test_cast_round_ties_to_even();
    test_fp_arith_add_float32_strict();
    test_fp_arith_sub_float32_strict();
    test_fp_arith_mul_float32_strict();
    test_fp_arith_div_float32_strict();
    test_fp_unary_neg_strict();
    test_fp_unary_abs_mode_matrix();
    test_fp_compare_nan_ne();
    test_fp_compare_eq_nan();
    test_fp_nan_compare_matrix();
    test_fp_unary_preserves_payload_bits();
    test_fp_compare_rejects_wrap();
    test_fp_cast_int_to_float64();
    test_fp_cast_float64_to_int32();
    test_fp_cast_truncate_f32_to_i32();
    test_fp_cast_truncate_i32_to_f32();
    test_fp_cast_truncate_f64_to_i64();
    test_fp_cast_truncate_i64_to_f64();
    test_fp_cast_truncate_f64_to_f32_mask();
    test_fp_cast_truncate_f32_to_f64_mask();
    test_fp_cast_truncate_f64_to_f32_roundtrip();
    test_fp_cast_truncate_neg_zero_bits();
    test_fp_arith_wrap_add();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
