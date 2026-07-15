# TC-VM 详细设计说明书

> **规范基线**：[TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md)（目标设计）
>
> **当前实现基线**：TC-VM v0.0.26（`TC_VM_VERSION`）
>
> **状态**：本文定义 0.0.31 目标架构；除明确标注的 v0.0.26 内容外，均待实现与验证。
>
> **适用范围**：批量源文件的 Parse、Analyze、Execute，以及 tc-vm 的实现边界

---

## 目录

1. [文档边界与设计目标](#1-文档边界与设计目标)
2. [总体架构](#2-总体架构)
3. [程序表示与目标 IR](#3-程序表示与目标-ir)
4. [Parser、缩进与块模型](#4-parser缩进与块模型)
5. [作用域、绑定与槽位](#5-作用域绑定与槽位)
6. [完整 CFG](#6-完整-cfg)
7. [Analyzer 与确定初始化](#7-analyzer-与确定初始化)
8. [Executor](#8-executor)
9. [数值与 RHS 语义](#9-数值与-rhs-语义)
10. [`let` 常量求值](#10-let-常量求值)
11. [I/O](#11-io)
12. [诊断](#12-诊断)
13. [模块与接口](#13-模块与接口)
14. [REPL 边界](#14-repl-边界)
15. [验证与交付门槛](#15-验证与交付门槛)
16. [实现基线与迁移](#16-实现基线与迁移)

---

## 1. 文档边界与设计目标

### 1.1 两条版本线

| 维度 | 版本 | 含义 |
| ---- | ---- | ---- |
| 语言规范 | 0.0.31 | 本文必须满足的合法程序集合、结果和诊断阶段 |
| 当前代码 | v0.0.26 | 仓库当前可构建、可运行、已有回归证据的实现 |
| 本文架构 | 0.0.31 目标设计 | 从 v0.0.26 演进到规范符合实现的内部设计 |

本文不得用于声称当前 `tc-vm` 已支持 `while`、`break`、`continue`、`bitcast` 或 0.0.31 的完整错误集合。当前命令行为见 [TC-VM 命令行参考](./TC-VM命令行参考.md)。

### 1.2 目标

- 同一份 typed program 可被 VM 执行器与 AOT 后端消费。
- Parse、名称解析、类型检查、完整 CFG、确定初始化和执行阶段边界固定。
- `if`、`while`、`break`、`continue`、`goto`、短路表达式进入同一控制流模型。
- VM、AOT 与 `let` 常量求值共享整数、浮点、转换和位重解释语义。
- 所有内存分配失败使用 `TC_ERR_OUT_OF_MEMORY`，不伪装成语言语法错误。
- 批量源文件语义与 REPL 的交互限制分离。

### 1.3 非目标

- 不在 VM 内引入 JIT、字节码文件格式或寄存器分配器。
- 不为未纳入 0.0.31 的函数、数组、结构体、指针或字符串预先定义 ABI。
- 不以更强的可选静态规则缩小 0.0.31 合法程序集。
- 不为尚未实现的目标符号承诺 C API 稳定性。

### 1.4 规范到实现的约束

语言标准决定可观察行为；本文只决定如何实现。若目标内部结构与标准冲突，以标准为准。实现可以替换算法或数据结构，但必须保持：

1. 接受和拒绝同一组批量程序；
2. 产生相同值、I/O 和控制流结果；
3. 在规定阶段报告同一错误种类；
4. VM、AOT 和常量求值结果一致。

---

## 2. 总体架构

### 2.1 目标流水线

```text
source
  │
  ├─ Lexer ───────────── token / line / column / NEWLINE / indent text
  │
  ├─ Parser ──────────── TcProgram（树形语句与 RHS）
  │
  ├─ Binder + Type ───── 绑定、类型、块路径、固定槽位
  │
  ├─ CFG Builder ─────── 全部可达控制边
  │
  ├─ Dataflow ────────── 确定初始化固定点
  │
  └─ TcTypedProgram
       ├─ Executor ───── VM 直接执行
       └─ AOT ────────── C99 代码生成
```

`tc_compile_source` 或 `tc_compile_file` 只有在所有静态阶段完成后才返回成功。Executor 不补做语法、名称或确定初始化检查；它只执行已验证程序并报告运行时错误。

### 2.2 失败模型

- 全流水线 fail-fast，`TcDiagnostic` 保存第一条错误。
- 每个阶段失败后释放自身已取得的所有权，不把部分初始化对象交给调用方。
- Parser 失败不修改输出程序；Analyzer 失败不产生可释放义务不明确的 `TcTypedProgram`。
- OOM 可发生在任意阶段，但始终映射为 `OutOfMemory`。

### 2.3 v0.0.26 可复用基础

当前模块拆分、`TcValue` 位模式、树形 `TcIfStmt`、`tc_stmt_index`、两遍 Analyzer、路径敏感 DFA、执行器和 AOT 共用语义函数均可作为迁移基础。它们不能自动证明 0.0.31 合规；尤其是循环、完整 CFG、强制初始化器和新转换规则仍需实现。

---

## 3. 程序表示与目标 IR

### 3.1 表示层次

| 层次 | 产出方 | 消费方 | 责任 |
| ---- | ------ | ------ | ---- |
| Token | Lexer | Parser | 词法类别、源位置、换行与原始缩进 |
| `TcProgram` | Parser | Analyzer | 保留源结构和名称，不承诺类型合法 |
| 绑定/类型元数据 | Analyzer | CFG、Executor、AOT | 名称解析、类型、槽位、块路径 |
| CFG | Analyzer | Dataflow、审查工具 | 全部控制边与可达性 |
| `TcTypedProgram` | Analyzer | Executor、AOT | 完整静态验证后的所有权根 |

### 3.2 目标 `TcStmtKind`

v0.0.26 已有 `VAR_DEF`、`CONST_DEF`、`ASSIGN`、`WRITE`、`WRITELN`、`READ`、`IF`、`LABEL_DEF`、`GOTO`。0.0.31 目标增加：

```c
TC_STMT_WHILE,
TC_STMT_BREAK,
TC_STMT_CONTINUE
```

建议目标数据形态：

```c
typedef struct {
    int line;
    TcRhs condition;
    TcStatement *body;
    size_t body_count;
} TcWhileStmt;

typedef struct {
    int line;
} TcLoopControlStmt;
```

`break` 和 `continue` 不在 AST 中保存文本标签；Analyzer 将它们绑定到最内层词法 `while`，CFG 边直接指向相应出口或条件节点。

### 3.3 目标 `TcRhsKind`

0.0.31 新增等位宽位重解释。目标 IR 增加：

```c
TC_RHS_BITCAST
```

目标 payload 至少包含目标类型、已解析源类型和一个合法 Operand。`bitcast` 与数值 `cast` 必须是不同 kind，避免在 Executor、AOT、常量求值和静态检查中重新猜测转换性质。

### 3.4 `var` 收敛

0.0.31 的 `TcVarDef` 不再需要表达合法的“无 RHS”状态。Parser 可以在识别缺失初始化器时直接产生 `TC_ERR_VAR_MISSING_INIT`；若为错误恢复保留 `has_rhs`，该字段不得进入成功的 `TcTypedProgram`。

### 3.5 语句序号

`stmt_index` 继续为源语句树提供稳定 DFS 扁平序号，用于：

- 标签和 goto 目标；
- 诊断与源映射；
- CFG 节点关联；
- Executor/AOT 的唯一标签名；
- 测试中的确定性断言。

`while`、`break`、`continue` 与 `label` 本身各占一个语句序号；`if`/`while` 的子语句按源序递归编号。CFG 可添加不对应源语句的合成入口、条件出口和会合节点，但不得改变源语句序号。

### 3.6 所有权

- `TcProgram` 拥有全部语句数组、名称字符串、RHS operand 名称和子块。
- Analyzer 成功时把 `TcProgram` 所有权转移给 `TcTypedProgram`。
- CFG 若持久化在 typed program 中，由 typed program 统一释放；若可重建，AOT 与 Executor 不得各自维护语义不同的副本。
- `TcWhileStmt.body` 与 `TcIfStmt.then_body/else_body` 使用相同递归释放规则。
- 新增 kind 后，parser-free、Analyzer 分发、Executor 分发、AOT 分发和测试覆盖必须同步。

---

## 4. Parser、缩进与块模型

### 4.1 行模型

每个逻辑行至多一条语句。`;` 引入行注释；Lexer 在 `NEWLINE` 前丢弃注释内容。空行和纯注释行不产生语句节点。

Parser 输入必须保留：

- 行号与首 Token 列号；
- 行首缩进的原始字符序列；
- `NEWLINE`/EOF 边界；
- `then`、`else`、`end`、`label` 的行完整性。

### 4.2 通用块解析

目标 Parser 不再把缩进引擎写成 if 专用逻辑，而采用统一块帧：

```text
BlockFrame {
    owner_kind       GLOBAL | IF_THEN | IF_ELSE | WHILE
    owner_line
    indent_level
    statements
}
```

- `if ... then` 打开 then 帧，可选 `else` 切换到独立互斥子作用域。
- `while ... then` 打开 while 帧。
- `end` 关闭最近的 if 或 while 所有者。
- `label` 是普通语句，不打开块，也不增加缩进。

### 4.3 强制缩进

目标实现执行标准 §4.7.2 的 R1～R7：

1. 同一文件只能选择 4 空格或 1 制表符作为一级缩进；
2. 直接块内语句恰好多一级；
3. `else`/`end` 与所有者行对齐；
4. 嵌套块逐级增加；
5. `label` 与同作用域普通语句对齐；
6. `while` 与 `if` 使用同一缩进规则。

缩进错误在 Parser 阶段报告。`if`/`while` 条件类型错误则在 Analyzer 阶段报告。

### 4.4 变量初始化器形态检查

`var name: type` 不再进入一般 RHS 缺失的 `SyntaxError` 路径。Parser 在识别完整声明前缀后若未见 `=` 与 RHS，报告：

```text
TC_ERR_VAR_MISSING_INIT / VarMissingInitializer
```

这一步与 CFG 是否能到达该声明无关。

### 4.5 循环控制的语法与上下文

Parser 负责构造 `break`/`continue` 节点，不必在此阶段决定它们是否合法。Analyzer 根据词法祖先判断是否存在 `while`；不存在时分别报告 `BreakOutsideLoop` 或 `ContinueOutsideLoop`。

---

## 5. 作用域、绑定与槽位

### 5.1 词法作用域树

目标作用域节点包含：

- 父作用域；
- kind：GLOBAL、IF、THEN、ELSE、WHILE；
- 稳定 block id 与 block path；
- 本作用域的变量/常量命名空间；
- 本作用域的标签命名空间；
- 最近的祖先 while。

变量/常量共享一个命名空间；标签使用独立命名空间。`then` 与 `else` 是不同的兄弟作用域，允许分别定义同名局部绑定或标签。

### 5.2 源序可见性

- `var`/`let` 只在定义之后可见；定义 RHS 不可引用自身。
- 外层已定义绑定在内层可见，内层同名绑定可屏蔽外层绑定。
- 离开块后，块内绑定不可见；这是名称解析错误，不是未初始化错误。
- `let` 编译期内联，无运行时槽。

### 5.3 固定槽位

Pass1 为每个词法 `var` 绑定分配唯一 slot。槽位生命周期与词法绑定一致，而不是与某次语句执行次数一致：

| 事件 | 槽位行为 |
| ---- | -------- |
| 进入作用域 | 槽位存在，初始化状态为 false |
| 执行 `var x = rhs` | 先求 RHS，再写槽并标记已初始化 |
| 下一次 while 迭代或后向 goto 再次执行同一 `var` | 覆盖同一槽，重新初始化 |
| `end`、`break` 或向外 goto 离开作用域 | 绑定失活；实现可在调试模式清除槽 |

固定槽使 VM 与 AOT 的存储模型一致，也为未来拥有资源的值类型保留确定的进入/离开点。

### 5.4 标签解析与范式隔离

- goto 从当前作用域沿父链查找标签。
- 目标在当前或祖先作用域：候选合法。
- 目标在子作用域：`JumpIntoBlockError`。
- 目标在兄弟作用域：`JumpToSiblingBlockError`。
- 当前作用域同名标签重复：`DuplicateLabel`。
- `while` 祖先内出现 `goto`：`GotoInsideLoop`。
- `while` 祖先内定义 `label`：`LabelInsideLoop`。

范式隔离在 CFG 构建前完成；非法 goto 不进入 CFG。

---

## 6. 完整 CFG

### 6.1 目标

0.0.31 要求所有可达控制路径参与同一确定初始化分析。v0.0.26 的路径敏感基础必须扩展为显式、可固定点求解的完整控制流图。

### 6.2 节点

每条源语句至少对应一个 CFG 节点。以下结构可使用合成节点：

- program entry / exit；
- if/while 条件后的 true/false 分流；
- 块会合；
- while continue target；
- while break/condition-false exit；
- label 后第一条可执行语句。

节点保留 `stmt_index`、源行、作用域和绑定读写集合。合成节点使用独立内部 id，不冒充源语句。

### 6.3 边

| 结构 | 必需边 |
| ---- | ------ |
| 普通语句 | 当前节点 → 下一顺序节点 |
| `if` | condition → then；condition → else/after；分支末尾 → after |
| `while` | condition true → body；condition false → after；body 正常末尾 → condition |
| `continue` | continue → 最内层 while condition |
| `break` | break → 最内层 while after |
| `goto` | goto → 目标 label 后第一节点；无顺序后继 |
| `label` | label → 下一顺序节点 |
| 短路 `and`/`or` | 左值决定跳过或进入右操作数检查路径 |

向外 `goto` 和 `break` 边还记录离开的作用域集合，供 Executor/AOT 的生命周期处理使用。

### 6.4 常量条件剪枝

只有条件由字面量与源序中更早的 `let` 构成、且满足 0.0.31 单层常量表达式规则时，Analyzer 才能删除不可能边：

- `if true` 删除 false 边；
- `if false` 删除 true 边；
- `while true` 无条件失败出口，仅 `break` 可到达循环后；
- `while false` 的循环体不可达。

不可达语句仍接受词法、语法、名称和类型检查，只不参与可达前驱的初始化交集。

### 6.5 构建顺序

1. 完成结构、名称、类型和 goto 合法性检查；
2. 建立全部节点及 label 目标；
3. 连接结构化边；
4. 连接 break/continue 与 goto 边；
5. 应用合法常量条件剪枝；
6. 计算可达性；
7. 运行固定点数据流。

---

## 7. Analyzer 与确定初始化

### 7.1 阶段

目标 Analyzer 逻辑顺序：

```text
shape checks
  → binding / scope / slot allocation
  → RHS and statement type checks
  → control-context checks
  → CFG build
  → reachability
  → definite-initialization fixed point
  → TcTypedProgram
```

实现可以合并遍历，但不得改变错误种类所属阶段。

### 7.2 传递函数

设 `IN[n]` 为节点执行前确定已初始化的绑定集合，`OUT[n]` 为执行后集合：

```text
IN[entry] = 已由祖先作用域确定初始化的绑定集合
IN[n]     = 所有可达前驱 OUT 的交集
OUT[var x = rhs] = IN[n] ∪ {x}
OUT[其他节点]    = IN[n]
```

`var` 的 RHS 在把 `x` 加入集合前检查。赋值目标和 `read` 目标与读取相同，都要求绑定属于当前 `IN`。

### 7.3 固定点算法

建议使用工作队列：

1. 所有节点初始化为 unknown；entry 使用确定集合；
2. 前驱输出变化时重新入队；
3. 会合取可达前驱交集；
4. 集合稳定后停止；
5. 对每个可达读取点验证 binding ∈ `IN`。

集合可以用 slot-index bitset 表示。`let` 不占 slot，不进入集合。

### 7.4 错误分工

| 情况 | 错误 |
| ---- | ---- |
| `var` 缺 RHS | `VarMissingInitializer` |
| 名称不存在、前向引用、自引用或跨块不可见 | `UndefinedVariable` |
| 绑定存在，但某可达前驱未执行初始化 | `UninitializedVariable` |

不得用 `GotoSkipsVarInit` 替代通用的 `UninitializedVariable`。仅越过一个从不使用的 `var` 不构成错误。

### 7.5 短路

逻辑 `and`/`or` 的 RHS 读取是否可达由左值决定。可在编译期确定的左值允许剪枝；无法确定时保留两条边。被短路排除的 RHS 不触发未初始化错误，但仍须通过语法、名称和类型检查。

---

## 8. Executor

### 8.1 输入契约

Executor 只接收成功的 `TcTypedProgram`：

- 所有名称已绑定；
- 类型与模式合法；
- goto/label 与循环上下文合法；
- 确定初始化已证明；
- `let` 已求值并可内联。

运行时仍可能产生算术、浮点、cast 或 I/O 错误。

### 8.2 目标执行控制

v0.0.26 的树形块执行和 `stmt_index` seeking 可复用，但 0.0.31 必须显式表达以下控制结果：

```text
NORMAL
BREAK(loop_id)
CONTINUE(loop_id)
GOTO(target_stmt_index)
ERROR
```

实现可以选择中央指令指针或块执行返回控制结果。无论选择哪种：

- `break`/`continue` 绑定到最内层 loop id；
- goto 可穿过多层 if 返回到共同调度点；
- 离开作用域时统一处理局部生命周期；
- 任何源语句最多在一次控制到达中执行一次。

### 8.3 `while`

```text
loop:
    condition = eval(bool_rhs)
    if condition == false: leave
    result = execute(body)
    if result == BREAK(this): leave
    if result == CONTINUE(this): goto loop
    if result == GOTO: propagate
    goto loop
leave:
    continue after end
```

嵌套循环只消费指向自身 loop id 的 break/continue；其它控制结果向外传播。

### 8.4 goto 与标签

标签本身零成本。goto 将下一执行位置设为目标标签后的第一条语句。向后跳转再次执行 `var` 时覆盖同一固定槽。向外跳转携带离开作用域集合；目标实现不得保留已离开块的活动绑定。

### 8.5 运行时槽

槽数组长度为变量绑定总数。每槽存 `TcValue` 位模式；调试构建可附加 initialized/active 标记，但发布语义不能依赖未初始化哨兵兜底，因为静态分析已经证明合法读取。

---

## 9. 数值与 RHS 语义

### 9.1 值表示

`TcValue` 使用 `TcType + uint64_t bits`。窄整数只使用低位；有符号解释在语义函数中按目标位宽完成。浮点通过无别名违规的位复制在 `uint32_t/uint64_t` 与 `float/double` 间转换。

### 9.2 整数

- strict：有符号 `add/sub/mul/neg/shl` 检查范围；除零和 `INT_MIN / -1` 按标准报错。
- `wrap`：仅用于标准允许的整数操作，按目标位宽回绕。
- `shr` 不掩码计数；计数大于等于位宽按标准定义结果。
- `mod` 只支持整数。

### 9.3 浮点

0.0.31 只保留：

| 模式 | 行为 |
| ---- | ---- |
| strict | 检测除零、上溢、下溢和无效操作并报告规定错误 |
| `ieee` | 产生 IEEE 754 结果，不把标准 NaN/Infinity 结果改为语言错误 |

浮点 `wrap` 在 0.0.31 中非法，Analyzer 报 `ModeMismatch`。每个操作按声明的 `float32` 或 `float64` 精度舍入；不得先用宿主更高精度串联计算再只在末尾舍入。比较使用标准固定的 NaN 规则，不再接受多余比较模式。

### 9.4 `cast`

严格 `cast` 是数值转换。目标值必须可由目标类型表示：

- 整数 ↔ 整数；
- 整数 ↔ 浮点；
- 浮点 ↔ 浮点；
- 数值 ↔ `bool`。

运行时失败统一为 `CastOverflow`；常量阶段为 `ConstantCastOverflow`。旧 `FloatCastOverflow` 分类删除。

### 9.5 `truncate`

`cast(T, truncate, operand)` 只允许整数到更窄整数，保留低位。它不是浮点取整，也不是位重解释。其它组合在静态阶段报告 `ModeMismatch`。

### 9.6 `bitcast`

`bitcast(T, operand)`：

- 源、目标必须等位宽；
- `bool` 不参与；
- 不做数值转换；
- 位模式原样复制；
- 不等宽报告 `BitcastWidthError`。

Executor、AOT 与常量求值必须共享同一位宽表。C 实现使用 `memcpy` 或等价安全方式，不通过违反严格别名规则的指针强转。

---

## 10. `let` 常量求值

### 10.1 合法形式

`let` RHS 只能是：

- 字面量；
- 源序中更早的 `let`；
- 单个运算、比较、逻辑、cast、truncate 或 bitcast 调用，其 operand 均为上述原子。

调用不可嵌套，不可引用 `var`。自引用和前向引用走名称解析错误；不再需要常量循环依赖专用错误。

### 10.2 与运行时一致

常量求值调用与 Executor 相同的纯语义核心，或通过逐项一致性测试证明等价。浮点每步按声明精度舍入，strict/ieee 行为与运行时一致。`let` 成功后保存 `TcValue` 并在使用处内联，不生成运行时槽。

### 10.3 错误映射

| 运行时类别 | 常量阶段类别 |
| ---------- | ------------ |
| Integer/Float overflow or underflow | `ConstantOverflow` |
| Division by zero | `ConstantDivisionByZero` |
| Cast overflow | `ConstantCastOverflow` |
| 非法常量形态或浮点无效操作 | `ConstantExpressionError` |

---

## 11. I/O

### 11.1 输出

`write`/`writeln` 支持 13 种格式符。Analyzer 在执行前验证格式符、类型和 operand 数量；runtime 只负责格式化和写入错误。

### 11.2 输入

`read(type, name)` 目标必须已定义、类型一致且在当前 CFG 点确定初始化。`read` 覆盖已有值，不能替代声明初始化器。非法输入、EOF、范围错误或流失败统一为 `IOError`，消息可进一步区分原因。

### 11.3 跨后端一致性

VM 与 AOT 共用 `tc_io` 或同一明确契约，特别是：符号、进制、浮点格式、NaN/Infinity 文本、换行和错误时机。

---

## 12. 诊断

### 12.1 单槽与位置

`TcDiagnostic` 保持 fail-fast 单槽，包含 kind、消息、文件名、行、列和源片段。新 Parser/CFG 路径必须保留原始源位置，不用内部合成节点行号覆盖用户位置。

### 12.2 0.0.31 集合

标准 §11.4 定义 41 个语言错误码（34 个静态、7 个运行时）和 1 个实现扩展 `OutOfMemory`。本文不复制权威全表，VM 必须逐项实现 `tc_error_kind_name()` 映射并由单元测试覆盖。

相对 v0.0.26 的关键迁移：

| 动作 | 错误种类 |
| ---- | -------- |
| 新增/恢复 | `VarMissingInitializer`、`UninitializedVariable`、`BitcastWidthError`、`LabelInsideLoop`、`GotoInsideLoop`、`BreakOutsideLoop`、`ContinueOutsideLoop` |
| 删除 | `GotoSkipsVarInit`、`OverflowMode`、`ConstantCircular`、`FloatCastOverflow`、`CrossBlockReference` |
| 统一 | 模式错误 → `ModeMismatch`；转换溢出 → `CastOverflow` |

### 12.3 阶段

| 阶段 | 代表错误 |
| ---- | -------- |
| Lexer/Parser | Syntax、缩进、MissingEnd、VarMissingInitializer |
| Binder/Type | UndefinedVariable、DuplicateDefinition、TypeMismatch、ModeMismatch、BitcastWidth |
| Control/CFG | label/goto/loop context、UninitializedVariable |
| Const eval | ConstantExpression/Overflow/DivisionByZero/CastOverflow |
| Runtime | DivisionByZero、Integer/Float errors、CastOverflow、IOError |
| 任意实现阶段 | OutOfMemory |

TC 0.0.31 没有编译警告；`TcWarningList` 若为 ABI/结构兼容保留，只能为空壳，不承担初始化或控制流诊断。

---

## 13. 模块与接口

### 13.1 当前 v0.0.26 模块

| 目录 | 责任 |
| ---- | ---- |
| `src/vm/lexer/` | Token 与词法位置 |
| `src/vm/parser/` | 语句/RHS 解析、递归释放 |
| `src/vm/analyzer/` | Pass1、Pass2、DFA、常量求值、REPL 分析 |
| `src/vm/executor/` | typed program 执行 |
| `src/vm/runtime/` | 类型、诊断、符号、语义、I/O、stmt_index |
| `src/vm/driver/` | CLI、文件模式、REPL、版本 |
| `src/libtc/` | 嵌入式编译/执行入口 |

### 13.2 0.0.31 模块边界

实现可以在现有 Analyzer 内新增 CFG 子模块，或把 CFG 抽为同目录独立 `tc_cfg.c/h`。无论命名如何，接口边界必须满足：

- Parser 不执行全局类型或可达性判断；
- CFG 不重新解析源码；
- Dataflow 不通过源序猜测 goto；
- Executor/AOT 不各自重做不同版本的静态合法性；
- 数值和 I/O 语义继续从 runtime 共享。

新增源模块时必须遵守 `tc_<module>.h` ↔ `tc_<module>.c` 配对并同步 CMake 与架构索引。

### 13.3 公共接口

libtc 公共函数签名目标上保持不变，内部 `TcTypedProgram` 可扩展。所有尚未存在的 target kind、CFG 结构和辅助函数都属于目标设计，不构成当前 API 承诺。

---

## 14. REPL 边界

0.0.31 语言标准只定义批量源文件语义。当前 v0.0.26 REPL 采用逐行增量 Parse/Analyze/Execute，并拒绝多行控制流。

0.0.31 目标允许 tc-vm 继续采用以下实现策略：

- REPL 拒绝 `if`、`while`、`goto`、`label`、`break`、`continue` 等需要多行或全文件 CFG 的输入；
- 该拒绝是 REPL 能力限制，不是 `SyntaxError`，也不能用于判断同一批量文件非法；
- REPL 帮助文本必须列出限制；
- 已提交成功的跨行变量/常量状态保持原有所有权契约；失败输入不得污染会话。

未来若提供多行缓冲 REPL，必须先形成完整编译单元，再复用批量流水线；不得实现一套更弱的数据流规则。

---

## 15. 验证与交付门槛

### 15.1 测试分层

| 层 | 0.0.31 必测内容 |
| -- | -------------- |
| Lexer | `while`/`break`/`continue`/`bitcast` Token，缩进与科学计数边界 |
| Parser | 通用块、嵌套循环、MissingEnd、VarMissingInitializer、bitcast 形态 |
| Analyzer | 循环上下文、范式隔离、scope path、完整 CFG、固定点、常量剪枝 |
| Semantics | strict cast、truncate、bitcast、浮点 strict/ieee、NaN/舍入 |
| Executor | 零次/多次循环、嵌套 break/continue、goto 重入、槽生命周期 |
| AOT | 与 VM 正例输出、退出码、运行时错误和位模式差分 |
| libtc | 失败不修改 out、所有权、诊断阶段、OOM 回滚 |
| REPL | 明确拒绝新控制流且不污染状态 |

### 15.2 关键路径用例

- `while false` 不执行 body；`while true` 仅以可达 `break` 离开。
- `continue` 回到最内层条件，`break` 只离开最内层循环。
- while 内任何 goto/label 均静态拒绝，包括嵌套 if 内。
- goto 跳过但不读取变量合法；可达未初始化读取统一拒绝。
- 循环回边和 continue 前驱参与交集，不能把一次迭代初始化错误带到下一入口。
- bitcast 往返保持全部位，包括 `-0.0`、NaN payload 和整数最高位。
- float32 常量每步舍入与运行时相同。
- 旧浮点 wrap、浮点 truncate 位重解释和严格无符号窄化旧行为按迁移规则拒绝或改写。

### 15.3 交付声明

只有在：

1. 全部 0.0.31 目标 kind 与分发点实现；
2. 标准错误集合和打印名实现；
3. VM、AOT、let 一致性测试通过；
4. 全量 VM/unit/AOT 与覆盖/命名检查通过；
5. CLI/API/合规文档同步更新；

之后，`TC_VM_VERSION` 才能升为 0.0.31 并把本文状态改为已实现。

---

## 16. 实现基线与迁移

### 16.1 v0.0.26 当前事实

- 版本宏：`src/vm/driver/tc_version.h` 中 `TC_VM_VERSION = 0.0.26`。
- 已交付：整数、布尔、浮点、if/else、块作用域、受限 goto/label、路径敏感未初始化错误。
- 当前 IR：16 个 RHS kind、9 个 statement kind；无 while/break/continue/bitcast。
- 当前测试基线：384 VM、857 unit、210 AOT，证明 v0.0.26 实现状态。
- 当前仍接受 0.0.31 已删除的部分行为，例如无初始化器 `var`、浮点 wrap 与浮点 truncate 位重解释。

### 16.2 迁移顺序

建议按以下依赖顺序实现：

1. types/IR 与错误枚举；
2. Lexer/Parser 与通用块；
3. 作用域、while 上下文和 fixed slot；
4. CFG 与确定初始化；
5. 数值、bitcast 与常量求值；
6. Executor；
7. AOT；
8. CLI/API/REPL 与全量验证。

### 16.3 历史里程碑

| 版本 | 已交付主题 |
| ---- | ---------- |
| 0.0.24 | if/else、块作用域、缩进引擎 |
| 0.0.25 | float32/float64 全链路 |
| 0.0.26 | 受限 goto/label、路径敏感未初始化错误 |
| 0.0.31 | 本文目标：循环、完整 CFG、强制初始化、bitcast 与语义收敛；尚未交付 |

---

*本文的规范性语言规则均以 [TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md) 为准。*
