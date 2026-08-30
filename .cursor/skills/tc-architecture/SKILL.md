---
name: tc-architecture
description: >-
  Navigate TC-Compiler — route by keyword to ONE doc (locations, pipeline, types,
  errors, syntax, features, test-map, gotchas, or one kg-*.md). Use for "where is
  X", call chains, AST/enums, or choosing what to read. Never load all subdocs.
---

# TC-Compiler Architecture

**v0.0.41** · 导航 [.cursor/README.md](../../README.md) · 易错点 [gotchas.md](gotchas.md)

## 三步

1. 下表选 **一行** → 打开对应 md。  
2. **只读该 md**；缺信息再读第二份。  
3. 跨模块：`@knowledge-graph` → **恰好一个** `kg-*.md`。

## 路由（一行一事）

| 意图 / 关键词 | 读这一份 | 勿同时读 |
|---------------|----------|----------|
| 符号在哪、定位 | [locations.md](locations.md) | pipeline / features |
| 调用链、Analyze 顺序、CFG 编排 | [pipeline.md](pipeline.md) | locations |
| 历史坑、菱形 import、const 堆、AOT 字段、memcopy 负下标 | [gotchas.md](gotchas.md) | 多个 kg-* |
| AST / 枚举 / equals / sizeof / 槽位 | [types.md](types.md) | syntax |
| 错误码、stderr、诊断阶段、测试子串 | [errors.md](errors.md)（先读文首「Agent」） | 通读 86 码表 |
| 语法 EBNF 摘要 | [syntax.md](syntax.md) | 语言标准全文 |
| 某特性改哪一层 | [features.md](features.md) → **一行** → `features/*.md` | 其它 features |
| 测试账本、是否已有用例 | [test-map.md](test-map.md)（先 `rg scripts/`） | kg-* |
| 加特性路径 | [feature-kinds.md](../add-compiler-feature/feature-kinds.md) **单 §** | 全文 |
| 浮点 / cast / bitcast / 新 RHS | `@knowledge-graph` + [kg-dispatch.md](kg-dispatch.md) | 其它 kg |
| goto / CFG / uninit / 块作用域 / 诊断阶段细节 | `@knowledge-graph` + [kg-cfg.md](kg-cfg.md) | 其它 kg |
| let / 短路 / AOT / 复合运行时 | `@knowledge-graph` + [kg-eval.md](kg-eval.md) | 其它 kg |
| `#program` / import / Self / 导入 struct | `@knowledge-graph` + [kg-module.md](kg-module.md) | 其它 kg |
| func / funcall / 调用图 / static | `@knowledge-graph` + [kg-func.md](kg-func.md) | 其它 kg |
| TC-Embed / `--embed` / value_bridge | `@knowledge-graph` + [kg-embed.md](kg-embed.md) | features 重复开 |
| I/O / format | `tc_io.c` + [features/scalar.md](features/scalar.md) § I/O | — |
| 位运算 / shift | `tc_semantics.c` + [features/scalar.md](features/scalar.md) § 位运算 | — |
| if / while / break / continue | [features/control.md](features/control.md) 对应 § | 必要时再 kg-cfg |
| stmt_index / DFS | `tc_stmt_index.h` → [kg-cfg.md](kg-cfg.md) | — |
| 跑测试 / CI | Skill `run-tests` | — |
| Review / PR | Skill `review-tc-code` | — |
| 0.0.41 语言/编译器规范全文 | `docs/TC语言标准设计说明书-0.0.41.md` 等 | 整份 skill |
| libtc API | `docs/libtc设计说明书-0.0.41.md` | pipeline |

## Pipeline（一行）

```
source → tokenize → parse → analyze(模块+Pass1/2+CFG+调用图)
       → execute | aot_emit | embed ；I/O→tc_io.c ；cast→tc_sem_cast.c
```

详情：[pipeline.md](pipeline.md)。
