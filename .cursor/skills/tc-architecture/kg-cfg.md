# CFG / 控制流 — stmt_index · 块作用域 · goto · 确定初始化 · 诊断阶段

**只读本文件** — 由 `@knowledge-graph` 索引指向；勿与其它 `kg-*.md` 同时整读。

> **v0.0.41 多域 CFG（Phase 3+5）**：`TcCfgSet`（顶层 + 各函数独立域）、`MISSING_RETURN`、goto/label **仅函数内**；函数体内 goto **执行/AOT 已落地**。

## stmt_index 子系统

```
tc_stmt_index.h — header-only（static inline），runtime 层
  TcStmtIndexCursor { next }         游标式扁平序号分配
  tc_stmt_index_reset / take         重置 / 取号推进
  tc_stmt_subtree_index_count        子树 DFS span（if/while 递归 body）
  tc_stmt_block_index_span           语句块 index 总数
  tc_stmt_index_skip_block           跳过未执行分支（Executor/AOT 通用）
```

DFS 先序：每条语句（含 if/while 自身）占 1 个 index；子块递归编号；Executor 跳过未执行分支时按子树 span 推进。

**使用方**：`tc_pass2_check_stmt`、`tc_execute_statement_impl`、`tc_aot_emit_statement_impl`。

**OOM 安全**：`tc_stmt_subtree_index_count` 只读递归，不分配。

## 块作用域

```
tc_symbol_table_push_scope / pop_scope     [tc_symbol.c]
  then 块：push → Pass1/Pass2 递归 → pop
  else 块：独立 push/pop（允许 then/else 同名局部）
  while 块：独立 push/pop
  pop_scope 末尾 → pop_labels（标签随块销毁）
tc_mark_block_scope_end → scope_end_stmt_index  [tc_analyzer_pass1.c，static]
Executor/AOT 查找：def_stmt_index + scope_end_stmt_index 过滤可见性
Pass1 预分配全局 slot；执行器不 pop slot
```

## 受限 goto / label

```
顶层：goto/label → TC_CE_GOTO/LABEL_OUTSIDE_FUNCTION（Pass2）
函数内路径规则：while 内禁止；跨函数 → CROSS_CONTROL_FLOW_JUMP
函数体内 goto 执行 / AOT：已落地（index.next / tc_label_<n>）

TcLabelEntry { name, stmt_index, block_depth, block_path[], def_line }
TcBlockPath：各层祖先 if 的 stmt_index

跳转判定（Pass2，函数内）：
  同路径同深度 → 平级 ✅
  label 为 goto 祖先 → 向外 ✅
  label 更深且前缀匹配 → JUMP_INTO_BLOCK ❌
  其余 → JUMP_TO_SIBLING_BLOCK ❌
  find_label NULL → LABEL_NOT_FOUND；同深度重名 → DUPLICATE_LABEL
  while 祖先内 goto/label → GOTO_INSIDE_LOOP / LABEL_INSIDE_LOOP ❌
```

无 `GOTO_SKIPS_VAR_INIT`：跳过初始化统一走 CFG 确定初始化 → `UNINITIALIZED_VARIABLE`。

## 完整 CFG 确定初始化（多域）

```
分析顺序（tc_analyze_ex）：
  模块/签名 → Pass1（含 func body）→ Pass2
  → tc_cfg_build_all（TcCfgSet：顶层 + 每函数独立域，状态不拼接）
  → tc_analyze_definite_init_all
  顶层 IN=∅；函数 IN=形参已初始化；return 边 → MISSING_RETURN / 结构 UNREACHABLE

tc_cfg.c：
  源语句节点；normal / if / while / goto / break / continue / return 边；保留 stmt_index
  tc_try_eval_static_bool → constant_condition → tc_cfg_prune_constant_edges
  tc_cfg_add_rhs_reads：and 左静 false / or 左静 true → 不记 rhs 读槽
  tc_analyze_definite_init：每 slot bitset；IN = 可达前驱 OUT 交集；worklist 固定点
  执行 var/Assign/Read → INIT；读取点 IN 非 INIT → UNINITIALIZED_VARIABLE

tc_analyzer_dfa.c（辅助，非固定点主路径）：
  TcInitHistory / TcInitState 工具；tc_check_operand_init；tc_prescan_init_history*
  块路径判定辅助；文件模式 defer_to_cfg 时跳过早报 uninit

while：condition false/break → after；body/continue → condition；后向 goto 参与同一固定点
var 强制初始化：parser 缺 = → VAR_MISSING_INIT（形态，与 CFG 无关）
var 值不做跨语句常量推测：左为 var → 静态布尔 UNKNOWN，RHS 读边保留
警告：0.0.41 无语言警告种类，warnings 始终为空
```

辅助：`tc_analyze_6a.c`（label 收集）· `tc_analyze_6e.c`（I/O format）  
短路读集细节见 [kg-eval.md](kg-eval.md)。函数域：[kg-func.md](kg-func.md)

## 诊断阶段（标准 §11.0 / 现网编排）

```
词法/语法 → 模块结构/import → 签名 → Pass1 名称
→ Pass2 类型/模式/funcall/return/goto → let / static let
→ 静态布尔 + CFG 剪枝 → 确定初始化 → 调用图
```

前一阶段无错才进后一阶段；同阶段按行列首错。同 Token 多规则优先级：
操作数数量 → 类型类别 → 模式 → bitcast 位宽 → 字面量范围/类型 → 格式符/其他。

初始化三分工：

| 层 | 错误码 | 性质 |
|----|--------|------|
| 语法 | `VAR_MISSING_INIT` | 缺 `= rhs`，与控制流无关 |
| 名称 | `UNDEFINED_VARIABLE` | 未声明 / 源序不可见 / 超作用域 |
| 数据流 | `UNINITIALIZED_VARIABLE` | CFG 可达点未确定初始化 |

详情：[errors.md](errors.md) · 标准 §11.0 · 管线：[pipeline.md](pipeline.md)
