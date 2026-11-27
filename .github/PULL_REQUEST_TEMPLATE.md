## Summary / 摘要

<!-- What and why (1–3 bullets). Link related issues: Fixes #N -->
<!-- 做了什么、为什么（1–3 条）。关联 Issue：Fixes #N -->

## Test plan / 测试清单

- [ ] Full verification: `cmake -S . -B .tmp/build && cmake --build .tmp/build && ctest --test-dir .tmp/build --output-on-failure`
- [ ] New conformance cases registered under `tests/conformance/**` with traceability ids
- [ ] Diagnostics unchanged, or updated together with the standard's §12.3 (or N/A)
- [ ] `usize`/`isize` and 32/64-bit profile differences considered where relevant (or N/A)
- [ ] Docs updated when semantics / CLI / targets change (or N/A); `python3 tests/tools/docs_cleanup_check.py` passes
- [ ] User-visible change noted in `CHANGELOG.md` (or N/A)

## Notes / 备注

<!-- Optional: risk, follow-ups, screenshots -->
<!-- 可选：风险、后续、截图 -->
