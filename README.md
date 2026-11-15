# TC-Compiler

TC **0.0.46** 语言及编译器规范 + 原生编译器（TC → ASM → 机器码）目标设计。

当前分支是 **0.0.46 clean-sheet 分支**：保留 0.0.46 标准、设计文档与按设计重写的实现。历史实现、旧测试与旧工具均已移除。**S0 契约冻结与测试基建已完成并通过 G0**（见 [G0 报告](docs/TC-G0门禁报告-0.0.46.md)）；**S1（LEX/SYN 与扁平 parse IR）已实现**，门禁证据与状态见 [G1 报告](docs/TC-G1门禁报告-0.0.46.md)；IMPORT/SEM/CT、VM 与原生后端从 S2 起继续。

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
  frontend/           input / num / lex / parse（S1 已实现）；module / resolve / analyze / consteval（S2+）
  ir/                 type / instr / operand / block / ir / region / object / address / span
  runtime/            alloc / io / trap / profile / module_loader
  vm/                 interp（S6）
  backend/            aarch64 / arm32 / riscv32 / common（S7 起）
  driver/             tc-vm / tc-aot CLI（S1 前端检查；后续阶段补齐）
tests/                L0/L1 测试、L3 一致性、harness、可追溯性、golden 与确定性门禁
```

## 构建

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

S0/G0 已通过；S1 已实现 LEX/SYN 前端。`tc_core`、`tc-vm`、`tc-aot` 构建通过；工具现在可以：

- `--syntax-only <source>`：只做 LEX/SYN，退出 0 表示**没有词法/语法诊断**（部分检查，不等于程序有效）；有诊断时退出 1；
- 普通调用：LEX/SYN 通过后仍显式返回“后续阶段未实现”（退出码 70），不宣称可执行；
- 文件打开失败、超出文档化实现上限等属实现级错误，使用 `tc_error_io`/`tc_error_internal`/`TC_ERR_OUT_OF_MEMORY`，不伪装成语言诊断。

契约与测试口径见 [src/CONTRACTS.md](src/CONTRACTS.md) 与 [tests/README.md](tests/README.md)。S1 文档化实现上限：单源文件 4 MiB、单 Token（标识符/注解/浮点）16 KiB、类型/块嵌套 256 层；超出返回 `tc_error_internal`（退出 71），不产生语言码。

## 参与

- [CONTRIBUTING.md](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)）
- [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）
- [CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）

## 许可

见 [LICENSE](LICENSE)。
