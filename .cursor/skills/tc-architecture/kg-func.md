# 函数系统 — 签名 · funcall · return · 调用图 · static · 调用帧

由 `@knowledge-graph` 索引指向；勿与其它 `kg-*.md` 同时整读。

> **Phase 4（F+H）验证 + Phase 5（I）执行/AOT 已落地**。

## 关键文件

| 文件 | 职责 |
|------|------|
| `tc_func_check.c` | 签名、funcall、return、可写目标、FUNCTION_SCOPE、static let/var |
| `tc_callgraph.c` | Tarjan SCC → `TC_CE_RECURSION` |
| `tc_call_frame.c` | VM 调用帧 push/pop |
| `tc_executor.c` | `FUNCALL`/`RETURN`/`FUNCALL_EXPR`；`tc_exec_call_function_public` |
| `tc_aot_codegen.c` | 函数 codegen + `tc_aot_emit_funcall` |

## Analyze 顺序（函数段）

```
tc_module_collect_signatures
→ tc_func_check_signatures          /* 阶段 5：重名、形参冲突 */
→ tc_pass1_collect_symbols          /* 含 func body */
→ tc_member_index_build
→ tc_func_eval_static_lets          /* H-5 拓扑 */
→ tc_func_check_static_vars         /* H-6 */
→ tc_pass2_type_check               /* funcall/return/goto 仅函数内 */
→ tc_cfg_build_all + definite_init  /* 函数域 IN=形参；MISSING_RETURN */
→ tc_callgraph_check                /* 阶段 12 */
```

## 导出 API（速查）

```
tc_func_check_signatures
tc_func_resolve_call_target
tc_func_check_funcall
tc_func_check_return
tc_func_check_writable_target
tc_func_try_function_scope_access
tc_func_eval_static_lets
tc_func_check_static_vars
tc_callgraph_check
tc_call_frame_push / tc_call_frame_pop
```

## RHS / STMT

| Kind | 说明 |
|------|------|
| `TC_STMT_FUNC_DEF` / `FUNCALL` / `RETURN` | 语句 |
| `TC_STMT_STATIC_VAR_DEF` / `STATIC_LET_DEF` | 顶层 static |
| `TC_RHS_FUNCALL_EXPR` | 表达式调用（Pass2/Executor/AOT；**let 禁**） |
| `TC_RHS_SELF_MEMBER` | `#lib` 内 `Self.member` |

## 控制流约束

- goto/label **仅函数内**（顶层 → `GOTO/LABEL_OUTSIDE_FUNCTION`）
- 跨函数跳转 → `CROSS_CONTROL_FLOW_JUMP`
- 函数 CFG 独立域；return 边参与 `MISSING_RETURN` / `UNREACHABLE`

## 测试

Phase 4/5 账本见 [test-map.md](test-map.md)；unit：`test_executor` / `test_module` / `test_cfg`  
CFG 细节：[kg-cfg.md](kg-cfg.md) · Embed 调函数：[kg-embed.md](kg-embed.md)
