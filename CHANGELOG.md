# Changelog

## [0.0.46] - 2026-11-13

### Added

- **S0 契约冻结与测试基建（G0 自动化证据通过，尚未签署）**：类型/IR/对象/档案/诊断/零 pass/构建契约头文件与 `tc_core` 私有库。
- 80 码机器可读表（`tests/traceability/codes.json`）与生成的 C 表；65 `TC_CE_*` + 15 `TC_RE_*` 由标准 §12.3/§12.5.1 校验。
- 首诊比较器、诊断缓冲、`TC_ERR_OUT_OF_MEMORY` 独立通道。
- `tc-vm`/`tc-aot` CLI 骨架：参数与档案校验为真，前端/代码生成未实现时显式退出 70。
- L0 契约测试、Python 用例 harness 与失败关闭自测、覆盖矩阵骨架、golden 评审校验、CLI 契约测试。
- `src/CONTRACTS.md` 记录契约属主、冻结边界与后续语义表归属。

### Changed

- 构建标准升为 C11；根 CMake 启用测试并只产出 `tc_core`、`tc-vm`、`tc-aot`。
- 重组 `src/`：跨阶段基础设施移入 `base/`（断言/版本）、`contract/`（契约检查/零 pass）、`diagnostic/`（80 码表/首诊选择）；`frontend/` 保留给 S1 起的 LEX/SYN 流水线；`runtime/profiles.c` 更名为 `profile.c`；两个 CLI 的档案选择合并到 `driver/profile_select.c`。

### Fixed

- G0 审计 F1/F2：补充 §12.2 #9/#4 键序文档说明；新增 `origin×rank` 合成 L0 用例（`TC-L0-DIAG-0001`）。
- `TC_CE_SAFE_VIOLATION` 写入 `same_site_rank=60`（`UNSAFE_*`/`FAST_CALL` 为 50），新增四码同位置优先 L0 用例（`TC-L0-DIAG-0002`）；覆盖矩阵新增 5 个 `same_site_priority` 行并标记 covered。
- 修复 `tc_instr_lhs_is_valid()`：有结果指令允许 `_` 丢弃（§6.15.1），同步修正错误测试并补 `TC-L0-IR-0001`。
- 补齐 §12.2 #10 调用/字段顺序、§8.8.2 调用安全优先、§12.2 #5 负移位优先的排序键与 L0 用例（`TC-L0-DIAG-0003`…`0006`）。
- 类型表创建时预创建 14 个标量单例，冻结后不再新增节点；补 `TC-L0-TYPE-0001`。覆盖矩阵现为 634 槽、231 项、5 L0 covered / 629 uncovered。

## [0.0.46] - 2026-11-12

### Changed

- 分支切换为 **0.0.46 clean-sheet**：只保留 0.0.46 标准、设计文档与实现骨架。
- `src/` 按设计说明书 §14 重建为 `frontend` / `ir` / `runtime` / `vm` / `backend` / `driver` 目录骨架，无实现、无 `tc-vm`/`tc-aot` 目标。
- 根 `CMakeLists.txt` 改为骨架配置；`make`、`ctest`、VM/AOT 测试与示例流程已移除。
- README、AGENTS、CONTRIBUTING、SECURITY、docs/README 统一为 0.0.46 文档/骨架口径。
- 32/64 位目标与 `usize`/`isize` 平台字长口径写入仓库入口；标准与设计已按 32/64 双字长修订（字长相关类型、`sizeof`/`bitcast`/`trunc`/布局/诊断与平台档案）。

### Removed

- 历史实现、旧测试与旧构建配置。
- 旧 C 宿主公共库/嵌入 API、嵌入模式与其槽位桥接。
- 旧设计文档与旧发布清单。
- `tests/`、`scripts/`、`examples/`、`tools/`、`.cursor/`、`.github/`、`Makefile` 与历史构建目录。
- 旧版本号、CLI golden、旧错误码数量与旧实现导航。

### Kept

- `docs/TC语言及编译器标准-0.0.46.md`（80 码、语言/编译器规则）。
- `docs/TC原生编译器设计说明书-0.0.46.md`（目标架构）。
- `LICENSE`、`CODE_OF_CONDUCT*`、`CONTRIBUTING*`、`SECURITY*`。

历史实现可从 git tag 恢复；本分支不再维护。
