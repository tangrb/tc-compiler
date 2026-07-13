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

# io_stress: 128 writeln calls outputting 0..127
IO_BASE="$(printf '0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59\n60\n61\n62\n63\n64\n65\n66\n67\n68\n69\n70\n71\n72\n73\n74\n75\n76\n77\n78\n79\n80\n81\n82\n83\n84\n85\n86\n87\n88\n89\n90\n91\n92\n93\n94\n95\n96\n97\n98\n99\n100\n101\n102\n103\n104\n105\n106\n107\n108\n109\n110\n111\n112\n113\n114\n115\n116\n117\n118\n119\n120\n121\n122\n123\n124\n125\n126\n127')"
IO_EXP="${IO_BASE}
"

run_diff_test() {
    file="$1"
    stdin_data="${2:-}"

    echo "DIFF $file"
    vm_out="$(mktemp)"
    aot_c="$(mktemp "${TMPDIR:-/tmp}/tcaot.XXXXXX").c"
    aot_out="$(mktemp)"

    if [ -n "$stdin_data" ]; then
        if ! printf '%s' "$stdin_data" | "$VM_BIN" "$file" >"$vm_out" 2>/dev/null; then
            fail "vm expected success: $file" "$file"
            rm -f "$vm_out" "$aot_c" "$aot_out"
            return
        fi
        if ! printf '%s' "$stdin_data" | "$AOT_BIN" --run -o "$aot_c" "$file" >"$aot_out" 2>/dev/null; then
            fail "aot run failed: $file" "$file"
            rm -f "$vm_out" "$aot_c" "$aot_out" "$aot_c.out"
            return
        fi
    else
        if ! "$VM_BIN" "$file" </dev/null >"$vm_out" 2>/dev/null; then
            fail "vm expected success: $file" "$file"
            rm -f "$vm_out" "$aot_c" "$aot_out"
            return
        fi
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

run_check_ok() {
    file="$1"

    echo "CHECK_OK $file"
    if ! "$VM_BIN" --check "$file" >/dev/null 2>/dev/null; then
        fail "vm --check expected success: $file" "$file"
        return
    fi
    if ! "$AOT_BIN" --check "$file" >/dev/null 2>/dev/null; then
        fail "aot --check expected success: $file" "$file"
        return
    fi
    pass
}

run_check_fail() {
    file="$1"
    msg="$2"

    echo "CHECK_FAIL $file"
    vm_err="$(mktemp)"
    aot_err="$(mktemp)"

    if "$VM_BIN" --check "$file" >/dev/null 2>"$vm_err"; then
        fail "vm --check expected failure: $file" "$file"
        rm -f "$vm_err" "$aot_err"
        return
    fi
    if "$AOT_BIN" --check "$file" >/dev/null 2>"$aot_err"; then
        fail "aot --check expected failure: $file" "$file"
        rm -f "$vm_err" "$aot_err"
        return
    fi
    if ! grep -Fq "$msg" "$vm_err"; then
        fail "vm --check stderr missing '$msg': $file" "$file"
        rm -f "$vm_err" "$aot_err"
        return
    fi
    if ! grep -Fq "$msg" "$aot_err"; then
        fail "aot --check stderr missing '$msg': $file" "$file"
        rm -f "$vm_err" "$aot_err"
        return
    fi
    pass
    rm -f "$vm_err" "$aot_err"
}

run_runtime_fail() {
    file="$1"
    msg="$2"
    stdin_data="${3:-}"

    echo "RUNTIME_FAIL $file"
    vm_err="$(mktemp)"
    aot_err="$(mktemp)"
    aot_c="$(mktemp "${TMPDIR:-/tmp}/tcaot.XXXXXX").c"

    if [ -n "$stdin_data" ]; then
        if printf '%s' "$stdin_data" | "$VM_BIN" "$file" >/dev/null 2>"$vm_err"; then
            fail "vm expected runtime failure: $file" "$file"
            rm -f "$vm_err" "$aot_err" "$aot_c"
            return
        fi
        if printf '%s' "$stdin_data" | "$AOT_BIN" --run -o "$aot_c" "$file" >/dev/null 2>"$aot_err"; then
            fail "aot expected runtime failure: $file" "$file"
            rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
            return
        fi
    else
        if "$VM_BIN" "$file" </dev/null >/dev/null 2>"$vm_err"; then
            fail "vm expected runtime failure: $file" "$file"
            rm -f "$vm_err" "$aot_err" "$aot_c"
            return
        fi
        if "$AOT_BIN" --run -o "$aot_c" "$file" </dev/null >/dev/null 2>"$aot_err"; then
            fail "aot expected runtime failure: $file" "$file"
            rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
            return
        fi
    fi
    if ! grep -Fq "$msg" "$vm_err"; then
        fail "vm runtime stderr missing '$msg': $file" "$file"
        rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
        return
    fi
    if ! grep -Fq "$msg" "$aot_err"; then
        fail "aot runtime stderr missing '$msg': $file" "$file"
        rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
        return
    fi
    pass
    rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
}

# --- differential tests: valid programs (stdout VM vs AOT) ---

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
run_diff_test "$ROOT/tests/valid/signed_wrap.tc"
run_diff_test "$ROOT/tests/valid/abs_neg_signed.tc"
run_diff_test "$ROOT/tests/valid/unary_wrap.tc"
run_diff_test "$ROOT/tests/valid/format_output.tc"
run_diff_test "$ROOT/tests/valid/format_hex_bin.tc"
run_diff_test "$ROOT/tests/valid/uninit_slot_value.tc"
run_diff_test "$ROOT/tests/valid/bool_var.tc"
run_diff_test "$ROOT/tests/valid/compare_ops.tc"
run_diff_test "$ROOT/tests/valid/logic_ops.tc"
run_diff_test "$ROOT/tests/valid/bitwise_runtime.tc"
run_diff_test "$ROOT/tests/valid/bitwise_and_or_xor_not_valid.tc"
run_diff_test "$ROOT/tests/valid/bitwise_shift_shl_shr_valid.tc"
run_diff_test "$ROOT/tests/valid/bitwise_shl_wrap_valid.tc"
run_diff_test "$ROOT/tests/valid/bitwise_shift_k_ge_n_valid.tc"
run_diff_test "$ROOT/tests/valid/bitwise_let_const_valid.tc"
run_diff_test "$ROOT/tests/valid/bitwise_io_format_valid.tc"
run_diff_test "$ROOT/tests/valid/bool_cast.tc"
run_diff_test "$ROOT/tests/valid/format_bool.tc"
run_diff_test "$ROOT/tests/valid/const_expr.tc"
run_diff_test "$ROOT/tests/valid/let_bool_constant.tc"
run_diff_test "$ROOT/tests/valid/let_logic_short_circuit.tc"
run_diff_test "$ROOT/tests/valid/format_spec_i.tc"
run_diff_test "$ROOT/tests/valid/if_basic.tc"
run_diff_test "$ROOT/tests/valid/if_else.tc"
run_diff_test "$ROOT/tests/valid/if_nested.tc"
run_diff_test "$ROOT/tests/valid/if_chain.tc"
run_diff_test "$ROOT/tests/valid/if_bool_literal.tc"
run_diff_test "$ROOT/tests/valid/if_local_same_name.tc"
run_diff_test "$ROOT/tests/valid/if_shadow_global.tc"
run_diff_test "$ROOT/tests/valid/if_false_skip_nested_then.tc"
run_diff_test "$ROOT/tests/valid/arithmetic_all_types.tc"
run_diff_test "$ROOT/tests/valid/all_type_boundaries.tc"
run_diff_test "$ROOT/tests/valid/literal_edge_cases.tc"
run_diff_test "$ROOT/tests/valid/unary_all_types.tc"
run_diff_test "$ROOT/tests/valid/format_spec_all.tc"
run_diff_test "$ROOT/tests/valid/wrap_arithmetic_all.tc"
run_diff_test "$ROOT/tests/valid/div_mod_all_signed.tc"
run_diff_test "$ROOT/tests/valid/cast_operations_all.tc"
run_diff_test "$ROOT/tests/valid/let_constant_all_types.tc"
run_diff_test "$ROOT/tests/valid/complex_expressions.tc"
run_diff_test "$ROOT/tests/valid/bin_hex_oct_io.tc"
run_diff_test "$ROOT/tests/valid/unary_wrap_unsigned.tc"
run_diff_test "$ROOT/tests/valid/io_extended.tc"
run_diff_test "$ROOT/tests/valid/fp_basic.tc"
run_diff_test "$ROOT/tests/valid/fp_arith.tc"
run_diff_test "$ROOT/tests/valid/fp_arith_ieee.tc"
run_diff_test "$ROOT/tests/valid/fp_arith_wrap.tc"
run_diff_test "$ROOT/tests/valid/fp_compare.tc"
run_diff_test "$ROOT/tests/valid/fp_cast.tc"
run_diff_test "$ROOT/tests/valid/fp_cast_truncate.tc"
run_diff_test "$ROOT/tests/valid/fp_io.tc" "3.14
"
run_diff_test "$ROOT/tests/valid/fp_const_expr.tc"
run_diff_test "$ROOT/tests/valid/fp_if_block.tc"
run_diff_test "$ROOT/tests/valid/format_spec_fp.tc"
run_diff_test "$ROOT/tests/valid/var_no_init.tc"
run_diff_test "$ROOT/tests/valid/assign_uninit_var_valid.tc"
run_diff_test "$ROOT/tests/valid/no_warn_after_assign.tc"
run_diff_test "$ROOT/tests/valid/uninitialized.tc"
run_diff_test "$ROOT/tests/valid/uninit_chain_warning.tc"
run_diff_test "$ROOT/tests/valid/more_warning_cases.tc"
run_diff_test "$ROOT/tests/valid/read_write.tc" "42
"
run_diff_test "$ROOT/tests/valid/read_bool.tc" "true
"
run_diff_test "$ROOT/tests/valid/read_int8.tc" "42
"
run_diff_test "$ROOT/tests/valid/read_int8.tc" "-128
"

# --- stress (stdout parity) ---

run_diff_test "$ROOT/tests/stress/massive_vars.tc"
run_diff_test "$ROOT/tests/stress/many_operations.tc"
run_diff_test "$ROOT/tests/stress/deep_recursion.tc"
run_diff_test "$ROOT/tests/stress/let_chain.tc"
run_diff_test "$ROOT/tests/stress/io_stress.tc" "$IO_EXP"
run_diff_test "$ROOT/tests/stress/many_vars_stress.tc"
run_diff_test "$ROOT/tests/stress/stress_if_nested.tc"
run_diff_test "$ROOT/tests/stress/type_combinatorial.tc"
run_diff_test "$ROOT/tests/stress/stress_fp_chain.tc"

# --- static analysis (--check VM vs AOT) ---

run_check_ok "$ROOT/tests/valid/example.tc"
run_check_ok "$ROOT/tests/valid/let_constant.tc"
run_check_ok "$ROOT/tests/valid/if_basic.tc"
run_check_ok "$ROOT/tests/valid/if_nested.tc"
run_check_ok "$ROOT/tests/valid/bitwise_runtime.tc"
run_check_ok "$ROOT/tests/valid/const_expr.tc"
run_check_ok "$ROOT/tests/valid/fp_basic.tc"
run_check_ok "$ROOT/tests/valid/fp_arith.tc"
run_check_ok "$ROOT/tests/valid/fp_arith_ieee.tc"
run_check_ok "$ROOT/tests/valid/fp_arith_wrap.tc"
run_check_ok "$ROOT/tests/valid/fp_compare.tc"
run_check_ok "$ROOT/tests/valid/fp_cast.tc"
run_check_ok "$ROOT/tests/valid/fp_cast_truncate.tc"
run_check_ok "$ROOT/tests/valid/fp_io.tc"
run_check_ok "$ROOT/tests/valid/fp_const_expr.tc"
run_check_ok "$ROOT/tests/valid/fp_if_block.tc"
run_check_ok "$ROOT/tests/valid/format_spec_fp.tc"

run_check_fail "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"
run_check_fail "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_check_fail "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_check_fail "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_check_fail "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_check_fail "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_check_fail "$ROOT/tests/errors/static/const_expr.tc" "constant expression cannot reference var variable"
run_check_fail "$ROOT/tests/errors/static/const_cyclic_dep.tc" "circular dependency in constant expression"
run_check_fail "$ROOT/tests/errors/static/const_overflow.tc" "constant overflow"
run_check_fail "$ROOT/tests/errors/static/const_div_zero.tc" "constant division by zero"
run_check_fail "$ROOT/tests/errors/static/compare_type_mismatch.tc" "literal type does not match context"
run_check_fail "$ROOT/tests/errors/static/logic_type_error.tc" "operand type does not match operation type"
run_check_fail "$ROOT/tests/errors/static/format_string_error.tc" "invalid format specifier"
run_check_fail "$ROOT/tests/errors/static/format_type_mismatch_signed.tc" "%u requires unsigned type"
run_check_fail "$ROOT/tests/errors/static/cast_wrap_keyword.tc" "wrap cannot be used with cast"
run_check_fail "$ROOT/tests/errors/static/forward_reference.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/bitwise_xor_bool_type_error.tc" "bitwise operation requires integer type"
run_check_fail "$ROOT/tests/errors/static/bitwise_wrap_on_shr_keyword_error.tc" "wrap cannot be used with shift operations"
run_check_fail "$ROOT/tests/errors/static/bitwise_shl_const_overflow.tc" "constant overflow"
run_check_fail "$ROOT/tests/errors/static/if_cond_type_arith.tc" "if condition must be bool"
run_check_fail "$ROOT/tests/errors/static/if_cond_type_literal.tc" "literal type does not match context"
run_check_fail "$ROOT/tests/errors/static/if_missing_end_eof.tc" "missing end for if statement"
run_check_fail "$ROOT/tests/errors/static/indent_mixed_tab_body.tc" "mixed spaces and tabs in indentation"
run_check_fail "$ROOT/tests/errors/static/indent_insufficient_then.tc" "insufficient indentation in block"
run_check_fail "$ROOT/tests/errors/static/indent_else_mismatch.tc" "else indentation does not match if"
run_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_after_end.tc" "cross-block reference"
run_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_then_to_else.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/assign_to_let.tc" "cannot assign to constant"
run_check_fail "$ROOT/tests/errors/static/self_ref_let.tc" "circular dependency"
run_check_fail "$ROOT/tests/errors/static/let_const_literal_range.tc" "invalid literal in constant expression"
run_check_fail "$ROOT/tests/errors/static/bool_literal_type_error.tc" "literal type does not match variable type"
run_check_fail "$ROOT/tests/errors/static/truncate_in_arith.tc" "truncate cannot be used with arithmetic"
run_check_fail "$ROOT/tests/errors/static/format_operand_count.tc" "operand count error"
run_check_fail "$ROOT/tests/errors/static/fp_mod_type_error.tc" "mod not supported for float types"
run_check_fail "$ROOT/tests/errors/static/fp_ieee_on_int.tc" "ieee mode is only allowed for float operations"
run_check_fail "$ROOT/tests/errors/static/fp_wrap_on_compare.tc" "wrap mode is not allowed for float comparison"
run_check_fail "$ROOT/tests/errors/static/fp_bitwise_type_error.tc" "bitwise operation requires integer type"
run_check_fail "$ROOT/tests/errors/static/fp_literal_range.tc" "literal out of range"
run_check_fail "$ROOT/tests/errors/static/fp_const_ieee_forbidden.tc" "ieee/wrap is not allowed in constant expression"

# --- runtime errors (VM vs AOT --run) ---

run_runtime_fail "$ROOT/tests/errors/runtime/signed_strict_overflow.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/signed_strict_mul.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/neg_int_min.tc" "neg(INT_MIN) overflow"
run_runtime_fail "$ROOT/tests/errors/runtime/abs_int_min.tc" "abs(INT_MIN) overflow"
run_runtime_fail "$ROOT/tests/errors/runtime/div_zero.tc" "division by zero"
run_runtime_fail "$ROOT/tests/errors/runtime/mod_zero.tc" "division by zero"
run_runtime_fail "$ROOT/tests/errors/runtime/cast_strict_overflow.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/read_invalid.tc" "unexpected end of input"
run_runtime_fail "$ROOT/tests/errors/runtime/read_invalid_input.tc" "invalid input" "abc
"
run_runtime_fail "$ROOT/tests/errors/runtime/read_out_of_range.tc" "input value out of range" "999
"
run_runtime_fail "$ROOT/tests/errors/runtime/read_bool_invalid_input.tc" "invalid input" "trueish
"
run_runtime_fail "$ROOT/tests/errors/runtime/read_bool_invalid_input.tc" "invalid input" "falsehood
"
run_runtime_fail "$ROOT/tests/errors/runtime/div_zero_int16.tc" "division by zero"
run_runtime_fail "$ROOT/tests/errors/runtime/signed_strict_overflow_int32.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/signed_strict_mul_overflow_int32.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/cast_strict_overflow_int32_to_int16.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/bitwise_shl_overflow_runtime.tc" "shift left overflow"
run_runtime_fail "$ROOT/tests/errors/runtime/int64_min_div.tc" "signed division overflow"
run_runtime_fail "$ROOT/tests/errors/runtime/read_out_of_range_int64.tc" "input value out of range" "99999999999999999999
"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_strict_overflow.tc" "float overflow"
run_diff_test "$ROOT/tests/errors/runtime/fp_strict_underflow.tc"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_strict_invalid.tc" "float invalid operation"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_cast_overflow.tc" "float cast overflow"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_div_zero.tc" "division by zero"
run_runtime_fail "$ROOT/tests/errors/runtime/read_fp_invalid.tc" "invalid input" "abc
"

echo ""
echo "$PASSED passed, $FAILED failed"

if [ "$FAIL" -ne 0 ]; then
    echo "Failed tests:" >&2
    printf '%s\n' "$FAILED_FILES" | sed '/^$/d' >&2
    exit 1
fi

echo "all aot differential tests passed"
