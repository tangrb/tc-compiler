# TC 0.0.44 语言标准符合性审计报告

> **权威基准**：[TC 语言标准设计说明书 0.0.44](./TC语言标准设计说明书-0.0.44.md)（3463 行，下称「标准」）。本报告**只**以该文件为准；其它设计文档与 `src/` 实现均为被检查对象。
> **审计对象**：6 份设计文档 + 2 份参考/说明文档，以及 `src/**` 全部实现（lexer / parser / analyzer / executor / runtime / driver / embed / aot / libtc）。
> **方法**：文档逐条比对；实现以 `build/vm/bin/tc-vm`、`build/aot/bin/tc-aot`、`build/src/libtc/libtc.a` 实测为准（每条结论附最小复现）；并运行全部既有门禁与测试基线。
> **审计日期**：2026-09-11

---

## 0. 结论摘要

### 0.1 基线（审计前实测）

| 项目 | 结果 |
| ---- | ---- |
| `bash scripts/run_tests.sh`（VM + AOT 差分 + unit） | **全绿**（analyzer 302 / cfg 91 / executor 20 / stmt_index 18 / type-check 54 / struct-field-access 34 / embed 590 / endianness 16 / embed-aot 383） |
| `check_doc_counts.py` / `check_rhs_coverage.py` / `check_source_naming.py` / `check_type_fact_source.py` | **全部通过** |
| 附录 B 码数 vs `TcErrorKind` | **双向完全一致**：86 = 74 `TC_CE_*` + 12 `TC_RE_*`，实现枚举 87（+`TC_ERR_OUT_OF_MEMORY`） |
| §2.7 关键字集 vs 词法器关键字表 | **双向完全一致**（86 = 86） |
| 各设计文档指向语言标准的 `§` 交叉引用 | **全部指向存在的条款**（无死引用） |
| 各设计文档指向编译器标准的 `§` 交叉引用 | **全部存在** |

**即：所有既有门禁与测试都是绿的；本报告的全部发现都是门禁/语料未覆盖的缺口，而非回归。**

### 0.2 发现统计

| 类别 | P0 | P1 | P2 | 合计 |
| ---- | -- | -- | -- | ---- |
| A. 标准自身待澄清/自相矛盾 | 0 | 0 | 4 | 4 |
| B. 实现（`src/`）不符标准 | 57 | 9 | 5 | 71 |
| B. 实现 — §2.12 补充发现 | 4 | 6 | 0 | 10 |
| C. 设计文档不符标准 | 2 | 16 | 30+ | 48+ |

> 实现侧 P0 中最大的两族是**静态语义漏检**（§2.6，17 条）与**语法接受集超集**（§2.9 的 B-28～B-37，10 条）：前者让非法程序通过 `--check` 再在运行期产生静默错值/内部错误，后者让不符合附录 A 的产生式被接受。
>
> 另有 **6 条过度拒绝**（B-26/B-48 浮点字面量、B-33 字段赋值的 `funcall`、B-37 大写变量名嵌套字段、B-41 缩进码归属、B-2 家族中的误拒），会把**合法程序误判为错误**，优先级与漏检同级。
>
> **两条最高风险项**：B-28（类型嵌套 5 万层 → SIGSEGV）与 B-42（`bitcast` 伪造指针 → 槽索引越界读/写 = 任意进程内存读写原语 + SIGSEGV）。两者都不是「诊断码不对」，而是**进程级故障**。

### 0.3 最关键的 10 条

1. **B-42（P0，进程级）** `bitcast(ptr<int32>, 0x7FFFFFFD)` 后 `ptr_load` **SIGSEGV**；ASan 报 `heap-buffer-overflow`，`ptr_store` 为越界**写**——当前实现是一条任意进程内存读写原语。
2. **B-28（P0，进程级）** `ptr<` 嵌套 5 万层直接 **SIGSEGV**（类型解析无深度上限；RHS 解析有 256 上限）。
3. **B-1（P0）** `funcall` 位置/结果两码**双向互换**：`void` 函数作值报 `FUNCALL_POSITION`（应 `FUNCALL_RESULT_TYPE`），非 `void` 返回类型不符报 `FUNCALL_RESULT_TYPE`（应 `TYPE_MISMATCH`）。
4. **B-2（P0）** **首个诊断的阶段内定序违反 §11 第 2 条**：实现按 13 个内部子阶段排序，导致**源序更晚**的 SEM 诊断压过源序更早的 SEM 诊断。
5. **§2.6（P0 族，17 条）** 静态语义大面积漏检：确定初始化 DFA 读集不完整、操作数类型只比 `tag`（丢失所指类型与 `N`）、名称冲突/作用域漏检、`while true` 后不可达、单行 `else if` **被接受且语义错误**。
6. **B-7～B-11、B-42～B-46（P0）** **VM 与 AOT 在 6 类程序上可观察行为分歧**（§1.3 直接要求一致），并伴随 4 处 VM `implementation error`、1 处 `%f` 输出错误、1 处 `shl` 溢出漏报。
7. **B-56（P0）** **`#lib` 模块内调用同库函数（`funcall(Self.f)`）时，该模块一旦被 `import` 就编译失败**（`UndefinedFunction`，且定位到入口文件）；单独编译同一文件却正常。全仓语料**零覆盖**该形态。
8. **B-13（P0）** **libtc 内存入口不解析 `import`**：`import NoSuchModule` / 自导入 / 非 `#lib` 目标全部 `rc=0` 接受，与文件入口接受集不同。
9. **B-50（P0）** **AOT `-r` 拒绝全部 `#lib`-only 合法程序**——生成的 C 把每个 TC 函数发射为 `static`，再被 `-Werror` 判为 `unused function`。抽测 `tests/valid/` 下 5 个 `#lib`-only 文件全部如此；AOT 套件因**没有一个** `#lib`-only 差分用例而全绿。
10. **C-1（P0）** 编译器标准 §8.5 / §11.4.2 **重定义了** `FUNCALL_POSITION` / `FUNCALL_RESULT_TYPE` 的触发条件，与附录 B 规范性定义冲突；§1.3 又以「13 阶段优先」覆盖了标准 §11 的「同阶段按源序」。

> 另有 §2.12 的补充发现 **B-57～B-66**（10 条）：AOT 侧伪造指针非确定读（B-57，进程级）、跨模块同名符号致合法程序被拒（B-65）、`memblock_copy` 空拷贝 dst 侧与常量负 dst 下标漏检（B-58/B-59）、格式越界诊断降级（B-60）、非 `bool` 条件错码不一致（B-61）、`const` 列表逗号（B-62）、顶层缩进不校验（B-63）、含点文件名（B-64）、`static let` 经 `Self.` 引用规则（B-66）。

### 0.4 与既有未关闭项的关系

审计前已登记的 9 项未关闭项（本报告 §5 编号 `既有-1`～`既有-9`）**全部经本审计独立复现确认成立**，无一误报，详见 §5。
本报告在此基础上**新增**：实现侧 **71** 项（P0 57 / P1 9 / P2 5，编号 B-1～B-56 ＋ §2.6 的 15 条子项）＋ **§2.12 补充发现 10 条（B-57～B-66）**、文档侧 **48+** 项、标准侧 **4** 项待澄清。

---

### 0.5 方法与局限

* **权威单向**：所有判定以语言标准为准；设计文档与实现都被视为被检查对象。标准自身的问题单列于 §1，不据此判实现「错」。
* **证据分级**：P0/P1 条目绝大多数由本审计**直接复现**（附程序 + 命令 + 观察输出）；少数来自并行分审计且本审计只做了源码核对者，已就地标注（如 B-25）。
* **未覆盖**：
  * `-DTC_FORCE_NO_FENV=ON`（无 fenv 的浮点回退路径）未跑。
  * 32 位目标未测（实现固定 64-bit-only，标准允许 32/64）。
  * `ptr_add`/`ptr_sub` 按**槽索引**而非 `sizeof_bits(T)` 步进的内部模型，与 AOT 侧是否一致未逐项核对。
  * 极端格式组合（`%g#` + 精度 0 等）仅抽样。
  * 文档 P2 清单为**抽样列举**，不保证穷尽。
* **不计入门禁**：本报告只做审计记录，**没有**改动任何 `src/`、测试或既有文档。

---

## 1. 标准（权威文件）自身待澄清项

标准是唯一权威，但下列 4 处存在**内部不一致或规范空白**，会让符合一致性的实现作出不同选择，建议由标准 owner 裁决后回填。

### A-1（P2）`ptr_address` / `ptr_add` / `ptr_sub` 被同时标为「RHS」与「`operand`」

| 位置 | 原文 |
| ---- | ---- |
| 标准 §6.8.4（L1712 附近） | 「语法：`ptr_address(T, identifier)`……类别：RHS（**`operand`**）」 |
| 标准 §6.8.5 / §6.8.6 | 「类别：RHS（**`operand`**）」（`ptr_add` / `ptr_sub`） |
| 标准 §6.1.2（L1183） | 「**调用型 RHS 不属于 `operand`**：`ptr_size`、`memblock_load`、`cast`、`bitcast` 等需要求值或带显式类型参数的调用形式只能作为整条 RHS」 |
| 标准附录 A（L3019） | `operand = identifier \| qualified_identifier \| integer_literal \| float_literal \| float_special \| bool_literal \| nullptr_literal \| imported_member_name \| field_access \| memblock_count_access ;` —— **不含** `ptr_address_expr` / `ptr_add_expr` / `ptr_sub_expr` |

**影响**：若按 §6.8.4 字面理解，`ptr_load(int32, ptr_address(int32, a))` 是合法程序；按附录 A + §6.1.2 则必须语法拒绝。
**实现选择**：拒绝（`SyntaxError: expected operand`）——与附录 A 一致，附录 A 是语法接受集的权威。
**建议**：删除 §6.8.4/§6.8.5/§6.8.6 中的「（`operand`）」括注，或统一改为「类别：RHS（**非 `operand`**）」。

### A-2（P2）`TC_CE_LITERAL_OUT_OF_RANGE` 的阶段列不完整

| 位置 | 原文 |
| ---- | ---- |
| 附录 B.1 | `TC_CE_LITERAL_OUT_OF_RANGE` \| **LT** \| 字面量数值超过 `2^64−1`（整数）；非零有限浮点舍入为零或有限舍入为无穷 |
| 标准 §3.6.1 / §2.3.5 / §2.4.1 | 「该判定只依赖 Token 自身，属词法值检查」 |
| 标准 §5.2.1 第 2 步 | 「RHS 为直接字面量时……**值不可表示时报 `TC_CE_LITERAL_OUT_OF_RANGE`**」——发生在 SEM 类型闭合步骤 |

标准正文在两处使用了同一码：Token 自身的词法上限（LT）与**依赖上下文期望类型**的范围检查（SEM）。附录 B 只登记了 LT。这会改变首个诊断的选择（LT 阶段优先 vs SEM 内按源序），是跨实现可观察差异。
**旁证**：编译器标准 §11.4.1 明确写「静态（第 2/6/7/8 阶段）」，即实现侧已按双阶段处理。
**建议**：附录 B 阶段列改为 `LT / SEM`，并比照 `TC_CE_CONSTANT_EXPRESSION` 的写法给出阶段细分说明。

### A-3（P2）`memcopy_unsafe` 负下标究竟属静态还是运行时，标准内部口径分裂

| 位置 | 原文 |
| ---- | ---- |
| 标准 §6.8.9 区间合法性 | `length ≥ 0 ∧ dst_idx ≥ 0 ∧ src_idx ≥ 0` |
| 标准 §6.8.9 判据段 | 「**`length < 0`** 时，若编译期可确定则报 `TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE`，运行时则报 `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`」——**只提 `length`** |
| 标准 §6.8.9 执行语义第 2 步 | 「若 `length` 的数学值 < 0 **或 `dst_idx` / `src_idx` < 0**，发生**运行时**错误（`TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`）」 |
| 附录 B.11 | `TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE` \| SEM \| 「`memcopy_unsafe` 的 **`length`** 在编译期为负」 |
| 标准 §11.1 表 / 附录 B.13 | `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE` \| RT \| 「`memcopy_unsafe` 的 **`length`** 运行时为负」 |

**分裂点**：§6.8.9 的「区间合法性」等式把下标纳入，但静态/运行时分工句只写 `length`；附录 B 与 §11.1 亦只写 `length`。**编译器标准 §3.2/§6.7/§11.4.6 与 CLI 参考 §6 采取「编译期可确定负下标也报静态码」的读法**（见 C-12、C-24）。
**实测**：实现对负下标**只**在运行时报 `TC_RE_*`（`-c` 通过），即在这一点上与附录 B 一致而与编译器标准不一致。
**建议**：明确「编译期可确定的负下标 → 运行时码」并同步附录 B.13/§11.1 与编译器标准、CLI 参考；或反过来把静态码扩展到下标，三处统一。

### A-4（P2）`bitcast(T, nullptr)` 的源类型未定义

标准 §6.6.6 要求 `bitcast` 源与目标等宽且源为整数/浮点/指针；`nullptr` 的定型上下文在 §3.10.2 / §6.1.1 只列出「声明、`funcall` 实参、`return`、`cast`」，**未含 `bitcast`**；而 §1.3 规定「规范未给出的形态不合法」。
**实测**：`var p: ptr<int32> = bitcast(ptr<int32>, nullptr)` **被接受**（`exit 0`），且 VM 详设 §1.5 把它登记为「正例 `bitcast_ptr_nullptr.tc`」。
**建议**：在 §6.6.1.1 的「字面量操作数的源类型确定」表中补一行 `bitcast` × `nullptr`（源类型由目标位宽决定），与 `inf`/`nan` 的处理并列。

---

## 2. 实现（`src/`）与标准的偏差

> 全部条目**均不在** §5 所列 9 项既有未关闭项之内，为本审计新增。
> 每条附最小复现。VM 命令：`build/vm/bin/tc-vm -e [-c] F.tc`；AOT：`build/aot/bin/tc-aot -r F.tc`。

### 2.1 P0 — 诊断码错误/错阶段

#### B-1 `funcall` 位置码与结果码**双向互换**

| 场景 | 标准要求 | 实现实测 |
| ---- | -------- | -------- |
| `void` 函数用于 `var x: T = funcall(...)` | `TC_CE_FUNCALL_RESULT_TYPE`（§8.2.3 表 L2067；附录 B.12 L3439） | **`FunctionCallPositionError`** = `TC_CE_FUNCALL_POSITION` |
| `void` 函数用于 `x = funcall(...)` | 同上（L2068） | **`FunctionCallPositionError`** |
| 非 `void` 函数以独立语句调用 | `TC_CE_FUNCALL_POSITION`（L2069） | `FunctionCallPositionError` ✅ |
| 非 `void` 返回类型与接收变量不符 | `TC_CE_TYPE_MISMATCH`（附录 B.4「赋值、传参、`return`…类型不一致」；B.12 把 `FUNCALL_RESULT_TYPE` 的触发条件**独占**给 `void` 作值） | **`FunctionCallResultTypeError`** = `TC_CE_FUNCALL_RESULT_TYPE` |

复现：
```text
#lib
public func f() void then
    return
end
public func g() int32 then
    var x: int32 = funcall(Self.f)   ; → FunctionCallPositionError（应 FUNCALL_RESULT_TYPE）
    return x
end
```
```text
#lib
public func u8f() uint8 then
    return 1u
end
public func g() void then
    var x: int32 = funcall(Self.u8f) ; → FunctionCallResultTypeError（应 TypeMismatch）
    return
end
```
**根因**：`src/vm/analyzer/tc_func_check.c:1147-1155`（`position==1 && is_void` 误用 `FUNCALL_POSITION`）与 `:1163-1167`（`expected != return_type && !is_void` 误用 `FUNCALL_RESULT_TYPE`）。
**注**：编译器标准 §8.5 表与 §11.4.2 表的措辞与实现一致，即**文档与实现同错**（见 C-1）。

#### B-2 首个诊断的阶段内定序违反标准 §11 第 2 条

标准 §11（L2384-2391）：
1. 阶段优先 LT → SYN → SEM → CT；2. **同阶段内按源序位置**（行升序、同行列升序）；3. 同位置专用码优先；4. 条款内既有顺序优先。

附录 B 把 `TC_CE_RECURSION`（B.12）与 `TC_CE_UNREACHABLE_STATEMENT`（B.10）都标为 **SEM**。因此它们与 `TC_CE_UNDEFINED_VARIABLE`（B.3，SEM）之间**必须**按源序选首个。

实现按 13 个内部子阶段排序（名称/类型 = 阶段 6 → CFG/可达性 = 阶段 11 → 调用图 = 阶段 12），于是**源序更晚**的阶段 6 诊断压过**源序更早**的阶段 11/12 诊断。

复现 1（调用图环 vs 未定义名）：
```text
#lib
public func f() void then
    funcall(Self.f)          ; 第 4 行 → TC_CE_RECURSION（SEM）
    return
end
public func g() void then
    writeln(int32, nosuch)   ; 第 8 行 → TC_CE_UNDEFINED_VARIABLE（SEM）
    return
end
```
实测首个诊断 = `:7/8: error [UndefinedVariable]`；标准要求第 4 行的 `TC_CE_RECURSION`。

复现 2（不可达语句 vs 未定义名）：`return` 后跟语句在第 4 行、未定义名在第 9 行 → 实测报第 8/9 行 `UndefinedVariable`（标准要求第 4 行 `TC_CE_UNREACHABLE_STATEMENT`）。

**根因**：`src/vm/analyzer/tc_analyzer.c` 先对全部编译单元跑完 pass2（名称/类型），再跑 CFG/确定初始化，最后跑调用图，且全程 fail-fast。
**注**：这不是实现疏漏，而是**编译器标准 §1.3 第 1 条**明文要求的行为（「先报告编号较小阶段的错误」），该条与标准 §11 冲突——见 C-2。

#### B-3 重复格式标志报 `TC_CE_SYNTAX`（应 `TC_CE_FORMAT_SPECIFIER`，SEM）

附录 A（L2544-2548）：
```ebnf
format_specifier = "%" , { format_flag } , [ format_width ] , [ "." , { digit } ] , format_conversion ;
format_flag = "-" | "+" | "#" | "0" ;
```
`{ … }` 允许重复，故 `%--d`、`%++d`、`%##x` **匹配 EBNF**。§10.5（L2367）明确：「`flags` 中每个标志至多出现一次」；「**已匹配附录 A `format_specifier` 形态后**，重复标志、宽度或精度超出上述范围，以及转换符不适用的标志/精度，均报告 `TC_CE_FORMAT_SPECIFIER`」。EBNF 注释亦写「重复标志……由 §10.5 静态检查」。

实测：`writeln(int32, %--d, a)` → `:3:16: error [SyntaxError]: invalid format specifier`（LT/SYN）。
**根因**：`src/vm/lexer/tc_lexer.c:1009-1013` 把 `tc_format_spec_parse` 的失败一律映射为 `TC_CE_SYNTAX`；而 `tc_format_spec_parse`（`src/vm/runtime/tc_types.c:660-680`）对重复标志返回 0（=形态失败），无法区分「不匹配 EBNF」与「匹配但标志重复」。

#### B-4 `let` RHS 为 `var`/形参且类型不同时错报 `TC_CE_TYPE_MISMATCH`

标准 §5.2.1 检查顺序第 3 步（L1075）：
> 「RHS 为标识符时，名称解析后以该绑定的声明类型为结果类型。**若绑定解析为 `var` 或函数参数，立即报 `TC_CE_CONSTANT_EXPRESSION`，不比较结果类型**，也不进入编译期求值。」

实测：
```
#program
var v: int32 = 1
let a: int8 = v      ; → :3: error [TypeMismatch]（应 ConstantExpressionError）
```
同类型时（`let a: int32 = v`）正确给出 `ConstantExpressionError`——说明是**判定次序**问题。
**根因**：`src/vm/analyzer/tc_type_check.c:124-130` 先做类型标签比较，`sym_kind == TC_SYM_VARIABLE` 的常量性检查在 `:129-133` 之后。

#### B-5 `memblock` 的 `N` / `count:` 来源校验过宽 + 错码

标准 §3.8.1（L491）：
> 「合法的 `N` 来源：整数字面量……、**类型为 `usize`** 的 `let` / `static let` 标识符……**来源不合法**（`var` / `static var`、形参或非常量表达式）**或数学值 < 1** 者，静态语义拒绝并报告 **`TC_CE_CONSTANT_EXPRESSION`**」

`count:` 同规则（§3.8.3 L540）。

| 输入 | 标准要求 | 实测 |
| ---- | -------- | ---- |
| `let a: isize = 5` + `memblock<int32, a>` | `TC_CE_CONSTANT_EXPRESSION` | **接受**（`-c` 无输出，rc=0） |
| `let a: isize = 2` + `memblock(int32, count: a, …)` | `TC_CE_CONSTANT_EXPRESSION` | **接受** |
| `let a: int32 = 5` + `memblock<int32, a>` | `TC_CE_CONSTANT_EXPRESSION` | `TypeMismatch`（"memblock count must be a usize constant"） |
| `let a: usize = 0` + `memblock<int32, a>` | `TC_CE_CONSTANT_EXPRESSION` | `MemblockElementCountMismatch` |
| `let a: usize = 0` + `memblock(int32, count: a, fill: 1)` | `TC_CE_CONSTANT_EXPRESSION` | `MemblockElementCountMismatch` |
| `memblock<int32, 0>`（字面量，对照） | `TC_CE_CONSTANT_EXPRESSION` | `ConstantExpressionError` ✅ |

**即：字面量路径正确，标识符路径系统性错误**——同一规则两条路径不一致。
**根因**：`src/vm/analyzer/tc_memblock_check.c:117` 接受 `TC_ISIZE`（应仅 `TC_USIZE`）；`:115` 用 `TC_CE_TYPE_MISMATCH`；`:122-131` 用 `TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH`（应 `TC_CE_CONSTANT_EXPRESSION`）。

#### B-6 `funcall` 实参「个数超限」检查抢在「重复/未知」之前

附录 B.12：`TC_CE_EXTRA_ARGUMENT` = 「`funcall` 实参个数多于形参（**名称全部已知**）」；§8.2.2 表同（L2050）。
编译器标准 §8.2 第 3 条：「实参**重复、未知、缺失、顺序、类型**，按此顺序检查」。

实测：
```
#lib
public func f(a: int32) void then
    return
end
public func g() void then
    funcall(Self.f, a: 1, a: 2, z: 3)
    return
end
; → :6: error [ExtraArgument]（应 DuplicateArgument；按 §8.2 次序，未知名 z 也应先于个数）
```
**根因**：`src/vm/analyzer/tc_func_check.c:188-195` 在**全部名称检查之前**短路返回 `TC_CE_EXTRA_ARGUMENT`，并带注释「置于名称检查之前：个数超限是最外层的诊断」——这是有意为之，但与附录 B 与编译器标准均冲突。同函数 `:250-254` 另有一处**正确**的（「名称全部已知」）检查，被前面的早退遮蔽。

> 既有未关闭项 **既有-3** 已记录「同一形参名重复实参报错码不符」，本条是**同一根因的更大范围**（重复 + 未知 + 顺序都被个数检查遮蔽）。

### 2.2 P0 — VM / AOT 可观察行为分歧

标准 §1.3「可观察行为」：是否通过静态检查、**首个规范诊断及其错误码**、按源序成功提交到标准输出的字节、正常结束或**首个运行时错误及错误码**。VM 与 AOT 是同一实现的两条后端，下列分歧均属可观察。

#### B-7 VM 按「槽位内类型标签」而非语句显式类型渲染 `write`/`writeln`

```
#program
var f: float64 = 0.0
var i: int64 = 7
var pf: ptr<float64> = ptr_address(float64, f)
var pi: ptr<int64> = cast(ptr<int64>, pf)
ptr_store(int64, pi, i)
writeln(float64, f)
```
| 后端 | 输出 |
| ---- | ---- |
| VM | `7` ❌ |
| AOT | `3.45846e-323` ✅（= int64 位模式 `7` 作为 float64 的精确值） |

`cast(ptr<int64>, pf)` 是 §3.7/§3.10.5 允许的指针**重标记**（指针恒等宽）；`ptr_store(int64, …)` 把 8 字节写入 `f` 的存储（§6.8.3）；`writeln(float64, f)` 必须按**显式标量类型** `float64` 解释该存储（§10.1「显式类型参数为 `scalar_type`；操作数类型与其一致」）。
**根因**：槽位是 `TcValue{type,bits}`；`tc_exec_load_binding`（`src/vm/executor/tc_executor.c:79` 附近）把写入时携带的类型标签一并取出，`tc_io_write_value` 据此渲染。

#### B-8 `memcopy_unsafe` 的结构体字段指针操作数：VM 内部错误 vs AOT 空指针

```
#program
struct S then
    var p: ptr<int32>
end
var a: int32 = 7
var pa: ptr<int32> = ptr_address(int32, a)
var s: S = S(p: pa)
memcopy_unsafe(int32, s.p, 0, s.p, 0, 1)
writeln(int32, a)
```
| 后端 | 结果 |
| ---- | ---- |
| VM（`-c` 通过） | `:8: implementation error: SyntaxError: internal error: unresolved field operand` ❌ |
| AOT（`-r`） | `:8: error: null pointer dereference` ❌ |
| 对照（把 `s.p` 换成裸 `pa`） | VM/AOT 均输出 `7` ✅ |

标准 §6.8.10 明列「结构体字段读取（如 `a.p`，字段类型为 `ptr<T>`）」为合法操作数，故本程序**应正常输出 `7`**。
**注**：既有项 **既有-5** 只登记了 VM 侧内部错误；本条补充 **AOT 侧给出错误的运行时码**与**两后端分歧**。

#### B-9 `memblock_copy` 的顶层 `let` 下标操作数：VM 内部错误 vs AOT 正确

```
#program
let I: int32 = 0
var d: memblock<int32, 4> = memblock(int32, count: 4, 1, 2, 3, 4)
var s: memblock<int32, 4> = memblock(int32, count: 4, 5, 6, 7, 8)
memblock_copy(int32, d, I, s, I, 1)
writeln(int32, 1)
```
| 后端 | 结果 |
| ---- | ---- |
| VM | `:5: implementation error: SyntaxError: internal error: unresolved binding metadata` ❌ |
| AOT | `1`，rc=0 ✅ |

§6.7.2.3 规定 `dst_index`/`src_index`/`length` 为 §6.1.2 `operand`（`let` 标识符合法）。
**注**：既有项 **既有-7** 已登记 VM 侧；本条补充 AOT 侧正确，即**仅 VM 需修**。

#### B-10 经 `bitcast(ptr<T>, <usize>)` 得到的指针解引用：两后端分歧

```
#program
var u: usize = 2u
var p: ptr<int32> = bitcast(ptr<int32>, u)
var v: int32 = ptr_load(int32, p)
writeln(int32, v)
```
| 后端 | 结果 |
| ---- | ---- |
| VM | `:4: implementation error: SyntaxError: internal error: invalid pointer value` ❌ |
| AOT | `:4: error: null pointer dereference`（`TC_RE_NULL_POINTER_DEREFERENCE`）❌ |

`bitcast(ptr<T> ↔ usize)` 在 §3.10.9/§6.6.6 明确合法，且 §1.3 把「经等宽 `bitcast` 观测到的抽象槽编码」列为**实现定义**（穷尽清单第 4 项）——但「解引用一个由任意 `usize` 伪造的编码」在两份文档中都没有规定，属 **A 类规范空白**；而**两后端给出不同错误码 / 内部错误**这一点无论如何都违反 §1.3。

#### B-11 经指针别名清零结构体存储后再读字段：VM 内部错误 vs AOT 正常

```
#program
struct S then
    var a: int32
    var b: int32
end
var s: S = S(a: 1, b: 2)
var ps: ptr<S> = ptr_address(S, s)
var pz: ptr<usize> = cast(ptr<usize>, ps)
var q: ptr<usize> = cast(ptr<usize>, ps)
var w: usize = 0u
ptr_store(usize, pz, 0u)
w = ptr_load(usize, q)
writeln(usize, w)
writeln(int32, s.a)
writeln(int32, s.b)
```
| 后端 | 输出 |
| ---- | ---- |
| VM | `0` 然后 `:14: implementation error: SyntaxError: internal error: invalid struct field read` ❌ |
| AOT | `0` / `0` / `0` ✅ |

指针别名读写同一存储是 §3.10 允许的；结构体字段读取必须给出当前存储值。

### 2.3 P0 — 接受集

#### B-12 `#program` 中 `struct` 名与 `import` 名冲突被放过

标准 §3.9.1（L594）：「结构体名……与导入名冲突 → `TC_CE_IMPORT_NAME_CONFLICT`」（**无模式限定**）；附录 B.2：「导入名与本模块成员名（**含结构体等类型名**，§3.9.1）冲突」。

实测：
```text
; Foo.tc:  #lib / public func f() void then / end
#program
import Foo
struct Foo then
    var x: int32
end
var v: int32 = 1
```
→ **接受**（`-c` 无输出，rc=0）。应为 `TC_CE_IMPORT_NAME_CONFLICT`。
**根因**：编译器标准 §4.6 明确把 `#program` 的受检查名称集合限缩为「顶层 `var` 名、顶层 `let` 名」（见 C-3），实现照此实现。
**对照（已实测）**：`#lib` 侧三种形态**全部正确拒绝**——`import Bar` + `public struct Bar` / `public static var Bar` / `public func Bar()` 均报 `ImportNameConflict`。**缺口仅限 `#program`**，涉及面很小。

#### B-13 libtc 内存入口（`tc_compile_source`）**不解析 `import`**

标准 §4.5（L992-999）：「目标存在｜必须唯一定位 `xxx.tc`｜`TC_CE_IMPORT_NOT_FOUND`」「循环导入｜模块依赖图必须为 DAG……任一长度的循环（含自导入）均报 `TC_CE_CIRCULAR_IMPORT`」；§1.3 把「是否通过静态检查」列为可观察行为。

实测（链接 `build/src/libtc/libtc.a`，探针见附录 R-1）：

| 源 | `tc_compile_source` | 同源经 `tc_compile_file_opts` |
| -- | ------------------- | ----------------------------- |
| `#program / import NoSuchModule / var x: int32 = 1` | **rc=0 接受** ❌ | `TC_CE_IMPORT_NOT_FOUND` ✅ |
| `#program / import NoSuchModule / var x: int32 = NoSuchModule.K` | `UndefinedVariable`（错码）❌ | `TC_CE_IMPORT_NOT_FOUND` ✅ |
| `#program / import NoLibHere / var x: int32 = 1` | **rc=0 接受** ❌ | `TC_CE_IMPORT_NOT_FOUND` ✅ |
| `#lib / import SelfLib / public func f() void then end` | **rc=0 接受** ❌ | `TC_CE_CIRCULAR_IMPORT` ✅ |

**影响**：libtc / TC-Embed 的**内存入口接受集与文件入口不同**，直接违反 §1.3 与 §4.5；嵌入宿主可编译出带未解析导入的 `TcTypedProgram`。
**根因**：`src/vm/analyzer/tc_analyzer.c:145` 仅在 `entry_path` 非空时执行模块解析。
**注**：libtc 设计说明书 §15.3 把这个行为**写成了文档**（「无路径的内存源仅做结构检查、不解析 import」），同一节又声称跑完「4a→4b→4c→4d」全管线（见 C-18）。

#### B-56 `#lib` 模块内一个函数调用同库另一个函数，**该模块一旦被导入即编译失败**

**这是本次审计发现的接受集缺口中最严重的一条**：它让「库内函数互相调用」这一最普通的写法在**被导入时**无法编译，而单独编译同一文件却完全正常。

标准 §4.3（L949-961）/§8.4.1（L2115）：`#lib` 函数体内访问同库成员须写 `Self.<名>`；§8.2.1 表把 `Self.<函数名>` 列为 `#lib` 函数体调用本库函数的**唯一合法写法**。

实测（两个文件，`M2.tc` 为 `#lib`，`g` 调用 `f`，**无递归**）：
```text
; M2.tc
#lib
public func f(a: int32) int32 then
    return a
end
public func g(a: int32) int32 then
    var v: int32 = funcall(Self.f, a: a)
    return v
end
```
```text
; entry.tc
#program
import M2
writeln(int32, 1)
```

| 编译方式 | 结果 |
| -------- | ---- |
| `tc-vm -c M2.tc`（模块**作入口**） | rc=0 ✅ |
| `tc-vm -c entry.tc`（模块**作依赖**） | **`entry.tc:6: error [UndefinedFunction]: undefined function 'f'`** ❌ |
| `tc-aot -c entry.tc` | 同样失败 ❌ |
| 入口真正调用 `funcall(M2.g, a: 5)` | 同样失败 ❌ |

诊断三要素全错：**文件**是入口（错误实际在 `M2.tc`）、**行号**取自模块（`entry.tc` 只有 3 行）、**错误码**是 `UndefinedFunction`（`Self.f` 明明已定义）。

**为什么语料没抓到**：全仓 `tests/valid/*.tc` 中只有 `phase4_self_funcall.tc` 含 `funcall(Self.`，而它**从未被任何用例 `import`**（`被 import 次数=0`）——该形态完全未被覆盖。
**根因（同族）**：分析依赖模块的函数体时，「本库顶层成员名索引」作用域上下文未设置（`src/vm/analyzer/tc_analyzer_pass2.c:202-207` 的全局 `g_name_scope_members` / `g_name_scope_in_function` 只对入口设置）。同一根因在入口/依赖两种编译方式下还会给出**不同的错误码**（入口为 `FUNCTION_SCOPE_ACCESS`，依赖为 `UNDEFINED_FUNCTION`）。
**与 B-14 的关系**：B-14 是「依赖模块的诊断定位到入口文件」；本条是它的一个**会导致合法程序被拒**的实例，严重度更高。

### 2.4 P1

#### B-14 被导入模块内的诊断定位到**入口文件**

```text
; Bad3.tc:  #lib / public static let K: int32 = undefined_thing
; b3.tc:    #program / import Bad3 / writeln(int32, 1)
```
实测：`b3.tc:2: error [UndefinedVariable]: undefined variable 'undefined_thing'`，并回显 **b3.tc 第 2 行**（`import Bad3`）作为源码片段。
另一例（错误在模块第 6 行、入口文件只有 3 行）：`b4.tc:6:12: error [SyntaxError]: unexpected character` —— **行号取自模块、文件路径取自入口、源码片段取自入口**。
**影响**：诊断位置三要素自相矛盾，用户无法定位；§11 的「按源序位置」在多文件程序下失去意义。
**根因**：诊断的 `filename`/`source` 由 `tc_diagnostic_set_source` 在入口处一次性设置，模块解析过程中的诊断未切换 source。

#### B-15 `tc-aot -r` 对相对路径输入失败

```text
$ cd /tmp && tc-aot -r w38b.tc
sh: w38b.c.out: command not found
tc-aot: run failed (exit 32512)
$ tc-aot -r /tmp/w38b.tc          # 绝对路径正常
```
生成的可执行文件以**裸相对名**调用，POSIX `sh` 不搜索 `.`。测试脚本因使用 `mktemp` 绝对路径而未暴露此问题。

#### B-16 模块文件 I/O 失败映射为语言码 `TC_CE_SYNTAX`

标准 §1.3 一致性判定与编译器标准 §11.4 把「文件打开/读取失败」排除在语言诊断之外（libtc 设计说明书 §15.4 亦明文承诺「seek/read/close 失败使用 `TC_DIAG_API / TC_API_ERR_FILE_READ`，不会伪装成语言 `SyntaxError`」）。
**实测/源码**：`src/vm/analyzer/tc_module.c:306-308` 对 `fseek`/`ftell` 失败报 `TC_CE_SYNTAX`（行号 0、无位置）；导入目标为目录时表现为 `error: expected #program or #lib`（定位到**入口**文件、无行号）。

#### B-17 `static var` 缺初始化器报笼统 `TC_CE_SYNTAX`（既有项 既有-6 复核）

```text
#lib
public static var V: int32
```
实测 `:2:27: error [SyntaxError]: unexpected token`。
标准 §1.3 的结构类语法阶段诊断清单与 §5.1.2 要求该形态报 **`TC_CE_VAR_MISSING_INIT`**；附录 A 的 `static_variable_def = (public|private) static variable_def`，而 `variable_def` 要求 `= rhs`。`#program` 路径实测正确（`VarMissingInitializer`），故为 `#lib` 路径缺口。

### 2.5 P2

#### B-18 内部错误以 `implementation error: SyntaxError: internal error: …` 形式泄漏给用户

`src/` 中共 **74** 处 `"internal error: …"` 字面量（`tc_executor.c` 最多）。B-8/B-9/B-10/B-11/B-20/B-21 已证明其中至少 6 处可由**合法或静态已接受**的程序触发。
`src/vm/runtime/tc_diagnostic.c:437` 打印为 `<file>:<line>: implementation error: <ErrorKind>: <message>`，其中 `ErrorKind` 被填成 `SyntaxError`——把实现缺陷伪装成语言诊断码 `TC_CE_SYNTAX`。标准 §1.3 只承认「实现资源失败」（`TC_ERR_OUT_OF_MEMORY`）一类实现侧失败。

#### B-19 `tc_embed` 临时槽区与已声明槽区**重叠**

`src/vm/embed/tc_embed.c:187`：`ctx->tmp_top = (int)slot_count;`
`src/vm/embed/tc_embed.c:419-421`：`ctx->tmp_marks[d] = ctx->tmp_top; ctx->tmp_top -= n; *base_slot_out = ctx->tmp_top;`
即首次分配 `n` 个临时槽返回 `slot_count - n`，**覆盖已声明槽位尾部**。任一使用临时区（如 `tc_embed_make_ptr`）的宿主调用都会别名破坏程序变量。
同时 `:416-419` 的 `if (ctx->tmp_top < n)` 与槽位数无逻辑关系，`n > slot_count` 时误报「temporary slot region exhausted」。
（该缺陷与 `docs/TC-Embed详细设计说明书-0.0.44.md` §12.2/§15.8.3 示例的「数据写入指针形参自身槽位」问题同源，见 C-20。）

### 2.6 静态语义漏检族（P0，共 17 条）

> 这一族的共同后果是：**非法程序通过 `--check`**，然后在运行期产生静默错值、内部错误或本应在编译期报出的码。它们不是「少报一个错」，而是**接受集扩大**，是本次审计中风险最高的一族。

#### B-20 `memcopy_unsafe` 的操作数类型不校验（漏检 → 内部错误）

标准 §6.8.9：`dst` / `src` 须为 `ptr<T>` 类型；§6.8.10 同。
实测（`-c` 全部**接受**）：
```text
#program
var x: int32 = 1
var y: int32 = 2
memcopy_unsafe(int32, x, 0, y, 0, 1)     ; 非指针操作数 → ACCEPT，运行时 internal error
```
```text
var p: ptr<int32> = ptr_address(int32, a)
var q: ptr<int32> = ptr_address(int32, b)
memcopy_unsafe(float32, p, 0, q, 0, 1)   ; T 与所指类型不符 → ACCEPT，正常运行（无诊断）
```
两例均应为 `TC_CE_TYPE_MISMATCH`。源码：`src/vm/analyzer/tc_memblock_check.c:517-535` 只检查了 `element_type.tag == TC_VOID` 与 `dst` 只读性，未校验操作数是否为 `ptr<T>`。

#### B-21 `memblock_copy` 的常量下标/length/元素类型不校验（漏检）

标准 §6.7.2.4：编译期可确定的负下标、负 `length`、越界 → 静态 `TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE`；§6.7.2.3：`T` 须与 `dst`/`src` 元素类型**同时严格一致**。
实测（`-c` 全部**接受**，仅运行时报错或无诊断）：

| 程序 | 应为 | 实测 |
| ---- | ---- | ---- |
| `memblock_copy(int32, b, 0, a, 0, 3)`，两侧 `N=2` | `TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE`（静态） | `-c` 通过；运行时 `MemblockIndexOutOfRange` |
| `length = -1` | 同上 | `-c` 通过；运行时 `MemcopyUnsafeInvalidRange` |
| `memblock_copy(float32, b, 0, a, 0, 2)`，`a` 为 `int32` | `TC_CE_TYPE_MISMATCH` | `-c` 通过 |

源码：`src/vm/analyzer/tc_memblock_check.c:474-515`，其中 `:511-513` 显式 `(void)hist; (void)warnings; (void)stmt_index; return 0;`。

#### 2.6.1 确定初始化（DFA）读集不完整

标准 §9.2（L2177）：「在绑定确定初始化前读取其值……→ `TC_CE_UNINITIALIZED_VARIABLE`」；§6.2（L1199）：赋值目标亦须已确定初始化；§7.3.3（L1965）同。

**(a) 赋值/读取目标未纳入 DFA** — `src/vm/analyzer/tc_cfg.c:558,565` 只写 `nodes[node].write_slot`。
```text
#lib
public func f(c: bool) void then
    if c then
        goto skip
    end
    var x: int32 = 1
    label skip:
    x = 2            ; 绕过了 x 的初始化 → ACCEPT（应 UNINITIALIZED_VARIABLE）
    return
end
```

**(b) 复合 RHS 与部分语句的读集完全不展开** — `src/vm/analyzer/tc_cfg.c:336`（注释明写「复合/调用 RHS：读集不在此展开……return 0」）、`:593`（`TC_STMT_RETURN` 不记读）。
同一 `goto skip` 形状下，下列写法**全部 ACCEPT**（均应 `UNINITIALIZED_VARIABLE`）：`funcall(Self.g, v: x)`、`return x`、`var v: int32 = memblock_load(int32, m, 0)`、`var n: usize = m.count`、`S(a: x)`、`memblock_store(int32, m, 0, x)`。
**补充实测**（本审计独立复现）：
```text
#lib
public func f(c: bool) int32 then
    if c then
        goto skip
    end
    var x: int32 = 1
    label skip:
    return x          ; → ACCEPT
end
```

#### 2.6.2 操作数/字段类型只比较 `tag`，丢失所指类型与 `N`

| 缺陷点 | 标准 | 复现 |
| ------ | ---- | ---- |
| `src/vm/analyzer/tc_struct_check.c:1313-1315`（构造器字段值只比 `field_def->type.tag`） | §3.9.2（L641）「每个实参操作数的类型必须与对应字段声明类型**严格相同**」 | `struct A{var x: int32}` / `struct B{var y: int32}` / `struct C{var a: A}` → `C(a: B(y: 7))` **ACCEPT**，运行时把 `7` 读作 `A.x` |
| 同上 | §3.8.4（L565）`N` 一致性 | 字段 `memblock<int32,4>` 传 `memblock<int32,2>` → **ACCEPT** |
| 同上 | §3.10.1 指针须严格同型 | 字段 `ptr<A>` 传 `ptr<B>` → **ACCEPT** |
| `src/vm/analyzer/tc_memblock_check.c:469-471`（`value` 只比元素 `tag`） | §6.7.2.2（L1626）「`value` 类型必须与 `T` **严格一致**」 | `memblock_store(A, mb, 0, b)`（`b: B`）→ **ACCEPT** |
| `src/vm/analyzer/tc_ptr_check.c:449-451`（同形） | §6.8.3（L1727） | `ptr_store(ptr<float32>, ppf, p)`（`p: ptr<int32>`）→ **ACCEPT** |
| `src/vm/analyzer/tc_analyzer_pass2_rhs.c:809-813 / :627`（`cast`/`bitcast` 只查 `ptr` 源，且位宽检查在前） | §6.6.6（L1585）memblock/struct 不得参与 `cast`/`bitcast` → `TC_CE_TYPE_MISMATCH` | `cast(int32, struct_value)` **ACCEPT**（运行时才报错）；`bitcast(int32, struct_value)` 报 `BitcastWidthError`（错码） |

#### 2.6.3 名称冲突与作用域漏检

| 缺陷点 | 标准 | 复现（均 **ACCEPT**） |
| ------ | ---- | --------------------- |
| `src/vm/analyzer/tc_analyzer_pass1.c:188` 只在**当前**作用域查找 | §8.1.2（L2013）「**任意层级** `var`/`let` 与形参同名」→ `TC_CE_DUPLICATE_DEFINITION` | `public func f(a: int32)` 体内 `if true then var a: int32 = 1 end` |
| `src/vm/analyzer/tc_func_check.c:151` 只遍历顶层项 | §8.1.2（L2009/L2013）、§8.4.1、§9.1 | `public func g()` 存在时，另一函数体内 `var g: int32 = 1` |
| `src/vm/analyzer/tc_struct_check.c:792`（只查重复定义）、`tc_func_check.c:162`、`tc_module.c:573`（`else if (prog->mode == TC_MODULE_LIB)`） | §3.9.1（L594）结构体名与函数名/值绑定名冲突 → `TC_CE_FUNCTION_NAME_CONFLICT`；与导入名冲突 → `TC_CE_IMPORT_NAME_CONFLICT` | `struct S …` + `var S: int32 = 7`；`public struct F` + `public func F()`；`import pub` + `struct pub`（`#program`） |

> 与 B-12 同源：结构体名**没有**被登记进值/函数/导入名冲突表。

#### 2.6.4 控制流/可达性漏检

| 缺陷点 | 标准 | 复现（均 **ACCEPT**） |
| ------ | ---- | --------------------- |
| `src/vm/analyzer/tc_cfg.c:1071`（注释「结构可达：忽略常量边剪枝」） | §7.2.2（恒真 `while` 仅能经 `break` 终止）、§5.2.2 静态三态、§9.2（L2184） | `while true then writeln(int32,1) end` 之后写 `writeln(int32,2)` → 后者不可达，应 `TC_CE_UNREACHABLE_STATEMENT` |
| `src/vm/parser/tc_parser.c:1219-1230`（`else` 之后未调用 `tc_expect_stmt_end`，对比 `then` 在 `:1204` 已调用） | 附录 A `if_stmt`（L2833）要求 `else` 后换行 + 缩进 + `suite`；§7.1.1（L1853-1854）不支持单行 `else if` | `if c then writeln(1) else if c then writeln(2) end`（`c=false`）→ **ACCEPT 且运行时打印 `2`**（else 体无条件执行）——**非法程序被接受且语义错误** |

#### 2.6.5 `#lib` 顶层可执行语句未被拒绝

标准附录 A：`library_module = type_region , static_region , function_region`——`#lib` **没有**可执行语句区；§4.2（L933）。
实测：`#lib` + `public static var W: int32 = 1` + 顶层 `writeln(int32, 42)` → `-c` **ACCEPT**；被 `#program` 导入时该语句被**静默丢弃**（只输出 `7`）。
源码：`src/vm/parser/tc_parser.c:694-748` 的 exec 分支未查询 `mode`。

#### 2.6.6 `isize` 被误当作 `usize` 接受（`.count` / `ptr_size` 期望类型）

标准 §3.8.5（L579）`.count` 返回 `usize`；§6.8.8（L1790）`ptr_size` 返回 `usize`；§5.2.1（L1071）要求严格一致。
实测 **ACCEPT**：`var c: isize = m.count`（运行时 `writeln(isize, %d, c)` 又报 `IOError`）；`let w: isize = ptr_size(int32, nullptr)`。
对照：`var u: usize = 1` + `var c: isize = u` → 正确报 `TypeMismatch`。
源码：`src/vm/analyzer/tc_memblock_check.c:419`、`src/vm/analyzer/tc_ptr_check.c:306`（均为 `expected->tag != TC_USIZE && != TC_ISIZE`）。
**与 B-5 同根**：`isize` 被多处误当作 `usize` 的等价类型。

### 2.7 更多错码/错阶段（P0）

#### B-22 字面量不匹配在多数位置错报 `TC_CE_TYPE_MISMATCH`

标准 §11 第 3 条（L2379）：「专用码优先于通用码——例如……**`TC_CE_LITERAL_TYPE` 优先于 `TC_CE_TYPE_MISMATCH`**」；§3.6.1（L467）；§6.5.1.1（L1442）；§10.1（L2204）。

| 程序 | 应为 | 实测 |
| ---- | ---- | ---- |
| `writeln(int32, 1u)` | `LiteralTypeError` | `TypeMismatch`（"literal type does not match context"） |
| `add(int32, 1u, 1)` / `and(int32, 1u, 1)` / `shl(int32, 1u, 1)` / `not(bool, 1)` | `LiteralTypeError` | `TypeMismatch` |
| `eq(int32, x, 1.5)` / `eq(int32, x, 2u)` | `LiteralTypeError` | `ComparisonTypeMismatch` |
| `S(a: true)` / `S(a: 1.5)` / `memblock_store(int32, m, 0, 1.5)` | `LiteralTypeError` | `TypeMismatch` |
| **对照**：`var a: int32 = 1u` | `LiteralTypeError` | `LiteralTypeError` ✅ |

即：`var` 声明路径正确，**其余所有操作数位置**都退化为通用码。源码：`src/vm/analyzer/tc_analyzer_pass2_rhs.c:219`（`tc_check_literal(..., type_err)`）、`:330-337`、`:516-521`、`tc_struct_check.c:1313-1315`、`tc_semantics.c:360-367`。

#### B-23 条件 RHS 的具体码被 `TC_CE_CONDITION_TYPE` 覆盖

标准 §7.1.1（L1852）：`TC_CE_CONDITION_TYPE` 仅适用于「条件的结果类型**不是 `bool`**」；附录 B.11（L3423）：跨类型指针比较 → `TC_CE_TYPE_MISMATCH`。

同一表达式，两种位置两种码：
```text
var p: ptr<int32> = ptr_address(int32, a)
var q: ptr<uint8> = ptr_address(uint8, c)
if ptr_lt(int32, p, q) then … end   ; → ConditionTypeError   （应 TypeMismatch）
var b: bool = ptr_lt(int32, p, q)   ; → TypeMismatch        ✅
```
源码：`src/vm/analyzer/tc_analyzer_pass2_rhs.c:842-858`（把条件 RHS 的任意 `TYPE_MISMATCH` 一律改写为 `CONDITION_TYPE`）。

#### B-24 常量 `cast` 的字面量错误码错

标准 §6.6.1.1（L1528）：无后缀整数字面量的源类型为 `int64`，数值须先落在其范围内；§5.2.1 第 2 步、附录 B.6。
实测：`let a: uint64 = cast(uint64, 18446744073709551615)` → `ConstantExpressionError`；而等价的 `var` 形式报 `LiteralOutOfRange` ✅。
源码：`src/vm/analyzer/tc_const_eval.c:1319-1322`。

#### B-25 共享 `if` 的子块标签 vs 兄弟块标签优先级反了

标准 §7.3.2 步骤 4（跳入子作用域 → `TC_CE_JUMP_INTO_BLOCK`）**优先于**步骤 5（互斥分支 → `TC_CE_JUMP_INCOMPATIBLE_BLOCK`）。
实测：嵌套 `if` 中同时存在「子块同名标签」与「兄弟块同名标签」时，实现在 `src/vm/analyzer/tc_analyzer_pass2.c:85,103`（`return any;`，即按标签表顺序）返回 `JumpIncompatibleBlockError`。
*（本条为审计分代理结论；本审计未逐字复现同一构造，但源码逻辑与 §7.3.2 步骤次序确实不符，建议一并复核。）*

### 2.8 过度拒绝（P0）

#### B-26 可表示的浮点非规格化数被误报 `TC_CE_LITERAL_OUT_OF_RANGE`

标准 §2.4.1（L195-196）：「非零有限值**舍入为零**，或有限值舍入为无穷 → 字面量范围错误；**可表示的非规格化数合法**」；§3.6.1（L467）同。

| 程序 | 应为 | 实测 |
| ---- | ---- | ---- |
| `var x: float64 = 1e-308` | 合法（可表示的非规格化数） | **`LiteralOutOfRange`** ❌ |
| `var x: float64 = 5e-324` | 合法（最小正非规格化数） | **`LiteralOutOfRange`** ❌ |
| `var x: float32 = 1e-45f` | 合法（roundTiesToEven 舍入到最小正非规格化数） | **`LiteralOutOfRange`** ❌ |
| `var x: float64 = 2.3e-308` | 合法 | 接受 ✅ |
| `var x: float32 = 1e-46f` | 舍入为零 → 应拒绝 | 拒绝 ✅ |

**根因**：`src/vm/lexer/tc_lexer.c:437-441` 把 `strtod` 的 `ERANGE` 直接当失败（非规格化也会置 `ERANGE`）；`:454-461` 用 `fabs(value) < 2^-149` 作 float32 阈值，而正确判据是「按 roundTiesToEven **舍入后**是否为零」（阈值 `2^-150`）。
这一族会把**合法程序误拒**——优先级与 §2.6 的漏检同级。

#### B-27 格式说明符长度上限过窄（P2，待核实）

`src/vm/lexer/tc_lexer.c:936-957` 的 `spec_buf` 为 32 字节。`%` + 40 个 `0` + `8d` 按 §10.5 的「连续 `0` 合并」规则等价于 `%08d`，应被接受，实测报 `SyntaxError: format specifier too long`。
标准 §1.3 允许「实现资源失败」不属可观察行为，但此处是**合法形态被拒**。

---

### 2.9 词法/语法阶段（P0/P1）

> 本节的判据是**附录 A 的产生式**（§1.3：附录 A 决定语法接受集）。下列条目全部实测复现。

#### B-28（P0，崩溃）类型表达式的无界递归导致 **SIGSEGV**

`src/vm/parser/tc_parser_type.c:52`（`ptr`）/ `:82`（`memblock`）递归下降**没有深度上限**（对比 RHS 解析有 `TC_PARSER_MAX_DEPTH = 256`，`tc_parser_rhs.c:1402`）。
```sh
python3 -c "open('/tmp/p.tc','w').write('#program\nvar p: '+'ptr<'*50000+'int32'+'>'*50000+' = nullptr\n')"
build/vm/bin/tc-vm -e -c /tmp/p.tc ; echo $?    # → Segmentation fault: 11（139）
```
`memblock<…>` 同形同样崩溃；20000 层可通过。
标准 §2.1/附录 A 对这种 Token 序列应给出 `TC_CE_SYNTAX`（或至少任一诊断），**不得崩溃**——崩溃时连「首个规范诊断」都不存在。

#### B-29（P0，超集）数字分隔符规则被绕过

标准 §2.3.3（L151）：「**不可在字面量首尾放置 `_`**」；附录 A `dec_literal`/`hex_literal` 同。
实测**接受**：`var a: uint32 = 1_u`、`var a: uint32 = 1_ `（行尾）、`0x1F_U`、`0b1_u`、`0o7_u`。
**根因**：`src/vm/lexer/tc_lexer.c:161` 在 `:164` 的数字判定**之前**就把 `prev_underscore = 0`，于是 `_` 后跟任意非数字（含 `u`/`U` 后缀或空格）都被当作合法位置。

#### B-30（P0，超集）4 处列表产生式接受**尾随逗号**

| 位置 | 产生式 |
| ---- | ------ |
| `src/vm/parser/tc_parser_func.c:129-131` | `parameter_list`（附录 A L2759） |
| `src/vm/parser/tc_parser_stmt.c:558-560` + `tc_parser_rhs.c:685-687` | `funcall` 命名实参（L2773-2774） |
| `src/vm/parser/tc_parser_rhs.c:556-558` | `struct_constructor`（L3195-3198） |
| `src/vm/parser/tc_parser_rhs.c:403-405` | `memblock_elems_ctor`（L3165-3168） |

实测**接受**：`public func f(a: int32,) int32 then`、`funcall(Self.g,)`、`S(a: 1, b: 2,)`、`memblock(int32, count: 2, 1, 2,)`。

#### B-31（P0，超集）3 处列表产生式接受**缺失逗号**

同一批循环（形参表 / 结构体构造器 / memblock 逐值构造器）**不要求**逗号分隔（funcall 的循环反而有 `expected , or )` 检查，见 `tc_parser_stmt.c:560-568`）。
实测**接受且可运行**：`public func f(a: int32 b: int32) int32 then`、`S(a: 1 b: 2)`、`memblock(int32, count: 2, 1 2)`。

#### B-32（P0，超集）结构体构造器的字段值接受**嵌套构造器**

附录 A `struct_constructor` 的字段值是 `operand`；§6.1.2「调用型 RHS 不属于 `operand`」。
实测**接受且可运行**：`S(t: T(v: 1))`、`S(m: memblock(int32, count: 2, fill: 0))`（`let`/`const_rhs` 变体同）。
**根因**：`src/vm/parser/tc_parser_rhs.c:511-554` 有意放行 memblock/struct 构造器。
> 对照：`funcall` 的命名实参**允许**构造器（附录 A `named_argument` L2782），实现正确。

#### B-33（P0，**过度拒绝**）字段赋值 RHS 不接受 `funcall`

标准 §6.1.1（L1200）：「赋值 RHS 为普通 `<rhs>` 或 `<funcall>`（**字段赋值同样允许二者**……）」；附录 A 有专用产生式 `field_funcall_assign_stmt`（L2798-2800）。
实测：`s.v = funcall(Self.f)` → `SyntaxError: expected rhs expression`；而 `s.v = 1` 与 `Self.x = funcall(...)` 均正常。
**根因**：`src/vm/parser/tc_parser_stmt.c:781`（`tc_parse_field_assign_stmt` 只调 `tc_parse_rhs`，缺 `TOK_FUNCALL` 分支；整绑定路径在 `:932` 有该分支）。

#### B-34（P0，错阶段）`cast(void, …)` / `bitcast(void, …)` 未在语法阶段拒绝

标准附录 A.3 引言（L2612）：「……以及 `void` 出现在**函数返回类型以外的位置**，均必须在语法阶段拒绝」；`type` 产生式（L2518）不含 `void`。
实测：`cast(void, 1)` → `TypeMismatch: cast target must be scalar or ptr type`（SEM）；`bitcast(void, 1)` 同。
**根因**：`src/vm/parser/tc_parser_rhs.c:856` / `:914` 传 `allow_void=1`（`cast(ptr<void>, …)` 反而正确语法拒绝）。

#### B-35（P0，错码）`TC_CE_OPERAND_COUNT` 在绝大多数元数失配下缺失

标准 §1.3（L56）+ §10.1（L2215）：「`write`/`writeln`/`read` **或运算、转换等调用**的操作数个数与对应产生式不符……报告 `TC_CE_OPERAND_COUNT`，**不得改报笼统 `TC_CE_SYNTAX`**」；附录 B.1。
实测（全部报 `SyntaxError: unexpected token`）：`add(int32, 1)`、`add(int32)`、`add(int32,1,2,3)`、`write(int32)`、`writeln(int32)`、`read(int32)`、`read(int32,a,b)`、`cast(int32)`、`ptr_load(int32)`、`ptr_size(int32)`、`memblock_load(int32, m)`、`neg(int32)`。
对照：`writeln(int32, a, a)`（**多**一个操作数）**正确**报 `OperandCountError`——即同一规则的「少」与「多」两条路径不一致。
**根因**：`tc_operand_count_error`（`src/vm/parser/tc_parser.c:44`）只在 `tc_parser_stmt.c:77/84/98` 三处被调用，其余元数失配落到 `tc_expect_token(...)` → `SYNTAX`。

#### B-36（P0，错码/错阶段）格式说明符的重复标志与长度上限

（与 B-3 同源，此处补充长度维度。）
实测：`write(int32, %--d, 1)` → `SyntaxError: invalid format specifier`（应 `TC_CE_FORMAT_SPECIFIER`）；`%` + 30 个数字 + `d`（32 字符）→ `SyntaxError: format specifier too long`，而 31 字符的同类说明符**正确**报 `FormatSpecifierError`。
**根因**：`src/vm/lexer/tc_lexer.c:938` 的 `char spec_buf[32]` 固定缓冲 + `:1010-1014` 把 `tc_format_spec_parse` 的任何失败都映射为 `TC_CE_SYNTAX`。而附录 A `format_width = non_zero_digit , { digit }`（L2549）**无长度上限**。

#### B-37（P0，**过度拒绝**）首字母大写的变量在嵌套字段访问中被误解析

**根因**：`src/vm/parser/tc_parser.c:352-370`（`tc_parse_field_access_base`）只要标识符首字符是 `A`–`Z` 就把 `X.y` 整体当作**导入成员**基址，不做名称解析。
实测：
```text
var A: Outer = Outer(i: Inner(v: 1))
writeln(int32, A.i.v)   ; → UndefinedVariable: undefined variable 'i'
```
对照：同一程序把 `A` 换成 `a` → 正常输出 `1`。`A.i.v = 5` 同样失败。
标准附录 A 的 `field_access`（L3205-3209）**没有任何大小写规则**——标识符的分类必须由名称解析决定。

#### B-38（P1，错码/错阶段）`#lib` 中裸顶层 `var`/`let` 由分析器报 `MODULE_LAYER`

实测：`#lib` + `var x: int32 = 1` → `ModuleLayerError: non-static value declaration not allowed in #lib`。
标准附录 A 的 `library_module`（L2679-2683）只接受带可见性的 `static` 成员；§1.3（L56-58）把 `TC_CE_MODULE_LAYER` 限定为「跨层交错」等 4 类**结构类语法阶段**形态，而「`#lib` 中裸 `var`」不在其列 → 应报语法拒绝 `TC_CE_SYNTAX`（SYN）。
**根因**：`src/vm/parser/tc_parser.c:714-721` 的 VAR/LET 路径没有 `#lib` 守卫，改由 `src/vm/analyzer/tc_module.c:222-227` 在 SEM 报码。
*（`MODULE_LAYER` vs `SYNTAX` 的具体归属可裁决；但「SEM 报出、晚于后续 SYN 错误」这一阶段错位没有疑义。）*

#### B-39（P1，阶段错位）SYN 阶段检查放在 SEM，导致更晚的语法错误抢先

`#program` 中的 `Self` 检查在 `src/vm/analyzer/tc_scope.c:164-203`（解析器只抓行首 `Self`，`tc_parser.c:609-612`）。
实测：`#program` / 第 2 行 `var b: int32 = Self.x` / 第 3 行 `var a: int32 = add(int32, 1)` → 报第 3 行 `SyntaxError`。按 §11 第 1 条（阶段优先）与 §1.3（L57），两处都属 SYN，应按**源序**报第 2 行的 `TC_CE_PROGRAM_MODE_MISUSE`。

#### B-40（P1，阶段错位）SEM 码由解析器发出，导致更晚的语法错误落败

`src/vm/parser/tc_parser_struct.c:33-39`（`@padding` 形态）、`tc_parser_type.c:99-104` / `tc_parser_rhs.c:331-336`（memblock `N` / `count:` 来源）在**语法阶段**直接发出 `TC_CE_CONSTANT_EXPRESSION`，而标准 §3.9.3（L660）/§3.8.1 规定它们在**静态语义阶段**判定。
实测：`@padding(4u)` 在第 3 行、第 5 行有 `add(int32, 1)` 语法错误 → 报第 3 行 `ConstantExpressionError`；按 §11 阶段优先应先报第 5 行 `SyntaxError`。

#### B-41（P2，待核实）深一级的 `end` 报 `INDENT_INSUFFICIENT` 而非 `INDENT_ELSE_END`

`src/vm/parser/tc_parser.c:1122-1135` 先做「缩进增量必须恰为一级」检查，再做 `else`/`end` 对齐检查。
实测：`if true then` / 4 空格体 / **8 空格 `end`** → `IndentInsufficientError`。
附录 A.2 末段与附录 B.1 把「`else`/`end` 与对应块头不对齐」归入 `TC_CE_INDENT_ELSE_END`；恰深一级时实现确实报 `ELSE_END`（语料 `indent_end_mismatch.tc` 钉住了该例）。归属需裁决。

### 2.10 执行器/运行时（P0/P1）

> 本节全部为**新**发现，实测环境 `build/vm/bin/tc-vm`（并用 `build-asan` / `build-ubsan` 复核）。

#### B-42（P0，崩溃 + 越界读写）`bitcast` 伪造指针后不校验槽索引上界

标准 §3.10.9/§6.6.6 允许 `bitcast(ptr<T>, <整数>)`；§1.3 要求「零未定义行为」，且实现定义清单只含 4 项。
实测：
```text
#program
var p: ptr<int32> = bitcast(ptr<int32>, 0x7FFFFFFD)
var v: int32 = ptr_load(int32, p)
writeln(int32, v)
```
→ **`Segmentation fault: 11`（exit 139）**。
`build-asan` 下为 `AddressSanitizer: heap-buffer-overflow … READ of size 16 … #1 tc_exec_ptr_load`；`ptr_store` 为 `WRITE of size 16`；`memcopy_unsafe` 的 `ctx->slots[dst_slot/src_slot]` 同。
**即：当前实现提供了一条任意进程内存读写原语**（越界读到的 `TcValue.type == NULL` 时 `tc_io.c:777` 还会空指针解引用，`build-ubsan` 可复现）。
**根因**：`src/vm/executor/tc_ptr_exec.c:65-69 / 98-101`（`slot` 只判 `slot < 0`，不判上界）、`src/vm/executor/tc_memblock_exec.c:544-546`。
**注**：需由标准侧裁定「整数 `bitcast` 得到的非真实编码」应报哪个码（§1.3 的实现定义清单未覆盖）。

#### B-43（P0，漏检）`memblock_copy` 在 `length == 0` 时跳过全部区间检查

标准 §6.7.2.3：区间合法性 `n ≥ 0 ∧ 0 ≤ d ∧ 0 ≤ s ∧ d + n ≤ count_dst ∧ s + n ≤ count_src`；§6.7.2.4 只把「下标**等于** count」放宽（`n = 0` 允许下标等于 `count`），下标**大于** `count` 仍非法。
实测（`a`/`b` 均 `count: 4`）：
```text
var s: usize = 9u
var n: usize = 0u
memblock_copy(int32, b, 0, a, s, n)   ; → 正常结束并打印 1
```
应为 `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`。**根因**：`src/vm/executor/tc_memblock_exec.c:452` 的 `if (length > 0 && (…))` 在 `length == 0` 时短路。

#### B-44（P0，漏检）`memblock_copy` 的常量区间从不做静态检查

（与 B-21 同源，此处给出运行期对照。）
实测：`memblock_copy(int32, b, 0, a, 0, -1)` → `-c -e` **exit 0**（静态通过），运行期才报 `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`；常量负 `dst`/`src` 下标同。
应为编译期 `TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE`（§6.7.2.4 表首行）。
**根因**：`src/vm/analyzer/tc_memblock_check.c:474-515` 未调用 `tc_memblock_check_index_literal`（`load` 在 `:306`、`store` 在 `:465` 都调用了）。

#### B-45（P0，静默错值）strict `shl` 在被移位数为 0 时不报溢出

标准 §6.4.2（L1392）/§6.4.2.1（L1400-1402）：strict `shl` 的溢出判定与**被移位数的值无关**，`k ≥ n` 即溢出。
实测：`var z: int32 = 0` / `var k: int32 = 32` / `shl(int32, z, k)` → **`0`，exit 0**；应 `TC_RE_INTEGER_OVERFLOW`。
常量形式 `let r: int32 = shl(int32, 0, 32)` 同（应 `TC_CE_CONSTANT_OVERFLOW`）。
**根因**：`src/vm/runtime/tc_sem_bitwise.c:191` 的 `if (val_bits == 0) { *out = 0; return 0; }` 早退先于 `:196` 的 `k >= n` 判定。（对照：非零值的 `shl` 溢出实测正确。）

#### B-46（P0，错误输出）`%f` 在极小量时错误进位

标准 §10.4（L2317）：十进制输出以源值的**精确数学值**为输入，按 roundTiesToEven 舍入到所需位数。
实测：`var a: float64 = 0.00009` + `writeln(float64, %.3f, a)` → **`0.001`**；正确值为 `0.000`。
同类：`%.0f` of `2^-40` → `1`（应 `0`）；`%.6f` of `6e-8` → `0.000001`（应 `0.000000`）。
**根因**：`src/vm/runtime/tc_io.c:324-350` 的 `if (keep <= 0)` 分支只按 `digits[0] > 5` 进位；`keep < 0` 时应恒出零，仅 `keep == 0` 才用 `digits[0]` 判定。
**注**：该审计对 132 组浮点格式做了位级对照，**仅此一项不符**——I/O 其余部分质量很高。

#### B-47（P0，精度）float32 字面量经 double 二次舍入

标准 §2.4.1（L195）：「有限十进制字面量先解析为精确数学值，再**直接**按 roundTiesToEven 舍入到后缀决定的 binary32/binary64；**不得通过另一浮点类型中转**」。
实测：`var x: float32 = 1.0000001788139343f` → `bitcast(uint32, x)` = **`3f800002`**；该十进制精确值小于中点 `1.000000178813934326171875`，直接舍入应为 **`3f800001`**（经 double 后恰落中点，ties-to-even 进位）。
对照：`read(float32, …)` 输入同一串（`tc_io.c:1213` 直接用 `strtof`）→ `3f800001` ✅——**同一实现内部两套口径**。

#### B-48（P0）浮点边界字面量被拒（与 B-26 同源，补充上界）

除 B-26 的下界问题外，上界判据 `fabs(value) > FLT_MAX` 也过严：`3.4028235e38f` 应舍入到 `FLT_MAX`（`0x7F7FFFFF`）却被拒。正确判据是「**舍入后**为 ±∞ 才非法」。
另：`src/vm/lexer/tc_lexer.c:437` 的 `strtod` `ERANGE` 判据对可表示的次正规结果也成立，`2.2e-308`、`5e-324` 均被误拒。

#### B-49（P1，UB）`ptr_sub` 的 `usize` 偏移在 ≥ 2^63 时有符号溢出

`src/vm/executor/tc_ptr_exec.c:121`（`*out = (int64_t)offset_value.bits;`）+ `:149`（`new_slot = slot - offset`）。
实测（`build-ubsan`）：偏移 `9223372036854775808u` → `signed integer overflow: 0 - -9223372036854775808`；非 sanitizer 下产生垃圾指针，配合 B-42 崩溃。
标准 §6.8.5（L1754/1767）：`offset` 为 `usize`，按无符号语义计算。

### 2.11 AOT 后端与嵌入（P0/P1/P2）

#### B-50（P0）AOT `-r` 拒绝**合法**程序：生成的 C 里所有 TC 函数都是 `static`，再被 `-Werror` 判为未使用

`src/aot/tc_aot_emit_func.c:39`/`:89` 把每个 TC 函数发射为 `static`；`src/aot/main.c:186` 用 `-Wall -Wextra -Werror -pedantic` 编译。
实测（`tests/valid/MathLib.tc` 等**每一个** `#lib`-only 语料）：
```text
$ tc-vm  tests/valid/MathLib.tc   → rc=0
$ tc-aot -r "$(pwd)/tests/valid/MathLib.tc"
  …/MathLib.c:18:13: error: unused function 'tc_aot_func_0' [-Werror,-Wunused-function]
  → rc=1
```
抽测 `tests/valid/` 下 5 个 `#lib`-only 文件，**全部** VM 通过而 AOT 失败（该审计报称共 28 个）。
**为什么既有测试没抓到**：`scripts/aot/run_tests.sh` 的 220 个 `run_diff_test` 里**没有一个**是 `#lib`-only 文件；AOT 套件因此全绿。

#### B-51（P0）`ptr_address(T, <函数形参>)` 使 AOT 代码生成失败

标准 §3.10.3（L791）/§6.8.4/§6.8.10 **明确允许**对函数形参取地址（随后 `ptr_store` 写穿报 `CONSTANT_ASSIGNMENT`）。
实测：
```text
#lib
public func f(x: int32) ptr<int32> then
    var p: ptr<int32> = ptr_address(int32, x)
    return p
end
```
| 入口 | 结果 |
| ---- | ---- |
| `tc-vm --check` | rc=0 ✅ |
| `tc-aot --check` | rc=0 ✅ |
| `tc-aot -o x.c` | **`code generation failed`** ❌ |

**根因**：`src/aot/tc_aot_emit_rhs.c:492-502` 按**名字**重新解析（`tc_aot_resolve_var_slot`，`tc_aot_codegen.c:258`）而不用 Pass2 已固化的绑定；形参在代码生成期不可按名解析。
**注**：`tests/valid/ptr_address_param_load.tc` 存在，但只被 `--check` 覆盖，故未被发现。

#### B-52（P0）strict 浮点下溢按**宿主** `FE_UNDERFLOW` 判定 → 与标准不符且随构建开关变化

标准 §6.3.2（L1246）要求 **tininess-after-rounding**，并明确「分类与判定结果**不得随宿主浮点环境变化**」。
实测：`mul(float32, 2^-126, 1-2^-24)` 的精确积恰为中点 `2^-126 − 2^-150`，按 roundTiesToEven 舍入到 `2^-126`（最小正规数，**不是 tiny**）→ 按标准**不应报错**，结果 `0x00800000`。
实际：
| 路径 | 结果 |
| ---- | ---- |
| VM | `error [FloatUnderflow]: float underflow` ❌ |
| `tc-aot -r`（带 `-DTC_HAVE_FENV=1`） | 同样报错 ❌ |
| 同一份生成的 C **不带** `-DTC_HAVE_FENV` 编译 | 打印 `8388608`，exit 0 ✅ |

**根因**：`src/vm/runtime/tc_sem_fp.c:533-535` 使用宿主 `FE_UNDERFLOW`（arm64 为 tininess-**before**-rounding）。

#### B-53（P0）`%.80d` 及其以上精度触发 `TC_RE_IO`，但标准允许精度到 65535

标准 §10.4（L2345）：精度取值 `0`～`65535`。
实测：`writeln(int32, %.79d, a)` 正常；`writeln(int32, %.80d, a)` → **`error [IOError]: output failed`**（运行期错误，且报文说"输出失败"，与实际原因无关）。
**根因**：`src/vm/runtime/tc_io.c:653` 的 `char digits[80]`。边界正好是 80。

#### B-54（P1）同一函数内裸名引用本库成员：作为依赖时报错码与作为入口时不同

标准 §4.3（L949-961）/§8.4.1（L2115）要求**无条件**报 `TC_CE_FUNCTION_SCOPE_ACCESS`。
实测：同一份 `#lib` 源文本——作为入口文件时 `FunctionScopeAccessError` ✅，被 `#program` 导入时 `UndefinedFunction` ❌。
**根因**：分析器的「本库成员名索引」作用域上下文未对依赖模块设置。

#### B-55（P2）其余运行时/工具链细节

| 项 | 位置 | 现象 |
| -- | ---- | ---- |
| `tc_embed_get_error` 返回**过期**消息 | `src/vm/embed/tc_embed.c:353-377` | 后续成功调用只清 `error_flag`，不清 `error_message` |
| `run failed (exit 256)` | `src/aot/main.c:395` | 打印 `system()` 的原始 wait status 而非子进程退出码 |
| 死代码 `empty_opts` | `src/libtc/tc_lib.c:276/309` | `memset` 后从未使用 |
| AOT `tc_aot_ptr_load` 缺 bool 规范化 | `src/aot/tc_aot_rt.c:340-356`（对照 `tc_ptr_exec.c:76-79`） | 当前不可观察（所有 bool 消费者都做非零→1），属潜伏 |

**AOT 侧「伪造指针」的确定性**：`bitcast(ptr<T>, <整数>)` 越界（B-42）在 AOT 侧的表现**依内存布局而变**——审计中先后观察到 `Bus error → exit 1`、`exit 0 且打印 0`。§1.3 要求「单个实现必须在每次运行时保持确定」，这种不确定性本身即违规（根因同 B-42）。

### 2.12 补充发现（B-57～B-66）

> 以下 10 条为 B-1～B-56 之外另行实测确认的偏差，编号续用 B 系列；每条附最小复现。

#### B-57（P0，进程级）AOT 侧 `bitcast` 伪造指针解引用不崩溃、静默读任意内存且逐次非确定

标准 §1.3 要求「零未定义行为」，且「单个实现必须在每次运行时保持确定」（实现定义行为亦不得非确定）。B-42 的同一程序在 AOT 侧：

```text
#program
var p: ptr<int32> = bitcast(ptr<int32>, 0x7FFFFFFD)
var v: int32 = ptr_load(int32, p)
writeln(int32, v)
```

`tc-aot -r` **不崩溃**（`rc=0`），连续 5 次运行输出互不相同的垃圾值（`63684056` / `1208226900` / `1852140901` / `1197434691` / `1634030188`）。这既违反「零未定义行为」，又违反单实现确定性，并与 VM 侧的崩溃（B-42）构成同一实现两后端的可观察分歧。B-55 尾注已记录该不确定性，本条给出稳定复现。
**根因**（同 B-42）：AOT 运行时（`src/aot/tc_aot_rt.c`）的槽索引无上界校验。

#### B-58（P0，漏检）`memblock_copy` 空拷贝的 **dst 侧**同源漏检

标准 §6.7.2.3 的区间合取含 `d + n ≤ count_dst`；§6.7.2.4 只把「下标**等于** `count`」放宽。
实测（两侧 `count: 4`）：`memblock_copy(int32, b, 9, a, 0, 0)`（`dst_index = 9 > 4`、`n = 0`）→ VM/AOT 均输出 `1`、`rc=0`。应为 `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`。即 `n = 0` 时 dst 与 src 两侧的区间合取式同时被短路（B-43 只举了 src 侧）。

#### B-59（P0，漏检）`memblock_copy` 常量负 **dst 下标**不做静态检查

标准 §6.7.2.4 表首行：编译期可确定的负下标 / 负 `length` → 静态 `TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE`。
实测：`memblock_copy(int32, b, -1, a, 0, 1)` → `-c` rc=0（静态通过），运行期才报 `MemblockIndexOutOfRange`；AOT 同。与 B-44（常量负 `length`）同根，但 B-44 只举了 `length`。

#### B-60（P1，错码/错阶段）格式宽度/精度越界且 Token 超 32 字节时被降级为 LT/SYN

标准 §10.5：宽度或精度超出 `65535` → `TC_CE_FORMAT_SPECIFIER`（SEM）。
实测：`%` ＋ 35 个 `9` ＋ `d` → `SyntaxError: format specifier too long`；而**短** Token 越界（`%99999999999d`、`%99999d`）**正确**给 `FormatSpecifierError`。与 B-27/B-36 同根（`tc_lexer.c` 的 `char spec_buf[32]`），但性质是**越界诊断被降级并改阶段**。

#### B-61（P1）非 `bool` 条件的错误码随 RHS 形态不一致

标准 §7.1.1 要求「条件的结果类型不是 `bool`」→ `TC_CE_CONDITION_TYPE`；§11 第 3 条只列举了 `TC_CE_LITERAL_TYPE` 优先于 `TC_CE_TYPE_MISMATCH`，**未规定**它与 `CONDITION_TYPE` 的次序。
实测：`if 1 then` → `[LiteralTypeError]`；`if x then`（`x: int32`）→ `[ConditionTypeError]`。同一「非 `bool` 条件」因 RHS 形态给出不同码；标准侧需先裁决优先级，否则与 B-22/B-23 的口径冲突。

#### B-62（P1，超集）`const` 变体的列表产生式同样接受尾随/缺失逗号

B-30/B-31 只覆盖非 `const` 路径。附录 A `const_struct_constructor`（L3203）与 `const_memblock_elems_ctor`（L3190）同形：无尾随逗号、且要求逗号分隔。
实测**全部 rc=0**：`let s: S = S(a: 1, b: 2,)`、`let s: S = S(a: 1 b: 2)`、`let m: memblock<int32, 2> = memblock(int32, count: 2, 1, 2,)`、`…, 1 2)`；对照 `memblock(int32, count: 2, fill: 0,)` 正确拒绝。

#### B-63（P1，超集）顶层行缩进完全不被校验

实现词法层**没有** `INDENT`/`DEDENT` token；附录 A.2 规定「缩进增加一级即生成 `INDENT`」，而顶层产生式（`program_module` / `program_exec_region` / `import_region`，L2629-2662）不含 `INDENT`，只有 `suite`（L2849）消费它。
实测：`#program` 后 4/8 空格的语句、`#lib` 顶层缩进、`end` 后缩进语句均 `rc=0` 并正常执行。合法形态边界需标准侧裁决。

#### B-64（P1）文件名含内部点 → 本地结构体解析失败

标准 §4.1 只规定一个 `.tc` 文件 = 一个模块，未禁止含点文件名；§3.9.1/§6.1.2 要求本地声明的结构体名可解析。
实测：同一源文本（`struct A` ＋ `var v: A = A(x: 7)` ＋ `writeln(int32, v.x)`），`abc.tc` → 输出 `7`、`rc=0`；改名 `a.b.c.tc` → `error [UndefinedStruct]: undefined struct 'a.b.c.A'`、`rc=1`。纯标量绑定不受影响。

#### B-65（P0，合法程序被拒）跨模块同名符号导致 CFG 假阳性 `TC_CE_UNINITIALIZED_VARIABLE`

标准 §9.2/§11：确定初始化判定须基于真实绑定，不得因跨模块同名而误报。
实测：入口 `#program` 声明 `var b: bool = funcall(FieldLib.fb)` 后 `writeln(bool, b)`，而 `FieldLib.fb` 内恰有同名局部 `var b` → `-c` 报 `[UninitializedVariable]: use of uninitialized variable`，**VM 与 AOT 同**。对照：仅把入口变量改名（或把库内局部改名）即通过并输出 `1`/`true`。
**根因**：CFG 跨模块按 `name + def_stmt_index` 解析符号，而 `stmt_index` 每模块由 `tc_stmt_index_reset` 重置 → 写入槽与读取槽错位（`src/vm/analyzer/tc_cfg.c`）。

#### B-66（P1，接受集过宽 ＋ 错码）`static let` 经 `Self.` 的引用规则不符

标准 §4.2/§5.2.1：静态成员初始化器引用**源序更晚**或**自身**的成员 → `TC_CE_UNDEFINED_VARIABLE`。
实测：
* `public static let k: int32 = Self.j`（`j` 在下一行）→ **被接受**，导入运行输出 `41`（VM/AOT 同）；
* `Self.k` 自引用 → `[ConstantExpressionError]: circular static let dependency`（应 `UNDEFINED_VARIABLE`）；
* **裸名**形式（`k = j`）反而**正确**报 `UndefinedVariable`。

`static var` 路径（`Self.x` 自身/更晚引用）行为正确。

---

## 3. 设计文档与标准的偏差

### 3.1 设计文档（P0/P1）

> 本节按文档集中列出**等级为 P0/P1** 的文档侧缺陷；P2 级统一见 §3.2。编号 `C-1`～`C-21` 覆盖全部 6 份设计文档与 2 份参考文档：`C-1`～`C-9` 编译器标准、`C-10`～`C-11` AOT 详设、`C-12`～`C-14` VM 详设、`C-15`～`C-18` VM 命令行参考、`C-19` libtc、`C-20`～`C-21` TC-Embed。

#### C-1（P0）§8.5 / §11.4.2 **重定义**了 `FUNCALL_POSITION` / `FUNCALL_RESULT_TYPE`

| 位置 | 原文 |
| ---- | ---- |
| §8.5（L1056-1059） | 表：`var x: T = funcall(...)` 对 **void** 函数 → `TC_CE_FUNCALL_POSITION`；`x = funcall(...)` 对 void → `TC_CE_FUNCALL_POSITION` |
| §11.4.2（L1464） | `TC_CE_FUNCALL_POSITION` = 「非 `void` 结果被丢弃，**或 `void` 调用用于 `var` 初始化或已有变量赋值**」 |
| §11.4.2（L1465） | `TC_CE_FUNCALL_RESULT_TYPE` = 「`var` 声明类型或赋值目标类型与非 `void` 返回类型不同」 |
| 标准 §8.2.3（L2067-2069） | void 作值 → `TC_CE_FUNCALL_RESULT_TYPE`；非 void 独立调用 → `TC_CE_FUNCALL_POSITION` |
| 附录 B.12（L3438-3439） | 同标准，且 `FUNCALL_RESULT_TYPE` 触发条件**独占**给 void 作值 |

§1.3 规定附录 B「**码名与阶段列为规范性名称**（唯一权威定义仍在正文引注条款）」。编译器标准 §11.4.2 的新定义**与权威条款直接冲突**，且它引入的 `FUNCALL_RESULT_TYPE` 新语义（接收类型不符）与附录 B.4 的 `TC_CE_TYPE_MISMATCH` 重复。
**建议**：§8.5 表与 §11.4.2 两行按附录 B 回改；实现同步（B-1）。

#### C-2（P0）§1.3 第 1 条以「13 阶段优先」覆盖标准 §11 的「同阶段按源序」

| 位置 | 原文 |
| ---- | ---- |
| 编译器标准 §1.3（L117） | 「**阶段优先**：先报告编号较小阶段的错误。」 |
| 编译器标准 §1.3（L127） | 「第 4–8、11、12 阶段 → **SEM**」 |
| 标准 §11（L2387-2389） | 「2. **同阶段内按源序位置**：行号升序；同一行内按 Token 出现次序（列序）升序。」 |

文档**自认**这 9 个内部阶段同属 SEM，却要求按内部阶段号而非源序选首个诊断——两者在同一句话里矛盾。B-2 是它的直接后果。
同节还有一处冲突：§1.3（L190）「`let` 类型闭合检查在第 6 阶段完成：……标识符/单层调用的**结果类型不同时报 `TC_CE_TYPE_MISMATCH`**」——漏掉了标准 §5.2.1 第 3 步「解析为 `var` / 形参时**立即**报 `CONSTANT_EXPRESSION`，不比较结果类型」的例外（同节末尾另有一句写对了，此处遗漏），对应实现缺陷 B-4。
**建议**：§1.3 第 1 条改为「阶段优先（LT→SYN→SEM→CT）；**同一诊断类阶段内按源序位置**」，并保留 13 阶段只作为实现步骤说明；`let` 闭合一句补 `var`/形参例外。

#### C-3（P1）§4.6 把 `#program` 的导入名冲突集合限缩为 `var`/`let`

| 位置 | 原文 |
| ---- | ---- |
| 编译器标准 §4.6（L562） | `#program` 受检查的顶层名称 = 「顶层 `var` 名、顶层 `let` 名」 |
| 编译器标准 §4.6（L567） | 「在 `#program` 中，若 `import foo` 且该模块顶层存在 `var foo` 或 `let foo`，报告本错误」 |
| 标准 §3.9.1（L594） | 「与导入名冲突 → `TC_CE_IMPORT_NAME_CONFLICT`」（无模式限定） |
| 附录 B.2 | 「导入名与本模块成员名（**含结构体等类型名**，§3.9.1）冲突」 |
| 编译器标准自身 §3.3（L338） | 无模式限定地写「与导入名冲突 → `TC_CE_IMPORT_NAME_CONFLICT`」 |

对应实现接受集缺口 B-12。

#### C-4（P1）§4.6 括注与 §4.6 第 3 条自相矛盾

§4.6（L562）括注：「（同名模块可被不同名称多次导入，只要不同 `import` 的命名自身不冲突）」。
§4.6（L574）第 3 条：「同一模块被不同名称 `import` 两次（如 `import a` 后 `import b` 且 `a.tc` 与 `b.tc` 为同一文件）→ 4b 在冲突检查之前先由重复导入检查报告 `TC_CE_DUPLICATE_IMPORT`」。
附录 B.2：`TC_CE_DUPLICATE_IMPORT` = 「同一模块被多次 `import`」。
**即同一节一处说允许、一处说拒绝。**

#### C-5（P1）§3.2 / §6.7 / §11.4.6 把静态 `MEMCOPY_UNSAFE_INVALID_RANGE` 扩展到负**下标**

§3.2（L325）：「编译期可确定 `length < 0` **或下标数学值 `< 0`** → `TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE`」；§6.7、§11.4.6 同。
标准 §6.8.9 只把 `length < 0` 归静态，负下标属运行时（附录 B.11/B.13、§11.1 亦只写 `length`）——但标准自身也有 A-3 的分裂。
**实测**：实现对负下标只在运行时报码（与附录 B 一致），故此处**文档比标准更宽**，按文档实现会误拒标准接受的程序。

#### C-6（P1）§11.4.1 `TC_CE_DUPLICATE_LABEL` 写「同一**作用域**」

标准附录 B.9：「同一**代码块**内出现重复标签名」；§7.3.2（L1955）：「同块重复标签 → `TC_CE_DUPLICATE_LABEL`；**跨块同名合法**」；编译器标准自身 §9.1（L1114）亦写「同块」。
**实测**：跨兄弟块同名标签被接受 ✅（实现正确）。按 §11.4.1 字面实现会误拒。

#### C-7（P1）§3.6 `u`/`U` 字面量行内自相矛盾

§3.6（L401）规则列：「**不得带 `-`（语法拒绝）**」；同一行错误码列：「`TC_CE_LITERAL_TYPE`（误用于有符号上下文**或带 `-`**）」。
§11.4.1（L1399）：「负号整数字面量带 `u`/`U`……属**第 2 阶段** `TC_CE_SYNTAX`」——既非 SEM 的 `LITERAL_TYPE`，也非「第 2 阶段词法」。
标准 §2.3.1（L124）「`-42u` 为非法」+ 附录 A `integer_literal` 注释「禁止 `-42u`」→ **语法拒绝** `TC_CE_SYNTAX`（SYN）。
**实测**：`var a: int32 = -42u` → `SyntaxError: negative value cannot use unsigned suffix` ✅（实现正确）。

#### C-8（P1）§6.0 模式矩阵把 `shr` 与 `div`/`mod`/`abs` 混为一谈

§6.0（L677）：「`div`/`mod`/`abs`/`shr` \| 全部整数 \| 无模式关键字 \| 写了模式关键字 → `TC_CE_MODE_MISMATCH`」；§3.5（L374）同。
但附录 A 的 `wrap_shift_expr` **只接受 `shl`**——普通 RHS 的 `shr(int32, wrap, x, k)` 是**语法拒绝** `TC_CE_SYNTAX`。编译器标准自身 §1.3（L187）与 §6 均如此写，§11.4.1 同。
**实测**：普通 RHS `shr(int32, wrap, x, k)` → `SyntaxError: wrap cannot be used with shift operations` ✅；`let y: int32 = shr(int32, wrap, 8, 1)`（`const_shift_expr`）→ `ModeMismatch` ✅。实现正确，文档错。

#### C-9（P1）§6.0 / §3.5 无符号算术写 `wrap` 处「（语法阶段也可拒绝）」

§6.0（L675）、§3.5（L381）：无符号算术「写了 `wrap` → `TC_CE_MODE_MISMATCH`（**语法阶段也可拒绝**）」。
标准 §6.3.1：组合不在矩阵 → `TC_CE_TYPE_MISMATCH` 或 `TC_CE_MODE_MISMATCH`；附录 A 的 `mode_binary_arith_expr` 对无符号类型同样提供模式槽，即 Token 序列被 EBNF 接受 → 只能在 SEM 报 `MODE_MISMATCH`。括注会让实现产出 `SYNTAX` 而非 `MODE_MISMATCH`。

#### C-10（P1）AOT 详设 §11.1 指针 shim 原型描述的是**宿主地址模型**，与实现和标准都冲突

| 位置 | 原文 |
| ---- | ---- |
| AOT 详设 §11.1（L643/649） | `int tc_aot_ptr_load(void **target, void *ptr, size_t element_size, …)`；`void *tc_aot_ptr_address(void *slot_addr);`；注释「返回**绑定槽的地址**（`&slots[X]` 或 `&static_slots[Y]`）」 |
| 实现 `src/aot/tc_aot_rt.h:56` | `uint64_t tc_aot_ptr_address(int slot);`，`src/aot/tc_aot_rt.c:336-338`：`return ((uint64_t)slot << 1) \| TC_AOT_PTR_TAG;` |
| 标准 §3.5（L449） | 「……**未经 `bitcast` 时**，该位模式对程序不可见……**实现定义的是该抽象槽编码**，而非宿主对象地址」 |
| 标准 §1.3（L79） | 「单个实现必须在每次运行时保持确定。不存在程序可在 TC 抽象机器中观测的非确定性行为」 |

若照文档实现（返回宿主对象地址），`bitcast(usize, ptr_address(int32, x))` 会暴露 ASLR/PIE 相关地址，同一程序两次运行 stdout 不同 → 违反实现定义行为的确定性要求。
**实测**：真实实现确定（`bitcast(usize, p)` 三次运行均为 `1`），文档与实现脱节。
同节还并存**三套互斥调用约定**：全局 `slots[]`+`tc_aot_ret_<id>`（L197/L237/L285/L439）、每函数 `locals[]`+局部 `retval`（L416-417/L451/L463）、`TcDiagnostic::ret_kind`（L459/L464，该字段在 `src/**` 中不存在）。

#### C-11（P1）AOT 详设 §16.1 差分等价判据**多claim且漏项**

| 位置 | 原文 |
| ---- | ---- |
| AOT 详设 §16.1（L50 / L799-803） | 「生成程序与 VM 在 stdout、**stderr 分类**、**退出状态**、数值位模式和运行时错误时机上一致」；「比较至少包括：……stderr 的 TC 错误**种类和关键消息**；退出成功/失败」 |
| 标准 §1.3（L65） | 「**可观察行为**：包括是否通过静态检查、**首个**规范诊断及其错误码（其选取规则见 §11「诊断顺序」）、按源序成功提交到 TC 抽象标准输出的字节、正常结束或首个运行时错误及错误码。槽位地址、帧布局、宿主字节序等宿主实现细节不是可观察行为。」 |

文档把「stderr 的**消息文本**」与「进程退出状态」升格为等价判据（标准未列，且同一码不同措辞在两个合规实现间是允许的），同时漏掉「**首个**诊断」与 §11 四步顺序这一最关键判据。

#### C-12（P1）VM 详设 §5.3 发明了不存在的函数签名语法 `-> return_type` 且称其可选

| 位置 | 原文 |
| ---- | ---- |
| VM 详设 §5.3（L378） | `[public\|private] func name(param1: type1, ...) -> return_type then` |
| VM 详设 §5.3（L383） | 「`-> return_type` **可选，省略表示 `void` 返回**。」 |
| 标准附录 A（L2754-2756） | `function_definition = ( "public" \| "private" ) , "func" , identifier , "(" , [ parameter_list ] , ")" , **return_type** , "then" , NEWLINE , suite , "end"` |
| 标准 §8.1.1（L1999） | 「`func <函数名>(<形参表>) <返回类型> then`」 |

标准中**没有** `->`，返回类型**必需**（`void` 必须显式写）。
**实测**：`func f() -> int32 then` 与省略返回类型均 `SyntaxError`。照此文档实现的解析器会接受非法程序、拒绝合法程序。
（CLI 参考 §8.1 的示例写法反而是正确的。）

#### C-13（P1）VM 详设 §14.1 精度范围错误 + 阶段归属错误

VM 详设 §14.1（L997）：「字段宽度 `width` 与精度 `.precision` 取值均为 **`1`～`65535`**」；「**Analyzer** 在执行前验证格式符、类型和 **operand 数量**」。
标准 §10.5（L2345）：精度「取值 **`0`**～`65535`；仅写 `.` 等价于 `.0`」；`width` 才是 `1`～`65535`。
附录 B.1：`TC_CE_OPERAND_COUNT` 阶段列为 **SYN**（语法阶段），非 Analyzer/SEM。
**实测**：`%.0f` 被接受并输出 0 位小数 ✅；`writeln(int32, a, a)` 报 `OperandCountError`（SYN）✅。

#### C-14（P1）VM 详设 §5.3 把形参重复检查放在 Parser

VM 详设 §5.3（L386）：「Parser 收集形参名列表，检查是否含重复 → `TC_CE_DUPLICATE_PARAMETER`。」
附录 B.3：`TC_CE_DUPLICATE_PARAMETER` 阶段 = **SEM**；附录 A（L2612）：「形参名是否重复由 §8.1.2 在**静态语义阶段**报告 `TC_CE_DUPLICATE_PARAMETER`，**不在本 EBNF 拦截**」。VM 详设自身 §2.1 阶段 5 与 §15.4 Parser 行都按 SEM 写。

#### C-15（P1）CLI 参考 §3.5 登记了不存在的「默认搜索路径」

CLI 参考 §3.5（L139-141）：「模块搜索路径优先级：1. 入口文件所在目录；2. `-I` 指定的路径（按参数顺序）；**3. 默认搜索路径**。」
`src/vm/analyzer/tc_module.c:354-407` 只查入口目录与 `-I`；`--help` 文本亦为「Module search order: entry directory, then -I paths」；全仓无默认路径常量/环境变量。libtc 设计说明书 §2.4/§15.4 同错。

#### C-16（P1）CLI 参考 §8.4 示例是**非法 TC 程序**

CLI 参考 §8.4（L437-439）：
```text
var arr: memblock<int32, 5> = memblock(int32, count: 5, 1, 2, 3, 4, 5)
var idx: int32 = 2
var p: ptr<int32> = ptr_address(int32, arr)
```
标准 §6.8.4：「`T` 必须与 `identifier` 的声明类型**严格一致**」；§6.7.2.6：「**不可取 `memblock` 或其元素的地址**，无 `&` 语义」。
**实测**：`:3: error [TypeMismatch]: ptr_address pointee type does not match variable type`，exit 1。照抄文档示例必然失败。

#### C-17（P1）CLI 参考 §2.2 的 `-e` 示例与实际输出不符

CLI 参考 §2.2（L86）：「`-e` …诊断首行附错误码名（**如 `error [TC_CE_SYNTAX]`**）」。
**实测**：`-e` 打印的是 `TcErrorKind` 的**打印名**——`error [SyntaxError]` / `[UndefinedVariable]` / `[DivisionByZero]` / `[FormatSpecifierError]`，从不打印 `TC_CE_*`。
同一文档 §6.1（L220）又说「`-e/--print-error-code` 使首行附码名」并给出打印名表——**文档内部两处不一致**。

#### C-18（P1）CLI 参考 §6「完整映射」表缺 `ExtraArgument`

CLI 参考 §6.1（L220）声明该表是「0.0.44 的 `TcErrorKind` **完整映射**」；§6.2–§6.6 共 87 行，`grep -i extra` 无命中。
附录 B.12 有 `TC_CE_EXTRA_ARGUMENT`；**实测** `error [ExtraArgument]`。同页 L464/L475 还断言「完整码表已落地」。

#### C-19（P1）libtc 设计说明书 §15.3 自相矛盾，且 §7.3 的跨入口承诺与实现不符

| 位置 | 原文 |
| ---- | ---- |
| libtc §15.3（L613） | 「无路径的内存源**仅做结构检查、不解析 import**」 |
| libtc §15.3（L617） | 「对完整 NUL 结尾 source 执行 13 阶段确定性编译管线：……→ 模块结构与导入解析（**4a→4b→4c→4d**）→……」 |
| libtc §7.3（L330） | 「同一源程序经由**不同入口编译应产生相同诊断**」 |

实测见 B-13：内存入口与文件入口接受集不同，§7.3 的承诺不成立，§15.3 两处互相否定。
**另**：该节把「不解析 import」当作设计说明，但 §4.5 是**强制**规则——文档不应把违约行为描述为正常形态。

#### C-20（P1）TC-Embed 详设 §12/§15.8 的 4 个 TC 示例是 **C 语法**，不是 TC

| 位置 | 原文 |
| ---- | ---- |
| Embed 详设 §12.1（L787-789） | `public func add(a: int32, b: int32) -> int32 {` / `return a + b` / `}` |
| Embed 详设 §12.2（L850/853/854） | `-> int32 {` / `while (i < len) {` / `total = total + ptr_load(data + i)` |
| Embed 详设 §12.3（L910/912/913） | `static var count: int32 = 0`（缺 `public`）/ `-> int32 {` / `count = count + 1` |
| Embed 详设 §15.8.1（L1719/1725/1726） | 同形，含 `ptr_store(data + i, ptr_load(data + i) * factor)` |

与标准的冲突点：附录 A.3 的 `function_definition` 用 `then … end`（无 `->`、无花括号）；§7.2.1 的 `while` 用 `then`/`end`；§6.1.2 禁止 `funcall`/运算作操作数、§3.10.1 禁止 `ptr` 参与通用算术（`data + i` 非法，须 `ptr_add`）；§2.7 中 `add` 是保留字，不能作函数名；附录 B.2 要求 `#lib` 的 `static` 带 `public`/`private`。
**实测**：`tc-aot` 对 `func add(...)` 报 `expected function name`。

#### C-21（P1）TC-Embed 详设 §12.2/§15.8.3 示例把数据写进**指针形参自身的槽位**

Embed 详设 §12.2（L875-884）：`int data_slot = info->param_slots[0];` 之后把 5 个元素写进 `data_slot + i`，再用 `tc_embed_ptr_encode(data_slot)` 作实参。
同文档 §7.1（L525-529）自己规定：调用时「将实参按顺序写入对应形参的 slot：`ctx->exec_ctx.slots[info->param_slots[i]] = args[i];`」——于是 `args[0]`/`args[1]` 会把刚写的数据覆盖掉。
**结论**：文档标注的输出（`sum = 15`、`scaled: 2 4 6 8 10`）按该写法**不可达**。应改用 §16.4 的临时区（`tc_embed_tmp_begin`/`tc_embed_make_ptr`）或与符号槽无重叠的数据区。
（与实现缺陷 B-19 同源。）

### 3.2 设计文档 P2 清单

| 文档 | 位置 | 问题 |
| ---- | ---- | ---- |
| 编译器标准 | §1.4 L215 / L217 | 内部引用 `§15.5`、`§6.1.2` 悬空（本文无 §15.x；§6.1 下无 6.1.2） |
| 编译器标准 | §1.4 L195 | 「**§9.1** 登记的实现侧待同步项……全部关闭」——§9.1 是「作用域」，同步表是 §1.4 自身；且既有未关闭项仍有 9 项（见本报告 §5） |
| 编译器标准 | L1177 | 「分析结果是 **0.0.42** 的强制语义」版本残留 |
| 编译器标准 | §1.1.1 L43 | 运算行缺 `ptr_eq`/`ptr_ne`（标准 §1.1.1 同表含） |
| 编译器标准 | §3.7 L426-431 | 字面量源类型表缺 `cast`×布尔 与 `bitcast`×`inf`/`nan`（标准 §6.6.1.1 有），照字面实现会把合法的 `bitcast(float32, inf)` 误报 `BITCAST_WIDTH` |
| 编译器标准 | §11.4.1 L1438 | `TC_CE_BITCAST_WIDTH` 触发条件缺「或源类型字面量不匹配」（附录 B.4 有） |
| 编译器标准 | §5.1 L590 | 「函数签名收集在第 5 阶段」与 §1.2（4d 收集签名）冲突 |
| 编译器标准 | §1.2 L103 | 阶段 9 求值 `static let` 与 §4.3（6b 起）冲突 |
| 编译器标准 | §8.1.1 L995 | `ptr<T>` 形参合法实参只列「同型标识符或 `nullptr`」，收窄了 §6.1.2（限定名/字段读取亦合法） |
| 编译器标准 | §8.2 L1018 | 第 2 条把三类「调用位置」并列却不给出各自错误码；第 3 条的顺序清单漏了「实参个数超限」一项 |
| VM 详设 | §1.5 L93/L94 | 表格列数损坏：D35 行缺两格、状态错位到下一行 |
| VM 详设 | §1.5 L97 | 内部引用 `§5.9` 不存在（本文 §5 只有 5.1–5.8） |
| VM 详设 | L270 / L1007 | `§3.9.3`/`§3.5`/`§10.4` 未加「[语言标准 …]」限定，按本文编号指向无关章节 |
| VM 详设 | L162 / §13.4 | 「函数签名收集后、函数体分析前完成 `static` 初始化」与本文 §2.1 阶段 9 冲突 |
| VM 详设 | §1.5 L87 / L1143 | 把 `bitcast(ptr<T>, nullptr)` 列为「正例」，但标准未定义该形态（见 A-4） |
| VM 详设 | L136 | 「阶段 13: VM 执行 / AOT 代码生成」——执行不属 13 阶段（编译器标准 §1.2 为「VM / AOT 代码生成」） |
| VM 详设 | §15.4 Parser 行 | 与 C-14 同源的阶段归属 |
| CLI 参考 | L71 | `tc-vm [options] [<file.tc>]` 方括号错误（实测文件是必需参数） |
| CLI 参考 | L316 | `FunctionCallResultTypeError` 条件写成「funcall 接收变量类型不匹配」（应为「`void` 函数用于 `var` 初始化/赋值」，见 C-1） |
| CLI 参考 | L249 | 静态 `MemcopyUnsafeInvalidRange` 条件含「或下标」（见 A-3/C-5） |
| CLI 参考 | L358-361 | bench 行名 `bench module resolve` / `bench analyze` 与实测 `bench analyze+modules` 不符 |
| CLI 参考 | §9.3 出现在 L477 与 L488 | 章节号重复 |
| CLI 参考 | L85 | `-I` 未注明 64 上限（见 B-20） |
| CLI 参考 | L485 | 「41+1 → 87」等历史列已过时（当前口径 86+1） |
| AOT 详设 | §4.3 L260/L320 vs §11.2 L670 | 同名 `tc_aot_memblock_copy` 两个不兼容原型（整块深拷贝 vs 9 参区间拷贝） |
| AOT 详设 | §1.4 L84/L85 | 表格结构损坏（D35 行缺格、状态错位） |
| AOT 详设 | §1.4 L76/L80 | 内部引用 `§12`/`§9.3` 指向不覆盖该主题的条款 |
| AOT 详设 | L33 | 目录锚点 `#19-已知可移植性债务不在-0041-强制范围` 与标题 `（0.0.42 已清零）` 不符（死锚 + 0.0.41 残留） |
| AOT 详设 | §18 L875/L886/L900 | 整章止于 v0.0.42，与 §1.1 声明的 v0.0.43 实现基线漂移 |
| AOT 详设 | §16.2 → §16.4 | 缺 §16.3（跳号） |
| AOT 详设 | §1.4 L71 表 | 11 行全标「已同步」，未反映既有 9 项未关闭（见本报告 §5）（与 VM/libtc/C-22 同源） |
| AOT 详设 | L218-219 | 「正确性依赖上游『确定初始化』静态保证（语言标准 **§1.3**）」——§1.3 无该内容，应指 §9.2 |
| AOT 详设 | L194-204 | 骨架使用未定义的 `STRUCT_TOTAL_BYTES` / `NUM_MEMBLOCK_ALLOCS` |
| AOT 详设 | §15.1 | 未列实现已提供的 `--embed` |
| libtc 详设 | §7.2 L326 | 「合计实现枚举 **86**」（应为 87） |
| libtc 详设 | §15.11 L860 | 「**85** 语言错误码 + OutOfMemory」（应为 86） |
| libtc 详设 | §11.1 L425 / §15.9 | bench 行名 `module resolve` 不存在（同 CLI 参考） |
| libtc 详设 | §11.3 L439 / §15.4 L648 | 声称「不使用可变全局状态、可并发编译」，但 `src/vm/analyzer/tc_analyzer_pass2.c:202-207` 有全局 `g_name_scope_members` / `g_name_scope_in_function` |
| libtc 详设 | §3.1 L146 | 「阶段 13: **VM 执行** / AOT 代码生成」（同 VM 详设 L136） |
| libtc 详设 | §7.3 L330 / §14.3 L545 | 「三个原则」/「四步选取顺序（LT→SYN→SEM→CT）」均未列全标准 §11 的四条 |
| libtc 详设 | §14.3 L545/L548 | 自引 `§5`/`§8.1` 指向无关章节；L554 引用不存在的《语言标准符合性检查分析报告》M-15/P5 |
| libtc 详设 | §14.3 / §15.11 | 状态表全「已同步/支持」，未反映 既有-1～既有-9 |
| Embed 详设 | §15.12 L1931/L1933 | 「`tc_embed_create_aot` 不依赖 `TcTypedProgram`」与 §15.5.2/§15.4.1 冲突 |
| Embed 详设 | §15.1.4/§15.1.2/§15.1.3/§15.2/§15.9.1 | `tc_ret_N` / `tc_func_%d` 与同文档及实测的 `tc_aot_ret_N` / `tc_aot_func_N` 不一致 |
| Embed 详设 | §15.1 L1079/L1190/L1025 | 「真实输出」行号/函数归属全部失锚（如 `tc_aot_emit_function` 实于 `tc_aot_emit_func.c:34`） |
| Embed 详设 | §15.3.1 | 「生成 `.c` 骨架」列出的 `tc_aot_initialized`、`tc_aot_init_slots`、`TC_AOT_MEMBLOCK_SLOT_COUNT`、`tc_aot_emit_static_init_rhs`、`tc_aot_func_table.inc` 等在实测产物中均不存在 |
| Embed 详设 | §10.3 L734 | 称静态错误由 `tc_embed_create` 报告（实为编译阶段） |
| Embed 详设 | L96 | 「（**§13**）」指错（本文 §13 是实施路径；应为 §10） |
| Embed 详设 | §10.2 L730 | 消息格式 `"<domain>: <kind>: <message>"` 与实测 `file:line:col: error [Kind]: message` 不符 |
| Embed 详设 | §15.3.2 L1351 | `TC_DIAG_OK` 不存在（应为 `diag->domain != TC_DIAG_NONE`） |
| Embed 详设 | §2.1 L117 / L115 | 引用标准 §3.5 时删去「未经 `bitcast` 时」限定；把 `(slot<<1)\|1` 说成「公开的、确定的算法」而非本实现的实现定义选择 |
| Embed 详设 | §3.2 L210-215 / §13.1 L938 等 | 「引入版本 v0.0.42」与仓库历史不符（文件自 v0.0.37 起存在） |
| Embed 详设 | §13.1 L948 / §15.7.1 L1624 | 声称新增 `tc_embed` / `tc_embed_aot` CMake 目标（实为编入 `libtc`） |
| Embed 详设 | §15.6.1 L1586 | 宿主编译命令列出不存在的 `tc_aot_embed.c`，且未链接 libtc |
| Embed 详设 | §5.2 L382 | 「同一时刻只有一个函数调用帧活跃」与本文 §7.3 L562、标准 §9.1 冲突 |
| Embed 详设 | §16.7 L2061 | 示例向 2 参 `sum` 传 1 个实参，与 §16.6 的 arity 检查冲突 |
| Embed 详设 | §14.1 L991-992 | 两个测试名 `test_embed_ptr_array_sum` / `test_embed_ptr_store_readback` 不存在 |
| Embed 详设 | §3.2 / §3.3 | 文件表缺 `tc_embed_internal.h`；头文件骨架缺 `tc_embed_slot_count` |
| Embed 详设 | §5.4 L413 | `find_free_slot_block` 不是已声明 API（应为 `tc_embed_tmp_begin`） |

### 3.3 跨文档一致性

* **C-22**：AOT / VM / libtc 三份文档的「实现同步状态」表均未反映既有 9 项未关闭（见本报告 §5）（AOT §1.4、VM §1.5、libtc §14.3/§15.11），读者会得到「零未决」的印象，而各文档后续章节又把这些行为当作已落地的前提来写。
* **C-23**：`FUNCALL_POSITION` / `FUNCALL_RESULT_TYPE` 的错误定义同时出现在编译器标准 §8.5/§11.4.2 与 CLI 参考 §6.6（C-1 + C-18），属文档集级口径问题，宜一次性裁决。
* **C-24**：`MEMCOPY_UNSAFE_INVALID_RANGE` 的静态/运行时分工在**标准自身**（A-3）、编译器标准 §3.2/§6.7/§11.4.6、CLI 参考 §6、VM 详设 §15.2 之间存在四种写法，建议随 A-3 一并统一。

---

## 4. 已核实**一致**的区域（抽样）

以下区域经逐条比对/实测未发现分歧，可作为回归基线。

**标准侧事实源与门禁**
- 附录 B 86 码（74 `TC_CE_*` + 12 `TC_RE_*`）↔ `TcErrorKind` 枚举 87（+`TC_ERR_OUT_OF_MEMORY`）**双向一致**；编译器标准 §11.4 的分表 44/18/4/8/10/2 与附录 B.1–B.13 分组一一对应。
- 86 个码在 `tc_error_kind_name` 中**各有唯一打印名**，与编译器标准 §11.4 的打印名列 **100% 一致**（无重名、无缺项）。
- §2.7 关键字集 ↔ 词法器关键字表 **双向完全一致**（86 = 86）；`padding` / `count` / `fill` 正确地**不是**关键字。
- §3.2/§3.3/§3.4 类型宽度表 ↔ `tc_type_bit_width`；`bool` 固定 8 位、`isize`/`usize` = 平台字长、`ptr` 等宽。
- §10.4 的 13 个转换符 ↔ `tc_format_spec_parse`；§10.5 的标志适用矩阵、`%0008d ≡ %08d` 拆分规则、宽度/精度 65535 上界、`%0.3f ≡ %.3f`、`%#.0o` 输出 `0` 等**逐条实测正确**。
- §10.4 的确定性输出规则实测全部正确：`%f`/`%e` 6 位、`%g` 6 位有效数字、`-0.000000`/`-0` 保号、`%x`/`%X`/`%o`/`%b` 对 `int8 -1` 输出 `ff`/`FF`/`377`/`11111111`、`%08.3f` → `0003.142`。
- 标准 §11 的**阶段优先**（LT 先于 SYN、SYN 先于 SEM、SEM 先于 CT）实测正确；CT 类诊断挂起至全部 SEM 无触发实测正确。
- 标准内部与各文档指向标准的 `§` 交叉引用**全部指向存在的条款**；指向编译器标准的引用亦全部存在。

**实现侧（抽样 60+ 例）**
- §2.1 词法拒绝：非法 UTF-8、BOM、U+0000、注释外非 ASCII、非法空白、前导零、`-nan`、`-42u`、字面量上限/浮点舍入为零或无穷 —— 全部按标准报码。
- 结构类语法阶段 4 码（`MODULE_LAYER` / `MISSING_VISIBILITY` / `PROGRAM_MODE_MISUSE` / `VAR_MISSING_INIT`，`#program` 路径）+ `MISSING_END` / `OPERAND_COUNT` 全部命中。
- §3.9 结构体 8 码、§3.10 指针静态规则、§6.6 `cast`/`bitcast`/`truncate` 边界（含指针重标记、`bitcast(ptr↔浮点)` 拒绝、`bool` 排除）、§6.7 memblock 静态约束、§8 函数/调用/return/递归、§9 作用域与确定初始化（含 goto 绕过）、§7 控制流与 goto 作用域 —— 抽样全部正确。
- CLI：5 组选项、退出码仅 0/1、help→stderr、`--version`→stdout、`--check` 静默不执行、无 `--bench`（走 `TC_BENCH=1`）、`writeln` 仅写 LF —— 与 CLI 参考 §2/§5 一致。
- 全部 **616** 个语料文件（`tests/errors/**` 361 + `tests/valid/**` + `tests/stress/**` 共 255）在 VM 与 AOT 两侧的静态判定**完全一致**（接受/拒绝一致，且拒绝时的诊断消息文本逐字相同）——本审计独立重跑确认，`diffs=0`。
  *（说明：`tc-aot` **没有** `--print-error-code` 选项——它的 `-e` 是 `--embed`——故 AOT 侧无法直接比对错误码名，只能比对消息文本；这也是 C-17 之外的另一个 CLI 面差异。）*

**执行器/运行期语义（抽样，来自并行分审计的独立复核）**
- 整数：strict/wrap 溢出与模 `2^n` 回绕、`div`/`mod` 除零、`div(INT_MIN,-1)`、`mod(INT_MIN,-1)=0`、`abs`/`neg(INT_MIN)`、无符号 `abs`/`neg`。
- 移位：负移位计数（strict 与 wrap、`shl`/`shr`，且**优先于**溢出码）、`shr k≥n`→0、`shl(wrap) k≥n`→0、strict `shl` **非零值**的溢出与边界合法值（仅零值路径有 B-45）。
- 模式矩阵：`ieee`/`wrap`/`truncate` 误用 → `TC_CE_MODE_MISMATCH` 或 `TC_CE_SYNTAX`，与附录 A 产生式边界一致。
- 浮点：canonical quiet NaN 位模式、strict/ieee 异常分类与优先级、**tininess-after-rounding** 下溢（精确次正规不报错）、`abs`/`neg` 保留 NaN payload、浮点 `mod` 与 `fmod` 逐位一致（含符号、`-0.0`、`±inf` 除数）、NaN 比较。
- 转换：`cast` 范围/截断/RNE/`nan→bool`、`truncate m≥n` 静态拒绝、`bitcast` 等宽与 `bool`/`ptr` 规则、`ptr↔int` 往返。
- memblock：越界（常量→静态、变量→运行期）、重叠 copy 的临时缓冲语义、`.count`、`bool` 元素规范化。
- 指针：各空指针码、`ptr_eq(null,null)=true`、`ptr_size(nullptr)`、`ptr_address` 静态规则、`memcopy_unsafe` 空指针优先于负区间。
- I/O：`%d/%i/%u/%x/%X/%o/%b/%t` 全宽度与全部标志/宽度/精度/`#`/`0` 组合（含 `inf`/`nan` 的 `0` 标志按空格、`-0.0`、`%g` 形态切换）；**132 组浮点格式位级对照仅 B-46 一项不符**；65535 宽度/精度在 ASan 下无越界。
- `read`：完整 Token 匹配/范围/EOF 语义、float64 次正规输入接受、`read(float32)` 直接舍入正确、`TC_RE_IO` 映射。
- `bool` 规范化、`isize`/`usize`/`ptr` = 64 位；12 个 `TC_RE_*` 全部有产出点。
- `tests/**` 全量 **UBSan 扫描无 UB**（除 B-49 的定向构造），`scripts/run_tests.sh` 全绿。

---

## 5. 九项既有未关闭项复核


全部 **9 项独立复现确认成立**。复核证据：

| 编号 | 复现命令/程序 | 实测（VM） | AOT |
| ---- | ------------- | ---------- | --- |
| 既有-1 | `#program / import ImpLib / var v: int32 = ImpLib.K` | `UndefinedVariable: undefined variable 'ImpLib'` | 同 |
| 既有-2 | `ptr_address(int32, Self.W)` / `ptr_address(int32, QLib.W)` | `SyntaxError: expected identifier` / `unexpected token` | 同 |
| 既有-3 | `funcall(L36.g, a: 1, a: 2)` | `ExtraArgument`（应 `DuplicateArgument`） | 同 |
| 既有-4 | `let p: ptr<int32> = nullptr` + `ptr_store(int32, p, 5)` | `ConstantAssignmentError` | 同 |
| 既有-5 | `memcopy_unsafe(int32, s.p, 0, s.p, 0, 1)` | `implementation error: … unresolved field operand` | `null pointer dereference`（**分歧**，见 B-8） |
| 既有-6 | `#lib / public static var V: int32` | `SyntaxError`（应 `VarMissingInitializer`，见 B-17） | 同 |
| 既有-7 | 顶层 `let I: int32 = 0` 作 `memblock_copy` 下标 | `implementation error: … unresolved binding metadata` | `1`（**分歧**，见 B-9） |
| 既有-8 | `var i: int32 = 1` 作 `memblock_load` 下标 | `TypeMismatch: operand type does not match operation type` | 同 |
| 既有-9 | `memcopy_unsafe(…, -1)` 常量负长度，`-c` | **rc=0 静态通过**；运行时报 `TC_RE_*`（`TC_CE_MEMCOPY_UNSAFE_INVALID_RANGE` 无产出点） | 同 |

补充：**既有-3** 的根因（个数检查抢在名称检查之前）**范围大于原登记描述**，同时覆盖「重复 + 未知 + 顺序」，见 B-6。

---

## 附录 R：复现材料

### R-1 libtc 内存入口探针

```c
#include <stdio.h>
#include <string.h>
#include "tc_lib.h"
static void try(const char *label, const char *src) {
    TcTypedProgram prog; TcDiagnostic diag;
    memset(&prog, 0, sizeof prog); memset(&diag, 0, sizeof diag);
    int rc = tc_compile_source(src, "mem.tc", &prog, &diag);
    printf("%-24s rc=%d  code=%s\n", label, rc,
           rc == 0 ? "-" : tc_error_kind_name(diag.kind));
    if (rc == 0) tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
}
int main(void) {
    try("import missing module", "#program\nimport NoSuchModule\nvar x: int32 = 1\n");
    try("import missing w/ use", "#program\nimport NoSuchModule\nvar x: int32 = NoSuchModule.K\n");
    try("self import",           "#lib\nimport SelfLib\npublic func f() void then\n    return\nend\n");
    try("plain ok",              "#program\nvar x: int32 = 1\nwriteln(int32, x)\n");
    return 0;
}
```
编译（沿用 `docs/libtc设计说明书-0.0.44.md` §15.6 的头文件集合）：
```sh
cc -std=c99 -I src/libtc -I src/vm/runtime -I src/vm/analyzer -I src/vm/parser \
   -I src/vm/executor -I src/vm/driver -I src/vm/lexer probe.c \
   build/src/libtc/libtc.a -o probe && ./probe
```
实测输出：
```text
import missing module    rc=0  code=-
import missing w/ use    rc=-1 code=UndefinedVariable
self import              rc=0  code=-
plain ok                 rc=0  code=-
```

### R-2 VM/AOT 分歧与诊断定序复现

见 §2.1 B-2 与 §2.2 B-7～B-11 内的完整程序；VM 用 `build/vm/bin/tc-vm -e F.tc`，AOT 用 `build/aot/bin/tc-aot -r $(pwd)/F.tc`（**须绝对路径**，见 B-15）。

---

*— 报告结束 —*
