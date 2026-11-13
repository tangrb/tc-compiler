# TC 0.0.46 S0 门禁报告（G0）

> **阶段**：S0 契约冻结与测试基建（对应设计 §13 P0）
> **依据**：[开发测试计划](TC开发测试计划-0.0.46.md) §5 S0、§7、附录 D；
> [标准](TC语言及编译器标准-0.0.46.md) §12/§13；[设计](TC原生编译器设计说明书-0.0.46.md) §2–§8/§12。
> **执行方式**：本会话无人介入（agent 执行）。契约对照为自动化/逐条文档核对；
> 独立人工评审未发生，见 §6。
> **修订说明（2026-11-13 第三次复核）**：F1（B4 文档 + B2 L0 钉扎）与 F2（B2
> SAFE_VIOLATION 排序键/用例）已按 §7 关闭，并重新生成码表/覆盖矩阵。G0 仍
> **未签署**，唯一剩余条件是独立人工契约评审。上一版对 F1 的 B1 撤回仍然有效——
> 比较器键序符合标准 §12.2 #9，F1 的残留问题不是比较器语义错误。

## 1. 结论

| 项 | 结果 |
| ---- | ---- |
| S0 开发交付 | 完成（契约头文件、`tc_core`、两个 CLI 骨架） |
| S0 测试活动 | 工程测试活动完成；F1/F2 已关闭，剩余 §4 PARTIAL 为 `.tc`/VM/AOT 后续阶段项 |
| 自动化证据 | **PASS**：Debug、Release、ASan/UBSan 三套构建各 `ctest` 10/10；harness 15/15；码表三向一致 |
| 契约语义审计 | F1 = **CLOSED**（B4 文档 + B2 L0 已补）；F2 = **CLOSED**（rank + L0 + 覆盖行）；无未关闭 B1/B2/B3 |
| 独立人工契约评审 | **未执行**（本会话无人介入） |
| **G0 门禁** | **未签署（NOT SIGNED）**：唯一剩余为独立人工契约评审 |
| 进入 S1 | **暂缓**，直至独立人工契约评审签署 |

> 计划 §7.3：B2 阻塞门禁、须补齐用例；B4 门禁前修复；B1 不得豁免。
> 计划 §5：前一阶段 G 门禁通过才进入下一阶段，允许同一阶段内迭代。
> 因此“自动化证据 PASS”与“G0 已通过”必须分开表述。

## 2. 环境与可重复性

| 项 | 值 |
| ---- | ---- |
| 宿主 | macOS arm64（Apple Silicon） |
| C 编译器 | Apple clang 21.0.0.21000334 |
| CMake | 4.4.2 |
| Python | 3.14.7（仅标准库，无网络） |
| 构建 | Debug（`build`）、Release（`build-release`）、ASan/UBSan（`build-sanitize`：Debug + `-fsanitize=address,undefined -fno-omit-frame-pointer`）；三套各 `ctest` 10/10 |
| 运行 | 本阶段无需 qemu/ELF；AOT 运行验证从 S7 开始 |

固定环境：测试进程使用 `LC_ALL=C`、`LANG=C`、`TZ=UTC`，固定 stdin，无网络，每个用例独立临时目录。
本轮实测：

- Debug/Release/ASan-UBSan 三套构建各 `ctest` 10/10；Release 下 `ctest -j8` 并行通过；
- `ctest --repeat until-fail:3` 通过；
- `coverage.py --out-dir` 重新生成结果与仓库文件逐字节一致；
- harness 端到端 JSON 报告连续 3 次 SHA-256 一致；
- macOS `leaks --atExit` 对 `tc_contract_test`/`tc_dump_codes` 报告 0 leaks。

## 3. 交付物与证据

| S0 交付 | 位置 | 证据 |
| ---- | ---- | ---- |
| 类型契约 | `src/ir/type.h`、`type.c` | intern/单例/冻结、`isize`≠`int32`、W 位宽；L0 `contract.unit` |
| IR 契约 | `src/ir/{operand,instr,block,ir,span}.h`、`instr.c` | 51 opcode、无结果指令 LHS、显式 CFG、命名实参、span |
| 对象/区契约 | `src/ir/{region,object,address,structs}.h` | 五区、对象表、地址 0 保留 |
| 档案契约 | `src/runtime/profile.h`、`profile.c` | 两个 64 位内置档案；W=32 档案校验（测试本地 target） |
| 运行时四缝 | `src/runtime/{io,alloc,trap,module_loader}.h/.c` | I/O all-or-nothing、短读/EOF/错误、目标 W 分配器、trap 首错、设备端不解析 import |
| 诊断契约 | `src/diagnostic/{codes,diagnostic,table}.{h,c,inc}` | 80 码机读表、首诊比较器、缓冲、OOM 独立通道 |
| 零 pass 契约 | `src/contract/optimizer.h/.c` | O0/O2 均 0 pass、`@fast` 不消费、`@inline` 只保存 |
| 构建契约 | `src/CMakeLists.txt`、`src/driver/` | 私有 `tc_core`；仅 `tc-vm`/`tc-aot`；无 install 规则 |
| 用例 harness | `tests/harness/` | 附录 C YAML 子集、7 判定器、超时/捕获/归一化、失败关闭 |
| 码表/覆盖 | `tests/traceability/` | `codes.json`、生成的 C 表、630 个覆盖槽、227 项（1 L0 covered / 629 uncovered） |
| golden 评审演练 | `tests/golden/` | 推导说明 + 评审记录 + 机器校验 |
| 契约索引 | `src/CONTRACTS.md` | 属主、冻结标记、后续阶段边界 |

## 4. 附录 D.1 通用门禁

| 检查项 | 结果 | 证据 |
| ---- | ---- | ---- |
| 80 码表未增删；码名/阶段/触发与标准一致 | PASS | `trace.codes`：65 CE + 15 RE，码名/打印名/阶段/C 表逐行一致 |
| 新增/修改用例有 `spec` 与独立评审 | PARTIAL | 样例 golden 有 §2.1/§2.2/§12.3/§12.5.1 推导与评审记录；评审人是演练占位（§6） |
| 覆盖矩阵已更新；未覆盖/挂起解释 | PASS | 630 槽：1 个 L0 covered（`same_site_priority`），629 个显式 `uncovered`；无静默通过 |
| 首诊/位置断言覆盖本阶段相关码 | PARTIAL | 比较器单测覆盖阶段/模块/源序/专用码/条款序/遍历无关；F1 的 `origin×rank` 合成用例已补（`TC-L0-DIAG-0001`）；`.tc` 用例从 S1 开始 |
| 32/64 期望分档案；无跨档案比较 | PARTIAL（结构级） | `golden/common` 仅 W 无关；`golden/w32`、`w64` 已建目录；覆盖矩阵含 12 个 W 敏感码的 w32/w64 槽；真实分档案期望从 S1 起 |
| 确定性套件（重复/哈希/线程/顺序） | PARTIAL（S0 范围） | 三套构建与重复运行一致；harness 固定环境、排序输出、无时间戳；哈希/线程/顺序扰动属于 S1+ 真前端用例，尚未覆盖 |
| 无新增警告；无标准之外诊断 | PASS | `-Wall -Wextra -Werror -Wpedantic`；`tc_diag_contract_violation()` 检查唯一码/打印名/阶段 |
| 构建/消毒器/符号审计 | PASS | ASan+UBSan 10/10；`tc-vm` 仅导出 `_main`；无 install 规则；无公共 ABI；`leaks` 0 |
| 门禁报告记录 profile/工具链版本 | PASS | 本文件 §2 |

## 5. S0 专项门禁（计划 §5 S0 出口准则）

1. **契约评审与唯一属主**：**PARTIAL**。`src/CONTRACTS.md` 已给出头文件、属主、冻结事实与消费者，`tc_contract_check()` 在工具启动时强制检查；F1/F2 已关闭，但独立人工评审未签。
2. **80 码机读校验**：**PASS**。`tests/traceability/codes.json` 由标准 §12.3/§12.5.1 与计划附录 A 生成；`check_codes.py` 重新解析标准与 C 表 dump 比对；计数 65+15，12 个 W 敏感码，5 个 IMPORT 报告码。
3. **harness 失败关闭**：**PASS**。`tests/harness/selftest.py` 15 个场景全部通过，覆盖：正确期望通过、错误码失败、漏报、额外诊断、位置错误、超时、adapter 崩溃、`skip` 被拒、`not-implemented` 不计通过且门禁非零、地址归一化、YAML/模式校验。
4. **覆盖矩阵可报告、未覆盖可见**：**PASS**。630 槽、227 项（1 L0 covered / 629 uncovered）；分类：标准章节 15、诊断码 80（344 槽）、opcode 63（155 槽）、设计 §12 15、首诊规则 §12.2 10、同位置优先级 1（covered）、32/64 翻转 17、注解 16、设备策略 4、I/O 源状态 4、退出策略 2。
5. **无实现语义承诺**：**PASS**。`tc-vm`/`tc-aot` 对合法调用显式返回 `tc_error_unavailable`（退出码 70），不产出工件、不打印成功；README/AGENTS 明确前端/VM/后端未实现。

### 5.1 零 pass 契约

- `tc_pipeline_build(O0|O2).pass_count == 0`，`tc_pipeline_levels_equivalent()` 为真；
- `@fast`/`@fast_math`/`@inline`/`@noinline` 仅保存为 `TcFuncFlags`；
- 未引入 SSA/regalloc/内联。

### 5.2 档案字段

- 内置 `aarch64-linux-user`（W=64 hosted）与 `aarch64-baremetal`（W=64 freestanding）通过结构校验；
- W=32 档案以测试本地 target 验证：W∈{32,64}、五区不重叠且不含地址 0、`D≥1`、`disable_icf==1`、栈/指针对齐、入口在代码区、32 位 `int64`/`float64` 双字/软浮点标志；
- 负例覆盖：W 非法、端序不符、`D=0`、`disable_icf=0`、区重叠/含 0/越界、入口越界、ABI 对齐不符、checked 值非法、缺 I/O 缝。

## 6. 挂起项与限制（显式，不计为通过）

| 项 | 状态 | 关闭阶段 |
| ---- | ---- | ---- |
| 独立人工契约评审/签署 | **未执行**（本会话无人介入；当前为自动化对照 + 文档核对） | G0 签署前 |
| F1/F2 关闭状态 | **CLOSED（2026-11-13）**，证据见 §7；不再挂起 | — |
| golden 评审人真实身份 | 演练占位 | S1 首个真实 golden 前替换 |
| 真实 `.tc` LEX/SYN/IMPORT/SEM/CT 用例 | 未实现（覆盖矩阵全部 `uncovered`） | S1–S5 |
| VM 执行与 golden | 未实现 | S6 |
| AArch64 后端/工具链/差分 | 未实现 | S7 |
| 32 位目标选型与后端 | 未实现（仅档案契约与本地校验） | S9 |
| ASan 泄漏检测 | 宿主 macOS ASan 不做 `detect_leaks`；已用 macOS `leaks --atExit` 复核两个 C 测试二进制，均 **0 leaks / 0 total leaked bytes** | Linux CI 支持时补 |
| harness 真实 adapter | 未实现（S0 只验证 harness 自身） | S1 |

> 注意：§7.4 只允许挂起“尚未实现的能力”，不适用于已知契约/测试缺口。
> F1/F2 已于 2026-11-13 按 §7 关闭，不再挂起；当前唯一 G0 签署阻塞项是独立人工评审。

## 7. 契约语义审计发现（2026-11-13 复核修订）

> 来源：本轮对冻结契约的语义审计（agent 执行），非计划/标准文本。执行证据：
> 三套构建各 `ctest` 10/10；`tests/harness/selftest.py` 15/15；CLI 合法调用退出 70；
> macOS `leaks` 复核 C 测试 0 leaks。
> **分级**：F1 的比较器键序**无 B1**；残留为 B4 + B2。F2 为 B2 覆盖缺失。
> 两项已于 2026-11-13 按 §7 关闭；下面保留问题分析与关闭证据。

### F1 首诊排序文档表述与 L0 钉扎不足（比较器键序正确）—— B4 + B2

| 项 | 内容 |
| ---- | ---- |
| 位置 | [src/diagnostic/diagnostic.c:139-151](../src/diagnostic/diagnostic.c#L139)（比较器，正确）、[src/diagnostic/diagnostic.h:3-11](../src/diagnostic/diagnostic.h#L3)（顺序注释，正确）、[设计 §6:440](TC原生编译器设计说明书-0.0.46.md#L440)（摘要表述） |
| 测试缺口 | **已补**：[tests/unit/contract_test.c](../tests/unit/contract_test.c) 的 `test_diag_origin_rank_conflict`（`TC-L0-DIAG-0001`） |
| 状态 | **CLOSED（2026-11-13）**：B4 文档 + B2 L0 已补 |

**比较器键序正确，B1 已撤回**：标准 §12.2 #9（[line 1705](TC语言及编译器标准-0.0.46.md#L1705)）是特定操作规则——同位置“操作数内部诊断”先于“构造级诊断”，例外仅 `call` 调用安全（§8.8.2）。#4（[line 1700](TC语言及编译器标准-0.0.46.md#L1700)）的第三例也明确 `CONDITION_TYPE` 不抢先于条件操作数内部码。因此同位置先按 #9 分 origin，再在同 origin 内按 #4 rank；比较器 `origin → rank → clause` 的键序符合标准。

设计 §6（[line 440](TC原生编译器设计说明书-0.0.46.md#L440)）以编号箭头罗列 #4→…→#9/10，容易被误读为扁平字典序；但 #6/#7/#8 是归属/生成约束，不是比较键。该处是**文档表述风险（B4）**，不是实现偏差。

原初审的 `RETURN_TYPE` vs `TYPE_MISMATCH` 可达反例也不成立：标准 §8.3（[line 1351](TC语言及编译器标准-0.0.46.md#L1351)）规定 `return` 操作数类型不符 → `TC_CE_RETURN_TYPE`；二者在 return 位置不共存。

**残留 B2（已关闭）**：修复前 `test_diag_ordering` 只分别测试“同 rank 的 origin 优先”和“同 origin 的 rank 优先”，没有 `origin×rank` 反向冲突的合成用例。已补的合成用例：

> `TYPE_MISMATCH(OPERAND, rank 1)` 与 `RETURN_TYPE(CONSTRUCT, rank 5)` 同位置 → **`TYPE_MISMATCH` 胜**（origin 支配 rank）。

该场景是**比较器合成测试**，不代表 `return` 位置存在真实双候选（§8.3 已使二者互斥）。

**关闭记录（2026-11-13）**：

1. B4：设计 §6 首诊选取条目已补注记——编号列表是规则索引、#6/#7/#8 非比较键、同位置先 #9 后 #4；`src/CONTRACTS.md` 同步记录键序。
2. B2：`tests/unit/contract_test.c` 增加 `test_diag_origin_rank_conflict`（`TC-L0-DIAG-0001`），断言 `TYPE_MISMATCH(OPERAND, rank 1)` 胜 `RETURN_TYPE(CONSTRUCT, rank 5)`。
3. 比较器键序未改，未发起标准澄清。

### F2 SAFE_VIOLATION 同位置优先未进排序键 —— B2（已关闭）

| 项 | 内容 |
| ---- | ---- |
| 位置 | [tests/traceability/codes.json](../tests/traceability/codes.json)（修复前四码 rank=0；现 `SAFE_VIOLATION`=20）、[src/diagnostic/diagnostic.c:153](../src/diagnostic/diagnostic.c#L153)（enum 平局）、[src/diagnostic/diagnostic.h:70-72](../src/diagnostic/diagnostic.h#L70)（override 声明，字段见 [line 90](../src/diagnostic/diagnostic.h#L90)） |
| 状态 | **CLOSED（2026-11-13）**：rank=20 + L0 用例 + 覆盖矩阵专门行 |

**事实**：标准 §12.3（[line 1794](TC语言及编译器标准-0.0.46.md#L1794)）明写 `SAFE_VIOLATION`「优先于 `UNSAFE_*`/`FAST_CALL`」（§9.3 [line 1482](TC语言及编译器标准-0.0.46.md#L1482)、§9 [line 1522](TC语言及编译器标准-0.0.46.md#L1522) 重申）。修复前四码 `same_site_rank=0`，同位置共存时 enum 平局会选 `UNSAFE_OPERATION`；该优先仅以自由文本 trigger 承载。修复后 `TC_CE_SAFE_VIOLATION.same_site_rank=20`，其余三个 `UNSAFE_*`/`FAST_CALL` 保持 0，比较器在同 origin 类内会选 `SAFE_VIOLATION`。

**修复前为何不能按 §7.4 挂起**：

- F2 不是“尚未实现的功能”，而是冻结契约中**已知的排序规则/测试缺口**；§7.4 只适用于 `not-implemented` 能力。
- 覆盖矩阵现在有 `same_site_priority:SAFE_VIOLATION_OVER_UNSAFE:priority` 专门行，并由 `TC-L0-DIAG-0002` 标记为 covered。
- 标准 §9.1/§9.3 使四码在正确 emitter 下按上下文互斥（`@safe` 内一律 `SAFE_VIOLATION`，其余报 `UNSAFE_*`/`FAST_CALL`），因此修复前**没有既成行为偏差**；但冻结契约仍需排序键与回归测试，现已补齐。

**关闭记录（2026-11-13，采用 A）**：

- 在 `tests/traceability/gen_codes.py` 的 `SAME_SITE_RANK` 为 `TC_CE_SAFE_VIOLATION` 编码 `20`，重生成 `codes.json`/`codes.inc`/`table.c`；
- `tests/unit/contract_test.c` 增加 `test_safe_violation_priority`（`TC-L0-DIAG-0002`），覆盖同位置四码候选、并验证 §12.2 #9 origin 仍支配 rank；
- 覆盖矩阵新增 `same_site_priority:SAFE_VIOLATION_OVER_UNSAFE:priority` 行，状态 covered，case id `TC-L0-DIAG-0002`。

## 8. 门禁判定

**自动化工程证据：PASS**。三套构建各 `ctest` 10/10、harness 15/15、码表三向一致、
覆盖矩阵 630 槽（1 L0 covered / 629 uncovered）、CLI 合法调用退出 70、macOS `leaks` 0。

**F1/F2：CLOSED（2026-11-13）**：

- F1-B4：设计 §6 注记 + `src/CONTRACTS.md` 键序说明已补；
- F1-B2：`TC-L0-DIAG-0001` 合成比较器用例已补；
- F2-B2：`TC_CE_SAFE_VIOLATION.same_site_rank=20`、`TC-L0-DIAG-0002`、覆盖矩阵专门行已补并重生成。

**G0：未签署（NOT SIGNED）**。唯一剩余原因是独立人工契约评审未执行。按计划 §7.3，
人工评审不是自动化测试可替代的治理项；其余出口准则已有可复现证据。

**进入 S1：暂缓**，直至独立人工评审签署并记录签署人/日期/证据版本；届时将本节改为
`G0 PASS`，再启动 S1。此前任何“G0 PASS、进入 S1 允许”的表述均无效。
