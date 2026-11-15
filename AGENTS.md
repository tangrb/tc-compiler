# TC-Compiler — Agent 指南

**0.0.46 clean-sheet 分支**：只保留 0.0.46 标准、设计文档与按设计重写的实现。历史实现、工具、测试、脚本与示例已移除。S0 契约与测试基建已完成并通过 G0（见 `docs/gates/TC-G0门禁报告-0.0.46.md`）；S1（LEX/SYN 与扁平 parse IR）已通过 G1；**S2（模块发现/IMPORT）与 S3（名称/类型/转换/布局）已实现**，门禁证据见 G2/G3 报告。CT、CFG/DI/安全/注解、VM 与后端仍待实现。

## 当前事实

- 语言及编译器标准：`docs/TC语言及编译器标准-0.0.46.md`（唯一权威）。
- 原生编译器目标设计：`docs/TC原生编译器设计说明书-0.0.46.md`。
- 实现现状：`src/` 有 S0 契约层、`tc_core`/`tc-vm`/`tc-aot` 目标，以及 `frontend/` 的 `input`、`num`、`lex`、`parse`、`module`、`sem`。模块发现/IMPORT、名称解析、类型表/冻结、字面量、转换矩阵、结构体布局与指令形状/类型检查已实现；CT、CFG/DI、返回/调用顺序、安全/注解、VM 与后端尚未实现。
- parse IR 是“扁平、带源名”的解析结果（`src/frontend/parse.h`）；SEM 使用并校验它，解析期保留源名/类型语法。冻结的最终 IR（`src/ir/*.h` 的 `TcInstr`/`TcIR`）在 IR 最终化阶段物化；S3 提供其所需的名称/类型/opcode/布局事实。不要为了前端修改冻结契约。
- 契约索引：`src/CONTRACTS.md`（S0 冻结的跨模块结构、属主与后续阶段边界）。
- 诊断码：80 个（65 `TC_CE_*` + 15 `TC_RE_*`），不得增删。
- 32/64 位均为 0.0.46 目标；`usize`/`isize` 为平台字长类型。
- 不提供 C 宿主公共库、嵌入 API/ABI；不要重新引入。
- 不兼容历史实现；历史代码只在 git tag 中。
- S1 文档化实现上限：单源文件 4 MiB、单 Token（标识符/注解/浮点）16 KiB、类型/块嵌套 256 层；超出是 `tc_error_internal`（退出 71），不是语言诊断。

## 工作规则

1. **先读文档，再动骨架**：语言/语义/诊断问题以 0.0.46 标准为准；实现架构以设计说明书为准。
2. **不要恢复旧实现**：不得从 git 历史或旧分支复制旧实现作为新实现；0.0.46 按设计重写。
3. **文档改动最小化**：只改相关章节；不要凭旧实现行为反推 0.0.46 语义。
4. **32/64 同时考虑**：`usize`/`isize`、`sizeof`、`bitcast`/`trunc`、布局与诊断的字长差异必须显式处理。
5. **构建与测试**：`cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`。
   `tc-vm`/`tc-aot` 支持 `--syntax-only` 与 `--static-only`：两者都是部分检查，退出 0 不表示程序有效；普通调用在静态阶段通过后仍显式返回未实现（退出码 70）。不得把部分能力写成“已支持”。
6. **提交信息**：`docs(0.0.46): ...` 或 `chore(0.0.46): ...`，说明本分支为 clean-sheet。
7. **代码注释边界**：注释只解释代码本身，引用标准/设计条款或自包含事实；不得引用阶段编号（S0/S1…）、门禁报告、测试计划或审计结果。测试用例 id 只用于测试/覆盖追溯。

## 导航

| 需求 | 位置 |
| ---- | ---- |
| 语言语义 / 诊断 / 平台档案 | `docs/TC语言及编译器标准-0.0.46.md` |
| IR / 运行时 / 后端 / 路线图 | `docs/TC原生编译器设计说明书-0.0.46.md` |
| 实现模块边界 | `src/README.md` |
| S0 冻结契约 | `src/CONTRACTS.md` |
| 前端接口 | `src/frontend/parse.h`、`src/frontend/lex.h`、`src/frontend/input.h`、`src/frontend/module.h`、`src/frontend/sem.h` |
| 测试基建 | `tests/README.md` |
| 门禁证据/状态 | `docs/gates/TC-G1门禁报告-0.0.46.md`、`TC-G2门禁报告-0.0.46.md`、`TC-G3门禁报告-0.0.46.md` |
| 一致性用例表 | `tests/conformance/static/s1_cases.py`、`s2s3_cases.py`、`tests/conformance/README.md` |
| 文档地图 | `docs/README.md` |
| 贡献 / 安全 | `CONTRIBUTING.md` / `SECURITY.md` |
