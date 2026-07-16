# TC 0.0.31 设计与实现评审报告

> 评审日期：2026-07-16
>
> 实施分支：`codex/tc-spec-0.0.31`
>
> 实施基线：`a603a7478201cc4f7db2375967cf5bb789b176a6`
>
> 规范一致性修复提交：`e7c08c84890e0171ba9dcaefd9147db5115f9a1b`
>
> 证据平台：macOS 26.5.2（25F84，arm64），Apple clang 21.0.0，CMake 4.3.4
>
> 评审对象：
> - 规范：`docs/TC语言标准设计说明书_0.0.31.md`
> - 实现：libtc / TC-VM / TC-AOT 分支 `codex/tc-spec-0.0.31` 的修复提交
> - 辅助资料：`docs/设计实现合规审查报告.md`、`docs/TC编译器_0.0.31阶段性开发计划.md`、`@knowledge-graph`

---

## 1. 评审结论

TC 0.0.31 的整体设计已经达到较高成熟度，属于一次成功的“规范收敛”版本。它将变量初始化、完整 CFG 确定初始化、受限 `goto`、结构化 `while`、浮点语义、`cast`/`truncate`/`bitcast` 边界，以及诊断阶段顺序统一收敛为一套明确、可验证、可测试的规则。

当前实现已完成本次一致性修复。静态布尔条件现在覆盖 §4.3 的完整合法单层 bool RHS，逻辑短路可使用源序更早且词法可见的 `let bool` 裁剪 RHS 读集，诊断阶段冲突也由专门矩阵锁定。求值使用 Pass2 的已解析绑定并委托共享 runtime semantics，不引入第二套名称解析或数值规则。

原评审记录的两项实现偏差与一项证明缺口均已关闭；M6 标准全量、结构、Sanitizer、no-fenv 与 macOS 内存门禁均已通过。最终结论恢复为“完全符合 TC 0.0.31”。

---

## 2. 评审范围与方法

本次评审同时覆盖“设计”和“实现”两个层面。

设计层面重点审查：

- 规范内部的一致性
- 规则是否可判定、可实现
- 错误模型是否闭环
- 控制流、类型系统、数值语义、I/O 与 `let` 常量求值是否互相对齐

实现层面重点审查：

- 代码知识图谱中的关键链路是否真实落地
- 规范关键点是否映射到具体模块
- VM / AOT / let / libtc 是否共享同一语义契约
- 测试矩阵是否足以支撑 0.0.31 的合规声明

采用的方法包括：

- 阅读 0.0.31 标准文档关键章节
- 使用 `@knowledge-graph` 对照实现分层与分发点
- 抽查 parser / analyzer / CFG / DFA / semantics / executor / AOT 关键代码
- 抽查典型 fixture 与 unit tests
- 运行全量测试与结构检查

---

## 3. 设计评审

### 3.1 设计整体评价

0.0.31 最重要的价值不在于“新增很多语法”，而在于把过去几个版本逐步引入的能力系统化地收口为一套单一、明确、互相兼容的规则体系。

主要优点如下：

1. `var` 强制初始化与完整 CFG 确定初始化形成双层保障。
2. `goto` 不再依赖专用近似错误码，而是统一纳入完整 CFG 分析。
3. `cast`、`truncate`、`bitcast` 的语义边界被彻底拆开。
4. 浮点严格模式 / `ieee` 模式与 NaN、异常、舍入规则被明确钉死。
5. `while` 与 `if + goto` 的范式隔离原则简化了静态分析模型。
6. 诊断阶段顺序被固定，避免实现对同一程序产生不同的首错结果。

整体上，这是一份偏“教学型但工程上可落地”的规范：刻意牺牲部分语法自由度，换取行为确定性、可测试性与实现一致性。

### 3.2 关键设计收敛点评

#### 3.2.1 初始化安全模型

0.0.31 将初始化问题拆为两层：

- 形态层：`var <name>: <type> = <rhs>` 是唯一合法形式
- 数据流层：变量是否可读、可赋值、可作为 `read` 目标，取决于完整 CFG 上是否“确定初始化”

这一拆分非常成功。它避免了把“声明长什么样”和“程序执行是否必然经过初始化点”混为一谈，也使 `goto`、`while`、`break`、`continue` 能被统一处理。

#### 3.2.2 控制流范式

规范通过“结构化 `while`”和“非结构化 `if + goto`”的二分，避免了在同一循环体内混用两种控制哲学。这样做的收益很高：

- 词法作用域更清晰
- CFG 构造更稳定
- `break` / `continue` 目标更明确
- `goto` 的合法性判定可保持简单

这是偏保守但正确的设计取舍。

#### 3.2.3 数值转换体系

`cast` / `truncate` / `bitcast` 三者的职责划分在 0.0.31 中已经足够清楚：

- `cast`：数值意义上的转换，必须满足目标可表示性
- `truncate`：仅限整数缩窄，保留低位
- `bitcast`：仅限等宽整数/浮点位模式重解释，`bool` 不参与

这套设计大幅降低了实现歧义，也让错误码分类自然稳定。

#### 3.2.4 诊断模型

规范 §11.0 对静态阶段顺序、首错策略、冲突优先级都做了规定。这对于一门强调一致性的教学/实验语言非常重要，因为它让“规范是否符合”不只是语言行为一致，也包括诊断输出的一致。

### 3.3 设计中的保守性与代价

0.0.31 的设计有意识地偏向“可验证优先”，代价包括：

- 不支持表达式嵌套
- `let` 常量表达式只允许单层调用
- 对短路、条件剪枝、模式关键字都采用非常保守的明文规则

这些不是缺陷，而是设计立场。如果未来目标从“教学与可验证性”进一步转向“可写性/语言舒适度”，这些约束可能需要重新评估。

---

## 4. 实现进度评审

### 4.1 版本与全量证据

当前工作区显示：

- `src/vm/driver/tc_version.h` 已为 `0.0.31`
- `src/aot/main.c` 已为 `0.0.31`
- `docs/设计实现合规审查报告.md` 给出 **48/48 合规**

本次评审过程中，已复核并运行：

- `make`：标准构建通过，C99 严格警告配置零警告
- `bash scripts/run_tests.sh`：VM 459/459、AOT 272/272、unit 1726/1726
- `python3 scripts/sync/check_rhs_coverage.py`、`check_source_naming.py`：通过
- `bash scripts/run_asan_all.sh`：全矩阵通过，无 ASan 报告
- `make build-ubsan`、`bash scripts/run_tests.sh --ubsan`：全矩阵通过，无 UBSan 报告
- `TC_FORCE_NO_FENV=ON` 的 `check-semantics`：494/494
- `make memcheck-macos`：MallocScribble 全矩阵通过；系统 `leaks --atExit` 下 VM 459/459，0 leaks；AOT 272/272、unit 1726/1726

全量测试结果为：

- VM：459/459 PASS
- AOT：272/272 PASS
- Unit：1726/1726 PASS（Analyzer 280/280、CFG 91/91）

### 4.2 按知识图谱对照落地情况

#### 4.2.1 RHS 分发点

知识图谱要求 `TcRhsKind` 在 8 个分发点闭环。检查结果显示：

- `TC_RHS_BITCAST` 已在 parser / analyzer / const_eval / executor / AOT / free 路径完整接入
- RHS 覆盖脚本通过

说明 0.0.31 在位重解释这一新增核心特性上的分发完整性已经达标。

#### 4.2.2 控制流 STMT 分发点

图谱中的 12 个 `TcStmtKind` 已完整映射到：

- parser
- parser_free
- pass1
- pass2
- cfg
- repl
- executor
- aot
- stmt_index

尤其 `while` / `break` / `continue` / `goto` / `label` 的控制元数据与运行链路已经成型，说明 0.0.31 的控制流核心不是停留在“语法支持”，而是完整贯穿到了执行与 AOT。

#### 4.2.3 完整 CFG 与确定初始化

知识图谱中的关键要求：

- 所有可达控制流进入 CFG
- 用交集固定点求确定初始化
- `goto` 不再用专用跳过初始化错误码

代码实现中，`tc_cfg.c` 与 `tc_analyzer_dfa.c` 已明确承担这一职责；`TC_ERR_UNINITIALIZED_VARIABLE` 也已回归为统一错误出口。这个方向与 0.0.31 规范完全一致。

#### 4.2.4 数值语义与共享层

`bitcast`、`cast`、`truncate` 的共享语义已经下沉到 runtime semantics 层，并被：

- analyzer
- let 常量求值
- executor
- aot shim

共同复用。这是当前实现质量较高的重要原因之一。

---

## 5. 实现质量评审

### 5.1 主要优点

#### 5.1.1 架构分层清晰

当前代码总体遵循了知识图谱定义的分层：

- Parser 负责词法/语法/形态
- Pass1/Pass2 负责名称、绑定、类型、模式和控制元数据
- CFG / DFA 负责可达性与确定初始化
- Executor / AOT 只消费 typed program，不重复静态合法性判断

这是健康的编译器结构。

#### 5.1.2 共享语义减少漂移

数值语义、bitcast、I/O 都尽量通过共享模块复用，而不是 VM / let / AOT 三处各自实现一遍。对于 0.0.31 这种强调“行为一致”的规范，这是正确路线。

#### 5.1.3 测试体系成熟

当前测试不仅覆盖 fixture，也覆盖：

- lexer / parser / analyzer / cfg / stmt_index / semantics 等 unit
- VM 行为
- AOT 差分
- no-fenv 分支
- ASan / UBSan / leaks / MallocScribble

这已经是一个相当严肃的发布级验证矩阵。

### 5.2 发现的实现缺口

以下问题不构成“主功能未完成”，但它们是本次评审中最值得记录的真实缺口。

#### 5.2.1 常量条件剪枝实现范围偏窄

规范允许对“仅由字面量与更早 `let` 组成的合法单层布尔条件”做条件剪枝。

已关闭。`tc_try_eval_static_bool()` 现覆盖 literal、const-ref、整数/浮点 compare、logic bin/un 和合法严格 bool cast，使用 `TcResolvedBinding.const_bits` 读取源序与词法作用域已经确认的常量，并复用 `tc_exec_*` 语义。`uninit_const_condition_if.tc`、`uninit_const_condition_while.tc` 与 Analyzer/CFG 三态测试证明 true/false/unknown 边一致。

#### 5.2.2 短路对 `let bool` 左操作数的裁剪未完全落地

规范要求：逻辑短路不仅识别布尔字面量，也应识别源序中更早的 `let bool` 左操作数。

已关闭。Pass2 与 CFG 统一调用静态 bool 操作数帮助函数；`let FALSE` 的 AND 与 `let TRUE` 的 OR 不记录 RHS 变量槽，`var` 左值保持 unknown。名称和类型检查仍在 CFG/DFA 前完成，分别由 `shortcircuit_let_invalid_rhs.tc` 与 `shortcircuit_let_rhs_type.tc` 锁定。

#### 5.2.3 诊断优先级的严格性仍需边界测试证明

已关闭。新增 `diag_priority_*` fixture 和 Analyzer 表驱动矩阵，覆盖词法/语法、名称、类型/模式、常量求值、CFG 与 DFA。测试曾发现 `and(bool, 1, missing)` 先报类型错误的阶段越界；Pass2 现先完成单层 RHS 名称预解析，因此稳定报告 `TC_ERR_UNDEFINED_VARIABLE`。

### 5.3 文档与索引滞后

已同步 `AGENTS.md`、`features.md`、`test-map.md`、knowledge graph 与无版本标准入口。无版本标准文件已明确标记为 0.0.26 历史草案，0.0.31 版本化文件为唯一权威来源。

---

## 6. 设计—实现一致性综合判断

综合本次评审，可将 0.0.31 的一致性状态总结如下：

### 6.1 已高度一致的领域

- `var` 强制初始化
- `goto` / `label` 的块路径与合法性判定
- `while` / `break` / `continue` 控制流
- `bitcast` 的 parser → analyzer → const_eval → runtime → AOT 全链路
- 浮点 `wrap` 删除与模式错误映射
- `TC_ERR_UNINITIALIZED_VARIABLE` 回归统一初始化错误
- 41 个语言错误 + 1 个实现扩展错误的枚举与命名

### 6.2 本轮已闭环的领域

- 常量条件剪枝的完整合法单层 bool RHS 覆盖
- `let bool` 参与短路分析的可达性与读集裁剪
- 诊断冲突优先级的边界用例与阶段越界修复
- 工程文档、架构索引与测试映射同步

---

## 7. 风险判断

### 7.1 已关闭风险

原评审中的保守条件剪枝、`let bool` 短路读集、文档滞后和诊断边界证明风险均已关闭。M6 全量安全与平台门禁已经复跑并全部通过，当前无遗留发布阻断风险。

### 7.2 当前未发现的高风险问题

本次评审未发现以下类型的严重问题：

- 规范核心规则在 VM 与 AOT 间行为分裂
- `bitcast` 仅停留在 parser/analyzer、未贯穿执行或 AOT
- 旧错误码仍残留在主路径
- `goto` 初始化模型仍然依赖旧的 source-order 近似
- `let` 常量求值与运行时数值语义完全脱节

---

## 8. 建议项完成情况

### P0：文档口径澄清

已完成：规范与报告明确完整静态 bool 范围、三态、源序/作用域和错误映射。

### P1：补充三类测试

已完成：三类 fixture 均已新增并注册 VM 执行/`--check`、AOT 差分/`--check`，同时补充 Analyzer/CFG 白盒。

### P2：补齐两处实现

已完成：`tc_const_eval` 提供统一三态和 bool 操作数入口，CFG/Pass2 复用。

### P3：同步协作文档

已完成：协作入口、测试映射、特性索引和知识图谱均已同步。

---

## 9. 最终结论

TC 0.0.31 是一次质量很高的规范收敛版本。它在语言设计上完成了从“逐步新增特性”到“统一语义模型”的转变，尤其在初始化安全、控制流、数值语义与错误诊断上表现突出。

当前实现已关闭本报告记录的全部规范偏差和证据缺口。代码、fixture、单元测试与工程文档已经闭环；M6 全量安全与平台门禁已全部通过。

**综合判断**：

- 设计：成熟、闭环、可实现
- 实现：规范偏差已关闭，VM/AOT 接受集与完整回归稳定
- 发布建议：恢复“完全符合 TC 0.0.31”声明，可进入发布流程

---

*— 评审结束 —*
