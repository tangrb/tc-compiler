# TC 0.0.46 S3 门禁报告（G3）

> **阶段**：S3 名称、类型、转换与布局（对应设计 §13 P1 的 SEM 部分）
> **依据**：[开发测试计划](../TC开发测试计划-0.0.46.md) §5 S3、§7、附录 D；
> [标准](../TC语言及编译器标准-0.0.46.md) §3/§4.5–§4.8/§5/§6.1–§6.15/§8.6/§12/§13.2/§13.3；
> [设计](../TC原生编译器设计说明书-0.0.46.md) §2/§3/§5/§6/§14。
> **执行方式**：agent 执行；独立只读审计由 `gate-auditor` 执行（非作者、未参与实现）。
> **结论**：**G3 PASS；允许进入 S4。**（独立审计结论见 §10；若审计发现未关闭，以 §10 为准。）

## 1. 结论

| 项 | 结果 |
| ---- | ---- |
| S3 开发交付 | **PASS** — 名称/命名空间/可见性、类型表 intern+冻结、字面量定型、转换矩阵、结构体布局/`sizeof` 事实、指令形状/类型、32/64 实例化 |
| S3 测试活动 | **PASS** — L1 `tc_sem_test` 58 checks；L3 160 个 SEM/翻转用例 + 45 个 import/库夹具 + 34 个接受工具用例（含 B1-5/B1-6 回归）；`determinism.s2s3` |
| 32/64 翻转矩阵 | **PASS** — 覆盖矩阵 `flip32_64` 34/34；32/64 分档案运行，不跨档比较 |
| 布局归属 | **PASS** — 布局在 SEM 计算；`LAYOUT_OVERFLOW` 为 SEM 码；未进入 CT |
| 类型表冻结 | **PASS** — Analyze 完成后 `tc_type_table_freeze`；`TC-UNIT-TYPE-0001`/`TC-UNIT-CONV-0001@w32/@w64` |
| 独立门禁审计 | **PASS** — 首轮 4 个 B1/B2 修复后复核 PASS；门禁后 B1-5 与全面自检 B1-6 批次均复核关闭（§10.4/§10.5） |
| **G3** | **PASS** |
| 进入 S4 | **允许** |

## 2. 范围与依据

本阶段只实现并验证**名称、类型、转换与布局**：

- 名称与命名空间：值/类型/标签分离；模块成员（struct/const/static/func）共用命名空间；`Self`/`m`；裸名值位置规则；`FUNCTION_SCOPE_ACCESS` 与 `TYPE_MISMATCH` 区分；`DUPLICATE_MEMBER`/`DUPLICATE_PARAMETER`/`DUPLICATE_NAME`；`private`/`public` 与 `PRIVATE_TYPE`（标准 §3.9/§4.6/§4.8/§5.1/§5.6）。
- 类型表：标量单例、复合 intern、`ptr<T>`、`fnptr` 逐项签名、struct 名义等价；Analyze 入口创建，Pass1/Pass2 intern，完成后冻结；W 相关布局不缓存在 `TcType`（标准 §3.9/§3.13.1；设计 §2/§2.2）。
- 字面量：普通上下文按期望类型；`cast`/`trunc`/`bitcast` 源类型按标准 §6.6.3；无后缀/`u`/负值符号性与范围；`inf`/`nan`/`nullptr`/bool；LEX 与 SEM 分工（标准 §2.4/§2.7.3/§6.6.3）。
- 转换：`cast` 数值/bool；`trunc` 仅整数且目标更窄；`bitcast` 类别矩阵先于位宽；W 位宽；禁止项（标准 §3.11/§6.6/§13.2.2）。
- 结构体与布局：字段顺序/唯一/非空；`readonly`/`var`；`@padding(N)` 形式与 N；紧凑字段 + 0x00 padding + 无隐式对齐/尾填充；求和按 W 位 `usize`；自引用/前向引用；`sizeof` 类型参数；字段访问/赋值路径；`addr const`（标准 §3.7/§3.8/§6.13/§6.14/§13.3）。
- 指令形状与类型：按标准 §6.2 的 opcode 规则检查 arity/shape、类型参数、模式矩阵、`.nsw/.nuw` 的 `@fast` 约束、无结果 LHS（含 var 初始化器形态）、`make`/`call` 实参/字段类型检查（标准 §6.1/§6.2/§6.15/§10.2）。
- 32/64：`isize`/`usize`/`ptr`/`fnptr` 宽度、`bitcast`/`trunc`、字面量范围、`@padding`/布局求和、`@align`/`@range` 参数范围（标准 §13.2.1/§13.2.2）。

不在本阶段范围：CFG/DI/返回末尾/调用顺序/安全/完整注解矩阵（S4）、CT/const 求值/IR 最终化（S5）、VM/AOT（S6+）。最终 `TcInstr`/`TcIR` 物化属于 IR 最终化；SEM 提供并冻结其所需的类型/名称/opcode/布局事实。

## 3. 环境与可重复性

| 项 | 值 |
| ---- | ---- |
| 宿主 | macOS arm64（Apple Silicon） |
| C 编译器 | Apple clang 21.0.0.21000334 |
| CMake | 4.4.2 |
| Python | 3.14.7（仅标准库，无网络） |
| 构建 | Debug、ASan+UBSan |
| 档案 | 64 位默认（`aarch64-linux-user`）；32 位静态档案 `w32`（`tc_profile_init_generic_w32`，不可执行目标） |
| 固定环境 | `LC_ALL=C`、`LANG=C`、`TZ=UTC`；无网络 |
| 比较规则 | 32/64 分档案保存/运行；不做跨档案静态结果比较 |

## 4. 交付物与证据

| S3 交付 | 位置 | 证据 |
| ---- | ---- | ---- |
| 名称/成员/可见性 | `src/frontend/sem.c` | `TC-SEM-NAME-*`、`TC-SEM-ASSIGN-*`、`TC-SEM-FUNC-*` |
| 类型表 intern/冻结 | `src/frontend/sem.c`；`src/ir/type.c` | `TC-UNIT-TYPE-0001`、`TC-UNIT-LAYOUT-0001`、`TC-UNIT-CONV-0001` |
| 类型等价 | `src/frontend/sem.c` | L1 标量/`isize`/`ptr`/`fnptr`/名义 struct；L3 `TC-SEM-TYPE-*` |
| 字面量定型 | `src/frontend/sem.c` | `TC-SEM-LIT-*`、`TC-TOOL-SEM-0003` |
| `cast`/`trunc`/`bitcast` | `src/frontend/sem.c` | `TC-SEM-CONV-*`、`TC-TOOL-SEM-0002/0015/0021/0022` |
| 结构体/布局/`sizeof` | `src/frontend/sem.c`；`src/ir/structs.h` | `TC-SEM-FIELD-*`、`TC-SEM-LAYOUT-*`、`TC-SEM-CONST-*`、`TC-UNIT-LAYOUT-0001` |
| 指令形状/类型 | `src/frontend/sem.c` | `TC-SEM-OP-*`、`TC-SEM-MODE-*`、`TC-SEM-FIELD-*` |
| 32/64 静态档案 | `src/runtime/profile.{h,c}`、`src/driver/profile_select.c` | `w32`；`TC-TOOL-SEM-0009/0010/0011…0018`；L1 layout |
| 覆盖矩阵 | `tests/traceability/coverage.*` | `flip32_64` 34/34、`first_diag_rule` 10/10、S3 码槽 covered |
| 报告/文档 | 本报告、`src/CONTRACTS.md`、`src/README.md`、`tests/*/README.md` | 范围/归属/状态同步 |

## 5. G3 出口准则（计划 §5 S3）

1. **类型等价、转换矩阵、字面量上下文的每条规则均有正例/反例**：**PASS**。等价（L1 + `TC-SEM-TYPE-*`）、`bitcast` 6 类矩阵代表项（`TC-SEM-CONV-0001…0005/0020…0024`、`TC-TOOL-SEM-0002/0015/0021/0022`）、`trunc` 窄化与等宽/加宽（`TC-SEM-CONV-0002/0006…0009/0014…0019`）、字面量上下文（声明/copy/make/call/return/write/read/br_if/switch，`TC-SEM-LIT-*`、`TC-TOOL-SEM-0003/0011/0012`）。
2. **标准 §13.2.2 的 32/64 翻转行全部有对应用例，且分档案期望正确**：**PASS**。17 行 × w32/w64 在覆盖矩阵全部 covered；行 1–3 由 L1 布局尺寸，行 4–16 由 `TC-SEM-CONV/LIT/LAYOUT/ANN-*` 与 `TC-TOOL-SEM-0011…0018` 覆盖。行 17 的**静态** isize/usize 类型与 `read`/`write` T 由 `TC-TOOL-SEM-0011/0012` 覆盖；其运行时输入范围/trap 属 S6。
3. **布局在 SEM 计算，`LAYOUT_OVERFLOW` 属 SEM，不进入 CT**：**PASS**。`resolve_structs` 在 Analyze 内计算 offset/size；`TC-SEM-LAYOUT-0001/0002` 断言 stage=SEM；CT 尚未实现。
4. **类型表 Analyze 后冻结，执行器/AOT 不重新解析**：**PASS**。`tc_type_table_freeze` 在全部 SEM 检查后调用；`tc_analysis_type_table_frozen` 断言；`TcIR`/VM 尚未实现，冻结表是唯一后续入口。
5. **所有 S3 相关码（名称/类型/转换/布局/字段/void）覆盖**：**PASS**。`UNDEFINED_NAME`、`DUPLICATE_NAME/MEMBER/PARAMETER`、`FUNCTION_SCOPE_ACCESS`、`UNDEFINED_FUNCTION`、`PRIVATE_ACCESS/TYPE`、`TYPE_MISMATCH`、`LITERAL_TYPE/RANGE`、`MODE`、`ANNOTATION`（本阶段 W 参数部分）、`OPERAND_COUNT`、`VOID`、`UNKNOWN/MISSING/DUPLICATE_FIELD`、`FIELD_ORDER`、`STRUCT_SELF_VALUE`、`EMPTY_STRUCT`、`LAYOUT_OVERFLOW`、`IMMUTABLE_FIELD`、`CONST_ASSIGNMENT`、`PARAMETER_ASSIGNMENT`、`ADDR_OF_CONST`、`BITCAST_WIDTH`、`TRUNC_WIDTH`、`CONST_FORM` 均有 trigger/position/stage 用例；未覆盖的 code 槽属 S4/S5/S6（见 §9）。

## 6. 附录 D.1 通用门禁

| 检查项 | 结果 | 依据/范围 |
| ---- | ---- | ---- |
| 80 码表未增删 | **PASS** | `trace.codes`/`trace.gen_c_table`/`trace.harness_codes` |
| 用例有 `spec` 与可追溯 | **PASS** | `s2s3_cases.py` 每项 `spec`；生成器 `--check` |
| 覆盖矩阵更新；未覆盖解释 | **PASS** | 245 covered / 389 uncovered / 0 suspended；S3 行与 flip 全绿 |
| 首诊/位置断言 | **PASS** | 每个 S3 用例断言 exactly-one + primary/related；`§12.2 #4/#9/#10` 的 S3 部分 |
| 32/64 分档案 | **PASS** | `profile: w32/w64`；L1 双 W；无跨档比较 |
| 确定性套件 | **PASS** | `determinism.s2s3`；哈希种子/顺序扰动 |
| 无新增警告/标准外诊断 | **PASS** | `-Wall -Wextra -Werror -Wpedantic`；80 码闭集 |
| 构建/消毒器/符号审计 | **PASS** | Debug + ASan+UBSan `ctest` |
| 报告记录工具链/环境 | **PASS** | §3 |

## 7. 附录 D.2 S3 附加检查

| 附加检查 | 结果 | 证据 |
| ---- | ---- | ---- |
| 类型表冻结 | **PASS** | `tc_type_table_freeze`；`tc_analysis_type_table_frozen` |
| `bitcast` 矩阵 | **PASS** | 类别先于位宽；禁止项 `TYPE_MISMATCH`、允许但不等宽 `BITCAST_WIDTH` |
| 32/64 翻转 | **PASS** | `flip32_64` 34/34 |
| 布局属 SEM | **PASS** | `resolve_structs`；`LAYOUT_OVERFLOW` stage=SEM |
| 单类型事实源 | **PASS** | `TcType*` 共享；`TcStructDef` 按 profile 计算；无裸标签写回 |
| 无隐式转换 | **PASS** | `TC-SEM-TYPE-0001`、`TC-SEM-LIT-*`、`TC-SEM-CONV-*` |

## 8. 测试与覆盖证据

### 8.1 L1（`sem.unit`，58 checks）

| 用例 | 内容 |
| ---- | ---- |
| `TC-UNIT-LAYOUT-0001@w32/@w64` | 紧凑字段、`@padding(3)`、offset/size；ptr/fnptr/isize/usize 尺寸 |
| `TC-UNIT-TYPE-0001` | 标量单例、`ptr` intern、名义 struct、`isize`≠`int64`、fnptr 签名 |
| `TC-UNIT-CONV-0001@w32/@w64` | `bitcast int32,isize` 的 W 翻转；类型表冻结 |
| `TC-UNIT-MODULE-0001/0002` | 钻石 order；同名不同路径实例 |

### 8.2 L3

`conformance.s2s3_cases`/`harness.conformance` 共 239 个 S2/S3 用例通过（0 fail / 0 not_implemented）：

- 160 个 SEM/翻转用例（含 32/64 分档案期望、B1-5 的 10 个 var 初始化器形态与命名/位置回归）；
- 45 个 import/库夹具用例；
- 34 个接受工具用例（`--static-only`，含 include roots、`w32`/`w64` 合法翻转、`fnptr<void>`、`padding(0)`、深度只读、跨模块/钻石布局）。

### 8.3 覆盖

| 类别 | 覆盖 |
| ---- | ---- |
| `flip32_64` | 34/34 |
| `first_diag_rule` | 10/10 |
| S3 code 行 | 名称/类型/转换/布局/字段/void 相关槽全绿；CT 6 码与 RT 15 码保持未覆盖 |
| S4/S5/S6 行 | 显式未覆盖，不是 S3 阻塞项 |

## 9. 范围说明与已知限制（不是 G3 阻塞项）

- **最终 IR 物化**：`TcInstr`/`TcIR` 在 IR 最终化阶段生成；S3 完成解析/校验/类型表冻结，提供 opcode/类型/布局事实。G3 出口准则未要求最终 IR 冻结（属 S5）。
- **CT/`sizeof` 值折叠**：`sizeof T` 的结果类型与布局在 SEM 决定，但常量值折叠/`const` 求值属 CT；失败 const 不进入代码生成。
- **CFG/DI/返回末尾/调用顺序/安全/注解**：属 S4。S3 只做指令形状/类型与 `make`/`call` 的实参类型顺序；`MISSING_RETURN`、`UNINITIALIZED`、`UNSAFE_*` 等保持未覆盖。
- **注解矩阵**：`@align`/`@range` 的 W 范围部分在 S3 覆盖；完整位置/互斥/承诺语义属 S4。`@inline`/`@noinline` 等不影响 S3 接受集。
- **静态存储**：`static var` 实例与初值属 S5；S3 只校验声明类型/初始化器类型。
- **w32**：静态分析档案，不是可执行 ABI 目标；32 位 ABI/后端属 S9。
- **翻转行 17 的运行时范围**：`read`/`write` 的 isize/usize 输入范围与 `TC_RE_IO` 属 S6；S3 覆盖其静态类型/W 宽度。翻转行 4/5 的运行时地址值/溢出同属 S6。
- **O-1 const 初始化器形态**：`const c: int32 = trap`/`copy` 等非法 const 形态属 S5 `CONST_FORM`；S3 只做名称/类型检查，不越权报告 SEM 形态码。
- **O-2 static var 初值**：`static var` 只能用 const 初始化器、初值写入静态区属 S5；S3 只校验声明类型与初始化器类型。
- **O-3 标签解析**：`br`/`switch` 未知标签属 S4 `LABEL_NOT_FOUND`；S3 只校验标签操作数形状与分支条件/选择器类型。
- **D-1 报告措辞**：本报告已把“opcode 表驱动”改为“按 §6.2 opcode 规则检查”，与 `switch` + `tc_opcode_has_result()` 的实现一致。
- **D-3 模块拆分**：`sem.c` 已超过 3600 行；S4 期间按名称/类型/指令/布局拆分，属可维护性工作，不是 G3 语义门禁项。

## 10. 独立审计

独立只读代理 `gate-auditor`（非作者、未参与实现、未修改工作树）从标准/设计独立推导，运行完整 `ctest`、`s2s3_cases.py --check`、`coverage.py --check`，并用 `/tmp` 下的临时 `.tc` 探测。

### 10.1 首轮结论

**G3 FAIL**：4 个 B1 规范偏差 + B2 覆盖缺口。

| 编号 | 发现 | 规范依据 |
| ---- | ---- | ---- |
| B1-1 | 结构体布局按 `module_order` 而非导入 DAG 逆向拓扑，跨模块结构体字段被误报 `UNDEFINED_NAME`；后续发现钻石交叉边仍失败 | §1.8/§3.7/§4.11.2.6/设计 §2.1 |
| B1-2 | public 函数体内的 private 类型被误报 `PRIVATE_TYPE` | §4.6（public 位置只含签名/字段/public const/static 类型或初始化器） |
| B1-3 | 裸名模块 `static var` 作为赋值目标被接受，应 `FUNCTION_SCOPE_ACCESS` | §5.6/§6.14 |
| B1-4 | 字段路径先检查 readonly 再检查后续字段存在性，应相反 | §5.7.3 步骤 3→4/§12.2 #5 |
| B2 | 缺跨模块 struct 字段、private body、裸 static 目标、字段顺序组合、完整 6×6 bitcast 矩阵、isize/usize 同宽不同型、局部名遮蔽、W64 布局边界等用例 | 计划 §5 S3/§7.3 |

### 10.2 修复

- **B1-1**：新增 `layout_order()`，对导入 DAG 做 DFS 后序（依赖先于使用者，含钻石交叉边）；模块内仍按源序。诊断仍按 `module_order` 选择。
- **B1-2**：public 函数体 `public_position = 0`；public 签名/字段/public const/static 初始化器保持 public 位置。
- **B1-3**：裸名 `static var` 赋值目标报 `FUNCTION_SCOPE_ACCESS`；`Self.V`/`m.V` 仍合法。
- **B1-4**：`resolve_fields` 先完成整条路径的存在性/中间结构体检查，再单独做 readonly 路径检查。
- **B2**：新增跨模块 struct 字段（线性 + 钻石交叉边）、public 函数体 private 类型、裸 static 目标、字段存在性/readonly 顺序、36 格 bitcast 类别矩阵、4 个 isize/usize 同宽不同型用例、局部名遮蔽、W64 布局边界等；`TC-TOOL-SEM-0025…0029`、`TC-SEM-BITCAST-0001…0036`、`TC-SEM-TYPE-0101…0104`、`TC-SEM-FIELD-0011`、`TC-IMP-0008`。

### 10.3 复核

修复后 `gate-auditor` 重跑：

- 原 4 个 B1 复现全部通过；钻石交叉边复现 `exit 0`；同模块前向引用仍正确 `UNDEFINED_NAME`（无过度接受）；
- `ctest` 19/19；harness 290/290 pass、0 fail；
- `s2s3_cases.py --check`：126 sem/flip、28 import、29 tool；
- `coverage.py --check`：矩阵为当前版本（389/634 未覆盖属 S4–S10，不是 G3 规范偏差）。

**复核结论：G3 PASS，无残留 B1/B2/B3/B4。**

### 10.4 门禁后补充发现 B1-5（已关闭）

门禁报告提交后，追加审计发现一个 S3 B1：

| 项 | 内容 |
| ---- | ---- |
| 现象 | 无结果指令作 `var` 初始化器被接受：`var t: int32 = store int32, p, 1`、`= trap`、`br`、`br_if`、`switch`、`memmove`、`write`、`writeln`、`store.volatile` |
| 依据 | §6.15.1 无结果指令的 LHS 必须是 `_`；var 初始化器没有 `_`，应 `TC_CE_OPERAND_COUNT`（测试计划附录 A.3 将该码首测阶段列为 S3） |
| 根因 | `check_instr` 把 `dst_base == NULL` 当作丢弃目标；该形态只用于 var/const 初始化器，语句形态 `x = store …` 会正确报错 |
| 修复 | `check_instr` 增加 `implicit_result` 参数：var 初始化器显式声明“该指令是值位置”；无结果指令在此形态直接 `OPERAND_COUNT`，不再进入值类型检查 |
| 回归 | `TC-SEM-OP-0004…0012`（#program 全部 9 种形态）与 `TC-SEM-OP-0101`（#lib 函数体）；语句形态 `TC-SEM-OP-0002` 保持通过；`TC-TOOL-SEM-0001` 等正常 var 初始化器不受影响 |
| 覆盖 | `TC_CE_OPERAND_COUNT` 的 trigger/position/stage 行与 `chapter:§6:negative` 已并入新用例；矩阵仍为 245 covered / 389 uncovered（无新增行） |

`const` 初始化器中的无结果指令保持 S5 `CONST_FORM` 范围（见 §9 的 O-1/O-2），`br`/`switch` 未知标签保持 S4 `LABEL_NOT_FOUND` 范围（O-3），未在本修复中越权报告。

修复后 Debug/Release/ASan+UBSan `ctest` 19/19、harness 346/346、S2/S3 239/239。同一只读审计代理复核：9 种无结果 var 初始化器均在助记符处报 `OPERAND_COUNT`；`_ =` 形态、`read`/`copy`/`add` 初始化器与显式目标语句保持正确；**G3 PASS，无残留**。

### 10.5 全面自检追加（B1-6 批次，已关闭）

B1-5 关闭后，按“全面自检、直至满足 S4 进入条件”的要求，另起三个独立只读探测（names/visibility/layout、literals/conversions/W、instruction shapes/driver），发现以下 S3 偏差：

| 类别 | 现象 | 修复/回归 |
| ---- | ---- | ---- |
| 命名操作数 | 固定位置（类型/值/标签/函数/return 等）接受 `foo:` 命名操作数并 exit 0 | 非 `call`/`call_indirect`/`make` 指令在任何检查前拒绝命名操作数；`call`/`make` 目标与 `call_indirect` fp 单独拒绝；`OPERAND_COUNT` 定位助记符 |
| `OPERAND_COUNT` 位置 | 操作数形状错误定位到操作数而非助记符 | `Sem.mnemonic_span` + 所有 `OPERAND_COUNT` 发射点统一到助记符 |
| private 非函数目标 | `call/funcaddr lib.P/lib.K/lib.V` 报 `UNDEFINED_FUNCTION` | 先检查可见性，再检查成员种类 → `PRIVATE_ACCESS`（§4.11.3） |
| private 非类型成员在类型位置 | `sizeof lib.K` 报 `TYPE_MISMATCH` | 导入 private 成员在类型位置 → `PRIVATE_ACCESS`；private struct 在公开位置仍 `PRIVATE_TYPE` |
| 裸模块 const 赋值 | #lib 内 `K = …` 报 `CONST_ASSIGNMENT` | 裸模块 const/static 赋值目标 → `FUNCTION_SCOPE_ACCESS`；`Self.K` 仍 `CONST_ASSIGNMENT` |
| `Self.S` 类型参数 | `sizeof/make/copy/load/store/addr Self.S` exit 0 | `Self` 是关键字、不是 `struct_name` 的 `IDENT`；类型参数中的 `Self.*` → `OPERAND_COUNT` |
| 限定缺失类型成员 | `sizeof lib.NOPE` 报 `TYPE_MISMATCH`，与 `Self.NOPE` 不一致 | 限定成员未命中 → `UNDEFINED_NAME`（位置成员名），不再看同名局部 |
| 混用位置/命名 | `make`/`call` 混用先报 `FIELD_ORDER`/`ARGUMENT_ORDER`，抢在重复/未知/缺失/数量之前 | 混用也按重复→未知→缺失→数量→顺序；覆盖完整时才报顺序码；**D-4 裁定**：混用形态定位到首个带名实参/字段（§12.5.1 的“实参名/字段名”字面；`TC-SEM-FIELD-0008`、`TC-SEM-OP-0112`） |
| 调用检查顺序 | `call` 先解析 LHS、先检查实参，导致 `VOID`/`UNDEFINED_FUNCTION` 被覆盖 | 目标存在性/可见性 → fp 类型 → LHS/返回形态 → 实参；void 调用 LHS 非 `_` 先报 `VOID` |

回归用例：`TC-SEM-OP-0013…0028`（命名/位置/混用）、`TC-SEM-OP-0102…0111`（调用顺序/混用/命名目标）、`TC-SEM-NAME-0109…0113`（private、裸 const、Self 类型参数、限定缺失成员）。

三个探测的其余 PASS 结论：名称/可见性/重复、跨模块/钻石结构体布局、padding/溢出、私有类型公开位置、36 格 bitcast 矩阵、cast/trunc W 翻转、字面量全部上下文、模式矩阵、全部 51 opcode 的 arity、无结果 LHS（语句与 var 初始化器）、字段路径、驱动退出码/阶段/位置均未发现新的 S3 偏差。

同一只读审计代理对 B1-6 批次复核：命名/位置、private 非函数/非类型、裸 const 目标、`Self.S`、混用顺序、调用顺序全部通过；`ctest` 19/19、harness 346/346；**G3 PASS，无 B1/B2 残留**。四项未决歧义均判定不阻塞 S3→S4；D-4 混用定位已裁定并锁入回归，不再列为未决：`call_indirect nullptr` 与 `.nsw` 优先级在 S4 固定，`-0` 选择已记录，`TcField.padding` 在 S5/IR 冻结前解决。

**未决但非 S3 阻塞的歧义（记录，不伪装为已解决）**：

- `call_indirect nullptr`：当前静态拒绝 `LITERAL_TYPE`；§8.8.2/§6.9.5 可将空函数指针留到运行时 `TC_RE_NULL_FUNCTION_POINTER`，但无类型参数无法唯一确定签名。S4 需结合 `fnptr` 期望类型规则裁定。
- `-0` 用于无符号类型：当前按“显式负号”报 `LITERAL_TYPE`；若按数学值 0 解释则可接受。S4 与标准澄清后固定。
- `.nsw/.nuw` 在非 `@fast` 且类型/符号也不匹配：当前 `ANNOTATION`；`MODE` 也有一读依据。S4 固定首诊。
- `@padding(N)` 的 `N > UINT32_MAX`：布局 `size`/`offset` 按完整 N 正确计算，但冻结的 `TcField.padding` 为 `uint32_t`，元数据被截到 `UINT32_MAX`；属 S5/后端使用该字段前的内部契约限制，不影响 S3 语言可见结果。S5 需用 `size/offset` 或复审契约。

## 11. 门禁判定

**G3 PASS（含 B1-5 与 B1-6 批次勘误关闭）。** 类型/转换/布局、类型表冻结、32/64 翻转矩阵、S3 相关诊断、命名操作数形状、`OPERAND_COUNT` 位置与调用/LHS 顺序均满足计划 §5 S3 与附录 D.2 S3 附加检查。首轮独立审计的 4 个 B1 与 B2 缺口、B1-5、以及全面自检 B1-6 批次均已关闭并复核通过；无未关闭的 S3 阻塞性 B1/B2/B3。§10.5 的 4 项歧义/内部契约限制已显式记录，移交 S4/S5，不作为 S3 通过依据。允许进入 S4。

## 附录 A. 复现命令

```sh
cmake -S . -B build && cmake --build build -j8
ctest --test-dir build --output-on-failure
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/traceability/coverage.py --check
ctest --test-dir build -R 'sem.unit|conformance.s2s3_cases' --output-on-failure
ctest --test-dir build -R 'determinism.s2s3|harness.conformance' --output-on-failure
```

## 附录 B. reviewed commit / 证据版本

- reviewed commit：`b7b9704`（S2/S3 实现）+ `3298cae`（B1-5 修复）+ `3508f1d`（B1-6 批次修复）+ `b94818b`（D-4 定位裁定）；基线 `3a12ab3`。
- 独立审计：`gate-auditor` 首轮 FAIL（4×B1 + B2）→ 修复 → 复核 PASS。
- 生成文件指纹由 `s2s3_cases.py --check` 与 `coverage.py --check` 固定。
- 报告时间：2026-11-15。
