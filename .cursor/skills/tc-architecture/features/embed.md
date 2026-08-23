# 特性 — TC-Embed（v0.0.39 / Phase 7）

**只读本文件** — 由 [features.md](../features.md) 路由指向。跨模块细节：[kg-embed.md](../kg-embed.md) · Rule `embed-src`。

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 公共 API | `src/vm/embed/tc_embed.h` | `TcEmbedCtx`、`TcEmbedFuncInfo`、`tc_embed_create/destroy`、`tc_embed_call`、`tc_embed_func_info`、`tc_embed_slot_write/read`、`tc_embed_ptr_encode/is_null/decode_slot` |
| 值桥接 | `src/vm/embed/tc_value_bridge.h` | `tc_value_from/to_int{8,16,32,64}`、`tc_value_from/to_uint{8,16,32,64}`、`tc_value_from/to_double/float`、`tc_value_from/to_bool` |
| VM 路径 | `src/vm/embed/tc_embed.c` | VM 模式：`TcExecuteCtx` + `tc_exec_call_function_public`；AOT 模式：`tc_aot_func_entry` 直调 |
| AOT 运行时 shim | `src/aot/tc_aot_embed_rt.h` | `tc_aot_func_entry` 函数表类型、`tc_aot_embed_abort`（非致命错误）、`tc_aot_embed_error_flag` |
| AOT codegen 嵌入模式 | `src/aot/tc_aot_codegen.c` | `tc_aot_emit_c`（`embed_mode` 参数）、`tc_aot_emit_func_table`、`tc_aot_emit_embed_header` |
| AOT CLI | `src/aot/main.c` | `--embed`、`-H/--header` |
| Executor 扩展 | `src/vm/executor/tc_executor.c/h` | `tc_exec_call_function_public`（原 static 提升为公共） |

**设计文档**：`docs/TC-Embed详细设计说明书-0.0.39.md`

**测试**：
- VM 单元：`test_embed.c` / check-embed（17 tests，含标量/float64/bool/ptr/static_var/错误路径）
- AOT 差分：`test_embed_aot.c` / check-embed-aot（7 tests，VM vs AOT 逐对对比）
- TC 用例：`tests/vm/embed/*.tc`（4 文件，ptr_sum/ptr_inplace/ptr_loop/nested_call）

**运行**：`cmake --build build --target check-embed` / `check-embed-aot`
