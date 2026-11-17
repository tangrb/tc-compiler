# TC-Compiler — Agent 指南

0.0.46 clean-sheet 分支。实现与验证以标准/设计/测试计划为准；历史实现、工具与测试已移除，
只在 git tag 中保留。

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
  UART0 stdout、UART1 退出记录、PSCI SYSTEM_OFF、无 semihosting）；首个 32 位目标
  `arm32-baremetal`（Cortex-M7 MPS2 AN500、AAPCS32/ILP32 softfp、独立 ARM32 后端、
  int64/float64 辅助、未对齐访问、四设备策略、`qemu-system-arm`）；`tc-aot`
  `--emit=asm|obj|elf`、clang/lld/qemu 驱动、全量 VM↔AOT 差分/ABI/一致性/产物/
  确定性套件、C 调用者 AAPCS64/AAPCS32 互操作、32/64 分档案隔离与生成式 fuzz。
  代码在 `src/frontend/`、`src/ir/`、`src/runtime/`、`src/vm/`、`src/backend/` 与
  `src/driver/`。
- 未实现：RISC-V 等后续目标与可选优化管线（SSA/regalloc/内联）；0.0.46 基线保持零 pass。
- 门禁：G0–G9 PASS，G9 允许进入 S10；见 `docs/gates/README.md`。
- 诊断码：80 个（65 `TC_CE_*` + 15 `TC_RE_*`），不得增删。
- 32/64 位均为目标；`usize`/`isize` 为平台字长类型。
- 不提供 C 宿主公共库、嵌入 API/ABI；不要重新引入。
- 文档化实现上限：单源文件 4 MiB、单 Token 16 KiB、类型/块嵌套 256 层；
  超出是 `tc_error_internal`（退出 71），不是语言诊断。

## 工作规则

1. **先读文档，再动骨架**：语义/诊断以标准为准，架构以设计说明书为准。
2. **不要恢复旧实现**：不得从 git 历史或旧分支复制旧实现；0.0.46 按设计重写。
3. **文档改动最小化**：只改相关章节；不要凭旧实现行为反推语义。
4. **32/64 同时考虑**：`usize`/`isize`、`sizeof`、`bitcast`/`trunc`、布局与诊断的字长差异
   必须显式处理。
5. **构建与测试**：`cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`。
   `--syntax-only`/`--sem-only`/`--static-only` 都是部分检查，退出 0 不表示程序有效；
   `--static-only` 含 CT 与 IR 最终化。普通 `tc-vm` 调用执行参考 VM（正常退出 0，
   trap 退出 1）；普通 `tc-aot` 调用发射目标汇编，`--emit=obj|elf` 经
   clang/lld 生成产物；`--profile=` 选择 `aarch64-linux-user`/`aarch64-baremetal`/
   `arm32-baremetal`（`w32` 仅 VM 静态档案），`--device=` 覆盖平台实例设备策略；
   无后端/工具链映射的档案显式退出 70 且不发布错误产物。不得把部分能力写成“已支持”。
   S7/S8/S9 差分/工具链测试默认要求 `clang`、`llvm-ar`、`ld.lld`、
   `qemu-system-aarch64` 与 `qemu-system-arm`；缺失时配置失败。
   `-DTC_ENABLE_AOT_TESTS=OFF` 只关闭 lld/qemu 测试项；运行时构建仍需 clang 与
   llvm-ar/aarch64-elf-ar，且不用于门禁。
6. **提交信息**：`docs(0.0.46): ...` 或 `chore(0.0.46): ...`，说明 clean-sheet。
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
   `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`，
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
   `tests/conformance/tools/s9_static_parity.py --check`、
   `tests/conformance/platform/s9_profiles.py --check`、
   `tests/determinism/s9_determinism.py --check`、`tests/golden/validate.py`、
   `tests/golden/run.py build/src/driver/tc-vm`、`tests/traceability/coverage.py --check`
   与 `tests/tools/docs_cleanup_check.py` 均通过。
7. **提交与洁净**：以 `docs(0.0.46): ...` 提交清理结果；确认 `git status` 干净，
   `build/` 与生成物未入库。

`docs.cleanup`（`tests/tools/docs_cleanup_check.py`）是上述清理的固定门禁：
出现审计/勘误/commit pin、门禁记录超长或 changelog 版本重复时失败。

## 导航

| 需求 | 位置 |
| ---- | ---- |
| 语言语义 / 诊断 / 平台档案 | `docs/TC语言及编译器标准-0.0.46.md` |
| IR / 运行时 / 后端 / 路线图 | `docs/TC原生编译器设计说明书-0.0.46.md` |
| 阶段 / 覆盖 / 门禁要求 | `docs/TC开发测试计划-0.0.46.md` |
| 实现模块边界 | `src/README.md` |
| 冻结契约与实现约定 | `src/CONTRACTS.md` |
| 前端接口 | `src/frontend/parse.h`、`lex.h`、`input.h`、`module.h`、`sem.h`、`ct.h` |
| CT/IR 实现约定 | `src/CONTRACTS.md`（CT 顺序/缓存/错误映射、对象布局、IR 操作数） |
| IR 打印 | `src/ir/print.h`、`src/ir/print.c` |
| 运行时语义核 / 格式化 / 输入 | `src/runtime/semantics.h`、`format.h`、`input.h` |
| AArch64 后端 / 目标映射 | `src/backend/aarch64/emit.h`、`emit.c`、`platform_semihost.c`、`platform_baremetal.c`、`start.S`、`link.ld`；`src/backend/common/toolchain.h` |
| ARM32 后端 / 平台 | `src/backend/arm32/emit.h`、`emit.c`、`emit_abi.c`、`emit_ops.c`、`rt_helpers.c`、`aeabi.c`、`platform_mps2.c`、`start.S`、`link.ld` |
| freestanding 运行时核 / 平台缝 | `src/backend/common/runtime.c`、`rt_abi.h`、`rt_platform.h`；`src/backend/{aarch64,arm32}/platform_*.c` |
| AOT 工具链驱动 | `src/driver/aot_driver.h`、`aot_driver.c`；`src/driver/cli.h` |
| VM 接口 / 内存对象 / 解释器 | `src/vm/vm.h`、`src/vm/internal.h`、`src/vm/memory.c`、`src/vm/interp.c` |
| 测试基建 | `tests/README.md` |
| 门禁状态 | `docs/gates/README.md` |
| 文档收尾检查 | `tests/tools/docs_cleanup_check.py`（`docs.cleanup`） |
| 一致性用例表 | `tests/conformance/static/s1_cases.py`、`s2s3_cases.py`、`s4_cases.py`、`s5_cases.py`、`s6_static_cases.py`；`tests/conformance/runtime/s6_cases.py` |
| S7 差分 / ABI / 工具链 | `tests/conformance/diff/s7_cases.py`、`s7_fuzz.py`；`tests/conformance/abi/s7_abi_cases.py`；`tests/conformance/tools/s7_toolchain_cases.py`、`s7_static_parity.py`；`tests/determinism/s7_determinism.py` |
| S8 全量差分 / ABI / checked / 一致性 / 产物 | `tests/conformance/diff/s8_cases.py`；`tests/conformance/abi/s8_abi_cases.py`；`tests/conformance/abi/s8_interop_cases.py`；`tests/conformance/diff/s8_*_fuzz.py`；`tests/conformance/checked/s8_checked_cases.py`；`tests/conformance/tools/s8_static_fuzz.py`；`tests/conformance/consistency/s8_consistency_cases.py`；`tests/conformance/tools/s8_artifact_cases.py`；`tests/determinism/s8_determinism.py` |
| S9 freestanding / ARM32 | `tests/conformance/platform/s9_baremetal_cases.py`；`tests/conformance/platform/s9_arm32_cases.py`；`tests/conformance/abi/s9_arm32_interop.py`；`tests/conformance/tools/s9_static_parity.py`；`tests/conformance/platform/s9_profiles.py`；`tests/determinism/s9_determinism.py` |
| 运行时 golden | `tests/golden/w32/`、`tests/golden/w64/`、`tests/golden/run.py` |
| 文档地图 | `docs/README.md` |
| 贡献 / 安全 | `CONTRIBUTING.md` / `SECURITY.md` |
