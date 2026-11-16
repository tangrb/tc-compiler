# 文档地图

本目录保留 0.0.46 标准、设计、开发测试计划与当前门禁状态。English: [README.en.md](README.en.md)。

## 核心文档

| 文档 | 用途 |
| ---- | ---- |
| [TC 语言及编译器标准-0.0.46.md](TC语言及编译器标准-0.0.46.md) | 0.0.46 语法、语义、诊断、编译器实现要求与平台档案的唯一权威（§0–§14；80 码） |
| [TC 原生编译器设计说明书-0.0.46.md](TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、语义运行时、平台档案、后端与分期路线 |
| [TC开发测试计划-0.0.46.md](TC开发测试计划-0.0.46.md) | S0–S10 阶段、门禁、80 码/opcode/章节覆盖与可追溯性 |
| [门禁状态](gates/README.md) | 已完成门禁的当前结论、证据入口与后续阶段边界 |

## 当前状态

- S0–S7 已实现并通过 G0–G7；**G7 允许进入 S8**。
- 已实现：前端、CT/IR、运行时语义核与参考 VM（32/64 档案）、AArch64 W=64
  子集后端/工具链与 VM↔AOT 差分/ABI 套件。
- 未实现：全量 ABI、checked build、freestanding/裸机与 32 位后端。
- 不提供 C 宿主公共库、嵌入 API/ABI。
- 32/64 位均为目标；`usize`/`isize` 为平台字长类型（§13.2.1/§13.2.2）。

## 仓库级文档

| 文档 | 用途 |
| ---- | ---- |
| [README.md](../README.md)（[English](../README.en.md)） | 项目入口、构建与当前状态 |
| [CHANGELOG.md](../CHANGELOG.md)（[English](../CHANGELOG.en.md)） | 0.0.46 变更 |
| [CONTRIBUTING.md](../CONTRIBUTING.md)（[English](../CONTRIBUTING.en.md)） | 贡献流程 |
| [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md)（[English](../CODE_OF_CONDUCT.en.md)） | 行为准则 |
| [SECURITY.md](../SECURITY.md)（[English](../SECURITY.en.md)） | 安全问题报告 |
| [AGENTS.md](../AGENTS.md) | Agent 工作指南 |

历史实现与旧设计文档只在 git tag 中，不在本分支维护。
