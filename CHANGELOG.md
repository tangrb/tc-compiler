# Changelog

## [0.0.46] - 2026-11-12

### Changed

- 分支切换为 **0.0.46 clean-sheet**：只保留 0.0.46 标准、设计文档与实现骨架。
- `src/` 按设计说明书 §14 重建为 `frontend` / `ir` / `runtime` / `vm` / `backend` / `driver` 目录骨架，无实现、无 `tc-vm`/`tc-aot` 目标。
- 根 `CMakeLists.txt` 改为骨架配置；`make`、`ctest`、VM/AOT 测试与示例流程已移除。
- README、AGENTS、CONTRIBUTING、SECURITY、docs/README 统一为 0.0.46 文档/骨架口径。
- 32/64 位目标与 `usize`/`isize` 平台字长口径写入仓库入口；规范文本的字长修订按后续任务推进。

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
