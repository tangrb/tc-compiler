---
name: tc-architecture
description: Navigate TC-Compiler — keyword routing, symbol locations, pipeline, types, errors, syntax. Use for "where is X", call chains, AST lookup, or which file to read. Read ONLY the one md the routing table points to; never load all subdocs.
---

# TC-Compiler Architecture

**实现版本**：v0.0.39（`src/vm/runtime/tc_version.h`）· **Embed**：v0.0.39 · **设计文档**：`docs/*-0.0.39.md` / Embed 详设-0.0.39  

**Phase 1–7 已落地**：类型内核 · 模块 · 复合类型验证 · 多域 CFG · 函数/调用图/static · Executor+AOT（含 struct）· CLI/`-I`/无 REPL · TC-Embed。

路线图：[TC-0.0.39-开发计划.md](../../../docs/TC-0.0.39-开发计划.md)

## 效率规则

1. **本 SKILL.md 是唯一路由表** — 按问题读 **一行** 指向的 md；禁止批量打开子文档。
2. **先 `rg` 后读文** — [locations.md](locations.md) 底部有常用命令。
3. **跨模块** → `@knowledge-graph` 索引 → **恰好一个** [kg-dispatch](kg-dispatch.md) / [kg-cfg](kg-cfg.md) / [kg-eval](kg-eval.md) / [kg-module](kg-module.md) / [kg-func](kg-func.md) / [kg-embed](kg-embed.md)；**测试映射** → [test-map.md](test-map.md)。
4. 编辑 `src/` 时 `compiler-src` 已附加（`src/aot/`→`aot-src`；`src/vm/embed/`→`embed-src`）。
5. 编辑 `docs/` 时 `docs-tc` 已附加 — 文档文件名带版本后缀。

## 读什么（一行一事）

| 问题 | 读这一份 | 勿读 |
|------|---------|------|
| 符号在哪个文件？ | [locations.md](locations.md) | pipeline, types, features |
| 调用链 / Analyze 顺序 / CFG？ | [pipeline.md](pipeline.md) | locations |
| 0.0.39 十三阶段管线规格 | `docs/TC编译器标准设计说明书-0.0.39.md` | — |
| AST / 枚举 / 类型内核？ | [types.md](types.md) | syntax |
| 错误种类 / stderr / 诊断阶段？ | [errors.md](errors.md) + 对应版本标准 | — |
| 语法 EBNF 摘要？ | [syntax.md](syntax.md) | — |
| 0.0.39 完整语法/语义 | `docs/TC语言标准设计说明书-0.0.39.md` | 整份 skill 子文档 |
| 某特性改哪些层？ | [features.md](features.md) **单 §** | 其他 § |
| 测试 / 特性覆盖？ | [test-map.md](test-map.md) | knowledge-graph / kg-* |
| 加新特性路径？ | [feature-kinds.md](../add-compiler-feature/feature-kinds.md) **单 §** | 全 skill |
| 浮点 / cast / bitcast / RHS kinds？ | `@knowledge-graph` + [kg-dispatch.md](kg-dispatch.md) | 其它 kg-* |
| goto / CFG / uninit / 诊断阶段 / 块作用域？ | `@knowledge-graph` + [kg-cfg.md](kg-cfg.md) | 其它 kg-* |
| let / 短路 / 重载+shift / OOM / AOT / 复合运行时？ | `@knowledge-graph` + [kg-eval.md](kg-eval.md) | 其它 kg-* |
| `#program` / `#lib` / import / Self / `-I`？ | `@knowledge-graph` + [kg-module.md](kg-module.md) | 其它 kg-* |
| func / funcall / return / 调用图 / static / 调用帧？ | `@knowledge-graph` + [kg-func.md](kg-func.md) | 其它 kg-* |
| TC-Embed / value_bridge / `--embed`？ | `@knowledge-graph` + [kg-embed.md](kg-embed.md) | 其它 kg-* |
| 跑测试？ | Skill `run-tests` | — |
| libtc API？ | `docs/libtc设计说明书-0.0.39.md` 或 `docs/libtc-api-0.0.39.md` | pipeline |
| 0.0.39 开发任务拆分 | `docs/TC-0.0.39-开发计划.md` | 全套设计书 |

## 关键词路由（只选一行）

| 意图 / 关键词 | 第一步 | 第二步 |
|---------------|--------|--------|
| 测试、CI、回归 | Skill `run-tests` | [errors.md](errors.md) |
| 测试映射、某特性已有用例 | [test-map.md](test-map.md) | `rg 名 scripts/` |
| 在哪、定位 | [locations.md](locations.md) | 目标 `.c` |
| 调用链 | [pipeline.md](pipeline.md) | — |
| 加特性、新运算符 | [feature-kinds.md](../add-compiler-feature/feature-kinds.md) § | [features.md](features.md) 单 § |
| Review / PR | Skill `review-tc-code` | `coding-standards` |
| I/O / format | `tc_io.c` | [features.md](features.md) § I/O |
| let 编译期 | `tc_const_eval.c` | [kg-eval.md](kg-eval.md) |
| 位运算 / shift | `tc_semantics.c` | [features.md](features.md) § 位运算；[kg-eval.md](kg-eval.md) |
| if / 块作用域 | `tc_parser.c` + `tc_symbol.c` | [kg-cfg.md](kg-cfg.md) |
| while / break / continue | [features.md](features.md) § while | [kg-cfg.md](kg-cfg.md) |
| goto / label / 未初始化 / CFG | [kg-cfg.md](kg-cfg.md) | VM/AOT 详设-0.0.39 |
| 诊断阶段 / 优先级 / var 缺初始化 | [errors.md](errors.md) §诊断阶段 | [kg-cfg.md](kg-cfg.md) |
| 静态布尔 / 短路读集 | [kg-eval.md](kg-eval.md) | `tc_const_eval.c` + `tc_cfg.c` |
| stmt_index / DFS 序号 | `tc_stmt_index.h` | [kg-cfg.md](kg-cfg.md) |
| OOM / 内存安全 / 错误码 | [errors.md](errors.md) + `tc_types.h` | [kg-eval.md](kg-eval.md) |
| 新 TcRhsKind / 分发点 | `@knowledge-graph` 分发点表 | [kg-dispatch.md](kg-dispatch.md) + `check_rhs_coverage.py` |
| AST / 枚举 / 类型内核 / equals / sizeof | [types.md](types.md) | `tc_types.h` |
| 语法摘要 | [syntax.md](syntax.md) | — |
| libtc API | `docs/libtc设计说明书-0.0.39.md` | `tc_lib.c` |
| **#program / #lib / import / Self / static 模块** | [kg-module.md](kg-module.md) | `tc_module.c` / `tc_scope.c` |
| **func / funcall / return / 调用图 / 调用帧** | [kg-func.md](kg-func.md) | `tc_func_check.c` / `tc_callgraph.c` / `tc_call_frame.c` |
| **ptr / memblock / struct 类型表示** | [types.md](types.md) + [features.md](features.md) §类型内核 | — |
| **ptr / memblock / struct 验证+运行时** | [kg-eval.md](kg-eval.md) | `tc_*_check.c` / `tc_*_exec.c` / `tc_aot_rt.c` |
| **TC-Embed / 宿主调用 / `--embed`** | [kg-embed.md](kg-embed.md) | Rule `embed-src` |
| **十三阶段 / 模块搜索路径 / 无 REPL** | `docs/TC编译器标准设计说明书-0.0.39.md` + VM 命令行参考-0.0.39 | 开发计划 |

## 特性速查（→ features.md 单 §）

**标量管线**：bool · 比较 · 逻辑短路 · 位运算/移位 · let · I/O · if · while · bitcast · var 强制初始化 · goto/label · 完整 CFG · 诊断阶段  

**0.0.39**：`TcType` 内核 · 模块 · 函数/调用图 · ptr/memblock/struct 验证与运行时 · 多域 CFG · CLI `-I`  

**0.0.39**：TC-Embed（VM + AOT 嵌入）— [features.md](features.md) § TC-Embed  

## Pipeline（一行）

```
source → tokenize → parse → analyze(模块+Pass1/2+CFG+调用图)
       → execute | aot_emit | embed ；I/O → tc_io.c ；cast/bitcast → tc_sem_cast.c
```

详情：[pipeline.md](pipeline.md)。
