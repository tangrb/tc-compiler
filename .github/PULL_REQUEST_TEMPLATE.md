## Summary / 摘要

<!-- What and why (1–3 bullets). Link related issues: Fixes #N -->
<!-- 做了什么、为什么（1–3 条）。关联 Issue：Fixes #N -->

## Test plan / 测试清单

- [ ] `bash scripts/run_tests.sh` (or a justified `--filter` plus CI)
- [ ] New `.tc` cases registered in `scripts/vm/run_tests.sh` (and `scripts/aot/run_tests.sh` if AOT-relevant)
- [ ] New `TcRhsKind` → `python3 scripts/sync/check_rhs_coverage.py`
- [ ] New `TcErrorKind` → `tc_error_kind_name()` + unit test + compiler spec §11.4
- [ ] New `src/` module → `python3 scripts/sync/check_source_naming.py`
- [ ] Type / CFG / Embed changes covered by the matching check targets
- [ ] Static error fixtures: one error per file
- [ ] User-visible change noted in `CHANGELOG.md` / `CHANGELOG.en.md` `[Unreleased]` (or N/A)
- [ ] Docs updated when semantics / public API / CLI change (or N/A)
- [ ] Commits do **not** include `Co-authored-by: Cursor <cursoragent@cursor.com>`

## Notes / 备注

<!-- Optional: risk, follow-ups, screenshots -->
<!-- 可选：风险、后续、截图 -->
