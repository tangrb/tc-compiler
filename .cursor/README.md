# TC-Compiler — Cursor 文档索引

> **v0.0.43**（Phase 1–7；语言规范 `docs/*-0.0.42.md`）  
> **始终加载**：[AGENTS.md](../AGENTS.md)（极简）· **本文件** = 导航权威（勿把长表复制进 AGENTS）

## 30 秒：Agent 该怎么读

```
用户说了什么？
  ├─ 已知改哪个 .c/.tc        → L0：rg + Glob Rule，不读 skill
  ├─ 「在哪」「调用链」「错误码」→ L1：tc-architecture SKILL 路由 1 行 → 1 md
  ├─ 加运算符/RHS/语句/模块    → L3：add-compiler-feature → feature-kinds 单§
  │                              跨层时再 @knowledge-graph → 1 kg-*.md
  ├─ 跑测试 / CI 红            → run-tests（勿开 knowledge-graph）
  └─ Review PR                 → review-tc-code
```

**关键词路由表**（一行一事）：[`skills/tc-architecture/SKILL.md`](skills/tc-architecture/SKILL.md) — **唯一权威**，本 README 不重复。

## 加载分级

| 级别 | 场景 | 读 | 禁止 |
|------|------|-----|------|
| **L0** | 改 1 个已知符号/文件 | Glob Rule + `rg` | 任何 skill 子文档 |
| **L1** | 定位 / 选文档 | Skill `tc-architecture` → **1 行路由** → 1 md | 批量打开子文档 |
| **L2** | 跨模块 RHS/CFG/模块/函数/Embed | `@knowledge-graph` → **1** 个 `kg-*.md` | 多个 kg 叠读 |
| **L3** | 加语言特性 | `add-compiler-feature` → `feature-kinds.md` **单 §** | 全文 feature-kinds |
| **G** | 易踩坑 | [gotchas.md](skills/tc-architecture/gotchas.md) | 与 kg 无必要叠读 |
| **T** | 写/查测试 | `tests-tc` / `unit-tests-c` + `run-tests` | knowledge-graph |

### L0 正面示例（不应加载 skill）

| 任务 | 做法 |
|------|------|
| 修 `tc_io.c` 某函数 off-by-one | `rg` 符号 → 改 → `--filter` 相关 `.tc` |
| 给 static 测试补一行期望子串 | Rule `tests-tc` 已附加，直接改 + 注册 |
| 更新 README 版本号 | 直接改，无需 architecture skill |
| `check_rhs_coverage.py` CI 报缺分支 | 打开脚本指出的 `.c`，对照 [kg-dispatch.md](skills/tc-architecture/kg-dispatch.md) |

### `@knowledge-graph` 触发条件

**加载**（读 [knowledge-graph.mdc](rules/knowledge-graph.mdc) → **恰好 1** 个 `kg-*.md`）当：

- 新增或修改 `TcRhsKind` / `TcStmtKind`
- 同一任务需同时理解 parser + analyzer + executor **或** aot
- 任务明确属于：dispatch / CFG / let·短路 / 模块 / 函数·调用图 / Embed

**不加载**当：

- 单文件 bugfix、文档、测试注册、查 test-map
- 仅 VM 或仅 AOT 一层且 `rg` 已定位
- 用户只问「跑什么测试」

## Skill 触发速查

| Skill | Cursor 何时应加载 | 首步 |
|-------|-------------------|------|
| `tc-architecture` | where is X、pipeline、errors、types、features 路由 | SKILL 路由表 1 行 |
| `add-compiler-feature` | 新语法、新运算符、新 RHS/STMT、format、let 扩展 | `feature-kinds.md` 单 § |
| `run-tests` | run tests、CI fail、stdout/stderr diff、注册用例 | `--filter` 或 `make ci` |
| `review-tc-code` | review、PR、checklist、合并前检查 | gotchas + checklist |

Skill 描述字段已优化以便 Cursor 自动匹配；仍遵守「每次只读 1 个子 md」。

## 常用工作流

### 新 TcRhsKind

```
rg "TC_RHS_" src/ → 改 8 分发点（见 kg-dispatch.md）
→ check_rhs_coverage.py → 注册 .tc → run_tests.sh --filter
→ 同步 features/*.md + test-map.md
```

### 新 TcStmtKind / 控制流

```
rg "TC_STMT_" → parser + pass1/2 + tc_cfg.c（必改）+ executor + aot
→ test_cfg + uninit .tc → kg-cfg.md
```

### 修 AOT 与 VM 不一致

```
gotchas.md → 比对 tc_exec_* vs tc_aot_rt shim
→ run_tests.sh AOT 段 → features/scalar.md § I/O 或 kg-eval.md
```

### 新 static 错误

```
errors.md（rg 码名）→ tests/errors/static/ 一文件一错
→ tc_error_kind_name + test_types.c + 标准 §11.4
```

## Glob Rules

| Glob | Rule | 用途 |
|------|------|------|
| `src/**` | `compiler-src` | 按层选文件、rg 首步 |
| `src/aot/**` | `aot-src` | AOT codegen / shim |
| `src/vm/embed/**` | `embed-src` | Embed API |
| `tests/**/*.tc`, `run_tests.sh` | `tests-tc` | .tc 模板与注册 |
| `tests/unit/**` | `unit-tests-c` | C 单元测试 |
| `docs/**` | `docs-tc` | 设计书同步清单 |
| alwaysApply | `coding-standards`, `git-commit`, `test-patterns` | 编码 / 提交 / 测试硬要求 |
| agent-requestable | `knowledge-graph` | 跨模块索引（见上触发条件） |

## 目录地图

```
.cursor/
├── README.md                 ← 本文件
├── rules/
│   ├── coding-standards · git-commit · test-patterns   [alwaysApply]
│   ├── compiler-src [src/**] · aot-src [src/aot/**] · embed-src [embed/**]
│   ├── tests-tc · unit-tests-c · docs-tc
│   └── knowledge-graph.mdc   [L2 · 跨模块]
└── skills/
    ├── tc-architecture/      ← 路由权威 SKILL.md
    │   ├── locations · pipeline · types · errors · syntax
    │   ├── features.md → features/{platform,scalar,control}.md
    │   ├── test-map.md · gotchas.md
    │   └── kg-{dispatch,cfg,eval,module,func,embed}.md
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

## 单一事实源

| 内容 | 权威 |
|------|------|
| 关键词路由 | `skills/tc-architecture/SKILL.md` |
| RHS 分发细节 | `kg-dispatch.md` + `check_rhs_coverage.py` |
| STMT / CFG 细节 | `kg-cfg.md` |
| 测试规模数字 | `test-map.md`（`check_doc_counts.py`） |
| 易错点 | `gotchas.md` |
| 加载分级 / 工作流 | **本 README** |
| 错误码表（查码用） | `errors.md`（rg，勿通读）· 语言标准附录 B **86** 码 + OOM = 实现 **87** |

新模块：`locations.md` · `features.md` · `compiler-src.mdc` · 必要时 `gotchas.md` / `unit-tests-c.mdc`。
