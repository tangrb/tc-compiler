# TC 0.0.41 语言标准符合性修复计划

> **基线**：[TC-0.0.41-语言标准符合性检查分析报告](./TC-0.0.41-语言标准符合性检查分析报告.md)
> **规范权威**：[TC 语言标准 0.0.41](./TC语言标准设计说明书-0.0.41.md) · [TC 编译器标准 0.0.41](./TC编译器标准设计说明书-0.0.41.md)
> **范围**：不新增语言能力；执行语义、接受集与诊断码向现行标准收敛（诊断码经 P0 标准修订后与实现一次性对齐），只做符合性修复与标准/文档同步。
> **原则**：① 安全/正确性优先；② 每项修复必须有测试证据；③ 决策集中、逐码裁决；④ 保留所有"诊断更友好"的专用码（经标准补条目合法化），只删除与标准既有规定直接冲突的码（**即分析报告 §4.1 方案 A，而非方案 B**）。
> **编排**：按阶段（P0–P6）组织任务与依赖；不排工期、不估人日。
> **收口（2026-08-30）**：P0–P6 已落地。对外发布结论见 **§10**。

---

## 0. 目标

消除报告中的 **30 项 S 级**、**27 项 M 级**偏差（报告 M 编号实为 M-1～M-27，摘要表"M 22"系笔误，本计划以 27 为准），修订语言标准 **9 处自身缺陷**，使设计文档与实现同语言标准一致（或经标准修订达到一致），并为每处修复建立回归测试。

**成功判据（DoD）**：
- **硬目标**：① 30 项 S 级全部清零；② 附录 B = 85 码且与实现枚举 1:1（另 +1 `TC_ERR_OUT_OF_MEMORY`）；③ 全部回归门禁（`run_tests.sh` / `make test-unit` / `check-embed*` / 4 个 `check_*.py`）通过。
- **软目标**：27 项 M 级除显式标注降级项（FP-4.5 端序、FP-4.6 自实现 printf，**默认降级**，见 P4）外清零；N/I 级随 P5 文档同步收敛（N 项逐条核对清单见 P5）。
- **定性说明**：P0 是**规范性修订**（改语言标准），不是"仅文档勘误"；错误码一次收敛后，实现不再保有标准之外的码。

**发布结论口径（防过度宣称）**：各阶段完成后的对外结论应写为「**S 级符合；M 级除标注的可降级项外收敛；可移植性例外见 P4**（FP-4.5 端序、FP-4.6 自实现 printf 若降级则明示未完全符合 §3.5 行 438 / §10.4 行 2285-2286）」，不得再写旧合规报告式的笼统"通过"。

---

## 1. 阶段总览与依赖

| 阶段 | 内容 | 关联发现 | 依赖 |
| ---- | ---- | -------- | ---- |
| **P0** | 语言标准修订（STD + 附录 B 补码） | STD-1~9、19 码裁决 | 无（与 P1 无相互依赖） |
| **P1** | 安全/正确性（内存安全、AOT 语义、浮点 mod、格式化渲染） | S-11~S-15、S-6、S-16；S-4/M-3/M-4（FP-4.1 并入本阶段） | 无 |
| **P2** | 接受集偏差（词法/语法/const 求值） | S-1~S-5、S-7、S-19~S-20、S-25~S-30、M-25~M-27 | 无（仅格式相关项与 P1 的 FP-1.5/FP-4.1 耦合） |
| **P3** | 错误码对齐（冲突码改标准码） | S-8~S-10、S-17~S-18、S-21~S-24、M-1 | P0（附录 B 定稿） |
| **P4** | 边界/格式/可移植性 | M-2、M-5~M-12（S-4/M-3/M-4 见 P1 / FP-4.1） | P1 |
| **P5** | 设计文档同步 | M-13~M-24、N-1~N-13、旧报告 | P0+P3 定稿后 |
| **P6** | 测试补齐与门禁固化 | 全部 | 随各阶段 |

P0 只改语言标准、P1 只改代码，二者无相互依赖。P3 必须在 P0 附录 B 定稿之后执行，避免错误码改两次。FP-4.1（格式静态校验）与 FP-1.5（运行期渲染）同属格式链路，归入 P1 一并完成。

> **阶段编号说明**：本计划的 P0–P6 是**独立编号**，与分析报告 §10 的 P0（正确性/安全）/P1（接受集+诊断码）/P2（文档+标准）**不同名同义**。对照：本计划 P1 ≈ 报告 P0，P2+P3 ≈ 报告 P1，P0+P5 ≈ 报告 P2。执行时以本计划编号为准。

---

## 2. P0：语言标准修订（`docs/TC语言标准设计说明书-0.0.41.md`）

| 任务 | 缺陷 | 修订动作 |
| ---- | ---- | -------- |
| FP-0.1 | STD-2 | 5 处 `neq` 改为 `ne`（行 820/832/848/1420/3027） |
| FP-0.2 | STD-1 | 删除 §6.3.1（行 1196）"运行时 shift_expr 不接受 mode"半句，保留"运行时 wrap_shift_expr 只接受 shl" |
| FP-0.3 | STD-4 | 统一 MODULE_LAYER/MISSING_VISIBILITY/PROGRAM_MODE_MISUSE 的阶段归属（建议标"语法阶段受限恢复"），附录 B.2 阶段列改为 SYN |
| FP-0.4 | STD-5 | §8.1.1（行 1981）措辞润色：改为"memblock 形参为只读**绑定**（不可整体/字段赋值），但可对副本做 memblock_store/memblock_copy"（**可选润色**——现行措辞已基本写清，非阻塞） |
| FP-0.5 | STD-6 | §7.3.2 增加"标签存在于另一函数"与"全程序无标签"的区分说明（统一 → `TC_CE_LABEL_NOT_FOUND`） |
| FP-0.6 | STD-7 | 附录 B.12 移出 4 个静态 CE 码（MEMBLOCK_INDEX_OUT_OF_RANGE 等）到 B.5/B.7 正确小节 |
| FP-0.7 | STD-8 | §3.5（行 436）加注：指针位模式经 `bitcast(ptr↔usize)` 可被观测，实现定义的是抽象槽编码而非宿主地址 |
| FP-0.8 | STD-3 | **附录 B 补码（核心）**：见本节错误码裁决表，把 14 个"保留"码补为规范条目；**每个保留码须「正文条款 + 附录 A 诊断边界句 + 附录 B」三处同改**——尤其 `MISSING_END`/`OPERAND_COUNT` 须改 A.3 行 2553 的"统一报 SYNTAX"句、`DUPLICATE_PARAMETER` 须改 §8.1.2 的"语法拒绝"为静态语义拒绝，避免"附录有码、正文仍写 SYNTAX"。**并加白名单句式**：仅"缺 `end`""操作数个数错误"两个失配用专用码，其余形态失配仍 `TC_CE_SYNTAX`，防止专用码边界继续膨胀 |
| FP-0.9 | STD-9 | 跨标准 §5.2.x 编号错位：在编译器标准 §5.2 前加"与语言标准 §5.2.x 编号不对齐"说明，或调整编号 |
| FP-0.10 | 码表澄清 | 在标准加一条短注区分形参可变性两类码：**直接**赋值/`read` 目标 → `TC_CE_PARAMETER_ASSIGNMENT`（§5/§3.9/§8）；**经 `ptr_address` 取址后 `ptr_store`/`memcopy_unsafe`** → `TC_CE_CONSTANT_ASSIGNMENT`（§6.8.3/§6.8.9/§3.10.3）。防止编译器标准再写回 `PARAMETER_ASSIGNMENT` 给 ptr_store |
| FP-0.11 | 版本治理 | 版本号保持 **0.0.41**；附录 B 补码至 85 码写入现行文本与文末变更记录；编译器标准等引用处写「附录 B 85 码」，不另标修订号 |

**验证**：`python3 scripts/sync/check_doc_counts.py` 仍通过；grep 附录 B 码数 = 71 + 14 = 85；标准文档头部含修订标记、文末变更记录与 FP-0.1～0.11 一一对应。

**状态（2026-08-29）**：P0 已落地。语言标准版本为 **0.0.41**；附录 B = 85 码；编译器标准已标注语言基线 0.0.41 与 §5.2.x 编号不对齐说明。P3 已将实现枚举收敛为 86（= 85 语言码 + 1 `TC_ERR_OUT_OF_MEMORY`）。

### 2.1 错误码裁决表（19 个标准未定义码的处置；P3 依此改实现）

| 码 | 处置 | 理由 | 动作落点 |
| -- | ---- | ---- | -------- |
| `TC_CE_DUPLICATE_FUNCTION` | **删除** | 标准 B.3 已完备（函数名重复 → `FUNCTION_NAME_CONFLICT`） | FP-3.1 改实现 + 编译器标准 §11.4.2 + VM 详设 §15.2 |
| `TC_CE_DUPLICATE_PARAMETER` | **保留 + 标准补码** | EBNF 无法表达"名不重复"，标准 §8.1.2 的"语法拒绝"措辞有误 | FP-0.8 改标准 §8.1.2 为"静态语义拒绝" |
| `TC_CE_CROSS_CONTROL_FLOW_JUMP` | **删除** | 标准 §7.3.2 步骤 6 已完备（`LABEL_NOT_FOUND`） | FP-3.2 改实现 + 编译器标准 §11.4.2 |
| `TC_CE_KEYWORD` | **删除** | 标准 §6.3.1/§6.4.1 已规定 SYNTAX/MODE_MISMATCH | FP-3.3 改实现 11 处调用点 |
| `TC_CE_MISSING_END` | **保留 + 标准补码** | §1.3 已授权"特定失配专用码"；诊断更友好、测试齐全 | FP-0.8 补附录 B.1（SYN 阶段） |
| `TC_CE_ELSE_POSITION` | **删除** | 标准 A.2（行 2547）已有 `TC_CE_INDENT_ELSE_END`（else/end 不对齐），保留会制造同义双码 | FP-3.6 改报 `INDENT_ELSE_END`（`tc_parser.c:1018`） |
| `TC_CE_OPERAND_COUNT` | **保留 + 标准补码** | 同上 | FP-0.8 补附录 B.1 |
| `TC_CE_ARGUMENT_TYPE` | **删除** | 死代码（恒不可达，实际已报 `TYPE_MISMATCH`） | FP-3.4 删枚举/命名/白名单 |
| `TC_CE_ARGUMENT_ORDER` / `MISSING_ARGUMENT` / `DUPLICATE_ARGUMENT` / `UNKNOWN_ARGUMENT` | **保留 + 标准补码** | §8.2.2 只写"静态错误"未给码 | FP-0.8 补附录 B（SEM） |
| `TC_CE_FUNCALL_POSITION` / `FUNCALL_RESULT_TYPE` | **保留 + 标准补码** | §8.2.3 未给码 | FP-0.8 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` / `RETURN_FORM` / `RETURN_TYPE` | **保留 + 标准补码** | §8.3.1 未给码 | FP-0.8 |
| `TC_CE_RECURSION` | **保留 + 标准补码** | §8.6 未给码 | FP-0.8 |
| `TC_CE_CONDITION_TYPE` | **保留 + 标准补码** | §7.1.1/§7.2.1 未给码 | FP-0.8 |

> 净结果：**保留 + 标准补码 14 码**、**删除 5 码**（DUPLICATE_FUNCTION / CROSS_CONTROL_FLOW_JUMP / KEYWORD / ARGUMENT_TYPE / ELSE_POSITION）。标准附录 B 由 71 码 → **85 码**（+14 补码）；实现由 91 码 → **86 码**（= 85 语言码 + 1 `TC_ERR_OUT_OF_MEMORY`，删 5 码）。注意：删除的 5 码本不在语言标准 71 内，故**不参与**附录 B 减法——标准净增恰为 +14，无 −5 项。双方最终以附录 B（85 码）为唯一权威清单。

---

## 3. P1：安全与正确性

本阶段不依赖 P0。格式静态校验（FP-4.1）与运行期渲染（FP-1.5）同属格式链路，在本阶段一并完成。

### FP-1.1 `memblock_copy` 边界检查回绕（S-15）
- **文件**：`src/vm/executor/tc_memblock_exec.c:418`、`src/aot/tc_aot_rt.c:656`（同源两处）。
- **改动**：`dst_index + length > dst_count` 改为无回绕判定 `length > dst_count || dst_index > dst_count - length`（对 src 同理）；同步校验 `tc_memblock_read_index` 对 usize 极值下标的处理。
- **测试**：新增 `tests/errors/runtime/memblock_copy_overflow_guard.tc`（`sub(uint64, 0u, 1u)` 构造极值下标 + 长度 1）→ 期望 `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`；同用例进 AOT 差分。
- **验证**：`bash scripts/run_tests.sh --filter memblock_copy`；VM/AOT 输出一致。

**状态（2026-08-29）**：P1 已落地并经 `bash scripts/run_tests.sh` 全绿（VM/AOT/Unit）。FP-1.1～1.7 与并入的 FP-4.1 均已改实现并补测试。附带对齐 §4.2：`static var` 初始化器接受 `memblock`/结构体构造器（供 FP-1.2 的 `Self.<memblock/struct>` 用例）。FP-1.6 溯源仅标记 `ptr_address(形参/let)` 的结果为只读，**不**把 `ptr<T>` 形参本身当作只读所指（Embed 宿主指针写入仍合法）。格式浮点十进制核心仍委托宿主 `snprintf`（FP-4.6 自实现 printf 维持 P4 默认降级）。

### FP-1.2 AOT `Self.<memblock/struct>` 深拷贝（S-13）
- **文件**：`src/aot/tc_aot_emit_rhs.c:655-668`。
- **改动**：`TC_RHS_SELF_MEMBER` 按 `expected_type` 分支——memblock → `tc_aot_memblock_clone`、struct → `tc_aot_struct_clone`、标量/ptr 保持现值，与 `TC_RHS_CONST_REF`（行 118-141、179-197）对齐。
- **测试**：`tests/valid/self_member_memblock_copy.tc`（`var x: memblock<int32,4> = Self.mb`，改 x 后断言 `Self.mb` 不变）、`self_member_struct_copy.tc`；VM/AOT 差分。
- **验证**：AOT 与 VM 输出字节逐字节一致。

### FP-1.3 embed 模式 abort 宏返回类型（S-14）
- **文件**：`src/aot/tc_aot_codegen.c:676-679`（宏）、`:699-704`（`tc_aot_init`）。
- **改动**：embed 模式宏改为带值返回（如 `do { tc_aot_embed_abort(...); return 1; } while(0)`），或 `tc_aot_init` 内用局部 `int rc` 收集错误再统一返回；同步 `src/aot/tc_aot_embed_rt.h`。
- **测试**：新增 embed 用例含可失败 static var 初始化器（`public static var s: int8 = add(int8, 100, 100)`），`cmake --build build --target check-embed-aot`。
- **验证**：生成 C 以 `-std=c99 -Wall -Wextra -Werror -pedantic` 编译通过。

### FP-1.4 浮点 `mod` 实现（S-11，唯一涉及新数学语义）
- **文件**：`src/vm/runtime/tc_sem_fp.c`（新增 `tc_fp_mod`）、`src/vm/runtime/tc_semantics.c:150-154`（删除对 `TC_MOD` 的拒绝）、`src/vm/runtime/tc_sem_fp.h`。
- **改动**：按 §6.3.7 实现——有限值先算数学整数商 `q = trunc(a/b)`（**不得先做浮点除法回乘回减**），`r = a - q*b` 直接构造目标类型位模式；strict 异常分类（NaN/无穷被除数 → invalid；`0 mod 0` → invalid；有限/零 → div0；优先级 invalid→div0）；ieee 返回 canonical quiet NaN；`-0.0` 符号保持。const_eval 复用同一核心（§5.2.1）。
- **测试**：`tests/valid/fp_mod_*.tc`（5.5 mod 2.0=1.5、-5.5 mod 2.0=-1.5、0 mod 0 invalid、5.0 mod 0.0 div0、ieee → canonical quiet NaN（`bitcast` 校验 `0x7FC00000`/`0x7FF8000000000000`，非仅 `isnan`）、float32/float64 分别）；`tests/unit/runtime/test_fp_mod.c`。
- **验证**：`bash scripts/run_tests.sh --filter fp_mod` + `make test-unit`；与标准 §6.3.7 表逐行对照。

### FP-1.5 格式化 flags/width/precision 运行期渲染（S-12）
- **文件**：`src/vm/runtime/tc_io.h`（签名加 `TcFormatFullSpec` 或增参数）、`src/vm/runtime/tc_io.c:186-390`、`src/vm/executor/tc_executor.c:203`。
- **改动**：把 `TcFormatFullSpec`（`tc_types.h:197-206` 已定义、`tc_types.c:626-710` 已解析）贯穿 `tc_io_write_value`/`tc_io_write_formatted`/`tc_io_render_value`；实现 §10.4/§10.5：先造数字文本 → 加符号/进制前缀 → 按 width 对齐/零填充 → precision 控制小数位/有效数字；`%g` 阈值（e<-4 或 e>=p）、`%e` 两位指数、`-0` 符号、`#` 备用形式、`+`/`0`/`-` 标志、特殊值 `INF/NAN`。可先对齐已解析的 flag/width/precision 子集，再开 P4 的 FP-4.6 全自实现。
- **阶段耦合**：本任务黄金用例含 `%-8t`/`%+d`/`%#x`，其静态合法性由 FP-4.1（格式标志校验对齐 §10.5）决定；**FP-4.1 须与本任务同属 P1 完成**，否则本阶段单独跑 format 会因 `%-8t` 被编译期拒绝而失败。
- **测试**：`tests/valid/format_spec_*.tc` 黄金输出（`%08.3f`→`0003.142`、`%-8f`、`%+d`、`%#x`、`%08d`、`%e`、`%g`、`%-8t`、宽度 65535 上限、宽度 65536 → `TC_CE_FORMAT_SPECIFIER`）。
- **验证**：`bash scripts/run_tests.sh --filter format`；与 §10.4 表逐行对照。

### FP-4.1 格式标志静态校验（S-4 / M-3 / M-4；并入 P1，与 FP-1.5 同阶段）
- **文件**：`src/vm/analyzer/tc_analyze_6e.c:45-68`、`src/vm/runtime/tc_types.c:640-659`。
- **改动**：标志校验对齐 §10.5（`%t` 允许 `-`/宽度、`-`+`0` 不互斥、`#` 对 `%d/%i/%u` 拒绝）；前导 `0` 合并（`%0008d`≡`%08d`）。
- **测试**：与 FP-1.5 的 `format_spec_*.tc` 共用；另覆盖 `%-8t` 静态接受、`%0008d` 合法。

### FP-1.6 形参经 `ptr_address` 后 `ptr_store` 的可变性检查（S-6）
- **文件**：`src/vm/analyzer/tc_ptr_check.c`（ptr_store/memcopy_unsafe 可变性校验）。
- **改动**：`ptr_store`/`memcopy_unsafe` 目标指针所指外层绑定为**形参**时，报 `TC_CE_CONSTANT_ASSIGNMENT`（与 §6.8.3 一致；同步编译器标准 §3.2 行 294 的 `TC_CE_PARAMETER_ASSIGNMENT` 表述）。本任务依赖 P0 的 FP-0.10 码表澄清（两类形参码分界）；实现改动本身不依赖附录 B 补码。
- **溯源机制**：采用**「ptr_store/memcopy_unsafe 侧溯源判定」**，**不在 `ptr_address` 处拦截形参取址**——标准 §6.8.3 的检查点在写操作，且对形参取址本身合法（现有 `tc_ptr_check.c:186` 仅拒绝 let/static let，实测取址已通过、仅 store 缺检查）。实现要点：① `ptr_address` 结果须携带所指外层绑定的来源标记（新增 `TcPtrTarget`/来源 `TcSymbol` 引用，或 ptr 值记录 `binding_kind ∈ {var, param, static_var}`）；② `tc_ptr_check` 的 `ptr_store`/`memcopy_unsafe` 沿该来源解析到形参符号 → 报 `TC_CE_CONSTANT_ASSIGNMENT`。若现无来源字段，需在 RHS→executor 的 ptr 值结构补字段并串通 const_eval/AOT 两路径。
- **测试**：`tests/errors/static/ptr_store_through_param.tc` → `TC_CE_CONSTANT_ASSIGNMENT`；反向门禁——对形参 `ptr_address`/`ptr_load` 读**仍合法**（不误报）。

### FP-1.7 embed bool 边界规范化（S-16）
- **文件**：`src/vm/embed/tc_embed.c:320-337`（`tc_embed_slot_write`）、`:555-557`（`tc_embed_call`）；`docs/TC-Embed详细设计说明书-0.0.41.md` 行 250/497-499/1997-2004。
- **改动**：按槽位类型（或 `value.type->tag == TC_BOOL`）做 `value.bits = value.bits ? 1 : 0`；AOT 模式下从 `TcTypedProgram` 恢复槽位类型（与 P4 的 FP-4.8 `slot_read` 类型补齐联动）。
- **测试**：`tests/unit/embed/test_embed_bool_normalize.c` 注入 `{TC_BOOL, 0xFF}` 后断言读回 `0x01`。

---

## 4. P2：接受集偏差（词法/语法/const 求值）

| 任务 | 发现 | 文件与改动 | 测试 |
| ---- | ---- | ---------- | ---- |
| FP-2.1 | S-1 `padding` 关键字 | `tc_lexer.c:609-612` 移除 `padding`→关键字；`tc_parser_struct.c:21` 改为按标识符拼写匹配 `@padding` | `tests/valid/identifier_named_padding.tc` |
| FP-2.2 | S-5 浮点 `1.` | `tc_lexer.c:367-407` 浮点扫描要求小数点后 ≥1 位数字 | `tests/errors/lexical/float_trailing_dot.tc` |
| FP-2.3 | S-7 read 接受集 | `tc_io.c:559-597` 要求 `digits_before>=1` 且 `.` 后 `digits_after>=1` | `tests/errors/runtime/read_float_invalid.tc`（`1.`/`.5`） |
| FP-2.4 | S-2 `ptr_size` in let | `tc_parser_rhs.c` const_rhs 分发（1527 起）补 `TC_TOK_PTR_SIZE`；`tc_const_eval.c` 补 `TC_RHS_PTR_SIZE` 求值（返回 `sizeof_bits(T)`） | `tests/valid/let_ptr_size.tc` |
| FP-2.5 | S-3 memblock 常量构造 | const_rhs 分发补 `TC_TOK_MEMBLOCK` → const_memblock_constructor；`tc_const_eval.c` 实现 const memblock 构造（全常量操作数） | `tests/valid/let_memblock_const.tc` |
| FP-2.6 | S-25/S-26/S-27 限定名 | 解析侧接受 `Qual.ident`/`Self.ident`；Pass2 对 `static var` 在 intern 前折叠 memblock 命名 N（`tc_analyzer_pass2.c`） | `tests/valid/qualified_memblock_count.tc`、`qualified_read_target.tc` |
| FP-2.7 | S-28/S-29 rhs 超收 | `tc_parser_stmt.c:497/625`、`tc_parser_rhs.c:506-527` 改为 `operand|memblock_ctor|struct_ctor` 三选一 | `tests/errors/static/funcall_arg_expr.tc`、`struct_ctor_field_expr.tc` |
| FP-2.8 | S-19 比较模式 | `tc_parser_rhs.c:960-977` 比较一律拒绝 ieee/wrap/truncate（`TC_CE_SYNTAX`）；修正 `tests/unit/runtime/test_analyzer.c:311/315` | `tests/errors/lexical/compare_mode.tc` |
| FP-2.9 | S-20 模块分层 | `tc_parser.c:542-571` 删除 EXEC↔VALUE 归一，恢复严格五层序；**并修正依赖顶层交错的示例/测试**（已确认 `examples/demo/main.tc` 第 36 行 writeln 后仍有第 38-45 行 var、`examples/composite/main.tc` 同——须把声明整体移到语句区之前） | `tests/errors/static/module_layer_interleave.tc`；扫 `examples/` 与 `tests/valid/` 顶层交错 |
| FP-2.10 | S-30 cast/bitcast 目标 | `tc_parser_rhs.c:848-852/912-917` 语法期接受完整 type（struct/memblock/bool 交语义报 `TC_CE_TYPE_MISMATCH`）；`:1350-1355` const cast 恢复接受 ptr 目标 | `tests/errors/static/bitcast_struct.tc`（改为语义码）、`tests/valid/let_ptr_cast_nullptr.tc` |
| FP-2.11 | M-25 memblock count-only | `tc_parser_rhs.c:352-355` 要求 count 后必须跟 `fill:` 或 ≥1 元素 | `tests/errors/lexical/memblock_count_only.tc` |
| FP-2.12 | M-26/M-27 UTF-8/NUL | `tc_lib.c` 读取层加 UTF-8 合法性校验（含注释）；`tc_compile_source` 字符串路径补 NUL 扫描 | `tests/errors/lexical/invalid_utf8_comment.tc`、`embedded_nul.tc` |

**状态（2026-08-29）**：P2 已落地，并经 `bash scripts/run_tests.sh` 全绿（VM 872 / AOT 执行 437 / Unit，含 check-embed*）及四个 `check_*.py`。词法/语法/const 求值接受集向 0.0.41 语言标准收敛；`padding` 不再是关键字；浮点 `1.`/`.5` 词法与 `read` 均拒绝；`let` 可 const 求值 `ptr_size` 与 memblock 构造；限定名可用于 memblock N/`count:`/`read` 目标（`static var` 的命名 N 在 Pass2 intern 前折叠，避免 AOT 按 count=0 分配）；funcall/struct 字段不再超收完整 rhs（字段值仅为 operand / memblock 构造 / struct 构造）；比较一律拒绝模式关键字；模块五层严格序（IMPORT→STRUCT→VALUE→FUNC→EXEC）；cast/bitcast 非法目标改语义码；count-only memblock 拒绝；源文件 UTF-8/NUL 在读取层拒绝。

---

## 5. P3：错误码对齐

本阶段依赖 P0 附录 B 定稿（见 §2.1 裁决表）。

| 任务 | 发现 | 改动 | 测试 |
| ---- | ---- | ---- | ---- |
| FP-3.1 | S-8 | `tc_func_check.c:852-861` 同名 func 改报 `TC_CE_FUNCTION_NAME_CONFLICT`；删 `TC_CE_DUPLICATE_FUNCTION`（枚举/命名/白名单/测试） | `tests/errors/static/duplicate_function.tc` 期望码更新 |
| FP-3.2 | S-10 | `tc_analyzer_pass2.c:420` 跨函数标签改报 `TC_CE_LABEL_NOT_FOUND`；删 `TC_CE_CROSS_CONTROL_FLOW_JUMP` | `goto_cross_function_label_not_found.tc` 期望码更新 |
| FP-3.3 | S-24 | `tc_parser_rhs.c:549-551` 及 11 处调用点：无模式位置者 → `TC_CE_SYNTAX`；const_shift 非法组合 → `TC_CE_MODE_MISMATCH`；删 `TC_CE_KEYWORD` | `bitwise_*_keyword_error.tc`、`cast_wrap_keyword.tc` 期望码更新 |
| FP-3.4 | M（死代码） | 删 `TC_CE_ARGUMENT_TYPE`（枚举 + `tc_error_kind_name` + test_types 白名单 + 误导用例）；**同提交删除 `tc_func_check.c:257-260` 不可达分支**（首个 `if` 已对 `TYPE_MISMATCH` 提前 return，第二个 `if (TYPE_MISMATCH)` 恒不可达；删枚举后该引用会编译失败） | 删除 `argument_type.tc` 或改为字面量路径 |
| FP-3.5 | S-17/S-18 | `tc_lexer.c:377-381`（浮点 u 后缀）、`:271-276`（`-42u`）改报 `TC_CE_SYNTAX` | 新增 `float_unsigned_suffix.tc`、`negative_unsigned.tc` |
| FP-3.6 | S-21/S-22/S-23 | 按裁决表执行：`ELSE_POSITION` **改报 `TC_CE_INDENT_ELSE_END`**（`tc_parser.c:1018`）并更新对应测试；`MISSING_END`/`OPERAND_COUNT` 标准补码后**保留**，仅同步阶段标注 | `indent_else_position.tc` 期望码更新 |
| FP-3.7 | M-1 | `tc_analyzer_pass2.c:635-643` 调换顺序（先 `tc_check_operand` 后 `tc_check_io_format`） | `diag_priority_format_after_operand.tc` |
| FP-3.8 | 收尾 | `python3 scripts/sync/check_doc_counts.py`、`check_rhs_coverage.py` 同步；`tc_error_kind_name` 与枚举 1:1 重新核对 | CI |

**状态（2026-08-29）**：P3 已落地。实现枚举 **86** = 85 语言码 + 1 `TC_ERR_OUT_OF_MEMORY`；已删除 `DUPLICATE_FUNCTION` / `CROSS_CONTROL_FLOW_JUMP` / `KEYWORD` / `ARGUMENT_TYPE` / `ELSE_POSITION`。同名 func → `FUNCTION_NAME_CONFLICT`；跨函数 goto → `LABEL_NOT_FOUND`；无模式位置的 wrap/truncate/ieee → `SYNTAX`，const_shift 非法组合 → `MODE_MISMATCH`；`-42u`/`1.5u` → `SYNTAX`；错位 else → `INDENT_ELSE_END`；write 先操作数后格式符。

---

## 6. P4：边界/格式/可移植性

本阶段依赖 P1。FP-4.1 已并入 P1（与 FP-1.5 同阶段），本表保留编号以便对照发现项。

| 任务 | 发现 | 改动 |
| ---- | ---- | ---- |
| FP-4.1 | S-4 / M-3 / M-4 | **并入 P1**（与 FP-1.5 同阶段），详见 P1 |
| FP-4.2 | M-2 | `tc_sem_bitwise.c:107-145` 负边界改为 `abs_val > (1ULL<<63)/pow2` |
| FP-4.3 | M-5 | memblock 头宽：**本计划取「声明 64-bit-only + 编译期断言」**；`usize` 头宽抽象/32 位目标支持另行立项，不在本计划范围 |
| FP-4.4 | M-6 | `nan` 字面量位模式硬编码 `0x7FC00000`/`0x7FF8000000000000`（`tc_lexer.c:548`、`tc_semantics.c:366-379`、`tc_aot_codegen.c:427-439` 对 `isnan` 特判） |
| FP-4.5 | M-8/M-9 | 有符号右移显式算术实现（M-8，本阶段必做）；memblock/struct 元素存取改位级构造、大小端无关（M-9，**默认降级、不在本阶段必须交付**——仅大端主机可观察，当前 x86/ARM 小端不受影响；降级后发布结论须显式标注"未完全符合 §3.5 行 438"） |
| FP-4.6 | M-10 | 浮点十进制输出**主路径自实现** roundTiesToEven + 两位指数（§10.4 行 2285-2286）；"文档化依赖 glibc"仅作未落地前的临时缓解说明、不构成符合性结论（**默认降级、不在本阶段必须交付，须显式标注未完全符合 §10.4**）；P1 的 FP-1.5 落地后若仍要做满，作为本阶段可选升级 |
| FP-4.7 | M-11/M-12 | `memcopy_unsafe` 负下标改报 `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`；`tc_umul64` 修正 hi 位或删除死代码 |
| FP-4.8 | AT-10 / N-8 | `tc_embed.c:347-353` AOT 模式 `slot_read` 从 `TcTypedProgram` 恢复槽位类型（支撑 P1 的 FP-1.7 bool 边界规范化） |

**降级项（已定）**：FP-4.5 的 M-9（端序）与 FP-4.6（自实现 printf）**默认降级**，不计入本计划必须交付项。降级项在发布结论中按 §0「发布结论口径」显式标注未完全符合的条款，不再单独判定"是否清零"。

**状态（2026-08-30）**：P4 必做项已落地。FP-4.2 负 `shl` 边界改为 `(1ULL<<63)/pow2`；FP-4.3 本实现声明 64-bit-only 并加 C99 编译期断言；FP-4.4 `nan`/`inf` 字面量使用硬编码 canonical 位模式；FP-4.5 M-8 有符号右移改为显式算术实现（M-9 端序按计划降级）；FP-4.6 自实现 printf 按计划降级；FP-4.7 `memcopy_unsafe` 负下标（含 VAR 操作数）改报 `TC_RE_MEMCOPY_UNSAFE_INVALID_RANGE`（VM 按有符号类型解码；AOT emit 从符号表取下标类型），`tc_umul64` 高位已修正；FP-4.8 AOT `slot_read` 从 `TcTypedProgram` 恢复槽位类型。发布结论须显式标注未完全符合 §3.5 行 438（端序）与 §10.4（自实现 printf）。

---

## 7. P5：设计文档同步

本阶段依赖 P0+P3 定稿。

| 文档 | 修订项 |
| ---- | ------ |
| `TC编译器标准设计说明书-0.0.41.md` | §11.4.2 按裁决表增删码；§3.2 形参 ptr_store 码统一为 `CONSTANT_ASSIGNMENT`；3 处章节引用（§9.2.1→§9.2、§7.4→删除、§7.3.3→§7.3.2）；§11.4 改为**镜像附录 B（85 码）**，删除"编译器专用语言码"表述（补码后即为语言码） |
| `TC-VM详细设计说明书-0.0.41.md` | §15.2 错误码表同步；§12.4 引用改 §3.7/§3.10.9；补 bool 规范字节/浮点 mod/移位 k≥n/ptr 步长/I-O 原子提交 6 处 |
| `TC-VM命令行参考-0.0.41.md` | ~~91 码口径注明~~（**已随 P3 改为 86 码**）；writeln LF 说明 |
| `TC-AOT详细设计说明书-0.0.41.md` | §12.3 优先级改"无效→除零"；§10.3 改 memcpy；§7 返回机制改专用返回标记；memblock 头 64-bit 标注；CLI 补 `-I` |
| `libtc设计说明书-0.0.41.md` | ~~§2.1 API 声明修复~~（**已修复并并入 §15，2026-08-29**）；错误码分类计数（47/8/2/12）修正（§15.8）；"16 个 TC_RE"→12 |
| `TC-Embed详细设计说明书-0.0.41.md` | bool 规范化三路径；static let 不占槽；ptr_add 位粒度步长；slot_read 类型；错误码映射 |
| ~~`设计实现合规审查报告-0.0.41.md`~~ | **已删除（2026-08-29）**——原"91→71+19 口径 / 替换过时码 / 标注被取代"等修订随文档删除一并关闭 |
| ~~`TC-0.0.41-开发计划.md`~~ | **已删除（2026-08-29，老版本开发计划）**——原"错误码名称对齐 / 补 TC-Embed 覆盖"随文档删除一并关闭；「RHS 分发覆盖」计数源已迁至 `.cursor/skills/tc-architecture/types.md` |
| ~~`TC-0.0.41-结构体字段operand修复计划.md`~~ | **已删除（2026-08-29）**——原 M-16 措辞修订随文档删除一并关闭 |
| `.cursor/skills/tc-architecture/errors.md` | 补齐到 85 语言码（另记 1 个 `TC_ERR_OUT_OF_MEMORY`）+ 标准章节对应 |

**状态（2026-08-30）**：P5 已落地。编译器标准 §11.4 镜像附录 B（85 码），形参直接赋值/`read` → `PARAMETER_ASSIGNMENT`、经 `ptr_address` 的 `ptr_store`/`memcopy_unsafe` → `CONSTANT_ASSIGNMENT`；语言标准章节引用已改为 §9.2 / §7.3.2，CFG 诊断锚定本节 §7.6。VM 详设 §15.2 去掉 `DUPLICATE_FUNCTION` 并补 `STRUCT_VALUE_SELF_REF`，§12.4 与 §15.5 六处语义重述已齐。CLI 为 86 码口径且写明 `writeln` 单 LF。AOT 详设浮点异常优先级、结构体/memblock 头 `memcpy`、专用返回标记、64-bit 头宽、`-I` 与 embed `int` 返回已对齐实现；§19 记录可移植性债务。libtc 分表计数改为 44/17/4/8/10/2（`TC_RE` 全集 12），阶段 4 补结构体表、阶段 13 标 VM/AOT。Embed 详设统一 abort 为「inline + `return 1` 宏」、补 I/O 语义与语言码映射、bool 三路径。`errors.md` 按附录 B 列出 85+OOM。N-9/N-11 随已删过程文档关闭；N-12/N-13 按计划记为债务（不强制本阶段改实现）。

> **N 级逐条核对清单**（本阶段兜底，随各文档同步收敛；下表为「覆盖处 / 处置」而非新增任务，逐条勾销）：
>
> | N 项 | 处置 |
> | ---- | ---- |
> | N-1/N-2/N-3 编译器标准章节引用（§9.2.1→§9.2、§7.4→删除、§7.3.3→§7.3.2） | **已勾销**：语言标准引用改为 §9.2 / §7.3.2；CFG 诊断锚定编译器本节 §7.6 |
> | N-4 VM 详设 §12.4 指针 cast 引用 | **已勾销**：§3.7 / §3.10.9 |
> | N-5 VM 详设 6 处未重述 | **已勾销**：§15.5 |
> | N-6 CLI writeln LF | **已勾销**：§3 / §5.4 |
> | N-7 libtc 设计阶段图（阶段 4 漏结构体表注册、阶段 13 只标 VM） | **已勾销**：阶段图 4c→结构体表→4d；阶段 13 = VM 执行 / AOT 代码生成；AOT CLI `-I` 已有 |
> | N-8 Embed 详设 abort 两形态矛盾、I/O 语义未讨论 | **已勾销**：§15.3.2 权威形态 + §5.6 I/O + §10.3 语言码映射 |
> | N-9 开发计划「70+ 错误码」→71、`MEMBLOCK_INDEX_OUT_RANGE` 缺 `_OF_` | **已随开发计划删除关闭** |
> | N-10 errors.md 仅 29 码 | **已勾销**：按附录 B 列出 85 语言码 + OOM |
> | N-11 旧合规报告口径 | **已随旧合规报告删除关闭** |
> | N-12 实现可移植性：slot_read 类型 → FP-4.8；FENV 下溢启发式 / `ptr_add/sub` 运行期回退 / `tc_aot_memblock_alloc` 缺饱和 / AOT `.count` 死代码回退 | **已记录**：AOT 详设 §19「已知可移植性债务」，不在本计划强制范围 |
> | N-13 funcall 多余实参落入 `ARGUMENT_ORDER`（细分不足） | **已记录**：编译器标准 §8.2 注明不另设「多余实参」码；`ARGUMENT_TYPE` → FP-3.4 |

---

## 8. P6：测试补齐与门禁固化

随各阶段补测；本阶段收口门禁与黄金集。

1. **黄金输出**：`format_spec_*.tc`（§10.4/§10.5 全表）、`fp_mod_*.tc`（§6.3.7 全表）、`let_ptr_size.tc`、`let_memblock_const.tc`、`qualified_*`、`read_float_invalid.tc`、`memblock_copy_overflow_guard.tc`、`ptr_store_through_param.tc`。
2. **AOT 差分**：所有新 `tests/valid/` 用例纳入 `scripts/aot/run_tests.sh` 差分；重点 `self_member_*_copy`、`fp_mod`、`format_spec`。
3. **embed**：`check-embed`/`check-embed-aot` 增加 bool 规范化 + 可失败 static var 用例。
4. **门禁固化**：把语言标准 §10.2 顺序、错误码唯一性（86 码白名单）、`%0008d` 合并、`memblock count:0` 等写入 `test_types` 白名单 + `check_doc_counts.py` 的数字源。
5. **回归命令**：`bash scripts/run_tests.sh`（全量）、`make test-unit`、`cmake --build build --target check-embed check-embed-aot`、四个 `scripts/sync/check_*.py`。

**状态（2026-08-30）**：P6 已落地。黄金集覆盖 §10.4/§10.5（`format_spec_*` + `format_width_max`）与 §6.3.7（`fp_mod_*` 有限余数 / ieee NaN / ±inf / −0 / invalid 优先于 div0）；`let_ptr_size` / `let_memblock_const` / `qualified_*` / `read_float_invalid` / `memblock_copy_overflow_guard` / `ptr_store_through_param` / `self_member_*_copy` 已入 VM+AOT。embed：`check-embed`/`check-embed-aot` 含 bool 规范化与可失败 static var。门禁：`test_types` 86 码唯一 + `%0008d` 合并 + `%65535d` 上限 + intern `count=0` 哨兵；`test_type_check` 锁定 §10.2 操作数优先与命名 N 折叠（intern 后 `count != 0`）；类型位置 `memblock<T, 0>` 语法拒绝；`check_doc_counts.py` 以语言标准附录 B 唯一码 = 85 且实现 86 = 85+OOM 为数字源。

---

## 9. 风险与注意事项

1. **浮点 `mod`** 是唯一涉及数学语义的新实现，务必用 §6.3.7 的数学域算法（`trunc(a/b)` 整数商）而非"先除再乘"，避免舍入/溢出改变余数；需与 IEEE `fmod` 的负零/无穷/NaN 边界做逐条对照测试。
2. **错误码删除**（DUPLICATE_FUNCTION/CROSS_CONTROL_FLOW_JUMP/KEYWORD/ARGUMENT_TYPE/ELSE_POSITION 共 5 码）会波及测试黄金集与 `errors.md`/`check_doc_counts.py` 的数字源，务必同一提交内完成"枚举 + 命名 + 白名单 + 测试 + 文档"五处同步；**附录 B（85 码）与 `check_doc_counts.py` 的数字源必须同一提交**，避免 `check_doc_counts.py` 报错；**禁止在未同步修订附录 B 的提交里单独改动 `check_doc_counts.py` 的数字源**（防止用"改脚本"掩盖"改标准"）。
3. **const 求值补 ptr_size/memblock** 涉及 `tc_const_eval.c` 与 `tc_parser_rhs.c` 两处联动，需保证 const 与运行时求值复用同一语义核心（与 §5.2.1/§K 系列原则一致）。
4. **格式化渲染**改动集中在 `tc_io.c`，但签名变化会牵动 executor 与 AOT shim（`tc_aot_write` 委托同一函数），需一并改。
5. 所有改动遵循 AGENTS.md 效率原则：先 `rg` 定位、改后最小回归（`--filter`）、动 `TcRhsKind`/错误码后跑 `check_rhs_coverage.py` / `check_doc_counts.py`。
6. **黄金输出变更会批量改写 `.out`**：格式化（FP-1.5/FP-4.1）与错误码（FP-3.x）修复会大量更新 `tests/**/*.out` 与错误码期望；须用脚本/一次性重生成并人工抽查关键用例，不得手工零星改。
7. **AOT/VM 差分必须同批**：凡影响语义/输出的修复（FP-1.2、FP-1.4、FP-1.5、FP-2.x），同一提交内更新 VM 与 AOT 两条路径及差分黄金，避免单侧遗漏。

---

## 10. 收口结论（2026-08-30）

P0–P6 均已落地。分析报告 §1–§13 仍是 2026-08-27 审计原文；现行对外结论以本节与分析报告 §14 为准。

**发布结论：S 级符合；M 级除标注的可降级项外收敛；可移植性例外见 P4。**

不得再写旧合规报告式的笼统「通过」。

| DoD | 结果 |
| --- | ---- |
| 硬目标 ① 30 项 S 级清零 | 达成 |
| 硬目标 ② 附录 B = 85 且与实现 1:1（+1 OOM → 86） | 达成 |
| 硬目标 ③ `run_tests.sh` / `make test-unit` / `check-embed*` / 四个 `check_*.py` | 达成（VM 892 / AOT 执行 456 / Unit 全绿，2026-08-30） |
| 软目标：M 级除降级项外清零 | 达成 |
| 降级（已定，须明示） | **FP-4.5 M-9**：memblock/struct 标量元素按宿主端序存取，未完全符合语言标准 §3.5；**FP-4.6**：浮点十进制输出仍委托宿主 `snprintf`，未完全符合 §10.4 |
| 不在本轮强制范围 | N-12 可移植性债务（AOT 详设 §19）；N-13 funcall 多余实参并入 `ARGUMENT_ORDER`（编译器标准 §8.2） |

*本计划与检查分析报告配套；行为权威为语言标准 0.0.41（含 P0 附录 B 85 码）。*
