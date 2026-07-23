# TC-VM 详细设计说明书

> **规范基线（唯一权威）**：[TC 语言标准 0.0.35](./TC语言标准设计说明书-0.0.35.md) · [TC 编译器标准 0.0.35](./TC编译器标准设计说明书-0.0.35.md)
>
> **当前实现基线**：TC-VM v0.0.35（`TC_VM_VERSION`）
>
> **状态**：0.0.35 架构设计，涵盖模块系统、函数、memblock、ptr、struct 与完整 13 阶段编译管线。
>
> **适用范围**：多文件编译单元的 Parse、模块解析、Analyze、Execute，以及 tc-vm 的实现边界。

---

## 目录

1. [文档边界与设计目标](#1-文档边界与设计目标)
2. [总体架构](#2-总体架构)
3. [程序表示与目标 IR](#3-程序表示与目标-ir)
4. [Lexer、缩进与行模型](#4-lexer缩进与行模型)
5. [Parser 与块模型](#5-parser-与块模型)
6. [模块系统实现](#6-模块系统实现)
7. [作用域、绑定与槽位](#7-作用域绑定与槽位)
8. [类型系统实现](#8-类型系统实现)
9. [函数与调用模型实现](#9-函数与调用模型实现)
10. [CFG 与确定初始化](#10-cfg-与确定初始化)
11. [Executor](#11-executor)
12. [数值与 RHS 语义](#12-数值与-rhs-语义)
13. [`let` 常量求值](#13-let-常量求值)
14. [I/O](#14-io)
15. [诊断](#15-诊断)
16. [模块与接口](#16-模块与接口)
17. [验证与交付门槛](#17-验证与交付门槛)
18. [实现基线与迁移](#18-实现基线与迁移)

---

## 1. 文档边界与设计目标

### 1.1 版本基线

| 维度 | 版本 | 含义 |
| ---- | ---- | ---- |
| 语言规范 | 0.0.35 | 本文必须满足的合法程序集合、结果和诊断阶段 |
| 编译器规范 | 0.0.35 | 确定的 13 阶段编译管线、错误码与检查顺序 |
| 本文架构 | 0.0.35 设计 | 面向当前语言能力的实现设计 |

### 1.2 目标

- 同一份 typed program 可被 VM 执行器与 AOT 后端消费。
- Parse、模块解析、名称解析、类型检查、完整 CFG、确定初始化和执行阶段边界固定。
- `if`、`while`、`break`、`continue`、`goto`、短路表达式进入同一控制流模型。
- 支持多文件模块系统（`#program` / `#lib`、`import`、`public` / `private`、`Self`）。
- 支持函数、`funcall`、`return`、命名实参、按值只读形参、无环调用图。
- 支持 `ptr<T>`、`memblock<T, N>`、`struct`、`isize`/`usize` 等新型类型。
- 支持 `static var` / `static let`，按依赖拓扑初始化。
- VM、AOT 与 `let` 常量求值共享整数、浮点、转换和位重解释语义。
- 所有内存分配失败使用 `TC_ERR_OUT_OF_MEMORY`，不伪装成语言语法错误。

### 1.3 非目标

- 不在 VM 内引入 JIT、字节码文件格式或寄存器分配器。
- 不为未纳入 0.0.35 的递归、异常、闭包等预先定义 ABI。
- 不以更强的可选静态规则缩小 0.0.35 合法程序集。
- 不为未纳入公开 API 的内部符号承诺 C ABI 稳定性。

### 1.4 规范到实现的约束

语言标准与编译器标准决定可观察行为；本文只决定如何实现。若目标内部结构与标准冲突，以标准为准。实现可以替换算法或数据结构，但必须保持：

1. 接受和拒绝同一组批量程序；
2. 产生相同值、I/O 和控制流结果；
3. 在编译器标准规定的阶段报告同一错误种类；
4. VM、AOT 和常量求值结果一致。

---

## 2. 总体架构

### 2.1 目标流水线（13 阶段）

0.0.35 编译器标准 §1.2 定义 13 个确定性处理阶段，VM 后端遵循此流水线。各阶段按序执行，前一阶段有错时不进入下一阶段：

```text
source files (.tc)
  │
  ├─ 阶段 1: UTF-8 解码
  │
  ├─ 阶段 2: Lexer ──────────── 最长匹配词法、缩进栈、字面量上限检查
  │
  ├─ 阶段 3: Parser ─────────── 语法解析、受限恢复、操作数数量检查
  │
  ├─ 阶段 4: 模块与导入解析 ─── 4a 单文件结构 → 4b 导入定位 → 4c 依赖环 → 4d 收集函数签名
  │
  ├─ 阶段 5: 函数重名与签名 ─── 全局函数名冲突、参数名冲突
  │
  ├─ 阶段 6: 名称/作用域/类型 ─ 6a 控制流上下文 → 6b 作用域预建 → 6c goto/label 解析 → 6d 类型/模式/字面量 → 6e I/O 格式
  │
  ├─ 阶段 7: funcall 检查 ───── 调用目标、位置、实参与字面量约束
  │
  ├─ 阶段 8: return 检查 ────── 返回位置、形式、操作数与类型
  │
  ├─ 阶段 9: let/static let 求值 + static var 初始化器验证
  │
  ├─ 阶段 10: 静态布尔三态判定 / 逻辑读边判定
  │
  ├─ 阶段 11: CFG 构建、可达性、确定初始化固定点（多域：顶层 + 各函数独立）
  │
  ├─ 阶段 12: 调用图递归环检查
  │
  └─ 阶段 13: VM 代码生成 / 执行
```

**阶段间约束**（编译器标准 §1.2）：
- 阶段 4 子阶段 4a→4b→4c→4d 顺序执行，前一子阶段有错时不进入后一子阶段；
- 阶段 6 子阶段 6a→6b→6c→6d→6e 顺序执行，每个子阶段内按同一源文件的行列序选择首个诊断；
- 阶段 7（funcall）和阶段 8（return）有专用优先级规则，不得与阶段 6 的通用规则混淆；
- 阶段 12（调用图环检查）只在更早阶段全部成功后执行。

首个诊断的通用规则见编译器标准 §1.3（阶段优先 → 源位置优先 → 规则优先）。

`tc_compile_source` 或 `tc_compile_file` 只有在所有静态阶段（1–12）完成后才返回成功的 `TcTypedProgram`。Executor 不补做语法、名称或确定初始化检查；它只执行已验证程序并报告运行时错误。

### 2.2 失败模型

- 全流水线 fail-fast，`TcDiagnostic` 保存第一条错误。
- 每个阶段失败后释放自身已取得的所有权，不把部分初始化对象交给调用方。
- Parser 失败不修改输出程序；Analyzer 失败不产生可释放义务不明确的 `TcTypedProgram`。
- OOM 可发生在任意阶段，但始终映射为 `OutOfMemory`。

### 2.3 多文件编译单元

0.0.35 引入模块系统：一个 `#program` 文件通过 `import` 引用零个或多个 `#lib` 模块。编译器必须：

- 从入口 `#program` 文件出发，按 `import` 语句逐层加载所有可达模块；
- 使用依赖拓扑序处理全部可达模块（DAG）；
- 所有模块的 `static var` 和 `static let` 在函数签名收集后、函数体分析前完成初始化器求值；
- 同一模块的 `static var` 槽全程序唯一，不同导入者共享同一状态。

---

## 3. 程序表示与目标 IR

### 3.1 表示层次

| 层次 | 产出方 | 消费方 | 责任 |
| ---- | ------ | ------ | ---- |
| Token | Lexer | Parser | 词法类别、源位置、换行与原始缩进 |
| `TcProgram` / `TcModule` | Parser | Analyzer | 保留源结构和名称，不承诺类型合法 |
| 绑定/类型元数据 | Analyzer | CFG、Executor、AOT | 名称解析、类型、槽位、块路径 |
| CFG + 调用图 | Analyzer | Dataflow、审查工具 | 全部控制边与可达性 |
| `TcTypedProgram` | Analyzer | Executor、AOT | 完整静态验证后的所有权根 |

### 3.2 目标 `TcStmtKind`

0.0.35 的 statement 集合相较 0.0.31 增加函数、模块、memblock、ptr 与 struct 相关 statement：

```c
/* 既有 */
TC_STMT_VAR_DEF,
TC_STMT_CONST_DEF,
TC_STMT_ASSIGN,
TC_STMT_FIELD_ASSIGN,           /* a.b = rhs */
TC_STMT_WRITE,
TC_STMT_WRITELN,
TC_STMT_READ,
TC_STMT_IF,
TC_STMT_WHILE,
TC_STMT_BREAK,
TC_STMT_CONTINUE,
TC_STMT_LABEL_DEF,
TC_STMT_GOTO,

/* 0.0.35 新增 */
TC_STMT_FUNC_DEF,               /* func 定义 */
TC_STMT_FUNCALL,                /* funcall 独立调用 */
TC_STMT_RETURN,                 /* return [operand] */
TC_STMT_MEMBLOCK_STORE,         /* memblock_store */
TC_STMT_MEMBLOCK_COPY,          /* memblock_copy */
TC_STMT_PTR_STORE,              /* ptr_store */
TC_STMT_MEMCOPY_UNSAFE,         /* memcopy_unsafe */
TC_STMT_STRUCT_DEF,             /* struct 类型定义 */
TC_STMT_STATIC_VAR_DEF,         /* static var 定义 */
TC_STMT_STATIC_LET_DEF,         /* static let 定义 */
TC_STMT_IMPORT,                 /* import 声明 */
```

### 3.3 目标 `TcRhsKind`

0.0.35 新增大量 RHS kind：

```c
/* 既有 */
TC_RHS_IDENTIFIER,
TC_RHS_QUALIFIED_NAME,          /* 模块.成员 限定名 */
TC_RHS_INT_LITERAL,
TC_RHS_FLOAT_LITERAL,
TC_RHS_FLOAT_SPECIAL,
TC_RHS_BOOL_LITERAL,
TC_RHS_NULLPTR,
TC_RHS_BINARY_OP,
TC_RHS_UNARY_OP,
TC_RHS_COMPARE,
TC_RHS_LOGICAL,
TC_RHS_BITWISE,
TC_RHS_SHIFT,
TC_RHS_CAST,
TC_RHS_BITCAST,

/* 0.0.35 新增 */
TC_RHS_MEMBLOCK_LOAD,           /* memblock_load(T, mb, idx) */
TC_RHS_MEMBLOCK_CONSTRUCTOR,    /* memblock(T, count: N, ...) */
TC_RHS_MEMBLOCK_COUNT,          /* mb.count */
TC_RHS_STRUCT_CONSTRUCTOR,      /* StructName(field1: v1, ...) */
TC_RHS_FIELD_READ,              /* a.b 字段读取 */
TC_RHS_PTR_LOAD,                /* ptr_load(T, ptr_val) */
TC_RHS_PTR_ADDRESS,             /* ptr_address(T, ident) */
TC_RHS_PTR_ADD,                 /* ptr_add(T, ptr_val, offset) */
TC_RHS_PTR_SUB,                 /* ptr_sub(T, ptr_val, offset) */
TC_RHS_PTR_EQ,                  /* ptr_eq(T, p1, p2) */
TC_RHS_PTR_NE,                  /* ptr_ne(T, p1, p2) */
TC_RHS_PTR_LT,                  /* ptr_lt(T, p1, p2) */
TC_RHS_PTR_LE,                  /* ptr_le(T, p1, p2) */
TC_RHS_PTR_GT,                  /* ptr_gt(T, p1, p2) */
TC_RHS_PTR_GE,                  /* ptr_ge(T, p1, p2) */
TC_RHS_PTR_SIZE,                /* ptr_size(T, ptr_val) */
TC_RHS_FUNCALL_EXPR,            /* var x: T = funcall(...) */
TC_RHS_SELF_MEMBER,             /* Self.成员 */
```

### 3.4 槽位管理

每个运行时绑定（`var` / `static var` / 形参）分配唯一槽位。槽位按以下规则分配：

| 绑定类别 | 槽位生命周期 | 槽位域 |
| -------- | ------------ | ------ |
| 顶层 `var` | 程序生命期 | 顶层槽数组 |
| 函数形参 | 函数调用帧 | 函数帧参数区 |
| 函数局部 `var` | 函数调用帧 | 函数帧局部区 |
| `static var` | 程序生命期，全程序唯一 | 静态槽数组 |
| `let` / `static let` | 编译期内联，无槽 | — |

- 槽位存储 `TcValue` 位模式（`uint64_t` 载体 + 类型标记）。
- `memblock` 值：槽位存储指向堆上 memblock 存储区的指针（或内联小 memblock 需实现决策）。
- `struct` 值：槽位存储按 §3.9.3 布局的连续字节序列。
- `ptr<T>` 值：槽位存储抽象指针位模式。

### 3.5 语句序号

`stmt_index` 继续为源语句树提供稳定 DFS 扁平序号，用于：

- 标签和 goto 目标；
- 诊断与源映射；
- CFG 节点关联；
- Executor/AOT 的唯一标签名；
- 测试中的确定性断言。

所有新增 statement kind 各占一个语句序号；`if`/`while`/`func` 的子语句按源序递归编号。

### 3.6 所有权

- `TcProgram` 拥有全部语句数组、名称字符串、RHS operand 名称、`import` 列表、函数签名列表和子块。
- Analyzer 成功时把 `TcProgram` 所有权转移给 `TcTypedProgram`。
- CFG 若持久化在 typed program 中，由 typed program 统一释放；若可重建，AOT 与 Executor 不得各自维护语义不同的副本。
- 新增 kind 后，parser-free、Analyzer 分发、Executor 分发、AOT 分发和测试覆盖必须同步。

---

## 4. Lexer、缩进与行模型

### 4.1 词法分析器

词法分析器采用 **最长匹配** 策略（对应编译器标准 §2.1）：

1. 扫描 `0` 时，检查后续字符是否为 `x`/`X`/`b`/`B`/`o`/`O`，转入对应进制。
2. 数字部分扫描完成后，检查后续字符是否为 `u`/`U`，归约为带后缀字面量。
3. 否则归约为无后缀字面量。

**字面量 Token 自身上限检查**（第 2 阶段）：
- 整数字面量数值不得超过 `2^64 − 1` → `TC_CE_LITERAL_OUT_OF_RANGE`。
- 有限浮点字面量按后缀源类型舍入为零/无穷 → `TC_CE_LITERAL_OUT_OF_RANGE`。

**新增 Token 识别**：
- `ptr`、`memblock`、`struct`、`func`、`funcall`、`return`、`void`、`isize`、`usize` 等新关键字。
- `nullptr` 字面量关键字。
- `Self` 关键字。
- `public`、`private` 可见性关键字。
- `static` 关键字。
- `import` 关键字。
- `#program`、`#lib` 模式指令。
- `inf`、`-inf`、`nan` 特殊浮点 Token（`float_special`）。
- `@padding` 属性 Token（仅出现在 struct 字段声明中）。

### 4.2 缩进处理

缩进由词法器维护的缩进级别栈处理（对应编译器标准 §2.2）：

1. 初始仅含顶层级别 `0`。
2. 行首只能出现 ASCII 空格 U+0020；出现 U+0009 → `TC_CE_INDENT_MIXED`。
3. 空格数量必须能被 4 整除；空格组数量与栈顶比较：
   - 增加时必须恰好增加 `1`，压栈并生成一个 `INDENT`；
   - 减少时逐项出栈并生成 `DEDENT`；
   - 减少后的空格组数量必须等于某个既有栈值。
4. 一级缩进恒为 4 个连续 ASCII 空格 U+0020。
5. 空行和纯注释行只生成 `NEWLINE`，不改变缩进栈。
6. `INDENT`/`DEDENT` 反映 `func`/`if`/`else`/`while`/`struct` 语句体的实际缩进。

### 4.3 行模型

- 每个逻辑行至多一条语句。`;` 引入行注释。
- 空行和纯注释行不产生语句节点。
- Parser 输入必须保留行号、首 Token 列号、行首缩进原始字符序列、`NEWLINE`/EOF 边界。

---

## 5. Parser 与块模型

### 5.1 模块头层解析

Parser 首先解析模块模式指令（第 1 层）和 `import` 声明区（第 2 层）：

```text
#program          → 程序入口模式
#lib              → 库模式
import <模块名>    → 导入声明
```

### 5.2 通用块解析

采用统一块帧模型：

```text
BlockFrame {
    owner_kind       GLOBAL | IF_THEN | IF_ELSE | WHILE | FUNC | STRUCT
    owner_line
    indent_level
    statements
}
```

- `#program` 顶层不会出现 `func` 定义块。
- `#lib` 顶层出现 `func` / `struct` 定义块。
- `if ... then` 打开 then 帧，可选 `else` 切换到独立互斥子作用域。
- `while ... then` 打开 while 帧。
- `func name(type1, ...) -> return_type then` 打开函数体帧。
- `struct Name then` 打开结构体定义帧。
- `end` 关闭最近的块所有者。
- `label` 是普通语句，不打开块。

### 5.3 函数定义解析

```text
[public|private] func name(param1: type1, ...) -> return_type then
    statements
end
```

- `-> return_type` 可选，省略表示 `void` 返回。
- 形参格式：`name: type`，类型仅可为标量、`memblock<T, N>`、`ptr<T>` 或结构体类型。
- `void` 不得作为形参类型（语法拒绝）。
- Parser 收集形参名列表，检查是否含重复 → `TC_CE_DUPLICATE_PARAMETER`。

### 5.4 `funcall` 解析

```text
funcall(func_name_or_qualified, arg1: expr1, arg2: expr2, ...)
```

- 第一项为函数标识符或 `<模块>.<函数名>` 或 `Self.<函数名>`。
- 每项实参使用 `<形参名>: <实参>` 形式。
- Parser 不在此阶段验证函数是否存在。

### 5.5 `return` 解析

```text
return                 ; void 返回
return operand         ; 有值返回
```

- 顶层不可出现 `return`（在 Analyzer 阶段拒绝为 `TC_CE_RETURN_OUTSIDE_FUNCTION`）。

### 5.6 类型定义解析

**struct 定义**：

```text
[public|private] struct Name then
    let field1: type1 @padding(N)
    var field2: type2 @padding(N)
end
```

- 每行恰好一个字段。
- `@padding(N)` 可选，省略等同 `@padding(0)`。
- 字段以 `let` / `var` 区分可变性。

**memblock 引用**：

```text
memblock<T, N>
```

- `T` 为完整类型名，`N` 为编译期 `usize` 常量表达式。

### 5.7 `static` 声明解析

```text
[public|private] static let name: type = const_rhs
[public|private] static var name: type = rhs
```

- 仅在 `#lib` 中合法。
- `#program` 中出现 `static` → 语法拒绝 / `TC_CE_PROGRAM_MODE_MISUSE`。

### 5.8 变量初始化器与操作数数量

- `var name: type` 必须带 `= <rhs>` 或 `= funcall(...)` → 否则 `TC_CE_VAR_MISSING_INIT`。
- 操作数数量按编译器标准 §1.3 权威表在语法阶段检查；不符 → `TC_CE_OPERAND_COUNT`。

---

## 6. 模块系统实现

### 6.1 模块加载与依赖图

编译器严格按编译器标准 §4.1 的子阶段顺序完成模块加载（第 4 阶段），子阶段间有严格的前后依赖（4a → 4b → 4c → 4d）：

1. **单文件结构检查（4a）**：
   - 检查五层排序（模式指令 → import → struct → 值声明 → 函数/语句）；
   - `#lib` 成员显式可见性检查（`TC_CE_MISSING_VISIBILITY`）；
   - `#program` 误用检查（`TC_CE_PROGRAM_MODE_MISUSE`）；
   - 同一文件内错误优先级：`TC_CE_MODULE_LAYER` → `TC_CE_MISSING_VISIBILITY` → `TC_CE_PROGRAM_MODE_MISUSE`。

2. **导入解析（4b）**：
   - 对每个 `import <模块名>`，在模块搜索路径中唯一定位 `模块名.tc`；
   - 找不到 → `TC_CE_IMPORT_NOT_FOUND`；
   - 目标非 `#lib` → `TC_CE_IMPORT_NOT_LIB`；
   - 逻辑名歧义 → `TC_CE_IMPORT_AMBIGUOUS`；
   - 重复导入 → `TC_CE_DUPLICATE_IMPORT`；
   - 导入名与本模块顶层名称冲突 → `TC_CE_IMPORT_NAME_CONFLICT`；
   - 同一 `import` 行上优先级依次为：`TC_CE_IMPORT_NOT_FOUND` → `TC_CE_IMPORT_NOT_LIB` → `TC_CE_IMPORT_AMBIGUOUS` → `TC_CE_DUPLICATE_IMPORT` → `TC_CE_IMPORT_NAME_CONFLICT`。

3. **依赖图环检查（4c）**：以模块为顶点、`import` 为边检查 DAG。存在多个环时，选择包含源位置最早 `import` 边的环 → `TC_CE_CIRCULAR_IMPORT`。

4. **收集函数签名（4d）**：仅在 4a–4c 全部成功后，收集全部可达 `#lib` 模块的函数签名，供后续阶段使用。

### 6.2 模块命名空间

- 每个 `#lib` 模块维护一个命名空间：`static let`、`static var`、`func`、`struct` 名。
- 函数体内通过 `Self.<成员名>` 访问本库成员。
- 导入者通过 `<模块名>.<成员名>` 访问公开成员。
- `private` 成员的外部访问 → `TC_CE_PRIVATE_MEMBER_ACCESS`。

### 6.3 `Self` 解析

- `Self` 仅在 `#lib` 模块内有效，表示当前模块命名空间。
- 函数体内裸名引用本库成员 → `TC_CE_FUNCTION_SCOPE_ACCESS`（须改为 `Self.<名>`）。

### 6.4 本库顶层成员名索引

在第 6b 子阶段为每个 `#lib` 建立本库顶层成员名索引，收集全部 `func` / `static let` / `static var` 的声明名。仅用于错误分类：函数内裸名查找失败后，若该名存在于索引，报告 `TC_CE_FUNCTION_SCOPE_ACCESS`；否则报告 `TC_CE_UNDEFINED_VARIABLE` 或 `TC_CE_UNDEFINED_FUNCTION`。

---

## 7. 作用域、绑定与槽位

### 7.1 多级作用域

0.0.35 的作用域层次：

| 层级 | 可见范围 | 绑定类别 |
| ---- | -------- | -------- |
| 全局函数签名表 | 全程序，先于函数体收集 | `func` 名 |
| 模块顶层命名空间 | `#lib` 内通过 `Self.`，外部通过 `<模块>.` | `static let` / `static var` / `func` / `struct` 名 |
| 顶层值作用域 | `#program` 顶层不在块内的绑定 | `var` / `let` |
| 函数作用域 | 函数体内 | 形参、局部 `var` / `let`、标签 |
| 块级作用域 | `if` then/else、`while` body | 局部 `var` / `let` |
| 标签作用域 | 函数体内，沿祖先链查找 | `label` 名 |

### 7.2 源序可见性

- `var`/`let` 只在定义之后可见。
- 外层已定义绑定在内层可见，内层同名可屏蔽外层。
- 离开块后，块内绑定不可见。
- 函数签名先于函数体收集，可调用后定义的函数（调用图不得有环）。
- `let` / `static let` 编译期内联，无运行时槽。

### 7.3 固定槽位分配

Pass1 为每个运行时绑定分配唯一 slot：

| 绑定类别 | 槽位分配 | 初始化 |
| -------- | -------- | ------ |
| `#program` 顶层 `var` | 连续槽位，程序生命期 | 执行 `var` 语句时写入 |
| `#lib` `static var` | 全程序唯一静态槽 | 按依赖拓扑序初始化 |
| 函数形参 | 函数帧参数区 | 帧建立时写入 |
| 函数局部 `var` | 函数帧局部区 | 执行 `var` 语句时写入 |
| `memblock` `var` | 槽位存储堆指针或内联存储 | 执行 `var` 时分配并初始化 |
| `struct` `var` | 槽位存储按布局的连续字节 | 执行 `var` 时写入 |
| `ptr<T>` `var` | 槽位存储指针位模式 | 执行 `var` 时写入 |

### 7.4 标签解析与范式隔离

- `goto` 从当前作用域沿父链查找标签，首个同名标签胜出。
- 目标在子作用域 → `TC_CE_JUMP_INTO_BLOCK`。
- 可见链无目标且无同名后代候选但存在不可比候选 → `TC_CE_JUMP_INCOMPATIBLE_BLOCK`。
- 跨函数标签（当前函数无同名标签、另一函数有） → `TC_CE_CROSS_CONTROL_FLOW_JUMP`。
- `while` 祖先内出现 `goto` → `TC_CE_GOTO_INSIDE_LOOP`。
- `while` 祖先内定义 `label` → `TC_CE_LABEL_INSIDE_LOOP`。
- 无 `func` 祖先的 `goto`/`label` → `TC_CE_GOTO_OUTSIDE_FUNCTION` / `TC_CE_LABEL_OUTSIDE_FUNCTION`。
- 范式隔离在 CFG 构建前完成；非法 goto 不进入 CFG。

---

## 8. 类型系统实现

### 8.1 类型表示

```c
typedef enum {
    TC_INT8, TC_UINT8, TC_INT16, TC_UINT16,
    TC_INT32, TC_UINT32, TC_INT64, TC_UINT64,
    TC_FLOAT32, TC_FLOAT64,
    TC_BOOL,
    TC_VOID,                     /* 仅返回类型 */
    TC_ISIZE, TC_USIZE,          /* 平台字长 */
    TC_PTR,                      /* ptr<T>，参数化为所指类型 */
    TC_MEMBLOCK,                 /* memblock<T>，参数化为元素类型 */
    TC_STRUCT                    /* 用户定义的结构体，由 struct_id 区分 */
} TcTypeKind;

typedef struct {
    TcTypeKind kind;
    union {
        struct { TcType *pointee; } ptr_type;
        struct { TcType *element; size_t count; } memblock_type;
        struct { int struct_id; } struct_type;
    } params;
} TcType;
```

### 8.2 类型宽度计算

```c
size_t sizeof_bits(const TcType *type) {
    switch (type->kind) {
    case TC_INT8:   case TC_UINT8:   case TC_BOOL:   return 8;
    case TC_INT16:  case TC_UINT16:                   return 16;
    case TC_INT32:  case TC_UINT32:  case TC_FLOAT32: return 32;
    case TC_INT64:  case TC_UINT64:  case TC_FLOAT64: return 64;
    case TC_ISIZE:  case TC_USIZE:   case TC_PTR:     return target_ptr_width();
    case TC_MEMBLOCK:
        return sizeof_bits(&tc_type_usize)
             + type->params.memblock_type.count
             * sizeof_bits(type->params.memblock_type.element);
    case TC_STRUCT:
        return struct_total_width(type->params.struct_type.struct_id);
    default: return 0;
    }
}
```

### 8.3 memblock 类型实现

- **类型等价**：仅由元素类型 `T` 决定；`memblock<int32, 10>` 与 `memblock<int32, 20>` 属于同一 TcType。
- **`N` 记录**：符号表为每个 memblock 绑定记录 `N` 的数学值。
- **类型级宽度**：`sizeof_bits(usize) + N × sizeof_bits(T)`。
- **`N` 比较**：赋值、传参时比较两侧声明的 `N` → `TC_CE_MEMBLOCK_SIZE_MISMATCH`。
- **`.count`**：编译期常量，结果等于声明 `N` 的数学值。
- **运行时存储**：memblock 值以堆上分配的连续存储区实现（长度头部 + 元素数据），槽位存储指向该存储区的指针。按值传参/赋值时执行深拷贝。

### 8.4 指针类型实现

- **类型携带所指 `T`**：`ptr<int32>` 与 `ptr<float64>` 不等价。
- **宽度**：恒为目标平台指针宽度（32 或 64 位），与 `T` 无关。
- **`nullptr` 定型**：由期望类型唯一确定所指类型。
- **禁止通用标量运算**：`ptr<T>` 不得进入通用算术、位运算、比较。
- **指针操作**：全部经专用 `ptr_*` 指令，见 §12.7。

### 8.5 结构体类型实现

- **类型定义**：符号表为每个结构体维护字段列表（名称、类型、可变性、padding）、总宽度。
- **值构造器验证**（第 6d 子阶段）：
  - 全字段必填，命名实参文本顺序与声明顺序一致；
  - 每个实参类型须与字段类型严格一致。
- **字段赋值双层检查**：外层绑定种类 × 字段 `let`/`var`，按编译器标准 §3.3 矩阵判定。
- **嵌套字段访问**：`a.b.c` 中间结果须为结构体类型。
- **运行时存储**：结构体值以连续字节序列存储，槽位存储该序列。按值传参/赋值时执行整块复制（memcpy）。

### 8.6 类型等价判定

```c
bool tc_type_equals(const TcType *a, const TcType *b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case TC_PTR:   return tc_type_equals(a->params.ptr_type.pointee,
                                          b->params.ptr_type.pointee);
    case TC_MEMBLOCK: return tc_type_equals(a->params.memblock_type.element,
                                            b->params.memblock_type.element);
                      /* N 不参与等价 */
    case TC_STRUCT: return a->params.struct_type.struct_id
                          == b->params.struct_type.struct_id;
    default: return true;
    }
}
```

---

## 9. 函数与调用模型实现

### 9.1 函数签名数据结构

```c
typedef struct {
    char *name;                  /* 函数名 */
    int line;                    /* 定义行号 */
    bool is_public;              /* public / private */
    TcType return_type;          /* 返回类型（可为 TC_VOID） */
    TcFuncParam *params;         /* 形参列表 */
    size_t param_count;
    TcStatement *body;           /* 函数体语句 */
    size_t body_count;
    int module_id;               /* 所属模块 */
} TcFuncSignature;
```

### 9.2 调用帧

每次 `funcall` 创建以下逻辑状态：

- 被调函数标识；
- 返回地址（调用者续点）；
- 按形参绑定的实参值（按值复制）；
- 函数局部 `var` 槽位（槽数组长度为函数局部变量数）；
- 活动块路径和函数内控制流状态；
- 非 `void` 函数的返回值位置。

```c
typedef struct {
    int func_id;
    size_t return_stmt_index;    /* 调用者续点 */
    TcValue *param_slots;        /* 形参槽位 */
    size_t param_count;
    TcValue *local_slots;        /* 局部 var 槽位 */
    size_t local_count;
    size_t *block_path;          /* 活动块路径 */
} TcCallFrame;
```

### 9.3 调用步骤（Executor 实现）

1. 按形参声明顺序，从左到右检查实参名称和期望类型，并读取实参操作数。
2. 任一运行时错误立即终止整个程序。
3. 创建被调函数帧并写入参数值（按值复制）。
4. 从函数体第一条语句开始执行。
5. 执行 `return` 时销毁函数帧并恢复调用者。
6. 非 `void` 结果若用于 `var` 声明则初始化新变量，若用于已有变量赋值则覆盖目标变量的当前值。

### 9.4 调用图与递归环检查

- 为全部函数体内的 `funcall` 建立有向调用图（第 12 阶段）。
- 函数调用图存在环 → `TC_CE_RECURSION`。
- 自调用（直接递归）是长度为 1 的环；间接递归同样非法。
- 递归环诊断的确定性选择规则按编译器标准 §8.9。

---

## 10. CFG 与确定初始化

### 10.1 多域 CFG

0.0.35 的 CFG 分为多个封闭域：

| CFG 域 | 入口 | 边界 |
| ------ | ---- | ---- |
| 顶层 CFG | 程序入口 | 不含 `return`/`goto`/`label`（第 6a 阶段拒绝）；顶层 `funcall` 为原子节点 |
| 函数 CFG | 函数体入口节点 | 含 `return`/`goto`/`label`/`break`/`continue`；边界止于函数 `end` |

顶层 CFG 与各函数 CFG 是完整、封闭、彼此独立的分析单元，不得拼接、内联或共享节点、边、可达性或数据流状态。

### 10.2 边类型

| 结构 | 必需边 |
| ---- | ------ |
| 普通语句 | 当前节点 → 下一顺序节点 |
| `if` | condition → then；condition → else/after；分支末尾 → after |
| `while` | condition true → body；condition false → after；body 正常末尾 → condition |
| `continue` | continue → 最内层 while condition |
| `break` | break → 最内层 while after |
| `goto` | goto → 目标 label 后第一节点；无顺序后继 |
| `label` | label → 下一顺序节点；接收源序落入边和 goto 入边 |
| `return` | return → 当前函数的正常返回终止节点；无函数内后继 |
| `funcall` (void) | 当前节点 → 下一顺序节点 |
| `funcall` (非 void) | 当前节点 → 下一顺序节点（返回值由 Executor 处理） |
| 短路 `and`/`or` | 左值决定跳过或进入右操作数检查路径 |

### 10.3 传递函数

```
IN[top-level entry] = ∅
IN[function entry]  = 当前函数的全部参数
IN[n]     = ∩ EDGE_OUT[p → n]
OUT[var x = rhs] = IN[n] ∪ {x}
OUT[var x = funcall(...)] = IN[n] ∪ {x}
OUT[x = funcall(...)] = IN[n]  ; x 须 ∈ IN[n]
OUT[let ...] = IN[n]               ; 无运行时效果
OUT[static var x = rhs] = IN[n] ∪ {x}  ; 仅顶层
OUT[其他语句] = IN[n]
```

- `var` 的 RHS 在把 `x` 加入集合前检查；初始化自引用仍非法。
- 会合点取可达前驱集合的交集。
- 最大固定点语义，等价于路径语义定义。
- `funcall` 在调用者 CFG 中是单个原子节点，不展开被调函数 CFG。
- `return` 节点只连接当前函数的正常返回终止节点。

### 10.4 构建顺序

1. 完成结构、名称、类型和 goto 合法性检查（第 6 阶段）；
2. 完成 `let`/`static let` 求值（第 9 阶段）；
3. 执行静态布尔三态判定与逻辑读边判定（第 10 阶段）；
4. 建立全部节点及 label 目标；
5. 连接结构化边；
6. 连接 break/continue 与 goto 边；
7. 应用合法常量条件剪枝（静态 true/false 删除不可能边）；
8. 计算可达性；
9. 仅在可达子图上运行确定初始化固定点；
10. 报告不可达语句（`TC_CE_UNREACHABLE_STATEMENT`）、未初始化读取（`TC_CE_UNINITIALIZED_VARIABLE`）、缺少返回（`TC_CE_MISSING_RETURN`）。

---

## 11. Executor

### 11.1 输入契约

Executor 只接收成功的 `TcTypedProgram`：

- 所有名称已绑定；
- 类型与模式合法；
- goto/label 与循环上下文合法；
- 确定初始化已证明；
- 调用图无环；
- `let` / `static let` 已求值并可内联；
- `static var` 初始化器已验证。

### 11.2 目标执行控制

使用控制信号模型：

```c
typedef enum {
    TC_EXEC_NORMAL,
    TC_EXEC_BREAK,       /* 携带 loop_id */
    TC_EXEC_CONTINUE,    /* 携带 loop_id */
    TC_EXEC_GOTO,        /* 携带 target_stmt_index */
    TC_EXEC_RETURN,      /* 携带返回值（若非 void） */
    TC_EXEC_ERROR
} TcExecResult;
```

### 11.3 顶层执行

```
for each top-level statement in source order:
    result = execute(statement)
    if result == ERROR: terminate program
```

- 顶层 `funcall` 直接执行函数体。
- 顶层 `static var` 在进入顶层执行前已按依赖拓扑序初始化。

### 11.4 函数执行

```
function_execute(func_id, args[]):
    frame = create_frame(func_id, args)
    for each statement in function body:
        result = execute_in_frame(frame, statement)
        if result == RETURN:
            return result.value
        if result == ERROR:
            return error
    /* 到达 end 而 return_type != void → TC_CE_MISSING_RETURN（已在静态阶段拒绝） */
```

### 11.5 `while` 执行

```
loop:
    condition = eval(bool_rhs)      ; 可能触发运行时错误
    if condition == false: leave
    result = execute(body)
    if result == BREAK(this): leave
    if result == CONTINUE(this): goto loop
    if result == GOTO: propagate
    if result == RETURN: propagate
    goto loop
leave:
    continue after end
```

### 11.6 `goto` 与标签

- 标签本身零成本。
- `goto` 将下一执行位置设为目标标签后的第一条语句。
- 向后跳转再次执行 `var` 时覆盖同一固定槽。
- 向外跳转携带离开作用域集合；不得保留已离开块的活动绑定。

### 11.7 运行时槽管理

- 槽数组长度为变量绑定总数（顶层 + 函数局部 + `static var`）。
- 每槽存 `TcValue` 位模式。
- `memblock` 槽：槽位存储指向堆上 memblock 存储区的指针。进入作用域时分配存储（含长度头部初始化），离开作用域时释放。按值传参/赋值时执行深拷贝。
- `struct` 槽：槽位存储连续字节序列。赋值/传参时整块复制。
- `ptr<T>` 槽：槽位存储指针位模式。

### 11.8 模块初始化

在顶层执行开始前，按依赖拓扑序依次初始化所有可达模块的 `static var`：

1. 对每个可达模块，按源序执行 `static var` 初始化器；
2. 初始化器结果写入全程序唯一静态槽位；
3. 任一 `static var` 初始化失败时整个程序准备失败。

---

## 12. 数值与 RHS 语义

### 12.1 值表示

```c
typedef struct {
    TcTypeKind type;             /* 解释位模式所需的类型元数据 */
    uint64_t bits;               /* 位模式（窄整数只使用低位） */
} TcValue;
```

对于 `memblock` 和 `struct` 值，`bits` 存储指向堆上存储的指针（或以扩展字段实现）。

### 12.2 整数

- strict：有符号 `add/sub/mul/neg/shl` 检查范围；除零和 `INT_MIN / -1` 按标准报错。
- `wrap`：仅用于标准允许的整数操作，按目标位宽回绕。
- `isize`/`usize`：行为与同位宽 `int*`/`uint*` 一致。
- `shr` 不掩码计数；负移位计数 → `TC_RE_NEGATIVE_SHIFT_COUNT`。

### 12.3 浮点

| 模式 | 行为 |
| ---- | ---- |
| strict | 检测除零、上溢、下溢和无效操作并报告规定错误 |
| `ieee` | 产生 IEEE 754 结果，不把标准 NaN/Infinity 结果改为语言错误 |

- 每个操作按声明的 `float32` 或 `float64` 精度舍入，不得先用宿主更高精度串联计算再只在末尾舍入。
- 固定舍入模式 roundTiesToEven。
- 禁止 FMA。
- 保留非规格化数。

### 12.4 `cast`

严格 `cast` 是数值转换。目标值必须可由目标类型表示。运行时失败统一为 `TC_RE_CAST_OVERFLOW`；常量阶段为 `TC_CE_CONSTANT_CAST_OVERFLOW`。

### 12.5 `truncate`

`cast(T, truncate, operand)`：仅整数→更窄整数，保留低位。其它组合在静态阶段报告 `TC_CE_MODE_MISMATCH`。

### 12.6 `bitcast`

- 源、目标必须等位宽（Analyzer 已保证）。
- `bool` 不参与（静态拒绝 `TC_CE_TYPE_MISMATCH`）。
- 不做数值转换；位模式原样复制。
- 执行器：`slots[DST] = slots[SRC] & tc_width_mask(TARGET_TYPE)`。

### 12.7 指针操作

| 指令 | Executor 实现 |
| ---- | ------------ |
| `ptr_load(T, ptr)` | 读取 ptr 所指槽位的值；`nullptr` → `TC_RE_NULL_POINTER_DEREFERENCE` |
| `ptr_store(T, ptr, value)` | 将 value 写入 ptr 所指槽位；`nullptr` 同上 |
| `ptr_address(T, ident)` | 返回绑定槽位的抽象地址 |
| `ptr_add(T, ptr, offset)` | 返回 ptr + offset 的抽象地址；`nullptr` → `TC_RE_NULL_POINTER_ARITHMETIC` |
| `ptr_sub(T, ptr, offset)` | 返回 ptr - offset 的抽象地址；`nullptr` 同上 |
| `ptr_eq/ptr_ne(T, p1, p2)` | 比较抽象地址；两个 `nullptr` → `true`/`false`，合法不报错 |
| `ptr_lt/le/gt/ge(T, p1, p2)` | 比较抽象地址序；`nullptr` → `TC_RE_NULL_POINTER_DEREFERENCE` |
| `ptr_size(T, ptr)` | 返回 `sizeof_bits(T)`，编译期常量，直接内联 |

### 12.8 memblock 操作

| 指令 | Executor 实现 |
| ---- | ------------ |
| `memblock_load(T, mb, idx)` | 边界检查 `0 ≤ idx < N` → 读取元素 `T[idx]`；越界 → `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE` |
| `memblock_store(T, mb, idx, value)` | 边界检查 → 写入元素；越界同上；不修改目标 |
| `memblock_copy(T, dst, d_idx, src, s_idx, len)` | 区间检查 → 先拷入临时缓冲再写入目标（memmove 语义）；越界同上；不修改目标 |
| `memcopy_unsafe(T, dst, d_idx, src, s_idx, len)` | `nullptr` → `TC_RE_NULL_POINTER_DEREFERENCE`；`len < 0` → `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`；不检查越界；先拷入临时缓冲再写入目标 |

### 12.9 结构体操作

- **值构造器**：按字段声明顺序依次求值，构造整块字节序列（填充字节为 `0x00`）。
- **字段读取** `a.b`：从结构体字节序列中按字段偏移读取。
- **字段赋值** `a.b = rhs`：按字段偏移写入结构体字节序列。

---

## 13. `let` 常量求值

### 13.1 合法形式

`let` RHS 只能是编译器标准 §5.2.1 定义的原子表达式或单层调用表达式：

- 字面量（整数、浮点、bool、`nullptr`）；
- 源序中更早的 `let` / `static let` 标识符；
- `Self.` / 导入限定形式的只读成员；
- `ptr_size` 查询；
- `.count` 查询；
- 单个运算、比较、逻辑、cast、truncate 或 bitcast 调用，其 operand 均为上述原子；
- memblock 构造器、结构体值构造器。

### 13.2 求值器

常量求值调用与 Executor 相同的纯语义核心。浮点每步按声明精度舍入，strict/ieee 行为与运行时一致。

### 13.3 错误映射

| 运行时类别 | 常量阶段类别 |
| ---------- | ------------ |
| Integer/Float overflow or underflow | `TC_CE_CONSTANT_OVERFLOW` |
| Division by zero | `TC_CE_CONSTANT_DIV_ZERO` |
| Cast overflow | `TC_CE_CONSTANT_CAST_OVERFLOW` |
| 非法常量形态或浮点无效操作 / 负移位计数 | `TC_CE_CONSTANT_EXPRESSION` |

### 13.4 `static let` 与 `static var` 初始化

- `static let` 在收集函数签名后、分析函数体前按依赖拓扑序求值。
- `static var` 初始化器同 `static let` 编译期约束验证，但不执行编译器内求值（运行时执行）。
- `static var` 初始化器操作数仅可为字面量、已成功初始化的 `Self` 成员（`static let` 与 `static var`），以及导入的公开 `static let` / `static var`。

---

## 14. I/O

### 14.1 输出

`write`/`writeln` 支持 13 种格式符。Analyzer 在执行前验证格式符、类型和 operand 数量；runtime 只负责格式化和写入错误。

### 14.2 输入

`read(type, name)` 目标必须已定义、类型一致且在当前 CFG 点确定初始化。`read` 覆盖已有值，不能替代声明初始化器。非法输入、EOF、范围错误或流失败统一为 `TC_RE_IO`。

### 14.3 跨后端一致性

VM 与 AOT 共用 `tc_io`，特别是：符号、进制、浮点格式、NaN/Infinity 文本、换行和错误时机。

---

## 15. 诊断

### 15.1 单槽与位置

`TcDiagnostic` 保持 fail-fast 单槽，包含 kind、消息、文件名、行、列和源片段。所有路径必须保留原始源位置。

### 15.2 0.0.35 错误码集合

编译器标准 §11.4 定义了完整错误码表，包括：

- **通用与核心**（§11.4.1）：`TC_CE_SYNTAX`、`TC_CE_UNDEFINED_VARIABLE`、`TC_CE_DUPLICATE_DEFINITION`、`TC_CE_TYPE_MISMATCH`、`TC_CE_LITERAL_OUT_OF_RANGE`、`TC_CE_LITERAL_TYPE`、`TC_CE_KEYWORD`、`TC_CE_CONSTANT_ASSIGNMENT`、`TC_CE_CONSTANT_EXPRESSION`、`TC_CE_CONSTANT_OVERFLOW`、`TC_CE_CONSTANT_DIV_ZERO`、`TC_CE_CONSTANT_CAST_OVERFLOW`、`TC_CE_COMPARISON_TYPE_MISMATCH`、`TC_CE_FORMAT_SPECIFIER`、`TC_CE_FORMAT_TYPE_MISMATCH`、`TC_CE_OPERAND_COUNT`，及对应的运行时错误码和缩进/控制流专用码。
- **函数诊断**（§11.4.2）：`TC_CE_DUPLICATE_FUNCTION`、`TC_CE_FUNCTION_NAME_CONFLICT`、`TC_CE_UNDEFINED_FUNCTION`、`TC_CE_DUPLICATE_PARAMETER`、`TC_CE_MISSING_ARGUMENT`、`TC_CE_DUPLICATE_ARGUMENT`、`TC_CE_UNKNOWN_ARGUMENT`、`TC_CE_ARGUMENT_ORDER`、`TC_CE_ARGUMENT_TYPE`、`TC_CE_FUNCALL_POSITION`、`TC_CE_FUNCALL_RESULT_TYPE`、`TC_CE_RETURN_OUTSIDE_FUNCTION`、`TC_CE_RETURN_FORM`、`TC_CE_RETURN_TYPE`、`TC_CE_MISSING_RETURN`、`TC_CE_UNREACHABLE_STATEMENT`、`TC_CE_PARAMETER_ASSIGNMENT`、`TC_CE_FUNCTION_SCOPE_ACCESS`、`TC_CE_CROSS_CONTROL_FLOW_JUMP`、`TC_CE_RECURSION`。
- **memblock 诊断**（§11.4.3）：`TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE` / `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`、`TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH`、`TC_CE_MEMBLOCK_SIZE_MISMATCH`。
- **结构体诊断**（§11.4.4）：`TC_CE_STRUCT_MISSING_FIELD`、`TC_CE_STRUCT_UNKNOWN_FIELD`、`TC_CE_STRUCT_DUPLICATE_FIELD`、`TC_CE_STRUCT_FIELD_ORDER`、`TC_CE_STRUCT_IMMUTABLE_FIELD`、`TC_CE_DUPLICATE_STRUCT`、`TC_CE_UNDEFINED_STRUCT`。
- **模块诊断**（§11.4.5）：`TC_CE_MODULE_LAYER`、`TC_CE_MISSING_VISIBILITY`、`TC_CE_PROGRAM_MODE_MISUSE`、`TC_CE_IMPORT_NOT_FOUND`、`TC_CE_IMPORT_NOT_LIB`、`TC_CE_IMPORT_AMBIGUOUS`、`TC_CE_DUPLICATE_IMPORT`、`TC_CE_IMPORT_NAME_CONFLICT`、`TC_CE_CIRCULAR_IMPORT`、`TC_CE_PRIVATE_MEMBER_ACCESS`。
- **指针与 memcopy 诊断**（§11.4.6）：`TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE` / `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`、`TC_RE_NULL_POINTER_DEREFERENCE`、`TC_RE_NULL_POINTER_ARITHMETIC`。

### 15.3 诊断域

```c
typedef enum {
    TC_DIAG_LANGUAGE,            /* 语言规范错误（TC_CE_*/TC_RE_*） */
    TC_DIAG_API,                 /* API/环境错误（文件名、选项冲突等） */
    TC_DIAG_IMPLEMENTATION       /* 实现错误（OOM、内部断言） */
} TcDiagnosticDomain;
```

### 15.4 阶段

| 阶段 | 代表错误 |
| ---- | -------- |
| Lexer | Syntax、缩进、字面量上限 |
| Parser | 语法、MissingEnd、VarMissingInitializer、ModuleLayer、OperandCount |
| 模块解析 | ImportNotFound、CircularImport、PrivateMemberAccess |
| 函数签名 | DuplicateFunction、FunctionNameConflict |
| Binder/Type | UndefinedVariable、TypeMismatch、ModeMismatch、memblock/struct/ptr 专用诊断 |
| Control/CFG | label/goto/loop context、UninitializedVariable、MissingReturn |
| Const eval | ConstantExpression/Overflow/DivisionByZero/CastOverflow |
| 调用图 | Recursion |
| Runtime | DivisionByZero、Integer/Float errors、CastOverflow、NullPointer*、IOError 等 |
| 任意实现阶段 | OutOfMemory |

TC 没有编译警告。

---

## 16. 模块与接口

### 16.1 模块组织

| 目录 | 责任 |
| ---- | ---- |
| `src/vm/lexer/` | Token 与词法位置、缩进栈 |
| `src/vm/parser/` | 语句/RHS/类型/函数/模块解析、递归释放 |
| `src/vm/analyzer/` | 模块解析、作用域/绑定、类型检查、CFG、DFA、常量求值、调用图 |
| `src/vm/executor/` | typed program 执行、调用帧管理 |
| `src/vm/runtime/` | 类型、诊断、符号、整数/浮点/位运算/转换语义、I/O、stmt_index |
| `src/vm/driver/` | CLI、文件模式、多文件编译、版本 |
| `src/libtc/` | 嵌入式编译/执行入口 |

### 16.2 接口边界

- Parser 不执行全局类型或可达性判断；
- CFG 不重新解析源码；
- Dataflow 不通过源序猜测 goto；
- 模块解析在 Parser 与类型检查之间独立执行；
- Executor/AOT 不各自重做不同版本的静态合法性；
- 数值和 I/O 语义继续从 runtime 共享。

### 16.3 公共接口

```c
/* 多文件编译 */
int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag);
int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out, TcDiagnostic *diag);

/* 编译 + 执行 */
int tc_run_file(const char *path, TcDiagnostic *diag);
int tc_run_program(const TcTypedProgram *program, TcDiagnostic *diag);

/* 模块搜索路径 */
void tc_set_module_search_paths(const char **paths, int count);
```

---

## 17. 验证与交付门槛

### 17.1 测试分层

| 层 | 0.0.35 必测内容 |
| -- | -------------- |
| Lexer | 所有新关键字、`nullptr`、特殊浮点 Token、`@padding` |
| Parser | 模块头、`import`、函数定义、`funcall`、`return`、struct 定义、`memblock<T, N>`、`ptr<T>`、`static` 声明 |
| 模块系统 | 单文件结构、导入解析、依赖环、可见性检查、`Self` 解析 |
| 函数系统 | 签名收集、funcall 检查、return 检查、调用图环检查 |
| 类型系统 | memblock `N` 比较、ptr 同型约束、struct 字段验证、类型等价 |
| Analyzer | 多域作用域、本库成员索引、13 阶段顺序、范式隔离 |
| CFG/DFA | 多 CFG 域、函数入口/返回边、确定初始化固定点 |
| Semantics | 所有新 ptr_*/memblock_* 指令、struct 操作、深拷贝语义 |
| Executor | 函数调用帧、return 传播、多文件 `static var` 拓扑初始化 |
| AOT | 与 VM 正例输出、退出码、运行时错误和位模式差分 |
| libtc | 多文件 API、失败不修改 out、所有权、诊断阶段 |
| 错误码 | 所有编译器标准 §11.4 列出的错误码均有测试覆盖 |

### 17.2 关键路径用例

- DAG 模块依赖、循环导入拒绝；
- 函数签名与重名冲突、funcall 实参顺序/类型检查；
- return 形式匹配、`void` 返回；
- memblock 深拷贝、`N` 不匹配拒绝、下标越界；
- struct 值构造器全字段验证、嵌套字段访问、双层可变性检查；
- `ptr_address` 对 `let` 拒绝、`ptr_store` 对只读绑定拒绝；
- `nullptr` 定型、空指针解引用/算术错误分类；
- `static var` 按依赖拓扑序初始化、跨模块共享；
- `cast(T, ptr<U>)` 等宽指针转换；
- float32/float64 每步舍入、strict/ieee 模式、`ieee` 模式 NaN 传播。

---

## 18. 实现基线与迁移

### 18.1 v0.0.31 → v0.0.35 关键迁移

| 类别 | v0.0.31 | v0.0.35 |
| ---- | ------- | ------- |
| 模块系统 | 单文件 | `#program`/`#lib`、`import`、`public`/`private`、`Self` |
| 函数 | 无 | `func`/`funcall`/`return`、无环调用图 |
| 类型 | `int8`~`uint64`、`float32`/`float64`、`bool` | 增加 `ptr<T>`、`memblock<T, N>`、`struct`、`isize`/`usize`、`void` |
| 运算 | 算术、位、比较、逻辑、cast/bitcast | 增加全部 `ptr_*`/`memblock_*`/`memcopy_unsafe` 指令 |
| 编译管线 | 6 阶段 | 13 确定性阶段 |
| 错误码 | 41+1 | 扩展至含函数、模块、memblock、struct、ptr 专用诊断 |
| REPL | 包含 | **已删除** |

### 18.2 预计迁移顺序

1. types/IR 与错误枚举更新；
2. Lexer 新关键字与 Token；
3. Parser 模块头、函数、类型定义；
4. 模块系统（加载、导入、依赖图）；
5. 作用域、函数级作用域、本库成员索引；
6. 类型系统（memblock / ptr / struct 验证）；
7. 函数签名收集与调用检查；
8. CFG 多域与确定初始化；
9. 常量求值与 `static var` 初始化；
10. Executor（调用帧、memblock 堆存储、ptr 指令）；
11. AOT；
12. CLI/API 与全量验证。

---

*本文的规范性语言规则均以 [TC 语言标准 0.0.35](./TC语言标准设计说明书-0.0.35.md) 与 [TC 编译器标准 0.0.35](./TC编译器标准设计说明书-0.0.35.md) 为准。*
