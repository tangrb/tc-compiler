# 贡献指南

感谢关注 TC-Compiler。本文说明如何报告问题、提出改动，以及本地开发与测试要求。

English: [CONTRIBUTING.en.md](CONTRIBUTING.en.md)

参与本项目即表示你同意遵守 [行为准则](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）。安全漏洞请按 [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）**私下**报告，不要开公开 Issue。

## 开发环境

- C99 编译器、CMake、Make
- Python 3（静态结构检查脚本）
- TC-AOT `--run` 需要可用的宿主 `cc`（gcc/clang 风格；Windows 请用 MinGW，勿用 MSVC）

```sh
make
./build/vm/bin/tc-vm tests/valid/example.tc
bash scripts/run_tests.sh --filter example
```

可选安装 git hooks（会剥离 Cursor Co-authored-by trailer 等）：

```sh
make hooks
```

教学向示例见 [`examples/`](examples/)。完整符合性用例在 `tests/`。

## 分支约定

| 分支 | 用途 |
| ---- | ---- |
| `master` | 主开发线 |
| `tc-0.0.xx` | 版本收口 / 发布相关分支（与 CI 触发分支对齐） |

功能与修复请基于最新 `master`（或维护者指定的版本分支）开短生命周期分支，例如 `fix/…`、`feat/…`、`docs/…`。

## 提 Issue

先搜索已有 Issue 与文档。Bug 报告请尽量包含：

- `tc-vm --version`（或 commit / tag）
- 操作系统与编译器
- 最小可复现 `.tc` 与完整命令行
- 期望输出 vs 实际 stdout/stderr
- 是否仅 VM、仅 AOT，或两者不一致

语言/语义变更需对照 [TC 语言标准设计说明书](docs/TC语言标准设计说明书-0.0.42.md)；设计书权威高于实现。

使用仓库提供的 [Issue 模板](.github/ISSUE_TEMPLATE/)。

## 提 Pull Request

1. Fork 并创建分支。
2. 做最小必要改动；避免无关格式化。
3. 按下方清单跑相关测试。
4. 用户可见变更请更新 [CHANGELOG.md](CHANGELOG.md) / [CHANGELOG.en.md](CHANGELOG.en.md) 的 `[Unreleased]`。
5. 使用仓库 [PR 模板](.github/PULL_REQUEST_TEMPLATE.md) 填写说明并勾选清单。
6. 提交信息风格：`feat:` / `fix:` / `refactor:` / `docs:` / `test:` / `chore:` + 一句说明「为什么」。
7. **不要**在提交中加入 `Co-authored-by: Cursor <cursoragent@cursor.com>`（hook 会剥离；请勿 `--no-verify` 绕过）。

本仓库 **不要求 CLA**。贡献按 [Apache License 2.0](LICENSE) 授权。

## 必测与硬要求

按改动范围选择：

```sh
bash scripts/run_tests.sh                # VM + AOT + unit
bash scripts/run_tests.sh --filter <名>  # 缩小 VM 范围时仍建议在 PR 前跑全量或 CI 等价检查
make test-unit                           # 仅改 tests/unit 时
make ci                                  # 与远端核心门禁对齐的本地入口
```

| 改动类型 | 额外要求 |
| -------- | -------- |
| 新 `.tc` 用例 | 注册到 `scripts/vm/run_tests.sh`（AOT 相关再注册 `scripts/aot/run_tests.sh`） |
| 新 `TcRhsKind` | `python3 scripts/sync/check_rhs_coverage.py` |
| 新 `TcErrorKind` | `tc_error_kind_name()` + `tests/unit` 对应用例 + 编译器标准 §11.4 |
| 新 `src/` 模块 | `python3 scripts/sync/check_source_naming.py`（`tc_*.h` ↔ `tc_*.c`） |
| 类型内核 | `check-types` / `check_type_fact_source.py` |
| 控制流 / 未初始化 | `test_cfg` + 对应 `.tc` |
| Embed | `check-embed` / `check-embed-aot` |
| static 错误用例 | **一文件一错** |

更多映射见 `.cursor/skills/tc-architecture/test-map.md`（维护者 / Agent 导航；贡献者不强制阅读 Cursor 文档）。

## 编码规范（摘要）

- C99；**4 空格缩进**，不用 Tab；`/* … */` 注释，不混用 `//`
- `{` 行尾（K&R）；`if` / `while` / `for` 始终加大括号
- 公共函数 `tc_` + 小写下划线；类型 `Tc` + 帕斯卡；枚举值 `TC_` + 大写下划线
- 实现与头文件同名：`tc_<module>.h` ↔ `tc_<module>.c`
- `malloc` 失败：返回 `-1`，`TC_ERR_OUT_OF_MEMORY`，消息 `"memory allocation failed"`
- fail-fast；`TcDiagnostic` 单槽；返回 `0` / `-1`

完整约定见仓库 Cursor rule `coding-standards`（`.cursor/rules/coding-standards.mdc`）与现有 `src/` 风格。

## 文档

- 用户与贡献者导航：[docs/README.md](docs/README.md)（[English](docs/README.en.md)）
- 变更记录：[CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）
- 发版步骤：[docs/release-checklist.md](docs/release-checklist.md)（[English](docs/release-checklist.en.md)）

改语言语义、错误码或公开 API 时，同步更新对应 `docs/*-0.0.42.md`（或当前版本设计书）。

## Cursor Agent（可选）

使用 Cursor 时，维护者侧上下文见 [AGENTS.md](AGENTS.md) 与 [`.cursor/README.md`](.cursor/README.md)。**这不是贡献前提**；人类贡献者只需本文件与标准 PR 流程。

## 获取帮助

- 一般问题与功能讨论：GitHub Issues
- 安全：见 [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- 维护者：唐荣兵 — yanhuang8923@qq.com
