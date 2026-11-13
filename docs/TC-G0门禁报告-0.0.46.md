# TC 0.0.46 S0 门禁报告（G0）

> **阶段**：S0 契约冻结与测试基建（对应设计 §13 P0）
> **依据**：[开发测试计划](TC开发测试计划-0.0.46.md) §5 S0、§7、附录 D；
> [标准](TC语言及编译器标准-0.0.46.md) §12/§13；[设计](TC原生编译器设计说明书-0.0.46.md) §2–§8/§12。
> **执行方式**：本会话无人介入（agent 执行）。契约对照为自动化/逐条文档核对；
> 独立人工评审未发生，作为治理项列在 §6，不伪装为已完成。

## 1. 结论

| 项 | 结果 |
| ---- | ---- |
| S0 开发交付 | 完成（契约头文件、`tc_core`、两个 CLI 骨架） |
| S0 测试活动 | 完成（码表机读校验、契约 L0、harness 自测、覆盖矩阵、golden 评审演练） |
| 自动化门禁 | **PASS**：`ctest` 10/10（Release 默认与 ASan/UBSan 构建各 10/10） |
| 人工独立评审 | **未执行（本会话无人介入）**，见 §6 挂起项 |
| 进入 S1 | 允许（以 §6 的治理挂起项不阻塞工程实现为前提） |

## 2. 环境与可重复性

| 项 | 值 |
| ---- | ---- |
| 宿主 | macOS arm64（Apple Silicon） |
| C 编译器 | Apple clang 21.0.0.21000334 |
| CMake | 4.4.2 |
| Python | 3.14.7（仅标准库，无网络） |
| 构建 | Debug；另以 `-fsanitize=address,undefined` 复跑 |
| 运行 | 本阶段无需 qemu/ELF；AOT 运行验证从 S7 开始 |

固定环境：测试进程使用 `LC_ALL=C`、`LANG=C`、`TZ=UTC`，固定 stdin，无网络，每个用例独立临时目录。
重复执行 3 次 `ctest` 均为 10/10；覆盖率与码表 `--check` 可逐字节复现。

## 3. 交付物与证据

| S0 交付 | 位置 | 证据 |
| ---- | ---- | ---- |
| 类型契约 | `src/ir/type.h`、`type.c` | intern/单例/冻结、`isize`≠`int32`、W 位宽；L0 `contract.unit` |
| IR 契约 | `src/ir/{operand,instr,block,ir,span}.h`、`instr.c` | 51 opcode、无结果指令 LHS、显式 CFG、命名实参、span |
| 对象/区契约 | `src/ir/{region,object,address,structs}.h` | 五区、对象表、地址 0 保留 |
| 档案契约 | `src/runtime/profile.h`、`profiles.c` | 两个 64 位内置档案；W=32 档案校验（测试本地 target） |
| 运行时四缝 | `src/runtime/{io,alloc,trap,module_loader}.h/.c` | I/O all-or-nothing、短读/EOF/错误、目标 W 分配器、trap 首错、设备端不解析 import |
| 诊断契约 | `src/frontend/diagnostic*.{h,c,inc}` | 80 码机读表、首诊比较器、缓冲、OOM 独立通道 |
| 零 pass 契约 | `src/frontend/optimizer.h/.c` | O0/O2 均 0 pass、`@fast` 不消费、`@inline` 只保存 |
| 构建契约 | `src/CMakeLists.txt`、`src/driver/` | 私有 `tc_core`；仅 `tc-vm`/`tc-aot`；无 install 规则 |
| 用例 harness | `tests/harness/` | 附录 C YAML 子集、7 判定器、超时/捕获/归一化、失败关闭 |
| 码表/覆盖 | `tests/traceability/` | `codes.json`、生成的 C 表、629 行覆盖骨架 |
| golden 评审演练 | `tests/golden/` | 推导说明 + 评审记录 + 机器校验 |
| 契约索引 | `src/CONTRACTS.md` | 属主、冻结标记、后续阶段边界 |

## 4. 附录 D.1 通用门禁

| 检查项 | 结果 | 证据 |
| ---- | ---- | ---- |
| 80 码表未增删；码名/阶段/触发与标准一致 | PASS | `trace.codes`：65 CE + 15 RE，码名/打印名/阶段/C 表逐行一致 |
| 新增/修改用例有 `spec` 与独立评审 | PARTIAL | 样例 golden 有 §2.1/§2.2/§12.3/§12.5.1 推导与评审记录；评审人是演练占位（§6） |
| 覆盖矩阵已更新；未覆盖/挂起解释 | PASS | 629/629 显式 `uncovered`；无静默通过 |
| 首诊/位置断言覆盖本阶段相关码 | PASS（契约级） | 比较器单测覆盖阶段/模块/源序/专用码/条款序/遍历无关；`.tc` 用例从 S1 开始 |
| 32/64 期望分档案；无跨档案比较 | PASS（结构级） | `golden/common` 仅 W 无关；`golden/w32`、`w64` 已建目录；覆盖矩阵含 12 个 W 敏感码的 w32/w64 槽 |
| 确定性套件（重复/哈希/线程/顺序） | PASS（S0 范围） | `ctest` 重复 3 次一致；harness 固定环境、排序输出、无时间戳；哈希/线程扰动属于 S1+ 真前端用例 |
| 无新增警告；无标准之外诊断 | PASS | `-Wall -Wextra -Werror -Wpedantic`；`tc_diag_contract_violation()` 检查唯一码/打印名/阶段 |
| 构建/消毒器/符号审计 | PASS | ASan+UBSan 10/10；`tc-vm` 仅导出 `_main`；无 install 规则；无公共 ABI |
| 门禁报告记录 profile/工具链版本 | PASS | 本文件 §2 |

## 5. S0 专项门禁（计划 §5 S0 出口准则）

1. **契约评审与唯一属主**：PASS（自动化对照）。`src/CONTRACTS.md` 对每个跨模块结构给出头文件、属主、冻结事实与消费者；`tc_contract_check()` 在工具启动时强制检查。
2. **80 码机读校验**：PASS。`tests/traceability/codes.json` 由标准 §12.3/§12.5.1 与计划附录 A 生成；`check_codes.py` 重新解析标准与 C 表 dump 比对；计数 65+15，12 个 W 敏感码，5 个 IMPORT 报告码。
3. **harness 失败关闭**：PASS。`tests/harness/selftest.py` 15 个场景全部通过，覆盖：正确期望通过、错误码失败、漏报、额外诊断、位置错误、超时、adapter 崩溃、`skip` 被拒、`not-implemented` 不计通过且门禁非零、地址归一化、YAML/模式校验。
4. **覆盖矩阵可报告、未覆盖可见**：PASS。629 行、226 项；分类：标准章节 15、诊断码 80（344 槽）、opcode 63（155 槽）、设计 §12 15、首诊规则 §12.2 10、32/64 翻转 17、注解 16、设备策略 4、I/O 源状态 4、退出策略 2。
5. **无实现语义承诺**：PASS。`tc-vm`/`tc-aot` 对合法调用显式返回 `tc_error_unavailable`（退出码 70），不产出工件、不打印成功；README/AGENTS 明确前端/VM/后端未实现。

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
| 独立人工契约评审/签署 | **未执行**（本会话无人介入；当前为自动化对照 + 文档核对） | S1 前由人工评审补签 |
| golden 评审人真实身份 | 演练占位 | S1 首个真实 golden 前替换 |
| 真实 `.tc` LEX/SYN/IMPORT/SEM/CT 用例 | 未实现（覆盖矩阵全部 `uncovered`） | S1–S5 |
| VM 执行与 golden | 未实现 | S6 |
| AArch64 后端/工具链/差分 | 未实现 | S7 |
| 32 位目标选型与后端 | 未实现（仅档案契约与本地校验） | S9 |
| ASan 泄漏检测 | 宿主 macOS 不支持 `detect_leaks`；已用 ASan/UBSan 运行全部 C 测试 | 运行平台支持时补 |
| harness 真实 adapter | 未实现（S0 只验证 harness 自身） | S1 |

## 7. 门禁判定

S0 的工程出口准则全部有可复现证据；唯一治理缺口是“独立人工评审”，已在 §6 显式挂起，
不影响按计划进入 S1 的实现工作。任何后续把未实现能力写成“已支持”、增删诊断码、
放宽接受集或跨 32/64 比较的行为都构成本阶段门禁回归。
