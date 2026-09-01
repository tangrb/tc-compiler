# TC-Compiler — Agent 指南

C99 编译器：libtc + TC-VM + TC-AOT + TC-Embed · **v0.0.42**（Phase 1–7）。

**导航权威**：[.cursor/README.md](.cursor/README.md)（加载分级 · Skill 触发 · 工作流）。本文件始终加载，**勿**复制长路由表。

## 硬上限（5 条）

1. **先 `rg`，再读 md** — 已知文件/符号则直接改；每次只打开路由指向的 **1** 个文档。
2. **Glob Rule 按目录附加**：`src/**`→`compiler-src`，`src/aot/**`→`aot-src`，`src/vm/embed/**`→`embed-src`，`*.tc`→`tests-tc`，`tests/unit/**`→`unit-tests-c`，`docs/**`→`docs-tc`。
3. **`@knowledge-graph` 仅跨层** — 新/改 `TcRhsKind`/`TcStmtKind` 或同时动 parser+analyzer+executor/aot；单文件小改 / 查测试 **勿**加载。
4. **改完最小测**：`bash scripts/run_tests.sh --filter <名>`；动 `TcRhsKind` → `check_rhs_coverage.py`。
5. **易踩坑**：改模块/import/const/AOT/memcopy 前先扫 [gotchas.md](.cursor/skills/tc-architecture/gotchas.md)。

## 何时用哪个 Skill

| 用户意图 / 场景 | Skill | 第一步 |
|-----------------|-------|--------|
| 符号在哪、调用链、选文档 | `tc-architecture` | 读 SKILL 路由 **1 行** → 1 个 md |
| 加语法/运算符/RHS/STMT/特性 | `add-compiler-feature` | `feature-kinds.md` **单 §** |
| 跑测试、修 CI、stdout 不匹配 | `run-tests` | `--filter` 或分层入口 |
| PR / 代码 Review | `review-tc-code` | gotchas + checklist |
| 只改 1 个 `.c` / 注释 / 已知 bugfix | **无** | Glob Rule + `rg`（L0） |

路由表权威：`skills/tc-architecture/SKILL.md`（README 不重复）。

## 决策树（一行）

```
L0 已知文件     → rg + Glob Rule
L1 找文档       → tc-architecture → 1 md
L2 跨模块/RHS   → @knowledge-graph → 1 kg-*.md
L3 加特性       → add-compiler-feature → feature-kinds 单§
T  测试         → run-tests / tests-tc / unit-tests-c
R  Review       → review-tc-code
规范全文        → docs/*-0.0.42.md（按需单文件）
```

## 命令

```bash
bash scripts/run_tests.sh --filter <名>
python3 scripts/sync/check_rhs_coverage.py [--fix]
python3 scripts/sync/check_source_naming.py
python3 scripts/sync/check_doc_counts.py
make hooks
```
