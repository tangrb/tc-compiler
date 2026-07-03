# TC-Compiler 工程改进建议

> **版本**：0.0.1（草案）  
> **日期**：2026-07-03  
> **范围**：TC-VM 实现、测试与后续 TC-AOT 扩展  
> **依据**：[TC-Compiler](../README.md) 设计与实现综合评审（v0.0.14）  
> **关联文档**：[TC语言标准设计说明书.md](./TC语言标准设计说明书.md)、[TC-VM详细设计说明书.md](./TC-VM详细设计说明书.md)

---

## 目录

1. [背景与现状](#1-背景与现状)
2. [优先级总览](#2-优先级总览)
3. [P0：立即修复](#3-p0立即修复)
4. [P1：短期完善](#4-p1短期完善)
5. [P2：中期增强](#5-p2中期增强)
6. [P3：长期规划](#6-p3长期规划)
7. [验收标准](#7-验收标准)
8. [附录：已知缺陷详情](#8-附录已知缺陷详情)

---

## 1. 背景与现状

TC-Compiler 当前已实现 **TC-VM**（直接执行引擎），语言标准与 VM 设计文档（v0.0.14）完整，模块划分清晰。综合评审结论：

| 维度       | 评分   | 说明                                       |
| ---------- | ------ | ------------------------------------------ |
| 语言设计   | 8.5/10 | 语义严谨，适合定宽整数教学                 |
| VM 架构    | 9.0/10 | 「源码即字节码」决策合理，模块边界清晰     |
| 代码质量   | 7.5/10 | 规范良好，但存在影响正确性的实现缺陷       |
| 文档       | 9.0/10 | 三份设计文档 + 知识图谱，完整度高          |
| 测试       | 7.0/10 | 覆盖面广，当前未全绿，缺少模块级单元测试   |
| **综合**   | **8.0/10** | 设计优秀，工程接近完成，需闭合验证闭环 |

当前主要差距：**实现细节与验证闭环尚未完全建立**。本地运行 `scripts/vm/run_tests.sh` 约有 20+ 项失败，根因之一是 Lexer 对 `TcToken` union 的误用（见 §8）。

---

## 2. 优先级总览

| 优先级 | 编号 | 建议项                               | 预期工作量 |
| ------ | ---- | ------------------------------------ | ---------- |
| P0     | 3.1  | 修复 Lexer union 覆写 bug            | 小         |
| P1     | 4.1  | 补充 Lexer 单元测试                  | 中         |
| P1     | 4.2  | 完善 `.gitignore`                    | 小         |
| P1     | 4.3  | 测试失败时输出更可读的诊断信息       | 小         |
| P2     | 5.1  | 为 `semantics.c` 增加表驱动单元测试  | 中         |
| P2     | 5.2  | 引入 C 单元测试框架                  | 中         |
| P2     | 5.3  | 补充 Parser / Analyzer 白盒测试      | 中         |
| P3     | 6.1  | TC-AOT 实现路线                      | 大         |
| P3     | 6.2  | 性能基准与 stress 测试量化           | 中         |
| P3     | 6.3  | Embedding API（libtc）               | 大         |

---

## 3. P0：立即修复

### 3.1 修复 Lexer union 覆写 bug

**问题**

`src/vm/lexer/lexer.c` 在识别关键字 / 类型 token 后，错误地清零 `token.u.literal` 字段：

```c
if (tc_keyword_token(start, len, &token)) {
    /* ... */
    token.u.literal.magnitude = 0;
    token.u.literal.negative = 0;
    token.u.literal.unsigned_suffix = 0;  /* 覆写了 int_type / arith_op */
}
```

`TcToken.u` 为 union，`int_type`、`arith_op`、`literal` 共享内存。`tc_keyword_token` 写入 `int_type = TC_UINT8 (1)` 后，`literal.magnitude = 0` 将其覆写为 `TC_INT8 (0)`。

**影响**

- 所有 `uint8`～`uint64` 类型声明被解析为对应位宽的有符号类型
- `var a: uint8 = 128` 及以上字面量报「literal out of range」
- `var a: uint8 = 10u` 报「literal type does not match」
- 大量 valid / errors 一致性测试失败

**修复方案（二选一）**

1. **最小改动**：对 `TC_TOK_INT_TYPE`、`TC_TOK_ARITH_OP` 等 token，**不要**清零 `literal` 字段；仅对 `TC_TOK_INTEGER` 设置 literal 成员。
2. **结构性改动**：将 `TcToken.u` union 改为带 tag 的 struct，或拆分为独立字段，彻底消除别名风险。

**涉及文件**

- `src/vm/lexer/lexer.c`（`tc_tokenize_line` 中 identifier/keyword 分支）
- `src/vm/lexer/tc_lexer.h`（若采用 struct 方案）

**验证**

```sh
make test
# 或
scripts/vm/run_tests.sh
```

修复后以下用例应通过静态检查：

```text
var a: uint8 = 250
var b: uint8 = 10u
var c: uint16 = 65535
```

---

## 4. P1：短期完善

### 4.1 补充 Lexer 单元测试

**目的**

防止 union 类低级错误再次引入；token 序列是 Parser 的输入契约，应在模块边界验证。

**建议用例**

| 输入                         | 期望 token 序列要点                          |
| ---------------------------- | -------------------------------------------- |
| `var a: uint8 = 250`         | `TC_TOK_INT_TYPE` → `u.int_type == TC_UINT8` |
| `var a: uint8 = 10u`         | `TC_TOK_INTEGER` → `unsigned_suffix == 1`    |
| `add(int32, wrap, x, y)`     | `TC_TOK_ARITH_OP` → `u.arith_op == TC_ADD`   |
| `0xFF_00u`                   | 十六进制 + 分隔符 + u 后缀                   |
| `; comment`                  | 空 token 列表或跳过                          |

**实现方式**

- 可在 `tests/` 下新增 `unit/lexer/` 目录，用小型 C 测试程序链接 `lexer.c`
- 或扩展 `scripts/vm/run_tests.sh` 增加 token dump 模式（`tc-vm --dump-tokens`，需新增 CLI）

---

### 4.2 完善 `.gitignore`

**问题**

`build-debug/`、`build-asan/` 等本地构建产物出现在 git status 中。

**建议**

在 `.gitignore` 中增加：

```gitignore
build-debug/
build-asan/
```

保留 `build/` 作为默认构建目录（与 README / Makefile 一致）。

---

### 4.3 测试脚本诊断信息改进

**问题**

`scripts/vm/run_tests.sh` 失败时需手动 grep 定位；stdout mismatch 仅输出 hex dump，不便阅读。

**建议**

- 失败时汇总打印失败文件列表与计数
- stdout mismatch 同时输出文本 diff（`diff -u expected got`）
- 脚本末尾：`N passed, M failed` 摘要
- 可选：`--verbose` / `--filter PATTERN` 参数

---

## 5. P2：中期增强

### 5.1 为 `semantics.c` 增加表驱动单元测试

**目的**

整数语义是语言核心，`semantics.c`（~770 行）逻辑复杂，端到端测试难以覆盖所有类型 × 模式 × 边界组合。

**建议覆盖矩阵**

| 维度     | 取值                                              |
| -------- | ------------------------------------------------- |
| 类型     | int8, uint8, int16, uint16, int32, uint32, int64, uint64 |
| 算术模式 | strict, wrap                                      |
| 运算     | add, sub, mul, div, mod                           |
| cast 模式 | strict, truncate                                 |
| 边界     | MIN, MAX, MIN-1, MAX+1, 0, -1（有符号）           |

**特殊用例**

- `int64` 最小值字面量与运算
- `uint64` 乘法溢出（`tc_umul64` 路径）
- 有符号 cast 到无符号的负值拒绝

---

### 5.2 引入 C 单元测试框架

**候选**

- [Unity](https://github.com/ThrowTheSwitch/Unity) — 轻量，单头文件
- [CMocka](https://cmocka.org/) — 支持 mock

**集成方式**

- CMake `add_executable(test-semantics ...)` + `add_test()`
- 根 `Makefile` 增加 `make test-unit` 目标
- 本地可通过 `make test-unit` 与 `make test` 分别运行单元测试与一致性测试

---

### 5.3 补充 Parser / Analyzer 白盒测试

**Parser**

- 六类语句的 parse 成功 / 失败
- 可选分号、行内注释截断
- 非法 token 序列的 syntax error

**Analyzer**

- Pass1：重复定义、slot 分配
- Pass2：源序可见性、forward reference、self reference
- 未初始化变量 warning 触发 / 不触发条件
- `let` 常量编译期求值

---

## 6. P3：长期规划

### 6.1 TC-AOT 实现路线

**现状**

`src/aot/` 仅占位，`make aot` 会提示未实现。

**建议路线**

1. **复用前端**：Lexer + Parser + Analyzer 与 VM 共享（提取为 `src/common/` 或静态库）
2. **复用语义**：`runtime/semantics.c` 可用于编译期常量折叠（扩展 `let` 支持常量表达式）
3. **代码生成**：
   - 阶段一：TC → C99 转译（最快验证）
   - 阶段二：TC → LLVM IR（性能与优化）
4. **测试**：共用 `tests/`，AOT 产物与 VM 执行结果对比（differential testing）

---

### 6.2 性能基准与 stress 测试量化

**现状**

`tests/stress/massive_vars.tc` 存在但未量化。

**建议**

- 增加 `scripts/vm/bench.sh`，测量大文件 parse + analyze + execute 耗时
- 记录基线（如 1000 变量定义 + 1000 赋值），供本地回归对比
- 关注：符号表查找、slot 分配、REPL 增量分析路径

---

### 6.3 Embedding API（libtc）

**目的**

允许其他程序嵌入 TC 解析 / 分析 / 执行能力。

**建议接口**

```c
/* 示意，非最终实现 */
int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag);
int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag);
void tc_typed_program_free(TcTypedProgram *program);
```

**前置条件**

- 模块边界进一步清晰化（runtime 独立为静态库）
- 文档化内存所有权约定

---

## 7. 验收标准

### P0 完成标准

- [ ] `scripts/vm/run_tests.sh` 全部通过（退出码 0）
- [ ] `scripts/vm/run_tests.sh --asan` 全部通过，无 ASAN 报告
- [ ] `var a: uint8 = 250`、`var b: uint8 = 10u` 等用例静态检查通过

### P1 完成标准

- [ ] Lexer 至少有 10 个单元测试，覆盖类型 token 与字面量后缀
- [ ] `.gitignore` 不再产生 build-debug / build-asan 脏文件
- [ ] 测试脚本失败时输出可读摘要

### P2 完成标准

- [ ] semantics 单元测试覆盖 8 类型 × strict/wrap 核心边界
- [ ] CMake `test-unit` 目标可用
- [ ] Parser / Analyzer 各有 ≥5 个 focused 测试

---

## 8. 附录：已知缺陷详情

### 8.1 Lexer union 覆写（P0-3.1）

**根因文件**：`src/vm/lexer/lexer.c`，identifier 识别分支（约第 370–377 行）

**`TcToken` 结构**（`src/vm/lexer/tc_lexer.h`）：

```c
union {
    TcIntType int_type;
    TcArithOp arith_op;
    TcLiteral literal;
} u;
```

**覆写过程**：

1. `tc_keyword_token("uint8")` → `u.int_type = TC_UINT8`（枚举值 1）
2. `u.literal.magnitude = 0` → 内存前 8 字节归零 → `u.int_type = TC_INT8`（枚举值 0）

**受影响测试示例**（非完整列表）：

- `tests/valid/uint8_wrap.tc`
- `tests/valid/wrap_uint8_output.tc`
- `tests/valid/div_mod_signed.tc`
- `tests/valid/wrap_sub_mul.tc`
- `tests/errors/runtime/div_zero.tc`（级联失败）
- 其他依赖正确 uint 类型解析的用例

### 8.2 测试与实现一致性

修复 P0-3.1 后，应重新运行完整测试套件确认无回归。若仍有失败，按以下顺序排查：

1. 诊断消息文本是否与测试脚本 `grep` 模式一致
2. `--check` 模式与执行模式的 exit code 约定
3. REPL 会话状态在错误后是否正确回滚

---

## 文档修订记录

| 版本  | 日期       | 说明                         |
| ----- | ---------- | ---------------------------- |
| 0.0.2 | 2026-07-03 | 移除 CI 相关建议             |
| 0.0.1 | 2026-07-03 | 初稿，基于 v0.0.14 综合评审  |
