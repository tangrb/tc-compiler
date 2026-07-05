#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VM_BIN="$ROOT/build/vm/bin/tc-vm"
AOT_BIN="$ROOT/build/aot/bin/tc-aot"
FAIL=0
PASSED=0
FAILED=0
FAILED_FILES=""

fail() {
    echo "$1" >&2
    FAIL=1
    FAILED=$((FAILED + 1))
    FAILED_FILES="${FAILED_FILES}
${2:-}"
}

pass() {
    PASSED=$((PASSED + 1))
}

if [ ! -x "$VM_BIN" ]; then
    echo "error: tc-vm not found at $VM_BIN (run: make vm)" >&2
    exit 1
fi
if [ ! -x "$AOT_BIN" ]; then
    echo "error: tc-aot not found at $AOT_BIN (run: make aot)" >&2
    exit 1
fi

run_diff_test() {
    file="$1"
    stdin_data="${2:-}"
    label="$file"

    echo "DIFF $file"
    vm_out="$(mktemp)"
    aot_c="$(mktemp "${TMPDIR:-/tmp}/tcaot.XXXXXX.c")"
    aot_out="$(mktemp)"

    if [ -n "$stdin_data" ]; then
        if ! printf '%s' "$stdin_data" | "$VM_BIN" "$file" >"$vm_out" 2>/dev/null; then
            fail "vm expected success: $file" "$file"
            rm -f "$vm_out" "$aot_c" "$aot_out"
            return
        fi
    else
        if ! "$VM_BIN" "$file" </dev/null >"$vm_out" 2>/dev/null; then
            fail "vm expected success: $file" "$file"
            rm -f "$vm_out" "$aot_c" "$aot_out"
            return
        fi
    fi

    if [ -n "$stdin_data" ]; then
        if ! printf '%s' "$stdin_data" | "$AOT_BIN" --run -o "$aot_c" "$file" >"$aot_out" 2>/dev/null; then
            fail "aot run failed: $file" "$file"
            rm -f "$vm_out" "$aot_c" "$aot_out" "$aot_c.out"
            return
        fi
    else
        if ! "$AOT_BIN" --run -o "$aot_c" "$file" >"$aot_out" 2>/dev/null; then
            fail "aot run failed: $file" "$file"
            rm -f "$vm_out" "$aot_c" "$aot_out" "$aot_c.out"
            return
        fi
    fi

    if ! cmp -s "$vm_out" "$aot_out"; then
        fail "vm/aot stdout mismatch: $file" "$file"
        if command -v diff >/dev/null 2>&1; then
            diff -u "$vm_out" "$aot_out" >&2 || true
        fi
    else
        pass
    fi

    rm -f "$vm_out" "$aot_c" "$aot_out" "$aot_c.out"
}

# --- differential tests: valid programs with deterministic stdout ---

run_diff_test "$ROOT/tests/valid/example.tc"
run_diff_test "$ROOT/tests/valid/wrap_int8_output.tc"
run_diff_test "$ROOT/tests/valid/wrap_uint8_output.tc"
run_diff_test "$ROOT/tests/valid/write_int8_number.tc"
run_diff_test "$ROOT/tests/valid/truncate_cast.tc"
run_diff_test "$ROOT/tests/valid/div_mod_signed.tc"
run_diff_test "$ROOT/tests/valid/int64_min.tc"
run_diff_test "$ROOT/tests/valid/mod_int_min_neg_one.tc"
run_diff_test "$ROOT/tests/valid/hex_literal.tc"
run_diff_test "$ROOT/tests/valid/bin_literal.tc"
run_diff_test "$ROOT/tests/valid/oct_literal.tc"
run_diff_test "$ROOT/tests/valid/literal_separator.tc"
run_diff_test "$ROOT/tests/valid/strict_cast_widen.tc"
run_diff_test "$ROOT/tests/valid/let_constant.tc"
run_diff_test "$ROOT/tests/valid/uint8_wrap.tc"
run_diff_test "$ROOT/tests/valid/wrap_sub_mul.tc"
run_diff_test "$ROOT/tests/valid/comments_semicolon.tc"
run_diff_test "$ROOT/tests/valid/write_no_newline.tc"
run_diff_test "$ROOT/tests/valid/sign_extend_cast.tc"
run_diff_test "$ROOT/tests/valid/semicolon_inline_comment.tc"
# int64_min_div.tc 已移至 errors/runtime（INT64_MIN / -1 是溢出错误）
# 不再作为 AOT 差分测试
run_diff_test "$ROOT/tests/valid/signed_wrap.tc"
run_diff_test "$ROOT/tests/valid/read_write.tc" "42
"
run_diff_test "$ROOT/tests/valid/uninit_slot_value.tc"

echo ""
echo "$PASSED passed, $FAILED failed"

if [ "$FAIL" -ne 0 ]; then
    echo "Failed tests:" >&2
    printf '%s\n' "$FAILED_FILES" | sed '/^$/d' >&2
    exit 1
fi

echo "all aot differential tests passed"
