# TC-Compiler

TC **0.0.46** 语言及编译器规范 + 原生编译器（TC → ASM → 机器码）目标设计。
0.0.46 为 clean-sheet 实现基线：只保留标准、设计、测试计划与按设计重写的实现；
历史实现与旧工具已移除，需要时从 git tag 获取。
`tc-0.0.47` 分支目标是在语言语义、opcode、诊断码与 ABI 不变的前提下交付开发调试工具链
与工程模型；两份设计已达到冻结条件，P0/T0、P1、P2、P3、P4a、T1、T2 与 T3
已完成，P4b 本地实现与四种 profile 的真实 gdb 验证已完成；固定 Linux CI
真实签名 artifact 待完成，P4b 正式验收与 G11 未完成；P5 与 T4–T6 尚未开始。
范围与契约见 [TC 开发调试工具链 0.0.47](docs/TC开发调试工具链-0.0.47.md) 与
[TC 工程模型 0.0.47](docs/TC工程模型-0.0.47.md)；RTOS、中断与并发移交 0.0.48。

## 当前状态

- **0.0.47 工具链与观测/源级映射**：工具链版本 0.0.47 与语言/契约版本 0.0.46/1 分离；私有 `tc_core` 链接
  `tc`/`tc-vm`/`tc-aot` 三个入口；实现级错误统一映射为 0/2/70/71。
  `tc` 已实现工程 CLI 语法、`--help`/`--version` 与 `--init-project` 完整模板、
  原子写入、回滚和身份检查。编译器已消费 `tc.project` v1、canonical 工程根、有序
  include 根与缺省 profile，并按父目录 fd 锚定模块身份；诊断、IR、VM trap 与 AOT
  源字符串使用工程根相对显示路径，源文件与 import 候选按绑定 fd 非阻塞有界读取。
  `tc --test` 已实现一层发现/筛选、fd 锚定 beneath 解析、严格侧车校验、私有只读
  快照、串行 fork worker、超时、流捕获、结构化结果、报告与退出映射。
  P4a 已实现共享确定性行表与私有 AOT 调试发射：`.file`/`.asciz`/`.loc`、源注释、
  最小 DWARF v4 CU 与 clang `.debug_line`；诊断、输入/输出身份与 stdio 保护在发布前完成。
  T1 已实现 VM/AOT `--dump-ir[=<path>]`、VM `--trace=ir,call,mem,io,trap`
  （`text`/`jsonl`、`--trace-limit=<n>` 与 `--trace-out=<path>`）、VM/AOT `--stats`
  与 `--stats-out=<path>`、结构化 trap 上下文、显式输出身份检查与预留保护，以及
  确定性观测输出；证据入口为 `t1.observe`。T2 已实现 VM/AOT
  `--dump-linemap[=<path>]` 与公开 `tc-aot --debug`，复用最终 IR 共享行表输出
  JSON v3、VM 合成地址、`.loc`/源注释与 DWARF v4 CU，并以 qemu gdbstub 和真实
  gdb 验证 `info line`、源码断点与单步；证据入口为 `t2.debug` 与 `t2.gdb`。
  T3 已实现 `tc-vm --gdb[=<port>]`（默认 0，仅 loopback），并要求
  `--gdb-stdout-journal=<dir>`；提供合成 IR 地址、`target.xml`/内存映射、
  断点/单步/继续、monitor、extended restart、原子 stdout journal 与可恢复 stdin。
  `--gdb-allow-write` 才允许受限写入；证据入口为 `gdb_journal.unit`、
  `gdb_rsp.unit`、`vm.gdb.protocol`（242 checks）、`vm.gdb.semantics` 与
  `vm.gdb.e2e`。P4b 已实现 `tc-vm --gdb-elf-out=<path>`：私有内部 writer
  直接生成只含符号与 DWARF v4 行表的 ELF，支持 AArch64、ARM32 与 RISC-V32 档案，
  不调用外部工具；四种档案均经独立语义解码，`toolchain.debug_elf` 已在四种
  profile 通过真实 gdb 调试 ELF、源级断点、64 位 `tc_ir_pc` 读取、IR 单步与
  程序语义回归。合成 `tc_ir_pc` 使用 64 位 `uint64`，架构 pc 保持 32 位
  `code_ptr`，ARM/RISC-V 的 Z0 kind=2 已被接受。ARM 客户端需
  `set arm force-mode thumb`；GDB 18.1 的 ARM `stepi` 因未建模 sp 不可直接使用，
  测试通过 maintenance packet `s` 与 flush register-cache 验证同一 RSP 单步。
- **后续阶段**：P4b 正式验收尚待 §6.10 固定 Linux CI 真实签名证据；
  P5 与 T4–T6 尚未完成，G11 未完成。
- **已实现**：契约层、LEX/SYN 与扁平 parse IR、模块发现/IMPORT、名称/类型/转换/布局、
  静态指令形状检查、const_expr 形态、CFG/可达性/DI、缺返回、调用顺序/安全、安全上下文与注解矩阵；
  CT 求值（按需求值/缓存、严格错误映射、静态初值）、IR 最终化/冻结与确定性打印；
  W 相关运行时语义核（整数/浮点/转换、§11.2 精确格式化、§11.3 输入）、参考 VM
  （五区/对象与字段子对象、指针状态、全部 51 个 opcode、调用/`D`、I/O、必选 trap、
  提交语义）；32/64 档案，VM 可执行 `aarch64-linux-user`、`aarch64-baremetal`、
  `arm32-baremetal`、`riscv32-baremetal` 与 `w32`；
  W=64 AArch64 全量后端（整数/位/比较/浮点/转换、指针/内存/struct、全部 51 个 opcode、
  AAPCS64 复合/HFA/大复合按引用指针/大 struct 间接返回、`fnptr`/`call_indirect`、多尺寸与未对齐访问、
  checked build）；freestanding 目标运行时（对象表/指针状态/必选定检/checked shadow、
  确定性 I/O、分配器、trap/退出），`aarch64-baremetal` 使用 PL011 UART0 输出与分帧输入、
  PL011 UART1 退出记录与 PSCI SYSTEM_OFF（无 semihosting）；首个 32 位目标
  `arm32-baremetal`（Cortex-M7 MPS2 AN500、AAPCS32/ILP32 softfp、独立 ARM32
  后端与全部 51 个 opcode、`int64` 辅助、软件 binary64、未对齐访问、四设备策略、
  qemu-system-arm 执行）；S10 新增第二个 32 位目标 `riscv32-baremetal`
  （RV32IMAFDC、ILP32 soft-float ABI、独立 RISC-V 后端与全部 51 个 opcode、
  `__divdi3`/`__udivdi3` 等 64 位辅助、NS16550A UART0 输出与分帧输入、
  semihosting 退出记录、SiFive test finisher、qemu-system-riscv32 virt）；
  复位入口在 `tc_main` 前清零 BSS、区基址、静态区 `.data`、ELF/flat 产物与
  clang/lld 驱动；远距离分支长跳转（RISC-V auipc/jalr，AArch64/ARM32
  条件跳过 + 无条件转移）；
  全量 VM↔AOT 差分/ABI/一致性/产物/确定性套件、C caller/TC caller 双向
  AAPCS64/AAPCS32/RISC-V ILP32 互操作、32/64 分档案隔离、S10 长期回归语料、
  发布审计、生成式 ABI/布局/内存/浮点 fuzz、实现边界/鲁棒性/checked/规模/
  长跳转回归。
- **未实现**：可选优化管线（SSA/regalloc/内联）；0.0.46 基线保持零 pass。
  P4b 正式验收所需的固定 Linux CI 证据、P5 与 T4–T6 不在当前完成范围内。
- **当前验证**：0.0.47 CTest 注册总数 92（P0 基线 73 + P1 三项 + P2 三项 + P3 一项 + P4a 两项 + P4b 一项 + T1 一项 + T2 两项 + T3 五项 + G11 证据一项）；
  G11 证据收集/校验与手动签名作业已接入，四种 profile 的本地真实 gdb 验证已完成，
  固定 Linux CI 真实签名 artifact 仍待完成，P4b 正式验收与 G11 未完成。
  0.0.46 已验收 harness 873/873、覆盖矩阵 636/636，G0–G10 PASS；**G10 完成 0.0.46 阶段出口，G11 未完成**。
  详见 [门禁状态](docs/gates/README.md)。

## 文档

| 文档 | 内容 |
| ---- | ---- |
| [TC 语言及编译器标准 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | 语言语义、诊断、编译器实现要求与平台档案的唯一权威 |
| [TC 原生编译器设计说明书 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、运行时、平台档案、后端与分期路线 |
| [TC 开发测试计划 0.0.46](docs/TC开发测试计划-0.0.46.md) | 阶段、门禁、覆盖与可追溯性 |
| [TC 开发调试工具链 0.0.47](docs/TC开发调试工具链-0.0.47.md) | 工具链范围、接口契约、里程碑与验收；P0/T0–P4a、T1–T3 已实现，P4b 验收未完成 |
| [TC 工程模型 0.0.47](docs/TC工程模型-0.0.47.md) | 工程布局、`tc.project`、`tc` 初始化/测试、根锚点与路径规则；P0–P4a 已实现，P4b 验收未完成 |
| [文档地图](docs/README.md) | 文档入口与阅读顺序 |

## 目标口径

- **32/64 位均为目标**；`usize`/`isize` 为平台字长类型，`ptr<T>`/`fnptr` 宽度为 `usize`。
- **80 码闭集**：65 个 `TC_CE_*` + 15 个 `TC_RE_*`；不得增删。
- 不提供 C 宿主公共库、嵌入 API/ABI；私有入口为 `tc-vm`/`tc-aot` 两个编译器入口
  与 `tc` 工程工具，见
  [TC 工程模型-0.0.47.md](docs/TC工程模型-0.0.47.md)。

## 仓库结构

```text
docs/                 标准、设计、测试计划、门禁状态
src/                  0.0.46 实现
  base/               assert / version / canonical 与显示路径上下文
  contract/           契约检查 / 零 pass 策略
  diagnostic/         80 码闭集 / 生成表 / 首诊选择
  frontend/           input num lex parse module sem ct irgen
  ir/                 type instr operand block ir region object address structs pointer_state print
  runtime/            alloc io trap profile module_loader；semantics/format/input
  vm/                 vm.h interp memory observe gdb gdb_rsp gdb_journal（五区/对象/指针状态/全部 opcode/GDBstub）
  backend/            common（目标无关 freestanding 运行时核/target 描述）
                      aarch64（全量后端 + semihosting/PL011 平台实例）
                      arm32（Cortex-M7 后端 + MPS2 平台实例）
                      riscv32（RISC-V 32 后端 + QEMU virt 平台实例）
  driver/             tc / tc-vm / tc-aot CLI、AOT 驱动与测试 runner
  project/            名称校验、manifest/根发现、路径绑定、原子工程初始化
tests/                L0–L7：单元、静态/运行时一致性、差分/ABI/平台、harness、
                      可追溯性、golden、确定性
```

## 构建与测试

```sh
cmake -S . -B .tmp/build
cmake --build .tmp/build
ctest --test-dir .tmp/build --output-on-failure
```

S7/S8/S9/S10 差分/工具链测试默认要求 `clang`、`llvm-ar`（或对应 target ar）、
`ld.lld`、`qemu-system-aarch64`、`qemu-system-arm` 与 `qemu-system-riscv32`；
P4a 的 DWARF 校验另需 `llvm-dwarfdump`，T2/T3 真实 gdb 冒烟另需 `gdb`（或
`gdb-multiarch`）；工具缺失时配置失败。
`-DTC_ENABLE_AOT_TESTS=OFF` 只关闭依赖 lld/qemu 的 ctest 项；构建目标运行时仍需要
clang 与 llvm-ar（或对应 target ar），且不用于门禁构建。
工具当前支持：

- `tc --help`/`--version`：工程工具帮助与双版本输出；`tc --init-project [--] <name>`
  在当前 canonical 目录生成完整模板，仅创建缺失目标，支持身份预检与失败回滚。
  `tc --test [--test-timeout=<seconds>] [--] [test_name...]` 在工程根内执行
  `tests/*/main.tc`，按名称字节序串行运行并报告通过/失败；缺省或 `0` 超时不设期限。
- `--syntax-only <source>`：只做 LEX/SYN；退出 0 表示无 LEX/SYN 诊断，**不是程序有效**。
- `--sem-only <source>`：执行 LEX/SYN/IMPORT/SEM（含 CFG/DI、调用、安全与注解）；
  退出 0 仍不代表程序有效，CT/IR/代码生成未运行。
- `--static-only <source>`：执行全部静态阶段（含 CT 与 IR 最终化）；
  退出 0 仍不代表程序有效，代码生成未运行。
- 普通 `tc-vm` 调用：静态阶段通过后执行参考 VM；正常终止退出 0，首个
  `TC_RE_*` trap 以 `<file>:<line>:<column>: error[RT]: <code>: <print-name>`
  报出并退出 1。`--profile=w32` 选择 32 位档案，`--checked` 增加 UB 访问检查
  （不改变必选 trap），`--opt=O0|O2` 为零 pass 基线、语义一致。
  T1 观测支持 `--dump-ir[=<path>]`、`--trace=ir,call,mem,io,trap`（text/jsonl、
  limit/output）与 `--stats`（含独立输出）；T2 支持 `--dump-linemap[=<path>]`，
  JSON v3 按最终 IR 全局指令序输出，VM 额外给出档案对应的合成指令地址。
  T3 支持 `--gdb[=<port>]`（默认 0，仅 loopback），必须同时给出
  `--gdb-stdout-journal=<dir>`；提供断点/单步/继续、monitor、extended restart、
  原子 stdout journal 与可恢复 stdin，默认只读，`--gdb-allow-write` 开放受限写入。
- 普通 `tc-aot` 调用：静态阶段通过后发射目标汇编到 stdout；`--emit=asm`
  （默认）/`--emit=obj`/`--emit=elf`/`--emit=flat` 与 `--output=<path>` 生成
  `.s`/ELF 目标文件/ELF 可执行文件/裸二进制
  可执行文件，`--checked` 生成 checked build，`--opt=O0|O2` 为零 pass 基线，
  `--profile=` 选择 `aarch64-linux-user`/`aarch64-baremetal`/`arm32-baremetal`/
  `riscv32-baremetal`（`w32` 仅 VM 静态档案），
  `--device=reject|read|readwrite|volatile` 覆盖平台实例的设备区策略。
  T1 观测支持 `--dump-ir[=<path>]` 与 `--stats`（含独立输出）；T2 支持
  `--dump-linemap[=<path>]` 与 `--debug`，后者为每条 IR 指令发射 `.loc`/源注释，
  并在 ELF/目标文件中生成最小 DWARF v4 CU 与 `.debug_line`；flat 产物不含最终调试节。
  无后端/工具链映射的档案显式退出 70，不发布错误产物。
- AOT 平台实例：`aarch64-linux-user` 使用 qemu-system-aarch64 + semihosting；
  `aarch64-baremetal` 使用 PL011 UART0（stdout/stdin）/UART1 + PSCI；`arm32-baremetal` 使用
  qemu-system-arm MPS2 AN500（semihosting stdout、CMSDK UART0 退出记录）；
  `riscv32-baremetal` 使用 qemu-system-riscv32 virt（NS16550A UART0 stdout/stdin、
  semihosting 退出记录、SiFive test finisher 终止）。
  qemu-user/Linux-user 用户态运行时不在本阶段的门禁路径内；差分比较程序 stdout、
  首个 trap 码与退出策略，且只在同一档案内进行。

实现级错误不伪装成语言诊断，统一映射为：`TC_IMPL_OK`→0、`TC_IMPL_USAGE`→2、
`TC_IMPL_IO`/`TC_IMPL_UNAVAILABLE`/`TC_IMPL_OOM`→70、`TC_IMPL_INTERNAL`→71。
OOM 固定输出 `<tool>: TC_ERR_OUT_OF_MEMORY: OutOfMemory: memory allocation failed`。
实现上限：单源文件 4 MiB、单 Token 16 KiB、类型/块嵌套 256 层；
超出返回 `tc_error_internal`（退出 71）。内置档案的帧区/栈按平台实例定义
（默认 `D`：hosted 512、`aarch64-baremetal`/`w32` 256、`arm32-baremetal` 32、
`riscv32-baremetal` 64）。VM 的帧由宿主内存承载；AOT 额外检查
`frame_total*(D+1) <= stack_size`，超出时以实现错误（退出 71）拒绝，不发布产物，
这不是语言诊断。ARM32 代码窗口为 1 MiB，超窗口函数在链接阶段拒绝；单函数
30000 指令已有 far-branch 回归，更大规模受宿主内存及汇编/链接工具处理能力影响，
不纳入门禁也不宣称支持。目标选择、ABI、区基址与运行时缝的当前约定以及契约/测试
口径见 [src/CONTRACTS.md](src/CONTRACTS.md) 与 [tests/README.md](tests/README.md)。

## 参与

- [CONTRIBUTING.md](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)）
- [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）
- [CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）

## 许可

见 [LICENSE](LICENSE)。
