# 贡献指南

感谢关注 TC-Compiler。当前分支是 **`tc-0.0.47` 工具链分支**：0.0.46 clean-sheet 基线（契约、LEX/SYN、IMPORT、名称/类型/转换/布局、静态指令检查、const_expr 形态、CFG/DI、调用顺序/安全与注解矩阵、CT 求值、IR 最终化/打印、运行时语义核、参考 VM、W=64 AArch64、W=32 ARM32 与 W=32 RISC-V 全量后端/工具链、三个 freestanding/裸机平台实例、四设备策略、checked build、全量差分/ABI/一致性/产物套件、生成式 ABI fuzz、实现边界/规模/长跳转回归、长期回归语料与发布审计）已实现，可选优化管线尚未启用；本分支目标是在语言语义不变的前提下交付开发调试工具链与工程模型（[TC开发调试工具链-0.0.47.md](docs/TC开发调试工具链-0.0.47.md)、[TC工程模型-0.0.47.md](docs/TC工程模型-0.0.47.md)）。两份设计达到冻结条件；P0/T0 治理与三入口契约、P1 工程初始化与 manifest/根发现 API、P2 canonical/显示路径与编译器路径消费、P3 工程测试 runner、P4a AOT 行表/调试信息、T1 VM/AOT 观测、T2 源级映射与 T3 VM GDB 调试均已完成；P4b 本地实现与四种 profile 的真实 gdb 验证已完成，固定 Linux CI 真实签名 artifact 待完成，P4b 正式验收与 G11 未完成。P5 与 T4–T6 尚未开始。

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
cmake -S . -B .tmp/build
cmake --build .tmp/build
ctest --test-dir .tmp/build --output-on-failure
```

构建私有 `tc_core` 与 `tc`/`tc-vm`/`tc-aot` 入口。编译器支持 `--syntax-only`/`--sem-only`/
`--static-only` 部分检查；普通 `tc-vm` 调用执行参考 VM（正常退出 0，trap 退出 1），
普通 `tc-aot` 调用发射目标汇编，`--emit=asm|obj|elf|flat` 经 clang/lld 生成
汇编/ELF/裸二进制产物，
`--checked` 生成 checked build；当前平台实例外的能力显式退出 70 且不发布错误产物。
`tc --init-project` 已在当前 canonical 目录实现完整模板、原子写入、回滚与身份检查；
编译器已消费 manifest 解析/最近根发现、canonical/显示路径、include 与 profile；
`tc --test` 已实现发现/筛选、fd 锚定 beneath 解析、严格侧车、私有只读快照、
fork worker、超时、流捕获、结构化结果、报告与退出映射。T1 已实现 VM/AOT
`--dump-ir`、VM `--trace=ir,call,mem,io,trap`（text/jsonl、limit/output）、VM/AOT
`--stats`、结构化 trap 上下文、显式输出身份/预留保护与确定性观测输出。T2 已实现
VM/AOT `--dump-linemap[=<path>]`、公开 `tc-aot --debug`、DWARF v4 CU 与真实
qemu gdbstub/gdb 冒烟。T3 已实现 `tc-vm --gdb[=<port>]`（默认 0，仅 loopback）、
强制 stdout journal、合成 IR 地址/`target.xml`、断点/单步/继续、monitor/extended
restart、原子 stdout journal、可恢复 stdin 与可选受限写入。P4b 已实现
`tc-vm --gdb-elf-out=<path>` 的私有调试 ELF writer、四种档案独立语义解码与
四种 profile 的真实 gdb 调试 ELF、源级断点、64 位 `tc_ir_pc` 读取、IR 单步及
程序语义回归。合成 `tc_ir_pc` 使用 64 位 `uint64`，架构 pc 保持 32 位
`code_ptr`，ARM/RISC-V 的 Z0 kind=2 已被接受；ARM 客户端需 `set arm force-mode thumb`，
GDB 18.1 的 ARM `stepi` 限制由 maintenance packet `s` 与 flush register-cache
的同一 RSP 单步验证覆盖。
P1/P2/P3/P4a/P4b/T1/T2/T3 测试入口为 `project.manifest`、`project.init`、
`project.init_fault`、`project.paths_unit`、`project.paths`、`project.paths_aot`、
`project.test_runner`、`linemap.unit`、`p4a.debug`、`toolchain.debug_elf`、
`toolchain.g11_evidence`、`t1.observe`、`t2.debug`、`t2.gdb`、
`gdb_journal.unit`、`gdb_rsp.unit`、`vm.gdb.protocol`、`vm.gdb.semantics` 与
`vm.gdb.e2e`，可用
`ctest --test-dir .tmp/build -R '^project\.|^linemap\.|^p4a\.|^toolchain\.(debug_elf|g11_evidence)|^t1\.|^t2\.|^gdb_|^vm\.gdb\.'
--output-on-failure` 运行。当前注册 92 项；G11 证据收集/校验与手动签名作业已接入，
固定 Linux CI 真实签名 artifact 仍待完成，P4b 正式验收与 G11 未完成。
AOT 差分/工具链测试需要 `clang`、`llvm-ar`、`ld.lld`、`qemu-system-aarch64`、
`qemu-system-arm` 与 `qemu-system-riscv32`；P4a 另需 `llvm-dwarfdump`，T2/T3 另需 `gdb`。

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
