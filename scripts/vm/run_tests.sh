#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EMPTY_STDIN=/dev/null
FAIL=0
PASSED=0
FAILED=0
FAILED_FILES=""
ASAN_MODE=0
VERBOSE=0
FILTER=""

while [ $# -gt 0 ]; do
    case "$1" in
    --asan)
        ASAN_MODE=1
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
        echo "usage: run_tests.sh [--asan] [--verbose] [--filter PATTERN]" >&2
        exit 1
        ;;
    esac
done

if [ "$ASAN_MODE" -eq 1 ] || [ "${ASAN:-0}" = "1" ]; then
    ASAN_MODE=1
    BIN="$ROOT/build-asan/vm/bin/tc-vm"
    echo "=== ASAN mode (build-asan) ==="
else
    BIN="$ROOT/build/vm/bin/tc-vm"
fi

if [ ! -x "$BIN" ]; then
    echo "error: binary not found at $BIN" >&2
    echo "  build first: make vm (or make build-asan for ASAN)" >&2
    exit 1
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
    if ! "$BIN" "$file" </dev/null >"$got" 2>/dev/null; then
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
    if ! printf '%s' "$stdin" | "$BIN" "$file" >"$got" 2>/dev/null; then
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

    output="$("$BIN" "$file" </dev/null 2>&1)" || {
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

    output="$("$BIN" "$file" </dev/null 2>&1)" && {
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

    output="$(printf '%s' "$stdin" | "$BIN" "$file" 2>&1)" && {
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

    output="$("$BIN" "$file" </dev/null 2>&1)" || {
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

    output="$("$BIN" --check "$file" </dev/null 2>&1)" || {
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

    if ! "$BIN" "$file" </dev/null >/dev/null; then
        fail "expected success: $file" "$file"
        return
    fi
    pass
}

run_expect_check_ok() {
    file="$1"

    should_run "$file" || return 0
    log_test "CHK $file"

    if ! "$BIN" --check "$file" </dev/null >/dev/null 2>&1; then
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

    output="$("$BIN" --check "$file" </dev/null 2>&1)" && {
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

    output="$(printf '%s\n' "$input" | "$BIN" --repl 2>&1)"
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
run_expect_stdout "$ROOT/tests/valid/int64_min_div.tc" "-9223372036854775808
"

run_expect_ok_warn "$ROOT/tests/valid/uninitialized.tc" "use of possibly uninitialized variable 'a'"
run_expect_ok_no_warn "$ROOT/tests/valid/no_warn_after_assign.tc"
run_expect_check_no_warn "$ROOT/tests/valid/no_warn_after_read.tc"
run_with_stdin "$ROOT/tests/valid/read_write.tc" "42
" "42
"

# --- stress test ---

run_expect_stdout "$ROOT/tests/stress/massive_vars.tc" "55
"

# --- errors/runtime (expect failure + diagnostic) ---

run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_overflow.tc" "out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_mul.tc" "out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/div_zero.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/mod_zero.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow.tc" "out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/read_invalid.tc" "unexpected end of input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_invalid_input.tc" "abc
" "invalid input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_out_of_range.tc" "999
" "input value out of range"

# --- errors/static (expect failure + diagnostic) ---

run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_type_error.tc" "literal type"
run_expect_fail_msg "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_expect_fail_msg "$ROOT/tests/errors/static/keyword_error.tc" "wrap cannot be used with cast"
run_expect_fail_msg "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_expect_fail_msg "$ROOT/tests/errors/static/const_expr.tc" "constant initializer must be a literal"
run_expect_fail_msg "$ROOT/tests/errors/static/truncate_in_arith.tc" "truncate cannot be used with arithmetic"
run_expect_fail_msg "$ROOT/tests/errors/static/negative_unsigned_literal.tc" "unsigned suffix"
run_expect_fail_msg "$ROOT/tests/errors/static/leading_zero.tc" "invalid integer literal"
run_expect_fail_msg "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_let_var.tc" "duplicate definition"
run_expect_fail_msg "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_literal.tc" "cast source must be a variable"
run_expect_fail_msg "$ROOT/tests/errors/static/forward_reference.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/self_reference.tc" "cannot reference itself"

# --check 模式下也应当捕获所有静态错误
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_expect_check_fail "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_expect_check_fail "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_expect_check_fail "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_expect_check_fail "$ROOT/tests/errors/static/const_expr.tc" "constant initializer must be a literal"
run_expect_check_fail "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"

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

run_repl_expect ":help
:quit" "Meta commands" "help command"

run_repl_expect "let X: int8 = 999
:vars
:quit" "(no variables)" "let const failure does not pollute session"

echo ""
echo "$PASSED passed, $FAILED failed"

if [ "$FAIL" -ne 0 ]; then
    echo "Failed tests:" >&2
    printf '%s\n' "$FAILED_FILES" | sed '/^$/d' >&2
    exit 1
fi

echo "all vm tests passed"
