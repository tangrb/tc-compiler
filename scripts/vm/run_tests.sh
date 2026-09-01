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
TC_VM_BIN="${TC_VM_BIN:-}"

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

if [ -n "$TC_VM_BIN" ]; then
    BUILD_HINT="(TC_VM_BIN override)"
elif [ "$ASAN_MODE" -eq 1 ] || [ "${ASAN:-0}" = "1" ]; then
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

if [ -x "${TC_VM_BIN}.exe" ] && [ ! -x "$TC_VM_BIN" ]; then
    TC_VM_BIN="${TC_VM_BIN}.exe"
fi

# Windows（MSYS2/MinGW）下把 CLI 金标路径归一化为二进制实际打印的形式：
# MSYS2 会把 argv[0]/程序路径转成反斜杠形式（cygpath -w），把命令行参数
# 转成正斜杠形式（cygpath -m）；金标必须按各自形式比较。
if command -v cygpath >/dev/null 2>&1; then
    case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        native_path() { cygpath -w "$1"; }
        mixed_path() { cygpath -m "$1"; }
        ;;
    *)
        native_path() { printf '%s' "$1"; }
        mixed_path() { printf '%s' "$1"; }
        ;;
    esac
else
    native_path() { printf '%s' "$1"; }
    mixed_path() { printf '%s' "$1"; }
fi

if [ ! -x "$TC_VM_BIN" ]; then
    echo "error: binary not found at $TC_VM_BIN" >&2
    echo "  build first: ${BUILD_HINT}" >&2
    exit 1
fi
TC_VM_BIN_NATIVE="$(native_path "$TC_VM_BIN")"

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
    run_with_leaks() (
        # macOS 26 writes the report to the child's stdout and masks the
        # child's non-zero status.  Run the leak probe with isolated streams,
        # then replay once normally to preserve the conformance-test contract.
        input="$(mktemp)" || exit 1
        trap 'rm -f "$input"' EXIT HUP INT TERM
        cat >"$input"
        if ! env MallocStackLogging=1 leaks --quiet --atExit -- \
            "$TC_VM_BIN" "$@" <"$input" >/dev/null 2>/dev/null; then
            exit 1
        fi
        "$TC_VM_BIN" "$@" <"$input"
    )
    BIN="run_with_leaks"
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


run_cli_golden() {
    argument="$1"
    expected_status="$2"
    expected_stdout="$3"
    expected_stderr="$4"
    label="$5"
    stdout_file="$(mktemp)"
    stderr_file="$(mktemp)"
    status=0

    should_run "$label" || {
        rm -f "$stdout_file" "$stderr_file"
        return 0
    }
    log_test "CLI $label"
    "$TC_VM_BIN" "$argument" >"$stdout_file" 2>"$stderr_file" || status=$?
    actual_stdout="$(cat "$stdout_file")"
    actual_stderr="$(cat "$stderr_file")"
    rm -f "$stdout_file" "$stderr_file"

    if [ "$status" -ne "$expected_status" ] ||
       [ "$actual_stdout" != "$expected_stdout" ] ||
       [ "$actual_stderr" != "$expected_stderr" ]; then
        fail "CLI golden failed: $label" "$label"
        if [ "$VERBOSE" -eq 1 ]; then
            echo "  status: $status (expected $expected_status)" >&2
            printf '  stdout: <%s>\n' "$actual_stdout" >&2
            printf '  stderr: <%s>\n' "$actual_stderr" >&2
        fi
        return
    fi
    pass
}

# --- valid: execution succeeds ---

run_cli_golden "--version" 0 "tc-vm 0.0.43" "" "cli version golden"

run_cli_golden "--help" 0 "" "Usage: $TC_VM_BIN_NATIVE [options] <file.tc>

TC language direct execution engine.

Options:
  -c, --check            static analysis only, do not execute
  -I, --include <path>   add module search path (repeatable)
  -e, --print-error-code  print error code name on diagnostic first line
  -h, --help             show this help and exit
  -V, --version          show version and exit

Notes:
  File execution and --check use the full libtc batch-language pipeline.
  Module search order: entry directory, then -I paths.

Examples:
  $TC_VM_BIN_NATIVE tests/valid/example.tc
  $TC_VM_BIN_NATIVE --check tests/valid/example.tc
  $TC_VM_BIN_NATIVE -I ./lib tests/modules/import_ok.tc" "cli help golden"

CLI_MISSING_PATH="$(mktemp "${TMPDIR:-/tmp}/tc-vm-missing.XXXXXX")"
rm -f "$CLI_MISSING_PATH"
EXPECTED_MISSING="$(mixed_path "$CLI_MISSING_PATH")"
run_cli_golden "$CLI_MISSING_PATH" 1 "" "$EXPECTED_MISSING: api error: FileOpen: cannot open input file" "cli file-open golden"

# D2：--print-error-code 使诊断首行附错误码名
CLI_ERR_PATH="$(mktemp "${TMPDIR:-/tmp}/tc-cli-err.XXXXXX.tc")"
printf '#program\nvar x: int32 = missing\n' > "$CLI_ERR_PATH"
if "$TC_VM_BIN" -e "$CLI_ERR_PATH" 2>&1 | grep -q "error \[UndefinedVariable\]"; then
    pass
else
    fail "cli print-error-code: expected 'error [UndefinedVariable]' in first line" "$CLI_ERR_PATH"
fi
rm -f "$CLI_ERR_PATH"

run_expect_ok "$ROOT/tests/valid/example.tc"
run_expect_ok "$ROOT/tests/valid/signed_wrap.tc"
run_expect_ok "$ROOT/tests/valid/uint8_wrap.tc"
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

run_expect_ok_no_warn "$ROOT/tests/valid/assign_uninit_var_valid.tc" "uninitialized"
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
run_expect_stdout "$ROOT/tests/valid/logic_xor.tc" "true
false
true
false
true
false
true
false
"
run_expect_stdout "$ROOT/tests/valid/bitwise_runtime.tc" "10100000
1011010
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
run_expect_stdout "$ROOT/tests/valid/shl_int64_neg_boundary.tc" "-9223372036854775808
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
run_with_stdin "$ROOT/tests/valid/read_uint64.tc" "18446744073709551615
" "18446744073709551615
"
run_with_stdin "$ROOT/tests/valid/read_float32.tc" "3.25
" "3.25
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

# --- v0.0.42 Phase 3: top-level goto/label rejected ---

run_expect_fail_msg "$ROOT/tests/errors/static/goto_outside_function.tc" \
    "goto is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/label_outside_function.tc" \
    "label is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_toplevel_simple.tc" \
    "label is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_toplevel_forward.tc" \
    "goto is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_toplevel_out_of_if.tc" \
    "goto is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_toplevel_nested_out.tc" \
    "goto is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_toplevel_label_same_name.tc" \
    "label is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_toplevel_var_reinitialize.tc" \
    "label is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_toplevel_let_inline.tc" \
    "goto is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_outside_function.tc" \
    "goto is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/label_outside_function.tc" \
    "label is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_toplevel_simple.tc" \
    "label is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_toplevel_forward.tc" \
    "goto is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_toplevel_out_of_if.tc" \
    "goto is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_toplevel_nested_out.tc" \
    "goto is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_toplevel_label_same_name.tc" \
    "label is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_toplevel_var_reinitialize.tc" \
    "label is only allowed inside a function"
run_expect_check_fail "$ROOT/tests/errors/static/goto_toplevel_let_inline.tc" \
    "goto is only allowed inside a function"
run_expect_fail_msg "$ROOT/tests/errors/static/missing_return.tc" \
    "missing return"
run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_struct.tc" \
    "duplicate struct"
run_expect_fail_msg "$ROOT/tests/errors/static/nullptr_non_ptr.tc" \
    "nullptr is only allowed"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_element_count.tc" \
    "memblock constructor value count"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_size_mismatch.tc" \
    "memblock constructor count does not match destination size"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_assign_size.tc" \
    "memblock size mismatch"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_index_oob.tc" \
    "memblock index out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_count_type.tc" \
    "memblock count result must be usize/isize"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_copy_size.tc" \
    "memblock copy size mismatch"
run_expect_fail_msg "$ROOT/tests/errors/static/undefined_struct.tc" \
    "undefined struct"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_missing_field.tc" \
    "missing field"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_unknown_field.tc" \
    "unknown struct field"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_duplicate_field.tc" \
    "duplicate struct field"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_field_order.tc" \
    "struct constructor fields must follow declaration order"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_immutable_field.tc" \
    "cannot assign to immutable struct field"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_assign_let_outer_let_field.tc" \
    "cannot assign to constant binding"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_assign_let_outer_var_field.tc" \
    "cannot assign to constant binding"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_empty.tc" \
    "struct must have at least one field"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_self_ref.tc" \
    "struct field cannot reference the struct being defined in value position"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_memblock_self_ref.tc" \
    "struct field cannot reference the struct being defined in value position"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_dup_field.tc" \
    "duplicate struct field name"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_padding_neg.tc" \
    "@padding size must be a non-negative decimal integer literal without suffix"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_padding_hex.tc" \
    "@padding size must be a non-negative decimal integer literal without suffix"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_padding_u.tc" \
    "@padding size must be a non-negative decimal integer literal without suffix"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_end_extra.tc" \
    "unexpected trailing tokens after end"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_end_indent.tc" \
    "end indentation does not match struct"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_ptr_fwd_ref.tc" \
    "undefined struct 'B' in field 'p'"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_memblock_undefined.tc" \
    "undefined struct 'NoSuch' in field 'items'"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_undefined_struct.tc" \
    "undefined struct 'NoSuchStruct'"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_struct_type_distinct.tc" \
    "field read result type does not match expected type"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_address_const.tc" \
    "cannot take address of constant binding"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_store_readonly.tc" \
    "cannot store through read-only pointer binding"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_compare_not_bool.tc" \
    "pointer comparison result must be bool"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_size_not_usize.tc" \
    "ptr_size result must be usize/isize"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_type_mismatch.tc" \
    "identifier type does not match destination type"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_cast_width.tc" \
    "pointer cast requires equal-width pointee types"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_io_writeln.tc" \
    "expected type"
run_expect_fail_msg "$ROOT/tests/errors/static/float_special_non_float.tc" \
    "float literal type does not match context"
run_expect_fail_msg "$ROOT/tests/errors/static/float32_suffix_mismatch.tc" \
    "float32 suffix requires float32 context"
run_expect_fail_msg "$ROOT/tests/errors/static/unreachable_after_return.tc" \
    "unreachable statement"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_fill_type.tc" \
    "bool literal requires bool context"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_count_zero.tc" \
    "memblock count must be at least 1"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_type_count_zero.tc" \
    "memblock count must be at least 1"
run_expect_fail_msg "$ROOT/tests/errors/static/memblock_store_oob.tc" \
    "memblock index out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_scalar_arith.tc" \
    "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_add_offset_type.tc" \
    "literal type does not match context"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_add_isize_offset.tc" \
    "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_add_result_type.tc" \
    "pointer arithmetic result type does not match destination"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_nested_non_struct.tc" \
    "field read requires struct base"
run_expect_check_fail "$ROOT/tests/errors/static/missing_return.tc" \
    "missing return"
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_struct.tc" \
    "duplicate struct"
run_expect_check_fail "$ROOT/tests/errors/static/nullptr_non_ptr.tc" \
    "nullptr is only allowed"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_element_count.tc" \
    "memblock constructor value count"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_size_mismatch.tc" \
    "memblock constructor count does not match destination size"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_assign_size.tc" \
    "memblock size mismatch"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_index_oob.tc" \
    "memblock index out of range"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_count_type.tc" \
    "memblock count result must be usize/isize"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_copy_size.tc" \
    "memblock copy size mismatch"
run_expect_check_fail "$ROOT/tests/errors/static/undefined_struct.tc" \
    "undefined struct"
run_expect_check_fail "$ROOT/tests/errors/static/struct_missing_field.tc" \
    "missing field"
run_expect_check_fail "$ROOT/tests/errors/static/struct_unknown_field.tc" \
    "unknown struct field"
run_expect_check_fail "$ROOT/tests/errors/static/struct_duplicate_field.tc" \
    "duplicate struct field"
run_expect_check_fail "$ROOT/tests/errors/static/struct_field_order.tc" \
    "struct constructor fields must follow declaration order"
run_expect_check_fail "$ROOT/tests/errors/static/struct_immutable_field.tc" \
    "cannot assign to immutable struct field"
run_expect_check_fail "$ROOT/tests/errors/static/struct_assign_let_outer_let_field.tc" \
    "cannot assign to constant binding"
run_expect_check_fail "$ROOT/tests/errors/static/struct_assign_let_outer_var_field.tc" \
    "cannot assign to constant binding"
run_expect_check_fail "$ROOT/tests/errors/static/struct_empty.tc" \
    "struct must have at least one field"
run_expect_check_fail "$ROOT/tests/errors/static/struct_self_ref.tc" \
    "struct field cannot reference the struct being defined in value position"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_address_const.tc" \
    "cannot take address of constant binding"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_store_readonly.tc" \
    "cannot store through read-only pointer binding"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_compare_not_bool.tc" \
    "pointer comparison result must be bool"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_size_not_usize.tc" \
    "ptr_size result must be usize/isize"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_type_mismatch.tc" \
    "identifier type does not match destination type"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_cast_width.tc" \
    "pointer cast requires equal-width pointee types"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_io_writeln.tc" \
    "expected type"
run_expect_check_fail "$ROOT/tests/errors/static/float_special_non_float.tc" \
    "float literal type does not match context"
run_expect_check_fail "$ROOT/tests/errors/static/float32_suffix_mismatch.tc" \
    "float32 suffix requires float32 context"
run_expect_check_fail "$ROOT/tests/errors/static/unreachable_after_return.tc" \
    "unreachable statement"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_fill_type.tc" \
    "bool literal requires bool context"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_count_zero.tc" \
    "memblock count must be at least 1"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_type_count_zero.tc" \
    "memblock count must be at least 1"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_store_oob.tc" \
    "memblock index out of range"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_scalar_arith.tc" \
    "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_add_offset_type.tc" \
    "literal type does not match context"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_add_result_type.tc" \
    "pointer arithmetic result type does not match destination"
run_expect_check_fail "$ROOT/tests/errors/static/struct_nested_non_struct.tc" \
    "field read requires struct base"
run_expect_check_ok "$ROOT/tests/valid/phase3_nullptr.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_struct_ctor.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_memblock.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_memblock_fill.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_memblock_store.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_ptr_ops.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_ptr_load.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_ptr_cmp.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_struct_nested.tc"
run_expect_check_ok "$ROOT/tests/valid/phase3_struct_mut_ok.tc"

# --- Phase 4: function semantics / static let ---
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_function.tc" "duplicate function"
run_expect_check_fail "$ROOT/tests/errors/static/function_name_conflict.tc" \
    "function name conflicts"
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_parameter.tc" "duplicate parameter"
run_expect_check_fail "$ROOT/tests/errors/static/undefined_function.tc" "undefined function"
run_expect_check_fail "$ROOT/tests/errors/static/function_scope_access.tc" \
    "function scope access"
run_expect_check_fail "$ROOT/tests/errors/static/funcall_position.tc" \
    "non-void function call must be used"
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_argument.tc" "duplicate argument"
run_expect_check_fail "$ROOT/tests/errors/static/unknown_argument.tc" "unknown argument"
run_expect_check_fail "$ROOT/tests/errors/static/missing_argument.tc" "missing argument"
run_expect_check_fail "$ROOT/tests/errors/static/argument_order.tc" "argument order"
run_expect_check_fail "$ROOT/tests/errors/static/funcall_extra_arg.tc" "too many arguments"
run_expect_check_fail "$ROOT/tests/errors/static/argument_type.tc" \
    "bool literal requires bool context"
run_expect_check_fail "$ROOT/tests/errors/static/funcall_result_type.tc" \
    "function call result type"
run_expect_check_fail "$ROOT/tests/errors/static/return_outside_function.tc" \
    "return outside function"
run_expect_check_fail "$ROOT/tests/errors/static/return_form.tc" \
    "void function cannot return a value"
run_expect_fail_msg "$ROOT/tests/errors/static/nonvoid_return_no_value.tc" \
    "non-void function must return a value"
run_expect_check_fail "$ROOT/tests/errors/static/nonvoid_return_no_value.tc" \
    "non-void function must return a value"
run_expect_fail_msg "$ROOT/tests/errors/static/return_type_var_mismatch.tc" \
    "return type does not match function return type"
run_expect_check_fail "$ROOT/tests/errors/static/return_type_var_mismatch.tc" \
    "return type does not match function return type"
run_expect_fail_msg "$ROOT/tests/errors/static/void_funcall_as_value.tc" \
    "void function call cannot be used as value"
run_expect_check_fail "$ROOT/tests/errors/static/void_funcall_as_value.tc" \
    "void function call cannot be used as value"
run_expect_check_fail "$ROOT/tests/errors/static/return_type.tc" "bool literal requires bool context"
run_expect_check_fail "$ROOT/tests/errors/static/parameter_assignment.tc" \
    "cannot assign to function parameter"
run_expect_check_fail "$ROOT/tests/errors/static/parameter_assignment_read.tc" \
    "cannot assign to function parameter"
run_expect_fail_msg "$ROOT/tests/errors/static/read_into_let.tc" \
    "cannot assign to constant"
run_expect_check_fail "$ROOT/tests/errors/static/read_into_let.tc" \
    "cannot assign to constant"
run_expect_fail_msg "$ROOT/tests/errors/static/read_type_mismatch.tc" \
    "read type does not match variable type"
run_expect_check_fail "$ROOT/tests/errors/static/read_type_mismatch.tc" \
    "read type does not match variable type"
run_expect_check_fail "$ROOT/tests/errors/static/recursion_direct.tc" "recursive function call"
run_expect_check_fail "$ROOT/tests/errors/static/recursion_indirect.tc" "recursive function call"
run_expect_check_fail "$ROOT/tests/errors/static/static_let_forward.tc" \
    "circular static let"
# Critical 1 回归：static let/var 的 memblock 逐值构造计数不匹配（const 求值先于 pass2）
run_expect_check_fail "$ROOT/tests/errors/static/static_let_memblock_count_mismatch.tc" \
    "memblock element count mismatch"
run_expect_check_fail "$ROOT/tests/errors/static/static_let_memblock_count_too_few.tc" \
    "memblock element count mismatch"
run_expect_check_fail "$ROOT/tests/errors/static/static_var_memblock_count_mismatch.tc" \
    "memblock constructor value count does not match count"
run_expect_check_fail "$ROOT/tests/errors/static/static_var_bad_init.tc" \
    "static var initializer"
run_expect_check_fail "$ROOT/tests/errors/static/self_member_undefined.tc" \
    "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/self_member_type_mismatch.tc" \
    "identifier type does not match destination type"
run_expect_check_fail "$ROOT/tests/errors/static/self_member_bare_name.tc" \
    "undefined variable"
run_expect_check_fail "$ROOT/tests/modules/private_member_access.tc" "private member access"
run_expect_fail_msg "$ROOT/tests/modules/imported_struct_bare_name.tc" \
    "undefined struct 'Box'"
run_expect_check_fail "$ROOT/tests/modules/imported_struct_bare_name.tc" \
    "undefined struct 'Box'"
run_expect_fail_msg "$ROOT/tests/modules/imported_struct_bare_ctor.tc" \
    "undefined struct 'Box'"
run_expect_check_fail "$ROOT/tests/modules/imported_struct_bare_ctor.tc" \
    "undefined struct 'Box'"
run_expect_fail_msg "$ROOT/tests/modules/imported_struct_not_imported.tc" \
    "undefined struct 'BoxLib.Box'"
run_expect_check_fail "$ROOT/tests/modules/imported_struct_not_imported.tc" \
    "undefined struct 'BoxLib.Box'"
run_expect_fail_msg "$ROOT/tests/modules/imported_struct_transitive.tc" \
    "undefined struct 'TransStructLib.Box'"
run_expect_check_fail "$ROOT/tests/modules/imported_struct_transitive.tc" \
    "undefined struct 'TransStructLib.Box'"
run_expect_fail_msg "$ROOT/tests/modules/imported_struct_private.tc" \
    "private member access"
run_expect_check_fail "$ROOT/tests/modules/imported_struct_private.tc" \
    "private member access"
run_expect_stdout "$ROOT/tests/modules/imported_struct_mid_ok.tc" "3
"
run_expect_check_ok "$ROOT/tests/modules/imported_struct_mid_ok.tc"
# Major 4 回归：菱形 import（Left/Right 均 import Shared）结构体注册须真拓扑序
run_expect_stdout "$ROOT/tests/modules/diamond_import_ok.tc" "3
3
"
run_expect_stdout "$ROOT/tests/modules/diamond_import_swapped_ok.tc" "4
4
"
run_expect_check_ok "$ROOT/tests/modules/diamond_import_ok.tc"
run_expect_check_ok "$ROOT/tests/modules/diamond_import_swapped_ok.tc"
run_expect_check_fail "$ROOT/tests/errors/static/struct_assign_through_param.tc" \
    "cannot assign to function parameter"
run_expect_check_fail "$ROOT/tests/errors/static/struct_assign_param_let.tc" \
    "cannot assign to function parameter"
run_expect_check_fail "$ROOT/tests/errors/static/parameter_name_conflict.tc" \
    "conflicts with function name"
run_expect_check_fail "$ROOT/tests/errors/static/param_shadow_local.tc" \
    "duplicate definition"
run_expect_check_fail "$ROOT/tests/errors/static/funcall_memblock_size.tc" \
    "memblock size mismatch"
run_expect_check_ok "$ROOT/tests/valid/phase4_self_funcall.tc"
run_expect_check_ok "$ROOT/tests/valid/phase4_static_let.tc"
run_expect_check_ok "$ROOT/tests/valid/phase4_func_goto.tc"

# --- Phase 5 / Module I: Executor (funcall, ptr, memblock) ---

run_expect_stdout "$ROOT/tests/valid/phase5_funcall_return.tc" "3
"
run_expect_stdout "$ROOT/tests/valid/phase5_ptr_basic.tc" "2
"
run_expect_stdout "$ROOT/tests/valid/phase5_ptr_cast.tc" "100
"
run_expect_stdout "$ROOT/tests/valid/phase5_ptr_cast_nullptr.tc" "true
true
"
run_expect_stdout "$ROOT/tests/valid/phase5_ptr_bitcast.tc" "7
"
run_expect_stdout "$ROOT/tests/valid/phase5_ptr_scope_outer.tc" "2
"
run_expect_stdout "$ROOT/tests/valid/phase5_memblock_deepcopy.tc" "1
1
"
run_expect_stdout "$ROOT/tests/valid/phase5_memcopy_unsafe.tc" "1
"
# Major 3 回归：memcopy_unsafe 负下标（字面量 / 有符号变量）须拒绝，不得回绕越界
run_expect_fail_msg "$ROOT/tests/errors/runtime/memcopy_unsafe_neg_dst_index.tc" \
    "memcopy_unsafe invalid range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memcopy_unsafe_neg_src_index.tc" \
    "memcopy_unsafe invalid range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memcopy_unsafe_neg_var_index.tc" \
    "memcopy_unsafe invalid range"
run_expect_stdout "$ROOT/tests/valid/memcopy_unsafe_positive_ok.tc" "1
4
"
run_expect_stdout "$ROOT/tests/valid/phase5_memblock_basic.tc" "9
2
"
run_expect_stdout "$ROOT/tests/valid/phase5_ptr_arith_cmp.tc" "true
true
32
"
run_expect_stdout "$ROOT/tests/valid/phase5_memblock_copy.tc" "2
3
"
run_expect_stdout "$ROOT/tests/valid/phase5_void_funcall.tc" "1
"
run_expect_stdout "$ROOT/tests/valid/phase5_static_var.tc" "7
"
run_expect_stdout "$ROOT/tests/valid/phase5_self_static_let.tc" "11
11
"
run_expect_stdout "$ROOT/tests/valid/phase5_self_static_ops.tc" "7
8
"
run_expect_stdout "$ROOT/tests/valid/phase5_ptr_cmp_more.tc" "true
true
true
true
true
"
run_expect_stdout "$ROOT/tests/valid/phase5_nullptr_eq.tc" "true
false
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_basic.tc" "3
4
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_field_assign.tc" "42
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_nested.tc" "7
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_copy.tc" "10
20
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_padding.tc" "1
2
9
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_nested_assign.tc" "99
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_whole_assign.tc" "10
20
30
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_mixed_types.tc" "1.5
true
2.25
false
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_extract_indep.tc" "5
8
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_funcall.tc" "743
"
run_expect_stdout "$ROOT/tests/valid/import_struct_type.tc" "7
3
7
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_memblock.tc" "10
20
10
99
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_multi_field.tc" "10
20
30
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_ptr_field.tc" "7
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_ptr_self_ref.tc" "1
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_ptr_roundtrip.tc" "42
99
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_memblock_of_struct.tc" "7
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_memblock_deepcopy.tc" "7
9
7
5
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_ptr_nested_self_ref.tc" "1
"
run_expect_stdout "$ROOT/tests/valid/phase5_struct_mut_matrix_ok.tc" "10
30
"
run_expect_stdout "$ROOT/tests/valid/phase5_memblock_fill.tc" "5
5
"
run_expect_stdout "$ROOT/tests/valid/phase5_memcopy_ptr_int32.tc" "7
"
run_expect_stdout "$ROOT/tests/valid/phase5_memcopy_ptr_struct.tc" "5
"
run_expect_stdout "$ROOT/tests/valid/phase5_memcopy_ptr_ptr.tc" "9
"
run_expect_stdout "$ROOT/tests/valid/phase5_nested_funcall.tc" "14
"
run_expect_stdout "$ROOT/tests/valid/isize_arith.tc" "7
"
run_expect_stdout "$ROOT/tests/valid/usize_arith.tc" "7
"
run_expect_check_ok "$ROOT/tests/valid/phase5_funcall_return.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_ptr_basic.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_ptr_cast.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_ptr_cast_nullptr.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_ptr_bitcast.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_ptr_scope_outer.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_memblock_basic.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_ptr_arith_cmp.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_memblock_copy.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_void_funcall.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_static_var.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_self_static_let.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_self_static_ops.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_ptr_cmp_more.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_nullptr_eq.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_basic.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_field_assign.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_nested.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_copy.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_padding.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_nested_assign.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_whole_assign.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_mixed_types.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_extract_indep.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_funcall.tc"
run_expect_check_ok "$ROOT/tests/valid/import_struct_type.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_memblock.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_multi_field.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_ptr_field.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_ptr_self_ref.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_ptr_roundtrip.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_memblock_of_struct.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_memblock_deepcopy.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_ptr_nested_self_ref.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_struct_mut_matrix_ok.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_memblock_fill.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_memcopy_ptr_int32.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_memcopy_ptr_struct.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_memcopy_ptr_ptr.tc"
run_expect_check_ok "$ROOT/tests/valid/phase5_nested_funcall.tc"
run_expect_check_ok "$ROOT/tests/valid/isize_arith.tc"
run_expect_check_ok "$ROOT/tests/valid/usize_arith.tc"

# --- struct field operand (0.0.42 field_access as operand) ---
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_arith.tc" "42
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_compare.tc" "false
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_io.tc" "99
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_return.tc" "17
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_nested.tc" "22
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_shortcircuit.tc" "false
true
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_const.tc" "5
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_ptr.tc" "13
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_memblock.tc" "20
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_count.tc" "4
2
true
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_let_rhs.tc" "5
"
run_expect_stdout "$ROOT/tests/valid/struct_field_named_count.tc" "12
6
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_composite.tc" "5
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_cast.tc" "65
1065353216
"
run_expect_stdout "$ROOT/tests/valid/struct_field_operand_self_base.tc" "2
2
"
run_expect_stdout "$ROOT/tests/valid/struct_field_static_init_run.tc" "9
10
true
10
"
run_expect_stdout "$ROOT/tests/valid/struct_field_static_topo_ops_run.tc" "-9
9
18
true
false
2.5
true
9
1.26117e-44
18
9
9
"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_arith.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_compare.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_io.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_return.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_nested.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_shortcircuit.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_const.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_ptr.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_memblock.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_count.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_let_rhs.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_named_count.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_composite.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_cast.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_operand_self_base.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_static_init.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_static_init_run.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_static_topo_ops.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_static_topo_ops_run.tc"
# Critical 2 回归：let/static let 基址的 struct/memblock 字段整体读出（AOT 曾段错误）
run_expect_stdout "$ROOT/tests/valid/struct_field_const_base_struct.tc" "11
11
"
run_expect_stdout "$ROOT/tests/valid/struct_field_const_base_memblock.tc" "2
1
"
run_expect_stdout "$ROOT/tests/valid/struct_field_const_base_nested.tc" "7
7
"
run_expect_check_ok "$ROOT/tests/valid/struct_field_const_base_struct.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_const_base_memblock.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_const_base_nested.tc"
run_expect_check_ok "$ROOT/tests/valid/static_let_memblock_ctor_ok.tc"
run_expect_check_ok "$ROOT/tests/valid/struct_field_static_let_base.tc"
run_expect_fail_msg "$ROOT/tests/errors/static/operand_nested_arith.tc" "expected operand"
run_expect_fail_msg "$ROOT/tests/errors/static/operand_field_var_in_let.tc" "constant expression cannot reference var variable"
run_expect_fail_msg "$ROOT/tests/errors/static/operand_field_var_in_const_op.tc" "constant expression cannot reference var variable"
run_expect_fail_msg "$ROOT/tests/errors/static/operand_field_static_var_forward.tc" "constant expression cannot reference var variable"
run_expect_check_fail "$ROOT/tests/errors/static/operand_nested_arith.tc" "expected operand"
run_expect_check_fail "$ROOT/tests/errors/static/operand_field_var_in_let.tc" "constant expression cannot reference var variable"
run_expect_check_fail "$ROOT/tests/errors/static/operand_field_var_in_const_op.tc" "constant expression cannot reference var variable"
run_expect_check_fail "$ROOT/tests/errors/static/operand_field_static_var_forward.tc" "constant expression cannot reference var variable"

run_expect_fail_msg "$ROOT/tests/errors/runtime/negative_shift_count.tc" "negative shift count"
run_expect_fail_msg "$ROOT/tests/errors/runtime/negative_shift_count_shl.tc" "negative shift count"
run_expect_fail_msg "$ROOT/tests/errors/runtime/null_ptr_deref.tc" "null pointer dereference"
run_expect_fail_msg "$ROOT/tests/errors/runtime/null_ptr_arith.tc" "null pointer arithmetic"
run_expect_fail_msg "$ROOT/tests/errors/runtime/null_ptr_store.tc" "null pointer dereference"
run_expect_fail_msg "$ROOT/tests/errors/runtime/null_ptr_cmp.tc" "null pointer dereference"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memblock_oob_rt.tc" "memblock index out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memblock_oob_store_rt.tc" "memblock index out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memcopy_unsafe_null.tc" "null pointer dereference"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memcopy_unsafe_neg.tc" "memcopy_unsafe invalid range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memcopy_unsafe_neg_index.tc" "memcopy_unsafe invalid range"

run_expect_stdout "$ROOT/tests/valid/uninit_both_paths.tc" "11
"
run_expect_stdout "$ROOT/tests/valid/uninit_shortcircuit.tc" "false
"
run_expect_check_ok "$ROOT/tests/valid/uninit_both_paths.tc"
run_expect_check_ok "$ROOT/tests/valid/uninit_shortcircuit.tc"

# --- v0.0.31: while / break / continue execution ---

run_expect_stdout "$ROOT/tests/valid/while_false.tc" "0
"
run_expect_stdout "$ROOT/tests/valid/while_counted.tc" "0
1
2
3
4
"
run_expect_stdout "$ROOT/tests/valid/while_nested.tc" "0
1
2
0
1
2
"
run_expect_stdout "$ROOT/tests/valid/while_break_continue.tc" "1
3
4
"
run_expect_stdout "$ROOT/tests/valid/while_var_reinitialize.tc" "10
11
12
"
run_expect_check_ok "$ROOT/tests/valid/while_false.tc"
run_expect_check_ok "$ROOT/tests/valid/while_counted.tc"
run_expect_check_ok "$ROOT/tests/valid/while_nested.tc"
run_expect_check_ok "$ROOT/tests/valid/while_break_continue.tc"
run_expect_check_ok "$ROOT/tests/valid/while_var_reinitialize.tc"

# --- v0.0.31: static bool CFG pruning ---

run_expect_stdout "$ROOT/tests/valid/uninit_shortcircuit_let_bool.tc" "false
true
"
run_expect_stdout "$ROOT/tests/valid/uninit_const_condition_if.tc" "11
"
run_expect_stdout "$ROOT/tests/valid/uninit_const_condition_while.tc" "true
"
run_expect_check_ok "$ROOT/tests/valid/uninit_shortcircuit_let_bool.tc"
run_expect_check_ok "$ROOT/tests/valid/uninit_const_condition_if.tc"
run_expect_check_ok "$ROOT/tests/valid/uninit_const_condition_while.tc"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_inside_loop.tc" \
    "goto is not allowed inside while"
run_expect_fail_msg "$ROOT/tests/errors/static/label_inside_loop.tc" \
    "label is not allowed inside while"
run_expect_fail_msg "$ROOT/tests/errors/static/break_outside_loop.tc" \
    "break used outside while"
run_expect_fail_msg "$ROOT/tests/errors/static/continue_outside_loop.tc" \
    "continue used outside while"

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
run_expect_ok "$ROOT/tests/valid/if_and_or_condition.tc"
run_expect_stdout "$ROOT/tests/valid/if_and_or_condition.tc" "1
2
3
"
run_expect_check_ok "$ROOT/tests/valid/if_and_or_condition.tc"
run_expect_ok "$ROOT/tests/valid/if_comparison_condition.tc"
run_expect_stdout "$ROOT/tests/valid/if_comparison_condition.tc" "1
2
3
4
5
6
"
run_expect_check_ok "$ROOT/tests/valid/if_comparison_condition.tc"
run_expect_ok "$ROOT/tests/valid/if_not_condition.tc"
run_expect_stdout "$ROOT/tests/valid/if_not_condition.tc" "1
2
3
4
"
run_expect_check_ok "$ROOT/tests/valid/if_not_condition.tc"
run_expect_ok "$ROOT/tests/valid/if_empty_body.tc"
run_expect_stdout "$ROOT/tests/valid/if_empty_body.tc" "0
1
2
3
"
run_expect_check_ok "$ROOT/tests/valid/if_empty_body.tc"

# --- v0.0.25: float32/float64 ---

run_expect_check_ok "$ROOT/tests/valid/fp_basic.tc"
run_expect_stdout "$ROOT/tests/valid/fp_arith.tc" "13
7
30
3.33333
3
"
run_expect_check_ok "$ROOT/tests/valid/fp_arith.tc"
run_expect_stdout "$ROOT/tests/valid/fp_arith_ieee.tc" "inf
nan
"
run_expect_check_ok "$ROOT/tests/valid/fp_arith_ieee.tc"
run_expect_stdout "$ROOT/tests/valid/fp_compare.tc" "false
true
true
true
false
false
false
true
"
run_expect_check_ok "$ROOT/tests/valid/fp_compare.tc"
run_expect_stdout "$ROOT/tests/valid/fp_cast.tc" "42
42
1
"
run_expect_check_ok "$ROOT/tests/valid/fp_cast.tc"
run_expect_stdout "$ROOT/tests/valid/fp_bitcast_roundtrip.tc" "3F800000
1
3FF0000000000000
1
"
run_expect_check_ok "$ROOT/tests/valid/fp_bitcast_roundtrip.tc"
run_expect_stdout "$ROOT/tests/valid/bitcast_roundtrip32.tc" "BF800000
7FC12345
80000000
7F800000
"
run_expect_check_ok "$ROOT/tests/valid/bitcast_roundtrip32.tc"
run_expect_stdout "$ROOT/tests/valid/nan_canonical_bits.tc" "7FC00000
7FF8000000000000
"
run_expect_stdout "$ROOT/tests/valid/bitcast_roundtrip64.tc" "7FF8000000001234
8000000000000000
"
run_expect_check_ok "$ROOT/tests/valid/bitcast_roundtrip64.tc"
run_expect_stdout "$ROOT/tests/valid/let_runtime_equivalence.tc" "-116
-116
-24
-24
7F800000
7F800000
"
run_expect_check_ok "$ROOT/tests/valid/let_runtime_equivalence.tc"
run_expect_stdout "$ROOT/tests/valid/let_wrap_allowed.tc" "-116
-116
0
0
"
run_expect_check_ok "$ROOT/tests/valid/let_wrap_allowed.tc"
run_expect_stdout "$ROOT/tests/valid/let_float_ieee.tc" "4008000000000000
"
run_expect_check_ok "$ROOT/tests/valid/let_float_ieee.tc"
run_expect_stdout "$ROOT/tests/valid/let_float32_step_rounding.tc" "4B800000
4B800000
0
0
"
run_expect_check_ok "$ROOT/tests/valid/let_float32_step_rounding.tc"
run_expect_stdout "$ROOT/tests/valid/let_bitcast_payload.tc" "7FF8000000001234
7FF8000000001234
"
run_expect_check_ok "$ROOT/tests/valid/let_bitcast_payload.tc"
run_expect_stdout "$ROOT/tests/valid/let_block_local_chain.tc" "2
"
run_expect_check_ok "$ROOT/tests/valid/let_block_local_chain.tc"
run_expect_stdout "$ROOT/tests/valid/cast_literal.tc" "10
10
1.5
3
true
"
run_expect_check_ok "$ROOT/tests/valid/cast_literal.tc"
run_with_stdin "$ROOT/tests/valid/fp_io.tc" "3.14
" "3.1400003.140000e+003.140000E+003.143.14
-0.000000
-0
inf
-INF
nan
NAN
"
run_expect_check_ok "$ROOT/tests/valid/fp_io.tc"
run_expect_stdout "$ROOT/tests/valid/fp_const_expr.tc" "7
false
"
run_expect_check_ok "$ROOT/tests/valid/fp_const_expr.tc"
run_expect_stdout "$ROOT/tests/valid/fp_if_block.tc" "3.5
1
"
run_expect_check_ok "$ROOT/tests/valid/fp_if_block.tc"
run_expect_stdout "$ROOT/tests/valid/format_spec_fp.tc" "3.141593
3.141593e+00
3.141593E+00
3.14159
3.14159
"
run_expect_check_ok "$ROOT/tests/valid/format_spec_fp.tc"
run_expect_stdout "$ROOT/tests/valid/format_spec_flags.tc" "+42
00000042
00000042
0x2a
0003.142
3.14    
3.141593e+00
3.14159
true    
 true
"
run_expect_check_ok "$ROOT/tests/valid/format_spec_flags.tc"
run_expect_stdout "$ROOT/tests/valid/format_spec_table.tc" "ff
FF
377
11111111
42
42
052
0b101010
0X2A
3.141593    
"
run_expect_check_ok "$ROOT/tests/valid/format_spec_table.tc"
run_expect_check_ok "$ROOT/tests/valid/format_width_max.tc"
run_expect_stdout "$ROOT/tests/valid/format_fp_exact.tc" "3.141593
0003.142
3.141593e+00
3.141593E+00
3.14159
1.50000
0
2
2
2.67
0.0001
1e-05
123456
1.23457e+06
1e+08
1.000000e+05
-0.000000
-0
4.9406564584124654e-324
1e+30
0.1000000015
0.1000000000
"
run_expect_check_ok "$ROOT/tests/valid/format_fp_exact.tc"
run_expect_stdout "$ROOT/tests/valid/fp_mod.tc" "1.5
-1.5
1.5
1.5
"
run_expect_check_ok "$ROOT/tests/valid/fp_mod.tc"
run_expect_stdout "$ROOT/tests/valid/fp_mod_ieee_nan.tc" "7ff8000000000000
7fc00000
"
run_expect_check_ok "$ROOT/tests/valid/fp_mod_ieee_nan.tc"
run_expect_stdout "$ROOT/tests/valid/fp_mod_edges.tc" "3
3
8000000000000000
"
run_expect_check_ok "$ROOT/tests/valid/fp_mod_edges.tc"
run_expect_stdout "$ROOT/tests/valid/self_member_memblock_copy.tc" "1
"
run_expect_check_ok "$ROOT/tests/valid/self_member_memblock_copy.tc"
run_expect_stdout "$ROOT/tests/valid/self_member_struct_copy.tc" "7
"
run_expect_check_ok "$ROOT/tests/valid/self_member_struct_copy.tc"
run_expect_check_ok "$ROOT/tests/valid/ptr_address_param_load.tc"
run_expect_stdout "$ROOT/tests/valid/identifier_named_padding.tc" "1
"
run_expect_check_ok "$ROOT/tests/valid/identifier_named_padding.tc"
run_expect_stdout "$ROOT/tests/valid/let_ptr_size.tc" "32
"
run_expect_check_ok "$ROOT/tests/valid/let_ptr_size.tc"
run_expect_stdout "$ROOT/tests/valid/let_memblock_const.tc" "10
20
2
"
run_expect_check_ok "$ROOT/tests/valid/let_memblock_const.tc"
run_expect_stdout "$ROOT/tests/valid/qualified_memblock_count.tc" "3
9
"
run_expect_check_ok "$ROOT/tests/valid/qualified_memblock_count.tc"
run_with_stdin "$ROOT/tests/valid/qualified_read_target.tc" "42
" "42
"
run_expect_check_ok "$ROOT/tests/valid/qualified_read_target.tc"
run_expect_stdout "$ROOT/tests/valid/let_ptr_cast_nullptr.tc" "true
"
run_expect_check_ok "$ROOT/tests/valid/let_ptr_cast_nullptr.tc"
run_expect_stdout "$ROOT/tests/valid/fp_neg_abs.tc" "-3.5
3.5
2.5
-1.5
-0
0
"
run_expect_check_ok "$ROOT/tests/valid/fp_neg_abs.tc"
run_expect_stdout "$ROOT/tests/valid/fp_const_let_arith.tc" "5
6
3
4
-2
true
false
7
4
"
run_expect_check_ok "$ROOT/tests/valid/fp_const_let_arith.tc"
run_expect_stdout "$ROOT/tests/valid/fp_ieee_ops.tc" "inf
inf
nan
inf
-inf
nan
inf
inf
inf
"
run_expect_check_ok "$ROOT/tests/valid/fp_ieee_ops.tc"
run_expect_stdout "$ROOT/tests/valid/fp_exact_subnormal.tc" "8000000000000
"
run_expect_check_ok "$ROOT/tests/valid/fp_exact_subnormal.tc"

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
run_expect_stdout "$ROOT/tests/stress/stress_fp_chain.tc" "3.14159
6.28319
7.28319
6.78319
3.39159
true
0.391593
0.391593
0
4.48046
"
run_expect_check_ok "$ROOT/tests/stress/stress_fp_chain.tc"
run_expect_stdout "$ROOT/tests/stress/stress_many_ifs.tc" "20
5
"

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

# --- errors/runtime: float ---

run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_strict_overflow.tc" "float overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp32_strict_overflow.tc" "float overflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_strict_underflow.tc" "float underflow"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_strict_invalid.tc" "float invalid operation"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_strict_invalid_before_divzero.tc" \
    "float invalid operation"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_cast_overflow.tc" "out of range"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_div_zero.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_mod_invalid.tc" "float invalid operation"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_mod_divzero.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_mod_invalid_inf.tc" "float invalid operation"
run_expect_fail_msg "$ROOT/tests/errors/runtime/fp_mod_invalid_before_divzero.tc" \
    "float invalid operation"
run_expect_fail_msg "$ROOT/tests/errors/runtime/memblock_copy_overflow_guard.tc" "memblock index out of range"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_fp_invalid.tc" "abc
" "invalid input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_float_invalid.tc" "1.
" "invalid input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_float_invalid.tc" ".5
" "invalid input"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_fp_out_of_range.tc" "1e400
" "input value out of range"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_out_of_range_uint64.tc" "99999999999999999999
" "input value out of range"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_out_of_range_float32.tc" "1e400
" "input value out of range"

# --- errors/runtime: extended tests (per-type coverage) ---

run_expect_fail_msg "$ROOT/tests/errors/runtime/div_zero_int16.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/div_zero_uint32.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/mod_zero_int64.tc" "division by zero"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_strict_sub_overflow.tc" "out of range for"
run_expect_fail_msg "$ROOT/tests/errors/runtime/signed_sub_overflow_int64.tc" "signed subtraction overflow"
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
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_int8_to_uint8.tc" "cast result out of range for target type"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_int16_to_int8.tc" "cast result out of range for target type"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_int32_to_int16.tc" "cast result out of range for target type"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_uint16_to_int16.tc" "cast result out of range for target type"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_uint32_to_int32.tc" "cast result out of range for target type"
run_expect_fail_msg "$ROOT/tests/errors/runtime/cast_strict_overflow_int64_to_int32.tc" "cast result out of range for target type"
run_expect_fail_stdin_msg "$ROOT/tests/errors/runtime/read_out_of_range_int64.tc" "99999999999999999999
" "input value out of range"

# --- errors/static (expect failure + diagnostic) ---

run_expect_fail_msg "$ROOT/tests/errors/static/uninit_simple.tc" "use of uninitialized variable"
run_expect_fail_msg "$ROOT/tests/errors/static/uninit_chain.tc" "use of uninitialized variable"
run_expect_fail_msg "$ROOT/tests/errors/static/uninit_multi.tc" "use of uninitialized variable"
run_expect_fail_msg "$ROOT/tests/errors/static/uninit_slot_value.tc" "use of uninitialized variable"
run_expect_fail_msg "$ROOT/tests/errors/static/uninit_if_path.tc" "use of uninitialized variable"
run_expect_fail_msg "$ROOT/tests/errors/static/uninit_goto_skip_init.tc" "use of uninitialized variable"
run_expect_fail_msg "$ROOT/tests/errors/static/shortcircuit_let_invalid_rhs.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/shortcircuit_let_rhs_type.tc" "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/uninit_shortcircuit_var_lhs.tc" "use of uninitialized variable"
run_expect_fail_msg "$ROOT/tests/errors/static/shortcircuit_let_forward_lhs.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/shortcircuit_let_out_of_scope_lhs.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/diag_priority_syntax_before_name.tc" "unexpected token"
run_expect_fail_msg "$ROOT/tests/errors/static/diag_priority_name_before_type.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/diag_priority_mode_before_literal.tc" "wrap mode is not allowed for float arithmetic"
run_expect_fail_msg "$ROOT/tests/errors/static/diag_priority_const_before_dfa.tc" "constant division by zero"
run_expect_fail_msg "$ROOT/tests/errors/static/diag_priority_format_after_operand.tc" \
    "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_undefined.tc" "label 'nonexistent' not found"
run_expect_fail_msg "$ROOT/tests/errors/static/label_duplicate.tc" "duplicate label"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_into_block.tc" "cannot jump into inner block"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_sibling.tc" "cannot jump into incompatible block"
run_expect_fail_msg "$ROOT/tests/errors/static/goto_cross_function_label_not_found.tc" \
    "label 'y' not found"
run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/literal_type_error.tc" \
    "unsigned suffix literal cannot be used in signed context"
run_expect_fail_msg "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_expect_fail_msg "$ROOT/tests/errors/static/abs_wrap_error.tc" "abs does not support wrap"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_wrap_on_shr_keyword_error.tc" "wrap cannot be used with shift operations"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_wrap_on_and_keyword_error.tc" "wrap cannot be used with bitwise operations"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_shl_truncate_keyword_error.tc" "truncate cannot be used with shift operations"
run_expect_fail_msg "$ROOT/tests/errors/static/const_shift_wrap_mode.tc" "wrap cannot be used with shift operations"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_shift_type_mismatch.tc" "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/bitwise_shl_const_overflow.tc" "constant overflow"
run_expect_fail_msg "$ROOT/tests/errors/static/keyword_error.tc" "wrap cannot be used with cast"
run_expect_fail_msg "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_expect_fail_msg "$ROOT/tests/errors/static/const_expr.tc" "constant expression cannot reference var variable"
run_expect_fail_msg "$ROOT/tests/errors/static/bool_literal_type_error.tc" "bool literal requires bool context"
run_expect_fail_msg "$ROOT/tests/errors/static/compare_type_mismatch.tc" "literal type does not match context"
run_expect_fail_msg "$ROOT/tests/errors/static/compare_type_mismatch_var.tc" \
    "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/logic_type_error.tc" "operand type does not match operation type"
run_expect_fail_msg "$ROOT/tests/errors/static/const_cyclic_dep.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/let_nested_call.tc" "nested calls are not allowed in constant expression"
run_expect_fail_msg "$ROOT/tests/errors/static/let_short_circuit_invalid_rhs.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/const_overflow.tc" "constant overflow"
run_expect_fail_msg "$ROOT/tests/errors/static/const_div_zero.tc" "constant division by zero"
run_expect_fail_msg "$ROOT/tests/errors/static/let_const_cast_overflow.tc" "constant cast overflow"
run_expect_fail_msg "$ROOT/tests/errors/static/let_const_cast_overflow_fp.tc" "constant cast overflow"
run_expect_fail_msg "$ROOT/tests/errors/static/truncate_in_arith.tc" "truncate cannot be used with arithmetic"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_bool_truncate_keyword_error.tc" "truncate requires an integer target narrower than the source"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_truncate_bool_source_error.tc" "truncate requires an integer target narrower than the source"
run_expect_fail_msg "$ROOT/tests/errors/static/negative_unsigned_literal.tc" "unsigned suffix"
run_expect_fail_msg "$ROOT/tests/errors/static/leading_zero.tc" "invalid integer literal"
run_expect_fail_msg "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_expect_fail_msg "$ROOT/tests/errors/static/duplicate_let_var.tc" "duplicate definition"
run_expect_fail_msg "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"
run_expect_fail_msg "$ROOT/tests/errors/static/utf8_bom.tc" "UTF-8 BOM not allowed in source file"
run_expect_fail_msg "$ROOT/tests/errors/static/null_char.tc" "null character (U+0000) not allowed in source"
run_expect_fail_msg "$ROOT/tests/errors/lexical/float_trailing_dot.tc" "invalid float literal"
run_expect_fail_msg "$ROOT/tests/errors/lexical/compare_mode.tc" "compare operations do not accept mode keywords"
run_expect_fail_msg "$ROOT/tests/errors/lexical/memblock_count_only.tc" "expected fill: or memblock elements"
run_expect_fail_msg "$ROOT/tests/errors/lexical/invalid_utf8_comment.tc" "invalid UTF-8 in source"
run_expect_fail_msg "$ROOT/tests/errors/lexical/embedded_nul.tc" "null character (U+0000) not allowed in source"
run_expect_fail_msg "$ROOT/tests/errors/lexical/float_unsigned_suffix.tc" \
    "float literal cannot use unsigned suffix"
run_expect_fail_msg "$ROOT/tests/errors/lexical/negative_unsigned.tc" \
    "negative value cannot use unsigned suffix"
run_expect_fail_msg "$ROOT/tests/errors/static/funcall_arg_expr.tc" "expected operand, memblock constructor, or struct constructor"
run_expect_fail_msg "$ROOT/tests/errors/static/struct_ctor_field_expr.tc" "expected operand, memblock constructor, or struct constructor"
run_expect_fail_msg "$ROOT/tests/errors/static/module_layer_interleave.tc" "declaration out of module layer order"
run_expect_fail_msg "$ROOT/tests/errors/static/bitcast_struct.tc" "bitcast target must be a non-bool integer, float, or ptr type"
run_expect_fail_msg "$ROOT/tests/errors/static/unexpected_char.tc" "unexpected character"
run_expect_fail_msg "$ROOT/tests/errors/static/bitcast_width_mismatch.tc" "bitcast source and target widths must match"
run_expect_fail_msg "$ROOT/tests/errors/static/bitcast_bool_type_mismatch.tc" "bool does not participate in bitcast"
run_expect_fail_msg "$ROOT/tests/errors/static/forward_reference.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/self_reference.tc" "cannot reference itself"
run_expect_fail_msg "$ROOT/tests/errors/static/format_string_error.tc" "invalid format specifier"
run_expect_fail_msg "$ROOT/tests/errors/static/format_specifier_too_long.tc" "format specifier too long"
run_expect_check_fail "$ROOT/tests/errors/static/format_specifier_too_long.tc" "format specifier too long"
run_expect_fail_msg "$ROOT/tests/errors/static/format_specifier_plus_unsigned.tc" \
    "'+' flag not supported for this format specifier"
run_expect_fail_msg "$ROOT/tests/errors/static/format_specifier_hash_bool.tc" \
    "'#' flag not supported for"
run_expect_fail_msg "$ROOT/tests/errors/static/format_specifier_flags_mutex.tc" \
    "'#' flag not supported for this format specifier"
run_expect_fail_msg "$ROOT/tests/errors/static/format_specifier_t_width.tc" \
    "%t does not support"
run_expect_fail_msg "$ROOT/tests/errors/static/format_width_overflow.tc" \
    "format width or precision out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/ptr_store_through_param.tc" \
    "cannot store through read-only pointer binding"
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
run_expect_fail_msg "$ROOT/tests/errors/static/self_ref_let.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/cast_wrap_keyword.tc" "wrap cannot be used with cast"
run_expect_fail_msg "$ROOT/tests/errors/static/format_type_mismatch_uint.tc" "%d requires signed type"
run_expect_fail_msg "$ROOT/tests/errors/static/format_type_mismatch_signed.tc" "%u requires unsigned type"
run_expect_fail_msg "$ROOT/tests/errors/static/format_missing_operand.tc" "unexpected token"
run_expect_fail_msg "$ROOT/tests/errors/static/let_const_literal_range.tc" "literal out of range for context type"
run_expect_fail_msg "$ROOT/tests/errors/static/let_non_literal.tc" "constant expression cannot reference var variable"
run_expect_fail_msg "$ROOT/tests/errors/static/missing_type_in_arith.tc" "expected type"
run_expect_fail_msg "$ROOT/tests/errors/static/invalid_hex_overflow.tc" "integer literal too large"
run_expect_fail_msg "$ROOT/tests/errors/static/format_int_with_t.tc" "%t requires bool type"
run_expect_fail_msg "$ROOT/tests/errors/static/format_fp_type_mismatch.tc" "float type requires float format specifier"

# --check 模式下也应当捕获所有静态错误
run_expect_check_fail "$ROOT/tests/errors/static/uninit_simple.tc" "use of uninitialized variable"
# --- 负移位计数（P0-6）：常量路径 ---
run_expect_check_fail "$ROOT/tests/errors/static/negative_shift_count_const.tc" "negative shift count"
# --- memblock N 规划个数：return / 字段读 / ptr_load 结果位置（P0-3） ---
run_expect_check_fail "$ROOT/tests/errors/static/return_memblock_size_mismatch.tc" "memblock size mismatch"
run_expect_check_fail "$ROOT/tests/errors/static/field_memblock_size_mismatch.tc" "memblock size mismatch"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_load_memblock_size_mismatch.tc" "memblock size mismatch"
# --- 前端小项：负 count / 函数体内可见性 / 前导零分隔符 ---
run_expect_check_fail "$ROOT/tests/errors/static/memblock_negative_count_type.tc" \
    "memblock count must be at least 1"
run_expect_check_fail "$ROOT/tests/errors/static/memblock_negative_count_ctor.tc" \
    "memblock count must be at least 1"
run_expect_check_fail "$ROOT/tests/errors/static/func_body_public_var.tc" \
    "visibility modifier is not allowed inside a function body"
run_expect_check_fail "$ROOT/tests/errors/static/literal_leading_zero_underscore.tc" \
    "invalid integer literal"
# --- goto/label 函数隔离（P0-4） ---
run_expect_check_fail "$ROOT/tests/errors/static/goto_cross_function_label_not_found.tc" \
    "label 'y' not found"
run_expect_check_ok "$ROOT/tests/valid/duplicate_label_across_functions_ok.tc"
# --- 缩进规范（P0-5）：tab / 非 4 倍数 / 跨级跳 ---
run_expect_check_fail "$ROOT/tests/errors/static/indent_tab_only.tc" \
    "mixed spaces and tabs in indentation"
run_expect_check_fail "$ROOT/tests/errors/static/indent_two_spaces.tc" \
    "insufficient indentation in block"
run_expect_check_fail "$ROOT/tests/errors/static/indent_multi_level_jump.tc" \
    "insufficient indentation in block"
run_expect_check_fail "$ROOT/tests/errors/static/uninit_chain.tc" "use of uninitialized variable"
run_expect_check_fail "$ROOT/tests/errors/static/uninit_multi.tc" "use of uninitialized variable"
run_expect_check_fail "$ROOT/tests/errors/static/uninit_slot_value.tc" "use of uninitialized variable"
run_expect_check_fail "$ROOT/tests/errors/static/uninit_if_path.tc" "use of uninitialized variable"
run_expect_check_fail "$ROOT/tests/errors/static/uninit_goto_skip_init.tc" "use of uninitialized variable"
run_expect_check_fail "$ROOT/tests/errors/static/shortcircuit_let_invalid_rhs.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/shortcircuit_let_rhs_type.tc" "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/uninit_shortcircuit_var_lhs.tc" "use of uninitialized variable"
run_expect_check_fail "$ROOT/tests/errors/static/shortcircuit_let_forward_lhs.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/shortcircuit_let_out_of_scope_lhs.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/diag_priority_syntax_before_name.tc" "unexpected token"
run_expect_check_fail "$ROOT/tests/errors/static/diag_priority_name_before_type.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/diag_priority_mode_before_literal.tc" "wrap mode is not allowed for float arithmetic"
run_expect_check_fail "$ROOT/tests/errors/static/diag_priority_const_before_dfa.tc" "constant division by zero"
run_expect_check_fail "$ROOT/tests/errors/static/diag_priority_format_after_operand.tc" \
    "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/goto_undefined.tc" "label 'nonexistent' not found"
run_expect_check_fail "$ROOT/tests/errors/static/goto_inside_loop.tc" \
    "goto is not allowed inside while"
run_expect_check_fail "$ROOT/tests/errors/static/label_inside_loop.tc" \
    "label is not allowed inside while"
run_expect_check_fail "$ROOT/tests/errors/static/label_duplicate.tc" "duplicate label"
run_expect_check_fail "$ROOT/tests/errors/static/goto_into_block.tc" "cannot jump into inner block"
run_expect_check_fail "$ROOT/tests/errors/static/goto_sibling.tc" "cannot jump into incompatible block"
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_expect_check_fail "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_expect_check_fail "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_expect_check_fail "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_expect_check_fail "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_expect_check_fail "$ROOT/tests/errors/static/const_expr.tc" "constant expression cannot reference var variable"
run_expect_check_fail "$ROOT/tests/errors/static/const_cyclic_dep.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/let_nested_call.tc" "nested calls are not allowed in constant expression"
run_expect_check_fail "$ROOT/tests/errors/static/let_short_circuit_invalid_rhs.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/const_overflow.tc" "constant overflow"
run_expect_check_fail "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"
run_expect_check_fail "$ROOT/tests/errors/static/utf8_bom.tc" "UTF-8 BOM not allowed in source file"
run_expect_check_fail "$ROOT/tests/errors/static/null_char.tc" "null character (U+0000) not allowed in source"
run_expect_check_fail "$ROOT/tests/errors/lexical/float_trailing_dot.tc" "invalid float literal"
run_expect_check_fail "$ROOT/tests/errors/lexical/compare_mode.tc" "compare operations do not accept mode keywords"
run_expect_check_fail "$ROOT/tests/errors/lexical/memblock_count_only.tc" "expected fill: or memblock elements"
run_expect_check_fail "$ROOT/tests/errors/lexical/invalid_utf8_comment.tc" "invalid UTF-8 in source"
run_expect_check_fail "$ROOT/tests/errors/lexical/embedded_nul.tc" "null character (U+0000) not allowed in source"
run_expect_check_fail "$ROOT/tests/errors/lexical/float_unsigned_suffix.tc" \
    "float literal cannot use unsigned suffix"
run_expect_check_fail "$ROOT/tests/errors/lexical/negative_unsigned.tc" \
    "negative value cannot use unsigned suffix"
run_expect_check_fail "$ROOT/tests/errors/static/funcall_arg_expr.tc" "expected operand, memblock constructor, or struct constructor"
run_expect_check_fail "$ROOT/tests/errors/static/struct_ctor_field_expr.tc" "expected operand, memblock constructor, or struct constructor"
run_expect_check_fail "$ROOT/tests/errors/static/module_layer_interleave.tc" "declaration out of module layer order"
run_expect_check_fail "$ROOT/tests/errors/static/bitcast_struct.tc" "bitcast target must be a non-bool integer, float, or ptr type"
run_expect_check_fail "$ROOT/tests/errors/static/unexpected_char.tc" "unexpected character"
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
run_expect_check_fail "$ROOT/tests/errors/static/self_ref_let.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/cast_wrap_keyword.tc" "wrap cannot be used with cast"
run_expect_check_fail "$ROOT/tests/errors/static/let_const_literal_range.tc" "literal out of range for context type"
run_expect_check_fail "$ROOT/tests/errors/static/let_non_literal.tc" "constant expression cannot reference var variable"
run_expect_check_fail "$ROOT/tests/errors/static/missing_type_in_arith.tc" "expected type"
run_expect_check_fail "$ROOT/tests/errors/static/format_int_with_t.tc" "%t requires bool type"
run_expect_check_fail "$ROOT/tests/errors/static/format_fp_type_mismatch.tc" "float type requires float format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/format_type_mismatch_uint.tc" "%d requires signed type"
run_expect_check_fail "$ROOT/tests/errors/static/format_type_mismatch_signed.tc" "%u requires unsigned type"
run_expect_check_fail "$ROOT/tests/errors/static/invalid_hex_overflow.tc" "integer literal too large"
run_expect_check_fail "$ROOT/tests/errors/static/literal_type_error.tc" \
    "unsigned suffix literal cannot be used in signed context"
run_expect_check_fail "$ROOT/tests/errors/static/bool_literal_type_error.tc" "bool literal requires bool context"
run_expect_check_fail "$ROOT/tests/errors/static/compare_type_mismatch.tc" "literal type does not match context"
run_expect_check_fail "$ROOT/tests/errors/static/compare_type_mismatch_var.tc" \
    "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/logic_type_error.tc" "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/const_div_zero.tc" "constant division by zero"
run_expect_check_fail "$ROOT/tests/errors/static/let_const_cast_overflow.tc" "constant cast overflow"
run_expect_check_fail "$ROOT/tests/errors/static/let_const_cast_overflow_fp.tc" "constant cast overflow"
run_expect_check_fail "$ROOT/tests/errors/static/truncate_in_arith.tc" "truncate cannot be used with arithmetic"
run_expect_check_fail "$ROOT/tests/errors/static/cast_bool_truncate_keyword_error.tc" "truncate requires an integer target narrower than the source"
run_expect_check_fail "$ROOT/tests/errors/static/cast_truncate_bool_source_error.tc" "truncate requires an integer target narrower than the source"
run_expect_check_fail "$ROOT/tests/errors/static/negative_unsigned_literal.tc" "unsigned suffix"
run_expect_check_fail "$ROOT/tests/errors/static/leading_zero.tc" "invalid integer literal"
run_expect_check_fail "$ROOT/tests/errors/static/bitcast_width_mismatch.tc" "bitcast source and target widths must match"
run_expect_check_fail "$ROOT/tests/errors/static/bitcast_bool_type_mismatch.tc" "bool does not participate in bitcast"
run_expect_check_fail "$ROOT/tests/errors/static/forward_reference.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/self_reference.tc" "cannot reference itself"
run_expect_check_fail "$ROOT/tests/errors/static/format_string_error.tc" "invalid format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/format_specifier_plus_unsigned.tc" \
    "'+' flag not supported for this format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/format_specifier_hash_bool.tc" \
    "'#' flag not supported for"
run_expect_check_fail "$ROOT/tests/errors/static/format_specifier_flags_mutex.tc" \
    "'#' flag not supported for this format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/format_specifier_t_width.tc" \
    "%t does not support"
run_expect_check_fail "$ROOT/tests/errors/static/format_width_overflow.tc" \
    "format width or precision out of range"
run_expect_check_fail "$ROOT/tests/errors/static/ptr_store_through_param.tc" \
    "cannot store through read-only pointer binding"
run_expect_check_fail "$ROOT/tests/errors/static/invalid_format_spec_x.tc" "invalid format specifier"
run_expect_check_fail "$ROOT/tests/errors/static/format_operand_count.tc" "operand count error"
run_expect_check_fail "$ROOT/tests/errors/static/duplicate_let_var.tc" "duplicate definition"
run_expect_check_fail "$ROOT/tests/errors/static/keyword_error.tc" "wrap cannot be used with cast"
run_expect_check_fail "$ROOT/tests/errors/static/abs_wrap_error.tc" "abs does not support wrap"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_wrap_on_shr_keyword_error.tc" "wrap cannot be used with shift operations"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_wrap_on_and_keyword_error.tc" "wrap cannot be used with bitwise operations"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_shl_truncate_keyword_error.tc" "truncate cannot be used with shift operations"
run_expect_check_fail "$ROOT/tests/errors/static/const_shift_wrap_mode.tc" "wrap cannot be used with shift operations"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_shift_type_mismatch.tc" "operand type does not match operation type"
run_expect_check_fail "$ROOT/tests/errors/static/bitwise_shl_const_overflow.tc" "constant overflow"

# --- v0.0.25: float static errors ---

run_expect_fail_msg "$ROOT/tests/errors/static/fp_ieee_on_int.tc" "ieee mode is only allowed for float operations"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_wrap_on_compare.tc" "compare operations do not accept mode keywords"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_arith_wrap_mode_mismatch.tc" "wrap mode is not allowed for float arithmetic"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_wrap_arith_mode_mismatch.tc" "wrap mode is not allowed for float arithmetic"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_wrap_mode_mismatch.tc" "float unary operations do not accept mode keywords"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_bitwise_type_error.tc" "expected type"
run_expect_fail_msg "$ROOT/tests/errors/static/fp_literal_range.tc" "literal out of range"
run_expect_fail_msg "$ROOT/tests/errors/static/float32_literal_range.tc" "float literal out of float32 range"
run_expect_check_fail "$ROOT/tests/errors/static/float32_literal_range.tc" "float literal out of float32 range"
run_expect_fail_msg "$ROOT/tests/errors/static/invalid_float_literal.tc" "invalid float literal"
run_expect_check_fail "$ROOT/tests/errors/static/invalid_float_literal.tc" "invalid float literal"
run_expect_check_fail "$ROOT/tests/errors/static/fp_ieee_on_int.tc" "ieee mode is only allowed for float operations"
run_expect_check_fail "$ROOT/tests/errors/static/fp_wrap_on_compare.tc" "compare operations do not accept mode keywords"
run_expect_check_fail "$ROOT/tests/errors/static/fp_arith_wrap_mode_mismatch.tc" "wrap mode is not allowed for float arithmetic"
run_expect_check_fail "$ROOT/tests/errors/static/fp_wrap_arith_mode_mismatch.tc" "wrap mode is not allowed for float arithmetic"
run_expect_check_fail "$ROOT/tests/errors/static/fp_wrap_mode_mismatch.tc" "float unary operations do not accept mode keywords"
run_expect_check_fail "$ROOT/tests/errors/static/fp_bitwise_type_error.tc" "expected type"
run_expect_check_fail "$ROOT/tests/errors/static/fp_literal_range.tc" "literal out of range"

# --- v0.0.24: if / indent static errors ---

run_expect_fail_msg "$ROOT/tests/errors/static/if_cross_block_ref_after_end.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_after_end.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cross_block_ref_else_to_then.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_else_to_then.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cross_block_ref_then_to_else.tc" "undefined variable"
run_expect_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_then_to_else.tc" "undefined variable"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cond_type_arith.tc" "if condition must be bool"
run_expect_check_fail "$ROOT/tests/errors/static/if_cond_type_arith.tc" "if condition must be bool"
run_expect_fail_msg "$ROOT/tests/errors/static/while_cond_type_arith.tc" "while condition must be bool"
run_expect_check_fail "$ROOT/tests/errors/static/while_cond_type_arith.tc" "while condition must be bool"
run_expect_fail_msg "$ROOT/tests/errors/static/if_cond_type_literal.tc" "literal type does not match context"
run_expect_check_fail "$ROOT/tests/errors/static/if_cond_type_literal.tc" "literal type does not match context"
run_expect_fail_msg "$ROOT/tests/errors/static/if_missing_end_eof.tc" "missing end for if statement"
run_expect_check_fail "$ROOT/tests/errors/static/if_missing_end_eof.tc" "missing end for if statement"
run_expect_fail_msg "$ROOT/tests/errors/static/if_missing_end_stmt.tc" "missing end for if statement"
run_expect_check_fail "$ROOT/tests/errors/static/if_missing_end_stmt.tc" "missing end for if statement"
run_expect_fail_msg "$ROOT/tests/errors/static/while_missing_end.tc" "missing end for while statement"
run_expect_check_fail "$ROOT/tests/errors/static/while_missing_end.tc" "missing end for while statement"
run_expect_fail_msg "$ROOT/tests/errors/static/var_missing_initializer.tc" "variable definition requires initializer"
run_expect_check_fail "$ROOT/tests/errors/static/var_missing_initializer.tc" "variable definition requires initializer"
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
-128
41
41
101
1000001
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
true
false
1.500000
1.500000e+00
1.500000E+00
1.5
1.5
3.141593
3.141593e+00
3.141593E+00
3.14159
3.14159
65true3.14159
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
run_expect_stdout "$ROOT/tests/valid/io_extended.tc" "422a2A52101010255ffFF37711111111255
10203040
18446744073709551615ffffffffffffffff
"

run_expect_stdout "$ROOT/tests/valid/let_cast_const.tc" "42
42
255
255
-100
1000
"
run_expect_check_ok "$ROOT/tests/valid/let_cast_const.tc"
run_expect_stdout "$ROOT/tests/valid/compare_unsigned.tc" "true
false
false
true
true
true
true
false
true
true
"
run_expect_check_ok "$ROOT/tests/valid/compare_unsigned.tc"
run_expect_stdout "$ROOT/tests/valid/shift_edge_cases.tc" "42
42
0
0
0
128
64
32768
1
2147483648
"
run_expect_check_ok "$ROOT/tests/valid/shift_edge_cases.tc"
run_expect_stdout "$ROOT/tests/valid/uninitialized_bool.tc" "true
false
false
"
run_expect_check_ok "$ROOT/tests/valid/uninitialized_bool.tc"

# Phase 2: module system errors (strict #program/#lib header)
run_expect_check_fail "$ROOT/tests/errors/module/no_header.tc" "expected #program or #lib"
run_expect_fail_msg "$ROOT/tests/errors/static/module_directive_invalid.tc" "invalid module directive"
run_expect_check_fail "$ROOT/tests/errors/static/module_directive_invalid.tc" "invalid module directive"
run_expect_check_fail "$ROOT/tests/errors/module/missing_visibility.tc" \
    "missing public or private visibility"
run_expect_check_fail "$ROOT/tests/errors/module/program_mode_misuse.tc" \
    "public is not allowed in #program mode"
run_expect_check_fail "$ROOT/tests/errors/module/module_layer.tc" \
    "import must appear before other declarations"
run_expect_check_fail "$ROOT/tests/errors/module/import_not_found.tc" "import module not found"
run_expect_check_fail "$ROOT/tests/errors/module/self_import.tc" "circular import"
run_expect_check_fail "$ROOT/tests/errors/module/import_name_conflict_program.tc" \
    "import name conflicts with a top-level declaration"
run_expect_check_fail "$ROOT/tests/errors/module/import_name_conflict_lib.tc" \
    "import name conflicts with a top-level declaration"
run_expect_check_fail "$ROOT/tests/errors/module/self_in_program.tc" "Self is not allowed in #program"
run_expect_check_fail "$ROOT/tests/errors/module/func_in_program.tc" \
    "func is not allowed in #program mode"
run_expect_check_fail "$ROOT/tests/errors/module/static_in_program.tc" \
    "static is not allowed in #program mode"
run_expect_check_fail "$ROOT/tests/modules/circular_import.tc" "circular import"
run_expect_check_fail "$ROOT/tests/modules/duplicate_import.tc" "duplicate import"
run_expect_check_fail "$ROOT/tests/modules/import_not_lib.tc" "imported module is not #lib"
run_expect_check_ok "$ROOT/tests/modules/import_ok.tc"
# --- 库函数体数据流检查（P0-1：导入库同样执行确定初始化 / MISSING_RETURN） ---
run_expect_check_fail "$ROOT/tests/modules/import_badlib_missing_return.tc" \
    "missing return on reachable path"
run_expect_check_fail "$ROOT/tests/modules/import_badlib_uninit.tc" \
    "use of uninitialized variable"
# --- memblock N 规划个数：funcall 返回值位置（P0-3） ---
run_expect_check_fail "$ROOT/tests/modules/import_mbsize_mismatch.tc" "memblock size mismatch"
# --- 同名形参跨函数 + 首条语句 store（P1：binding 持久化回归） ---
run_expect_stdout "$ROOT/tests/modules/phase5_memblock_param_scope.tc" "0
0
0
"

# --- v0.0.42 TC-Embed: library function checking ---
run_expect_check_ok "$ROOT/tests/vm/embed/ptr_sum.tc"
run_expect_check_ok "$ROOT/tests/vm/embed/ptr_inplace.tc"
run_expect_check_ok "$ROOT/tests/vm/embed/ptr_loop.tc"
run_expect_check_ok "$ROOT/tests/vm/embed/nested_call.tc"
run_expect_check_ok "$ROOT/tests/vm/embed/sum_while.tc"
run_expect_check_ok "$ROOT/tests/vm/embed/increment_static.tc"

# -I module search path
run_include_search() {
    label="$1"
    should_run "$label" || return 0
    log_test "OK $label"
    output="$($BIN -c -I "$ROOT/tests/modules/extra_libs" \
        "$ROOT/tests/modules/include_search_ok.tc" 2>&1)" || {
        fail "expected success with -I: include_search_ok.tc" "include_search_ok.tc"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    }
    pass
}
run_include_search "module -I search path"
run_expect_check_fail "$ROOT/tests/modules/include_search_ok.tc" "import module not found"

# D-15：公开规模上限（-I 路径数）必须用 CLI 文案，不得报 OutOfMemory
run_include_path_limit() {
    label="$1"
    should_run "$label" || return 0
    log_test "CLI $label"
    args=()
    i=0
    while [ "$i" -lt 65 ]; do
        args+=(-I "/tmp/tc-include-limit-$i")
        i=$((i + 1))
    done
    stderr_file="$(mktemp)"
    status=0
    "$BIN" "${args[@]}" -c "$ROOT/tests/valid/example.tc" >/dev/null 2>"$stderr_file" || status=$?
    actual_stderr="$(cat "$stderr_file")"
    rm -f "$stderr_file"
    if [ "$status" -eq 0 ] ||
       ! printf '%s' "$actual_stderr" | grep -Fq "too many -I paths" ||
       printf '%s' "$actual_stderr" | grep -Eqi 'OutOfMemory|memory allocation failed'; then
        fail "expected non-OOM include-path limit: $label" "$label"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '  status=%s stderr=<%s>\n' "$status" "$actual_stderr" >&2
        fi
        return
    fi
    pass
}
run_include_path_limit "cli -I path limit is not OutOfMemory"

run_ambiguous_import() {
    label="$1"
    should_run "$label" || return 0
    log_test "CFL $label"
    output="$($BIN -c \
        -I "$ROOT/tests/modules/ambiguous_a" \
        -I "$ROOT/tests/modules/ambiguous_b" \
        "$ROOT/tests/modules/import_ambiguous.tc" 2>&1)" || true
    if ! printf '%s' "$output" | grep -Fq "import module path is ambiguous"; then
        fail "expected ambiguous import: import_ambiguous.tc" "import_ambiguous.tc"
        if [ "$VERBOSE" -eq 1 ]; then
            printf '%s\n' "$output" >&2
        fi
        return
    fi
    pass
}
run_ambiguous_import "module ambiguous -I import"

echo ""
echo "$PASSED passed, $FAILED failed"

if [ "$FAIL" -ne 0 ]; then
    echo "Failed tests:" >&2
    printf '%s\n' "$FAILED_FILES" | sed '/^$/d' >&2
    exit 1
fi

echo "all vm tests passed"
