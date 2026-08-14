/*
 * test_shift.c — 移位运算语义单元测试
 *
 * 覆盖 tc_exec_shift：
 *   - shl strict / wrap，k >= n，val = 0
 *   - shr 算术/逻辑右移，k >= n
 *   - 负数移位计数触发 TC_RE_NEGATIVE_SHIFT_COUNT（有符号按补码解码）
 *   - 无符号计数恒非负，按 k >= n 分支处理
 *   - 标准 §6.2 / §10.21 边界示例
 */

#include "tc_semantics.h"
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

static TcValue make_int(TcTypeTag type, uint64_t bits) {
    return tc_value_make(type, bits);
}

static void test_shl_strict_overflow(void) {
    TcDiagnostic diag;
    TcValue val = make_int(TC_INT8, 0x7F);
    TcValue cnt = make_int(TC_INT8, 2);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_INTEGER_OVERFLOW,
          "shl(int8, 127, 2) strict overflow");

    tc_diagnostic_clear(&diag);
}

static void test_shl_wrap_examples(void) {
    TcDiagnostic diag;
    TcValue val = make_int(TC_INT8, 0x7F);
    TcValue cnt = make_int(TC_INT8, 2);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_WRAP, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && tc_bits_to_signed(TC_INT8, out.bits) == -4,
          "shl(int8, wrap, 127, 2) = 0xFC (-4)");

    val = make_int(TC_UINT8, 0xFF);
    cnt = make_int(TC_UINT8, 4);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_UINT8, TC_ARITH_WRAP, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 0xF0, "shl(uint8, wrap, 255, 4) = 240");

    val = make_int(TC_UINT8, 0xFF);
    cnt = make_int(TC_UINT8, 8);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_UINT8, TC_ARITH_WRAP, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "shl(uint8, wrap, 255, 8) k>=n -> 0");

    val = make_int(TC_INT8, 64);
    cnt = make_int(TC_INT8, 2);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_WRAP, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "shl(int8, wrap, 64, 2) 256 mod 256 = 0");

    tc_diagnostic_clear(&diag);
}

static void test_shl_zero_value(void) {
    TcDiagnostic diag;
    TcValue val = make_int(TC_INT8, 0);
    TcValue cnt = make_int(TC_INT8, 100);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "shl strict with val=0 always 0");

    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_WRAP, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "shl wrap with val=0 always 0");

    tc_diagnostic_clear(&diag);
}

static void test_shr_signed_unsigned(void) {
    TcDiagnostic diag;
    TcValue val = make_int(TC_INT8, 0x80);
    TcValue cnt = make_int(TC_INT8, 1);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHR, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && tc_bits_to_signed(TC_INT8, out.bits) == -64,
          "shr(int8, -128, 1) = -64");

    cnt = make_int(TC_INT8, 8);
    rc = tc_exec_shift(TC_SHIFT_SHR, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "shr(int8, -128, 8) k>=n -> 0");

    val = make_int(TC_UINT8, 0x80);
    cnt = make_int(TC_UINT8, 1);
    rc = tc_exec_shift(TC_SHIFT_SHR, TC_UINT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 64, "shr(uint8, 128, 1) = 64 logical");

    tc_diagnostic_clear(&diag);
}

static void test_shl_success_strict(void) {
    TcDiagnostic diag;
    TcValue val = make_int(TC_INT8, 16);
    TcValue cnt = make_int(TC_INT8, 2);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 64, "shl(int8, 16, 2) = 64");

    tc_diagnostic_clear(&diag);
}

static void test_negative_shift_count(void) {
    TcDiagnostic diag;
    TcValue val = make_int(TC_INT8, 1);
    TcValue cnt = make_int(TC_INT8, 0xFF); /* 有符号 -1 */
    TcValue out;
    int rc = 0;

    /* 有符号计数 -1：shl（strict/wrap）与 shr 都必须报 TC_RE_NEGATIVE_SHIFT_COUNT，
     * 负计数判定先于 k >= n（0xFF 按无符号解为 255 会误入 k>=n 分支）。 */
    tc_diagnostic_init(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_NEGATIVE_SHIFT_COUNT,
          "shl strict with count -1 -> negative shift count");

    tc_diagnostic_clear(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_WRAP, &val, &cnt, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_NEGATIVE_SHIFT_COUNT,
          "shl wrap with count -1 -> negative shift count");

    tc_diagnostic_clear(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHR, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_NEGATIVE_SHIFT_COUNT,
          "shr with count -1 -> negative shift count");

    tc_diagnostic_clear(&diag);

    /* 无符号计数恒非负：uint8 0xFF 按 255 解码，落入 k >= n 分支 */
    val = make_int(TC_UINT8, 1);
    cnt = make_int(TC_UINT8, 0xFF);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_UINT8, TC_ARITH_WRAP, &val, &cnt, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "shl(uint8, wrap, 1, 255) k>=n -> 0");

    tc_diagnostic_clear(&diag);
}

static void test_k_ge_n_strict_shl(void) {
    TcDiagnostic diag;
    TcValue val = make_int(TC_INT8, 1);
    TcValue cnt = make_int(TC_INT8, 8);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    rc = tc_exec_shift(TC_SHIFT_SHL, TC_INT8, TC_ARITH_STRICT, &val, &cnt, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_INTEGER_OVERFLOW,
          "shl strict k=8 >= n=8 overflow");

    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_shl_strict_overflow();
    test_shl_wrap_examples();
    test_shl_zero_value();
    test_shr_signed_unsigned();
    test_shl_success_strict();
    test_negative_shift_count();
    test_k_ge_n_strict_shl();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
