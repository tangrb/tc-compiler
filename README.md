# TC-Compiler

TC **0.0.46** 语言及编译器规范 + 原生编译器（TC → ASM → 机器码）目标设计。

当前分支是 **0.0.46 clean-sheet 分支**：只保留 0.0.46 标准、设计文档与实现骨架。历史实现、工具、测试、脚本与示例均已移除。0.0.46 编译器尚未开始实现。

## 文档

| 文档 | 内容 |
| ---- | ---- |
| [TC 语言及编译器标准 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | 语言语义、诊断、编译器实现要求与平台档案的唯一权威 |
| [TC 原生编译器设计说明书 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、运行时、平台档案、后端与分期路线 |
| [文档地图](docs/README.md) | 文档入口与阅读顺序 |

## 目标口径

- **32/64 位均为 0.0.46 目标**；`usize`/`isize` 是平台字长类型，`ptr<T>`/`fnptr` 宽度为 `usize`。规范文本中的 32/64 修订按文档任务推进。
- **80 码闭集**：65 个 `TC_CE_*` + 15 个 `TC_RE_*`。
- 不提供 C 宿主公共库、嵌入 API 或 ABI；`tc-vm`/`tc-aot` 是未来的唯一工具入口。
- 不兼容历史实现；历史代码保留在 git tag 中，不在本分支维护。

## 仓库结构

```text
docs/                 0.0.46 标准与设计
src/                  0.0.46 实现骨架（无实现）
  frontend/           lex / parse / module / resolve / analyze / consteval / diagnostic
  ir/                 type / instr / block / cfg / region / address / print
  runtime/            sem_int / sem_fp / sem_cast / sem_bitwise / io / sink / alloc / trap / profile
  vm/                 interp / memory / pointer_state
  backend/            aarch64 / arm32 / riscv32 / common
  driver/             tc-vm / tc-aot
tests/                符合性测试占位（待建）
```

## 构建

当前只有骨架，没有编译器目标：

```sh
cmake -S . -B build
cmake --build build
```

构建成功不代表 `tc-vm`/`tc-aot` 可用；实现从 0.0.46 设计开始。

## 参与

- [CONTRIBUTING.md](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)）
- [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）
- [CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）

## 许可

见 [LICENSE](LICENSE)。
