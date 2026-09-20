# Agent 易错点（勿通读其它 kg）

**何时读**：改模块/const/AOT 复合字段/`memcopy_unsafe`/uninit 前扫一眼；或 Review 怀疑「看起来对但历史踩过坑」。  
**勿**为普通标量改动加载本文件。

## 模块 / 结构体注册

| 错觉 | 事实 |
|------|------|
| deps 收集后**简单逆序**即可 importee 先注册 | 菱形依赖（A→C、B→C）下逆序非法。须 `tc_module_topological_dep_order`（`tc_module.c`）再 `tc_struct_table_register_program` |
| 传递依赖模块的裸名/限定名可用 | 仅**本文件直接 import** 的 `Mod.Name`；传递依赖 → `UNDEFINED_STRUCT` |
| 导入 private struct → undefined | 须 `PRIVATE_MEMBER_ACCESS`，不得降为 undefined |
| 导入 private `static let`/`static var` → undefined | 同样须 `PRIVATE_MEMBER_ACCESS`（`<模块>.<名>` 读/写/取址/read 目标/memblock 操作数一律；`tc_qualified_member_allowed` / `tc_reject_private_member_access`）。符号表以**裸成员名**存放且带模块标记，按限定名直接查表查不到 |
| `Self.<名>` 是裸名查全局表 | 必须先用**当前模块成员索引**判定归属（`tc_resolve_self_member`）；否则 `Self.K` 会命中另一模块同名成员（含 private） |
| `<模块>.<名> = rhs` 会走整绑定赋值 | 解析器把它塑造成**单字段字段赋值**（`Self.<名>` 才有专用 ASSIGN 分支）；分析器须先重分类（`tc_reclassify_qualified_binding_assign`），否则报 `undefined variable '<模块名>'` |

测试：`diamond_import_*` · `imported_struct_*` · `test_module.c` · `member_private_*` · `SelfForeign{Priv,Pub,Field}Lib` · `import_member_assign_*`

## let / static let / const 堆

| 错觉 | 事实 |
|------|------|
| memblock 逐值构造计数只靠 pass2 | **static let** 在 pass2 前走 `tc_eval_const_memblock_ctor`；`value_count != count` 必须在此拒绝（曾堆越界写） |
| `const_value.bits` 相等 ⇒ 堆别名 | 仅 **STRUCT/MEMBLOCK** 的 bits 是堆地址；标量位模式可能碰巧相等（`tc_const_heap_named`） |
| AOT 可把 `const_bits`（分析期指针）写进生成 C | **禁止**。标量：codegen 折叠位；STRUCT/MEMBLOCK **整值**（`CONST_REF` / `SELF_MEMBER` / operand 赋值 / FIELD_READ 0 字段）与 const 基址字段：字节内联 + `tc_aot_struct_extract`（或 `tc_aot_memblock_from_bytes`）运行期重建 |
| `let x = Self.K`（常量上下文）可用 | 常量求值须有 `TC_RHS_SELF_MEMBER` 分支（`tc_eval_const_rhs`）；`static let M = Self.K` 另走 `tc_eval_one_static_let`，两个常量上下文不得行为不一致 |
| `.count` 的常量绑定按名查符号即可 | 常量绑定槽位为 −1 且符号表以裸名存放，限定名按名查不到；AOT 须按 `binding.type` 折叠声明 `N`（`TC_RHS_MEMBLOCK_COUNT` 分支） |

测试：`static_let_memblock_count_*` · `struct_field_const_base_*` · `self_const_agg` · `const_{struct,memblock}_copy` · `memblock_count_rhs` · `import_member_assign_*`

## memcopy_unsafe / 索引

| 错觉 | 事实 |
|------|------|
| 先按 `TC_USIZE` 求值再判负 | 负字面量/有符号绑定会回绕成巨大 usize，判负失效。须先按操作数有符号性选 `TC_ISIZE`/`TC_USIZE`（VM：`tc_memblock_read_index_kind`；AOT：`tc_aot_memcopy_index_type`） |

测试：`memcopy_unsafe_neg*` · `memcopy_unsafe_positive_ok`

## CFG / 诊断 / 固定点

| 错觉 | 事实 |
|------|------|
| `tc_analyze_definite_init` 在 `tc_analyzer_dfa.c` | **在 `tc_cfg.c`**（`tc_analyze_definite_init_all`）；dfa 文件多为 defer 辅助 |
| `var` 缺 `=` → `SYNTAX` | 必须 `TC_CE_VAR_MISSING_INIT` |
| 跳过初始化 → 专用 goto 错 | 统一 `UNINITIALIZED_VARIABLE`（无 `GOTO_SKIPS_VAR_INIT`） |

## I/O / AOT / Embed

| 错觉 | 事实 |
|------|------|
| executor / `tc_aot_rt` 可内联 format | 只委托 `tc_io.c` |
| Embed AOT 运行时错应 `abort` 进程 | 非致命：`tc_aot_embed_abort` + error_flag |

## 加载纪律（复述）

- 本文件与任意 `kg-*.md`：**同任务最多再加一个** kg，勿堆叠。
- 单文件小改：`rg` + 目标 `.c`，不必开 gotchas。
