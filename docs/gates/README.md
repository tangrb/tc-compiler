# 门禁状态（0.0.46 / 0.0.47）

本目录只保留各阶段的当前门禁结论与可复现证据入口。逐轮审计、修复过程、历史 commit 与环境快照
不进入分支文档；需要追溯时使用 git 历史。

| 门禁 | 阶段 | 结论 | 记录 |
| ---- | ---- | ---- | ---- |
| G0 | S0 契约冻结与测试基建 | **PASS** | [G0](TC-G0门禁报告-0.0.46.md) |
| G1 | S1 LEX/SYN 与扁平 parse IR | **PASS** | [G1](TC-G1门禁报告-0.0.46.md) |
| G2 | S2 模块发现与 IMPORT | **PASS** | [G2](TC-G2门禁报告-0.0.46.md) |
| G3 | S3 名称、类型、转换与布局 | **PASS** | [G3](TC-G3门禁报告-0.0.46.md) |
| G4 | S4 CFG、DI、返回、调用、安全与注解 | **PASS** | [G4](TC-G4门禁报告-0.0.46.md) |
| G5 | S5 CT 与 IR 冻结 | **PASS** | [G5](TC-G5门禁报告-0.0.46.md) |
| G6 | S6 VM 参考实现（W=32/64） | **PASS** | [G6](TC-G6门禁报告-0.0.46.md) |
| G7 | S7 AArch64 子集后端、工具链与差分 | **PASS** | [G7](TC-G7门禁报告-0.0.46.md) |
| G8 | S8 全量语义、运行时与全量差分 | **PASS** | [G8](TC-G8门禁报告-0.0.46.md) |
| G9 | S9 freestanding/裸机与 32 位后端 | **PASS** | [G9](TC-G9门禁报告-0.0.46.md) |
| G10 | S10 多目标扩展与长期回归 | **PASS** | [G10](TC-G10门禁报告-0.0.46.md) |
| G11 | 0.0.47 工具链与工程模型 | **PASS** | [G11](TC-G11门禁报告-0.0.47.md) |

## 当前结论

- G0–G11 全部通过；G10 完成 0.0.46 阶段出口，G11 完成 0.0.47 阶段出口。
- 0.0.47 的 P0–P5、T0–T6、P4b 正式验收均已完成；当前注册 99 项 CTest、51 项收尾检查。
- 固定 Linux CI 的四 profile 真实 gdb 签名证据已通过发布审计；本地、SKIP 或不完整证据不能关闭 G11。
- 当前能力、实现边界与完整复现命令见 [README](../../README.md)、[AGENTS](../../AGENTS.md) 与
  [tests/README](../../tests/README.md)。

## 0.0.47 证据入口

| 范围 | 当前证据 |
| ---- | ---- |
| P0–P3 版本、CLI、初始化、manifest 与路径 | `cli.contract`、`project.manifest`、`project.init`、`project.init_fault`、`project.paths_unit`、`project.paths`、`project.paths_aot`、`project.test_runner` |
| P4a / T2 行表与源级映射 | `linemap.unit`、`p4a.debug`、`t2.debug`、`t2.gdb` |
| T1 观测 | `t1.observe` |
| T3 GDBstub | `gdb_journal.unit`、`gdb_rsp.unit`、`vm.gdb.protocol`、`vm.gdb.semantics`、`vm.gdb.e2e` |
| P4b / G11 调试 ELF 与签名证据 | `toolchain.debug_elf`、`toolchain.g11_evidence`、`release.s10_audit` |
| T4–T6 诊断、测试基建与可复现发布 | `t4.diag`、`toolchain.diag_json`、`toolchain.presets`、`toolchain.reduce`、`release.t6_repro` |

规范依据始终是 [标准](../TC语言及编译器标准-0.0.46.md)、
[设计](../TC原生编译器设计说明书-0.0.46.md) 与
[开发测试计划](../TC开发测试计划-0.0.46.md)；门禁记录不增加或放宽任何规范要求。
