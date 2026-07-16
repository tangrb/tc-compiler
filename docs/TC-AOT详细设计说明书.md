# TC-AOT 详细设计说明书

> **规范基线**：[TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md)
>
> **当前实现基线**：TC-AOT v0.0.31（`TC_AOT_VERSION`）
>
> **状态**：0.0.31 代码生成架构已实现，并通过 257 项 VM/AOT 差分与 C99 严格编译门禁。
>
> **上游契约**：[TC-VM 详细设计说明书](./TC-VM详细设计说明书.md) 的 typed program、完整 CFG 与共享运行时语义

---

## 目录

1. [边界与目标](#1-边界与目标)
2. [总体架构](#2-总体架构)
3. [输入契约](#3-输入契约)
4. [生成 C99 的布局](#4-生成-c99-的布局)
5. [语句代码生成](#5-语句代码生成)
6. [控制流代码生成](#6-控制流代码生成)
7. [RHS 与运行时 shim](#7-rhs-与运行时-shim)
8. [`bitcast` 与数值一致性](#8-bitcast-与数值一致性)
9. [`let` 常量](#9-let-常量)
10. [I/O 与诊断](#10-io-与诊断)
11. [CLI、构建与产物](#11-cli构建与产物)
12. [差分验证](#12-差分验证)
13. [模块与接口](#13-模块与接口)
14. [实现基线与迁移](#14-实现基线与迁移)

---

## 1. 边界与目标

### 1.1 版本基线

| 维度 | 版本 | 状态 |
| ---- | ---- | ---- |
| 目标语言 | TC 0.0.31 | 规范已确定 |
| 当前转译器 | TC-AOT v0.0.31 | 已实现、已有 257 项差分基线 |
| 本文 | 0.0.31 已实现设计 | 代码与测试均已交付 |

当前 `tc-aot` 已支持 `while`、`break`、`continue`、`bitcast`、强制 `var` 初始化器和 0.0.31 诊断集合。

### 1.2 目标

- 将成功的 `TcTypedProgram` 确定性转译为可移植 C99。
- 生成程序与 VM 在 stdout、stderr 分类、退出状态、数值位模式和运行时错误时机上一致。
- 所有静态错误在生成 C 前完成；AOT 不重新发明另一套合法性规则。
- 结构化循环优先生成结构化 C；受限 goto 生成唯一 C 标签。
- 算术、浮点、cast、bitcast 与 I/O 复用共享语义或经差分证明等价。
- 生成代码不依赖 C 的有符号溢出、严格别名违规、未初始化读取或实现定义移位。

### 1.3 非目标

- 不输出机器码、目标文件或自定义链接器格式。
- 不把宿主 C 编译器诊断当成 TC 语言诊断。
- 不承诺生成 C 的人工可维护性优先于语义一致性。
- 不在 codegen 中接受 Analyzer 已拒绝的程序。

---

## 2. 总体架构

```text
.tc source
  │
  ├─ libtc: Parse + Bind/Type + CFG + Dataflow
  │
  └─ TcTypedProgram（静态合法）
       │
       ├─ tc_aot_emit_c
       │    ├─ preamble / slots / diagnostic
       │    ├─ statement emission
       │    └─ runtime error guards
       │
       └─ generated .c
            + tc_aot_rt.c
            + shared runtime semantics
                 │
                 └─ host C99 compiler → executable
```

### 2.1 分层

| 层 | 责任 | 不负责 |
| -- | ---- | ------ |
| libtc/Analyzer | 全部静态合法性、槽位、绑定、CFG 语义 | 生成 C |
| codegen | 结构与表达式的确定性发射 | 重新判断语言是否合法 |
| AOT runtime shim | 运行时数值、I/O、诊断桥接 | 源码解析与作用域 |
| host compiler | 编译合法 C99 | 定义 TC 语义 |

### 2.2 失败边界

- `tc_compile_file` 失败：打印 TC/实现诊断，不创建可信输出。
- `tc_aot_emit_c` 失败：通常为输出 I/O、OOM 或内部未覆盖 kind；删除/忽略不完整产物。
- host C 编译失败：属于工具链失败，不映射为 TC `SyntaxError`。
- 生成程序失败：通过 `TcDiagnostic` 与 `tc_aot_abort` 报告运行时 TC 错误。

---

## 3. 输入契约

### 3.1 必须已满足的条件

codegen 入口只接受 Analyzer 成功产出的 typed program：

- `var` 均有 RHS；
- 名称、源序和块作用域均已解析；
- 每个运行时变量有固定 slot；
- RHS 类型、运算模式和格式符已检查；
- while/goto 范式隔离已检查；
- break/continue 已绑定到最内层 while；
- goto 目标与块路径已解析；
- 完整 CFG 确定初始化已通过；
- `let` 已求值为声明类型的 `TcValue`。

AOT 可以使用断言捕获内部契约破坏，但不得把断言或 host 编译错误暴露成合法用户程序的常规路径。

### 3.2 需要持久化的信息

codegen 至少需要：

- 语句树与稳定 `stmt_index`；
- 变量 binding → slot 映射；
- 每个语句的词法 scope/block path；
- goto → label 的已解析目标；
- break/continue → loop id；
- RHS 的解析类型与模式；
- `let` 的编译期位模式；
- 源文件名和行号。

若这些信息不直接保存在 `TcTypedProgram`，必须能由共享只读元数据确定性重建；不得在 AOT 中使用与 Analyzer 不同的名称解析算法。

### 3.3 不持久化 CFG 的条件

AOT 不必直接遍历 CFG 才能生成结构化 C，但只有在 typed statement 已携带所有解析后的控制目标时才可省略 CFG。完整 CFG 仍由 Analyzer 负责证明合法性，不能因 codegen 使用树形遍历而被跳过。

---

## 4. 生成 C99 的布局

### 4.1 目标骨架

```c
#include <stdint.h>
#include <string.h>
#include "tc_aot_rt.h"

static uint64_t slots[SLOT_COUNT];

int main(void) {
    TcDiagnostic diag;
    tc_aot_diag_init(&diag);
    tc_aot_init_slots(slots, SLOT_COUNT);

    /* generated statements */

    tc_diagnostic_clear(&diag);
    return 0;
}
```

若 `SLOT_COUNT == 0`，不得生成零长度数组；可省略数组或使用标准允许的最小占位并保证不访问。

### 4.2 固定槽位

所有词法 `var` 的 slot 在程序开始前固定。TC `var` 语句生成的是一次 RHS 求值和槽写入，不是新的 C 局部声明：

```text
var x: int32 = add(int32, a, b)
```

目标形态：

```c
if (tc_aot_arith(..., &slots[X], slots[A], slots[B], &diag, line) != 0) {
    tc_aot_abort(&diag, line);
}
```

循环下一迭代或向后 goto 再次到达同一 `var` 时，覆盖同一 slot。这个模型与 VM 的固定词法槽一致。

### 4.3 名称与确定性

生成的内部名称使用稳定 id，不使用用户标识符直接拼接：

- label：`tc_label_<stmt_index>`；
- loop：`tc_loop_<stmt_index>`（仅在显式标签策略中需要）；
- 条件临时值：`tc_cond_<stmt_index>`；
- 内部临时变量：以稳定语句 id 后缀隔离。

相同 typed program 必须生成语义等价且顺序稳定的 C 文本，便于差分和调试。

---

## 5. 语句代码生成

### 5.1 映射表

| TC 语句 | 目标 C99 |
| ------- | -------- |
| `var` | RHS → 固定 slot |
| 赋值 | RHS → 已有 slot |
| `let` | 不生成运行时语句；使用编译期位模式 |
| `write`/`writeln` | `tc_aot_write` |
| `read` | `tc_aot_read` + abort guard |
| `if` | 条件 RHS + 原生 C `if/else` |
| `while` | 原生无限循环 + 每次迭代显式条件 |
| `break` | 原生 C `break` |
| `continue` | 原生 C `continue` |
| `label` | `tc_label_<stmt_index>: ;` |
| `goto` | `goto tc_label_<target_stmt_index>;` |

### 5.2 运行时错误 guard

所有可能失败的 shim 统一生成：

```c
if (tc_aot_<op>(..., &diag, source_line) != 0) {
    tc_aot_abort(&diag, source_line);
}
```

`tc_aot_abort` 打印与 VM 同类的诊断并终止生成程序。codegen 不忽略返回码，也不把失败结果继续写入 slot。

### 5.3 条件 RHS

`if` 和 `while` 条件是普通 bool RHS，可能包含运行时操作。必须先完整求值到独立 `uint64_t` 临时值，再按 TC bool 语义分支；不得把可能失败的 shim 调用直接嵌入 C 条件表达式而改变错误顺序。

---

## 6. 控制流代码生成

### 6.1 `if`

目标形态：

```c
uint64_t tc_cond_12;
/* emit condition into tc_cond_12 */
if (tc_cond_12 != 0) {
    /* then */
} else {
    /* else */
}
```

then/else 保持语句源序。局部绑定仍使用顶部固定 slots，因此互斥分支的同名局部不会发生 C 名称冲突。

### 6.2 `while`

推荐生成原生 C 无限循环，每轮显式求条件：

```c
for (;;) {
    uint64_t tc_cond_20;
    /* emit TC condition; may abort */
    if (tc_cond_20 == 0) {
        break;
    }
    /* body; native break/continue target this loop */
}
```

这样保证：

- 条件在每次迭代开始重新求值；
- 正常 body 末尾回到条件；
- `continue` 回到条件；
- `break` 到循环后；
- 嵌套 C 循环自然匹配最内层 TC while。

### 6.3 为何可以使用原生 `break`/`continue`

Analyzer 已禁止 while 体内的 goto 和 label，且结构化语句树保留嵌套关系。因此最内层 TC while 与最内层生成 C 循环一一对应，原生 `break`/`continue` 不会被非结构化跳转破坏。

### 6.4 goto/label

goto 只在 while 外出现。Analyzer 提供解析后的目标；codegen 直接发射唯一 C 标签，不再次沿块路径搜索。允许同层和向外跳转，禁止的子块/兄弟块目标不可能进入 codegen。

对于向外 goto，C 的控制转移必须与 TC 局部生命周期一致。当前值均为标量 slots，没有 C 自动对象析构；未来资源型值必须在跳转前显式发射 scope-exit 清理，不能依赖 C goto 自动处理。

### 6.5 结构保持与显式标签策略

若 host 编译器或未来资源清理要求不适合原生 C `while`，允许改用唯一内部标签实现同一 CFG。两种策略必须通过相同差分用例；不得改变 continue 的条件重算点或 break 的作用域退出点。

---

## 7. RHS 与运行时 shim

### 7.1 分发原则

每个 `TcRhsKind` 必须在 codegen 中有显式分支。新增 `TC_RHS_BITCAST` 后必须同步：

- Parser/Analyzer 目标 kind；
- `tc_aot_emit_rhs`；
- runtime helper（若需要）；
- VM Executor；
- const evaluator；
- 覆盖检查和单元/差分测试。

未知 kind 是内部错误，不能静默生成 0。

### 7.2 shim 职责

| shim 类别 | 目标委托 |
| --------- | -------- |
| integer arithmetic/unary | `tc_sem_int` |
| compare/logic | 共享 semantics |
| bitwise/shift | `tc_sem_bitwise` |
| float arithmetic/unary/compare | `tc_sem_fp` |
| strict cast/truncate | 共享 cast 语义 |
| bitcast | 位宽验证已静态完成；运行时只复制规范化位模式 |
| I/O | `tc_io` |

shim 把 `uint64_t` slot 位模式封装成 `TcValue`，调用共享函数，再把成功结果写回位模式。

### 7.3 求值顺序

TC 每条语句至多一个非嵌套调用。codegen 仍须固定：左 operand 读取 → 右 operand 读取 → shim 调用 → 成功写回。逻辑短路要按 TC 规则避免求值不可达右 operand，不能无条件调用普通二元 helper。

### 7.4 模式枚举

目标生成器只会发射标准允许的模式：

- 整数 strict 或允许位置的 wrap；
- 浮点 strict 或 ieee；
- cast strict；
- 整数窄化 truncate；
- bitcast 无模式参数。

浮点 wrap 和浮点 truncate 位重解释不得从成功 typed program 到达 codegen。

---

## 8. `bitcast` 与数值一致性

### 8.1 位模式策略

内部 slot 已以 `uint64_t` 保存位模式。若源和目标都是规范化的等位宽 slot 表示，bitcast 可以直接复制并按位宽掩码：

```c
slots[DST] = slots[SRC] & tc_width_mask(TARGET_TYPE);
```

若 helper 需要在 `float`/`double` 宿主对象与整数之间转换，必须使用 `memcpy`：

```c
uint32_t bits;
float value;
memcpy(&value, &bits, sizeof(value));
```

禁止通过不兼容指针解引用或依赖 union type-punning 扩展。

### 8.2 位宽

Analyzer 已保证源、目标等宽且都不是 bool。runtime/helper 仍可使用断言防御内部错误。目标覆盖：

- int32/uint32 ↔ float32；
- int64/uint64 ↔ float64；
- 等宽整数之间；
- float32 ↔ 32 位整数、float64 ↔ 64 位整数的往返。

必须保持 `-0.0`、Infinity、NaN payload 和符号位，不进行数值规范化。

### 8.3 严格 cast

严格 cast 是数学值转换，不是位复制。VM 与 AOT 对目标可表示性使用同一 helper。旧的独立 float-cast 错误在目标诊断中统一为 `CastOverflow`。

### 8.4 浮点

- 每个 float32 操作在该步舍入到 float32；float64 同理。
- strict 按标准优先级报告除零、无效、上溢、下溢。
- ieee 产生标准结果。
- 比较 NaN 行为固定，不添加 codegen 私有模式。
- 生成 C 不依赖宿主开启 fast-math；构建参数不得破坏 NaN、Infinity 或舍入契约。

---

## 9. `let` 常量

### 9.1 输入状态

Analyzer/const evaluator 已把合法 `let` 求为声明类型的精确 `TcValue`。AOT 不重新使用宿主 C 常量表达式计算，因为宿主折叠精度与异常规则可能不同。

### 9.2 发射

- `let` 定义本身不生成运行时 slot 或赋值。
- 引用处直接发射已求得的十六进制位模式。
- float32/float64 也发射位模式，不用十进制文本让 host 编译器重新舍入。

### 9.3 一致性

常量求值必须与 VM runtime 每步精度和模式一致。差分测试同时比较：

1. `let` 预计算结果；
2. 等价 runtime 运算结果；
3. AOT 输出位模式。

---

## 10. I/O 与诊断

### 10.1 I/O

`tc_aot_write`/`tc_aot_read` 委托共享 `tc_io`：

- 13 种格式符；
- 整数符号和进制；
- bool 文本；
- float32/float64 格式；
- 输入非法、范围、EOF；
- stdout/stderr 写入失败。

`read` 只覆盖已初始化 slot；这个静态前提由 Analyzer 保证。

### 10.2 运行时诊断

生成程序持有单个 `TcDiagnostic`。shim 失败设置 kind、行号和消息，`tc_aot_abort` 打印并以非零状态终止。打印名来自共享 `tc_error_kind_name()`，确保 VM/AOT 一致。

### 10.3 静态诊断

AOT `--check` 和普通转译都经 libtc 完成全部静态阶段。当前实现覆盖 41 个语言错误码和 `OutOfMemory`，错误名由共享 `tc_error_kind_name()` 全表测试固定。

### 10.4 非语言失败

输出文件无法创建、host C 编译器缺失、host 编译失败等属于 tc-aot 工具错误。它们使用清晰 stderr 和非零退出码，但不伪装成 TC 语言错误 kind。

---

## 11. CLI、构建与产物

### 11.1 当前 v0.0.31 CLI

```text
tc-aot [options] <file.tc>
  -o, --output FILE
  -c, --check
  -r, --run
  -h, --help
  -V, --version
```

0.0.31 未新增 CLI 选项；现有入口保持兼容，版本和帮助已同步当前语言能力。

### 11.2 C99 构建

`--run` 调用 host `cc -std=c99 -Wall -Wextra -Werror -pedantic` 并链接 AOT runtime 与共享 runtime 模块，满足：

- 不依赖 GNU C 扩展；
- fenv 能力有明确配置与回退；
- 不启用破坏浮点语义的优化选项；
- 临时/输出路径安全引用；
- 生成或编译失败不执行陈旧二进制。

### 11.3 产物

- 默认 `.tc` → `.c`；
- `-o` 指定 C 输出；
- `--check` 不发射 C；
- `--run` 编译并运行生成 C。

文档和测试不得把 host 工具链可用性当成语言符合性的必要条件；纯 codegen 与差分环境分别验证。

---

## 12. 差分验证

### 12.1 原则

AOT 的核心正确性证据是同一源文件经 VM 与 AOT 产生相同可观察结果。比较至少包括：

- stdout 字节；
- stderr 的 TC 错误种类和关键消息；
- 退出成功/失败；
- 对专门用例导出的数值位模式；
- `--check` 的接受/拒绝结果。

### 12.2 0.0.31 测试矩阵

| 类别 | 用例 |
| ---- | ---- |
| structured loop | 零次、一次、多次、嵌套 while |
| loop control | 最内层 break/continue、嵌套 if 中控制 |
| paradigm isolation | while 内 goto/label 全部静态拒绝 |
| non-structured loop | if + backward goto、向外跳转 |
| definite init | 条件、回边、continue、break、goto 会合 |
| fixed slots | 每迭代 var 重初始化、后向 goto 重入 |
| bitcast | 等宽往返、NaN payload、-0.0、最高位 |
| cast | 全严格可表示性、整数 truncate、旧形式拒绝 |
| float | strict/ieee、每步精度、异常优先级 |
| let | 编译期与 runtime 位模式一致 |
| diagnostics | 目标错误 kind、行号、打印名 |

### 12.3 当前基线

v0.0.31 有 257 项 AOT 差分并全部通过，覆盖新语法、静态接受集、运行时错误、I/O 与数值位模式。

### 12.4 提交门槛

- 新 statement/RHS kind 的 codegen 分发覆盖；
- `check_rhs_coverage.py` 通过；
- VM/AOT/let 数值一致性通过；
- 生成 C 以 C99 严格警告编译；
- 全量 VM 435、unit 1617、AOT 257 基线不回退；
- 后续新增用例后，总数继续以脚本实测为准。

---

## 13. 模块与接口

### 13.1 当前文件

| 文件 | 责任 |
| ---- | ---- |
| `src/aot/main.c` | CLI、文件输出、host 编译/运行、版本 |
| `src/aot/tc_aot_codegen.c/h` | typed program → C99 |
| `src/aot/tc_aot_rt.c/h` | 生成程序的 shim |
| `src/vm/runtime/tc_sem_*.c` | 共享整数、浮点、位运算语义 |
| `src/vm/runtime/tc_io.c` | 共享 I/O |

### 13.2 目标接口

```c
int tc_aot_emit_c(FILE *out,
                  const TcTypedProgram *program,
                  const char *source_name);
```

公共 codegen 入口可以保持不变。新增 loop context、resolved target 和 bitcast helper 属于内部接口。若引入新 `.c/.h` 模块，必须同名配对并更新 CMake。

### 13.3 分发完整性

每个 statement kind 和 RHS kind 都要在 VM、AOT、free、Analyzer、const-eval（适用时）出现明确处理或明确不可达断言。AOT 不允许通过 default 分支把新 kind 当作普通赋值。

---

## 14. 实现基线与迁移

### 14.1 v0.0.31 当前事实

- `TC_AOT_VERSION` 为 0.0.31。
- 支持 var/let/赋值、I/O、if/else、while/break/continue、受限 goto/label 与全部 0.0.31 RHS。
- 生成静态 `uint64_t slots[]`；if/while 使用原生 C，goto 使用 `tc_label_<stmt_index>`。
- runtime shim 委托共享 semantics、`tc_sem_cast` 和 I/O。
- bitcast 发射等宽位复制；旧浮点 wrap 与 truncate 位重解释不进入 codegen。

### 14.2 已完成的迁移顺序

1. 共享 types/Analyzer 完成目标 kind 与静态契约；
2. 增加 while/break/continue statement emission；
3. 增加 bitcast 安全位复制；
4. 收敛 strict cast、truncate 和浮点模式；
5. 移除 `let` 运行时兼容发射，统一内联位模式；
6. 扩展 runtime shim 与错误打印名；
7. 增加差分和 C99 可移植性测试；
8. 全部门禁通过后升版。

### 14.3 发布证据

VM/AOT 差分 257/257、完整 `--check` 接受集与 runtime 诊断矩阵均通过；生成 C 全部经 `-std=c99 -Wall -Wextra -Werror -pedantic` 零警告编译。ASan、UBSan 和 no-fenv 门禁同时覆盖共享 runtime。

| 目标 | 实现链接 | 测试链接 |
| ---- | -------- | -------- |
| 结构化 while 与最内层控制 | [tc_aot_codegen.c](../src/aot/tc_aot_codegen.c) | [while_nested.tc](../tests/valid/while_nested.tc)、[while_break_continue.tc](../tests/valid/while_break_continue.tc) |
| goto/resolved target | [tc_aot_codegen.c](../src/aot/tc_aot_codegen.c) | [goto_var_reinitialize.tc](../tests/valid/goto_var_reinitialize.tc) |
| 共享数值与 bitcast shim | [tc_aot_rt.c](../src/aot/tc_aot_rt.c)、[tc_sem_cast.c](../src/vm/runtime/tc_sem_cast.c) | [fp_bitcast_roundtrip.tc](../tests/valid/fp_bitcast_roundtrip.tc)、[let_runtime_equivalence.tc](../tests/valid/let_runtime_equivalence.tc) |
| 共享 I/O | [tc_io.c](../src/vm/runtime/tc_io.c) | [test_io.c](../tests/unit/runtime/test_io.c)、[fp_io.tc](../tests/valid/fp_io.tc) |
| 完整差分与 C99 编译 | [AOT runner](../scripts/aot/run_tests.sh) | 257/257 发布输出 |

---

*语言合法性与可观察语义以 [TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md) 为准。*
