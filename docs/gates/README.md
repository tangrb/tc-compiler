# 门禁报告（G0–G10）

本目录集中保存 TC 0.0.46 的**阶段门禁报告**（过程性文档）。语言语义、诊断、接受集与
可追溯性仍以 [标准](../TC语言及编译器标准-0.0.46.md)、[设计](../TC原生编译器设计说明书-0.0.46.md)
与 [开发测试计划](../TC开发测试计划-0.0.46.md) 为唯一依据；门禁报告只记录证据、审计与判定，
不增加或放宽任何规范要求。

| 门禁 | 阶段 | 结论 | 报告 |
| ---- | ---- | ---- | ---- |
| G0 | S0 契约冻结与测试基建 | **PASS**；允许进入 S1 | [TC-G0门禁报告-0.0.46.md](TC-G0门禁报告-0.0.46.md) |
| G1 | S1 LEX/SYN 与扁平 parse IR | **PASS**；允许进入 S2 | [TC-G1门禁报告-0.0.46.md](TC-G1门禁报告-0.0.46.md) |
| G2 | S2 模块发现与 IMPORT | **PASS**；允许进入 S3 | [TC-G2门禁报告-0.0.46.md](TC-G2门禁报告-0.0.46.md) |
| G3 | S3 名称、类型、转换与布局 | **PASS**；允许进入 S4 | [TC-G3门禁报告-0.0.46.md](TC-G3门禁报告-0.0.46.md) |

后续 S2–S10 的 G2–G10 报告继续加入本目录；每份报告须记录 reviewed code commit、
独立评审/审计结论、适用/不适用门禁项与明确的范围说明。当前 S0/S1 报告均以通过状态签署，
并保留首轮审计发现与修复记录（见 G1 §12）。

返回：[文档地图](../README.md) · [仓库入口](../../README.md)

## English

This directory holds the 0.0.46 phase **gate reports** (process documents).  The
language standard, design and test plan remain the sole normative sources; a
gate report records evidence, independent audit and the gate decision only.
G0 (S0), G1 (S1), G2 (S2) and G3 (S3) are PASS; G3 allows S4.  Future G4–G10 reports belong here.
