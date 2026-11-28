# TC-Compiler — Agent 指南

0.0.46 为 clean-sheet 实现基线。实现与验证以标准/设计/测试计划为准；历史实现、工具与测试
已移除，只在 git tag 中保留。`tc-0.0.47` 分支目标是在语言语义不变的前提下交付开发调试工具链
（范围见 `docs/TC开发调试工具链-0.0.47.md`）与工程模型（`docs/TC工程模型-0.0.47.md`）；
两份设计已达到冻结条件；P0–P4a、P5 本地验收、T0–T6 已完成，P4b 本地实现与四种
profile 的真实 gdb 验证已完成；固定 Linux CI 真实签名 artifact 待完成，P4b 正式验收与
G11 未完成。
当前私有入口为 `tc`/`tc-vm`/`tc-aot`；`tc --init-project` 已实现完整模板、原子写入、
回滚与身份检查；`tc --test` 已实现一层发现/筛选、fd 锚定 beneath 解析、严格侧车、
私有只读快照、fork worker、超时、流捕获、结构化结果、报告与退出映射。
编译器已消费 `tc.project`、canonical 根、有序 include 根与工程根相对显示路径。
RTOS/中断/并发移交 0.0.48。

## 当前事实

- 语言及编译器标准：`docs/TC语言及编译器标准-0.0.46.md`（唯一权威）。
- 原生编译器目标设计：`docs/TC原生编译器设计说明书-0.0.46.md`。
- 已实现：契约、LEX/SYN 与扁平 parse IR、IMPORT、名称/类型/转换/布局、静态指令形状检查、
  const_expr 形态、CFG/可达性/DI、缺返回、调用顺序/安全、安全上下文与注解矩阵、CT 求值
  （按需求值/缓存、静态初值；与 VM 共用 `runtime/semantics.c`）、IR 最终化/冻结与打印、
  W 相关运行时语义核、§11.2 精确格式化、§11.3 输入、参考 VM（五区/对象与字段子对象、
  指针状态、全部 51 个 opcode、调用/`D`、I/O、必选 trap、提交语义、checked 越界/悬垂/
  未初始化检测）、32/64 档案；W=64 AArch64 全量后端（整数/位/比较/浮点/转换、指针/
  内存/struct、全部 51 个 opcode、AAPCS64 复合/HFA/大复合按引用/大 struct 间接返回、`fnptr`/
  `call_indirect`、多尺寸与未对齐访问、checked build、复位入口/区基址/静态 `.data`/
  ELF/禁用 ICF）；freestanding 目标运行时核与平台缝（对象表/指针状态/必选定检/checked
  shadow、确定性 I/O、分配器、trap/退出）；`aarch64-baremetal` 平台实例（PL011
  UART0 stdout 与分帧 stdin、UART1 退出记录、PSCI SYSTEM_OFF、无 semihosting）；首个 32 位目标
  `arm32-baremetal`（Cortex-M7 MPS2 AN500、AAPCS32/ILP32 softfp、独立 ARM32 后端、
  int64/float64 辅助、未对齐访问、四设备策略、`qemu-system-arm`）；S10 新增
  `riscv32-baremetal`（RV32IMAFDC、ILP32 soft-float ABI、独立 RISC-V 32 后端、
  全部 51 个 opcode、64 位除法辅助、NS16550A UART0 stdout/stdin、semihosting
  退出记录、SiFive test finisher、`qemu-system-riscv32` virt）；`tc-aot`
  `--emit=asm|obj|elf|flat`、clang/lld/qemu 驱动、全量 VM↔AOT 差分/ABI/一致性/产物/
  确定性套件、C caller/TC caller 双向 AAPCS64/AAPCS32/RISC-V ILP32 互操作、
  生成式 ABI fuzz、实现边界/checked/规模/长跳转回归、复位入口 BSS 清零与
  far-branch 长跳转、32/64 分档案隔离、S10 长期回归语料/发布审计；ABI 参考版本、规则矩阵
  与范围限制见 `tests/conformance/abi/README.md`。代码在 `src/frontend/`、`src/ir/`、
  `src/runtime/`、`src/vm/`、`src/backend/` 与 `src/driver/`。
- 0.0.47 当前实现：P0/T0 完成版本拆分、三入口 CLI 契约与私有 `TcImplError` 退出码映射；
  P1 完成 `tc --init-project` 的完整模板、原子写入、回滚与身份检查，以及 `tc.project` v1
  解析与最近根发现 API；P2 完成 manifest/include/profile 的编译器消费、canonical 与显示
  路径分离、父目录 fd 锚定的模块身份、非阻塞有界源读取和 import 候选绑定。诊断、IR、
  VM trap 与 AOT 源字符串均使用工程根相对显示路径。P3 完成 `tc --test` 的一层发现/
  筛选、fd 锚定 beneath 解析、严格侧车、私有只读快照、fork worker、超时、流捕获、
  结构化结果、报告与退出映射；P4a 完成共享确定性行表、私有 AOT `.loc`/源注释与
  最小 DWARF v4 CU（clang 生成 `.debug_line`），并在发布诊断/产物前保护 stdio、
  项目输入与输出保留身份。T1 完成 VM/AOT `--dump-ir`、VM `--trace=ir,call,mem,io,trap`
  （text/jsonl、limit/output）、VM/AOT `--stats`、结构化 trap 上下文、显式输出身份/
  预留保护与确定性观测输出，证据入口为 `t1.observe`。T2 完成 VM/AOT
  `--dump-linemap[=<path>]` 与公开 `tc-aot --debug`，复用最终 IR 共享行表生成
  JSON v3、VM 合成地址、`.loc`/源注释与 DWARF v4 CU，并以 qemu gdbstub 和真实
  gdb 验证 `info line`、源码断点与单步，证据入口为 `t2.debug` 与 `t2.gdb`。
  T3 完成 `tc-vm --gdb[=<port>]`（默认 0，仅 loopback），强制
  `--gdb-stdout-journal=<dir>`；支持合成 IR 地址、`target.xml`/内存映射、
  断点/单步/继续、monitor、extended restart、原子 stdout journal 与可恢复 stdin，
  `--gdb-allow-write` 才开放受限写入。证据入口为 `gdb_journal.unit`、
  `gdb_rsp.unit`、`vm.gdb.protocol`（242 checks）、`vm.gdb.semantics` 与
  `vm.gdb.e2e`。P4b 完成 `tc-vm --gdb-elf-out=<path>`：私有内部 writer 生成
  只含符号与 DWARF v4 行表的 ELF，支持 AArch64、ARM32 与 RISC-V32 档案；
  四种档案均经独立语义解码，`toolchain.debug_elf` 已在四种 profile 通过真实 gdb
  调试 ELF、源级断点、64 位 `tc_ir_pc` 读取、IR 单步与程序语义回归。合成
  `tc_ir_pc` 使用 64 位 `uint64`，架构 pc 仍为 `code_ptr`（AArch64 64 位，
  ARM32/RISC-V32 32 位），ARM/RISC-V 的 Z0 kind=2 已被接受。ARM 客户端需 `set arm force-mode thumb`；GDB 18.1 的 ARM
  `stepi` 因未建模 sp 不可直接使用，测试通过 maintenance packet `s` 与 flush
  register-cache 验证同一 RSP 单步。T4 完成诊断 JSONL；T5 完成 CMake presets、固定
  标签、统一 `tests/run.py` 与 `tc-reduce`；T6 完成三架构调试产物重复/跨根确定性。
  §6.10 固定 Linux CI 真实签名 artifact 尚未完成，P4b 正式验收与 G11 均未完成。
- 版本：工具链为 0.0.47，语言与契约仍为 0.0.46/1；工具链版本不参与语言契约检查。
- 未实现：可选优化管线（SSA/regalloc/内联）；0.0.46 基线保持零 pass。
- 门禁：G0–G10 PASS，G10 完成 0.0.46 阶段出口；0.0.47 的 G11 未完成。
  G11 证据收集/校验与手动签名作业已接入，四种 profile 的本地真实 gdb 验证已完成，
  固定 Linux CI 真实签名 artifact 仍待完成，P4b 正式验收与 G11 未完成。已注册
  CTest 总数 99，收尾检查 51 项，见 `docs/gates/README.md`。
- 诊断码：80 个（65 `TC_CE_*` + 15 `TC_RE_*`），不得增删。
- 32/64 位均为目标；`usize`/`isize` 为平台字长类型。
- 不提供 C 宿主公共库、嵌入 API/ABI；不要重新引入。三个入口只链接私有静态 `tc_core`。
- 实现级退出码只由 `tc_impl_error_exit` 映射：OK→0、USAGE→2、IO/UNAVAILABLE/OOM→70、
  INTERNAL→71；源/输出 IO、能力不可用与实际 OOM 不得映射为 71。
- 文档化实现上限：单源文件 4 MiB、单 Token 16 KiB、类型/块嵌套 256 层；
  超出是 `tc_error_internal`（退出 71），不是语言诊断。AOT 还检查
  `frame_total*(D+1) <= stack_size`（VM 宿主帧不受此限），ARM32 代码窗口
  1 MiB；单函数 30000 指令有 far-branch 回归，更大规模受宿主/工具链资源影响，
  不纳入门禁。

## 工作规则

1. **先读文档，再动骨架**：语义/诊断以标准为准，架构以设计说明书为准。
2. **不要恢复旧实现**：不得从 git 历史或旧分支复制旧实现；0.0.46 按设计重写。
3. **文档改动最小化**：只改相关章节；不要凭旧实现行为反推语义。
4. **32/64 同时考虑**：`usize`/`isize`、`sizeof`、`bitcast`/`trunc`、布局与诊断的字长差异
   必须显式处理。
5. **构建与测试**：`cmake -S . -B .tmp/build && cmake --build .tmp/build && ctest --test-dir .tmp/build --output-on-failure`。
   CTest 将 `TMPDIR` 固定为项目 `.tmp/`；临时测试脚本、fixture 与编译产物均置于 `.tmp/`，
   版本控制测试源仍位于 `tests/`。不得提交 `build/`、`.tmp/` 或生成物。
   `--syntax-only`/`--sem-only`/`--static-only` 都是部分检查，退出 0 不表示程序有效；
   `--static-only` 含 CT 与 IR 最终化。普通 `tc-vm` 调用执行参考 VM（正常退出 0，
   trap 退出 1）；普通 `tc-aot` 调用发射目标汇编，`--emit=asm|obj|elf|flat` 经
   clang/lld 生成产物；`--profile=` 选择 `aarch64-linux-user`/`aarch64-baremetal`/
   `arm32-baremetal`/`riscv32-baremetal`（`w32` 仅 VM 静态档案），`--device=` 覆盖
   平台实例设备策略；
   无后端/工具链映射的档案显式退出 70 且不发布错误产物。不得把部分能力写成“已支持”。
   S7/S8/S9/S10 差分/工具链测试默认要求 `clang`、`llvm-ar`、`ld.lld`、
   `qemu-system-aarch64`、`qemu-system-arm` 与 `qemu-system-riscv32`；P4a DWARF 校验另需
   `llvm-dwarfdump`，T2/T3 真实 gdb 冒烟另需 `gdb`（或 `gdb-multiarch`）；缺失时配置失败。
   `-DTC_ENABLE_AOT_TESTS=OFF` 只关闭 lld/qemu 测试项；运行时构建仍需 clang 与
   llvm-ar（或对应 target ar），且不用于门禁。
6. **提交信息**：`docs(0.0.47): ...` 或 `chore(0.0.47): ...`。
7. **代码注释边界**：注释只解释代码本身、引用标准/设计条款或自包含事实；不得引用阶段编号、
   门禁报告、测试计划或审计结果。测试用例 id 只用于测试/覆盖追溯。
8. **收尾即清理**：每个阶段或分支收尾必须执行下方“收尾工作”；文档清理是门禁的一部分，
   不是可选的整理工作。

## 收尾工作

阶段/分支收尾按以下清单执行，顺序固定：

1. **冻结规范文档**：不改标准、设计、测试计划的语义条款；只允许修正链接或必要的
   交叉引用。
2. **清理过程/历史内容**：从 README、AGENTS、CONTRIBUTING、CHANGELOG、`docs/gates/`、
   `src/README.md`、`src/CONTRACTS.md`、`tests/*/README.md` 等非规范文档中删除：
   - 逐轮审计、首轮/复核、勘误、B1/B2/B3/B4 编号与修复日志；
   - reviewed commit、evidence version、环境快照与逐轮实测记录；
   - 按日期堆叠的迭代 changelog 条目、过期的阶段状态/计数；
   - 已被当前实现取代的约定或“历史实现如何做”的叙述。
   详细过程保留在 git 历史中，不进入分支文档。
3. **收敛门禁记录**：`docs/gates/` 只保留 G 状态表与每阶段一页以内的当前记录
   （结论、范围、证据入口、复现命令、当前限制）。门禁记录不得复述审计过程。
4. **归并当前约定**：仍影响实现的诊断位置、边界裁定、已知限制与后续阶段移交事项写入
   `src/CONTRACTS.md`，不留在门禁过程记录中。
5. **同步当前状态**：更新 README/AGENTS/CONTRIBUTING/文档地图/测试 README 的当前能力、
   测试计数、门禁状态与链接；CHANGELOG 按发布版本收敛为当前条目。
6. **全量验证**：执行
   `cmake -S . -B .tmp/build && cmake --build .tmp/build && ctest --test-dir .tmp/build --output-on-failure`，
   并确认 `tests/conformance/static/*_cases.py --check`、
   `tests/conformance/runtime/s6_cases.py --check`、
   `tests/conformance/static/s6_static_cases.py --check`、
   `tests/conformance/diff/s7_cases.py --check`、`tests/conformance/diff/s7_fuzz.py --check`、
   `tests/conformance/abi/s7_abi_cases.py --check`、
   `tests/conformance/tools/s7_toolchain_cases.py --check`、
   `tests/conformance/tools/s7_static_parity.py --check`、
   `tests/determinism/s7_determinism.py --check`、
   `tests/conformance/diff/s8_cases.py --check`、`tests/conformance/abi/s8_abi_cases.py --check`、
   `tests/conformance/abi/s8_interop_cases.py --check`、`tests/conformance/diff/s8_*_fuzz.py --check`、
   `tests/conformance/checked/s8_checked_cases.py --check`、
   `tests/conformance/tools/s8_static_fuzz.py --check`、
   `tests/conformance/consistency/s8_consistency_cases.py --check`、
   `tests/conformance/tools/s8_artifact_cases.py --check`、
   `tests/determinism/s8_determinism.py --check`、
   `tests/conformance/platform/s9_baremetal_cases.py --check`、
   `tests/conformance/platform/s9_arm32_cases.py --check`、
   `tests/conformance/abi/s9_arm32_interop.py --check`、
   `tests/conformance/abi/reverse_interop_cases.py --check`、
   `tests/tools/abi_mutation_audit.py --check`、
   `tests/tools/s9_capacity_report.py --check`、
   `tests/tools/s9_softfloat_audit.py --check`、
   `tests/conformance/tools/s9_static_parity.py --check`、
   `tests/conformance/platform/s9_profiles.py --check`、
   `tests/determinism/s9_determinism.py --check`、
   `tests/conformance/platform/s10_riscv32_cases.py --check`、
   `tests/conformance/platform/s10_limits_cases.py --check`、
   `tests/conformance/abi/s10_riscv32_interop.py --check`、
   `tests/conformance/abi/s10_reverse_interop.py --check`、
   `tests/conformance/abi/s10_abi_fuzz.py --check --seeds 8`、
   `tests/conformance/tools/s10_static_parity.py --check`、
   `tests/conformance/tools/s8_static_fuzz.py --check --profile=riscv32-baremetal`、
   `tests/conformance/tools/s10_frontend_scale.py --check`、
   `tests/conformance/checked/s10_checked_cases.py --check`、
   `tests/conformance/platform/s10_long_branch.py --check`、
   `tests/conformance/longterm/s10_longterm.py --check`、
   `tests/determinism/s10_determinism.py --check`、
   `tests/tools/s10_release_audit.py --check`、
   `tests/golden/validate.py`、
   `tests/golden/run.py .tmp/build/src/driver/tc-vm`、`tests/traceability/coverage.py --check`
   与 `tests/tools/docs_cleanup_check.py` 均通过。
7. **提交与洁净**：以 `docs(0.0.47): ...` 提交清理结果；确认 `git status` 干净，
   `build/`、`.tmp/` 与生成物未入库。

`docs.cleanup`（`tests/tools/docs_cleanup_check.py`）是上述清理的固定门禁：
出现审计/勘误/commit pin、门禁记录超长或 changelog 版本重复时失败。

## 导航

| 需求 | 位置 |
| ---- | ---- |
| 语言语义 / 诊断 / 平台档案 | `docs/TC语言及编译器标准-0.0.46.md` |
| IR / 运行时 / 后端 / 路线图 | `docs/TC原生编译器设计说明书-0.0.46.md` |
| 阶段 / 覆盖 / 门禁要求 | `docs/TC开发测试计划-0.0.46.md` |
| 0.0.47 工具链范围 / 契约 / 里程碑 | `docs/TC开发调试工具链-0.0.47.md` |
| 0.0.47 工程模型 / tc / 测试布局 | `docs/TC工程模型-0.0.47.md` |
| 工程名称 / manifest / 初始化 / 路径 | `src/project/name.h`、`manifest.h`、`init.h`、`paths.h`；P1 测试入口 `project.manifest`/`project.init`/`project.init_fault`，P2 入口 `project.paths_unit`/`project.paths`/`project.paths_aot` |
| 工程测试 runner / `tc --test` | `src/driver/test_runner.h`、`test_runner.c`；P3 入口 `project.test_runner` |
| 共享行表 / AOT 调试信息 | `src/ir/linemap.h`、`linemap.c`、`src/driver/aot_driver.c`；P4a 入口 `linemap.unit`/`p4a.debug`，T2 入口 `t2.debug`/`t2.gdb` |
| VM 调试 ELF / GDBstub | `src/vm/debug_elf.h`、`debug_elf.c`、`gdb.h`、`gdb.c`、`gdb_rsp.h`、`gdb_rsp.c`、`gdb_journal.h`、`gdb_journal.c`；P4b 入口 `toolchain.debug_elf`，T3 入口 `gdb_journal.unit`/`gdb_rsp.unit`/`vm.gdb.protocol`/`vm.gdb.semantics`/`vm.gdb.e2e` |
| G11 证据 / 发布审计 | `tests/tools/g11_evidence.py`、`s10_release_audit.py`；入口 `toolchain.g11_evidence` |
| VM/AOT 观测 | `src/driver/observe.h`、`observe.c`、`src/vm/observe.h`、`observe.c`、`src/backend/common/emit_stats.h`、`emit_stats.c`；T1 入口 `t1.observe` |
| 诊断 JSON / 测试基建 / 可复现发布 | T4 入口 `t4.diag`/`toolchain.diag_json`；T5 入口 `toolchain.presets`/`toolchain.reduce`；T6 入口 `release.t6_repro` |
| 实现模块边界 | `src/README.md` |
| 冻结契约与实现约定 | `src/CONTRACTS.md` |
| 前端接口 | `src/frontend/parse.h`、`lex.h`、`input.h`、`module.h`、`sem.h`、`ct.h` |
| CT/IR 实现约定 | `src/CONTRACTS.md`（CT 顺序/缓存/错误映射、对象布局、IR 操作数） |
| IR 打印 | `src/ir/print.h`、`src/ir/print.c` |
| 运行时语义核 / 格式化 / 输入 | `src/runtime/semantics.h`、`format.h`、`input.h` |
| AArch64 后端 / 目标映射 | `src/backend/aarch64/emit.h`、`emit.c`、`platform_semihost.c`、`platform_baremetal.c`、`start.S`、`link.ld`；`src/backend/common/toolchain.h` |
| ARM32 后端 / 平台 | `src/backend/arm32/emit.h`、`emit.c`、`emit_abi.c`、`emit_ops.c`、`rt_helpers.c`、`aeabi.c`、`platform_mps2.c`、`start.S`、`link.ld` |
| RISC-V 32 后端 / 平台 | `src/backend/riscv32/emit.h`、`emit.c`、`emit_abi.c`、`emit_ops.c`、`rt_helpers.c`、`compiler_rt.c`、`platform_virt.c`、`semi.S`、`start.S`、`link.ld` |
| freestanding 运行时核 / 平台缝 | `src/backend/common/runtime.c`、`rt_abi.h`、`rt_platform.h`；`src/backend/{aarch64,arm32,riscv32}/platform_*.c` |
| AOT 工具链驱动 | `src/driver/aot_driver.h`、`aot_driver.c`；`src/driver/cli.h` |
| VM 接口 / 内存对象 / 解释器 | `src/vm/vm.h`、`src/vm/internal.h`、`src/vm/memory.c`、`src/vm/interp.c` |
| 测试基建 | `tests/README.md` |
| 门禁状态 | `docs/gates/README.md` |
| 文档收尾检查 | `tests/tools/docs_cleanup_check.py`（`docs.cleanup`） |
| 一致性用例表 | `tests/conformance/static/s1_cases.py`、`s2s3_cases.py`、`s4_cases.py`、`s5_cases.py`、`s6_static_cases.py`；`tests/conformance/runtime/s6_cases.py` |
| S7 差分 / ABI / 工具链 | `tests/conformance/diff/s7_cases.py`、`s7_fuzz.py`；`tests/conformance/abi/s7_abi_cases.py`；`tests/conformance/tools/s7_toolchain_cases.py`、`s7_static_parity.py`；`tests/determinism/s7_determinism.py` |
| S8 全量差分 / ABI / checked / 一致性 / 产物 | `tests/conformance/diff/s8_cases.py`；`tests/conformance/abi/s8_abi_cases.py`；`tests/conformance/abi/s8_interop_cases.py`；`tests/conformance/diff/s8_*_fuzz.py`；`tests/conformance/checked/s8_checked_cases.py`；`tests/conformance/tools/s8_static_fuzz.py`；`tests/conformance/consistency/s8_consistency_cases.py`；`tests/conformance/tools/s8_artifact_cases.py`；`tests/determinism/s8_determinism.py` |
| S9 freestanding / ARM32 | `tests/conformance/platform/s9_baremetal_cases.py`；`tests/conformance/platform/s9_arm32_cases.py`；`tests/conformance/abi/s9_arm32_interop.py`；`tests/conformance/tools/s9_static_parity.py`；`tests/conformance/platform/s9_profiles.py`；`tests/determinism/s9_determinism.py` |
| S10 RISC-V / 长期回归 / 发布审计 | `tests/conformance/platform/s10_riscv32_cases.py`、`s10_limits_cases.py`；`tests/conformance/abi/s10_riscv32_interop.py`、`s10_reverse_interop.py`、`s10_abi_fuzz.py`；`tests/conformance/tools/s10_static_parity.py`、`s10_frontend_scale.py`；`tests/conformance/checked/s10_checked_cases.py`、`tests/conformance/platform/s10_long_branch.py`；`tests/conformance/longterm/s10_longterm.py`；`tests/determinism/s10_determinism.py`；`tests/tools/s10_release_audit.py` |
| ABI 规则矩阵 / 双向互操作 | `tests/conformance/abi/README.md`；`tests/conformance/abi/s8_interop_cases.py`；`tests/conformance/abi/s9_arm32_interop.py`；`tests/conformance/abi/reverse_interop_cases.py` |
| ABI mutation / 容量 / 软浮点审计 | `tests/tools/abi_mutation_audit.py`；`tests/tools/s9_capacity_report.py`；`tests/tools/s9_softfloat_audit.py`（手动工具，不进入 ctest） |
| 运行时 golden | `tests/golden/w32/`、`tests/golden/w64/`、`tests/golden/run.py` |
| 文档地图 | `docs/README.md` |
| 贡献 / 安全 | `CONTRIBUTING.md` / `SECURITY.md` |
