#!/bin/sh
# TC-Compiler 全面测试套件
# 覆盖原有一致性测试 + 新增的边界/算术/cast/I/O/格式/字面量/错误测试

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/vm/bin/tc-vm"
FAIL=0
PASSED=0
FAILED=0
FAILED_FILES=""
VERBOSE=0

while [ $# -gt 0 ]; do
    case "$1" in
    --verbose|-v)
        VERBOSE=1
        shift
        ;;
    *)
        echo "usage: run_comprehensive_tests.sh [--verbose]" >&2
        exit 1
        ;;
    esac
done

if [ ! -x "$BIN" ]; then
    echo "error: binary not found at $BIN (run: make vm)" >&2
    exit 1
fi

pass() { PASSED=$((PASSED + 1)); }

fail() {
    msg="$1"
    file="${2:-}"
    FAIL=1
    FAILED=$((FAILED + 1))
    if [ -n "$file" ]; then
        FAILED_FILES="${FAILED_FILES}
${file}"
    fi
    echo "$msg" >&2
}

log_test() {
    label="$1"
    [ "$VERBOSE" -eq 1 ] && echo "$label"
}

relpath() { echo "$1" | sed "s|^$ROOT/||"; }

run_expect_ok() {
    file="$1"
    log_test "OK $(relpath "$file")"
    if ! "$BIN" "$file" </dev/null >/dev/null 2>/dev/null; then
        fail "expected success: $file" "$file"
        return
    fi
    pass
}

run_expect_stdout() {
    file="$1"
    expected="$2"
    log_test "OUT $(relpath "$file")"
    got="$("$BIN" "$file" </dev/null 2>/dev/null)" || {
        fail "expected success: $file" "$file"
        return
    }
    if [ "$got" != "$expected" ]; then
        fail "stdout mismatch: $(relpath "$file")" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            echo "  expected: [$expected]"
            echo "  got:      [$got]"
        fi
        return
    fi
    pass
}

run_expect_fail() {
    file="$1"
    expected_msg="$2"
    log_test "ERR $(relpath "$file") ($expected_msg)"
    output="$("$BIN" "$file" </dev/null 2>&1)" && {
        fail "expected failure: $file" "$file"
        return
    }
    if [ -n "$expected_msg" ]; then
        if ! printf '%s' "$output" | grep -Fq "$expected_msg"; then
            fail "expected error '$expected_msg' in $(relpath "$file")" "$file"
            [ "$VERBOSE" -eq 1 ] && echo "  got output: $output"
            return
        fi
    fi
    pass
}

run_expect_fail_stdin() {
    file="$1"
    stdin="$2"
    expected_msg="$3"
    log_test "ERR $(relpath "$file") ($expected_msg, stdin)"
    output="$(printf '%s' "$stdin" | "$BIN" "$file" 2>&1)" && {
        fail "expected failure: $file" "$file"
        return
    }
    if ! printf '%s' "$output" | grep -Fq "$expected_msg"; then
        fail "expected error '$expected_msg' in $(relpath "$file")" "$file"
        [ "$VERBOSE" -eq 1 ] && echo "  got output: $output"
        return
    fi
    pass
}

run_expect_ok_warn() {
    file="$1"
    pattern="$2"
    log_test "WRN $(relpath "$file")"
    output="$("$BIN" "$file" </dev/null 2>&1)" || {
        fail "expected success with warning: $file" "$file"
        return
    }
    if ! printf '%s' "$output" | grep -Fq "$pattern"; then
        fail "expected warning '$pattern' in $(relpath "$file")" "$file"
        return
    fi
    pass
}

run_repl_test() {
    input="$1"
    expected="$2"
    label="$3"
    log_test "REPL $label"
    output="$(printf '%s\n' "$input" | "$BIN" --repl 2>&1)"
    if ! printf '%s' "$output" | grep -Fq "$expected"; then
        fail "REPL test failed: $label" "$label"
        echo "  expected substring: $expected" >&2
        [ "$VERBOSE" -eq 1 ] && printf '%s\n' "$output" >&2
        return
    fi
    pass
}

# Helper: compare multi-line expected output using temp files
expect_stdout_file() {
    file="$1"
    expected_file="$2"
    log_test "OUT $(relpath "$file")"
    got="$("$BIN" "$file" </dev/null 2>/dev/null)" || {
        fail "expected success: $file" "$file"
        return 1
    }
    expected="$(cat "$expected_file")"
    if [ "$got" != "$expected" ]; then
        fail "stdout mismatch: $(relpath "$file")" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            echo "  expected file: $expected_file"
            echo "  expected: [$expected]"
            echo "  got:      [$got]"
        fi
        return 1
    fi
    pass
    return 0
}

# =============================================================
# 1. 类型系统边界测试
# =============================================================
echo "=== 1. 类型系统边界测试 ==="
run_expect_stdout "$ROOT/tests/valid/all_type_boundaries.tc" "-128
127
0
255
-32768
32767
0
65535
-2147483648
2147483647
0
4294967295
-9223372036854775808
9223372036854775807
0
18446744073709551615"

# =============================================================
# 2. 算术运算全面测试
# =============================================================
echo "=== 2. 算术运算全面测试 ==="
run_expect_stdout "$ROOT/tests/valid/arithmetic_all_types.tc" "15
-5
50
0
5
30
10
200
2
0
300
100
3000
300000
-100000
10000000
7000000
1000000
1100000000
500000000000
1500000000
700000000000"

run_expect_stdout "$ROOT/tests/valid/wrap_arithmetic_all.tc" "-128
0
2
-32768
0
-2147483648
2147483647
0
-9223372036854775808
0
18446744073709551615
18446744073709551615
0"

run_expect_stdout "$ROOT/tests/valid/unary_all_types.tc" "42
42
1000
1000
50000
50000
300000000
300000000
-128
-32768
-2147483648
-9223372036854775808
200"

# =============================================================
# 3. Cast 操作全面测试
# =============================================================
echo "=== 3. Cast 操作全面测试 ==="
run_expect_stdout "$ROOT/tests/valid/cast_operations_all.tc" "-100
-100
-100
200
-24
232
1000
80
70000
4464
3392
255
255
4000000000
-2045911175
-8327
121"

# =============================================================
# 4. 格式说明符全面测试
# =============================================================
echo "=== 4. 格式说明符全面测试 ==="
run_expect_stdout "$ROOT/tests/valid/format_spec_all.tc" "65
-128
41
41
101
01000001
255
ff
FF
377
11111111
-32768
8000
8000
65535
ffff
FFFF
177777
1111111111111111
-2147483648
80000000
80000000
20000000000
10000000000000000000000000000000
4294967295
ffffffff
FFFFFFFF
37777777777
11111111111111111111111111111111
-9223372036854775808
8000000000000000
8000000000000000
18446744073709551615
ffffffffffffffff
FFFFFFFFFFFFFFFF
1777777777777777777777
1111111111111111111111111111111111111111111111111111111111111111"

# =============================================================
# 5. 字面量边界测试
# =============================================================
echo "=== 5. 字面量边界测试 ==="
run_expect_stdout "$ROOT/tests/valid/literal_edge_cases.tc" "18446744073709551615
4000000000
200
18446744073709551615
2147483647
42480
240
1073741823
65535
255
50
0
-128
-32768
-2147483648"

# =============================================================
# 6. Let 常量全面测试
# =============================================================
echo "=== 6. Let 常量全面测试 ==="
run_expect_stdout "$ROOT/tests/valid/let_constant_all_types.tc" "42
200
1000
50000
100000
3000000000
5000000000
10000000000
150000
50000
42
-128
127
255
-32768
32767
65535
-2147483648
2147483647
4294967295
-9223372036854775808
9223372036854775807
18446744073709551615"

# =============================================================
# 7. 复杂表达式测试
# =============================================================
echo "=== 7. 复杂表达式测试 ==="
run_expect_stdout "$ROOT/tests/valid/complex_expressions.tc" "62
6000
6000500
621f4115
-2
150
4
3"

# =============================================================
# 8. I/O 扩展测试
# =============================================================
echo "=== 8. I/O 扩展测试 ==="
run_expect_stdout "$ROOT/tests/valid/io_extended.tc" "422a2A5200101010255ffFF37711111111255
10203040
18446744073709551615ffffffffffffffff"

# =============================================================
# 9. Div/Mod 全面测试
# =============================================================
echo "=== 9. Div/Mod 全面测试 ==="
run_expect_stdout "$ROOT/tests/valid/div_mod_all_signed.tc" "3
1
-3
-1
-3
1
3
-1
14
2
333
1
3
2
384
8
1010
10"
run_expect_stdout "$ROOT/tests/valid/mod_int_min_neg_one.tc" "0
0
0"

# =============================================================
# 10. 多进制 + I/O 测试
# =============================================================
echo "=== 10. 多进制 + I/O 测试 ==="
run_expect_stdout "$ROOT/tests/valid/bin_hex_oct_io.tc" "11111111
1777
FFFF
318
31
122761
f1"

# =============================================================
# 11. 无符号一元 wrap
# =============================================================
echo "=== 11. 无符号一元 wrap ==="
run_expect_stdout "$ROOT/tests/valid/unary_wrap_unsigned.tc" "214
65494
4294967254
18446744073709551574"

# =============================================================
# 12. 运行时错误测试
# =============================================================
echo "=== 12. 运行时错误测试 ==="
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_overflow_int16.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_overflow_int32.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_overflow_int64.tc" "signed addition overflow"
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_sub_overflow.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_mul_overflow_int16.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_mul_overflow_int32.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/div_zero_int16.tc" "division by zero"
run_expect_fail "$ROOT/tests/errors/runtime/div_zero_uint32.tc" "division by zero"
run_expect_fail "$ROOT/tests/errors/runtime/mod_zero_int64.tc" "division by zero"
run_expect_fail "$ROOT/tests/errors/runtime/cast_strict_overflow_int8_to_uint8.tc" "cannot cast negative signed value"
run_expect_fail "$ROOT/tests/errors/runtime/cast_strict_overflow_int16_to_int8.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/cast_strict_overflow_uint16_to_int16.tc" "unsigned value out of signed target range"
run_expect_fail "$ROOT/tests/errors/runtime/cast_strict_overflow_int32_to_int16.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/cast_strict_overflow_uint32_to_int32.tc" "unsigned value out of signed target range"
run_expect_fail "$ROOT/tests/errors/runtime/neg_int_min_int16.tc" "neg(INT_MIN) overflow"
run_expect_fail "$ROOT/tests/errors/runtime/neg_int_min_int32.tc" "neg(INT_MIN) overflow"
run_expect_fail "$ROOT/tests/errors/runtime/abs_int_min_int16.tc" "abs(INT_MIN) overflow"
run_expect_fail "$ROOT/tests/errors/runtime/abs_int_min_int32.tc" "abs(INT_MIN) overflow"
run_expect_fail "$ROOT/tests/errors/runtime/abs_int_min_int64.tc" "abs(INT_MIN) overflow"
run_expect_fail "$ROOT/tests/errors/runtime/neg_int_min_int64.tc" "neg(INT_MIN) overflow"
run_expect_fail_stdin "$ROOT/tests/errors/runtime/read_out_of_range_int64.tc" "99999999999999999999" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_overflow_int8.tc" "out of range"
run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_mul_overflow_int64.tc" "signed multiplication overflow"
run_expect_fail "$ROOT/tests/errors/runtime/int64_min_div.tc" "signed division overflow"
run_expect_fail "$ROOT/tests/errors/runtime/int32_min_div.tc" "signed division overflow"

# =============================================================
# 13. 静态错误测试
# =============================================================
echo "=== 13. 静态错误测试 ==="
run_expect_fail "$ROOT/tests/errors/static/literal_range_uint8.tc" "literal out of range"
run_expect_fail "$ROOT/tests/errors/static/literal_range_int16.tc" "literal out of range"
run_expect_fail "$ROOT/tests/errors/static/literal_range_uint32.tc" "literal out of range"
run_expect_fail "$ROOT/tests/errors/static/literal_range_int64.tc" "literal out of range"
run_expect_fail "$ROOT/tests/errors/static/let_const_literal_range.tc" "literal out of range"
run_expect_fail "$ROOT/tests/errors/static/invalid_hex_overflow.tc" "integer literal too large"
run_expect_fail "$ROOT/tests/errors/static/missing_type_in_arith.tc" "expected type"
run_expect_fail "$ROOT/tests/errors/static/wrap_mode_error_mod.tc" "div/mod do not support wrap mode"
run_expect_fail "$ROOT/tests/errors/static/type_mismatch_arith_op.tc" "operand type does not match"
run_expect_fail "$ROOT/tests/errors/static/assign_to_let.tc" "cannot assign to constant"
run_expect_fail "$ROOT/tests/errors/static/cast_wrap_keyword.tc" "wrap cannot be used with cast"
run_expect_fail "$ROOT/tests/errors/static/let_non_literal.tc" "constant initializer must be a literal"
run_expect_fail "$ROOT/tests/errors/static/invalid_format_spec_x.tc" "invalid format specifier"
run_expect_fail "$ROOT/tests/errors/static/format_type_mismatch_uint.tc" "%d requires signed type"
run_expect_fail "$ROOT/tests/errors/static/format_type_mismatch_signed.tc" "%u requires unsigned type"
run_expect_fail "$ROOT/tests/errors/static/format_missing_operand.tc" "unexpected token"
run_expect_fail "$ROOT/tests/errors/static/duplicate_var_let.tc" "duplicate definition"
run_expect_fail "$ROOT/tests/errors/static/self_ref_let.tc" "expected rhs expression"
run_expect_fail "$ROOT/tests/errors/static/type_mismatch_assign.tc" "operand type does not match"
run_expect_fail "$ROOT/tests/errors/static/type_mismatch_unary.tc" "operand type does not match"

# =============================================================
# 14. 警告测试
# =============================================================
echo "=== 14. 警告测试 ==="
run_expect_ok_warn "$ROOT/tests/valid/more_warning_cases.tc" "use of possibly uninitialized variable 'a'"
run_expect_ok_warn "$ROOT/tests/valid/uninit_chain_warning.tc" "use of possibly uninitialized variable 'a'"

# =============================================================
# 15. 压力测试
# =============================================================
echo "=== 15. 压力测试 ==="
run_expect_stdout "$ROOT/tests/stress/many_operations.tc" "55
45
12345678910
-4672212345678902107221234567890
3"
run_expect_ok "$ROOT/tests/stress/massive_vars.tc"

# =============================================================
# 16. REPL 扩展测试
# =============================================================
echo "=== 16. REPL 扩展测试 ==="
run_repl_test "var a: int32 = 10
var b: int32 = 20
var c: int32 = add(int32, a, b)
var d: int32 = mul(int32, c, 2)
writeln(int32, d)
:quit" "60" "repl chain arithmetic"

run_repl_test "let N: int32 = 100
var x: int32 = add(int32, N, 50)
writeln(int32, x)
:quit" "150" "repl let constant arithmetic"

run_repl_test "var z: int32 = 99
:reset
var z2: int32 = 11
var z3: int32 = 22
var z4: int32 = add(int32, z2, z3)
writeln(int32, z4)
:quit" "33" "repl reset then compute"

run_repl_test "var a: uint32 = 0xFF
var b: uint32 = 0b1010
writeln(uint32, add(uint32, a, b))
writeln(uint32, a)
:quit" "269
255" "repl hex and bin literals"

run_repl_test "let N: int8 = 42
var using_n: int16 = cast(int16, N)
writeln(int16, using_n)
:quit" "42" "repl let cast"

run_repl_test "var u: int32
u = 42
writeln(int32, u)
:quit" "42" "repl assign after decl"

# =============================================================
# 17. 回归测试
# =============================================================
echo "=== 17. 回归测试（运行原有 VM 测试）==="
sh "$ROOT"/scripts/vm/run_tests.sh
if [ $? -ne 0 ]; then
    fail "regression: original VM tests failed" ""
fi

# =============================================================
# 18. 词法单元测试
# =============================================================
echo "=== 18. 词法单元测试 ==="
LEXER_RESULT=$("$ROOT"/build/tests/bin/test-lexer 2>&1)
echo "$LEXER_RESULT" | tail -1 | grep -q "0 failed"
if [ $? -ne 0 ]; then
    fail "original lexer tests failed"
fi
EXT_RESULT=$("$ROOT"/build/tests/bin/test-lexer-extended 2>&1)
echo "$EXT_RESULT" | tail -1 | grep -q "0 failed"
if [ $? -ne 0 ]; then
    fail "extended lexer tests failed"
fi

# =============================================================
# 汇总
# =============================================================
echo ""
echo "=============== 全面测试结果 ==============="
echo "$PASSED passed, $FAILED failed"

if [ "$FAIL" -ne 0 ]; then
    echo "Failed tests:" >&2
    printf '%s\n' "$FAILED_FILES" | sed '/^$/d' >&2
    exit 1
fi

echo "all comprehensive tests passed"
