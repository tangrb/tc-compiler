# 特性实现地图（索引）

**只读一个文件** — 从下表选一行，打开对应 `features/*.md`；勿批量加载。

> **v0.0.39** Phase 1–6（含 struct 运行时）+ Embed v0.0.39  
> 跨模块分发点：Rule `@knowledge-graph` · 细节 `kg-*.md`（每次只读一个）

## 路由表

| 特性 / 问题 | 读这一份 |
|-------------|---------|
| 类型内核 / equals / sizeof / 槽位 | [features/platform.md](features/platform.md) § 类型内核 |
| `#program` / import / Self / `-I` | [features/platform.md](features/platform.md) § 模块 · 或 [kg-module.md](kg-module.md) |
| func / funcall / 调用图 / static | [features/platform.md](features/platform.md) § 函数 · 或 [kg-func.md](kg-func.md) |
| ptr / memblock / struct | [features/platform.md](features/platform.md) § 复合类型 · 或 [kg-eval.md](kg-eval.md) |
| bool | [features/scalar.md](features/scalar.md) § bool |
| 比较 eq/ne/lt/… | [features/scalar.md](features/scalar.md) § 比较 |
| 逻辑 and/or/not 短路 | [features/scalar.md](features/scalar.md) § 逻辑 |
| 位运算 / 移位 | [features/scalar.md](features/scalar.md) § 位运算 |
| let 编译期常量 | [features/scalar.md](features/scalar.md) § let · 或 [kg-eval.md](kg-eval.md) |
| I/O / format | [features/scalar.md](features/scalar.md) § I/O |
| float32/float64 / cast / bitcast | [features/scalar.md](features/scalar.md) § 浮点 · 或 [kg-dispatch.md](kg-dispatch.md) |
| if / 块作用域 | [features/control.md](features/control.md) § if |
| while / break / continue | [features/control.md](features/control.md) § while |
| goto / label / 跳转表 | [features/control.md](features/control.md) § goto |
| var 缺初始化器 | [features/control.md](features/control.md) § var |
| CFG / 未初始化 / 静态布尔剪枝 | [features/control.md](features/control.md) § CFG · 或 [kg-cfg.md](kg-cfg.md) |
| 诊断阶段 / 优先级 | [features/control.md](features/control.md) § 诊断 · [errors.md](errors.md) |
| TC-Embed / `--embed` | [features/embed.md](features/embed.md) · 或 [kg-embed.md](kg-embed.md) |

## 加新特性时同步更新

1. 在对应 `features/*.md` 新增或扩展 §
2. [test-map.md](test-map.md) + `@knowledge-graph` + 对应 `kg-*.md`
3. `errors.md` / `syntax.md`（语言变更）
4. `scripts/vm/run_tests.sh`（+ AOT）
5. 新 `TcRhsKind` → `check_rhs_coverage.py [--fix]`
6. 新特性类型 → 本路由表追加一行
