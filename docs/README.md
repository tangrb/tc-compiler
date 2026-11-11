# 文档地图

本目录存放 TC-Compiler **正式设计与规范文档**。English map: [README.en.md](README.en.md)。

入门请先阅读仓库根目录 [README.md](../README.md) 与 [`examples/`](../examples/)，再按需深入下列文档。

## 用户文档

| 文档 | 用途 |
| ---- | ---- |
| [TC 语言及编译器标准-0.0.46.md](TC语言及编译器标准-0.0.46.md) | 0.0.46 语法、语义、诊断与编译器实现规范的**唯一权威**（语言篇 §0–§13、编译器篇 §14–§26；实现基线 0.0.44） |
| [TC-VM 命令行参考-0.0.44.md](TC-VM命令行参考-0.0.44.md) | `tc-vm` 用法、输出与退出行为 |
| [TC-Embed 详细设计说明书-0.0.44.md](TC-Embed详细设计说明书-0.0.44.md) | C↔TC 嵌入 API（含用户向 API 说明） |
| [libtc 设计说明书-0.0.44.md](libtc设计说明书-0.0.44.md) | 嵌入式库生命周期与调用者 API 速查（§15） |

## 贡献者 / 实现者文档

| 文档 | 用途 |
| ---- | ---- |
| [TC-VM 详细设计说明书-0.0.44.md](TC-VM详细设计说明书-0.0.44.md) | VM 流水线、IR、CFG、执行器 |
| [TC-AOT 详细设计说明书-0.0.44.md](TC-AOT详细设计说明书-0.0.44.md) | C99 生成、runtime shim、差分验证 |

> 编译器实现规范（13 步管线、诊断优先级、平台档案）见合并标准的编译器篇 §14–§26。

## 仓库级文档（根目录）

| 文档 | 用途 |
| ---- | ---- |
| [README.md](../README.md)（[English](../README.en.md)） | 入门、测试与质量门禁、**GitHub CI / 覆盖率** |
| [CHANGELOG.md](../CHANGELOG.md)（[English](../CHANGELOG.en.md)） | 用户可见变更记录 |
| [CONTRIBUTING.md](../CONTRIBUTING.md)（[English](../CONTRIBUTING.en.md)） | 贡献流程与测试要求 |
| [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md)（[English](../CODE_OF_CONDUCT.en.md)） | 社区行为准则 |
| [SECURITY.md](../SECURITY.md)（[English](../SECURITY.en.md)） | 漏洞报告 |
| [release-checklist.md](release-checklist.md)（[English](release-checklist.en.md)） | 发版检查清单 |
| [AGENTS.md](../AGENTS.md) | Cursor Agent 入口（维护者可选） |

当前实现版本：**v0.0.44**（Phase 1–7 与 0.0.44 设计文档对齐）。**0.0.45 未发布且已废弃**。0.0.46 标准已定稿并为唯一事实源：[TC语言及编译器标准-0.0.46.md](TC语言及编译器标准-0.0.46.md)（语言篇 §0–§13、编译器篇 §14–§26，含 §12 完整诊断码表与 §26 平台档案；实现尚未跟进）。
