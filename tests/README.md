# Tests

测试层级、覆盖清单与门禁要求见
[TC 开发测试计划 0.0.46](../docs/TC开发测试计划-0.0.46.md)；当前门禁状态见
[门禁状态](../docs/gates/README.md)。

0.0.47 已注册 P0/T0 的三入口版本、CLI 语法、退出码与发布审计测试，并覆盖
OOM 故障注入、报告写失败与审计变异自测；P1 新增 `project.manifest`、
`project.init`、`project.init_fault` 并更新 `cli.contract`，覆盖 manifest/根发现、
完整模板、预检/身份/别名与逐故障点回滚；P2 新增 `project.paths_unit`、
`project.paths`、`project.paths_aot`，覆盖 canonical/显示路径、外部/回退、模块身份、
manifest/include/profile 消费、错误优先级、非阻塞有界源读取与 AOT 路径消费者；
P3 新增 `project.test_runner`，覆盖发现/筛选、fd 锚定 beneath 解析、严格侧车、
私有只读快照、fork worker、超时、流捕获、结构化结果、报告与退出映射；P4a 新增
`linemap.unit` 与 `p4a.debug`，覆盖共享 JSON v3 行表、IR 到 `.loc`/源注释顺序、
最小 DWARF v4、跨根确定性、非 debug 字节与 VM/AOT 输出保护边界；T1 新增
`t1.observe`，覆盖 VM/AOT `--dump-ir`、VM `--trace=ir,call,mem,io,trap` 的
text/jsonl/limit/output、VM/AOT `--stats`、结构化 trap 上下文、显式输出身份/预留保护
与确定性观测输出；T2 新增 `t2.debug` 与 `t2.gdb`/`t2.gdb.arm32`/`t2.gdb.riscv32`，覆盖 VM/AOT `--dump-linemap`、
公开 `tc-aot --debug`、全 IR 行表映射、VM 合成地址、稳定键序/external、trace 多对一、
跨根确定性、输出安全，以及 qemu gdbstub 上真实 gdb 的 `info line`、源码断点与单步；
T3 新增 `gdb_journal.unit`、`gdb_rsp.unit`、`vm.gdb.protocol`（242 checks）、
`vm.gdb.semantics` 与 `vm.gdb.e2e`，覆盖原子 stdout journal、可恢复 stdin、
协议、只读/受限写入、AArch64/w32/ARM32/RISC-V32 合成地址与 `target.xml`、
断点/单步/继续、monitor、extended restart、trap/退出一致性与真实 gdb 端到端调试。
P4b 新增 `toolchain.debug_elf`，覆盖四种档案的独立 ELF/DWARF 解码与真实 gdb
调试 ELF、源级断点、64 位 `tc_ir_pc` 读取、IR 单步和程序语义回归；G11 新增
`toolchain.g11_evidence`，覆盖证据 schema、精确用例矩阵、摘要与 provenance 校验。
T4 新增 `t4.diag` 与 `toolchain.diag_json`，覆盖稳定 JSONL schema、80 码映射与文本诊断
同序；T5 新增 `toolchain.presets` 与 `toolchain.reduce`，覆盖 presets、固定标签、统一
运行器、认证、指纹保持、身份保护、超时、stdin 快照与原子交付；T6 新增
`release.t6_repro`，覆盖三架构调试 `asm/obj/elf/flat` 的重复与跨根字节一致性。
所有临时脚本、fixture 与编译产物置于项目 `.tmp/`，版本控制测试源仍位于 `tests/`。
T3 的 journal/RSP/协议/语义测试为 `unit`/`toolchain`/`slow`，`vm.gdb.e2e` 为
`unit`/`toolchain`/`aot` 且不属于 fast；
上述已注册入口依据 [工程模型 §9.1](../docs/TC工程模型-0.0.47.md)与
[工具链 §7.1](../docs/TC开发调试工具链-0.0.47.md)验收；未注册项不得作为当前套件或
PASS 证据。

| Suite | Location | Level | Content |
| --- | --- | --- | --- |
| Project | `project/` | L0/L2 | P1：`project.manifest`（`tc.project` v1/缺省/错误/最近根发现）、`project.init`（完整模板、预检、身份/别名、报告失败）、`project.init_fault`（逐故障点原子回滚与清理残留）；P2：`project.paths_unit`、`project.paths`、`project.paths_aot`（路径显示/身份/源边界与 VM/AOT 消费者）；P3：`project.test_runner`（发现/筛选、路径边界、严格侧车、私有快照/fork worker/超时/流捕获/结构化结果/报告与退出映射）；P4a/T2：`linemap.unit`、`p4a.debug`、`t2.debug`、`t2.gdb`（JSON v3/IR 行序/VM 合成地址/DWARF v4/跨根确定性/非 debug 字节/输出保护/真实 gdb）；P4a 标签 `unit`/`toolchain`/`fast`，T2 标签 `toolchain`/`aot` |
| Contract/unit | `unit/` | L0/L1/L2 | 契约检查；`tc_frontend_test`（LEX/SYN、parse IR、位置/限制）；`tc_sem_test`（类型身份、布局、W 尺寸、冻结、模块顺序/实例）；`tc_ct_test`（CT 向量/缓存/局部 const）；`tc_ir_test`（最终 IR 形态/CFG/打印）；`tc_vm_test`（语义核、格式化、输入、内存/指针、trap/位置、调用/`D`、设备策略、W 矩阵）；T3 `gdb_journal.unit`/`gdb_rsp.unit`（原子 journal、可恢复 stdin、RSP 编解码）；`tc_oom_test`（SEM、VM 与三后端逐分配点 OOM 故障注入及分配清理校验） |
| Conformance | `conformance/` | L3/L4/L5/L6 | 静态 LEX/SYN/IMPORT/SEM/CFG-DI/调用/安全/注解/CT 用例、接受工具用例、属性用例；VM 运行时用例（`runtime/`）；S7/S8 VM↔AOT 差分（`diff/`）、AAPCS64 ABI（`abi/`）、checked build（`checked/`）、§13.4 八项一致性（`consistency/`）、工具链/ELF/静态一致（`tools/`）；S9 freestanding/裸机与 ARM32 平台（`platform/`）、AAPCS32 互操作（`abi/s9_*`）、双档案静态一致（`tools/s9_*`）；S10 RISC-V 32 平台/ABI/静态一致、checked/规模/长跳转、长期回归语料（`platform/s10_*`、`abi/s10_*`、`tools/s10_*`、`checked/s10_*`、`longterm/`）；见 [conformance/README.md](conformance/README.md) |
| Harness | `harness/` | — | 元数据解析、7 判定器、失败关闭自测、root-module `--syntax-only`/`--static-only` adapter |
| Traceability | `traceability/` | — | 80 码表、结构性覆盖矩阵、生成器检查、矩阵 case id 存在性交叉检查 |
| Golden | `golden/` | L3/L4 | 规范推导的期望值及规范来源；`validate.py` 校验诊断与运行时 golden，`run.py` 执行 w32/w64 运行时 golden |
| Tools | `tools/` | L6 | `tc`/`tc-vm`/`tc-aot` CLI 契约（三入口版本、工程 CLI 语法、`--test` 发现/筛选与失败关闭、用法/档案、部分检查、诊断、实现错误、AOT 产物/显式拒绝、关闭 stdout/stderr、无副作用）；T1 `t1.observe`（IR/trace/stats、trap 上下文、输出身份/预留保护与确定性）；T2 `t2.debug`/`t2.gdb`（共享行表、公开 debug/DWARF v4、输出安全、真实 qemu/gdb 断点与单步）；T3 `vm.gdb.protocol`/`vm.gdb.semantics`/`vm.gdb.e2e`（协议、语义与真实 gdb；均非 fast）；P4b `toolchain.debug_elf`（四种档案 ELF/DWARF 与真实 gdb）；G11 `toolchain.g11_evidence`（证据结构、矩阵、摘要与 provenance）；T4 `t4.diag`/`toolchain.diag_json`；T5 `toolchain.presets`/`toolchain.reduce`；T6 与 S10 发布审计（跨根/重复产物、80 码闭集、私有 core、三个 CLI、freestanding 符号、文档/版本一致性）及审计变异自测 |
| Determinism | `determinism/` | L7 | 重复运行、哈希种子/顺序扰动、逐字节一致的 harness JSON（S1/S2-S3/S4/S5/S6）；S7/S8/S9/S10 零 pass/ELF 构建/执行稳定性与 checked/release 分离；T6 三架构调试 asm/obj/elf/flat 重复与跨根字节一致性 |
| Docs | `tools/docs_cleanup_check.py` | — | 文档收尾门禁：无过程/历史叙事、门禁记录简短、changelog 版本不重复 |

运行：

```sh
python3 tests/run.py --preset fast
python3 tests/run.py --preset ci
# P1/P2/P3/P4a/P4b/T1–T6/G11 工程模型与调试测试
ctest --test-dir .tmp/build -R '^project\.|^linemap\.|^p4a\.|^toolchain\.(debug_elf|g11_evidence|diag_json|presets|reduce)|^t[124]\.|^gdb_|^vm\.gdb\.|^release\.(t6_repro|s10_audit|audit_selftest)' --output-on-failure
ctest --test-dir .tmp/build -L '^fast$' --output-on-failure
```

`project.manifest`、`project.init`、`project.init_fault` 与更新的 `cli.contract`
共同构成 P1 当前回归入口；`project.paths_unit`、`project.paths` 与
`project.paths_aot` 构成 P2 当前回归入口；`project.test_runner` 构成 P3 当前回归入口；`linemap.unit` 与 `p4a.debug` 构成 P4a
当前回归入口；`t1.observe` 构成 T1 当前回归入口；`t2.debug` 与 `t2.gdb`/`t2.gdb.arm32`/`t2.gdb.riscv32` 构成 T2
当前回归入口；T3 的 `gdb_journal.unit`、`gdb_rsp.unit`、`vm.gdb.protocol` 与
`vm.gdb.semantics` 构成 slow 调试回归入口，`vm.gdb.e2e` 为非 fast 的端到端回归入口；
`toolchain.debug_elf` 构成 P4b 回归入口，`toolchain.g11_evidence` 构成 G11 证据基础设施
回归入口；`t4.diag`/`toolchain.diag_json`、`toolchain.presets`/`toolchain.reduce` 与
`release.t6_repro` 分别构成 T4、T5 与 T6 回归入口；标签筛选使用 CTest 的标签匹配语法。

门禁使用 adapter 且不允许 `not_implemented`；缺失、跳过、超时、崩溃均为失败。
每次阶段/分支收尾还必须通过 `docs.cleanup`（见 `AGENTS.md` 的“收尾工作”）。
一致性用例按规范对象组织，期望必须引用标准条款，不得由编译器输出回填。
S1/S2/S3/S4/S5 用例表分别由 `tests/conformance/static/s1_cases.py`、
`s2s3_cases.py`、`s4_cases.py`、`s5_cases.py` 生成并 `--check`；S6 运行时用例表由
`tests/conformance/runtime/s6_cases.py`、逐 opcode 静态形状用例由
`tests/conformance/static/s6_static_cases.py` 生成并 `--check`。
S4 接受用例使用 `--sem-only`（SEM 阶段的部分检查）；编译用例与 S5 接受用例使用
`--static-only`（含 CT 与 IR 最终化）；S6 运行用例调用 `tc-vm` 并断言 stdout 字节、
首个 `TC_RE_*`、退出状态与终止方式。

AOT 差分用例调用 `tc-aot --emit=elf`，再用 qemu-system-aarch64 执行 ELF，比较程序
stdout、首个 trap 码与退出策略；`s7_cases.py`/`s7_fuzz.py` 覆盖子集与整数 fuzz，
`s7_abi_cases.py` 覆盖 AAPCS64 子集，`s7_toolchain_cases.py` 覆盖 `.s`/`.o`/ELF、
显式拒绝与静态一致，`s7_static_parity.py` 对全部静态 `.tc` 比较两个工具的首诊，
`s7_determinism.py` 覆盖 O0/O2、重复构建与重复执行。S8 套件在同一执行机制上
覆盖全部 51 个 opcode 与全部值类型：`s8_cases.py`（全量差分）、
`s8_abi_cases.py`（全量 ABI + 汇编形态）、`s8_interop_cases.py`（clang
AAPCS64 调用方互操作）、`s8_abi_fuzz.py`/`s8_layout_fuzz.py`/`s8_mem_fuzz.py`/
`s8_fp_fuzz.py`（生成式对抗用例）、`s8_static_fuzz.py`（静态变异双工具首诊）、
`s8_checked_cases.py`（checked/
必选 trap/合法集分离）、`s8_consistency_cases.py`（§13.4 八项）、
`s8_artifact_cases.py`（入口/区/符号/静态字节/ICF）与 `s8_determinism.py`。
S9 套件在 `conformance/platform/`、`conformance/abi/s9_arm32_interop.py`、
`conformance/abi/reverse_interop_cases.py`、`conformance/tools/s9_static_parity.py`
与 `determinism/s9_determinism.py`：
`aarch64-baremetal` 的 ELF/flat、PL011 UART0 输出与分帧输入、trap、静态字节、
四设备策略与符号审计，
`arm32-baremetal` 的 29 个固定语义/ABI 用例 + 305 个 S6/S8 语料同档案
VM↔AOT 差分、14 个 C 调用者 AAPCS32 互操作用例、19 个 TC caller → C callee
反向 ABI 用例、1072 个静态源 × 2 个裸机档案的 VM/AOT 首诊逐字节一致、
32/64 分档案隔离与重复构建/执行确定性。AAPCS64/AAPCS32 的参考版本、规则矩阵
与明确不支持的 over-aligned/bit-field/vector/variadic 形态见
[conformance/abi/README.md](conformance/abi/README.md)。
S10 套件在 `conformance/platform/s10_riscv32_cases.py`、
`conformance/abi/s10_riscv32_interop.py`、`conformance/abi/s10_reverse_interop.py`、
`conformance/abi/s10_abi_fuzz.py`、`conformance/platform/s10_limits_cases.py`、
`conformance/tools/s10_static_parity.py`、`conformance/tools/s10_frontend_scale.py`、
`conformance/checked/s10_checked_cases.py`、`conformance/platform/s10_long_branch.py`、
`conformance/longterm/s10_longterm.py`、
`determinism/s10_determinism.py` 与 `tools/s10_release_audit.py`：
`riscv32-baremetal` 的 ELF32/EM_RISCV/区符号/无动态依赖/无未定义符号审计、
29 个固定语义/ABI 用例 + 305 个 S6/S8 语料同档案 VM↔AOT 差分、四设备策略、
checked/必选 trap 分离、UART 分帧 stdin、flat 产物、1072 个静态源的 VM/AOT
首诊一致、32 个 C 调用者 RISC-V ILP32 互操作、14 个 TC caller → C callee 反向
ABI 用例、重复构建/执行与 O0/O2 确定性，以及标准条款/边界/差分/fuzz/平台长期
回归语料。RISC-V ABI 规则见 [conformance/abi/README.md](conformance/abi/README.md)。
AOT 构建/测试默认要求 `clang`、`llvm-ar`（或对应 target ar）、`ld.lld`、
`qemu-system-aarch64`、`qemu-system-arm` 与 `qemu-system-riscv32`；P4a 的 DWARF
校验另需 `llvm-dwarfdump`，T2/T3 真实 gdb 冒烟另需 `gdb`（或 `gdb-multiarch`）；
缺失时 CMake 配置失败。
`-DTC_ENABLE_AOT_TESTS=OFF` 只关闭 lld/qemu 测试项；运行时构建仍需 clang 与
llvm-ar（或对应 target ar）。32/64 结果分档案保存；差分只在同一档案内比较 VM 与 AOT。
S8 生成式套件默认每个 family 48 seeds（门禁预算）；手动长跑使用
`python3 tests/conformance/diff/s8_abi_fuzz.py --check --seeds 256`（layout/mem/fp
同理）扩大样本，不改变 ctest 的默认时间。

0.0.47 的 `toolchain.debug_elf` 独立解码 ELF/DWARF，校验四种档案映射、符号/行表与
共享 linemap 全序一致、空函数与顶层单元边界、CLI/输出身份/写失败，并以真实 gdb
验证四种 profile 的调试 ELF、源级断点、64 位 `tc_ir_pc` 读取、IR 单步与程序语义回归。
合成 `tc_ir_pc` 使用 64 位 `uint64`，架构 pc 仍为 `code_ptr`（AArch64 64 位，
ARM32/RISC-V32 32 位），ARM/RISC-V 的 Z0 kind=2 已被接受。ARM 客户端需
`set arm force-mode thumb`；ARM/RISC-V 合成 IR 单步通过 maintenance packet `s`
与 flush register-cache 验证同一 RSP 单步。
`toolchain.g11_evidence` 校验证据 schema、transcript 逐用例断言、SKIP、artifact 摘要与
provenance 源提交；发布审计可通过 `--require-g11-evidence=<artifact-dir>` 强制校验两份签名。
手动 CI 作业要求输入钉住的容器 digest，并为 `result.json` 与 `transcript.log` 生成 attestation。
固定 Linux CI 真实签名证据已通过发布审计，P4b 正式验收与 G11 均已完成。

0.0.47 CTest 注册总数 99，收尾检查 51 项；P0–P5、T0–T6、P4b 正式验收与
G11 均已完成，G0–G11 全部通过。
0.0.46 harness 873/873
（608 编译、131 工具、133 运行、1 属性）；单元检查 frontend 688、sem 102、ct 370、
ir 674、vm 514、aot 81、oom 383。S9 平台套件另有 29 个固定 ARM32 用例、
305 个 S6/S8 语料、14 个 AAPCS32 互操作用例、33 个 AAPCS64 互操作用例、
19 个反向 ABI 用例与 1072×2 个静态源一致性检查；S10 另有 29 个固定 RISC-V
用例、305 个同档案语料、32 个 C 调用者用例、14 个反向 ABI 用例、1072 个 RISC-V
静态源一致性与 154×2 个长期回归静态源。
S10 对抗套件另含：

- 48 个生成式 ABI 用例（8 seeds × 3 目标 × C→TC/TC→C；可 `--seeds` 长跑）；
- 7 类实现边界/鲁棒性用例（帧/静态区预算、`D` 边界、设备窗口末端、空输入、
  flat 执行、长路径 trap 记录、栈深度）；
- 240 个 RISC-V 静态变异源；
- 800-case switch 前端规模回归；
- 39 个 RISC-V checked UB 用例（VM/AOT 一致）；
- 30000 指令 far-branch 回归（RISC-V auipc/jalr 长跳转）；
- 诊断/浮点/I/O 回归：div-by-inf 精确零、binade 字面量、注解矩阵
  （bare NAME 放置、多重注解、`ptr<Nope>` 对照）、closed stdout/broken pipe 的
  `TC_RE_IO`、非法 UTF-8 全诊断路径/畸形序列/导入/offset 0。
`tests/tools/abi_mutation_audit.py --check`、
`tests/tools/s9_capacity_report.py --check` 与
`tests/tools/s9_softfloat_audit.py --check` 是手动审计工具，不进入 ctest；
后者扫描 ARM32 运行时对象，确认 binary64 不含 `d` 寄存器浮点指令。
