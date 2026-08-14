#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VM_BIN="${TC_VM_BIN:-$ROOT/build/vm/bin/tc-vm}"
AOT_BIN="${TC_AOT_BIN:-$ROOT/build/aot/bin/tc-aot}"
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
    vm_status=0
    aot_status=0

    if [ -n "$stdin_data" ]; then
        if printf '%s' "$stdin_data" | "$VM_BIN" "$file" >/dev/null 2>"$vm_err"; then
            vm_status=0
        else
            vm_status=$?
        fi
        if printf '%s' "$stdin_data" | "$AOT_BIN" --run -o "$aot_c" "$file" >/dev/null 2>"$aot_err"; then
            aot_status=0
        else
            aot_status=$?
        fi
    else
        if "$VM_BIN" "$file" </dev/null >/dev/null 2>"$vm_err"; then
            vm_status=0
        else
            vm_status=$?
        fi
        if "$AOT_BIN" --run -o "$aot_c" "$file" </dev/null >/dev/null 2>"$aot_err"; then
            aot_status=0
        else
            aot_status=$?
        fi
    fi
    if [ "$vm_status" -eq 0 ]; then
        fail "vm expected runtime failure: $file" "$file"
        rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
        return
    fi
    if [ "$aot_status" -eq 0 ]; then
        fail "aot expected runtime failure: $file" "$file"
        rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
        return
    fi
    if [ "$vm_status" -ne "$aot_status" ]; then
        fail "vm/aot runtime exit status mismatch ($vm_status != $aot_status): $file" "$file"
        rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
        return
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
    vm_diag="$(sed -n '/: error:/{p;q;}' "$vm_err")"
    aot_diag="$(sed -n '/: error:/{p;q;}' "$aot_err")"
    if [ -z "$vm_diag" ] || [ "$vm_diag" != "$aot_diag" ]; then
        fail "vm/aot runtime diagnostic mismatch: $file" "$file"
        if command -v diff >/dev/null 2>&1; then
            diff -u "$vm_err" "$aot_err" >&2 || true
        fi
        rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
        return
    fi
    pass
    rm -f "$vm_err" "$aot_err" "$aot_c" "$aot_c.out"
}

run_codegen_contains() {
    file="$1"
    pattern="$2"
    aot_c="$(mktemp "${TMPDIR:-/tmp}/tcaot.XXXXXX").c"

    echo "CODEGEN $file"
    if ! "$AOT_BIN" -o "$aot_c" "$file" >/dev/null 2>/dev/null; then
        fail "aot codegen failed: $file" "$file"
    elif ! grep -Fq "$pattern" "$aot_c"; then
        fail "aot codegen missing canonical bit pattern '$pattern': $file" "$file"
    else
        pass
    fi
    rm -f "$aot_c"
}

run_codegen_not_contains() {
    file="$1"
    pattern="$2"
    aot_c="$(mktemp "${TMPDIR:-/tmp}/tcaot.XXXXXX").c"

    echo "CODEGEN_NO $file"
    if ! "$AOT_BIN" -o "$aot_c" "$file" >/dev/null 2>/dev/null; then
        fail "aot codegen failed: $file" "$file"
    elif grep -Fq "$pattern" "$aot_c"; then
        fail "aot codegen unexpectedly contains '$pattern': $file" "$file"
    else
        pass
    fi
    rm -f "$aot_c"
}

run_aot_cli_golden() {
    argument="$1"
    expected_status="$2"
    expected_stdout="$3"
    expected_stderr="$4"
    label="$5"
    stdout_file="$(mktemp)"
    stderr_file="$(mktemp)"
    status=0

    echo "CLI $label"
    "$AOT_BIN" "$argument" >"$stdout_file" 2>"$stderr_file" || status=$?
    actual_stdout="$(cat "$stdout_file")"
    actual_stderr="$(cat "$stderr_file")"
    rm -f "$stdout_file" "$stderr_file"

    if [ "$status" -ne "$expected_status" ] ||
       [ "$actual_stdout" != "$expected_stdout" ] ||
       [ "$actual_stderr" != "$expected_stderr" ]; then
        fail "aot CLI golden failed: $label" "$label"
        return
    fi
    pass
}

# --- differential tests: valid programs (stdout VM vs AOT) ---

run_aot_cli_golden "--version" 0 "tc-aot 0.0.37" "" "aot version golden"

run_aot_cli_golden "--help" 0 "" "Usage: $AOT_BIN [options] <file.tc>

TC ahead-of-time compiler (TC → C99).

Options:
  -o, --output FILE      write generated C to FILE (default: <input>.c)
  -H, --header FILE      write embed header to FILE (requires --embed)
  -c, --check            static analysis only, do not emit C
  -r, --run              compile and run generated C (requires host C compiler)
  -I, --include <path>   add module search path (repeatable)
  -e, --embed            embed library mode (no main(), public symbols + func table)
  -h, --help             show this help
  -V, --version          show version

Notes:
  --check uses the same libtc batch-language acceptance set as tc-vm --check." "aot help golden"

AOT_MISSING_PATH="$(mktemp "${TMPDIR:-/tmp}/tc-aot-missing.XXXXXX")"
rm -f "$AOT_MISSING_PATH"
run_aot_cli_golden "$AOT_MISSING_PATH" 1 "" "$AOT_MISSING_PATH: api error: FileOpen: cannot open input file" "aot file-open golden"

# D-15：AOT 公开 -I 上限不得报 OutOfMemory
{
    echo "CLI aot -I path limit is not OutOfMemory"
    args=()
    i=0
    while [ "$i" -lt 65 ]; do
        args+=(-I "/tmp/tc-aot-include-limit-$i")
        i=$((i + 1))
    done
    stderr_file="$(mktemp)"
    status=0
    "$AOT_BIN" "${args[@]}" --check "$ROOT/tests/valid/example.tc" >/dev/null 2>"$stderr_file" || status=$?
    actual_stderr="$(cat "$stderr_file")"
    rm -f "$stderr_file"
    if [ "$status" -eq 0 ] ||
       ! printf '%s' "$actual_stderr" | grep -Fq "too many -I paths" ||
       printf '%s' "$actual_stderr" | grep -Eqi 'OutOfMemory|memory allocation failed'; then
        fail "expected non-OOM include-path limit" "aot -I path limit"
    else
        pass
    fi
}

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
run_diff_test "$ROOT/tests/valid/uninit_both_paths.tc"
run_diff_test "$ROOT/tests/valid/uninit_shortcircuit.tc"
run_diff_test "$ROOT/tests/valid/uninit_shortcircuit_let_bool.tc"
run_diff_test "$ROOT/tests/valid/uninit_const_condition_if.tc"
run_diff_test "$ROOT/tests/valid/uninit_const_condition_while.tc"
run_diff_test "$ROOT/tests/valid/while_false.tc"
run_diff_test "$ROOT/tests/valid/while_counted.tc"
run_diff_test "$ROOT/tests/valid/while_nested.tc"
run_diff_test "$ROOT/tests/valid/while_break_continue.tc"
run_diff_test "$ROOT/tests/valid/while_var_reinitialize.tc"
run_diff_test "$ROOT/tests/valid/if_else.tc"
run_diff_test "$ROOT/tests/valid/if_nested.tc"
run_diff_test "$ROOT/tests/valid/if_chain.tc"
run_diff_test "$ROOT/tests/valid/if_bool_literal.tc"
run_diff_test "$ROOT/tests/valid/if_local_same_name.tc"
run_diff_test "$ROOT/tests/valid/if_shadow_global.tc"
run_diff_test "$ROOT/tests/valid/phase5_memblock_deepcopy.tc"
run_diff_test "$ROOT/tests/valid/if_false_skip_nested_then.tc"
run_diff_test "$ROOT/tests/valid/if_and_or_condition.tc"
run_diff_test "$ROOT/tests/valid/if_comparison_condition.tc"
run_diff_test "$ROOT/tests/valid/if_not_condition.tc"
run_diff_test "$ROOT/tests/valid/if_empty_body.tc"
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
run_diff_test "$ROOT/tests/valid/fp_compare.tc"
run_diff_test "$ROOT/tests/valid/fp_cast.tc"
run_diff_test "$ROOT/tests/valid/fp_bitcast_roundtrip.tc"
run_diff_test "$ROOT/tests/valid/bitcast_roundtrip32.tc"
run_diff_test "$ROOT/tests/valid/bitcast_roundtrip64.tc"
run_diff_test "$ROOT/tests/valid/let_runtime_equivalence.tc"
run_diff_test "$ROOT/tests/valid/let_wrap_allowed.tc"
run_diff_test "$ROOT/tests/valid/let_float_ieee.tc"
run_diff_test "$ROOT/tests/valid/let_float32_step_rounding.tc"
run_diff_test "$ROOT/tests/valid/let_bitcast_payload.tc"
run_diff_test "$ROOT/tests/valid/let_block_local_chain.tc"
run_codegen_contains "$ROOT/tests/valid/let_float32_step_rounding.tc" \
    "0x000000004b800000ULL"
run_codegen_contains "$ROOT/tests/valid/let_bitcast_payload.tc" \
    "0x7ff8000000001234ULL"
run_codegen_not_contains "$ROOT/tests/valid/let_constant.tc" "slots["
run_codegen_contains "$ROOT/tests/valid/write_int8_number.tc" "if (tc_aot_write("
run_diff_test "$ROOT/tests/valid/cast_literal.tc"
run_diff_test "$ROOT/tests/valid/fp_io.tc" "3.14
"
run_diff_test "$ROOT/tests/valid/fp_io.tc" "nan
"
run_diff_test "$ROOT/tests/valid/fp_io.tc" "-inf
"
run_diff_test "$ROOT/tests/valid/fp_const_expr.tc"
run_diff_test "$ROOT/tests/valid/fp_if_block.tc"
run_diff_test "$ROOT/tests/valid/format_spec_fp.tc"
run_diff_test "$ROOT/tests/valid/fp_neg_abs.tc"
run_diff_test "$ROOT/tests/valid/fp_const_let_arith.tc"
run_diff_test "$ROOT/tests/valid/fp_ieee_ops.tc"
run_diff_test "$ROOT/tests/valid/fp_exact_subnormal.tc"
run_diff_test "$ROOT/tests/valid/assign_uninit_var_valid.tc"
run_diff_test "$ROOT/tests/valid/no_warn_after_assign.tc"
run_diff_test "$ROOT/tests/valid/read_write.tc" "42
"
run_diff_test "$ROOT/tests/valid/read_bool.tc" "true
"
run_diff_test "$ROOT/tests/valid/read_int8.tc" "42
"
run_diff_test "$ROOT/tests/valid/read_int8.tc" "-128
"
run_diff_test "$ROOT/tests/valid/let_cast_const.tc"
run_diff_test "$ROOT/tests/valid/compare_unsigned.tc"
run_diff_test "$ROOT/tests/valid/shift_edge_cases.tc"
run_diff_test "$ROOT/tests/valid/uninitialized_bool.tc"

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
run_diff_test "$ROOT/tests/stress/stress_many_ifs.tc"

# --- Phase 5: Executor/AOT (funcall, ptr, memblock) ---
run_diff_test "$ROOT/tests/valid/phase5_funcall_return.tc"
run_diff_test "$ROOT/tests/valid/phase5_ptr_basic.tc"
run_diff_test "$ROOT/tests/valid/phase5_ptr_cast.tc"
run_diff_test "$ROOT/tests/valid/phase5_ptr_cast_nullptr.tc"
run_diff_test "$ROOT/tests/valid/phase5_ptr_bitcast.tc"
run_diff_test "$ROOT/tests/valid/phase5_ptr_scope_outer.tc"
run_diff_test "$ROOT/tests/valid/phase5_memblock_basic.tc"
run_diff_test "$ROOT/tests/valid/phase5_ptr_arith_cmp.tc"
run_diff_test "$ROOT/tests/valid/phase5_memblock_copy.tc"
run_diff_test "$ROOT/tests/valid/phase5_void_funcall.tc"
run_diff_test "$ROOT/tests/valid/phase5_static_var.tc"
run_diff_test "$ROOT/tests/valid/phase5_self_static_let.tc"
run_diff_test "$ROOT/tests/valid/phase5_self_static_ops.tc"
run_diff_test "$ROOT/tests/valid/phase5_ptr_cmp_more.tc"
run_diff_test "$ROOT/tests/valid/phase5_nullptr_eq.tc"
run_diff_test "$ROOT/tests/valid/phase5_memblock_fill.tc"
run_diff_test "$ROOT/tests/valid/phase5_nested_funcall.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_basic.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_field_assign.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_nested.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_copy.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_padding.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_nested_assign.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_whole_assign.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_mixed_types.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_extract_indep.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_funcall.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_memblock.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_multi_field.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_ptr_field.tc"
run_diff_test "$ROOT/tests/valid/phase5_struct_mut_matrix_ok.tc"
run_diff_test "$ROOT/tests/valid/isize_arith.tc"
run_diff_test "$ROOT/tests/valid/usize_arith.tc"

# --- static analysis (--check VM vs AOT) ---

run_check_ok "$ROOT/tests/valid/example.tc"
run_check_ok "$ROOT/tests/valid/let_constant.tc"
run_check_ok "$ROOT/tests/valid/if_basic.tc"
run_check_ok "$ROOT/tests/valid/uninit_both_paths.tc"
run_check_ok "$ROOT/tests/valid/uninit_shortcircuit.tc"
run_check_ok "$ROOT/tests/valid/uninit_shortcircuit_let_bool.tc"
run_check_ok "$ROOT/tests/valid/uninit_const_condition_if.tc"
run_check_ok "$ROOT/tests/valid/uninit_const_condition_while.tc"
run_check_ok "$ROOT/tests/valid/while_false.tc"
run_check_ok "$ROOT/tests/valid/while_counted.tc"
run_check_ok "$ROOT/tests/valid/while_nested.tc"
run_check_ok "$ROOT/tests/valid/while_break_continue.tc"
run_check_ok "$ROOT/tests/valid/while_var_reinitialize.tc"
run_check_ok "$ROOT/tests/valid/if_nested.tc"
run_check_ok "$ROOT/tests/valid/bitwise_runtime.tc"
run_check_ok "$ROOT/tests/valid/const_expr.tc"
run_check_ok "$ROOT/tests/valid/fp_basic.tc"
run_check_ok "$ROOT/tests/valid/fp_arith.tc"
run_check_ok "$ROOT/tests/valid/fp_arith_ieee.tc"
run_check_ok "$ROOT/tests/valid/fp_compare.tc"
run_check_ok "$ROOT/tests/valid/fp_cast.tc"
run_check_ok "$ROOT/tests/valid/fp_bitcast_roundtrip.tc"
run_check_ok "$ROOT/tests/valid/bitcast_roundtrip32.tc"
run_check_ok "$ROOT/tests/valid/bitcast_roundtrip64.tc"
run_check_ok "$ROOT/tests/valid/let_runtime_equivalence.tc"
run_check_ok "$ROOT/tests/valid/let_wrap_allowed.tc"
run_check_ok "$ROOT/tests/valid/let_float_ieee.tc"
run_check_ok "$ROOT/tests/valid/let_float32_step_rounding.tc"
run_check_ok "$ROOT/tests/valid/let_bitcast_payload.tc"
run_check_ok "$ROOT/tests/valid/let_block_local_chain.tc"
run_check_ok "$ROOT/tests/valid/cast_literal.tc"
run_check_ok "$ROOT/tests/valid/fp_io.tc"
run_check_ok "$ROOT/tests/valid/fp_const_expr.tc"
run_check_ok "$ROOT/tests/valid/fp_if_block.tc"
run_check_ok "$ROOT/tests/valid/format_spec_fp.tc"
run_check_ok "$ROOT/tests/valid/fp_neg_abs.tc"
run_check_ok "$ROOT/tests/valid/fp_const_let_arith.tc"
run_check_ok "$ROOT/tests/valid/fp_ieee_ops.tc"
run_check_ok "$ROOT/tests/valid/fp_exact_subnormal.tc"
run_check_ok "$ROOT/tests/valid/if_and_or_condition.tc"
run_check_ok "$ROOT/tests/valid/if_comparison_condition.tc"
run_check_ok "$ROOT/tests/valid/if_not_condition.tc"
run_check_ok "$ROOT/tests/valid/if_empty_body.tc"
run_check_ok "$ROOT/tests/valid/let_cast_const.tc"
run_check_ok "$ROOT/tests/valid/compare_unsigned.tc"
run_check_ok "$ROOT/tests/valid/shift_edge_cases.tc"
run_check_ok "$ROOT/tests/valid/uninitialized_bool.tc"
run_check_ok "$ROOT/tests/valid/phase5_funcall_return.tc"
run_check_ok "$ROOT/tests/valid/phase5_ptr_basic.tc"
run_check_ok "$ROOT/tests/valid/phase5_ptr_cast.tc"
run_check_ok "$ROOT/tests/valid/phase5_ptr_cast_nullptr.tc"
run_check_ok "$ROOT/tests/valid/phase5_ptr_bitcast.tc"
run_check_ok "$ROOT/tests/valid/phase5_ptr_scope_outer.tc"
run_check_ok "$ROOT/tests/valid/phase5_memblock_basic.tc"
run_check_ok "$ROOT/tests/valid/phase5_ptr_arith_cmp.tc"
run_check_ok "$ROOT/tests/valid/phase5_memblock_copy.tc"
run_check_ok "$ROOT/tests/valid/phase5_void_funcall.tc"
run_check_ok "$ROOT/tests/valid/phase5_static_var.tc"
run_check_ok "$ROOT/tests/valid/phase5_self_static_let.tc"
run_check_ok "$ROOT/tests/valid/phase5_self_static_ops.tc"
run_check_ok "$ROOT/tests/valid/phase5_ptr_cmp_more.tc"
run_check_ok "$ROOT/tests/valid/phase5_nullptr_eq.tc"
run_check_ok "$ROOT/tests/valid/phase5_memblock_fill.tc"
run_check_ok "$ROOT/tests/valid/phase5_nested_funcall.tc"
run_check_ok "$ROOT/tests/valid/isize_arith.tc"
run_check_ok "$ROOT/tests/valid/usize_arith.tc"

run_check_fail "$ROOT/tests/errors/static/syntax_error.tc" "unexpected token"
run_check_fail "$ROOT/tests/errors/static/negative_shift_count_const.tc" "negative shift count"
run_check_fail "$ROOT/tests/errors/static/return_memblock_size_mismatch.tc" "memblock size mismatch"
run_check_fail "$ROOT/tests/errors/static/field_memblock_size_mismatch.tc" "memblock size mismatch"
run_check_fail "$ROOT/tests/errors/static/ptr_load_memblock_size_mismatch.tc" "memblock size mismatch"
run_check_fail "$ROOT/tests/errors/static/memblock_negative_count_type.tc" "memblock count must be at least 1"
run_check_fail "$ROOT/tests/errors/static/memblock_negative_count_ctor.tc" "memblock count must be at least 1"
run_check_fail "$ROOT/tests/errors/static/func_body_public_var.tc" "visibility modifier is not allowed inside a function body"
run_check_fail "$ROOT/tests/errors/static/literal_leading_zero_underscore.tc" "invalid integer literal"
run_check_fail "$ROOT/tests/errors/static/undefined_variable.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/duplicate_def.tc" "duplicate definition"
run_check_fail "$ROOT/tests/errors/static/type_mismatch.tc" "operand type does not match"
run_check_fail "$ROOT/tests/errors/static/literal_range.tc" "literal out of range"
run_check_fail "$ROOT/tests/errors/static/wrap_mode_error.tc" "div/mod do not support wrap"
run_check_fail "$ROOT/tests/errors/static/const_assign.tc" "cannot assign to constant"
run_check_fail "$ROOT/tests/errors/static/const_expr.tc" "constant expression cannot reference var variable"
run_check_fail "$ROOT/tests/errors/static/const_cyclic_dep.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/let_nested_call.tc" "nested calls are not allowed in constant expression"
run_check_fail "$ROOT/tests/errors/static/let_short_circuit_invalid_rhs.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/shortcircuit_let_invalid_rhs.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/shortcircuit_let_rhs_type.tc" "operand type does not match operation type"
run_check_fail "$ROOT/tests/errors/static/uninit_shortcircuit_var_lhs.tc" "use of uninitialized variable"
run_check_fail "$ROOT/tests/errors/static/shortcircuit_let_forward_lhs.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/shortcircuit_let_out_of_scope_lhs.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/diag_priority_syntax_before_name.tc" "unexpected token"
run_check_fail "$ROOT/tests/errors/static/diag_priority_name_before_type.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/diag_priority_mode_before_literal.tc" "wrap mode is not allowed for float arithmetic"
run_check_fail "$ROOT/tests/errors/static/diag_priority_const_before_dfa.tc" "constant division by zero"
run_check_fail "$ROOT/tests/errors/static/const_overflow.tc" "constant overflow"
run_check_fail "$ROOT/tests/errors/static/const_div_zero.tc" "constant division by zero"
run_check_fail "$ROOT/tests/errors/static/compare_type_mismatch.tc" "literal type does not match context"
run_check_fail "$ROOT/tests/errors/static/logic_type_error.tc" "operand type does not match operation type"
run_check_fail "$ROOT/tests/errors/static/format_string_error.tc" "invalid format specifier"
run_check_fail "$ROOT/tests/errors/static/format_type_mismatch_signed.tc" "%u requires unsigned type"
run_check_fail "$ROOT/tests/errors/static/cast_wrap_keyword.tc" "wrap cannot be used with cast"
run_check_fail "$ROOT/tests/errors/static/bitcast_width_mismatch.tc" "bitcast source and target widths must match"
run_check_fail "$ROOT/tests/errors/static/bitcast_bool_type_mismatch.tc" "bool does not participate in bitcast"
run_check_fail "$ROOT/tests/errors/static/var_missing_initializer.tc" "variable definition requires initializer"
run_check_fail "$ROOT/tests/errors/static/forward_reference.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/bitwise_wrap_on_shr_keyword_error.tc" "wrap cannot be used with shift operations"
run_check_fail "$ROOT/tests/errors/static/bitwise_shl_const_overflow.tc" "constant overflow"
run_check_fail "$ROOT/tests/errors/static/if_cond_type_arith.tc" "if condition must be bool"
run_check_fail "$ROOT/tests/errors/static/if_cond_type_literal.tc" "literal type does not match context"
run_check_fail "$ROOT/tests/errors/static/if_missing_end_eof.tc" "missing end for if statement"
run_check_fail "$ROOT/tests/errors/static/indent_mixed_tab_body.tc" "mixed spaces and tabs in indentation"
run_check_fail "$ROOT/tests/errors/static/indent_insufficient_then.tc" "insufficient indentation in block"
run_check_fail "$ROOT/tests/errors/static/indent_else_mismatch.tc" "else indentation does not match if"
run_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_after_end.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/if_cross_block_ref_then_to_else.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/assign_to_let.tc" "cannot assign to constant"
run_check_fail "$ROOT/tests/errors/static/struct_immutable_field.tc" \
    "cannot assign to immutable struct field"
run_check_fail "$ROOT/tests/errors/static/struct_assign_let_outer_let_field.tc" \
    "cannot assign to constant binding"
run_check_fail "$ROOT/tests/errors/static/struct_assign_let_outer_var_field.tc" \
    "cannot assign to constant binding"
run_check_fail "$ROOT/tests/errors/static/struct_assign_through_param.tc" \
    "cannot assign to function parameter"
run_check_fail "$ROOT/tests/errors/static/struct_assign_param_let.tc" \
    "cannot assign to function parameter"
run_check_fail "$ROOT/tests/errors/static/self_ref_let.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/self_member_undefined.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/self_member_type_mismatch.tc" \
    "identifier type does not match destination type"
run_check_fail "$ROOT/tests/errors/static/self_member_bare_name.tc" "undefined variable"
run_check_fail "$ROOT/tests/errors/static/let_const_literal_range.tc" "invalid literal in constant expression"
run_check_fail "$ROOT/tests/errors/static/bool_literal_type_error.tc" "bool literal requires bool context"
run_check_fail "$ROOT/tests/errors/static/truncate_in_arith.tc" "truncate cannot be used with arithmetic"
run_check_fail "$ROOT/tests/errors/static/format_operand_count.tc" "operand count error"
run_check_fail "$ROOT/tests/errors/static/fp_mod_type_error.tc" "mod not supported for float types"
run_check_fail "$ROOT/tests/errors/static/fp_ieee_on_int.tc" "ieee mode is only allowed for float operations"
run_check_fail "$ROOT/tests/errors/static/fp_wrap_on_compare.tc" "wrap mode is not allowed for float comparison"
run_check_fail "$ROOT/tests/errors/static/fp_arith_wrap_mode_mismatch.tc" "wrap mode is not allowed for float arithmetic"
run_check_fail "$ROOT/tests/errors/static/fp_wrap_arith_mode_mismatch.tc" "wrap mode is not allowed for float arithmetic"
run_check_fail "$ROOT/tests/errors/static/fp_wrap_mode_mismatch.tc" "float unary operations do not accept mode keywords"
run_check_fail "$ROOT/tests/errors/static/fp_bitwise_type_error.tc" "expected type"
run_check_fail "$ROOT/tests/errors/static/fp_literal_range.tc" "literal out of range"
run_check_fail "$ROOT/tests/errors/static/goto_inside_loop.tc" "goto is not allowed inside while"
run_check_fail "$ROOT/tests/errors/static/label_inside_loop.tc" "label is not allowed inside while"
run_check_fail "$ROOT/tests/errors/static/break_outside_loop.tc" "break used outside while"
run_check_fail "$ROOT/tests/errors/static/continue_outside_loop.tc" "continue used outside while"

# --- runtime errors (VM vs AOT --run) ---

run_runtime_fail "$ROOT/tests/errors/runtime/signed_strict_overflow.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/signed_strict_mul.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/neg_int_min.tc" "neg(INT_MIN) overflow"
run_runtime_fail "$ROOT/tests/errors/runtime/abs_int_min.tc" "abs(INT_MIN) overflow"
run_runtime_fail "$ROOT/tests/errors/runtime/negative_shift_count.tc" "negative shift count"
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
run_runtime_fail "$ROOT/tests/errors/runtime/fp_strict_underflow.tc" "float underflow"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_strict_invalid.tc" "float invalid operation"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_strict_invalid_before_divzero.tc" \
    "float invalid operation"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_cast_overflow.tc" "out of range"
run_runtime_fail "$ROOT/tests/errors/runtime/fp_div_zero.tc" "division by zero"
run_runtime_fail "$ROOT/tests/errors/runtime/read_fp_invalid.tc" "invalid input" "abc
"

# --- embed mode (--embed) codegen ---

run_codegen_embed_contains() {
    file="$1"
    pattern="$2"
    aot_c="$(mktemp "${TMPDIR:-/tmp}/tcaot.XXXXXX").c"

    echo "EMBED_CODEGEN $file"
    if ! "$AOT_BIN" --embed -o "$aot_c" "$file" >/dev/null 2>/dev/null; then
        fail "aot --embed codegen failed: $file" "$file"
    elif ! grep -Fq "$pattern" "$aot_c"; then
        fail "aot --embed codegen missing '$pattern': $file" "$file"
    else
        pass
    fi
    rm -f "$aot_c"
}

run_codegen_embed_not_contains() {
    file="$1"
    pattern="$2"
    aot_c="$(mktemp "${TMPDIR:-/tmp}/tcaot.XXXXXX").c"

    echo "EMBED_CODEGEN_NO $file"
    if ! "$AOT_BIN" --embed -o "$aot_c" "$file" >/dev/null 2>/dev/null; then
        fail "aot --embed codegen failed: $file" "$file"
    elif grep -Fq "$pattern" "$aot_c"; then
        fail "aot --embed codegen unexpectedly contains '$pattern': $file" "$file"
    else
        pass
    fi
    rm -f "$aot_c"
}

# embed mode: 必须包含嵌入运行时头文件和函数表
run_codegen_embed_contains "$ROOT/tests/vm/embed/nested_call.tc" 'tc_aot_embed_rt.h'
run_codegen_embed_contains "$ROOT/tests/vm/embed/nested_call.tc" 'tc_aot_func_table'
run_codegen_embed_contains "$ROOT/tests/vm/embed/nested_call.tc" 'tc_aot_init'
run_codegen_embed_contains "$ROOT/tests/vm/embed/nested_call.tc" 'tc_aot_cleanup'
# embed mode: 不生成 main()
run_codegen_embed_not_contains "$ROOT/tests/vm/embed/nested_call.tc" 'int main(void)'
# embed mode: 函数非 static
run_codegen_embed_not_contains "$ROOT/tests/vm/embed/nested_call.tc" 'static void tc_'

echo ""
echo "$PASSED passed, $FAILED failed"

if [ "$FAIL" -ne 0 ]; then
    echo "Failed tests:" >&2
    printf '%s\n' "$FAILED_FILES" | sed '/^$/d' >&2
    exit 1
fi

echo "all aot differential tests passed"
