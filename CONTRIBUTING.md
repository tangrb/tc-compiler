# 贡献指南

感谢关注 TC-Compiler。当前分支是 **0.0.46 clean-sheet 分支**：契约、LEX/SYN、IMPORT、名称/类型/转换/布局、静态指令检查、const_expr 形态、CFG/DI、调用顺序/安全与注解矩阵、CT 求值、IR 最终化/打印、运行时语义核、参考 VM、W=64 AArch64 与 W=32 ARM32 全量后端/工具链、freestanding/裸机平台实例、四设备策略、checked build 与全量差分/ABI/一致性/产物套件已实现；RISC-V 等后续目标与可选优化管线尚未实现。

English: [CONTRIBUTING.en.md](CONTRIBUTING.en.md)

参与即表示同意遵守 [行为准则](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）。安全问题请按 [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）私下报告。

## 当前可贡献内容

- 0.0.46 标准勘误、歧义澄清、诊断/平台档案修订。
- 原生编译器设计补全（IR、ABI、profile、路线图）。
- `src/` 冻结契约、模块边界与前端接口。
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

构建私有 `tc_core` 与 `tc-vm`/`tc-aot` 入口。工具支持 `--syntax-only`/`--sem-only`/
`--static-only` 部分检查；普通 `tc-vm` 调用执行参考 VM（正常退出 0，trap 退出 1），
普通 `tc-aot` 调用发射目标汇编，`--emit=obj|elf` 经 clang/lld 生成 ELF 产物，
`--checked` 生成 checked build；当前平台实例外的能力显式退出 70 且不发布错误产物。
AOT 差分/工具链测试需要 `ld.lld` 与 `qemu-system-aarch64`。

## 提 Issue

请先搜索已有 Issue 与文档。报告请包含：

- 目标文档与章节（标准/设计）；
- 期望规则 vs 当前文本；
- 若涉及 32/64：明确字长与示例；
- 是否有可复现的 TC 片段（可用 `--static-only` 复现静态阶段行为）。

## 提 Pull Request

1. 基于 `tc-0.0.46` 开短生命周期分支。
2. 做最小必要改动；避免无关格式化。
3. 检查文档内部链接与 `§` 引用。
4. 用户可见变更更新 [CHANGELOG.md](CHANGELOG.md) / [CHANGELOG.en.md](CHANGELOG.en.md)。
5. 阶段/分支收尾按 [AGENTS.md](AGENTS.md) 的“收尾工作”清理文档，并通过 `ctest -R docs.cleanup`。
