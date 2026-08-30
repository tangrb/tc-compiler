# TC-Compiler — Cursor 文档索引

> **v0.0.42**（Phase 1–7）· 规范 `docs/*-0.0.42.md`  
> **始终加载**：根目录 [AGENTS.md](../AGENTS.md)（极简）· 本文件为**导航权威**（勿把长表复制进 AGENTS）

## Agent 读文档协议

1. **先 `rg` / Glob Rule，后读 md** — 已知文件则直接改，勿开 skill 子文档。
2. **每次只读一份目标 md** — 读完即停；缺信息再读第二份。
3. **禁止**预防性通读 `tc-architecture/`、同时打开多个 `kg-*.md`、为查测试加载 knowledge-graph。
4. **规模数字**以 [test-map.md](skills/tc-architecture/test-map.md) 为准（`check_doc_counts.py`）。

| 级别 | 场景 | 读 | 禁止 |
|------|------|-----|------|
| **L0** | 改 1 个已知符号/文件 | Glob Rule + `rg` | 任何 skill 子文档 |
| **L1** | 定位 / 选文档 | Skill `tc-architecture` **路由 1 行** → 1 个 md | 批量打开子文档 |
| **L2** | 跨模块（RHS/CFG/let/模块/函数/Embed） | `@knowledge-graph` → **恰好 1** 个 `kg-*.md` | 其它 kg |
| **L3** | 加语言特性 | `add-compiler-feature` → `feature-kinds.md` **单 §** | 全文 feature-kinds |
| **G** | 易踩坑（菱形 import / const 堆 / memcopy 负下标…） | [gotchas.md](skills/tc-architecture/gotchas.md) | 与多个 kg 叠读 |
| **T** | 写/查测试 | `tests-tc` / `unit-tests-c`；映射用 `rg` 或 test-map **一节** | knowledge-graph |

## 意图 → 第一步

| 意图 | 第一步 |
|------|--------|
| 符号在哪 | [locations.md](skills/tc-architecture/locations.md) 或 `rg` |
| 调用链 / Analyze | [pipeline.md](skills/tc-architecture/pipeline.md) |
| 易错点 / 历史坑 | [gotchas.md](skills/tc-architecture/gotchas.md) |
| 跑测试 / CI | Skill `run-tests` |
| 加运算符 / RHS / STMT | `@knowledge-graph` + 对应 `kg-*.md` |
| 加特性（泛） | Skill `add-compiler-feature` |
| goto / CFG / uninit | [kg-cfg.md](skills/tc-architecture/kg-cfg.md) |
| let / 短路 / AOT 语义 | [kg-eval.md](skills/tc-architecture/kg-eval.md) |
| 模块 / import / Self | [kg-module.md](skills/tc-architecture/kg-module.md) |
| func / funcall / 调用图 | [kg-func.md](skills/tc-architecture/kg-func.md) |
| Embed / `--embed` | [kg-embed.md](skills/tc-architecture/kg-embed.md) + Rule `embed-src` |
| 写 `.tc` / C unit | Rule `tests-tc` / `unit-tests-c` |
| 改 `docs/` | Rule `docs-tc` |
| Review | Skill `review-tc-code` |
| 语言/编译器规范全文 | `docs/TC语言标准设计说明书-0.0.42.md` 等（按需单文件） |

## 目录地图

```
.cursor/
├── README.md                 ← 本文件（导航权威）
├── rules/                    ← Glob 或 alwaysApply；agent-requestable 见 knowledge-graph
│   ├── coding-standards.mdc · test-patterns.mdc · git-commit.mdc   [始终]
│   ├── compiler-src.mdc      [src/**]
│   ├── aot-src.mdc           [src/aot/**]
│   ├── embed-src.mdc         [src/vm/embed/**]
│   ├── tests-tc.mdc          [*.tc / run_tests.sh]
│   ├── unit-tests-c.mdc      [tests/unit/**]
│   ├── docs-tc.mdc           [docs/**]
│   └── knowledge-graph.mdc   [@knowledge-graph · 分发点表]
└── skills/
    ├── tc-architecture/      ← 路由权威 SKILL.md → 每次 1 个子 md
    │   ├── locations · pipeline · types · errors · syntax
    │   ├── features.md → features/{platform,scalar,control}.md（每次 1）
    │   ├── test-map.md · gotchas.md
    │   └── kg-{dispatch,cfg,eval,module,func,embed}.md（每次 1）
    ├── add-compiler-feature/ · run-tests/ · review-tc-code/
```

## 常用命令

```bash
bash scripts/run_tests.sh --filter <名>
make test-unit
python3 scripts/sync/check_rhs_coverage.py [--fix]
python3 scripts/sync/check_source_naming.py
python3 scripts/sync/check_doc_counts.py
make hooks
cmake --build build --target check-embed check-embed-aot
```

## 维护约定（单一事实源）

| 内容 | 权威位置 |
|------|----------|
| 关键词/问题路由表 | `skills/tc-architecture/SKILL.md` |
| RHS/STMT 分发点 | `rules/knowledge-graph.mdc` |
| 测试规模数字 | `test-map.md`（`check_doc_counts.py`） |
| 易错点 | `gotchas.md` |
| 加载分级 / 本地图 | **本 README**（AGENTS 只链到此） |

新模块：`locations.md` · `features.md` 路由 · 对应 `features/*.md` · `compiler-src.mdc` · 必要时 `unit-tests-c.mdc` / `gotchas.md`。
