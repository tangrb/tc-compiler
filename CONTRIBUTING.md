# 贡献指南

感谢关注 TC-Compiler。当前分支是 **0.0.46 clean-sheet 分支**：S0 契约层与测试基建已实现，前端、VM 与原生后端尚未实现；历史实现已移除。

English: [CONTRIBUTING.en.md](CONTRIBUTING.en.md)

参与即表示同意遵守 [行为准则](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）。安全问题请按 [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）私下报告。

## 当前可贡献内容

- 0.0.46 标准勘误、歧义澄清、诊断/平台档案修订。
- 原生编译器设计补全（IR、ABI、profile、路线图）。
- `src/` 冻结契约、模块边界与 S0/S1+ 接口。
- 32/64 字长语义、`usize`/`isize`、`sizeof`/`bitcast`/`trunc`/布局的目标相关规则。

当前**不接受**：

- 恢复历史实现或 C 宿主公共库/嵌入 API。
- 以旧实现行为为依据、绕过 0.0.46 标准的兼容补丁。
- 与当前文档任务无关的大规模重构。

## 文档优先

语言/语义/诊断变更必须先更新 [TC 语言及编译器标准 0.0.46](docs/TC语言及编译器标准-0.0.46.md)；架构变更同步 [TC 原生编译器设计说明书 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md)。

## 分支与提交

- 当前开发分支：`tc-0.0.46`。
- 提交信息建议：`docs(0.0.46): ...`、`design(0.0.46): ...`、`chore(0.0.46): ...`。
- 不要提交 `build/` 或生成物。

## 构建与测试

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

S0 构建私有 `tc_core` 与 `tc-vm`/`tc-aot` 入口。两个工具尚未实现前端；合法调用会以退出码 70 返回未实现。

## 提 Issue

请先搜索已有 Issue 与文档。报告请包含：

- 目标文档与章节（标准/设计）；
- 期望规则 vs 当前文本；
- 若涉及 32/64：明确字长与示例；
- 是否有可复现的 TC 片段（当前无编译器，仅作语义说明）。

## 提 Pull Request

1. 基于 `tc-0.0.46` 开短生命周期分支。
2. 做最小必要改动；避免无关格式化。
3. 检查文档内部链接与 `§` 引用。
4. 用户可见变更更新 [CHANGELOG.md](CHANGELOG.md) / [CHANGELOG.en.md](CHANGELOG.en.md)。
