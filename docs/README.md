# 文档地图

本目录存放 TC-Compiler **正式设计与规范文档**。English map: [README.en.md](README.en.md)。

入门请先阅读仓库根目录 [README.md](../README.md) 与 [`examples/`](../examples/)，再按需深入下列文档。

## 用户文档

| 文档 | 用途 |
| ---- | ---- |
| [TC 语言标准设计说明书-0.0.44.md](TC语言标准设计说明书-0.0.44.md) | 语法、语义与诊断的**唯一权威** |
| [TC-VM 命令行参考-0.0.44.md](TC-VM命令行参考-0.0.44.md) | `tc-vm` 用法、输出与退出行为 |
| [TC-Embed 详细设计说明书-0.0.44.md](TC-Embed详细设计说明书-0.0.44.md) | C↔TC 嵌入 API（含用户向 API 说明） |
| [libtc 设计说明书-0.0.44.md](libtc设计说明书-0.0.44.md) | 嵌入式库生命周期与调用者 API 速查（§15） |

## 贡献者 / 实现者文档

| 文档 | 用途 |
| ---- | ---- |
| [TC 编译器标准设计说明书-0.0.44.md](TC编译器标准设计说明书-0.0.44.md) | 13 阶段管线、诊断优先级、调用图 |
| [TC-VM 详细设计说明书-0.0.44.md](TC-VM详细设计说明书-0.0.44.md) | VM 流水线、IR、CFG、执行器 |
| [TC-AOT 详细设计说明书-0.0.44.md](TC-AOT详细设计说明书-0.0.44.md) | C99 生成、runtime shim、差分验证 |

## 过程记录

| 文档 | 用途 |
| ---- | ---- |
| [TC-0.0.44-语言标准符合性审计报告.md](TC-0.0.44-语言标准符合性审计报告.md) | **符合性审计**：以语言标准为唯一权威核对全部设计文档与 `src/` 实现；含既有未关闭项复核与新增发现的最小复现 |
| [TC-0.0.44-符合性整改进度台账.md](TC-0.0.44-符合性整改进度台账.md) | **整改进度台账**：阶段 1 设计文档对齐与阶段 2 实现逐条修复的状态、提交记录与待裁决项（审计报告保持纯清单） |

> 历史过程文档（0.0.41 分析报告／修复计划、0.0.42 遗留问题清零计划、0.0.42 规范文本）已从仓库移除；需要时用发布标签恢复，例如 `git show v0.0.42:docs/TC语言标准设计说明书-0.0.42.md`。

## 仓库级文档（根目录）

| 文档 | 用途 |
| ---- | ---- |
| [CHANGELOG.md](../CHANGELOG.md)（[English](../CHANGELOG.en.md)） | 用户可见变更记录 |
| [CONTRIBUTING.md](../CONTRIBUTING.md)（[English](../CONTRIBUTING.en.md)） | 贡献流程与测试要求 |
| [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md)（[English](../CODE_OF_CONDUCT.en.md)） | 社区行为准则 |
| [SECURITY.md](../SECURITY.md)（[English](../SECURITY.en.md)） | 漏洞报告 |
| [release-checklist.md](release-checklist.md)（[English](release-checklist.en.md)） | 发版检查清单 |
| [AGENTS.md](../AGENTS.md) | Cursor Agent 入口（维护者可选） |

当前核心版本：**v0.0.43**（语言规范与**全部设计文档**均已同步为 **0.0.44**；0.0.42 仅保留为历史基线文档）。
