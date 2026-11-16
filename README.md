# TC-Compiler

TC **0.0.46** 语言及编译器规范 + 原生编译器（TC → ASM → 机器码）目标设计。
当前分支为 0.0.46 clean-sheet：只保留标准、设计、测试计划与按设计重写的实现；
历史实现与旧工具已移除，需要时从 git tag 获取。

## 当前状态

- **已实现**：契约层、LEX/SYN 与扁平 parse IR、模块发现/IMPORT、名称/类型/转换/布局、
  静态指令形状检查、const_expr 形态、CFG/可达性/DI、缺返回、调用顺序/安全、安全上下文与注解矩阵；
  32/64 静态档案。
- **未实现**：CT、IR 最终化与打印、VM 与原生后端。
- 当前门禁：G0–G4 PASS；**G4 允许进入 S5**。详见 [门禁状态](docs/gates/README.md)。

## 文档

| 文档 | 内容 |
| ---- | ---- |
| [TC 语言及编译器标准 0.0.46](docs/TC语言及编译器标准-0.0.46.md) | 语言语义、诊断、编译器实现要求与平台档案的唯一权威 |
| [TC 原生编译器设计说明书 0.0.46](docs/TC原生编译器设计说明书-0.0.46.md) | 目标架构：扁平 TAC IR、运行时、平台档案、后端与分期路线 |
| [TC 开发测试计划 0.0.46](docs/TC开发测试计划-0.0.46.md) | 阶段、门禁、覆盖与可追溯性 |
| [文档地图](docs/README.md) | 文档入口与阅读顺序 |

## 目标口径

- **32/64 位均为目标**；`usize`/`isize` 为平台字长类型，`ptr<T>`/`fnptr` 宽度为 `usize`。
- **80 码闭集**：65 个 `TC_CE_*` + 15 个 `TC_RE_*`；不得增删。
- 不提供 C 宿主公共库、嵌入 API/ABI；`tc-vm`/`tc-aot` 是唯一工具入口。

## 仓库结构

```text
docs/                 标准、设计、测试计划、门禁状态
src/                  0.0.46 实现
  base/               assert / version
  contract/           契约检查 / 零 pass 策略
  diagnostic/         80 码闭集 / 生成表 / 首诊选择
  frontend/           input num lex parse module sem
  ir/                 type instr operand block ir region object address structs pointer_state
  runtime/            alloc io trap profile module_loader
  vm/                 interp（后续阶段）
  backend/            aarch64 arm32 riscv32 common（后续阶段）
  driver/             tc-vm / tc-aot CLI
tests/                L0/L1 测试、L3 一致性、harness、可追溯性、golden、确定性
```

## 构建与测试

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

工具当前支持：

- `--syntax-only <source>`：只做 LEX/SYN；退出 0 表示无 LEX/SYN 诊断，**不是程序有效**。
- `--static-only <source>`：执行 LEX/SYN/IMPORT/SEM（含 CFG/DI、调用、安全与注解）；
  退出 0 仍不代表程序有效，CT 与代码生成未运行。
- 普通调用：静态阶段通过后显式退出 70（后续阶段未实现）。

文件/资源失败的实现级错误使用 `tc_error_io`/`tc_error_internal`/`TC_ERR_OUT_OF_MEMORY`，
不伪装成语言诊断。实现上限：单源文件 4 MiB、单 Token 16 KiB、类型/块嵌套 256 层；
超出返回 `tc_error_internal`（退出 71）。契约与测试口径见
[src/CONTRACTS.md](src/CONTRACTS.md) 与 [tests/README.md](tests/README.md)。

## 参与

- [CONTRIBUTING.md](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)）
- [SECURITY.md](SECURITY.md)（[English](SECURITY.en.md)）
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)（[English](CODE_OF_CONDUCT.en.md)）
- [CHANGELOG.md](CHANGELOG.md)（[English](CHANGELOG.en.md)）

## 许可

见 [LICENSE](LICENSE)。
