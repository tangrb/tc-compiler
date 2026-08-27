# TC-Compiler — Agent 指南

TC 语言编译器（C99）：libtc + TC-VM + TC-AOT + TC-Embed · **v0.0.41**（Phase 1–7 已落地）。

**完整导航**：[.cursor/README.md](.cursor/README.md)（加载分级 · 文档地图 · 意图速查）

## 效率原则（硬上限）

1. **先 `rg` 后读文** — 定位符号再打开 `.c`；勿一次读完 `tc-architecture/` 全部 md。
2. **每次只读路由指向的单文件** — 路由权威：Skill `tc-architecture`；特性地图：`features.md` → 一个 `features/*.md`。
3. **Glob 已附加 Rule** — `src/`→`compiler-src`；`src/aot/`→`aot-src`；`src/vm/embed/`→`embed-src`；`tests/`→`tests-tc`；`tests/unit/`→`unit-tests-c`。
4. **`@knowledge-graph` 仅跨模块** — 索引 → **恰好一个** `kg-*.md`；单文件小改 / 查测试 **勿加载**。
5. **改完最小测试** — `bash scripts/run_tests.sh --filter <名>`；动 `TcRhsKind` → `check_rhs_coverage.py`。

## 决策树

```
加特性     → add-compiler-feature（feature-kinds 单§）
跑测试     → run-tests
找代码     → tc-architecture → locations / rg
跨模块     → @knowledge-graph → 恰好一个 kg-*.md
0.0.41规范 → docs/*-0.0.41.md
写 .tc     → tests-tc（已附加）
写 unit    → unit-tests-c（已附加）
Review     → review-tc-code
```

## Skills（勿全读）

| Skill | 触发 | 只读 |
|-------|------|------|
| `tc-architecture` | 定位/架构/关键词路由 | 路由表指向的 **1** 个 md（特性经 `features.md` → `features/*.md`） |
| `add-compiler-feature` | 新语法/运算符 | `feature-kinds.md` 单 § |
| `run-tests` | 构建/测试/CI | 失败排查表（按需） |
| `review-tc-code` | Review/PR | checklist |

## 常用命令

```bash
bash scripts/run_tests.sh --filter <名>
python3 scripts/sync/check_rhs_coverage.py [--fix]
python3 scripts/sync/check_source_naming.py
python3 scripts/sync/check_doc_counts.py
make hooks
cmake --build build --target check-embed check-embed-aot
```
