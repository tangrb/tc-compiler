---
name: tc-architecture
description: >-
  Navigate TC-Compiler codebase — keyword routing to symbol locations, pipeline,
  types, errors, syntax, features, and tests. Use when locating code ("where is X"),
  tracing call chains, understanding AST/enums, or choosing which doc to read.
  Read ONLY the single md file the routing table points to; never load all subdocs.
---

# TC-Compiler Architecture

**v0.0.39** · Embed v0.0.39 · 规格 `docs/*-0.0.39.md` · 导航 [.cursor/README.md](../../README.md)

## 三步工作流

1. **查下表** — 按意图选 **一行**，确定目标 md。
2. **只读该 md** — 读完即停；缺信息再读第二份。
3. **跨模块** — 加载 `@knowledge-graph` → **恰好一个** `kg-*.md`。

## 读什么（一行一事）

| 问题 | 读这一份 | 勿读 |
|------|---------|------|
| 符号在哪个文件？ | [locations.md](locations.md) | pipeline, types, features |
| 调用链 / Analyze 顺序 / CFG？ | [pipeline.md](pipeline.md) | locations |
| 0.0.39 十三阶段管线规格 | `docs/TC编译器标准设计说明书-0.0.39.md` | — |
| AST / 枚举 / 类型内核？ | [types.md](types.md) | syntax |
| 错误种类 / stderr / 诊断阶段？ | [errors.md](errors.md) | — |
| 语法 EBNF 摘要？ | [syntax.md](syntax.md) | — |
| 0.0.39 完整语法/语义 | `docs/TC语言标准设计说明书-0.0.39.md` | 整份 skill 子文档 |
| 某特性改哪些层？ | [features.md](features.md) → **一行** → `features/*.md` | 其它 features 文件 |
| 测试 / 特性覆盖？ | [test-map.md](test-map.md) | kg-* |
| 加新特性路径？ | [feature-kinds.md](../add-compiler-feature/feature-kinds.md) **单 §** | 全 skill |
| 浮点 / cast / bitcast / RHS kinds？ | `@knowledge-graph` + [kg-dispatch.md](kg-dispatch.md) | 其它 kg-* |
| goto / CFG / uninit / 块作用域？ | `@knowledge-graph` + [kg-cfg.md](kg-cfg.md) | 其它 kg-* |
| let / 短路 / AOT / 复合运行时？ | `@knowledge-graph` + [kg-eval.md](kg-eval.md) | 其它 kg-* |
| `#program` / `#lib` / import / Self？ | `@knowledge-graph` + [kg-module.md](kg-module.md) | 其它 kg-* |
| func / funcall / 调用图 / 调用帧？ | `@knowledge-graph` + [kg-func.md](kg-func.md) | 其它 kg-* |
| TC-Embed / value_bridge / `--embed`？ | `@knowledge-graph` + [kg-embed.md](kg-embed.md) | 其它 kg-* |
| 跑测试？ | Skill `run-tests` | — |
| libtc API？ | `docs/libtc设计说明书-0.0.39.md` | pipeline |

## 关键词路由（只选一行）

| 意图 / 关键词 | 第一步 | 第二步 |
|---------------|--------|--------|
| 测试、CI、回归 | Skill `run-tests` | [errors.md](errors.md) |
| 测试映射、某特性已有用例 | [test-map.md](test-map.md) | `rg 名 scripts/` |
| 在哪、定位 | [locations.md](locations.md) | 目标 `.c` |
| 调用链 | [pipeline.md](pipeline.md) | — |
| 加特性、新运算符 | [feature-kinds.md](../add-compiler-feature/feature-kinds.md) § | [features.md](features.md) 路由 → 一个 `features/*.md` |
| Review / PR | Skill `review-tc-code` | `coding-standards` |
| I/O / format | `tc_io.c` | [features/scalar.md](features/scalar.md) § I/O |
| let 编译期 | `tc_const_eval.c` | [features/scalar.md](features/scalar.md) § let |
| 位运算 / shift | `tc_semantics.c` | [features/scalar.md](features/scalar.md) § 位运算 |
| if / 块作用域 | `tc_parser.c` + `tc_symbol.c` | [features/control.md](features/control.md) § if |
| while / break / continue | [features/control.md](features/control.md) § while | [kg-cfg.md](kg-cfg.md) |
| goto / label / 未初始化 / CFG | [kg-cfg.md](kg-cfg.md) | VM/AOT 详设-0.0.39 |
| 诊断阶段 / var 缺初始化 | [errors.md](errors.md) §诊断阶段 | [kg-cfg.md](kg-cfg.md) |
| 静态布尔 / 短路读集 | [kg-eval.md](kg-eval.md) | `tc_const_eval.c` + `tc_cfg.c` |
| stmt_index / DFS 序号 | `tc_stmt_index.h` | [kg-cfg.md](kg-cfg.md) |
| OOM / 错误码 | [errors.md](errors.md) + `tc_types.h` | [kg-eval.md](kg-eval.md) |
| 新 TcRhsKind / 分发点 | `@knowledge-graph` 分发点表 | [kg-dispatch.md](kg-dispatch.md) |
| AST / equals / sizeof | [types.md](types.md) | `tc_types.h` |
| **#program / import / Self** | [kg-module.md](kg-module.md) | `tc_module.c` |
| **func / funcall / 调用图** | [kg-func.md](kg-func.md) | `tc_func_check.c` |
| **ptr / memblock / struct** | [types.md](types.md) + [kg-eval.md](kg-eval.md) | — |
| **TC-Embed / `--embed`** | [kg-embed.md](kg-embed.md) | Rule `embed-src` |

## Pipeline（一行）

```
source → tokenize → parse → analyze(模块+Pass1/2+CFG+调用图)
       → execute | aot_emit | embed ；I/O → tc_io.c ；cast/bitcast → tc_sem_cast.c
```

详情：[pipeline.md](pipeline.md)。
