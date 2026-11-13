# TC 0.0.46 S0 门禁报告（G0）

> **阶段**：S0 契约冻结与测试基建（对应设计 §13 P0）
> **依据**：[开发测试计划](TC开发测试计划-0.0.46.md) §5 S0、§7、附录 D；
> [标准](TC语言及编译器标准-0.0.46.md) §12/§13；[设计](TC原生编译器设计说明书-0.0.46.md) §2–§8/§12。
> **执行方式**：本会话无人介入（agent 执行）。独立评审由独立只读代理
> `gate-auditor` 执行（非作者、未参与实现），不伪造人工签署。
> **结论**：**G0 PASS；允许进入 S1。**

## 1. 结论

| 项 | 结果 |
| ---- | ---- |
| S0 开发交付 | **PASS** — 契约头文件、`tc_core`、`tc-vm`/`tc-aot` 骨架、指针状态契约 |
| S0 测试活动 | **PASS** — L0 契约/类型/IR/对象/指针状态；harness 23/23；golden 1/1 + golden selftest；覆盖矩阵 634 槽 / 231 项 |
| 自动化证据 | **PASS** — Debug/Release/ASan+UBSan 各 `ctest` 11/11；harness 23/23；golden 校验通过；码表/覆盖检查通过；CLI 合法调用 70；leaks 0；`nm` 仅 `_main` |
| 契约审计 | **CLOSED** — 独立非作者 reviewer `gate-auditor`（read-only subagent，无实现参与），reviewed code commit `5ca7449`，evidence version `S0-2026-11-13`；无未关闭 B1/B2/B3/B4 |
| 独立评审 | **PASS** — `gate-auditor`；golden derivation 独立复核 |
| **G0** | **PASS** |
| 进入 S1 | **允许** |

> 计划 §7.3：B1 不得豁免，B2 须补齐用例，B4 须在门禁前修复。
> 计划 §5：前一阶段 G 门禁通过后才进入下一阶段。
> 本报告以通过状态签署，但保留后续阶段的显式 N/A/范围说明（§4、§6）。

## 2. 环境与可重复性

| 项 | 值 |
| ---- | ---- |
| 宿主 | macOS arm64（Apple Silicon） |
| C 编译器 | Apple clang 21.0.0.21000334 |
| CMake | 4.4.2 |
| Python | 3.14.7（仅标准库，无网络） |
| 构建 | Debug、Release、ASan/UBSan（Debug + `-fsanitize=address,undefined -fno-omit-frame-pointer`）；三套各 `ctest` 11/11 |
| 运行 | 本阶段无需 qemu/ELF；AOT 运行验证从 S7 开始 |

固定环境：`LC_ALL=C`、`LANG=C`、`TZ=UTC`，固定 stdin，无网络，每个用例独立临时目录。

本轮实测：

- Debug/Release/ASan-UBSan 三套构建各 `ctest` 11/11；
- Release 下 `ctest -j8` 并行通过，`ctest --repeat until-fail:3` 通过；
- `coverage.py --out-dir` 重新生成结果与仓库文件逐字节一致；
- harness 端到端 JSON 报告连续 3 次 SHA-256 一致；
- macOS `leaks --atExit` 对 `tc_contract_test`/`tc_dump_codes` 报告 0 leaks；
- `nm` 显示 `tc-vm`/`tc-aot` 除 Mach-O 固有 `__mh_execute_header` 外仅导出 `_main`，安装产物为 0 个文件。

## 3. 交付物与证据

| S0 交付 | 位置 | 证据 |
| ---- | ---- | ---- |
| 类型契约 | `src/ir/type.h`、`type.c` | 标量单例预创建、intern/冻结、`isize`≠`int32`、W 位宽、void 非标量值；L0 `TC-L0-TYPE-0001` |
| IR 契约 | `src/ir/{operand,instr,block,ir,span}.h`、`instr.c` | 51 opcode、`_` 丢弃规则（§6.15.1）、显式 CFG、命名实参、span；L0 `TC-L0-IR-0001`、`TC-L0-CONTRACT-0001` |
| 对象/区契约 | `src/ir/{region,object,address,structs}.h` | 五区、对象表、地址 0 保留 |
| 指针状态契约 | `src/ir/pointer_state.{h,c}` | §3.12 四态矩阵（bitcast 仅 live/dangling）；L0 `TC-L0-OBJ-0001` |
| 档案契约 | `src/runtime/profile.h`、`profile.c` | 两个 64 位内置档案；W=32 档案校验（测试本地 target） |
| 运行时四缝 | `src/runtime/{io,trap}.h`、`src/runtime/{alloc,module_loader}.h/.c` | I/O all-or-nothing、短读/EOF/错误、目标 W 分配器、trap 首错、设备端不解析 import |
| 诊断契约 | `src/diagnostic/{codes.h,codes.inc,diagnostic.h,diagnostic.c}`、生成的 `src/diagnostic/table.c` | 80 码机读表、首诊比较器（origin/rank/clause、call/make site phase/rank）、缓冲深拷贝、OOM 独立通道 |
| 零 pass 契约 | `src/contract/optimizer.h/.c` | O0/O2 均 0 pass、`@fast` 不消费、`@inline` 只保存 |
| 构建契约 | `src/CMakeLists.txt`、`src/driver/` | 私有 `tc_core`；仅 `tc-vm`/`tc-aot`；无 install 规则 |
| 用例 harness | `tests/harness/` | 7 判定器、超时/捕获/归一化、失败关闭、23 个自测；空套件/路径/IMPORT 阶段均 fail-closed |
| 码表/覆盖 | `tests/traceability/` | `codes.json`、生成的 C 表、634 槽、231 项（5 L0 covered / 629 uncovered） |
| golden | `tests/golden/` | 独立评审记录、code/stage/source/profile/kind/reviewer 校验、负例自测 |
| 契约索引 | `src/CONTRACTS.md` | 属主、冻结标记、指针状态与后续阶段边界 |

## 4. 附录 D.1 通用门禁

| 检查项 | 结果 | 依据/范围 |
| ---- | ---- | ---- |
| 80 码表未增删；码名/阶段/触发与标准一致 | **PASS** | `trace.codes`：65 CE + 15 RE；C 表/JSON/标准逐行一致 |
| 新增/修改用例有 `spec` 与独立评审 | **PASS** | golden `TC-LEX-COMMENT-0001` 由独立 `gate-auditor` 复核并记录；L0 case 在 `tests/unit/README.md` 有 spec 依据 |
| 覆盖矩阵已更新；未覆盖/挂起解释 | **PASS (S0)** | 634 槽、231 项：5 个 L0 covered，629 个显式 `uncovered`；未覆盖项标 deferred-to-S1+ |
| 首诊/位置断言覆盖本阶段相关码 | **PASS (S0)** | 比较器覆盖 §12.2 #1–#10，含 target/visibility → safety → LHS/return shape → args → receiver 阶段与跨位置测试；真实 `.tc` 位置断言 `N/A until S1` |
| 32/64 期望分档案；无跨档案比较 | **PASS (S0)** | W=32/64 类型宽度/档案结构测试；`golden/common` W 无关；真实分档案 golden `N/A until S3/S9` |
| 确定性套件（重复/哈希/线程/顺序） | **PASS (S0)** | 三套构建、`-j8`、repeat-3、coverage/harness JSON 可复现；哈希/线程/顺序扰动 `N/A until S1+` 前端 |
| 无新增警告；无标准之外诊断 | **PASS** | `-Wall -Wextra -Werror -Wpedantic`；`tc_diag_contract_violation()` 检查唯一码/打印名/阶段 |
| 构建/消毒器/符号审计 | **PASS** | ASan+UBSan 11/11；`tc-vm`/`tc-aot` 仅 `_main`；无 install 规则；无公共 ABI；`leaks` 0 |
| 门禁报告记录 profile/工具链版本 | **PASS** | 本文件 §2；reviewed commit `5ca7449` |

## 5. S0 专项门禁（计划 §5 S0 出口准则）

1. **契约评审与唯一属主**：**PASS**。`src/CONTRACTS.md` 给出头文件、属主、冻结事实与消费者；`tc_contract_check()` 在工具启动时强制检查；独立评审 `gate-auditor` 已复核。
2. **80 码机读校验**：**PASS**。65 CE + 15 RE；12 个 W 敏感码，5 个 IMPORT 报告码；`check_codes.py` 与 C 表 dump 一致。
3. **harness 失败关闭**：**PASS**。`tests/harness/selftest.py` 23 个场景通过：全部 7 判定器、空套件、缺失 source/include/link、IMPORT 阶段、超时、崩溃、`not-implemented`、地址归一化、schema/YAML。
4. **覆盖矩阵可报告、未覆盖可见**：**PASS**。634 槽、231 项（5 L0 covered / 629 uncovered）；分类完整，无静默通过。
5. **无实现语义承诺**：**PASS**。`tc-vm`/`tc-aot` 对合法调用显式返回 `tc_error_unavailable`（退出码 70），不产出工件、不打印成功；README/AGENTS 明确前端/VM/后端未实现。

### 5.1 零 pass 契约

- `tc_pipeline_build(O0|O2).pass_count == 0`，`tc_pipeline_levels_equivalent()` 为真；
- `@fast`/`@fast_math`/`@inline`/`@noinline` 仅保存为 `TcFuncFlags`；
- 未引入 SSA/regalloc/内联。

### 5.2 档案字段

- 内置 `aarch64-linux-user`（W=64 hosted）与 `aarch64-baremetal`（W=64 freestanding）通过结构校验；
- W=32 档案以测试本地 target 验证：W∈{32,64}、五区不重叠且不含地址 0、`D≥1`、`disable_icf==1`、栈/指针对齐、入口在代码区、32 位 `int64`/`float64` 双字/软浮点标志；
- 负例覆盖：W 非法、端序不符、`D=0`、`disable_icf=0`、区重叠/含 0/越界、入口越界、ABI 对齐不符、checked 值非法、缺 I/O 缝。

## 6. 范围说明（N/A，不是阻塞项）

| 项 | S0 状态 | 关闭阶段 |
| ---- | ---- | ---- |
| 真实 `.tc` LEX/SYN/IMPORT/SEM/CT 用例 | `N/A (S0)`；覆盖矩阵槽位显式 `uncovered` | S1–S5 |
| 真实 32/64 分档案 golden | `N/A (S0)`；结构级档案检查已通过 | S3/S9 |
| 哈希种子/线程数/顺序扰动 | `N/A (S0)`；重复/并行/固定环境已通过 | S1+ 前端 |
| VM 执行与 golden | `N/A (S0)` | S6 |
| AArch64 后端/工具链/差分 | `N/A (S0)` | S7 |
| 32 位目标选型与后端 | `N/A (S0)`；档案契约已校验 | S9 |
| harness 真实 adapter | `N/A (S0)`；harness 自身已通过 23 个自测 | S1 |

上述项不属于 S0 出口准则，均已在本表显式标注为 `N/A (S0)`，不伪装为通过，也不构成 G0 阻塞。

## 7. 契约审计结论（CLOSED）

独立 reviewer `gate-auditor`（read-only subagent，无实现参与）复核 reviewed code commit `5ca7449`，确认以下项关闭：

- **首诊排序**：一般位置 `origin → same_site_rank → clause_rank`；同一 CALL/MAKE site 按 §8.8.2 phase（target/visibility → safety → LHS/return shape → args/fields → receiver）再按 §12.2 #10 rank；跨位置测试与 site_id/phase 校验覆盖。L0 `TC-L0-DIAG-0001`…`0008`。
- **`_` 丢弃**：§6.15.1 任何有结果指令可用 `_`；无结果指令必须 `_`。L0 `TC-L0-IR-0001`。
- **类型契约**：14 个标量单例预创建、冻结后不新增节点；`void` 非标量值。L0 `TC-L0-TYPE-0001`。
- **指针状态**：§3.12 四态矩阵，bitcast 仅 live/dangling；one-past ptradd 仅 0。L0 `TC-L0-OBJ-0001`。
- **诊断缓冲**：记录深拷贝文件名；生命周期测试；OOM 通道。
- **harness**：23 个自测覆盖 7 判定器、IMPORT 阶段、空套件、缺失 source/include/link、超时/崩溃/not-implemented。
- **golden**：由 `gate-auditor` 独立复核推导（line 2, column 30）；validator 检查冻结码表/阶段/source/profile/kind/reviewer/duplicate，并与 `review-record.md` 交叉校验；负例自测通过。
- **文档**：中英文 README/CHANGELOG/CONTRIBUTING 与契约索引同步。

审计未发现未关闭 B1/B2/B3/B4。

## 8. 门禁判定

**G0：PASS。**

- 自动化工程证据：Debug/Release/ASan+UBSan 各 `ctest` 11/11；harness 23/23；golden 1/1 + golden selftest；覆盖矩阵 634/231/5 L0 covered；CLI 合法调用退出 70；leaks 0；符号/install 审计通过。
- 契约审计：CLOSED；独立评审记录见 `tests/golden/review-record.md`，reviewed code commit `5ca7449`。
- D.1：所有适用项 PASS；不适用项显式 `N/A (S0)` 并注明关闭阶段；无 `PARTIAL` 非阻塞行。
- **S1：允许**。

后续阶段的 `.tc` 用例、VM/AOT、32 位目标按计划交付；任何把未实现能力写成“已支持”、增删诊断码、放宽接受集或跨 32/64 比较的行为都构成本阶段门禁回归。
