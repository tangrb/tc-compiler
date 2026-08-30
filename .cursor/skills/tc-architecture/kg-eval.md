# 求值语义 — let · 短路 · 重载/shift · 复合运行时 · 内存 · AOT

**只读本文件** — 由 `@knowledge-graph` 索引指向；勿与其它 `kg-*.md` 同时整读。

> **v0.0.42**：标量 let + static let 拓扑、调用帧、ptr/memblock/struct 运行时与 AOT shim 已落地。规格：`docs/*-0.0.42.md`。

## let 编译期求值

```
tc_pass2 → tc_resolve_const_value → tc_eval_const_rhs
  LIT/CONST_REF/ARITH/UNARY/COMPARE/LOGIC_*/BITWISE_*/SHIFT/CONST_CAST/BITCAST
  + STRUCT_CONSTRUCTOR（let）
  源序可见性阻止自引用/前向引用；运行时错 → tc_const_map_runtime_error
→ TcSymbol.const_value（规范化 TcValue，slot=-1）→ CFG 常量剪枝
→ Executor/AOT 在使用处内联；let 声明不产生运行时槽
```

允许：整数 wrap、浮点 ieee、整数窄化 truncate、等宽 bitcast；每个 let RHS 最多一个调用层。  
禁：var 引用、调用嵌套、`FUNCALL_EXPR`、自引用、前向 let；ptr/memblock/field/self 在 const_eval **defer**。  
操作×类型×模式由 `tc_validate_*_mode` 共用矩阵；逻辑短路仍先校验两个原子操作数。

**memblock 逐值构造**：`tc_eval_const_memblock_ctor` 须 `value_count == count`（static let 早于 pass2；见 [gotchas.md](gotchas.md)）。  
**AOT const 复合字段**：禁止嵌入分析期堆指针；字节内联 + `tc_aot_struct_extract`（同 gotchas）。

**static let**：`tc_func_eval_static_lets` 拓扑求值（H-5）— 见 [kg-func.md](kg-func.md)。

## 逻辑短路（Analyzer + CFG + Executor + let）

```
Pass2：lhs 为 false/true 字面量或更早可见 let bool（is_const）→ 临时 check_init=0 仍检名称/类型
CFG：同上条件 → tc_cfg_add_rhs_reads 不记录 rhs 读槽
Executor/AOT：and 的 lhs bits==0、or 的 lhs bits!=0 → 不求 rhs
静态条件：tc_try_eval_static_bool → lit / const-ref / int+float compare / logic / strict bool cast
左为 var → UNKNOWN，保守认为 RHS 可达（不做跨语句常量推测）
```

## and/or/not 重载 + shift

```
tc_parse_and_or_not_rhs(type):
  TC_BOOL → LOGIC_*（短路）；整数 → BITWISE_*（无短路）
xor → BITWISE_BIN（仅整数）
SHIFT: shl 可选 wrap（let 禁）；shr 禁 wrap（显式算术右移，不依赖宿主 `>>`）；k>=n → shr=0；
strict shl 溢出报错；有符号 `shl(int64, -2^62, 1)` = INT64_MIN（负边界用 `(1ULL<<63)/pow2`）
```

## 复合类型运行时

| 层 | 文件 |
|----|------|
| 验证 | `tc_{type,ptr,memblock,struct}_check.c` |
| VM | `tc_{ptr,memblock,struct}_exec.c` ← `tc_executor.c` |
| AOT | `tc_aot_codegen.c` + `tc_aot_{ptr,memblock,struct}_*`（`tc_aot_rt.c`） |
| 存储 | `TcRuntimeSlots`：`memblock_storage` / `struct_storage`（memblock 头 64-bit-only） |

结构体名解析（Analyzer，注册结构体表时）：裸名仅本模块；导入须 `Mod.Name`；表按 `(module_name, name)`。见 [kg-module.md](kg-module.md)。

## 内存安全

```
OOM 诊断：Implementation/TC_ERR_OUT_OF_MEMORY（非语法）；后备消息 "memory allocation failed"
  分发：parser / analyzer+CFG / symbol / lexer / executor / libtc / embed
tc_warning_list_add：先 strdup 再 realloc；失败 free(msg_copy) 回滚
tc_rhs_free / tc_statement_free / tc_operand_free：入口 NULL 守卫
```

```bash
rg "TC_ERR_OUT_OF_MEMORY" src/
rg "memory allocation failed" src/
cmake --build build --target check-warning   # 无语言警告，空壳
python3 scripts/sync/check_rhs_coverage.py
```

## 流水线（摘要）

```
libtc: tokenize → parse → analyze(模块+Pass1/2+CFG+调用图) → tc_run_program → execute
execute: tc_eval_rhs → tc_exec_* / ptr·memblock·struct_exec；I/O → tc_io.c；funcall → call_frame
AOT: tc_aot_emit_rhs → shim → tc_exec_* / tc_io_* / tc_aot_{ptr,memblock,struct}_*
Embed: tc_embed_call → VM 或 AOT 函数表（见 kg-embed.md）
```

详情：[pipeline.md](pipeline.md)

## AOT 要点

- 运算 shim → `tc_semantics` / `tc_sem_*`；I/O → `tc_io.c`；转换 → `tc_sem_cast.c`
- 复合：`tc_aot_ptr_*` / `tc_aot_memblock_*` / `tc_aot_struct_*`
- 未初始化哨兵 `TC_UNINITIALIZED_SLOT_BITS` 只保护内部不变量；可达未初始化在 Analyze 拒绝
- AOT if/while：原生 C；break/continue 经最内层 loop id；goto/label 无 rt shim
- Embed 模式：非致命 `tc_aot_embed_abort` — [kg-embed.md](kg-embed.md)
