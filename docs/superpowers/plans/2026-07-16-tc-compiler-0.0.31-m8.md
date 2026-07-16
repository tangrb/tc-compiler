# TC Compiler 0.0.31 M8 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete M8 by emitting structured C99 loops, aligning every fallible AOT shim with VM runtime behavior, completing the 0.0.31 VM/AOT differential matrix, and enforcing the exact shared I/O contract.

**Architecture:** `tc_aot_codegen.c` emits native C blocks while preserving Analyzer-assigned `loop_id` and DFS `stmt_index`; every fallible generated operation writes through a temporary/result-on-success helper and aborts through the shared diagnostic path. `tc_io.c` remains the sole implementation of formatting and parsing, while VM and AOT only adapt values and diagnostics around it.

**Tech Stack:** C99, CMake/Make, POSIX shell differential tests, TC-VM, TC-AOT, host `cc` with strict warnings.

## Global Constraints

- Preserve the current dirty M0-M7 work on branch `codex/tc-spec-0.0.31`; do not stage or commit unrelated changes.
- Use stable DFS `stmt_index` values for generated internal names; never derive C identifiers from TC user names.
- Generated C compiles with `-std=c99 -Wall -Wextra -Werror -pedantic` and never enables fast-math.
- VM and AOT share `tc_semantics` and `tc_io`; do not copy arithmetic, cast, bitcast, formatting, or parsing logic into codegen.
- Each production change follows a witnessed red-green cycle and ends with the smallest relevant regression command.

---

### Task 1: Structured C99 while emission

**Files:**
- Modify: `scripts/aot/run_tests.sh`
- Modify: `src/aot/tc_aot_codegen.c`

**Interfaces:**
- Consumes: `TcWhileStmt.loop_id`, `TcLoopControlStmt.loop_id`, `tc_stmt_index_take()`, and `TcBlockId{owner_stmt_index, TC_BLOCK_WHILE}`.
- Produces: C99 `for (;;)`, unique `uint64_t tc_cond_<stmt_index>`, and native `break`/`continue` only for the top loop frame with the same `loop_id`.

- [x] **Step 1: Register the six M7 control-flow fixtures as AOT stdout and `--check` differentials**

```sh
run_diff_test "$ROOT/tests/valid/while_false.tc"
run_diff_test "$ROOT/tests/valid/while_counted.tc"
run_diff_test "$ROOT/tests/valid/while_nested.tc"
run_diff_test "$ROOT/tests/valid/while_break_continue.tc"
run_diff_test "$ROOT/tests/valid/while_var_reinitialize.tc"
run_diff_test "$ROOT/tests/valid/goto_var_reinitialize.tc"
```

- [x] **Step 2: Run the AOT suite and witness failure caused by missing while emission**

Run: `bash scripts/aot/run_tests.sh`

Expected: non-zero; at least `while_counted.tc`, `while_nested.tc`, `while_break_continue.tc`, or `while_var_reinitialize.tc` reports an AOT stdout mismatch.

- [x] **Step 3: Add loop metadata to `TcAotEmitCtx` and emit the loop condition into a stable temporary**

```c
typedef struct {
    int loop_ids[TC_AOT_BLOCK_DEPTH_MAX];
    int depth;
} TcAotLoopStack;

fprintf(out, "%suint64_t tc_cond_%d;\n", loop_indent, while_stmt_index);
fprintf(out, "%sfor (;;) {\n", loop_indent);
tc_aot_emit_rhs(out, &while_stmt->condition, TC_BOOL, cond_name,
                body_indent, symbols, &ctx->sym_index,
                while_stmt_index, while_stmt->line);
fprintf(out, "%sif (tc_cond_%d == 0) { break; }\n",
        body_indent, while_stmt_index);
```

- [x] **Step 4: Push the while block path and loop id while recursively emitting the body**

```c
tc_aot_block_path_push(&ctx->block_path,
                       (TcBlockId){while_stmt_index, TC_BLOCK_WHILE});
tc_aot_loop_stack_push(&ctx->loops, while_stmt->loop_id);
/* emit body */
tc_aot_loop_stack_pop(&ctx->loops);
tc_aot_block_path_pop(&ctx->block_path);
```

- [x] **Step 5: Emit loop control only when Analyzer metadata matches the innermost frame**

```c
if (ctx->loops.depth == 0 ||
    ctx->loops.loop_ids[ctx->loops.depth - 1] != loop_control->loop_id) {
    return -1;
}
fprintf(out, "%s%s;\n", indent,
        stmt->kind == TC_STMT_BREAK ? "break" : "continue");
```

- [x] **Step 6: Rebuild and run the AOT suite**

Run: `make -j4 && bash scripts/aot/run_tests.sh`

Expected: exit 0 with all registered AOT differentials passing.

### Task 2: Fallible AOT shims and runtime diagnostics

**Files:**
- Modify: `src/aot/tc_aot_rt.c`
- Modify: `src/aot/tc_aot_rt.h`
- Modify: `src/aot/tc_aot_codegen.c`
- Modify: `src/aot/main.c`
- Modify: `scripts/aot/run_tests.sh`

**Interfaces:**
- Consumes: `tc_exec_cast`, `tc_exec_truncate`, `tc_exec_bitcast`, `tc_io_write_value`, `tc_io_read_value`, and `tc_diagnostic_print`.
- Produces: `int tc_aot_write(..., TcDiagnostic *diag, int line)` plus generated return-value checks; diagnostics retain the TC source filename and line.

- [x] **Step 1: Strengthen runtime differential assertions**

Capture VM and AOT exit statuses, require both to be non-zero and equal, then compare the first `: error:` diagnostic line after code generation binds the same source filename.

- [x] **Step 2: Run cast/read runtime differentials and witness the filename/diagnostic mismatch**

Run: `bash scripts/aot/run_tests.sh`

Expected: non-zero because generated runtime diagnostics currently use `<source>` instead of the TC path.

- [x] **Step 3: Bind the generated diagnostic to the escaped TC source filename**

```c
tc_aot_diag_init(&diag);
tc_diagnostic_set_source(&diag, "escaped/source.tc", NULL);
```

- [x] **Step 4: Make output a checked shim**

```c
int tc_aot_write(TcType type, TcFormatSpec fmt, uint64_t bits, int newline,
                 TcDiagnostic *diag, int line) {
    TcValue value = tc_value_make(type, bits);
    if (tc_io_write_value(&value, fmt, newline, stdout) != 0) {
        tc_diagnostic_set(diag, TC_ERR_IO, line, TC_COLUMN_UNKNOWN, "output failed");
        return -1;
    }
    return 0;
}
```

- [x] **Step 5: Generate an abort check around every write call**

```c
if (tc_aot_write(type, fmt, bits, newline, &diag, line) != 0)
    tc_aot_abort(&diag, line);
```

- [x] **Step 6: Enforce strict host compilation**

Change the host command to `cc -std=c99 -Wall -Wextra -Werror -pedantic` and leave all fast-math flags absent.

- [x] **Step 7: Rebuild and run runtime/AOT regressions**

Run: `make -j4 && bash scripts/aot/run_tests.sh`

Expected: exit 0; runtime diagnostic line and exit status parity pass.

### Task 3: Complete the 0.0.31 differential matrix

**Files:**
- Create: `tests/errors/static/goto_inside_loop.tc`
- Create: `tests/errors/static/label_inside_loop.tc`
- Create: `tests/errors/static/break_outside_loop.tc`
- Create: `tests/errors/static/continue_outside_loop.tc`
- Modify: `scripts/vm/run_tests.sh`
- Modify: `scripts/aot/run_tests.sh`

**Interfaces:**
- Consumes: the M5-M7 valid bitcast/let/CFG fixtures and Analyzer loop-isolation diagnostics.
- Produces: stdout, `--check`, runtime error, and strict host-C coverage for every M8 matrix row.

- [x] **Step 1: Add the four loop-isolation fixtures**

```tc
while true then
    goto outside
end
label outside:
```

```tc
while true then
    label inside:
    break
end
```

```tc
break
```

```tc
continue
```

- [x] **Step 2: Register static failures in VM and AOT**

Expected messages: `goto is not allowed inside while`, `label is not allowed inside while`, `break used outside while`, and `continue used outside while`.

- [x] **Step 3: Verify the complete matrix**

Run: `bash scripts/run_tests.sh --filter while_`

Run: `bash scripts/run_tests.sh --filter goto_var_reinitialize`

Run: `bash scripts/aot/run_tests.sh`

Expected: all commands exit 0.

### Task 4: Exact shared I/O contract

**Files:**
- Modify: `src/vm/runtime/tc_io.c`
- Modify: `src/vm/runtime/tc_io.h`
- Modify: `tests/unit/runtime/test_io.c`
- Modify: `tests/valid/fp_io.tc`
- Modify: `tests/valid/format_spec_all.tc`
- Modify: `scripts/vm/run_tests.sh`
- Modify: `scripts/aot/run_tests.sh`

**Interfaces:**
- Consumes: `TcFormatSpec`, canonical NaN bit patterns, `tc_fp_bits_to_double`, and `TcDiagnostic`.
- Produces: exact 13-format output, ASCII-token input, canonical `nan`, signed zero, direct `strtof`/`strtod` target rounding, and `TC_ERR_IO` failures without modifying the destination.

- [x] **Step 1: Add table-driven format and input tests before production changes**

The format table contains all 13 specifiers and exact strings, including `%b` positive `5 -> "101"`, int8 `-1 -> "11111111"`, float `-0`, `inf`, `-inf`, and `nan`/`NAN`. Input cases include `12abc`, `+1`, EOF, range overflow, `inf`, `-inf`, `nan`, negative zero, representable subnormal, and non-zero underflow-to-zero.

- [x] **Step 2: Run `check-io` and witness the contract failures**

Run: `cmake --build build --target check-io`

Expected: non-zero for positive binary leading zero, incomplete integer token acceptance, and unsupported float special tokens.

- [x] **Step 3: Enforce format/type compatibility and binary trimming**

For `%b`, print `0` for zero, omit leading zeros for non-negative values, and print the complete declared-width bit pattern for negative signed values.

- [x] **Step 4: Centralize complete ASCII token reading**

Read one token terminated only by U+0009-U+000D, U+0020, or EOF; reject buffer overflow, stream failure, and trailing non-grammar characters as `TC_ERR_IO`.

- [x] **Step 5: Parse floats at target precision**

Use `strtof` for `float32` and `strtod` for `float64`; accept only the decimal grammar plus exact lowercase `inf`, `-inf`, and `nan`; emit canonical NaN bits; allow non-zero representable subnormals and reject finite non-zero values rounded to zero or infinity.

- [x] **Step 6: Normalize deterministic floating output**

Emit exact lowercase special values except uppercase `%E`/`%G`, preserve the negative-zero sign, force round-to-nearest during formatting where fenv is available, and replace the active locale decimal separator with `.` before writing.

- [x] **Step 7: Extend language fixtures and expected stdout**

Add all five float formats for negative zero, infinities, and NaN to `fp_io.tc`; add `%i`, `%t`, all five float formats, positive `%b` trimming, and write/no-write newline boundaries to `format_spec_all.tc`.

- [x] **Step 8: Run I/O and differential regressions**

Run: `cmake --build build --target check-io`

Run: `bash scripts/run_tests.sh --filter fp_io`

Run: `bash scripts/run_tests.sh --filter format_spec_all`

Run: `bash scripts/run_tests.sh --filter read_`

Run: `bash scripts/aot/run_tests.sh`

Expected: all commands exit 0 and VM/AOT stdout is byte-identical.

### Task 5: M8 gate and acceptance evidence

**Files:**
- Modify: `docs/TC编译器_0.0.31阶段性开发计划.md`

**Interfaces:**
- Consumes: all Task 1-4 regression evidence.
- Produces: checked M8 boxes and a dated acceptance-evidence paragraph without changing M9/M10 status.

- [x] **Step 1: Run source consistency checks**

Run: `python3 scripts/sync/check_rhs_coverage.py`

Run: `python3 scripts/sync/check_source_naming.py`

- [x] **Step 2: Run the complete suite fresh**

Run: `make test`

Expected: VM, unit, and AOT all report zero failures.

- [x] **Step 3: Review the final diff for scope and generated artifacts**

Run: `git diff --check`

Run: `git status --short`

Expected: no whitespace errors; only intentional M8 additions plus preserved M0-M7 work.

- [x] **Step 4: Record M8 acceptance evidence**

Check every M8 task and gate box and append the fresh VM/unit/AOT totals and source-check results. Leave the overall 0.0.31 conclusion pending M9-M10.
