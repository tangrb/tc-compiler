# TC 编译器标准

> **版本**：0.0.46
> **语言规范基线**：[TC语言标准-0.0.46](./TC语言标准-0.0.46.md)（完整诊断集 **80** 码 = 65 个 `TC_CE_*` + 15 个 `TC_RE_*`）
> **作者**：唐荣兵（[yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)）
>
> **规范地位**：本文规定编译器为实现 [TC语言标准-0.0.46](./TC语言标准-0.0.46.md) 所必须遵循的确定性编译阶段、静态验证规则、分析算法、诊断选择顺序与错误码体系。语言语义以语言标准为准；本文只规定**如何**确定性实现这些语义，不得增加、放宽或改写语言的静态接受集与可观察行为。

本文与语言标准的分工是：语言标准定义程序的含义、合法形态、诊断码集合与运行时可观察行为；本文定义编译器的阶段划分、模块遍历顺序、静态检查归属、CFG 与确定初始化算法、调用图与效果摘要、诊断定位与代码生成约束。凡本文与语言标准冲突，以语言标准为准。

### 与语言标准的章节对应

| 本章 | 对应语言标准 | 编译器补充内容 |
| ---- | ------------ | -------------- |
| §1 | [语言标准 §0]、[语言标准 §1]、[语言标准 §12] | 规范化处理管线、13 个实现步骤、模块遍历顺序、首个诊断选择、诊断缓冲、无警告约束 |
| §2 | [语言标准 §2] | UTF-8 解码、唯一 `///` 注释、最长匹配记号化、`DISCARD`、逻辑行与续行、字面量 Token 自身上限、词法诊断定位 |
| §3 | [语言标准 §3]、[语言标准 §6.6] | 单一类型事实源、类型等价、结构体布局与 `sizeof`、转换类别矩阵与位宽检查、常量表达式验证 |
| §4 | [语言标准 §4] | 模块发现与 PARSE 闭包、`module_order`、IMPORT 阶段、依赖拓扑、可见性与成员命名空间、跨模块 CT |
| §5 | [语言标准 §5]、[语言标准 §6.14] | 槽分类、名字解析、赋值目标与字段路径、确定初始化规则与数据流方程 |
| §6 | [语言标准 §6] | 统一指令形态、操作数形状、指令级静态验证、字面量期望类型、诊断定位 |
| §7 | [语言标准 §7]、[语言标准 §12.4] | CFG 构建、可达性、无静态条件折叠、确定初始化固定点、函数末尾检查 |
| §8 | [语言标准 §8] | 签名与参数验证、调用检查顺序、函数指针、注解与效果、调用帧与调用图 |
| §9 | [语言标准 §9] | 安全上下文判定、unsafe 指令与调用穷尽表、`unsafe` 块、UB 清单、写保护 |
| §10 | [语言标准 §10] | as-if 约束、`@fast` 承诺、`@fast_math`、优化级别、地址可观察性、优化验证 |
| §11 | [语言标准 §11] | 核心 I/O 的确定性输入输出、Token 验证、提交规则、I/O 诊断 |
| §12 | [语言标准 §12] | 诊断模型、首诊顺序、完整 80 码表、诊断定位、运行时提交与终止、实现资源错误 |
| §13 | [语言标准 §1.2]、[语言标准 §1.4] | 平台档案、布局与地址、checked build、VM/AOT 一致性验证 |

---

## 1. 概述与编译管线

### 1.1 文档定位

本文是编译器行为的规范性条款。凡影响静态接受集、首个诊断及其错误码、模块解析结果、确定初始化判定、调用图/效果摘要、运行时可观察行为的实现步骤，均必须按本文执行。VM 与 AOT 共享同一套前端静态语义；代码生成可以不同，但不得改变本文规定的静态结果与语言标准规定的可观察行为。

编译器不得：

- 增加语言标准未定义的静态拒绝、警告或错误码；
- 放宽语言标准的语法或静态接受集；
- 以实现资源限制替代语言诊断；
- 因优化级别、目标后端或内部数据结构的遍历顺序改变首个诊断。

### 1.2 能力边界

编译器必须覆盖语言标准的核心能力范围；核心之外的字符串、数组/切片、泛型、闭包、异常、GC、并发、FFI 与格式化 I/O 不属于核心，相关功能由运行时库提供。编译器不得为核心指令引入隐藏的运行时语义。

| 类别 | 编译器必须支持的范围 |
| ---- | -------------------- |
| 类型 | `int8`～`uint64`、`isize`/`usize`、`float32`/`float64`、`bool`、`ptr<T>`、`fnptr<…>`、结构体、`void` 仅作返回类型 |
| 指令 | 算术、位运算与逻辑、比较、`cast`/`trunc`/`bitcast`、`load`/`store`/`memmove`/`addr`/`ptradd`/`ptrsub`/`ptrdiff`/`ptreq`～`ptrge`/`sizeof`、`funcaddr`/`call`/`call_indirect`/`funceq`/`funcne`、`make`/`copy`、`br`/`br_if`/`switch`/`return`/`trap`、`read`/`write`/`writeln` |
| 声明 | `var`、`const`、`static var`、`struct`、`func`、`import`、`label`、`#program`、`#lib` |
| 注解 | `@unsafe`、`@safe`、`@fast`、`@fast_math`、`@pure`、`@readonly`、`@nounwind`、`@willreturn`、`@no_overflow`、`@no_alias`、`@nonnull`、`@align`、`@range`、`@padding`、`@inline`、`@noinline` |
| 控制流 | 只有 `label`、`br`、`br_if`、`switch`、`return`、`trap`；没有 `if`、`while`、`goto`、`break`、`continue` |
| I/O | 仅标量 `read`/`write`/`writeln`；没有格式说明符、字符串或整块 I/O |

### 1.3 编译阶段

编译器采用以下 13 个**实现步骤**。步骤编号是工作分解，不是诊断优先级；诊断优先级由 §1.5 与语言标准 [语言标准 §12.2] 决定。

| 步骤 | 核心任务 | 诊断类阶段 |
| ---- | -------- | ---------- |
| 1 | 读取源文件并做 UTF-8 解码；文件打开失败属于实现/API 层 | PARSE |
| 2 | LEX：字符集、注释、续行、记号化、字面量 Token 自身上限 | PARSE |
| 3 | SYN：按语言标准 EBNF 建立声明/指令结构；结构类语法诊断 | PARSE |
| 4 | 模块发现与 PARSE 闭包：递归定位 `import` 目标，固定 `module_order` | PARSE |
| 5 | IMPORT：解析导入、校验目标模式、收集全部可达模块的函数签名 | IMPORT |
| 6 | SEM 名称：名字解析、绑定、可见性、成员命名空间、标签解析 | SEM |
| 7 | SEM 类型与指令：类型检查、转换、字面量、安全性、注解放置 | SEM |
| 8 | SEM 布局：结构体布局、`sizeof`、静态存储布局 | SEM |
| 9 | SEM CFG：控制流图构建、可达性、确定初始化固定点 | SEM |
| 10 | SEM 返回与调用：返回路径、调用形参/结果类型、函数末尾检查 | SEM |
| 11 | SEM 调用图：调用图与效果摘要，仅用于分析与优化，不作为静态拒绝 | SEM |
| 12 | CT：`const` 与 `static var` 初始化器的按需求值 | CT |
| 13 | 代码生成：生成 VM 指令流或 AOT C 代码 | — |

步骤 1–4 是 PARSE 类的内部工作；步骤 5 是 IMPORT 类；步骤 6–11 是 SEM 类；步骤 12 是 CT 类。步骤 11 的调用图分析不得产生语言标准之外的递归拒绝或可达性诊断。

### 1.4 处理顺序与诊断缓冲

编译器的确定性处理顺序如下：

1. **PARSE 闭包**：从根模块开始，按 `import` 源序深度优先发现模块；每个模块执行 LEX→SYN，成功后加入 `module_order` 并标记已访问。LEX/SYN 错误立即报告并停止；导入目标缺失、循环、重复等记录到 IMPORT 缓冲，不阻止继续发现其它模块。具体算法见 §4.3。
2. **IMPORT 阶段**：全部可达模块 PARSE 成功后，按 `module_order` 与模块内源序解析导入、校验模式、检查名称冲突并收集签名。记录的错误在此阶段统一报告。IMPORT 码在语言标准码表中标记为 SEM 子阶段，但报告时机早于 SEM。
3. **SEM 阶段**：对全部可达模块执行步骤 6–11。所有 SEM 诊断必须缓冲；最终按 `module_order`、模块内源位置、同位置专用码优先的规则选择首个诊断。不得按步骤 6→7→8→9→10→11 的实现顺序抢先报告。
4. **CT 阶段**：按 §4.6 与语言标准 [语言标准 §4.10] 对 `const`/`static var` 初始化器按需求值。只有全部 SEM 诊断无触发后才进入；CT 的首个失败按求值过程中遇到的第一个失败报告，位置为目标声明。
5. **代码生成**：只有 PARSE、IMPORT、SEM、CT 全部成功后执行。

模块的静态布局、签名注册与依赖分析需要“依赖先于使用者”的顺序；该顺序由导入 DAG 的逆向拓扑序确定，同层并列时按 `module_order` 打破平局。**诊断顺序仍使用 `module_order`，不使用依赖拓扑序**。两者不得混用。

### 1.5 首个诊断规则

一个程序同时存在多个错误时，唯一首个诊断按以下顺序确定：

1. **阶段优先**：PARSE → IMPORT → SEM → CT → RT。前一阶段有诊断即停止，不进入后一阶段。
2. **模块顺序**：同一阶段内，从根模块开始，按 `import` 源序深度优先遍历，首次访问为准。`module_order` 在 PARSE 闭包完成后固定，后续阶段不得改变。
3. **同模块源序**：行号升序；同行列号升序。
4. **同位置专用码优先**：同一位置存在多个候选码时，语义更专用的码优先于通用码。例如 `TC_CE_MISSING_END` 优先于 `TC_CE_SYNTAX`；字面量的 `TC_CE_LITERAL_TYPE`/`TC_CE_LITERAL_RANGE` 优先于笼统的 `TC_CE_TYPE_MISMATCH`；`TC_CE_CONDITION_TYPE` 只用于条件值本身不是 `bool`，不抢先于条件操作数内部的字面量/类型专用码。
5. **条款内顺序优先**：语言标准各章规定的顺序（浮点异常优先级、负移位计数先行、空指针分类先行等）优先于通用规则。
6. **布局诊断归属 SEM**：结构体布局在 `struct` 声明处计算；`TC_CE_LAYOUT_OVERFLOW` 按 SEM 的模块遍历/源序参与首诊排序，不进入 CT。
7. **遍历无关**：函数表、哈希表、CFG、调用图、导入表、字段表的内部遍历顺序不得改变首个诊断。

**IMPORT 码的阶段例外**：`TC_CE_IMPORT_NOT_FOUND`、`TC_CE_IMPORT_NOT_LIB`、`TC_CE_DUPLICATE_IMPORT`、`TC_CE_CIRCULAR_IMPORT`、`TC_CE_IMPORT_NAME_CONFLICT` 在语言标准码表中归入 SEM 子阶段，但按 [语言标准 §12.2] 必须在 IMPORT 阶段报告。任何实现都不得把它们推迟到步骤 6–11 再报告。

### 1.6 源位置与诊断跨度

- 源位置使用 1 基行号与 1 基列号。列号按解码后的 Unicode 标量值计数；ASCII 空格计一个标量；行终止符不属于下一行的列。
- 非法 UTF-8 的行列按首个非法字节之前的最长有效前缀计算；首个非法字节占据下一列，并以 0 基字节偏移作为附加位置。主错误码仍为 `TC_CE_SYNTAX`。
- 诊断跨度默认覆盖触发构造的首个 Token 到该构造结束；跨文件诊断使用导入方源文件中的主位置，关联位置可指向被导入模块的声明。
- 诊断文本可以本地化；错误码、主位置、关联位置与首个诊断的选择必须稳定。实现资源错误 `TC_ERR_OUT_OF_MEMORY` 的打印名与消息除外（§12.6）。

### 1.7 无警告与确定性

TC 不定义编译警告。所有规范诊断都是错误；不得以警告方式放行类型、初始化、溢出、可见性或控制流违规，也不得以警告代替公开 `static var` 的跨模块写入规则或 `private` 成员的拒绝规则。

编译器必须保证：同一源程序、同一模块搜索路径、同一平台档案下，不论优化级别、内部哈希顺序、线程数或后端选择，首个诊断及其错误码一致；成功编译后的运行时可观察行为符合语言标准 [语言标准 §1.3] 与 [语言标准 §10.8]。

---

## 2. 词法

本章对应 [语言标准 §2]。语言标准定义字符集、记号形态与关键字；本章固定扫描器的确定性实现与词法诊断定位。

### 2.1 字节解码与字符集

1. 源文件必须是有效 UTF-8；不接受 BOM；U+0000 非法；注释外只允许 ASCII 字母、数字、空白、`_` 与语言标准 §2.1 列出的符号。
2. 非法 UTF-8、BOM、U+0000、注释外非 ASCII、注释外未列出的 ASCII 字符、注释外的 `%`，以及注释外不成 `///` 的 `/`、`//` 与 `;`，统一报告 `TC_CE_SYNTAX`。`%` 没有语义，不得被解释为格式说明符的起始；`///` 是唯一注释起始。
3. 注释内允许任意已解码 Unicode 标量值（U+0000 除外）；注释内容不进入记号流。
4. 不做 Unicode 规范化、大小写折叠或全角/半角转换。

### 2.2 注释与续行

- `///` 从当前位置到行尾是唯一行注释，可出现在行首或代码之后；无块注释。
- `/`、`//` 与 `;` 不开始注释；出现在注释外是 `TC_CE_SYNTAX`，不得静默丢弃该行剩余内容，也不得把 `//` 当作注释。
- 逻辑行以反斜杠 `\` 结尾时，下一物理行属于同一逻辑行；`\` 后不得有注释；注释内的 `\` 只是注释内容。文件末尾的孤立续行按 `TC_CE_SYNTAX` 处理。
- 空白行与纯注释行不产生指令或声明。

### 2.3 记号与最长匹配

词法分析采用最长匹配：

1. `///` 在任意位置起始行注释，直到行尾；注释识别先于记号化，`//` 与单个 `/` 不是注释。
2. `#program`、`#lib` 作为模式记号；`@name` 作为注解记号。
3. 标识符与关键字按最长匹配；关键字和助记符不可作标识符。
4. 指令助记符的后缀 `.wrap`、`.ieee`、`.volatile`、`.nsw`、`.nuw` 属于助记符的一部分，不拆成独立关键字。
5. 数字记号按语言标准 §2.4 的整数/浮点产生式最长匹配；进制前缀、`u`/`U`、`f`/`F` 后缀参与匹配。
6. `-` 紧邻数字或 `inf` 时属于字面量记号；`-nan` 不是合法字面量，按 `TC_CE_SYNTAX` 拒绝。`+` 只在指数或注解参数等语言标准允许的位置出现。
7. 单独的 `_` 是 `DISCARD`；`_x`、`x_`、`_1`、`__` 是合法标识符。只接受 `IDENT` 的位置出现 `DISCARD` 时报告 `TC_CE_SYNTAX`。

### 2.4 标识符、关键字与注解

标识符区分大小写，ASCII only。关键字按语言标准 §2.3 分类：模式、声明、安全、类型、字面量、命名空间、指令、指令后缀与注解。`Self` 是保留的模块命名空间记号；变量名可以写作小写 `self`，但不得与 `Self` 混淆。

注解词法为 `@` + 注解名 + 可选参数列表。`@padding(N)` 的 `N` 必须是**无后缀、无进制前缀、无负号**的非负十进制字面量；`N` 的形式不合法在 SEM 阶段报告 `TC_CE_CONST_FORM`。未知注解、注解位置不合法、重复或冲突的注解在 SEM 阶段报告 `TC_CE_ANNOTATION`。

### 2.5 逻辑行与缩进

- 一条指令或声明一行；逻辑行由 LF、CRLF 或 CR 结束。Token 之间可用空格或 Tab。
- 行首缩进**仅是风格**，不参与语法，不产生 `INDENT`/`DEDENT` 记号，也不产生缩进诊断。
- 块由 `then … end` 界定（`func`/`struct`/`unsafe`）；`end` 与块头在逻辑上对齐，但缩进错误不产生诊断。
- 空行、纯注释行和续行不改变块结构。

### 2.6 字面量

**整数**：

- 支持十进制、`0x`/`0X` 十六进制、`0b`/`0B` 二进制、`0o`/`0O` 八进制；进制只改变书写，不改变数学值。
- 十进制不允许前导零（单个 `0` 除外）；`_` 只能在数字之间，不可连续、不可位于首尾或进制前缀内。
- 无后缀为有符号字面量；`u`/`U` 为无符号字面量；`-` 只与无后缀字面量组合，`-42u` 非法。
- 词法值范围为 `0 ≤ N ≤ 2^64−1`；超出报告 `TC_CE_LITERAL_LEX_RANGE`。该检查只依赖 Token 自身，属于 LEX。

**浮点**：

- 浮点字面量必须含小数点或指数；小数点两侧都要有数字；无后缀源类型为 `float64`，`f`/`F` 后缀源类型为 `float32`。
- 精确十进制值直接按 roundTiesToEven 舍入到源类型；不得先经另一浮点类型中转。
- 非零有限值舍入为零，或有限值舍入为无穷，报告 `TC_CE_LITERAL_LEX_RANGE`（LEX）。
- 显式 `-` 且数学值为零时产生负零；`inf`、`-inf`、`nan` 是特殊字面量；`nan` 是 canonical quiet NaN；`-nan` 语法拒绝。

**布尔与空指针**：`true`/`false` 只用于 `bool`；`nullptr` 只用于期望类型为 `ptr<T>` 或 `fnptr<…>` 的位置。`nullptr` 的所指类型由期望类型决定，字面量本身不携带类型。`bitcast` 不接受 `nullptr`。

**字面量的阶段分工**：LEX 只做 Token 自身的整数上限与浮点源类型舍入检查；SEM 才结合期望类型检查类别、后缀、符号性与目标范围。`TC_CE_LITERAL_LEX_RANGE` 与 `TC_CE_LITERAL_RANGE` 不得混用。

### 2.7 词法诊断与位置

| 错误码 | 位置 |
| ------ | ---- |
| `TC_CE_SYNTAX` | 非法字节/非法标量定位到首个非法字节或标量；非法记号定位到记号首 Token；注释外的 `/`、`//` 或 `;` 定位到首个 `/` 或 `;`；`DISCARD` 出现在只接受 `IDENT` 的位置定位到 `_` |
| `TC_CE_LITERAL_LEX_RANGE` | 完整字面量的第一个 Token；负号属于字面量时定位到 `-` |
| `TC_CE_MISSING_END` | 未配对的 `func`/`struct`/`unsafe` 开启关键字；多个候选时取源位置最早者 |

词法阶段不得报告 `TC_CE_LITERAL_TYPE`、`TC_CE_LITERAL_RANGE`、`TC_CE_TYPE_MISMATCH` 或其他 SEM 码。词法阶段失败的记号不得进入语法或语义分析。

---

## 3. 类型系统

本章对应 [语言标准 §3] 与 [语言标准 §6.6]。类型分类、等价、转换与布局语义以语言标准为准；本章固定编译器侧的类型事实源、验证顺序与诊断归属。

### 3.1 类型事实源

编译器必须维护单一类型事实源：

- 完整类型由类型对象（种类 + 参数）表示；符号、绑定、值与分析期期望类型持有**稳定、共享**的类型指针，不得在绑定或值上另存一份裸标签充当完整类型。
- 类型种类标签只是投影，用于判别、宽度与语义内核分派；不得用投影降级写回绑定或值。
- 指针元素类型、函数指针签名、结构体标识等复合参数只存在于类型对象的参数中；不得在符号表上建立平行的冗余字段。
- Analyze 入口创建类型表；Pass1 对声明类型做 intern；Pass2 对 `cast`/`trunc`/`bitcast` 目标做 intern；Analyze 完成后类型表对执行器/AOT 只读。
- `cast`/`trunc`/`bitcast` 的目标类型必须按完整类型语法解析，包括 `ptr<T>`、`fnptr<…>` 与结构体名；执行器/AOT 不得重新解析或改写类型表。

### 3.2 类型分类与等价

| 类别 | 类型 | 编译器验证 |
| ---- | ---- | ---------- |
| 整数 | `int8`～`uint64`、`isize`、`usize` | 位宽与符号性均相同才等价；`isize`/`usize` 是独立的字长类型，不与固定宽度类型隐式等价 |
| 浮点 | `float32`、`float64` | 按关键字等价；不得隐式加宽/缩窄 |
| 布尔 | `bool` | 规范字节 `0x00`/`0x01`；不参与位运算以外的数值隐式转换 |
| 指针 | `ptr<T>` | `T` 等价才等价；`T` 必须是非 `void` 值类型 |
| 函数指针 | `fnptr<ret, params…>` | 返回类型与参数逐项等价；参数名不参与 |
| 结构体 | `S` | 名义等价；不同结构体即使字段相同也不同型 |
| 无值 | `void` | 仅返回类型；不是值类型 |

编译器不得实现子类型、隐式加宽、隐式窄化、隐式符号转换或结构体结构化等价。赋值、传参、返回、`make` 字段与 `copy` 均要求严格同型。

### 3.3 整数、浮点与布尔

**整数**：

- 使用二进制补码；有符号 `add/sub/mul/neg/shl` 默认 strict，溢出 trap 或 CT 失败；`.wrap` 按模 `2^n` 回绕。
- 无符号 `add/sub/mul/neg` 固定模 `2^n`；`shl` 默认 strict，`.wrap` 回绕。
- `div` 向零截断；`mod` 余数符号随被除数；除零 trap。`div(INT_MIN, −1)`、`abs(INT_MIN)`、`neg(INT_MIN)` strict 为溢出；`mod(INT_MIN, −1) = 0`。
- `shl`/`shr` 的被移位数与计数必须同为 `T`；计数按 `T` 的数学值解释。负计数先报告负移位；`k >= n` 时按语言标准 [语言标准 §6.3] 的特殊规则处理。
- 位宽或符号不同的值不得隐式转换；必须显式 `cast`/`trunc`/`bitcast`。

**浮点**：

- 每条指令按其类型参数独立执行并立即按 roundTiesToEven 舍入到目标格式；禁止扩展精度、FMA、FTZ、DAZ。
- strict 模式的异常优先级固定为 `INVALID → DIV_ZERO → OVERFLOW → UNDERFLOW`。`.ieee` 模式不 trap，返回 ±inf/NaN；NaN 统一为 canonical quiet NaN。
- `abs`/`neg` 只改符号位，不 trap；浮点 `mod` 是商向零截断的余数，不是 IEEE remainder。
- 浮点不参与位运算；位模式经等宽 `bitcast`。
- 常量求值与运行时使用同一精度、舍入与异常规则；不得依赖宿主浮点环境。

**布尔**：

- `bool` 占 1 字节；`false` 为 `0x00`，`true` 为 `0x01`。
- `load bool` 必须把任意非零字节规范化为 `true`；`store bool` 必须写 `0x00` 或 `0x01`。
- 逻辑 `and/or/xor/not` 严格求值，不短路；短路只能用 `br_if`/`br` 显式构造。
- `bitcast` 禁止 `bool`；`cast` 允许 `bool` 与整数/浮点互转，按 0/非 0 规则。

### 3.4 指针与函数指针

**指针**：

- `ptr<T>` 的 `T` 必须是非 `void` 值类型；宽度等于 `usize`；地址单位是字节；`nullptr` 位模式为 0，地址 0 保留。
- 指针不参与通用算术、位运算、标量比较或 I/O；指针算术只能用 `ptradd`/`ptrsub`，比较只能用 `ptreq`/`ptrne`/`ptrlt`/`ptrle`/`ptrgt`/`ptrge`。
- `addr T, target` 只接受 `var`/`static var`/程序 `var`/参数/字段路径；`const` 不可取址，报告 `TC_CE_ADDR_OF_CONST`。参数地址只读，写穿由运行期报告 `TC_RE_WRITE_PROTECTED`。
- `load`/`store`/`memmove`/`ptradd`/`ptrsub` 是 unsafe 指令；空指针由运行期 trap；越界、悬垂、one-past 访问与未初始化读取属于 UB，编译器不作静态保证。
- `ptrdiff` 返回字节差 `isize`；结果不可表示时运行期 `TC_RE_PTRDIFF_OVERFLOW`。同一对象内的差确定；跨对象/悬垂结果环境定义。
- `sizeof T` 返回字节数 `usize`；`sizeof(void)` 报告 `TC_CE_VOID`；结构体布局溢出报告 `TC_CE_LAYOUT_OVERFLOW`。

**函数指针**：

- `fnptr<ret, params…>` 的返回类型可以为 `void`，参数必须是值类型；宽度等于 `usize`；签名逐项等价，参数名不参与。
- `nullptr` 与任意 `fnptr<…>` 兼容；`funcaddr` 产生 `fnptr<sig>`；`call_indirect` 只接受 `fnptr<…>`，空指针运行期 `TC_RE_NULL_FUNCTION_POINTER`。
- `ptr` 与 `fnptr` 禁止互转；`fnptr` 与浮点禁止 `bitcast`；`fnptr` 与整数 `bitcast` 是 unsafe；同签名 `fnptr↔fnptr` 是 safe 重解释；跨签名 `bitcast` 禁止。
- 函数指针不携带 `@unsafe` 信息；所有间接调用都在 unsafe 上下文中进行。
- 不同函数的入口地址必须互不相同；实现必须禁用 ICF 或等价合并。

### 3.5 结构体

结构体是名义值类型，字段按声明顺序排列：

- 至少 1 个字段；空结构体报告 `TC_CE_EMPTY_STRUCT`。
- 字段名在同一结构体内唯一；重复报告 `TC_CE_DUPLICATE_FIELD`。
- 字段修饰符为 `readonly` 或 `var`；字段类型可以是标量、`ptr<T>`、`fnptr<…>`、结构体或另一个完整结构体类型。
- 值位置不得直接或间接包含正在定义的结构体；`ptr<S>`/`ptr<ptr<S>>` 与 `fnptr` 签名中的 `S` 是自引用例外。值位置自引用报告 `TC_CE_STRUCT_SELF_VALUE`。
- 字段类型不得引用尚未定义完毕的其它结构体（即使是指针）；报告 `TC_CE_UNDEFINED_NAME`。`S` 自身的指针/`fnptr` 签名自引用是例外。
- 布局为字段紧凑排列；`@padding(N)` 在该字段后插入 `N` 个 `0x00` 字节；无隐式对齐、无尾部填充。`sizeof(S) = Σ(字段大小 + padding)`；求和中溢出 `usize` 报告 `TC_CE_LAYOUT_OVERFLOW`。
- `make S, …` 按声明顺序提供全部字段，类型严格；支持位置形式与 `field: value` 命名形式。缺字段 `TC_CE_MISSING_FIELD`；未知字段 `TC_CE_UNKNOWN_FIELD`；重复字段 `TC_CE_DUPLICATE_FIELD`；顺序不符 `TC_CE_FIELD_ORDER`。构造时 padding 写 `0x00`。
- 结构体是值类型：整块赋值、传参、返回复制全部字段与 padding；指针字段只复制地址。整块赋值可以替换整个值，包括其中的 `readonly` 字段；字段赋值按路径规则检查。
- 结构体不支持算术、比较、`cast`/`bitcast`、标量 I/O；按字段操作。

### 3.6 转换

编译器必须按形式分别验证：

| 形式 | 静态验证 | 运行时 |
| ---- | -------- | ------ |
| `cast T, x` | 数值或 `bool` 转换；指针不参与 `cast`；类别不符报告 `TC_CE_TYPE_MISMATCH` | 范围不可表示报告 `TC_RE_CAST_OVERFLOW`；浮点 NaN/无穷转整数报告 `TC_RE_CAST_OVERFLOW` |
| `trunc T, x` | 源、目标均为整数；非整数报告 `TC_CE_TYPE_MISMATCH`；目标位宽 ≥ 源报告 `TC_CE_TRUNC_WIDTH` | 保留低位，无检查 |
| `bitcast T, x` | 先查类别矩阵（禁止 → `TC_CE_TYPE_MISMATCH`），再查位宽（允许但不等宽 → `TC_CE_BITCAST_WIDTH`） | 位模式不变 |

`bitcast` 类别矩阵以 [语言标准 §3.11] 为准：数值↔数值 safe；`ptr↔ptr` safe；同签名 `fnptr↔fnptr` safe；`ptr↔整数`、`fnptr↔整数` unsafe；`bool`、指针↔浮点、`fnptr`↔浮点、指针↔`fnptr`、跨签名 `fnptr`、结构体、`nullptr` 一律禁止。禁止项报告 `TC_CE_TYPE_MISMATCH`，不得改报位宽错误。

**字面量源类型**：

- `cast` 中无后缀整数源类型为 `int64`，`u`/`U` 为 `uint64`；无后缀浮点为 `float64`，`f`/`F` 为 `float32`；`inf`/`-inf`/`nan` 以目标浮点类型为源类型；`true`/`false` 源类型为 `bool`。
- `bitcast` 中整数字面量的源位宽等于目标位宽，后缀决定符号解释；浮点字面量的源精度由 `f`/`F` 决定；`inf`/`-inf`/`nan` 的源类型由目标位宽决定；`nullptr` 非法。
- 普通指令、`copy`、`make`、`call`、`return`、`write` 的字面量以期望类型检查。字面量超出源类型或期望类型范围报告 `TC_CE_LITERAL_RANGE`；类别/后缀/符号性不符报告 `TC_CE_LITERAL_TYPE`。

### 3.7 常量表达式

`const` 与 `static var` 的初始化器使用语言标准 [语言标准 §3.10] 的 `const_expr`：

- 允许字面量、此前定义的 `const`、`Self.K`、`m.K`、`const` 结构体字段读取、纯运算、`cast`/`trunc`/`bitcast`、`make` 与 `sizeof`。
- 禁止 `var`/参数、`addr`、`load`、`call`、`funcaddr`、`read`、`write` 与 unsafe 操作。`const` 中的 `bitcast` 只允许数值↔数值；指针/`fnptr` 的位重解释报告 `TC_CE_CONST_FORM`。
- 常量表达式先做名称/类型检查，再做 CT 求值。名称/类型错误按 SEM 码报告；合法形态的求值失败按原因报告 `TC_CE_CONST_OVERFLOW`、`TC_CE_CONST_DIV_ZERO`、`TC_CE_CONST_SHIFT`、`TC_CE_CONST_FLOAT_INVALID`、`TC_CE_CONST_FLOAT_UNDERFLOW`、`TC_CE_CONST_CAST`。
- `@padding(N)` 的 `N` 必须是非负十进制字面量；带后缀、进制或负号报告 `TC_CE_CONST_FORM`。

### 3.8 类型诊断与位置

| 错误码 | 主位置 |
| ------ | ------ |
| `TC_CE_TYPE_MISMATCH` | 首个不匹配操作数；RHS 结果与接收目标不符时定位到 RHS 首 Token；字段路径中间层不是结构体时定位到该字段名 |
| `TC_CE_LITERAL_TYPE` / `TC_CE_LITERAL_RANGE` | 完整字面量首 Token；负号属于字面量时定位到 `-` |
| `TC_CE_MODE` | 非法的模式后缀 Token |
| `TC_CE_BITCAST_WIDTH` | 源操作数首 Token |
| `TC_CE_TRUNC_WIDTH` | 目标类型 Token |
| `TC_CE_VOID` | `void` 类型 Token |
| `TC_CE_STRUCT_SELF_VALUE` | 自引用字段的类型 Token |
| `TC_CE_EMPTY_STRUCT` | 结构体名 |
| `TC_CE_LAYOUT_OVERFLOW` | 触发溢出的字段或结构体声明 |
| `TC_CE_ADDR_OF_CONST` | `addr` 的 `const` 目标名 |

---

## 4. 程序结构、模块与声明

本章对应 [语言标准 §4]。模块模式、层序、导入、可见性与跨模块常量求值以语言标准为准；本章固定编译器的发现顺序、IMPORT 阶段与诊断归属。

### 4.1 文件、模块与模式

- 一个 `.tc` 文件 = 一个编译单元 = 一个模块；模块名为文件名去掉 `.tc`，必须是合法标识符，否则不能被 `import`。
- 文件首条有效行必须恰好是一个 `#program` 或 `#lib`；缺失、重复、模式后再出现另一模式均属于语法拒绝 `TC_CE_SYNTAX`。
- `#program` 是入口模块；`#lib` 是库模块，不直接执行。一个程序由一个 `#program` 及其可达 `#lib` 导入组成。
- `#program` 顶层可以包含 `var`/`const` 声明与指令；`#lib` 顶层只允许 `struct`、`const`、`static var`、`func`。

### 4.2 模块层序

**`#lib` 五层**：

| 层 | 内容 | 要求 |
| -- | ---- | ---- |
| 1 | `#lib` | 恰好一个 |
| 2 | `import` | 连续，可为空 |
| 3 | `struct` | 类型定义 |
| 4 | `const` / `static var` | 初始化器必须是 `const_expr` |
| 5 | `func` | 顺序不限 |

**`#program` 四层**：

| 层 | 内容 | 要求 |
| -- | ---- | ---- |
| 1 | `#program` | 恰好一个 |
| 2 | `import` | 连续，可为空 |
| 3 | `struct` | 类型定义 |
| 4 | 声明与指令体 | `var`/`const` 与指令；不得定义 `func`、使用 `static`/可见性/`Self` |

跨层交错报告 `TC_CE_MODULE_LAYER`（SYN）。`#lib` 顶层成员缺 `public`/`private` 报告 `TC_CE_MISSING_VISIBILITY`（SYN）。`#program` 使用 `func`/`static`/`public`/`private`/`Self`，或 `#lib` 使用顶层 `var`/指令，报告 `TC_CE_PROGRAM_MODE_MISUSE`（SYN）。

### 4.3 模块发现与 PARSE 顺序

PARSE 阶段按以下算法确定模块集合与 `module_order`：

1. 从根模块开始，对当前模块执行 LEX→SYN；成功后加入 `module_order` 并标记已访问。
2. 按该模块 `import` 的源序逐条处理：
   - 目标文件不存在：记录 `TC_CE_IMPORT_NOT_FOUND`，不递归，继续下一条；
   - 目标在当前递归栈上：记录 `TC_CE_CIRCULAR_IMPORT`，不递归，继续下一条（循环优先于重复）；
   - 当前导入者已引用同一解析目标：记录 `TC_CE_DUPLICATE_IMPORT`，不递归，继续下一条；
   - 目标已解析且不在当前栈上：正常复用，不重复 PARSE，不重复加入 `module_order`；
   - 新目标：定位文件并递归执行本算法。
3. 目标存在但模式为 `#program`：仍执行 LEX→SYN；在 IMPORT 阶段记录 `TC_CE_IMPORT_NOT_LIB`。
4. 任何 LEX/SYN 错误立即报告并停止。第 2 步记录的导入错误延迟到 IMPORT 阶段，不阻止继续发现其它导入。
5. 每个成功发现的模块只 PARSE 一次。所有可达模块 PARSE 成功后，`module_order` 固定；后续阶段不得因依赖拓扑或哈希遍历改变它。记录的错误归属于发起该导入的模块/`import` 语句，IMPORT 阶段按 `module_order`/源序报告并校验导入，随后 SEM 使用同一顺序；CT 以同一顺序启动，但按 §4.6 按需求值。

示例：`root → a → c` 与 `root → b → c` 的 `module_order` 为 `root、a、c、b`；`c` 只 PARSE 一次，`b` 对 `c` 的导入复用，不报重复。

### 4.4 IMPORT 阶段

IMPORT 阶段按 `module_order` 与模块内 `import` 源序处理：

1. 定位每个 `import m` 的目标 `m.tc`；搜索顺序为导入文件所在目录 → 环境声明的 include 根目录（按顺序）→ 首个匹配。include 根目录属于环境定义，实现必须文档化。
2. 目标必须是 `#lib`；导入 `#program` 报告 `TC_CE_IMPORT_NOT_LIB`。
3. 同一导入者重复引用同一解析目标报告 `TC_CE_DUPLICATE_IMPORT`；不同导入者共享同一目标属于正常复用。
4. 目标在当前递归栈上报告 `TC_CE_CIRCULAR_IMPORT`；循环优先于重复。
5. 导入名占用模块命名空间；导入名与本模块的模块成员名或局部名冲突报告 `TC_CE_IMPORT_NAME_CONFLICT`。检查与 `import` 的文本位置无关。
6. 导入成功后，按依赖 DAG 的逆向拓扑序收集全部可达模块的函数签名、结构体定义与模块成员；同层并列时按 `module_order` 打破平局。签名收集不得依赖函数表遍历顺序。
7. IMPORT 阶段不报告名称解析、类型或 CFG 诊断；这些属于 SEM。

### 4.5 可见性与成员命名空间

- `#lib` 的每个顶层成员必须显式 `public` 或 `private`，无默认值。
- `public` 成员可被导入方通过 `m.x` 访问；`private` 成员只能在本模块通过 `Self.x` 访问。导入方访问 `private` 成员报告 `TC_CE_PRIVATE_ACCESS`，不得改报 `TC_CE_UNDEFINED_NAME`/`TC_CE_UNDEFINED_FUNCTION`。
- `private` 类型出现在 `public` 签名、`public struct` 字段、`public const`/`public static var` 的声明类型或初始化器中，报告 `TC_CE_PRIVATE_TYPE`。
- `public static var` 可读可写；`public const` 只读；`public func` 可调用。结构体字段全部对外可见，不提供字段可见性。
- 模块成员（`struct`/`const`/`static var`/`func`）之间不得重名；重复报告 `TC_CE_DUPLICATE_MEMBER`。
- 函数体中的裸名解析顺序见 §5.2。模块成员必须写 `Self.x` 或 `m.x`；函数体内裸名命中本模块成员报告 `TC_CE_FUNCTION_SCOPE_ACCESS`。
- `Self` 只在 `#lib` 中合法；`#program` 使用报告 `TC_CE_PROGRAM_MODE_MISUSE`。不支持别名、选择性导入、通配导入或重导出；传递依赖不会自动可见。

### 4.6 模块常量与静态存储

- `const` 是编译期常量，无运行时存储、无地址，使用处内联。`const` 初始化器按源序求值；只能引用更早的 `const`；前向/自引用报告 `TC_CE_UNDEFINED_NAME`。
- `static var` 是模块静态存储，初始化器必须是 `const_expr`；在程序准备阶段一次性写入；所有导入者共享同一个槽；`static var` 不参与运行时初始化顺序问题。
- 跨模块 CT 按需求值，遵循 [语言标准 §4.10] 的确定性规则：每个常量声明有未求值/求值中/成功/失败状态；遇到 `Self.K`/`m.K` 时先递归求值目标；成功使用缓存值，失败归属目标声明，引用处不产生级联诊断。首个引用失败后停止，后续引用不再求值。
- CT 以 `module_order`/模块内源序为启动顺序，对 `const` 与 `static var` 初始化器按需求值；初始化器内的常量引用按源文法从左到右递归求值，`make` 的命名字段按书写顺序；该顺序只决定 CT 的依赖求值与诊断，不改变 [语言标准 §3.10] 的名称/类型检查阶段，是 §1.5 模块顺序在 CT 子阶段的例外。
- 同一常量在模块内或菱形依赖中只求值一次；成功与失败都缓存。常量依赖无环：同模块只能引用更早的 `const`，跨模块只能沿导入 DAG 单向引用。
- 结构体布局与 `sizeof` 在 SEM 阶段计算并缓存；普通指令中的 `sizeof` 不参与 CT；`TC_CE_LAYOUT_OVERFLOW` 在 SEM 报告。
- `static var` 初始化失败是编译期错误（因为只允许 `const_expr`），不存在运行时静态初始化失败。`#lib` 不自动执行代码；需要运行时初始化的库提供显式 `public func init()`，由 `#program` 调用。

### 4.7 声明验证

- `var` 必须带初始化器；缺 `=` 或初始化器报告 `TC_CE_VAR_MISSING_INIT`（SYN）。`var` 初始化器必须是一个 op（`copy`、计算、`call`、`read`、`make` 等），不能是裸值。
- `const` 必须带 `const_expr`；无存储、无地址；不得作为赋值目标。
- `label L:` 是声明，不产生运行时动作；标签属于独立命名空间，与值名分离。
- 结构体声明、函数声明、导入声明与块结构必须符合语言标准 EBNF；不符合的形态使用 `TC_CE_SYNTAX` 或对应的专用 SYN 码，不得为产生后续语义诊断而放宽语法。

### 4.8 模块诊断与位置

| 错误码 | 主位置 | 阶段 |
| ------ | ------ | ---- |
| `TC_CE_MODULE_LAYER` | 破坏层序的最早构造首 Token | SYN |
| `TC_CE_MISSING_VISIBILITY` | 缺少 `public`/`private` 的成员关键字 | SYN |
| `TC_CE_PROGRAM_MODE_MISUSE` | 误用的 `func`/`static`/`public`/`private`/`Self` 关键字 | SYN |
| `TC_CE_IMPORT_NOT_FOUND` | `import` 的模块名 | IMPORT |
| `TC_CE_IMPORT_NOT_LIB` | `import` 的模块名 | IMPORT |
| `TC_CE_DUPLICATE_IMPORT` | 第二次及后续 `import` 的模块名；关联首次导入 | IMPORT |
| `TC_CE_CIRCULAR_IMPORT` | 环中源位置最早的 `import` 边；关联环路径 | IMPORT |
| `TC_CE_IMPORT_NAME_CONFLICT` | `import` 的模块名；关联冲突的本模块成员 | IMPORT |
| `TC_CE_PRIVATE_ACCESS` | 限定名中的成员名；关联私有声明 | SEM |
| `TC_CE_PRIVATE_TYPE` | 出现在公开位置的私有类型名；关联类型声明 | SEM |
| `TC_CE_DUPLICATE_MEMBER` | 第二次及后续的成员名；关联首次声明 | SEM |

---

## 5. 槽、作用域与确定初始化

本章对应 [语言标准 §5] 与 [语言标准 §6.14]。槽分类、名字解析与确定初始化语义以语言标准为准；本章固定编译器的作用域模型、赋值目标验证与数据流算法。

### 5.1 槽的种类

| 槽 | 建立方式 | 存储期 | 可写 | 参与 DI |
| -- | -------- | ------ | ---- | ------- |
| 参数 | `func` 形参 | 调用帧 | 否 | 入口即已初始化 |
| 局部 `var` | 函数体 `var` | 调用帧 | 是 | 是 |
| 程序 `var` | `#program` 顶层 `var` | 程序 | 是 | 是 |
| `static var` | `#lib` 成员 | 程序 | 是 | 入口即已初始化 |
| `const` | 声明 | 无存储 | 否 | 否 |

- **没有块级作用域**：`var`/`const` 是函数/程序作用域；`unsafe` 块也不引入新作用域。`if`/`while` 不存在，控制流由标签与分支构成，因此不存在块作用域栈。
- 名字从声明处可见到函数/程序末尾（源序）；不得在声明前使用。
- 同一函数/程序内不得重名（`var`/`const`/参数共享值名字空间）；重复报告 `TC_CE_DUPLICATE_NAME`。标签不在值名字空间中。
- 局部名可以与模块成员同名；模块成员必须 `Self.`/`m.` 限定。类型名与值名分属不同命名空间。

### 5.2 名字解析

函数/程序体内裸标识符按以下顺序解析：

1. 当前函数/程序的 `var`/`const`/参数（从声明处可见）；
2. 若未命中，检查是否为本模块成员名：是则报告 `TC_CE_FUNCTION_SCOPE_ACCESS`（必须写 `Self.`）；
3. 否则报告 `TC_CE_UNDEFINED_NAME`。

作为赋值目标时同样按本节解析：裸本模块函数名报告 `TC_CE_FUNCTION_SCOPE_ACCESS`；`Self.f`/`m.f` 已解析为函数报告 `TC_CE_TYPE_MISMATCH`；未解析报告 `TC_CE_UNDEFINED_NAME`。

`Self.x` 只解析本模块成员；`m.x` 只解析导入成员；字段访问 `x.f` 先解析 `x`，再按结构体字段解析。标签在分支位置解析，值位置不解析标签；`label L:` 与 `var L` 可以共存。

### 5.3 变量、常量与参数

- `var` 必须在声明处初始化；初始化器是一个 op。`var x: T = rhs` 的 RHS 在 `x` 加入已初始化集合之前检查，因此初始化自引用不能通过 DI 绕过；它按名称可见性报告 `TC_CE_UNDEFINED_NAME`。
- `var` 执行成功后槽变为已初始化；再次执行同一 `var`（循环/跳转）会重新初始化同一词法槽，不创建新绑定。
- `const` 在编译期求值并内联；无运行时槽；不参与 DI；前向/自引用报告 `TC_CE_UNDEFINED_NAME`。
- 参数按值传递、只读、入口已初始化；不得对参数绑定赋值、字段赋值或作为 `read` 目标，否则报告 `TC_CE_PARAMETER_ASSIGNMENT`。对参数 `addr` 合法；经该地址 `store` 由运行期报告 `TC_RE_WRITE_PROTECTED`。
- 结构体参数按值复制；指针参数复制地址，调用者与被调方共享所指对象；`fnptr` 参数复制入口地址。

### 5.4 赋值目标与字段路径

`lhs` 可以是简单槽、`Self.x`/`m.x` 模块静态槽，或字段路径 `x.f`/`x.f.g`。

**简单槽**：

| 目标 | 规则 |
| ---- | ---- |
| `var` / `static var` / 程序 `var` | 允许；类型严格等于声明类型；目标必须已确定初始化（`var` 初始化语句除外） |
| `const` / `Self.K` / `m.K` | `TC_CE_CONST_ASSIGNMENT` |
| 参数 | `TC_CE_PARAMETER_ASSIGNMENT` |
| 函数名 | 裸本模块函数名 → `TC_CE_FUNCTION_SCOPE_ACCESS`；`Self.f`/`m.f` 已解析函数 → `TC_CE_TYPE_MISMATCH`；未解析 → `TC_CE_UNDEFINED_NAME` |
| `_` | 丢弃结果；仍做完整类型检查 |

**字段目标 `x.f`**：

- 基址 `x` 必须是结构体类型的可写槽（`var`/`static var`/程序 `var`）；基址是参数报告 `TC_CE_PARAMETER_ASSIGNMENT`；基址是 `const` 报告 `TC_CE_CONST_ASSIGNMENT`。
- 字段必须存在（`TC_CE_UNKNOWN_FIELD`）；中间层必须是结构体（`TC_CE_TYPE_MISMATCH`）。
- 从基址沿目标路径自左向右检查每个字段；任一字段为 `readonly` 报告 `TC_CE_IMMUTABLE_FIELD`，定位到路径上首个 `readonly` 字段。
- 深度只读只作用于结构体值子树；`ptr` 字段所指对象不受该字段 `readonly` 修饰影响，经指针写入按 unsafe 规则。
- 右侧结果类型必须严格等于**字段**类型；字段赋值不改变外层槽的“已初始化”状态；外层槽本身必须已初始化。
- 整块赋值 `x = make S, …` 或 `x = copy S, y` 可以替换整个值，包括其中的 `readonly` 字段；字段赋值仍禁止。
- `addr T, x.f` 取字段地址；字段类型必须等于 `T`。

### 5.5 确定初始化规则

- 每个 `var` 槽在函数/程序入口处于**未初始化**；`var x: T = …` 成功执行后，`x` 在该路径上变为已初始化。
- 读取 `x`、把 `x` 作为赋值目标、把 `x` 作为 `read` 目标，均要求在该程序点**所有可达路径**上已初始化；否则报告 `TC_CE_UNINITIALIZED`。
- 分支汇合取交集：所有可达前驱都已初始化才算已初始化。
- 循环回边按前驱处理；`var` 在循环体内再次执行会重新初始化同一槽。
- 参数与 `static var` 入口即已初始化；`const` 不参与 DI。
- 不可达代码不参与 DI，但必须通过词法/语法/名称/类型/安全/构造检查；不可达代码不是错误。
- `addr` 不要求目标已初始化；`load`/`store`/`memmove`/`ptradd`/`ptrsub`/`ptrdiff`/`ptreq`～`ptrge` 只要求指针绑定本身已初始化，所指对象是否已初始化不在静态判定范围。
- `var x: T = read T` 是初始化：读取成功后 `x` 加入已初始化集合；`x = read T` 要求 `x` 已初始化，读取成功后覆盖原值。`read` 失败时目标槽保持原值，程序终止。
- `copy`、`make`、算术等纯指令不改变除 LHS 之外的 DI 状态；`call` 是原子节点，成功返回后按结果写入 LHS；`void` 调用或丢弃结果不改变 DI 状态。

### 5.6 确定初始化数据流

编译器必须为顶层程序体和每个函数建立封闭 CFG（§7），并在可达子图上求最大固定点：

```text
IN[program entry] = ∅
IN[function entry] = 当前函数的全部参数
IN[n] = ∩ EDGE_OUT[p → n]        /// p 为 n 的全部可达前驱
EDGE_OUT[p → n] = OUT[p]         /// 无块作用域，不做作用域退出裁剪
OUT[var x = rhs] = IN[n] ∪ {x}
OUT[x = rhs] = IN[n]             /// x 必须 ∈ IN[n]；结果覆盖同一槽，不改变成员关系
OUT[const ...] = IN[n]           /// 无运行时效果的身份节点
OUT[其他指令] = IN[n]
```

- `U[n]` 是当前函数/程序内全部运行时绑定的全集：当前函数形参（若为函数）以及函数/程序内**所有** `var` 槽，不论相应 `var` 是否已经执行；不包含 `const`、其它函数的绑定或模块 `static var`（后者入口即已初始化，不进入 `U`）。
- 对非入口可达节点，以 `U[n]` 初始化 `IN[n]` 与 `OUT[n]`，再按上表单调向下迭代，直至稳定；两个入口边界值保持不变。工作队列、节点遍历与回边发现顺序不得改变最终结果。
- 最大固定点等价于路径语义：绑定属于 `IN[n]`，当且仅当从当前控制流域唯一入口到 `n` 的每一条有限可达路径都已初始化该绑定。
- 没有任何可达前驱的非入口节点不定义 `IN`；它们已由可达性排除，不参与固定点。
- `br_if` 的常量条件**不裁剪**任何边；两条后继都参与可达性与 DI。不得实现静态条件折叠或三态判定。
- `switch` 的所有目标标签（含默认标签）都是可达后继；不得因常量选择器删除边。
- `call` 在调用者 CFG 中是单个原子节点，不展开被调函数 CFG；被调函数发生 trap 时程序终止，不形成返回调用者的 CFG 边。
- `return` 只连接当前函数的正常返回终止节点；函数末尾节点若从入口可达，触发 `TC_CE_MISSING_RETURN`。

### 5.7 初始化诊断分层

| 层次 | 违规形态 | 错误码 | 性质 |
| ---- | -------- | ------ | ---- |
| 语法层 | `var` 声明缺少 `= <rhs>` | `TC_CE_VAR_MISSING_INIT` | 纯形态；与控制流无关 |
| 名称层 | 标识符未声明、尚未源序可见或已越出函数/程序作用域 | `TC_CE_UNDEFINED_NAME` | 名称解析 |
| 数据流层 | 声明存在，但在可达使用点不满足确定初始化 | `TC_CE_UNINITIALIZED` | CFG 固定点 |

`TC_CE_VAR_MISSING_INIT` 不得降级为通用语法错误；`TC_CE_UNINITIALIZED` 不得替代名称解析错误。不定义 `TC_CE_GOTO_SKIPS_VAR_INIT` 或任何针对单一控制流语句的初始化错误。

---

## 6. 指令集

本章对应 [语言标准 §6]。指令语义以语言标准 §6.2 的 opcode 表为准；本章固定编译器的统一解析、操作数形状、静态验证顺序与诊断定位。

### 6.1 统一指令形态

```text
instruction := lhs "=" op [arg { "," arg }]
lhs         := assignable | DISCARD
assignable  := identifier { "." identifier }
             | "Self" "." identifier { "." identifier }
             | identifier "." identifier    /// 导入模块成员 m.x
op          := mnemonic [ "." modifier ]
arg         := type | value | label | function | struct | named_arg
```

规则：

1. `=` 是每条指令的一部分；`lhs` 是可赋值目标或 `_`。`_` 只能作为丢弃目标，不能作为读取操作数或声明名。
2. 一条指令一个 op；操作数是值，不得嵌套 op/call。需要嵌套时先写入临时槽。
3. 类型是参数列表中的一种，由 opcode 表决定位置；有的指令无类型（`br`/`return`/`trap`），有的首项是函数名/标签/结构体名/类型。
4. `copy T, x` 是复制指令；赋值/初始化必须写 `copy` 或其它 op。裸值不能直接作为赋值/初始化操作数。
5. `var x: T = op …` 的初始化器必须是一个 op；`var` 是声明，不是指令。
6. `_` 是独立记号 `DISCARD`；在只接受 `IDENT` 的位置出现报告 `TC_CE_SYNTAX`。
7. `call` 的实参可按位置或按名字写；名字必须匹配形参且保持声明顺序。`make` 的字段同理。
8. `switch`/`br`/`br_if` 的目标是同一函数/程序内的标签。

### 6.2 操作数形状与通用验证

编译器必须先按 opcode 表验证操作数数量与形状，再验证类型：

| 操作数种类 | 合法形式 | 验证 |
| ---------- | -------- | ---- |
| 值操作数 | 标识符、`Self.x`、`m.x`、字段路径、字面量、`nullptr` | 名称解析、可见性、类型、字面量期望类型 |
| 类型参数 | 标量、`ptr<T>`、`fnptr<…>`、结构体名 | 完整类型解析；`void` 只在允许位置 |
| 标签 | 标识符 | 同一函数/程序的标签命名空间；前向/后向均可 |
| 函数 | `Self.f`/`m.f` | 函数签名；可见性；`@unsafe`/`@fast` 调用约束 |
| 结构体 | 结构体名 | 已定义、可见、字段完整 |
| 命名实参 | `name: value` | 名字匹配、唯一、顺序、全部必填 |

- 操作数数量/形状不符合 opcode 表报告 `TC_CE_OPERAND_COUNT`；未知的命名实参报告 `TC_CE_UNKNOWN_ARGUMENT`，重复报告 `TC_CE_DUPLICATE_ARGUMENT`，缺失报告 `TC_CE_MISSING_ARGUMENT`，顺序不符报告 `TC_CE_ARGUMENT_ORDER`。
- 语法上不符合 EBNF 的形态属于 SYN，不得用 `TC_CE_OPERAND_COUNT` 代替；`TC_CE_OPERAND_COUNT` 只用于“opcode 已识别、但操作数数量/形状不符合该 opcode 表”的情形。
- 所有类型参数与操作数类型必须严格一致；不做隐式转换。字面量以期望类型检查；`nullptr` 以期望的 `ptr<T>`/`fnptr<…>` 定型。

### 6.3 算术与位运算

| 指令 | 类型 | 模式 | 静态验证 |
| ---- | ---- | ---- | -------- |
| `add`/`sub`/`mul` | 有符号/无符号整数、浮点 | 有符号 strict 或 `.wrap`；浮点 strict 或 `.ieee`；无符号固定模 | 操作数类型等于 `T`；模式合法性；`ptr` 禁止 |
| `div`/`mod` | 整数、浮点 | 整数无模式；浮点 strict 或 `.ieee` | 除数为常量 0 在 CT 报 `TC_CE_CONST_DIV_ZERO`；运行时 `TC_RE_DIV_ZERO` |
| `neg` | 整数、浮点 | 有符号 strict/`.wrap`；无符号固定模；浮点纯符号位 | 同上 |
| `abs` | 整数、浮点 | 无模式 | 有符号 `INT_MIN` 溢出；浮点纯符号位 |
| `shl` | 整数 | strict 或 `.wrap` | 计数与值同为 `T`；负计数；`k >= n` 特殊规则 |
| `shr` | 整数 | 无模式 | 计数与值同为 `T`；负计数；`k >= n` 返回 0 |
| `and`/`or`/`xor` | 整数、`bool` | 无模式 | 整数按位；`bool` 逻辑；严格求值不短路 |
| `not` | 整数、`bool` | 无模式 | 整数按位；`bool` 逻辑 |

- 模式不适用于该类型/运算报告 `TC_CE_MODE`；`ptr<T>` 进入算术/位运算报告 `TC_CE_TYPE_MISMATCH`。
- 浮点 strict 异常优先级固定为 `INVALID → DIV_ZERO → OVERFLOW → UNDERFLOW`；`.ieee` 不 trap，返回 ±inf/NaN。
- 整数 `div` 向零截断；`mod` 余数符号随被除数；`div(INT_MIN, −1)`、`abs(INT_MIN)`、`neg(INT_MIN)` strict 溢出；`mod(INT_MIN, −1) = 0`。
- `shr` 在 `k < n` 时 signed 算术右移、unsigned 逻辑右移；`k >= n` 时 `shr` 返回 0，`shl` 按 strict/`.wrap` 规则处理。
- `.nsw`/`.nuw` 只允许在 `@fast` 函数内的 `add`/`sub`/`mul`/`shl` 上；其它位置报告 `TC_CE_ANNOTATION`。它们不改变默认语义，只是优化承诺。

### 6.4 比较与逻辑

- `eq`/`ne`/`lt`/`le`/`gt`/`ge` 的类型参数可以是整数或浮点；`eq`/`ne` 也接受 `bool`；结果固定为 `bool`。
- 浮点 NaN 语义：`eq/lt/le/gt/ge` 对 NaN 返回 false，`ne` 返回 true；浮点比较不 trap。
- 整数按数学值比较；无符号按非负值；`bool` 只支持 `eq`/`ne`，按真值比较。
- 指针与 `fnptr` 不使用本组；指针用 `ptreq`～`ptrge`，函数指针用 `funceq`/`funcne`。
- 逻辑 `and`/`or` **不短路**；短路必须用 `br_if`/`br` 显式构造。编译器不得把 `and`/`or` 改写为条件分支而不保持严格求值语义。
- `br_if c, L` 的 `c` 必须是 `bool`；否则报告 `TC_CE_CONDITION_TYPE`。`switch x, …` 的 `x` 必须是整数；否则报告 `TC_CE_SWITCH_TYPE`。

### 6.5 转换与字面量

- `cast T, x`：数值或 `bool` 转换；指针不参与 `cast`（报告 `TC_CE_TYPE_MISMATCH`）；范围检查在运行期，失败报告 `TC_RE_CAST_OVERFLOW`。
- `trunc T, x`：仅整数窄化；非整数源/目标报告 `TC_CE_TYPE_MISMATCH`；目标位宽 ≥ 源报告 `TC_CE_TRUNC_WIDTH`；保留低位，无运行时检查。
- `bitcast T, x`：先查类别矩阵，再查位宽；类别禁止报告 `TC_CE_TYPE_MISMATCH`，允许但不等宽报告 `TC_CE_BITCAST_WIDTH`；`nullptr` 非法；`bool`/结构体禁止。
- 字面量在转换操作数位置按 §3.6 的源类型规则定型；普通指令位置按期望类型定型。字面量类别/后缀/符号性不符报告 `TC_CE_LITERAL_TYPE`；值不可表示报告 `TC_CE_LITERAL_RANGE`。
- `sizeof T` 返回字节数 `usize`，是编译期常量；`sizeof(void)` 报告 `TC_CE_VOID`；结构体布局溢出报告 `TC_CE_LAYOUT_OVERFLOW`。

### 6.6 内存与指针

| 指令 | 类型/操作数 | 关键约束 |
| ---- | ----------- | -------- |
| `load T, p` | `p: ptr<T>`，`T` 值类型 | unsafe；空指针 `TC_RE_NULL_POINTER`；越界/悬垂/未初始化读取 UB；代码区/区外/设备拒绝 `TC_RE_ADDR_UNAVAILABLE` |
| `load.volatile T, p` | 同上 | 不得合并/删除/重排相对其它 volatile 访问 |
| `store T, p, v` | `p: ptr<T>`，`v: T` | unsafe；只读区 `TC_RE_WRITE_PROTECTED`；空指针 `TC_RE_NULL_POINTER` |
| `store.volatile T, p, v` | 同上 | 同上，保持 volatile 顺序 |
| `memmove T, dst, src, n` | `dst/src: ptr<T>`，`n: usize` | unsafe；按元素复制；重叠等价于临时缓冲；空指针 trap；越界/悬垂/未初始化读取 UB |
| `addr T, target` | 槽/字段路径 | safe；`const` 不可取址 → `TC_CE_ADDR_OF_CONST`；参数地址只读 |
| `ptradd T, p, n` | `p: ptr<T>`，`n: usize` | unsafe；按字节 `p + n*sizeof(T)`；空指针 trap；越界/悬垂 UB |
| `ptrsub T, p, n` | 同上 | unsafe；同上 |
| `ptrdiff T, p, q` | `p/q: ptr<T>` | safe；返回字节差 `isize`；不可表示 `TC_RE_PTRDIFF_OVERFLOW` |
| `ptreq`/`ptrne`/`ptrlt`/`ptrle`/`ptrgt`/`ptrge` | `ptr<T>`/`nullptr` | safe；按地址比较；空指针比较不 trap；跨对象序环境定义 |
| `sizeof T` | 类型 | safe；编译期常量 |

- `load`/`store` 的 `T` 可为标量、指针、`fnptr`、结构体；对齐不要求；端序环境定义；访问范围必须完全位于一个 live 对象/子对象内。
- `bool` 的 `load` 必须规范化；`store` 写 `0x00`/`0x01`。
- `addr` 产生指向 live 对象/子对象的指针；对象生命期结束后指针悬垂。`const` 无地址。
- 指针算术结果必须位于同一对象的 `[start, one-past]` 内；`ptradd` 从 one-past 只允许 `n=0`；`ptrsub` 可从 one-past 回到对象内；dangling 指针任何算术都是 UB。
- 空指针分类固定：`load`/`store`/`memmove`/`ptradd`/`ptrsub` 使用 `TC_RE_NULL_POINTER`；`call_indirect` 使用 `TC_RE_NULL_FUNCTION_POINTER`。不得混用。
- 编译器不检查越界指针解引用；越界、悬垂、one-past 访问与未初始化读取属于 UB，不要求静态或运行期检出（checked build 可选，见 §13）。

### 6.7 函数与结构体指令

- `funcaddr f`：`f` 是 `Self.f`/`m.f`；结果 `fnptr<sig>`；签名逐项一致；跨模块 `private` 报告 `TC_CE_PRIVATE_ACCESS`；不是编译期常量，不得进入 `const_expr`。
- `call f, args`：直接调用；`f` 是 `Self.f`/`m.f`；实参从左到右读取，类型严格匹配；可按位置或命名实参；命名实参必须匹配、唯一、完整、保持声明顺序。
- `call_indirect fp, args`：`fp` 必须为 `fnptr<…>`；只按位置传参；命名实参报告 `TC_CE_UNKNOWN_ARGUMENT`；空指针运行期 `TC_RE_NULL_FUNCTION_POINTER`；伪造或签名不符是 UB；所有间接调用在 unsafe 上下文。
- 调用结果：LHS 类型必须严格等于返回类型；`void` 调用只能写 `_`，写入非 `_` 目标报告 `TC_CE_VOID`。任何有结果的指令都可以用 `_` 丢弃结果，包括非 `void` 的 `call`。
- 调用 `@unsafe` 函数在 unsafe 上下文外报告 `TC_CE_UNSAFE_CALL`；调用 `@fast`/`@fast_math` 函数在 unsafe 上下文外报告 `TC_CE_FAST_CALL`。
- `funceq`/`funcne`：显式类型参数为 `fnptr<…>`；两个操作数同型或其一为 `nullptr`；结果 `bool`；通用 `eq`/`ne` 不接受 `fnptr`。
- `make S, …`：字段按声明顺序，全部必填，类型严格；命名形式 `field: value` 必须匹配、唯一、完整、保持顺序；padding 写 `0x00`。
- `copy T, x`：`x` 的类型必须严格等于 `T`；标量/指针/`fnptr` 复制值；结构体整块复制（含 padding）；字段指针复制地址；`copy` 不 trap。

### 6.8 控制指令

- `label L:` 声明标签；不产生运行时动作；同一函数/程序内标签重名报告 `TC_CE_DUPLICATE_LABEL`；标签与值名分属不同命名空间。
- `br L` 跳到同一函数/程序的标签；目标不存在报告 `TC_CE_LABEL_NOT_FOUND`；允许前向/后向跳转；允许跳过 `var` 声明，读取未初始化槽由 DI 报告。
- `br_if c, L`：`c` 必须为 `bool`；true 跳转，false 落到下一条；条件不是 `bool` 报告 `TC_CE_CONDITION_TYPE`。
- `switch x, L0, …, Ld`：`x` 必须为整数；`0→L0, …, n−1→Ln−1`，其它（含负数）→ `Ld`；`Ld` 必须存在；非整数报告 `TC_CE_SWITCH_TYPE`。
- `return [x]`：只在函数体内；`#program` 顶层报告 `TC_CE_RETURN_OUTSIDE_FUNCTION`；`void` 函数无操作数，非 `void` 必须有操作数；形态不符报告 `TC_CE_RETURN_FORM`；类型不符报告 `TC_CE_RETURN_TYPE`。
- `trap`：立即以 `TC_RE_TRAP` 终止；不提交任何目标；不执行清理。
- 非 `void` 函数所有可达路径必须被 `return`/`trap`/无出口 `br` 阻止；否则报告 `TC_CE_MISSING_RETURN`。`void` 函数可以自然落到 `end`。

### 6.9 指令诊断与位置

| 错误码 | 主位置 |
| ------ | ------ |
| `TC_CE_OPERAND_COUNT` | 指令助记符 |
| `TC_CE_TYPE_MISMATCH` | 首个不匹配操作数；结果类型不符时定位到 LHS 或 RHS 首 Token |
| `TC_CE_MODE` | 模式后缀 Token |
| `TC_CE_LITERAL_TYPE` / `TC_CE_LITERAL_RANGE` | 字面量首 Token |
| `TC_CE_BITCAST_WIDTH` | 源操作数首 Token |
| `TC_CE_TRUNC_WIDTH` | 目标类型 Token |
| `TC_CE_CONDITION_TYPE` | `br_if` 条件操作数 |
| `TC_CE_SWITCH_TYPE` | `switch` 值操作数 |
| `TC_CE_LABEL_NOT_FOUND` | 分支指令的标签名 |
| `TC_CE_ARGUMENT_*` | 见 §8.8 |
| `TC_CE_UNKNOWN_FIELD` / `TC_CE_MISSING_FIELD` / `TC_CE_DUPLICATE_FIELD` / `TC_CE_FIELD_ORDER` | `make` 的字段名或结构体名 |

语法形态错误优先于类型/模式错误；同一位置先报告专用码，再报告通用码。指令级检查失败后，编译器不得继续用该指令的结果推导后续类型或 DI。

---

## 7. 控制流核心

本章对应 [语言标准 §7] 与 [语言标准 §12.4]。控制流语义以语言标准为准；本章固定 CFG 构建、可达性、确定初始化与函数末尾检查的确定性算法。

### 7.1 控制形式与标签

TC 的控制流只有：

```text
label L:
_ = br L
_ = br_if c, L
_ = switch x, L0, …, Ld
_ = return x
_ = return
_ = trap
```

- `label` 是声明，不是指令；标签在函数/程序内建立独立命名空间，与值名分离。
- 同一函数/程序的标签命名空间内重名报告 `TC_CE_DUPLICATE_LABEL`；`br`/`switch` 目标不存在报告 `TC_CE_LABEL_NOT_FOUND`。
- 标签可以前向/后向；可以跳过 `var` 声明；读取未初始化槽由 DI 报告。
- 标签不得跨函数/程序；每个函数/程序有独立标签表。
- `return` 只作为指令助记符存在，前面必须有 `_ =`。

### 7.2 CFG 构建

编译器必须为 `#program` 顶层与每个函数分别建立封闭 CFG：

1. 程序入口是 `#program` 第一条可执行指令；函数入口是函数体第一条指令。每个函数入口都是独立分析根，不因函数未被调用而把函数体判为不可达。
2. 普通指令连接源序后继；`var`/`const`/`label` 建立相应节点。`const` 是无运行时效果的身份节点；`label` 是基本块入口。
3. `br L` 只连接目标标签；`br_if c, L` 连接目标标签与源序后继两条边；`switch x, L0, …, Ld` 连接全部标签（含默认标签）。**不得因常量条件或常量选择器裁剪任何边**。
4. `return` 只连接当前函数的正常返回终止节点；`trap` 只连接终止节点；两者都没有函数内后继。
5. `call` 在调用者 CFG 中是单个原子节点，不展开被调函数 CFG；被调函数发生 trap 时程序终止，不形成返回调用者的 CFG 边。
6. 程序顶层自然落到文件末尾是正常终止；函数末尾节点表示未经 `return` 到达 `end`。
7. 顶层与各函数 CFG 是完整、封闭、彼此独立的分析单元；不得拼接、内联或共享节点、边、可达性、已初始化集合或固定点状态。

### 7.3 可达性

- 从程序入口/函数入口沿 CFG 边遍历，所得节点集合是该域的唯一可达集合。
- **不把不可达代码当错误**；编译器不得报告 `TC_CE_UNREACHABLE_STATEMENT` 或任何等价诊断。
- 不可达代码仍必须通过词法、语法、名称、类型、安全与构造检查；但不参与 DI、函数末尾可达性传播或其他运行时数据流。
- `br_if` 的常量条件仍保留两条边；不得因静态条件把分支体或循环体判为不可达。没有静态条件折叠。
- 只含 `const`/`label` 的节点仍是源级节点；空 suite 没有语句节点，不产生可达性或 DI 影响。

### 7.4 确定初始化数据流

确定初始化按 §5.6 的方程在可达子图上求最大固定点：

- `IN[program entry] = ∅`；`IN[function entry] = 全部参数`。
- `IN[n] = ∩ OUT[p]`，`p` 为全部可达前驱。
- `OUT[var x = rhs] = IN[n] ∪ {x}`；`OUT[x = rhs] = IN[n]`（`x` 必须已初始化）；`OUT[const] = IN[n]`；其它指令 `OUT = IN`。
- `U[n]` 包含当前函数形参与函数/程序内全部 `var` 槽；不包含 `const`、其它函数绑定或 `static var`。
- 以 `U[n]` 初始化非入口可达节点的 `IN`/`OUT`，再单调向下迭代至稳定；工作队列与遍历顺序不得改变结果。
- 最大固定点等价于“所有有限可达路径都已初始化”的路径语义；分支汇合取交集，循环回边参与交集。
- `call` 成功返回后按结果写入 LHS；`void` 调用或丢弃结果不改变 DI 状态。`read` 作为 `var` 初始化时加入新槽；作为已有槽赋值时要求目标已初始化。
- 不可达节点不定义 `IN`，不参与固定点。

### 7.5 函数末尾与缺失返回

- 非 `void` 函数：从入口可达的函数末尾节点报告 `TC_CE_MISSING_RETURN`；所有可达路径必须被 `return`、`trap` 或无出口 `br` 阻止。
- `void` 函数可以自然落到 `end`，视为隐式返回。
- `#program` 可以自然落到文件末尾，视为正常终止。
- 无出口循环、永久 `br` 循环与 `trap` 路径不产生缺少返回错误；`br_if` 的 false 边仍存在，因此常量真条件不能证明函数必返回。
- 多个函数都缺少返回时，按 `module_order` 与函数定义源序选择最早者；主位置为函数返回类型，比较位置使用函数体 `end`。

### 7.6 控制流诊断与位置

| 错误码 | 主位置 |
| ------ | ------ |
| `TC_CE_DUPLICATE_LABEL` | 第二次及后续标签名；关联首次声明 |
| `TC_CE_LABEL_NOT_FOUND` | 分支指令的标签名 |
| `TC_CE_CONDITION_TYPE` | `br_if` 条件操作数 |
| `TC_CE_SWITCH_TYPE` | `switch` 值操作数 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` | `return` 关键字 |
| `TC_CE_RETURN_FORM` | `return` 关键字或操作数 |
| `TC_CE_RETURN_TYPE` | 返回操作数 |
| `TC_CE_MISSING_RETURN` | 函数返回类型；比较位置为函数体 `end` |

---

## 8. 函数

本章对应 [语言标准 §8]。函数语法、参数、返回、调用、函数指针与注解以语言标准为准；本章固定签名收集、调用检查顺序、调用帧与调用图行为。

### 8.1 定义与签名

- 仅 `#lib` 允许 `func`；`#program` 定义函数报告 `TC_CE_PROGRAM_MODE_MISUSE`。
- 语法为 `[public|private] {annotation} func name (params) ret then body end`；`ret` 是值类型或 `void`。
- 函数名在模块内唯一，且不得与模块成员重名；重复报告 `TC_CE_DUPLICATE_MEMBER`。
- 函数体是语句序列；`end` 结束；`end` 后不得有同函数代码。
- 不支持重载、默认参数、可变参数、闭包、嵌套函数、匿名函数、方法、泛型函数、异常与尾调用保证。
- 所有函数签名在分析函数体前收集；函数可以调用后定义的函数；递归允许。

### 8.2 参数

- 形式为 `name: type`，可带注解；类型为标量、`ptr<T>`、`fnptr<…>` 或结构体；`void` 非法。
- 按值传递、只读、入口已初始化；形参名在签名内唯一，重复报告 `TC_CE_DUPLICATE_PARAMETER`。
- 形参名不得被函数体内的 `var`/`const` 重名；否则报告 `TC_CE_DUPLICATE_NAME`。
- 参数绑定本身不得赋值、字段赋值或作为 `read` 目标；否则报告 `TC_CE_PARAMETER_ASSIGNMENT`。`addr` 参数合法；经该地址写穿由运行期报告 `TC_RE_WRITE_PROTECTED`。
- 指针/结构体参数按值复制；结构体的指针字段复制地址；指针参数与调用者共享所指对象。

### 8.3 返回

- 非 `void` 函数的所有可达路径必须有 `return <operand>`；`void` 函数必须有裸 `return` 或自然落到 `end`。
- 返回操作数类型必须严格等于声明返回类型；否则报告 `TC_CE_RETURN_TYPE`。字面量以返回类型为期望类型检查。
- 非 `void` 函数不得用无值 `return`；`void` 函数不得带值；形态不符报告 `TC_CE_RETURN_FORM`。
- `return` 位于函数体外报告 `TC_CE_RETURN_OUTSIDE_FUNCTION`。
- 返回结构体按值复制；返回指针复制地址；返回 `fnptr` 复制入口地址。

### 8.4 调用

- 直接调用写作 `d = call Self.f, args` 或 `d = call m.f, args`；`void` 用 `_`。
- 间接调用写作 `d = call_indirect fp, args`；`fp` 是 `fnptr<…>`；只按位置传参；命名实参报告 `TC_CE_UNKNOWN_ARGUMENT`。
- 实参从左到右读取；类型严格匹配；字面量以形参类型为期望类型检查。
- 命名实参必须匹配形参、唯一、完整、保持声明顺序；重复 `TC_CE_DUPLICATE_ARGUMENT`，未知 `TC_CE_UNKNOWN_ARGUMENT`，缺失 `TC_CE_MISSING_ARGUMENT`，顺序不符 `TC_CE_ARGUMENT_ORDER`，位置实参数量不符 `TC_CE_ARGUMENT_COUNT`。
- 调用结果 LHS 必须严格等于返回类型；`void` 只能用 `_`；任何有结果的指令都可以用 `_` 丢弃结果。
- 调用 `@unsafe` 函数在 unsafe 上下文外报告 `TC_CE_UNSAFE_CALL`；调用 `@fast`/`@fast_math` 函数在 unsafe 上下文外报告 `TC_CE_FAST_CALL`。
- 递归允许；每次 `call` 前按抽象调用深度 `D` 检查：`depth == D` 报告 `TC_RE_STACK_OVERFLOW`；否则 `depth++`，`return` 递减。尾调用优化不得改变抽象深度计数或该 trap。
- `call_indirect` 的 `fp` 为 `nullptr` 报告 `TC_RE_NULL_FUNCTION_POINTER`；伪造或签名不符是 UB；函数指针不携带 `@unsafe` 信息，所有间接调用都在 unsafe 上下文。

### 8.5 函数指针

- `d = funcaddr f`：`f` 是 `Self.f`/`m.f`；结果 `fnptr<sig>`；签名逐项一致；跨模块 `private` 报告 `TC_CE_PRIVATE_ACCESS`；不是编译期常量，不得进入 `const_expr`。
- `fnptr` 可作参数、返回值、结构体字段；`nullptr` 兼容；`call_indirect` 只接受 `fnptr`，不接受整数或 `ptr`。
- `ptr` ↔ `fnptr` 禁止；`fnptr` ↔ 浮点禁止；`fnptr` ↔ 整数 `bitcast` 是 unsafe；同签名 `fnptr↔fnptr` 是 safe 重解释；跨签名 `bitcast` 禁止。
- `funceq`/`funcne` 只比较同型 `fnptr` 或其一为 `nullptr`；通用 `eq`/`ne` 不接受 `fnptr`。
- 不同函数入口地址必须互不相同；实现必须禁用 ICF 或等价合并。

### 8.6 注解与效果

| 注解 | 位置 | 含义 | 违反 |
| ---- | ---- | ---- | ---- |
| `@unsafe` | 函数 | 函数体是 unsafe 上下文；调用者需要 unsafe 上下文 | 静态检查调用上下文 |
| `@safe` | 函数 | 断言函数体不含 unsafe 指令/调用；与 `@unsafe`/`@fast` 互斥 | `TC_CE_SAFE_VIOLATION` |
| `@fast` | 函数 | unsafe 上下文 + 优化契约；调用者需要 unsafe 上下文 | 承诺违反是 UB |
| `@fast_math` | `@fast` 函数 | 仅 §10.3 的浮点因果例外 | 必须与 `@fast` 同用 |
| `@pure` | `@fast` 函数 | 无内存效果；不 `addr`/`load`/`store`/I/O；不调用非 `@pure`；结果只依赖参数值 | UB |
| `@readonly` | `@fast` 函数 | 无内存写入（普通局部槽写除外）；不 I/O、不读 volatile/设备；只调用 `@pure`/`@readonly` | UB |
| `@nounwind` | `@fast` 函数 | 函数体及动态调用范围内无 trap（含栈溢出） | UB |
| `@willreturn` | `@fast` 函数 | 有限步内返回或执行终止 trap；不会无限执行 | UB |
| `@no_overflow` | `@fast` 函数 | strict 有符号 `add/sub/mul/neg/abs`、`div` 溢出与全部 strict `shl` 不溢出（不覆盖除零） | UB |
| `@nonnull` | `@fast` 指针形参 | 非空 | UB |
| `@no_alias` | `@fast` 指针形参 | 对可达对象的内存访问必须经该参数或其合法派生指针 | UB |
| `@align(N)` | `@fast` 指针形参 | 地址按 N 对齐；N 必须是正的 2 的幂 | UB / `TC_CE_ANNOTATION` |
| `@range(lo,hi)` | `@fast` 整数形参 | 调用时值在 `[lo,hi]`；`lo/hi` 为同类型常量；`lo <= hi` | UB / `TC_CE_ANNOTATION` |
| `@padding(N)` | 结构体字段 | 字段后插入 N 个 `0x00` 字节；N 为非负十进制字面量 | `TC_CE_CONST_FORM` |
| `@inline`/`@noinline` | 任意函数 | 优化提示；不改变语义；二者互斥 | `TC_CE_ANNOTATION` |

- `@unsafe`/`@safe`/`@fast` 三者互斥；`@fast_math` 必须与 `@fast` 同用；`@inline`/`@noinline` 互斥。
- `@no_overflow`/`@pure`/`@readonly`/`@nounwind`/`@willreturn` 只在 `@fast` 函数上合法；`@nonnull`/`@no_alias`/`@align`/`@range` 只在 `@fast` 指针/整数形参上合法；否则报告 `TC_CE_ANNOTATION`。
- 未知注解、注解位置不合法、重复或冲突的注解报告 `TC_CE_ANNOTATION`。承诺内容违反是 UB，不是静态错误；编译器不得为承诺内容额外发明静态拒绝。
- `@inline`/`@noinline` 是优化提示，不产生 UB，不改变可观察行为。

### 8.7 调用帧与调用图

调用帧至少包含：被调函数标识、返回地址或继续点、按形参绑定的实参值、函数局部 `var` 槽、控制流状态、非 `void` 返回值位置。VM 与 AOT 的物理布局可以不同，可观察语义必须一致。

调用步骤：

1. 按形参声明顺序从左到右检查同一文本位置的实参名称与期望类型，并读取实参操作数。
2. 任一运行时错误立即终止整个程序。
3. 创建被调函数帧并写入参数值。
4. 从函数体第一条指令开始执行。
5. 执行 `return` 时销毁函数帧并恢复调用者。
6. 非 `void` 结果写入调用点 LHS；`_` 表示丢弃；`void` 只能丢弃。

调用图是分析工具，不是静态拒绝规则：编译器可以为全部直接与间接调用建立调用图，用于优化、效果摘要与递归分析；允许递归，**不得**因调用图存在环而拒绝程序。调用图边不是 CFG 边，不参与 DI、可达性、缺少返回或控制流传播。

### 8.8 函数诊断与位置

| 错误码 | 主位置 |
| ------ | ------ |
| `TC_CE_DUPLICATE_MEMBER` | 重复函数名；关联首次声明 |
| `TC_CE_DUPLICATE_PARAMETER` | 第二次及后续参数名；关联首次参数 |
| `TC_CE_DUPLICATE_NAME` | 与参数或已有值绑定重名的内层声明 |
| `TC_CE_UNDEFINED_FUNCTION` | 调用目标的函数名 |
| `TC_CE_FUNCTION_SCOPE_ACCESS` | 函数体内裸名引用本模块成员 |
| `TC_CE_PRIVATE_ACCESS` | 限定名中的私有函数名 |
| `TC_CE_ARGUMENT_COUNT` | 调用助记符 |
| `TC_CE_DUPLICATE_ARGUMENT` | 第二个及后续重复实参名 |
| `TC_CE_UNKNOWN_ARGUMENT` | 最早未知实参名 |
| `TC_CE_MISSING_ARGUMENT` | 调用助记符；指出声明顺序最靠前的缺失形参 |
| `TC_CE_ARGUMENT_ORDER` | 第一个名称已知、集合完整但与对应位置形参不同的实参名 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` | `return` 关键字 |
| `TC_CE_RETURN_FORM` | `return` 关键字或操作数 |
| `TC_CE_RETURN_TYPE` | 返回操作数 |
| `TC_CE_MISSING_RETURN` | 函数返回类型；比较位置为函数体 `end` |
| `TC_CE_UNSAFE_CALL` / `TC_CE_FAST_CALL` | 调用助记符 |
| `TC_CE_SAFE_VIOLATION` | `@safe` 函数内的 unsafe 指令/调用 |

调用检查顺序：目标存在性/可见性 → 调用点 LHS 与返回类型形态 → 实参重复/未知/缺失/数量/顺序/类型 → 接收变量类型。前一检查失败时不再检查后续实参。

---

## 9. 安全模型

本章对应 [语言标准 §9]。安全上下文、unsafe 指令与 UB 以语言标准为准；本章固定编译器的上下文判定、穷尽表与诊断归属。

### 9.1 安全代码与不安全上下文

- **安全代码**：未标注函数/`@safe` 函数的函数体、`#program` 顶层（不含 `unsafe` 块），且不含 unsafe 指令/调用。
- **不安全上下文**：`@unsafe` 函数体、`@fast` 函数体、`unsafe` 块。
- 安全代码不得含 unsafe 指令/调用；否则报告 `TC_CE_UNSAFE_OPERATION`/`TC_CE_UNSAFE_CALL`。
- 安全代码不可达 UB；UB 只在 unsafe 上下文或 `@fast` 承诺违反中产生。
- `unsafe` 块不引入新作用域，不改变类型或运行时语义；嵌套 `unsafe` 块是冗余但合法的词法许可。

### 9.2 不安全指令与调用

以下指令/调用是 unsafe（穷尽）：

| 类别 | 内容 |
| ---- | ---- |
| 内存访问 | `load`、`store`、`load.volatile`、`store.volatile`、`memmove` |
| 指针算术 | `ptradd`、`ptrsub` |
| 间接调用 | `call_indirect` |
| 地址/位串重解释 | `bitcast` 指针↔整数、`fnptr`↔整数 |
| 不安全调用 | 直接 `call` 到 `@unsafe`/`@fast`/`@fast_math` 函数（在 unsafe 上下文外） |

以下**不是** unsafe：`addr`、`ptrdiff`、`ptreq`/`ptrne`/`ptrlt`/`ptrle`/`ptrgt`/`ptrge`、`funcaddr`、`funceq`/`funcne`、数值 `bitcast`、`ptr↔ptr` `bitcast`、同签名 `fnptr↔fnptr` `bitcast`、`cast`、`trunc`、全部算术/位/比较/逻辑、`make`、`copy`、`sizeof`、`read`/`write`/`writeln`、`trap`。

### 9.3 unsafe 块与函数注解

- `unsafe then … end`：词法许可，不改变类型/运行时；可嵌套。
- `@unsafe func`：整个函数体是 unsafe 上下文；调用者需要 unsafe 上下文或 `unsafe` 块。
- `@safe func`：断言不含 unsafe；若含报告 `TC_CE_SAFE_VIOLATION`。
- 未标注函数按 safe 检查；可以在内部使用 `unsafe` 块。
- `@unsafe`/`@safe`/`@fast` 三者互斥；`@fast_math` 只与 `@fast` 同用。

### 9.4 安全诊断与位置

| 错误码 | 主位置 | 触发 |
| ------ | ------ | ---- |
| `TC_CE_UNSAFE_OPERATION` | unsafe 指令助记符 | safe 代码中的 unsafe 指令 |
| `TC_CE_UNSAFE_CALL` | 调用助记符 | safe 代码直接调用 `@unsafe` 函数 |
| `TC_CE_FAST_CALL` | 调用助记符 | safe 代码直接调用 `@fast`/`@fast_math` 函数 |
| `TC_CE_SAFE_VIOLATION` | 违规指令/调用 | `@safe` 函数体内含 unsafe 指令/调用 |

安全诊断属于 SEM，与类型/名称诊断一起按 `module_order` 与源序选择首诊；不得以警告或运行时 trap 代替静态拒绝。

### 9.5 UB 清单

UB 是穷尽的：

1. 悬垂指针的解引用、指针算术、`memmove` 或对原对象的访问（`copy`/返回/比较/`ptrdiff`/`bitcast ptr↔ptr` 是定义行为，不是 UB）。
2. 越出所属对象边界的访问或指针算术（`load`/`store`/`memmove`/`ptradd`/`ptrsub`）。
3. 经指针/`memmove` 读取未初始化存储。
4. 伪造或签名不符的 `fnptr` 调用。
5. `@fast` 承诺被违反（`@no_overflow`/`@no_alias`/`@pure`/`@readonly`/`@nounwind`/`@willreturn`/`@nonnull`/`@align`/`@range`/`.nsw`/`.nuw`）。

**不是 UB**：空指针解引用（trap）、整数溢出（trap）、除零（trap）、`cast` 溢出（trap）、未初始化变量的直接读取（静态错误）、`ptrdiff` 溢出（`TC_RE_PTRDIFF_OVERFLOW`）、`trap`（定义好的 trap）。经指针读取未初始化存储是 UB；`@fast_math` 之外的浮点重关联是实现错误；本语言没有并发，不存在数据竞争。

编译器不承诺编译期检出悬垂/越界；检出不改变合法程序集合，也不改变 UB 语义。

### 9.6 写保护与 const

- `const` 无存储、无地址；`addr` 报 `TC_CE_ADDR_OF_CONST`。
- 参数只读；`addr` 参数合法；经该地址 `store` 由运行期报告 `TC_RE_WRITE_PROTECTED`。
- `static var`/程序 `var` 可写；`addr` 可写。
- 不存在运行时“常量区”；`const` 已内联。只读区由只读参数槽构成；写只读参数槽报告 `TC_RE_WRITE_PROTECTED`；代码区/区外/设备拒绝报告 `TC_RE_ADDR_UNAVAILABLE`。

### 9.7 @fast 的安全含义

- `@fast` 是显式 unsafe 优化上下文；调用者必须处于 unsafe 上下文；函数内可以包含 unsafe 指令。
- 承诺违反是 UB；因此 `@fast` 调用不能出现在安全代码中。
- `@fast` 不改变类型/ABI；只改变优化假设。
- `@fast_math` 只放宽 §10.3 许可的浮点变换；不改变非浮点语义。

---

## 10. 优化契约

本章对应 [语言标准 §10]。优化不得改变语言标准规定的可观察行为；本章固定编译器与后端的 as-if 约束、`@fast` 承诺处理、地址可观察性与验证要求。

### 10.1 默认 as-if

默认优化（O0/O2）只做语义保持变换，必须保留：

- 全部静态诊断及其首个错误码；
- 实际执行路径上的 trap 存在性与顺序（默认模式；不包含依赖 §10.5 地址相关结果的部分）；
- 标准输出字节与顺序（默认模式；不包含依赖 §10.5 地址相关结果的部分）；
- 已定义程序的值结果、浮点 trap 存在性/顺序与终止/非终止行为（不包含地址相关结果的部分）；
- 安全代码的“无 UB”性质。

默认模式不得：删除可能 trap 的指令（除非能证明不 trap）；把 strict 运算静默改成 `.wrap`；做浮点重关联、FMA、FTZ 或 DAZ；把非终止执行变为终止，或把终止执行变为非终止；引入 UB。

### 10.2 @fast 承诺

`@fast` 函数是唯一的承诺/UB 区域；调用者必须处于 unsafe 上下文。承诺是优化器可以假设的契约，不产生静态检查；违反是 UB。

| 承诺 | 位置 | 含义 |
| ---- | ---- | ---- |
| `@no_overflow` | 函数 | strict 有符号 `add/sub/mul/neg/abs`、`div` 溢出与全部 strict `shl` 不溢出（不覆盖除零） |
| `.nsw` | 指令后缀 | 该指令有符号不溢出；只允许 `@fast` 内的 `add/sub/mul/shl` |
| `.nuw` | 指令后缀 | 该指令无符号不溢出；只允许 `@fast` 内的 `add/sub/mul/shl` |
| `@no_alias` | 指针形参 | 对可达对象的内存访问必须经该参数或其合法派生指针 |
| `@nonnull` | 指针形参 | 非空 |
| `@align(N)` | 指针形参 | 地址按 N 对齐 |
| `@range(lo,hi)` | 整数形参 | 调用时实参值在 `[lo,hi]`；`lo/hi` 为同类型常量 |
| `@pure` | 函数 | 无内存效果；不 `addr`/`load`/`store`/I/O；不调用非 `@pure`；结果只依赖参数值 |
| `@readonly` | 函数 | 无内存写入（普通局部槽写除外）；可读普通内存；不 I/O；不读 volatile/设备；只调用 `@pure`/`@readonly` |
| `@nounwind` | 函数 | 函数体及动态调用范围内无 trap（含栈溢出） |
| `@willreturn` | 函数 | 有限步内返回或执行终止 trap；不会无限执行 |

- `@pure`：普通局部/参数槽的值读写不构成内存效果；`load`/`store`/`memmove`、全局/静态/程序/宿主/设备/volatile 访问构成内存效果。不得使用 `addr`；经 `fnptr` 间接调用时，被调函数也必须满足动态契约。
- `@readonly`：普通局部槽写入不构成内存写入；可读取普通内存；不得写全局/静态/程序/宿主/设备/volatile，不得 I/O，不得 `load.volatile`/设备访问；经 `addr` 取得指针后的 `load` 算读取、`store` 算写入。
- `@no_alias`：合法派生沿 `copy`、`bitcast ptr↔ptr`、`ptradd`/`ptrsub` 以及经派生指针 `load` 取得的嵌套指针传递；仅创建、复制、比较、`bitcast`、`ptradd`/`ptrsub` 派生指针不构成违反。
- `@nounwind`：函数体及其动态调用范围内不发生任何 trap，包括 `trap`、算术/转换/内存 trap、`call_indirect` 空指针与 `TC_RE_STACK_OVERFLOW`。
- `.nsw`/`.nuw`：与 `@no_overflow` 同时出现时两者都生效，任一违反即 UB；没有优先级。
- `@willreturn`：不承诺无 trap，也不承诺有限时间内的具体步数。

### 10.3 @fast_math

- 只允许与 `@fast` 同用；调用者必须处于 unsafe 上下文。
- 允许重关联、FMA、FTZ/DAZ、放宽 NaN/Inf 假设。
- 仅允许改变由浮点变换引起的浮点值结果、浮点 trap（`TC_RE_FLOAT_*`、浮点 `TC_RE_DIV_ZERO`）的存在性/顺序，以及由这些浮点值决定的控制流/终止。
- 非浮点值、非浮点指令自身的 trap 条件及与许可浮点变换无关的行为仍按默认规则；I/O 和内存结果只有在控制流确实由被放宽的浮点值决定时才允许不同。
- 默认模式（含 `.ieee`）禁止这些变换；`.ieee` 只改变异常处理，不是 fast-math。

### 10.4 优化级别

| 级别 | 语义 |
| ---- | ---- |
| O0 | 不做优化；保持源序/trap 顺序；地址仍环境定义 |
| O2 | as-if + 语义保持优化；不使用 UB 假设 |
| `@fast` | 在 `@fast` 函数内使用承诺/UB 优化；其它区域仍 O2 |
| `@fast_math` | 仅 §10.3 的浮点因果例外 |

优化级别是实现/工具链选择，不是语言语义。同一程序在 O0/O2 下的已定义行为必须相同，使用同一 `D` 与同一抽象深度/trap 规则；§10.5 的地址相关结果除外。`@fast`/`@fast_math` 是源级 opt-in，不是全局 `-O3` 开关。

### 10.5 地址可观察性

地址相关结果包括：

- `bitcast ptr→int` 的结果（unsafe）；
- 跨对象或悬垂指针的 `ptreq`/`ptrne`/`ptrlt`/`ptrle`/`ptrgt`/`ptrge` 结果；
- 跨对象或悬垂指针的 `ptrdiff` 结果。

地址相关结果环境定义，且不保证跨优化级别稳定；依赖它们的控制流、输出、trap 或终止不属于跨优化确定。同一对象（含 one-past）内的指针比较与 `ptrdiff` 是确定的；`nullptr` 的相等比较是确定的；`ptrdiff` 结果不可由 `isize` 表示时 trap。优化可以改变帧布局/静态布局，但不得改变不依赖地址相关结果的值、输出和 trap。

### 10.6 别名与效果

- 默认保守：不得跨 `store`/`call`/volatile 访问重排可能观察内存的 `load`。
- `@no_alias` 允许优化器假设不存在经非派生指针的访问；`@pure`/`@readonly` 允许在无潜在别名写的情况下移动读取/调用。
- 删除一个结果未使用的调用还需 `@nounwind`，并且要么函数带 `@willreturn`，要么优化器能证明该调用终止。`@nounwind` 本身不构成终止证明。
- `@nounwind` 允许删除异常边。

### 10.7 优化验证

编译器/后端必须验证：

- 优化前后静态诊断与首个错误码一致；优化不得把非法程序变成合法程序，也不得把合法程序变成非法程序。
- 默认模式下 trap 的存在性/顺序、标准输出字节/顺序、已定义程序的值结果与终止行为不变。
- 浮点常量求值与运行时使用同一精度/舍入/异常规则；不得依赖宿主 `fenv`、扩展精度、FMA、FTZ/DAZ。
- 地址相关结果只在 §10.5 允许的范围内变化；不得把环境定义结果当作跨优化常量。
- 优化不得在安全代码中引入 UB；不得把安全代码的越界/悬垂检查省略成未定义行为。

---

## 11. 标准 I/O 与运行时

本章对应 [语言标准 §11]。核心 I/O 语义以语言标准为准；本章固定编译器的静态验证、输入 Token 解析、输出格式与提交规则。

### 11.1 核心 I/O

- 只有 `read T`、`write T, x`、`writeln T, x`；`T` 必须是标量类型。
- 没有格式说明符、字符串、字段整块 I/O、文件/参数/环境 API。`%` 在注释外是 `TC_CE_SYNTAX`，不是格式说明符起始。
- `write`/`writeln` 的整条输出要么全部提交，要么 `TC_RE_IO` 且贡献 0 字节；已提交的前缀不回滚。
- `read` 失败不修改目标（`read` 以 LHS 为目标，失败时 LHS 不提交）。
- 无文件 I/O、命令行参数、环境变量、时间、随机数；这些由宿主/运行时库提供，不属于核心的确定性保证。

### 11.2 输入

`read T` 跳过前导 ASCII 空白（`0x09`–`0x0D`、`0x20`），读取一个完整 Token：

- 有符号整数：可选 `-` + 十进制数字；无 `+`、进制前缀、分隔符；输入允许前导零。
- 无符号整数：十进制数字；不接受 `-`。
- `bool`：`true`/`false`（小写）。
- 浮点：十进制整数/小数/指数；`inf`/`-inf`/`nan`；不接受 `f` 后缀、`-nan`、`+`；小数点两侧要有数字；`nan` 产生 canonical quiet NaN（正号）。
- 输入 Token 必须完整匹配目标格式；`12abc` 整体非法，不得只接受前缀 `12`。
- 浮点输入先解析精确十进制值，再按 roundTiesToEven 到目标格式；非零→0 或有限→inf 报告 `TC_RE_IO`；可表示的非规格化数合法。
- ASCII 空白仅指 `0x09`–`0x0D` 与 `0x20`，不受 locale 影响。
- 读取失败（宿主 I/O 失败、非法 Token、超范围、非预期 EOF）统一报告 `TC_RE_IO`；失败时目标槽保持原值，随后程序终止。

静态验证：

- `read T` 的 `T` 必须是标量；`void` 报告 `TC_CE_VOID`，`ptr`/`fnptr`/结构体报告 `TC_CE_TYPE_MISMATCH`；不得接受非标量。
- `read` 的 LHS 必须是可写 `var`/`static var`/程序 `var` 或字段路径；`const` → `TC_CE_CONST_ASSIGNMENT`；参数 → `TC_CE_PARAMETER_ASSIGNMENT`；未解析 → `TC_CE_UNDEFINED_NAME`。
- `var x: T = read T` 是初始化；`x = read T` 要求 `x` 已确定初始化；字段目标要求基址已初始化。

### 11.3 输出

| 类型 | 输出格式 |
| ---- | -------- |
| 有符号整数 | 十进制，可带 `-`；`0` 输出 `0`；无前导零 |
| 无符号整数 | 十进制；无前导零 |
| `bool` | `true`/`false` |
| `float32`/`float64` | `%g` 风格，6 位有效数字；小写 `e`；`inf`/`-inf`/`nan`；负零输出 `-0` |

- `%g` 规则：先按 6 位有效数字 roundTiesToEven；十进制指数 `e`；`e < −4` 或 `e >= 6` 用科学计数法，否则定点；删除小数末尾 0 与空小数点。
- 指数至少两位；负零保留符号；NaN 不带符号（即使位模式符号位为 1）。
- 十进制输出以源 binary32/64 的精确数学值为输入，按 roundTiesToEven 舍入；不得经宿主低精度类型。
- `write T, x` 的 `x` 类型必须严格等于 `T`；`T` 必须是标量；字面量以 `T` 为期望类型检查。`writeln` 在输出末尾追加 LF。

### 11.4 运行时库

格式化输出、字符串、数组/切片、分配、容器、错误处理属于运行时库；它们可以调用核心指令，但不得改变核心语义。运行时库 API 不在本标准中定义；本标准只保证核心 I/O 的确定性。

宿主分配对象必须由至少 1 个 `T` 元素组成（`n >= 1`）；零长度请求不创建核心对象，由库报错或返回库级错误。宿主对象与核心对象遵循同一套边界/生命期规则；分配/释放 API 不在核心。

### 11.5 I/O 诊断与位置

| 错误码 | 主位置 | 触发 |
| ------ | ------ | ---- |
| `TC_CE_TYPE_MISMATCH` | 操作数或目标标识符 | `write` 的 `x` 类型与 `T` 不一致；`read` 目标声明类型与 `T` 不一致 |
| `TC_CE_CONST_ASSIGNMENT` | `read` 目标 | `read` 目标为 `const`/`Self.K`/`m.K` |
| `TC_CE_PARAMETER_ASSIGNMENT` | `read` 目标 | `read` 目标为参数 |
| `TC_CE_UNINITIALIZED` | `read` 目标 | 已有槽作为 `read` 目标但未确定初始化 |
| `TC_RE_IO` | 当前 I/O 指令 | 输入非法/超范围/EOF、标准输入读取失败、输出写入失败 |

`write`/`writeln` 的原子提交在运行期完成：只有全部字节提交成功才追加到标准输出；失败时贡献 0 字节。编译器不得把 I/O 重排到可能改变可观察字节顺序的位置。

---

## 12. 诊断

本章对应 [语言标准 §12]。诊断模型、首个诊断顺序与完整码集以语言标准为准；本章固定编译器实现细节、打印名约定与定位规则。

### 12.1 诊断模型

- 静态阶段：PARSE（逐模块 LEX→SYN）→ IMPORT（导入解析/签名）→ SEM → CT。前序阶段有诊断即停止；CT 内部按需求值。
- 运行时：遇到首个 trap 即终止；trap 有确定的错误码。
- fail-fast：静态阶段只报告首个规范诊断；运行时只报告首个 trap。
- 无编译警告：所有规范诊断都是错误。
- 诊断位置是源文件的行/列/跨度；同一位置多个候选码按“专用码优先”选取。

### 12.2 首个诊断顺序

1. **阶段优先**：PARSE → IMPORT → SEM → CT → RT。
2. **模块遍历顺序**：每个阶段内，从根模块开始，按 `import` 源序深度优先遍历，首次访问为准；CT 的依赖求值例外见 §4.6。
3. **同模块源序**：行升序；同行列升序；同位置专用码优先。
4. **条款内顺序优先**：浮点异常优先级、负移位计数先行、空指针分类先行等。
5. **布局诊断归属 SEM**：`TC_CE_LAYOUT_OVERFLOW` 在 `struct` 声明处计算，按 SEM 的模块遍历/源序参与首诊，不进入 CT。

IMPORT 码在码表中标为 SEM 子阶段，但报告时机是 IMPORT；实现必须遵守阶段优先，不得把它们推迟到 SEM。

### 12.3 完整诊断码表

完整集合为 **80** 码 = 65 个 `TC_CE_*` + 15 个 `TC_RE_*`；正文未列出的码不存在。打印名是实现约定，错误码才是规范标识；不同实现的打印文本可以本地化，但错误码、阶段与触发条件必须一致。

#### 12.3.1 词法/语法

| 码 | 打印名 | 阶段 | 触发 |
| -- | ------ | ---- | ---- |
| `TC_CE_SYNTAX` | `SyntaxError` | LEX/SYN | 非法 UTF-8/BOM/U+0000/注释外非 ASCII/未列出字符；Token 序列不符合语法；注释外的 `/`、`//` 或 `;`；`DISCARD` 出现在只接受 `IDENT` 的位置 |
| `TC_CE_MISSING_END` | `MissingEnd` | SYN | `func`/`struct`/`unsafe` 缺 `end` |
| `TC_CE_MODULE_LAYER` | `ModuleLayerError` | SYN | 模块层序交错 |
| `TC_CE_MISSING_VISIBILITY` | `MissingVisibility` | SYN | `#lib` 成员缺 `public`/`private` |
| `TC_CE_PROGRAM_MODE_MISUSE` | `ProgramModeMisuse` | SYN | `#program` 使用 `func`/`static`/`public`/`private`/`Self`，或 `#lib` 使用顶层 `var`/指令 |
| `TC_CE_VAR_MISSING_INIT` | `VarMissingInitializer` | SYN | `var` 缺 `=` 或初始化器 |
| `TC_CE_LITERAL_LEX_RANGE` | `LiteralLexRange` | LEX | 整数字面量 > 2^64−1；浮点字面量舍入 0/±inf |
| `TC_CE_ANNOTATION` | `AnnotationError` | SEM | 未知注解/位置非法/注解冲突/承诺不在 `@fast` |
| `TC_CE_OPERAND_COUNT` | `OperandCountError` | SEM | opcode 的操作数数量/形状不符 |

#### 12.3.2 名称/模块

| 码 | 打印名 | 阶段 | 触发 |
| -- | ------ | ---- | ---- |
| `TC_CE_UNDEFINED_NAME` | `UndefinedName` | SEM | 名字未解析到槽/`const`/成员 |
| `TC_CE_DUPLICATE_NAME` | `DuplicateName` | SEM | 同一函数/程序的值名字空间内 `var`/`const`/参数重名 |
| `TC_CE_DUPLICATE_MEMBER` | `DuplicateMember` | SEM | 模块成员重名 |
| `TC_CE_DUPLICATE_PARAMETER` | `DuplicateParameter` | SEM | 形参重名 |
| `TC_CE_FUNCTION_SCOPE_ACCESS` | `FunctionScopeAccess` | SEM | 函数体内裸名引用本模块成员 |
| `TC_CE_UNDEFINED_FUNCTION` | `UndefinedFunction` | SEM | 函数名未解析 |
| `TC_CE_PRIVATE_ACCESS` | `PrivateAccess` | SEM | 访问 `private` 成员 |
| `TC_CE_PRIVATE_TYPE` | `PrivateType` | SEM | `private` 类型出现在 `public` 签名、`public struct` 字段、`public const`/`public static var` 声明类型或初始化器 |
| `TC_CE_IMPORT_NOT_FOUND` | `ImportNotFound` | SEM | 找不到 `m.tc` |
| `TC_CE_IMPORT_NOT_LIB` | `ImportNotLib` | SEM | 导入目标不是 `#lib` |
| `TC_CE_DUPLICATE_IMPORT` | `DuplicateImport` | SEM | 重复导入 |
| `TC_CE_CIRCULAR_IMPORT` | `CircularImport` | SEM | 自导入/循环导入 |
| `TC_CE_IMPORT_NAME_CONFLICT` | `ImportNameConflict` | SEM | 导入名与本地名冲突 |
| `TC_CE_DUPLICATE_LABEL` | `DuplicateLabel` | SEM | 同函数/程序的标签命名空间内重名 |
| `TC_CE_LABEL_NOT_FOUND` | `LabelNotFound` | SEM | `br`/`switch` 目标不存在 |

#### 12.3.3 类型/声明/控制

| 码 | 打印名 | 阶段 | 触发 |
| -- | ------ | ---- | ---- |
| `TC_CE_TYPE_MISMATCH` | `TypeMismatch` | SEM | 赋值/传参/返回/操作数类型或目标形态不符，含 `bitcast`/`cast` 的类别禁止组合 |
| `TC_CE_LITERAL_TYPE` | `LiteralType` | SEM | 字面量类别/后缀/符号性与期望不符 |
| `TC_CE_LITERAL_RANGE` | `LiteralRange` | SEM | 字面量值在源类型或期望类型中不可表示 |
| `TC_CE_MODE` | `ModeError` | SEM | 运算/类型/模式组合非法 |
| `TC_CE_CONDITION_TYPE` | `ConditionType` | SEM | `br_if` 条件不是 `bool` |
| `TC_CE_RETURN_TYPE` | `ReturnType` | SEM | `return` 类型不符 |
| `TC_CE_RETURN_FORM` | `ReturnForm` | SEM | `void`/非 `void` 的 `return` 形态不符 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` | `ReturnOutsideFunction` | SEM | 函数外 `return` |
| `TC_CE_MISSING_RETURN` | `MissingReturn` | SEM | 非 `void` 函数有路径到 `end` |
| `TC_CE_ARGUMENT_COUNT` | `ArgumentCount` | SEM | 位置实参数量不符 |
| `TC_CE_ARGUMENT_ORDER` | `ArgumentOrder` | SEM | 命名实参顺序不符 |
| `TC_CE_UNKNOWN_ARGUMENT` | `UnknownArgument` | SEM | 命名实参未知 |
| `TC_CE_DUPLICATE_ARGUMENT` | `DuplicateArgument` | SEM | 命名实参重复 |
| `TC_CE_MISSING_ARGUMENT` | `MissingArgument` | SEM | 命名实参缺失 |
| `TC_CE_UNKNOWN_FIELD` | `UnknownField` | SEM | `make`/字段访问未知字段 |
| `TC_CE_MISSING_FIELD` | `MissingField` | SEM | `make` 缺字段 |
| `TC_CE_DUPLICATE_FIELD` | `DuplicateField` | SEM | `make` 重复字段，或结构体字段重名 |
| `TC_CE_STRUCT_SELF_VALUE` | `StructSelfValue` | SEM | 结构体值位置直接/间接自引用（无限布局；`fnptr` 签名除外） |
| `TC_CE_EMPTY_STRUCT` | `EmptyStruct` | SEM | 结构体没有字段 |
| `TC_CE_LAYOUT_OVERFLOW` | `LayoutOverflow` | SEM | 结构体布局求和溢出 `usize` |
| `TC_CE_FIELD_ORDER` | `FieldOrder` | SEM | `make` 字段顺序不符 |
| `TC_CE_IMMUTABLE_FIELD` | `ImmutableField` | SEM | 对 `readonly` 字段赋值，或字段赋值路径经过 `readonly` 字段 |
| `TC_CE_CONST_ASSIGNMENT` | `ConstAssignment` | SEM | 对 `const`/`Self.K`/`m.K` 赋值 |
| `TC_CE_PARAMETER_ASSIGNMENT` | `ParameterAssignment` | SEM | 对参数赋值/`read` 目标 |
| `TC_CE_UNINITIALIZED` | `Uninitialized` | SEM | 读取/赋值/`read` 未确定初始化 |
| `TC_CE_ADDR_OF_CONST` | `AddrOfConst` | SEM | 对 `const` 取址 |
| `TC_CE_BITCAST_WIDTH` | `BitcastWidth` | SEM | `bitcast` 类别允许但位宽不等 |
| `TC_CE_TRUNC_WIDTH` | `TruncWidth` | SEM | `trunc` 目标位宽 ≥ 源 |
| `TC_CE_SWITCH_TYPE` | `SwitchType` | SEM | `switch` 值不是整数 |
| `TC_CE_VOID` | `VoidType` | SEM | `void` 作值类型/参数/`var`/转换目标 |

#### 12.3.4 安全/常量

| 码 | 打印名 | 阶段 | 触发 |
| -- | ------ | ---- | ---- |
| `TC_CE_UNSAFE_OPERATION` | `UnsafeOperation` | SEM | unsafe 指令不在 unsafe 上下文 |
| `TC_CE_UNSAFE_CALL` | `UnsafeCall` | SEM | 调 `@unsafe` 不在 unsafe 上下文 |
| `TC_CE_FAST_CALL` | `FastCall` | SEM | 调 `@fast`/`@fast_math` 不在 unsafe 上下文 |
| `TC_CE_SAFE_VIOLATION` | `SafeViolation` | SEM | `@safe` 函数含 unsafe 指令/调用 |
| `TC_CE_CONST_FORM` | `ConstForm` | SEM | `const` 初始化器含非法形态（`var`/`call`/`addr`/`load` 等），或 `@padding(N)` 的 `N` 不合 §2.6 |
| `TC_CE_CONST_OVERFLOW` | `ConstOverflow` | CT | 常量 strict 整数/浮点溢出或 strict `shl` 溢出 |
| `TC_CE_CONST_DIV_ZERO` | `ConstDivZero` | CT | 常量整数除零、strict 浮点除零（含 `mod` 零除数） |
| `TC_CE_CONST_SHIFT` | `ConstShift` | CT | 常量 `shl`/`shr` 计数为负 |
| `TC_CE_CONST_FLOAT_INVALID` | `ConstFloatInvalid` | CT | 常量 strict 浮点无效操作/NaN 输入 |
| `TC_CE_CONST_FLOAT_UNDERFLOW` | `ConstFloatUnderflow` | CT | 常量 strict 浮点下溢 |
| `TC_CE_CONST_CAST` | `ConstCast` | CT | 常量 `cast` 溢出 |

#### 12.3.5 运行时

| 码 | 打印名 | 触发 |
| -- | ------ | ---- |
| `TC_RE_INTEGER_OVERFLOW` | `IntegerOverflow` | 有符号 strict 溢出；`div(INT_MIN,−1)`/`abs`/`neg`/strict `shl` |
| `TC_RE_DIV_ZERO` | `DivisionByZero` | 整数除零；strict 浮点除零 |
| `TC_RE_NEGATIVE_SHIFT` | `NegativeShift` | 移位计数为负 |
| `TC_RE_FLOAT_OVERFLOW` | `FloatOverflow` | strict 浮点上溢 |
| `TC_RE_FLOAT_UNDERFLOW` | `FloatUnderflow` | strict 浮点下溢 |
| `TC_RE_FLOAT_INVALID` | `FloatInvalidOperation` | strict 浮点无效操作/NaN 输入 |
| `TC_RE_CAST_OVERFLOW` | `CastOverflow` | `cast` 结果不可表示 |
| `TC_RE_NULL_POINTER` | `NullPointer` | `load`/`store`/`memmove`/`ptradd`/`ptrsub` 空指针 |
| `TC_RE_PTRDIFF_OVERFLOW` | `PtrdiffOverflow` | `ptrdiff` 结果不可由 `isize` 表示 |
| `TC_RE_WRITE_PROTECTED` | `WriteProtected` | 只读参数槽写穿 |
| `TC_RE_ADDR_UNAVAILABLE` | `AddrUnavailable` | 代码区/区外/设备拒绝 |
| `TC_RE_STACK_OVERFLOW` | `StackOverflow` | 抽象调用深度达到环境定义上限 `D` |
| `TC_RE_NULL_FUNCTION_POINTER` | `NullFunctionPointer` | `call_indirect` 空指针 |
| `TC_RE_IO` | `IOError` | `read`/`write`/`writeln` 失败 |
| `TC_RE_TRAP` | `Trap` | `trap` 指令 |

### 12.4 诊断位置

除各章已给出的专用定位规则外，静态诊断按以下规则定位：

- 类型参数不受支持：定位到该类型参数 Token；类型参数合法但操作数不匹配：定位到源位置最早的不匹配操作数。
- RHS 自身合法但结果类型与接收目标不符：定位到 RHS 第一个 Token（或 LHS 目标，按专用规则）。
- 模式错误：定位到模式后缀；`bitcast` 位宽错误：定位到源操作数首 Token；`trunc` 位宽错误：定位到目标类型 Token。
- 字面量类型/范围：定位到完整字面量首 Token；负号属于字面量时定位到 `-`。
- `br_if` 条件类型：定位到条件操作数；`switch` 类型：定位到值操作数。
- 调用实参：重复定位到第二个重复名；未知定位到最早未知名；缺失定位到调用助记符；顺序定位到第一个位置不符的名称；数量定位到调用助记符。
- 重复定义：定位到第二个及后续名称；关联首个声明。私有访问：定位到限定名中的成员名；关联声明。
- 确定初始化：定位到最早未初始化的读取/目标标识符。
- 布局溢出：定位到触发溢出的字段或结构体声明。
- 运行时错误：定位到当前指令首 Token；I/O 失败定位到 I/O 指令；trap 定位到 `trap`。

### 12.5 运行时提交与终止

- 每条指令先读取全部实际需要的操作数并完成检查，再提交目标值。
- 算术、移位、转换、指针运算或函数调用发生运行时错误时，不提交当前指令的目标槽、调用返回值或新 `var` 的已初始化状态。
- `memmove` 在下标、区间或空指针检查失败时不修改目标的任何元素；`store` 在空指针或写保护检查失败时不提交写入。
- `read` 失败不修改目标；`write`/`writeln` 失败贡献 0 字节，已提交前缀不回滚。
- 首个运行时错误立即终止整个程序；不执行清理代码、后续语句或调用者续点，也不回滚此前已成功完成的赋值与 I/O。
- `switch` 的越界值选择默认标签；默认标签必须存在。
- `trap` 立即报告 `TC_RE_TRAP`，不提交任何目标。

### 12.6 实现资源错误

`TC_ERR_OUT_OF_MEMORY` 不属于 80 码语言诊断集，只表示实现为完成当前阶段所需的内部分配实际失败。它不是静态语义或运行时语义的一部分，不能用作固定长度、嵌套深度、标识符长度或程序规模限制的替代诊断。实现若事先施加资源上限，必须文档化并与真实资源失败区分。

固定实现约定：打印名 `OutOfMemory`，诊断消息 `memory allocation failed`，前缀保留 `TC_ERR_` 以区别于语言规范码。该错误不参与语言诊断的首诊排序；发生时应立即终止编译并报告资源失败。

---

## 13. 平台档案与实现验证

本章承载语言标准移交给实现的平台与环境事项。语言标准只定义环境可变的类别与确定性契约；本章规定编译器、VM 与 AOT 共享的档案字段、布局与验证要求。

### 13.1 环境定义项

以下项目属于环境定义，实现必须文档化，且同一程序在同一档案下的行为必须确定：

- 地址宽度与端序；
- 地址空间布局与基址、栈大小；
- 抽象调用深度上限 `D`；
- include 根目录及搜索顺序；
- 进程退出状态；
- 设备内存内容与设备拒绝行为；
- 经 `bitcast` 指针→整数观察到的地址值；
- 跨对象/悬垂指针比较与 `ptrdiff` 的结果。

依赖地址相关结果的控制流、输出、trap 或终止不属于跨优化确定（§10.5）。

### 13.2 平台档案

平台档案至少包含：

| 字段 | 说明 |
| ---- | ---- |
| 地址宽度 | 本实现固定 64 位目标字长；`isize`/`usize`/指针/`fnptr`/栈帧地址均为 64 位 |
| 端序 | 设备与内存的字节序；多字节 `load`/`store` 按目标端序转换 |
| 区表 | 帧区、静态区、设备区、代码区的基址与范围 |
| 栈大小 | 帧变量区与参数区；必须保证 `D` 个抽象帧可执行 |
| 地址 0 | 保留给 `nullptr`；不得放置 TC 可访问对象 |
| 代码入口 | 复位入口或由装载器提供 |
| stdout 汇 | UART、信号量或等价输出端点 |
| 代码合并 | 必须禁用 ICF 或等价入口合并 |

同一档案下，VM 与 AOT 必须生成相同的区内部偏移；32 位目标不属于本文定义的档案。

### 13.3 布局与地址

- 静态存储按依赖 DAG 的逆向拓扑序、模块内声明源序排列；每个静态元素按平台地址宽度对齐；同一档案下布局确定。
- 结构体布局按字段声明顺序紧凑排列，`@padding` 插入 0 字节；不做隐式对齐或尾部填充。
- 地址翻译是宿主到目标的映射，属实现定义且不可观察；实现可选择恒等映射或维护偏置。翻译不得改变 `bitcast` 观察到的目标地址值。
- 诊断构建（checked build）可选：函数返回造成的悬垂可用栈水位 trap，地址落在 `[stack_base, 当前帧基址)` 报告 `TC_RE_ADDR_UNAVAILABLE`；块退出造成的悬垂可用毒化/ASan 式检查。诊断构建不得放宽语言标准，也不得改变合法程序集合与已定义语义。
- 静态分析与外部工具不属于语言承诺；其结果不得改变语言定义的合法程序集合与已定义语义。

### 13.4 验证要求

编译器必须通过以下验证：

1. **静态一致性**：同一源程序在 VM 与 AOT 前端产生相同的诊断码与首个诊断；优化前后静态诊断不变。
2. **模块顺序**：`module_order`、IMPORT 报告顺序、SEM 首诊顺序在重复运行、不同哈希种子与不同线程数下一致。
3. **CFG/DI**：`br_if` 常量条件不裁剪边；不可达代码不报告错误；DI 最大固定点与路径语义一致；函数末尾检查只使用可达路径。
4. **类型与转换**：类别矩阵先于位宽检查；指针不参与 `cast`；`bitcast` 禁止项报告 `TC_CE_TYPE_MISMATCH`；`trunc` 只窄化。
5. **安全**：safe 代码不含 unsafe 指令/调用；`@safe` 违规报告 `TC_CE_SAFE_VIOLATION`；承诺违反不在静态阶段报错。
6. **浮点**：常量与运行时使用同一精度/舍入/异常优先级；不使用宿主扩展精度、FMA、FTZ/DAZ；异常顺序固定。
7. **I/O**：输入 Token 完整匹配；输出字节确定；失败提交规则符合 §11；不受 locale 影响。
8. **平台**：VM/AOT 对同一档案生成相同静态偏移；差分测试按地址字节差对拍，不比对绝对地址；checked build 的 trap 用例与静态诊断分开统计。

### 13.5 一致性声明

- 本文规定的静态接受集、首个诊断、错误码与运行时可观察行为均为必须遵守的规范性要求。
- 实现不得增加语言标准未定义的诊断码、警告或静态拒绝；不得以减少诊断为理由放宽语言标准。
- 实现可以限制程序规模或资源，但必须文档化，并使用 `TC_ERR_OUT_OF_MEMORY` 或明确的实现级错误报告，不得伪装成语言诊断。
- 语言标准与本文冲突时，以语言标准为准；本文未规定且语言标准未禁止的实现细节，由实现自由选择，但不得改变可观察行为。

---

*— 文档结束 —*
