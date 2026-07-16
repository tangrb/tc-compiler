# TC 编译器 0.0.31 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标：** 在保持 v0.0.26 已交付基线可追溯的前提下，使 libtc、TC-VM 与 TC-AOT 完整实现 TC 0.0.31，并以共享 typed program、完整 CFG、统一数值语义和 VM/AOT/let 一致性测试形成可发布证据。

**架构：** 采用“依赖优先 + 纵向闭环”的混合迁移。先稳定共享 IR、诊断和作用域元数据，再建立唯一的完整 CFG 与固定点分析；随后收敛 cast/truncate/bitcast、浮点与 let 语义，最后接通 VM、AOT、libtc、CLI 和发布审查。每个阶段都必须有独立的测试证据，版本号只在最终发布门禁通过后升级。

**技术栈：** C99、CMake、Make、shell 测试驱动、TC 源码 fixtures、VM 直接执行、AOT C99 差分、ASan、UBSan、fenv/no-fenv 双路径。

## 全局约束

- 规范权威来源是 [TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md)；实现计划不得改写其合法程序集、结果或诊断阶段。
- 当前实现基线是 v0.0.26：384 项 VM、857 项 unit、210 项 AOT；这些数字只用于冻结迁移起点，不作为 0.0.31 的完成率。
- TC 0.0.31 是破坏性升级；不保留长期的 v0.0.26/v0.0.31 双语义开关。
- C99 严格编译：-std=c99 -Wall -Wextra -pedantic；不得依赖 GNU 扩展、C 有符号溢出、严格别名违规或实现定义移位。
- 全流水线 fail-fast；TcDiagnostic 只保存第一条错误。
- 任意分配失败必须返回 TC_ERR_OUT_OF_MEMORY，消息固定为 memory allocation failed。
- Parser 负责形态与缩进；Binder/Type 负责名称、类型与模式；Control/CFG 负责跳转、循环上下文和确定初始化；Executor/AOT 不重新判断静态合法性。
- VM、AOT 与 let 必须共享或逐项证明相同的位宽、舍入、NaN、异常、cast、bitcast 和 I/O 语义。
- 0.0.31 没有语言警告；TcWarningList 只能作为兼容空壳。
- 新 TcRhsKind 必须在 8 个分发点闭环，并通过 scripts/sync/check_rhs_coverage.py。
- 新 TcStmtKind 必须覆盖 parse/free、Pass1、Pass2、CFG、REPL、Executor、AOT、stmt_index 和测试。
- 新源模块必须使用同名 tc_<module>.h / tc_<module>.c，加入 CMake，并通过 scripts/sync/check_source_naming.py。
- static fixture 一文件一错；新增 fixture 必须注册到 scripts/vm/run_tests.sh，并在适用时加入 scripts/aot/run_tests.sh。
- 迁移期间不得提前修改 src/vm/driver/tc_version.h 或 src/aot/main.c 中的 0.0.26 版本号。
- 0.0.31 范围不包含函数、数组、结构体、指针、字符串、JIT、字节码文件格式或新公共编译 API。

---

## 1. 规划依据与当前差距

### 1.1 权威输入

| 文档 | 本计划使用的约束 |
| ---- | ---------------- |
| [TC 语言标准 0.0.31](./TC语言标准设计说明书_0.0.31.md) | 合法程序集合、运行结果、41 个语言错误、强制 var 初始化、while、完整 CFG、bitcast、数值与 let 规则 |
| [TC-VM 详细设计说明书](./TC-VM详细设计说明书.md) | typed program、通用块、固定槽、CFG、Executor 控制结果、REPL 边界 |
| [TC-AOT 详细设计说明书](./TC-AOT详细设计说明书.md) | C99 代码生成、原生 while、shim、位模式发射、差分门禁 |
| [libtc 设计说明书](./libtc设计说明书.md) | 成功才转移所有权、诊断域、可重复消费、OOM 回滚 |
| [设计—实现合规与差距报告](./设计实现合规审查报告.md) | v0.0.26 冻结基线、0.0.31 差距矩阵、迁移用例与发布门槛 |

### 1.2 明确差距

| 领域 | 当前 v0.0.26 | 0.0.31 目标 |
| ---- | ------------ | ----------- |
| IR | 16 RHS、9 STMT | 增加 bitcast、while、break、continue 及解析后控制元数据 |
| var | RHS 可选 | 唯一合法形态为 var name: type = rhs |
| 控制流 | if + 受限 goto | 加入结构化 while，且与 goto/label 范式隔离 |
| 初始化分析 | if/goto 路径敏感近似 | 所有边显式进入 CFG，按可达前驱交集求固定点 |
| 转换 | strict/truncate/float cast 分散 | strict 全可表示性；truncate 仅整数窄化；bitcast 等宽位复制 |
| 浮点 | strict/ieee/wrap | 仅 strict/ieee；逐操作目标精度；固定异常和 NaN 规则 |
| let | 旧模式限制与宿主精度行为 | 单层调用、源序更早 let、与 runtime 每步一致 |
| 诊断 | 40 kind 含 OOM，文件错误冒充 SyntaxError | 41 个语言错误 + OOM；Language/API/Implementation 分域 |
| 后端 | VM/AOT 支持 v0.0.26 | 同一 typed contract，控制流、位模式和错误时机一致 |

### 1.3 方案比较与选型

| 方案 | 优点 | 风险 | 结论 |
| ---- | ---- | ---- | ---- |
| 依赖优先 + 纵向闭环 | CFG 和共享语义只建设一次；每个后端按稳定契约接入 | 前几个阶段主要交付内部能力 | **采用** |
| 按特性纵向切片 | 单个 while 或 bitcast 较快可见 | 容易在 VM/AOT/let 重复临时语义，CFG 会二次返工 | 不采用为主线 |
| 一次性大切换 | 最终结构可一次落位 | 长期红灯、回归定位困难、无法小步评审 | 不采用 |

bitcast 与数值 helper 相对独立，计划仍把它作为一个完整纵向切片；while 则必须等待作用域和 CFG 契约稳定后才开放执行。

---

## 2. 阶段总览

| 阶段 | 主题 | 主要产物 | 退出门槛 |
| ---- | ---- | -------- | -------- |
| M0 | 冻结基线与迁移账本 | 可复现 v0.0.26 基线 | 384/857/210 与两项结构检查通过 |
| M1 | 共享 IR 与诊断骨架 | 新 statement 结构、目标错误名、诊断域骨架 | types/free/ownership 单测通过 |
| M2 | Lexer、Parser 与通用块 | while/break/continue、强制 var RHS、R1–R7 | lexer/parser/stmt_index 与 var 迁移用例通过 |
| M3 | 作用域、绑定与控制元数据 | while scope、loop id、resolved goto、固定 slot | symbol/analyzer 控制上下文单测通过 |
| M4 | 完整 CFG 与固定点 | tc_cfg 模块、可达性、常量剪枝、统一确定初始化 | CFG 结构断言与 static --check 矩阵通过 |
| M5 | cast/truncate/bitcast 与浮点收敛 | 共享 tc_sem_cast、TC_RHS_BITCAST、模式迁移 | RHS 覆盖、语义矩阵、bitcast VM/AOT 差分通过 |
| M6 | let 语义收敛 | 单层常量调用、逐操作精度、runtime 等价 | let/runtime/AOT 位模式三方一致 |
| M7 | VM 控制流执行 | while、最内层 break/continue、控制结果传播 | VM 循环与固定槽用例通过 |
| M8 | AOT 控制流与差分 | 原生 C while、完整运行时差分 | 新旧 AOT 全矩阵与 C99 严格编译通过 |
| M9 | libtc、REPL、CLI 边界 | 事务式编译、API 错误域、REPL 限制 | API/OOM/重复消费/CLI golden 通过 |
| M10 | 迁移清理、文档与发布 | 删除旧语义、全量证据、版本升级 | 全部 0.0.31 合规门槛关闭 |

依赖主链：

~~~text
M0 → M1 → M2 → M3 → M4 → M7 → M8 → M9 → M10
                  └────→ M5 → M6 ────────┘
~~~

M5 可在 M3 稳定后与 M4 的后半段交错，但 M6 的常量条件结果必须在 M4 的常量剪枝最终验收前接入。

---

## 3. 开发与提交纪律

- 每个任务遵循：先加最小失败测试 → 确认失败原因正确 → 最小实现 → 最小回归 → 受影响层回归 → 提交。
- 一个提交只关闭一个可独立评审的契约；测试迁移与对应语义修改放在同一提交。
- 旧 fixture 证明了已删除行为时，不直接删除：改名或迁移到 static/runtime，并在提交说明中记录 0.0.31 对应条款。
- 每个阶段结束更新 docs/设计实现合规审查报告.md 的差距状态和关闭证据，但在 M10 之前总体结论保持“0.0.31 当前不符合”。
- 测试总数始终由脚本实测，不在实现中预写目标数字。
- 每阶段建议提交顺序：test → core contract → implementation → migration/docs；若测试与实现无法独立构建，则合为一个原子提交。

---

## M0：冻结 v0.0.26 基线

### Task 0.1：复现并记录迁移起点

**Files**

- Verify: src/vm/driver/tc_version.h
- Verify: src/aot/main.c
- Modify after verification: docs/设计实现合规审查报告.md

**Interfaces**

- Consumes: 当前 v0.0.26 源码与现有测试注册。
- Produces: 后续每阶段可比较的基线证据；不修改语言行为。

- [ ] 运行构建与全量测试。

~~~bash
make
bash scripts/run_tests.sh
python3 scripts/sync/check_rhs_coverage.py
python3 scripts/sync/check_source_naming.py
~~~

预期：构建成功；基线为 384 VM、857 unit、210 AOT；两项结构检查通过。若仓库在执行本计划前已有新提交，以脚本实测数字替换报告中的基线并说明来源。

- [ ] 确认 TC_VM_VERSION 与 TC_AOT_VERSION 均为 0.0.26。
- [ ] 在合规报告中记录提交哈希、测试日期、平台与命令，不把结果写成 0.0.31 通过率。
- [ ] 提交建议：docs: freeze v0.0.26 migration baseline

### Task 0.2：建立迁移 fixture 清单

**Files**

- Modify: .cursor/skills/tc-architecture/test-map.md
- Modify: docs/设计实现合规审查报告.md
- Inspect/Register later: scripts/vm/run_tests.sh
- Inspect/Register later: scripts/aot/run_tests.sh

**Produces**

- 旧行为迁移表：var_no_init、fp_*wrap*、fp_cast_truncate、const_cyclic_dep、cross-block、fp_cast_overflow、旧模式错误。

- [ ] 为每个旧用例标记目标动作：保留、改写、迁为 static、迁为 runtime 或由新用例替代。
- [ ] 明确 var_no_init 在 M2 迁为 VarMissingInitializer。
- [ ] 明确浮点 wrap 与浮点 truncate 位重解释在 M5 迁移。
- [ ] 明确 ConstantCircular、CrossBlockReference、FloatCastOverflow 的期望名在对应语义阶段迁移。
- [ ] 提交建议：test: map 0.0.31 breaking migrations

**M0 Gate**

- [ ] 工作树干净。
- [ ] 基线命令全部通过。
- [ ] 未修改 0.0.31 标准文件。

---

## M1：共享 IR、所有权与诊断骨架

### Task 1.1：增加结构化循环 AST 契约

**Files**

- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/parser/tc_parser_free.c
- Modify: src/vm/runtime/tc_stmt_index.h
- Modify: tests/unit/runtime/test_types.c
- Modify: tests/unit/runtime/test_stmt_index.c

**Interfaces**

- Produces:

~~~c
typedef struct {
    int line;
    int loop_id;              /* Analyzer 成功后 >= 0 */
    TcRhs condition;
    TcStatement *body;
    size_t body_count;
} TcWhileStmt;

typedef struct {
    int line;
    int loop_id;              /* Analyzer 成功后指向最内层 while */
} TcLoopControlStmt;
~~~

- TcStmtKind 增加 TC_STMT_WHILE、TC_STMT_BREAK、TC_STMT_CONTINUE。
- TcStatement union 增加 while_stmt、break_stmt、continue_stmt。
- stmt_index 规则固定为：while 自身占 1，body 按 DFS 源序递归；break/continue 各占 1。

- [ ] 先在 test_types.c 构造三类 statement，验证字段初始契约和 kind 名单。
- [ ] 先在 test_stmt_index.c 构造嵌套 if/while，写出精确 DFS span 断言。
- [ ] 运行 check-types 与 check-stmt-index，确认新测试因 kind/结构缺失而失败。
- [ ] 添加目标结构和 kind。
- [ ] 扩展 tc_statement_free，递归释放 while body；break/continue 无堆 payload。
- [ ] 扩展 tc_stmt_subtree_index_count，使 while span = 1 + body span。
- [ ] 运行：

~~~bash
cmake --build build --target check-types
cmake --build build --target check-stmt-index
~~~

预期：两项目标通过，无泄漏或越界。

- [ ] 提交建议：feat: add loop statement IR contracts

### Task 1.2：建立目标诊断枚举与诊断域骨架

**Files**

- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/runtime/tc_types.c
- Modify: src/vm/runtime/tc_diagnostic.h
- Modify: src/vm/runtime/tc_diagnostic.c
- Modify: tests/unit/runtime/test_types.c
- Create: tests/unit/runtime/test_diagnostic.c
- Modify: tests/unit/runtime/CMakeLists.txt
- Modify: tests/CMakeLists.txt

**Interfaces**

~~~c
typedef enum {
    TC_DIAG_NONE,
    TC_DIAG_LANGUAGE,
    TC_DIAG_API,
    TC_DIAG_IMPLEMENTATION
} TcDiagnosticDomain;

typedef enum {
    TC_API_ERR_NONE,
    TC_API_ERR_INVALID_ARGUMENT,
    TC_API_ERR_FILE_OPEN,
    TC_API_ERR_FILE_READ
} TcApiErrorCode;
~~~

- TcDiagnostic 增加 domain 与 api_code；language/implementation 继续使用 TcErrorKind。
- tc_diagnostic_set 保持现有调用兼容：普通语言 kind 设置 Language，OutOfMemory 设置 Implementation。
- 新增 tc_diagnostic_set_api；API 错误不伪造 TcErrorKind。
- 新增目标错误名：VarMissingInitializer、BitcastWidthError、LabelInsideLoop、GotoInsideLoop、BreakOutsideLoop、ContinueOutsideLoop。
- 旧 kind 暂时保留到调用点迁移完成；M10 删除并以全表测试确认最终集合。

- [ ] 在 test_types.c 先加入所有新增打印名的精确字符串断言。
- [ ] 在 test_diagnostic.c 先加入 Language/API/Implementation 初始化、覆盖、clear 和 print 前缀测试。
- [ ] 运行测试确认新增符号缺失。
- [ ] 实现域、API code、setter 和打印逻辑；覆盖旧 message/snippet 前必须释放。
- [ ] 为 OOM 验证 domain == TC_DIAG_IMPLEMENTATION。
- [ ] test-diagnostic 直接链接 libtc；把 check-diagnostic 加入 check-unit。
- [ ] 运行 check-types、check-diagnostic 与全量现有 unit。
- [ ] 提交建议：feat: add 0.0.31 diagnostic domains

**M1 Gate**

- [ ] 新 AST 可独立构造、递归释放和编号。
- [ ] 诊断域不会改变现有语言错误输出。
- [ ] make test-unit 通过。
- [ ] 版本仍为 0.0.26。

---

## M2：Lexer、Parser、通用块与强制初始化

### Task 2.1：新增循环关键字 Token

**Files**

- Modify: src/vm/lexer/tc_lexer.h
- Modify: src/vm/lexer/tc_lexer.c
- Modify: tests/unit/lexer/test_lexer.c
- Modify: tests/unit/lexer/test_lexer_extended.c

**Produces**

- TC_TOK_WHILE、TC_TOK_BREAK、TC_TOK_CONTINUE。
- 精确关键字匹配；whilex、breakfast、continued 仍是标识符。

- [ ] 先写关键字、大小写、前后缀边界和 token name 测试。
- [ ] 运行 check-lexer 与 check-lexer-extended，确认 unknown identifier/缺少 token 分支的失败。
- [ ] 实现关键字表和 tc_token_kind_name 映射。
- [ ] 运行两个 lexer target。
- [ ] 提交建议：feat: lex structured loop keywords

### Task 2.2：把 if 专用块解析收敛为通用块解析

**Files**

- Modify: src/vm/parser/tc_parser.c
- Modify: src/vm/parser/tc_parser.h
- Modify: src/vm/parser/tc_parser_internal.h
- Modify: src/vm/parser/tc_parser_free.c
- Modify: tests/unit/parser/test_parser.c

**Interfaces**

~~~c
typedef enum {
    TC_BLOCK_GLOBAL,
    TC_BLOCK_IF_THEN,
    TC_BLOCK_IF_ELSE,
    TC_BLOCK_WHILE
} TcParserBlockKind;
~~~

- 通用块帧保存 owner kind、owner line、base indent 和 statement buffer。
- if 可切换 then/else；while 只有 body；end 关闭最近的 if/while。
- label 是普通语句，不打开块。

- [ ] 先写 while false 空 body、嵌套 while、if 内 while、while 内 if、缺 end、错位 end 和多级意外缩进的 Parser 测试。
- [ ] 运行 check-parser，确认失败点是 while 未识别或块未闭合。
- [ ] 提取通用 block-body 逻辑，保持现有 if 用例输出 AST 不变。
- [ ] 增加 tc_parse_while_stmt；条件复用 tc_parse_rhs，then 必须行末结束。
- [ ] break/continue 只构造节点，合法上下文留给 Analyzer。
- [ ] 确保 Parser 失败释放当前 statement、已完成 body 和块栈。
- [ ] 运行 check-parser 与既有 indent/if 最小回归。
- [ ] 提交建议：refactor: parse if and while through common blocks

### Task 2.3：强制 var 声明带初始化器

**Files**

- Modify: src/vm/parser/tc_parser.c
- Modify: src/vm/runtime/tc_types.h
- Modify: tests/unit/parser/test_parser.c
- Move/Rewrite: tests/valid/var_no_init.tc → tests/errors/static/var_missing_initializer.tc
- Modify: scripts/vm/run_tests.sh
- Modify: scripts/aot/run_tests.sh

**Produces**

- var name: type 缺少等号或 RHS 时固定报告 TC_ERR_VAR_MISSING_INIT / VarMissingInitializer。
- 成功的 TcProgram 中 TcVarDef.has_rhs 恒为 1；字段只在迁移期间保留，M10 可删除。

- [ ] 先把 var_no_init 迁为 static fixture，断言错误 kind/name 与行号。
- [ ] 先写 Parser unit：完整声明成功；缺等号、等号后缺 RHS 均走专用错误。
- [ ] 运行过滤测试，确认当前实现仍接受旧 fixture或返回 SyntaxError。
- [ ] 在完整 var 前缀后做形态检查，不把错误交给通用 SyntaxError。
- [ ] 确认 read 不能补救无初始化器声明。
- [ ] 同一提交更新 VM/AOT 注册，防止旧 valid 路径残留。
- [ ] 运行：

~~~bash
bash scripts/run_tests.sh --filter var_missing_initializer
cmake --build build --target check-parser
~~~

- [ ] 提交建议：feat: require var initializers

### Task 2.4：扩展 stmt_index 与 Parser OOM 回滚

**Files**

- Modify: src/vm/runtime/tc_stmt_index.h
- Modify: tests/unit/runtime/test_stmt_index.c
- Modify: tests/unit/parser/test_parser.c

- [ ] 覆盖 while/if 混合 DFS 序号、空 body、深层嵌套和 body skip。
- [ ] 覆盖构造 body 中途失败时的递归释放。
- [ ] 运行 check-parser、check-stmt-index 与 ASan 下的对应过滤测试。
- [ ] 提交建议：test: cover loop indexing and parser rollback

### Task 2.5：锁定 0.0.31 词法与 EBNF 边界

**Files**

- Modify: src/vm/lexer/tc_lexer.c
- Modify: src/vm/parser/tc_parser.c
- Modify: tests/unit/lexer/test_lexer_extended.c
- Modify: tests/unit/parser/test_parser.c

- [ ] 增加无小数点科学计数 1e5、float32 后缀、inf/-inf/nan、仅小写 true/false 的边界测试。
- [ ] 增加负号与 u 后缀互斥、字面量后缀后继续数字/标识符、行注释后 NEWLINE/EOF 的反例。
- [ ] 确认 Lexer 只负责 Token 和位置，Parser 负责行结构、缩进与 end 配对。
- [ ] 对照标准附录 A 逐项核对新增关键字与产生式，不把缩进语义塞入单行 Lexer。
- [ ] 运行 check-lexer-extended 与 check-parser。
- [ ] 提交建议：test: lock 0.0.31 lexical boundaries

**M2 Gate**

- [ ] Lexer/Parser 接受标准 §4.8/§4.10 的结构，拒绝缺 end 与错误缩进。
- [ ] 浮点科学计数、布尔大小写、后缀边界与附录 A 一致。
- [ ] var 缺 RHS 固定在 Parser 阶段报告专用错误。
- [ ] 既有 if/goto Parser 测试无回退。
- [ ] 新循环尚不作为可执行能力对外声明。

---

## M3：作用域、绑定、固定槽与控制目标

### Task 3.1：扩展块路径为带类型的作用域标识

**Files**

- Modify: src/vm/analyzer/tc_analyzer_internal.h
- Modify: src/vm/analyzer/tc_analyzer_dfa.c
- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/runtime/tc_symbol.h
- Modify: src/vm/runtime/tc_symbol.c
- Modify: tests/unit/runtime/test_symbol.c
- Modify: tests/unit/runtime/test_analyzer.c
- Add: tests/errors/static/break_outside_loop.tc
- Add: tests/errors/static/continue_outside_loop.tc
- Modify: scripts/vm/run_tests.sh
- Modify: scripts/aot/run_tests.sh

**Interfaces**

~~~c
typedef enum {
    TC_BLOCK_IF_THEN,
    TC_BLOCK_IF_ELSE,
    TC_BLOCK_WHILE
} TcBlockKind;

typedef struct {
    int owner_stmt_index;
    TcBlockKind kind;
} TcBlockId;
~~~

- TcBlockId 定义在 src/vm/runtime/tc_types.h，因为持久化的 TcLabelEntry 与 typed program 都引用它；tc_analyzer_internal.h 只定义构建期容器和辅助函数。
- TcBlockPath 从 int 数组迁为 TcBlockId 数组，避免 while 与 then/else 编码碰撞。
- 标签仍按当前块和祖先链解析；不同兄弟块允许同名。

- [ ] 先写路径相等、祖先、子块、兄弟块和 while path 测试。
- [ ] 运行 check-symbol/check-analyzer 确认类型和比较逻辑未更新。
- [ ] 更新 push/pop/prefix 比较与标签 block_path 所有权。
- [ ] 保持现有 goto 合法性结果不变。
- [ ] 提交建议：refactor: type analyzer block paths

### Task 3.2：为 while 分配 scope、slot 与 loop id

**Files**

- Modify: src/vm/analyzer/tc_analyzer_pass1.c
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: src/vm/runtime/tc_symbol.c
- Modify: tests/unit/runtime/test_symbol.c
- Modify: tests/unit/runtime/test_analyzer.c

**Produces**

- 每个词法 var 在 Pass1 获得唯一固定 slot；循环迭代不重新分配。
- while body 是独立 scope；内层可见外层，end 后不可见。
- 每个 while 获得稳定 loop_id，break/continue 绑定最内层 loop_id。

- [ ] 先写同名 shadow、嵌套循环、循环外不可见、每迭代同 slot、最内层 loop id 测试。
- [ ] Pass1 对 while push scope → 递归收集 → 标记 scope end → pop。
- [ ] Pass2 维护 loop context 栈，校验条件结果为 bool。
- [ ] break/continue 无祖先 while 时分别报告目标错误。
- [ ] 运行 check-symbol 与 check-analyzer。
- [ ] 提交建议：feat: bind loop scopes and control targets

### Task 3.3：实现结构化/非结构化范式隔离

**Files**

- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Add: tests/errors/static/goto_inside_loop.tc
- Add: tests/errors/static/label_inside_loop.tc
- Add: tests/errors/static/goto_inside_nested_if_in_loop.tc
- Add: tests/errors/static/label_inside_nested_if_in_loop.tc
- Modify: scripts/vm/run_tests.sh
- Modify: scripts/aot/run_tests.sh

- [ ] 先注册四个一文件一错 fixture。
- [ ] 确认当前 analyzer 未给出目标错误。
- [ ] 对 goto/label 检查词法祖先 while，而不是只看直接父节点。
- [ ] 非法节点不得进入 CFG。
- [ ] 既有 while 外 goto/label 用例必须继续通过。
- [ ] 提交建议：feat: enforce loop and goto paradigm isolation

### Task 3.4：持久化解析后的控制目标

**Files**

- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: src/vm/executor/tc_executor.c
- Modify: src/aot/tc_aot_codegen.c
- Modify: tests/unit/runtime/test_analyzer.c

**Produces**

- TcGoto 保存 resolved_target_stmt_index 与 resolved 标志。
- TcLoopControlStmt 保存 loop_id。
- Executor/AOT 最终消费解析结果，不再次按名称或 block path 解析。

- [ ] 先在 analyzer unit 断言 forward/backward/向外 goto 的 target index。
- [ ] 分析成功后填入 resolved metadata；失败路径不留下部分有效标志。
- [ ] 暂时保留现有运行时查找作为断言对照；M7/M8 删除重复解析。
- [ ] 提交建议：feat: persist resolved control targets

**M3 Gate**

- [ ] while scope、固定 slot、loop id 和 goto target 都可从 typed program 唯一确定。
- [ ] break/continue 与 goto/label 错误阶段符合设计。
- [ ] check-symbol、check-analyzer 和现有 goto 回归通过。

---

## M4：完整 CFG、可达性与确定初始化固定点

### Task 4.1：新增唯一 CFG 模块

**Files**

- Create: src/vm/analyzer/tc_cfg.h
- Create: src/vm/analyzer/tc_cfg.c
- Modify: src/libtc/CMakeLists.txt
- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/analyzer/tc_analyzer.c
- Modify: src/vm/analyzer/tc_analyzer.h
- Create: tests/unit/runtime/test_cfg.c
- Modify: tests/unit/runtime/CMakeLists.txt
- Modify: tests/CMakeLists.txt

**Interfaces**

~~~c
typedef enum {
    TC_CFG_ENTRY,
    TC_CFG_EXIT,
    TC_CFG_STATEMENT,
    TC_CFG_BRANCH,
    TC_CFG_MERGE,
    TC_CFG_LOOP_CONDITION,
    TC_CFG_LOOP_EXIT
} TcCfgNodeKind;

typedef enum {
    TC_CFG_FALLTHROUGH,
    TC_CFG_TRUE,
    TC_CFG_FALSE,
    TC_CFG_BREAK,
    TC_CFG_CONTINUE,
    TC_CFG_GOTO,
    TC_CFG_SHORT_CIRCUIT
} TcCfgEdgeKind;

int tc_cfg_build(const TcProgram *program,
                 const TcSymbolTable *symbols,
                 TcCfg *out,
                 TcDiagnostic *diag);
void tc_cfg_init(TcCfg *cfg);
void tc_cfg_free(TcCfg *cfg);
~~~

- 节点保存内部 id、可选 stmt_index、line、scope id、读集合、写集合。
- 边保存 kind、from、to 和离开的 scope 集合。
- src/vm/runtime/tc_types.h 前置声明 typedef struct TcCfg TcCfg；TcTypedProgram 以 TcCfg *cfg 持有单一所有权，避免把 Analyzer 私有结构展开到公共头；Executor/AOT 只读该对象。

- [ ] 先写空程序、顺序、if/else、while、break、continue、goto、label 的精确 node/edge 断言。
- [ ] 运行 check-cfg，确认模块缺失。
- [ ] 实现 init/free、容量增长和 OOM 回滚。
- [ ] 实现结构化边，再接 resolved goto/control target。
- [ ] 把 tc_cfg.c 加入 libtc；test-cfg 链接 libtc 并仅在测试 target 暴露 Analyzer 私有 include path。
- [ ] 运行 check-cfg 和 source naming。
- [ ] 提交建议：feat: add explicit control flow graph

### Task 4.2：实现常量条件剪枝与可达性

**Files**

- Modify: src/vm/analyzer/tc_cfg.c
- Modify: src/vm/analyzer/tc_const_eval.c
- Modify: src/vm/analyzer/tc_const_eval.h
- Modify: tests/unit/runtime/test_cfg.c

**Interfaces**

~~~c
int tc_try_eval_static_bool(const TcRhs *rhs,
                            const TcSymbolTable *symbols,
                            int stmt_index,
                            int *is_constant,
                            int *value,
                            TcDiagnostic *diag);
~~~

- 仅字面量和源序中更早 let 组成的合法单层表达式可剪枝。
- while true 无条件失败边；while false body 不可达；不可达语句仍须通过语法、名称和类型检查。

- [ ] 先写 if true/false、while true/false、非恒定条件和非法前向 let 引用测试。
- [ ] CFG 先建全边，再应用合法剪枝，再从 entry 标记 reachable。
- [ ] 不可达节点保留在图中但不参与数据流前驱交集。
- [ ] 提交建议：feat: prune constant control-flow edges

### Task 4.3：把 tc_analyzer_dfa 改为 CFG 固定点

**Files**

- Modify: src/vm/analyzer/tc_analyzer_dfa.h
- Modify: src/vm/analyzer/tc_analyzer_dfa.c
- Modify: src/vm/analyzer/tc_analyzer_internal.h
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: tests/unit/runtime/test_analyzer.c
- Modify: tests/unit/runtime/test_cfg.c

**Interfaces**

~~~c
int tc_analyze_definite_init(const TcCfg *cfg,
                             size_t slot_count,
                             TcDiagnostic *diag);
~~~

- IN[entry] 为祖先已初始化集合。
- IN[n] 为所有可达前驱 OUT 的交集。
- OUT[var x = rhs] = IN[n] ∪ {x}；加入 x 前检查 RHS。
- 赋值目标、read 目标和变量读取都要求 binding 属于 IN。
- 使用 slot-index bitset、工作队列和单调传递函数；禁止路径枚举。

- [ ] 先写 diamond merge、零次循环、正常回边、continue、break、向前/向后 goto 和多前驱会合测试。
- [ ] 写 goto 跳过未使用 var 的合法测试。
- [ ] 写 goto 跳过后读取的 UninitializedVariable 测试。
- [ ] 写循环体仅初始化变量、循环后读取仍非法的测试。
- [ ] 写恒真 while 只有 break 前驱参与出口交集的测试。
- [ ] 运行新测试，确认旧源序/DFA 近似不能满足断言。
- [ ] 用 CFG 固定点替换 tc_prescan_init_history 主路径；REPL 兼容路径保持隔离。
- [ ] 删除影响合法程序集的轮次上限或路径猜测。
- [ ] 运行 check-analyzer、check-cfg 与 uninit/goto 过滤回归。
- [ ] 提交建议：feat: solve definite initialization on cfg

### Task 4.4：固定 Analyzer 阶段顺序与所有权

**Files**

- Modify: src/vm/analyzer/tc_analyzer.c
- Modify: src/vm/analyzer/tc_analyzer_pass1.c
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: src/vm/analyzer/tc_analyzer.h
- Modify: tests/unit/runtime/test_analyzer.c

**Produces**

~~~text
shape
→ bind/scope/slot
→ RHS and statement type
→ control context and label resolution
→ let values needed for pruning
→ CFG build
→ reachability
→ definite-init fixed point
→ TcTypedProgram
~~~

- [ ] 为同一源码包含多种错误的情况写第一错误阶段断言。
- [ ] tc_analyze 使用局部临时 typed state，全部成功后才移动到 out。
- [ ] CFG 构建或 fixed point OOM 时释放 program、symbols、CFG、bitsets 和队列。
- [ ] 运行 check-analyzer 和 ASan 最小集。
- [ ] 提交建议：refactor: make analyzer pipeline transactional

### Task 4.5：验证 CFG 复杂度与固定点终止

**Files**

- Modify: src/libtc/tc_lib.c
- Modify: src/vm/analyzer/tc_analyzer.c
- Modify: tests/stress/stress_many_ifs.tc
- Add: tests/stress/stress_cfg_loops.tc
- Add: tests/stress/stress_cfg_goto_merge.tc
- Modify: scripts/vm/run_tests.sh
- Modify: tests/unit/runtime/test_cfg.c

- [ ] 构造多层 if/while、多个 continue/break 前驱、大量 slot 和合法向后 goto，不使用指数级路径展开。
- [ ] 在 test_cfg.c 断言节点/边数量与源语句规模线性相关。
- [ ] 用工作队列处理前驱 OUT 变化；bitset 传递函数单调且有限终止。
- [ ] 通过 TC_BENCH=1 分别记录 parse、bind-type、cfg、dataflow，bench 只写 stderr。
- [ ] 运行 stress 过滤测试并确认无轮次上限、栈溢出或超线性内存增长。
- [ ] 提交建议：test: stress cfg fixed-point convergence

**M4 Gate**

- [ ] 所有顺序、if、while、break、continue、goto 和短路边存在唯一 CFG 表示。
- [ ] 固定点对循环可终止，结果只由标准传递函数决定。
- [ ] CFG 构建为 O(nodes + edges)，数据流不枚举路径。
- [ ] static --check 正反例通过；Executor/AOT 尚未执行循环时不得对外宣称完整支持。
- [ ] check-cfg、check-analyzer、check_source_naming 通过。

---

## M5：cast、truncate、bitcast 与浮点语义收敛

### Task 5.1：建立独立共享转换语义模块

**Files**

- Create: src/vm/runtime/tc_sem_cast.h
- Create: src/vm/runtime/tc_sem_cast.c
- Modify: src/vm/runtime/tc_semantics.h
- Modify: src/vm/runtime/tc_sem_int.h
- Modify: src/vm/runtime/tc_sem_int.c
- Modify: src/vm/runtime/tc_sem_fp.h
- Modify: src/vm/runtime/tc_sem_fp.c
- Modify: src/libtc/CMakeLists.txt
- Modify: tests/unit/runtime/test_semantics.c
- Modify: tests/unit/runtime/CMakeLists.txt

**Interfaces**

~~~c
int tc_exec_cast(TcType target,
                 const TcValue *source,
                 TcValue *out,
                 TcDiagnostic *diag,
                 int line);

int tc_exec_truncate(TcType target,
                     const TcValue *source,
                     TcValue *out,
                     TcDiagnostic *diag,
                     int line);

int tc_exec_bitcast(TcType target,
                    const TcValue *source,
                    TcValue *out,
                    TcDiagnostic *diag,
                    int line);
~~~

- strict cast 覆盖整数、浮点、bool 的全部方向。
- truncate 只接受整数到更窄整数。
- bitcast 只接受等宽整数/浮点，bool 不参与。

- [ ] 先写 11 源类型 × 11 目标类型的表驱动 strict cast 测试，逐项标记成功值或 CastOverflow。
- [ ] 补同类型 cast：整数/浮点/bool 恒等；浮点同类型必须保留正负零和完整 NaN payload。
- [ ] 先写 truncate：有/无符号组合、m<n、m==n、m>n、bool/float 非法。
- [ ] 先写 bitcast：32/64 位往返、-0.0、Infinity、NaN payload、最高位、bool 与宽度错误。
- [ ] 运行 check-semantics，确认旧 helper 在无符号窄化、float truncate 或错误名上失败。
- [ ] 实现新模块；内部使用 TcValue 位模式和 memcpy，不使用指针强转。
- [ ] 旧 tc_exec_cast/tc_exec_fp_cast 调用点同一提交迁到新接口，然后删除重复实现。
- [ ] 更新 CMake 与 source naming。
- [ ] 提交建议：refactor: centralize cast and bitcast semantics

### Task 5.2：增加 bitcast Token、RHS 与全部分发点

**Files**

- Modify: src/vm/lexer/tc_lexer.h
- Modify: src/vm/lexer/tc_lexer.c
- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/parser/tc_parser_rhs.c
- Modify: src/vm/parser/tc_parser_free.c
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: src/vm/analyzer/tc_const_eval.c
- Modify: src/vm/executor/tc_executor.c
- Modify: src/aot/tc_aot_codegen.c
- Modify: src/aot/tc_aot_rt.c
- Modify: src/aot/tc_aot_rt.h
- Modify: scripts/sync/check_rhs_coverage.py
- Modify: tests/unit/parser/test_parser.c
- Modify: tests/unit/runtime/test_types.c
- Modify: tests/unit/runtime/test_analyzer.c
- Add: tests/valid/bitcast_roundtrip32.tc
- Add: tests/valid/bitcast_roundtrip64.tc
- Add: tests/errors/static/bitcast_width_mismatch.tc
- Add: tests/errors/static/bitcast_bool_type_mismatch.tc

**Interfaces**

~~~c
typedef struct {
    TcType target;
    TcType source_type;
    int source_type_resolved;
    TcOperand source;
} TcBitcastRhs;
~~~

- TcRhsKind 增加 TC_RHS_BITCAST。
- 标识符源类型来自绑定；字面量源类型严格按标准 §8.1.1 唯一确定。

- [ ] 先写 bitcast lexer/parser 正反例。
- [ ] 先写 Analyzer：bool → TypeMismatch，不等宽 → BitcastWidthError，合法等宽写入 resolved source type。
- [ ] 先写 VM/AOT/let 位模式 fixture。
- [ ] 增加 token、AST payload、runtime/const parser 和 free。
- [ ] 在 8 个 RHS 分发点实现明确 case；未知 kind 不得默认为零。
- [ ] AOT bitcast 复制规范化 slot bits；需要宿主 float 对象时使用 memcpy。
- [ ] 运行：

~~~bash
python3 scripts/sync/check_rhs_coverage.py
bash scripts/run_tests.sh --filter bitcast
cmake --build build --target check-parser
cmake --build build --target check-analyzer
cmake --build build --target check-semantics
~~~

- [ ] 提交建议：feat: implement bitcast across all backends

### Task 5.3：允许 cast 字面量并收敛 truncate 形态

**Files**

- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/parser/tc_parser_rhs.c
- Modify: src/vm/parser/tc_parser_free.c
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: src/vm/executor/tc_executor.c
- Modify: src/aot/tc_aot_codegen.c
- Modify: tests/unit/parser/test_parser.c
- Modify: tests/unit/runtime/test_analyzer.c
- Rewrite: tests/errors/static/cast_literal.tc
- Move/Rewrite: tests/valid/fp_cast_truncate.tc → tests/valid/fp_bitcast_roundtrip.tc

**Produces**

- runtime cast 的 source 从 char * 改为 TcOperand；支持标准规定的字面量源类型。
- cast(T, truncate, operand) 仅在源/目标均为整数且目标更窄时通过。
- 旧 fp_cast_truncate 改写为 bitcast 正例；旧 cast_literal 从错误用例迁为正例或范围错误用例。

- [ ] 先写无后缀 int64、u 后缀 uint64、float32/64、inf/nan、bool 字面量源类型测试。
- [ ] 更新 AST 所有权和 free。
- [ ] Analyzer 保存解析后的 source type，Executor/AOT 不重新猜测。
- [ ] 运行 cast/bitcast 过滤回归与 AOT 差分。
- [ ] 提交建议：feat: align cast operands and truncate rules

### Task 5.4：删除浮点 wrap 可达路径并固定浮点规则

**Files**

- Modify: src/vm/parser/tc_parser_rhs.c
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: src/vm/runtime/tc_sem_fp.c
- Modify: src/aot/tc_aot_rt.c
- Modify: tests/unit/runtime/test_semantics.c
- Move/Rewrite: tests/valid/fp_arith_wrap.tc → tests/errors/static/fp_arith_wrap_mode_mismatch.tc
- Move/Rewrite: tests/valid/fp_wrap_arith.tc → tests/errors/static/fp_wrap_arith_mode_mismatch.tc
- Modify: tests/errors/static/fp_wrap_on_compare.tc
- Add: tests/errors/static/fp_wrap_mode_mismatch.tc

- [ ] 先写 strict/ieee 的异常优先级、NaN 比较、roundTiesToEven、float32 每步舍入测试。
- [ ] 把浮点 wrap 程序迁为 ModeMismatch；需要位操作的示例改用 bitcast → integer wrap → bitcast。
- [ ] TC_FLOAT_WRAP 可暂留为 Parser 非法模式哨兵，但成功 typed program 中不可出现。
- [ ] strict 与 ieee 共享确定的计算路径；no-fenv 回退必须得到同一语言结果。
- [ ] FloatCastOverflow 调用点统一为 CastOverflow。
- [ ] 运行 check-semantics、fp 过滤、no-fenv 专项。
- [ ] 提交建议：feat: converge float strict and ieee semantics

**M5 Gate**

- [ ] strict cast、integer truncate、bitcast 三者边界互不重叠。
- [ ] VM/AOT 对 bitcast 位模式、错误 kind 和退出状态一致。
- [ ] 浮点 wrap 不再进入成功 typed program。
- [ ] check_rhs_coverage.py 与 check_source_naming.py 通过。

---

## M6：let 常量求值与运行时逐操作一致

### Task 6.1：收敛 let 合法形态和名称规则

**Files**

- Modify: src/vm/parser/tc_parser_rhs.c
- Modify: src/vm/analyzer/tc_const_eval.c
- Modify: src/vm/analyzer/tc_const_eval.h
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: tests/unit/runtime/test_analyzer.c
- Rewrite: tests/errors/static/const_cyclic_dep.tc
- Rewrite: tests/errors/static/self_ref_let.tc
- Rewrite: tests/errors/static/forward_reference.tc

- [x] 先写 literal、更早 let、一个非嵌套调用、嵌套调用拒绝、var 引用拒绝、自引用/前向引用 UndefinedVariable 测试。
- [x] 删除依赖 DFS 循环检测才能形成的 ConstantCircular 主路径；源序规则使循环不可形成。
- [x] 允许标准准许的 integer wrap、float ieee、integer truncate 与 bitcast 常量调用。
- [x] 不可把多个调用嵌套在一个 let RHS。
- [ ] 提交建议：feat: enforce single-layer let expressions

### Task 6.2：让 let 复用共享数值核心

**Files**

- Modify: src/vm/analyzer/tc_const_eval.c
- Modify: src/vm/runtime/tc_sem_cast.c
- Modify: src/vm/runtime/tc_sem_fp.c
- Modify: tests/unit/runtime/test_analyzer.c
- Add: tests/valid/let_runtime_equivalence.tc
- Add: tests/valid/let_float32_step_rounding.tc
- Add: tests/valid/let_bitcast_payload.tc
- Modify: scripts/vm/run_tests.sh
- Modify: scripts/aot/run_tests.sh

- [x] 对每类运算建立 let 结果与 runtime 结果的成对 fixture。
- [x] float32 每个操作立即舍入到 binary32；float64 每步为 binary64。
- [x] 运行时错误映射固定为 ConstantOverflow、ConstantDivisionByZero、ConstantCastOverflow 或 ConstantExpressionError。
- [x] AOT 引用 let 时发射十六进制位模式，不发射宿主十进制常量表达式。
- [x] 比较 VM、AOT 和 let 输出的位模式。
- [ ] 提交建议：feat: share runtime semantics with let evaluation

### Task 6.3：把最终 let 结果接回 CFG 常量剪枝

**Files**

- Modify: src/vm/analyzer/tc_cfg.c
- Modify: src/vm/analyzer/tc_analyzer.c
- Modify: tests/unit/runtime/test_cfg.c
- Modify: tests/unit/runtime/test_analyzer.c

- [x] 增加 let true/false、let 比较、非法嵌套和前向 let 的可达性测试。
- [x] 确认剪枝使用规范化 TcValue，不重复用宿主表达式求值。
- [x] 确认不可达语句仍做名称与类型检查。
- [ ] 提交建议：feat: prune cfg with canonical let values

**M6 Gate**

- [x] let 与 runtime 对相同操作得到相同值或对应的常量错误。
- [x] float32/64 位模式在 VM/AOT/let 三方一致。
- [x] ConstantCircular 不再是可达 0.0.31 语言错误。

---

## M7：VM Executor 结构化控制流

### Task 7.1：引入显式执行控制结果

**Files**

- Modify: src/vm/executor/tc_executor.c
- Modify: src/vm/executor/tc_executor.h
- Create: tests/unit/runtime/test_executor.c
- Modify: tests/unit/runtime/CMakeLists.txt
- Modify: tests/CMakeLists.txt
- Add: tests/valid/while_false.tc
- Add: tests/valid/while_counted.tc
- Add: tests/valid/while_nested.tc
- Add: tests/valid/while_break_continue.tc
- Modify: scripts/vm/run_tests.sh

**Interfaces**

~~~c
typedef enum {
    TC_EXEC_NORMAL,
    TC_EXEC_BREAK,
    TC_EXEC_CONTINUE,
    TC_EXEC_GOTO,
    TC_EXEC_ERROR
} TcExecControlKind;

typedef struct {
    TcExecControlKind kind;
    int loop_id;
    int target_stmt_index;
} TcExecControl;
~~~

- [x] 先注册 zero/multi/nested/break/continue fixture，确认 Executor 未处理新 kind。
- [x] block 执行返回 TcExecControl，不通过隐式全局状态传递。
- [x] while 只消费指向自身 loop_id 的 break/continue；goto 与 error 向外传播。
- [x] continue 回到条件求值；break 到 end 之后。
- [x] test-executor 直接链接 libtc；把 check-executor 加入 check-unit，运行 check-executor 与 while 过滤测试。
- [ ] 提交建议：feat: execute structured loops in vm

### Task 7.2：固定槽重初始化与作用域退出

**Files**

- Modify: src/vm/executor/tc_executor.c
- Modify: src/vm/runtime/tc_semantics.c
- Modify: tests/unit/runtime/test_executor.c
- Add: tests/valid/while_var_reinitialize.tc
- Add: tests/valid/goto_var_reinitialize.tc
- Modify: scripts/vm/run_tests.sh

- [x] 验证循环每次执行 var 都覆盖同一 slot，不分配新槽。
- [x] 验证后向 goto 再执行 var 也覆盖同一 slot。
- [x] end、break、向外 goto 的 scope exit 对当前标量模型不泄漏活动状态。
- [x] 在 test_executor.c 中对同一 typed program 连续执行两次，确认每次分配全新 slots，前一次运行值不泄漏。
- [x] 移除依赖未初始化哨兵判定合法性的执行路径；哨兵只可用于调试。
- [ ] 提交建议：fix: enforce fixed-slot execution lifecycle

### Task 7.3：删除 Executor 的名称与 goto 二次解析

**Files**

- Modify: src/vm/executor/tc_executor.c
- Modify: tests/valid/goto_forward.tc
- Modify: tests/valid/goto_nested_out.tc

- [x] Executor 直接使用 Analyzer 持久化的 slot、loop_id 与 target_stmt_index。
- [x] 对 unresolved metadata 使用内部断言或 Implementation 域错误，不映射为语言错误。
- [x] 保持 goto 标签零成本与目标标签后第一语句语义。
- [ ] 提交建议：refactor: consume resolved control metadata in vm

**M7 Gate**

- [x] VM 正确执行零次、多次、嵌套 while 和最内层 break/continue。
- [x] 固定槽与重复运行测试通过。
- [x] 全部静态合法性仍由 libtc/Analyzer 决定。

**M7 验收证据（2026-07-16）**：`check-executor` 15/15、`check-unit` 1457/1457、VM 421/421、ASan 定向 12/12；`while_*` 与 `goto_var_reinitialize` 过滤回归全绿，resolved-slot 名称扰动和 goto 末尾标签边界均有单元回归。0.0.31 总体结论仍保持“不符合”，等待 M8～M10。

---

## M8：AOT 控制流、runtime shim 与差分

### Task 8.1：生成结构化 C99 while

**Files**

- Modify: src/aot/tc_aot_codegen.c
- Modify: src/aot/tc_aot_codegen.h
- Modify: scripts/aot/run_tests.sh

**Produces**

~~~c
for (;;) {
    uint64_t tc_cond_N;
    /* evaluate TC condition, abort on error */
    if (tc_cond_N == 0) {
        break;
    }
    /* body */
}
~~~

- [x] 先把 M7 的合法循环 fixture 加入 AOT 差分，确认 codegen 不识别 kind。
- [x] 条件每轮求值到独立临时值，可能失败的 shim 不嵌入 C 条件。
- [x] 原生 C break/continue 只用于与 TC loop_id 一一对应的最内层循环。
- [x] 生成内部名称只用稳定 stmt_index，不拼接用户标识符。
- [ ] 提交建议：feat: emit structured loops in aot

### Task 8.2：统一 AOT shim 与运行时错误

**Files**

- Modify: src/aot/tc_aot_rt.c
- Modify: src/aot/tc_aot_rt.h
- Modify: src/aot/tc_aot_codegen.c
- Modify: tests/errors/runtime/fp_cast_overflow.tc
- Modify: scripts/aot/run_tests.sh

- [x] 每个可能失败的 helper 都检查返回值，失败后不写目标 slot。
- [x] cast/truncate/bitcast 调用 M5 的共享 semantics。
- [x] tc_aot_abort 打印与 VM 相同的错误名、关键消息和源行。
- [x] 输出/host compiler 失败属于工具错误，不伪装成 TC SyntaxError。
- [ ] 提交建议：refactor: align aot runtime error shims

### Task 8.3：完成 0.0.31 差分矩阵

**Files**

- Modify: scripts/aot/run_tests.sh
- Modify: tests/valid/while_false.tc
- Modify: tests/valid/while_counted.tc
- Modify: tests/valid/while_nested.tc
- Modify: tests/valid/while_break_continue.tc
- Modify: tests/valid/while_var_reinitialize.tc
- Modify: tests/valid/goto_var_reinitialize.tc
- Modify: tests/valid/bitcast_roundtrip32.tc
- Modify: tests/valid/bitcast_roundtrip64.tc
- Modify: tests/valid/let_runtime_equivalence.tc
- Modify: tests/valid/let_float32_step_rounding.tc
- Modify: tests/valid/let_bitcast_payload.tc
- Modify: tests/errors/static/goto_inside_loop.tc
- Modify: tests/errors/static/label_inside_loop.tc
- Modify: tests/errors/static/break_outside_loop.tc
- Modify: tests/errors/static/continue_outside_loop.tc
- Modify: tests/errors/runtime/cast_strict_overflow.tc
- Modify: tests/errors/runtime/fp_cast_overflow.tc

- [x] 比较 stdout 字节、stderr 错误 kind/关键消息、退出状态和 --check 接受集。
- [x] 数值专项额外输出十六进制位模式。
- [x] 覆盖 while、break/continue、范式隔离、CFG 初始化、固定槽、bitcast、cast、float 和 let。
- [x] 生成 C 必须通过 C99 严格警告；不得启用 fast-math。
- [ ] 提交建议：test: add 0.0.31 vm aot differential matrix

### Task 8.4：复核标准 I/O 的精确契约

**Files**

- Modify: src/vm/runtime/tc_io.c
- Modify: src/vm/runtime/tc_io.h
- Modify: tests/unit/runtime/test_io.c
- Modify: tests/valid/fp_io.tc
- Modify: tests/valid/format_spec_all.tc
- Modify: tests/errors/runtime/read_invalid.tc
- Modify: tests/errors/runtime/read_out_of_range.tc
- Modify: scripts/vm/run_tests.sh
- Modify: scripts/aot/run_tests.sh

- [x] 以表驱动测试覆盖 13 种格式符、整数符号/进制、bool 文本、float32/64 的 f/e/E/g/G、换行与不换行。
- [x] 固定 NaN、Infinity、负零和边界整数的可观察文本，并比较 VM/AOT stdout 字节。
- [x] 覆盖 read 的合法输入、EOF、非法文本、范围错误和流失败；运行时统一 IOError。
- [x] read 目标必须已定义、类型一致且在当前 CFG 点确定初始化；它只覆盖现值，不能代替 var 初始化器。
- [x] AOT shim 委托 tc_io，不复制格式化或解析实现。
- [x] 运行 check-io、I/O 过滤回归与 AOT 差分。
- [ ] 提交建议：test: verify 0.0.31 io contract

**M8 Gate**

- [x] 所有新合法程序 VM/AOT 输出一致。
- [x] 所有新 static 程序 VM/AOT --check 结果一致。
- [x] 所有新 runtime 错误 VM/AOT kind、时机和退出状态一致。
- [x] 13 种格式符、read 错误与浮点特殊文本通过共享 I/O 契约。

**M8 验收证据（2026-07-16）**：`make test` 全绿（VM 425/425、unit 1534/1534，其中 `check-io` 124/124、AOT 254/254）；AOT 矩阵覆盖结构化 while、break/continue、循环范式隔离、CFG 重初始化、bitcast/cast/float/let、三组浮点 I/O 输入、静态接受集以及 runtime 诊断首行和退出状态，生成 C 全部通过 `-std=c99 -Wall -Wextra -Werror -pedantic` 且未启用 fast-math。`check_rhs_coverage.py`、`check_source_naming.py` 与 `git diff --check` 均通过。0.0.31 总体结论仍保持“不符合”，等待 M9～M10。

---

## M9：libtc 事务、诊断分域、REPL 与 CLI

### Task 9.1：实现 success-only ownership

**Files**

- Modify: src/libtc/tc_lib.c
- Modify: src/libtc/tc_lib.h
- Modify: src/vm/analyzer/tc_analyzer.c
- Create: tests/unit/runtime/test_libtc.c
- Modify: tests/unit/runtime/CMakeLists.txt
- Modify: tests/CMakeLists.txt

- [x] 先写 compile_source/file 成功所有权、每阶段失败 out 不需释放、source 返回后可释放、typed program 重复运行与重复 AOT 消费测试。
- [x] tc_compile_source 使用局部 TcProgram/TcTypedProgram，全部成功后才移动到 out。
- [x] Parser、Binder、CFG、const eval 任一失败都由 libtc/analyzer 回收临时所有权。
- [x] tc_typed_program_free 同时释放 program、symbols、CFG、warnings。
- [x] test-libtc 直接链接 libtc；把 check-libtc 加入 check-unit。
- [ ] 提交建议：refactor: make libtc compilation transactional

### Task 9.2：完成 API/Environment 错误分域

**Files**

- Modify: src/libtc/tc_lib.c
- Modify: src/vm/runtime/tc_diagnostic.c
- Modify: tests/unit/runtime/test_libtc.c
- Modify: tests/unit/runtime/test_diagnostic.c

- [x] 文件不存在 → API/FileOpen，不是 SyntaxError。
- [x] seek/read 失败 → API/FileRead。
- [x] 无效 API 前置条件 → API/InvalidArgument。
- [x] 分配失败 → Implementation/OutOfMemory。
- [x] 同一源码经 compile_source/file 在语言阶段得到相同 language kind。
- [ ] 提交建议：feat: separate libtc api and language diagnostics

### Task 9.3：固定 REPL 的非规范边界

**Files**

- Modify: src/vm/driver/tc_repl.c
- Modify: src/vm/analyzer/tc_analyzer_repl.c
- Modify: src/vm/driver/tc_driver.c
- Modify: tests/valid/repl_extended_scenarios.txt
- Modify: scripts/vm/run_tests.sh
- Modify: docs/TC-VM命令行参考.md

- [x] REPL 明确拒绝 if、while、goto、label、break、continue 等需要多行/全文件 CFG 的输入。
- [x] 拒绝原因属于 REPL 能力限制，不设置语言 SyntaxError。
- [x] 失败输入不污染已提交变量/常量状态。
- [x] 帮助文本列出限制；批量文件仍走完整 libtc。
- [ ] 提交建议：fix: separate repl limitations from language errors

### Task 9.4：更新 CLI 行为但保持迁移版本号

**Files**

- Modify: src/vm/driver/tc_driver.c
- Modify: src/aot/main.c
- Modify: docs/TC-VM命令行参考.md
- Modify: docs/libtc-api.md

- [x] --help 与 API 参考描述当前分支的实际行为和错误域，但在 M10 前不使用“0.0.31 已实现/兼容”的发布措辞。
- [x] --version 在 M10 前仍输出 0.0.26，避免中间提交冒充发布。
- [x] tc-vm --check 与 tc-aot --check 共用 libtc 接受集。
- [ ] 提交建议：docs: align cli and api migration behavior

**M9 Gate**

- [x] libtc 每个失败阶段无泄漏、无双重释放、out 所有权明确。
- [x] 文件/API/REPL 错误不冒充语言错误。
- [x] typed program 可重复执行和重复 AOT 发射。

**M9 验收证据（2026-07-16）**：`make test` 全绿（VM 435/435、unit 1614/1614，其中 `check-diagnostic` 24/24、`check-libtc` 69/69，AOT 257/257）。`test-libtc` 覆盖 Parse、Binder、const eval、CFG 失败回滚，source/file 生命周期，InvalidArgument、FileOpen、非 seekable FileRead，source/file language kind 一致，以及重复 VM/AOT 消费；诊断故障注入覆盖 source、message、snippet、API message 及 OOM 报告自身的二次分配失败，统一到带免分配固定消息的 Implementation/OutOfMemory。`run_asan_all.sh` 全量通过，审查修复后的 diagnostic/libtc ASan 定向回归通过；MallocScribble 69/69，`leaks --atExit` 报告 0 leaks / 0 bytes。VM/AOT CLI golden 固定帮助、错误流、退出状态和 0.0.26 迁移版本。0.0.31 总体结论仍保持“不符合”，等待 M10。

---

## M10：迁移清理、全量验证、文档与发布

### Task 10.1：删除旧错误与旧语义残留

**Files**

- Modify: src/vm/runtime/tc_types.h
- Modify: src/vm/runtime/tc_types.c
- Modify: src/vm/runtime/tc_sem_fp.c
- Modify: src/vm/analyzer/tc_const_eval.c
- Modify: src/vm/analyzer/tc_analyzer_pass2.c
- Modify: tests/unit/runtime/test_types.c
- Modify: tests/unit/runtime/test_warning.c

**Removals**

- TC_ERR_OVERFLOW_MODE → TC_ERR_MODE_MISMATCH。
- TC_ERR_CONSTANT_CIRCULAR → TC_ERR_UNDEFINED_VARIABLE。
- TC_ERR_FLOAT_CAST_OVERFLOW → TC_ERR_CAST_OVERFLOW。
- TC_ERR_CROSS_BLOCK_REFERENCE → TC_ERR_UNDEFINED_VARIABLE。
- TC_ERR_GOTO_SKIPS_VAR_INIT 不得出现。
- 浮点 wrap 成功路径、浮点 truncate 位重解释、旧 source-order 初始化近似不得可达。

- [x] 用 rg 搜索每个旧枚举、旧错误字符串、TC_FLOAT_WRAP 成功分支和 has_rhs 合法分支。
- [x] 更新 tc_error_kind_name 全表测试，使 41 个语言错误 + OutOfMemory 映射完整且无重复。
- [x] warnings 始终为空。
- [x] 运行 check-types、check-warning 和错误过滤回归。
- [ ] 提交建议：refactor: remove pre-0.0.31 semantic paths

### Task 10.2：完成迁移 fixture 和注册清理

**Files**

- Modify: scripts/vm/run_tests.sh
- Modify: scripts/aot/run_tests.sh
- Modify: .cursor/skills/tc-architecture/test-map.md
- Verify: tests/errors/static/var_missing_initializer.tc
- Verify: tests/errors/static/fp_arith_wrap_mode_mismatch.tc
- Verify: tests/errors/static/fp_wrap_arith_mode_mismatch.tc
- Verify: tests/valid/fp_bitcast_roundtrip.tc
- Modify: tests/errors/static/const_cyclic_dep.tc
- Modify: tests/errors/static/self_ref_let.tc
- Modify: tests/errors/static/forward_reference.tc
- Modify: tests/errors/static/if_cross_block_ref_after_end.tc
- Modify: tests/errors/static/if_cross_block_ref_else_to_then.tc
- Modify: tests/errors/static/if_cross_block_ref_then_to_else.tc
- Modify: tests/errors/runtime/fp_cast_overflow.tc

- [x] 确认 var_no_init 不再位于 valid。
- [x] 确认 fp wrap 用例已迁为 ModeMismatch 或 bitcast 方案。
- [x] 确认 fp_cast_truncate 已改为 bitcast。
- [x] 确认 constant circular/self-reference/forward reference 期望为 UndefinedVariable。
- [x] 确认 cross-block 期望为 UndefinedVariable。
- [x] 确认 float cast overflow 期望为 CastOverflow。
- [x] static 仍为一文件一错；VM/AOT 注册无悬空文件或重复条目。
- [ ] 提交建议：test: finish 0.0.31 breaking migration

### Task 10.3：运行功能与非功能发布门禁

**Files**

- Verify: 全仓库
- Modify only with evidence: docs/设计实现合规审查报告.md

- [x] 正常全量：

~~~bash
make
bash scripts/run_tests.sh
python3 scripts/sync/check_rhs_coverage.py
python3 scripts/sync/check_source_naming.py
~~~

- [x] ASan：

~~~bash
make build-asan
bash scripts/run_tests.sh --asan
~~~

- [x] UBSan：

~~~bash
make build-ubsan
bash scripts/run_tests.sh --ubsan
~~~

- [x] no-fenv：

~~~bash
cmake -S . -B build-nofenv -DTC_FORCE_NO_FENV=ON
cmake --build build-nofenv
cmake --build build-nofenv --target check-semantics
~~~

- [x] 平台可用时运行 macOS leaks 或 Valgrind。
- [x] 检查生成 C 使用 -std=c99 -Wall -Wextra -Werror -pedantic 且零警告。
- [x] 在报告中记录实测 VM/unit/AOT 新总数；不沿用旧数字。

### Task 10.4：同步架构与用户文档

**Files**

- Do not modify for implementation drift: docs/TC语言标准设计说明书_0.0.31.md
- Modify: docs/TC-VM详细设计说明书.md
- Modify: docs/TC-AOT详细设计说明书.md
- Modify: docs/libtc设计说明书.md
- Modify: docs/TC-VM命令行参考.md
- Modify: docs/libtc-api.md
- Modify: docs/设计实现合规审查报告.md
- Modify: .cursor/skills/tc-architecture/features.md
- Modify: .cursor/skills/tc-architecture/test-map.md
- Modify: .cursor/skills/tc-architecture/locations.md
- Modify: .cursor/skills/tc-architecture/errors.md
- Modify: .cursor/skills/tc-architecture/syntax.md
- Modify: .cursor/rules/knowledge-graph.mdc
- Modify when new modules require it: .cursor/rules/compiler-src.mdc
- Modify when new unit targets require it: .cursor/rules/unit-tests-c.mdc

- [x] 三份目标设计从“待实现”改为“已实现”时，逐条链接代码和测试证据。
- [x] 更新 CFG、tc_sem_cast、while 和 bitcast 的模块位置与分发点。
- [x] 更新错误表、语法、测试映射和实测数量。
- [x] 合规报告重新建立 0.0.31 检查项编号，不把 v0.0.26 的 375 项机械追加为新分母。
- [x] 检查相对链接、冲突标记、版本陈述和 Markdown 空白。
- [ ] 提交建议：docs: align implementation with tc 0.0.31

### Task 10.5：最终版本升级与发布提交

**Files**

- Modify: src/vm/driver/tc_version.h
- Modify: src/aot/main.c
- Modify: README.md when it states current capabilities/version
- Modify: README.en.md when it states current capabilities/version
- Modify: docs/设计实现合规审查报告.md

- [x] 只有 Task 10.1–10.4 全部门禁通过后，将 TC_VM_VERSION 和 TC_AOT_VERSION 改为 0.0.31。
- [x] 运行 --version、--help、VM smoke、AOT smoke 与最终全量。
- [x] 确认仓库中不存在“当前实现仍为 v0.0.26”的过时陈述；历史基线记录保留并明确标注历史。
- [x] 确认没有未注册的新 fixture、未覆盖 kind、未配对源文件或未释放对象。
- [ ] 最终提交建议：release: tc compiler 0.0.31

**M10 / Release Gate**

- [x] 语言与 IR：强制 var 初始化、while/break/continue、范式隔离、bitcast、全部分发完成。
- [x] 静态分析：完整 CFG、可达性、常量剪枝、固定点确定初始化完成。
- [x] 数值：strict cast、integer truncate、bitcast、float strict/ieee、let 每步精度完成。
- [x] 后端：VM/AOT/let 在值、位模式、错误和 I/O 上一致。
- [x] API：所有权、失败回滚、OOM、诊断域和 REPL 边界完成。
- [x] 验证：正常、ASan、UBSan、no-fenv、结构检查和平台内存检查通过。
- [x] 文档：六份配套文档、架构索引、测试映射和合规报告同步。
- [x] 版本：最后一步才升级到 0.0.31。

**M10 验收证据（2026-07-16）**：最终版本为 `tc-vm 0.0.31` / `tc-aot 0.0.31`；版本切换后全量通过（VM 435/435、unit 1617/1617、AOT 257/257）。ASan、UBSan 全矩阵通过；no-fenv `check-semantics` 494/494；生成 C 全部以 `-std=c99 -Wall -Wextra -Werror -pedantic` 编译。MallocScribble 完整矩阵通过，系统 `leaks --atExit` 对 libtc 69/69 所有权套件报告 0 leaks / 0 bytes。`check_rhs_coverage.py`、`check_source_naming.py`、`git diff --check`、旧错误/`has_rhs` 零命中与新 fixture 注册审计均通过。0.0.31 合规报告重新编号为 48 项并达到 48/48。

---

## 4. 必测用例矩阵

### 4.1 Lexer / Parser

- while、break、continue、bitcast 关键字与标识符边界。
- while then 行结束、独立 end、空 body、嵌套 if/while。
- R1–R7：空格/Tab 单一策略、直接子语句恰好多一级、else/end 对齐、label 不开块。
- var 缺等号、缺 RHS 固定 VarMissingInitializer。
- cast/bitcast 字面量源类型与非嵌套调用。

### 4.2 Scope / Control

- while 内 shadow、嵌套同名、end 后不可见。
- break/continue 绑定最内层 while。
- 循环外 break/continue。
- while 的直接或间接子块内 goto/label。
- goto 平级、向外、子块、兄弟块、同名标签和未定义标签。

### 4.3 CFG / Definite Initialization

- 顺序、if 有/无 else、diamond merge。
- while 零次、正常回边、continue 回边、break 出口。
- while true/false 常量剪枝。
- goto forward/backward/向外及 label 后第一语句。
- goto 跳过未使用 var 合法；跳过后可达读取非法。
- 循环体内初始化不自动进入循环后集合。
- 短路右操作数可达/不可达。
- 不可达语句仍做名称和类型检查。

### 4.4 Numeric / let

- strict integer 全符号/位宽转换矩阵。
- integer → float、float → integer、float32 ↔ float64、bool ↔ 数值。
- truncate 仅整数窄化，等宽/加宽/float/bool 均拒绝。
- bitcast 32/64 位往返、-0.0、Infinity、NaN payload、最高位。
- float strict/ieee、异常优先级、NaN 比较、逐操作舍入。
- let 单层调用、更早 let、前向/自引用、runtime 等价、AOT 位模式。

### 4.5 Ownership / Tooling

- Parser/Analyzer/CFG/const eval 每个 OOM 点回滚。
- compile 失败 out 无所有权；成功只转移一次。
- typed program 重复 run、重复 emit、最终 free 一次。
- 文件 open/read 错误为 API 域。
- REPL 限制不污染语言 SyntaxError。
- host compiler 缺失/失败为工具错误。

---

## 5. 风险与控制

| 风险 | 早期信号 | 控制措施 |
| ---- | -------- | -------- |
| CFG 与现有 stmt_index 不一致 | goto/label 目标偏移、AOT 标签错位 | CFG 节点保留 stmt_index；新增 test-cfg 与 test-stmt-index 交叉断言 |
| while 作用域与固定 slot 混淆 | 每迭代分配、旧值泄漏 | Pass1 每绑定只分配一次；重复运行和 reinitialize fixture |
| 常量剪枝改变错误顺序 | 不可达代码漏报名称/类型错误 | 先完成 shape/name/type，再剪边和做 dataflow |
| 浮点依赖宿主扩展精度 | VM/AOT/let 位模式不同 | 每步显式目标精度；位模式测试；fenv/no-fenv 双路径 |
| bitcast 触发严格别名 UB | sanitizer 或优化构建差异 | slot 位复制或 memcpy；禁止指针强转 |
| 诊断枚举迁移造成旧路径漏改 | 旧 kind 仍被 rg 命中 | M10 建立零命中清单 + tc_error_kind_name 全表测试 |
| 中间提交误报 0.0.31 | --version 或文档提前升级 | 版本号冻结到最终 gate；合规报告保持不符合结论 |
| 测试数字失真 | 文档数字与脚本不一致 | 所有总数由最终脚本输出回填 |
| 新模块构建覆盖不全 | unit 通过但 libtc/AOT 缺对象 | 同步 libtc CMake、unit CMake、source naming 与全量构建 |

---

## 6. 完成定义

以下条件同时满足，开发计划才算完成：

1. TC 0.0.31 标准中的全部强制规则均有实现位置、正例、反例和后端一致性证据。
2. 41 个语言错误与 OutOfMemory 映射完整；旧错误不可达；API/Environment 错误已分域。
3. typed program 是 VM 和 AOT 的唯一静态合法性来源，CFG 与解析控制目标不在后端重复推导。
4. VM、AOT 和 let 对数值、位模式、错误时机与 I/O 的结果一致。
5. 正常、ASan、UBSan、no-fenv、RHS coverage、source naming 和平台内存检查全部通过。
6. 文档与实测数字同步，合规报告建立新的 0.0.31 检查项并全部通过。
7. 最后才把 VM/AOT 版本升级到 0.0.31，并完成发布提交。

---

*本计划是 0.0.31 的执行路线，不改变标准本身。实施过程中若代码结构必须调整，可替换内部算法或私有数据结构，但阶段边界、可观察语义、诊断分工与发布门禁不得弱化。*
