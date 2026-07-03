# TC-VM 详细设计说明书（完整版）

> **版本**：0.0.18（草案）  
> **作者**：唐荣兵（yanhuang8923@qq.com）  
> **依赖**：[TC语言标准设计说明书.md](./TC语言标准设计说明书.md) v0.0.18  
> **工程**：[TC-Compiler](../README.md) 之 `src/vm/` 组件  
> **定位**：TC 源码即高级字节码；**不** lowering 为第二套字节码，经静态分析后直接执行

---

## 目录

1. [设计目标与原则](#1-设计目标与原则)
2. [总体架构](#2-总体架构)
3. [TC 作为可执行指令文本](#3-tc-作为可执行指令文本)
4. [源文件与行模型](#4-源文件与行模型)
5. [内部表示：TcStatement](#5-内部表示tcstatement)
6. [符号表与变量槽位](#6-符号表与变量槽位)
7. [静态分析阶段（Analyzer）](#7-静态分析阶段analyzer)
8. [执行阶段（Executor）](#8-执行阶段executor)
9. [运行时值表示](#9-运行时值表示)
10. [内建指令语义实现](#10-内建指令语义实现)
11. [错误处理与诊断](#11-错误处理与诊断)
12. [模块划分与接口](#12-模块划分与接口)
13. [执行流程示例](#13-执行流程示例)
14. [测试策略](#14-测试策略)
15. [与语言标准的边界](#15-与语言标准的边界)
16. [后续扩展预留](#16-后续扩展预留)
17. [I/O 语句实现](#17-io-语句实现)
18. [交互式 REPL 实现](#18-交互式-repl-实现)

附录

- [附录 A：TcStatement 与源语句对照](#附录-atcstatement-与源语句对照)
- [附录 B：Fetch–Decode–Execute 对照](#附录-bfetchdecodeexecute-对照)
- [附录 C：文档修订记录](#附录-c文档修订记录)

---

## 1. 设计目标与原则

### 1.1 目标

TC-VM 是 TC 语言的**直接执行引擎**：用户编写的 `.tc` 源文件中的每条语句，经词法、语法、静态分析后，由执行引擎**逐条 dispatch**，无需转译为另一套 opcode 语言或持久化字节码文件。

### 1.2 核心原则

| 原则           | 说明                                                         |
| -------------- | ------------------------------------------------------------ |
| 源码即程序     | TC 文本是权威程序表示；不存在独立的 `.tcbc` 等第二语义层     |
| 语句同构       | 内部 `TcStatement` 与源语句 **1:1 对应**，仅便于 dispatch，不引入新语义 |
| 先检后跑       | 全程序静态分析通过后，再进入顺序执行                         |
| 语义归语言标准 | 算术、cast、I/O、常量、未初始化变量等行为以《TC 语言标准设计说明书》为准；本文档只规定**实现架构** |
| 单作用域       | 与语言标准 §3.6 全局单作用域一致                             |
| 可观测即标准   | 对外可观测行为（结果值、错误类型、终止时机）必须与语言标准一致 |

### 1.3 实现版本

可执行文件 `tc-vm` 与本文档同为 **v0.0.18**（`src/vm/driver/tc_version.h` 中 `TC_VM_VERSION`；`tc-vm --version` 可查看）。

### 1.4 非目标（v0.0.18）

- 不定义、不生成第二套字节码指令集
- 不做 JIT / LLVM 后端
- 不实现语言标准 §5.4 扩展表中预留的浮点、布尔、位运算、比较、控制流、函数、复合类型等扩展（仅预留接口）
- `let` 常量 RHS 仅支持字面量，不支持 `add`/`sub` 等编译时常量表达式

---

## 2. 总体架构

### 2.1 流水线

```text
┌─────────┐   ┌─────────┐   ┌──────────┐   ┌──────────┐
│  Lexer  │ → │ Parser  │ → │ Analyzer │ → │ Executor │
└─────────┘   └─────────┘   └──────────┘   └──────────┘
     ↑              ↑              ↑              ↑
  Token 流      AST/Stmt      符号表+        变量槽位
               初步结构      类型检查       顺序执行
```

**无 `Codegen` 模块**：Parser/Analyzer 产出 `TcStatement` 列表，Executor 直接消费。

### 2.2 组件职责

| 组件         | 输入               | 输出                          | 职责                                                         |
| ------------ | ------------------ | ----------------------------- | ------------------------------------------------------------ |
| **Lexer**    | 源文本             | `Token` 流                    | 按语言标准 §2 词法规则切分；记录行号、列号                   |
| **Parser**   | `Token` 流         | `TcStatement`（未完全类型化） | 按语言标准附录 A 语法解析单条语句                            |
| **Analyzer** | `TcStatement` 列表 | 已类型化程序 + 符号表         | Pass 1 符号收集、Pass 2 源序可见性类型检查、常量编译期求值、未初始化警告 |
| **Executor** | 已类型化程序       | 执行结果或运行时错误          | 按 PC 顺序 dispatch 内建语义                                 |

### 2.3 对外入口

```text
run(source: string) -> Result<void, Diagnostic>
run_file(path: string) -> Result<void, Diagnostic>
```

语义：

1. 读入源文本
2. 按行切分并逐行 Lex + Parse（或先切行再 parse）
3. Analyzer 全程序检查
4. 通过则 Executor 从 PC=0 执行至程序结束
5. 任一步失败则返回 `Diagnostic`，**不执行**（静态错误）或**终止程序**（运行时错误）

---

## 3. TC 作为可执行指令文本

### 3.1 语句即指令

TC v0.0.18 中，每条合法语句对应一条「高级指令」：

| 源形式                              | 指令含义                                                     |
| ----------------------------------- | ------------------------------------------------------------ |
| `var x: T = <rhs>`                  | **DEF** — 定义槽位 `x` 为类型 `T` 并初始化                   |
| `var x: T`                          | **DEF** — 定义槽位 `x` 为类型 `T`，**未初始化**              |
| `let x: T = <literal>`              | **DEF_CONST** — 定义编译期常量槽位 `x`，不可赋值（RHS 仅限字面量） |
| `x = <rhs>`                         | **MOV** — 将 `<rhs>` 结果写入已定义槽位 `x`                  |
| `<rhs> = integer_literal`           | 加载字面量                                                   |
| `<rhs> = add/sub/mul/div/mod(T, …)` | 双目算术指令                                                 |
| `<rhs> = abs/neg(T, …)`             | 单目算术指令                                                 |
| `<rhs> = cast(T, …)`                | **CAST** — 目标类型 `T`，模式 strict/`truncate`              |
| `write/writeln(T, …)`               | **WRITE** — 标准输出                                         |
| `read(T, x)`                        | **READ** — 标准输入                                          |

`<rhs>` 仅四选一（字面量 / 双目算术 / 单目算术 / cast），**无嵌套**，故一条语句的语义完全由该行决定。

### 3.2 执行模型

采用与经典 VM 同构的 **Fetch–Decode–Execute**，但 Fetch 的对象是 **TC 语句**（或其 `TcStatement`），而非二进制 opcode：

```text
PC ← 0
while PC < program.len:
    stmt ← program[PC]
    decode(stmt)        // 已在前端完成，运行时仅 match 枚举
    execute(stmt)       // 调用内建语义
    PC ← PC + 1
// 正常结束：PC 到达程序末尾
```

### 3.3 与「字节码 VM」的区分

| 维度      | 传统字节码 VM          | TC-VM（本设计）                                |
| --------- | ---------------------- | ---------------------------------------------- |
| 程序表示  | 源语言 + 独立 bytecode | **仅 TC 源文本**                               |
| 持久化 IR | `.class` / `.bc` 等    | 可选内存缓存 `TcStatement`，**无独立文件格式** |
| 指令集    | 与源语言异构           | **与 TC 语法同构**                             |
| 反汇编    | 需要                   | **源文件即反汇编结果**                         |

---

## 4. 源文件与行模型

语言标准 EBNF 对空行、文件尾换行未完全规定；**TC-VM v0.0.18 实现约定**如下（不影响 TC 语义，仅规范实现）：

| 规则                | 约定                                                         |
| ------------------- | ------------------------------------------------------------ |
| 行结束符            | 支持 `\n`、`\r\n`、`\r`（与 EBNF `newline` 一致）            |
| 空行                | **忽略**（不含 statement，不占用 PC）                        |
| 仅空白行            | **忽略**                                                     |
| 仅注释行            | **忽略**（整行以 `;` 开始，或仅含 `ws` + `line_comment`）    |
| 文件末尾换行        | **可选**；最后一条 statement 后有无 newline 均可             |
| 一条 statement 一行 | **强制**；同一行内不得有多条 statement                       |
| PC 编号             | 对**非忽略行**中的 statement 从 0 递增；`Diagnostic` 使用 **源文件行号**（1-based） |

行处理伪代码：

```text
lines ← split_source(source)
program ← empty
for line_no, line in lines:
    if is_skippable(line): continue
    stmt ← parse_line(line, line_no)
    program.push(stmt)
```

---

## 5. 内部表示：TcStatement

源码中的实际类型名为 `TcStatement`（定义于 `runtime/tc_types.h`）。本节为设计文档中的类型定义，与源码 1:1 对应。

### 5.1 设计文档与 C 类型对照

| 设计文档名称   | C 类型 / 枚举                                               | 头文件       |
| -------------- | ----------------------------------------------------------- | ------------ |
| `TcStatement`  | `TcStatement` + `TcStmtKind`                                | `tc_types.h` |
| `VarDef`       | `TcVarDef`（`has_rhs` 标志可选 RHS）                        | `tc_types.h` |
| `ConstDef`     | `TcConstDef`                                                | `tc_types.h` |
| `Assign`       | `TcAssign`                                                  | `tc_types.h` |
| `Rhs`          | `TcRhs` + `TcRhsKind`                                       | `tc_types.h` |
| `Operand`      | `TcOperand` + `TcOperandKind`                               | `tc_types.h` |
| `Literal`      | `TcLiteral`（`magnitude`/`negative`/`unsigned_suffix`）     | `tc_types.h` |
| `WrapMode`     | `TcWrapMode`（`TC_ARITH_STRICT` / `TC_ARITH_WRAP`）         | `tc_types.h` |
| `TruncateMode` | `TcTruncateMode`（`TC_TRUNC_STRICT` / `TC_TRUNC_TRUNCATE`） | `tc_types.h` |
| `TypedProgram` | `TcTypedProgram`（`program` + `symbols` + `warnings`）      | `tc_types.h` |
| `SymbolEntry`  | `TcSymbol`（含 `def_stmt_index`、`has_const_value`）        | `tc_types.h` |
| `SlotStore`    | `TcValue[]`，按 `TcSymbol.slot` 索引                        | `tc_types.h` |
| `FormatSpec`   | `TcFormatSpec`（7 种格式化符号）                            | `tc_types.h` |
| `UnaryOp`      | `TcUnaryOp`（`TC_UNARY_ABS` / `TC_UNARY_NEG`）              | `tc_types.h` |
| `ErrorKind`    | `TcErrorKind`（含 21 种错误类型）                           | `tc_types.h` |
| `WarningKind`  | `TcWarningKind`                                             | `tc_types.h` |

### 5.2 类型枚举

```text
IntType ::= int8 | uint8 | int16 | uint16 | int32 | uint32 | int64 | uint64

WrapMode ::= Strict | Wrap       // 关键字 wrap → TC_ARITH_WRAP；仅 add/sub/mul/neg 可用
TruncateMode ::= StrictTrunc | Truncate   // 关键字 truncate → TC_TRUNC_TRUNCATE；仅 cast 可用

ArithOp ::= Add | Sub | Mul | Div | Mod

UnaryOp ::= Abs | Neg              // 单目运算

FormatSpec ::= D | I | U | X | XU | O | B   // %d, %i, %u, %x, %X, %o, %b
```

### 5.3 字面量（TcLiteral）

```text
TcLiteral ::= {
    magnitude: uint64,      // 数值绝对值
    negative: bool,         // 是否有负号
    unsigned_suffix: bool,  // 是否有 u/U 后缀
    base: int               // 10 | 16 | 8 | 2
}
```

### 5.4 操作数（TcOperand）

```text
TcOperand ::= Variable(name: string)
            | Literal(lit: TcLiteral)
```

解析阶段 `Literal` 可存原始十进制字符串，Analyzer 阶段按上下文类型校验范围。

### 5.5 右值（TcRhs）

```text
TcRhs ::= LitRhs { value: uint64 }
        | ArithRhs { op: ArithOp, type: IntType, mode: WrapMode, lhs: Operand, rhs: Operand }
        | UnaryRhs { op: UnaryOp, type: IntType, mode: WrapMode, operand: Operand }
        | CastRhs { target: IntType, mode: TruncateMode, source: string }

ConstRhs ::= LitRhs { value: uint64 }   /* 常量 RHS 仅允许字面量 */
```

> **约束**：
> - `ArithRhs` 中 `div`/`mod` 不支持 `Wrap` 模式
> - `UnaryRhs` 中 `abs` 不支持 `Wrap` 模式
> - `CastRhs.source` 必须为已定义变量，不可为字面量或嵌套表达式

### 5.6 语句（TcStatement）

```text
TcStatement ::=
    | VarDef { line, name, type: IntType, rhs: TcRhs? }     /* rhs 可选：无 rhs 表示未初始化 */
    | ConstDef { line, name, type: IntType, rhs: ConstRhs }  /* let 常量定义，rhs 必填 */
    | Assign { line, name, rhs: TcRhs }
    | Write { line, type: IntType, fmt: FormatSpec?, operand: Operand }
    | Writeln { line, type: IntType, fmt: FormatSpec?, operand: Operand }
    | Read { line, type: IntType, name: string }
```

> **说明**：
> - `Write`/`Writeln` 中 `fmt` 为 `None` 时表示无格式输出
> - `let` 常量值在编译期已确定，不生成运行时赋值指令；常量**不可**作为 `Assign` 的左值

### 5.7 已类型化程序（TcTypedProgram）

```text
TcTypedProgram ::= {
    statements: TcStatement[],
    symbol_table: TcSymbolTable   // Analyzer 产出
}
```

**说明**：上述结构是内存中的解析/执行缓存，**不是** TC 语言的一部分，也不作为对外交换格式。

---

## 6. 符号表与变量槽位

### 6.1 符号表（编译期）

Analyzer 维护：

```text
TcSymbolTable ::= Map<identifier, TcSymbol>

TcSymbol ::= {
    kind: SymKind,          // TC_SYM_VARIABLE | TC_SYM_CONSTANT
    type: IntType,
    slot: SlotIndex,        // 与 Executor 槽位一一对应
    def_line: int,
    def_stmt_index: int,    // 定义语句在 program 中的下标（源序可见性）
    initialized: bool,      // Variable 定义时是否有 RHS
    has_const_value: bool,
    const_value: TcValue,   // Constant 的编译期确定值（Pass 2 填入）
    name: string
}
```

符号表 CRUD 由独立模块 `runtime/symbol.c`（`tc_symbol.h`）实现：`tc_symbol_table_init/free/find/add/pop_last`。

规则：

- 仅在 `VarDef` / `ConstDef` 时插入；重复定义同名（含变量与常量间冲突）→ **重复定义错误**
- `Assign` / 操作数 / cast 源 引用的 name 必须存在 → 否则 **未定义标识符错误**
- `ConstDef` 的 `kind = Constant`；`Assign` 左侧若为常量 → **常量赋值错误**
- `initialized` 标记用于检测未初始化变量读取 → **未初始化变量警告**
- `const_value` 在 Pass 2 中解析字面量后填入，Executor 直接使用

### 6.2 变量槽位（运行期）

Executor 维护：

```text
SlotStore ::= Vec<TcValue>   // 下标即 SlotIndex

TcValue ::= { type: IntType, bits: uint64 }
```

- 所有运算在 `bits` 上进行，按 `type` 解释符号性与范围
- 每种 `IntType` 统一用 64 位容器存储位模式；**窄类型运算前不隐式扩展语义**，仅在内建函数内按类型位宽处理

### 6.3 槽位分配

按 `VarDef` / `ConstDef` 在程序中出现的顺序分配 `slot`：0, 1, 2, …  
Analyzer 第一遍扫描所有 `VarDef` / `ConstDef` 建立符号表；第二遍做类型检查。

常量与变量共用槽位编号空间。常量值在编译期确定，Executor 在对应槽位中预先填入常量值。

---

## 7. 静态分析阶段（Analyzer）

### 7.1 阶段划分

```text
analyze(program):
    pass1_collect_symbols(program)   // 仅 VarDef + ConstDef
    pass2_type_check(program)          // 全语句 + 常量值解析
    return TypedProgram
```

任一步失败 → 返回 `Diagnostic`，Executor **不得**运行。

### 7.2 Pass 1：符号收集

对每条 `VarDef` / `ConstDef`（`tc_pass1_collect_symbols`）：

- 若 `name` 已存在 → `TC_ERR_DUPLICATE_DEFINITION`
- 否则按出现顺序分配 `slot`（0, 1, 2, …），记录 `type`、`sym_kind`、`initialized`、`def_line`、`def_stmt_index`
- `ConstDef` 的值留待 Pass 2 解析

### 7.3 Pass 2：源序可见性（`visible` 符号表）

Pass 2（`tc_pass2_type_check`）维护增量符号表 `visible`：按程序顺序遍历语句，**仅在当前语句检查通过后**才将本条 `VarDef`/`ConstDef` 插入 `visible`。因此：

- `Assign`、操作数、`cast` 源只能引用 **已出现在 `visible` 中** 的标识符
- 前向引用 → `TC_ERR_UNDEFINED_VARIABLE`
- `VarDef` RHS 引用自身 → `TC_ERR_UNDEFINED_VARIABLE`（消息：`cannot reference itself in its initializer`）

全局符号表 `symbols`（Pass 1 产出）用于槽位分配、`def_stmt_index` 与常量 `const_value` 存储；`visible` 仅用于源序可见性检查。

### 7.4 Pass 2：类型检查与常量值解析

#### 7.4.1 常量值解析

对每条 `ConstDef`：

1. 从 `ConstRhs` 中提取字面量数值
2. 按目标类型检查字面量范围（参见语言标准 §3.4）
3. 若范围检查通过，将字面量编码为 `TcValue` 存入 `TcSymbol.const_value`
4. 若字面量超出范围 → **字面量范围错误**
5. 检查无符号字面量（`u`/`U` 后缀）是否用于有符号类型 → **字面量类型错误**

常量值在编译期即已确定，Executor 执行到 `ConstDef` 时直接从符号表读取 `const_value` 写入槽位，不重新解析字面量。

#### 7.4.2 类型检查表

对每条语句检查下表（与语言标准 §4、§5、§6 对齐）：

| 检查项            | 规则                                                         |
| ----------------- | ------------------------------------------------------------ |
| Assign 目标       | 必须已定义；若为 `let` 常量 → **常量赋值错误**               |
| 左值类型 vs rhs   | `VarDef` / `ConstDef` / `Assign` 左侧类型必须与 rhs 结果类型一致 |
| const_rhs 约束    | `ConstDef` 的 rhs 必须为 `LitRhs`（仅字面量）→ 否则 **常量表达式错误** |
| 算术 `type` 参数  | 必须与 lhs、rhs 操作数类型一致                               |
| 操作数同型        | 两 `Operand` 解析后类型相同                                  |
| 操作数 Variable   | 已定义且类型匹配；若为未初始化变量 → **未初始化变量警告**（见 §7.4.4） |
| Literal 范围      | 落在上下文 `IntType` 可表示范围内                            |
| 字面量类型错误    | `u`/`U` 后缀字面量用于有符号类型上下文；或负号与 `u`/`U` 后缀组合 |
| `wrap` 合法性     | `div`/`mod`/`abs` 使用 `Wrap` 模式 → **溢出模式错误**        |
| `truncate` 合法性 | 算术运算（`add`/`sub`/`mul`/`div`/`mod`/`abs`/`neg`）使用 `Truncate` 模式 → **关键字错误** |
| `wrap` 用于 cast  | `CastRhs` 中使用 `Wrap` 模式 → **关键字错误**                |
| cast 源           | 必须为已定义变量；`CastRhs.source` 仅变量名，非字面量        |
| cast 目标         | `VarDef`/`Assign` 左侧类型 == `CastRhs.target`               |
| 格式化符号兼容性  | `%d`/`%i` 要求有符号类型；`%u` 要求无符号类型；其他任意      |
| 格式化操作数数量  | 格式化版本必须恰好一个操作数；无格式版本也恰好一个操作数     |
| 格式字符串        | 必须是 `%d`/`%i`/`%u`/`%x`/`%X`/`%o`/`%b` 之一               |

#### 7.4.3 未初始化变量检查算法

实现采用 `last_init_stmt_index[slot]` 缓存（Pass 2 预扫描 `Assign`/`Read` 语句填充；REPL 在 `TcReplAnalyzeCtx` 中增量维护）。

对程序中每个变量读取点（`VarDef` RHS、`Assign` RHS、`Write`/`Writeln` operand、`ArithRhs` operand、`UnaryRhs` operand）：

1. 若符号为 `TC_SYM_CONSTANT` → 跳过
2. 若定义时 `initialized == 1`（`VarDef` 有 RHS）→ 跳过
3. 若 `last_init_stmt_index[slot]` 有效且位于 `(def_stmt_index, 当前语句下标)` 之间 → 视为已初始化，跳过
4. 否则产生 `TC_WARN_UNINITIALIZED_VARIABLE`，消息格式：`use of possibly uninitialized variable '<name>'`

**边界情况**：

| 场景                         | 行为                                               |
| ---------------------------- | -------------------------------------------------- |
| `VarDef` RHS 引用自身        | **静态错误** `TC_ERR_UNDEFINED_VARIABLE`（非警告） |
| 前向引用未定义变量           | **静态错误** `TC_ERR_UNDEFINED_VARIABLE`           |
| `read(type, var)` 之后的读取 | `read` 计入初始化，不再警告                        |
| 常量（`let`）                | 不参与未初始化检查                                 |

### 7.5 rhs 结果类型推断

| Rhs 形式   | 结果类型           |
| ---------- | ------------------ |
| `LitRhs`   | 由左侧变量类型决定 |
| `ArithRhs` | `ArithRhs.type`    |
| `UnaryRhs` | `UnaryRhs.type`    |
| `CastRhs`  | `CastRhs.target`   |

### 7.6 静态可判定性（v0.0.18）

以下错误在 v0.0.18 **均可静态检查**，Analyzer 应尽可能在运行前报出：

- 未定义标识符、重复定义、类型错误、字面量范围错误、字面量类型错误、溢出模式错误、关键字错误、常量赋值错误、常量表达式错误、格式字符串错误、格式类型不匹配、操作数数量错误

以下在 v0.0.18 **依赖运行期操作数值**，Analyzer **无法**完全静态判定：

- 除零错误
- 有符号 strict 算术溢出（`add`/`sub`/`mul`/`neg`/`abs`）
- strict cast 不可表示

---

## 8. 执行阶段（Executor）

### 8.1 初始化

```text
execute(program: TypedProgram):
    slots ← Vec::new(size = symbol_table.len)
    // 初始化时用毒化填充 0xFE
    memset(slots, 0xFE, sizeof(TcValue) * symbol_table.len)
    
    for stmt in program.statements:
        match stmt:
            VarDef with rhs → eval_rhs → slots[slot] ← value
            VarDef without rhs → 跳过（槽位保持毒化填充）
            ConstDef → slots[slot] ← symbol_table[slot].const_value
            Assign → eval_rhs → slots[slot] ← value
            Write/Writeln → 格式化输出 operand 值
            Read → 从 stdin 读取并写入 slots[slot]
    // 正常结束：无 HALT 指令，执行完最后一条即结束
```

**注意**：
- `VarDef` 在运行时再次执行初始化（与语义「定义并赋值」一致）
- `VarDef` 无 RHS 时槽位保持 **毒化填充**（实现用 `0xFE` 模式 `memset`，值视为未定义）
- `ConstDef` 的值在编译期已确定并存入 `TcSymbol.const_value`，Executor 直接从符号表读取并写入槽位
- 分析通过后、执行前，`driver.c` 将 `TcTypedProgram.warnings` 打印到 stderr（`warning: <msg> (line N)`），**不阻止执行**

### 8.2 eval_rhs

```text
eval_rhs(rhs, expected_type, slots, symbols):
    LitRhs(v)          → value(type=expected_type, bits=v)
    ArithRhs(op, ...)  → exec_arith(op, type, mode, lhs, rhs, slots, symbols)
    UnaryRhs(op, ...)  → exec_unary(op, type, mode, operand, slots, symbols)
    CastRhs(...)       → exec_cast(target, mode, source, slots, symbols)
```

### 8.3 操作数求值

```text
eval_operand(Operand, expected_type, ...):
    Variable(name) → slots[symbols[name].slot]  // 类型已在 Analyzer 验证
    Literal(v)     → value(expected_type, v)
```

求值顺序（语言标准 §4.6）：

- 双目算术：先 `lhs`，再 `rhs`，再运算
- 单目算术：求值 `operand`，再运算
- `wrap`/`truncate` 为模式关键字，不参与运行时求值

### 8.4 运行时错误

发生以下情况时，Executor **立即终止**，返回 `Diagnostic`（含源行号）：

| 错误         | 条件                                                       |
| ------------ | ---------------------------------------------------------- |
| 除零错误     | `div`/`mod` 右操作数 `bits == 0`                           |
| 整数溢出错误 | 有符号 + strict + `add`/`sub`/`mul`/`neg`/`abs` 结果超范围 |
| 转换溢出错误 | strict `cast` 不可表示                                     |
| I/O 错误     | `read` 读取失败（非法输入/超范围/EOF）                     |

无符号算术 **不** 报整数溢出错误（模 2^n 回绕）。

---

## 9. 运行时值表示

### 9.1 统一容器

```text
TcValue { type: IntType, bits: uint64 }
```

- 存储 **位模式**（bit pattern），非数学抽象整数
- 解释时按 `type` 的位宽 `n` 取 `bits mod 2^n`（有符号则按二补码解释）

### 9.2 字面量解析

- 词法为十进制非负整数（含可选的 `-` 前缀）
- 解析为 `uint64`；若超过 `uint64` 最大值 → **字面量范围错误**（静态）
- 带负号的无后缀字面量解析规则（对应语言标准 §2.3.5 的 `INT64_MIN` 边界情况）：
  1. 将数字部分（不含负号）解析为 `uint64` 值 `N`
  2. 要求 `0 ≤ N ≤ UINT64_MAX`
  3. 取 `-N` 作为有符号整数值
  4. `N = 9223372036854775808` 时，`-N = INT64_MIN`，为合法边界值
- 与上下文类型结合时，按语言标准 §3.4 检查上界

### 9.3 中间计算

| 场景                   | 实现要求（`runtime/semantics.c`）                          |
| ---------------------- | ---------------------------------------------------------- |
| 有符号 strict 加减     | `tc_sadd_overflow` / `tc_ssub_overflow` 在 int64 域检测    |
| 有符号 strict 乘法     | 宽整数路径；`uint64` 用 `tc_umul64`（32 位分块 64×64→128） |
| 有符号 strict 取负     | 检测操作数是否为 `INT_MIN`                                 |
| 有符号 strict 取绝对值 | 检测操作数是否为 `INT_MIN`                                 |
| 有符号 strict 除法     | 向零截断；`INT_MIN / -1` 商不可表示 → 整数溢出错误         |
| 有符号 strict 取余     | `INT_MIN % -1` 余数为 0；其余按数学定义，结果须落在类型范围内 |
| 无符号运算             | 位宽掩码 `tc_mask_bits`；64 位乘法经 `tc_umul64`           |
| cast                   | `tc_cast_strict` / `tc_cast_truncate` 在 `bits` 上操作     |

---

## 10. 内建指令语义实现

内建函数是 TC 语义的**唯一实现入口**；行为必须与《TC 语言标准设计说明书》§5、§6 一致。

### 10.1 函数清单

```text
exec_arith(op, type, mode, lhs, rhs) -> TcValue | Error      // 双目算术
exec_unary(op, type, mode, operand) -> TcValue | Error       // 单目算术
exec_cast(target, mode, source_slot) -> TcValue | Error      // 类型转换
```

可按 `IntType` 分派到具体实现，或统一实现后按 `type` 参数化。

### 10.2 双目算术（exec_arith）

**有符号 `int8`～`int64`**

| op          | strict                        | wrap                          |
| ----------- | ----------------------------- | ----------------------------- |
| add/sub/mul | 结果超范围 → 整数溢出错误     | mod 2^n 回绕，位模式解释      |
| div         | 向零截断；商不可表示 → 整数溢出错误；除零 → 错误 | 同 strict（div 无 wrap 模式） |
| mod         | 余数符号同被除数；余数不可表示 → 整数溢出错误；除零 → 错误 | 同 strict                     |

**无符号 `uint8`～`uint64`**

| op          | 行为                   |
| ----------- | ---------------------- |
| add/sub/mul | 超范围 mod 2^n，不报错 |
| div/mod     | 数学定义；除零 → 错误  |

**wrap 模式与无符号**：`add(T, wrap, …)` 对有符号 T 启用回绕；对无符号 T 与三参数形式行为相同（无符号默认回绕）。

有符号 `wrap` 下的 `add`/`sub`/`mul` 与同位宽无符号默认回绕 **位模式一致**（语言标准 §5.1）。

**特殊情形**（`b ≠ 0`）：

| 表达式                   | 结果 | 说明                                                         |
| ------------------------ | ---- | ------------------------------------------------------------ |
| `div(T, INT_MIN(T), −1)` | —    | **整数溢出错误**（数学商 \(+2^{n-1}\) 不可在 `<T>` 中表示） |
| `mod(T, INT_MIN(T), −1)` | `0`  | 余数为 0，**不报错**                                         |

**实现备注**：
- `div` 在 `a == INT_MIN(T) && b == -1` 时直接报整数溢出错误，不调用 C `/`（`int64` 上为 UB）
- `mod` 在同上输入时直接返回 0，不调用 C `%`
- 其余 `div`/`mod` 在 int64 域计算后检查结果是否落在目标类型范围内
- 检测除零在运算符求值后立即进行

### 10.3 单目算术（exec_unary）

**有符号 `int8`～`int64`**

| op   | strict                   | wrap                     |
| ---- | ------------------------ | ------------------------ |
| abs  | `INT_MIN` → 整数溢出错误 | 不支持（静态错误）       |
| neg  | `INT_MIN` → 整数溢出错误 | mod 2^n 回绕，位模式解释 |

**无符号 `uint8`～`uint64`**

| op   | 行为                   |
| ---- | ---------------------- |
| abs  | 恒等返回（`abs` 恒等） |
| neg  | `0 - operand` mod 2^n  |

**实现伪代码**：

```c
int tc_exec_unary(TcUnaryOp op, TcIntType type, TcWrapMode mode,
                  const TcValue *operand, TcValue *out,
                  TcDiagnostic *diag, int line) {
    uint64_t bits = operand->bits & tc_type_mask(type);
    int n = tc_type_bit_width(type);
    uint64_t mask = (n == 64) ? UINT64_MAX : ((1ULL << n) - 1);
    
    switch (op) {
    case TC_UNARY_ABS:
        if (tc_type_is_signed(type)) {
            int64_t val = tc_bits_to_signed(bits, n);
            if (val == tc_type_min_signed(type)) {
                return tc_diag_set(diag, TC_ERR_INTEGER_OVERFLOW, line,
                                  "abs(INT_MIN) overflow");
            }
            if (val < 0) val = -val;
            out->bits = (uint64_t)val & mask;
        } else {
            out->bits = bits;
        }
        break;
        
    case TC_UNARY_NEG:
        if (tc_type_is_signed(type) && mode == TC_ARITH_STRICT) {
            int64_t val = tc_bits_to_signed(bits, n);
            if (val == tc_type_min_signed(type)) {
                return tc_diag_set(diag, TC_ERR_INTEGER_OVERFLOW, line,
                                  "neg(INT_MIN) overflow");
            }
            val = -val;
            out->bits = (uint64_t)val & mask;
        } else {
            // 有符号 wrap 或无符号：二补码取负
            out->bits = (bits == 0) ? 0 : (mask ^ bits) + 1;
        }
        break;
    }
    
    out->type = type;
    return 1;
}
```

### 10.4 类型转换（exec_cast）

分 `mode` 两支：

**StrictTrunc（strict）** — 按语言标准 §6.2 表：

| 源 → 目标       | strict 规则                         |
| --------------- | ----------------------------------- |
| 有符号 → 有符号 | 加宽：符号扩展；缩窄：范围检查      |
| 无符号 → 无符号 | 加宽：零扩展；缩窄：截断（不报错）  |
| 有符号 → 无符号 | 源 ≥ 0 且范围内 → 转换；否则 → 错误 |
| 无符号 → 有符号 | 源 ≤ 目标 max → 转换；否则 → 错误   |
| 同类型          | 恒等                                |

**Truncate** — 按语言标准 §6.3 位模式算法：

1. 源位宽 `n`，目标位宽 `m`，源位模式 `bits`
2. `m ≤ n`：`bits mod 2^m`，按目标类型解释
3. `m > n`：零扩展或符号扩展（见语言标准 §6.3 第 2 点）

> `cast` 中 `wrap` 关键字不可用，使用则触发静态 **关键字错误**。

### 10.5 与 C 语义对照

实现与测试时以语言标准附录 B 为参考：

- TC 有符号 strict 溢出 **必须报错**（C 为 UB）
- TC strict cast 不可表示 **必须报错**
- 无符号回绕与 C 一致
- TC `div(INT_MIN, -1)` 报整数溢出错误；`mod(INT_MIN, -1)` 为 0（C 为 UB）

---

## 11. 错误处理与诊断

### 11.1 错误分类

| 阶段         | 错误类型                               | 是否执行         |
| ------------ | -------------------------------------- | ---------------- |
| Lexer/Parser | 语法错误（非法 token、括号不匹配等）   | 否               |
| Analyzer     | 语言标准 §9.1 中可静态确定的错误       | 否               |
| Executor     | 除零、整数溢出、转换溢出、I/O 输入失败 | 否（已部分执行） |

语法错误不在语言标准 §9 枚举内，TC-VM 扩展为 **语法错误**。

### 11.2 Diagnostic 结构

```text
Diagnostic ::= {
    kind: ErrorKind,
    message: string,        // 人类可读
    line: int,              // 1-based 源行号
    column: int?,           // 可选
    snippet: string?        // 可选，出错行原文
}
```

### 11.3 ErrorKind 枚举（`TcErrorKind`）

与语言标准 §9.1 对齐（`runtime/tc_types.h`）：

```text
TcErrorKind ::=
    TC_ERR_SYNTAX                  // VM 扩展：词法/语法/OOM/文件 I/O
    TC_ERR_UNDEFINED_VARIABLE      // 未定义标识符错误
    TC_ERR_DUPLICATE_DEFINITION    // 重复定义错误
    TC_ERR_TYPE_MISMATCH           // 类型错误
    TC_ERR_LITERAL_OUT_OF_RANGE    // 字面量范围错误
    TC_ERR_LITERAL_TYPE            // 字面量类型错误
    TC_ERR_OVERFLOW_MODE           // 溢出模式错误（div/mod/abs 使用 wrap）
    TC_ERR_KEYWORD                 // 关键字错误（wrap/truncate 误用）
    TC_ERR_CONSTANT_ASSIGNMENT     // 常量赋值错误
    TC_ERR_CONSTANT_EXPRESSION     // 常量表达式错误
    TC_ERR_FORMAT_STRING           // 格式字符串错误
    TC_ERR_FORMAT_TYPE_MISMATCH    // 格式类型不匹配
    TC_ERR_OPERAND_COUNT           // 操作数数量错误
    TC_ERR_DIVISION_BY_ZERO        // 除零错误
    TC_ERR_INTEGER_OVERFLOW        // 整数溢出错误
    TC_ERR_CAST_OVERFLOW           // 转换溢出错误
    TC_ERR_IO                      // I/O 错误

TcWarningKind ::=
    TC_WARN_UNINITIALIZED_VARIABLE // 未初始化变量警告
```

### 11.4 诊断策略

- **静态错误**：报告第一条致命错误即停止；后续可扩展为 collect-all
- **编译警告**：报告所有可检测的警告（如未初始化变量读取），**不阻止执行**
- **运行时错误**：报告错误时 PC 对应当前 `TcStatement.line`
- 消息中应包含：错误/警告类型、行号、涉及变量名/类型（若适用）

---

## 12. 模块划分与接口

### 12.1 工程布局（TC-Compiler）

TC-VM 位于 [TC-Compiler](../README.md) 的 `src/vm/`，与 TC-AOT（`src/aot/`，预留）并列。构建由 CMake 统一管理；根目录 `Makefile` 为薄封装，VM 在 `CMakeLists.txt` 中定义 `tc-vm` 与 `check-vm` 目标。

```text
src/vm/
├── runtime/            # TcValue、TcIntType、TcDiagnostic、TcWarning、Semantics、Symbol
│   ├── tc_types.h      # 所有类型定义、枚举
│   ├── tc_types.c      # tc_type_*、tc_error_kind_name、tc_format_spec_name
│   ├── tc_diagnostic.c # tc_diagnostic_init/set/print
│   ├── tc_symbol.c     # tc_symbol_table_*（符号表 CRUD）
│   ├── tc_warning.c    # tc_warning_list_*
│   └── tc_semantics.c  # tc_exec_arith、tc_exec_unary、tc_exec_cast、字面量/位模式工具
├── lexer/              # tc_tokenize_line（含多进制字面量、u 后缀）
│   ├── tc_lexer.h
│   └── tc_lexer.c
├── parser/             # tc_parse_statement、tc_program_push
│   ├── tc_parser.h
│   └── tc_parser.c
├── analyzer/           # tc_analyze、tc_analyze_statement（两遍分析 + REPL 增量）
│   ├── tc_analyzer.h
│   └── tc_analyzer.c
├── executor/           # tc_execute、tc_execute_statement、I/O
│   ├── tc_executor.h
│   └── tc_executor.c
├── driver/             # tc_run_source、tc_run_file、tc_repl_run、main
│   ├── tc_driver.h
│   ├── tc_driver.c
│   ├── tc_repl.h
│   └── tc_repl.c
└── CMakeLists.txt      # tc-vm + check-vm（C99 -Wall -Wextra -pedantic）
```

各模块对外接口见 `tc_*.h` 头文件；`tc_types.h` 为全模块共享的数据契约。

### 12.2 核心接口（C API）

```c
// lexer/tc_lexer.h
int tc_tokenize_line(const char *line, int line_no, TcTokenList *out, TcDiagnostic *diag);

// parser/tc_parser.h
int tc_parse_statement(const TcTokenList *tokens, int line_no, TcStatement *out, TcDiagnostic *diag);

// analyzer/tc_analyzer.h
int tc_analyze(TcProgram *program, TcTypedProgram *out, TcDiagnostic *diag);
int tc_analyze_statement(const TcStatement *stmt, TcSymbolTable *symbols,
                         TcReplAnalyzeCtx *repl_ctx, TcWarningList *warnings,
                         TcDiagnostic *diag);

// executor/tc_executor.h
int tc_execute(const TcTypedProgram *program, TcDiagnostic *diag);
int tc_execute_statement(const TcStatement *stmt, TcValue *slots,
                         const TcSymbolTable *symbols, TcDiagnostic *diag);

// runtime/tc_semantics.h
int tc_exec_arith(TcArithOp op, TcIntType type, TcWrapMode mode,
                  const TcValue *lhs, const TcValue *rhs, TcValue *out,
                  TcDiagnostic *diag, int line);
int tc_exec_unary(TcUnaryOp op, TcIntType type, TcWrapMode mode,
                  const TcValue *operand, TcValue *out,
                  TcDiagnostic *diag, int line);
int tc_exec_cast(TcIntType target, TcTruncateMode mode, const TcValue *source,
                 TcValue *out, TcDiagnostic *diag, int line);

// driver/tc_driver.h
int tc_run_source(const char *source, int check_only, TcDiagnostic *diag);
int tc_run_file(const char *path, int check_only, TcDiagnostic *diag);

// driver/tc_repl.h
int tc_repl_run(TcDiagnostic *diag);
```

### 12.3 CLI

可执行文件路径：`build/vm/bin/tc-vm`。命令行用法、退出码与诊断输出格式见 **[TC-VM 命令行参考](./TC-VM命令行参考.md)**。

```text
tc-vm [options] [<file.tc>]   # 执行文件，成功静默退出 0
tc-vm --check <file.tc>       # 仅 Analyzer，不执行
tc-vm --repl                  # 交互式 REPL
tc-vm --help                  # 显示用法
tc-vm --version               # 显示版本（v0.0.18）
```

退出码：0 成功；非 0 失败（静态或运行时错误）。REPL 正常退出也返回 0。

### 12.4 构建与测试

VM 构建脚本与 AOT 分离；根目录 `Makefile` 转发至 CMake，也可直接调用 CMake。

| 方式        | 编译                  | 测试                                    |
| ----------- | --------------------- | --------------------------------------- |
| 根 Makefile | `make` / `make vm`    | `make test` / `make test-vm`            |
| 根 CMake    | `cmake --build build` | `cmake --build build --target check-vm` |

测试脚本：`scripts/vm/run_tests.sh`（由 `check-vm` 目标调用）。

---

## 13. 执行流程示例

源文件 `example.tc`（含 v0.0.18 新特性）：

```text
; 变量定义
var a: int32 = 10
var b: int32 = 20

; 双目算术
var sum: int32 = add(int32, a, b)

; 单目算术
var neg_a: int32 = neg(int32, a)

; 常量定义
let SCALE: int32 = 1000

; 类型转换
var scaled: int32 = mul(int32, sum, SCALE)
var c: int8 = cast(int8, truncate, scaled)

; I/O 格式化
writeln(int32, %d, c)
```

### 13.1 Analyzer 产出（示意）

| PC   | 行   | 语句                             | 槽位   |
| ---- | ---- | -------------------------------- | ------ |
| 0    | 2    | VarDef a, int32, Lit(10)         | slot 0 |
| 1    | 3    | VarDef b, int32, Lit(20)         | slot 1 |
| 2    | 6    | VarDef sum, int32, Add strict    | slot 2 |
| 3    | 9    | VarDef neg_a, int32, Neg strict  | slot 3 |
| 4    | 12   | ConstDef SCALE, int32, Lit(1000) | slot 4 |
| 5    | 15   | VarDef scaled, int32, Mul strict | slot 5 |
| 6    | 16   | VarDef c, int8, Cast truncate    | slot 6 |
| 7    | 19   | Writeln fmt=%d, c                | —      |

### 13.2 Executor 逐步执行

| PC   | 动作                        | slots 快照   |
| ---- | --------------------------- | ------------ |
| 0    | a ← 10                      | a=10         |
| 1    | b ← 20                      | b=20         |
| 2    | sum ← add(10,20)=30         | sum=30       |
| 3    | neg_a ← neg(10)=-10         | neg_a=-10    |
| 4    | SCALE ← 1000                | SCALE=1000   |
| 5    | scaled ← mul(30,1000)=30000 | scaled=30000 |
| 6    | c ← cast truncate(30000)    | c=-16        |
| 7    | writeln " -16\n"            | stdout       |

---

## 14. 测试策略

### 14.1 测试分层

| 层级 | 对象                                               | 方法                              |
| ---- | -------------------------------------------------- | --------------------------------- |
| 单元 | `exec_arith`、`exec_unary`、`exec_cast`、`TcValue` | 表驱动，对照语言标准 §5、§6 边界  |
| 组件 | Lexer、Parser、Analyzer                            | 单条/多条语句快照                 |
| 集成 | `run(source)`                                      | 语言标准 §8 全文示例              |
| 回归 | 错误用例                                           | 每条语言标准 §9 错误至少 1 个负例 |

### 14.2 Conformance 目录

```text
tests/
├── valid/           # 正例：执行成功 + 可选 stdout 期望 / 警告 / --check
├── errors/static/   # Analyzer 阶段失败
├── errors/runtime/  # Executor 阶段失败
└── stress/          # 压力用例（如 massive_vars.tc）
```

`scripts/vm/run_tests.sh` 由 CMake 目标 `check-vm` 调用；支持 `ASAN=1` 或 `--asan` 切换至 `build-asan/vm/bin/tc-vm`。

### 14.3 回归用例（v0.0.18）

#### valid — 执行成功

| 用例                                                         | 验证方式                                                  |
| ------------------------------------------------------------ | --------------------------------------------------------- |
| `example.tc`、`signed_wrap.tc`、`uint8_wrap.tc`、`var_no_init.tc` | 退出码 0                                                  |
| `no_warn_after_assign.tc`、`no_warn_after_read.tc`           | 成功且无 `warning:`                                       |
| `uninitialized.tc`                                           | 成功且 stderr 含 `use of possibly uninitialized variable` |
| `let_constant.tc`、`hex/oct/bin_literal.tc`、`literal_separator.tc` | stdout 期望值                                             |
| `wrap_int8/uint8_output.tc`、`wrap_sub_mul.tc`、`truncate_cast.tc` | stdout 期望值                                             |
| `div_mod_signed.tc`、`int64_min.tc`、`mod_int_min_neg_one.tc` | stdout 期望值                                             |
| `strict_cast_widen.tc`、`sign_extend_cast.tc`                | stdout 期望值                                             |
| `write_int8_number.tc`、`write_no_newline.tc`                | stdout 期望值                                             |
| **`abs_neg_signed.tc`**                                      | stdout 期望值（新增）                                     |
| **`unary_wrap.tc`**                                          | stdout 期望值（新增）                                     |
| **`format_output.tc`**                                       | stdout 期望值（新增）                                     |
| **`format_hex_bin.tc`**                                      | stdout 期望值（新增）                                     |
| `comments_semicolon.tc`、`semicolon_inline_comment.tc`       | stdout 期望值                                             |
| `read_write.tc`                                              | 管道 stdin + stdout 期望                                  |
| `example.tc`、`let_constant.tc`                              | `--check` 模式退出码 0                                    |

#### stress

| 用例              | 验证方式                 |
| ----------------- | ------------------------ |
| `massive_vars.tc` | 55 个变量定义后输出 `55` |

#### errors/runtime

| 用例                                                | 期望消息片段               |
| --------------------------------------------------- | -------------------------- |
| `signed_strict_overflow.tc`、`signed_strict_mul.tc` | `out of range`             |
| `neg_int_min.tc`                                    | `neg(INT_MIN) overflow`    |
| `abs_int_min.tc`                                    | `abs(INT_MIN) overflow`    |
| `int64_min_div.tc`、`int32_min_div.tc`              | `signed division overflow` |
| `div_zero.tc`、`mod_zero.tc`                        | `division by zero`         |
| `cast_strict_overflow.tc`                           | `out of range`             |
| `read_invalid.tc`                                   | `unexpected end of input`  |
| `read_invalid_input.tc`                             | `invalid input`            |
| `read_out_of_range.tc`                              | `input value out of range` |

#### errors/static

| 用例                                       | 期望消息片段                              |
| ------------------------------------------ | ----------------------------------------- |
| `duplicate_def.tc`、`duplicate_let_var.tc` | `duplicate definition`                    |
| `literal_range.tc`                         | `literal out of range`                    |
| `literal_type_error.tc`                    | `literal type`                            |
| `wrap_mode_error.tc`                       | `div/mod do not support wrap`             |
| `abs_wrap_error.tc`                        | `abs does not support wrap`               |
| `keyword_error.tc`                         | `wrap cannot be used with cast`           |
| `truncate_in_arith.tc`                     | `truncate cannot be used with arithmetic` |
| `const_assign.tc`                          | `cannot assign to constant`               |
| `const_expr.tc`                            | `constant initializer must be a literal`  |
| `negative_unsigned_literal.tc`             | `unsigned suffix`                         |
| `leading_zero.tc`                          | `invalid integer literal`                 |
| `undefined_variable.tc`                    | `undefined variable`                      |
| `forward_reference.tc`                     | `undefined variable`                      |
| `self_reference.tc`                        | `cannot reference itself`                 |
| `type_mismatch.tc`                         | `operand type does not match`             |
| `syntax_error.tc`                          | `unexpected token`                        |
| `cast_literal.tc`                          | `cast source must be a variable`          |
| **`format_string_error.tc`**               | `invalid format specifier`                |
| **`format_type_mismatch.tc`**              | `%d requires signed type`                 |
| **`format_operand_count.tc`**              | `operand count error`                     |

### 14.4 REPL 测试

| 场景                 | 期望                          |
| -------------------- | ----------------------------- |
| 跨行变量 + `writeln` | 输出含 `30`                   |
| 重复 `var` 定义      | 诊断含 `duplicate definition` |
| `:reset` + `:vars`   | `(no variables)`              |
| `let` 常量           | 输出含 `99`                   |
| `:help`              | 输出含 `Meta commands`        |
| `let` 字面量超范围   | 会话无污染                    |

### 14.5 必测边界

- 有符号 strict 溢出：`add(int8, 127, 1)`、`neg(int8, -128)`、`abs(int8, -128)`
- 有符号 wrap：`add(int8, wrap, 127, 1)` → -128、`neg(int8, wrap, -128)` → -128
- 无符号回绕：`add(uint8, 250, 10)` → 4、`neg(uint8, 1)` → 255
- div/mod 向零截断与 C 一致用例
- strict / truncate cast 对照
- `div`/`mod`/`abs` 带 `wrap` → 静态溢出模式错误
- `cast` 带 `wrap` → 静态关键字错误
- 格式化输出：`%x` 输出无符号位模式（`-1` → `ff`）
- `u` 后缀字面量用于有符号类型 → 字面量类型错误
- 无 RHS 的 `var` 定义 → 不报错，读取时产生警告
- `let` 常量定义 → 编译期确定，运行时只读
- `INT64_MIN` 字面量 `-9223372036854775808` → 合法

---

## 15. 与语言标准的边界

| 主题                   | 语言标准       | TC-VM 本文档         |
| ---------------------- | -------------- | -------------------- |
| 语法、语义             | 权威           | 引用，不重复定义     |
| 空行、文件尾           | §4.1 规定      | §4 实现约定          |
| 语法错误               | §9.1 未枚举    | §11.1 VM 扩展        |
| 静态 vs 运行时错误划分 | §9 区分两类    | §7.6、§8.4 实现约定  |
| 编译警告               | §9.3 定义 1 种 | §11.3 WarningKind    |
| 内部 `TcStatement`     | 无             | 实现细节，非语言成分 |
| 诊断格式               | 无             | §11.2 实现约定       |
| 格式化符号             | §7.4 定义 7 种 | §5.2 FormatSpec 枚举 |

若实现约定与语言标准可观测语义冲突，**以语言标准为准**。

---

## 16. 后续扩展预留

语言标准 §5.4、§6.5、§7.6 预留特性在 TC-VM 中的预期挂载点：

| 扩展             | Analyzer                 | Executor              | 状态 |
| ---------------- | ------------------------ | --------------------- | ---- |
| 比较指令         | 新 `Rhs` 变体            | 新 `exec_cmp`         | 预留 |
| 位运算           | 新 `ArithOp` 值          | 新 `exec_bitwise`     | 预留 |
| 逻辑运算         | 新 `bool` 类型           | 新 `exec_logic`       | 预留 |
| `label` / `goto` | 标签符号表、跳转目标检查 | PC 非线性更新         | 预留 |
| `if`             | 条件类型检查             | 条件分支              | 预留 |
| 函数             | 多作用域符号表、参数槽   | 栈帧 / call-return    | 预留 |
| 数组             | 元素类型、索引检查       | 连续内存 + load/store | 预留 |
| 浮点类型         | `float32`/`float64` 类型 | 新 `exec_fp`          | 预留 |
| 字符串           | 字符串类型、格式化输出   | 字符串缓冲区          | 预留 |

扩展时：

1. 仍保持 **TC 源码即指令**，不引入第二字节码
2. 新语法纳入 EBNF 后，同步增加 `TcStatement` / `TcRhs` 变体
3. 保持 `TcStatement` 与源语句 1:1

---

## 17. I/O 语句实现

TC-VM 实现三条 I/O 语句的机制，遵循语言标准 §7 定义的语义。

### 17.1 语句解析（Parser）

`tc_parse_statement()` 根据首 Token 分派：

```
首 Token → TC_TOK_WRITE    → parse_write_stmt()
         → TC_TOK_WRITELN  → parse_writeln_stmt()
         → TC_TOK_READ     → parse_read_stmt()
         → TC_TOK_VAR      → parse_var_def()
         → TC_TOK_LET      → parse_const_def()
         → TC_TOK_IDENTIFIER → assign 分支
```

各分支解析完毕后构造对应的 `TcStatement`：

```text
write(int32, x)           → Write { line, type: int32, fmt: 0, operand: Variable("x") }
write(int32, %d, x)       → Write { line, type: int32, fmt: D, operand: Variable("x") }
write(uint8, %x, 255u)    → Write { line, type: uint8, fmt: X, operand: Literal(255u) }
writeln(int32, x)         → Writeln { line, type: int32, fmt: 0, operand: Variable("x") }
read(int32, x)            → Read { line, type: int32, name: "x" }
```

### 17.2 静态分析（Analyzer）

Pass 2 新增对三种 I/O 语句的检查：

| 语句                | 检查项                                             | 错误类型                             |
| ------------------- | -------------------------------------------------- | ------------------------------------ |
| `write` / `writeln` | `operand` 为变量时：变量已定义且类型与 `type` 一致 | `TypeMismatch` / `UndefinedVariable` |
| `write` / `writeln` | `operand` 为字面量时：值在 `type` 范围内           | `LiteralOutOfRange`                  |
| `write` / `writeln` | 格式化符号与类型兼容性检查                         | `FormatTypeMismatch`                 |
| `write` / `writeln` | 格式化版本恰好一个操作数                           | `OperandCount`                       |
| `read`              | 目标变量已定义且声明的类型与 `type` 一致           | `TypeMismatch` / `UndefinedVariable` |

### 17.3 执行（Executor）

**Write / Writeln 执行流程**：

1. 求值 `operand`，得到 `TcValue { type, bits }`
2. 若 `fmt == 0`（无格式）：输出十进制
3. 若 `fmt != 0`（格式化）：按格式符号输出
4. `write` 不换行；`writeln` 追加 `\n`

**格式化输出实现**：

```c
static void write_formatted(TcIntType type, TcFormatSpec fmt,
                            const TcValue *value, FILE *out) {
    uint64_t bits = value->bits & tc_type_mask(type);
    int n = tc_type_bit_width(type);
    uint64_t mask = (n == 64) ? UINT64_MAX : ((1ULL << n) - 1);
    uint64_t uval = bits & mask;
    
    switch (fmt) {
    case TC_FMT_D:
    case TC_FMT_I: {
        if (!tc_type_is_signed(type)) {
            fprintf(out, "%llu", (unsigned long long)uval);
        } else {
            int64_t sval = tc_bits_to_signed(bits, n);
            fprintf(out, "%lld", (long long)sval);
        }
        break;
    }
    case TC_FMT_U:
        fprintf(out, "%llu", (unsigned long long)uval);
        break;
    case TC_FMT_X:
        fprintf(out, "%llx", (unsigned long long)uval);
        break;
    case TC_FMT_XU:
        fprintf(out, "%llX", (unsigned long long)uval);
        break;
    case TC_FMT_O:
        fprintf(out, "%llo", (unsigned long long)uval);
        break;
    case TC_FMT_B:
        for (int i = n - 1; i >= 0; i--) {
            fputc((uval >> i) & 1 ? '1' : '0', out);
        }
        break;
    }
}
```

**Read 执行流程**：

1. 从 `stdin` 读取输入：
   ```
   skip_whitespace()
   if type is signed and peek() == '-': sign = -1; advance()
   value = 0; digit_count = 0
   while peek() is digit: value = value*10 + digit; digit_count++
   if digit_count == 0: → TC_ERR_IO
   value *= sign
   if not fits_type(value, type): → TC_ERR_IO
   if EOF before any digit: → TC_ERR_IO
   ```
2. 将 `value` 编码为 `type` 的位模式，写入目标变量槽位

**特别说明**：`int8` 和 `uint8` 按整数数字输出，而非 ASCII 字符。例如 `write(int8, 65)` 输出 `"65"`（与语言标准 §7.1 一致）。

---

## 18. 交互式 REPL 实现

TC-VM 提供交互式 REPL 模式，支持逐条输入 TC 语句并立即执行，变量跨行保留。实现文件为 `driver/repl.c`。

### 18.1 设计目标

| 目标       | 说明                                                  |
| ---------- | ----------------------------------------------------- |
| 增量执行   | 每输入一行即执行，无需等待完整程序                    |
| 有状态     | 变量定义与赋值跨行保留，接近交互式调试体验            |
| 错误容忍   | 单行错误不影响已建立的变量状态，即时报告后继续        |
| 零外部依赖 | 使用 POSIX `getline` + `isatty`，不引入 readline 等库 |

### 18.2 架构

REPL 不经过 `tc_run_source` / `tc_run_file` 的完整流水线，而是独立维护一个有状态的 `TcReplSession`：

```text
TcReplSession {
    symbols:           TcSymbolTable      // 增量累积的符号表
    slots:             TcValue[]          // 运行时槽位；容量随 symbols 动态扩展
    slots_capacity:    size_t
    line_no:           int                // 行计数器（从 1 递增）
    analyze_ctx:       TcReplAnalyzeCtx   // stmt_count + last_init_stmt_index[]
}

TcReplAnalyzeCtx {
    stmt_count:              size_t   // 已执行语句计数
    last_init_stmt_index:    int[]    // 每槽最后 Assign/Read 的 stmt_index
    last_init_capacity:      size_t
}
```

主循环伪代码：

```text
session ← 初始化
while 未退出:
    可选显示提示符 "tc> "（仅 TTY 模式）
    读取一行
    跳过空行/注释行
    若以 ':' 开头 → 处理元命令
    否则:
        tokenize_line → parse_statement → analyze_statement → execute_statement
        slot 数组按需扩展
        line_no++
```

### 18.3 增量分析

与文件模式的关键区别：

| 方面       | 文件模式                             | REPL 模式                                |
| ---------- | ------------------------------------ | ---------------------------------------- |
| 分析       | 全程序 `tc_analyze()`                | 逐条 `tc_analyze_statement()`            |
| 符号表     | 嵌入 `TcTypedProgram`                | 持久化在 `TcReplSession.symbols`         |
| 初始化历史 | Pass 2 预扫描 `last_init_stmt_index` | `TcReplAnalyzeCtx` 增量维护              |
| 变量槽     | Executor 临时分配                    | 持久化在 `TcReplSession.slots`，按需扩展 |
| 警告       | 分析后批量打印                       | 每行分析后打印（若有）                   |
| 错误处理   | 失败即终止                           | 报告错误后继续接受下一条输入             |

### 18.4 槽位管理

REPL 的变量槽数组随符号表同步增长：

```text
tc_repl_ensure_slots(session):
    if session.symbols.count > session.slots_capacity:
        session.slots ← realloc(session.slots, count * sizeof(TcValue))
        新增部分置零
```

槽索引由 `tc_analyze_statement()` 在插入新符号时分配。

### 18.5 元命令

| 命令                     | 实现                                                 |
| ------------------------ | ---------------------------------------------------- |
| `:quit` / `:exit` / `:q` | 设置 `should_quit = 1`，退出主循环                   |
| `:reset`                 | `tc_repl_session_reset()` 释放符号表+槽位+分析上下文 |
| `:vars`                  | 遍历 `session.symbols`，输出 `name: type = value`    |
| `:help`                  | 打印内置命令帮助                                     |

### 18.6 TTY 检测

REPL 通过 POSIX `isatty()` 检测 stdin 和 stderr 是否为终端：

- **TTY 模式**：输出欢迎信息、显示 `tc>` 提示符、交互式会话
- **非 TTY 模式**：静默处理，不显示提示符和欢迎信息

### 18.7 错误处理

| 阶段          | 错误处理                                          |
| ------------- | ------------------------------------------------- |
| 词法/语法错误 | 打印诊断到 stderr，不更新符号表和槽位，继续下一行 |
| 静态分析错误  | 同上；`VarDef`/`ConstDef` 失败时**不**插入符号表  |
| 槽位分配失败  | 若已插入符号则 `tc_symbol_table_pop_last` 回滚    |
| 运行时错误    | 打印诊断；该语句副作用不发生，已有状态保留        |

所有错误均为非致命：REPL 不会因单行错误退出。

---

## 附录 A：TcStatement 与源语句对照

| 源语句                        | TcStatement                                                  |
| ----------------------------- | ------------------------------------------------------------ |
| `var n: int32 = 0`            | `VarDef(line, "n", int32, LitRhs(0))`                        |
| `var n: int32`                | `VarDef(line, "n", int32, None)`                             |
| `let N: int32 = 42`           | `ConstDef(line, "N", int32, LitRhs(42))`                     |
| `n = add(int32, a, b)`        | `Assign(line, "n", ArithRhs(Add, int32, Strict, Var(a), Var(b)))` |
| `n = add(int32, wrap, a, b)`  | `ArithRhs(..., Wrap, ...)`                                   |
| `n = abs(int32, a)`           | `Assign(line, "n", UnaryRhs(Abs, int32, Strict, Var(a)))`    |
| `n = neg(int32, wrap, a)`     | `Assign(line, "n", UnaryRhs(Neg, int32, Wrap, Var(a)))`      |
| `n = cast(int8, x)`           | `Assign(line, "n", CastRhs(int8, StrictTrunc, "x"))`         |
| `n = cast(int8, truncate, x)` | `CastRhs(int8, Truncate, "x")`                               |
| `write(int32, x)`             | `Write(line, int32, fmt=0, Variable("x"))`                   |
| `write(int32, %d, x)`         | `Write(line, int32, fmt=D, Variable("x"))`                   |
| `writeln(uint8, %x, 255u)`    | `Writeln(line, uint8, fmt=X, Literal(255u))`                 |
| `read(int32, x)`              | `Read(line, int32, "x")`                                     |

---

## 附录 B：Fetch–Decode–Execute 对照

| 经典 VM          | TC-VM                                                      |
| ---------------- | ---------------------------------------------------------- |
| Fetch 二进制指令 | Fetch `program[PC]`（TcStatement）                         |
| Decode opcode    | `match stmt / rhs` 枚举                                    |
| Execute          | `exec_arith` / `exec_unary` / `exec_cast` / 槽位写入 / I/O |
| PC++             | 除非未来 `goto` 改 PC                                      |

---

## 附录 C：文档修订记录

| 版本       | 日期           | 说明                                                         |
| ---------- | -------------- | ------------------------------------------------------------ |
| 1.0        | 2026-07-01     | 首版：直接执行式 TC-VM 架构                                  |
| 1.0.1      | 2026-07-01     | 对齐 TC-Compiler 工程布局                                    |
| 1.0.2      | 2026-07-01     | §12.3 引用《TC-VM 命令行参考》                               |
| 1.1        | 2026-07-01     | 新增 I/O 语句实现                                            |
| 1.2        | 2026-07-01     | 新增交互式 REPL 实现                                         |
| 0.0.13     | 2026-07-02     | 版本号对齐；`overflow` 拆分；`let` 常量；未初始化变量        |
| 0.0.13-fix | 2026-07-02     | 修复与语言标准对齐问题                                       |
| 0.0.14     | 2026-07-03     | 与 TC-VM 源码全面对齐                                        |
| **0.0.17** | **2026-07-03** | **与语言标准 v0.0.17 全面对齐**：新增单目运算 `abs`/`neg` 支持；新增 I/O 格式化输出（7 种格式符号）；完善字面量 `u` 后缀解析；完善常量 `let` 编译期值处理；完善 `cast` strict/truncate 语义实现；完善 `wrap` 模式位级一致性；补充格式字符串/类型不匹配/操作数数量等静态错误类型；更新 §5、§7、§10、§11、§14、§17、§18 及附录 A |
| **0.0.18** | **2026-07-03** | **与语言标准 v0.0.18 对齐**：`div(INT_MIN, −1)` 报整数溢出错误、`mod(INT_MIN, −1)` 为 0；更新 §9.3、§10.2、§10.5、§14.3 及回归用例索引 |

---

*— 文档结束 —*
