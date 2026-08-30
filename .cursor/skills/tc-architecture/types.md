# 核心类型体系

**何时读**：AST 枚举、`TcType`/`TcRhsKind`/`TcStmtKind` 规模、equals/sizeof/槽位 API。更快：`rg` in `tc_types.h`。

错误种类：**86**（`test_types.c` 断言）。共同契约：`src/vm/runtime/tc_types.h`

RHS 分发覆盖：**34**（`tc_types.h` 的 `TcRhsKind` 枚举；8 个分发点全覆盖，`check_rhs_coverage.py`）

## 基础枚举

| 枚举 | 要点 |
|------|------|
| `TcTypeTag` | INT8..UINT64 + BOOL + FLOAT32/64 + **ISIZE/USIZE/VOID/PTR/MEMBLOCK/STRUCT**（仅标签） |
| `TcType` | `{ tag, params }`：ptr 携 pointee；memblock 携 element+count（等价忽略 N）；struct 携 struct_id |
| `TcTypeTable` | 分析期类型池；`tc_type_intern` 返回稳定指针；标量不入池；Analyze 完成后 Executor/AOT 只读 |
| `tc_type_tag_singleton` / `TC_TYPE_PTR` | 进程内标量/void/「仅标签」复合单例 |
| `TcStmtIndexCursor` | `{ next }` 游标 — DFS 先序 stmt_index 分配（`tc_stmt_index.h`） |
| `TcWrapMode` | STRICT / WRAP |
| `TcTruncateMode` | STRICT / TRUNCATE |
| `TcFloatMode` | STRICT / IEEE；WRAP 仅为 Parser 非法模式哨兵 |
| `TcArithOp` | ADD/SUB/MUL/DIV/MOD |
| `TcCompareOp` | EQ/NE/LT/LE/GT/GE |
| `TcLogicOp` | AND/OR/NOT |
| `TcBitwiseOp` | AND/OR/XOR/NOT（整数按位；与 logic 共享 and/or/not 关键字） |
| `TcShiftOp` | SHL/SHR |
| `TcStmtKind` | **24** 种全部可解析（控制流 + FIELD_ASSIGN/FUNC_*/RETURN/MEMBLOCK_*/PTR_STORE/MEMCOPY_UNSAFE/STRUCT_DEF/STATIC_*/IMPORT） |
| `TcErrorKind` | **86** 种（`TC_CE_*` / `TC_RE_*` / `TC_ERR_OUT_OF_MEMORY`）；CE/RE 越界对打印名可相同 |
| `TcTokenKind` | 模块/函数/控制流关键字（`#program`/`#lib`/`import`/`Self`/`func`/`return` 等） |
| `TcRhsKind` | **34** 种全部可解析；let 对部分复合/`FUNCALL_EXPR` const_eval defer |
| `TcFormatSpec` | `%d/%i/%u/%x/%X/%o/%b/**%t**` + `%f/%e/%E/%g/%G` |
| `TcSlotDomain` | TOPLEVEL / STATIC / PARAM / LOCAL |
| `TcRuntimeSlots` | toplevel + static + memblock/struct 堆存储跟踪 |

辅助：`tc_type_is_bool/integer/float`、`tc_type_equals`、`tc_sizeof_bits`、`tc_type_scalar`、`tc_type_make_*`

## 数据流

```
char* → TcTokenList → TcStatement → TcProgram → TcTypedProgram { program, symbols, cfg*, warnings, slot counts }
                                                      ↓
                                              TcValue slots[slot_index]（域见 TcSlotDomain）
```

`cfg*`：Analyze 末段 `tc_cfg_build_all`（`TcCfgSet` 多域）+ `tc_analyze_definite_init_all`；warnings 始终为空兼容壳。

## 静态类型验证（模块 E）

```
tc_struct_check — struct 表 / struct_id / 构造器与字段
tc_ptr_check    — nullptr 定型、address/store、ptr_*
tc_memblock_check — N 比较、.count、构造器、可判定越界
tc_type_check   — 期望 TcType 下 RHS/字面量调度；pass2 接入
```

运行时：`tc_{ptr,memblock,struct}_exec.c` + AOT `tc_aot_{ptr,memblock,struct}_*` — 见 [kg-eval.md](kg-eval.md)。

`TcLiteral`：`is_nullptr` / `is_float_special` / `float_special`；`TcSymbol.type`（`const TcType*`）为唯一类型事实源；`cast`/`bitcast` 的 `target_type` 由 Pass2 预 intern。

## 语句/RHS 结构（摘要）

```
TcStatement — VAR_DEF / CONST_DEF / ASSIGN / I/O / IF / WHILE / BREAK / CONTINUE
            / LABEL_DEF / GOTO / FIELD_ASSIGN / FUNC_DEF / FUNCALL / RETURN
            / MEMBLOCK_STORE|COPY / PTR_STORE / MEMCOPY_UNSAFE / STRUCT_DEF
            / STATIC_VAR_DEF / STATIC_LET_DEF / IMPORT

TcRhs — 标量 16 + MEMBLOCK_* / STRUCT_CONSTRUCTOR / FIELD_READ
      / PTR_* / FUNCALL_EXPR / SELF_MEMBER

TcOperand: VAR(name) | LIT(TcLiteral)
```

## 符号与运行时

```
TcSymbol { name, type(const TcType*), slot, slot_domain, sym_kind, ... }
TcSymbolTable { symbols[], scopes[], labels[] }
TcValue  { const TcType *type, uint64_t bits }
  /* type→单例或 intern；bool bits 0/1；
   * memblock/struct：bits 存堆指针。本实现 64-bit-only（`tc_target_ptr_width_bits()==64`）；无 32 位目标。 */
TcResolvedBinding { ..., const TcType *type, ... }
TcTypedProgram { ..., struct_table, type_table }
  /* struct_table 条目：name + module_name；裸名仅当前模块；
   * pending_name 可为 Player 或 ScoreLib.Player，注册后解析为 struct_id */
```

`memblock` 的 N / `struct_id` 仅存于 `TcType.params`；经 `tc_type_memblock_count` / `tc_type_struct_id` 解包。

## let 常量求值限制

- 支持：lit、const_ref、单层算术/比较/逻辑/位运算/移位/cast/truncate/bitcast、浮点 strict/ieee、struct ctor
- 禁止：引用 var、嵌套调用、`FUNCALL_EXPR`、自引用、前向引用；ptr/memblock/field/self 在 const_eval defer
- 名称按源序可见；结果存 `TcSymbol.const_value`
