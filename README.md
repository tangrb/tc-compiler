# TC-Compiler

TC **0.0.46** 语言及编译器规范 + 原生编译器（TC → ASM → 机器码）目标设计。
当前分支为 0.0.46 clean-sheet：只保留标准、设计、测试计划与按设计重写的实现；
历史实现与旧工具已移除，需要时从 git tag 获取。

## 当前状态

- **已实现**：契约层、LEX/SYN 与扁平 parse IR、模块发现/IMPORT、名称/类型/转换/布局、
  静态指令形状检查、const_expr 形态、CFG/可达性/DI、缺返回、调用顺序/安全、安全上下文与注解矩阵；
  CT 求值（按需求值/缓存、严格错误映射、静态初值）、IR 最终化/冻结与确定性打印；
  W 相关运行时语义核（整数/浮点/转换、§11.2 精确格式化、§11.3 输入）、参考 VM
  （五区/对象与字段子对象、指针状态、全部 51 个 opcode、调用/`D`、I/O、必选 trap、
  提交语义）；32/64 档案，VM 可执行 `aarch64-linux-user`、`aarch64-baremetal`、
  `arm32-baremetal` 与 `w32`；
  W=64 AArch64 全量后端（整数/位/比较/浮点/转换、指针/内存/struct、全部 51 个 opcode、
  AAPCS64 复合/HFA/大复合按引用指针/大 struct 间接返回、`fnptr`/`call_indirect`、多尺寸与未对齐访问、
  checked build）；freestanding 目标运行时（对象表/指针状态/必选定检/checked shadow、
  确定性 I/O、分配器、trap/退出），`aarch64-baremetal` 使用 PL011 UART0 输出、
  PL011 UART1 退出记录与 PSCI SYSTEM_OFF（无 semihosting）；首个 32 位目标
  `arm32-baremetal`（Cortex-M7 MPS2 AN500、AAPCS32/ILP32 softfp、独立 ARM32
  后端与全部 51 个 opcode、`int64`/`float64` 辅助、未对齐访问、四设备策略、
  qemu-system-arm 执行）；复位入口、区基址、静态区 `.data`、ELF 产物与 clang/lld 驱动；
  全量 VM↔AOT 差分/ABI/一致性/产物/确定性套件、C 调用者 AAPCS64/AAPCS32 互操作、
  32/64 分档案隔离与生成式 ABI/布局/内存/浮点 fuzz。
- **未实现**：RISC-V 等后续目标与可选优化管线（SSA/regalloc/内联）；0.0.46 基线保持零 pass。
- 当前门禁：G0–G9 PASS；**G9 允许进入 S10**。详见 [门禁状态](docs/gates/README.md)。

## 文档

| 文档 | 内容 |
| ---- | ---- |
| [TC 语言及编译器标准 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | 语言语义、诊断、编译器实现要求与平台档案的唯一权威 |
| [TC 原生编译器设计说明书 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、运行时、平台档案、后端与分期路线 |
| [TC 开发测试计划 0.0.46](docs/TC开发测试计划-0.0.46.md) | 阶段、门禁、覆盖与可追溯性 |
| [文档地图](docs/README.md) | 文档入口与阅读顺序 |

## 目标口径

- **32/64 位均为目标**；`usize`/`isize` 为平台字长类型，`ptr<T>`/`fnptr` 宽度为 `usize`。
- **80 码闭集**：65 个 `TC_CE_*` + 15 个 `TC_RE_*`；不得增删。
- 不提供 C 宿主公共库、嵌入 API/ABI；`tc-vm`/`tc-aot` 是唯一工具入口。

## 仓库结构

```text
docs/                 标准、设计、测试计划、门禁状态
src/                  0.0.46 实现
  base/               assert / version
  contract/           契约检查 / 零 pass 策略
  diagnostic/         80 码闭集 / 生成表 / 首诊选择
  frontend/           input num lex parse module sem ct irgen
  ir/                 type instr operand block ir region object address structs pointer_state print
  runtime/            alloc io trap profile module_loader；semantics/format/input
  vm/                 vm.h interp memory（五区/对象/指针状态/全部 opcode）
  backend/            common（目标无关 freestanding 运行时核/target 描述）
                      aarch64（全量后端 + semihosting/PL011 平台实例）
                      arm32（Cortex-M7 后端 + MPS2 平台实例）；riscv32（后续）
  driver/             tc-vm / tc-aot CLI 与 AOT 驱动
tests/                L0/L1/L2 测试、L3/L4 一致性、harness、可追溯性、golden、确定性
```

## 构建与测试

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

S7/S8/S9 差分/工具链测试默认要求 `clang`、`llvm-ar`（或 `aarch64-elf-ar`）、`ld.lld`、
`qemu-system-aarch64` 与 `qemu-system-arm`；工具缺失时配置失败。
`-DTC_ENABLE_AOT_TESTS=OFF` 只关闭依赖 lld/qemu 的 ctest 项；构建目标运行时仍需要
clang 与 llvm-ar/aarch64-elf-ar，且不用于门禁构建。
工具当前支持：

- `--syntax-only <source>`：只做 LEX/SYN；退出 0 表示无 LEX/SYN 诊断，**不是程序有效**。
- `--sem-only <source>`：执行 LEX/SYN/IMPORT/SEM（含 CFG/DI、调用、安全与注解）；
  退出 0 仍不代表程序有效，CT/IR/代码生成未运行。
- `--static-only <source>`：执行全部静态阶段（含 CT 与 IR 最终化）；
  退出 0 仍不代表程序有效，代码生成未运行。
- 普通 `tc-vm` 调用：静态阶段通过后执行参考 VM；正常终止退出 0，首个
  `TC_RE_*` trap 以 `<file>:<line>:<column>: error[RT]: <code>: <print-name>`
  报出并退出 1。`--profile=w32` 选择 32 位档案，`--checked` 增加 UB 访问检查
  （不改变必选 trap），`--opt=O0|O2` 为零 pass 基线、语义一致。
- 普通 `tc-aot` 调用：静态阶段通过后发射目标汇编到 stdout；`--emit=asm`
  （默认）/`--emit=obj`/`--emit=elf` 与 `--output=<path>` 生成 `.s`/ELF 目标文件/ELF
  可执行文件，`--checked` 生成 checked build，`--opt=O0|O2` 为零 pass 基线，
  `--profile=` 选择 `aarch64-linux-user`/`aarch64-baremetal`/`arm32-baremetal`
  （`w32` 仅 VM 静态档案），`--device=reject|read|readwrite|volatile` 覆盖平台实例
  的设备区策略。无后端/工具链映射的档案显式退出 70，不发布错误产物。
- AOT 平台实例：`aarch64-linux-user` 使用 qemu-system-aarch64 + semihosting；
  `aarch64-baremetal` 使用 PL011 UART0/UART1 + PSCI；`arm32-baremetal` 使用
  qemu-system-arm MPS2 AN500（semihosting stdout、CMSDK UART0 退出记录）。
  qemu-user/Linux-user 用户态运行时不在本阶段的门禁路径内；差分比较程序 stdout、
  首个 trap 码与退出策略，且只在同一档案内进行。

文件/资源失败的实现级错误使用 `tc_error_io`/`tc_error_internal`/`TC_ERR_OUT_OF_MEMORY`，
不伪装成语言诊断。实现上限：单源文件 4 MiB、单 Token 16 KiB、类型/块嵌套 256 层；
超出返回 `tc_error_internal`（退出 71）。内置档案的帧区/栈按平台实例定义
（默认 `D`：hosted 512、`aarch64-baremetal`/`w32` 256、`arm32-baremetal` 32）；
程序静态帧足迹耗尽该区域属实现错误（退出 71），不是语言诊断。目标选择、ABI、
区基址与运行时缝的当前约定以及契约/测试口径见 [src/CONTRACTS.md](src/CONTRACTS.md)
与 [tests/README.md](tests/README.md)。

## 参与

- [CONTRIBUTING.md](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)）
- [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）
- [CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）

## 许可

见 [LICENSE](LICENSE)。
