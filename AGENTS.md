# TC-Compiler — Agent 指南

TC 语言编译器（C99）：libtc + TC-VM + TC-AOT + TC-Embed。

- **实现版本**：`src/vm/runtime/tc_version.h`（**v0.0.39**）· Embed **v0.0.39**
- **设计目标**：**v0.0.39** — 权威文档见 `docs/*-0.0.39.md`；路线图 `docs/TC-0.0.39-开发计划.md`；Embed 见 `docs/TC-Embed详细设计说明书-0.0.39.md`
- **落地阶段**：
  - **Phase 1（A）**：类型内核 — `TcType` / equals / sizeof / 槽位（[types.md](.cursor/skills/tc-architecture/types.md)）
  - **Phase 2（B+C+D）**：`#program`/`#lib`、模块 AST、`tc_module`/`tc_scope`
  - **Phase 3（E+G）**：`tc_*_check`、多域 CFG、goto/label 仅函数内、`MISSING_RETURN`
  - **Phase 4（F+H）**：`tc_func_check` / `tc_callgraph`、static let/var
  - **Phase 5（I+J）**：调用帧、funcall、ptr/memblock/struct 运行时与 AOT shim
  - **Phase 6（K）**：CLI `-I`、无 REPL、libtc `tc_run_program`、版本 0.0.39
  - **Phase 7（L）**：TC-Embed — `src/vm/embed/` + `--embed` / 函数表 / 非致命 abort

## 效率原则（硬上限）

1. **先 `rg` 后读文** — 定位符号再打开 `.c`；勿一次读完 `tc-architecture/` 全部 md。
2. **每次只读路由指向的单文件** — 缺信息再读第二份。路由权威：Skill `tc-architecture`。
3. **Glob 已附加 Rule** — `src/`→`compiler-src`；`src/aot/`→`aot-src`；`src/vm/embed/`→`embed-src`；`tests/`→`tests-tc`；`tests/unit/`→`unit-tests-c`。
4. **`@knowledge-graph` 仅跨模块** — 索引 → **恰好一个** `kg-*.md`；单文件 / 查测试 **勿加载**。
5. **改完最小测试** — `bash scripts/run_tests.sh --filter <名>`；动 `TcRhsKind` → `check_rhs_coverage.py`。

## 决策树

```
加特性     → Skill add-compiler-feature（feature-kinds 单§）
跑测试     → Skill run-tests
找代码     → Skill tc-architecture → locations / rg
跨模块     → @knowledge-graph 索引 → 恰好一个 kg-*.md
  RHS/浮点 → kg-dispatch · CFG → kg-cfg · let/AOT → kg-eval
  模块     → kg-module · 函数 → kg-func · Embed → kg-embed
0.0.39 规范 → docs/*-0.0.39.md；类型 → types.md
写 .tc     → tests-tc（已附加）
写 unit    → unit-tests-c（已附加）
改 AOT     → aot-src（已附加）
改 Embed   → embed-src（已附加）
改 docs    → docs-tc（已附加）
Review     → Skill review-tc-code
```

## 自动附加 Rule

| Glob | Rule |
|------|------|
| `src/**` | `compiler-src`（+ `aot-src` 若 `src/aot/`） |
| `src/vm/embed/**` | `embed-src` |
| `src/aot/**` | `aot-src` |
| `tests/**/*.tc`, `run_tests.sh` | `tests-tc` |
| `tests/unit/**` | `unit-tests-c` |
| `docs/**` | `docs-tc` |
| 始终 | `coding-standards`, `test-patterns`, `git-commit` |

## Skills（勿全读）

| Skill | 触发 | 只读 |
|-------|------|------|
| `tc-architecture` | 定位/架构/**关键词路由** | 路由表指向的 **1** 个 md |
| `add-compiler-feature` | 新语法/运算符 | `feature-kinds.md` 单 § |
| `run-tests` | 构建/测试 | 失败排查表（按需） |
| `review-tc-code` | Review | checklist |

跨模块细节：`kg-dispatch` / `kg-cfg` / `kg-eval` / `kg-module` / `kg-func` / `kg-embed`（经 `@knowledge-graph` 选一）。

## 常用命令

```bash
bash scripts/run_tests.sh --filter <名>
python3 scripts/sync/check_rhs_coverage.py [--fix]
python3 scripts/sync/check_source_naming.py
make hooks
cmake --build build --target check-embed check-embed-aot
```
