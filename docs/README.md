# 文档地图

本目录保留 **0.0.46** 标准、设计文档与评估报告（过程性）。English: [README.en.md](README.en.md)。

## 核心文档

| 文档 | 用途 |
| ---- | ---- |
| [TC 语言及编译器标准-0.0.46.md](TC语言及编译器标准-0.0.46.md) | 0.0.46 语法、语义、诊断、编译器实现要求与平台档案的**唯一权威**（§0–§14；80 码） |
| [TC 原生编译器设计说明书-0.0.46.md](TC原生编译器设计说明书-0.0.46.md) | TC → 汇编 → 机器码原生编译器的**目标架构**；扁平 TAC IR、语义运行时、平台档案、后端与分期路线 |
| [TC开发测试计划-0.0.46.md](TC开发测试计划-0.0.46.md) | 按 S0–S10 阶段组织的开发与验证计划；80 码、opcode、标准章节与设计 §12 的覆盖/门禁/追溯 |
| [TC-G0门禁报告-0.0.46.md](TC-G0门禁报告-0.0.46.md) | S0 契约冻结与测试基建的 G0 证据、检查表与显式挂起项 |

## 当前状态

- 分支：`tc-0.0.46` clean-sheet。
- S0 契约与测试基建已完成（G0 自动化证据通过）；`src/` 有契约层与 `tc-vm`/`tc-aot` 骨架，前端/VM/后端尚未实现。
- 不提供 C 宿主公共库、嵌入 API/ABI。
- 32/64 位均为 0.0.46 目标；`usize`/`isize` 为平台字长类型。标准与设计已按 32/64 双档案修订（§13.2.1/§13.2.2）。

## 仓库级文档

| 文档 | 用途 |
| ---- | ---- |
| [README.md](../README.md)（[English](../README.en.md)） | 项目入口、文档与骨架结构 |
| [CHANGELOG.md](../CHANGELOG.md)（[English](../CHANGELOG.en.md)） | 0.0.46 clean-sheet 变更 |
| [CONTRIBUTING.md](../CONTRIBUTING.md)（[English](../CONTRIBUTING.en.md)） | 贡献流程 |
| [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md)（[English](../CODE_OF_CONDUCT.en.md)） | 行为准则 |
| [SECURITY.md](../SECURITY.md)（[English](../SECURITY.en.md)） | 安全问题报告 |
| [AGENTS.md](../AGENTS.md) | Agent 工作指南 |

## 历史

历史实现与旧设计文档已从本分支移除；需要时从 git tag 恢复。本分支不再维护历史兼容。
