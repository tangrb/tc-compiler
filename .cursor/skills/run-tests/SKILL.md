---
name: run-tests
description: >-
  Build and run TC-Compiler tests — VM integration, C unit tests, AOT diff,
  ASan/UBSan/Valgrind, and CI sync scripts. Use when running tests, fixing test
  failures, adding test registrations, or debugging stdout/stderr mismatches.
  Templates in tests-tc rule; mappings in test-map.md.
---

# Run TC-Compiler Tests

模板：Rule `tests-tc` · 映射：[test-map.md](../tc-architecture/test-map.md) · 单元：Rule `unit-tests-c`

## 入口

```bash
make && bash scripts/run_tests.sh          # 推荐全量
bash scripts/run_tests.sh --filter <名>    # 最小回归（仅 VM）
bash scripts/run_tests.sh --asan           # AddressSanitizer
make test                                  # Gate：892 VM + ~3191 unit + 456 AOT 执行
```

`--filter`/`--asan`/`--ubsan`/`--valgrind`/`--leaks` **仅 VM**；AOT/unit 始终全量。

## 分层

```bash
bash scripts/vm/run_tests.sh [--filter/--verbose/--asan]
bash scripts/aot/run_tests.sh               # 注册 403 / 执行 456
make test-unit                              # check-* 汇总（~3191 check()）
cmake --build build --target check-embed check-embed-aot
build/vm/bin/tc-vm tests/valid/foo.tc
build/vm/bin/tc-vm -c tests/errors/static/foo.tc
```

## CI 同步

```bash
python3 scripts/sync/check_rhs_coverage.py [--fix]
python3 scripts/sync/check_source_naming.py
python3 scripts/sync/check_type_fact_source.py
python3 scripts/sync/check_doc_counts.py
```

## 失败排查

| 现象 | 查 |
|------|-----|
| stdout diff | `writeln` 末尾 `\n`；期望完全一致 |
| static | stderr 子串；[errors.md](../tc-architecture/errors.md) |
| 未注册 | `rg foo.tc scripts/vm/run_tests.sh` |
| AOT 不一致 | codegen vs `tc_aot_rt.c` shim |
| I/O | VM/AOT 均走 `tc_io.c`？ |
| unit FAIL | `./build/tests/bin/test-<name>` |
| RHS CI | `check_rhs_coverage.py --verbose` |
| uninit / CFG | `test_cfg` / `uninit_*.tc` |
| Embed | `check-embed` / `check-embed-aot` |

## 新增测试后

1. `scripts/vm/run_tests.sh`（static + `run_expect_check_fail`）
2. AOT 相关 → `scripts/aot/run_tests.sh`
3. 补 `tests/unit/**/test_*.c`（按模块）
4. 同步 [test-map.md](../tc-architecture/test-map.md)
