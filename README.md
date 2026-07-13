# TC-Compiler

TC 语言编译器（C99）。当前包含 **libtc**（编译/执行静态库）、**TC-VM**（直接执行引擎）、**TC-AOT**（ahead-of-time 编译，将 `.tc` 转译为 C99 源码）。

版本：**v0.0.25**（`src/vm/driver/tc_version.h`）

## 目录结构

```text
docs/                  语言标准、VM 详细设计、AOT、libtc API 等文档
src/
├── libtc/             共享静态库（编译 + 执行流水线，CMakeLists.txt）
├── vm/                TC-VM 源码（lexer / parser / analyzer / executor / runtime）
└── aot/               TC-AOT 源码（codegen / rt shim / CLI）
tests/
    ├── valid/             一致性测试（含 bool/let/I/O/format/if 等特性）
    ├── errors/            错误测试（static 79 个 + runtime 43 个）
    ├── unit/              C 单元测试（lexer / parser / semantics / types / io / bitwise / shift / symbol / warning / analyzer / stmt-index）
    └── stress/            压力测试
scripts/
├── ci.sh              本地 CI 流水线（构建 + 测试 + 静态检查）
├── run_tests.sh        统一测试入口（推荐）
├── run_asan_all.sh     ASan 一键构建 + 全量测试
├── run_memcheck_macos.sh  macOS 内存安全检查（MallocScribble + leaks）
├── valgrind-suppressions.supp  Valgrind 压制文件（libc 启动期分配）
├── git-hooks/          Git hooks（commit-msg 剥离 Cursor trailer）
├── vm/                 VM 测试脚本
├── aot/                AOT 差分测试脚本
├── sync/               RHS 覆盖检查（check_rhs_coverage.py）
└── install-git-hooks.sh
build/                 构建产物（git 忽略）
├── vm/bin/tc-vm       VM 可执行文件
└── aot/bin/tc-aot     AOT 可执行文件
```

## 已实现的特性

| 类别 | 特性 |
|------|------|
| 类型系统 | `int8` / `int16` / `int32` / `int64` / `uint8` / `uint16` / `uint32` / `uint64` / `bool` / `float32` / `float64` |
| 字面量 | 十进制、十六进制（`0x`/`0X`）、八进制（`0o`/`0O`）、二进制（`0b`/`0B`）、数字分隔符（`_`）、浮点（科学计数法、`f` 后缀、`inf`/`nan`） |
| 变量 | `var` 声明（可选初始化）、`let` 常量（编译期求值） |
| 算术运算 | `add` / `sub` / `mul` / `div` / `mod`，支持 strict（溢出报错）和 wrap 模式；浮点支持 strict / ieee / wrap |
| 比较运算 | `eq` / `neq` / `lt` / `gt` / `le` / `ge` |
| 逻辑运算 | `and` / `or`（短路求值）、`not` |
| 单目运算 | `abs` / `neg` |
| 类型转换 | `cast`（支持 truncate / strict / widen 模式） |
| I/O | `write` / `writeln` / `read`，支持格式说明符（`d`/`i`/`u`/`x`/`X`/`o`/`b`/`t`/`f`/`e`/`E`/`g`/`G`） |
| 常量折叠 | `let` 初始化表达式的编译期求值（算术/比较/逻辑/cast/位运算均支持） |
| 控制流 | `if-then-else-end`（缩进敏感，支持嵌套，块级作用域） |
| 块级作用域 | then/else 互斥子作用域，允许同名局部变量，嵌套 shadowing |
| 位运算 | `and` / `or` / `xor` / `not`（按位，无溢出）；`shl`（strict/wrap）/ `shr`（算术/逻辑） |
| REPL | 交互式逐条执行，变量跨行保留（不支持 if） |

## 构建

构建由 **CMake** 统一管理；根目录 `Makefile` 是对 CMake 的薄封装。

### Makefile（推荐）

```sh
make                    # 配置并编译全部（libtc + VM + AOT）
make vm                 # 仅 VM
make aot                # 仅 AOT
make test               # 运行全部测试（VM + AOT + 单元测试）
make test-vm            # VM 一致性测试
make test-aot           # AOT 差分测试
make test-unit          # C 单元测试
make test-valgrind      # Valgrind Memcheck 模式（Linux）
make test-leaks         # macOS leaks 模式
make memcheck-macos     # macOS 完整内存检查（MallocScribble + leaks）
make bench              # 性能基准测试
make build-asan         # ASan 构建
make build-ubsan        # UBSan 构建
make ci                 # 本地 CI（构建 + 全部测试 + 静态检查）
make ci-coverage        # 本地 CI + 覆盖率报告
make clean              # 删除 build/ 目录
```

### CMake（等价命令）

```sh
cmake -S . -B build
cmake --build build                       # 编译全部
cmake --build build --target tc-vm        # 仅 VM
cmake --build build --target tc-aot       # 仅 AOT
cmake --build build --target check-vm     # VM 一致性测试
cmake --build build --target check-aot    # AOT 差分测试
cmake --build build --target check-unit   # C 单元测试
cmake --build build --target check        # 全部测试
```

### Sanitizer 模式

#### AddressSanitizer（内存错误检测）

```sh
# 方式一：Makefile 快捷方式
make build-asan
bash scripts/run_tests.sh --asan

# 方式二：手动 cmake
cmake -S . -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan
bash scripts/run_tests.sh --asan

# 方式三：一键脚本（构建 + 全量测试 + 报告）
bash scripts/run_asan_all.sh
```

#### UndefinedBehaviorSanitizer（未定义行为检测）

```sh
make build-ubsan
bash scripts/run_tests.sh --ubsan
```

### 内存安全检查

#### Linux：Valgrind Memcheck

```sh
make test-valgrind
# 等价于：
bash scripts/run_tests.sh --valgrind
```

#### macOS：leaks + MallocScribble

```sh
make test-leaks                   # 仅泄漏检测（leaks --atExit）
make memcheck-macos               # 双阶段：MallocScribble（越界/UAF）+ leaks（泄漏）
# 等价于：
bash scripts/run_tests.sh --leaks
bash scripts/run_memcheck_macos.sh
```

macOS `run_memcheck_macos.sh` 脚本依次执行：
1. **标准构建**（`make vm`）
2. **MallocScribble 模式**：`MallocScribble=1 MallocPreScribble=1`，检测越界读取/使用已释放内存
3. **leaks --atExit 模式**：进程退出时报告未释放的内存

> **注意**：Valgrind 在 macOS 上兼容性不佳，请优先使用内置的 `leaks` + `MallocScribble` 组合。

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

### AOT 模式

```sh
./build/aot/bin/tc-aot source.tc                      # 转译为 C99 源码（输出 source.tc.c）
./build/aot/bin/tc-aot -o output.c source.tc           # 指定输出路径
./build/aot/bin/tc-aot -r source.tc                    # 编译并运行生成的 C 代码
./build/aot/bin/tc-aot --check source.tc               # 仅静态分析
./build/aot/bin/tc-aot --help                          # 查看用法
```

## 文档

| 文档 | 说明 |
|------|------|
| [TC 语言标准设计说明书](docs/TC语言标准设计说明书.md) | 语言语法与语义权威定义（v0.0.25） |
| [TC-VM 详细设计说明书](docs/TC-VM详细设计说明书.md) | 直接执行引擎架构与实现约定（v0.0.25） |
| [TC-VM 命令行参考](docs/TC-VM命令行参考.md) | 使用 tc-vm 处理 `.tc` 源文件的命令说明（v0.0.25） |
| [TC-AOT 详细设计说明书](docs/TC-AOT详细设计说明书.md) | AOT 代码生成与 shim 层（v0.0.25） |
| [libtc 设计说明书](docs/libtc设计说明书.md) | libtc 静态库的设计架构与错误契约（v0.0.25） |
| [libtc 嵌入 API](docs/libtc-api.md) | libtc 静态库的嵌入编程接口速查 |

实现行为以语言标准为准；VM / AOT 详细设计文档规定各后端的实现架构，不重复定义语言语义。

## 性能分析

设置环境变量 `TC_BENCH=1` 时，编译/执行各阶段向 stderr 输出耗时：

```sh
TC_BENCH=1 ./build/vm/bin/tc-vm tests/valid/example.tc
```

配合 `scripts/vm/bench.sh` 用于本地回归对比。

## libtc 嵌入

libtc 是 TC 编译器的静态库，提供「编译（Parse + Analyze）」与「执行」分离的嵌入接口：

```c
#include "tc_lib.h"

TcDiagnostic diag;
TcTypedProgram program;

tc_diagnostic_init(&diag);
if (tc_compile_source("var x: int32 = 1\nwriteln(int32, x)\n", &program, &diag) != 0
    || tc_run_typed(&program, &diag) != 0) {
    tc_diagnostic_print(&diag, stderr);
    tc_typed_program_free(&program);
    return 1;
}
tc_typed_program_free(&program);
tc_diagnostic_clear(&diag);
return 0;
```

详见 [docs/libtc-api.md](docs/libtc-api.md)。

## 测试

### 统一入口（推荐）

```sh
bash scripts/run_tests.sh                          # 运行全部测试
bash scripts/run_tests.sh --filter foo             # 仅 VM 中匹配 "foo" 的用例
bash scripts/run_tests.sh --verbose                # 显示详细日志
bash scripts/run_tests.sh --asan                   # AddressSanitizer 模式
bash scripts/run_tests.sh --ubsan                  # UndefinedBehaviorSanitizer 模式
bash scripts/run_tests.sh --valgrind               # Valgrind Memcheck 模式（Linux）
bash scripts/run_tests.sh --leaks                  # macOS leaks 模式
```

### 等价 Makefile

```sh
make test
```

### 本地 CI

本地 CI 脚本替代远端 GitHub Actions，在本地执行完整的构建、测试和静态检查流水线。

**手动触发**（非自动，无需推送）：

```sh
make ci                          # 标准 CI（构建 + 全部测试 + 静态检查）
make ci-coverage                 # 含覆盖率收集与 HTML 报告
# 等价于：
bash scripts/ci.sh               # 标准 CI
bash scripts/ci.sh --coverage    # 含覆盖率
bash scripts/ci.sh --full        # 同上
```

CI 流水线包含 5 个阶段：

| 阶段 | 检查项 | 命令 |
|------|--------|------|
| 1/5 | 构建 (VM + AOT + libtc) | `cmake --build build` |
| 2/5 | VM Conformance 测试 | `make test-vm` |
| 3/5 | C 单元测试 | `make test-unit` |
| 4/5 | AOT Differential 测试 | `make test-aot` |
| 5/5 | 静态检查（RHS 覆盖 + 命名规范） | `check_rhs_coverage.py` + `check_source_naming.py` |

每次 CI 运行约 **1260+** 检测点（325 VM + 760 unit + 175 AOT）。

### GitHub Actions：ASan CI

`.github/workflows/asan.yml` 在每次推送至 `main` 或 PR 时自动触发：

- **平台**：`ubuntu-latest`
- **流程**：cmake ASan 配置 → 构建 → VM 一致性测试 → 单元测试 → AOT 差分测试
- 若发现内存错误（泄漏、越界、使用后释放等），CI 将失败

覆盖率报告生成于 `build-coverage/coverage_html/index.html`，可使用浏览器打开查看。

### Git hooks（可选）

首次克隆后建议安装，自动剥离提交信息中的 `Co-authored-by: Cursor <cursoragent@cursor.com>`：

```sh
make hooks
# 或：bash scripts/install-git-hooks.sh
```

## 作者

- **唐荣兵** ([yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)) — 项目创建与维护者
