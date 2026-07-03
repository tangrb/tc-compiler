# TC-Compiler

TC 语言的实现工程。当前包含 **TC-VM**（直接执行引擎，已实现）；**TC-AOT**（ahead-of-time 编译，将 `.tc` 编译为原生目标代码）预留目录，尚未实现。

## 目录结构

```text
docs/                  语言标准、VM 详细设计等文档
src/
├── vm/                TC-VM 源码、CMakeLists.txt
└── aot/               TC-AOT 预留（CMakeLists.txt）
tests/                 一致性测试（valid/ + errors/ + stress/）
scripts/
├── vm/                VM 测试脚本
└── aot/               AOT 测试脚本（预留）
build/                 构建产物（git 忽略）
├── vm/bin/tc-vm       VM 可执行文件
└── aot/bin/           AOT 可执行文件（预留）
```

## 构建

构建由 **CMake** 统一管理；根目录 `Makefile` 是对 CMake 的薄封装，`CMakeLists.txt` 定义各组件目标。

### Makefile（推荐）

```sh
make            # 配置并编译 VM（默认）
make vm         # 同上
make aot        # 编译 AOT（尚未实现，会报错提示）
make test       # 运行 VM 一致性测试
make test-vm    # 同上
make test-aot   # 运行 AOT 测试（尚未实现）
make clean      # 删除 build/ 目录
```

### CMake（等价命令）

```sh
cmake -S . -B build
cmake --build build                  # 编译 tc-vm
cmake --build build --target check-vm
cmake --build build --target check-aot
cmake --build build --target check   # 当前等同 check-vm
```

## 运行

### 文件模式

```sh
./build/vm/bin/tc-vm tests/valid/example.tc
./build/vm/bin/tc-vm --check tests/valid/example.tc   # 仅静态分析
./build/vm/bin/tc-vm --help                           # 查看用法
```

### 交互式 REPL

```sh
./build/vm/bin/tc-vm --repl        # 启动交互式 REPL
./build/vm/bin/tc-vm -i            # 同上（短选项）
```

REPL 支持逐条输入 TC 语句并立即执行，变量跨行保留。内置元命令包括 `:quit`（退出）、`:reset`（清空变量）、`:vars`（列出变量）、`:help`（帮助）。

## 文档

| 文档 | 说明 |
|------|------|
| [TC 语言标准设计说明书](docs/TC语言标准设计说明书.md) | 语言语法与语义权威定义（v0.0.14） |
| [TC-VM 详细设计说明书](docs/TC-VM详细设计说明书.md) | 直接执行引擎架构与实现约定（v0.0.14） |
| [TC-VM 命令行参考](docs/TC-VM命令行参考.md) | 使用 tc-vm 处理 `.tc` 源文件的命令说明（v0.0.14） |

实现行为以语言标准为准；VM / AOT 详细设计文档规定各后端的实现架构，不重复定义语言语义。

## 作者

- **唐荣兵** ([yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)) — 项目创建与维护者
