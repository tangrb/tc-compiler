# TC 0.0.46 开发测试计划

> **文档定位**：本文是 TC 0.0.46 编译器（VM 与原生 AOT）的**开发与测试阶段计划**，以
> [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md)（下称“标准”）与
> [TC 原生编译器设计说明书 0.0.46](TC原生编译器设计说明书-0.0.46.md)（下称“设计”）为唯一依据。
> 本文描述“要做什么、按什么顺序做、每个阶段怎样判定完成”，**不描述当前实现现状**。
>
> **约束**：不设时间节点、不设人力投入、不做工期估算；只按**阶段**与**门禁**组织。
> 阶段编号只为表达依赖顺序，不代表日历排期。
>
> **优先级**：标准 > 设计 > 本文。本文与标准冲突时以标准为准；本文不得增加、删除或放宽标准的
> 静态接受集、诊断码、首个诊断规则与可观察行为。

---

## 目录

- [1. 范围、依据与原则](#1-范围依据与原则)
- [2. 质量目标与验收维度](#2-质量目标与验收维度)
- [3. 测试体系](#3-测试体系)
- [4. 阶段划分总览](#4-阶段划分总览)
- [5. 各阶段开发与测试](#5-各阶段开发与测试)
- [6. 专项测试设计](#6-专项测试设计)
- [7. 门禁、出口准则与缺陷处理](#7-门禁出口准则与缺陷处理)
- [8. 可追溯性](#8-可追溯性)
- [9. 配置与环境矩阵](#9-配置与环境矩阵)
- [10. 交付物、评审与变更控制](#10-交付物评审与变更控制)
- [附录 A 80 个诊断码测试清单](#附录-a-80-个诊断码测试清单)
- [附录 B 指令覆盖清单](#附录-b-指令覆盖清单)
- [附录 C 用例元数据格式](#附录-c-用例元数据格式)
- [附录 D 阶段门禁检查表](#附录-d-阶段门禁检查表)

---

## 1. 范围、依据与原则

### 1.1 范围

本计划覆盖 0.0.46 编译器的全生命周期开发与验证：

| 范围 | 内容 |
| ---- | ---- |
| 前端 | 读取与 UTF-8 解码、LEX、SYN、模块发现、IMPORT、SEM（名称/类型/转换/布局/CFG/DI/返回/调用/安全/注解）、CT |
| IR | 扁平 TAC IR、类型表、操作数/字段路径、基本块/CFG、区与对象、const/静态初值、IR 打印 |
| 运行时 | 整数/浮点/转换/位运算语义核、I/O 汇/源、分配器、trap、平台档案、`@fast_math` 边界 |
| VM | 参考解释器、内存/对象/指针状态、静态 DI 的执行侧配合、trap 提交语义、`D` 深度 |
| AOT | AArch64 先行后端、lowering、ABI、帧/区布局、汇编发射、工具链驱动、ELF/flat |
| 32 位 | 32 位平台档案、AAPCS32/ILP32、`int64`/`float64` 辅助、未对齐访问、32 位后端 |
| 平台 | hosted（Linux 用户态）与 freestanding（裸机 + UART）双目标；设备策略、退出策略 |
| 工具 | `tc-vm`、`tc-aot` 两个 CLI；私有 `tc_core`；不提供 C 宿主公共库、嵌入 API/ABI |
| 验证 | 静态诊断、运行时语义、VM↔AOT 差分、32/64 分档案、freestanding 门禁、确定性与鲁棒性 |

不覆盖：历史实现兼容、C 宿主公共库、嵌入 API/ABI、AOT 嵌入模式、标准之外的诊断或警告、
未列入标准的语言扩展、性能指标承诺。

### 1.2 依据

| 依据 | 用途 |
| ---- | ---- |
| 标准 §0–§14 | 语法、语义、诊断码（65 `TC_CE_*` + 15 `TC_RE_*`）、首诊顺序、诊断位置、平台档案、验证要求 |
| 标准 §13.4 | 八项一致性验证要求：静态一致、模块顺序、CFG/DI、类型与转换、安全、浮点、I/O、平台 |
| 设计 §12 | 15 项验证策略：差分、golden、freestanding、checked build、32/64、ABI、对象、诊断、`@fast_math`、IR、const、类型表、退出/设备/输入、零 pass |
| 设计 §13 | P0–P5 分期路线；本计划将其细化为 S0–S10 阶段 |
| 设计 §15 | 已定设计决策：非 SSA、FP/复杂内存调运行时、无 regalloc、ELF+Clang+LLD+qemu、五区+对象表、必选 trap 与 checked build 分离、单一规则表等 |

**禁止**：以历史实现、旧测试或 git 历史代码反推 0.0.46 语义；以某个实现的实际输出作为规范期望值。
除差分对拍外，期望值必须由标准条款独立推导。

### 1.3 阶段化原则

1. **先契约、后实现**：P0 先冻结 IR、类型、档案、诊断与运行时接口，避免实现中途改契约。
2. **前端先行、VM 参考先行**：先让静态语义与 VM 闭合，再让 AOT 追平；AOT 的正确性基准是
   同一档案下的 VM 与标准推导的 golden，而不是 AOT 自身的历史输出。
3. **每阶段可判定**：每个阶段都有进入条件、开发交付、测试活动、出口准则；出口准则未满足
   不得进入下一阶段（允许在同一阶段内迭代）。
4. **失败关闭**：测试工具的漏判、跳过、静默通过都视为失败；未实现的用例必须显式标记为
   `not-implemented`，不得计入通过。
5. **双档案分离**：W=32 与 W=64 是两套档案。静态结果、布局、CT、golden、差分结果均分档案保存，
   不做跨档案比较。
6. **规范码封闭**：任何阶段都不得新增语言诊断码；实现资源错误只用 `TC_ERR_OUT_OF_MEMORY`
   或明确的实现级错误，且不得伪装成语言诊断。
7. **确定性优先于优化**：0.0.46 基线为零 pass；O0/O2 语义一致；`@fast` 承诺只记录不消费。
   零 pass 是 0.0.46 基线而非永久契约；启用优化后按标准 §10.4 as-if 与 §10.8 不变量验收。

### 1.4 术语

| 术语 | 含义 |
| ---- | ---- |
| 档案 / profile | 由 W、端序、区表、栈、`D`、I/O、分配器、trap、退出策略、ABI 等构成的平台实例 |
| 首诊 | 标准 §12.2 定义的首个规范诊断及其错误码与位置 |
| golden | 由标准条款独立推导的期望输出/期望诊断/期望位模式，不是实现输出快照 |
| 差分 | 同一源码、同一档案、同一 IR 下，VM 与 AOT 的可观察行为逐字节对拍 |
| 必选 trap | 标准 §6.7 定义的空指针/只读区/代码区/设备拒绝等；所有构建都必须执行 |
| checked build | 仅额外检测越界/悬垂/one-past/未初始化等 UB 的构建；不改变必选 trap 与合法程序集合 |
| 零 pass | O0/O2 初期不做 IR 变换，只保留 `@inline`/`@noinline` 等标记 |
| 地址归一化 | 差分中屏蔽/归一化环境定义的地址值，只比较规范确定部分 |

---

## 2. 质量目标与验收维度

### 2.1 正确性目标

| 编号 | 目标 | 验收方式 |
| ---- | ---- | ---- |
| Q1 | 静态接受集与标准完全一致：不增加、不放宽、不遗漏 | 正例/反例套件；每个 EBNF 产生式与每个静态规则均有对应用例 |
| Q2 | 80 码封闭且一一对应；不产生标准之外的码或警告 | 码表机读校验；每码至少一个触发用例；反例断言“恰好一个规范诊断” |
| Q3 | 首诊及其错误码、主位置、关联位置符合标准 §12.2/§12.5.1 | 多错误组合用例；位置断言；跨文件主位置规则（含关联位置约束）用例 |
| Q4 | 运行时可观察行为符合标准 §1.3：输出字节、终止、trap 码、终止/非终止 | VM golden；AOT 差分；trap/提交语义用例 |
| Q5 | 32/64 双档案各自确定；字长相关静态结果与运行时结果按 W 正确实例化 | 32/64 翻转矩阵；分档案 golden；不跨档案比较 |
| Q6 | VM 与 AOT 在同一档案下可观察行为一致 | 差分门禁；地址归一化；trap/退出状态对拍 |
| Q7 | freestanding 目标可独立构建运行：无 libc 依赖、无设备端 import 解析 | `-ffreestanding -nostdlib` 门禁；符号审计；qemu-system 运行 |
| Q8 | 安全模型正确：安全代码不可达 UB；unsafe 显式 opt-in；`@safe` 严格 | 安全上下文矩阵；注解矩阵；承诺违反不产生静态错误的用例 |

### 2.2 确定性目标

| 编号 | 目标 | 验收方式 |
| ---- | ---- | ---- |
| D1 | 同一源码、同一档案、同一模块搜索路径下首诊固定 | 重复运行；不同哈希种子；不同线程数；不同测试执行顺序 |
| D2 | `module_order` 与 IMPORT/SEM 报告顺序不随遍历结构变化 | 钻石依赖、同层并列、哈希扰动用例 |
| D3 | O0/O2 在零 pass 基线下静态结果一致 | 同源双级别编译对拍 |
| D4 | 汇编发射可重复：相同输入产生相同 `.s` | 重复发射逐字节比较；标签/注释归一化后仍一致 |
| D5 | 测试判定本身确定：无超时抖动、无 locale/时区/环境依赖 | 固定 `LC_ALL=C`、固定 stdin、固定工作目录、无网络 |

### 2.3 “不得作为通过依据”的项

- 仅“VM 能跑通”而没有标准推导的 golden；
- 仅“AOT 输出与 VM 当前输出一致”而 VM 本身未经 golden 验证；
- 诊断只比较错误码，不比较阶段、主位置、是否恰好一个；
- 用 `printf("%g")` 或宿主浮点库作为 0.0.46 浮点输出的期望值来源；
- 用跳过/忽略的用例凑覆盖率；
- 跨 32/64 比较静态结果、绝对地址或布局；
- 把 checked build 的 UB 检出算作必选 trap 的通过项；
- 把未实现（`not-implemented`）计入通过。

---

## 3. 测试体系

### 3.1 测试层级

| 层级 | 名称 | 对象 | 判定 |
| ---- | ---- | ---- | ---- |
| L0 | 契约与静态检查 | 数据结构、码表、枚举、档案字段、ABI 描述 | 编译期断言/表驱动校验 |
| L1 | 单元测试 | 词法器、解析器、类型表、布局、CFG、DI 固定点、CT、运行时核、lowering | 函数/模块级断言 |
| L2 | 组件测试 | 前端组件、IR 构造、VM 子模块、后端子模块 | 组件接口行为 |
| L3 | 一致性静态测试 | 完整 `.tc` 源 → 诊断 | 阶段、码、主位置、关联位置、恰好一个 |
| L4 | VM 运行时测试 | 完整 `.tc` 源 → VM 执行 | stdout 字节、trap 码、退出状态、终止 |
| L5 | 差分测试 | 同源同档案 VM vs AOT | 可观察行为逐字节一致（地址归一化） |
| L6 | 平台测试 | hosted / freestanding / 32 位 / 设备策略 | 链接、运行、符号、退出、设备 trap |
| L7 | 非功能测试 | 确定性、鲁棒性、模糊测试、资源失败、重复构建 | 无崩溃/无挂起/可重复/错误可区分 |

### 3.2 套件布局

在 `tests/` 下按“规范对象”而不是按实现模块组织，避免测试与实现耦合：

```text
tests/
  unit/                 L1：词法/解析/类型/布局/CFG/DI/CT/运行时/后端单元
  conformance/
    static/             L3：lex/ syn/ import/ sem/ ct 诊断用例
    runtime/            L4：arith/ float/ conv/ mem/ ptr/ call/ io/ struct/ const
    abi/                L6：aarch64-64、arm32、riscv32 的参数/返回/struct/HFA
    32-64-diff/         L3/L6：字长翻转矩阵（分档案期望；目录名沿用设计 §14，
                        与 L5 VM↔AOT 的 diff/ 区分）
  diff/                 L5：VM↔AOT 同档案差分
  platform/             L6：hosted、baremetal、device-policy、exit-policy
  tools/                L6：tc-vm/tc-aot CLI、include roots、产物、可重复性
  determinism/          L7：重复运行、哈希种子、线程数、顺序扰动
  fuzz/                 L7：词汇/语法/导入路径模糊测试与回归语料
  golden/               期望值库（按 profile 分子目录）
  traceability/         标准章节/码/指令/设计 §12 的覆盖矩阵
  fixtures/             公共源片段、include 根、stdin 字节、链接脚本、UART stub
```

`tests/golden/w32` 与 `tests/golden/w64` 必须分离；`tests/golden/common` 只允许存放与 W 无关的
期望（如纯词法错误码）。任何 golden 文件都必须记录其推导依据（标准章节/条款）。

### 3.3 用例标识

格式：`TC-<层级>-<领域>-<四位序号>[@w32|@w64][-release|-checked]`

| 层级 | 示例 |
| ---- | ---- |
| LEX | `TC-LEX-LIT-0007@w64` |
| SYN | `TC-SYN-EBNF-0031` |
| IMP | `TC-IMP-ORDER-0004` |
| SEM | `TC-SEM-CONV-0042@w32` |
| CT | `TC-CT-EVAL-0011` |
| RT | `TC-RT-FP-0107@w64` |
| AOT | `TC-AOT-ABI-0009` |
| DIFF | `TC-DIFF-A64-0003` |
| PLT | `TC-PLT-BARE-0006` |
| TOOL | `TC-TOOL-CLI-0002` |
| DET | `TC-DET-HASH-0005` |
| FZ | `TC-FZ-LEX-0008` |

同一语义在 32/64 下的不同期望必须是两个用例或同一用例的两个 profile 条目；不得用条件跳过
掩盖差异。

### 3.4 判定器类型

| 类型 | 输入 | 输出断言 |
| ---- | ---- | ---- |
| 编译判定 | 源码 + profile + include roots | 成功，或阶段/码/主位置/关联位置/恰好一个 |
| 运行判定 | 源码 + profile + stdin 字节 | stdout 字节、trap 码、退出状态、是否终止 |
| 差分判定 | 源码 + profile | VM 与 AOT 的 stdout/trap/退出逐字节一致；地址按规则归一化 |
| 单元判定 | 直接调用编译期内部接口 | 结构/值/位模式/顺序断言 |
| 平台判定 | 源码 + 目标/链接脚本/UART stub | 产物存在、符号集、运行输出、设备策略 trap、退出状态 |
| 工具判定 | CLI 参数 + 文件系统 | 退出码、stdout/stderr 契约、产物内容、可重复性 |
| 属性判定 | 生成器/模糊器 | 不崩溃、不挂起、首诊封闭、合法程序不被误拒 |

### 3.5 期望值来源与评审

1. **静态诊断**：期望来自标准 §12.3 触发列 + §12.5.1 位置规则 + §12.2 首诊顺序；元数据必须
   引用具体条款（如 `spec: §3.11 bitcast 矩阵`）。
2. **运行时值**：期望来自标准 §6.3/§6.6/§6.13/§11 的精确规则。浮点用位模式（binary32/64
   位串）表达，不用十进制近似；输出格式用独立的大整数/十进制渲染器按 §11.2 计算，不调用
   宿主 `printf`。
3. **布局**：期望由字段宽度 + `@padding` + W 位求和独立计算；不读取实现布局结果。
4. **差分**：差分期望不是 golden，而是“两边一致”；但参与的源码必须已有 VM golden 或明确的
   可观察行为断言，避免“两个错误互相印证”。
5. **评审**：新增/修改 golden 必须独立评审，并附推导说明；禁止用“运行实现后回填”的方式
   更新静态/运行时 golden。差分工具的输出只能作为差分证据，不能反向成为规范 golden。

### 3.6 覆盖度量

不使用单一百分比作为门禁；使用**结构性覆盖清单**：

- 80 码：每码至少 1 个触发用例，且每码至少 1 个“不误报”的正例或邻近正例；
- 每个标准章节：至少 1 个正例、1 个反例（§0/§14 等纯约定章节用规则检查代替）；
- 每个 opcode：至少 1 个正常运行用例、1 个静态错误用例；有 trap 的 opcode 至少 1 个 trap 用例；
- 每个 profile 敏感项：W=32 与 W=64 各至少 1 个用例，覆盖“合法/诊断”翻转；
- 每个首诊排序规则（§12.2 第 1–10 项）：至少 1 个组合用例；
- 每个设计 §12 验证项：至少 1 个套件映射与 1 个门禁用例；
- 每个注解：位置合法/非法、互斥、参数范围、承诺违反非静态错误；
- 每个 `TcDevicePolicy`、每个 `TcIoSrc` 状态（OK/EOF/ERROR/短读）、每个退出策略分支。

覆盖矩阵由 `tests/traceability/` 维护，并在每个阶段门禁时审计。缺失项必须显式列出，不能
用“未实现”静默填充。

---

## 4. 阶段划分总览

阶段与设计 §13 的 P0–P5 对应关系如下。阶段是依赖顺序，不是日历顺序。

| 阶段 | 名称 | 对应设计期 | 主要开发交付 | 主要测试交付 | 门禁 |
| ---- | ---- | ---- | ---- | ---- | ---- |
| S0 | 契约冻结与测试基建 | P0 | 类型/IR/档案/诊断/运行时接口/零 pass 契约；私有 `tc_core` 与两个 CLI 骨架；测试框架 | L0 契约检查、码表机读校验、harness 自测、覆盖矩阵骨架 | G0 |
| S1 | LEX/SYN 与扁平 IR 构造 | P1 | 词法器、解析器、span、字面量、注解、块结构、SYN 专用码 | L1/L3 词法/语法套件；非法字节/位置；IR 形态 | G1 |
| S2 | 模块发现与 IMPORT | P1 | import DFS、`module_order`、include roots、模块实例键、签名收集、可见性表 | L3 导入套件；顺序/循环/重复/同名不同路径 | G2 |
| S3 | 名称、类型、转换与布局 | P1 | 类型表 intern/冻结、等价、字面量上下文、bitcast/trunc/cast、布局/`sizeof`、字段/结构体规则 | L3 类型/转换/布局套件；32/64 翻转矩阵 | G3 |
| S4 | CFG、DI、返回、调用、安全与注解 | P1 | 基本块/CFG、可达性、DI 固定点、缺返回、调用/`make` 检查顺序、安全上下文、注解 | L3 控制流/DI/调用/安全套件；首诊顺序组合 | G4 |
| S5 | CT 与 IR 冻结 | P1 | const 求值、跨模块按需求值、缓存、静态初值、IR 最终化/冻结、IR 打印 | L3/L1 CT 套件；IR 不变量；const 码 | G5 |
| S6 | VM 参考实现（W=32/64） | P1 | 解释器、对象/区内存、指针状态、算术/浮点/转换、调用/`D`、I/O、trap、提交语义 | L4 全运行时套件；VM golden；32/64 分档案 | G6 |
| S7 | AArch64 子集后端与工具链、差分 | P2 | AArch64 子集 lowering、AAPCS64 子集、帧/区布局、汇编发射、`tc-aot`、clang/lld/qemu 驱动 | L5 子集差分；L2/L6 ABI/工具链；可重复构建 | G7 |
| S8 | 全量语义、运行时与全量差分 | P3 | 指针/区/struct/控制流/浮点/I/O 全量；checked build；全量后端 | L4 全量、L5 全量差分、L6 ABI/对象、设计 §12 全项 | G8 |
| S9 | freestanding/裸机与 32 位后端 | P4 | freestanding 运行时、链接脚本、PL011 UART、heap arena、设备/退出策略；32 位目标选型与后端 | L6 freestanding 门禁、32 位 ABI/语义、qemu-system | G9 |
| S10 | 多目标扩展与长期回归 | P5 | 新目标 backend（RISC-V/x86-64/Cortex-M 按需）；可选优化 pass 管线（若启用） | 各目标差分、长期回归语料、模糊回归、发布审计 | G10 |

阶段进入/退出采用“硬门禁”：前一阶段 G 门禁通过才进入下一阶段；同一阶段内允许反复迭代。
若某阶段的部分能力依赖后续平台（如 32 位辅助），可在该阶段以 `not-implemented` 显式挂起，
但必须在对应阶段门禁前关闭，不能带入发布。

---

## 5. 各阶段开发与测试

### S0 契约冻结与测试基建

**目标**：在写实现前冻结所有跨模块契约，使后续阶段的“正确性”有唯一判定接口。

**开发交付**

1. **类型契约**：`TcType`/`TcTypeTag`、标量单例、`ptr`/`fnptr`/`struct` 复合参数、intern 与
   冻结规则；`isize`/`usize` 独立等价类，宽度按 W。
2. **IR 契约**：`TcOperand`（VREG/FIELD/CONST/LABEL/FUNC/STRUCT/DISCARD）、`TcArg`（保留命名）、
   `TcInstr`（op/mode/type/dst/args/switch targets/span）、`TcBlock`/`TcUnit`（显式 CFG、
   `vreg_types`、`vreg_objects`）、`TcIR` 容器。
3. **对象/区契约**：五区、`TcObject`（region/offset/size/type/lifetime/init/writable/parent）、
   `TcAddress`、指针状态表、`addr`/`ptrdiff`/`memmove`/越界检查的共同事实源。
4. **档案契约**：`TcPlatformProfile`、`TcTargetDesc`、`TcABIDesc`、`TcDevicePolicy`、
   `TcExitPolicy`；`TcIo`/`TcAllocator`/`TcTrapHandler` 三组接口，加上模块加载（构建/链接
   边界，设备端不解析 import）构成设计 §7.2 的四缝。
5. **诊断契约**：80 码的机器可读表（码、打印名、阶段、触发摘要、首诊排序键、位置规则）；
   首诊比较器接口；缓冲/停止策略；`TC_ERR_OUT_OF_MEMORY` 的独立通道。
6. **零 pass 契约**：O0/O2 等价；`@fast` 承诺记录不消费；`@inline`/`@noinline` 只保存；
   不引入 SSA/regalloc/内联。
7. **构建契约**：私有 `tc_core`（不安装、不导出 ABI）+ `tc-vm`/`tc-aot` 两个入口；不得出现
   C 宿主公共库/嵌入 API。
8. **测试框架**：用例清单解析、profile 参数化、§3.4 定义的七种判定器（编译/运行/差分/单元/
   平台/工具/属性）、超时、输出捕获、诊断断言、地址归一化、`not-implemented` 显式状态、
   失败关闭。

**测试活动**

- 码表校验：总数 80，其中 `TC_CE_*` 65、`TC_RE_*` 15；码名与标准 §12.3 逐项一致；无额外码。
- 契约静态检查：枚举/字段/默认值/单位（字节、位、列号 1 基）与标准/设计一致。
- 档案字段检查：W∈{32,64}；五区；`D≥1`；`disable_icf==1`；栈对齐；地址 0 保留。
- harness 自测：故意写错期望必须失败；漏报、额外诊断、超时、崩溃、跳过均必须失败；
  `not-implemented` 不得计为通过。
- 覆盖矩阵骨架：标准章节、80 码、opcode、设计 §12 的条目全部建行，初始为未覆盖。
- 金标准评审流程演练：新增/修改 golden 的推导说明与独立评审记录。

**出口准则（G0）**

- 契约评审通过，所有跨模块结构有唯一属主与冻结标记；
- 80 码表机读校验通过，计数 65+15，无增删；
- harness 自测全部通过，失败关闭行为可演示；
- 覆盖矩阵可生成报告，未覆盖项可见；
- 无实现语义承诺（不把“尚未实现”写成“已支持”）。

---

### S1 LEX/SYN 与扁平 IR 构造

**目标**：把源文件确定地变成带完整行列 span 的扁平 IR；只报告 LEX/SYN 诊断。

**开发交付**

1. **读取与解码**：有效 UTF-8、拒绝 BOM、拒绝 U+0000、注释外仅 ASCII；非法字节保留
   “最长有效前缀字节数 + 首个非法字节 0 基偏移”。
2. **LEX**：`///` 行注释、续行 `\`、CR/LF/CRLF；最长匹配；`#program`/`#lib`；`ANNOTATION`
   单一 Token（所有注解；`@unsafe`/`@readonly` 为允许名）；关键字/助记符/后缀；整数/浮点；
   `inf`/`nan` 为关键字字面量；`-nan` 拒绝；`DISCARD` 与合法下划线标识符；Token 自身上限
   `TC_CE_LITERAL_LEX_RANGE`。
3. **SYN**：EBNF（§14）；模式声明与层序；`struct`/`func`/`unsafe`/`var`/`const`/`static var`/
   `label`；`op_form`、`lhs`、`value`、`type`、`ptr<T>`/`fnptr<…>`、命名实参/命名字段；
   嵌套 op/裸值赋值在 SYN 拒绝；块结构 `then…end`；`TC_CE_MISSING_END`。
4. **扁平 IR 构造**：parser 直接产 `TcInstr`；字段路径保留为 `TcFieldRef`；命名实参/字段保留
   `TcArg.name`；span 保留文件/行/列；不建 AST 再降级。
5. **SYN 专用码**：`TC_CE_MODULE_LAYER`、`TC_CE_MISSING_VISIBILITY`、
   `TC_CE_PROGRAM_MODE_MISUSE`、`TC_CE_VAR_MISSING_INIT`、`TC_CE_SYNTAX`（含未知
   opcode/后缀、未定义 `(base,suffix)` 组合）。

**测试活动**

- **字符集与解码**：BOM、U+0000、非法 UTF-8 各位置、注释外非 ASCII、`%`、`;`、`/`、`//`、
  未构成模式的 `#`、非续行 `\`；注释内 Unicode 合法且不规范化；CR/LF/CRLF 行列。
- **记号化**：`///` 优先于 `/`；`@unsafe`/`@readonly` vs `@name`；助记符最长匹配与
  `.wrap/.ieee/.volatile/.nsw/.nuw`；`-` 与数字/`inf` 的归属；`-nan` 拒绝；`+` 仅在指数；
  `_` vs `_x/x_/_1/__`；关键字不可作标识符。
- **字面量**：十进制前导零、`0_0`、下划线规则、进制只改书写、`u/U`、`f/F`、`.` 两侧数字、
  `e/E` 指数、整数词法上限、浮点舍入到 0/inf 的 LEX 范围、负零；确认 LEX 不报 SEM 码。
- **语法/结构**：模式恰好一个；`#lib` 层序；`#program` 误用；`#lib` 缺可见性；`var` 缺初始化；
  `const` 缺 `=`/初始化器 → `SYNTAX`；`static var` 缺初始化 → `VAR_MISSING_INIT`；
  `struct`/`func`/`unsafe` 缺 `end`（多个候选取源位置最早）；
  `func` 形参/返回/注解；字段 `readonly`/`var`/`@padding`；`label L:`；`unsafe then…end`；
  空体/仅注释体/续行块结构；EOF 缺换行。
- **IR 形态**：所有合法源形态可构造 IR：字段作为值（`copy T, s.f`、`br_if s.flag`、
  `call_indirect s.fp`）、字段作为 lvalue、`addr T, s.f`、命名实参/命名字段、`switch` 多目标、
  `sizeof` 类型参数；span 覆盖构造首 Token 到结束。
- **位置**：主位置 1 基行列、列按 Unicode 标量、行终止符不计入下一行列；非法字节位置；
  `-` 属于字面量时定位到 `-`；未知 opcode/后缀定位到助记符首 Token；`DISCARD` 定位到 `_`。
- **模糊/属性**：随机字节、随机截断、随机续行、随机注释嵌套不崩溃不挂起；所有拒绝都只报
  一个 LEX/SYN 码；合法 EBNF 样本不被误拒。

**出口准则（G1）**

- LEX 与 SYN 的全部专用码均有触发用例与不误报正例；
- 非法 UTF-8/BOM/U+0000/非 ASCII/非法 ASCII 全部只报 `TC_CE_SYNTAX`；
- 所有合法 EBNF 形态可构造 IR（含字段 lvalue/命名实参/span）；
- LEX 阶段不出现任何 SEM/CT 码；词法失败 Token 不进入语法/语义；
- 首诊在词法/语法阶段内固定，重复运行一致。

---

### S2 模块发现与 IMPORT

**目标**：确定性地发现模块、固定 `module_order`、完成导入校验与签名收集；IMPORT 码必须在
IMPORT 阶段报告，不得推迟到 SEM。

**开发交付**

1. **模块发现**：根必须是 `#program`；按 import 源序 DFS；每模块只做一次 LEX/SYN；成功后加入
   `module_order`；导入错误写入 IMPORT 缓冲，不阻止继续发现其它模块。
2. **解析与实例**：搜索顺序“导入文件目录 → include roots（按序）→ 首个匹配”；模块身份按
   解析后的文件路径；同名不同路径是不同实例；每个 `import` 绑定到该导入者解析到的实例。
3. **IMPORT 阶段**：目标存在性、目标必须 `#lib`、循环优先于重复、同一导入者重复引用同一目标、
   导入名占用模块命名空间、冲突检查、签名/结构体/成员收集（依赖 DAG 逆向拓扑，同层按
   `module_order`）。
4. **可见性表**：`public`/`private`；本模块 `Self.x` 与导入 `m.x`。S2 只收集可见性信息；
   `private` 类型出现在公开位置等 SEM 检查在 S3 进行，IMPORT 阶段不报告。
5. **模式误用边界**：目标是 `#program` 时仍 LEX/SYN，但不递归其 import；IMPORT 阶段报
   `TC_CE_IMPORT_NOT_LIB`。

**测试活动**

- 顺序：`root → a → c`、`root → b → c` 的 `module_order` 为 `root、a、c、b`；`c` 只解析一次；
  `b → c` 复用不报重复。
- 循环 vs 重复：自导入、两点环、三点环；同一导入者重复同一目标；不同导入者共享同一目标；
  不同导入者按各自搜索路径绑定不同实例且静态存储独立；循环优先于重复。
- 缺失/非库/冲突：目标不存在；目标 `#program`；导入名与本地成员/局部名冲突（检查与 import
  文本位置无关）；同名不同路径实例分离；include roots 顺序改变时的首个匹配（include roots 属
  环境定义，必须文档化）；根为 `#lib` 时按实现/API 错误处理，不产生语言诊断；源文件打开失败
  属实现/API 层，不报语言码。
- 顺序确定性：哈希种子/线程数/文件系统遍历顺序变化下 `module_order` 与 IMPORT 报告顺序不变。
- 阶段边界：LEX/SYN 错误立即停止；IMPORT 错误在 IMPORT 阶段统一报告且早于 SEM；IMPORT 不
  报告名称解析/类型/CFG 诊断。
- 静态存储：同一实例的 `static var` 只有一份；同名不同路径实例各自独立（与 S3/S5 联动）。

**出口准则（G2）**

- 5 个 `TC_CE_IMPORT_*` 码全部覆盖，且均在 IMPORT 阶段报告；
- 钻石依赖/循环/重复/同名不同路径用例全部通过；
- `module_order` 在扰动下稳定；
- IMPORT 阶段不产生 SEM 名称/类型/CFG 诊断；
- 模块实例键控（路径）在签名、静态存储、成员表上一致。

---

### S3 名称、类型、转换与布局

**目标**：完成名称解析、类型等价、字面量定型、转换矩阵、结构体布局与 `sizeof`；所有 W 相关
静态结果按档案实例化。

**开发交付**

1. **名称与命名空间**：值名/类型名/标签名分离；模块成员命名空间（struct/const/static var/func
   共用）；`Self`/`m`；裸名值位置规则；`FUNCTION_SCOPE_ACCESS`/`TYPE_MISMATCH` 区分；
   可见性与私有类型；重复名/重复成员/重复形参。
2. **类型表**：标量单例、复合 intern、`ptr<T>`、`fnptr` 逐项签名、struct 名义等价；Analyze
   入口创建，Pass1/Pass2 intern，完成后冻结；不缓存 W 相关布局。
3. **字面量**：普通上下文按期望类型；`cast`/`trunc`/`bitcast` 源类型按 §6.6.3；符号性
   （无后缀/`u`/负值）与范围；`inf`/`nan`/`nullptr`/bool；LEX 与 SEM 分工。
4. **转换**：`cast` 数值/bool；`trunc` 仅整数且目标更窄；`bitcast` 类别矩阵先于位宽；
   `ptr`/`fnptr`/`isize`/`usize` 宽度按 W；`nullptr`/`bool`/struct/指针↔浮点禁止。
5. **结构体与布局**：字段顺序/唯一/非空；`readonly`/`var`；`@padding(N)` 形式与 N；字段紧凑 +
   0x00 padding + 无隐式对齐/尾填充；`sizeof` 求和按 W 位 `usize` 溢出；自引用（值字段 →
   `TC_CE_STRUCT_SELF_VALUE`；`ptr<S>`/`ptr<ptr<S>>`/`fnptr` 签名允许）；前向引用其它 struct
   （即使 `ptr<其它>`，负例 → `TC_CE_UNDEFINED_NAME`）；`sizeof void`；字段访问/字段赋值路径规则；
   `addr const`。
6. **指令形状与类型**：opcode 表驱动；操作数数量/形状；模式矩阵；`.nsw/.nuw` 三分类；
   无结果指令 LHS 必须 `_`；类型参数完整解析。

**测试活动**

- **名称**：局部与类型同名、局部与模块成员同名、`Self.f`/`m.f`、裸函数名、裸 struct 名、
  `Self` 在 `#program`、重复声明、导入私有成员/私有类型、`sizeof` 类型参数解析的
  未解析/非类型分支；`public const` 初始化器引用 `private const` 允许（值内联），引用
  `private` 类型/成员按公开位置规则报错；`ptr<void>` 非法、`fnptr<void>` 零参零返回。
- **等价**：`isize`≠`int32`（W=32）/≠`int64`（W=64）；`usize` 同理；struct 名义；`ptr<T>`
  按 T；`fnptr` 参数名无关；无隐式加宽/窄化/符号转换。
- **字面量**：每个期望类型上下文（声明、`copy`、`make`、`call`、`return`、`write`、`br_if`、
  `switch`、`read`）；`cast`/`trunc`/`bitcast` 源类型；负值/`u` 符号性；边界值 ±1；进制写法；
  `inf`/`nan` 仅浮点上下文；`nullptr` 仅指针/`fnptr`。
- **转换矩阵**：6×6 类别矩阵逐格；允许项等宽/不等宽；禁止项先报 `TYPE_MISMATCH` 再报宽度；
  `bitcast ptr↔int` 仅 unsafe 且仅 W 位整数；`fnptr` 仅同签名；`trunc` 目标≥源；`cast` 不作用于
  指针；`ptr↔fnptr` 禁止；`nullptr` 不作 bitcast。
- **32/64 翻转**：标准 §13.2.2 表逐行：`bitcast isize↔int32/int64`、`bitcast usize↔uint32/uint64`、
  `trunc isize,int64`、`trunc int32,isize`、`trunc usize,int64`、`trunc int32,usize`、`sizeof`
  字长类型、`isize/usize` 字面量范围、`@padding`/布局求和、`@range`/`@align`。
- **布局**：多字段 + 多 padding；零 padding；padding 为 0；求和刚好溢出；W=32 下大结构体溢出；
  W=64 下不溢出；空结构体；自引用值字段 → `TC_CE_STRUCT_SELF_VALUE`、`ptr<S>`/`ptr<ptr<S>>`/
  `fnptr` 签名合法；前向引用其它 struct（即使 `ptr<其它>`，负例 → `TC_CE_UNDEFINED_NAME`）；
  重复字段；`@padding` 的进制/后缀/负号/非整数字面量。
- **字段与赋值**：字段存在性、中间层非 struct、`readonly` 路径首个字段、深度只读不延伸到
  `ptr` 所指、整块 `make`/`copy` 替换 readonly、字段赋值不改外层初始化状态、`addr const`；
  `make` 检查顺序重复→未知→缺失→数量/多余→顺序→类型；位置/命名字段混用。
- **模式分类**：`add.wrap` 无符号/浮点、`add.ieee` 整数、类型族正确但模式不适用 → `MODE`；
  `div.wrap`/`load.wrap`/`br.wrap`/未知 base/suffix 属未定义组合 → `SYNTAX`（S1 覆盖，S3 复核
  不误报）；`.nsw/.nuw` 在合法 base 非 `@fast` → `ANNOTATION`，其它 base → `SYNTAX`，类型/符号
  不符 → `MODE`。

**出口准则（G3）**

- 类型等价、转换矩阵、字面量上下文的每条规则均有正例/反例；
- 标准 §13.2.2 的 32/64 翻转行全部有对应用例，且分档案期望正确；
- 布局在 SEM 计算，`LAYOUT_OVERFLOW` 属 SEM，不进入 CT；
- 类型表 Analyze 后冻结，执行器/AOT 不重新解析；
- 所有 S3 相关码（名称/类型/转换/布局/字段/void）覆盖。

---

### S4 CFG、DI、返回、调用、安全与注解

**目标**：完成控制流图、可达性、确定初始化固定点、函数末尾、调用/`make` 验证顺序、安全上下文
与注解静态校验。

**开发交付**

1. **CFG**：顶层与每个函数独立、封闭；指令/声明节点；`br`/`br_if`/`switch` 全边；`return`/
   `trap` 终止；`call` 原子节点；函数末尾节点；不可达集合。
2. **DI**：`U[n]` 为运行时绑定全集；对非入口可达节点，`IN[n]`/`OUT[n]` 以 `U[n]` 初始化；
   入口边界；`∩` 合取；最大固定点；`var x = read T` 读取成功后才初始化、失败不提交；已有变量
   `x = read T` 仍要求已初始化；字段赋值不改状态；
   `br_if` 常量不裁剪；`switch` 全目标；不可达不参与。
3. **返回/末尾**：`MISSING_RETURN` 可达末尾；空函数体；`void` 自然结束；`#program` 自然结束；
   无出口循环/永久 `br`/`trap` 合法。
4. **调用**：签名收集后检查；直接/间接；位置/命名；重复→未知→缺失→数量/多余→顺序→类型；
   位置/命名混用；调用安全先于实参；LHS 与返回类型；`void` 只能 `_`；递归允许。
5. **标签/控制**：标签命名空间；重复/未找到；`br_if` bool；`switch` 整数/default 必须存在；
   `return` 仅在函数内；形态/类型。
6. **安全**：safe/unsafe 上下文；unsafe 指令/调用清单；`unsafe` 块；`@unsafe`/`@safe`/`@fast`；
   `@safe` 禁止任何 unsafe 块（含空块）且 `SAFE_VIOLATION` 优先。
7. **注解**：位置、互斥、参数、范围；`@nonnull`/`@no_alias`/`@align`/`@range` 仅 `@fast` 形参；
   `@pure`/`@readonly`/`@nounwind`/`@willreturn`/`@no_overflow` 仅 `@fast`；`@inline`/`@noinline`
   互斥；承诺违反不在静态阶段报错。

**测试活动**

- **DI**：顺序/分支/汇合/循环回边；声明在循环内重复执行；`var x = read T` 成功才初始化、
  失败不提交；已有变量 `x = read T` 先要求初始化；`br` 跳过 `var` 后读取；不可达代码仍做
  名称/类型但不参与 DI；`addr` 取未初始化存储合法、`load` 才是问题（UB 运行时）。
- **CFG/末尾**：`br_if` 恒真仍保留 false 边；`switch` 恒 0 仍保留全部目标；`call` 不展开；
  `trap` 无后继；空体非 void → `MISSING_RETURN`；`void` 空体合法；无出口循环合法；
  多个缺返回按 `module_order`/源序取最早。
- **调用顺序**：同一调用同时有重复/未知/缺失/数量/顺序/类型错误时按固定顺序报首个；位置与命名
  混用；`call_indirect` 命名实参 → `UNKNOWN_ARGUMENT`；`call` 安全先于实参（§8.8.2 例外）；
  `void` 调用写 LHS → `VOID`；有结果指令用 `_` 丢弃仍做类型检查。
- **标签/控制**：标签与值同名；前向/后向；跨函数不可见；重复标签；未找到；`br_if` 非 bool；
  `switch` 非整数；零标签 `OPERAND_COUNT`；default 缺失；`return` 在 `#program`；void/非 void
  形态；返回类型不符。
- **安全**：每个 unsafe 指令在 safe/`unsafe` 块/`@unsafe`/`@fast` 下的接受性；`@safe` 内空
  `unsafe` 块；`@safe` vs `UNSAFE_*` 优先级；调用 `@unsafe`/`@fast` 的 `UNSAFE_CALL`/
  `FAST_CALL`；`bitcast ptr↔int` 与 `call_indirect` 的 unsafe 性；`addr`/`ptrdiff`/`funcaddr` 非
  unsafe 的对照。
- **注解**：`@padding` 与普通注解；未知注解；位置非法；重复/冲突；`@fast_math` 必须与 `@fast`；
  `@range` 的 lo>hi、非同型、非整数、超出类型；`@align` 非 2 的幂、0、超出 W 位地址宽度
  可表示范围；字面量范围与 `LITERAL_RANGE` 的优先级；承诺内容违反（如 `@pure` 函数 `store`）
  不得静态报错。
- **首诊组合**：阶段优先、模块顺序、源序、同位置专用码、条款内顺序、布局归属、遍历无关、
  操作数内部优先、call/make 顺序；重点覆盖标准 §12.2 第 9/10 项。

**出口准则（G4）**

- DI 固定点通过路径语义对照（含分支/循环/不可达/回边）；
- `MISSING_RETURN`、调用参数顺序、标签、安全、注解的全部相关码覆盖；
- 首诊组合用例覆盖 §12.2 第 1–10 项；
- 安全代码不产生 UB 的静态保证：safe 代码中无 unsafe 指令/调用，`@safe` 无任何 unsafe 块；
- 承诺违反只作为 UB/运行时假设，不新增静态拒绝。

---

### S5 CT 与 IR 冻结

**目标**：完成 `const`/`static var` 的编译期求值、跨模块按需求值、失败归属与缓存、IR 最终化；
确保失败的 const 不进入代码生成。

**开发交付**

1. **CT 求值器**：正向 allowlist（字面量、此前 `const`、`Self.K`/`m.K`、const 字段链、纯运算、
   `cast`/`trunc`/`bitcast` 数值、`make`、`sizeof`）；按 W 求值；严格异常映射。
2. **按需求值**：`module_order`/模块内源序启动；引用先求值目标；从左到右；`make` 命名字段按
   书写顺序；同一常量只求值一次；成功/失败都缓存；失败归属目标声明；引用处不级联。
3. **静态初值**：`static var` 用 const 初始化；`TcIR.consts` 标量 `bits`/聚合 `TcInitImage`；
   `TcObject.init`；`const` 使用处内联不占对象。
4. **IR 最终化**：字段名/源名消失，操作数变为 vreg/field/const/label/func/struct；`TcSpan` 保留；
   `TcIR` 绑定 profile；类型表冻结；模块实例 id 贯穿 struct/func/const/object。
5. **CT 码**：`CONST_FORM`（SEM 形态）、`CONST_OVERFLOW`/`CONST_DIV_ZERO`/`CONST_SHIFT`/
   `CONST_FLOAT_INVALID`/`CONST_FLOAT_UNDERFLOW`/`CONST_CAST`（CT）。

**测试活动**

- **allowlist 正例**：四类字面量、const 引用、跨模块 `public const`、`Self.K`、const struct 字段
  逐层、纯运算全组、`cast`/`trunc`/数值 `bitcast`、`make`、`sizeof`。
- **禁止形态**：`var`/参数、`addr`、`load`、`call`、`funcaddr`、I/O、指针/`fnptr` 位重解释、
  未列 op、带 `.wrap/.ieee/.nsw/.nuw/.volatile`、`@padding` 非法形式。
- **求值失败**：strict 整数/浮点溢出；整数除零/浮点 strict 除零/`mod` 零；负移位；浮点无效/NaN
  输入；strict 下溢；`cast` 溢出；各错误的位置为目标声明。
- **顺序/缓存**：同模块前向引用/自引用 → `UNDEFINED_NAME`；钻石依赖只求值一次；首个失败归属
  目标；失败后停止后续；未触达的独立错误按启动顺序。
- **W 相关**：`sizeof`/布局求和溢出、`cast` 范围、`bitcast`/`trunc` 宽度、`isize/usize` 字面量
  范围在 32/64 下的不同结果；`static var` 初值分档案。
- **IR 不变量**：字段作为值/lvalue、`addr T, s.f`、命名实参保留、`switch` 目标、span、无源名；
  类型表冻结后不可改写；失败 const 不在 `TcIR.consts`；`TcIR.modules` 按 `module_order`。

**出口准则（G5）**

- 6 个 CT 码全部覆盖，位置正确；
- CT 在 SEM 全过后才启动；失败不产生级联诊断；
- 同一常量只求值一次，成功/失败缓存正确；
- IR 形态测试覆盖设计 §12.11；类型表冻结测试通过；
- 静态初值在 VM 预载与后续 AOT `.data` 上有一致性用例（S8 完成）。

---

### S6 VM 参考实现（W=32/64）

**目标**：实现可作为可执行参考的 VM；在同一档案下按标准产生可观察行为；为 AOT 提供差分基准。

**开发交付**

1. **解释器**：扁平 IR 取指/执行；每指令一个 op；PC/调用栈/帧；`label`/`br`/`br_if`/`switch`/
   `return`/`trap`；函数调用/返回。
2. **内存/对象**：五区；`TcObject`/`vreg_objects`；对象边界/子对象/生命期；`addr`/`load`/
   `store`/`memmove`/`ptradd`/`ptrsub`/`ptrdiff`；指针状态 null/live/one-past/dangling。
3. **语义核**：整数 strict/wrap；有符号溢出；`div`/`mod` 边界；移位；浮点 binary32/64、
   roundTiesToEven、异常优先级、`mod` 精确有理商；`cast`/`trunc`/`bitcast`；bool 规范化。
4. **调用/深度**：参数按值；帧创建/销毁；`D` 检查（`depth==D` → `TC_RE_STACK_OVERFLOW`）；
   尾调用不改变抽象深度；`call_indirect` 空指针/签名 UB；`funcaddr` 地址唯一。
5. **I/O**：`TcIoSink` 整条提交；`TcIoSrc` 短读/EOF/错误；`read`/`write`/`writeln` 格式；
   失败映射 `TC_RE_IO`；locale 无关。
6. **trap/提交**：先检查后提交；失败不提交目标/返回值/初始化状态；I/O 不回滚；trap 立即终止；
   `switch` 越界走 default；`trap` 报 `TC_RE_TRAP`。
7. **档案隔离**：每个 `TcIR` 绑定一个 profile；32/64 不共享布局/CT/对象表/地址；宿主
   `size_t`/`uintptr_t` 不泄漏到目标值。

**测试活动**

- **逐 opcode**：附录 B 每个 opcode 的正常/边界/trap/静态错误；模式矩阵；无结果指令用 `_`。
- **整数**：`INT_MIN`/`INT_MAX`；`neg/abs` 边界；`div(INT_MIN,-1)` trap；`mod(INT_MIN,-1)=0`；
  除零；负移位；`k>=n` 的 `shl`/`shl.wrap`/`shr`；无符号回绕；`int64/uint64` 在 W=32 的 64 位
  语义。
- **浮点**：±0、±inf、NaN（canonical quiet）、subnormal、最大有限值、舍入边界；NaN 比较；
  `mod` 的精确有理商与符号；`b=±inf`；strict 异常优先级 INVALID→DIV_ZERO→OVERFLOW→UNDERFLOW；
  `.ieee` 不 trap；`abs/neg` 仅符号位；`@fast_math` 边界（直接/间接调用不扩散）。
- **转换**：整数→整数范围；整数→浮点；浮点→整数截断/NaN/inf；`float64→float32` 溢出/下溢；
  同类型 cast 保留 NaN payload；异类型 NaN canonical；`bitcast` 位串与端序无关。
- **内存/指针**：指针状态四态允许/禁止逐格；对象边界/子对象；one-past 的 `ptradd n=0`/
  `ptrsub` 回对象；dangling 的 copy/比较/`ptrdiff` 定义 vs 解引用 UB；`memmove` 重叠/`n=0`/
  null/one-past/dangling；`n×sizeof T` 数学值；未对齐访问；volatile 顺序。
- **调用**：参数从左到右；任一 trap 终止；递归；`D` 边界（D-1 成功、D 失败）；`call_indirect`
  null → `TC_RE_NULL_FUNCTION_POINTER`（全构建必选 trap）；签名不符/伪造 UB（不检测）；
  `void` 结果；LHS 类型。
- **I/O**：`%g` 6 位有效数字、指数 2 位、`e+00`、`inf/-inf/nan`、`-0`；整数/uint64 输出
  无前导零；`isize/usize` 范围按 W；输入前导空白、完整 Token、无 `+`、无 `f` 后缀、`-0`、
  EOF/短读/错误；
  `read` 失败不改目标；`write` 失败该条 0 字节、此前不回滚。
- **VM 确定性/隔离**：重复运行字节一致；32/64 分档案；宿主地址不泄漏；同一 IR 不可跨档案执行。

**出口准则（G6）**

- 全部 opcode 正常/边界/trap 用例通过；
- 15 个 `TC_RE_*` 均有触发用例；必选 trap 在 release VM 中执行；
- VM golden 在 W=32/64 分别建立且不交叉比较；
- 提交/终止语义用例通过；
- VM 可作为 S7 差分参考：同一源码在同一档案下输出可重复。

---

### S7 AArch64 子集后端与工具链、差分

**目标**：打通 `TC → .s → .o → ELF` 并让 AArch64 子集在 qemu 上运行；建立 VM↔AOT 差分门禁。

**开发交付**

1. **AArch64 子集**：标量整数算术/位/比较、`br`/`br_if`/`switch`/`return`/`trap`、直接调用、
   `read`/`write`/`writeln`、`sizeof`；栈上局部 + 固定临时寄存器，无 regalloc。
2. **ABI 子集**：AAPCS64 整数/指针 x0–x7、x0 返回、sp 16 字节对齐、callee-saved 保存；
   bool/int8/int16 扩展到 32 位；帧/区布局按 profile。
3. **lowering 契约**：strict 溢出/负计数检查→trap；`div/mod` 除零/INT_MIN 检查；
   `switch` 标签映射；`call` 深度 `D` 检查。
4. **工具链**：`tc-aot` 发射 `.s`；clang 集成汇编；lld 链接；qemu-aarch64 用户态运行；
   `TcTargetDesc` 选择 target triple；每次构建只绑定一个 profile。
5. **差分框架**：同一 `TcIR`/profile；运行 VM 与 AOT；比较 stdout 字节、trap 码、退出状态；
   地址归一化；固定 stdin；超时；差异最小化报告。

**测试活动**

- **lowering 单元**：每个子集 opcode 的汇编形态/寄存器/标志；有符号/无符号比较；shift 边界；
  条件分支；`switch` 跳转表/比较链；trap 调用；栈帧布局。
- **ABI 子集**：整数/指针参数/返回；bool/int8/int16 扩展；栈对齐；callee-saved；递归；
  调用深度 `D` 边界。
- **工具链**：`.s` 可被 clang 汇编；链接成功；ELF 可被 qemu 执行；无未解析符号；hosted 档案
  允许其平台 I/O 实例的宿主依赖，freestanding 档案必须无 libc（S9 门禁）；相同输入 `.s`
  逐字节可重复。
- **差分**：子集运行时用例全部 VM↔AOT；trap 码/退出状态一致；stdout 字节一致；地址相关
  用例归一化；O0/O2 零 pass 一致；不做跨 32/64 比较。
- **失败诊断**：AOT 前端与 VM 前端对同一源码产生相同首诊；AOT 不因后端选择改变静态结果。
- **负向**：未支持的 opcode 必须显式报“未实现”或构建失败，不得静默生成错误代码。

**出口准则（G7）**

- 子集所有用例 VM↔AArch64 差分零不一致；
- `.s` 可重复；链接/运行可重复；qemu 结果稳定；
- 子集必选 trap 在 release 构建执行；
- 前端静态结果与 VM 完全一致；
- 零 pass：O0/O2 无 IR 变换，结果一致。

---

### S8 全量语义、运行时与全量差分

**目标**：补齐指针/区/struct/控制流/浮点/I/O/调用全量；完成全量 ABI、checked build 与全量差分；
关闭标准 §13.4 八项与设计 §12 全项。

**开发交付**

1. **全量运行时**：FP/cast/bitcast、复杂内存/指针、`make`/`copy`/`memmove`、struct 布局/复制/
   bool 规范化、volatile、设备策略、退出策略、hosted 分配器。
2. **全量后端**：struct 按 AAPCS64 复合/HFA、大 struct 间接返回、`fnptr`/`call_indirect`、
   `funcaddr`/`funceq`/`funcne`、`load/store` 多尺寸/未对齐、volatile 不合并/不重排、
   `int64/float64`、checked build 的 UB 检查。
3. **入口/产物**：复位入口、区基址映射、静态区 `.data`、代码区、宿主区；ELF 可执行；
   不同函数入口地址唯一（禁用 ICF）。
4. **一致性**：VM/AOT 同档案相同区内偏移；静态初值 VM 预载与 AOT `.data` 字节一致；
   `const` 内联；CT 结果按档案。
5. **checked build**：越界/悬垂/one-past/未初始化检测；仅覆盖 UB 访问；不改变必选 trap、
   合法程序集合与已定义语义。

**测试活动**

- **全量 opcode**：附录 B 全部；模式/后缀；无结果指令；`_` 丢弃；命名实参/字段。
- **全量 ABI**：AAPCS64 参数/返回/struct/HFA/大复合/间接返回；`fnptr`；`addr` 参数；bool 扩展；
  sp 对齐；callee-saved；`call_indirect` 前置条件不检查（违反 UB）。
- **对象/内存**：五区；宿主分配块含 n≥1 个 T 元素；`memmove` 重叠；`ptrdiff` 同对象/跨对象/
  悬垂；对象边界；未对齐 64 位；`n×sizeof T`；必选 trap vs checked build 分离；四类设备策略。
- **浮点/IO 全量**：const 与运行时同精度/舍入/异常；输出 golden；输入 short-read/EOF/error；
  `write` 整条提交；`read` 失败不改目标；locale 无关。
- **全量差分**：所有 runtime 用例 VM↔AOT；地址归一化；trap/退出；O0/O2；checked/release 分开
  统计；不做跨 32/64。
- **标准 §13.4 八项**：静态一致（VM/AOT 前端）；模块顺序（重复/哈希/线程）；CFG/DI；类型转换；
  安全；浮点；I/O；平台（同档案区内偏移）。
- **设计 §12 十五项**：逐项建立套件映射与门禁用例（见 §8.2）。
- **静态初值**：`TcIR.consts`/`TcObject.init` 在 VM 与 AOT `.data` 字节一致；聚合 const；
  `const` 内联不占对象；CT 失败不进入代码生成。
- **类型表/模块实例**：冻结后不可重解析；钻石依赖共享静态存储；同名不同路径分离。
- **退出/设备/输入**：正常/trap 退出状态；四策略 trap；短读/EOF/错误路径。
- **零 pass**：O0/O2 无 IR 变换；`@fast` 承诺不消费；`@inline`/`@noinline` 只保存；无内联/
  regalloc/SSA。

**出口准则（G8）**

- 全量 opcode 覆盖；全量 runtime golden 通过；
- 全量 VM↔AOT 差分零不一致；
- 标准 §13.4 八项全部有证据；设计 §12 十五项全部有套件映射；
- checked build 用例与必选 trap 分开统计，且不改变合法程序集合；
- 全量 ABI 用例通过；ICF 禁用验证通过；
- 无开放 B1 级规格偏差。

---

### S9 freestanding/裸机与 32 位后端

**目标**：让编译器在无 libc 的裸机环境可运行，并完成 32 位档案与后端；验证 32/64 分档案差异。

**开发交付**

1. **freestanding 运行时**：`-ffreestanding -nostdlib`；无 libc 依赖；四缝 freestanding 实例
   （PL011 UART I/O、arena/bump 分配器、halt/复位 trap、设备端不解析 import）。
2. **裸机产物**：链接脚本定义复位入口/区基址；ELF/flat；`aarch64-baremetal` profile；UART
   stdout 汇；设备策略。
3. **32 位目标选型**：首个 32 位目标在 S9 选定，设计未预先决定（设计 §14 的 `arm32/`、
   `riscv32/` 均标“目标待定”；设计 §8 另列 Cortex-M/RV32/ARM32-Linux 候选），选定后记录
   ABI（AAPCS32/ILP32）、target triple、qemu 运行方式。
4. **32 位后端**：独立于 AArch64 的 backend；r0–r3 参数、r0/r1 或 v0 返回、r4–r11 callee-saved、
   sp 8 字节对齐；`int64`/`float64` 双字/软浮点辅助；未对齐 64 位访问；32 位 `usize` 运算。
5. **32 位语义核**：`int64/uint64` 乘除/移位/比较；`float64` 软浮点；`n×sizeof T` 数学值；
   `ptrdiff`/`sizeof`/`isize`/`usize` 按 32 位；地址/指针 4 字节。
6. **设备/退出**：四种 `TcDevicePolicy`；`TC_DEV_VOLATILE` 仅 volatile；退出策略正常/trap；
   `disable_icf`。

**测试活动**

- **freestanding 构建**：交叉编译运行时；`-nostdlib` 链接；无未解析 libc 符号；无动态依赖；
  最小 UART stub 可输出；链接脚本区基址正确。
- **裸机运行**：qemu-system-aarch64 virt + PL011；正常输出/退出；trap 停机；设备区拒绝/
  只读/可读写/volatile-only；heap arena；初始化静态区；无 import 解析。
- **32 位 ABI**：AAPCS32/ILP32 参数/返回/struct/HFA；`int64`/`float64` 双字/软浮点；sp 对齐；
  callee-saved；bool/int8/int16 扩展；`addr` 参数；`funcaddr`/`call_indirect`。
- **32 位语义**：标准 §13.2.2 全表；`sizeof`/布局/`bitcast`/`trunc`/`cast`/字面量范围/`@range`/
  `@align`/`read`/`write`；`ptrdiff`/`memmove`/`n×sizeof T`；32 位地址空间边界。
- **32/64 分档案**：同一源码在 W=32/64 的静态结果/golden 分别保存；不比较跨档案；差分只在
  同档案内进行；32 位用 qemu-arm/qemu-riscv32 或 qemu-system。
- **平台门禁**：freestanding 编译门禁；无 libc；设备策略；退出策略；地址归一化；checked build
  在裸机可选，但必选 trap 必须执行。
- **受限环境**：qemu-user 在 macOS 不可用时用 Linux CI/容器；不得用 Mach-O 代替 ELF 门禁。

**出口准则（G9）**

- freestanding 构建/运行门禁通过；符号审计无 libc；
- 32 位目标选定并有 ABI/后端实现；32 位 ABI/语义套件通过；
- 32/64 分档案 golden 与差分隔离；
- 四种设备策略、退出策略、UART/heap/trap 全部覆盖；
- 32 位 `int64`/`float64` 辅助与未对齐访问用例通过。

---

### S10 多目标扩展与长期回归

**目标**：把“新增目标”变成可重复的工程流程，并维持长期回归与确定性。

**开发交付**

1. **新增目标清单**：`backend/<target>` + `TcTargetDesc`/`TcABIDesc` + 工具链驱动 + 链接脚本 +
   平台实例；目标无关层复用，不复制前端/IR/VM。
2. **可选优化管线（若启用）**：先声明 pass 管线与 `@fast` 消费策略，再引入 SSA/regalloc/内联；
   未声明前保持零 pass 基线。
3. **长期回归语料**：标准条款语料、边界语料、差分语料、模糊回归语料、平台语料。
4. **发布审计**：产物、符号、API/ABI 面、文档、变更记录。

**测试活动**

- **新目标准入**：同一套件在目标上重跑；ABI/调用/平台/差分；不得跳过规范确定用例。
- **回归**：全量静态/运行时/差分/平台套件；模糊回归；确定性；重复构建。
- **优化启用后**：新增 `@fast`/`@fast_math`/内联边界与 §10.8 不变量测试；优化前后静态诊断
  一致；不把安全代码变成 UB；不得改变可观察行为（除 §10.3/§10.5 允许范围）。
- **发布审计**：无额外诊断码；无 C 宿主公共库/嵌入 API；`tc_core` 私有；两个 CLI 是唯一入口；
  文档与实现一致；`TC_ERR_OUT_OF_MEMORY` 不作为规模限制替代。

**出口准则（G10）**

- 每个新目标通过同等级门禁；
- 长期回归全绿；无已知 B1；
- 优化（若启用）有完整不变量测试与回退策略；
- 发布审计通过，文档/变更记录同步。

---

## 6. 专项测试设计

### 6.1 诊断与首诊

**测试对象**：80 码表、阶段、首诊顺序、位置规则、fail-fast、无警告。

**用例族**

1. **单码触发**：每码至少一个最小源；断言阶段、码、主位置、恰好一个。
2. **阶段优先**：同一程序同时含 LEX/SYN/IMPORT/SEM/CT 错误，断言只报最早阶段。
3. **模块顺序**：根 + 多导入 + 钻石；IMPORT/SEM 首诊按 `module_order` 而非依赖拓扑。
4. **源序/列序**：同模块多错误取行号/列号最小；同位置多候选按专用码优先。
5. **同位置专用码**：`MISSING_END` vs `SYNTAX`；`LITERAL_TYPE/RANGE` vs `TYPE_MISMATCH`；
   `CONDITION_TYPE` 不抢先于条件操作数内部字面量码。
6. **条款内顺序**：浮点异常优先级；负移位先行；空指针分类先行；`call` 安全先于实参。
7. **布局归属**：`LAYOUT_OVERFLOW` 在 SEM，不进入 CT。
8. **遍历无关**：改变函数表/字段表/导入表/CFG 遍历顺序，首诊不变。
9. **档案字长**：同源在 W=32/64 可有不同候选/首诊；码不增删。
10. **操作数内部优先**：`br_if`/`switch`/`return`/`call`/`make` 位置的错误字面量先报
    `LITERAL_TYPE/RANGE`。
11. **call/make 顺序**：重复→未知→缺失→数量/多余→顺序→类型；位置/命名混用码区分。
12. **位置规则**：1 基行列、Unicode 标量列、非法 UTF-8 前缀/偏移、跨文件主位置规则（主位置在
    导入方/被导入模块；关联位置不改变主文件）、负号/`_`/未知 opcode/后缀的定位。
13. **无额外诊断**：每个反例断言“恰好一个”；不得有警告输出。

**判定**：编译判定；元数据必须含 `spec` 引用；位置断言精确到行列；跨文件断言主文件。

### 6.2 词法与语法

- 字符集/解码/注释/续行/Token 最长匹配/字面量/`DISCARD`/关键字/注解；
- EBNF 全部产生式：program/library、import、struct、const/static var、func/param、statement、
  instruction、var、unsafe、type、literal、opcode；
- 模式专用误用：`#program` 的 `func/static/public/private/Self`；`#lib` 顶层 `var`/指令；
- 块结构：`then…end`、缺 `end`、缩进不参与语法、续行不改变块；
- 嵌套 op/裸值赋值/`DISCARD` 位置；
- 位置与跨度：首个 Token 到构造结束。

### 6.3 模块与导入

- DFS/`module_order`；循环 vs 重复；目标 `#program`；include roots 顺序；同名不同路径；
  导入名冲突；私有成员/私有类型；签名收集依赖序 vs 诊断序；
- 静态存储共享：同一实例一份；不同实例分离；钻石依赖复用；
- 模块模式：根必须 `#program`；`#lib` 不自动执行；`public func init()` 由程序显式调用（运行时
  库/示例层面，不在核心）。

### 6.4 类型、布局与转换

- 类型身份/等价/无隐式转换；标量单例/复合 intern；`isize/usize` 独立类；
- 结构体：字段顺序/唯一/非空/readonly/var/padding/布局/`sizeof`/溢出；自引用：值字段 →
  `TC_CE_STRUCT_SELF_VALUE`，`ptr<S>`/`ptr<ptr<S>>`/`fnptr` 签名合法；前向引用其它 struct（即使
  `ptr<其它>`）→ `TC_CE_UNDEFINED_NAME`；
- 转换：`cast`/`trunc`/`bitcast` 全矩阵；类别先于宽度；W 相关宽度；`nullptr`/`bool`/struct/
  指针↔浮点/`fnptr` 禁止；
- 字面量：普通上下文/转换操作数/符号性/范围/进制/浮点舍入/负零/`inf`/`nan`；
- 32/64 翻转矩阵：标准 §13.2.2 逐行。

### 6.5 CFG、DI 与控制流

- 封闭 CFG；入口；普通/分支/多路/返回/trap；`call` 原子；函数末尾；
- 可达性：不可达不报错、仍做非 DI 检查；`br_if` 不裁剪；`switch` 全目标；
- DI：`U` 全集、入口边界、`∩`、最大固定点、`var` 例外、字段赋值、循环回边、不可达；
- 返回：`MISSING_RETURN` 可达末尾、空体、`void`/`#program` 自然结束、无出口循环；
- 标签：独立命名空间、重复/未找到、前向/后向、不跨函数；
- 属性测试：随机 CFG/DI 小图与“所有路径都已初始化”的路径语义对拍。

### 6.6 安全、注解与优化契约

- 安全上下文矩阵：safe/`unsafe` 块/`@unsafe`/`@fast`/`@safe`；
- unsafe 指令/调用穷尽清单；非 unsafe 的对照清单；
- `@safe` 禁止任何 unsafe 块（含空块），`SAFE_VIOLATION` 优先；
- 注解位置/互斥/参数/范围；`@fast_math` 与 `@fast`；`@inline`/`@noinline`；
- `.nsw`/`.nuw` 三分类；
- 承诺违反（`@pure`/`@readonly`/`@nounwind`/`@willreturn`/`@no_overflow`/`@nonnull`/
  `@no_alias`/`@align`/`@range`）不得成为静态错误；测试只验证“不额外静态拒绝、默认区域语义
  不受影响”；承诺被优化器消费属于后续优化阶段，需单独的不变量/假设测试；
- 优化契约：零 pass 下 O0/O2 一致；未来启用优化时，§10.8 不变量测试。

### 6.7 CT

- 正向 allowlist；禁止形态；求值顺序；跨模块按需求值；缓存；失败归属；
- 6 个 CT 码；`CONST_FORM` 的 SEM 形态；
- const struct 字段链；`make`/`sizeof`；W 相关溢出/范围；
- `static var` 初值；失败不进入代码生成。

### 6.8 运行时算术与浮点

- 整数 strict/wrap/无符号；溢出；`div/mod`；移位；`INT_MIN` 边界；
- 浮点 IEEE binary32/64；roundTiesToEven；NaN/Inf/subnormal；异常优先级；`mod` 精确有理商；
  `@fast_math` 边界；
- 转换：cast/trunc/bitcast；bool 规范化；
- 扩展精度/FMA/FTZ/DAZ 禁用检查：使用位模式 golden，不用十进制近似。

### 6.9 内存、指针与对象

- 五区/对象/子对象/生命期；`addr`/`ptrdiff`/`memmove`/`ptradd`/`ptrsub`；
- 指针状态四态矩阵；one-past/dangling/null；`n=0` 不绕过；
- 对象边界/越界/未初始化（UB，checked build）；必选 trap 与 UB 分离；
- 宿主分配块含 n≥1 个 T 元素（位于宿主/堆区）；`TcAllocator` W 一致；allocator 边界不泄漏
  宿主宽度；
- 静态布局/对齐/地址 0 保留；按依赖 DAG 逆向拓扑 + 模块内源序排列，按 W 对齐（32/64 → 4/8），
  `#program` 顶层 `var` 与 `static var` 同属静态区，同档案确定；
- checked build 只在产生 UB 的访问触发；悬垂栈地址落在 `[stack_base, 当前帧基址)` 时报告
  `TC_RE_ADDR_UNAVAILABLE`；对悬垂指针的 `copy`/返回/比较/`ptrdiff`/`bitcast ptr↔ptr` 不得触发；
- `funcaddr` 地址唯一/ICF 禁用；`call_indirect` 空指针 vs 签名 UB。

### 6.10 调用与 ABI

- 直接/间接；位置/命名；参数顺序/数量/类型；`void`；递归；`D` 边界；
- AAPCS64/AAPCS32/ILP32：整数/浮点/指针/fnptr 参数类、返回类、struct/HFA、栈对齐、
  callee-saved、bool/int8/int16 扩展、`int64/float64` 双字/软浮点；
- `TcParam` 前置条件：SEM 校验、优化器假设、运行期不检查；`fnptr` 不携带；违反 UB；
- 帧/区布局：VM/AOT 同档案区内偏移一致；栈大小保证 D 帧；O0/O2 使用同一 `D` 与同一抽象
  深度/trap 规则。

### 6.11 I/O

- 输出：十进制整数/uint/bool/浮点 `%g` 规则/指数两位/负零/NaN 不带符号；
- 输入：空白/Token 完整性/无 `+`/无 `f` 后缀/`-0`/范围/EOF/短读/错误；
- 提交：整条 all-or-nothing；失败 0 字节；此前不回滚；`read` 失败不改目标；`_ = read T` 读取后
  丢弃，失败仍 `TC_RE_IO`，不参与 DI；
- 不受 locale 影响；`TcIoSink`/`TcIoSrc` 接口契约；
- 32/64 范围：`isize/usize` 按 W；`int64/uint64` 始终 64 位。

### 6.12 VM

- 逐 opcode；对象/区/指针；初始化状态（配合静态 DI）；调用/`D`；trap/提交；I/O；bool 规范化；
- profile 隔离；宿主宽度不泄漏；地址可观察性（环境定义，跨优化不稳定）；
- 参考角色：运行时 golden 由标准推导；VM 不依赖 AOT 输出。

### 6.13 AArch64 后端

- lowering：算术/位/比较/分支/调用/内存/结构体/FP 调用运行时；
- ABI：AAPCS64 全量；
- 帧/区布局：sp 对齐、callee-saved、栈帧、静态区/设备区/宿主区基址；
- 必选 trap 分支；checked build 分支；volatile 访问；
- 汇编/链接/运行：clang/lld/qemu；`.s` 可重复；ELF 可执行；
- 入口/ICF：不同函数入口唯一；`disable_icf`。

### 6.14 差分

- 同源、同 IR、同档案；VM vs AOT；
- 比较：stdout 字节、trap 码、退出状态、终止/非终止；
- 地址归一化：`bitcast ptr→int` 输出、跨对象/悬垂比较/`ptrdiff` 结果按环境定义处理；
  差分按“地址字节差”对拍，不比绝对地址；
- 不做跨 32/64；不把 Mach-O 便利路径纳入门禁；
- checked/release 分开统计；O0/O2 分开或证明一致；
- 差异最小化：输出首个差异字节/指令位置/两条执行轨迹摘要。

### 6.15 32/64 双档案

- 分档案 golden；分档案差分；分档案 CT/布局/对象表；
- 标准 §13.2.2 全表；`bitcast`/`trunc`/`cast`/`sizeof`/`@padding`/`ptrdiff`/字面量范围/
  `@range`/`@align`/`read`/`write` 的 W 差异；
- 32 位 `int64/float64` 辅助、未对齐 64 位、`n×sizeof T` 数学值；
- 32 位 ABI/栈对齐/callee-saved；
- 不比较跨档案绝对地址/静态结果/首诊。

### 6.16 freestanding/裸机

- `-ffreestanding -nostdlib`；无 libc；链接脚本；复位入口；区基址；
- PL011 UART 输出/输入；设备策略四态；trap halt/复位；退出策略；
- heap arena；静态初值；设备端不解析 import；
- qemu-system-aarch64 virt；无动态依赖；符号审计；
- 按 freestanding 档案自身的 golden 与 `platform/baremetal` 用例判定（W=64 即
  `aarch64-baremetal`；W=32 裸机目标待 S9 选定后建库）；与语言级确定行为的差异仅允许来自
  平台参数（设备策略/退出策略/heap/区基址）；不要求与 hosted 跨档案一致。

### 6.17 工具链与驱动

- `tc-vm`/`tc-aot` CLI：源路径、profile、include roots、输出路径、退出码、stdout/stderr 契约；
- 产物：`.s`/`.o`/ELF/flat；可执行权限；链接脚本；运行时 `.a`；
- 可重复性：相同输入相同 `.s`；对象/ELF 比较归一化元数据；
- 私有性：`tc_core` 不安装、不导出公共 API/ABI；无 C 宿主公共库；
- 错误处理：找不到源/目标/include；资源失败 `TC_ERR_OUT_OF_MEMORY`；不伪装语言诊断。

### 6.18 确定性与鲁棒性

- 重复运行；不同哈希种子；不同线程数；不同测试顺序；
- 编译期 ASan/UBSan；内存泄漏；崩溃/挂起；
- 模糊：随机字节、截断、深层嵌套、超长行/标识符、随机 import 图；
- 资源限制：若实现施加规模上限，必须文档化并与 `TC_ERR_OUT_OF_MEMORY` 区分；
- 超时/死循环保护；测试隔离（临时目录、无网络、固定环境）。

---

## 7. 门禁、出口准则与缺陷处理

### 7.1 通用门禁

| 门禁 | 内容 | 判定 |
| ---- | ---- | ---- |
| G-A 规范一致性 | 80 码封闭；不增不减；不产生警告；首诊/位置正确 | 码表校验 + 一致性套件 |
| G-B 确定性 | 重复/哈希/线程/顺序扰动下结果一致 | 确定性套件 |
| G-C 双档案 | W=32/64 分档案 golden/差分/CT/布局 | 32/64 套件 |
| G-D 差分 | 同档案 VM↔AOT 零不一致 | 差分套件 |
| G-E 平台 | 必选 trap、freestanding、设备/退出策略 | 平台套件 |
| G-F 工程质量 | 构建无错误、无未定义行为、无泄漏、无公共 ABI 泄漏 | 构建/符号/消毒器 |
| G-G 覆盖 | 结构性覆盖清单完成；未覆盖项显式列出 | 覆盖报告 |
| G-H 文档 | 追溯矩阵、门禁报告、变更记录同步 | 文档审计 |

### 7.2 阶段门禁要点

| 阶段 | 必须通过 | 不允许带入下一阶段 |
| ---- | ---- | ---- |
| G0 | 契约/码表/harness/矩阵 | 未冻结的跨模块接口；未定义的判定器行为 |
| G1 | LEX/SYN 全码；IR 形态；位置 | LEX 报 SEM 码；合法 EBNF 不能构造 IR |
| G2 | 导入 5 码；顺序；实例键 | IMPORT 码推迟到 SEM；`module_order` 不稳定 |
| G3 | 类型/转换/布局；32/64 翻转 | 布局进入 CT；跨档案共享布局 |
| G4 | CFG/DI/调用/安全/注解；首诊组合 | DI 用实现遍历顺序；承诺违反被静态拒绝 |
| G5 | CT 6 码；缓存；IR 冻结 | 失败 const 进入代码生成；CT 在 SEM 前启动 |
| G6 | VM 全 opcode/trap；golden；提交语义 | 宿主地址泄漏；跨档案复用对象表 |
| G7 | 子集差分；工具链；零 pass | 未支持 opcode 静默生成错误代码 |
| G8 | 全量差分；§13.4 八项；§12 十五项 | checked build 改变必选 trap/合法集合 |
| G9 | freestanding；32 位 ABI/语义 | 用 Mach-O 代替 ELF 门禁；跨档案比较 |
| G10 | 新目标准入；长期回归；发布审计 | 未声明优化改变基线；公共 ABI 泄漏 |

### 7.3 缺陷等级

| 等级 | 定义 | 处理 |
| ---- | ---- | ---- |
| B1 | 规格偏差：错误码/首诊/位置/可观察行为/接受集与标准不符；安全代码出现 UB；差分不一致 | 阻塞门禁，必须修复 |
| B2 | 覆盖缺失：规范条款无对应用例；平台/ABI 路径未测 | 阻塞门禁，补齐用例 |
| B3 | 工程质量：确定性抖动、可重复性、资源失败、测试工具误判 | 阻塞门禁，修复 |
| B4 | 文档/可维护性：说明缺失、矩阵未更新 | 门禁前修复 |

**禁止豁免**：B1 不得以“已知问题”“实现差异”“优化需要”为由豁免；标准不允许的静态拒绝、
额外诊断码、放宽接受集都属于 B1。

### 7.4 已知限制与挂起

- 允许在阶段内以 `not-implemented` 标注未完成能力，但必须：
  1) 在覆盖矩阵中可见；2) 不进入“通过”统计；3) 在对应阶段门禁前关闭；
  4) 不得出现在发布产物中。
- 平台环境限制（如 macOS 无 qemu-user）不属于实现缺陷，但必须改用 Linux CI/容器或
  qemu-system，且不得降低门禁。

### 7.5 覆盖审计

每个阶段门禁前生成：

- 80 码覆盖表（触发/正例/位置/阶段）；
- opcode 覆盖表（正常/静态错误/trap）；
- 标准章节覆盖表（正例/反例）；
- 设计 §12 十五项映射表；
- 32/64 翻转表；
- 未覆盖/挂起清单。

审计结果进入门禁报告；未覆盖项未解释则门禁不通过。

---

## 8. 可追溯性

### 8.1 标准章节 → 测试套件

| 标准章节 | 主题 | 主要阶段 | 主要套件 |
| ---- | ---- | ---- | ---- |
| §0 | 原则/字长/目标相关静态结果 | S0/S3 | L0、L3 |
| §1 | 抽象机/可观察行为/阶段/顺序 | S0/S6/S8 | L1、L3、L4 |
| §2 | 词法 | S1 | L1、L3（lex） |
| §3 | 类型/转换/对象/指针状态 | S3/S6 | L1、L3（sem）、L4（ptr/mem） |
| §4 | 程序结构/模块/导入/可见性/CT 顺序 | S1/S2/S5 | L3（import/sem/ct） |
| §5 | 存储对象/作用域/DI | S3/S4 | L3（sem/di） |
| §6 | 指令集/语义/验证 | S1/S3/S4/S6/S8 | L1、L3、L4、L5 |
| §7 | 控制流核心/CFG | S4 | L1、L3（cfg） |
| §8 | 函数/调用/注解 | S4/S7/S8/S9 | L3、L4、L6（abi） |
| §9 | 安全模型 | S4/S8 | L3（safety）、L4 |
| §10 | 优化契约 | S0/S7/S8/S10 | L3、L5、L7 |
| §11 | 标准 I/O | S6/S8/S9 | L4（io）、L6 |
| §12 | 诊断/首诊/位置/80 码 | S0/S1/S3/S4/S5 | L0、L3、全阶段 |
| §13 | 平台档案/验证要求 | S0/S3/S7/S8/S9 | L0、L5、L6、L7 |
| §14 | EBNF | S1 | L1、L3（syn） |

### 8.2 设计 §12 十五项 → 套件

| # | 设计 §12 项 | 阶段 | 套件/判定 |
| ---- | ---- | ---- | ---- |
| 1 | VM↔AOT 差分 | S7/S8 | `diff/`，L5 |
| 2 | §13.4 八项一致性 | S8 | 一致性套件，L3/L5/L6 |
| 3 | golden 向量 | S6/S8 | `golden/w32`、`golden/w64`，L4 |
| 4 | freestanding 编译门禁 | S9 | `platform/baremetal`，L6 |
| 5 | checked build | S8/S9 | `runtime/*`（`-checked` 后缀用例），L4/L6 |
| 6 | 32/64 静态矩阵 | S3/S9 | `32-64-diff/`，L3/L6 |
| 7 | ABI/调用 | S7/S8/S9 | `conformance/abi/`，L6 |
| 8 | 对象/内存 | S8 | `runtime/mem`、`runtime/ptr`，L4 |
| 9 | 诊断/语法 | S1/S3/S4 | `conformance/static/`，L3 |
| 10 | `@fast_math` | S8 | `runtime/float`，L4/L5 |
| 11 | IR 形态 | S5 | `unit/ir`，L1 |
| 12 | const/静态初值 | S5/S8 | `conformance/static/ct`、`runtime/const`，L3/L4 |
| 13 | 类型表/模块实例 | S0/S2/S5 | `unit/ir`、`conformance/static/import`，L1/L3 |
| 14 | 退出/设备/输入 | S8/S9 | `platform/`、`runtime/io`，L4/L6 |
| 15 | 零 pass | S7/S8/S10 | `determinism/`、`diff/`，L5/L7 |

### 8.3 80 码 → 首测阶段

见附录 A；每个码必须同时出现在 `tests/traceability/codes.*` 中，字段包括：码、阶段、首测阶段、
用例 ID、正例 ID、位置断言、32/64 敏感性。

### 8.4 阶段 → 设计 P 期

| 本计划 | 设计 §13 | 说明 |
| ---- | ---- | ---- |
| S0 | P0 | 契约先行 |
| S1–S6 | P1 | 前端 + IR + VM 参考 |
| S7 | P2 | AArch64 子集 + 工具链 + 差分 |
| S8 | P3 | 全量语义/运行时/差分 |
| S9 | P4 | freestanding + 32 位 |
| S10 | P5 | 多目标 + 长期回归 |

---

## 9. 配置与环境矩阵

### 9.1 编译/运行配置

| 维度 | 取值 | 用途 |
| ---- | ---- | ---- |
| W | 32 / 64 | 双档案；分 golden/差分 |
| 目标 | aarch64-linux-user（64 hosted） | 主 hosted 差分 |
| 目标 | aarch64-baremetal（64 freestanding） | 裸机/UART/设备策略 |
| 目标 | arm32 或 riscv32（32 位，S9 选定） | 32 位 ABI/语义 |
| 构建 | Debug / Release | 断言/优化差异；Release 必选 trap 仍执行 |
| checked | off / on | UB 检出与必选 trap 分开统计 |
| 优化 | O0 / O2 | 零 pass 下应一致 |
| 消毒器 | host ASan/UBSan | 编译器自身鲁棒性 |
| 执行 | VM / qemu-user / qemu-system | 参考/用户态/裸机 |

### 9.2 环境固定

- `LC_ALL=C`、`LANG=C`、`TZ=UTC`；固定 stdin 字节；固定工作目录；禁网络；禁宿主 locale
  影响浮点/输出。
- qemu 版本/参数固定并记录；链接脚本/UART stub 版本化。
- macOS 上 qemu-user 不可用时，hosted 差分放 Linux CI/容器；不得用 Mach-O 便利路径替代 ELF
  门禁。
- 测试报告记录：profile、目标、构建类型、checked、优化级别、工具链版本、qemu 版本、用例 ID。

### 9.3 产物审计

- ELF：无未解析符号、无动态依赖（freestanding）、入口正确、区段/基址符合链接脚本。
- 符号：不同函数入口唯一；无 ICF 合并；私有 `tc_core` 不导出公共 API。
- `.s`：可重复；标签/注释归一化后仍一致；不含实现内部临时名泄漏到诊断（诊断只看 span）。

---

## 10. 交付物、评审与变更控制

### 10.1 交付物

| 交付物 | 内容 | 阶段 |
| ---- | ---- | ---- |
| 契约文档/头文件 | 类型/IR/档案/诊断/运行时接口 | S0 |
| 80 码机读表 | 码/阶段/触发/位置/首测阶段 | S0 起维护 |
| 测试框架 | 判定器/清单/profile/归一化/报告 | S0 |
| 测试套件 | 单元/静态/运行时/差分/ABI/平台/工具 | S1–S10 |
| golden 库 | W=32/64 分档案，含推导依据 | S1 起累积 |
| 覆盖矩阵 | 码/opcode/章节/设计 §12/32-64 | 每阶段更新 |
| 门禁报告 | G0–G10 的通过证据与挂起清单 | 每阶段 |
| 平台产物 | ELF/flat、链接脚本、UART stub、运行时 `.a` | S7–S9 |
| 发布审计 | 符号/API/文档/变更记录 | S10 |

### 10.2 评审

- 契约评审：S0 冻结前；任何跨模块结构变更都需重新评审。
- golden 评审：新增/修改期望必须附标准条款推导；静态/运行时 golden 禁止由实现输出回填。
- 门禁评审：每阶段出口前；覆盖审计、未覆盖清单、缺陷等级、差分报告。
- 平台评审：新目标/新 ABI/新设备策略接入前；确认不改变规范确定行为与 80 码封闭。

### 10.3 变更控制

- 标准/设计变更：先更新标准/设计，再同步契约、码表、golden、覆盖矩阵、门禁；不得只改实现。
- 测试用例变更：必须记录原因（标准条款/缺陷/平台）；删除用例需说明覆盖替代。
- 差分基线变更：不得为了让差分通过而改 VM 或 AOT 的期望；必须先定位是规范偏差还是
  环境定义差异。
- 优化启用：必须先声明 pass 管线与不变量测试，再改零 pass 基线；未声明前不得引入 SSA/
  regalloc/内联。
- 禁止：新增语言诊断码；放宽接受集；用实现资源限制替代语言诊断；恢复历史实现/测试。

---

## 附录 A 80 个诊断码测试清单

> 阶段取标准 §12.3；首测阶段为本计划建议。每个码都必须有：触发用例、恰好一个诊断断言、
> 主位置断言；行内注明“W 相关”的码需覆盖 32/64 差异。

### A.1 词法/语法（7）

| 码 | 阶段 | 首测 | 测试要点 |
| ---- | ---- | ---- | ---- |
| `TC_CE_SYNTAX` | LEX/SYN | S1 | 非法字节/非 ASCII/未列字符/`/`/`//`/`;`/`%`/`#`；Token 序列；未知 opcode/后缀；未定义 `(base,suffix)`；`DISCARD` 位置；位置到首 Token |
| `TC_CE_MISSING_END` | SYN | S1 | `func`/`struct`/`unsafe` 缺 `end`；多候选取最早；位置到未配对开启关键字 |
| `TC_CE_MODULE_LAYER` | SYN | S1 | `#lib`/`#program` 层序交错；位置到破坏层序的最早构造 |
| `TC_CE_MISSING_VISIBILITY` | SYN | S1 | `#lib` 成员缺 `public`/`private`；位置到成员关键字 |
| `TC_CE_PROGRAM_MODE_MISUSE` | SYN | S1 | `#program` 用 `func/static/public/private/Self`；`#lib` 顶层 `var`/指令 |
| `TC_CE_VAR_MISSING_INIT` | SYN | S1 | `var`/`static var` 缺 `=` 或初始化器；位置到标识符 |
| `TC_CE_LITERAL_LEX_RANGE` | LEX | S1 | 整数 >2^64−1；浮点舍入 0/±inf；位置到完整字面量首 Token（含 `-`） |

### A.2 名称/模块（15）

| 码 | 阶段 | 首测 | 测试要点 |
| ---- | ---- | ---- | ---- |
| `TC_CE_UNDEFINED_NAME` | SEM | S3 | 未解析名；`var` 初始化器自引用；`sizeof` 类型参数；字段类型前向引用 |
| `TC_CE_DUPLICATE_NAME` | SEM | S3 | 同函数/程序值名重复；位置到第二次，关联首次 |
| `TC_CE_DUPLICATE_MEMBER` | SEM | S3 | 模块成员重名；位置到第二次，关联首次 |
| `TC_CE_DUPLICATE_PARAMETER` | SEM | S3 | 形参重名；位置到第二次 |
| `TC_CE_FUNCTION_SCOPE_ACCESS` | SEM | S3 | 值位置裸名引用本模块函数/const/static var |
| `TC_CE_UNDEFINED_FUNCTION` | SEM | S3 | `Self.f`/`m.f` 未解析；位置到函数名 |
| `TC_CE_PRIVATE_ACCESS` | SEM | S3 | 访问导入 `private` 成员；位置到成员名，关联声明 |
| `TC_CE_PRIVATE_TYPE` | SEM | S3 | `private` 类型出现在 public 签名/字段/const/static var |
| `TC_CE_IMPORT_NOT_FOUND` | SEM（IMPORT 报告） | S2 | 搜索目录/include roots 无 `m.tc`；位置到模块名 |
| `TC_CE_IMPORT_NOT_LIB` | SEM（IMPORT 报告） | S2 | 目标为 `#program`；位置到模块名 |
| `TC_CE_DUPLICATE_IMPORT` | SEM（IMPORT 报告） | S2 | 同一导入者重复同一目标；位置到后一个 `import` |
| `TC_CE_CIRCULAR_IMPORT` | SEM（IMPORT 报告） | S2 | 自导入/循环；循环优先于重复；位置到环中最早边 |
| `TC_CE_IMPORT_NAME_CONFLICT` | SEM（IMPORT 报告） | S2 | 导入名与本地模块/局部名冲突 |
| `TC_CE_DUPLICATE_LABEL` | SEM | S4 | 同函数/程序标签重名；位置到第二次 |
| `TC_CE_LABEL_NOT_FOUND` | SEM | S4 | `br`/`switch` 目标不存在；位置到标签名 |

### A.3 类型/声明/控制（32）

| 码 | 阶段 | 首测 | 测试要点 |
| ---- | ---- | ---- | ---- |
| `TC_CE_TYPE_MISMATCH` | SEM | S3 | 赋值/传参/返回/操作数类型；`bitcast`/`cast` 类别禁止；字段中间层非 struct；位置到首个不匹配操作数 |
| `TC_CE_LITERAL_TYPE` | SEM | S3 | 类别/后缀/符号性不符；`br_if`/`switch`/`return`/实参位置优先于构造码 |
| `TC_CE_LITERAL_RANGE` | SEM | S3 | 源/期望类型范围；W 相关；与 `LITERAL_TYPE` 同现时前者优先 |
| `TC_CE_MODE` | SEM | S3 | 已知 `(base,suffix)`、类型族正确但符号/模式不适用 |
| `TC_CE_ANNOTATION` | SEM | S4 | 未知/位置/冲突/承诺不在 `@fast`；参数 `@align`/`@range`；W 相关 |
| `TC_CE_OPERAND_COUNT` | SEM | S3 | 操作数数量/形状；无结果指令 LHS 非 `_`；`make` 多余位置字段 |
| `TC_CE_CONDITION_TYPE` | SEM | S4 | `br_if` 条件非 bool；不抢先于条件内部字面量码 |
| `TC_CE_RETURN_TYPE` | SEM | S4 | `return` 类型不符 |
| `TC_CE_RETURN_FORM` | SEM | S4 | void/非 void 返回形态不符 |
| `TC_CE_RETURN_OUTSIDE_FUNCTION` | SEM | S4 | `#program` 顶层 `return` |
| `TC_CE_MISSING_RETURN` | SEM | S4 | 非 void 可达末尾；空体；位置到返回类型，关联 `end` |
| `TC_CE_ARGUMENT_COUNT` | SEM | S4 | 位置实参数量不符 |
| `TC_CE_ARGUMENT_ORDER` | SEM | S4 | 命名实参顺序；位置/命名混用 |
| `TC_CE_UNKNOWN_ARGUMENT` | SEM | S4 | 命名实参未知；`call_indirect` 命名 |
| `TC_CE_DUPLICATE_ARGUMENT` | SEM | S4 | 命名实参重复 |
| `TC_CE_MISSING_ARGUMENT` | SEM | S4 | 命名实参缺失 |
| `TC_CE_UNKNOWN_FIELD` | SEM | S3 | `make`/字段访问未知字段 |
| `TC_CE_MISSING_FIELD` | SEM | S3 | `make` 缺字段 |
| `TC_CE_DUPLICATE_FIELD` | SEM | S3 | `make` 重复字段；struct 字段重名 |
| `TC_CE_STRUCT_SELF_VALUE` | SEM | S3 | 值字段直接/间接自引用；`fnptr` 签名例外 |
| `TC_CE_EMPTY_STRUCT` | SEM | S3 | 无字段 |
| `TC_CE_LAYOUT_OVERFLOW` | SEM | S3 | 布局求和溢出 W 位 `usize`；W 相关 |
| `TC_CE_FIELD_ORDER` | SEM | S3 | `make` 顺序；位置/命名字段混用 |
| `TC_CE_IMMUTABLE_FIELD` | SEM | S3 | readonly 字段赋值；路径首个 readonly |
| `TC_CE_CONST_ASSIGNMENT` | SEM | S3 | 对 const/`Self.K`/`m.K` 赋值 |
| `TC_CE_PARAMETER_ASSIGNMENT` | SEM | S3 | 对参数赋值/`read` 目标 |
| `TC_CE_UNINITIALIZED` | SEM | S4 | 读取/赋值/`read` 未确定初始化；最早标识符 |
| `TC_CE_ADDR_OF_CONST` | SEM | S3 | `addr` 的 const 目标 |
| `TC_CE_BITCAST_WIDTH` | SEM | S3 | 类别允许但位宽不等；W 相关 |
| `TC_CE_TRUNC_WIDTH` | SEM | S3 | 目标位宽 ≥ 源；W 相关 |
| `TC_CE_SWITCH_TYPE` | SEM | S4 | switch 值非整数 |
| `TC_CE_VOID` | SEM | S3 | void 作值/参数/var/字段/fnptr 参数/ptr 所指/转换目标/`sizeof`/I/O |

### A.4 安全/常量（11）

| 码 | 阶段 | 首测 | 测试要点 |
| ---- | ---- | ---- | ---- |
| `TC_CE_UNSAFE_OPERATION` | SEM | S4 | unsafe 指令不在 unsafe 上下文 |
| `TC_CE_UNSAFE_CALL` | SEM | S4 | 调 `@unsafe` 不在 unsafe 上下文 |
| `TC_CE_FAST_CALL` | SEM | S4 | 调 `@fast`/`@fast_math` 不在 unsafe 上下文 |
| `TC_CE_SAFE_VIOLATION` | SEM | S4 | `@safe` 含 unsafe 指令/调用/任何 unsafe 块；优先于 `UNSAFE_*`/`FAST_CALL` |
| `TC_CE_CONST_FORM` | SEM | S5 | 非法 const 形态/后缀/指针位重解释；`@padding` N 形式 |
| `TC_CE_CONST_OVERFLOW` | CT | S5 | strict 整数/浮点溢出；strict `shl` 溢出；W 相关 |
| `TC_CE_CONST_DIV_ZERO` | CT | S5 | 整数除零；strict 浮点除零；`mod` 零除数 |
| `TC_CE_CONST_SHIFT` | CT | S5 | 常量 `shl`/`shr` 负计数 |
| `TC_CE_CONST_FLOAT_INVALID` | CT | S5 | 常量 strict 浮点无效/NaN 输入 |
| `TC_CE_CONST_FLOAT_UNDERFLOW` | CT | S5 | 常量 strict 浮点下溢 |
| `TC_CE_CONST_CAST` | CT | S5 | 常量 `cast` 溢出；W 相关 |

### A.5 运行时（15）

| 码 | 首测 | 测试要点 |
| ---- | ---- | ---- |
| `TC_RE_INTEGER_OVERFLOW` | S6（VM）/S8（AOT） | 有符号 strict 溢出；`div(INT_MIN,−1)`/`abs`/`neg`/strict `shl`；W 相关 |
| `TC_RE_DIV_ZERO` | S6/S8 | 整数除零/模零；strict 浮点除零 |
| `TC_RE_NEGATIVE_SHIFT` | S6/S8 | 移位计数为负 |
| `TC_RE_FLOAT_OVERFLOW` | S6/S8 | strict 浮点上溢 |
| `TC_RE_FLOAT_UNDERFLOW` | S6/S8 | strict 浮点下溢 |
| `TC_RE_FLOAT_INVALID` | S6/S8 | strict 浮点无效/NaN 输入 |
| `TC_RE_CAST_OVERFLOW` | S6/S8 | `cast` 结果不可表示；W 相关 |
| `TC_RE_NULL_POINTER` | S6/S8 | `load/store/memmove/ptradd/ptrsub` 空指针 |
| `TC_RE_PTRDIFF_OVERFLOW` | S6/S8 | `ptrdiff` 不可由 W 位 `isize` 表示 |
| `TC_RE_WRITE_PROTECTED` | S6/S8 | 只读参数对象写穿 |
| `TC_RE_ADDR_UNAVAILABLE` | S6/S8 | 代码区/区外/设备拒绝 |
| `TC_RE_STACK_OVERFLOW` | S6/S8 | 抽象深度达到 `D`；`call`/`call_indirect` |
| `TC_RE_NULL_FUNCTION_POINTER` | S6/S8 | `call_indirect` 空指针 |
| `TC_RE_IO` | S6/S8 | `read`/`write`/`writeln` 失败；EOF/短读/非法 Token |
| `TC_RE_TRAP` | S6/S8 | `trap` 指令 |

---

## 附录 B 指令覆盖清单

> 每个 opcode 至少：正常运行、边界、静态错误；有 trap 的再加 trap 用例；有模式的覆盖模式矩阵。
> 位置/类型/安全诊断由 L3 覆盖，运行时由 L4/L5 覆盖。

| 组 | opcode | 运行时重点 |
| ---- | ---- | ---- |
| 算术 | `add/sub/mul`（strict/`.wrap`/`.ieee`） | 溢出/回绕/IEEE；模式与类型不匹配静态错误 |
| 算术 | `div/mod`（strict/`.ieee`） | 除零/`INT_MIN/−1`/`mod` 符号/精确有理商 |
| 算术 | `neg`（signed: strict/`.wrap`；unsigned/浮点无模式） | `INT_MIN`；unsigned 固定模 2^n；浮点仅符号位 |
| 算术 | `abs`（无模式） | `INT_MIN`；unsigned 恒等；浮点仅符号位 |
| 算术 | `shl`（strict/`.wrap`） | 负计数/`k>=n`/signed vs unsigned；浮点非法 |
| 算术 | `shr`（无模式） | 负计数/`k>=n`/算术 vs 逻辑右移；浮点非法 |
| 位/逻辑 | `and/or/xor/not` | 整数按位；bool 逻辑；严格求值不短路 |
| 比较 | `eq/ne/lt/le/gt/ge` | 有符号/无符号/浮点 NaN；bool 仅 eq/ne |
| 转换 | `cast/trunc/bitcast` | 范围/窄化/等宽位串；类别先于宽度 |
| 内存 | `load/store`（`.volatile`） | 空指针/只读/代码/设备；bool 规范化；volatile 顺序 |
| 内存 | `memmove` | 重叠/`n=0`/null/one-past/dangling/越界 |
| 指针 | `addr/ptradd/ptrsub` | 对象边界/one-past/n×sizeof/空指针 |
| 指针 | `ptrdiff/ptreq/ptrne/ptrlt/ptrle/ptrgt/ptrge` | 同对象/跨对象/悬垂/null；溢出 |
| 类型 | `sizeof` | W 相关；struct 求和；`sizeof void` |
| 函数 | `funcaddr/funceq/funcne` | 入口唯一；nullptr；不同签名 |
| 函数 | `call/call_indirect` | 参数顺序/深度 `D`/空 `fnptr`/签名 UB |
| 结构体 | `make/copy` | 字段顺序/命名/类型；整块复制含 padding |
| 控制 | `br/br_if/switch/return/trap` | 标签/默认/返回/`TRAP`；CFG 边与 DI |
| I/O | `read/write/writeln` | 格式/Token/提交/EOF/短读/错误 |

**模式后缀**：`.wrap` 仅 `add/sub/mul/neg/shl` 合法位置；`.ieee` 仅浮点 `add/sub/mul/div/mod`；
`.volatile` 仅 `load/store`；`.nsw/.nuw` 仅 `@fast` 的 `add/sub/mul/shl`。每个非法组合都要有
`MODE`/`SYNTAX`/`ANNOTATION` 分类用例。

---

## 附录 C 用例元数据格式

### C.1 静态诊断用例

```yaml
id: TC-SEM-CONV-0042@w32
profiles: [w32]
source: sem/conv/bitcast_ptr_int_32.tc
include_roots: [fixtures/include]
spec: "§3.11 bitcast 矩阵；§13.2.2 字长相关"
expect:
  stage: SEM
  code: TC_CE_BITCAST_WIDTH
  primary: {file: main.tc, line: 7, column: 11}
  related: []
  diagnostics: exactly-one
```

### C.2 运行时用例

```yaml
id: TC-RT-FP-0107@w64
profiles: [w64]
source: runtime/float/format_g6.tc
run:
  stdin_hex: ""
spec: "§11.2 %g 输出；§6.3 舍入"
expect:
  stdout_hex: "31 2e 32 33 34 35 36 0a"
  trap: null
  exit: 0
  termination: normal
```

### C.3 差分用例

```yaml
id: TC-DIFF-A64-0003
profile: aarch64-linux-user
source: diff/subset/add_overflow.tc
oracle: vm
compare: [stdout_bytes, trap_code, exit_status, termination]
normalize: [address_literals, stack_offsets_unobservable]
spec: "§13.4 静态一致；设计 §12.1 差分"
expect:
  diff: identical
```

### C.4 平台用例

```yaml
id: TC-PLT-BARE-0006
profile: aarch64-baremetal
source: platform/baremetal/uart_write.tc
link: fixtures/baremetal/link.ld
uart: pl011
spec: "设计 §7.2 四缝；§8 档案"
expect:
  uart_hex: "4f 4b 0a"
  trap: null
  exit: 0
```

**要求**：所有用例必须含 `id`、`profiles/profile`、`source`、`spec`、`expect`；诊断用例必须
断言 `diagnostics: exactly-one`；运行时用例必须显式写 `trap`（null 或码）与 `exit`；差分用例的
`expect` 至少写 `diff: identical`，其 trap/exit 由 `compare` 列表覆盖；平台用例必须声明链接
脚本/UART/设备策略。

---

## 附录 D 阶段门禁检查表

在每个阶段出口逐项勾选；任一项“否”则门禁不通过。

### D.1 通用

- [ ] 80 码表未增删；码名/阶段/触发与标准一致
- [ ] 新增/修改用例均有 `spec` 依据与独立评审
- [ ] 覆盖矩阵已更新；未覆盖/挂起项有解释
- [ ] 首诊/位置断言覆盖本阶段相关码
- [ ] 32/64 期望分档案；无跨档案比较
- [ ] 确定性套件通过（重复/哈希/线程/顺序）
- [ ] 无新增警告；无标准之外的诊断
- [ ] 构建/消毒器/符号审计通过
- [ ] 门禁报告记录 profile/目标/工具链版本

### D.2 各阶段附加

| 阶段 | 附加检查 |
| ---- | ---- |
| S0 | 契约冻结；码表机读；harness 失败关闭；矩阵骨架 |
| S1 | LEX 不报 SEM 码；所有合法 EBNF 可构造 IR；span 行列 |
| S2 | IMPORT 码在 IMPORT 阶段；`module_order` 稳定；实例键 |
| S3 | 类型表冻结；bitcast 矩阵；32/64 翻转；布局属 SEM |
| S4 | DI 固定点；调用顺序；安全/注解；首诊 §12.2 第 1–10 项 |
| S5 | CT 只在 SEM 后；缓存/失败归属；IR 冻结；失败 const 不进入代码生成 |
| S6 | VM 全 opcode；必选 trap；提交语义；VM golden |
| S7 | 子集差分零不一致；`.s` 可重复；零 pass；未支持 opcode 显式失败 |
| S8 | 全量差分；§13.4 八项；§12 十五项；checked 分离；全量 ABI |
| S9 | freestanding 无 libc；32 位 ABI/语义；四设备策略；qemu-system |
| S10 | 新目标准入；长期回归；优化（若启用）不变量；发布审计 |

---

*— 文档结束 —*
