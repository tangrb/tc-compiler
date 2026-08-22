# 编译流水线与关键函数

> **现网实现 v0.0.39**（Phase 1–6，含 struct 运行时）+ **Embed v0.0.39**。13 阶段规格见 `docs/TC编译器标准设计说明书-0.0.39.md`。  
> 诊断阶段见 [errors.md](errors.md) / 语言标准-0.0.39。

Analyze：Pass1/2 + 模块/函数检查 + `tc_cfg_build_all` + `tc_analyze_definite_init_all` + 调用图。  
**无 REPL**（无 `tc_repl` / `tc_analyze_statement`）。

## libtc 入口 (`src/libtc/tc_lib.c`)

```
tc_compile_source(source, name, …)
  └─ tc_parse_source
  └─ tc_analyze               /* 无路径：结构检查，不解析 import */
tc_compile_file_opts(path, opts) → read + tc_analyze_ex(path, opts 会话搜索路径)
  /* opts = TcCompileOptions；无进程级全局 */
tc_run_program → tc_execute   /* 含 static var 拓扑初始化 */
```

文件模式 parse：`tc_parse_source_to_program` 扫描 `TcSourceLine[]`，`if`/`while`/func 走通用块解析。

## Analyze 编排 (`tc_analyze_ex`)

```
tc_typed_program_init
→ tc_module_check_structure          /* 4a */
→ tc_module_resolve_imports          /* 4b/4c，需 entry_path */
→ tc_struct_table_register_program
→ tc_module_collect_signatures       /* 4d */
→ tc_func_check_signatures           /* 5 */
→ tc_pass1_collect_symbols           /* deps + 入口 */
→ tc_member_index_build
→ tc_func_eval_static_lets           /* H-5 */
→ tc_func_check_static_vars          /* H-6 */
→ tc_pass2_type_check                /* 6：含 6a/6c/6d/6e */
→ tc_cfg_build_all + tc_analyze_definite_init_all
→ tc_callgraph_check                 /* 12 */
```

## 文件模式 (`src/vm/driver/tc_driver.c`)

```
main → (-I → TcCompileOptions) → tc_run_file
  └─ tc_compile_file_opts → tc_run_program → tc_execute
       └─ tc_execute_statement_impl
            ├─ IF/WHILE → 条件 tc_eval_rhs(TC_BOOL) → 递归块 / 控制结果传播
            ├─ FUNCALL / RETURN / 调用帧
            ├─ ptr/memblock/struct 语句 → tc_*_exec
            └─ 其他 → tc_eval_rhs → tc_exec_*
```

## AOT (`src/aot/main.c`)

```
(-I → TcCompileOptions) → tc_compile_file_opts → tc_aot_emit_c
[--embed] 函数表 + embed header；[-H] 宿主头
[--run] cc 链接 tc_aot_rt.c + semantics/types/diagnostic
```

`tc_aot_rt.c` 委托共享 semantics；转换统一走 `tc_sem_cast.c`，I/O 与 executor 共享 `tc_io.c`。  
header-only `tc_stmt_index.h` 为 Analyzer/Executor/AOT 共用 DFS 先序编号模块。

## Embed (`src/vm/embed/`)

```
tc_embed_create(program) | tc_embed_create_aot(slots, table)
  └─ tc_embed_call → VM: tc_exec_call_function_public
                  → AOT: tc_aot_func_entry（非致命 abort）
```

细节：[kg-embed.md](kg-embed.md)

## 各层入口函数

| 层 | 关键函数 |
|----|---------|
| Lexer | `tc_tokenize_line`, `tc_keyword_token` |
| Parser | `tc_parse_statement`, `tc_parse_if_stmt`, `tc_parse_while_stmt`, `tc_parse_source_to_program`, `tc_parse_const_rhs`, `tc_statement_free` |
| Symbol | `tc_symbol_table_add/find`, `push_scope/pop_scope`, `find_in_scope` |
| Module | `tc_module_check_structure`, `tc_module_resolve_imports`, `tc_module_collect_signatures`, `tc_scope_*` |
| Analyzer | `tc_analyze` / `tc_analyze_ex`, Pass1/Pass2, `tc_cfg_build_all`, `tc_func_check*`, `tc_callgraph_check`, `tc_eval_const_rhs` |
| Executor | `tc_execute`, `tc_execute_statement_impl`, `tc_eval_rhs`, `tc_call_frame`, `tc_{ptr,memblock,struct}_exec` |
| Semantics | `tc_exec_arith/unary/compare/logic_*`, `tc_exec_cast/truncate/bitcast` |
| Types | `tc_type_parse`, `tc_type_equals`, `tc_sizeof_bits`, `tc_type_make_*` |
| stmt_index | `tc_stmt_index_reset/take`, `tc_stmt_subtree_index_count`, `tc_stmt_index_skip_block` |
| AOT | `tc_aot_emit_c`, `tc_aot_emit_statement_impl`, `tc_aot_{arith,ptr,memblock,struct}_*` |
| Embed | `tc_embed_create`, `tc_embed_call`, `tc_embed_slot_*` |
| I/O | `tc_io_write_formatted`, `tc_io_write_value`, `tc_io_read_value` |
| libtc | `tc_compile_source`, `tc_compile_file_opts`, `tc_run_source`, `tc_run_file`, `tc_run_program` |

## I/O 模块（VM/AOT 共享）

```
tc_execute_statement (WRITE/READ)     [executor.c]
  └─ tc_io_write_formatted / tc_io_read_value   [tc_io.c]

tc_aot_write / tc_aot_read            [tc_aot_rt.c]
  └─ tc_io_*                            [tc_io.c]
```

格式解析在 analyzer（`tc_check_io_format` / `tc_analyze_6e.c`）。

## Analyzer 要点

```
Pass1：DFS；if 双作用域、while/func 作用域；固定 slot；loop id；tc_mark_block_scope_end
Pass2：DFS stmt_index；名称/类型/模式；resolved goto；break/continue metadata；funcall/return
       hist.defer_to_cfg=1（文件模式 uninit 留给 CFG）
       if/while 条件须 bool；while 内 goto/label 静态拒绝；顶层 goto/label 拒绝
静态布尔：tc_try_eval_static_bool（lit / 更早可见 let；var → UNKNOWN）
CFG：tc_cfg_build_all → 常量边剪枝 → 读集 → tc_analyze_definite_init_all
let / static let：源序可见 + 拓扑；tc_const_map_runtime_error
```

初始化三分工：`VAR_MISSING_INIT` ≠ `UNDEFINED_VARIABLE` ≠ `UNINITIALIZED_VARIABLE`。

## Semantics 要点

- 有符号 strict：`tc_sadd/ssub/smul_overflow`；div/mod 除零；INT_MIN/-1 特殊
- 无符号：位掩码；64 位乘用 `tc_umul64`
- compare：有符号/无符号分支，返回 `TC_BOOL`
- logic：`and`/`or` 短路；`not` 取反
- cast：`tc_sem_cast.c` strict 数值可表示性；truncate 仅整数窄化；bitcast 仅非 bool 等宽位复制

## let 编译期求值链

```
tc_pass2_check_stmt
  └─ tc_resolve_const_value → tc_eval_const_rhs
       ├─ 源序解析 const_ref
       ├─ 算术/比较/逻辑/浮点 → tc_exec_* + tc_const_map_runtime_error
       └─ cast/truncate/bitcast → tc_sem_cast.c
```

## 逻辑短路（三层一致）

| 阶段 | and 左 false / or 左 true |
|------|---------------------------|
| Pass2 | 仍检名称/类型；临时不检 rhs uninit |
| CFG | `tc_cfg_add_rhs_reads` 不记 rhs 读槽 |
| Executor / AOT / let | 不求 rhs |

左为 **var** → 静态 UNKNOWN，RHS 读边保留。
