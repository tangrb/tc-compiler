# TC-Embed — C 宿主零拷贝调用（v0.0.41）

**只读本文件** — 由 `@knowledge-graph` 索引指向；编辑 `src/vm/embed/` 时另见 Rule `embed-src`。

> **Phase 7（模块 L）已落地**。规格：`docs/TC-Embed详细设计说明书-0.0.41.md`

## 双模式

| 模式 | 创建 | 调用路径 |
|------|------|----------|
| VM | `tc_embed_create(program, diag)` | `TcExecuteCtx` + `tc_exec_call_function_public` |
| AOT | `tc_embed_create_aot(slots, …, table)` | `tc_aot_func_entry` 直调 |

同一套 `tc_embed_call` / `tc_embed_slot_*` / `tc_embed_ptr_*` API。

## 文件

| 文件 | 职责 |
|------|------|
| `tc_embed.h` / `.c` | 公共 API、VM 路径 |
| `tc_embed_aot.h` / `.c` | AOT 模式装配 |
| `tc_embed_internal.h` | 内部态 |
| `tc_value_bridge.h` | 标量 `tc_value_from/to_*`（header-only） |
| `tc_aot_embed_rt.h` | `tc_aot_func_entry`、非致命 `tc_aot_embed_abort` / error_flag |
| `tc_aot_codegen.c` | `embed_mode`、`tc_aot_emit_func_table`、`tc_aot_emit_embed_header` |
| `aot/main.c` | `--embed`、`-H/--header` |

## 公共 API（速查）

```
tc_embed_create / tc_embed_create_aot / tc_embed_destroy
tc_embed_func_info / tc_embed_top_var_slot / tc_embed_self_var_slot
tc_embed_slot_count / tc_embed_slot_write / tc_embed_slot_read
tc_embed_ptr_encode / tc_embed_ptr_is_null / tc_embed_ptr_decode_slot
tc_embed_call / tc_embed_get_error / tc_embed_had_error
```

## 必守

1. **非致命 abort**（AOT）：运行时错置 error_flag，不 `abort()` 进程
2. 值桥接目前覆盖**标量**；memblock/struct 宿主互操作仍 reserved（见 `tc_value_bridge.h` 注释）
3. 槽位与程序 `TcRuntimeSlots` 布局一致；ptr 用 slot 编码约定
4. 改完跑 `check-embed` + `check-embed-aot`

## 测试

| 入口 | 覆盖 |
|------|------|
| `test_embed.c` / check-embed | 生命周期、标量/float64/bool/ptr、static、错误 |
| `test_embed_aot.c` / check-embed-aot | VM vs AOT 差分；除零不终止 |
| `tests/vm/embed/*.tc` | ptr_sum / ptr_inplace / ptr_loop / nested_call |

账本：[test-map.md](test-map.md) Phase 7 · 特性表：[features/embed.md](features/embed.md)
