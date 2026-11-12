# Tests

The 0.0.46 conformance suite is not implemented yet.

When the compiler skeleton gains real targets, this directory should hold:

- static diagnostic tests (LEX/SYN/IMPORT/SEM/CT; 80-code closed set);
- VM/AOT differential tests at the same 32/64 profile;
- target-dependent tests for `usize`/`isize`, `sizeof`, `bitcast`, `trunc`,
  layout, `ptrdiff`, `@range`, and `@align`;
- runtime tests for traps, I/O, heap, pointer state, and 32-bit `int64`/`float64`
  helpers.

Do not copy the historical test tree from git history; write 0.0.46 tests from
the standard and design.
