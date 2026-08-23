# TC-Compiler — Cursor 文档索引

> **版本**：v0.0.39（Phase 1–7 已落地）· 权威规格 `docs/*-0.0.39.md`  
> **Agent 入口**：根目录 [AGENTS.md](../AGENTS.md)（始终加载，保持精简）

## 加载分级（硬上限）

| 级别 | 何时 | 读 | 禁止 |
|------|------|-----|------|
| **L0** | 改 1 个已知文件/符号 | Rule `compiler-src` + `rg` | 任何 skill 子文档 |
| **L1** | 定位/查架构 | Skill `tc-architecture` **路由表 1 行** → 对应 md | 批量打开 `tc-architecture/` |
| **L2** | 跨模块（RHS/CFG/let/模块/函数/Embed） | Rule `@knowledge-graph` → **恰好 1 个** `kg-*.md` | 其它 kg 文件 |
| **L3** | 加语言特性 | Skill `add-compiler-feature` → `feature-kinds.md` **单 §** | 全文 feature-kinds |
| **T** | 写/查测试 | Rule `tests-tc` 或 `unit-tests-c` + `test-map.md` 相关 § | knowledge-graph |

**读完目标文件即停**；缺信息再读第二份，不要预防性通读。

## 文档地图

```
.cursor/
├── README.md          ← 本文件（导航）
├── rules/             ← Glob 触发或 alwaysApply
│   ├── coding-standards.mdc   [始终]
│   ├── test-patterns.mdc      [始终]
│   ├── git-commit.mdc         [始终]
│   ├── compiler-src.mdc       [src/**]
│   ├── aot-src.mdc            [src/aot/**]
│   ├── embed-src.mdc          [src/vm/embed/**]
│   ├── tests-tc.mdc           [tests/**.tc, run_tests.sh]
│   ├── unit-tests-c.mdc       [tests/unit/**]
│   ├── docs-tc.mdc            [docs/**]
│   └── knowledge-graph.mdc    [@knowledge-graph 手动]
└── skills/
    ├── tc-architecture/       ← 路由权威（SKILL.md）
    │   ├── locations.md       符号 → 文件
    │   ├── pipeline.md        调用链 / Analyze 编排
    │   ├── types.md           类型内核 / 枚举规模
    │   ├── errors.md          诊断 / 错误码
    │   ├── syntax.md          语法 EBNF 摘要
    │   ├── features.md        特性索引 → features/*.md（每次只读 1 个）
    │   ├── features/
    │   │   ├── platform.md    类型/模块/函数/复合类型
    │   │   ├── scalar.md      bool/比较/逻辑/位运算/let/I/O/浮点
    │   │   ├── control.md     if/while/goto/CFG/诊断
    │   │   └── embed.md       TC-Embed
    │   ├── test-map.md        测试账本 / 错误→用例
    │   └── kg-*.md            跨模块细节（每次只读 1 个）
    ├── add-compiler-feature/  ← 加特性工作流
    ├── run-tests/             ← 构建与测试
    └── review-tc-code/        ← Code Review checklist
```

## 意图 → 第一步（速查）

| 用户意图 | 第一步 |
|----------|--------|
| 符号在哪 / `rg` 什么 | `locations.md` 或 `compiler-src` |
| 调用链 / Analyze 顺序 | `pipeline.md` |
| 跑测试 / CI 失败 | Skill `run-tests` |
| 加运算符 / 新 RHS / 新 STMT | `@knowledge-graph` + 对应 `kg-*.md` |
| 加语言特性（泛） | Skill `add-compiler-feature` |
| goto / CFG / uninit | `kg-cfg.md` |
| let / 短路 / AOT 语义 | `kg-eval.md` |
| 模块 / import / Self | `kg-module.md` |
| func / funcall / 调用图 | `kg-func.md` |
| TC-Embed / `--embed` | `kg-embed.md` + Rule `embed-src` |
| 写 `.tc` 测试 | Rule `tests-tc` |
| 写 C 单元测试 | Rule `unit-tests-c` |
| 改 `docs/` | Rule `docs-tc` |
| Review / PR | Skill `review-tc-code` |
| 0.0.39 语言/编译器规范 | `docs/TC语言标准设计说明书-0.0.39.md` 等 |

## 常用命令

```bash
bash scripts/run_tests.sh --filter <名>    # 最小回归
make test-unit                           # C 单元测试
python3 scripts/sync/check_rhs_coverage.py [--fix]
python3 scripts/sync/check_source_naming.py
python3 scripts/sync/check_type_fact_source.py
python3 scripts/sync/check_doc_counts.py # 文档数字校验
```

## 维护约定

1. **路由表**只维护在 `skills/tc-architecture/SKILL.md`；AGENTS.md 与 README 只保留速查，不复制长表。
2. **分发点表**（8 RHS / STMT）维护在 `rules/knowledge-graph.mdc`；细节在各 `kg-*.md`。
3. **测试规模数字**以 `test-map.md` 为准，由 `check_doc_counts.py` 校验。
4. 新模块同步：`locations.md` · `features.md` 路由表 + 对应 `features/*.md` · `compiler-src.mdc` · 必要时 `unit-tests-c.mdc`。
