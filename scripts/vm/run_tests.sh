#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/build/vm/bin/tc-vm"
FAIL=0

run_expect_ok() {
    file="$1"
    echo "OK  $file"
    "$BIN" "$file"
}

run_expect_fail() {
    file="$1"
    echo "ERR $file"
    if "$BIN" "$file"; then
        echo "expected failure: $file" >&2
        FAIL=1
    fi
}

run_expect_ok "$ROOT/tests/valid/example.tc"
run_expect_ok "$ROOT/tests/valid/signed_overflow.tc"
run_expect_ok "$ROOT/tests/valid/uint8_wrap.tc"

run_expect_fail "$ROOT/tests/errors/runtime/signed_strict_overflow.tc"
run_expect_fail "$ROOT/tests/errors/runtime/div_zero.tc"
run_expect_fail "$ROOT/tests/errors/static/duplicate_def.tc"
run_expect_fail "$ROOT/tests/errors/static/literal_range.tc"
run_expect_fail "$ROOT/tests/errors/static/overflow_mode.tc"

run_repl_expect() {
    input="$1"
    expected="$2"
    label="$3"
    echo "REPL $label"
    output="$(printf '%s\n' "$input" | "$BIN" --repl 2>&1)"
    if ! printf '%s' "$output" | grep -Fq "$expected"; then
        echo "REPL test failed: $label" >&2
        echo "expected substring: $expected" >&2
        echo "got:" >&2
        printf '%s\n' "$output" >&2
        FAIL=1
    fi
}

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

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi

echo "all vm tests passed"
