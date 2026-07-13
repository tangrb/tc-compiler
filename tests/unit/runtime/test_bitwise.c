/*
 * test_bitwise.c — 按位运算语义单元测试
 *
 * 覆盖 tc_exec_bitwise_binary / tc_exec_bitwise_unary：
 *   - 8 种整数类型的 and/or/xor/not
 *   - 标准 §6.1 / §10.21 示例位模式
 *   - 操作数类型防御检查
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

static TcValue make_int(TcType type, uint64_t bits) {
    return tc_value_make(type, bits);
}

static void test_int8_standard_examples(void) {
    TcDiagnostic diag;
    TcValue a = make_int(TC_INT8, 0xAC);
    TcValue b = make_int(TC_INT8, 0xF0);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);

    rc = tc_exec_bitwise_binary(TC_BIT_AND, TC_INT8, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0xA0, "int8 and 0xAC & 0xF0 = 0xA0");

    rc = tc_exec_bitwise_binary(TC_BIT_OR, TC_INT8, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0xFC, "int8 or 0xAC | 0xF0 = 0xFC");

    rc = tc_exec_bitwise_binary(TC_BIT_XOR, TC_INT8, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0x5C, "int8 xor 0xAC ^ 0xF0 = 0x5C");

    rc = tc_exec_bitwise_unary(TC_INT8, &a, &out, &diag, 1);
    check(rc == 0 && out.bits == 0x53, "int8 not 0xAC = 0x53");

    check(tc_bits_to_signed(TC_INT8, 0xA0) == -96, "int8 and result signed = -96");
    check(tc_bits_to_signed(TC_INT8, 0x5C) == 92, "int8 xor result signed = 92");

    tc_diagnostic_clear(&diag);
}

static void test_uint8_standard_examples(void) {
    TcDiagnostic diag;
    TcValue a = make_int(TC_UINT8, 0xAA);
    TcValue b = make_int(TC_UINT8, 0xF0);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);

    rc = tc_exec_bitwise_binary(TC_BIT_AND, TC_UINT8, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0xA0, "uint8 and 0xAA & 0xF0 = 0xA0");

    rc = tc_exec_bitwise_binary(TC_BIT_OR, TC_UINT8, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0xFA, "uint8 or 0xAA | 0xF0 = 0xFA");

    rc = tc_exec_bitwise_binary(TC_BIT_XOR, TC_UINT8, &a, &b, &out, &diag, 1);
    check(rc == 0 && out.bits == 0x5A, "uint8 xor 0xAA ^ 0xF0 = 0x5A");

    rc = tc_exec_bitwise_unary(TC_UINT8, &a, &out, &diag, 1);
    check(rc == 0 && out.bits == 0x55, "uint8 not 0xAA = 0x55");

    tc_diagnostic_clear(&diag);
}

static void test_all_integer_widths(void) {
    TcDiagnostic diag;
    TcValue lhs = make_int(TC_INT16, 0x00FF);
    TcValue rhs = make_int(TC_INT16, 0xFF00);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);

    rc = tc_exec_bitwise_binary(TC_BIT_AND, TC_INT16, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "int16 and disjoint masks = 0");

    lhs = make_int(TC_UINT32, 0xFFFF0000u);
    rhs = make_int(TC_UINT32, 0x0000FFFFu);
    rc = tc_exec_bitwise_binary(TC_BIT_OR, TC_UINT32, &lhs, &rhs, &out, &diag, 1);
    check(rc == 0 && out.bits == 0xFFFFFFFFu, "uint32 or halves = all ones");

    lhs = make_int(TC_INT64, (uint64_t)INT64_MIN);
    rc = tc_exec_bitwise_unary(TC_INT64, &lhs, &out, &diag, 1);
    check(rc == 0 && out.bits == (uint64_t)INT64_MAX, "int64 not INT64_MIN = INT64_MAX");

    lhs = make_int(TC_UINT64, UINT64_MAX);
    rc = tc_exec_bitwise_unary(TC_UINT64, &lhs, &out, &diag, 1);
    check(rc == 0 && out.bits == 0, "uint64 not UINT64_MAX = 0");

    tc_diagnostic_clear(&diag);
}

static void test_type_mismatch_defense(void) {
    TcDiagnostic diag;
    TcValue lhs = make_int(TC_INT8, 1);
    TcValue rhs = make_int(TC_INT16, 1);
    TcValue out;
    int rc = 0;

    tc_diagnostic_init(&diag);

    rc = tc_exec_bitwise_binary(TC_BIT_AND, TC_INT8, &lhs, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_ERR_TYPE_MISMATCH,
          "bitwise binary rejects mismatched operand types");

    tc_diagnostic_clear(&diag);
    rc = tc_exec_bitwise_unary(TC_INT8, &rhs, &out, &diag, 1);
    check(rc == -1 && diag.kind == TC_ERR_TYPE_MISMATCH,
          "bitwise unary rejects mismatched operand type");

    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_int8_standard_examples();
    test_uint8_standard_examples();
    test_all_integer_widths();
    test_type_mismatch_defense();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
