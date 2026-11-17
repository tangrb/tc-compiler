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
  提交语义）；32/64 档案，VM 可执行 `aarch64-linux-user`、`aarch64-baremetal` 与 `w32`；
  W=64 AArch64 全量后端（整数/位/比较/浮点/转换、指针/内存/struct、全部 51 个 opcode、
  AAPCS64 复合/HFA/大 struct 间接返回、`fnptr`/`call_indirect`、多尺寸与未对齐访问、
  checked build）；freestanding AArch64 运行时（对象表/指针状态/必选定检/checked
  shadow、确定性 I/O、分配器、trap/退出）；复位入口、区基址、静态区 `.data`、ELF
  产物（禁用 ICF）与 clang/lld/qemu 驱动；`tc-vm`/`tc-aot` 的 checked build，以及
  全量 VM↔AOT 差分/ABI/一致性/产物/确定性套件。
- **未实现**：freestanding/裸机平台实例（PL011 UART、裸机链接/设备策略）与 32 位
  后端/ABI；这些属 S9。
- 当前门禁：G0–G8 PASS；**G8 允许进入 S9**。详见 [门禁状态](docs/gates/README.md)。

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
  backend/            aarch64（全量后端 + freestanding 运行时）/ common（target 描述）
                      arm32 riscv32（后续阶段）
  driver/             tc-vm / tc-aot CLI 与 AOT 驱动
tests/                L0/L1/L2 测试、L3/L4 一致性、harness、可追溯性、golden、确定性
```

## 构建与测试

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

S7/S8 差分/工具链测试默认要求 `clang`、`llvm-ar`（或 `aarch64-elf-ar`）、`ld.lld`
与 `qemu-system-aarch64`；工具缺失时配置失败。`-DTC_ENABLE_AOT_TESTS=OFF`
只关闭依赖 lld/qemu 的 ctest 项；构建 AArch64 运行时仍需要 clang 与
llvm-ar/aarch64-elf-ar，且不用于门禁构建。
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
- 普通 `tc-aot` 调用：静态阶段通过后发射 AArch64 汇编到 stdout；`--emit=asm`
  （默认）/`--emit=obj`/`--emit=elf` 与 `--output=<path>` 生成 `.s`/ELF 目标文件/ELF
  可执行文件，`--checked` 生成 checked build，`--opt=O0|O2` 为零 pass 基线。
  当前 hosted 平台实例之外的能力（`w32` 与非 `TC_DEV_REJECT` 档案）显式退出 70，
  不发布错误产物。
- S7 的 AOT 平台实例为 qemu-system-aarch64 + semihosting；qemu-user/Linux-user
  用户态运行时尚未实现，不能把该 ELF 当作 Linux 用户程序直接执行。
  差分比较程序 stdout、首个 trap 码与退出策略。

文件/资源失败的实现级错误使用 `tc_error_io`/`tc_error_internal`/`TC_ERR_OUT_OF_MEMORY`，
不伪装成语言诊断。实现上限：单源文件 4 MiB、单 Token 16 KiB、类型/块嵌套 256 层；
超出返回 `tc_error_internal`（退出 71）。内置档案的帧区/栈为 16 MiB
（默认 `D`：hosted 512，baremetal/w32 256）；程序静态帧足迹耗尽该区域属实现错误
（退出 71），不是语言诊断。AOT 使用 W=64 `aarch64-linux-user` 的 qemu-system
semihosting 平台实例；freestanding/裸机与 32 位后端属 S9。契约与测试口径见
[src/CONTRACTS.md](src/CONTRACTS.md) 与 [tests/README.md](tests/README.md)。

## 参与

- [CONTRIBUTING.md](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)）
- [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）
- [CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）

## 许可

见 [LICENSE](LICENSE)。
