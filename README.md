# TC-Compiler

TC **0.0.46** 语言及编译器规范 + 原生编译器（TC → ASM → 机器码）目标设计。

当前分支是 **0.0.46 clean-sheet 分支**：保留 0.0.46 标准、设计文档与按设计重写的实现。历史实现、旧测试与旧工具均已移除。**S0 契约冻结与测试基建已完成并通过 G0**（见 [G0 报告](docs/TC-G0门禁报告-0.0.46.md)）；LEX/SYN 等前端实现从 S1 开始。

## 文档

| 文档 | 内容 |
| ---- | ---- |
| [TC 语言及编译器标准 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | 语言语义、诊断、编译器实现要求与平台档案的唯一权威 |
| [TC 原生编译器设计说明书 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、运行时、平台档案、后端与分期路线 |
| [文档地图](docs/README.md) | 文档入口与阅读顺序 |

## 目标口径

- **32/64 位均为 0.0.46 目标**；`usize`/`isize` 是平台字长类型，`ptr<T>`/`fnptr` 宽度为 `usize`；标准与设计已按 32/64 双档案修订。
- **80 码闭集**：65 个 `TC_CE_*` + 15 个 `TC_RE_*`。
- 不提供 C 宿主公共库、嵌入 API 或 ABI；`tc-vm`/`tc-aot` 是唯一工具入口。
- 不兼容历史实现；历史代码保留在 git tag 中，不在本分支维护。

## 仓库结构

```text
docs/                 0.0.46 标准与设计
src/                  0.0.46 实现（S0 契约层已实现）
  base/               assert / version
  contract/           S0 契约检查 / 零 pass 策略
  diagnostic/         80 码闭集 / 生成表 / 首诊选择
  frontend/           lex / parse / module / resolve / analyze / consteval（S1 起）
  ir/                 type / instr / operand / block / ir / region / object / address / span
  runtime/            alloc / io / trap / profile / module_loader
  vm/                 interp（S6）
  backend/            aarch64 / arm32 / riscv32 / common（S7 起）
  driver/             tc-vm / tc-aot CLI 骨架
tests/                L0 契约测试、harness、可追溯性与 golden 校验
```

## 构建

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

S0/G0 已通过；S1 起实现前端/VM/后端。S0 构建 `tc_core`、`tc-vm`、`tc-aot`；两个工具当前对合法调用显式返回
“未实现”（退出码 70），不宣称可用。契约与测试口径见
[src/CONTRACTS.md](src/CONTRACTS.md) 与 [tests/README.md](tests/README.md)。

## 参与

- [CONTRIBUTING.md](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)）
- [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）
- [CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）

## 许可

见 [LICENSE](LICENSE)。
