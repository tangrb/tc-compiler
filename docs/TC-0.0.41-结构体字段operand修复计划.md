# TC 0.0.41 — 结构体字段访问（`field_access`）作 Operand 修复计划

> **计划日期**：2026-08-27  
> **目标版本**：TC 0.0.41（语言标准 / 编译器标准 / VM / AOT 一致）  
> **性质**：规范对齐修复（spec ↔ implementation），非语言特性变更  
> **权威依据**：`docs/TC语言标准设计说明书-0.0.41.md` §3.9.5、§6.1.2；附录 A `operand` / `field_access` 产生式

---

## 1. 背景与问题陈述

### 1.1 现象

以下写法在语言标准下合法，但当前 `tc-vm` 在 Parser 阶段即失败（错误信息如括号标注）：

```tc
var doubled: int32 = mul(int32, cur.score, 2)     ; unexpected token
writeln(int32, p.x)                               ; unexpected token
gt(int32, a.x, b.x)                               ; unexpected token
mul(usize, mb.count, 2)                           ; unexpected token（memblock count 作 operand）
let doubled2: int32 = mul(int32, S.x, 2)          ; unexpected token（const 上下文 operand）
let px: int32 = p.x                               ; unexpected trailing tokens（const-RHS 顶层字段读）
```

而等价的拆行写法可以通过静态检查与执行：

```tc
var old: int32 = cur.score
var doubled: int32 = mul(int32, old, 2)
```

> **例外（不在本缺口内）**：`funcall` 命名实参位置的字段读取（如 `funcall(Lib.add_bonus, p: third, bonus: p0.score)`）**当前已可用**——实参值经 `tc_parse_rhs` 按完整 RHS 解析（`tc_parser_stmt.c` `tc_parse_funcall_rhs`），字段读 RHS 早已支持。计划 §5.3 的示例回写不涉及该路径。
>
> `let` 上下文（顶层 `let x = p.x`、`let n = mb.count` 与 const 运算 operand）与运行时运算同属缺口，见 §1.3 与 Phase 2c/2d。

### 1.2 概念澄清（必读）

TC 表达式分为两层，**不可混用**：

| 层 | 含义 | 示例 |
|----|------|------|
| **RHS（运算产生式）** | 一次完整右值运算 | `add(int32, a, b)`、`ptr_load(Player, p)` |
| **Operand（操作数）** | 运算的原子实参 | `a`、`42`、`cur.score`、`mb.count` |

标准 §6.1.2 禁止的是：**在 operand 位置嵌套 RHS 运算**（如 `mul(int32, add(int32, a, b), 2)`）。

`cur.score` **不是嵌套表达式**，而是与 `identifier` 同级的 **operand 原子形式**（§6.1.1：字段读「不是独立的运算产生式」）。

本修复的目标是让实现与上述分层一致，而非放宽「禁止运算嵌套」规则。

### 1.3 根因定位

| 路径 | 解析入口 | 现状 |
|------|----------|------|
| 整条 RHS：`var x = cur.score` | `tc_parse_field_read_rhs` → `TC_RHS_FIELD_READ` | ✅ 已实现 |
| `funcall` 命名实参：`bonus: p0.score` | `tc_parse_funcall_rhs` → `tc_parse_rhs` | ✅ 已实现（实参是 `TcRhs`，不是 `TcOperand`） |
| 运算 / I/O / return 等实参 | `tc_parse_operand` | ❌ 仅支持 `identifier \| literal` |
| const-RHS：`let x = p.x` / `let n = mb.count` | `tc_parse_const_rhs` | ❌ `identifier` 仅产出 `TC_RHS_CONST_REF`，无字段链分支 |

缺口集中在 **Parser 的 `tc_parse_operand` 与 `tc_parse_const_rhs`**；Analyzer / Executor / AOT 均假设 `TcOperand` 只有 `TC_OPERAND_VAR` 与 `TC_OPERAND_LIT`。

---

## 2. 规范依据摘要

### 2.1 语言标准（权威）

- §3.9.5：字段读取是 `operand` 的合法形式，可出现在运算、`funcall` 实参、`return`、输出等位置；支持 `a.b.c` 链式访问。
- §6.1.2：`field_access` 列为 operand 形式之一；基址为裸标识符、`Self.<名>`、`<模块名>.<名>` 等。
- 附录 A EBNF：

```text
operand = identifier | qualified_identifier | imported_member_name
        | field_access | memblock_count_access | …字面量…

field_access = ( identifier | qualified_identifier | imported_member_name ) ,
               { "." , identifier }
```

> 本计划覆盖其中 `field_access` 与 `memblock_count_access` 两形式；`qualified_identifier` / `imported_member_name` **直接**作 operand（不含 `.field` 链，如 `add(int32, Self.K, 1)`）是同一 EBNF 下的独立缺口，不在本范围（§8）。

### 2.2 仍须禁止（不在本修复范围）

```tc
mul(int32, add(int32, a.score, b.score), 2)   ; operand 内嵌 RHS 运算
mul(int32, ptr_load(Player, p).score, 2)      ; 基址不能是 ptr_load 结果
mul(int32, funcall(get_p).score, 2)           ; 基址不能是 funcall 结果
```

### 2.3 合规状态

`docs/设计实现合规审查报告-0.0.41.md` 中 struct 相关检查点（T-05、T-15 等）已标「通过」，但**未**单独列出「`field_access` 作 operand」的测试证据。本修复完成后应补充证据行或新增检查项。

---

## 3. 设计决策

### 3.1 推荐方案：扩展 `TcOperand`

在 `tc_types.h` 扩展 `TcOperandKind`：

```c
typedef enum {
    TC_OPERAND_VAR,
    TC_OPERAND_LIT,
    TC_OPERAND_FIELD_READ      /* cur.score / a.b.c / mb.count（.count 语义由 Analyzer 判定） */
} TcOperandKind;
```

`TcOperand` 的 union 复用与 `TcRhs.u.field_read` 相同的 `{ base, fields[], field_count }` 布局。

**不单设 `TC_OPERAND_MEMBLOCK_COUNT`**：`x.count` 在 parse 期无法区分 memblock 头与 struct 字段（解析器无类型信息），统一解析为字段链，由 Analyzer 按基址类型判定语义（Phase 2a）——顺带修复既有 RHS 无条件把 `x.count` 判为 memblock count 的同名字段误报。

**选用理由**：

- 字段读在语义上是 operand，不是新 `TcRhsKind`
- 执行层可复用 `tc_exec_struct_field_read` 逻辑
- 顶层 `var x = cur.score` 继续走 `TC_RHS_FIELD_READ`，改动面可控

**实现约定（本计划强制）**：

1. **分析期固化**：Pass2 将基址 slot 与各级字段 offset（以及 `.count` 判定结果）固化进 `TcOperand`，Executor / AOT 不再按名查表——与 `TcResolvedBinding` 的既有设计一致；具体结构见 §3.4（Phase 0 前置设计）。`TC_RHS_FIELD_READ` 路径**同步迁移**到同一表示（§3.4 第 5 点），不保留按名解析双轨。
2. **所有权纪律**：`TcOperand` 在多处按值复制（`memblock_ctor.values`、`struct_ctor.value_op` 等），新增堆字段后须审计全部复制 / 释放点，保证零初始化与单一释放（Phase 1 含审计任务）。
3. **ptr / memblock 上下文完整支持**：`ptr_load(T, a.mvp)`、`memblock_load(T, a.items, 0)` 等 operand 位置同样接受字段读（规范 §6.1.2 未区分上下文）；相关 checker 的限制一并放开（Phase 2a）。

### 3.2 不推荐方案

在 operand 位置解析为临时 `TcRhs` 再向下传递——会破坏 `TcOperand` / `TcRhs` 边界，且 `writeln`、`write`、`return`、`ptr_store` 等 API 均绑定 `TcOperand`；`funcall` 命名实参走 `TcRhs` 是唯一例外（其已支持字段读，不在本缺口内）。

### 3.3 不新增 `TcRhsKind`

本修复不增加 RHS 种类，`scripts/sync/check_rhs_coverage.py` 无需变更。

### 3.4 分析期表示（Phase 0 前置设计，开工前冻结）

`TC_OPERAND_FIELD_READ` 的 parse 期形态为 `{ base, fields[], field_count }`（与 `TcRhs.u.field_read` 同布局）；Pass2 固化后的表示必须在编码前冻结，避免 Phase 2/3 反复改结构：

```c
typedef struct {
    int base_slot;              /* 基址运行时槽（let 基址为 -1，直接读 const_value） */
    uint64_t const_bits;        /* let / static let 基址的规范化位模式（仅 const 上下文） */
    const TcType *field_type;   /* 末字段声明类型（含 memblock 的 N；执行期 / AOT 取宽度与 tag） */
    uint32_t *offsets;          /* 每级字段的位偏移（长度 = field_count） */
    size_t field_count;
    int is_memblock_count;      /* .count 消歧结果：1 = memblock 头语义 */
} TcResolvedFieldAccess;
```

要点：

1. **offset 链**：`offsets[i]` 为第 i 级字段的位偏移（相对其所在结构体），逐级累加定位末字段；由 `tc_struct_table` 在 Pass2 一次性计算，Executor / AOT 不再查表。
2. **末字段类型**：`field_type` 直接保存（memblock 字段须连同规划个数 `N`，供值语义检查）。
3. **`is_memblock_count`**：`.count` 语义消歧（Phase 2a）的结果固化于此；执行期不再按名 / 按类型判定。
4. **字符串生命周期**：固化完成后释放 parse 期 `base` / `fields[]`；`tc_operand_free` 支持两种形态——未固化时释放字符串，固化后释放 offset 链（配合 §3.1 约定 2 的复制点审计）。
5. **RHS 路径同步迁移**：现有 `TC_RHS_FIELD_READ` 执行期按名解析（`tc_exec_struct.c` `tc_exec_load_struct_base` + `tc_struct_path_offset_bytes`），与 operand 固化路径会形成双轨。Phase 2 顺带将 `TcRhs.u.field_read` 固化（增加 resolved 字段或内嵌 `TcResolvedFieldAccess`），使共享的 `tc_exec_eval_field_access` / `tc_aot_emit_field_access` 操作同一表示，避免双路径漂移。

---

## 4. 分阶段实施计划

### Phase 1 — Parser（解锁语法）

**目标**：`mul(int32, cur.score, 2)` 等可通过 `tc-vm --check`。

| 任务 | 文件 | 说明 |
|------|------|------|
| 抽取公共解析 | `tc_parser.c` / `tc_parser_internal.h` | 新增 `tc_parse_field_access_operand`：解析 `ident [. field]*`；`.count` 与普通字段**统一**解析为字段链，不做 parse 期区分（语义消歧见 Phase 2a） |
| 扩展 operand 解析 | `tc_parser.c` `tc_parse_operand` | 见 `identifier` 且后继为 `.` 时走 field access；否则保持原逻辑 |
| 可选：统一 RHS 路径 | `tc_parser_rhs.c` | `tc_parse_field_read_rhs` 内部调用同一 helper |
| const-RHS 入口 | `tc_parser_rhs.c` `tc_parse_const_rhs` | `identifier` 后跟 `.` 时走字段链（对齐 `tc_parse_rhs`；单段 `.count` 暂按现状产 `TC_RHS_MEMBLOCK_COUNT`，多段产 `TC_RHS_FIELD_READ`，最终解释由语义层按基址类型判定，见 Phase 2a） |
| 释放路径 | `tc_parser_free.c` `tc_operand_free` | 释放 `field_read.base` / `fields[]`；**并审计全部 `TcOperand` 按值复制点**（零初始化 + 单一释放，§3.1 约定 2） |
| 基址扩展（同批） | 同上 | 支持 `Self.<名>` / `<模块名>.<名>` 作 field 基址（EBNF 已要求，现 `tc_parse_field_chain` 仅裸 `identifier`） |

**Phase 1 验收**：

```bash
build/vm/bin/tc-vm --check tests/valid/struct_field_operand_arith.tc     # 新增用例
build/vm/bin/tc-vm --check tests/valid/struct_field_operand_let_rhs.tc   # const-RHS 入口（新增用例）
```

---

### Phase 2 — Analyzer（类型 + 确定初始化 + 常量）

**目标**：静态语义与标准一致；补齐既有缺口。

#### 2a. 类型检查复用与全上下文放开

| 任务 | 文件 | 说明 |
|------|------|------|
| 抽取公共检查 | `tc_struct_check.c` | `tc_struct_check_field_access(base, fields, n, expected, …)`（含基址绑定解析与确定初始化检查） |
| RHS 路径 | `tc_type_check.c` | `TC_RHS_FIELD_READ` 委托新函数 |
| Operand 路径 | `tc_analyzer_pass2_rhs.c` `tc_check_operand` | `TC_OPERAND_FIELD_READ` 分支委托新函数 |
| 名称预检 | `tc_analyzer_pass2_rhs.c` `tc_precheck_operand_name` | 对字段 operand 预检**基址**绑定 |
| 指针操作数 | `tc_ptr_check.c` `tc_ptr_check_operand` | 增加 `TC_OPERAND_FIELD_READ` 分支：字段类型须与 `ptr<T>` 严格匹配、基址须已初始化（`ptr_load(T, a.mvp)` 等） |
| memblock 操作数 | `tc_memblock_check.c` | 放开 `memblock_load` 基址与复合元素 operand 的 `TC_OPERAND_VAR` 限制，支持字段读（类型 / init 复用公共检查） |
| static var 初始化器 | `tc_func_check.c` `tc_static_var_operand_valid` | 字段 operand 按 const 规则处理：基址须为 `let` / `static let` struct，否则 `TC_CE_CONSTANT_EXPRESSION`（与现有 static var 初始化器限制一致） |
| `.count` 语义消歧 | `tc_memblock_check.c` / `tc_struct_check.c` | 单段 `.count`：基址类型为 memblock → count 语义；为 struct → 普通字段读。**顺带修复**既有 RHS 无条件判 `TC_RHS_MEMBLOCK_COUNT` 的同名字段误报（struct 字段名为 `count` 时现误报 `memblock count requires memblock variable`） |
| RHS 路径固化迁移 | `tc_types.h` / `tc_struct_check.c` | `TC_RHS_FIELD_READ` 同步迁移到 `TcResolvedFieldAccess`（§3.4 第 5 点），`tc_exec_struct_field_read` / AOT 发射改为读固化 offset，不再按名解析 |

#### 2b. 确定初始化（DFA）— 既有缺口，本计划一并修复

现状：`tc_struct_check_field_read` 接收 `hist` 但未使用（`(void)hist`）；`tc_cfg.c` 对 `TC_RHS_FIELD_READ` 不展开基址读集。

| 任务 | 文件 | 说明 |
|------|------|------|
| 基址 init 检查 | `tc_struct_check.c` | 运行时 `var` / 形参基址：`tc_check_operand_init` |
| CFG 读集 | `tc_cfg.c` `tc_cfg_add_operand_read` | `TC_OPERAND_FIELD_READ` → `tc_cfg_node_add_read(base)` |
| 短路逻辑 | `tc_cfg.c` | `and`/`or` 右侧含字段 operand 时，遵循与变量 operand 相同的短路剪枝 |

> 说明：该缺口当前**几乎不可触发**——goto 跳过带计算初始化器的声明会被判 `unreachable statement`，仅字面量初始化器可构造「可能未初始化」用例，而 struct 无字面量初始化器。本项为规范字面合规（§6.1.2「对运行时 `var` 基址须确定初始化」）与未来触发面扩展而修复。

#### 2c. `let` 常量上下文 — operand 位置

| 任务 | 文件 | 说明 |
|------|------|------|
| 编译期字段读 | `tc_const_eval.c` `tc_eval_const_operand` | `TC_OPERAND_FIELD_READ`：基址须为 `let` / `static let` struct；从 `const_value` 按 offset 读字段 |
| 负例 | `tests/errors/static/` | `var` 基址字段用于 `let` RHS → `constant expression cannot reference var variable` |

#### 2d. `let` 常量上下文 — 顶层 RHS 入口

现状缺口：`tc_parse_const_rhs` 无字段链分支（Phase 1 已补），`tc_eval_const_rhs` 无 `TC_RHS_FIELD_READ` / `TC_RHS_MEMBLOCK_COUNT` 处理——`let px: int32 = p.x` 与 `let n: usize = mb.count` 当前均失败（附录 A `const_rhs` 已要求，与 operand 缺口同根）。

| 任务 | 文件 | 说明 |
|------|------|------|
| const-RHS 编译期求值 | `tc_const_eval.c` `tc_eval_const_rhs` | `TC_RHS_FIELD_READ`：基址 `let` / `static let` struct，从 `const_value` 按 offset 读字段；`TC_RHS_MEMBLOCK_COUNT`：返回编译期 `N` |
| 负例 | `tests/errors/static/` | `let px: int32 = v.x`（`v` 为 `var`）→ `constant expression cannot reference var variable` |

**Phase 2 验收**：

- valid：字段 operand + 算术 / 比较 / I/O / return；`ptr_load(T, a.mvp)`、`memblock_load(T, a.items, 0)`；`let px = p.x`、`let n = mb.count`
- static：let 中 var 基址、static var 初始化器中 var 基址；uninit 基址以 unit 注入 `hist` 覆盖（§5.1 unit 段）
- runtime：短路不读未到达的字段 operand

---

### Phase 3 — VM 执行器

**目标**：运行时行为与拆行写法语义等价。

| 任务 | 文件 | 说明 |
|------|------|------|
| Operand 求值 | `tc_executor.c` `tc_eval_operand` | `TC_OPERAND_FIELD_READ`：调用共享的 `tc_exec_eval_field_access`（使用分析期固化的 slot / offset，§3.4） |
| 逻辑复用 | `tc_struct_exec.c` | 抽出 `tc_exec_eval_field_access(...)` 操作 `TcResolvedFieldAccess`，供 RHS（已迁移，§3.4）与 Operand 共用 |
| I/O 求值 | `tc_executor.c` `tc_exec_io_write` | 内联的 operand 求值改为委托 `tc_eval_operand`（或补字段读分支），避免新 operand 种类被此内联路径遗漏 |
| `.count` 语义 | 同上 | 分析期已判定为 memblock count 时返回头部值（与 RHS 路径一致；不新增 `TC_OPERAND_MEMBLOCK_COUNT`） |

**Phase 3 验收**：

```bash
bash scripts/run_tests.sh --filter struct_field_operand
```

---

### Phase 4 — AOT 代码生成

**目标**：VM / AOT 差分一致。

| 任务 | 文件 | 说明 |
|------|------|------|
| Operand 发射 | `tc_aot_codegen.c` | `tc_aot_emit_operand_expr` / `tc_aot_emit_operand_assign` 增加字段读分支 |
| 逻辑复用 | `tc_aot_emit_rhs.c` 或 `tc_aot_codegen.c` | 抽 `tc_aot_emit_field_access(...)` 操作固化表示（§3.4），与 `TC_RHS_FIELD_READ` 共用 |
| 值语义 | 同上 | struct / memblock 字段：标量 `tc_aot_struct_load_bits`；复合 `tc_aot_struct_extract`（§3.9.4） |

> AOT 的 `write`/`writeln` 已经 `tc_aot_emit_operand_expr` 发射（`tc_aot_emit_stmt.c`），自动继承本修复；无 VM 侧 `tc_exec_io_write` 式的内联路径问题。

**Phase 4 验收**：

```bash
bash scripts/aot/run_tests.sh --filter struct_field_operand
```

---

### Phase 5 — 测试、示例与文档同步

#### 5.1 新增 valid 用例（`tests/valid/`）

| 用例文件 | 覆盖点 |
|----------|--------|
| `struct_field_operand_arith.tc` | `mul(int32, p.score, 2)` |
| `struct_field_operand_compare.tc` | `gt(int32, a.x, b.x)` |
| `struct_field_operand_io.tc` | `writeln(int32, p.score)` |
| `struct_field_operand_return.tc` | `#lib` 内 `return p.score` |
| `struct_field_operand_nested.tc` | `a.b.c` 作 operand |
| `struct_field_operand_shortcircuit.tc` | `and(bool, false, p.score)` |
| `struct_field_operand_const.tc` | `let y = add(int32, S.x, 1)`（S 为 let struct） |
| `struct_field_operand_ptr.tc` | `ptr_load(Player, a.mvp)`（ptr 字段作 operand） |
| `struct_field_operand_memblock.tc` | `memblock_load(B, a.items, 0)`（memblock 字段作 operand 基址） |
| `struct_field_operand_count.tc` | `mul(usize, mb.count, 2)`、`writeln(usize, mb.count)`、`gt(usize, mb.count, 1)`（memblock count 作 operand，§1.1 示例；AOT 折叠声明 N，VM 读运行时头，差分一致） |
| `struct_field_operand_let_rhs.tc` | `let px: int32 = p.x`（const-RHS 入口）；`let n: usize = mb.count` 正例**不可构造**（let memblock 不可表达，见 §6 备注） |
| `struct_field_named_count.tc` | struct 字段名为 `count` 的读取（`.count` 语义消歧，operand 与 RHS 双入口） |
| `struct_field_operand_self_base.tc` | `Self.<名>.field`（lib 内）与 `<模块名>.<名>.field`（import 形态）双基址 |
| `struct_field_operand_composite.tc` | struct 字段值作 operand 的复合值语义：`ptr_store(Player, p, a.player)`、`return a.player`、`memblock(Player, count: 2, a.p, b.p)`（深拷贝，§3.9.4） |
| `struct_field_operand_cast.tc` | `cast(int32, p.score)`、`bitcast(float32, p.bits)`（cast/bitcast source 经 `tc_check_operand` 的 smoke） |

注册：`scripts/vm/run_tests.sh`、`scripts/aot/run_tests.sh` 对应 `# --- struct ---` 区块。

**unit（`tests/unit/runtime/test_struct_field_access.c`，已落地）**：`test_struct_field_access_*` 覆盖类型链校验（`a.b.c`）、未知字段、非 struct 中间层、`.count` 消歧、const 基址拒绝、operand 各位置；`tc_struct_check_field_access` 的 hist 检查以注入 `TC_INIT_UNINIT` 状态覆盖「未初始化基址」负例（§5.2 备注，.tc 负例不可构造）。CMake 注册于 `tests/unit/runtime/CMakeLists.txt`（`check-struct-field-access`）并挂入 `check-unit`。

#### 5.2 新增 static 负例（`tests/errors/static/`）

| 用例文件 | 期望 stderr 子串 |
|----------|------------------|
| `operand_nested_arith.tc` | syntax / `unexpected token` |
| `operand_field_var_in_let.tc` | `constant expression cannot reference var variable`（.tc 覆盖 const-RHS 入口；const operand 入口由 unit `test_struct_field_access_const` 覆盖——单文件首个错误即中止，无法在 .tc 内同时覆盖两入口） |

> **备注（`operand_field_uninit.tc` 暂不落地）**：期望 `use of uninitialized variable` 的字段基址 DFA 负例按当前可达性规则**无法构造为 .tc**——TC 声明强制带初始化器，goto 跳过带计算初始化器的声明会先报 `unreachable statement`，仅字面量初始化器可构造「可能未初始化」的标量，而 struct 无字面量初始化器（同 Phase 2b 说明）。改由 unit 注入 `hist` 覆盖（§5.1 unit 段）；待未来初始化器语法扩展后可补回 .tc 负例。

#### 5.3 示例回写

`examples/composite/main.tc`、`ScoreLib.tc`：去掉仅为规避 **operand** 缺口引入的中间变量（算术 / I/O 位置的字段 operand 写法）。`funcall` 命名实参路径已支持字段读（§1.1），无需改动。入口程序对库结构体须写 `ScoreLib.Player` / `ScoreLib.Team`（[语言标准 §3.9.1] / §4.4），不得用裸名。

#### 5.4 文档同步（本修复落地后）

| 文档 | 动作 |
|------|------|
| `docs/设计实现合规审查报告-0.0.41.md` | 新增或回填「field_access 作 operand（含 const 上下文与 ptr/memblock 位置）」检查项与测试证据 |
| `.cursor/skills/tc-architecture/syntax.md` | 若摘要中 operand 示例缺失，补一行 |
| 本计划文档 | 各 Phase 完成后勾选验收清单（见 §6） |

**Phase 5 验收**：

```bash
cmake --build build --target check-embed check-embed-aot   # TC-Embed 无 API 变更，共享 libtc 管线，仅回归确认
```

---

## 5. 改动文件清单

| 模块 | 文件 | 预估改动量 |
|------|------|------------|
| AST | `src/vm/runtime/tc_types.h` | 小 |
| Parser | `src/vm/parser/tc_parser.c`、`tc_parser_rhs.c`（含 `tc_parse_const_rhs`）、`tc_parser_free.c`、`tc_parser_internal.h` | 中 |
| Analyzer | `src/vm/analyzer/tc_struct_check.c`、`tc_analyzer_pass2_rhs.c`、`tc_cfg.c`、`tc_const_eval.c`、`tc_ptr_check.c`、`tc_memblock_check.c`、`tc_func_check.c` | 中 |
| VM | `src/vm/executor/tc_executor.c`（`tc_eval_operand` + `tc_exec_io_write`）、`tc_struct_exec.c`（field_read 固化迁移，§3.4） | 小 |
| AOT | `src/aot/tc_aot_codegen.c`、`tc_aot_emit_rhs.c`（RHS 固化迁移） | 中 |
| 测试 | `tests/valid/*.tc`、`tests/errors/static/*.tc`、`tests/unit/runtime/*.c`、`scripts/*/run_tests.sh` | 小 |
| 示例 | `examples/composite/*.tc` | 小 |

---

## 6. 验收清单

修复完成后须全部满足：

- [x] `mul(int32, cur.score, 2)`、`writeln(int32, p.x)`、`gt(int32, a.x, b.x)` 通过 `tc-vm --check` 与运行
- [x] `ptr_load(Player, a.mvp)`、`memblock_load(B, a.items, 0)` 通过 `tc-vm --check` 与运行
- [x] `let px: int32 = p.x` 通过 `tc-vm --check` 与运行；`let n: usize = mb.count` 正例**不可构造**（`let mb: memblock = …` 的 const_eval 不支持 memblock 构造，let-memblock 绑定不存在），var 基址负例由 `operand_field_var_in_let.tc` 覆盖
- [x] struct 字段名为 `count` 的读取不再误报 `memblock count requires memblock variable`（operand 与 RHS 双入口，`struct_field_named_count.tc`）
- [x] `Self.<名>.field` / `<模块名>.<名>.field` 基址通过（#lib / import 用例）
- [x] `and(bool, false, p.score)` 短路时不因 `p` 未初始化而误报（若 `p` 仅在短路右侧使用）
- [x] 未初始化基址读字段 → `use of uninitialized variable`（unit 注入 `hist` 验证：`test_struct_field_access_hist_uninit`；.tc 负例受可达性规则限制不可构造，见 §5.2 备注）
- [x] `let` 中 `var` 基址字段 → `constant expression cannot reference var variable`
- [x] `mul(int32, add(int32, a, b), 2)` 仍为语法错误（嵌套 RHS 未放开）
- [x] AOT 与 VM 对新用例 stdout 差分一致
- [x] unit `test_struct_field_access_*` 通过（§5.1 unit 段，21 断言）
- [x] `cmake --build build --target check-embed check-embed-aot` 通过（§5.4 Phase 5 验收）
- [x] `python3 scripts/sync/check_rhs_coverage.py` 通过（无新 `TcRhsKind`）
- [x] `examples/composite/main.tc` 可运行且输出不变

---

*文档版本：0.0.41-fix-plan · 状态：已落地*

> **实施补充记录（2026-08-27 测试全面更新）**：
>
> 1. **修复缺陷**：`tc_memblock_check.c` 的 `.count` 重解释路径中，
>    `rhs->u.memblock_count.memblock_name = NULL;` 与 `field_read.base` 是同一 union
>    存储，会把刚写入的基址清空，导致 `var n: int32 = s.count`（struct 字段名为
>    `count` 的 RHS 入口）误报 `invalid struct field read`。原 `.tc` 用例仅走 operand
>    入口（`mul(int32, s.count, 2)`）未暴露；已删除该置空语句并补 RHS 入口覆盖。
> 2. **诊断消息变更**：let-RHS type_check 扩展为全部 RHS 种类后，
>    `let a: int8 = 128` 由 const_eval 的 `invalid literal in constant expression`
>    变为 type_check 的 `literal out of range for context type`（更精确）；
>    `let_const_literal_range.tc` 三处脚本期望子串已同步。
> 3. **新增覆盖**：`struct_field_operand_count.tc`（`.count` 作 operand）、
>    `struct_field_operand_self_base.tc` 补 import 形态基址、`struct_field_named_count.tc`
>    补 RHS 入口、unit `test_struct_field_access.c`（含 hist 注入）。

以下与「字段 operand」同根、标准已允许的缺口**已并入本计划**：

| 缺口 | 处理位置 |
|------|----------|
| 顶层 `TC_RHS_FIELD_READ` 的 DFA（`tc_struct_check_field_read` 忽略 `hist`） | Phase 2b |
| `mb.count` 作 operand | Phase 1–4（不设独立 kind，语义消歧） |
| `Self.<名>.field` / `<模块名>.<名>.field` 基址 | Phase 1 基址扩展 |
| const-RHS 字段读（`let px = p.x` / `let n = mb.count`） | Phase 2d |
| struct 字段名为 `count` 的读取误报 | Phase 2a 语义消歧（顺带修复既有 RHS 误判） |
| 合规报告证据 | Phase 5.4 |

以下相关缺口**不在本计划范围**（见 §8）：裸限定名直接作 operand（`Self.<名>` / `<模块名>.<名>`，不含 `.field` 链）。

---

## 8. 明确不在范围

- 允许 `ptr_load(T, p).score`、`funcall(f).x` 等「RHS 结果作 field 基址」
- 放宽 operand 内嵌套算术 / cast / funcall
- 修改语言标准正文（本项为实现补齐，非规范变更）
- 新增 `TcRhsKind` 或变更 `check_rhs_coverage.py` 映射
- 裸限定名 operand：`Self.<名>` / `<模块名>.<名>` **直接**作 operand（不含 `.field` 链）——operand EBNF 的另一独立缺口（§2.1 注、§7），本计划仅扩展 field_access 的**基址**形式，留待后续专项

---

## 9. 建议实施顺序

```text
Phase 0 前置设计（冻结 §3.4 分析期表示）
    → Phase 1 Parser
        → Phase 2 Analyzer（类型 → DFA → const_eval）
            → Phase 3 VM
                → Phase 4 AOT
                    → Phase 5 测试 + 示例 + 合规报告
```

每阶段以最小新增用例门禁，避免一次性改动全链路难以回归定位。

---

## 10. 参考

| 文档 / 代码 | 用途 |
|-------------|------|
| `docs/TC语言标准设计说明书-0.0.41.md` §3.9.5、§6.1.2、附录 A | 规范权威 |
| `docs/TC编译器标准设计说明书-0.0.41.md` §6、§1.3 | 编译器阶段与诊断 |
| `src/vm/parser/tc_parser.c` `tc_parse_operand` | 当前缺口入口 |
| `src/vm/parser/tc_parser_rhs.c` `tc_parse_const_rhs` | const-RHS 缺口入口 |
| `src/vm/analyzer/tc_struct_check.c` `tc_struct_check_field_read` | 类型检查复用来源 |
| `src/vm/analyzer/tc_ptr_check.c` / `tc_memblock_check.c` | ptr / memblock operand 限制放开位置 |
| `src/vm/executor/tc_struct_exec.c` `tc_exec_struct_field_read` | 执行复用来源 |
| `examples/composite/` | 修复后示例简化目标 |

---

*文档版本：0.0.41-fix-plan · 状态：已落地（含 unit 测试与测试全面更新，见 §6 实施补充记录）*
