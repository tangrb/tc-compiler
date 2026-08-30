# TC-Compiler — Agent 指南

C99 编译器：libtc + TC-VM + TC-AOT + TC-Embed · **v0.0.41**（Phase 1–7）。

**导航权威**：[.cursor/README.md](.cursor/README.md)（加载分级 · 意图表 · 目录地图）。本文件保持极简，**勿**复制长路由表。

## 硬上限

1. 先 `rg`，再读 md；每次只打开路由指向的 **1** 个文档。
2. Glob Rule 已按目录附加：`src/`→`compiler-src`，`aot/`→`aot-src`，`embed/`→`embed-src`，`.tc`→`tests-tc`，`tests/unit/`→`unit-tests-c`。
3. `@knowledge-graph` → **恰好一个** `kg-*.md`；单文件小改 / 查测试勿加载。
4. 改完最小测：`bash scripts/run_tests.sh --filter <名>`；动 `TcRhsKind` → `check_rhs_coverage.py`。
5. 易踩坑先扫 [gotchas.md](.cursor/skills/tc-architecture/gotchas.md)（菱形 import、const 堆、AOT 字段、memcopy 负下标）。

## 决策树

```
找代码     → tc-architecture → locations / rg
跨模块     → @knowledge-graph → 一个 kg-*.md
加特性     → add-compiler-feature（feature-kinds 单§）
跑测试     → run-tests
写 .tc     → tests-tc（已附加）
写 unit    → unit-tests-c（已附加）
Review     → review-tc-code
规范全文   → docs/*-0.0.41.md（按需）
```

## Skills（勿全读子文档）

| Skill | 用途 |
|-------|------|
| `tc-architecture` | 路由表 → **1** 个 md |
| `add-compiler-feature` | `feature-kinds.md` 单 § |
| `run-tests` | 构建 / 过滤 / CI 同步 |
| `review-tc-code` | checklist |

## 命令

```bash
bash scripts/run_tests.sh --filter <名>
python3 scripts/sync/check_rhs_coverage.py [--fix]
python3 scripts/sync/check_source_naming.py
python3 scripts/sync/check_doc_counts.py
make hooks
```
