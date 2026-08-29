# TC 0.0.41 语言标准符合性检查分析报告

> **规范基线（唯一权威）**：[TC 语言标准 0.0.41](./TC语言标准设计说明书-0.0.41.md)（§1–§11 + 附录 A/B，3371 行）
> **审查对象**：`docs/` 下全部设计文档（编译器标准、VM 详设、AOT 详设、Embed 详设、libtc 设计与 API、VM 命令行参考、开发计划、修复计划、既有合规审查报告）+ `src/` 全部实现（lexer/parser/analyzer/executor/runtime/AOT/libtc/embed）
> **审查方法**：以语言标准为唯一基线，逐章核对全部设计文档与实现，并以构建产物（`build/vm/bin/tc-vm` / `build/aot/bin/tc-aot`）实测 40+ 边界用例、脚本 diff 错误码与关键字集交叉验证
> **审查日期**：2026-08-27
> **本报告性质**：检查分析结论，不修改任何被审文件。

> **后续变更注记（2026-08-29）**：本报告审查范围内的下列文档已删除/合并——`设计实现合规审查报告-0.0.41.md`（已删除）、`TC-0.0.41-结构体字段operand修复计划.md`（已删除，其修复已落地）、`libtc-api-0.0.41.md`（已并入 `libtc设计说明书-0.0.41.md` §15）、`TC-0.0.41-开发计划.md`（已删除，属老版本开发计划，历史见发布版本）。§6 表格保留原审计结论并标注现状。
>
> **收口注记（2026-08-30）**：§1–§13 为 2026-08-27 审计原文，发现项与「完全遵循不成立」均相对当时实现，**不改写**。配套[修复计划](./TC-0.0.41-语言标准符合性修复计划.md) P0–P6 已落地。现行对外结论见文末 **§14**，勿把 §1 当作现状。§9 亮点中的 `examples/demo`、`examples/composite` 为当时抽查路径；**`examples/` 已移出版本库**，演示入口改用 `tests/valid/`。

---

## 1. 结论摘要

**总体结论：实现与设计文档主体遵循语言标准，但「完全遵循」不成立。** 核心语言语义（整数 strict/wrap、浮点 strict/ieee 与异常优先级、移位规则、bool 规范字节、memblock/struct 布局、指针语义、短路、确定初始化、模块系统）实现质量高、抽查全部正确；偏差集中在三类系统性问题：

1. **诊断码体系与标准脱节**：语言标准定义 **71 个诊断码**（59 `TC_CE_*` + 12 `TC_RE_*`），实现枚举 **91 个**（78 CE + 12 RE + 1 OOM），其中 **19 个 `TC_CE_*` 码在语言标准中不存在**；其中至少 **7 个**与标准既有规定**直接冲突**（如函数重名应报 `TC_CE_FUNCTION_NAME_CONFLICT`，实现报 `TC_CE_DUPLICATE_FUNCTION`；缺 `end` 应报 `TC_CE_SYNTAX`，实现报 `TC_CE_MISSING_END`）。由于 §1.3 把"首个规范诊断及其错误码"列为可观察行为，这是规范性偏差。根因有二：实现/编译器标准单方面扩码 + 语言标准附录 B 自身不完备（多处"静态错误"未给码）。
2. **接受集与行为偏差**：至少 **7 项**实测确认的实现偏差——`padding` 被误保留为关键字、浮点 `1.` 被误接受、`ptr_size` 与 memblock 常量构造器在 `let` 中被误拒绝、形参经指针写入被静默放行、`%-8t`（标准自身示例）被拒绝、浮点 `mod` 整节未实现、格式化 flags/width/precision 运行期完全丢弃。词法/语法层另有 **14 处 S 级偏差**（错误码替换 + 接受集偏差：顶层 var/语句交错放行、`Self.x`/`m.x` 限定名在 read/memblock 名/计数位置被拒、funcall 实参与 struct 构造器字段值按完整 RHS 超收、比较运算接受 `ieee` 模式、`-42u`/`1.5u` 报错码错误等）。
3. **AOT 三处严重缺陷**：`Self.<memblock/struct>` 浅拷贝导致 AOT 与 VM 语义分歧、embed 模式生成违反 C99 约束的代码、`memblock_copy` 边界检查无符号回绕可被合法 TC 程序绕过（宿主堆越界，违反零 UB）。

**设计文档总体可信**：VM 详设一致度高、编译器标准/AOT/Embed 详设一致度中，libtc 设计说明书存在 API 声明自相矛盾；所有文档共约 30 处实质缺陷，多数为错误码口径与引用错误。既有《设计实现合规审查报告》的"91 错误码"与若干错误码/章节引用已与现行语言标准脱节。

**语言标准自身缺陷 9 处**（`neq` 笔误 ×5、§6.3.1 与附录 A 的 shift-mode 矛盾、附录 B 不完备、B.12 混入静态码、§3.5 与 §6.6.6 的指针位模式矛盾等），不归咎实现，但影响一致性判定，需先行修订。

统计总览：

| 级别 | 数量 | 含义 |
| ---- | ---- | ---- |
| **S（严重）** | **30** | 可观察行为/接受集/内存安全偏差（16 项语义/AOT/Embed + 14 项词法语法层） |
| **M（重要）** | **27** | 错误码冲突、检查顺序反转、32 位目标布局、文档规则冲突等（编号 M-1～M-27：§4.2 的 12 + §4.3 的 12 + §3.6 的 3） |
| **N（次要）** | **~25** | 章节引用错误、计数错误、覆盖缺口、可移植性风险 |
| **I（信息）** | **~15** | 覆盖确认、软偏差、防御性建议 |
| 标准自身缺陷 | **9** | 需修订语言标准（见 §8） |
| 实证通过项 | **50+** | 抽查符合标准的用例（见 §9） |

---

## 2. 审查口径

- **规范权威**：`docs/TC语言标准设计说明书-0.0.41.md`。正文决定静态与执行语义，附录 A 决定语法接受集，附录 B 为错误码速查（§1.3）。
- **可观察行为**（§1.3 行 61）：是否通过静态检查、**首个规范诊断及其错误码**、按源序提交到标准输出的字节、正常结束或首个运行时错误及错误码。错误码不一致即偏差。
- **语法拒绝 vs 静态语义拒绝**：不符合附录 A → 语法阶段拒绝（默认 `TC_CE_SYNTAX`），不得进入语义检查。
- **零 UB / as-if**：实现不得泄漏宿主未定义行为；§11 fail-fast 提交规则。
- **严重度**：S = 接受/拒绝合法程序、错误码冲突、运行时值/错误错误、宿主 UB/内存安全；M = 检查顺序、32 位目标、文档与标准规则冲突、死代码诊断码；N/I = 引用、计数、覆盖缺口、建议。

---

## 3. S 级发现（严重：可观察行为偏差，全部有实测或源码行号证据）

### 3.1 接受集偏差（拒绝合法程序）

**[S-1] `padding` 被实现保留为关键字，与标准 §2.7 直接矛盾**
- 标准：§2.7（行 264）明确"`padding` **不是**保留关键字；它仅作为 `@padding(N)` 属性名出现"。
- 实现：`src/vm/lexer/tc_lexer.c:609` 将 `padding` 识别为关键字；实测 `var padding: int32 = 3` 报 "expected identifier" 拒绝。
- 判定：拒绝合法程序。修复：从关键字表移除 `padding`。

**[S-2] `ptr_size` 在 `let` 常量上下文中被拒绝**
- 标准：§5.2.1（行 1049）允许 `ptr_size(p)`（`p` 可为 `nullptr` 或更早的 `let ptr<T>`）；§6.8.8（行 1775）"`ptr_size` 是合法的 `const_operand`"；附录 A.3 `const_rhs`（行 2883）包含 `ptr_size_expr`。
- 实现：实测 `let w: usize = ptr_size(int32, nullptr)` 与 `ptr_size(int32, p)`（`p` 为 `let ptr<int32>`）均报 "expected constant expression"。const 解析路径未分发 `TC_TOK_PTR_SIZE`（运行时 RHS 路径正常，实测输出 32）。
- 判定：拒绝合法程序。注：编译器标准详设行 297 亦要求"可在 let / static let 中使用"，属实现违背两文档。

**[S-3] `let` 的 memblock 常量构造器被拒绝**
- 标准：§5.2.1（行 1047）允许 `memblock(T, count: N, fill: v)` / 逐值形式作 `const_rhs`（全常量操作数）；附录 A.3 有 `const_memblock_constructor`（行 3112-3125）；§6.7.1（行 1587）允许 `let` memblock。
- 实现：实测 `let m: memblock<int32, 3> = memblock(int32, count: 3, fill: 0)` 报 "expected constant expression"（const_eval 未实现 const memblock 构造，与修复计划文档行 292/351 的自述吻合——但该文档把实现缺口误写成语言事实，见 [M-16]）。
- 判定：拒绝合法程序。

**[S-4] `write(bool, %-8t, flag)` 被拒绝（标准自身示例被实现拒绝）**
- 标准：§10.2（行 2178）官方语法示例即 `write(bool, %-8t, flag)`；§10.5 允许标志表 %t 行允许 `-`；宽度仅受 1–65535 范围限制，无适用性排除。
- 实现：实测 `%-8t`、`%-4t` 报 "%%t does not support flags, width, or precision"（`src/vm/analyzer/tc_analyze_6e.c:60-68` 对 %t 拒绝一切 flag/width）。
- 判定：拒绝合法程序（标准自身的示例用例）。

### 3.2 接受集偏差（接受非法程序）

**[S-5] 浮点字面量 `1.` 被接受**
- 标准：附录 A.2 `dec_float_literal = digit, {digit}, ".", digit, {digit}`（行 2429）要求小数点后至少一位数字；§1.3 语法拒绝口径。
- 实现：实测 `var x: float64 = 1.` 通过检查且值为 1.0（`.5` 正确拒绝，说明是小点后无数字的漏检）。
- 判定：接受非法程序。

**[S-6] 形参经 `ptr_address` 后 `ptr_store` 被静默放行**
- 标准：§6.8.3（行 1712）"所指为 `let`/`static let`/**形参**时报 `TC_CE_CONSTANT_ASSIGNMENT`"；§3.10.9（行 889）"对形参做 `ptr_store` 非法"。
- 实现：实测 `func f(p: int32)` 内 `ptr_address(int32, p)` 后 `ptr_store(int32, pp, 5)` **通过静态检查并执行**（无任何诊断）。
- 三方不一致：语言标准要求 `TC_CE_CONSTANT_ASSIGNMENT`；编译器标准 §3.2（行 294）要求 `TC_CE_PARAMETER_ASSIGNMENT`；实现静默接受。三份各自不同，属双重偏离。
- 判定：接受非法程序。

**[S-7] `read` 浮点输入接受集过宽（接受 `1.` 与 `.5`）**
- 标准：§10.3（行 2254）"小数须在 `.` 前后均有数字（`1.` 和 `.5` 非法）"。
- 实现：`src/vm/runtime/tc_io.c:559-597` 仅在两侧都无数字时拒绝；实测 `read(float64, x)` 输入 `1.` → 1、`.5` → 0.5。
- 判定：接受非法输入（运行时行为偏差）。

### 3.3 错误码冲突（首个诊断码是可观察行为）

**[S-8] 函数名重复：实现报 `TC_CE_DUPLICATE_FUNCTION`，标准规定 `TC_CE_FUNCTION_NAME_CONFLICT`**
- 标准：§8.1.2（行 1990）与附录 B.3（行 3270）"同一作用域内**函数名重复** → `TC_CE_FUNCTION_NAME_CONFLICT`"（标准完备，无歧义）。
- 实现：`src/vm/analyzer/tc_func_check.c:852-861` 对同名 func 报 `TC_CE_DUPLICATE_FUNCTION`（`FUNCTION_NAME_CONFLICT` 仅用于函数 vs 值绑定）。
- 判定：实现偏离。编译器标准 §11.4.2 与 VM 详设 §15.2 亦沿用该切分，三处需同步。

**[S-9] 形参名重复：实现报语义码 `TC_CE_DUPLICATE_PARAMETER`，标准规定"语法拒绝"**
- 标准：§8.1.2（行 1991）"形参名唯一 … 违反错误码 = **语法拒绝**"（§1.3 口径即 `TC_CE_SYNTAX`，且语法拒绝不得进入语义检查）。
- 实现：`src/vm/analyzer/tc_func_check.c:115-124`、`tc_analyzer_pass1.c:153` 在语义阶段报 `TC_CE_DUPLICATE_PARAMETER`（实测 "duplicate parameter 'a'"）。
- 判定：阶段 + 码双偏离。（注：标准措辞本身有张力——EBNF 无法表达"名不重复"，建议标准改为显式静态语义码，见 §8-STD-4 修复方案。）

**[S-10] 跨函数标签 `goto`：实现报 `TC_CE_CROSS_CONTROL_FLOW_JUMP`，标准规定 `TC_CE_LABEL_NOT_FOUND`**
- 标准：§7.3.2（行 1935）诊断表步骤 6 "全程序无匹配 → `TC_CE_LABEL_NOT_FOUND`"；附录 B.9 仅有该码。
- 实现：实测 `goto other`（标签在另一函数）报 "cannot jump to label in another function"（`tc_analyzer_pass2.c:420` 附近使用 `TC_CE_CROSS_CONTROL_FLOW_JUMP`）。
- 判定：实现偏离；编译器标准 §11.4.2（行 1432）为该拆分码的源头，需一并修订。

### 3.4 语义未实现 / 运行期丢弃

**[S-11] 浮点 `mod` 完全未实现（§6.3.7 整节缺失）**
- 标准：§6.3.1 矩阵（行 1192）浮点 `mod` 支持 strict/ieee；§6.3.7（行 1295-1324）完整定义向零截断精确余数、-0.0 符号、`a` 有限 `b=∞` 返回 `a`、`0 mod 0` → invalid、有限/零 → div0、ieee 返回 canonical NaN。
- 实现：`src/vm/runtime/tc_semantics.c:150-154` 对 `TC_MOD` 直接报 `TC_CE_TYPE_MISMATCH` "mod not supported for float types"；`tc_sem_fp.c` 无任何实现。实测 `mod(float64, 5.5, 2.0)` 被编译期拒绝。VM/AOT/const_eval 共享核同缺。
- 判定：拒绝合法程序（整节规范缺失）。

**[S-12] 格式化输出的 flags/width/precision 运行期完全丢弃（§10.5 全部控制项不生效）**
- 标准：§10.4/§10.5（行 2263-2335）定义确定性拆分、宽度/精度、标志表、零填充、对齐；示例 `%08.3f` 须输出 `0003.142`。
- 实现：`src/vm/executor/tc_executor.c:203` 只把 `fmt.spec` 传给 `tc_io_write_value`，`TcFormatFullSpec` 的 flag/width/precision 已解析（`tc_types.c:626-710`）但全链路从不消费（`tc_io.c:186-390`）。实测 `%08.3f` → `3.141593`、`%-8f` → `3.141593`。
- 判定：可观察输出字节偏差，且与 [S-4] 的编译期拒绝共同构成"格式控制半实现"状态。

### 3.5 AOT 与内存安全

**[S-13] AOT `Self.<memblock/struct>` RHS 浅拷贝 → AOT 与 VM 语义分歧，破坏按值语义**
- 标准：§3.8.4（行 549-553）memblock 赋值整体深拷贝；§3.9.4（行 661）struct 按值；§4.3 `Self.<static var>` 可读。
- 实现：`src/aot/tc_aot_emit_rhs.c:655-668` 对 `TC_RHS_SELF_MEMBER` 的 `slot >= 0` 一律发射 `slots[%d]` 浅拷贝，不按 `expected_type` clone；同文件 `TC_RHS_CONST_REF` 对 memblock/struct 均有 clone。VM 走"RHS 浅指针 + 承接点 clone"模型，故 `var x: memblock<int32,4> = Self.mb` 在 VM 得到深拷贝、在 AOT 得到别名。
- 判定：`memblock_store`/字段写会污染 `Self.mb`，AOT 与 VM 输出分歧。可观察行为偏差。

**[S-14] embed 模式 `tc_aot_abort` 展开为裸 `return;` 落入 `int` 函数 → 生成违反 C99 约束的代码**
- 标准：§4.2（行 933）static var 初始化器按运行时语义执行、失败即全程序准备失败；§1.3 零 UB。
- 实现：`src/aot/tc_aot_codegen.c:676-679` 宏展开为 `return;`，落在返回 `int` 的 `tc_aot_init`（行 699-704）。含可失败初始化器（如 `add(int8, 100, 100)`）的合法 `#lib` 在 embed 模式生成的 C 违反 C99 §6.8.6.4（`-pedantic -Werror` 下无法编译）。
- 判定：合法程序无法编译（embed 模式）。

**[S-15] `memblock_copy` 边界检查无符号加法回绕 → 宿主堆越界（违反零 UB）**
- 标准：§6.7.2.3（行 1617-1632）区间合法性 `d + n ≤ count_dst`；§1.3 零未定义行为。
- 实现：`src/aot/tc_aot_rt.c:656` 与 `src/vm/executor/tc_memblock_exec.c:418` 同源写法 `dst_index + length > dst_count`，三操作数为 `uint64_t`，加法模 2^64 回绕。`dst_index = 2^64-1`（可用 `sub(uint64, 0u, 1u)` 合法构造）、`length = 1` 时检查被绕过，`memcpy` 宿主堆越界读/写。
- 判定：合法 TC 程序可触发宿主 UB（崩溃/堆破坏），是本报告认定的最严重安全项。修复：改为无回绕判定（`length > dst_count || dst_index > dst_count - length`）。

**[S-16] Embed 的 bool 非规范字节注入缺口（文档 + 实现双重缺陷）**
- 标准：§3.4（行 350-351）"后端在 ABI … 接收非规范布尔字节，**必须**先规范化 … 不得让非规范字节影响比较、逻辑、I/O、函数传参或 memblock 元素内容"。
- 实现：`src/vm/embed/tc_embed.c:320-337` `tc_embed_slot_write` 原样写入 `value.bits`，不按槽位类型规范化 bool（对比 `tc_embed_make_ptr`/`tc_embed_arg_bool` 均做了 `!!` 归一）。Embed 详设行 250/497-499/1997-2004 亦未规定这三条路径的规范化义务。
- 判定：宿主可经 `tc_embed_call`/`tc_embed_slot_write` 注入 `0xFF` 等非规范 bool 字节进入 TC 抽象机。TC 纯程序不可触发，但违反 §3.4 的"必须"级要求（Embed 是 v0.0.41 正式产品面）。

### 3.6 词法/语法层（14 处 S 级，EBNF 全量比对 + 实证确认）

**[S-17] 浮点字面量 `u`/`U` 后缀报 `TC_CE_LITERAL_TYPE`，标准规定 `TC_CE_SYNTAX`**：§2.4.2（行 217）"浮点字面量不得带整数 u/U 后缀…属语法拒绝（`TC_CE_SYNTAX`）"；实测 `1.5u` 报 "float literal cannot use unsigned suffix"（`tc_lexer.c:377-381`）。阶段+码双偏离。
**[S-18] `-42u` 报 `TC_CE_LITERAL_TYPE`，应 `TC_CE_SYNTAX`**：§2.3.1（行 113）+ 附录 A `integer_literal` 负号分支无 u 后缀；`tc_lexer.c:271-276`。实测确认。
**[S-19] 浮点比较接受 `ieee` 模式（超收）**：§6.5.1（行 1414/1418）与 A.3（行 2553）"比较没有模式参数位置…属语法拒绝"；`tc_parser_rhs.c:960-977` 语法层接受 ieee（wrap 报 MODE_MISMATCH、truncate 报 KEYWORD），且 `tests/unit/runtime/test_analyzer.c:311/315` 固化了该错误行为。实测 `eq(float64, ieee, a, b)` 报 "float comparisons do not accept mode keywords" 而非 TC_CE_SYNTAX。
**[S-20] `#program` 顶层 var/语句交错不报 `TC_CE_MODULE_LAYER`**：§4.1（行 900/922）+ A.3（行 2555）"进入语句区后又出现顶层声明 → TC_CE_MODULE_LAYER"；`tc_parser.c:542-571` 把 EXEC 与 VALUE 归一（注释自述"允许交错"）。实测 `writeln(…)\nvar x: int32 = 1` 完全接受并执行。
**[S-21] 错位 `else` 报 `TC_CE_ELSE_POSITION`，标准规定 `TC_CE_INDENT_ELSE_END`**：A.2（行 2547）；`tc_parser.c:1017-1020`。
**[S-22] 缺 `end` 报 `TC_CE_MISSING_END`，标准规定 `TC_CE_SYNTAX`**：A.3 if/while 产生式 + B.1；`tc_parser.c:1104/1122/1127/1200`、`tc_parser_func.c:161`、`tc_parser_struct.c:211`。（实测确认）
**[S-23] write/writeln 多余操作数报 `TC_CE_OPERAND_COUNT`，标准规定 `TC_CE_SYNTAX`**：A.3（行 3217-3223）；`tc_parser.c:42-45`、`tc_parser_stmt.c:47/54/68`。
**[S-24] 非法模式关键字位置报 `TC_CE_KEYWORD`，标准规定 `TC_CE_SYNTAX`**：A.3（行 2553）"不满足形态统一报 TC_CE_SYNTAX"；`tc_parser_rhs.c:549-551` 用于 11 处（`add(int32, truncate, …)`、`shr(int32, wrap, …)`、`cast(int32, wrap, …)` 等）。
**[S-25] memblock 长度 `N` 缺限定名（拒绝合法程序）**：A.2 `usize_operand`（行 2460-2463）含 `qualified_identifier`/`imported_member_name`；`tc_parser_type.c:94-112` 与 `tc_parser_rhs.c:327-350` 仅接受 INTEGER/IDENTIFIER。实测 `memblock<int32, Self.N>`、`memblock(int32, count: Self.N, fill: 0)` 报 "expected memblock size"。
**[S-26] `read` 目标缺限定名**：A.3（行 3225-3226）目标 = `identifier | qualified_identifier | imported_member_name`；`tc_parser.c:446-457` 仅裸标识符。实测 `read(int32, Self.x)` 报 "expected identifier"。
**[S-27] `memblock_store`/`memblock_copy` 的 memblock 名缺限定名**：A.3 `memblock_name`（行 3061-3064）含限定形式；`tc_parser_stmt.c:841-845/914-915/941-942` 仅裸标识符。
**[S-28] funcall 命名实参按完整 `rhs` 解析（超收）**：A.3 `named_argument`（行 2720-2721）= `operand | memblock_constructor | struct_constructor`；§8.2.2 标量形参仅 operand；`tc_parser_stmt.c:497/625` 用 `tc_parse_rhs`。实测 `funcall(Self.f, a: add(int32, 1, 2))` 通过全部静态检查。
**[S-29] struct 构造器字段值按完整 `rhs` 解析（超收）**：A.3 `struct_constructor` 字段值 = `operand`（行 3129-3132）；`tc_parser_rhs.c:506-527` 回退 `tc_parse_rhs`。
**[S-30] cast/bitcast 目标为 struct/memblock 在语法期拒绝（应语义 `TC_CE_TYPE_MISMATCH`）；且 const cast 拒绝 `ptr` 目标（拒绝合法程序）**：§6.6.6（行 1562）+ A.3 注释（行 2932/3055）"语法上接受 type 全部取值，语义检查中拒绝"；§3.10.6（行 880）`cast(ptr<T>, nullptr)` 合法。`tc_parser_rhs.c:848-852/912-917/1350-1355` 语法期拒绝（含 PTR）。实测 `bitcast(S, u)` 语法期报错。

**词法/语法层其余发现（M/N）**：
- [M-25] `memblock(T, count: N)` 无 fill/元素被接受（A.3 fill_ctor 必须带 `fill:`、elems_ctor 必须 ≥1 元素；`tc_parser_rhs.c:352-355` 超收）。
- [M-26] UTF-8 有效性从未校验：§2.1（行 84-92）要求"非法 UTF-8 在 UTF-8 解码阶段拒绝"，实现仅查 BOM/U+0000（文件路径），注释内非法 UTF-8 字节被静默接受。
- [M-27] `tc_compile_source`（字符串路径）不检查内嵌 U+0000（§2.1 行 86"任何位置均非法"），内嵌 NUL 被截断而非拒绝。
- [N] `0_`/`0_u` 词法为 `0`+标识符（下游报误导性语法错，诊断精度）；`TC_TOK_BITWISE_OP` 及 `tc_parse_bitwise_bin_rhs` 为死代码（lexer 全走 LOGIC_OP 分派）；`@padding` 负/进制/u 与 `TC_CE_VAR_MISSING_INIT` 在 parse 期报告（标准标静态语义阶段）。
- **通过项**：关键字 86 词全集无遗漏；整数字面量（四进制/前导零含 0_0/分隔符/2^64-1 上限/最长匹配）正确；浮点核心（.或 e 必备、`1e5`、负零、inf/nan 小写按标识符、舍入上界）正确；缩进（4 格一级/4 倍数/行首 tab→MIXED/else-end 对齐/空行注释行/label）正确；void 仅返回类型位置、wrap_shift 仅 shl、count:/fill: 按拼写、§4.1 三类受限恢复码（MODULE_LAYER/MISSING_VISIBILITY/PROGRAM_MODE_MISUSE 语法阶段定位）均已落地。

---

## 4. M 级发现（重要）

### 4.1 错误码体系（19 个标准未定义码的系统性清单）

语言标准全文共 71 个诊断码（59 `TC_CE_*` + 12 `TC_RE_*`）；实现枚举 91 个（`src/vm/runtime/tc_types.h`，78 CE + 12 RE + 1 `TC_ERR_OUT_OF_MEMORY`）。**19 个 `TC_CE_*` 码在语言标准正文与附录 B 中零出现**，全部有真实报错路径（其中 1 个为死代码）：

| 码 | 报告位置 | 标准对应情形 | 定性 |
| -- | -------- | ------------ | ---- |
| `TC_CE_DUPLICATE_FUNCTION` | tc_func_check.c:858 | §8.1.2/B.3 规定 `FUNCTION_NAME_CONFLICT` | **冲突（= S-8）** |
| `TC_CE_DUPLICATE_PARAMETER` | tc_func_check.c:121, pass1.c:153 | §8.1.2 规定语法拒绝 | **冲突（= S-9）** |
| `TC_CE_CROSS_CONTROL_FLOW_JUMP` | tc_analyzer_pass2.c:420 | §7.3.2 规定 `LABEL_NOT_FOUND` | **冲突（= S-10）** |
| `TC_CE_KEYWORD` | tc_parser_rhs.c:549-550（11 处调用点） | §6.4.1 规定 `SYNTAX`；§6.3.1 规定 `MODE_MISMATCH` | **冲突** |
| `TC_CE_MISSING_END` | tc_parser.c:1104 等 6 处 | 附录 A 缺 end → `SYNTAX` | **冲突（= S-22）** |
| `TC_CE_ELSE_POSITION` | tc_parser.c:1018 | else 对齐 → B.1 `INDENT_ELSE_END` | **冲突（= S-21）** |
| `TC_CE_OPERAND_COUNT` | tc_parser.c:43, tc_parser_stmt.c:47/54/68 | 附录 A.3（行 2553）规定统一报 `SYNTAX` | **冲突（= S-23）** |
| `TC_CE_ARGUMENT_TYPE` | tc_func_check.c:250-261 | 传参类型 → B.4 `TYPE_MISMATCH` | **死代码**（恒不可达；实际行为已合规） |
| `TC_CE_ARGUMENT_ORDER` | tc_func_check.c:241 | §8.2.2 乱序=静态错误未给码 | 标准未定义，实现扩展 |
| `TC_CE_MISSING_ARGUMENT` | tc_func_check.c:233 | §8.2.2 缺失未给码 | 同上 |
| `TC_CE_DUPLICATE_ARGUMENT` | tc_func_check.c:192 | §8.2.2 重复未给码 | 同上 |
| `TC_CE_UNKNOWN_ARGUMENT` | tc_func_check.c:203/216 | §8.2.2 未知未给码 | 同上 |
| `TC_CE_FUNCALL_POSITION` | tc_func_check.c:975/980 | §8.2.3 位置错误未给码 | 同上 |
| `TC_CE_FUNCALL_RESULT_TYPE` | tc_func_check.c:991 | §8.2.3 返回值适配未给码 | 同上 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` | tc_func_check.c:1020 | §8.3.1 顶层 return 未给码 | 同上 |
| `TC_CE_RETURN_FORM` | tc_func_check.c:1029/1034 | §8.3.1 void/非void 形态未给码 | 同上 |
| `TC_CE_RETURN_TYPE` | tc_func_check.c:1053/1072 | §8.3.1 返回类型未给码 | 同上 |
| `TC_CE_RECURSION` | tc_callgraph.c:602 | §8.6 递归环未给码 | 同上 |
| `TC_CE_CONDITION_TYPE` | tc_analyzer_pass2_rhs.c:802 | §7.1.1/§7.2.1 条件须 bool 未给码 | 同上 |

判定与修复（二选一，需语言标准与编译器标准同步）：
- **方案 A（推荐）**：语言标准附录 B 扩充为"语言码 + 编译器确定性映射码"两级，把 14 个"未给码"情形补上正式码（这 14 个码语义清晰、测试齐全，具备成为规范码的条件）；同时把 4 个冲突码改为标准既有码。
- **方案 B**：实现删除 19 个码，改用 `SYNTAX`/`TYPE_MISMATCH`/`FUNCTION_NAME_CONFLICT`/`LABEL_NOT_FOUND` 等既有码（损失诊断精度，且需大改测试黄金集）。

### 4.2 其他实现缺陷

- **[M-1] §10.2 格式化检查顺序反转**：`tc_analyzer_pass2.c:635-643` 先 `tc_check_io_format`（报 FORMAT 错）后 `tc_check_operand`（报 TYPE/LITERAL 错）；标准行 2183 要求先操作数一致性。实测 `writeln(bool, %d, x)`（x 为 int64）报 `FORMAT_TYPE_MISMATCH` 而非 `TYPE_MISMATCH`。首诊断码可观察。
- **[M-2] strict `shl` 负边界 off-by-one**：`tc_sem_bitwise.c:107-145` 负值分支 `abs_val > INT64_MAX / pow2`；实测 `shl(int64, -2^62, 1)` 数学结果 `-2^63`（可表示）误报 `TC_RE_INTEGER_OVERFLOW`。
- **[M-3] `%0008d` 被词法拒绝**：`tc_types.c:640-659` 把第二个 `0` 当重复标志；标准 §10.5（行 2304）要求"`%0008d` 等价于 `%08d`"。实测报 "invalid format specifier"。
- **[M-4] 格式标志校验表与 §10.5 不符**：`tc_analyze_6e.c:45-68`——`-`+`0` 误判互斥（标准：`0` 被忽略）；`%t` 拒绝 `-`/宽度（= S-4）；`#` 对 `%d`/`%i`/`%u` 漏检（标准：静态格式错误）。
- **[M-5] memblock 长度头部硬编码 `uint64_t`（8 字节）**：`tc_memblock_exec.c:133/139/224-225/251/289/345/411-412`、`tc_aot_rt.c:423/429` 均 `sizeof(uint64_t)`；§3.8.2 要求 `sizeof_bits(usize)` 平台宽度。64 位构建下巧合一致；**若实现声明 32 位目标则布局错位**。§3.2.1 允许实现固定 64 位目标字长，故当前实现可接受，但需文档显式声明"64-bit-only"并加编译期断言。
- **[M-6] `nan` 字面量位模式宿主依赖**：`tc_lexer.c:548` 用宿主 `NAN`、`tc_aot_codegen.c:427-439` 原样发射；§3.6.1/§6.3.3 要求 canonical quiet NaN（`0x7FC00000`/`0x7FF8000000000000`）硬编码。实测 x86 上 `bitcast(uint32, nan)` = `7fc00000` 巧合正确，但为宿主依赖（可观察经 bitcast）。
- **[M-7] `tc_embed_slot_write` 不规范化 bool**（= S-16 的实现侧；修复需同时改 Embed 详设）。
- **[M-8] 有符号右移依赖宿主 `>>`**（`tc_sem_bitwise.c:251-254`；C99 实现定义，主流平台为算术右移；建议位模式显式实现）。
- **[M-9] 标量元素按宿主小端字节序存取**（`tc_memblock_exec.c:158/177/271/367`、`tc_struct_exec.c:160/199` 直接 `memcpy` `uint64_t bits` 低字节；大端主机上 int16/32/64 元素字节序反转，违反 §3.5 行 438 与字节序无关）。
- **[M-10] 十进制格式化依赖宿主 `printf`**（`tc_io.c:173` `%f/%e/%g`；舍入模式与 `%e` 两位指数随 libc；glibc/macOS 正确，其他平台可能不符 §10.4 行 2285-2286）。
- **[M-11] `memcopy_unsafe` 负下标误报 memblock 错误码**（`tc_memblock_exec.c:468-471` 报 `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`；§6.8.9 仅定义 `length<0` 的码）。
- **[M-12] `tc_umul64` 高 64 位结果错误**（`tc_sem_int.c:104-119`；当前所有调用点只消费 `lo`，属死代码隐患）。

### 4.3 设计文档缺陷

| 编号 | 文档 | 缺陷 | 标准依据 |
| ---- | ---- | ---- | -------- |
| M-13 | 编译器标准 §11.4.2（行 1410-1433） | 单方面新增 19 码（自称"涵盖附录 B 全部码+确定性映射"，§1.3/§11 未授权新造码） | 附录 B "全部诊断码" |
| M-14 | 编译器标准/VM 详设/CLI/旧报告 | "91 错误码"口径未澄清（71 语言码 + 19 扩展 + OOM） | 附录 B |
| M-15 | libtc设计 §2.1（行 82-92） | `tc_compile_file_opts` 少 opts 参数、`TcCompileOptions` 被声明为函数；与 libtc-api 及自身示例（行 458）矛盾（= S-16 文档侧） | —（API 层） |
| M-16 | 修复计划（行 292/351） | 把"let memblock 绑定不存在"写成语言事实，实为实现缺口 | §5.2.1/§6.7.1（= S-3） |
| M-17 | libtc设计/libtc-api 错误码分类计数 | §11.4.1 实为 47（文档"60+"）、struct 8（文档"7"）、§11.4.6 仅 2 唯一码（文档"4"）、TC_RE 12（api 写"16"） | 编译器标准 §11.4 实测 |
| M-18 | AOT 详设 §12.3（行 669） | 浮点异常优先级写反："除零、无效"应为"无效 → 除零" | §6.3.2 行 1223 |
| M-19 | AOT 详设 §10.3（行 585/588） | `*(uint64_t*)` 结构体字段解引用：严格别名+对齐 UB，违反其自身 §12.1 与标准零 UB | §3.5 行 438/§1.3 |
| M-20 | AOT 详设 §7（行 424/429 vs 401-410） | `diag->kind = TC_RETURN_SIGNAL` 承载正常返回，调用侧按非 OK 即 abort → 设计矛盾 | as-if |
| M-21 | Embed 详设（行 461） | static let "占据一个 slot（用于 ptr_address 取地址）" | §5.2 行 1023 + §6.8.4（ptr_address 禁止 let） |
| M-22 | Embed 详设（行 575） | "ptr_add ≡ slot+n，无需 sizeof(T) 乘法" | §3.10.8 位粒度步长 |
| M-23 | 开发计划（行 264-536） | 大量错误码名称过时（KEYWORD/OPERAND_COUNT/CONDITION_TYPE/MISSING_END/ELSE_POSITION/CROSS_CONTROL_FLOW_JUMP/DUPLICATE_FUNCTION/DUPLICATE_PARAMETER/ARGUMENT_*/FUNCALL_*/RETURN_*/RECURSION） | 附录 B |
| M-24 | VM 详设 §15.2（行 991） | 呈现了 DUPLICATE_FUNCTION 分歧码（随 S-8） | B.3 |

---

## 5. N / I 级发现（摘要）

**N 级（次要）**：
- N-1 编译器标准引用不存在的"语言标准 §9.2.1"（行 24）；N-2 "CFG 诊断优先级"锚定 §7.4 错误（行 22，§7.4 是 break/continue）；N-3 goto 祖先链查找引用 §7.3.3 应为 §7.3.2（行 922/92）。
- N-4 VM 详设 §12.4（行 885）指针 cast 引用"§3.10.6"应为 §3.7/§3.10.9。
- N-5 VM 详设未重述：bool 规范字节、tininess-after-rounding 与异常优先级、浮点 mod 语义、移位 k≥n、ptr 步长、I/O 原子提交（6 处覆盖缺口）。
- N-6 CLI 未复述 writeln LF 不 CRLF 改写。
- N-7 libtc设计阶段 4 图漏结构体表注册；阶段 13 只标 VM；AOT CLI 缺 -I。
- N-8 Embed 详设：AOT slot_read 不写 type（行 1926-1927）；全文 0 个错误码引用；I/O 语义未讨论；tc_aot_embed_abort 两形态矛盾（行 1297-1303 vs 1677-1681）。
- N-9 开发计划"70+ 错误码"（实际 71）、行 356 `MEMBLOCK_INDEX_OUT_RANGE` 缺 `_OF_`；未覆盖 TC-Embed 模块（Phase 1-6 止于 libtc）。
- N-10 errors.md（.cursor）仅 29 个码且含 4 个标准外码，与"错误码账本"定位不符。
- N-11 旧合规报告"91 语言错误码"把实现码集当语言码集；引用过时码与"编译器标准 §8.8–§8.9"等章节。
- N-12 实现可移植性：无 FENV 下溢启发式（tc_sem_fp.c:139-193）、`ptr_add/sub` 运行期回退接受 isize（tc_ptr_exec.c:104-118）、tc_aot_memblock_alloc 缺饱和检查、AOT `.count` 死代码回退、AOT slot_read 恒 TC_INT64。
- N-13 funcall 多余实参落入 `ARGUMENT_ORDER`（细分不足）；`ARGUMENT_TYPE` 测试用例实为字面量路径、未覆盖死代码。

**I 级（信息/确认）**：CLI 不强制 .tc 扩展名、诊断首行不打印码（软偏差，可编程获取）；Embed 宿主位置实参绕过 §8.2.2（合理扩展应明示）；AOT 预清零 slots 依赖上游确定初始化（建议加注）；"13 阶段"不冲突（标准未规定阶段数）；int→float 经 double 中转无二次舍入（双舍入定理，53≥2×24+1）。

---

## 6. 设计文档审查总评（分文档）

| 文档 | 一致度 | 主要问题 |
| ---- | ------ | -------- |
| TC 编译器标准设计说明书 | **中** | 19 个新造码（含 1 个 S 级冲突码 CROSS_CONTROL_FLOW_JUMP、1 个 S 级码切分冲突 ptr_store/形参）；3 处章节引用错误；91 码口径未澄清 |
| TC-VM 详细设计说明书 | **高** | 仅 DUPLICATE_FUNCTION 呈现（随 S-8）+ 6 处细节未重述 + 1 处引用不准；指令集全覆盖无多余指令 |
| TC-VM 命令行参考 | **高** | 91 码口径混淆；LF/扩展名小项 |
| TC-AOT 详细设计说明书 | **中** | 优先级写反、严格别名 UB、RETURN_SIGNAL 矛盾、32 位示例硬编码、缺 -I |
| TC-Embed 详细设计说明书 | **中** | bool 注入缺口（S-16 文档侧）、static let 占槽误述、ptr_add 语义误述、memblock/struct 桥接整体留白 |
| libtc 设计说明书 | **中** | §2.1 API 声明自相矛盾（tc_compile_file_opts/TcCompileOptions，**已修复并并入 §15**）、错误码计数多处错 |
| libtc-api | **中高** | 计数错（"16 个 TC_RE"）、91 口径（**已并入 libtc 设计说明书 §15**） |
| TC-0.0.41-开发计划 | **中低**（过程文档，**已删除**） | 错误码大量过时、无 TC-Embed 覆盖 |
| 结构体字段 operand 修复计划 | **高**（过程文档，**已删除**） | 仅 1 处把实现缺口写成语言事实（M-16） |
| 设计实现合规审查报告（旧） | **需更新**（**已删除**） | "91 语言错误码"口径失实；引用标准不存在的码；未反映本报告的发现 |

---

## 7. 实现分域审查总评

| 模块 | 一致度 | 要点 |
| ---- | ------ | ---- |
| 词法/语法（lexer/parser） | **低** | 核心（86 关键字全集、字面量规则、缩进、void 定位、受限恢复三码）正确；但语法层 14 处 S 级偏差（§3.6：错误码替换 SYNTAX/INDENT_ELSE_END、限定名缺位、rhs 超收、模块分层漏检）+ 3 处 M（UTF-8/NUL 校验缺失、memblock count-only 构造超收） | |
| 静态语义（analyzer） | **中偏高** | 主体规则（const_rhs 顺序、双层可变性矩阵、模块分层、命名实参、无环调用图、确定初始化、MISSING_RETURN 豁免）全部符合；偏差集中在错误码（S-8/S-9/S-10、M-13 体系）与 §10.2 顺序（M-1） |
| VM 执行器/运行时 | **中** | 数值语义正确率高（S-11 浮点 mod 除外）；S-12 格式运行期丢弃；M-2/M-3/M-4/M-5/M-7 边界缺陷 |
| AOT + libtc + Embed | **中高** | 共享核委托策略正确；S-13 浅拷贝、S-14 embed 宏、S-15 回绕检查三处严重；libtc 失败路径/版本/诊断契约正确 |
| 测试体系 | — | 19 个扩展码均有行为用例；但 `ARGUMENT_TYPE` 用例误导、§10.2 顺序无反向门禁、const_rhs 的 ptr_size/memblock 构造无覆盖（正是 S-2/S-3 未被发现的直接原因） |

---

## 8. 语言标准自身缺陷（9 处，建议先行修订）

| 编号 | 位置 | 缺陷 |
| ---- | ---- | ---- |
| STD-1 | §6.3.1（行 1196） | "运行时 `shift_expr` 不接受 `mode`"与附录 A.3 `wrap_shift_expr`（行 3009-3011）、行 2553 矛盾；实现按 EBNF 接受 `shl(int8, wrap, 1, 1)`（实测合法），建议删除行 1196 该半句 |
| STD-2 | 行 820/832/848/1420/3027 | 5 处 `neq` 应为 `ne`（A.2 `compare_op` 仅定义 `ne`） |
| STD-3 | §8.2.2/§8.3.1/§8.6/§7.1.1/§8.4.2 | 多处"静态错误"未给错误码，与 §11/附录 B"完整错误码速查"承诺矛盾 → 实现被迫自造 14 个码（M-13 体系根因） |
| STD-4 | §4.1（行 900）vs 附录 B.2 vs 附录 A.3（行 2555-2557） | MODULE_LAYER/MISSING_VISIBILITY/PROGRAM_MODE_MISUSE 阶段归属自相矛盾（SEM vs 语法阶段受限恢复） |
| STD-5 | §8.1.1（行 1981） | memblock 形参"只读"与"可 memblock_store/memblock_copy 改副本"措辞自相矛盾 |
| STD-6 | §7.3.2 | 未区分"跨函数同名标签"与"全程序无标签"，导致编译器标准拆分出 CROSS_CONTROL_FLOW_JUMP（S-10 源头） |
| STD-7 | 附录 B.12（行 3357/3359/3360/3364） | "运行时错误"表混入 4 个静态 CE 码（MEMBLOCK_INDEX_OUT_OF_RANGE 等），并误导 libtc-api 的"16 个 TC_RE"计数 |
| STD-8 | §3.5（行 436）vs §6.6.6/§3.10.9 | "指针位模式对程序不可见/槽位地址非可观察行为"与允许 `bitcast(ptr↔usize)` 矛盾（Embed 槽编码 E-04 放大了该矛盾） |
| STD-9 | 跨标准 §5.2.x | 语言 §5.2.2=三态判定 vs 编译器 §5.2.2=类型闭合、§5.2.4=三态判定，编号错位易致引用错误 |

---

## 9. 符合性亮点（实证通过，佐证实现主体正确）

- 关键字 `add`/`int32`/`usize` 等不可作标识符；`count` 可（§2.7 一致）；`-42u`、`007`、`0x_FF`、`1__2`、`0b`、`0_7`、`00`、`.5` 全部正确拒绝；`1e5`、`-0x80`、`1_2_3` 正确接受；`0x1Au` 用于有符号上下文报字面量类型错误。
- memblock `count ≥ 1`（0 拒绝）；`wrap shl` 运行时合法；`ptr_lt(nullptr,…)` → `TC_RE_NULL_POINTER_DEREFERENCE`；`ptr_eq(nullptr,nullptr)` = true；`-0.0` 无格式输出 `-0`、`%f` 输出 `-0.000000`；`eq(-0.0, 0.0)` = true。
- `bitcast(float32, 1.0)` → 位宽错误；`bitcast` 排除 bool；`truncate` 加宽拒绝；`cast(float32, 1.0f)` 正常。
- 移位先查 k<0/k≥n 再执行宿主移位；整数溢出先查后算（无"先算后查"）；bitcast 统一 `uint64_t` 抽象位串（大小端无关，符合 §3.5 行 438）；浮点→整数先判 NaN/无穷/范围；strict 浮点分类表与优先级（invalid→div0→overflow→underflow）正确；volatile 阻断 FMA 与扩展精度；无 `-ffast-math`。
- bool 规范化不变量成立（全部写路径归一化 0x00/0x01）；短路 and/or 与 xor 不短路正确；fail-fast 提交规则正确；memblock_copy 重叠 memmove 语义（临时缓冲）；struct 整块深拷贝含嵌套 memblock。
- I/O 复用统一 `tc_io`（13 种转换符齐全，`%x/%o/%b` 负值全宽如 int8 -1 → ff/377/11111111）；writeln 单 LF；原子提交（open_memstream 一次 fwrite）。
- 模块系统：静态分层、Self/导入可见性、无环调用图、static var 拓扑序与准备失败语义；libtc 失败不写 out、文件错误不冒充语法错误、无编译警告（§11.2）。
- 版本 tc-vm/tc-aot 0.0.41；REPL 已移除；示例 examples/demo 与 examples/composite 编译通过；同步脚本（doc_counts/rhs_coverage/source_naming/type_fact_source）全部通过；标准 71 个诊断码全部在实现枚举中实现。

---

## 10. 修复路线建议（按优先级）

**P0 — 正确性/安全（建议立即修复）**
1. [S-15] `memblock_copy` 边界检查改无回绕判定（VM `tc_memblock_exec.c:418` + AOT `tc_aot_rt.c:656`）。
2. [S-13] AOT `TC_RHS_SELF_MEMBER` 按 `expected_type` 深拷贝（复用 `tc_aot_memblock_clone`/`tc_aot_struct_clone`）。
3. [S-14] embed 模式 `tc_aot_init` 使用带返回值的 abort 宏（如 `return 1`）。
4. [S-11] 实现浮点 `mod`（§6.3.7 全文：`trunc(a/b)` 精确余数核心 + strict 异常分类 + ieee canonical NaN），删除 `tc_validate_fp_arith_mode` 中的拒绝。
5. [S-12] 把 `TcFormatFullSpec`（flag/width/precision）贯穿 `tc_io_write_value` 渲染链，实现 §10.4/§10.5（`%08.3f` → `0003.142`）。
6. [S-6] 补 `ptr_store`/`memcopy_unsafe` 经形参的可变性检查（统一 `TC_CE_CONSTANT_ASSIGNMENT`）。
7. [S-16] `tc_embed_slot_write`/`tc_embed_call` 按槽位类型规范化 bool。

**P1 — 接受集与诊断码对齐**
8. [S-2/S-3] const_rhs 补 `ptr_size` 与 memblock 常量构造器（const_eval）。
9. [S-5/S-7] 词法拒绝 `1.`；`read` 接受集收紧（`1.`/`.5` 拒绝）。
10. [S-1] 移除 `padding` 关键字。
11. [S-4/M-3/M-4] 格式校验对齐 §10.5（%t 允许 `-`/宽度、`-`+`0` 不互斥、`#` 范围、`%0008d` 合并前导零）。
12. [S-8/S-9/S-10] 错误码三处冲突改报标准码（或按方案 A 同步修订标准附录 B）。
13. [S-17/S-18/S-21~S-24] 词法/语法错误码替换：`-42u`/`1.5u`→`TC_CE_SYNTAX`；ELSE_POSITION→INDENT_ELSE_END；MISSING_END/OPERAND_COUNT/KEYWORD→`TC_CE_SYNTAX`（或标准补码）。
14. [S-25/S-26/S-27] read 目标、memblock 名、memblock 计数 N 补 qualified/imported 限定名。
15. [S-28/S-29] funcall 实参与 struct 构造器字段值改为 `operand|memblock_ctor|struct_ctor` 三选一解析。
16. [S-19] 比较运算语法层拒绝 ieee/wrap/truncate（TC_CE_SYNTAX），并修正 test_analyzer.c:311/315 固化用例。
17. [S-20] 恢复 #program 严格五层序（EXEC↔VALUE 不再归一），报告 TC_CE_MODULE_LAYER。
18. [S-30] cast/bitcast 目标语法期接受完整 type（struct/memblock/bool 交语义 TC_CE_TYPE_MISMATCH）；const cast 恢复接受 ptr 目标（`cast(ptr<T>, nullptr)` 合法）。
19. [M-1] §10.2 检查顺序对调并补 `diag_priority_format_after_operand` 门禁。
20. [M-2] `shl` 负边界判定改 `abs_val > (1<<63)/pow2`。
21. [M-25/M-26/M-27] memblock count-only 构造拒绝；UTF-8 解码校验（含注释）；字符串路径 NUL 拒绝。
22. [M-5/M-9/M-8] 目标字长策略：显式声明 64-bit-only + 断言（或实现 usize 头宽与大小端无关存取）。

**P2 — 文档与标准修订**
23. 语言标准：修复 STD-1～STD-9（附录 B 扩充为两级码表；`neq`→`ne`；阶段归属统一；§8.1.1 措辞）。
24. 编译器标准：3 处章节引用；§11.4.2 与语言标准附录 B 对齐（19 码归属裁决）；§3.2 形参 ptr_store 码统一；91 码口径注明。
25. libtc 设计说明书 §2.1 API 声明、错误码计数；AOT 详设优先级/别名/返回机制/32 位示例；Embed 详设 bool 规范化义务/static let/ptr_add 措辞。
26. 旧合规报告：更新 91→71+19 口径、替换过时码引用。
27. 测试补齐：`ptr_size`/memblock 构造 in `let`、`%-8t`/`%08.3f`/`%0008d` 黄金输出、形参 ptr_store 拒绝、浮点 mod 全套、memblock_copy 极值下标。

---

## 11. 附录 A：诊断码集合对照（实证脚本结论）

- 语言标准（全文 grep）：**71 码** = 59 `TC_CE_*` + 12 `TC_RE_*`（附录 B.12 混入 4 个静态码，见 STD-7）。
- 实现枚举（`tc_types.h`）：**91 码** = 78 `TC_CE_*` + 12 `TC_RE_*` + 1 `TC_ERR_OUT_OF_MEMORY`；`tc_error_kind_name` 91 个 case 与枚举 1:1、打印名无重复。
- 标准 71 码 → 全部实现（差集为空）。
- 实现 19 个额外 CE 码 → 全部有报错路径（其中 `ARGUMENT_TYPE` 为死代码），逐码定性见 §4.1 表。
- "91"出现处：旧合规报告行 47/330/371、CLI 参考行 463/473、libtc-api 行 314——均指实现/编译器标准口径，与语言标准 71 无一处说明关系。

## 12. 附录 B：实证用例清单（全部使用工作区构建产物实测）

| # | 用例 | 结果 | 对应发现 |
| -- | ---- | ---- | -------- |
| 1 | `var padding: int32 = 3` | 拒绝（"expected identifier"） | S-1 |
| 2 | `var x: float64 = 1.` | 接受，值 1 | S-5 |
| 3 | `let w: usize = ptr_size(int32, nullptr)` | 拒绝（"expected constant expression"） | S-2 |
| 4 | `let m: memblock<int32,3> = memblock(int32, count: 3, fill: 0)` | 拒绝 | S-3 |
| 5 | 重复形参 `f(a: int32, a: int32)` | "duplicate parameter 'a'"（语义码） | S-9 |
| 6 | 形参 `ptr_address` 后 `ptr_store` | 接受并执行（无诊断） | S-6 |
| 7 | 跨函数标签 `goto` | "cannot jump to label in another function" | S-10 |
| 8 | `writeln(bool, %-8t, t)` | 拒绝（"%%t does not support flags, width, or precision"） | S-4 |
| 9 | `writeln(float64, %08.3f, 3.14159265)` | 输出 `3.141593`（应为 `0003.142`） | S-12 |
| 10 | `mod(float64, 5.5, 2.0)` | 拒绝（"mod not supported for float types"） | S-11 |
| 11 | `read(float64)` 输入 `1.` / `.5` | 接受，值 1 / 0.5 | S-7 |
| 12 | 同名 `func`（`g()` 定义两次） | `TC_CE_DUPLICATE_FUNCTION` 路径（源码） | S-8 |
| 13 | `writeln(bool, %d, x)`（x: int64） | 报格式错而非操作数类型错 | M-1 |
| 14 | `shl(int64, -2^62, 1)` | 误报 "shift left overflow" | M-2 |
| 15 | `%0008d` | 拒绝（"invalid format specifier"） | M-3 |
| 16 | `bitcast(uint32, nan)` | `7fc00000`（x86 巧合正确，宿主依赖） | M-6 |
| 17 | 关键字 `add`/`int32`/`usize` 作标识符 | 正确拒绝；`count` 可用 | OK-1 |
| 18 | `-42u`/`007`/`0x_FF`/`1__2`/`0b`/`0_7`/`00`/`.5` | 正确拒绝 | OK-2 |
| 19 | `1e5`/`-0x80`/`1_2_3`/`0x1Au`(无符号上下文) | 正确接受/按规则报字面量类型错 | OK-2 |
| 20 | `memblock count: 0` | 正确拒绝（≥1） | OK-2 |
| 21 | `shl(int8, wrap, 1, 1)` | 合法，输出 2 | STD-1 佐证 |
| 22 | `ptr_lt(nullptr)` / `ptr_eq(nullptr,nullptr)` | `TC_RE_NULL_POINTER_DEREFERENCE` / `true` | OK-3 |
| 23 | `-0.0` 无格式 / `%f` / `eq(-0.0,0.0)` | `-0` / `-0.000000` / `true` | OK-4 |
| 24 | `bitcast(float32, 1.0)` / `bitcast(int8, true)` / `truncate` 加宽 | 位宽错 / bool 排除 / 拒绝 | OK |
| 25 | AOT `--check` 与 VM 全同（t5/t19b/t29/t34/t35 差分） | 共享前端行为一致 | 附注 |
| 26 | 示例 examples/demo、examples/composite | 编译通过 | OK-5 |
| 27 | 顶层 `writeln` 后 `var x` 交错 | **被接受并执行**（应 TC_CE_MODULE_LAYER） | S-20 |
| 28 | `eq(float64, ieee, a, b)` | 报 MODE_MISMATCH 类错（应 TC_CE_SYNTAX） | S-19 |
| 29 | `memblock<int32, Self.N>` / `read(int32, Self.x)` | 被拒（应接受限定名） | S-25/S-26 |
| 30 | `funcall(Self.f, a: add(int32, 1, 2))` | **通过全部静态检查**（应 TC_CE_SYNTAX） | S-28 |
| 31 | `bitcast(S, u)`（S 为 struct） | 语法期报错（应语义 TC_CE_TYPE_MISMATCH） | S-30 |
| 32 | `1.5u` / `-42u` | 报 TC_CE_LITERAL_TYPE（应 TC_CE_SYNTAX） | S-17/S-18 |
| 33 | `0x_FF`/`1__2`/`0b`/`0_7`/`00` 拒绝；`-0x80`/`1_2_3` 接受 | 正确 | OK-2 |

---

## 13. 结语

TC 0.0.41 的抽象机语义核心（数值、内存、控制流、模块）实现扎实，与语言标准高度一致；本报告的偏差集中在**诊断码体系（19 个未定义码/4 个冲突码）、格式控制半实现（编译期拒绝 + 运行期丢弃）、浮点 mod 整节缺失、AOT 三处严重缺陷**以及**文档与标准的口径脱节**。多数问题修复成本低、测试黄金集更新即可收敛；真正的工程决策点在于：**语言标准附录 B 需要一次补齐修订**（把 14 个"未给码"情形正式化），否则"首个诊断码可观察"（§1.3）与"附录 B 为完整速查"（§11）的承诺将长期无法被任何实现满足。

*本报告为检查分析结论；对全部被审文件未作修改。*

---

## 14. 收口结论（2026-08-30）

本节是 P0–P6 落地后的对外结论，不回溯修改 §1–§13。口径取自修复计划 §0，禁止写成笼统「通过」。

**发布结论：S 级符合；M 级除标注的可降级项外收敛；可移植性例外见修复计划 P4。**

| 项 | 收口状态 |
| -- | -------- |
| 30 项 S 级 | 清零（接受集、诊断码冲突、浮点 `mod`、格式控制、AOT 深拷贝/embed abort/无回绕边界、Embed bool 规范化） |
| 附录 B ↔ 实现 | **85** 语言码与实现枚举 1:1，另 +1 `TC_ERR_OUT_OF_MEMORY`（合计 86） |
| 27 项 M 级 | 必做项清零；**降级** FP-4.5 M-9（memblock/struct 元素按宿主端序存取，未完全符合语言标准 §3.5）、FP-4.6（浮点十进制输出仍委托宿主 `snprintf`，未完全符合 §10.4） |
| N/I | P5 已核对；N-12 / N-13 记为债务（见修复计划 P5 清单与 AOT 详设 §19），不计入本轮必须交付 |
| 门禁（2026-08-30） | `bash scripts/run_tests.sh` 全绿：VM **911**、AOT 执行 **464**、Unit（含 `check-embed` / `check-embed-aot`）；四个 `scripts/sync/check_*.py` 通过 |

降级项的实现侧说明见 [TC-AOT 详细设计说明书](./TC-AOT详细设计说明书-0.0.41.md) §19。任务对照与阶段状态见[修复计划](./TC-0.0.41-语言标准符合性修复计划.md) §10。`examples/` 已移出版本库，不作为发布门禁路径。
