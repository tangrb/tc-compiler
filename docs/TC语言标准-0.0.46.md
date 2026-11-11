# TC 语言标准

> **版本**：0.0.46
> **规范性引用**：IEEE 754-2019（二进制浮点）。除此之外不引用其它文档。

本文档是 TC 语言的规范性定义：词法、语法、静态语义、抽象机执行语义、可观察行为，以及安全/优化契约。**核心可执行语法只有一种指令形态**：

```text
lhs = op [type] [operands]
```

其中 `lhs` 是目标槽或 `_`（丢弃/仅效果），`op` 是助记符，`type` 是显式类型参数，`operands` 是值。声明、标签不是指令；控制流只由核心 `br`/`br_if`/`switch`/`return`/`trap` 表达。

---

## 目录

- [0. 设计原则与约定](#0-设计原则与约定)
- [1. 概述与抽象机](#1-概述与抽象机)
- [2. 词法](#2-词法)
- [3. 类型系统](#3-类型系统)
- [4. 程序结构、模块与声明](#4-程序结构模块与声明)
- [5. 槽、作用域与确定初始化](#5-槽作用域与确定初始化)
- [6. 指令集](#6-指令集)
- [7. 控制流核心](#7-控制流核心)
- [8. 函数](#8-函数)
- [9. 安全模型](#9-安全模型)
- [10. 优化契约](#10-优化契约)
- [11. 标准-I/O-与运行时](#11-标准-io-与运行时)
- [12. 诊断](#12-诊断)
- [13. 形式化语法ebnf](#13-形式化语法ebnf)
- [14. 开放问题](#14-开放问题)

---

## 0. 设计原则与约定

### 0.1 设计原则

- **伪汇编**：一行一条指令 `lhs = op …`；`_` 表示效果；控制流只有 `label`/`br`/`br_if`/`switch`/`return`/`trap`；声明与指令分离。
- **可读**：命名槽、显式类型、类型首位、`end` 块、缩进仅为风格、`//` 注释/续行、分支模式、`const`/`var` 区分。
- **AI 友好**：单一指令文法 + 规范性 opcode 表；确定性首个诊断；无静默删码；核心/库分离。
- **确定**：抽象机；穷尽 trap/UB；固定初始化与模块解析；首个诊断确定；优化保持可观察行为。
- **安全**：每指令安全类；结构性 `@unsafe`/`unsafe` 块；checked 运算；安全代码不可达 UB。
- **可选优化**：默认 as-if；`@fast` + 承诺；O0/O2/`@fast` 级别；地址/浮点可观察性。

### 0.2 组成

| 层 | 内容 | 规则 |
|---|---|---|
| **核心指令** | `lhs = op`、类型、槽、内存、调用、标签/分支、trap | 一行一指令；语义在本标准 §6–§9 定义 |
| **声明** | `var`、`const`、`struct`、`func`、`import`、`label`、`#program`/`#lib` | 建立名字/布局/签名；不是指令 |
| **运行时库** | 格式化 I/O、字符串、数组/切片、分配、容器、错误处理 | 非核心；由标准库/宿主提供；不得改变核心语义 |

**核心保持小、正交、可 lowering；声明与运行时库不进入指令集。**

### 0.3 规范约定

- **必须 / 禁止 / 仅可 / 不得**：一致性要求；**应当 / 建议**：实现或风格建议；**可以**：实现自由，但不得改变可观察行为。
- **阶段**：LEX（词法）→ SYN（语法）→ IMPORT（导入解析，SEM 子阶段）→ SEM（静态语义）→ CT（常量求值）→ RT（运行时）。
- **首个诊断**：按 §12.2 的阶段顺序优先、模块遍历顺序次之、再源序/专用码选取；报告后停止。运行时按执行顺序取首个 trap。
- **已定义程序**：不存在 UB 的程序。安全代码不得产生 UB；unsafe 代码可以产生 UB。

## 1. 概述与抽象机

### 1.1 语言定位

TC 是**带类型的三地址码（TAC）/ 伪汇编**，面向教学、AI 生成与人类阅读。执行模型是逐条执行：每条指令至多一个 op，操作数只能是值，禁止表达式嵌套。TC 不是通用高级语言：字符串、数组/切片、泛型、闭包、异常、GC、并发、FFI、格式化 I/O 不在核心；字符串/数组/格式化 I/O 属于运行时库，其余不在本标准范围。

### 1.2 抽象机状态

抽象机包含：

| 状态 | 说明 |
|---|---|
| **PC** | 当前函数/程序中的指令位置 |
| **调用栈** | 每个活动调用一个帧；帧含参数槽、局部槽、返回地址/返回槽 |
| **槽（slot）** | 命名的值存储；标量/指针/`fnptr`/结构体；由 `var`、参数、`static var`、`#program` 顶层 `var` 建立 |
| **内存** | 一个字节寻址的地址空间；分为帧区、静态区、设备区、代码区 |
| **标准输入/输出** | `read`/`write`/`writeln` 的字节流 |

- 不暴露寄存器、栈指针、标志位；这些是实现细节。
- 指令按 PC 顺序执行，除非 `br`/`br_if`/`switch`/`call`/`return`/`trap` 改变控制流。
- 每次 `call` 创建新帧；`return` 销毁当前帧并把结果写入调用点的 LHS。
- 抽象机维护调用深度 `depth`；环境定义上限 `D`（≥1）。每次 `call` 先检查：`depth == D` → `TC_RE_STACK_OVERFLOW`；否则 `depth++`。`return` 递减。
- `D` 是环境定义；同一实现的所有优化级别使用同一 `D`；实现必须保证 `D` 个抽象帧可执行（物理栈不足属实现错误）。尾调用优化不得改变抽象深度计数或该 trap。
- 发生 trap 时，当前指令不提交目标槽；程序立即终止；不执行清理代码。

### 1.3 可观察行为

可观察行为包括：

1. 是否通过静态检查，以及**首个规范诊断及其错误码**；
2. 正常运行时的标准输出字节、正常终止，或首个运行时 trap 的错误码；
3. 经 `bitcast` 指针→整数观察到的地址值（仅 unsafe），以及跨对象/悬垂指针比较、`ptrdiff` 的地址相关结果（safe 或 unsafe）；它们**环境定义，且不保证跨优化级别稳定**；
4. 终止/非终止：程序在无 trap、无进一步 I/O 时是否终止；默认模式必须保持该选择（`@fast` 承诺违反的 UB，或仅由 §10.3 许可变换引起的浮点相关控制流/终止除外）。

依赖地址相关结果（§10.5）的控制流、输出、trap 或终止不属于跨优化确定。

**不是可观察行为**：帧内布局、寄存器分配、指令选择、栈帧大小、宿主→目标的翻译方式、优化前后的中间表示。实现可以改变这些，只要保持上述可观察行为（§1.5）。

### 1.4 行为分类（穷尽）

| 类别 | 内容 |
|---|---|
| **规范确定** | 除下列类别外的全部行为：trap 条件、值结果、I/O 字节、终止/非终止、诊断码/顺序 |
| **环境定义** | 地址空间布局与基址、装载地址、端序、设备内存内容、经 `bitcast` 指针→整数观察到的地址值、跨对象/悬垂指针比较与 `ptrdiff` 结果、抽象调用深度上限 `D`、include 根目录、进程退出状态 |
| **实现定义** | 帧内布局、栈帧大小、寄存器分配、指令选择、优化级别；同一实现的不同运行可以不同，但不得改变规范确定行为 |
| **UB（穷尽）** | ① 悬垂指针的解引用、指针算术、`memmove` 或对原对象的访问；② 越出所属对象边界的访问或指针算术；③ 经指针/`memmove` 读取未初始化存储；④ 伪造或签名不符的 `fnptr` 调用；⑤ `@fast` 承诺被违反 |

`@fast_math` 仅放宽 §10.3 许可的浮点变换；由此引起的浮点值、浮点 trap、由这些浮点值决定的控制流/终止不属于规范确定。非浮点值、非浮点指令自身的 trap 条件及与许可浮点变换无关的行为仍按默认规则；I/O 和内存结果只有在控制流确实由被放宽的浮点值决定时才允许不同。

**安全代码不可达 UB。** UB 只可能由 unsafe 指令/调用或 `@fast` 承诺违反产生。对 `const` 取址是静态拒绝（`TC_CE_ADDR_OF_CONST`），不是 UB；`@fast_math` 之外的浮点重关联是实现错误；本语言无并发，不存在数据竞争。

本标准**不承诺**编译期检出悬垂/越界；检出不改变合法程序集合，也不改变 UB 语义。

### 1.5 as-if 与优化基线

实现可以改变内部表示与指令序列，但必须保留：

- 全部静态诊断及其首个错误码；
- 实际执行路径上的 trap 存在性与顺序（默认模式；仅 §10.3 许可变换引起的浮点因果例外；不包含依赖 §10.5 地址相关结果的部分）；
- 标准输出字节与顺序（默认模式；仅 §10.3 许可变换引起的浮点因果例外；不包含依赖 §10.5 地址相关结果的部分）；
- 已定义程序的值结果、浮点 trap 存在性/顺序，以及由浮点值决定的控制流/终止（仅 §10.3 许可变换引起的浮点因果例外；不包含 §10.5 地址相关结果）。
- 终止/非终止行为（默认模式；`@fast` 承诺违反的 UB，或仅由 §10.3 许可变换引起的浮点因果例外除外；不包含依赖 §10.5 地址相关结果的部分）。

**默认模式下不得**因结果未使用而删除可能 trap 的指令；**不得**把 strict 运算静默改成 wrap；**不得**在默认模式下做浮点重关联/FMA/FTZ/DAZ。

### 1.6 源文件与模块

- 一个 `.tc` 文件 = 一个编译单元 = 一个模块；模块名 = 文件名（不含 `.tc`）。
- 文件首条非空、非注释行必须是 `#program` 或 `#lib`；恰好一个。
- 未声明模式、重复模式、模式后出现另一模式 → 语法拒绝。
- 源文件必须是有效 UTF-8；不接受 BOM；U+0000 非法；注释外仅 ASCII。

---

## 2. 词法

### 2.1 字符集

- 注释外只允许 ASCII：字母、数字、空白（空格/Tab/LF/CR）、`_`、`+ - # % = : , ( ) < > ; . @ \ /`。
- `%` 在本标准中没有语义；出现即 `TC_CE_SYNTAX`。
- 注释内允许任意已解码 Unicode 标量值（U+0000 除外）。
- 非法 UTF-8、BOM、U+0000、注释外非 ASCII、未列出 ASCII → `TC_CE_SYNTAX`（LEX）。

### 2.2 注释与续行

```text
// 行注释：从 // 到行尾；可出现在任何位置
; 行注释：仅当 ; 是逻辑行的第一个非空白字符
```

- **`;` 在代码之后出现 → `TC_CE_SYNTAX`**。这消除了“`x = 1; y = 2` 静默删掉 `y = 2`”的陷阱。
- 注释内容不产生 Token。
- **续行**：逻辑行以 `\` 结尾时，下一物理行属于同一逻辑行；`\` 后不得有注释。续行用于长指令/长类型。
- 注释中的 `\` 只是注释内容，不产生续行；续行只作用于代码行尾。
- 空白行、仅注释行合法；不产生指令。

### 2.3 标识符与关键字

```ebnf
identifier = letter , { letter | digit | "_" }
           | "_" , ( letter | digit | "_" ) , { letter | digit | "_" } ;
discard    = "_" ;
letter     = "A"…"Z" | "a"…"z" ;
```

- 区分大小写；ASCII only。
- `_` 单独出现是词法记号 `DISCARD`（丢弃目标），不是 `IDENT`；`_x`、`x_`、`_1`、`__` 是合法标识符。
- 只接受 `IDENT` 的位置（声明名、字段名、参数名、标签名、结构体/函数名、导入名、命名实参）出现 `DISCARD` → `TC_CE_SYNTAX`（SYN）。
- 关键字/助记符不可作标识符。关键字集合按类别：

| 类别 | 词 |
|---|---|
| 模式 | `#program` `#lib` |
| 声明 | `var` `const` `struct` `func` `import` `public` `private` `readonly` `static` `label` `then` `end` |
| 安全 | `unsafe` |
| 类型 | `void` `bool` `int8` `uint8` `int16` `uint16` `int32` `uint32` `int64` `uint64` `isize` `usize` `float32` `float64` `ptr` `fnptr` |
| 字面量 | `true` `false` `nullptr` `inf` `nan` |
| 命名空间 | `Self` |
| 指令 | `add` `sub` `mul` `div` `mod` `neg` `abs` `and` `or` `xor` `not` `shl` `shr` `eq` `ne` `lt` `le` `gt` `ge` `cast` `trunc` `bitcast` `copy` `load` `store` `memmove` `addr` `ptradd` `ptrsub` `ptrdiff` `ptreq` `ptrne` `ptrlt` `ptrle` `ptrgt` `ptrge` `funcaddr` `funceq` `funcne` `call` `call_indirect` `make` `sizeof` `br` `br_if` `switch` `return` `trap` `read` `write` `writeln` |
| 指令后缀 | `.wrap` `.ieee` `.volatile` `.nsw` `.nuw`（作为助记符的一部分，不是独立关键字） |
| 注解 | `@unsafe` `@safe` `@fast` `@fast_math` `@pure` `@readonly` `@nounwind` `@willreturn` `@no_overflow` `@no_alias` `@nonnull` `@align` `@range` `@padding` `@inline` `@noinline` |

> `Self` 保留大写以区分变量 `self`；`Self.x` 表示本模块成员。导入成员写作 `m.x`。

### 2.4 字面量

**整数**：

```ebnf
int_lit = signed_int | unsigned_int ;
signed_int = [ "-" ] , ( dec_lit | hex_lit | bin_lit | oct_lit ) ;
unsigned_int = ( dec_lit | hex_lit | bin_lit | oct_lit ) , ( "u" | "U" ) ;
dec_lit = non_zero , { [ "_" ] , digit } | "0" ;
hex_lit = ( "0x" | "0X" ) , hex , { [ "_" ] , hex } ;
bin_lit = ( "0b" | "0B" ) , ( "0" | "1" ) , { [ "_" ] , ( "0" | "1" ) } ;
oct_lit = ( "0o" | "0O" ) , oct , { [ "_" ] , oct } ;
```

- 无后缀 = 有符号；`u`/`U` = 无符号。
- `-` 只与无后缀字面量组合；`-42u` 非法。
- 十进制不允许前导零（`007` 非法；`0_0` 非法）；单个 `0` 合法。
- `_` 只能在数字之间，不可连续/首尾/前缀内。
- 词法值范围 `0 ≤ N ≤ 2^64−1`；超出 → `TC_CE_LITERAL_LEX_RANGE`（LEX）。
- 进制只改变书写，不改变数学值；`0b1000_0000` 是 128，不能直接进 `int8` 上下文。

**浮点**：

```ebnf
float_lit = [ "-" ] , (
    digit , { digit } , "." , digit , { digit } , [ exp ]
  | digit , { digit } , exp
  ) , [ "f" | "F" ] ;
exp = ( "e" | "E" ) , [ "+" | "-" ] , digit , { digit } ;
```

- 无后缀 `float64`；`f`/`F` → `float32`。
- 必须含 `.` 或 `e`/`E`；`.` 两侧都要有数字（`.5`、`1.` 非法）。
- 精确十进制值直接按 roundTiesToEven 舍入到目标格式；不得经另一浮点类型中转。
- 非零有限值舍入为零/有限值舍入为无穷 → `TC_CE_LITERAL_LEX_RANGE`（LEX）。
- 负零：显式 `-` 且数学值为零 → 负零。
- `inf`/`nan` 是关键字字面量，源类型由期望类型决定；`-inf` 合法；`-nan` 语法拒绝。
- `nan` 是 canonical quiet NaN。
- 浮点字面量只能用于能确定源浮点类型的上下文（浮点类型参数、`cast`/`bitcast`、声明类型、`write`/`read` 的 T）；用于整数/`bool`/指针上下文 → `TC_CE_LITERAL_TYPE`。

**布尔/空指针**：`true`、`false`、`nullptr`。`nullptr` 的所指类型由期望类型决定；`bitcast` 不接受 `nullptr`。`true`/`false` 只用于 `bool`；`nullptr` 只用于 `ptr<T>`/`fnptr<…>`；否则 `TC_CE_LITERAL_TYPE`。

### 2.5 行、空白、缩进

- **一条指令/声明一行**；逻辑行由 LF、CRLF 或 CR 结束。
- Token 间可用空格/Tab；行首缩进**仅是风格**，不参与语法。
- 块由 `then … end` 定义（`func`/`struct`/`unsafe`）；`end` 与块头**逻辑对齐**，但缩进错误不产生诊断。
- 空行/注释行不产生 Token。
- 续行（§2.2）把多个物理行合成一个逻辑行。

### 2.6 注解与属性

```ebnf
annotation = "@" , annotation_name , [ "(" , annotation_args , ")" ] ;
annotation_name = IDENT | "unsafe" | "readonly" ;  /* readonly 是关键字，但可作注解名 */
annotation_args = annotation_arg , { "," , annotation_arg } ;
annotation_arg = INT | IDENT ;
```

- 函数/参数/字段上的注解在 §8/§10/§3.7 定义。
- `@padding(N)`：`N` 是非负十进制字面量（无 `u`、无进制、无负号）；带后缀/进制/负号 → `TC_CE_CONST_FORM`（SEM）。
- 未知注解、注解位置不合法、注解冲突 → `TC_CE_ANNOTATION`（SEM）。

---

## 3. 类型系统

### 3.1 类型分类

| 类别 | 类型 | 说明 |
|---|---|---|
| 整数 | `int8`…`uint64`、`isize`、`usize` | 固定宽度补码；`isize`/`usize` 随目标字长（32/64） |
| 浮点 | `float32`、`float64` | IEEE 754 binary32/binary64 |
| 布尔 | `bool` | 1 字节；`false`=0x00，`true`=0x01；load 规范化 |
| 指针 | `ptr<T>` | 字节地址；宽度 `usize`；`T` 是非 `void` 值类型 |
| 函数指针 | `fnptr<ret, params…>` | 函数入口地址；宽度 `usize` |
| 结构体 | `S` | 具名值类型；字段 `readonly`/`var` |
| 无值 | `void` | 仅返回类型 |

**值类型**：所有标量、`ptr<T>`、`fnptr<…>`、结构体。`void` 不是值类型。核心**没有**数组/字符串/联合/枚举/泛型/闭包/引用/GC 类型。

### 3.2 整数

| 类型 | 位宽 | 范围 |
|---|---|---|
| `int8`/`uint8` | 8 | −2^7…2^7−1 / 0…2^8−1 |
| `int16`/`uint16` | 16 | … |
| `int32`/`uint32` | 32 | … |
| `int64`/`uint64` | 64 | … |
| `isize`/`usize` | 目标字长 | 32 或 64 |

- 补码；无符号非负。
- 有符号 `add/sub/mul/neg/shl` 默认 strict（溢出 trap）；`.wrap` 回绕。
- 无符号 `add/sub/mul/neg` 固定模 2^n；`shl` 默认 strict，`.wrap` 回绕。
- `div` 向零截断；`mod` 余数符号随被除数；除零 trap。
- `div(INT_MIN, −1)`、`abs(INT_MIN)`、`neg(INT_MIN)` strict → 溢出 trap；`mod(INT_MIN, −1)=0`。
- 位宽/符号不同的值禁止隐式转换；必须 `cast`/`trunc`/`bitcast`。

### 3.3 浮点

- `float32`/`float64` 对应 IEEE 754 binary32/binary64；固定 roundTiesToEven。
- 默认禁止 FMA、扩展精度、FTZ、DAZ；每条指令按目标格式立即舍入。
- strict 模式：上溢/下溢/除零/无效操作 trap；优先级 `INVALID → DIV_ZERO → OVERFLOW → UNDERFLOW`。
- `.ieee` 模式：异常不终止，返回 ±inf/NaN；NaN 统一 canonical quiet NaN（正号）。
- `abs`/`neg` 是纯符号位操作，不 trap。
- `mod` 是“商向零截断”的余数（fmod 语义），不是 IEEE remainder。
- 浮点不参与位运算；位模式经等宽 `bitcast`。
- `@fast_math` 区域可以重关联/使用 FMA/FTZ；默认模式禁止。

### 3.4 布尔

- `bool` 占 1 字节；规范值 0x00/0x01。
- `load bool`：字节 0 → `false`，非 0 → `true`（规范化）；`store bool` 写 0x00/0x01。
- `bitcast` 禁止 `bool`；`cast` 允许 bool↔整数/浮点（0/非0）。
- 逻辑 `and/or/xor/not` 严格求值，**不短路**；短路用 `br_if`/`br` 显式分支。

### 3.5 指针

```text
ptr<T>   T 是非 void 值类型；宽度 = usize；地址单位 = 字节
```

- `nullptr` 位模式 0；**地址 0 保留**，`addr`/`funcaddr` 结果非 0。
- 不同 `T` 的指针禁止隐式转换；`bitcast ptr<T>, ptr<U>` 是等宽重标记（safe）；`cast` 不做指针转换。
- `ptr<T>` 与整数：`bitcast` 等宽，**unsafe**。
- 指针与浮点：禁止 `bitcast`。
- 解引用：`load`/`store`；空指针 trap；越界/悬垂 UB（仅 unsafe 代码）。
- 指针算术：`ptradd`/`ptrsub`（unsafe）；`ptrdiff`（safe，字节差）。
- 比较：`ptreq`/`ptrne`/`ptrlt`/`ptrle`/`ptrgt`/`ptrge`（safe；跨对象序为环境定义）。
- `addr T, target`：取槽/字段地址；`const` 无地址；参数槽只读（写穿 trap）。
- **自引用例外**：在定义 `S` 的字段类型中，`ptr<S>`/`ptr<ptr<S>>` 以及 `fnptr<…>` 签名中的 `S` 允许 `S` 尚未完成；其它位置仍要求 `T` 是完整值类型（§3.7）。
- `sizeof(T)`：字节数；常量表达式。

### 3.6 函数指针

```text
fnptr<ret, p1, p2, …>   ret 可为 void；参数必须是值类型；宽度 = usize
```

- 签名逐项等价；参数名不参与。
- `nullptr` 兼容；`funcaddr f` 产生 `fnptr<sig>`；`call_indirect` 是 unsafe 指令。
- `fnptr` ↔ 整数：`bitcast` 等宽，unsafe；`ptr` ↔ `fnptr`、`fnptr` ↔ 浮点：禁止 `bitcast`。
- 跨签名 `bitcast` 禁止；同签名 `fnptr↔fnptr` 是 safe 重解释；`funceq`/`funcne` 只比较同型（或 nullptr）。
- 函数指针不携带 `@unsafe` 信息；所有间接调用都在 unsafe 上下文中进行。

### 3.7 结构体

```text
struct S then
    readonly x: int32
    var y: float64 @padding(4)
end
```

- 具名、名义类型；字段按声明顺序；字段名在同一结构体内唯一 → `TC_CE_DUPLICATE_FIELD`。
- 至少 1 个字段；空结构体 → `TC_CE_EMPTY_STRUCT`。
- 字段修饰符：`readonly`（构造后其结构体值子树在安全代码中不可字段赋值）或 `var`（外层可写且路径无 `readonly` 时可字段赋值）。
- **自引用**：字段类型可以是 `ptr<S>`/`ptr<ptr<S>>`，以及 `fnptr<…>` 签名中的 `S`（`fnptr` 宽度固定为 `usize`，签名不内联 `S`），因为指针/函数指针宽度固定；只有结构体值字段直接/间接出现 `S` 才会造成无限布局 → `TC_CE_STRUCT_SELF_VALUE`。
- **前向引用**：字段类型不得引用尚未定义完毕的其它结构体（即使是 `ptr<其它>`）→ `TC_CE_UNDEFINED_NAME`；`S` 自身的指针/`fnptr` 签名自引用是例外。
- 布局：字节单位，字段紧排；`@padding(N)` 在该字段后插入 N 个 0x00 字节；无隐式对齐/尾部填充。
- `sizeof(S)` 返回字节数 = Σ(字段大小 + padding)；布局在 `struct` 声明处由 SEM 计算并缓存，求和中溢出 `usize` → `TC_CE_LAYOUT_OVERFLOW`（SEM）。
- 结构体是值类型：整块赋值/传参/返回复制全部字段与 padding；指针字段复制地址（所指对象共享）。
- **整块赋值可以替换整个值，包括其中的 `readonly` 字段**；字段赋值按路径规则（§6.14）：目标路径上任一 `readonly` 字段都阻止赋值。深度只读作用于结构体值子树，不延伸到 `ptr` 字段所指对象。
- 不支持结构体比较、算术、`cast`/`bitcast`、I/O；按字段操作。
- 字段访问 `x.f`，可嵌套；`x.f` 是值（operand）。
- 构造：`x = make S, v1, v2`；字段按声明顺序，全部必填，类型严格；也可写 `make S, x: v1, y: v2`（名字必须匹配字段且保持声明顺序）。
- 结构体字段全部对外可见（透明记录）；本版本不提供字段可见性/不透明类型。
- 不支持泛型结构体；类型参数不在核心（§1.1）。

### 3.8 void

- 仅作函数返回类型/`fnptr` 返回类型；不是值类型。
- 不得作参数、`var`、`const`、指针所指、`fnptr` 参数、转换目标、运算类型；可作 `fnptr` 返回。
- `ptr<void>` 非法；`fnptr<void>` = 零参零返回；`fnptr<void, T>` = 一参 T、返回 void。

### 3.9 类型等价

- 标量：按关键字。`isize`/`usize` 是独立的字长类型：宽度/范围由目标决定，但不与任何固定宽度类型等价；与固定宽度类型之间必须显式 `cast`/`trunc`/`bitcast`。
- 结构体：名义等价；不同结构体即使字段相同也不同型。
- `ptr<T>`：`T` 等价才等价。
- `fnptr<ret, ps…>`：返回与参数逐项等价；参数名无关。
- 无子类型/隐式加宽/隐式窄化/隐式符号转换。
- 结构体/指针/`fnptr` 的赋值、传参、返回必须严格同型。

### 3.10 常量表达式

`const` 初始化器使用 `const_expr`（可含 `sizeof`）；`@padding(N)` 的 `N` 按 §2.6 单独约束：

- 字面量（整数/浮点/bool/`nullptr`）；
- 此前定义的 `const` 引用；`Self.K`（本模块 `const`，含 `private`）/ `m.K`（必须是 `public const`；跨模块求值/缓存见 §4.10）；
- `const` 结构体字段读取 `<const>.<field>`（逐层；每层中间结果是 const 结构体值）；
- 纯运算：`add/sub/mul/div/mod/neg/abs/and/or/xor/not/shl/shr/eq/ne/lt/le/gt/ge`；
- `cast`/`trunc`/`bitcast`；
- `make S, …`（字段全为 const）；
- `sizeof(T)`。

**禁止**：`var`/参数、`addr`、`load`、`call`、`funcaddr`、`read`、`write`、`unsafe` 操作。 `const` 中的 `bitcast` 仅限数值↔数值（safe）；指针/`fnptr` 的位重解释在常量求值中不可用 → `TC_CE_CONST_FORM`。常量求值失败按原因映射（均为 CT）：

- strict 整数/浮点溢出、strict `shl` 溢出 → `TC_CE_CONST_OVERFLOW`；
- 整数除零、strict 浮点除零（含 `mod` 零除数）→ `TC_CE_CONST_DIV_ZERO`；
- 负移位计数 → `TC_CE_CONST_SHIFT`；
- strict 浮点无效操作/NaN 输入 → `TC_CE_CONST_FLOAT_INVALID`；
- strict 浮点下溢 → `TC_CE_CONST_FLOAT_UNDERFLOW`；
- `cast` 溢出 → `TC_CE_CONST_CAST`。

常量表达式先做名称/类型检查，再做求值。

### 3.11 转换框架

| 形式 | 作用 | 规则 |
|---|---|---|
| `cast T, x` | 数值/bool 转换 | 范围检查；失败 trap `TC_RE_CAST_OVERFLOW`；浮点按 §6.6 |
| `trunc T, x` | 整数窄化 | 仅整数，目标位宽 < 源；保留低位；不检查；目标位宽 ≥ 源 → `TC_CE_TRUNC_WIDTH` |
| `bitcast T, x` | 等宽位重解释 | 类别矩阵见下；类别禁止 → `TC_CE_TYPE_MISMATCH`；类别允许但位宽不等 → `TC_CE_BITCAST_WIDTH` |

**`bitcast` 类别矩阵**（行 = 源，列 = 目标；允许项仍须等宽）：

| 源 \ 目标 | 整数 | 浮点 | `bool` | `ptr<U>` | `fnptr<sig2>` | 结构体 |
|---|---|---|---|---|---|---|
| 整数 | safe | safe | 禁止 | unsafe | unsafe | 禁止 |
| 浮点 | safe | safe | 禁止 | 禁止 | 禁止 | 禁止 |
| `bool` | 禁止 | 禁止 | 禁止 | 禁止 | 禁止 | 禁止 |
| `ptr<T>` | unsafe | 禁止 | 禁止 | safe | 禁止 | 禁止 |
| `fnptr<sig1>` | unsafe | 禁止 | 禁止 | 禁止 | safe（仅同签名） | 禁止 |
| 结构体 | 禁止 | 禁止 | 禁止 | 禁止 | 禁止 | 禁止 |

- `nullptr` 禁止作为 `bitcast` 源或目标。
- 矩阵覆盖所有值类型类别；`void` 不是值类型（§3.8），作为转换目标 → `TC_CE_VOID`；函数、标签、模块不是类型，出现在类型位置按名称解析的既有诊断处理。
- 判定顺序：先查类别矩阵（禁止 → `TC_CE_TYPE_MISMATCH`），再查位宽（允许类别但不等宽 → `TC_CE_BITCAST_WIDTH`）。
- `ptr` 与 `fnptr` 宽度均为 `usize`，故 `ptr↔ptr` 恒等宽、`fnptr↔fnptr` 恒等宽；后者仍只允许同签名。
- 指针重标记只用 `bitcast`；`cast` 不作用于指针。
- `bitcast` 的类别禁止项（`bool`、指针↔浮点、`fnptr`↔浮点、指针↔`fnptr`、跨签名 `fnptr`、结构体、`nullptr`）及 `cast` 作用于指针 → `TC_CE_TYPE_MISMATCH`（SEM）。
- `nullptr` 不需要转换：期望类型为 `ptr<T>`/`fnptr<…>` 时直接使用。
- 字面量在转换操作数位置的源类型见 §6.6.3。

### 3.12 对象、存储期与指针状态

**对象**是带类型和生命期的存储区域：

- 每个 `var`/参数/程序槽/`static var` 是一个对象；
- 结构体值是对象，其每个字段是对应的子对象；
- 宿主分配库创建的块是 `n` 个 `T` 元素组成的对象（`n>=1`；§11.4）；
- 代码区/设备区不是语言对象，访问按 §6.7 的 trap/UB 规则处理。

**生命期**：帧槽从调用进入到返回；程序槽/`static var` 从程序开始到结束；宿主对象由运行时库创建/释放。生命期结束后，指向该对象的指针为**悬垂指针**。

**指针状态**：

| 状态 | 允许 | 禁止 |
|---|---|---|
| **null** | `copy`、比较、`ptrdiff`（按地址 0） | `load`/`store`/`ptradd`/`ptrsub`/`memmove`/`call_indirect`（trap） |
| **live** | 读/写、算术、比较、`memmove` | 超出对象边界 |
| **one-past** | 比较、`ptrdiff`、`ptradd` offset 0、`ptrsub` 回到对象内 | `load`/`store`、`ptradd` 非零、`memmove` |
| **dangling** | `copy`、返回、存入槽/字段、`bitcast ptr↔ptr`、比较、`ptrdiff` | 解引用、`ptradd`/`ptrsub`、`memmove` |

- **悬垂指针是地址值，不是立即 UB**：只有解引用、指针算术、`memmove` 或对原对象的访问才产生 UB；`copy`/返回/比较/`ptrdiff`/`bitcast ptr↔ptr` 定义。
- **对象边界**：`load/store T` 访问的 `[p, p+sizeof(T))` 必须完全位于一个 live 对象/子对象内；否则 UB。
- **`ptradd`/`ptrsub T`**：以 `sizeof(T)` 为步长；结果必须在同一对象的 `[start, one-past]` 内；否则 UB。`ptradd` 的输入为 one-past 时只允许 `n=0`；`ptrsub` 可从 one-past 回到对象内；输入为 dangling 时任何算术都 UB。
- **字段子对象**：字段各自是独立子对象；不能通过一个字段的指针 `ptradd` 进入相邻字段。
- **`memmove T, dst, src, n`**：dst/src 必须分别指向至少 `n` 个 `T` 元素的 live 对象；重叠按临时缓冲；越界/悬垂/未初始化读取 UB；`n` 是 `usize`。
- **`ptrdiff T, p, q`**：同一对象（含 one-past）内为精确字节差；悬垂指针按原地址差（原本同对象则为原字节差，否则环境定义）；跨对象为环境定义；null 视为地址 0；结果不可由 `isize` 表示 → `TC_RE_PTRDIFF_OVERFLOW`。
- **`ptreq/ptrne/ptrlt/le/gt/ge`**：比较地址值；dangling 可比较；跨对象序为环境定义。
- **`addr T, target`**：产生指向 live 对象/子对象的指针；生命期结束后变为 dangling；`const` 不可取址。
- **未初始化存储**：`addr` 可取址；经 `load` 读取未初始化存储是 UB（unsafe 代码）；安全代码不可 `load`。
- **空结构体禁止**（至少 1 个字段），因此不存在零大小对象。
- **宿主对象**：宿主分配对象必须由至少 1 个 `T` 元素组成（`n>=1`）；`n=0` 不创建核心对象，由运行时库报错或返回库级错误。宿主对象与核心对象遵循同一套边界/生命期规则；分配/释放 API 不在核心（§11.4）。

---

## 4. 程序结构、模块与声明

### 4.1 文件、模块、模式

- 一个 `.tc` 文件 = 一个模块；模块名 = 文件名（去 `.tc`），且必须是合法标识符；否则不能被 `import`。
- 首条有效行必须是 `#program` 或 `#lib`；恰好一个。
- `#program` 是入口模块；`#lib` 是库模块，不直接执行。
- 一个程序由一个 `#program` 及其可达 `#lib` 导入组成。

### 4.2 模块层序

**`#lib` 顶层成员必须按层出现**：

| 层 | 内容 | 要求 |
|---|---|---|
| 1 | `#lib` | 恰好一个 |
| 2 | `import` | 连续；可在类型/值之前 |
| 3 | `struct` | 类型定义 |
| 4 | `const` / `static var` | 模块常量/静态存储；初始化器必须是 const 表达式 |
| 5 | `func` | 函数定义；顺序不限 |

**`#program` 顶层**：

| 层 | 内容 | 要求 |
|---|---|---|
| 1 | `#program` | 恰好一个 |
| 2 | `import` | 连续 |
| 3 | `struct` | 类型定义 |
| 4 | 指令/声明体 | `var`/`const` 声明与指令；`var` 是程序槽；无 `func`/`static`/可见性/`Self` |

跨层交错 → `TC_CE_MODULE_LAYER`（SYN）。`#lib` 成员缺 `public`/`private` → `TC_CE_MISSING_VISIBILITY`。`#program` 使用 `func`/`static`/`public`/`private`/`Self`，或 `#lib` 使用顶层 `var`/指令 → `TC_CE_PROGRAM_MODE_MISUSE`。

### 4.3 `#program` 语义

- 顶层 `var`/`const` 是**程序体声明**：`var` 建立程序槽，`const` 是编译期常量；可见性从声明处到程序末尾（源序）。
- 指令按源序执行；遇到 `end of file` 正常终止。
- 顶层可以出现 `label`/`br`/`br_if`/`switch`；`#program` 是一个控制流域。
- 顶层不得定义 `func`；函数只能来自 `#lib`。
- 顶层 `var` 在声明执行后初始化；DI 规则同函数体（§5.4）。
- 每次运行重新初始化程序槽与所有可达库的 `static var`（后者由 const 初始化，见 §4.7）。

### 4.4 `#lib` 语义

- 只允许 `struct`、`const`、`static var`、`func`；不得有顶层指令/顶层 `var`。
- 每个顶层成员必须显式 `public` 或 `private`；无默认值。
- `static var` 在程序准备阶段用 const 初始化；初始化失败是编译期错误（因为只允许 const），不存在运行时静态初始化失败。
- `#lib` 的所有函数签名在分析函数体前收集；函数可以调用后定义的函数；递归允许（§8.4）。
- `#lib` 不自动执行任何代码；需要初始化的库可以提供 `public func init()`，由 `#program` 显式调用。

### 4.5 导入与模块解析

```tc
import m
```

- 解析顺序：**导入文件所在目录 → 环境声明的 include 根目录（按顺序）→ 首个匹配的 `m.tc`**。
- include 根目录属**环境定义**；实现必须文档化。解析是确定的：首个匹配胜出，不产生 `AMBIGUOUS`。
- 目标文件必须是 `#lib`；导入 `#program` → `TC_CE_IMPORT_NOT_LIB`。
- 目标在当前递归栈上 → `TC_CE_CIRCULAR_IMPORT`（循环优先于重复）；同一导入者的导入列表重复引用同一解析目标 → `TC_CE_DUPLICATE_IMPORT`（位置为后一个 `import`）；不同导入者共享同一已解析目标 → 正常复用，不诊断。
- 每个成功发现的模块只执行一次 PARSE，并只保留一份模块静态存储（§4.7）；跨分支复用不重复加入 `module_order`。
- 导入名占用**模块命名空间**；本文件的模块成员/局部名不得与导入名相同 → `TC_CE_IMPORT_NAME_CONFLICT`。
- 访问导入成员必须限定：`m.f`、`m.K`、`m.S`；且成员必须是 `public`。
- 不支持别名/选择性导入/通配导入/重导出。传递依赖不会自动可见；需要时显式 `import`。
- 如果 `public` 签名使用导入类型，客户端必须同样导入该模块；不存在重导出。
- 模块发现：PARSE 阶段按 §12.2.1 递归定位；缺少目标、同一导入者重复/循环记录到 IMPORT，跨分支已解析目标正常复用；LEX/SYN 错误立即报告。导入遍历顺序为根模块 → 按 `import` 源序深度优先，首次访问为准。

### 4.6 可见性

| 修饰符 | 本模块 `Self.x` | 导入 `m.x` |
|---|---|---|
| `public` | 可访问 | 可访问 |
| `private` | 可访问 | `TC_CE_PRIVATE_ACCESS` |

- 可见性只由修饰符决定；与名字字符无关。
- `public static var` 可读可写（`m.x`）；`public const` 只读；`public func` 可调用；`private` 成员只能 `Self.x`。
- `private` **类型** 出现在 `public` 签名、`public struct` 字段、`public const`/`public static var` 的声明类型或初始化器中 → `TC_CE_PRIVATE_TYPE`（SEM）。
- `public` 初始化器引用 `private const` 是允许的（值内联，不泄露名字）；引用 `private` 类型/成员按上述规则检查。
- 结构体字段全部对外可见（透明记录）；本版本不提供字段可见性/不透明类型。

### 4.7 模块常量与静态存储

```tc
public const K: int32 = 42
public static var counter: int32 = 0
```

- `const`：编译期常量；无运行时存储、无地址；使用处内联。
- `static var`：模块静态存储；初始化器必须是 `const_expr`；在程序准备阶段一次性写入；所有导入者共享同一个槽；可写。已解析模块的跨分支复用不会创建第二份静态存储。
- `static var` 不参与运行时初始化顺序问题：初始化器是常量，不需要执行代码。
- `static var` 的 `addr` 在程序生命期内有效。
- 模块 `const`/`static var` 的初始化器**按源序**求值；只可引用更早的 `const`。前向引用/自引用 → `TC_CE_UNDEFINED_NAME`。跨模块求值顺序见 §4.10。
- `const` 的初始化器可以引用更早的 `const`、`sizeof`、`make`（const 字段）；不得引用 `static var`/`var`/参数/函数。

### 4.8 函数命名空间与 `Self`

- 函数名属于**模块命名空间**，不是程序全局；不同模块可以同名。
- 本模块函数在函数体内必须写 `Self.f`；导入函数写 `m.f`；裸函数名在函数体内 → `TC_CE_FUNCTION_SCOPE_ACCESS`。
- 局部 `var`/`const`/参数名可以与本模块函数名/模块成员同名；因为模块成员必须限定访问，不产生歧义。
- 模块成员（`struct`/`const`/`static var`/`func`）之间不得重名 → `TC_CE_DUPLICATE_MEMBER`。
- `Self` 只在 `#lib` 中合法；`#program` 使用 → `TC_CE_PROGRAM_MODE_MISUSE`。

### 4.9 入口与终止

- `#program` 的第一条可执行指令即入口；无特殊 `main`。
- 执行到程序体末尾正常终止；进程退出状态为环境定义。
- `trap` 指令、运行时 trap、`#program` 顶层 `trap` 均终止程序；不执行清理。
- 无 `exit` 指令；需要非零退出码时用 `trap` 或宿主约定。

### 4.10 跨模块常量求值（CT）

CT 阶段以 `module_order`/模块内源序为**启动顺序**，对 `const` 与 `static var` 初始化器做按需求值：

1. 每个常量声明有缓存状态：未求值/求值中/成功/失败。
2. 求值一个初始化器时，遇到 `Self.K`/`m.K`：若目标未求值，先递归求值其声明；成功则使用缓存值，失败则失败归属于目标声明，当前引用处不再产生级联诊断。
3. 初始化器内的常量引用按源文法从左到右依次递归求值；`make` 的命名字段按书写顺序处理。首个引用失败后立即停止，后续引用不再求值。该顺序只决定 CT 依赖求值与诊断，不改变 §3.10 的名称/类型检查阶段；它是 §12.2 模块顺序在 CT 子阶段的例外。
4. 同一常量在一个模块或钻石依赖中只求值一次；成功和失败都缓存。
5. CT 的首个诊断是按需求值过程中遇到的第一个失败，位置为目标声明；失败后停止，不再求值其它常量。
6. 结构体布局与 `sizeof` 大小在 SEM 阶段计算并缓存；普通指令中的 `sizeof` 不参与 CT 求值，布局求和溢出 `TC_CE_LAYOUT_OVERFLOW` 在 SEM 报告，不进入 CT 排序。
7. 模块内源序和跨模块启动顺序保证独立错误的首错确定；依赖求值可以把目标模块提前，这是允许的，启动顺序只在无依赖时决定先后。
8. 常量依赖无环：同模块只能引用更早的 `const`，跨模块只能沿 import DAG 单向引用，因此不会在“求值中”命中自身。

---

## 5. 槽、作用域与确定初始化

### 5.1 槽的种类

| 槽 | 建立方式 | 存储期 | 可写 |
|---|---|---|---|
| 参数 | `func` 形参 | 调用帧 | 否（只读） |
| 局部 `var` | 函数体/`#program` 体 `var` | 帧/程序 | 是 |
| 程序 `var` | `#program` 顶层 `var` | 程序 | 是 |
| `static var` | `#lib` 成员 | 程序 | 是 |
| `const` | 声明 | 无存储 | 否（不可寻址） |

- **没有块级作用域**：`var`/`const` 是函数/程序作用域；`unsafe` 块也不引入新作用域。
- 名字从声明处可见到函数/程序末尾（源序）；不得在声明前使用。
- 同一函数/程序内不得重名（`var`/`const`/参数共享值名字空间；标签不在其中）→ `TC_CE_DUPLICATE_NAME`。
- 标签有独立的标签命名空间：同一函数/程序内标签名与值名可以相同；值位置解析值，分支位置解析标签。
- 局部名可以与模块成员同名；模块成员必须 `Self.`/`m.` 限定。
- 类型名与值名分属不同命名空间；局部值名可以与结构体名相同（类型位置解析类型，值位置解析值）。

### 5.2 `var` 与 `const`

```tc
var x: int32 = add int32, a, b
const K: int32 = 42
```

- **`var` 必须带初始化器**；缺 `=` 或初始化器 → `TC_CE_VAR_MISSING_INIT`。
- `var` 的初始化器必须是一个 op（`copy T, …`、计算、`call`、`read`、`make` 等）；`const` 初始化器是 `const_expr`，允许字面量。
- `var` 执行后槽初始化为该值；再次执行同一 `var`（循环/跳转）会重新初始化。
- `const` 必须带 `const_expr`；编译期求值；无存储、无地址；不得作为赋值目标。
- `const` 不参与运行时 DI；它不属于“读取前必须初始化”的槽。
- `const` 的可见性从声明处到作用域末尾；前向引用/自引用 → `TC_CE_UNDEFINED_NAME`。

### 5.3 参数

- 按值传递；只读；在函数入口已初始化。
- 参数不得赋值、不得作为 `read` 目标 → `TC_CE_PARAMETER_ASSIGNMENT`。
- `addr` 参数合法，得到只读参数槽地址；经该地址 `store` → `TC_RE_WRITE_PROTECTED`。
- 指针/结构体参数按值复制；结构体的指针字段复制地址（所指对象共享）。

### 5.4 确定初始化（DI）

- 每个 `var` 槽在函数/程序入口处于**未初始化**。
- `var x: T = …` 成功执行后，`x` 在该路径上变为已初始化。
- 读取 `x`、把 `x` 作为赋值目标、把 `x` 作为 `read` 目标，均要求在该程序点**所有可达路径**上已初始化；否则 `TC_CE_UNINITIALIZED`。
- 分支汇合取合取：所有前驱都已初始化才算已初始化。
- 循环：循环体回边按前驱处理；`var` 在循环体内重新执行会重新初始化同一槽。
- `param`/`static var` 入口即已初始化。
- `const` 不参与。
- 不可达代码不参与 DI，但仍必须通过词法/语法/名称/类型检查；本版本**不把不可达代码当错误**（§12.4）。

### 5.5 地址与 `const`

- `addr T, x` 只接受 `var`/参数/`static var`/字段路径；`const` 无地址 → `TC_CE_ADDR_OF_CONST`。
- `addr` 结果：`var` → 可写帧/静态地址；参数 → 只读参数地址；`static var` → 静态地址。
- 对 `var` 取址不改变其可写性；对参数取址写穿是运行时 `TC_RE_WRITE_PROTECTED`。
- 局部 `var`/参数地址在函数返回后悬垂；`static var`/程序 `var` 的地址在程序生命期内有效。悬垂指针的 `copy`/返回/存入/比较/`ptrdiff`/`bitcast ptr↔ptr` 定义；解引用、指针算术、`memmove` 是 UB（§3.12）。

### 5.6 名字解析

函数/程序体内裸标识符按以下顺序解析：

1. 当前函数/程序的 `var`/`const`/参数（从声明处可见）；
2. 若未命中，检查是否为本模块成员名：是 → `TC_CE_FUNCTION_SCOPE_ACCESS`（必须 `Self.`）；
3. 否则 → `TC_CE_UNDEFINED_NAME`。

作为赋值目标时同样按本节解析：裸本模块函数名 → `TC_CE_FUNCTION_SCOPE_ACCESS`；`Self.f`/`m.f` 已解析为函数 → `TC_CE_TYPE_MISMATCH`；未解析 → `TC_CE_UNDEFINED_NAME`。

`Self.x` 只解析本模块成员；`m.x` 只解析导入成员；字段访问 `x.f` 先解析 `x`，再按结构体字段解析。

---

## 6. 指令集

### 6.1 统一指令形态

```text
instruction := lhs "=" op [arg { "," arg }]
lhs         := assignable | DISCARD
assignable  := identifier { "." identifier }
             | "Self" "." identifier { "." identifier }
op          := 助记符 [ "." 模式/修饰 ]
arg         := type | value | label | function | struct | named_arg
```

规则：

1. **`=` 是每条指令的一部分**；`lhs` 为可赋值目标（槽/字段/`Self.x`/`m.x`）或 `_`（无结果/仅效果/控制）。
2. **一条指令一个 op**；操作数是值，不得嵌套 op/call。
3. 类型是**参数列表中的一种**，由 opcode 表决定它出现在哪个位置；有的 op 无类型（`br`/`return`/`trap`），有的首项是函数名/标签/结构体名/类型（由 opcode 表定义）。语法层不强制“类型永远第一”，但 §6.2 的表中类型参数写作第一个参数。
4. `copy T, x` 是复制指令；赋值/初始化必须写 `copy` 或其它 op。
5. `var x: T = op …` 的初始化器必须是一个 op（`copy`/计算/`call`/`read`/`make` 等）；`var` 是声明，不是指令；一行仍只含一个 op。
6. 不允许 `x = add T, y, mul T, a, b` 这类嵌套；先落到临时槽。
7. `_` 是独立的丢弃记号 `DISCARD`，不能作为读取操作数，也不能作变量/参数/字段/标签/结构体/函数等声明名；在只接受 `IDENT` 的位置出现 → `TC_CE_SYNTAX`（SYN）。
8. `call` 的实参可以按位置或按名字写；名字必须匹配形参且保持声明顺序（§8.4）。
9. `make` 的字段可以按位置或按名字写；名字必须匹配字段且保持声明顺序（§3.7、§8.4）。
10. `switch`/`br` 的目标是同一函数/程序内的标签。

### 6.2 opcode 表（规范性）

> 下表是指令集的**单一事实源**。`d` 表示 LHS；`T` 是类型参数；`_` 表示效果/控制。安全列：`safe` 可在 safe 代码中；`unsafe` 必须处于 `@unsafe` 或 `unsafe` 块；`cond` 表示取决于被调函数。

#### 6.2.1 算术

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `add` | `d = add T, a, b` | 有符号/无符号整数、浮点 | T | pure | 有符号溢出；浮点 strict 异常 | safe |
| `add.wrap` | `d = add.wrap T, a, b` | 有符号整数 | T | pure | 无 | safe |
| `add.ieee` | `d = add.ieee T, a, b` | 浮点 | T | pure | 无（返回 inf/NaN） | safe |
| `sub` / `sub.wrap` / `sub.ieee` | 同上 | 同上 | T | pure | 同上 | safe |
| `mul` / `mul.wrap` / `mul.ieee` | 同上 | 同上 | T | pure | 同上 | safe |
| `div` | `d = div T, a, b` | 整数/浮点 | T | pure | 除零；整数 `INT_MIN/−1`；浮点 strict 异常 | safe |
| `div.ieee` | `d = div.ieee T, a, b` | 浮点 | T | pure | 无 | safe |
| `mod` | `d = mod T, a, b` | 整数/浮点 | T | pure | 除零；浮点 strict 异常 | safe |
| `mod.ieee` | `d = mod.ieee T, a, b` | 浮点 | T | pure | 无 | safe |
| `neg` | `d = neg T, a` | 整数/浮点 | T | pure | 有符号 `INT_MIN`；无符号按 `0−a mod 2^n` | safe |
| `neg.wrap` | `d = neg.wrap T, a` | 有符号整数 | T | pure | 无 | safe |
| `abs` | `d = abs T, a` | 整数/浮点 | T | pure | 有符号 `INT_MIN` | safe |
| `shl` | `d = shl T, a, k` | 整数 | T | pure | 负计数；结果不可表示（非零 `a` 的 `k>=n` 必溢出） | safe |
| `shl.wrap` | `d = shl.wrap T, a, k` | 整数 | T | pure | 负计数 | safe |
| `shr` | `d = shr T, a, k` | 整数 | T | pure | 负计数 | safe |

> `.nsw`/`.nuw` 是 `@fast` 内的优化承诺后缀（§10.2），不改变默认语义；不在本节表单列。

#### 6.2.2 位运算与逻辑

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `and` / `or` / `xor` | `d = and T, a, b` | 整数、`bool` | T | pure | 无 | safe |
| `not` | `d = not T, a` | 整数、`bool` | T | pure | 无 | safe |

- `T=bool` 时是逻辑运算；`T=整数` 时按位。
- 严格求值，**不短路**；短路用 `br_if`/`br` 显式分支。

#### 6.2.3 比较

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `eq` / `ne` / `lt` / `le` / `gt` / `ge` | `d = eq T, a, b` | 整数/浮点；`eq`/`ne` 也接受 `bool` | `bool` | pure | 无 | safe |

- 浮点 NaN 语义：`eq/lt/le/gt/ge` 对 NaN 返回 false，`ne` 返回 true。
- 整数按数学值；无符号按非负值；`bool` 只支持 `eq`/`ne`。
- 指针/`fnptr` 不使用本组；用 §6.2.5/§6.2.6。

#### 6.2.4 转换

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `cast` | `d = cast T, x` | 数值/bool | T | pure | 范围不可表示 → `TC_RE_CAST_OVERFLOW` | safe |
| `trunc` | `d = trunc T, x` | 整数→更窄整数 | T | pure | 无 | safe |
| `bitcast` | `d = bitcast T, x` | 等宽数值/指针 | T | pure | 无 | 见 §3.11：ptr↔int、fnptr↔int 为 unsafe |

#### 6.2.5 内存与指针

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `load` | `d = load T, p` | `T` 值类型 | T | read | 空指针；越界/悬垂/未初始化 UB；代码区/设备拒绝 → `TC_RE_ADDR_UNAVAILABLE` | unsafe |
| `load.volatile` | `d = load.volatile T, p` | 同上 | T | read/volatile | 同上 | unsafe |
| `store` | `_ = store T, p, v` | `T` 值类型 | — | write | 空指针；只读区；越界/悬垂 UB；代码区/设备拒绝 | unsafe |
| `store.volatile` | `_ = store.volatile T, p, v` | 同上 | — | write/volatile | 同上 | unsafe |
| `memmove` | `_ = memmove T, dst, src, n` | `T` 值类型；`n: usize` | — | read+write | 空指针；只读区；越界/悬垂/未初始化读取 UB | unsafe |
| `addr` | `d = addr T, target` | 目标类型 `T` | `ptr<T>` | pure | 无 | safe |
| `ptradd` | `d = ptradd T, p, n` | `ptr<T>`, `usize` | `ptr<T>` | pure | 空指针；越界/悬垂 UB | unsafe |
| `ptrsub` | `d = ptrsub T, p, n` | `ptr<T>`, `usize` | `ptr<T>` | pure | 空指针；越界/悬垂 UB | unsafe |
| `ptrdiff` | `d = ptrdiff T, p, q` | `ptr<T>`, `ptr<T>` | `isize`（字节差） | pure | 结果不可表示 → `TC_RE_PTRDIFF_OVERFLOW` | safe |
| `ptreq` / `ptrne` | `d = ptreq T, p, q` | `ptr<T>`/`nullptr` | `bool` | pure | 无 | safe |
| `ptrlt` / `ptrle` / `ptrgt` / `ptrge` | 同上 | `ptr<T>`/`nullptr` | `bool` | pure | 无 | safe |
| `sizeof` | `d = sizeof T` | 类型 | `usize`（字节） | pure | 无 | safe |

- `addr` 的 target 是槽/字段路径；`const` 不可取址。`addr` 产生指向 live 对象/子对象的指针；对象生命期结束后指针悬垂（§3.12）。
- `load/store` 的 `T` 可为标量/指针/`fnptr`/结构体；对齐不要求；端序环境定义。访问范围必须完全位于一个 live 对象/子对象内；one-past/悬垂/越界访问 UB；未初始化存储的读取 UB。
- `memmove` 按 `T` 元素复制，重叠等价于先临时缓冲；`n` 是元素数；dst/src 必须指向至少 `n` 个 `T` 元素的 live 对象；越界/悬垂 UB。
- 指针算术按字节：`p ± n × sizeof(T)`；结果必须位于同一对象的 `[start, one-past]` 内；`ptradd` 从 one-past 只允许 `n=0`，`ptrsub` 可从 one-past 回到对象内；dangling/越界算术 UB。

#### 6.2.6 函数

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `funcaddr` | `d = funcaddr f` | 函数名 | `fnptr<sig>` | pure | 无 | safe |
| `call` | `d = call f, args` 或 `_ = call f, args` | 直接函数 | 返回类型 | call | 被调函数内的 trap | safe；若 `f` 是 `@unsafe`/`@fast` 则 unsafe |
| `call_indirect` | `d = call_indirect fp, args` | `fnptr<sig>` | 返回类型 | call | 空指针；签名不符/伪造 UB | unsafe |
| `funceq` / `funcne` | `d = funceq T, f1, f2` | `fnptr<…>`/`nullptr` | `bool` | pure | 无 | safe |

- `call` 的 args 可按位置或名字写；名字必须匹配形参且保持声明顺序；`call_indirect` 只按位置。
- `call_indirect` 的 args 按 `fnptr` 签名位置；不支持命名；出现命名实参 → `TC_CE_UNKNOWN_ARGUMENT`。
- `funcaddr` 的目标是 `Self.f`/`m.f`；未解析 → `TC_CE_UNDEFINED_FUNCTION`。
- 函数指针宽度 = `usize`；`nullptr` 兼容；`ptr` ↔ `fnptr` 禁止。

#### 6.2.7 结构体与复制

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `make` | `d = make S, v1, v2, …` | 结构体 `S` | `S` | pure | 无 | safe |
| `copy` | `d = copy T, x` | 任意值类型 | T | pure | 无 | safe |

- `make`：字段按声明顺序，全部必填，类型严格；缺字段 → `TC_CE_MISSING_FIELD`，顺序不符 → `TC_CE_FIELD_ORDER`，重复字段 → `TC_CE_DUPLICATE_FIELD`；也可用 `field: value` 命名形式，名字必须匹配字段且保持声明顺序；`@padding` 字节为 0。
- `copy`：标量/指针/`fnptr` 复制值；结构体整块复制（含 padding）；字段指针复制地址。
- 赋值/初始化必须写 `copy` 或其它 op。

#### 6.2.8 控制

| op | 语法 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|
| `br` | `_ = br L` | — | control | 无 | safe |
| `br_if` | `_ = br_if c, L` | — | control | 无 | safe |
| `switch` | `_ = switch x, L0, …, Ld` | — | control | 无 | safe |
| `return` | `_ = return [x]` | — | control | 无 | safe |
| `trap` | `_ = trap` | — | control | `TC_RE_TRAP` | safe |

- `switch`：`x` 必须为整数（否则 `TC_CE_SWITCH_TYPE`）；`0→L0, 1→L1, …, n−1→Ln−1`；其它（含负数）→ `Ld`（最后一个标签，必须有）。
- `return`：`void` 函数无操作数；非 `void` 必须有操作数，类型严格等于返回类型。
- `trap`：立即以 `TC_RE_TRAP` 终止。
- 标签是声明 `label L:`，不是指令；同一函数/程序的标签命名空间内唯一。

#### 6.2.9 标准 I/O

| op | 语法 | 类型 | 结果 | 效果 | trap | 安全 |
|---|---|---|---|---|---|---|
| `read` | `d = read T` | `T` 标量 | T | input | 非法输入/范围/EOF/读失败 → `TC_RE_IO` | safe |
| `write` | `_ = write T, x` | `T` 标量 | — | output | 写失败 → `TC_RE_IO` | safe |
| `writeln` | `_ = writeln T, x` | `T` 标量 | — | output | 写失败 → `TC_RE_IO` | safe |

- 只支持标量；无格式说明符/字符串/字段整块 I/O；格式化由运行时库提供。
- 输出格式见 §11。

### 6.3 算术语义

**模式矩阵**：

| 运算 | 有符号整数 | 无符号整数 | 浮点 |
|---|---|---|---|
| `add/sub/mul` | strict 或 `.wrap` | 固定模 2^n（无模式） | strict 或 `.ieee` |
| `neg` | strict 或 `.wrap` | 固定模 2^n（无模式） | 纯符号位（无模式） |
| `div/mod` | 无模式 | 无模式 | strict 或 `.ieee` |
| `abs` | 无模式 | 无模式（恒等） | 纯符号位（无模式） |
| `shl` | strict 或 `.wrap` | strict 或 `.wrap` | 非法 |
| `shr` | 无模式 | 无模式 | 非法 |

- 模式不适用于该类型/运算 → `TC_CE_MODE`。
- `shl`/`shr` 的被移位数 `a` 与计数 `k` 必须同为 `T`；不同位宽先 `cast`。计数按 `T` 的数学值；负数 → `TC_RE_NEGATIVE_SHIFT`。
- `shl` 的数学结果是 `a × 2^k`（`a` 按其有符号/无符号数学值解释）；strict 时若结果不能在 `T` 中表示 → `TC_RE_INTEGER_OVERFLOW`；`.wrap` 取低 n 位。
- 有符号 strict 溢出 → `TC_RE_INTEGER_OVERFLOW`；`div(INT_MIN,−1)`/`abs(INT_MIN)`/`neg(INT_MIN)` 同。
- 无符号回绕；`shl` 默认 strict，`.wrap` 截断。
- 整数 `div` 向零；`mod` 余数随被除数；除零 → `TC_RE_DIV_ZERO`。
- `shr` 在 `k<n` 时：signed 为算术右移（补符号），unsigned 为逻辑右移（补 0）。
- `k>=n`：非零 `a` 的 strict `shl` 溢出（`a=0` 时结果为 0）；`shl.wrap` 返回 0；`shr` 返回 0。`shr` 的这个 0 是 TC 的特殊大计数规则，不做符号扩展。
- 浮点 strict：上溢 → `TC_RE_FLOAT_OVERFLOW`、下溢 → `TC_RE_FLOAT_UNDERFLOW`、除零 → `TC_RE_DIV_ZERO`、无效操作 → `TC_RE_FLOAT_INVALID`；优先级 `INVALID → DIV_ZERO → OVERFLOW → UNDERFLOW`。
- 浮点 `.ieee`：返回 ±inf/NaN；NaN canonical quiet NaN；不 trap。
- 浮点 `mod`：商向零截断的余数；`b=0` 的 strict 规则见 §6.3.3。
- 浮点 `abs`/`neg`：只改符号位；NaN/Inf 不 trap。
- 浮点比较：NaN 为 unordered；不 trap。

#### 6.3.1 浮点异常分类（摘要）

| 运算 | 无效操作 | 除零 | 其它 |
|---|---|---|---|
| `add` | NaN 输入；异号 inf 相加 | — | IEEE 加法 |
| `sub` | NaN 输入；同号 inf 相减 | — | IEEE 减法 |
| `mul` | NaN 输入；0×inf | — | IEEE 乘法 |
| `div` | NaN 输入；0/0；inf/inf | 有限非零/±0 | IEEE 除法 |
| `mod` | NaN；被除数 inf；0 mod 0 | 有限非零 mod 0 | 精确余数 |

#### 6.3.2 浮点精度

- 每条指令按 `T` 执行并立即舍入；禁止扩展精度/FMA/FTZ/DAZ。
- 下溢按 tininess-after-rounding；精确非规格化不触发。
- 有限结果舍入为 inf = 上溢。

#### 6.3.3 浮点 `mod`

- 有限 `a`、`b≠0`：`q=trunc(a/b)`，`r=a−q*b`；`r` 在 `T` 中精确表示。
- `r` 符号随 `a`；`r=0` 时保留 `a` 的符号。
- `b=±inf` → 结果按位等于 `a`。
- NaN/`a=inf`/`0 mod 0`：strict `TC_RE_FLOAT_INVALID`；`.ieee` canonical NaN。
- 有限非零 `a`、`b=0`：strict `TC_RE_DIV_ZERO`；`.ieee` canonical NaN。

### 6.4 位运算与逻辑

- 整数：按位；补码解释。
- `bool`：逻辑；严格求值，不短路。
- 无溢出；无模式。
- 浮点/指针不参与。

### 6.5 比较

- 整数按数学值；无符号非负；`bool` 的 `eq`/`ne` 按真值。
- 浮点按 IEEE 普通比较谓词；NaN unordered；不触发 strict trap。
- `eq/ne/lt/le/gt/ge` 的结果是 `bool`；`T` 必须是整数/浮点；`eq/ne` 也接受 `bool`。
- 指针/`fnptr` 用专用 op（§6.2.5/§6.2.6）。

### 6.6 转换与字面量

#### 6.6.1 `cast`

- 整数→整数：范围检查；不可表示 → `TC_RE_CAST_OVERFLOW`。
- 整数→浮点：按 roundTiesToEven；允许精度损失；不产生 inf（现有整数范围）。
- 浮点→整数：向零截断；NaN/inf/越界 → `TC_RE_CAST_OVERFLOW`。
- `float64→float32`：roundTiesToEven；有限值→inf → `TC_RE_CAST_OVERFLOW`；下溢到 0/非规格化合法；NaN 按异类型规则规范化。
- `float32→float64`：有限值无损；NaN 按异类型规则规范化。
- `bool↔数值`：false=0，true=1；数值 0→false，非 0（含 NaN）→true。
- 同类型 `cast` 恒等（保留全部位模式，含 NaN payload）。
- **异类型**浮点→浮点 NaN：canonical quiet NaN，不保留 payload。同类型需要保留位模式时，使用同类型 `cast` 或等宽 `bitcast`；不要用 `bitcast` 完成 `float32↔float64` 的跨类型转换（违反等宽要求 → `TC_CE_BITCAST_WIDTH`）。

#### 6.6.2 `bitcast`

- 源/目标等宽；不改位。
- 类别矩阵见 §3.11：数值↔数值 safe；`ptr↔ptr` safe；同签名 `fnptr↔fnptr` safe；`ptr↔整数`、`fnptr↔整数` unsafe；其余类别组合一律禁止。
- 判定顺序：先类别后位宽。类别禁止（含 `bool`、指针↔浮点、`fnptr`↔浮点、指针↔`fnptr`、跨签名 `fnptr`、结构体、`nullptr`）→ `TC_CE_TYPE_MISMATCH`；类别允许但位宽不等 → `TC_CE_BITCAST_WIDTH`。
- 位模式按抽象位串，不依赖宿主端序。

#### 6.6.3 字面量操作数的源类型

| 上下文 | 字面量 | 源类型 |
|---|---|---|
| `cast` | 无后缀整数 | `int64`；必须在 int64 范围 |
| `cast` | `u` 整数 | `uint64` |
| `cast` | 浮点 | `f`/`F` → float32；否则 float64 |
| `cast` | `inf`/`-inf`/`nan` | 目标必须是浮点类型；源类型 = 目标类型 |
| `bitcast` | 整数 | 源位宽 = 目标位宽；后缀决定符号 |
| `bitcast` | 浮点 | `f`/`F` → float32；否则 float64 |
| `bitcast` | `inf`/`-inf`/`nan` | 目标 32 位→float32；64 位→float64 |
| `bitcast` | `nullptr` | 非法 |

- 普通指令/`copy`/`make`/`call`/`return`/`write` 的字面量以期望类型检查。
- 字面量值超出其源类型或上下文期望类型可表示范围 → `TC_CE_LITERAL_RANGE`（SEM）。
- 非零有限浮点字面量舍入为零/有限值舍入为 inf → `TC_CE_LITERAL_LEX_RANGE`（LEX）。
- 类别/后缀/符号性不符 → `TC_CE_LITERAL_TYPE`（SEM）。

### 6.7 内存语义

- 地址空间字节寻址；地址宽度 `usize`；端序环境定义。对象、生命期、指针状态与 one-past 规则见 §3.12。
- `load T, p` 从 `p` 起读 `sizeof(T)` 字节；`store` 写。
- `bool` 的 load 规范化；store 写 0/1。
- 未对齐访问合法。
- `load.volatile`/`store.volatile`：不得合并/删除/重排相对其它 volatile 访问；用于设备内存。
- 空指针 → `TC_RE_NULL_POINTER`。
- 代码区/区外/设备拒绝 → `TC_RE_ADDR_UNAVAILABLE`。
- 只读区写 → `TC_RE_WRITE_PROTECTED`。
- 越界/悬垂/one-past 访问/未初始化读取 → UB（unsafe 代码）。
- `memmove` 重叠等价于先临时缓冲；`n` 是 `usize` 元素数；空指针 trap；越界/悬垂/未初始化读取 UB；不存在负范围。

### 6.8 指针语义

- `addr T, x`：目标类型必须等于 `T`；结果 `ptr<T>`；`const` 不可取址；生命期结束后指针悬垂（§3.12）。
- `ptradd/sub`：按数学字节偏移 `p ± n*sizeof(T)`；空指针 trap；结果必须位于同一对象的 `[start, one-past]`；`ptradd` 从 one-past 只允许 `n=0`，`ptrsub` 可从 one-past 回到对象内；越界/dangling UB。
- `ptrdiff`：同一对象（含 one-past）为精确字节差；悬垂指针按原地址差（原本同对象为原差值，否则环境定义）；跨对象为环境定义；null 按地址 0；结果不可由 `isize` 表示 → `TC_RE_PTRDIFF_OVERFLOW`；跨对象/悬垂结果不保证跨优化级别稳定（§10.5）。
- 指针比较：按地址整数比较；`nullptr`=0；dangling 可比较；跨对象序环境定义；不保证跨优化级别稳定（§10.5）；不 trap。
- `bitcast ptr↔int`：unsafe；地址值环境定义；不得假设跨优化级别稳定。
- `ptr` 不参与通用算术/位运算/标量 I/O。

### 6.9 调用语义

1. 从左到右读取实参；任一 trap 终止。
2. 创建新帧；参数按值写入；从函数体第一条指令执行。
3. `return` 销毁帧；结果写入调用点 LHS；`_` 表示丢弃/void。
4. 递归允许；`call` 前按抽象调用深度 `D` 检查；达到 `D` → `TC_RE_STACK_OVERFLOW`；尾调用优化不得改变深度计数/trap。
5. `call_indirect`：`fp` 为 `nullptr` → `TC_RE_NULL_FUNCTION_POINTER`；签名不符/伪造 → UB。
6. `call` 的目标是 `Self.f`/`m.f`；`@unsafe`/`@fast` 目标要求 unsafe 上下文。
7. 调用点的 LHS 类型必须严格等于返回类型（或 `_`）；`void` 只能用 `_`。

### 6.10 控制语义

- 标签在函数/程序内建立**独立的标签命名空间**（与值名分离）；`label L:` 声明；分支位置 `br L` 解析标签。
- `br_if c, L`：`c` 为 `bool`；true 跳转。
- `switch x, L0,…,Ld`：整数；越界→`Ld`。
- `return`：见 §6.9。
- `trap`：立即 `TC_RE_TRAP`。
- 非 `void` 函数体不得“落入 end”：CFG 上存在到达函数末尾的路径 → `TC_CE_MISSING_RETURN`；`void` 函数可以自然落到 `end`（隐式返回）；永久 `br` 循环/`trap` 路径合法。
- `#program` 可以落到末尾（正常终止）。

### 6.11 标准 I/O 语义

- `read T`：跳过前导 ASCII 空白（`0x09–0x0D`、`0x20`），读一个完整 Token；非法/范围/EOF → `TC_RE_IO`。
  - 整数：可选 `-`（仅 signed）+ 十进制数字；无前导 `+`/进制前缀/分隔符。
  - `bool`：`true`/`false`（小写）。
  - 浮点：十进制/小数点/指数；`inf`/`-inf`/`nan`；不接受 `f` 后缀/`-nan`/`+`；`nan` 产生 canonical quiet NaN（正号）。
- `write T, x`：写该值的确定性文本；`writeln` 追加 LF。
  - 整数：十进制；signed 可带 `-`；无前导零（0 除外）。
  - `bool`：`true`/`false`。
  - 浮点：`%g` 风格，6 位有效数字，小写 `e`；`inf`/`-inf`/`nan`；负零输出 `-0`。
  - 无格式说明符/字符串。
- `write`/`writeln` 要么整条提交，要么 `TC_RE_IO` 且贡献 0 字节；已提交的前缀不回滚。
- 无文件/参数/环境变量 API；由运行时库提供（非核心）。

### 6.12 `copy` 指令

- `copy T, x`：同型值复制；`x` 的类型必须严格等于 `T`；结构体整块（含 padding）；指针/`fnptr` 复制地址。
- 赋值写作 `x = copy T, y`；初始化写作 `var x: T = copy T, y`。
- 字面量/标识符/字段读取/`nullptr` 作为值时必须显式 `copy`；裸值不能直接作为赋值/初始化操作数。
- `x = make S, …` / `x = add …` / `x = call …` / `x = read …` 等直接使用该 op 的结果，不需要 `copy`。
- `copy` 不可能 trap；安全。

### 6.13 `sizeof` 与常量

- `sizeof(T)`：字节数；`usize`；编译期常量；可用于 `const_expr` 和指令。
- 文档中的 `sizeof(T)` 是类型大小记号；指令语法是 `sizeof T`（§6.2.5、§13.4）。
- 结构体大小 = Σ(字段大小 + `@padding`)；无隐式对齐/尾部填充；布局在 `struct` 声明处由 SEM 计算并缓存，求和中溢出 `usize` → `TC_CE_LAYOUT_OVERFLOW`（SEM）。
- `sizeof(void)` → `TC_CE_VOID`；`sizeof` 不作用于函数/标签/模块。
- `sizeof(void)` 在使用处报 `TC_CE_VOID`；结构体布局溢出在声明处报 `TC_CE_LAYOUT_OVERFLOW`；两者同属 SEM，按 §12.2 的模块遍历/源序确定首诊。

| 类型 | `sizeof`（字节） |
|---|---|
| `bool` / `int8` / `uint8` | 1 |
| `int16` / `uint16` | 2 |
| `int32` / `uint32` / `float32` | 4 |
| `int64` / `uint64` / `float64` | 8 |
| `isize` / `usize` / `ptr<T>` / `fnptr<…>` | 目标字长：4 或 8 |
| `S` | Σ(字段 + `@padding`) |

### 6.14 赋值目标与字段赋值

`lhs` 可以是简单槽、`Self.x`/`m.x` 模块静态槽，或字段路径 `x.f`/`x.f.g`。赋值/计算结果写入该目标。

**简单槽**：

| 目标 | 规则 |
|---|---|
| `var` / `static var` / 程序 `var` | 允许；类型严格等于目标声明类型；目标必须已确定初始化 |
| `const` / `Self.K` / `m.K` | `TC_CE_CONST_ASSIGNMENT` |
| 参数 | `TC_CE_PARAMETER_ASSIGNMENT` |
| 函数名 | 裸本模块函数名 → `TC_CE_FUNCTION_SCOPE_ACCESS`；`Self.f`/`m.f` 已解析函数 → `TC_CE_TYPE_MISMATCH`；未解析 → `TC_CE_UNDEFINED_NAME` |
| `_` | 丢弃结果；仍做类型检查 |

**字段目标** `x.f`：

- 基址 `x` 必须是结构体类型的可写槽（`var`/`static var`/程序 `var`）；基址是参数 → `TC_CE_PARAMETER_ASSIGNMENT`；基址是 `const` → `TC_CE_CONST_ASSIGNMENT`。
- 字段必须存在（`TC_CE_UNKNOWN_FIELD`）；中间层必须是结构体（`TC_CE_TYPE_MISMATCH`）。
- 路径规则：从基址开始沿目标路径自左向右检查每个字段；任一字段为 `readonly` → `TC_CE_IMMUTABLE_FIELD`（诊断为路径上首个 `readonly` 字段）；全部为 `var` → 允许。
- 深度只读只作用于结构体值子树；`ptr` 字段所指对象不受该字段 `readonly` 修饰影响，经指针写入按 unsafe 规则。
- `readonly` 是**安全代码的赋值规则**；`addr` 取得的字段指针在 unsafe 代码中经 `store` 可以写穿。这是显式 unsafe 行为，不属于安全代码。
- 右侧结果类型必须严格等于**字段**类型（不是整个结构体类型）。
- 整块赋值 `x = make S, …`/`x = copy S, y` 可以替换整个值（路径规则的唯一例外），包括其中 `readonly` 字段；字段赋值仍禁止。
- 字段赋值不改变外层槽的“已初始化”状态；外层槽本身必须已初始化。
- `addr T, x.f` 取字段地址；字段类型必须等于 `T`。

**`read` 作为右侧**：`x = read T` / `x.f = read T` 合法；目标按上表检查。

**结果丢弃**：任何有结果的指令都可以用 `_` 丢弃结果（包括非 `void` 的 `call`）；仍做完整类型检查。

---

## 7. 控制流核心

### 7.1 唯一控制流形式

TC 的控制流**只有**：

```text
label L:              // 标签声明
_ = br L              // 无条件分支
_ = br_if c, L        // 条件分支
_ = switch x, L0, …   // 多路分支（最后一个是默认）
_ = return x           // 有返回值
_ = return             // void
_ = trap              // 立即 trap
```

- 控制流由上述指令组成。
- `return` 只作为核心指令助记符存在；返回写作 `_ = return x` / `_ = return`。
- 标签在函数/程序内建立独立的标签命名空间（与值名分离）；可前向/后向；不跨函数/程序；可跳过 `var` 声明，由 DI 负责。

### 7.2 标签与无条件分支

```tc
label L:
_ = br L
```

- `label L:` 是声明，不产生运行时动作；同一函数/程序内标签重名 → `TC_CE_DUPLICATE_LABEL`。
- 标签名与值名分属不同命名空间：`label L:` 与 `var L` 可以共存；分支位置解析标签，值位置解析值。
- `_ = br L` 跳到同函数/程序的 `L`；目标标签不存在 → `TC_CE_LABEL_NOT_FOUND`。
- 允许前向/后向跳转；允许跳过 `var` 声明；读取未初始化槽 → `TC_CE_UNINITIALIZED`。
- 不允许跨函数/程序；标签表按函数/程序隔离。

### 7.3 条件分支模式

```tc
_ = br_if c, L_true
_ = br L_false
label L_true:
; true 分支
_ = br L_end
label L_false:
; false 分支
label L_end:
```

- `br_if c, L`：`c` 是 `bool`；true 跳转，false 落到下一条。
- false 分支用 `br` 和标签显式写出。
- 没有 false 分支时，`L_false` 就是 `L_end`：

```tc
_ = br_if c, L_true
_ = br L_end
label L_true:
; true 分支
label L_end:
```

- `c` 不是 `bool` → `TC_CE_CONDITION_TYPE`。
- 条件可以是 `eq`/`lt`/`and`/`not`/`ptreq` 等纯指令的结果；不能是 `call`/`load`/`read` 等效果指令（先落临时槽）。

### 7.4 循环模式

```tc
label L_cond:
; 计算条件 → c
_ = br_if c, L_body
_ = br L_end
label L_body:
; 循环体
_ = br L_cond
label L_end:
```

- `_ = br L_cond` 是“继续”；`_ = br L_end` 是“退出”。
- 循环可能零次执行；没有静态条件折叠；`MISSING_RETURN` 按普通 CFG 处理（`br_if` 有 false 边）。
- 条件必须是显式计算出的 `bool` 值，由前面的指令写入槽。
- 嵌套循环用不同标签；`br` 的目标始终显式。

### 7.5 多路分支

```tc
_ = switch x, L0, L1, L2
label L0:
; x == 0
_ = br L_end
label L1:
; x == 1
_ = br L_end
label L2:
; 默认
label L_end:
```

- `x` 必须是整数（否则 `TC_CE_SWITCH_TYPE`）；`0→L0, 1→L1, …, n−1→Ln−1`；其它（含负数）→最后一个标签 `Ld`。
- 最后一个标签是默认分支，必须有。
- `switch` 是核心指令。

### 7.6 函数返回

```tc
_ = return x
_ = return
```

- 只在函数体内；`#program` 顶层 → `TC_CE_RETURN_OUTSIDE_FUNCTION`。
- 非 `void` 必须带值；`void` 不得带值 → `TC_CE_RETURN_FORM`。
- 操作数类型严格等于返回类型 → `TC_CE_RETURN_TYPE`。
- `return` 是助记符，前面必须有 `_ =`。
- 非 `void` 函数的所有到 `end` 的路径必须被 `_ = return`/`trap`/无出口 `br` 阻止；否则 `TC_CE_MISSING_RETURN`。
- `void` 函数可以自然落到 `end`（隐式返回）。

## 8. 函数

### 8.1 定义

```tc
public @fast @no_overflow @readonly func sum(p: ptr<int32>, n: usize) int32 then
    ...
end
```

- 仅 `#lib` 允许 `func`；`#program` 定义函数 → `TC_CE_PROGRAM_MODE_MISUSE`。
- 语法：`[public|private] {annotation} func name (params) ret then body end`。
- `ret` 是值类型或 `void`；`void` 函数无返回值。
- 函数名在模块内唯一；不得与模块成员重名。
- 函数体是语句序列；`end` 结束；`end` 后不得有同函数代码。

### 8.2 形参

- 形式：`name: type`；可带注解。
- 类型：标量/`ptr<T>`/`fnptr<…>`/结构体；`void` 非法。
- 按值传递；只读；入口已初始化。
- 形参名在签名内唯一 → `TC_CE_DUPLICATE_PARAMETER`。
- 形参名不得被函数体内的 `var`/`const` 重名 → `TC_CE_DUPLICATE_NAME`。
- 不支持默认参数/可变参数/`out`/`ref`。

### 8.3 返回

- 非 `void` 函数：所有到达 `end` 的路径必须已被 `_ = return`/`trap`/无出口 `br` 阻止；否则 `TC_CE_MISSING_RETURN`。
- `_ = return x` 的操作数类型严格等于返回类型；`_ = return` 用于 `void`。
- 返回只写作 `_ = return …`。
- `void` 函数可以自然落到 `end`（隐式返回）。
- 返回结构体按值复制；指针返回复制地址。

### 8.4 调用

- 直接：`d = call Self.f, args` / `d = call m.f, args`；`void` 用 `_`。
- 间接：`d = call_indirect fp, args`；`fp` 是 `fnptr<sig>`；unsafe。
- 实参从左到右读取；类型严格匹配。
- 直接 `call` 可按位置或按 `name: value` 写；名字必须匹配形参、保持声明顺序、全部必填、不得重复/未知；否则 `TC_CE_ARGUMENT_ORDER` / `TC_CE_UNKNOWN_ARGUMENT` / `TC_CE_DUPLICATE_ARGUMENT` / `TC_CE_MISSING_ARGUMENT`。位置实参数量不符 → `TC_CE_ARGUMENT_COUNT`。
- `call_indirect` 只按位置；命名 → `TC_CE_UNKNOWN_ARGUMENT`。
- 递归允许；`call` 前按抽象调用深度 `D` 检查；达到 `D` → `TC_RE_STACK_OVERFLOW`；尾调用优化不得改变深度计数/trap。
- 调用 `@unsafe`/`@fast`/`@fast_math` 函数需要 unsafe 上下文；否则 `TC_CE_UNSAFE_CALL` / `TC_CE_FAST_CALL`。
- 调用点 LHS 必须匹配返回类型；`void` 只能用 `_`。

### 8.5 函数指针

- `d = funcaddr f`；`f` 是 `Self.f`/`m.f`；结果 `fnptr<sig>`。
- `fnptr` 可作参数/返回值/结构体字段；`nullptr` 兼容。
- `call_indirect` 只接受 `fnptr`；不接受整数/`ptr`。
- 伪造/签名不符 → UB；空 → `TC_RE_NULL_FUNCTION_POINTER`。
- `ptr` ↔ `fnptr` 禁止；`fnptr` ↔ 整数 `bitcast` 是 unsafe。
- 函数指针不携带 `@unsafe`；所有间接调用都在 unsafe 上下文。

### 8.6 注解与效果

| 注解 | 位置 | 含义 |
|---|---|---|
| `@unsafe` | 函数 | 函数体是 unsafe 上下文；调用者需要 unsafe 上下文 |
| `@safe` | 函数 | 断言函数体不含 unsafe 指令/调用；与 `@unsafe`/`@fast` 互斥 |
| `@fast` | 函数 | unsafe 上下文 + 优化契约；调用者需要 unsafe 上下文；允许承诺注解 |
| `@fast_math` | `@fast` 函数 | 仅 §10.3 的浮点因果例外：浮点值、浮点 trap、由这些值决定的控制流/终止可能不同；调用者需要 unsafe 上下文 |
| `@pure` | `@fast` 函数 | 无内存效果（普通局部/参数槽值操作除外）；不 `addr`/`load`/`store`/I/O；不调用非 `@pure`；结果只依赖参数值；违反 UB |
| `@readonly` | `@fast` 函数 | 无内存写入（普通局部槽写除外）；可读普通内存，不 I/O、不读 volatile/设备；只调用 `@pure`/`@readonly`；违反 UB |
| `@nounwind` | `@fast` 函数 | 函数体及动态调用范围内无 trap（含栈溢出）；违反 UB |
| `@willreturn` | `@fast` 函数 | 有限步内返回或执行终止 trap；不会无限执行；违反 UB |
| `@no_overflow` | `@fast` 函数 | 函数内 strict 有符号 `add/sub/mul/neg/abs`、`div` 的溢出与全部 strict `shl` 不溢出（不覆盖除零）；违反 UB |
| `@nonnull` | `@fast` 指针形参 | 非空；违反 UB |
| `@no_alias` | `@fast` 指针形参 | 对可达对象的内存访问必须经该参数或其合法派生指针；非派生访问违反 |
| `@align(N)` | `@fast` 指针形参 | 地址按 N 对齐；违反 UB |
| `@range(lo,hi)` | `@fast` 整数形参 | 调用时值在 `[lo,hi]`；`lo/hi` 为同类型常量；违反 UB |
| `@inline`/`@noinline` | 任意函数 | 优化提示；不改变语义 |

- 未标注函数 = safe-checked：不得含 unsafe 指令/调用，除非在 `unsafe` 块中。
- `@safe` 是可选的显式断言；未标注函数已经按 safe 检查。
- `@fast` 函数可以包含 unsafe 指令/调用；不需要 `unsafe` 块。
- `@unsafe`/`@safe`/`@fast` 三者互斥；`@fast_math` 必须与 `@fast` 同用；`@inline`/`@noinline` 互斥。
- `@no_overflow`/`@pure`/`@readonly`/`@nounwind`/`@willreturn` 只在 `@fast` 函数上合法；`@nonnull`/`@no_alias`/`@align`/`@range` 只在 `@fast` 指针/整数形参上合法；否则 `TC_CE_ANNOTATION`。
- `@align(N)` 的 `N` 必须是正的 2 的幂；`@range(lo,hi)` 必须 `lo <= hi`；否则 `TC_CE_ANNOTATION`。
- 注解顺序不限；重复/冲突 → `TC_CE_ANNOTATION`。

### 8.7 不支持的函数形态

- 函数重载、默认参数、可变参数、闭包、嵌套函数、匿名函数、方法、泛型函数。
- 一等函数值；函数只能经 `fnptr` + `funcaddr`/`call_indirect`。
- 异常/`try`/`catch`；错误是 trap。
- 尾调用保证；协程/生成器。

---

## 9. 安全模型

### 9.1 安全代码与不安全代码

- **安全代码**：未标注函数/`@safe` 函数的函数体、`#program` 顶层（不含 `unsafe` 块），且不含 unsafe 指令/调用。
- **不安全上下文**：`@unsafe` 函数体、`@fast` 函数体、`unsafe` 块。
- 安全代码不得含 unsafe 指令/调用；否则 `TC_CE_UNSAFE_OPERATION` / `TC_CE_UNSAFE_CALL`。
- 安全代码不可达 UB；UB 只在 unsafe 上下文（或 `@fast` 承诺违反）中产生。

### 9.2 unsafe 指令/调用（穷尽）

| 类别 | 内容 |
|---|---|
| 内存访问 | `load`、`store`、`load.volatile`、`store.volatile`、`memmove` |
| 指针算术 | `ptradd`、`ptrsub` |
| 间接调用 | `call_indirect` |
| 地址/位串重解释 | `bitcast` 指针↔整数、`fnptr`↔整数 |
| 不安全调用 | 直接 `call` 到 `@unsafe`/`@fast`/`@fast_math` 函数（在 unsafe 上下文外） |

以下**不是** unsafe：`addr`、`ptrdiff`、`ptreq/ne/lt/le/gt/ge`、`funcaddr`、`funceq/funcne`、数值 `bitcast`、`ptr↔ptr` `bitcast`、同签名 `fnptr↔fnptr` `bitcast`、`cast`、`trunc`、全部算术/位/比较/逻辑、`make`、`copy`、`sizeof`、`read`/`write`/`writeln`、`trap`。

### 9.3 `unsafe` 块与 `@unsafe` 函数

- `unsafe then … end`：词法许可；不改变类型/运行时；可嵌套（冗余）。
- `@unsafe func`：整个函数体是 unsafe 上下文；调用者需要 unsafe 上下文或 `unsafe` 块。
- `@safe func`：断言不含 unsafe；若含 → `TC_CE_SAFE_VIOLATION`。
- 未标注函数：按 safe 检查；可以在内部使用 `unsafe` 块。
- `@unsafe`/`@safe`/`@fast` 三者互斥；`@fast_math` 只与 `@fast` 同用。

### 9.4 UB（穷尽）

1. 悬垂指针的解引用、指针算术、`memmove` 或对原对象的访问（copy/返回/比较/`ptrdiff`/`bitcast ptr↔ptr` 定义，不是 UB）。
2. 越出所属对象边界的访问或指针算术（`load`/`store`/`memmove`/`ptradd`/`ptrsub`）。
3. 经指针/`memmove` 读取未初始化存储。
4. 伪造或签名不符的 `fnptr` 调用。
5. `@fast` 承诺被违反（`@no_overflow`/`@no_alias`/`@pure`/`@readonly`/`@nounwind`/`@willreturn`/`@nonnull`/`@align`/`@range`/`.nsw`/`.nuw`）。

**不是 UB**：空指针解引用（trap）、整数溢出（trap）、除零（trap）、cast 溢出（trap）、未初始化变量的直接读取（静态错误）、`ptrdiff` 溢出（`TC_RE_PTRDIFF_OVERFLOW`）、`trap`（定义好的 trap）。经指针读取未初始化存储是 UB（§3.12）。`@fast_math` 之外的浮点重关联是实现错误；数据竞争不存在（无并发）。

### 9.5 写保护与 `const`

- `const` 无存储/无地址；`addr` 报 `TC_CE_ADDR_OF_CONST`。
- 参数只读；`addr` 参数合法；经该地址 `store` → `TC_RE_WRITE_PROTECTED`。
- `static var`/程序 `var` 可写；`addr` 可写。
- 不存在运行时“常量区”；`const` 已内联。

### 9.6 `@fast` 的安全含义

- `@fast` 是**显式 unsafe 优化上下文**：调用者必须处于 unsafe 上下文；函数内可以包含 unsafe 指令。
- 承诺违反是 UB；因此 `@fast` 调用不能出现在安全代码中。
- `@fast` 不改变类型/ABI；只改变优化假设。

### 9.7 安全总结

| 代码 | 可含 unsafe 指令？ | 可调用 `@unsafe`/`@fast`？ | 可产生 UB？ |
|---|---|---|---|
| 安全/未标注（无 `unsafe` 块） | 否 | 否 | 否 |
| 安全/未标注 + `unsafe` 块 | 块内是 | 块内是 | 块内可 |
| `@safe` | 否 | 否 | 否 |
| `@unsafe` | 是 | 是 | 是 |
| `@fast` | 是 | 是 | 是（承诺违反） |

---

## 10. 优化契约

### 10.1 默认：as-if，保持确定/安全

- 优化可以改变指令选择/寄存器分配/帧布局，但不得改变：
  - 静态诊断与首个错误码；
  - 实际执行路径上的 trap 存在性与顺序（包括抽象调用深度产生的 `TC_RE_STACK_OVERFLOW`；不包含依赖 §10.5 地址相关结果的部分）；
  - 标准输出字节与顺序（默认模式；仅 §10.3 许可变换引起的浮点因果例外；不包含依赖 §10.5 地址相关结果的部分）；
  - 已定义程序的值结果（不包含 §10.5 地址相关结果）。
- 不得删除可能 trap 的指令（除非能证明不 trap）；不得把 strict 改成 wrap；不得默认做浮点重关联/FMA/FTZ/DAZ。
- 默认优化（O0/O2）只做语义保持变换；不引入 UB；不得把非终止执行变为终止，也不得把终止执行变为非终止（`@fast` 承诺违反的 UB，或仅由 §10.3 许可变换引起的浮点因果例外除外；不包含依赖 §10.5 地址相关结果的部分）。

### 10.2 `@fast`：显式 UB 优化契约

`@fast` 函数是唯一的“承诺/UB”区域。调用者必须处于 unsafe 上下文（§9.6）。在 `@fast` 函数内可以使用以下承诺：

| 承诺 | 位置 | 含义 | 违反 |
|---|---|---|---|
| `@no_overflow` | 函数 | 函数内 strict 有符号 `add/sub/mul/neg/abs`、`div` 的溢出与全部 strict `shl` 不溢出（不覆盖除零） | UB |
| `.nsw` | 指令后缀 | 该指令有符号不溢出 | UB |
| `.nuw` | 指令后缀 | 该指令无符号不溢出 | UB |
| `@no_alias` | 指针形参 | 对可达对象的内存访问必须经该参数或其合法派生指针；非派生访问违反 | UB |
| `@nonnull` | 指针形参 | 非空 | UB |
| `@align(N)` | 指针形参 | 地址按 N 对齐 | UB |
| `@range(lo,hi)` | 整数形参 | 调用时实参值在 `[lo,hi]`；`lo/hi` 为同类型常量 | UB |
| `@pure` | 函数 | 无内存效果（普通局部/参数槽值操作除外）；不 `addr`/`load`/`store`/I/O；不调用非 `@pure`；结果只依赖参数值 | UB |
| `@readonly` | 函数 | 无内存写入（普通局部槽写除外）；可读普通内存，不 I/O、不读 volatile/设备；只调用 `@pure`/`@readonly` | UB |
| `@nounwind` | 函数 | 函数体及动态调用范围内不发生任何 trap（含 `TC_RE_STACK_OVERFLOW`） | UB |
| `@willreturn` | 函数 | 有限步内返回或执行终止 trap；不会无限执行 | UB |

- `.nsw`/`.nuw` 只允许在 `@fast` 函数内的 `add/sub/mul/shl` 上；其它位置 → `TC_CE_ANNOTATION`。
- 承诺是“优化器可以假设”的契约；不检查，违反即 UB。
- `@fast` 函数可以包含 unsafe 指令；不需要 `unsafe` 块。

#### 10.2.1 承诺的精确定义

- **`@pure`**：函数体无**显式内存效果**。普通局部槽和参数槽的值读写（`var`、`copy`、算术、`make`、`call` 参数等）不构成内存效果；`load`/`store`/`memmove`、全局/静态/程序/宿主/设备/volatile 访问构成内存效果。不得使用 `addr`；不执行 I/O；不调用非 `@pure`；结果只依赖各值参数的**值**（标量、`fnptr`、结构体按值；指针本身的值）。结构体字段读取属于按值读取；指针所指内容不可读；经 `fnptr` 间接调用时，被调函数也必须满足 `@pure` 的动态契约。违反 UB。
- **`@readonly`**：函数体无**显式内存写入**；普通局部槽的写入不构成内存写入。可读取普通内存（通过局部/参数槽、指针参数、全局/静态区），但不得 `store`/`memmove`/写全局/静态/程序/宿主/设备/volatile，不得 I/O，不得 `load.volatile`/设备访问；只能调用 `@pure`/`@readonly`。经 `addr` 取得指针后的 `load` 算读取、`store` 算写入。违反 UB。
- **`@no_alias`**：在调用期间，对标注指针参数可达对象的内存访问，必须通过该参数或其合法派生指针完成；通过其它参数、全局/静态/设备/宿主对象取得的非派生指针访问同一对象，属于违反。合法派生沿 `copy`、`bitcast ptr↔ptr`、`ptradd/ptrsub`，以及经派生指针 `load` 取得的嵌套指针传递；仅创建、复制、比较、`bitcast`、`ptradd/ptrsub` 派生指针不构成违反。违反 UB。
- **`@nounwind`**：函数体及其动态调用范围内不发生任何 trap，包括 `trap`、算术/转换/内存 trap、`call_indirect` 空指针和 `TC_RE_STACK_OVERFLOW`。违反 UB。
- **`@range(lo,hi)`**：调用时实参的数学值在 `[lo,hi]` 内；`lo/hi` 必须是与形参同类型的常量；不做运行时检查。违反 UB。
- **`.nsw`/`.nuw`**：指令级承诺；与 `@no_overflow` 同时出现时两者都生效，任一违反即 UB；没有优先级。
- **`@no_overflow`**：函数内直接出现的 strict 有符号 `add/sub/mul/neg/abs`、`div` 溢出与全部 strict `shl` 不溢出；不覆盖除零、内存、转换或浮点 trap。违反 UB。
- **`@willreturn`**：函数在有限步内返回，或执行一个已定义的终止 trap；不得无限执行。`@willreturn` 不承诺无 trap（仍需 `@nounwind`），也不承诺有限时间内的具体步数。违反 UB。
- **`@fast_math`**：不是承诺，而是浮点语义放宽的许可，见 §10.3。
- **`@inline`/`@noinline`**：优化提示，不产生 UB。

### 10.3 `@fast_math`：显式浮点重关联

- 只允许与 `@fast` 同用。
- 允许：重关联、FMA、FTZ/DAZ、放宽 NaN/Inf 假设。
- **仅允许改变由浮点变换引起的浮点值结果、浮点 trap（`TC_RE_FLOAT_*`、浮点 `TC_RE_DIV_ZERO`）的存在性/顺序，以及由这些浮点值决定的控制流/终止**。非浮点值、非浮点指令自身的 trap 条件及与许可浮点变换无关的行为仍按默认规则；I/O 和内存结果只有在控制流确实由被放宽的浮点值决定时才允许不同。
- 调用者必须处于 unsafe 上下文；这是显式 opt-in，不属于默认确定性保证。
- 默认模式（含 `.ieee`）禁止这些变换；`.ieee` 只改变异常处理，不是 fast-math。

### 10.4 优化级别

| 级别 | 语义 |
|---|---|
| **O0** | 不做优化；保持源序/trap 顺序；地址仍环境定义 |
| **O2** | as-if + 语义保持优化；不使用 UB 假设 |
| **`@fast`** | 在 `@fast` 函数内使用承诺/UB 优化；其它区域仍 O2 |
| **`@fast_math`** | 仅 §10.3 的浮点因果例外：允许浮点重关联；浮点值/浮点 trap/由这些值决定的控制流可与默认模式不同 |

- 优化级别是实现/工具链选择，不是语言语义；同一程序在 O0/O2 下的已定义行为必须相同（使用同一 `D` 与同一抽象深度/trap 规则）；但 §10.5 的地址相关结果除外，同一对象内的地址结果仍相同。
- `@fast`/`@fast_math` 是源级 opt-in；不是全局 `-O3` 开关。

### 10.5 地址可观察性

**地址相关结果**包括（依赖它们的控制流、输出、trap 或终止不在跨优化保证内）：

- `bitcast ptr→int` 的结果（unsafe）；
- 跨对象或悬垂指针的 `ptreq`/`ptrne`/`ptrlt`/`ptrle`/`ptrgt`/`ptrge` 结果；
- 跨对象或悬垂指针的 `ptrdiff` 结果。

- 地址相关结果**环境定义，且不保证跨优化级别稳定**；依赖它们的值或输出不属于“跨优化确定”。
- 同一对象（含 one-past）内的指针比较与 `ptrdiff` 是确定的；`nullptr` 的相等比较是确定的；`ptrdiff` 结果不可由 `isize` 表示时 trap（`TC_RE_PTRDIFF_OVERFLOW`）。
- `bitcast ptr→int` 仅 unsafe；打印地址的程序不属于“跨优化确定”的程序。
- 优化可以改变帧布局/静态布局；不得改变不依赖地址相关结果的值、输出和 trap；依赖地址相关结果的控制流、输出、trap 或终止明确排除在跨优化保证之外。

### 10.6 浮点

- 默认：每条指令按目标格式舍入；禁止 FMA/扩展精度/FTZ/DAZ；禁止重关联。
- `.ieee`：只改变异常处理，不改变舍入/重关联规则。
- `@fast_math`：仅 §10.3 的浮点因果例外；允许重关联/FMA/FTZ/DAZ，浮点值、浮点 trap、由这些值决定的控制流可能不同。
- 常量求值与运行时使用同一规则；不得用宿主扩展精度。

### 10.7 别名与效果

- 默认保守：不得跨 `store`/`call`/volatile 访问重排可能观察内存的 `load`。
- `@no_alias` 允许优化器假设不存在经非派生指针的访问；`@pure`/`@readonly` 允许在无潜在别名写的情况下移动读取/调用；删除一个结果未使用的调用还需 `@nounwind`，并且要么函数带 `@willreturn`，要么优化器能证明该调用终止。`@nounwind` 本身不构成终止证明。
- `@nounwind` 允许删除异常边。

### 10.8 优化不得改变什么

- 静态诊断/首个错误码；
- 已执行路径上的 trap 存在性/顺序（默认模式；仅 §10.3 许可变换引起的浮点因果例外；不包含依赖 §10.5 地址相关结果的部分）；
- 标准输出字节/顺序（默认模式；仅 §10.3 许可变换引起的浮点因果例外；不包含依赖 §10.5 地址相关结果的部分）；
- 已定义程序的值结果（仅 §10.3 许可变换引起的浮点因果例外；不包含 §10.5 地址相关结果）；
- 终止/非终止行为（默认模式；`@fast` 承诺违反的 UB，或仅由 §10.3 许可变换引起的浮点因果例外除外；不包含依赖 §10.5 地址相关结果的部分）；
- 安全代码的“无 UB”性质（优化不得把安全代码变成 UB）。

---

## 11. 标准 I/O 与运行时

### 11.1 核心 I/O

- 只有 `read T`、`write T, x`、`writeln T, x`；`T` 是标量。
- 无格式说明符、字符串、字段整块 I/O、文件/参数/环境 API。
- `write`/`writeln` 的整条输出要么全部提交，要么 `TC_RE_IO` 且贡献 0 字节；已提交前缀不回滚。
- `read` 失败不修改目标（因为 `read` 以 LHS 为目标，失败时 LHS 不提交）。

### 11.2 输出格式（确定性）

| 类型 | 格式 |
|---|---|
| 有符号整数 | 十进制，可带 `-`；`0` 输出 `0`；无前导零 |
| 无符号整数 | 十进制；无前导零 |
| `bool` | `true` / `false` |
| `float32`/`float64` | `%g` 风格，6 位有效数字；小写 `e`；`inf`/`-inf`/`nan`；负零 `-0` |

- `%g` 规则：先按 6 位有效数字 roundTiesToEven；十进制指数 `e`；`e < −4` 或 `e >= 6` 用科学计数法，否则定点；删除小数末尾 0 与空小数点。
- 指数至少两位；`e+00`；负零保留符号；NaN 不带符号（即使位模式符号位为 1）。
- 十进制输出以源 binary32/64 的精确数学值为输入，按 roundTiesToEven 舍入；不得经宿主低精度类型。

### 11.3 输入格式

- 跳过前导 ASCII 空白（`0x09–0x0D`、`0x20`）；读一个完整 Token；非法/范围/EOF → `TC_RE_IO`。
- 整数：可选 `-`（仅 signed）+ 十进制数字；无 `+`/进制前缀/分隔符/前导零限制（输入允许前导零）。
- `bool`：`true`/`false`（小写）。
- 浮点：十进制整数/小数/指数；`inf`/`-inf`/`nan`；不接受 `f` 后缀/`-nan`/`+`；小数点两侧要有数字；`nan` 产生 canonical quiet NaN（正号）。
- 浮点输入先解析精确十进制值，再按 roundTiesToEven 到目标格式；非零→0/有限→inf 是 `TC_RE_IO`。
- 输入 Token 必须完整匹配；`12abc` 非法。

### 11.4 运行时库（非核心）

- 格式化输出、字符串、数组/切片、分配、容器、错误处理属于运行时库；它们可以调用核心指令，但不得改变核心语义。
- 运行时库的 API 不在本标准中定义；本标准只保证核心 I/O 的确定性。
- 宿主分配对象必须 `n>=1`；零长度请求不创建核心对象，由库报错或返回库级错误。宿主对象与核心对象遵循同一套边界/生命期规则（§3.12）；分配/释放 API 不在核心。
- 需要文本标签/格式化时，由库提供；核心不引入字符串字面量。

### 11.5 无文件/参数/环境

- 核心无文件 I/O、命令行参数、环境变量、时间、随机数。
- 这些由宿主/运行时库提供；不属于“确定计算”的核心保证。

---

## 12. 诊断

### 12.1 诊断模型

- 静态阶段：PARSE（逐模块 LEX → SYN）→ IMPORT（导入解析/签名）→ SEM → CT。前序阶段有诊断即停止；CT 内部按 §4.10 按需求值。
- 运行时：遇到首个 trap 即终止；trap 有确定的错误码。
- **fail-fast**：静态阶段只报告首个规范诊断；运行时只报告首个 trap。
- **无编译警告**：本标准不定义警告；所有规范诊断都是错误。
- 诊断的**位置**是源文件的行/列/跨度；同一位置多个候选码按“专用码优先”选取。

### 12.2 首个诊断顺序

1. **阶段优先**：PARSE（所有模块 LEX→SYN，含 §12.2.1 的模块发现）→ IMPORT（所有模块的导入解析/签名；`TC_CE_IMPORT_*` 在此阶段报告，码表中仍标为 SEM 子阶段）→ SEM（所有模块）→ CT（所有模块；按 §4.10 按需求值）。前一阶段有诊断即停止。
2. **模块遍历顺序**：每个阶段内，从根模块开始，按 `import` 源序深度优先遍历，首次访问为准；CT 的依赖求值例外见 §4.10。
3. **同模块、同阶段按源序**：行升序；同行列升序；同位置按专用码优先（如 `TC_CE_MISSING_END` 优先于 `TC_CE_SYNTAX`）。
4. **条款内顺序优先**：如浮点异常优先级、负移位计数先行、空指针先于范围检查。
5. **布局诊断归属 SEM**：结构体布局在 `struct` 声明处计算；`TC_CE_LAYOUT_OVERFLOW` 按 SEM 的模块遍历/源序参与首诊排序，不进入 CT 阶段。

#### 12.2.1 模块发现与 PARSE 顺序

PARSE 阶段用以下算法确定模块集合与顺序：

1. 从根模块开始，对当前模块执行 LEX→SYN；成功后加入 `module_order` 并标记已访问。
2. 按该模块 `import` 的源序逐条处理：
   - **目标文件不存在**：记录 `TC_CE_IMPORT_NOT_FOUND`，不递归，继续下一条；
   - **目标已在当前递归栈上**：记录 `TC_CE_CIRCULAR_IMPORT`，不递归，继续下一条（循环优先于重复）；
   - **当前导入者的导入列表已引用同一解析目标**：记录 `TC_CE_DUPLICATE_IMPORT`，不递归，继续下一条；
   - **目标已解析但不在当前栈上**：正常复用；不产生诊断、不重复 PARSE、不重复加入 `module_order`，继续下一条；
   - **新目标**：定位文件并递归执行步骤 1–2。
3. 目标文件存在但模式为 `#program`：仍执行 LEX→SYN；在 IMPORT 阶段记录 `TC_CE_IMPORT_NOT_LIB`。
4. 任何 LEX/SYN 错误立即报告并停止；第 2 步的记录错误延迟到 IMPORT 阶段，不阻止继续发现其它导入。
5. 每个成功发现的模块只 PARSE 一次；复用不改变 `module_order`。所有可达模块 PARSE 成功后，`module_order` 固定；记录的错误归属于发起该导入的模块/`import` 语句，IMPORT 阶段按 `module_order`/源序报告并校验导入，随后 SEM 使用同一顺序；CT 以同一顺序启动，但按 §4.10 按需求值。

例：`root → a → c` 与 `root → b → c` 的 `module_order` 为 `root、a、c、b`；`c` 只 PARSE 一次，`b` 对 `c` 的导入复用，不报重复。

### 12.3 诊断码（完整集）

> 本表是完整集合（65 个 `TC_CE_*` + 15 个 `TC_RE_*`）；正文未列出的码不存在。阶段：LEX/SYN/SEM/CT/RT。

#### 词法/语法

| 码 | 阶段 | 触发 |
|---|---|---|
| `TC_CE_SYNTAX` | LEX/SYN | 非法 UTF-8/BOM/U+0000/注释外非 ASCII/未列出字符；Token 序列不符合语法；代码后 `;`；`DISCARD` 出现在只接受 `IDENT` 的位置 |
| `TC_CE_MISSING_END` | SYN | `func`/`struct`/`unsafe` 缺 `end` |
| `TC_CE_MODULE_LAYER` | SYN | 模块层序交错 |
| `TC_CE_MISSING_VISIBILITY` | SYN | `#lib` 成员缺 `public`/`private` |
| `TC_CE_PROGRAM_MODE_MISUSE` | SYN | `#program` 使用 `func`/`static`/`public`/`private`/`Self`，或 `#lib` 使用顶层 `var`/指令 |
| `TC_CE_VAR_MISSING_INIT` | SYN | `var` 缺 `=` 或初始化器 |
| `TC_CE_LITERAL_LEX_RANGE` | LEX | 整数字面量 > 2^64−1；浮点字面量舍入 0/±inf |
| `TC_CE_ANNOTATION` | SEM | 未知注解/位置非法/注解冲突/承诺不在 `@fast` |
| `TC_CE_OPERAND_COUNT` | SEM | opcode 的操作数数量/形状不符 |

#### 名称/模块

| 码 | 阶段 | 触发 |
|---|---|---|
| `TC_CE_UNDEFINED_NAME` | SEM | 名字未解析到槽/`const`/成员 |
| `TC_CE_DUPLICATE_NAME` | SEM | 同一函数/程序的值名字空间内 `var`/`const`/参数重名 |
| `TC_CE_DUPLICATE_MEMBER` | SEM | 模块成员重名 |
| `TC_CE_DUPLICATE_PARAMETER` | SEM | 形参重名 |
| `TC_CE_FUNCTION_SCOPE_ACCESS` | SEM | 函数体内裸名引用本模块成员 |
| `TC_CE_UNDEFINED_FUNCTION` | SEM | 函数名未解析 |
| `TC_CE_PRIVATE_ACCESS` | SEM | 访问 `private` 成员 |
| `TC_CE_PRIVATE_TYPE` | SEM | `private` 类型出现在 `public` 签名、`public struct` 字段、`public const`/`public static var` 声明类型或初始化器 |
| `TC_CE_IMPORT_NOT_FOUND` | SEM | 找不到 `m.tc` |
| `TC_CE_IMPORT_NOT_LIB` | SEM | 导入目标不是 `#lib` |
| `TC_CE_DUPLICATE_IMPORT` | SEM | 重复导入 |
| `TC_CE_CIRCULAR_IMPORT` | SEM | 自导入/循环导入 |
| `TC_CE_IMPORT_NAME_CONFLICT` | SEM | 导入名与本地名冲突 |
| `TC_CE_DUPLICATE_LABEL` | SEM | 同函数/程序的标签命名空间内重名 |
| `TC_CE_LABEL_NOT_FOUND` | SEM | `br`/`switch` 目标不存在 |

#### 类型/声明/控制

| 码 | 阶段 | 触发 |
|---|---|---|
| `TC_CE_TYPE_MISMATCH` | SEM | 赋值/传参/返回/操作数类型或目标形态不符，含 `bitcast`/`cast` 的类别禁止组合 |
| `TC_CE_LITERAL_TYPE` | SEM | 字面量类别/后缀/符号性与期望不符 |
| `TC_CE_LITERAL_RANGE` | SEM | 字面量值在源类型或期望类型中不可表示 |
| `TC_CE_MODE` | SEM | 运算/类型/模式组合非法 |
| `TC_CE_CONDITION_TYPE` | SEM | `br_if` 条件不是 `bool` |
| `TC_CE_RETURN_TYPE` | SEM | `return` 类型不符 |
| `TC_CE_RETURN_FORM` | SEM | `void`/非 `void` 的 `return` 形态不符 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` | SEM | 函数外 `return` |
| `TC_CE_MISSING_RETURN` | SEM | 非 `void` 函数有路径到 `end` |
| `TC_CE_ARGUMENT_COUNT` | SEM | 位置实参数量不符 |
| `TC_CE_ARGUMENT_ORDER` | SEM | 命名实参顺序不符 |
| `TC_CE_UNKNOWN_ARGUMENT` | SEM | 命名实参未知 |
| `TC_CE_DUPLICATE_ARGUMENT` | SEM | 命名实参重复 |
| `TC_CE_MISSING_ARGUMENT` | SEM | 命名实参缺失 |
| `TC_CE_UNKNOWN_FIELD` | SEM | `make`/字段访问未知字段 |
| `TC_CE_MISSING_FIELD` | SEM | `make` 缺字段 |
| `TC_CE_DUPLICATE_FIELD` | SEM | `make` 重复字段，或结构体字段重名 |
| `TC_CE_STRUCT_SELF_VALUE` | SEM | 结构体值位置直接/间接自引用（无限布局；`fnptr` 签名除外） |
| `TC_CE_EMPTY_STRUCT` | SEM | 结构体没有字段 |
| `TC_CE_LAYOUT_OVERFLOW` | SEM | 结构体布局求和溢出 `usize` |
| `TC_CE_FIELD_ORDER` | SEM | `make` 字段顺序不符 |
| `TC_CE_IMMUTABLE_FIELD` | SEM | 对 `readonly` 字段赋值，或字段赋值路径经过 `readonly` 字段 |
| `TC_CE_CONST_ASSIGNMENT` | SEM | 对 `const`/`Self.K`/`m.K` 赋值 |
| `TC_CE_PARAMETER_ASSIGNMENT` | SEM | 对参数赋值/`read` 目标 |
| `TC_CE_UNINITIALIZED` | SEM | 读取/赋值/`read` 未确定初始化 |
| `TC_CE_ADDR_OF_CONST` | SEM | 对 `const` 取址 |
| `TC_CE_BITCAST_WIDTH` | SEM | `bitcast` 类别允许但位宽不等 |
| `TC_CE_TRUNC_WIDTH` | SEM | `trunc` 目标位宽 ≥ 源 |
| `TC_CE_SWITCH_TYPE` | SEM | `switch` 值不是整数 |
| `TC_CE_VOID` | SEM | `void` 作值类型/参数/`var`/转换目标 |

#### 安全/常量

| 码 | 阶段 | 触发 |
|---|---|---|
| `TC_CE_UNSAFE_OPERATION` | SEM | unsafe 指令不在 unsafe 上下文 |
| `TC_CE_UNSAFE_CALL` | SEM | 调 `@unsafe` 不在 unsafe 上下文 |
| `TC_CE_FAST_CALL` | SEM | 调 `@fast`/`@fast_math` 不在 unsafe 上下文 |
| `TC_CE_SAFE_VIOLATION` | SEM | `@safe` 函数含 unsafe 指令/调用 |
| `TC_CE_CONST_FORM` | SEM | `const` 初始化器含非法形态（`var`/`call`/`addr`/`load` 等），或 `@padding(N)` 的 `N` 不合 §2.6 |
| `TC_CE_CONST_OVERFLOW` | CT | 常量 strict 整数/浮点溢出或 strict `shl` 溢出 |
| `TC_CE_CONST_DIV_ZERO` | CT | 常量整数除零、strict 浮点除零（含 `mod` 零除数） |
| `TC_CE_CONST_SHIFT` | CT | 常量 `shl`/`shr` 计数为负 |
| `TC_CE_CONST_FLOAT_INVALID` | CT | 常量 strict 浮点无效操作/NaN 输入 |
| `TC_CE_CONST_FLOAT_UNDERFLOW` | CT | 常量 strict 浮点下溢 |
| `TC_CE_CONST_CAST` | CT | 常量 `cast` 溢出 |

#### 运行时

| 码 | 触发 |
|---|---|
| `TC_RE_INTEGER_OVERFLOW` | 有符号 strict 溢出；`div(INT_MIN,−1)`/`abs`/`neg`/strict `shl` |
| `TC_RE_DIV_ZERO` | 整数除零；strict 浮点除零 |
| `TC_RE_NEGATIVE_SHIFT` | 移位计数为负 |
| `TC_RE_FLOAT_OVERFLOW` | strict 浮点上溢 |
| `TC_RE_FLOAT_UNDERFLOW` | strict 浮点下溢 |
| `TC_RE_FLOAT_INVALID` | strict 浮点无效操作/NaN 输入 |
| `TC_RE_CAST_OVERFLOW` | `cast` 结果不可表示 |
| `TC_RE_NULL_POINTER` | `load`/`store`/`memmove`/`ptradd`/`ptrsub` 空指针 |
| `TC_RE_PTRDIFF_OVERFLOW` | `ptrdiff` 结果不可由 `isize` 表示 |
| `TC_RE_WRITE_PROTECTED` | 只读参数槽写穿 |
| `TC_RE_ADDR_UNAVAILABLE` | 代码区/区外/设备拒绝 |
| `TC_RE_STACK_OVERFLOW` | 抽象调用深度达到环境定义上限 `D` |
| `TC_RE_NULL_FUNCTION_POINTER` | `call_indirect` 空指针 |
| `TC_RE_IO` | `read`/`write`/`writeln` 失败 |
| `TC_RE_TRAP` | `trap` 指令 |

### 12.4 不可达代码

- 本标准**不把不可达代码当错误**。
- 不可达代码仍必须通过词法/语法/名称/类型检查；不参与 DI。
- `MISSING_RETURN` 只按 CFG 的可达路径判定；不可达路径忽略。
- 没有静态条件折叠；`br_if` 的常量条件在 CFG 中仍视为可达；类型/DI 仍检查。

---

## 13. 形式化语法（EBNF）

### 13.1 约定

- 空行、纯注释行、续行由词法层处理；语法层的 `NL` 只表示语句/声明结束。
- `IDENT`/`INT`/`FLOAT`/`DISCARD` 是 §2 的 Token；`opcode` 必须是 §6.2 的助记符（可带 `.wrap`/`.ieee`/`.volatile`/`.nsw`/`.nuw`）。
- `op_form` 是通用形态；opcode 表决定字段是 type/value/label/function/struct。字段不匹配 → `TC_CE_OPERAND_COUNT`/`TC_CE_TYPE_MISMATCH`。
- `%` 不参与语法；注释外的 `%` 是 `TC_CE_SYNTAX`。
- `Self` 只在 `#lib`；`#program` 使用 → `TC_CE_PROGRAM_MODE_MISUSE`。

### 13.2 词法（摘要）

```ebnf
NL        = "\n" | "\r\n" | "\r" ;
IDENT     = letter , { letter | digit | "_" }
          | "_" , ( letter | digit | "_" ) , { letter | digit | "_" } ;
DISCARD   = "_" ;
INT       = ( [ "-" ] , ( dec | hex | bin | oct ) ) | ( ( dec | hex | bin | oct ) , ( "u" | "U" ) ) ;  /* 负号不与 u/U 组合 */
FLOAT     = [ "-" ] , ( digit , { digit } , "." , digit , { digit } , [ exp ]
                      | digit , { digit } , exp ) , [ "f" | "F" ] ;
INF_LIT   = [ "-" ] , "inf" ;
exp       = ( "e" | "E" ) , [ "+" | "-" ] , digit , { digit } ;
dec       = non_zero , { [ "_" ] , digit } | "0" ;
hex       = ( "0x" | "0X" ) , hex_digit , { [ "_" ] , hex_digit } ;
bin       = ( "0b" | "0B" ) , ( "0" | "1" ) , { [ "_" ] , ( "0" | "1" ) } ;
oct       = ( "0o" | "0O" ) , ( "0"…"7" ) , { [ "_" ] , ( "0"…"7" ) } ;
```

- 注释：`//` 到行尾；`;` 仅逻辑行首。代码后 `;` → `TC_CE_SYNTAX`。
- 续行：行尾 `\` 合并下一物理行；`\` 后不得有注释。
- 缩进：仅风格，不进入语法。

### 13.3 程序/声明

```ebnf
program        = ( program_module | library_module ) , EOF ;
program_module = "#program" , NL , import_region , program_type_region , program_body ;
library_module = "#lib" , NL , import_region , library_type_region ,
                 library_value_region , function_region ;

import_region  = { import_decl } ;
import_decl    = "import" , IDENT , NL ;

program_type_region = { struct_def } ;
library_type_region = { visibility , struct_def } ;
program_body   = { statement } ;
library_value_region = { visibility , ( const_decl | static_var_decl ) } ;
function_region = { func_def } ;

struct_def     = "struct" , IDENT , "then" , NL , { field_def } , "end" , NL ;
field_def      = ( "readonly" | "var" ) , IDENT , ":" , type ,
                 [ "@padding" , "(" , INT , ")" ] , NL ;
const_decl     = "const" , IDENT , ":" , type , "=" , const_expr , NL ;
static_var_decl= "static" , "var" , IDENT , ":" , type , "=" , const_expr , NL ;

func_def       = visibility , { annotation } , "func" , IDENT ,
                 "(" , [ param_list ] , ")" , ret_type , "then" , NL ,
                 { statement } , "end" , NL ;
param_list     = param , { "," , param } ;
param          = { annotation } , IDENT , ":" , type ;
ret_type       = type ;
visibility     = "public" | "private" ;
annotation     = "@" , annotation_name , [ "(" , annotation_args , ")" ] ;
annotation_name = IDENT | "unsafe" | "readonly" ;  /* @safe/@fast/... 是 IDENT；@unsafe/@readonly 用关键字 */
annotation_args = annotation_arg , { "," , annotation_arg } ;
annotation_arg = INT | IDENT ;
```

### 13.4 语句/指令

```ebnf
statement      = instruction | var_decl | const_decl | label_def
               | unsafe_block ;

var_decl       = "var" , IDENT , ":" , type , "=" , op_form , NL ;
instruction    = lhs , "=" , op_form , NL ;
lhs            = assignable | DISCARD ;
assignable     = IDENT , { "." , IDENT }
               | "Self" , "." , IDENT , { "." , IDENT } ;
op_form        = opcode , [ op_arg , { "," , op_arg } ] ;
op_arg         = type | value | label | function_name | struct_name | named_arg ;
named_arg      = IDENT , ":" , value ;
value          = IDENT , { "." , IDENT }
               | "Self" , "." , IDENT , { "." , IDENT }
               | literal | "nullptr" ;
label          = IDENT ;
function_name  = "Self" , "." , IDENT | IDENT , "." , IDENT ;
struct_name    = IDENT | IDENT , "." , IDENT ;
label_def      = "label" , label , ":" , NL ;

unsafe_block   = "unsafe" , "then" , NL , { statement } , "end" , NL ;
const_expr     = value | op_form ;
```

### 13.5 类型

```ebnf
type           = scalar_type | "void" | "ptr" , "<" , type , ">"
               | "fnptr" , "<" , ret_type , [ "," , type , { "," , type } ] , ">"
               | struct_name ;
scalar_type    = "int8" | "uint8" | "int16" | "uint16"
               | "int32" | "uint32" | "int64" | "uint64"
               | "isize" | "usize" | "float32" | "float64" | "bool" ;
literal        = INT | FLOAT | "true" | "false" | INF_LIT | "nan" ;
opcode         = IDENT , [ "." , IDENT ] ;  /* 必须是 §6.2 的助记符/后缀 */
```

### 13.6 规范列表的一致性（维护）

§2.3 的关键字/助记符、§6.2 的 opcode、§12.3 的诊断码、§13.4 的 EBNF 必须保持一致。实现/维护工具应当自动校验：

- opcode 表与关键字表的差集/重复；
- 诊断码表与正文引用、计数；
- EBNF 与正文语法形态（如 `label L:`、`copy`）。

这是维护要求，不改变语言语义；若发现不一致，以 §6.2 的 opcode 表和 §12.3 的完整诊断表为准，并修正其它列表。

---

## 14. 开放问题

以下问题不影响本版本的一致性判定；后续版本可能调整：

1. **`@fast` 承诺的语法粒度**：保持函数级，还是允许指令级（如 `.nsw`/`.nuw`）？当前两者并存，是否需要收敛为一种。
2. **`@fast_math` 的调用边界**：是否改为类型级 effect，使调用者不必进入 unsafe 上下文也能识别浮点语义变化。
3. **地址可观察性**：是否增加 `@stable_address`，让需要打印/比较地址的程序获得跨优化级别的稳定值。
4. **`switch` 的跳转表提示**：是否在核心提供密度/跳转表提示，还是完全交给优化器。
5. **运行时库边界**：字符串/数组/格式化 API 是否、何时进入标准，还是长期留在运行时库。

---

*— 文档结束 —*
