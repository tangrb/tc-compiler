---
name: run-tests
description: Build and run TC-Compiler tests — VM, unit, AOT, filter, CI debug. Use when running tests or fixing failures. Test templates in tests-tc rule; mappings in test-map.md.
---

# Run TC-Compiler Tests

模板：`tests-tc` rule · 映射：[test-map.md](../tc-architecture/test-map.md) · 单元：`unit-tests-c` rule

## 入口

```bash
make && bash scripts/run_tests.sh          # 推荐全量
bash scripts/run_tests.sh --filter <名>    # 最小回归（仅 VM）
bash scripts/run_tests.sh --asan           # AddressSanitizer
bash scripts/run_tests.sh --ubsan          # UndefinedBehaviorSanitizer
bash scripts/run_tests.sh --valgrind       # Valgrind Memcheck（Linux）
bash scripts/run_tests.sh --leaks          # macOS leaks（需 Xcode）
make test                                  # Gate：~726 VM + ~3025 unit check() + ~318 AOT（当前树为准）
```

`--filter`/`--asan`/`--ubsan`/`--valgrind`/`--leaks`/`--verbose` **仅 VM**；AOT/unit 始终全量。

## 分层

```bash
bash scripts/vm/run_tests.sh [--filter/--verbose/--asan/--ubsan/--valgrind/--leaks]
bash scripts/aot/run_tests.sh               # Gate 272 / 当前约 327（stdout + --check + runtime + let）
make test-unit                              # check-*：lexer×2, parser, analyzer, module, cfg, executor,
                                            # stmt_index, semantics, types, diagnostic, libtc,
                                            # io, bitwise, shift, symbol, warning, embed, embed-aot
make test-valgrind
make test-leaks
make memcheck-macos                         # MallocScribble + leaks
bash scripts/run_asan_all.sh
bash scripts/run_memcheck_macos.sh
cmake --build build --target check-embed check-embed-aot
build/vm/bin/tc-vm tests/valid/foo.tc
build/vm/bin/tc-vm -c tests/errors/static/foo.tc
```

## CI 同步

```bash
python3 scripts/sync/check_rhs_coverage.py [--fix]   # 8 分发点
python3 scripts/sync/check_source_naming.py
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
| uninit / CFG | `test_cfg` / `uninit_*.tc`；固定点在 `tc_cfg.c` |
| Embed | `check-embed` / `check-embed-aot`；非致命 abort |

## 新增测试后

1. `scripts/vm/run_tests.sh`（static + `run_expect_check_fail`）
2. AOT 相关 → `scripts/aot/run_tests.sh`
3. semantics/types/io/bitwise/shift/symbol/cfg/analyzer → 补 `tests/unit/**/test_*.c`
4. 同步 [test-map.md](../tc-architecture/test-map.md)

Stress：`tests/stress/`（**10** 个，见 vm 脚本 + [test-map.md](../tc-architecture/test-map.md)）；`bash tests/stress/gen_stress_tests.sh` 可重生。
