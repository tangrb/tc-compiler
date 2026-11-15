# TC-Compiler — Agent 指南

0.0.46 clean-sheet 分支。实现与验证以标准/设计/测试计划为准；历史实现、工具与测试已移除，
只在 git tag 中保留。

## 当前事实

- 语言及编译器标准：`docs/TC语言及编译器标准-0.0.46.md`（唯一权威）。
- 原生编译器目标设计：`docs/TC原生编译器设计说明书-0.0.46.md`。
- 已实现：契约、LEX/SYN 与扁平 parse IR、IMPORT、名称/类型/转换/布局、静态指令形状检查、
  CFG/可达性/DI、缺返回、调用顺序/安全、安全上下文与注解矩阵、32/64 静态档案；代码在
  `src/frontend/` 的 `input`/`num`/`lex`/`parse`/`module`/`sem`。
- 未实现：CT、IR 最终化与打印、VM 与后端。
- 门禁：G0–G4 PASS，G4 允许进入 S5；见 `docs/gates/README.md`。
- 诊断码：80 个（65 `TC_CE_*` + 15 `TC_RE_*`），不得增删。
- 32/64 位均为目标；`usize`/`isize` 为平台字长类型。
- 不提供 C 宿主公共库、嵌入 API/ABI；不要重新引入。
- 文档化实现上限：单源文件 4 MiB、单 Token 16 KiB、类型/块嵌套 256 层；
  超出是 `tc_error_internal`（退出 71），不是语言诊断。

## 工作规则

1. **先读文档，再动骨架**：语义/诊断以标准为准，架构以设计说明书为准。
2. **不要恢复旧实现**：不得从 git 历史或旧分支复制旧实现；0.0.46 按设计重写。
3. **文档改动最小化**：只改相关章节；不要凭旧实现行为反推语义。
4. **32/64 同时考虑**：`usize`/`isize`、`sizeof`、`bitcast`/`trunc`、布局与诊断的字长差异
   必须显式处理。
5. **构建与测试**：`cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`。
   `--syntax-only`/`--static-only` 都是部分检查，退出 0 不表示程序有效；普通调用在静态阶段
   后仍退出 70。不得把部分能力写成“已支持”。
6. **提交信息**：`docs(0.0.46): ...` 或 `chore(0.0.46): ...`，说明 clean-sheet。
7. **代码注释边界**：注释只解释代码本身、引用标准/设计条款或自包含事实；不得引用阶段编号、
   门禁报告、测试计划或审计结果。测试用例 id 只用于测试/覆盖追溯。
8. **收尾即清理**：每个阶段或分支收尾必须执行下方“收尾工作”；文档清理是门禁的一部分，
   不是可选的整理工作。

## 收尾工作

阶段/分支收尾按以下清单执行，顺序固定：

1. **冻结规范文档**：不改标准、设计、测试计划的语义条款；只允许修正链接或必要的
   交叉引用。
2. **清理过程/历史内容**：从 README、AGENTS、CONTRIBUTING、CHANGELOG、`docs/gates/`、
   `src/README.md`、`src/CONTRACTS.md`、`tests/*/README.md` 等非规范文档中删除：
   - 逐轮审计、首轮/复核、勘误、B1/B2/B3/B4 编号与修复日志；
   - reviewed commit、evidence version、环境快照与逐轮实测记录；
   - 按日期堆叠的迭代 changelog 条目、过期的阶段状态/计数；
   - 已被当前实现取代的约定或“历史实现如何做”的叙述。
   详细过程保留在 git 历史中，不进入分支文档。
3. **收敛门禁记录**：`docs/gates/` 只保留 G 状态表与每阶段一页以内的当前记录
   （结论、范围、证据入口、复现命令、当前限制）。门禁记录不得复述审计过程。
4. **归并当前约定**：仍影响实现的诊断位置、边界裁定、已知限制与后续阶段移交事项写入
   `src/CONTRACTS.md`，不留在门禁过程记录中。
5. **同步当前状态**：更新 README/AGENTS/CONTRIBUTING/文档地图/测试 README 的当前能力、
   测试计数、门禁状态与链接；CHANGELOG 按发布版本收敛为当前条目。
6. **全量验证**：执行
   `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`，
   并确认 `tests/conformance/static/*_cases.py --check`、`tests/traceability/coverage.py --check`
   与 `tests/tools/docs_cleanup_check.py` 均通过。
7. **提交与洁净**：以 `docs(0.0.46): ...` 提交清理结果；确认 `git status` 干净，
   `build/` 与生成物未入库。

`docs.cleanup`（`tests/tools/docs_cleanup_check.py`）是上述清理的固定门禁：
出现审计/勘误/commit pin、门禁记录超长或 changelog 版本重复时失败。

## 导航

| 需求 | 位置 |
| ---- | ---- |
| 语言语义 / 诊断 / 平台档案 | `docs/TC语言及编译器标准-0.0.46.md` |
| IR / 运行时 / 后端 / 路线图 | `docs/TC原生编译器设计说明书-0.0.46.md` |
| 阶段 / 覆盖 / 门禁要求 | `docs/TC开发测试计划-0.0.46.md` |
| 实现模块边界 | `src/README.md` |
| 冻结契约与实现约定 | `src/CONTRACTS.md` |
| 前端接口 | `src/frontend/parse.h`、`lex.h`、`input.h`、`module.h`、`sem.h` |
| 测试基建 | `tests/README.md` |
| 门禁状态 | `docs/gates/README.md` |
| 文档收尾检查 | `tests/tools/docs_cleanup_check.py`（`docs.cleanup`） |
| 一致性用例表 | `tests/conformance/static/s1_cases.py`、`s2s3_cases.py`、`s4_cases.py` |
| 文档地图 | `docs/README.md` |
| 贡献 / 安全 | `CONTRIBUTING.md` / `SECURITY.md` |
