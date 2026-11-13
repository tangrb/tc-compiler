# Tests

S0 provides the test infrastructure: L0 contract tests (`unit/`), the case
harness (`harness/`), traceability/code-table checks (`traceability/`), golden
review validation (`golden/`) and CLI contract checks (`tools/`).  The staging,
suites, coverage checklist and gates are defined in
[TC 开发测试计划 0.0.46](../docs/TC开发测试计划-0.0.46.md).

Run everything with:

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

Case-level conformance suites are added from S1 onward and should hold:

- static diagnostic tests (LEX/SYN/IMPORT/SEM/CT; 80-code closed set);
- VM/AOT differential tests at the same 32/64 profile;
- target-dependent tests for `usize`/`isize`, `sizeof`, `bitcast`, `trunc`,
  layout, `ptrdiff`, `@range`, and `@align`;
- runtime tests for traps, I/O, heap, pointer state, and 32-bit `int64`/`float64`
  helpers.

Do not copy the historical test tree from git history; write 0.0.46 tests from
the standard and design.
