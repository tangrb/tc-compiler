# 特性 — 平台层（类型 / 模块 / 函数 / 复合类型）

**只读本文件** — 由 [features.md](../features.md) 路由指向。

## 类型内核（0.0.41 Phase 1 / 模块 A）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 种类/完整类型 | `tc_types.h` | `TcTypeTag`（判别标签）；`TcType{tag,params}`；运行时/绑定/标量 AST 存 `const TcType*`（单例/`TcTypeTable`） |
| 工具 | `tc_types.c` | `tc_type_scalar` / `make_ptr|memblock|struct`；`tc_type_equals`（memblock 忽略 N）；`tc_sizeof_bits`；`tc_target_ptr_width_bits`（本实现恒为 64，64-bit-only） |
| 槽位 | `tc_types.h` / `.c` | `TcSlotDomain`；`TcRuntimeSlots`（含 memblock/struct 堆跟踪） |
| 符号扩展 | `tc_symbol.c` | `slot_domain` / `memblock_count` / `struct_id`；`TC_SYM_PARAMETER|STATIC_*` |
| 枚举规模 | `tc_types.h` | STMT **24** / RHS **34** / 错误 **86**（85 语言码 + `TC_ERR_OUT_OF_MEMORY`） |
| 覆盖闸门 | `check_rhs_coverage.py` | 8 分发点 + per-point skip（无全局 Phase1 reserved） |

测试：`tests/unit/runtime/test_types.c`（check-types）。摘要：[types.md](../types.md)。

## 模块系统（Phase 2）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| Lexer/Parser | `tc_lexer.c` / `tc_parser.c` | `#program`/`#lib`/`import`/`Self`/`@` |
| 模块检查 | `tc_module.c` | `tc_module_check_structure` / `resolve_imports` / `collect_signatures` |
| 作用域 | `tc_scope.c` | `tc_member_index_*` / `tc_scope_check_self_usage` |
| CLI/API | `tc_driver.c` / `tc_lib.c` | `-I`（`TcCompileOptions` 会话路径）/ `tc_compile_file_opts` |

细节：[kg-module.md](../kg-module.md)。测试：check-module · `tests/errors/module/` · `include_search_ok` · **import_struct_type** / **imported_struct_mid_ok** · **imported_struct_bare_name** / **imported_struct_bare_ctor** / **imported_struct_not_imported** / **imported_struct_transitive** / **imported_struct_private**。

## 函数 / 调用图 / static（Phase 4–5）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 验证 | `tc_func_check.c` | 签名、funcall、return、static let/var |
| 调用图 | `tc_callgraph.c` | `tc_callgraph_check` → `TC_CE_RECURSION` |
| VM | `tc_call_frame.c` / `tc_executor.c` | 调用帧、`FUNCALL`/`RETURN`/`FUNCALL_EXPR` |
| AOT | `tc_aot_codegen.c` | 函数 codegen + `tc_aot_emit_funcall` |

细节：[kg-func.md](../kg-func.md)。

## ptr / memblock / struct（Phase 3 验证 + Phase 5 运行时）

| 层 | 文件 | 关键符号 |
|----|------|---------|
| 验证 | `tc_{type,ptr,memblock,struct}_check.c` | `tc_type_check_rhs` 调度；memblock N≥1（类型字面量 `0` 与构造器 `count: 0` 均拒绝）；Pass2 折叠命名 N 后再 intern |
| VM | `tc_{ptr,memblock,struct}_exec.c` | load/store/ctor/field/… |
| AOT | `tc_aot_rt.c` | `tc_aot_{ptr,memblock,struct}_*` |

细节：[kg-eval.md](../kg-eval.md) · 导入 struct 限定名：[kg-module.md](../kg-module.md) · 测试账本 Phase 3/5：[test-map.md](../test-map.md)。
