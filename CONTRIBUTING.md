# 贡献指南

感谢关注 TC-Compiler。当前分支是 **`tc-0.0.47` 工具链分支**：0.0.46 clean-sheet 基线（契约、LEX/SYN、IMPORT、名称/类型/转换/布局、静态指令检查、const_expr 形态、CFG/DI、调用顺序/安全与注解矩阵、CT 求值、IR 最终化/打印、运行时语义核、参考 VM、W=64 AArch64、W=32 ARM32 与 W=32 RISC-V 全量后端/工具链、三个 freestanding/裸机平台实例、四设备策略、checked build、全量差分/ABI/一致性/产物套件、生成式 ABI fuzz、实现边界/规模/长跳转回归、长期回归语料与发布审计）已实现，可选优化管线尚未启用；本分支目标是在语言语义不变的前提下交付开发调试工具链与工程模型（[TC开发调试工具链-0.0.47.md](docs/TC开发调试工具链-0.0.47.md)、[TC工程模型-0.0.47.md](docs/TC工程模型-0.0.47.md)）。两份设计达到冻结条件；P0/T0 治理与三入口契约已完成，P1 的工程初始化与 manifest/根发现 API 亦已完成。manifest 尚未接入编译器消费，P2–P5/T1–T6 尚未开始；`tc --test` 仍显式退出 70。

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
0.0.47 工具链与工程变更以两份 0.0.47 设计为准；设计验收条款不是当前实现证据。

## 分支与提交

- 当前开发分支：`tc-0.0.47`。
- 提交信息建议：`docs(0.0.47): ...`、`design(0.0.47): ...`、`chore(0.0.47): ...`。
- 不要提交 `build/` 或生成物。

## 构建与测试

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

构建私有 `tc_core` 与 `tc`/`tc-vm`/`tc-aot` 入口。编译器支持 `--syntax-only`/`--sem-only`/
`--static-only` 部分检查；普通 `tc-vm` 调用执行参考 VM（正常退出 0，trap 退出 1），
普通 `tc-aot` 调用发射目标汇编，`--emit=asm|obj|elf|flat` 经 clang/lld 生成
汇编/ELF/裸二进制产物，
`--checked` 生成 checked build；当前平台实例外的能力显式退出 70 且不发布错误产物。
`tc --init-project` 已在当前 canonical 目录实现完整模板、原子写入、回滚与身份检查；
manifest 解析/最近根发现 API 尚未接入编译器消费，`tc --test` 仍显式退出 70，
不执行测试。P1 测试入口为 `project.manifest`、`project.init`、`project.init_fault`，
可用 `ctest --test-dir build -R '^project\.' --output-on-failure` 运行。
AOT 差分/工具链测试需要 `clang`、`llvm-ar`、`ld.lld`、`qemu-system-aarch64`、
`qemu-system-arm` 与 `qemu-system-riscv32`。

## 提 Issue

请先搜索已有 Issue 与文档。报告请包含：

- 目标文档与章节（标准/设计）；
- 期望规则 vs 当前文本；
- 若涉及 32/64：明确字长与示例；
- 是否有可复现的 TC 片段（可用 `--static-only` 复现静态阶段行为）。

## 提 Pull Request

1. 基于 `tc-0.0.47` 开短生命周期分支。
2. 做最小必要改动；避免无关格式化。
3. 检查文档内部链接与 `§` 引用。
4. 用户可见变更更新 [CHANGELOG.md](CHANGELOG.md) / [CHANGELOG.en.md](CHANGELOG.en.md)。
5. 阶段/分支收尾按 [AGENTS.md](AGENTS.md) 的“收尾工作”清理文档，并通过 `ctest -R docs.cleanup`。
