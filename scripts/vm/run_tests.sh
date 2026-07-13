#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EMPTY_STDIN=/dev/null
FAIL=0
PASSED=0
FAILED=0
FAILED_FILES=""
ASAN_MODE=0
LEAKS_MODE=0
UBSAN_MODE=0
VALGRIND_MODE=0
VERBOSE=0
FILTER=""

while [ $# -gt 0 ]; do
    case "$1" in
    --asan)
        ASAN_MODE=1
        shift
        ;;
    --leaks)
        LEAKS_MODE=1
        shift
        ;;
    --ubsan)
        UBSAN_MODE=1
        shift
        ;;
    --valgrind)
        VALGRIND_MODE=1
        shift
        ;;
    --verbose)
        VERBOSE=1
        shift
        ;;
    --filter)
        if [ -z "${2:-}" ]; then
            echo "error: --filter requires a pattern" >&2
            exit 1
        fi
        FILTER="$2"
        shift 2
        ;;
    *)
        echo "error: unknown option: $1" >&2
        echo "usage: run_tests.sh [--asan] [--ubsan] [--valgrind] [--leaks] [--verbose] [--filter PATTERN]" >&2
        exit 1
        ;;
    esac
done

if [ "$ASAN_MODE" -eq 1 ] || [ "${ASAN:-0}" = "1" ]; then
    ASAN_MODE=1
    TC_VM_BIN="$ROOT/build-asan/vm/bin/tc-vm"
    BUILD_HINT="make build-asan"
    echo "=== ASAN mode (build-asan) ==="
elif [ "$UBSAN_MODE" -eq 1 ]; then
    TC_VM_BIN="$ROOT/build-ubsan/vm/bin/tc-vm"
    BUILD_HINT="make build-ubsan"
    echo "=== UBSan mode (build-ubsan) ==="
else
    TC_VM_BIN="$ROOT/build/vm/bin/tc-vm"
    BUILD_HINT="make vm"
fi

if [ ! -x "$TC_VM_BIN" ]; then
    echo "error: binary not found at $TC_VM_BIN" >&2
    echo "  build first: ${BUILD_HINT}" >&2
    exit 1
fi

# Wrap with Valgrind / Leaks if requested
if [ "$VALGRIND_MODE" -eq 1 ]; then
    SUPP_FILE="$ROOT/scripts/valgrind-suppressions.supp"
    if [ ! -f "$SUPP_FILE" ]; then
        echo "error: suppression file not found: $SUPP_FILE" >&2
        exit 1
    fi
    echo "=== Valgrind Memcheck mode ==="
    BIN="valgrind --tool=memcheck --suppressions=$SUPP_FILE --error-exitcode=1 --leak-check=full $TC_VM_BIN"
elif [ "$LEAKS_MODE" -eq 1 ]; then
    if ! command -v leaks >/dev/null 2>&1; then
        echo "error: 'leaks' not found — install Xcode or run 'xcode-select --install'" >&2
        exit 1
    fi
    echo "=== macOS leaks mode (MallocStackLogging + leaks --atExit) ==="
    BIN="env MallocStackLogging=1 leaks --atExit -- $TC_VM_BIN"
else
    BIN="$TC_VM_BIN"
fi

should_run() {
    target="$1"
    if [ -z "$FILTER" ]; then
        return 0
    fi
    case "$target" in
    *"$FILTER"*) return 0 ;;
    *) return 1 ;;
    esac
}

pass() {
    PASSED=$((PASSED + 1))
}

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
    if [ "$VERBOSE" -eq 1 ] || [ -z "$FILTER" ]; then
        echo "$label"
    fi
}

relpath() {
    echo "$1" | sed "s|^$ROOT/||"
}

verify_diag_format() {
    file="$1"
    output="$2"

    if ! printf '%s' "$output" | grep -Eq "^[^ ]+:[0-9]+: error:"; then
        fail "diagnostic format missing 'file:line: error:' in: $file" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            echo "  output: $(printf '%s' "$output" | head -3)" >&2
        fi
        return 1
    fi
    return 0
}

report_stdout_mismatch() {
    file="$1"
    exp="$2"
    got="$3"

    fail "stdout mismatch: $file" "$file"
    if command -v diff >/dev/null 2>&1; then
        echo "  --- expected" >&2
        echo "  +++ got" >&2
        diff -u "$exp" "$got" >&2 || true
    fi
    if [ "$VERBOSE" -eq 1 ]; then
        echo "  expected (hex): $(od -An -tx1 < "$exp")" >&2
        echo "  got (hex):      $(od -An -tx1 < "$got")" >&2
    fi
}

run_expect_stdout() {
    file="$1"
    expected="$2"
    exp="$(mktemp)"
    got="$(mktemp)"

    should_run "$file" || return 0
    log_test "OUT $file"

    printf '%s' "$expected" > "$exp"
    if ! $BIN "$file" </dev/null >"$got" 2>/dev/null; then
        fail "expected success: $file" "$file"
        rm -f "$exp" "$got"
        return
    fi
    if ! cmp -s "$exp" "$got"; then
        report_stdout_mismatch "$file" "$exp" "$got"
        rm -f "$exp" "$got"
        return
    fi
    pass
    rm -f "$exp" "$got"
}

run_with_stdin() {
    file="$1"
    stdin="$2"
    expected="$3"
    exp="$(mktemp)"
    got="$(mktemp)"

    should_run "$file" || return 0
    log_test "IN  $file"

    printf '%s' "$expected" > "$exp"
    if ! printf '%s' "$stdin" | $BIN "$file" >"$got" 2>/dev/null; then
        fail "expected success: $file" "$file"
        rm -f "$exp" "$got"
        return
    fi
    if ! cmp -s "$exp" "$got"; then
        report_stdout_mismatch "$file" "$exp" "$got"
        rm -f "$exp" "$got"
        return
    fi
    pass
    rm -f "$exp" "$got"
}

run_expect_ok_warn() {
    file="$1"
    pattern="$2"

    should_run "$file" || return 0
    log_test "WRN $file"

    output="$($BIN "$file" </dev/null 2>&1)" || {
        fail "expected success with warning: $file" "$file"
        return
    }
    if ! printf '%s' "$output" | grep -Fq "$pattern"; then
        fail "expected warning containing '$pattern' in: $file" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}

run_expect_fail_msg() {
    file="$1"
    pattern="$2"

    should_run "$file" || return 0
    log_test "ERR $file ($pattern)"

    output="$($BIN "$file" </dev/null 2>&1)" && {
        fail "expected failure: $file" "$file"
        return
    }
    verify_diag_format "$file" "$output" || return
    if ! printf '%s' "$output" | grep -Fq "$pattern"; then
        fail "expected error containing '$pattern' in: $file" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}

run_expect_fail_stdin_msg() {
    file="$1"
    stdin="$2"
    pattern="$3"

    should_run "$file" || return 0
    log_test "ERR $file ($pattern, stdin)"

    output="$(printf '%s' "$stdin" | $BIN "$file" 2>&1)" && {
        fail "expected failure: $file" "$file"
        return
    }
    verify_diag_format "$file" "$output" || return
    if ! printf '%s' "$output" | grep -Fq "$pattern"; then
        fail "expected error containing '$pattern' in: $file" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}

run_expect_ok_no_warn() {
    file="$1"

    should_run "$file" || return 0
    log_test "NOW $file"

    output="$($BIN "$file" </dev/null 2>&1)" || {
        fail "expected success without warning: $file" "$file"
        return
    }
    if printf '%s' "$output" | grep -Fq "warning:"; then
        fail "unexpected warning in: $file" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}

run_expect_check_no_warn() {
    file="$1"

    should_run "$file" || return 0
    log_test "CNW $file"

    output="$($BIN --check "$file" </dev/null 2>&1)" || {
        fail "expected --check success without warning: $file" "$file"
        return
    }
    if printf '%s' "$output" | grep -Fq "warning:"; then
        fail "unexpected warning in: $file" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}

run_expect_ok() {
    file="$1"

    should_run "$file" || return 0
    log_test "OK  $file"

    if ! $BIN "$file" </dev/null >/dev/null; then
        fail "expected success: $file" "$file"
        return
    fi
    pass
}

run_expect_check_ok() {
    file="$1"

    should_run "$file" || return 0
    log_test "CHK $file"

    if ! $BIN --check "$file" </dev/null >/dev/null 2>&1; then
        fail "expected --check success: $file" "$file"
        return
    fi
    pass
}

run_expect_check_fail() {
    file="$1"
    pattern="$2"

    should_run "$file" || return 0
    log_test "CFL $file ($pattern)"

    output="$($BIN --check "$file" </dev/null 2>&1)" && {
        fail "expected --check failure: $file" "$file"
        return
    }
    verify_diag_format "$file" "$output" || return
    if ! printf '%s' "$output" | grep -Fq "$pattern"; then
        fail "expected --check error containing '$pattern' in: $file" "$file"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}

run_repl_expect() {
    input="$1"
    expected="$2"
    label="$3"

    should_run "$label" || return 0
    log_test "REPL $label"

    output="$(printf '%s\n' "$input" | $BIN --repl 2>&1)"
    if ! printf '%s' "$output" | grep -Fq "$expected"; then
        fail "REPL test failed: $label" "$label"
        echo "  expected substring: $expected" >&2
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}

# --- valid: execution succeeds ---

run_expect_ok "$ROOT/tests/valid/example.tc"
run_expect_ok "$ROOT/tests/valid/signed_wrap.tc"
run_expect_ok "$ROOT/tests/valid/uint8_wrap.tc"
run_expect_ok "$ROOT/tests/valid/var_no_init.tc"
run_expect_ok "$ROOT/tests/valid/no_warn_after_assign.tc"
run_expect_check_ok "$ROOT/tests/valid/example.tc"
run_expect_check_ok "$ROOT/tests/valid/let_constant.tc"

run_expect_stdout "$ROOT/tests/valid/wrap_int8_output.tc" "-128
"
run_expect_stdout "$ROOT/tests/valid/wrap_uint8_output.tc" "4
"
run_expect_stdout "$ROOT/tests/valid/write_int8_number.tc" "65"
run_expect_stdout "$ROOT/tests/valid/truncate_cast.tc" "-24
"
run_expect_stdout "$ROOT/tests/valid/div_mod_signed.tc" "-2
-1
"
run_expect_stdout "$ROOT/tests/valid/int64_min.tc" "-9223372036854775808
"
run_expect_stdout "$ROOT/tests/valid/mod_int_min_neg_one.tc" "0
0
0
"
run_expect_stdout "$ROOT/tests/valid/hex_literal.tc" "255
"
run_expect_stdout "$ROOT/tests/valid/bin_literal.tc" "163
"
run_expect_stdout "$ROOT/tests/valid/oct_literal.tc" "493
"
run_expect_stdout "$ROOT/tests/valid/literal_separator.tc" "1000000
"
run_expect_stdout "$ROOT/tests/valid/strict_cast_widen.tc" "200
"
run_expect_stdout "$ROOT/tests/valid/let_constant.tc" "42
"
run_expect_stdout "$ROOT/tests/valid/uint8_wrap.tc" "4
"
run_expect_stdout "$ROOT/tests/valid/wrap_sub_mul.tc" "98
-56
"
run_expect_stdout "$ROOT/tests/valid/comments_semicolon.tc" "1
"
run_expect_stdout "$ROOT/tests/valid/write_no_newline.tc" "42"
run_expect_stdout "$ROOT/tests/valid/sign_extend_cast.tc" "-100
"
run_expect_stdout "$ROOT/tests/valid/semicolon_inline_comment.tc" "10
"
run_expect_fail_msg "$ROOT/tests/errors/runtime/int64_min_div.tc" "signed division overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/int32_min_div.tc" "signed division overflow"
run_expect_stdout "$ROOT/tests/valid/abs_neg_signed.tc" "42
42
"
run_expect_stdout "$ROOT/tests/valid/unary_wrap.tc" "-32768
"
run_expect_stdout "$ROOT/tests/valid/format_output.tc" "42
-128
"
run_expect_stdout "$ROOT/tests/valid/format_hex_bin.tc" "ff
11111111
"

run_expect_ok_warn "$ROOT/tests/valid/uninitialized.tc" "use of possibly uninitialized variable 'a'"
run_expect_ok_no_warn "$ROOT/tests/valid/assign_uninit_var_valid.tc" "uninitialized"
run_expect_stdout "$ROOT/tests/valid/uninit_slot_value.tc" "-16843010
"
run_expect_ok_no_warn "$ROOT/tests/valid/no_warn_after_assign.tc"
run_expect_check_no_warn "$ROOT/tests/valid/no_warn_after_read.tc"
run_with_stdin "$ROOT/tests/valid/read_write.tc" "42
" "42
"

# --- v0.0.21: bool / compare / logic / const expr ---

run_expect_stdout "$ROOT/tests/valid/bool_var.tc" "true
false
"
run_expect_stdout "$ROOT/tests/valid/compare_ops.tc" "false
true
true
true
false
false
"
run_expect_stdout "$ROOT/tests/valid/logic_ops.tc" "false
true
true
false
false
true
"
run_expect_stdout "$ROOT/tests/valid/bitwise_runtime.tc" "10100000
01011010
64
-64
0
64
"
run_expect_stdout "$ROOT/tests/valid/bitwise_and_or_xor_not_valid.tc" "3
15
12
-16
0
255
255
15
15
255
240
-256
0
65535
65535
255
15
255
240
-256
0
4278190335
4278190335
16777215
15
255
240
-256
0
18446744073709551615
18446744073709551615
255
"
run_expect_stdout "$ROOT/tests/valid/bitwise_shift_shl_shr_valid.tc" "64
-64
64
"
run_expect_stdout "$ROOT/tests/valid/bitwise_shl_wrap_valid.tc" "0
0
"
run_expect_stdout "$ROOT/tests/valid/bitwise_shift_k_ge_n_valid.tc" "0
0
0
"
run_expect_stdout "$ROOT/tests/valid/bitwise_let_const_valid.tc" "255
15
15
240
"
run_expect_stdout "$ROOT/tests/valid/bitwise_io_format_valid.tc" "10100000
5a
"
run_expect_stdout "$ROOT/tests/valid/bool_cast.tc" "true
false
1
0
"
run_expect_stdout "$ROOT/tests/valid/format_bool.tc" "true
false
"
run_expect_stdout "$ROOT/tests/valid/const_expr.tc" "1003
true
1003
"
run_with_stdin "$ROOT/tests/valid/read_bool.tc" "true
" "true
"
run_with_stdin "$ROOT/tests/valid/read_int8.tc" "42
" "42
"
run_with_stdin "$ROOT/tests/valid/read_int8.tc" "-128
" "-128
"
run_expect_stdout "$ROOT/tests/valid/let_bool_constant.tc" "true
false
"
run_expect_stdout "$ROOT/tests/valid/let_logic_short_circuit.tc" "false
true
"
run_expect_stdout "$ROOT/tests/valid/format_spec_i.tc" "42
-128
"

# --- v0.0.24: if-then-else control flow ---

run_expect_ok "$ROOT/tests/valid/if_basic.tc"
run_expect_stdout "$ROOT/tests/valid/if_basic.tc" "1
10
20
"
run_expect_ok "$ROOT/tests/valid/if_else.tc"
run_expect_stdout "$ROOT/tests/valid/if_else.tc" "2
4
5
7
10
"
run_expect_ok "$ROOT/tests/valid/if_nested.tc"
run_expect_stdout "$ROOT/tests/valid/if_nested.tc" "5
1
42
"
run_expect_ok "$ROOT/tests/valid/if_chain.tc"
run_expect_stdout "$ROOT/tests/valid/if_chain.tc" "2
"
run_expect_ok "$ROOT/tests/valid/if_bool_literal.tc"
run_expect_stdout "$ROOT/tests/valid/if_bool_literal.tc" "1
3
4
5
"
run_expect_ok "$ROOT/tests/valid/if_local_same_name.tc"
run_expect_stdout "$ROOT/tests/valid/if_local_same_name.tc" "1
4
"
run_expect_ok "$ROOT/tests/valid/if_shadow_global.tc"
run_expect_stdout "$ROOT/tests/valid/if_shadow_global.tc" "1
100
"
run_expect_stdout "$ROOT/tests/valid/if_false_skip_nested_then.tc" "100
"
run_expect_check_ok "$ROOT/tests/valid/if_basic.tc"
run_expect_check_ok "$ROOT/tests/valid/if_nested.tc"

# --- v0.0.25: float32/float64 ---

run_expect_check_ok "$ROOT/tests/valid/fp_basic.tc"

# --- stress test ---

run_expect_stdout "$ROOT/tests/stress/massive_vars.tc" "55
"
run_expect_stdout "$ROOT/tests/stress/many_operations.tc" "55
45
12345678910
-4672212345678902107221234567890
3
"
run_expect_stdout "$ROOT/tests/stress/deep_recursion.tc" "199
"
run_expect_stdout "$ROOT/tests/stress/let_chain.tc" "1125899906842624
"
# io_stress: 128 writeln calls outputting 0..127
IO_BASE="$(printf '0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59\n60\n61\n62\n63\n64\n65\n66\n67\n68\n69\n70\n71\n72\n73\n74\n75\n76\n77\n78\n79\n80\n81\n82\n83\n84\n85\n86\n87\n88\n89\n90\n91\n92\n93\n94\n95\n96\n97\n98\n99\n100\n101\n102\n103\n104\n105\n106\n107\n108\n109\n110\n111\n112\n113\n114\n115\n116\n117\n118\n119\n120\n121\n122\n123\n124\n125\n126\n127')"
# $(...) strips trailing newlines; restore it so last line matches writeln output
IO_EXP="${IO_BASE}
"
run_expect_stdout "$ROOT/tests/stress/io_stress.tc" "$IO_EXP"
run_expect_stdout "$ROOT/tests/stress/many_vars_stress.tc" "1275
12750
562949953421312
"
run_expect_stdout "$ROOT/tests/stress/stress_if_nested.tc" "10
"
run_expect_ok "$ROOT/tests/stress/type_combinatorial.tc"

# --- errors/runtime (expect failure + diagnostic) ---

run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_overflow.tc" "out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_mul.tc" "out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/neg_int_min.tc" "neg(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/abs_int_min.tc" "abs(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/div_zero.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/mod_zero.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow.tc" "out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/read_invalid.tc" "unexpected end of input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_invalid_input.tc" "abc
" "invalid input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_out_of_range.tc" "999
" "input value out of range"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_bool_invalid_input.tc" "trueish
" "invalid input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_bool_invalid_input.tc" "falsehood
" "invalid input"

# --- errors/runtime: extended tests (per-type coverage) ---

run_expect_fail_msg "$ROOT/tests/errors/runtime/div_zero_int16.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/div_zero_uint32.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/mod_zero_int64.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_sub_overflow.tc" "out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_overflow_int8.tc" "out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/bitwise_shl_overflow_runtime.tc" "shift left overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_overflow_int16.tc" "out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_overflow_int32.tc" "out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_overflow_int64.tc" "signed addition overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_mul_overflow_int16.tc" "out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_mul_overflow_int32.tc" "out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_mul_overflow_int64.tc" "signed multiplication overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/neg_int_min_int16.tc" "neg(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/neg_int_min_int32.tc" "neg(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/neg_int_min_int64.tc" "neg(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/abs_int_min_int16.tc" "abs(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/abs_int_min_int32.tc" "abs(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/abs_int_min_int64.tc" "abs(INT_MIN) overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_int8_to_uint8.tc" "cannot cast negative signed value to unsigned"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_int16_to_int8.tc" "value out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_int32_to_int16.tc" "value out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_uint16_to_int16.tc" "unsigned value out of signed target range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_uint32_to_int32.tc" "unsigned value out of signed target range"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_out_of_range_int64.tc" "99999999999999999999
" "input value out of range"

# --- errors/static (expect failure + diagnostic) ---

run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_type_error.tc" "literal type"
run_expect_fail_msg "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_expect_fail_msg "$ROOT/tests/errors/static/abs_wrap_error.tc" "abs does not support wrap"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_xor_bool_type_error.tc" "bitwise operation requires integer type"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_wrap_on_shr_keyword_error.tc" "wrap cannot be used with shift operations"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_wrap_on_and_keyword_error.tc" "wrap cannot be used with bitwise operations"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_shl_truncate_keyword_error.tc" "truncate cannot be used with shift operations"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_shift_type_mismatch.tc" "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_shl_const_overflow.tc" "constant overflow"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_let_wrap_forbidden.tc" "wrap cannot be used in constant expression"
run_expect_fail_msg "$ROOT/tests/errors/static/keyword_error.tc" "wrap cannot be used with cast"
run_expect_fail_msg "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_expect_fail_msg "$ROOT/tests/errors/static/const_expr.tc" "constant expression cannot reference var variable"
run_expect_fail_msg "$ROOT/tests/errors/static/bool_literal_type_error.tc" "literal type does not match variable type"
run_expect_fail_msg "$ROOT/tests/errors/static/compare_type_mismatch.tc" "literal type does not match context"
run_expect_fail_msg "$ROOT/tests/errors/static/logic_type_error.tc" "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/const_cyclic_dep.tc" "circular dependency in constant expression"
run_expect_fail_msg "$ROOT/tests/errors/static/const_overflow.tc" "constant overflow"
run_expect_fail_msg "$ROOT/tests/errors/static/const_div_zero.tc" "constant division by zero"
run_expect_fail_msg "$ROOT/tests/errors/static/truncate_in_arith.tc" "truncate cannot be used with arithmetic"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_bool_truncate_keyword_error.tc" "truncate is only allowed for integer to integer conversion"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_truncate_bool_source_error.tc" "truncate is only allowed for integer to integer conversion"
run_expect_fail_msg "$ROOT/tests/errors/static/negative_unsigned_literal.tc" "unsigned suffix"
run_expect_fail_msg "$ROOT/tests/errors/static/leading_zero.tc" "invalid integer literal"
run_expect_fail_msg "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_let_var.tc" "duplicate definition"
run_expect_fail_msg "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_literal.tc" "cast source must be a variable"
run_expect_fail_msg "$ROOT/tests/errors/static/forward_reference.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/self_reference.tc" "cannot reference itself"
run_expect_fail_msg "$ROOT/tests/errors/static/format_string_error.tc" "invalid format specifier"
run_expect_fail_msg "$ROOT/tests/errors/static/invalid_format_spec_x.tc" "invalid format specifier"
run_expect_fail_msg "$ROOT/tests/errors/static/format_type_mismatch.tc" "%u requires unsigned type"
run_expect_fail_msg "$ROOT/tests/errors/static/format_operand_count.tc" "operand count error"

# --- errors/static: extended tests (per-type coverage) ---

run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_var_let.tc" "duplicate definition"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_range_int16.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_range_int64.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_range_uint8.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_range_uint32.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/wrap_mode_error_mod.tc" "div/mod do not support wrap"
run_expect_fail_msg "$ROOT/tests/errors/static/assign_to_let.tc" "cannot assign to constant"
run_expect_fail_msg "$ROOT/tests/errors/static/type_mismatch_assign.tc" "operand type does not match"
run_expect_fail_msg "$ROOT/tests/errors/static/type_mismatch_arith_op.tc" "operand type does not match"
run_expect_fail_msg "$ROOT/tests/errors/static/type_mismatch_unary.tc" "operand type does not match"
run_expect_fail_msg "$ROOT/tests/errors/static/self_ref_let.tc" "circular dependency"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_wrap_keyword.tc" "wrap cannot be used with cast"
run_expect_fail_msg "$ROOT/tests/errors/static/format_type_mismatch_uint.tc" "%d requires signed type"
run_expect_fail_msg "$ROOT/tests/errors/static/format_type_mismatch_signed.tc" "%u requires unsigned type"
run_expect_fail_msg "$ROOT/tests/errors/static/format_missing_operand.tc" "unexpected token"
run_expect_fail_msg "$ROOT/tests/errors/static/let_const_literal_range.tc" "invalid literal in constant expression"
run_expect_fail_msg "$ROOT/tests/errors/static/let_non_literal.tc" "constant expression cannot reference var variable"
run_expect_fail_msg "$ROOT/tests/errors/static/missing_type_in_arith.tc" "expected type"
run_expect_fail_msg "$ROOT/tests/errors/static/invalid_hex_overflow.tc" "integer literal too large"
run_expect_fail_msg "$ROOT/tests/errors/static/format_int_with_t.tc" "%t requires bool type"
run_expect_fail_msg "$ROOT/tests/errors/static/format_fp_type_mismatch.tc" "float type requires float format specifier"

# --check 模式下也应当捕获所有静态错误
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_expect_check_fail "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_expect_check_fail "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_expect_check_fail "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_expect_check_fail "$ROOT/tests/errors/static/const_expr.tc" "constant expression cannot reference var variable"
run_expect_check_fail "$ROOT/tests/errors/static/const_cyclic_dep.tc" "circular dependency in constant expression"
run_expect_check_fail "$ROOT/tests/errors/static/const_overflow.tc" "constant overflow"
run_expect_check_fail "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_var_let.tc" "duplicate definition"
run_expect_check_fail "$ROOT/tests/errors/static/literal_range_int16.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/literal_range_int64.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/literal_range_uint8.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/literal_range_uint32.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/wrap_mode_error_mod.tc" "div/mod do not support wrap"
run_expect_check_fail "$ROOT/tests/errors/static/assign_to_let.tc" "cannot assign to constant"
run_expect_check_fail "$ROOT/tests/errors/static/type_mismatch_assign.tc" "operand type does not match"
run_expect_check_fail "$ROOT/tests/errors/static/type_mismatch_arith_op.tc" "operand type does not match"
run_expect_check_fail "$ROOT/tests/errors/static/type_mismatch_unary.tc" "operand type does not match"
run_expect_check_fail "$ROOT/tests/errors/static/self_ref_let.tc" "circular dependency"
run_expect_check_fail "$ROOT/tests/errors/static/cast_wrap_keyword.tc" "wrap cannot be used with cast"
run_expect_check_fail "$ROOT/tests/errors/static/let_const_literal_range.tc" "invalid literal in constant expression"
run_expect_check_fail "$ROOT/tests/errors/static/let_non_literal.tc" "constant expression cannot reference var variable"
run_expect_check_fail "$ROOT/tests/errors/static/missing_type_in_arith.tc" "expected type"
run_expect_check_fail "$ROOT/tests/errors/static/format_int_with_t.tc" "%t requires bool type"
run_expect_check_fail "$ROOT/tests/errors/static/format_fp_type_mismatch.tc" "float type requires float format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/format_type_mismatch_uint.tc" "%d requires signed type"
run_expect_check_fail "$ROOT/tests/errors/static/format_type_mismatch_signed.tc" "%u requires unsigned type"
run_expect_check_fail "$ROOT/tests/errors/static/invalid_hex_overflow.tc" "integer literal too large"
run_expect_check_fail "$ROOT/tests/errors/static/literal_type_error.tc" "literal type does not match variable type"
run_expect_check_fail "$ROOT/tests/errors/static/bool_literal_type_error.tc" "literal type does not match variable type"
run_expect_check_fail "$ROOT/tests/errors/static/compare_type_mismatch.tc" "literal type does not match context"
run_expect_check_fail "$ROOT/tests/errors/static/logic_type_error.tc" "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/const_div_zero.tc" "constant division by zero"
run_expect_check_fail "$ROOT/tests/errors/static/truncate_in_arith.tc" "truncate cannot be used with arithmetic"
run_expect_check_fail "$ROOT/tests/errors/static/cast_bool_truncate_keyword_error.tc" "truncate is only allowed for integer to integer conversion"
run_expect_check_fail "$ROOT/tests/errors/static/cast_truncate_bool_source_error.tc" "truncate is only allowed for integer to integer conversion"
run_expect_check_fail "$ROOT/tests/errors/static/negative_unsigned_literal.tc" "unsigned suffix"
run_expect_check_fail "$ROOT/tests/errors/static/leading_zero.tc" "invalid integer literal"
run_expect_check_fail "$ROOT/tests/errors/static/cast_literal.tc" "cast source must be a variable"
run_expect_check_fail "$ROOT/tests/errors/static/forward_reference.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/self_reference.tc" "cannot reference itself"
run_expect_check_fail "$ROOT/tests/errors/static/format_string_error.tc" "invalid format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/invalid_format_spec_x.tc" "invalid format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/format_operand_count.tc" "operand count error"
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_let_var.tc" "duplicate definition"
run_expect_check_fail "$ROOT/tests/errors/static/keyword_error.tc" "wrap cannot be used with cast"
run_expect_check_fail "$ROOT/tests/errors/static/abs_wrap_error.tc" "abs does not support wrap"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_xor_bool_type_error.tc" "bitwise operation requires integer type"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_wrap_on_shr_keyword_error.tc" "wrap cannot be used with shift operations"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_wrap_on_and_keyword_error.tc" "wrap cannot be used with bitwise operations"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_shl_truncate_keyword_error.tc" "truncate cannot be used with shift operations"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_shift_type_mismatch.tc" "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_shl_const_overflow.tc" "constant overflow"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_let_wrap_forbidden.tc" "wrap cannot be used in constant expression"

# --- v0.0.25: float static errors ---

run_expect_fail_msg "$ROOT/tests/errors/static/fp_mod_type_error.tc" "mod not supported for float types"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_ieee_on_int.tc" "ieee mode is only allowed for float operations"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_wrap_on_compare.tc" "wrap mode is not allowed for float comparison"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_bitwise_type_error.tc" "bitwise operation requires integer type"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_literal_range.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/fp_mod_type_error.tc" "mod not supported for float types"
run_expect_check_fail "$ROOT/tests/errors/static/fp_ieee_on_int.tc" "ieee mode is only allowed for float operations"
run_expect_check_fail "$ROOT/tests/errors/static/fp_wrap_on_compare.tc" "wrap mode is not allowed for float comparison"
run_expect_check_fail "$ROOT/tests/errors/static/fp_bitwise_type_error.tc" "bitwise operation requires integer type"
run_expect_check_fail "$ROOT/tests/errors/static/fp_literal_range.tc" "literal out of range"

# --- v0.0.24: if / indent static errors ---

run_expect_fail_msg "$ROOT/tests/errors/static/if_cross_block_ref_after_end.tc" "cross-block reference"
run_expect_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_after_end.tc" "cross-block reference"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cross_block_ref_else_to_then.tc" "cross-block reference"
run_expect_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_else_to_then.tc" "cross-block reference"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cross_block_ref_then_to_else.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_then_to_else.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cond_type_arith.tc" "if condition must be bool"
run_expect_check_fail "$ROOT/tests/errors/static/if_cond_type_arith.tc" "if condition must be bool"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cond_type_literal.tc" "literal type does not match context"
run_expect_check_fail "$ROOT/tests/errors/static/if_cond_type_literal.tc" "literal type does not match context"
run_expect_fail_msg "$ROOT/tests/errors/static/if_missing_end_eof.tc" "missing end for if statement"
run_expect_check_fail "$ROOT/tests/errors/static/if_missing_end_eof.tc" "missing end for if statement"
run_expect_fail_msg "$ROOT/tests/errors/static/if_missing_end_stmt.tc" "missing end for if statement"
run_expect_check_fail "$ROOT/tests/errors/static/if_missing_end_stmt.tc" "missing end for if statement"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_insufficient_then.tc" "insufficient indentation in block"
run_expect_check_fail "$ROOT/tests/errors/static/indent_insufficient_then.tc" "insufficient indentation in block"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_insufficient_nested.tc" "insufficient indentation in block"
run_expect_check_fail "$ROOT/tests/errors/static/indent_insufficient_nested.tc" "insufficient indentation in block"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_insufficient_block.tc" "insufficient indentation in block"
run_expect_check_fail "$ROOT/tests/errors/static/indent_insufficient_block.tc" "insufficient indentation in block"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_mixed_tab_body.tc" "mixed spaces and tabs in indentation"
run_expect_check_fail "$ROOT/tests/errors/static/indent_mixed_tab_body.tc" "mixed spaces and tabs in indentation"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_mixed_space_if.tc" "mixed spaces and tabs in indentation"
run_expect_check_fail "$ROOT/tests/errors/static/indent_mixed_space_if.tc" "mixed spaces and tabs in indentation"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_else_mismatch.tc" "else indentation does not match if"
run_expect_check_fail "$ROOT/tests/errors/static/indent_else_mismatch.tc" "else indentation does not match if"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_else_position.tc" "else must appear at same indentation as if"
run_expect_check_fail "$ROOT/tests/errors/static/indent_else_position.tc" "else must appear at same indentation as if"
run_expect_fail_msg "$ROOT/tests/errors/static/indent_end_mismatch.tc" "end indentation does not match if"
run_expect_check_fail "$ROOT/tests/errors/static/indent_end_mismatch.tc" "end indentation does not match if"

# --- REPL ---

run_repl_expect "var a: int32 = 10
var b: int32 = 20
var sum: int32 = add(int32, a, b)
writeln(int32, sum)
:quit" "30" "cross-line variables"

run_repl_expect "var x: int32 = 1
var x: int32 = 2
:quit" "duplicate definition of 'x'" "duplicate definition error"

run_repl_expect "var a: int32 = 1
:reset
:vars
:quit" "(no variables)" "session reset"

run_repl_expect "let N: int32 = 99
writeln(int32, N)
:quit" "99" "let constant in REPL"

run_repl_expect "if true then
:quit" "if statements are not supported in REPL mode" "repl if unsupported"

run_repl_expect ":help
:quit" "Meta commands" "help command"

run_repl_expect "let X: int8 = 999
:vars
:quit" "(no variables)" "let const failure does not pollute session"

# --- valid: extended tests (all types, format specs, cast, wrap, complex) ---

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
700000000000
"
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
18446744073709551615
"
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
-2147483648
"
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
200
"
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
1111111111111111111111111111111111111111111111111111111111111111
"
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
0
"
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
10
"
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
-1
255
4000000000
-2045911175
-8327
121
"
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
18446744073709551615
"
run_expect_stdout "$ROOT/tests/valid/complex_expressions.tc" "62
6000
6000500
621f4115
-2
150
4
3
"
run_expect_stdout "$ROOT/tests/valid/bin_hex_oct_io.tc" "11111111
1777
FFFF
318
31
122761
f1
"
run_expect_stdout "$ROOT/tests/valid/unary_wrap_unsigned.tc" "214
65494
4294967254
18446744073709551574
"
run_expect_stdout "$ROOT/tests/valid/io_extended.tc" "422a2A5200101010255ffFF37711111111255
10203040
18446744073709551615ffffffffffffffff
"

run_expect_ok_warn "$ROOT/tests/valid/uninit_chain_warning.tc" "use of possibly uninitialized variable 'a'"
run_expect_ok_warn "$ROOT/tests/valid/more_warning_cases.tc" "use of possibly uninitialized variable 'a'"

echo ""
echo "$PASSED passed, $FAILED failed"

if [ "$FAIL" -ne 0 ]; then
    echo "Failed tests:" >&2
    printf '%s\n' "$FAILED_FILES" | sed '/^$/d' >&2
    exit 1
fi

echo "all vm tests passed"
