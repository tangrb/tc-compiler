# TC 语言标准设计说明书

> **版本**：0.8.1（草案）  
> **作者**：唐荣兵（[yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)）  
> **范围**：固定宽度整数类型、五类算术运算指令、`cast` 类型转换指令、`write`/`writeln`/`read` I/O 语句  
> **风格**：表达式 / 赋值式伪汇编  
> **参考实现**：[TC-Compiler](../README.md) 工程（TC-VM 已实现，TC-AOT 预留）

---

## 目录

1. [概述](#1-概述)
2. [形式化语法（EBNF）](#2-形式化语法ebnf)
3. [词法说明](#3-词法说明)
4. [类型系统](#4-类型系统)
5. [语句语义](#5-语句语义)
6. [算术运算语义](#6-算术运算语义)
7. [类型转换语义（cast）](#7-类型转换语义cast)
8. [运行时语义](#8-运行时语义)
9. [完整示例](#9-完整示例)
10. [错误汇总](#10-错误汇总)
11. [I/O 语句语义](#11-io-语句语义)
12. [后续扩展预留](#12-后续扩展预留)
13. [参考实现（TC-Compiler）](#13-参考实现tc-compiler)
- [附录 A：与 C 语言对照](#附录-a与-c-语言整数语义对照)
- [附录 B：类型边界常量](#附录-b各类型边界常量速查)
- [附录 C：文档修订记录](#附录-c文档修订记录)

---

## 1. 概述

TC 是一门伪汇编语言：语法接近高级语言的赋值表达式，执行模型保留汇编式的 **逐条语句** 顺序执行。每条语句至多包含 **一个** 运算或转换调用，**不支持** 表达式嵌套。

本文档定义：

- **8 种** 固定宽度整数类型（`int8`～`uint64`）
- **5 类** 算术运算指令：`add`、`sub`、`mul`、`div`、`mod`
- **1 类** 类型转换指令：`cast`
- **3 条** 标准 I/O 语句：`write`、`writeln`、`read`
- 可选 **`overflow`** 模式（算术回绕 / 位截断）

### 1.1 设计原则

| 原则 | 说明 |
|------|------|
| 声明与赋值分离 | 首次定义使用 `var`，后续修改省略 `var` |
| 显式类型标注 | 变量定义须声明具体整数类型 |
| 运算函数化 | 算术以 `op(<类型>, 左, 右)` 形式调用 |
| 类型参数首位 | 运算与转换的类型均作为 **第一个参数** 显式给出 |
| 默认严格溢出 | 有符号 `add`/`sub`/`mul` 与 `cast` 默认检测溢出并报错；回绕或位截断须显式传入 `overflow` |
| 同类型运算 | 同一运算的两操作数须为相同整数类型 |
| 显式类型转换 | 跨类型须通过 `cast` 完成，禁止隐式转换 |
| 逐条执行 | 每条语句独立执行，无隐式表达式嵌套求值 |

---

## 2. 形式化语法（EBNF）

本章为 TC 语言的 **权威语法定义**。后续章节的语义规则均以此处的产生式为准。

### 2.1 符号约定

本文采用 **扩展巴科斯-瑙尔范式（EBNF）**，符号含义如下：

| 元符号 | 含义 |
|--------|------|
| `=` | 产生式定义（读作「定义为」） |
| `\|` | 择一（alternation） |
| `[ … ]` | 可选（0 或 1 次） |
| `{ … }` | 重复（0 或多次） |
| `( … )` | 分组 |
| `"…"` | 终结符（字面量 token） |
| *斜体名* | 非终结符 |
| `/* … */` | 注释（非语言成分，仅供阅读） |

**程序结构约束**（词法层，见 §3.5）：

- 源文件由若干 **行** 组成；每行至多一条 *statement*。
- 行尾可跟 *line_comment*；若同一行存在注释，语句与注释之间 **必须** 以 `;` 分隔。

### 2.2 词法（Lexical Grammar）

```ebnf
/* ── 基本字符类 ── */

letter     = "A" | "B" | "C" | "D" | "E" | "F" | "G" | "H" | "I" | "J"
           | "K" | "L" | "M" | "N" | "O" | "P" | "Q" | "R" | "S" | "T"
           | "U" | "V" | "W" | "X" | "Y" | "Z"
           | "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h" | "i" | "j"
           | "k" | "l" | "m" | "n" | "o" | "p" | "q" | "r" | "s" | "t"
           | "u" | "v" | "w" | "x" | "y" | "z" | "_" ;

digit      = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;

/* ── 标识符与字面量 ── */

identifier = letter , { letter | digit } ;

integer_literal
           = digit , { digit } ;          /* 十进制非负整数，无符号前缀 */

/* ── 类型关键字 ── */

int_type   = "int8"   | "uint8"
           | "int16"  | "uint16"
           | "int32"  | "uint32"
           | "int64"  | "uint64" ;

/* ── 运算与模式关键字 ── */

arith_op   = "add" | "sub" | "mul" | "div" | "mod" ;

overflow_kw
           = "overflow" ;                 /* 溢出回绕 / 位截断模式关键字 */

/* ── 注释 ── */

line_comment
           = ";" , { ? any character except newline ? } ;
```

### 2.3 语法（Syntactic Grammar）

```ebnf
/* ── 程序 ── */

program    = { statement , newline } ;

statement  = variable_def
           | assignment
           | write_stmt
           | writeln_stmt
           | read_stmt ;

/* ── 变量定义与赋值 ── */

variable_def
           = "var" , ws , identifier , ws , ":" , ws , int_type , ws ,
             "=" , ws , rhs
             , [ stmt_terminator ] ;

assignment = identifier , ws , "=" , ws , rhs
             , [ stmt_terminator ] ;

stmt_terminator
           = ";" , [ ws , line_comment ]
           | ws , line_comment ;          /* 有注释时 ; 为必须，见 §3.5 */

/* ── 右值表达式（三选一，不可嵌套） ── */

rhs        = integer_literal
           | arith_expr
           | cast_expr ;

/* ── 操作数 ── */

operand    = identifier
           | integer_literal ;            /* 按上下文类型解析，见 §4.3 */

/* ── 算术表达式 ── */

arith_expr = strict_arith_expr
           | overflow_arith_expr ;

strict_arith_expr
           = arith_op , "(" , ws , int_type , ws , ","
             , ws , operand , ws , "," , ws , operand , ws , ")" ;

overflow_arith_expr
           = arith_op , "(" , ws , int_type , ws , ","
             , ws , overflow_kw , ws , ","
             , ws , operand , ws , "," , ws , operand , ws , ")" ;

/* arith_op ∈ { add, sub, mul, div, mod }
   overflow 形式仅 add/sub/mul 合法；div/mod 使用则报错，见 §6.1 */

/* ── 类型转换表达式 ── */

cast_expr  = strict_cast_expr
           | overflow_cast_expr ;

strict_cast_expr
           = "cast" , "(" , ws , int_type , ws , ","
             , ws , identifier , ws , ")" ;

overflow_cast_expr
           = "cast" , "(" , ws , int_type , ws , ","
             , ws , overflow_kw , ws , ","
             , ws , identifier , ws , ")" ;

/* cast 源操作数须为变量，不可为字面量或嵌套表达式，见 §7.1 */

/* ── I/O 语句 ── */

write_stmt
           = "write" , "(" , ws , int_type , ws , ","
             , ws , operand , ws , ")"
             , [ stmt_terminator ] ;

writeln_stmt
           = "writeln" , "(" , ws , int_type , ws , ","
             , ws , operand , ws , ")"
             , [ stmt_terminator ] ;

read_stmt  = "read" , "(" , ws , int_type , ws , ","
             , ws , identifier , ws , ")"
             , [ stmt_terminator ] ;

/* ── 空白（仅用于可读性，无语义） ── */

ws         = { " " | "\t" } ;

newline    = "\n" | "\r\n" | "\r" ;
```

### 2.4 产生式索引

| 非终结符 | 含义 | 语义章节 |
|----------|------|----------|
| `program` | 完整源程序 | §5 |
| `variable_def` | 变量定义（含 `var`） | §5.1 |
| `assignment` | 变量赋值（无 `var`） | §5.2 |
| `rhs` | 赋值右值 | §5.3 |
| `operand` | 算术操作数 | §5.4 |
| `strict_arith_expr` | 三参数算术（默认 strict） | §6 |
| `overflow_arith_expr` | 四参数算术（overflow 回绕） | §6.1 |
| `strict_cast_expr` | 两参数 cast（默认 strict） | §7 |
| `overflow_cast_expr` | 三参数 cast（overflow 位截断） | §7.2 |
| `int_type` | 八种整数类型 | §4 |
| `integer_literal` | 非负整数字面量 | §3.2、§4.3 |
| `write_stmt` | 输出语句（不换行） | §11.1 |
| `writeln_stmt` | 输出语句（换行） | §11.2 |
| `read_stmt` | 输入语句 | §11.3 |

### 2.5 语法约束摘要

以下约束由语义分析器在 EBNF 之上强制执行：

| 约束 | 说明 |
|------|------|
| 操作数同型 | `strict_arith_expr` / `overflow_arith_expr` 中两 *operand* 类型须与第一参数 *int_type* 相同 |
| 目标类型一致 | *variable_def* / *assignment* 左侧变量类型须与 *rhs* 结果类型相同 |
| cast 源为变量 | *strict_cast_expr* / *overflow_cast_expr* 的源操作数须为已定义的 *identifier* |
| 无嵌套表达式 | *operand* 不可为 *arith_expr* 或 *cast_expr* |
| overflow 适用范围 | *overflow_arith_expr* 中 *arith_op* 仅允许 `add`/`sub`/`mul` |
| 无重复定义 | 同一作用域内 *identifier* 不可重复出现在 *variable_def* 左侧 |

---

## 3. 词法说明

### 3.1 标识符

- 由 `letter` 与 `digit` 组成，**必须以 `letter` 开头**（`letter` 含 `_`）。
- **区分大小写**：`a` 与 `A` 为不同变量。
- 合法：`a`、`count`、`_tmp1`；非法：`1a`、`-x`（`-` 非标识符字符）。

### 3.2 整数字面量

- 仅支持十进制非负整数：`0`、`10`、`255`。
- **不支持** 负号前缀、小数、科学计数法、进制前缀（如 `0x`）。
- 字面量本身无类型；合法取值范围由 **上下文类型** 决定（§4.3）。

### 3.3 注释

- 行注释以 `;` 开始，延续至行尾。
- 示例：`var a: int32 = 10    ; 定义 32 位整数`

### 3.4 关键字

以下 token 为保留关键字，不可用作标识符：

```text
var  int8  uint8  int16  uint16  int32  uint32  int64  uint64
add  sub  mul  div  mod  cast  overflow  write  writeln  read
```

### 3.5 语句与行结构

- 每条 *statement* 独占一行。
- 语句末尾 `;` **可选**；若同行存在 *line_comment*，则 `;` **必须** 出现在语句与注释之间（见 `stmt_terminator` 产生式）。

---

## 4. 类型系统

### 4.1 类型总览

| 类型 | 位宽 | 符号 | 最小值 | 最大值 |
|------|------|------|--------|--------|
| `int8` | 8 | 有符号 | −128 | 127 |
| `uint8` | 8 | 无符号 | 0 | 255 |
| `int16` | 16 | 有符号 | −32,768 | 32,767 |
| `uint16` | 16 | 无符号 | 0 | 65,535 |
| `int32` | 32 | 有符号 | −2,147,483,648 | 2,147,483,647 |
| `uint32` | 32 | 无符号 | 0 | 4,294,967,295 |
| `int64` | 64 | 有符号 | −9,223,372,036,854,775,808 | 9,223,372,036,854,775,807 |
| `uint64` | 64 | 无符号 | 0 | 18,446,744,073,709,551,615 |

类型命名：`<符号前缀><位宽>`，其中符号前缀为 `int`（有符号）或 `uint`（无符号），位宽为 `8`/`16`/`32`/`64`。

### 4.2 类型性质

**有符号类型（`int8`～`int64`）**

- 采用 **二补码** 表示。
- `add`/`sub`/`mul` 默认 **strict**：结果超出范围 → **整数溢出错误**（§10）。
- 显式 `overflow` 模式：按模 2^n 回绕，不报错（§6.1）。

**无符号类型（`uint8`～`uint64`）**

- 仅表示非负整数。
- 算术结果超出最大值时 **模 2^n 回绕**，不报错。
- `div`/`mod` 结果恒为非负。

### 4.3 字面量与类型的关系

整数字面量须能放入上下文所要求的类型范围内，否则触发 **字面量范围错误**：

```text
var a: uint8 = 255     ; 合法
var b: uint8 = 256     ; 字面量范围错误
var c: int32 = 100     ; 合法（对所有更宽类型均合法）
```

在 *arith_expr* 中，无类型字面量按 **第一类型参数**（*int_type*）解析：

```text
var x: uint8 = 200
var y = add(uint8, x, 50)     ; 50 解析为 uint8
var z = add(uint8, x, 100)    ; 100 解析为 uint8，回绕为 44
```

---

## 5. 语句语义

语法产生式见 §2.3 的 `variable_def`、`assignment`、`rhs`。

### 5.1 变量定义（variable_def）

使用 `var` 声明并初始化变量，**必须** 显式标注 *int_type*。同一作用域内不可重复定义同名变量。

```text
var a: int32 = 10
var b: int32 = add(int32, a, 20)
var c: int8  = cast(int8, overflow, a)
```

### 5.2 变量赋值（assignment）

对已定义变量重新赋值时 **省略** `var`。右侧 *rhs* 结果类型须与变量已声明类型 **完全一致**。

```text
var c: int32 = add(int32, a, b)
c = mul(int32, a, b)
```

跨类型赋值须先 `cast`，使 *rhs* 类型与左侧变量一致。

### 5.3 右值表达式（rhs）

| 产生式 | 结果类型 | 说明 |
|--------|----------|------|
| `integer_literal` | 由左侧变量类型决定 | 须满足字面量范围约束 |
| `arith_expr` | 第一参数 *int_type* | 五种算术运算 |
| `cast_expr` | 第一参数 *int_type* | 类型转换 |

### 5.4 操作数（operand）

| 形式 | 约束 |
|------|------|
| *identifier* | 已定义，且类型与 *int_type* 一致 |
| *integer_literal* | 非负整数，须落在 *int_type* 取值范围内 |

**不支持** 嵌套：*operand* 不可为 *arith_expr* 或 *cast_expr*。复合计算须拆分为多条语句：

```text
var t: int32 = add(int32, a, b)
var c: int32 = add(int32, t, c)
```

---

## 6. 算术运算语义

语法：`strict_arith_expr` 与 `overflow_arith_expr`（§2.3）。

### 6.1 溢出模式（overflow）

`add`、`sub`、`mul` 支持可选的 `overflow` 参数；`div`、`mod` **不支持**。

| 调用形式 | 模式 | 有符号（`int8`～`int64`） | 无符号（`uint8`～`uint64`） |
|----------|------|---------------------------|------------------------------|
| `<op>(<T>, 左, 右)` | strict（默认） | 超范围 → **整数溢出错误** | 超最大值 → **模 2^n 回绕** |
| `<op>(<T>, overflow, 左, 右)` | overflow | 超范围 → **模 2^n 回绕** | 与三参数形式相同 |

其中 `<op>` ∈ {`add`, `sub`, `mul`}；n 为 `<T>` 位宽。

**回绕规则**：先按 `<T>` 的位宽与符号性计算算术结果，再取 mod 2^n，将无符号位模式解释为目标类型值。有符号 `overflow` 回绕与同位宽无符号默认回绕 **位模式一致**。

**约束**：

- `overflow` 必须是关键字，不能是变量或字面量。
- `div`/`mod` 使用 `overflow` → **溢出模式错误**。
- 第二参数为 `overflow` 以外的标识符 → **溢出模式错误**。

```text
var a: int8 = 127
var b: int8 = 1
var c: int8 = add(int8, a, b)               ; 整数溢出错误
var d: int8 = add(int8, overflow, a, b)     ; d = -128
```

### 6.2 有符号运算（`int8`～`int64`）

| 指令 | 语义 |
|------|------|
| `add` / `sub` / `mul` | 返回和 / 差 / 积；strict 超范围报错，overflow 回绕 |
| `div` | 商，**向零截断**（同 C 有符号 `/`）；除数为 0 → **除零错误** |
| `mod` | 余数符号与 **被除数** 相同（同 C 有符号 `%`）；除数为 0 → **除零错误** |

恒等式（`b ≠ 0`）：`mod(<T>, a, b) == sub(<T>, a, mul(<T>, div(<T>, a, b), b))`

```text
var g: int32 = 7
var h: int32 = -3
var i: int32 = div(int32, g, h)    ; i = -2（向零截断）

var j: int32 = -7
var k: int32 = 3
var m: int32 = mod(int32, j, k)    ; m = -1
```

### 6.3 无符号运算（`uint8`～`uint64`）

| 指令 | 语义 |
|------|------|
| `add` / `sub` / `mul` | 和 / 差 / 积；超范围模 2^n 回绕 |
| `div` | 数学整数商；除数为 0 → **除零错误** |
| `mod` | 余数满足 `0 ≤ 余数 < 除数`；除数为 0 → **除零错误** |

```text
var a: uint8 = 200
var b: uint8 = 100
var c: uint8 = add(uint8, a, b)    ; c = 44（300 mod 256）

var h: uint8 = 5
var i: uint8 = 10
var j: uint8 = sub(uint8, h, i)    ; j = 251
```

---

## 7. 类型转换语义（cast）

语法：`strict_cast_expr` 与 `overflow_cast_expr`（§2.3）。TC 提供 **唯一** 转换指令 `cast`，八种整数类型互相转换均复用该指令。

### 7.1 基本约束

- 第一参数 *int_type*：目标类型（类型关键字，非变量）。
- 源操作数：须为已定义的 *identifier*（八种整数类型均可）。
- 返回值类型：与第一参数相同。
- 左侧变量声明类型须与第一参数一致。
- **不支持** 嵌套调用；须拆分为多条语句。

### 7.2 转换模式（strict / overflow）

| 调用形式 | 模式 | 行为 |
|----------|------|------|
| `cast(<T>, 源)` | strict（默认） | 数值可表示性检查；不可表示 → **转换溢出错误** |
| `cast(<T>, overflow, 源)` | overflow | 按位转换（截断 / 扩展 / 重解释），不报错 |

`cast` 与算术运算共用关键字 `overflow`，但语义为 **按位转换** 而非算术回绕。

```text
var big: int32 = 1000
var err: int8 = cast(int8, big)                  ; 转换溢出错误
var trunc: int8 = cast(int8, overflow, big)      ; trunc = -24

var n: int32 = sub(int32, 0, 1)                  ; n = -1
var u: uint8 = cast(uint8, n)                    ; 转换溢出错误
var bits: uint8 = cast(uint8, overflow, n)       ; bits = 255
```

### 7.3 strict 模式规则

#### 加宽（目标位宽 > 源位宽）

| 源 | 目标 | 规则 |
|----|------|------|
| 有符号 | 有符号 | 符号扩展，数值不变 |
| 无符号 | 无符号 | 零扩展，数值不变 |
| 有符号 | 无符号 | 源 ≥ 0 则转换；源 < 0 → 错误 |
| 无符号 | 有符号 | 源 ≤ 目标 max 则转换；否则 → 错误 |

#### 等宽（位宽相同，符号性不同）

| 源 | 目标 | 规则 |
|----|------|------|
| 有符号 | 无符号 | 源 ≥ 0 则转换；源 < 0 → 错误 |
| 无符号 | 有符号 | 源 ≤ 目标 max 则转换；否则 → 错误 |

#### 缩窄（目标位宽 < 源位宽）

| 源 | 目标 | 规则 |
|----|------|------|
| 有符号 | 有符号 | 源在目标范围内则转换；否则 → 错误 |
| 无符号 | 无符号 | 截断低 n 位（mod 2^n），不报错 |
| 有符号 | 无符号 | 源 ≥ 0 且 ≤ 目标 max 则转换；否则 → 错误 |
| 无符号 | 有符号 | 源 ≤ 目标 max 则转换；否则 → 错误 |

#### 同类型

源类型与目标类型相同 → 恒等转换。

### 7.4 overflow 模式规则

设源位宽 n、目标位宽 m；先将源操作数表示为 n 位整数 `bits`（0 ≤ `bits` < 2^n）。

1. **缩窄或等宽**（m ≤ n）：取 `bits mod 2^m`，按目标类型解释。
2. **加宽**（m > n）：
   - 目标无符号，或源无符号：零扩展至 m 位。
   - 源、目标均有符号且 `bits` 第 n−1 位为 1：符号扩展至 m 位。
   - 其余加宽：零扩展至 m 位。

| 场景 | strict | overflow |
|------|--------|----------|
| 有符号缩窄超范围 | 错误 | 截断低 m 位并重解释 |
| 有符号 → 无符号（源为负） | 错误 | 重解释（如 −1 → 255） |
| 无符号 → 有符号（源 > max） | 错误 | 重解释（如 200 → −56） |
| 无符号 → 无符号缩窄 | 截断 | 截断（相同） |
| strict 下合法的加宽 | 扩展 | 相同结果 |

### 7.5 转换规则速查

**strict**：

```text
                    目标类型
                 int（有符号）          uint（无符号）
              ┌──────────────────┬──────────────────┐
   源   int   │ 范围内保留/符号扩展 │  非负且范围内     │
              │  超出 → 错误      │  否则 → 错误      │
              ├──────────────────┼──────────────────┤
       uint   │  ≤ 目标 max      │  同宽/缩窄：截断  │
              │  否则 → 错误      │  加宽：零扩展      │
              └──────────────────┴──────────────────┘
```

**overflow**：任意源 → 任意目标，按 §7.4 位模式算法处理，不报错。

---

## 8. 运行时语义

### 8.1 运算优先级与求值顺序

- TC **不存在** 运算符优先级；复合计算须拆分为多条语句。
- 同一语句内：左操作数先于右操作数求值；`overflow` 为编译期关键字，不参与运行时求值。
- 两操作数求值完毕后执行运算。

### 8.2 负数构造

字面量仅支持非负数。有符号负数通过减法获得：

```text
var zero: int32 = 0
var n: int32 = sub(int32, zero, 10)    ; n = -10
```

### 8.3 除零

所有 `div`/`mod` 右操作数为 `0` 时 → **除零错误**，程序终止。

### 8.4 未定义变量

引用未通过 `var` 定义的 *identifier* → **未定义变量错误**，程序终止。

### 8.5 类型一致性检查

触发 **类型错误** 的情况：

- 运算第一类型参数与操作数类型不一致
- 赋值左侧类型与 *rhs* 结果类型不一致
- 两操作数类型不同
- `cast` 目标变量类型与第一参数不一致

触发 **溢出模式错误** 的情况：

- `div`/`mod` 使用了 `overflow`
- `add`/`sub`/`mul`/`cast` 在类型参数之后出现非法第二参数（既非 `overflow` 又非合法操作数）

---

## 9. 完整示例

### 9.1 int32 四则运算

```text
var a: int32 = 10
var b: int32 = 20
var sum:  int32 = add(int32, a, b)
var diff: int32 = sub(int32, a, b)
var prod: int32 = mul(int32, a, b)
var quot: int32 = div(int32, b, a)
var rem:  int32 = mod(int32, b, a)
```

### 9.2 uint8 回绕

```text
var a: uint8 = 250
var b: uint8 = 10
var c: uint8 = add(uint8, a, b)    ; 4
var d: uint8 = sub(uint8, b, a)    ; 16
var e: uint8 = mul(uint8, a, b)    ; 228
```

### 9.3 复合计算 `(a + b) * c`

```text
var a: int16 = 3
var b: int16 = 4
var c: int16 = 5
var t: int16 = add(int16, a, b)
var result: int16 = mul(int16, t, c)    ; 35
```

### 9.4 跨类型转换与运算组合

```text
var x: int32 = 50
var y: uint8 = 200
var y32: int32 = cast(int32, y)
var sum: int32 = add(int32, x, y32)    ; 250

var big: int32 = 1000
var truncated: int8 = cast(int8, overflow, big)    ; -24
```

### 9.5 有符号 overflow 回绕

```text
var a: int8 = 127
var b: int8 = 1
var wrap: int8 = add(int8, overflow, a, b)    ; -128

var big: int16 = 30000
var small: int16 = 30000
var prod: int16 = mul(int16, overflow, big, small)
```

### 9.6 I/O 输出示例

```text
var x: int32 = 42
var y: int32 = 100
write(int32, x)            ; 输出 "42"
write(int32, y)            ; 输出 "100"        → stdout: "42100"
writeln(int32, x)          ; 输出 "42\n"
writeln(int32, y)          ; 输出 "100\n"      → stdout: "42\n100\n"
```

### 9.7 I/O 输入输出综合示例

```text
var a: int32 = 0
var b: int32 = 0
read(int32, a)             ; 从 stdin 读取一个 int32
read(int32, b)
var sum: int32 = add(int32, a, b)
writeln(int32, sum)        ; 输出结果并换行
```

### 9.8 读取负数

```text
var n: int32 = 0
read(int32, n)             ; 输入 "-42" → n = -42
writeln(int32, n)          ; 输出 "-42\n"
```

---

## 10. 错误汇总

| 错误类型 | 触发条件 |
|----------|----------|
| 未定义变量错误 | 使用未 `var` 声明的 *identifier* |
| 重复定义错误 | 同一作用域重复 `var` 同名变量 |
| 类型错误 | 运算或赋值时类型不一致 |
| 字面量范围错误 | *integer_literal* 超出上下文类型范围 |
| 除零错误 | `div`/`mod` 右操作数为 0 |
| 整数溢出错误 | 有符号类型 strict 模式下 `add`/`sub`/`mul` 结果超范围 |
| 溢出模式错误 | `div`/`mod` 使用 `overflow`；或 `add`/`sub`/`mul`/`cast` 第二参数非法 |
| 转换溢出错误 | `cast` strict 模式下结果无法在目标类型中表示 |
| I/O 错误 | `read` 读取失败（非法输入格式、数值超出变量类型范围、非预期的 EOF） |

---

## 11. I/O 语句语义

本章定义三条 I/O 语句：`write`、`writeln`、`read`。三者均属于 *statement* 产生式（§2.3），**不作为 RHS 表达式**，保持 RHS 作为纯计算表达式的设计纯度。

### 11.1 `write(type, operand)` — 不换行输出

| 项目 | 说明 |
|------|------|
| 语法 | `write(int32, x)`、`write(uint8, 42)` |
| 功能 | 将 `operand` 的值按 `type` 解释，以十进制文本写入 stdout，**不换行** |
| 操作数规则 | 变量（须已定义）或整数字面量（须在 `type` 范围内） |
| 类型约束 | `type` 须与 `operand` 实际类型一致；字面量须在 `type` 范围内 |
| 副作用 | 无（不修改任何变量） |

**输出格式**：纯十进制，无前导零。
- 有符号类型（`int8`~`int64`）：负数带 `-` 前缀，非负数无 `+` 前缀
- 无符号类型（`uint8`~`uint64`）：纯数字，无 `u` 后缀

**静态检查**（Analyzer）：
- 若 `operand` 为变量：检查变量已定义，且变量类型与 `type` 一致
- 若 `operand` 为字面量：检查字面量值在 `type` 可表示范围内

### 11.2 `writeln(type, operand)` — 换行输出

| 项目 | 说明 |
|------|------|
| 语法 | `writeln(int32, x)`、`writeln(uint8, sum)` |
| 功能 | 与 `write` 相同，但在输出末尾追加换行符 `\n` |
| 操作数规则 | 同 `write`（§11.1） |
| 类型约束 | 同 `write`（§11.1） |

`write` 与 `writeln` 配合可灵活控制输出格式：

```text
var x: int32 = 10
var y: int32 = 20
write(int32, x)            ; stdout: "10"
writeln(int32, y)          ; stdout: "20\n"     → 终端显示: "1020\n"
```

### 11.3 `read(type, identifier)` — 标准输入

| 项目 | 说明 |
|------|------|
| 语法 | `read(int32, x)`、`read(uint8, count)` |
| 功能 | 从 stdin 读取一个十进制整数，存入已定义的变量 `identifier` |
| 目标变量 | **必须已通过 `var` 声明**，且声明的类型必须与 `type` 一致 |
| 输入格式 | 十进制整数；`int*` 类型支持可选 `-` 前缀；跳过前导空白 |
| 错误处理 | 非法输入 / 数值超出类型范围 / 非预期 EOF → **I/O 错误**（§10） |

**输入规则**：
1. 跳过前导空白（空格、制表符、换行符、回车符）
2. 对于 `int8`~`int64`：可选 `-` 前缀（必须紧接数字）
3. 对于 `uint8`~`uint64`：**不接受** `-` 前缀
4. 读取连续十进制数字，直到遇到非数字字符或流结束
5. 至少读取一位数字；0 位数字 → I/O 错误
6. 数值超出 `type` 可表示范围 → I/O 错误
7. 遇到 EOF 且未读取到任何数字 → I/O 错误

**静态检查**（Analyzer）：
- 检查 `identifier` 已通过 `var` 定义
- 检查 `identifier` 声明的类型与 `type` 完全一致

**类型一致性示例**：

```text
var a: uint8 = 0
read(int32, a)             ; 静态错误：read 的 type (int32) 与变量 a 的类型 (uint8) 不匹配
```

---

## 12. 后续扩展预留

本文档覆盖 8 种整数类型、五类算术指令（含 `overflow`）、`cast` 转换（含 `overflow`）以及标准 I/O 语句（`write`/`writeln`/`read`）。后续可扩展：

- 布尔类型与比较指令（`eq`、`lt` 等）
- 控制流（`label`、`goto`、`if`）
- 函数定义与调用
- 数组与内存操作
- 浮点类型（`float32`/`float64`）

扩展时应保持 §1.1 设计原则，并将新语法纳入 §2 EBNF。

---

## 13. 参考实现（TC-Compiler）

[TC-Compiler](../README.md) 是本语言的参考实现工程，与本文档的关系如下：

| 组件 | 状态 | 设计文档 | 说明 |
|------|------|----------|------|
| **TC-VM** | 已实现 | [TC-VM 详细设计说明书](./TC-VM详细设计说明书.md)、[TC-VM 命令行参考](./TC-VM命令行参考.md) | 源码经静态分析后直接执行，不生成第二套字节码；支持交互式 REPL |
| **TC-AOT** | 预留 | （待编写） | 将 `.tc` 编译为原生目标代码；构建脚本已独立，实现尚未开始 |

### 13.1 工程布局

```text
docs/                  本文档、VM 详细设计等
src/vm/                TC-VM 源码与独立构建配置（含 REPL）
src/aot/               TC-AOT 预留
tests/                 一致性测试（当前供 VM 使用）
scripts/vm/            VM 测试脚本（含 REPL 回归测试）
scripts/aot/           AOT 测试脚本（预留）
```

### 13.2 一致性测试

`tests/` 目录按错误发生阶段划分，供 VM 回归使用；AOT 实现后应复用或扩展同一套语义用例：

```text
tests/
├── valid/             正例：执行成功
├── errors/static/     静态分析阶段失败
└── errors/runtime/    执行阶段失败
```

### 13.3 规范与实现的边界

- **本文档**定义可观测的语言语义（语法、类型、运算、错误条件）。
- **TC-VM / TC-AOT 设计文档**定义各后端的内部架构、模块划分与实现约定。
- 若实现约定与本文档可观测语义冲突，**以本文档为准**。

---

## 附录 A：与 C 语言整数语义对照

| 类型 | TC 类型 | C 语言等价 |
|------|---------|------------|
| 8 位有符号 | `int8` | `int8_t` |
| 8 位无符号 | `uint8` | `uint8_t` |
| 16 位有符号 | `int16` | `int16_t` |
| 16 位无符号 | `uint16` | `uint16_t` |
| 32 位有符号 | `int32` | `int32_t` |
| 32 位无符号 | `uint32` | `uint32_t` |
| 64 位有符号 | `int64` | `int64_t` |
| 64 位无符号 | `uint64` | `uint64_t` |

| 运算 | TC（以 int32 为例） | C 等价 |
|------|---------------------|--------|
| 加 | `add(int32, a, b)` | `(int32_t)a + (int32_t)b` |
| 加（回绕） | `add(int32, overflow, a, b)` | 同宽无符号运算后 reinterpret |
| 减 / 乘 | `sub` / `mul` | 对应 `-` / `*` |
| 减 / 乘（回绕） | `sub(..., overflow, ...)` 等 | 同宽无符号运算后 reinterpret |
| 整除 / 求余 | `div` / `mod` | `/` / `%` |

| 转换 | TC | C |
|------|----|---|
| strict | `cast(int8, a)` | `(int8_t)a`（不可表示时 TC 报错） |
| overflow | `cast(int8, overflow, a)` | `(int8_t)a`（按位截断） |

**主要差异**：TC 有符号 strict 溢出报错（C 为 UB）；TC `cast` strict 对不可表示转换报错（C 为实现定义/UB）；TC 无符号回绕与 ISO C 一致。

---

## 附录 B：各类型边界常量速查

| 类型 | 最小值 | 最大值 |
|------|--------|--------|
| `int8` | −128 | 127 |
| `uint8` | 0 | 255 |
| `int16` | −32,768 | 32,767 |
| `uint16` | 0 | 65,535 |
| `int32` | −2,147,483,648 | 2,147,483,647 |
| `uint32` | 0 | 4,294,967,295 |
| `int64` | −9,223,372,036,854,775,808 | 9,223,372,036,854,775,807 |
| `uint64` | 0 | 18,446,744,073,709,551,615 |

---

## 附录 C：文档修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.7 | 2026-07-01 | 首版草案：整数类型、算术与 cast 语义 |
| 0.7.1 | 2026-07-01 | 补充 §13 参考实现与 TC-Compiler 工程说明 |
| 0.7.2 | 2026-07-01 | §13 补充 TC-VM 命令行参考链接 |
| 0.8 | 2026-07-01 | 新增三条 I/O 语句 `write`/`writeln`/`read`；补充错误类型 I/O 错误 |
| 0.8.1 | 2026-07-01 | §13 补充 TC-VM REPL 交互模式说明 |
