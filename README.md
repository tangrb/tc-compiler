# TC-Compiler

TC-Compiler 是一个使用 C99 实现的 TC 语言工具链，包含：

- **libtc**：编译、静态分析和执行的嵌入式静态库；
- **TC-VM**：直接执行 TC 源文件的命令行工具；
- **TC-AOT**：将 TC 源码转译为严格 C99 的 ahead-of-time 编译器；
- **TC-Embed**：C 宿主程序调用 TC 编译产物的零拷贝嵌入式运行时（v0.0.39）。

当前核心版本：**v0.0.39**，Embed 模块版本：**v0.0.39**。语言语法与可观察语义以 [TC 语言标准设计说明书](docs/TC语言标准设计说明书-0.0.39.md) 为唯一权威来源。

## 快速开始

环境要求：C99 编译器、CMake 和 Make。TC-AOT 的 `--run` 模式还需要可用的宿主 `cc`。

```sh
make
./build/vm/bin/tc-vm tests/valid/example.tc
```

检查源码但不执行：

```sh
./build/vm/bin/tc-vm --check tests/valid/example.tc
```

将 TC 转译为 C99：

```sh
./build/aot/bin/tc-aot tests/valid/example.tc
# 输出 tests/valid/example.c
```

运行完整标准回归：

```sh
bash scripts/run_tests.sh
```

## 已实现能力

| 类别 | 当前能力 |
| ---- | -------- |
| 类型 | `int8` / `int16` / `int32` / `int64`、对应无符号整数、`bool`、`float32`、`float64`、`isize` / `usize`（平台字长） |
| 字面量 | 十进制、十六进制、八进制、二进制、数字分隔符、科学计数法、`f` 后缀、`inf` / `nan` |
| 绑定 | 强制初始化的 `var`；编译期求值且无运行时槽的 `let` |
| 运算 | 整数与浮点算术、比较、逻辑短路、单目运算、位运算和移位 |
| 转换 | 严格数值 `cast`、整数窄化 `truncate`、非 `bool` 等宽 `bitcast` |
| 控制流 | `if-then-else-end`、`while-then-end`、最内层 `break` / `continue`、受限 `goto` / `label` |
| 静态分析 | 13 阶段确定性编译管线、词法作用域、完整 CFG、可达性、静态布尔剪枝和固定点确定初始化 |
| I/O | `write` / `writeln` / `read`；13 种整数、布尔和浮点格式符 |
| 后端一致性 | VM、AOT 和 `let` 复用共享数值与 I/O 语义；AOT 运行差分锁定可观察结果 |
| 模块/函数 | `#program`/`#lib`、`import`、`func`/`funcall`/`return`、无环调用图、`static var`/`let` |
| 复合类型 | `ptr<T>`、`memblock<T,N>`、`struct`（构造器 / 字段读写 / 深拷贝；VM + AOT） |
| 嵌入互操作 | C→TC 零拷贝函数调用、共享 `slots[]` 数据平面、`ptr<T>` 句柄编码、符号查询；VM 与 AOT 双模式 API 兼容（v0.0.39） |

0.0.39 已移除 REPL；批量文件模式支持完整控制流。`goto`/`label` 仅函数内且 `while` 外。

## 语言示例

0.0.39 源文件必须以 `#program` 或 `#lib` 开头。算术、比较等运算使用显式类型的内建调用，而不是中缀运算符。

```tc
#program

var a: int32 = 10
var b: int32 = 20
var sum: int32 = add(int32, a, b)
writeln(int32, %d, sum)

if eq(int32, sum, 30) then
    writeln(int32, 1)
else
    writeln(int32, 0)
end
```

`#lib` 中的函数必须带 `public` / `private`，返回类型写在参数列表之后，函数体由 `then` / `end` 包裹：

```tc
#lib

public func plus(a: int32, b: int32) int32 then
    var sum: int32 = add(int32, a, b)
    return sum
end
```

入口程序通过 `import` 与 `funcall` 调用库函数（命名实参）：

```tc
#program
import math_lib

var r: float64 = 3.14
var area: float64 = funcall(math_lib.compute_area, r: r)
writeln(float64, %f, area)
```

完整语法与语义见 [TC 语言标准设计说明书](docs/TC语言标准设计说明书-0.0.39.md)。

## 构建

根目录 Makefile 是 CMake 的便捷入口。

```sh
make                    # 配置并构建默认全部目标：libtc、TC-VM、TC-AOT
make vm                 # 当前与 make 相同，构建默认全部目标
make aot                # 构建 TC-AOT 及其依赖
make clean              # 删除 build/
```

需要精确构建单个 CMake 目标时：

```sh
cmake -S . -B build
cmake --build build --target tc-vm
cmake --build build --target tc-aot
cmake --build build --target libtc
```

项目以 `-std=c99 -Wall -Wextra -pedantic` 编译；AOT 差分测试对生成的 C 额外启用 `-Werror`。

## 使用

### TC-VM

```sh
./build/vm/bin/tc-vm program.tc          # 编译并执行
./build/vm/bin/tc-vm --check program.tc  # 仅编译与静态分析
./build/vm/bin/tc-vm -I ./lib program.tc # 附加模块搜索路径
./build/vm/bin/tc-vm --help
./build/vm/bin/tc-vm --version
```

完整命令行为见 [TC-VM 命令行参考](docs/TC-VM命令行参考-0.0.39.md)。

### TC-AOT

```sh
./build/aot/bin/tc-aot source.tc             # 生成 source.c（标准模式，含 main()）
./build/aot/bin/tc-aot -o output.c source.tc # 指定 C 输出路径
./build/aot/bin/tc-aot --check source.tc      # 仅静态分析，不生成 C
./build/aot/bin/tc-aot --run source.tc        # 生成、用宿主 cc 编译并执行
./build/aot/bin/tc-aot --embed source.tc      # 嵌入库模式：无 main()，公开符号 + 函数表
./build/aot/bin/tc-aot --embed -H out.h source.tc  # 嵌入模式 + 生成宿主头文件
./build/aot/bin/tc-aot --help
```

`--run` 依赖宿主 C99 工具链；纯代码生成和 `--check` 不要求把宿主编译器可用性视为 TC 语言合规条件。`--embed` 与 `--run` 互斥。

## 嵌入 libtc

libtc 采用"成功才转移所有权"的契约：编译成功后，调用方必须释放 `TcTypedProgram`；编译失败时，调用方没有取得输出所有权，不得释放本次输出。

```c
#include <stdio.h>

#include "tc_lib.h"

int main(void) {
    const char *source =
        "#program\n"
        "var x: int32 = add(int32, 1, 6)\n"
        "writeln(int32, %d, x)\n";
    TcDiagnostic diag;
    TcTypedProgram program;

    tc_diagnostic_init(&diag);

    if (tc_compile_source(source, "example.tc", &program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (tc_run_program(&program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_typed_program_free(&program);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
    return 0;
}
```

公共入口为 `tc_compile_source`、`tc_compile_file`、`tc_set_module_search_paths` 和 `tc_run_program`；完整所有权、诊断和构建说明见 [libtc 嵌入 API](docs/libtc-api-0.0.39.md)。

## 嵌入 TC-Embed（v0.0.39）

TC-Embed 提供 C 宿主程序对 TC 编译产物的零拷贝调用能力。C 和 TC 共享同一个 `TcValue slots[]` 数组，`ptr<T>` 槽位编码 `(slot << 1) | 1` 作为 C↔TC 之间传递变量引用的统一句柄。

核心头文件：`src/vm/embed/tc_embed.h` + `src/vm/embed/tc_value_bridge.h`。

### VM 模式

通过 libtc 编译 TC 源码后，用 `tc_embed_create` 创建嵌入上下文：

```c
#include <stdint.h>
#include <stdio.h>

#include "tc_embed.h"
#include "tc_lib.h"

int main(void) {
    const char *src =
        "#lib\n"
        "public func plus(a: int32, b: int32) int32 then\n"
        "    var sum: int32 = add(int32, a, b)\n"
        "    return sum\n"
        "end\n";
    TcDiagnostic diag;
    TcTypedProgram prog;
    TcEmbedCtx *ctx;
    const TcEmbedFuncInfo *info;
    TcValue result;
    int32_t ret;

    tc_diagnostic_init(&diag);
    if (tc_compile_source(src, "plus.tc", &prog, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    ctx = tc_embed_create(&prog, &diag);
    if (ctx == NULL) {
        tc_diagnostic_print(&diag, stderr);
        tc_typed_program_free(&prog);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    info = tc_embed_func_info(ctx, NULL, "plus");
    if (info == NULL ||
        tc_embed_call_typed(ctx, info,
                            TC_EMBED_ARGS(tc_embed_arg_i32(3),
                                          tc_embed_arg_i32(4)),
                            &result) != 0) {
        fprintf(stderr, "%s\n", tc_embed_get_error(ctx));
        tc_embed_destroy(ctx);
        tc_typed_program_free(&prog);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    tc_value_to_int32(result, &ret);
    printf("3 + 4 = %d\n", ret);  /* 输出: 3 + 4 = 7 */

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
    return 0;
}
```

### AOT 模式

将 TC 源码经 `tc-aot --embed` 转译为嵌入库 C 代码，用宿主 `cc` 编译为共享库，通过 `tc_embed_create_aot` 加载。同一套 `tc_embed_call` / `tc_embed_slot_*` / `tc_embed_ptr_*` API 在 VM 和 AOT 模式间 API 兼容。

```sh
# 生成嵌入库 C 代码和宿主头文件
./build/aot/bin/tc-aot --embed -o mylib.c -H mylib.h mylib.tc
```

将生成的 `mylib.c` 与宿主程序、AOT runtime shim（`tc_aot_rt.c`）以及 Embed 桥接源一并按 C99 链接。完整文件清单与 `cc` 命令见 [TC-Embed 详细设计说明书](docs/TC-Embed详细设计说明书-0.0.39.md) §15.6。

### 值桥接

`tc_value_bridge.h` 提供 `tc_value_from_*` / `tc_value_to_*` 纯 inline 辅助函数族，覆盖 `int8`–`int64`、`uint8`–`uint64`、`float32`、`float64` 和 `bool`，无需额外编译单元或运行时开销。

```c
TcValue v = tc_value_from_int64(42);
int64_t x;
tc_value_to_int64(v, &x);
```

完整 API 设计见 [TC-Embed 详细设计说明书](docs/TC-Embed详细设计说明书-0.0.39.md)。

## 测试与质量门禁

### 标准回归

```sh
bash scripts/run_tests.sh                # VM + AOT + unit
bash scripts/run_tests.sh --filter foo   # 仅过滤 VM；AOT 和 unit 仍全量运行
bash scripts/run_tests.sh --verbose      # 仅增加 VM 日志
```

也可分别运行：

```sh
make test-vm
make test-unit
make test-aot
make test
```

### 静态结构检查

```sh
python3 scripts/sync/check_rhs_coverage.py
python3 scripts/sync/check_source_naming.py
```

### Sanitizer 与内存检查

```sh
bash scripts/run_asan_all.sh                 # ASan 构建及 VM/AOT/unit 全矩阵

make build-ubsan
bash scripts/run_tests.sh --ubsan            # UBSan VM；AOT/unit 使用标准构建

make test-valgrind                           # Linux；Valgrind VM + 标准 AOT/unit
make memcheck-macos                          # macOS；MallocScribble + leaks
```

`bash scripts/run_tests.sh --asan`、`--ubsan`、`--valgrind` 和 `--leaks` 只把模式参数传给 VM 测试；AOT 与 unit 仍按标准构建运行。需要完整 ASan 矩阵时使用 `scripts/run_asan_all.sh`。

### 本地与远端 CI

```sh
make ci                  # 构建、三组测试、RHS 覆盖和源文件命名检查
make ci-coverage         # 额外生成覆盖率报告
```

本地入口由 `scripts/ci.sh` 实现，与 `.github/workflows/ci.yml` 的核心五阶段一致。GitHub Actions 还运行：

- Ubuntu、macOS 与 **Windows（MSYS2 UCRT64 / MinGW gcc）** 标准矩阵；
- Ubuntu UBSan；
- no-fenv 浮点后备路径；
- benchmark 回归；
- coverage artifact；
- 独立 Ubuntu ASan 工作流；
- 打 `v*` tag 时构建 Linux / macOS / Windows 二进制并创建 GitHub Release。

覆盖率 HTML 输出到 `build-coverage/coverage_html/index.html`。Windows 作业使用 MinGW 而非 MSVC：AOT `--run` 需要 gcc 风格的 host `cc`。

## 性能观测

设置 `TC_BENCH` 后，parse、analyze 和 execute 阶段耗时会写入 stderr：

```sh
TC_BENCH=1 ./build/vm/bin/tc-vm tests/valid/example.tc
```

本地 benchmark：

```sh
make bench
sh scripts/vm/bench.sh --check
```

回归阈值位于 `tests/stress/bench_limits.txt`。

## 项目结构

```text
docs/               正式语言、实现、CLI 与 API 文档
src/
├── libtc/          嵌入式编译/执行库
├── vm/
│   ├── lexer/      词法分析
│   ├── parser/     语法分析
│   ├── analyzer/   静态分析（含 CFG、类型检查、函数/调用图）
│   ├── executor/   执行器与调用帧
│   ├── runtime/    运行时（类型、语义、I/O、符号表、诊断）
│   ├── embed/      TC-Embed 嵌入运行时（v0.0.39）
│   └── driver/     入口程序与版本
└── aot/            C99 codegen、runtime shim、CLI、嵌入模式运行时
tests/
├── valid/          合法程序与可观察输出
├── errors/         静态和运行时错误
├── vm/embed/       TC-Embed 集成测试
├── unit/           C 单元测试（27 个 target，含 check-embed / check-embed-aot）
├── modules/        模块系统测试
└── stress/         压力与性能场景
scripts/
├── vm/             VM 回归与 benchmark
├── aot/            AOT 差分与嵌入代码生成测试
└── sync/           RHS 分发与源文件命名检查
```

## 文档

| 文档 | 职责 |
| ---- | ---- |
| [TC 语言标准设计说明书](docs/TC语言标准设计说明书-0.0.39.md) | 0.0.39 语法、语义和诊断的唯一权威来源 |
| [TC 编译器标准设计说明书](docs/TC编译器标准设计说明书-0.0.39.md) | 13 阶段管线、诊断优先级、调用图等编译器规范 |
| [TC-VM 命令行参考](docs/TC-VM命令行参考-0.0.39.md) | `tc-vm` 使用方式、输出和退出行为 |
| [libtc 嵌入 API](docs/libtc-api-0.0.39.md) | 公共函数、所有权与诊断速查 |
| [TC-VM 详细设计说明书](docs/TC-VM详细设计说明书-0.0.39.md) | VM 流水线、IR、CFG、执行器设计 |
| [TC-AOT 详细设计说明书](docs/TC-AOT详细设计说明书-0.0.39.md) | C99 生成、runtime shim 与差分验证 |
| [libtc 设计说明书](docs/libtc设计说明书-0.0.39.md) | libtc 架构、事务、生命周期和错误契约 |
| [TC-Embed 详细设计说明书](docs/TC-Embed详细设计说明书-0.0.39.md) | C→TC 嵌入互操作 API、`ptr<T>` 句柄模型、VM/AOT 双模式设计 |
| [设计—实现合规审查报告](docs/设计实现合规审查报告-0.0.39.md) | 0.0.39 的 ~182 项合规矩阵与发布证据 |

## Git hooks

可选安装仓库 hooks：

```sh
make hooks
# 或 bash scripts/install-git-hooks.sh
```

## 许可证

本项目基于 Apache License 2.0 发布，详见 [LICENSE](LICENSE)。

## 作者

**唐荣兵**（[yanhuang8923@qq.com](mailto:yanhuang8923@qq.com)）— 项目创建与维护者
