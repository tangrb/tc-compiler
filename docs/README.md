# 文档地图

本目录保留 0.0.46 标准、设计、开发测试计划、0.0.47 工具链与工程模型、以及当前门禁状态。
English: [README.en.md](README.en.md)。

## 核心文档

| 文档 | 用途 |
| ---- | ---- |
| [TC 语言及编译器标准-0.0.46.md](TC语言及编译器标准-0.0.46.md) | 0.0.46 语法、语义、诊断、编译器实现要求与平台档案的唯一权威（§0–§14；80 码） |
| [TC 原生编译器设计说明书-0.0.46.md](TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、语义运行时、平台档案、后端与分期路线 |
| [TC开发测试计划-0.0.46.md](TC开发测试计划-0.0.46.md) | S0–S10 阶段、门禁、80 码/opcode/章节覆盖与可追溯性 |
| [TC 开发调试工具链-0.0.47.md](TC开发调试工具链-0.0.47.md) | 0.0.47 工具链范围、接口契约、里程碑与验收；语言语义仍为 0.0.46；P0/T0–P4a、T1–T3 已实现，P4b 验收未完成 |
| [TC 工程模型-0.0.47.md](TC工程模型-0.0.47.md) | 0.0.47 工程布局、`tc.project`、`tc` 初始化/测试、根锚点与路径规则；P0–P4a 已实现，P4b 验收未完成 |
| [门禁状态](gates/README.md) | 已完成门禁的当前结论、证据入口与后续阶段边界 |
| [ABI 规则矩阵](../tests/conformance/abi/README.md) | AAPCS64/AAPCS32/RISC-V ILP32 参考版本、双向 C/TC 互操作、规则覆盖与明确范围限制 |

## 当前状态

- 0.0.47 工具链与工程模型两份设计达到冻结条件；P0/T0、P1、P2、P3、P4a、T1、T2 与 T3 已完成。
  `tc --init-project` 已实现完整模板、原子写入、回滚与身份检查；编译器已消费
  `tc.project`、canonical 根、有序 include 根与 manifest 缺省 profile，并将
  canonical 路径与工程根相对显示路径分离。诊断、IR、VM trap 与 AOT 源字符串
  使用显示路径；模块身份由绑定父目录和实际目录项名组成，源读取采用非阻塞有界 fd。
  `tc --test` 已实现一层发现/筛选、fd 锚定 beneath 解析、严格侧车、私有只读快照、
  fork worker、超时、流捕获、结构化结果、报告与退出映射。P4a 已实现共享确定性
  行表与私有 AOT `.loc`/源注释、最小 DWARF v4 CU 和 clang `.debug_line`。T1 已实现
  VM/AOT `--dump-ir`、VM `--trace=ir,call,mem,io,trap`（text/jsonl、limit/output）、
  VM/AOT `--stats`、结构化 trap 上下文、显式输出身份/预留保护与确定性观测输出，
  证据入口为 `t1.observe`。T2 已实现 VM/AOT `--dump-linemap[=<path>]` 与公开
  `tc-aot --debug`，复用最终 IR 共享行表输出 JSON v3、VM 合成地址、`.loc`/源注释
  与 DWARF v4 CU，并以 qemu gdbstub 和真实 gdb 验证 `info line`、源码断点与
  单步，证据入口为 `t2.debug` 与 `t2.gdb`。T3 已实现 `tc-vm --gdb[=<port>]`
  （默认 0，仅 loopback）与强制 `--gdb-stdout-journal=<dir>`，支持合成 IR 地址、
  `target.xml`/内存映射、断点/单步/继续、monitor、extended restart、原子 stdout
  journal 与可恢复 stdin；`--gdb-allow-write` 才开放受限写入，证据入口为
  `gdb_journal.unit`、`gdb_rsp.unit`、`vm.gdb.protocol`（242 checks）、
  `vm.gdb.semantics` 与 `vm.gdb.e2e`。P4b 已实现 `tc-vm --gdb-elf-out=<path>`，
  由私有 writer 生成符号与 DWARF v4 行表，四种档案均经独立语义解码；
  `toolchain.debug_elf` 已在四种 profile 通过真实 gdb 调试 ELF、源级断点、
  64 位 `tc_ir_pc` 读取、IR 单步与程序语义回归。合成 `tc_ir_pc` 使用 64 位
  `uint64`，架构 pc 保持 32 位 `code_ptr`，ARM/RISC-V 的 Z0 kind=2 已被接受。
  G11 证据收集/校验与手动签名作业已接入；固定 Linux CI 真实签名 artifact
  尚未完成，P4b 正式验收与 G11 未完成，P5 与 T4–T6 尚未开始；
  语言/契约保持 0.0.46/1，RTOS、中断与并发移交 0.0.48。
- S0–S10 已实现并通过 G0–G10；**G10 完成 0.0.46 阶段出口**。0.0.47 CTest
  注册总数为 92（P0 基线 73 + P1 三项 + P2 三项 + P3 一项 + P4a 两项 + P4b 一项 + T1 一项 + T2 两项 + T3 五项 + G11 证据一项）。
  0.0.46 覆盖矩阵为 636/636。
- 已实现：前端、CT/IR、运行时语义核与参考 VM（32/64 档案）、W=64 AArch64、
  W=32 ARM32 与 W=32 RISC-V 全量后端/工具链（全量 ABI、checked build、
  ELF/区布局、复位入口 BSS 清零、远距离分支长跳转）、freestanding 目标运行时核与
  `aarch64-baremetal`/`arm32-baremetal`/`riscv32-baremetal` 平台实例、四设备策略、
  32/64 分档案隔离、全量差分/ABI/一致性/产物/确定性套件、生成式 ABI fuzz、
  实现边界/checked/规模/长跳转回归、S10 长期回归语料与发布审计。
- 未实现：可选优化管线（SSA/regalloc/内联）。
- 不提供 C 宿主公共库、嵌入 API/ABI；私有入口仅为 `tc`/`tc-vm`/`tc-aot`。
- 32/64 位均为目标；`usize`/`isize` 为平台字长类型（§13.2.1/§13.2.2）。

## 仓库级文档

| 文档 | 用途 |
| ---- | ---- |
| [README.md](../README.md)（[English](../README.en.md)） | 项目入口、构建与当前状态 |
| [CHANGELOG.md](../CHANGELOG.md)（[English](../CHANGELOG.en.md)） | 0.0.46 实现与 0.0.47 当前交付状态 |
| [CONTRIBUTING.md](../CONTRIBUTING.md)（[English](../CONTRIBUTING.en.md)） | 贡献流程 |
| [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md)（[English](../CODE_OF_CONDUCT.en.md)） | 行为准则 |
| [SECURITY.md](../SECURITY.md)（[English](../SECURITY.en.md)） | 安全问题报告 |
| [AGENTS.md](../AGENTS.md) | Agent 工作指南 |

历史实现与旧设计文档只在 git tag 中，不在本分支维护。
