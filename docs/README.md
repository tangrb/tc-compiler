# 文档地图

本目录保留 0.0.46 标准、设计、开发测试计划、0.0.47 工具链与工程模型、以及当前门禁状态。
English: [README.en.md](README.en.md)。

## 核心文档

| 文档 | 用途 |
| ---- | ---- |
| [TC 语言及编译器标准-0.0.46.md](TC语言及编译器标准-0.0.46.md) | 0.0.46 语法、语义、诊断、编译器实现要求与平台档案的唯一权威（§0–§14；80 码） |
| [TC 原生编译器设计说明书-0.0.46.md](TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、语义运行时、平台档案、后端与分期路线 |
| [TC开发测试计划-0.0.46.md](TC开发测试计划-0.0.46.md) | S0–S10 阶段、门禁、80 码/opcode/章节覆盖与可追溯性 |
| [TC 开发调试工具链-0.0.47.md](TC开发调试工具链-0.0.47.md) | 0.0.47 工具链范围、接口契约、里程碑与验收；语言语义仍为 0.0.46 |
| [TC 工程模型-0.0.47.md](TC工程模型-0.0.47.md) | 0.0.47 工程布局、`tc.project`、`tc` 初始化/测试、根锚点与路径规则；P0/P1 已实现，P2/P3 未开始 |
| [门禁状态](gates/README.md) | 已完成门禁的当前结论、证据入口与后续阶段边界 |
| [ABI 规则矩阵](../tests/conformance/abi/README.md) | AAPCS64/AAPCS32/RISC-V ILP32 参考版本、双向 C/TC 互操作、规则覆盖与明确范围限制 |

## 当前状态

- 0.0.47 工具链与工程模型两份设计达到冻结条件；P0/T0 与 P1 已完成。
  `tc --init-project` 已实现完整模板、原子写入、回滚与身份检查；
  `src/project/manifest.c` 提供 `tc.project` v1 解析与最近根发现 API，但尚未接入编译器消费。
  P2–P5/T1–T6 尚未开始，`tc --test` 仍显式退出 70，G11 未完成；语言语义、opcode、
  诊断码与 ABI 保持 0.0.46 冻结，RTOS 能力移交 0.0.48。
- S0–S10 已实现并通过 G0–G10；**G10 完成 0.0.46 阶段出口**。0.0.47 CTest
  注册总数为 76（P0 基线 73 + P1 三项），P1 的 Debug/Release/ASan+UBSan
  全量回归均为 76/76；0.0.46 覆盖矩阵为 636/636。
- 已实现：前端、CT/IR、运行时语义核与参考 VM（32/64 档案）、W=64 AArch64、
  W=32 ARM32 与 W=32 RISC-V 全量后端/工具链（全量 ABI、checked build、
  ELF/区布局、复位入口 BSS 清零、远距离分支长跳转）、freestanding 目标运行时核与
  `aarch64-baremetal`/`arm32-baremetal`/`riscv32-baremetal` 平台实例、四设备策略、
  32/64 分档案隔离、全量差分/ABI/一致性/产物/确定性套件、生成式 ABI fuzz、
  实现边界/checked/规模/长跳转回归、S10 长期回归语料与发布审计。
- 未实现：可选优化管线（SSA/regalloc/内联）。
- 不提供 C 宿主公共库、嵌入 API/ABI。
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
