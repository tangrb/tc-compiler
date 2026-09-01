---
name: run-tests
description: >-
  Build and run TC-Compiler tests — VM integration, unit tests, AOT diff, ASan,
  CI scripts. Use when tests fail, adding test registrations, debugging stdout/
  stderr mismatch, running make ci, or check_rhs_coverage. Counts in test-map.md.
---

# Run TC-Compiler Tests

模板：Rule `tests-tc` · 账本：[test-map.md](../tc-architecture/test-map.md) · 单元：Rule `unit-tests-c`

## 选入口

| 场景 | 命令 |
|------|------|
| 最小回归（改 `.tc` 后） | `bash scripts/run_tests.sh --filter <名>` |
| 全量 Gate / 合并前 | `make ci` 或 `bash scripts/run_tests.sh` |
| 仅单元 | `make test-unit` |
| ASan | `bash scripts/run_asan_all.sh` |
| 单文件调试 | `./build/vm/bin/tc-vm tests/valid/foo.tc` |

`--filter`/`--asan`/`--ubsan`/`--valgrind` **仅 VM**；AOT/unit 始终全量。

## 分层

```bash
bash scripts/vm/run_tests.sh [--filter/--verbose/--asan]
bash scripts/aot/run_tests.sh
make test-unit
cmake --build build --target check-embed check-embed-aot
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
| static | stderr 子串；[errors.md](../tc-architecture/errors.md) 或 Rule `tests-tc` |
| 未注册 | `rg foo.tc scripts/vm/run_tests.sh` |
| AOT 不一致 | [gotchas.md](../tc-architecture/gotchas.md) · shim vs `tc_exec_*` |
| I/O | VM/AOT 均走 `tc_io.c`？ |
| unit FAIL | `./build/tests/bin/test-<name>` |
| RHS CI | `check_rhs_coverage.py --verbose` |
| uninit / CFG | `test_cfg` / `uninit_*.tc` |
| Embed | `check-embed` / `check-embed-aot` |

## 新增测试后

1. `scripts/vm/run_tests.sh`（static 另加 `run_expect_check_fail`）
2. AOT 相关 → `scripts/aot/run_tests.sh`
3. 补 `tests/unit/**/test_*.c`
4. [test-map.md](../tc-architecture/test-map.md)（`check_doc_counts.py`）

**勿**为查测试加载 `@knowledge-graph`。
