/*
 * test_fp_mod.c — 浮点 mod（§6.3.7）单元测试
 *
 * 有限值走 significand 整数归约；strict 异常分类；ieee canonical quiet NaN。
 */
#include "tc_semantics.h"
#include "tc_diagnostic.h"

#include <math.h>
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

static TcValue fp64_from_double(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return tc_value_make(TC_FLOAT64, bits);
}

static TcValue fp32_from_double(double value) {
    float f = (float)value;
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return tc_value_make(TC_FLOAT32, (uint64_t)bits);
}

static int fp64_approx_equal(uint64_t bits, double expected) {
    double actual = 0.0;
    memcpy(&actual, &bits, sizeof(actual));
    return fabs(actual - expected) < 1e-12;
}

static int fp32_approx_equal(uint64_t bits, double expected) {
    uint32_t b32 = (uint32_t)bits;
    float actual = 0.0f;
    memcpy(&actual, &b32, sizeof(actual));
    return fabs((double)actual - expected) < 1e-6;
}

static void test_fp_mod_finite(void) {
    TcDiagnostic diag;
    TcValue lhs;
    TcValue rhs;
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    lhs = fp64_from_double(5.5);
    rhs = fp64_from_double(2.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp64_approx_equal(out.bits, 1.5), "5.5 mod 2.0 == 1.5");

    lhs = fp64_from_double(-5.5);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp64_approx_equal(out.bits, -1.5), "-5.5 mod 2.0 == -1.5");

    lhs = fp32_from_double(5.5);
    rhs = fp32_from_double(2.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT32, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && fp32_approx_equal(out.bits, 1.5), "float32 5.5 mod 2.0 == 1.5");

    lhs = fp64_from_double(-0.0);
    rhs = fp64_from_double(2.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && (out.bits == UINT64_C(0x8000000000000000)),
          "mod(-0.0, 2.0) preserves -0.0");

    lhs = fp64_from_double(3.0);
    rhs = fp64_from_double(INFINITY);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.bits == lhs.bits, "finite mod ±inf returns a bitwise");
    tc_diagnostic_clear(&diag);
}

static void test_fp_mod_strict_exceptions(void) {
    TcDiagnostic diag;
    TcValue lhs;
    TcValue rhs;
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);
    lhs = fp64_from_double(0.0);
    rhs = fp64_from_double(0.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_FLOAT_INVALID, "0 mod 0 → invalid");

    tc_diagnostic_clear(&diag);
    tc_diagnostic_init(&diag);
    lhs = fp64_from_double(5.0);
    rhs = fp64_from_double(0.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_DIVISION_BY_ZERO, "5.0 mod 0.0 → div0");

    tc_diagnostic_clear(&diag);
    tc_diagnostic_init(&diag);
    lhs = fp64_from_double(INFINITY);
    rhs = fp64_from_double(2.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_FLOAT_INVALID, "inf mod finite → invalid");
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    lhs = tc_value_make(TC_FLOAT64, UINT64_C(0x7FF8000000000000));
    rhs = fp64_from_double(1.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_FLOAT_INVALID, "nan mod 1 → invalid");
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    lhs = fp64_from_double(INFINITY);
    rhs = fp64_from_double(0.0);
    rc = tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_STRICT, &lhs, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_RE_FLOAT_INVALID, "inf mod 0 → invalid not div0");
    tc_diagnostic_clear(&diag);
}

static void test_fp_mod_ieee_nan(void) {
    TcDiagnostic diag;
    TcValue lhs;
    TcValue rhs;
    TcValue out;

    tc_diagnostic_init(&diag);
    lhs = fp64_from_double(0.0);
    rhs = fp64_from_double(0.0);
    check(tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_IEEE, &lhs, &rhs, &out, &diag, 1) == 0 &&
              out.bits == UINT64_C(0x7FF8000000000000),
          "ieee 0 mod 0 → canonical qNaN f64");

    lhs = fp32_from_double(0.0);
    rhs = fp32_from_double(0.0);
    check(tc_exec_fp_arith(TC_MOD, TC_FLOAT32, TC_FLOAT_IEEE, &lhs, &rhs, &out, &diag, 1) == 0 &&
              out.bits == UINT64_C(0x7FC00000),
          "ieee 0 mod 0 → canonical qNaN f32");

    lhs = fp64_from_double(INFINITY);
    rhs = fp64_from_double(0.0);
    check(tc_exec_fp_arith(TC_MOD, TC_FLOAT64, TC_FLOAT_IEEE, &lhs, &rhs, &out, &diag, 1) == 0 &&
              out.bits == UINT64_C(0x7FF8000000000000),
          "ieee inf mod 0 → canonical qNaN f64");
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_fp_mod_finite();
    test_fp_mod_strict_exceptions();
    test_fp_mod_ieee_nan();
    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
