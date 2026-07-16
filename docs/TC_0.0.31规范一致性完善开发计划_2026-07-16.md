# TC 0.0.31 规范一致性完善开发计划

> 计划日期：2026-07-16
>
> 状态：已完成（M0～M6 全部门禁通过）
>
> 评审依据：[`TC_0.0.31设计与实现评审报告_2026-07-16.md`](./TC_0.0.31设计与实现评审报告_2026-07-16.md)
>
> 规范基线：[`TC语言标准设计说明书_0.0.31.md`](./TC语言标准设计说明书_0.0.31.md)
>
> 实现基线：分支 `codex/tc-spec-0.0.31`，提交 `a603a7478201cc4f7db2375967cf5bb789b176a6`
>
> 修复提交：`e7c08c84890e0171ba9dcaefd9147db5115f9a1b`

---

## 1. 目标与发布口径

本计划用于关闭 TC 0.0.31 当前已确认的两项规范偏差，并补齐诊断优先级、测试证据和工程文档：

1. `if` / `while` 的常量条件剪枝只支持布尔字面量和直接 `let bool` 引用，未覆盖完整的合法单层常量布尔表达式。
2. `and` / `or` 的确定初始化读集裁剪只识别布尔字面量，未识别源序更早且词法可见的 `let bool` 左操作数。
3. 同一 Token 位置存在多重违规时，诊断阶段和首错优先级缺少系统化自动测试证明。
4. 合规报告、架构索引、测试映射和协作说明存在口径不同步或评审基线不精确的问题。

完成本计划前，发布口径应为：

> 实现具备高完成度和稳定回归，但存在两项已知规范偏差，不声明完全符合 TC 0.0.31。

完成全部发布门禁后，方可恢复“完全符合 TC 0.0.31”的声明。

### 1.1 范围内

- 标准 §4.2 / §4.3、§7.2.2、§11.0 的语义澄清
- `tc_const_eval` 的静态布尔求值复用
- Pass2 已解析常量绑定的使用
- CFG 条件边剪枝和逻辑 RHS 读集裁剪
- CFG / DFA / Analyzer 单元测试、VM fixture、AOT 差分和 `--check` 覆盖
- 合规报告、架构索引、测试映射和协作文档同步

### 1.2 非目标

- 不新增语法、Token、AST 节点或 `TcRhsKind`
- 不改变 `let` 的单层表达式限制
- 不修改 VM / AOT 的运行时短路结果
- 不引入跨语句常量传播或对 `var` 值进行推测
- 不放宽词法作用域、源序可见性或 `goto` 合法性规则

---

## 2. 已确定的设计决策

以下决策作为实现前提，不再通过缩减标准来适配当前代码：

1. 保留 0.0.31 现有完整常量条件剪枝要求。
2. “静态布尔条件”必须返回 `bool`，并且是 §4.3 定义的合法单层常量表达式。
3. 常量操作数必须是字面量，或源序更早且在当前词法作用域可见的 `let`。
4. 含 `var`、前向 `let`、不可见 `let` 或非单层调用的条件返回“未知”，CFG 同时保留 true / false 边。
5. 常量形态成立但求值产生溢出、除零或非法转换时，按 §4.3 的常量错误映射在编译期失败，不静默降级为“未知”。
6. 短路仅裁剪确定初始化读边；被裁剪的 RHS 仍必须通过词法、语法、名称和类型检查。
7. 静态布尔求值优先使用 Pass2 已写入的 `TcResolvedBinding` / `const_bits`，避免 CFG 再次按名称执行一套可能与作用域解析漂移的查找。
8. 内部判定使用三态结果：`true`、`false`、`unknown`；求值错误继续使用现有 `TcDiagnostic` fail-fast 契约。

---

## 3. 执行顺序与依赖

```text
M0 冻结基线
  → M1 标准语义定稿
  → M2 先补失败测试
  → M3 实现统一静态布尔求值和 CFG 裁剪
  → M4 诊断优先级证明
  → M5 文档与合规证据同步
  → M6 全量发布门禁
```

要求：M2 中用于复现规范偏差的测试必须在 M3 修改前稳定失败，并记录实际诊断；M3 完成后同一批测试必须转为通过。

---

## 4. 任务清单

### M0：冻结评审和实现基线

| ID | 状态 | 任务 | 主要产物 | 完成判据 |
|----|------|------|----------|----------|
| BASE-01 | [x] | 记录实施分支、完整提交哈希、平台、编译器和 CMake 版本 | 开发记录或提交说明 | 所有测试证据可关联到唯一提交 |
| BASE-02 | [x] | 保存当前正常回归基线 | VM / AOT / unit 结果 | 基线明确记录为 VM 435、AOT 257、unit 1617 全通过 |
| BASE-03 | [x] | 保存两个最小复现程序的当前失败结果 | 复现记录 | `let bool` 短路和复合常量条件均稳定复现 `UninitializedVariable` |
| BASE-04 | [x] | 将合规状态临时改为“存在已知偏差” | 合规报告状态 | 实现修复前不保留“48/48 完全合规”口径 |

实施基线记录（M0，2026-07-16）：

- 分支：`codex/tc-spec-0.0.31`
- 提交：`a603a7478201cc4f7db2375967cf5bb789b176a6`
- 平台：macOS 26.5.2（Build 25F84，arm64）
- 编译器：Apple clang 21.0.0（clang-2100.1.1.101）
- CMake：4.3.4
- 基线命令：`make`、`bash scripts/run_tests.sh`
- 基线结果：VM 435/435、AOT 257/257、unit 1617/1617，0 failed
- 原始复现：`let FALSE` 短路在结果定义处、`gt(int32, TEN, FIVE)` 条件在后续读取处均稳定报告 `TC_ERR_UNINITIALIZED_VARIABLE`。

### M1：标准语义定稿

| ID | 状态 | 任务 | 主要文件 | 完成判据 |
|----|------|------|----------|----------|
| STD-01 | [x] | 在确定初始化章节定义“静态布尔条件” | `docs/TC语言标准设计说明书_0.0.31.md` §4.2 / §4.3 | 明确结果类型、允许形态、词法可见性、源序和三态判定 |
| STD-02 | [x] | 固定分析阶段顺序 | 标准 §4.2、§11.0 | 明确 `名称/类型 → let 求值 → 静态布尔判定 → CFG → DFA` |
| STD-03 | [x] | 明确常量条件求值错误的阶段与映射 | 标准 §4.3、§11.0 | 溢出、除零、非法转换不再存在实现选择空间 |
| STD-04 | [x] | 完善 `let bool` 短路的规范措辞 | 标准 §7.2.2 | 明确 AND/OR、字面量/更早 let、只裁剪读边、仍检查名称和类型 |
| STD-05 | [x] | 增加正例和反例 | 标准 §4 / §7 示例 | 至少包含复合常量条件、`let FALSE`/`let TRUE` 短路和 `var` 不推测示例 |
| STD-06 | [x] | 复核规则间无重复白名单 | 标准全文相关段落 | CFG 章节引用 §4.3，不另行维护易漂移的操作符列表 |

### M2：测试先行

#### M2.1 VM / AOT fixture

| ID | 状态 | 建议用例 | 目的 | 预期结果 |
|----|------|----------|------|----------|
| TEST-01 | [x] | `tests/valid/uninit_shortcircuit_let_bool.tc` | `let FALSE` 的 AND 和 `let TRUE` 的 OR 排除未初始化 RHS 读取 | VM `--check` 和执行通过；AOT 差分通过 |
| TEST-02 | [x] | `tests/valid/uninit_const_condition_if.tc` | `gt(int32, TEN, FIVE)`、`and(bool, FLAG_A, FLAG_B)` 剪除不可达 if 边 | 不可达分支不触发未初始化错误 |
| TEST-03 | [x] | `tests/valid/uninit_const_condition_while.tc` | 复合常量 false 的 while 体不可达 | while 体中的未初始化读取不参与 DFA |
| TEST-04 | [x] | `tests/errors/static/shortcircuit_let_invalid_rhs.tc` | 验证短路 RHS 仍做名称检查 | 报 `TC_ERR_UNDEFINED_VARIABLE` |
| TEST-05 | [x] | `tests/errors/static/shortcircuit_let_rhs_type.tc` | 验证短路 RHS 仍做类型检查 | 报规范指定的类型错误 |
| TEST-06 | [x] | `tests/errors/static/uninit_shortcircuit_var_lhs.tc` | 验证 `var bool` 左值不参与值推测 | RHS 保持可达并报 `TC_ERR_UNINITIALIZED_VARIABLE` |
| TEST-07 | [x] | 复用或新增前向/跨块 let 用例 | 验证源序和词法作用域 | 前向或不可见名称按名称解析阶段报错，不参与静态求值 |

fixture 文件名可在实现时按项目现有命名约定微调，但测试语义和覆盖点不得删除。

#### M2.2 单元测试

| ID | 状态 | 任务 | 主要文件 | 完成判据 |
|----|------|------|----------|----------|
| TEST-08 | [x] | 增加静态布尔操作数判定表驱动测试 | `tests/unit/runtime/test_analyzer.c` | 覆盖字面量、常量绑定、var、前向/不可见绑定 |
| TEST-09 | [x] | 增加完整 RHS 静态布尔求值测试 | `test_analyzer.c` | 覆盖 literal、const_ref、整数/浮点 compare、logic bin/un、合法 bool cast |
| TEST-10 | [x] | 增加 CFG 条件边剪枝断言 | `tests/unit/runtime/test_cfg.c` | if/while 的 true、false、unknown 三种结果均有断言 |
| TEST-11 | [x] | 增加逻辑 RHS 读集断言 | `test_cfg.c` | 字面量和 `let bool` 短路时均不记录 RHS 变量槽 |
| TEST-12 | [x] | 增加 DFA 合流结果断言 | `test_analyzer.c` / `test_cfg.c` | 剪枝后仅可达前驱参与确定初始化交集 |
| TEST-13 | [x] | 验证常量求值错误映射 | `test_analyzer.c` | 常量形态条件的溢出/除零/非法转换符合 STD-03 |

#### M2.3 测试注册

| ID | 状态 | 任务 | 主要文件 | 完成判据 |
|----|------|------|----------|----------|
| TEST-14 | [x] | 注册 VM 执行与 `--check` 测试 | `scripts/vm/run_tests.sh` | 每个 valid/static fixture 都有对应断言 |
| TEST-15 | [x] | 注册 AOT 差分和 `--check` 测试 | `scripts/aot/run_tests.sh` | 与 VM 对静态接受集和可执行结果一致 |
| TEST-16 | [x] | 确认单元目标覆盖新增断言 | `tests/unit/**/CMakeLists.txt`（仅按需） | `check-analyzer`、`check-cfg` 可独立运行 |

M2 修复前证据：`shortcircuit_let` 过滤器 8 passed / 2 failed，`const_condition` 过滤器 0 passed / 4 failed；Analyzer 242 passed / 4 failed，CFG 79 passed / 12 failed。所有失败均对应待修复的静态布尔/读集裁剪断言，反例诊断保持通过。

### M3：实现统一静态布尔求值与 CFG 裁剪

| ID | 状态 | 任务 | 主要文件 | 依赖 | 完成判据 |
|----|------|------|----------|------|----------|
| IMPL-01 | [x] | 新增/重构静态布尔操作数帮助函数 | `src/vm/analyzer/tc_const_eval.c/.h` | STD-01、TEST-08 | 能识别 bool 字面量和已解析的 `let bool` 绑定；`var` 返回 unknown |
| IMPL-02 | [x] | 扩展 `tc_try_eval_static_bool()` | `tc_const_eval.c/.h` | IMPL-01、TEST-09 | 覆盖 §4.3 中所有结果为 bool 的合法单层 RHS |
| IMPL-03 | [x] | 复用共享运行时语义 | `tc_const_eval.c`、`tc_semantics.*` | IMPL-02 | compare、logic、cast 不复制 VM/AOT 数值逻辑；浮点模式一致 |
| IMPL-04 | [x] | 实现常量求值错误传播 | `tc_const_eval.c` | STD-03、TEST-13 | 使用现有常量错误映射和 fail-fast 诊断，不吞错、不改写为 SyntaxError |
| IMPL-05 | [x] | 接入 if/while CFG 条件剪枝 | `src/vm/analyzer/tc_cfg.c` | IMPL-02、TEST-10 | true/false 删除不可能边，unknown 保留双边 |
| IMPL-06 | [x] | 接入逻辑 RHS 读集裁剪 | `tc_cfg.c` | IMPL-01、TEST-11 | `let FALSE` AND、`let TRUE` OR 均不加入 RHS 读槽 |
| IMPL-07 | [x] | 保证名称和类型检查不被短路绕过 | `src/vm/analyzer/tc_analyzer_pass2.c` | TEST-04、TEST-05 | RHS 名称/类型错误仍在 CFG/DFA 前报告 |
| IMPL-08 | [x] | 清理或统一 Pass2 的字面量专用短路逻辑 | `tc_analyzer_pass2.c` | IMPL-01、IMPL-06 | 文件模式不再维护第二套不一致规则；REPL 如需保留则复用同一帮助函数 |
| IMPL-09 | [x] | 修正代码注释中的规范章节引用 | `tc_analyzer_pass2.c`、`tc_cfg.c`、`tc_const_eval.c` | IMPL-08 | 短路引用统一指向 §7.2.2，常量条件引用 §4.2 / §4.3 |
| IMPL-10 | [x] | 复核内存和错误路径 | 上述实现文件 | IMPL-01～09 | 新帮助函数无不必要分配；所有错误路径遵循 0/-1 和 `TcDiagnostic` 单槽契约 |

#### IMPL-02 必须覆盖的 bool RHS

- `TC_RHS_LIT`
- `TC_RHS_CONST_REF`
- `TC_RHS_COMPARE`
- `TC_RHS_FLOAT_COMPARE`
- `TC_RHS_LOGIC_BIN`
- `TC_RHS_LOGIC_UN`
- 目标为 `bool` 且满足 §4.3 的合法严格转换

`bitcast` 不允许 `bool`，整数 `truncate` 不能产生合法 bool 条件；不得为了凑分支而新增例外路径。

### M4：诊断阶段和首错优先级证明

| ID | 状态 | 任务 | 主要文件 | 完成判据 |
|----|------|------|----------|----------|
| DIAG-01 | [x] | 从标准 §11.0 提取诊断优先级矩阵 | 本计划附属记录 / 标准 | 每一阶段至少有一组冲突场景 |
| DIAG-02 | [x] | 增加同 Token 多违规 fixture 或单元测试 | `tests/errors/static/`、`test_analyzer.c` | 语法、名称、类型/模式、常量求值、CFG/DFA 首错顺序均被锁定 |
| DIAG-03 | [x] | 修复测试暴露的阶段越界 | parser / pass2 / const_eval / CFG（按实际） | 实现首错与规范完全一致 |
| DIAG-04 | [x] | 验证不可达代码诊断边界 | fixture + analyzer unit | 不可达代码仍报名称/类型错误，但不报被剪枝读取的未初始化错误 |

M4 诊断优先级矩阵：

| 优先阶段 | 冲突场景 | 规范首错 | 自动化证据 |
|----------|----------|----------|------------|
| 词法/缩进 | 非法字符后另有未定义名称 | `TC_ERR_SYNTAX` | `test_analyze_diagnostic_priority_matrix` |
| 语法 | `and` 缺操作数且已有未定义名称 | `TC_ERR_SYNTAX` | `diag_priority_syntax_before_name.tc` + Analyzer unit |
| 名称 | 同一 RHS 左操作数类型错误、右操作数未定义 | `TC_ERR_UNDEFINED_VARIABLE` | `diag_priority_name_before_type.tc` + Analyzer unit |
| 类型/模式 | 浮点 `wrap` 与错误后缀字面量并存 | `TC_ERR_MODE_MISMATCH` | `diag_priority_mode_before_literal.tc` + Analyzer unit |
| `let` 求值 | 常量除零后另有可达未初始化读取 | `TC_ERR_CONSTANT_DIV_ZERO` | `diag_priority_const_before_dfa.tc` + Analyzer unit |
| 静态布尔/CFG | 静态不可达代码含名称或类型错误 | 名称/类型错误，不进入 DFA | `shortcircuit_let_invalid_rhs.tc`、`shortcircuit_let_rhs_type.tc`、Analyzer unit |
| DFA | `var` 左操作数使逻辑 RHS 保持可达 | `TC_ERR_UNINITIALIZED_VARIABLE` | `uninit_shortcircuit_var_lhs.tc` + Analyzer unit |

矩阵曾暴露 `and(bool, 1, missing)` 先报类型错误的阶段越界；Pass2 现先对单层 RHS 完成名称预解析，再进入类型、模式与初始化检查。

### M5：文档、索引和合规证据同步

| ID | 状态 | 任务 | 主要文件 | 完成判据 |
|----|------|------|----------|----------|
| DOC-01 | [x] | 更新完整 CFG 特性说明 | `.cursor/skills/tc-architecture/features.md` | “字面量短路”改为“字面量或更早且可见的 let bool” |
| DOC-02 | [x] | 更新测试映射和最终测试规模 | `.cursor/skills/tc-architecture/test-map.md` | 新 fixture/unit 映射完整，规模数字取实际结果 |
| DOC-03 | [x] | 更新知识图谱 CFG / 数据流节点 | `.cursor/rules/knowledge-graph.mdc`（按项目实际路径） | 静态布尔入口、读集裁剪和测试关系可追踪 |
| DOC-04 | [x] | 更新协作入口版本和特性摘要 | `AGENTS.md` | 当前版本为 0.0.31，新增能力与限制准确 |
| DOC-05 | [x] | 处理无版本标准文件的旧错误分类 | `docs/TC语言标准设计说明书.md` | 与 0.0.31 同步或明确标记为历史版本，避免双权威来源 |
| DOC-06 | [x] | 更新合规检查项 C-03 和短路检查项 | `docs/设计实现合规审查报告.md` | 检查项包含复合条件与 `let bool`，不是只证明字面量子集 |
| DOC-07 | [x] | 更新本次评审报告结论 | `docs/TC_0.0.31设计与实现评审报告_2026-07-16.md` | 明确修复提交、复现关闭情况和最终发布判断 |
| DOC-08 | [x] | 固化证据元数据 | 两份报告 | 记录分支、完整提交哈希、平台、命令、实际计数和日期 |

M5 完成证据：版本化标准为 0.0.31 唯一权威来源；架构特性、测试映射、知识图谱、协作入口和两份评审报告均已同步，最终规模统一为 VM 459、AOT 272、unit 1726。

### M6：验证与发布门禁

#### M6.1 最小验证

```bash
bash scripts/vm/run_tests.sh --filter shortcircuit
bash scripts/vm/run_tests.sh --filter const_condition
cmake --build build --target check-analyzer
cmake --build build --target check-cfg
```

| ID | 状态 | 验证项 | 完成判据 |
|----|------|--------|----------|
| VAL-01 | [x] | 两个原始最小复现 | 均由 `UninitializedVariable` 转为编译通过 |
| VAL-02 | [x] | 新增 valid fixture | VM 执行、VM `--check`、AOT 差分、AOT `--check` 全通过 |
| VAL-03 | [x] | 新增 static fixture | 错误 kind 和关键消息与标准一致 |
| VAL-04 | [x] | Analyzer / CFG 单元测试 | 所有新增断言通过 |

#### M6.2 全量和结构验证

```bash
make
bash scripts/run_tests.sh
python3 scripts/sync/check_rhs_coverage.py
python3 scripts/sync/check_source_naming.py
```

| ID | 状态 | 验证项 | 完成判据 |
|----|------|--------|----------|
| VAL-05 | [x] | 标准构建 | C99 严格警告配置零错误、零警告 |
| VAL-06 | [x] | 全量 VM / AOT / unit | 新的实际总数全部通过，0 failed |
| VAL-07 | [x] | RHS 分发覆盖 | 脚本通过；本任务不新增 `TcRhsKind` |
| VAL-08 | [x] | 源文件命名 | 脚本通过；本任务原则上不新增模块 |

#### M6.3 发布级安全和平台验证

```bash
bash scripts/run_asan_all.sh
make build-ubsan
bash scripts/run_tests.sh --ubsan
cmake -S . -B build-no-fenv -DTC_FORCE_NO_FENV=ON
cmake --build build-no-fenv --target check-semantics
make memcheck-macos
```

| ID | 状态 | 验证项 | 完成判据 |
|----|------|--------|----------|
| VAL-09 | [x] | ASan / UBSan | 全矩阵无 sanitizer 报告 |
| VAL-10 | [x] | no-fenv | 浮点比较参与静态条件时仍与正常语义一致 |
| VAL-11 | [x] | macOS 内存检查 | MallocScribble / leaks 门禁通过，无已知泄漏 |
| VAL-12 | [x] | VM/AOT 接受集一致性 | 新增程序在 VM 与 AOT `--check` 中结论一致 |

M6 完成证据（2026-07-16）：

- 最小回归：`shortcircuit_let` 10/10、`const_condition` 4/4、`diag_priority` 8/8；Analyzer 280/280、CFG 91/91。
- 标准全量：VM 459/459、AOT 272/272、unit 1726/1726，0 failed；标准构建零警告。
- 结构检查：RHS 覆盖与源文件命名脚本均通过。
- ASan / UBSan：两套全矩阵均通过，无 sanitizer 报告。
- no-fenv：`check-semantics` 494/494。
- macOS：MallocScribble 全矩阵通过；`leaks --atExit` 下 VM 459/459，报告 0 leaks，随后 AOT 272/272、unit 1726/1726 通过。为兼容 macOS 26，VM leaks 包装器隔离系统报告并保留被测程序真实输出与退出码。

---

## 5. 发布验收标准（Definition of Done）

只有以下项目全部满足，计划才可标记完成：

- [x] 标准正式定义静态布尔条件、常量求值错误和 `let bool` 短路规则。
- [x] 两个已确认复现程序编译通过，且执行结果符合短路/条件剪枝语义。
- [x] 复合常量条件覆盖 if/while、true/false/unknown 三态。
- [x] 被短路或常量条件排除的读取不触发 `TC_ERR_UNINITIALIZED_VARIABLE`。
- [x] 被排除代码中的语法、名称和类型错误仍能按规范报告。
- [x] `var` 条件不做值推测，前向或不可见 `let` 不被错误折叠。
- [x] 常量形态条件的求值错误与 §4.3 / §11.0 一致。
- [x] VM、AOT、unit、结构检查和发布级安全门禁全部通过。
- [x] `features.md`、`test-map.md`、知识图谱和 `AGENTS.md` 已同步。
- [x] 合规报告不再以字面量子集证明完整 C-03，所有新增检查项有代码和自动化测试证据。
- [x] 两份评审/合规报告记录唯一提交基线、修复提交和实际测试计数。
- [x] 最终发布结论经复核后恢复为“完全符合 TC 0.0.31”。

---

## 6. 建议提交拆分

为便于审查和回滚，建议按以下顺序提交：

1. `docs: clarify TC 0.0.31 static boolean reachability rules`
2. `test: add failing CFG constant-condition and let-bool short-circuit cases`
3. `fix: evaluate bound constant booleans for CFG pruning`
4. `test: lock diagnostic priority and unreachable-code behavior`
5. `docs: refresh TC 0.0.31 architecture and compliance evidence`

测试提交允许在实现提交前暂时失败，但不得进入发布分支；合并后的每个发布候选提交必须满足 M6 全部门禁。

---

*— 计划结束 —*
