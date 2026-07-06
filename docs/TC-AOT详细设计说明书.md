# TC-AOT 详细设计说明书

> **版本**：0.0.23（草案）  
> **依赖**：[TC语言标准设计说明书.md](./TC语言标准设计说明书.md) v0.0.23  
> **工程**：[TC-Compiler](../README.md) 之 `src/aot/` 组件  
> **定位**：将 TC 源文件提前编译（Ahead-of-Time）为 C99 源码，经系统编译器生成原生可执行文件

---

## 目录

1. [设计目标与原则](#1-设计目标与原则)
2. [总体架构](#2-总体架构)
3. [流水线](#3-流水线)
4. [代码生成策略](#4-代码生成策略)
5. [运行时 Shim 层](#5-运行时-shim-层)
6. [I/O 实现](#6-io-实现)
7. [let 编译期常量优化](#7-let-编译期常量优化)
8. [CLI 与构建](#8-cli-与构建)
9. [测试策略](#9-测试策略)
10. [与 VM 的行为一致性保障](#10-与-vm-的行为一致性保障)
11. [后续扩展](#11-后续扩展)

附录

- [附录 A：生成 C 代码示例](#附录-a生成-c-代码示例)
- [附录 B：文档修订记录](#附录-b文档修订记录)

---

## 1. 设计目标与原则

### 1.1 目标

TC-AOT 将 `.tc` 源文件编译为 C99 源码，再调用系统 C 编译器（gcc/clang）生成原生可执行文件。AOT 编译的输出与 VM 解释执行结果完全一致。

### 1.2 核心原则

| 原则 | 说明 |
| ---- | ---- |
| **委托不重复** | 算数、比较、逻辑、cast、I/O 等运算语义**委托** `tc_semantics.c` / `tc_io.c`，不在 AOT 层重新实现 |
| **差分一致** | 对同一 `.tc` 源文件，`tc-vm` 与 `tc-aot --run` 的输出逐字节一致 |
| **编译期折叠** | `let` 常量的编译期求值结果由 Analyzer 完成，AOT codegen 直接使用位模式，不重复求值 |
| **TC 源码即程序** | 不引入中间表示或 IR，直接从 `TcTypedProgram` 生成 C 代码 |

### 1.3 实现版本

可执行文件 `tc-aot` 版本为 **v0.0.23**（`main.c` 中 `TC_AOT_VERSION`）；与 VM 共享 `tc_types.h` 中的类型定义。

---

## 2. 总体架构

```
.tc 源文件
    │
    ▼
libtc (tc_compile_file)    ← 与 VM 共享编译流水线
    │  TcTypedProgram
    ▼
tc_aot_emit_c               ← 逐语句生成 C99
    │  .c 文件
    ▼
host C compiler (gcc/clang)
    │  可执行文件
    ▼
运行
```

### 2.1 组件文件

| 文件 | 职责 |
|------|------|
| `main.c` | CLI 入口：参数解析、委托 libtc 编译、调用 codegen、可选编译运行 |
| `tc_aot_codegen.c` / `.h` | `tc_aot_emit_c()` — 从 `TcTypedProgram` 生成 C99 源码 |
| `tc_aot_rt.c` / `.h` | 运行时辅助函数（shim），委托 `tc_semantics.c` / `tc_io.c` |

### 2.2 构建集成

```cmake
add_executable(tc-aot main.c tc_aot_codegen.c)
target_link_libraries(tc-aot PRIVATE libtc)
```

AOT 生成的可执行文件在链接时包含 `tc_aot_rt.c`、`tc_types.c`、`tc_diagnostic.c`、`tc_semantics.c`、`tc_io.c`，与 VM 共享运行时实现。

---

## 3. 流水线

```
tc-aot <file.tc>
    │
    ├─ 1. tc_compile_file()       libtc：Parse + Analyze
    │     └─ TcTypedProgram
    ├─ 2. tc_aot_emit_c()         codegen：emit C99
    │     ├─ 生成 slots[] + includes
    │     ├─ tc_aot_emit_statement() × N
    │     └─ main() 入口
    ├─ 3. [--run] host cc         编译 .c → 可执行文件
    └─ 4. [--run] 运行可执行文件
```

### 3.1 编译阶段（libtc 委托）

AOT 与 VM 共享 `tc_compile_file()`，因此词法、语法、静态分析（含 let 编译期求值）行为完全一致。差异从代码生成开始。

### 3.2 代码生成阶段

`tc_aot_emit_c()` 接收 `TcTypedProgram`，输出 C99 源码：

1. 生成 `#include` 和 `slots[]` 数组定义
2. 生成 `main()` 函数
3. 调用 `tc_aot_init_slots()` 初始化未初始化哨兵
4. 逐语句发射 C 代码

### 3.3 编译运行阶段（`--run` 模式）

`tc_aot_run_generated()` 调用系统 C 编译器编译生成的 `.c` 文件，并执行生成的可执行文件：

```bash
cc -std=c99 -Wall -Wextra -pedantic \
  -I"<aot_dir>" -I"<vm_dir>/runtime" \
  "<file>.c" "<aot_dir>/tc_aot_rt.c" \
  "<vm_dir>/runtime/tc_types.c" \
  "<vm_dir>/runtime/tc_diagnostic.c" \
  "<vm_dir>/runtime/tc_semantics.c" \
  "<vm_dir>/runtime/tc_io.c" \
  -o "<file>.out" && "<file>.out"
```

---

## 4. 代码生成策略

### 4.1 整体结构

生成的 C 文件包含：

```c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tc_aot_rt.h"

static uint64_t slots[N];         /* 变量槽位数组（TcValue.bits 的扁平表示） */

int main(void) {
    TcDiagnostic diag;
    tc_aot_diag_init(&diag);
    tc_aot_init_slots(slots, N);   /* 填充未初始化哨兵 */
    
    /* 逐语句生成：变量定义、赋值、I/O */
    slots[0] = tc_aot_lit(TC_INT32, 42ULL, 0, 0);                    /* var x = 42 */
    if (tc_aot_arith(TC_ADD, TC_INT32, TC_ARITH_STRICT, &slots[1],  /* y = add(...) */
                     slots[0], tc_aot_lit(TC_INT32, 1ULL, 0, 0),
                     &diag, 3) != 0)
        tc_aot_abort(&diag, 3);
    tc_aot_write(TC_INT32, TC_FMT_NONE, slots[1], 1);               /* writeln(y) */
    
    return 0;
}
```

### 4.2 变量槽位

- 变量槽位使用 `uint64_t` 数组（`TcValue.bits` 的扁平表示），类型信息由枚举参数传给运行时函数
- 槽位索引由 Analyzer Pass2 分配的 `TcSymbol.slot` 确定，与 VM 一致
- 未初始化的槽位由 `tc_aot_init_slots()` 填充 0xFE 哨兵（与 VM 的 `tc_slots_init_uninitialized()` 一致）

### 4.3 语句发射

`tc_aot_emit_statement()` 按 `TcStatement.kind` 分发：

| 语句类型 | 生成策略 |
|----------|---------|
| `VarDef`（有 RHS） | 调用 `tc_aot_emit_rhs()`，结果写入目标槽位 |
| `VarDef`（无 RHS） | 跳过，槽位保持未初始化哨兵 |
| `ConstDef`（有 const_value） | 直接写入编译期求值的位模式 |
| `ConstDef`（无 const_value） | 调用 `tc_aot_emit_rhs()`（极少发生，仅当 Analyzer 无法求值） |
| `Assign` | 调用 `tc_aot_emit_rhs()`，结果写入目标槽位 |
| `Write` / `Writeln` | 调用 `tc_aot_write()` |
| `Read` | 调用 `tc_aot_read()`，失败时 `tc_aot_abort()` |

### 4.4 RHS 发射

`tc_aot_emit_rhs()` 是 codegen 的核心 switch 分发点，按 `TcRhsKind` 生成对应的运行时调用：

| RHS Kind | 生成的调用 |
|----------|-----------|
| `LIT` | 内联 `tc_aot_lit()` 调用 |
| `ARITH` | `tc_aot_arith(op, type, mode, &slots[dst], lhs, rhs, &diag, line)` |
| `UNARY` | `tc_aot_unary(op, type, mode, &slots[dst], operand, &diag, line)` |
| `COMPARE` | `tc_aot_compare(op, type, &slots[dst], lhs, rhs, &diag, line)` |
| `LOGIC_BIN` | `tc_aot_logic(op, &slots[dst], lhs, rhs, &diag, line)` |
| `LOGIC_UN` | `tc_aot_logic_unary(op, &slots[dst], operand, &diag, line)` |
| `BITWISE_BIN` | `tc_aot_bitwise_binary(op, type, &slots[dst], lhs, rhs, &diag, line)` |
| `BITWISE_UN` | `tc_aot_bitwise_unary(type, &slots[dst], operand, &diag, line)` |
| `SHIFT` | `tc_aot_shift(op, type, mode, &slots[dst], value, count, &diag, line)` |
| `CAST` | `tc_aot_cast(target, mode, src_bits, src_type, &slots[dst], &diag, line)` |
| `CONST_REF` / `CONST_CAST` | 编译期已折叠，返回 `-1` 表示不应在运行时生成代码（参见 §7） |

所有 RHS 调用均检查返回值：非 0 表示运行时错误，通过 `tc_aot_abort()` 终止。

### 4.5 字面量发射

`tc_aot_emit_literal_expr()` 生成对 `tc_aot_lit()` 的调用：

```c
/* bool 字面量 */
tc_aot_lit(TC_BOOL, 1ULL, 0, 0)

/* 整数字面量 */
tc_aot_lit(TC_INT32, 42ULL, 0, 0)
tc_aot_lit(TC_INT32, 10ULL, 1, 0)  /* 负数：magnitude=10, negative=1 */
```

---

## 5. 运行时 Shim 层

### 5.1 设计模式

`tc_aot_rt.c` 采用 **Shim（垫片）** 模式：将 AOT 生成的 C 代码中 `uint64_t` 槽位操作委托给 `tc_semantics.c` 中基于 `TcValue` 的运算函数。

```
AOT 生成代码 (uint64_t slots[])
    │  tc_aot_arith(out, lhs, rhs, ...)
    ▼
tc_aot_rt.c shim
    │  构建 TcValue → 调用 tc_exec_arith → 提取 result.bits
    ▼
tc_semantics.c (与 VM 共享)
```

### 5.2 函数映射

| 运行时函数 | 委托目标 | 对应 VM 执行函数 |
|-----------|---------|-----------------|
| `tc_aot_lit` | `tc_literal_to_value` | `tc_eval_operand`（字面量分支） |
| `tc_aot_arith` | `tc_exec_arith` | `tc_eval_rhs`（ARITH 分支） |
| `tc_aot_unary` | `tc_exec_unary` | `tc_eval_rhs`（UNARY 分支） |
| `tc_aot_compare` | `tc_exec_compare` | `tc_eval_rhs`（COMPARE 分支） |
| `tc_aot_logic` | `tc_exec_logic_binary` | `tc_eval_rhs`（LOGIC_BIN 分支） |
| `tc_aot_logic_unary` | `tc_exec_logic_unary` | `tc_eval_rhs`（LOGIC_UN 分支） |
| `tc_aot_bitwise_binary` | `tc_exec_bitwise_binary` | `tc_eval_rhs`（BITWISE_BIN 分支） |
| `tc_aot_bitwise_unary` | `tc_exec_bitwise_unary` | `tc_eval_rhs`（BITWISE_UN 分支） |
| `tc_aot_shift` | `tc_exec_shift` | `tc_eval_rhs`（SHIFT 分支） |
| `tc_aot_cast` | `tc_exec_cast` | `tc_eval_rhs`（CAST 分支） |
| `tc_aot_write` | `tc_io_write_value` | `tc_execute_statement`（Write 分支） |
| `tc_aot_read` | `tc_io_read_value` | `tc_execute_statement`（Read 分支） |

### 5.3 为什么用 Shim 而非重复实现

1. **一致性保证**：一次实现，VM 与 AOT 共享，消除平行实现导致的偏差
2. **维护成本**：新增运算语义只需改 `tc_semantics.c`，AOT 侧自动受益
3. **测试覆盖**：单元测试已覆盖 `tc_semantics.c` 边界，AOT 差分测试验证全链路

### 5.4 错误处理

运行时函数返回非 0 时，`tc_aot_abort()` 打印诊断到 stderr 并以退出码 1 终止：

```c
void tc_aot_abort(const TcDiagnostic *diag, int line) {
    tc_diagnostic_print(diag, stderr);
    exit(1);
}
```

---

## 6. I/O 实现

AOT 的 I/O（`write` / `writeln` / `read`）与 VM 完全共享 `tc_io.c` 的实现：

| 函数 | 委托链路 |
|------|---------|
| `tc_aot_write` | → `tc_io_write_value()`（与 `tc_executor.c` 一致） |
| `tc_aot_read` | → `tc_io_read_value()`（与 `tc_executor.c` 一致） |

Codegen 阶段为 I/O 语句生成对 `tc_aot_write()` / `tc_aot_read()` 的直接调用，与 RHS 运算的 shim 模式一致。

---

## 7. let 编译期常量优化

### 7.1 编译期折叠

`let` 常量的编译期求值在 Analyzer Pass2 完成（`tc_const_eval.c`），求值结果存储在 `TcSymbol.const_value` 中。

### 7.2 Codegen 阶段

当 `TcSymbol.has_const_value` 为真时，codegen **跳过** RHS 的运行时求值，直接使用编译期位模式：

```c
if (symbol->has_const_value) {
    /* 直接使用编译期求值的位模式 */
    fprintf(out, "    slots[%d] = 0x%016" PRIx64 "ULL;\n",
            symbol->slot, symbol->const_value.bits);
}
```

### 7.3 CONST_REF / CONST_CAST

`tc_aot_emit_rhs()` 遇到 `TC_RHS_CONST_REF` 或 `TC_RHS_CONST_CAST` 时返回 `-1`。这些 RHS kind 不应出现在已折叠的 `ConstDef` 中；若出现（如 `Assign` 中引用常量），则 codegen 报错。

---

## 8. CLI 与构建

### 8.1 命令行用法

```
tc-aot [options] <file.tc>

选项:
  -o, --output FILE   输出路径（默认 <input>.c）
  -c, --check         仅静态分析，不生成 C
  -r, --run           编译并运行生成的 C（依赖 host C 编译器）
  -h, --help          显示帮助
  -V, --version       显示版本号（v0.0.23）

退出码: 0 成功，1 失败
```

### 8.2 构建目标

| 目标 | 说明 |
|------|------|
| `make aot` | 编译 tc-aot |
| `make test-aot` / `check-aot` | 运行 AOT 差分测试 |
| `make test` | 全量测试（VM + AOT + 单元） |

### 8.3 编译依赖

`tc-aot` 链接 libtc，编译时须包含以下头文件路径：

- `src/libtc/` — libtc API
- `src/aot/` — AOT codegen / rt 头文件
- `src/vm/runtime/` — `TcValue`、`TcDiagnostic` 等运行时类型

---

## 9. 测试策略

### 9.1 差分测试

AOT 测试采用**差分比较**策略，确保 AOT 生成的二进制输出与 VM 解释执行结果一致：

1. 用 `tc-vm` 运行 `.tc` 源文件，捕获 stdout
2. 用 `tc-aot --run` 编译并运行同一源文件，捕获 stdout
3. 比较两者是否完全一致

### 9.2 测试脚本

| 脚本 | 职责 | 被调用方 |
|------|------|---------|
| `scripts/aot/run_tests.sh` | 注册 AOT 差分测试用例 | `check-aot` cmake target |
| `scripts/vm/run_tests.sh` | 注册 VM conformance 测试 | `check-vm` cmake target |
| `scripts/run_tests.sh` | 统一入口，顺序调用二者 | — |

### 9.3 新增 AOT 测试用例流程

添加新的语言特性后，按以下步骤确保 AOT 覆盖：

1. **在 `tests/valid/` 中编写正例**（与 VM conformance 共用同一文件）
2. **在 `scripts/aot/run_tests.sh` 中注册**：
   ```bash
   run_diff_test "$ROOT/tests/valid/your_new_test.tc"
   ```
3. **在 `scripts/vm/run_tests.sh` 中注册**（预期 stdout 或成功退出码）
4. **验证**：
   ```bash
   bash scripts/run_tests.sh           # 统一入口
   # 或:
   make test                            # cmake 入口
   ```

### 9.4 注册规则

- AOT 差分测试要求程序的 stdout **完全确定**（非 `--check` 模式，非常量错误场景）
- 使用 stdin 输入的用例需传递第二参数：
  ```bash
  run_diff_test "$ROOT/tests/valid/read_foo.tc" "input_data\n"
  ```
- 涉及运行时错误的用例不在 AOT 差分测试中注册（AOT 测试仅验证成功执行的程序）

### 9.5 常见遗漏检查

新增 `TcRhsKind` 或语句变体后，运行 `python3 scripts/sync/check_rhs_coverage.py` 验证所有分发点（含 `tc_aot_emit_rhs`）已覆盖。代码生成变更后务必执行 `make test-aot` 确认无 regression。

### 9.6 当前覆盖（~22 条）

AOT 差分测试覆盖：基本运算、wrap 模式、cast、字面量、let 常量、I/O、注释等——与 VM valid 正例共用测试文件。

---

## 10. 与 VM 的行为一致性保障

| 保障机制 | 说明 |
|---------|------|
| **共享编译流水线** | 使用相同的 libtc `tc_compile_file()`，Parse + Analyze 结果一致 |
| **共享语义实现** | shim 层委托 `tc_semantics.c` / `tc_io.c`，与 VM 执行器共享所有运算函数 |
| **共享未初始化哨兵** | 使用 `tc_slot_bits_init_uninitialized()`，与 `tc_slots_init_uninitialized()` 值一致 |
| **差分测试** | 同一 `.tc` 文件在 VM 和 AOT 下的 stdout 逐字节比较 |
| **RHS 分发点同步** | `check_rhs_coverage.py` 验证 `tc_aot_emit_rhs` 与所有 VM 分发点同步 |

---

## 11. 后续扩展

### 11.1 预留扩展点

| 扩展 | 影响 |
|------|------|
| 新 `TcRhsKind` | 在 `tc_aot_emit_rhs()` 新增 case + `tc_aot_rt.c` 新增 shim |
| 新语句类型 | 在 `tc_aot_emit_statement()` 新增 case |
| 位运算/移位 | `tc_aot_bitwise_*` / `tc_aot_shift` shim → `tc_exec_bitwise_*` / `tc_exec_shift` |
| 运行时错误恢复 | 当前 `tc_aot_abort()` 策略（exit），未来可改为返回值传播 |

### 11.2 已知限制

- AOT 不支持 REPL 模式（REPL 是 VM 交互式特性）
- AOT 不支持 `--check` 模式生成代码（`--check` 时仅做静态分析，不生成 C）
- AOT 不支持常量运行时错误场景（如除零不会通过 Analyzer）

---

## 附录 A：生成 C 代码示例

输入 `example.tc`：

```tc
var a: int32 = 10
var b: int32 = 20
var sum: int32 = add(int32, a, b)
writeln(int32, sum)
```

输出 `example.c`：

```c
/* Auto-generated by tc-aot from example.tc. Do not edit. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tc_aot_rt.h"

static uint64_t slots[3];

int main(void) {
    TcDiagnostic diag;
    tc_aot_diag_init(&diag);
    tc_aot_init_slots(slots, 3);

    slots[0] = tc_aot_lit(TC_INT32, 10ULL, 0, 0);           /* var a: int32 = 10 */
    slots[1] = tc_aot_lit(TC_INT32, 20ULL, 0, 0);           /* var b: int32 = 20 */
    if (tc_aot_arith(TC_ADD, TC_INT32, TC_ARITH_STRICT,     /* var sum: int32 = add(...) */
                     &slots[2], slots[0], slots[1],
                     &diag, 3) != 0)
        tc_aot_abort(&diag, 3);
    tc_aot_write(TC_INT32, TC_FMT_NONE, slots[2], 1);       /* writeln(int32, sum) */

    return 0;
}
```

---

## 附录 B：文档修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.0.21 | 2026-07-06 | 首版：从 TC-VM 详细设计说明书独立——提取 §14.6 AOT 差分测试、§12.1 工程布局、§14.7 层间调用关系相关内容；新增架构、codegen、shim 层、一致性保障等章节 |
| 0.0.23 | 2026-07-06 | 位运算/移位：`tc_aot_bitwise_binary/unary`、`tc_aot_shift` shim；`tc_aot_emit_rhs` 新增 BITWISE/SHIFT case；差分测试增至 26（含 `bitwise_*`） |
