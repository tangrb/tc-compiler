# TC 0.0.46 S1 门禁报告（G1）

> **阶段**：S1 LEX/SYN 与扁平 IR 构造（对应设计 §13 P1 的前端部分）
> **依据**：[开发测试计划](TC开发测试计划-0.0.46.md) §5 S1、§7、附录 D；
> [标准](TC语言及编译器标准-0.0.46.md) §1.6–§1.9/§2/§4.1–§4.2/§4.11/§5/§6.1–§6.15/§8/§12/§13.5/§14；
> [设计](TC原生编译器设计说明书-0.0.46.md) §3/§5/§6/§8/§14。
> **执行方式**：本会话无人介入（agent 执行）。独立评审由独立只读代理
> `gate-auditor` 执行（非作者、未参与实现、未运行编译器），不伪造人工签署。
> **结论**：**G1 PASS；允许进入 S2。**（首轮独立审计 FAIL；A1–A9 修复后复核通过。）

## 1. 结论

| 项 | 结果 |
| ---- | ---- |
| S1 开发交付 | **PASS（A1–A9 复核通过）** — UTF-8 读取/解码、`num` 精确字面量转换、LEX、SYN/扁平 parse IR、LEX/SYN 诊断与位置、`--syntax-only` 部分检查 |
| S1 测试活动 | **PASS（A1–A9 复核通过）** — L1 683 项检查；L3 105 个一致性/接受/属性用例；L7 确定性门禁；8 个 golden（7 个 S1）独立复核；覆盖矩阵 44 covered / 587 uncovered / 3 N/A(S0) |
| 自动化证据 | **PASS（三套从当前源码重跑）** — Debug/Release/ASan+UBSan 各 `ctest` 16/16；`-j8` 与 `--repeat until-fail:3` 通过；`leaks` 0；`nm` 仅 `_main`（及 Mach-O 固有 `__mh_execute_header`）；install 产物 0 |
| 独立 golden 评审 | **PASS** — 7 个 S1 golden 由 `gate-auditor` 独立推导并批准（2026-11-15）；记录见 `tests/golden/review-record.md` |
| 独立门禁审计 | **PASS** — `gate-auditor` 首轮发现 A1–A9；修复后复核通过，无未关闭 B1/B2/B3/B4（见 §12） |
| **G1** | **PASS** |
| 进入 S2 | **允许** |

## 2. 范围与依据

本阶段只实现并验证 **LEX/SYN**，不实现 IMPORT/SEM/CT/VM/AOT：

- 读取与解码：有效 UTF-8、拒绝 BOM、拒绝 U+0000、注释外仅 ASCII、非法字节保留“最长有效前缀 + 首个非法字节 0 基偏移”（标准 §1.6/§2.1/§12.5.1）。
- LEX：`///` 注释、`\` 续行、LF/CRLF/CR；最长匹配；`#program`/`#lib`；`ANNOTATION` 单一 Token；关键字/助记符/后缀；整数/浮点（含下划线、进制、`u`/`U`、`f`/`F`、`inf`/`nan`/`-inf`）；`DISCARD`；字面量词法范围（标准 §2、§6.2/§6.15.1、§14.2）。
- SYN：§14 EBNF；模式与层序；`struct`/`func`/`unsafe`/`var`/`const`/`static var`/`label`/`import`；`op_form`、`lhs`、`value`、`type`、`ptr<T>`/`fnptr<…>`、命名实参/命名字段；块结构 `then…end`；模式专用误用与 5 个专用 SYN 码；EOF/换行严格性（标准 §4.1/§4.2/§4.11.3/§14）。
- 扁平 parse IR：parser 直接产生“一行一 op”的扁平结构，不做 AST→TAC 降级；字段路径保留 `TcFieldRef` 形状；命名实参/字段保留名字；`switch` 目标按源序保留；保留文件/行/列 span 与构造结束位置（设计 §3.1/§3.2/§5）。
- 诊断：LEX/SYN 首诊、阶段、主位置；LEX 不报 SEM/CT 码；词法失败 Token 不进入语法分析（标准 §1.8/§12.1/§12.2/§12.5.1）。
- 工具：`tc-vm`/`tc-aot` 的 `--syntax-only` 部分检查；普通调用在 LEX/SYN 通过后仍显式退出 70（设计 §11、§15 决策⑦）。

不在本阶段范围：IMPORT/S2、SEM/S3–S4、CT/S5、VM/S6、AOT/S7–S9、32/64 静态结果、opcode 语义/安全/注解语义、运行时/平台。所有相关覆盖行保持显式 `uncovered`/`N/A(S0)`，见 §11。

## 3. 环境与可重复性

| 项 | 值 |
| ---- | ---- |
| 宿主 | macOS arm64（Apple Silicon） |
| C 编译器 | Apple clang 21.0.0.21000334 |
| CMake | 4.4.2 |
| Python | 3.14.7（仅标准库，无网络） |
| 构建 | Debug、Release、ASan+UBSan（Debug + `-fsanitize=address,undefined -fno-omit-frame-pointer`）；三套各 `ctest` 16/16 |
| 固定环境 | `LC_ALL=C`、`LANG=C`、`TZ=UTC`；每个用例独立临时目录；固定 stdin；无网络 |
| 运行 | S1 不需 qemu/ELF；AOT 差分从 S7 开始 |

本轮实测：

- Debug/Release/ASan+UBSan 三套构建各 `ctest` 16/16；
- Debug `ctest -j8` 通过，`ctest --repeat until-fail:3` 通过；
- Release `ctest` 16/16，ASan+UBSan `ctest` 16/16；
- `leaks --atExit` 对 `tc_frontend_test` 与 `tc-vm --syntax-only` 报告 0 leaks；
- `nm -gU` 显示 `tc-vm`/`tc-aot` 除 Mach-O 固有 `__mh_execute_header` 外仅 `_main`；
- `cmake --install` 安装产物 0 个文件（无公共 API/ABI、无 install 规则）；
- 确定性门禁 `determinism.s1`：104 个源文件各 3 次运行一致，105 个用例的 harness JSON 在 `PYTHONHASHSEED=0/1/42` 与逆序参数下逐字节一致；
- 修复后 Debug `tc-vm --syntax-only` 对 `_ = switch x` 退出 0（审计 A4 回归源）；Debug/Release/ASan 三套均从当前源码重建。

## 4. 交付物与证据

| S1 交付 | 位置 | 证据 |
| ---- | ---- | ---- |
| 输入/arena/文档化上限 | `src/frontend/input.h`、`input.c` | 4 MiB/16 KiB（标识符/注解/浮点）/256 层上限；越限为 `tc_error_internal`（L1 `TC-UNIT-LIMIT-0001/0002`） |
| 精确十进制→binary32/64 | `src/frontend/num.h`、`num.c` | roundTiesToEven、纯整数运算；下溢/上溢/负零/次正规/舍入到偶（L1 `TC-UNIT-NUM-0001`） |
| LEX | `src/frontend/lex.h`、`lex.c` | UTF-8、BOM/U+0000/非 ASCII、注释/续行、最长匹配、助记符/后缀、注解、DISCARD、字面量、span（L1 `TC-UNIT-LEX-*`；L3 `TC-LEX-*`） |
| SYN + 扁平 parse IR | `src/frontend/parse.h`、`parse.c` | §14 EBNF、模式/层序/可见性、块结构、全部声明/语句/类型/指令、字段路径/命名实参/`switch` 目标、unsafe 深度、span（L1 `TC-UNIT-SYN-*`/`TC-UNIT-OPCODE-0001`；L3 `TC-SYN-*`） |
| LEX/SYN 诊断 | `src/frontend/diag.h`、两端解析器 | 七码触发/阶段/位置；LEX 码闭集（L1 `TC-UNIT-DIAG-0001`/`TC-UNIT-LEX-0004`） |
| 首诊顺序 | LEX→SYN、源序 | `TC-UNIT-ORDER-0001/0002/0003`（#1/#3）；`TC-SYN-END-0001`/`TC-SYN-VAR-0001`（#4 专用码）；覆盖矩阵 `first_diag_rule` 01/03/04 |
| `--syntax-only` | `src/driver/frontend_driver.{h,c}`、`tc-vm.c`、`tc-aot.c`、`cli.{h,c}` | 退出码 0/1/70/71；诊断行格式；`cli.contract` |
| L1 前端单元 | `tests/unit/frontend_test.c` | 683 checks；CTest `frontend.unit` |
| 适配器失败关闭 | `tests/adapters/adapter_selftest.py` | fake tc-vm 覆盖干净诊断/诊断+垃圾/错误文件/stdout/属性 exit 0+垃圾；CTest `adapter.selftest` |
| L3 一致性/接受 | `tests/conformance/static/{lex,syn,accept}`、`s1_cases.py` | 94 个诊断 + 10 个接受 + 1 个属性；`conformance.s1_cases` 与 `harness.conformance` |
| S1 adapter | `tests/adapters/tc_adapter.py.in` | CMake 生成 `build/tc_adapter.py`；编译/工具/属性动作；失败关闭 |
| L7 确定性 | `tests/determinism/s1_determinism.py` | `determinism.s1` |
| golden | `tests/golden/common/*.golden.yaml` | 8 个通过校验；7 个 S1 独立复核（2026-11-15） |
| 覆盖矩阵 | `tests/traceability/coverage.*` | 44 covered / 587 uncovered / 3 N/A(S0) |
| 契约/文档 | `src/CONTRACTS.md`、`src/README.md`、`AGENTS.md`、README/CHANGELOG/tests 文档 | S1 边界与解析 IR 归属；文档同步 |

## 5. S1 出口准则（计划 §5 S1）

1. **LEX 与 SYN 的全部专用码均有触发用例与不误报正例**：**PASS**。七码的 trigger/no_false_positive/position/stage 四个槽位均在覆盖矩阵标记 covered，case id 见 `tests/conformance/static/s1_cases.py` 与 `tests/traceability/coverage-report.md`。
2. **非法 UTF-8/BOM/U+0000/非 ASCII/非法 ASCII 全部只报 `TC_CE_SYNTAX`**：**PASS**。L3 `TC-LEX-SYN-0025/0026/0027`、L1 `TC-UNIT-UTF8-0001`/`TC-UNIT-LEX-0003`；LEX 码闭集由 `TC-UNIT-LEX-0004` 钉扎。
3. **所有合法 EBNF 形态可构造 IR（含字段 lvalue/命名实参/span）**：**PASS**。L1 `TC-UNIT-SYN-0001/0002/0003`、`TC-UNIT-OPCODE-0001`、`TC-UNIT-ACCEPT-0001`；L3 10 个接受用例覆盖 `#program`/`#lib`/import/struct/func/const/static/unsafe/注解/类型/字面量/字段路径/命名实参/switch/续行/CRLF/CR/注释 Unicode。S1 判据使用计划 S1 澄清的 `TcParseInstr`（字段路径 `TcFieldRef` 形状、命名实参、`switch` 目标、span）；冻结 `TcInstr` 由 S3 物化（设计 §3.1）。
4. **LEX 阶段不出现任何 SEM/CT 码**：**PASS**。`frontend/lex.c` 只产生 `TC_CE_SYNTAX`/`TC_CE_LITERAL_LEX_RANGE`；`TC-UNIT-LEX-0004` 遍历非法输入断言 stage=LEX 且码属于该二元集。
5. **词法失败 Token 不进入语法/语义**：**PASS**。前端先完成全部 LEX，只有 LEX 无诊断时才进入 parser；LEX 诊断路径在 `tc_parse_source` 中直接返回（`src/frontend/parse.c`）。
6. **首诊在 LEX/SYN 阶段内固定，重复运行一致**：**PASS**。LEX 源序 fail-fast；SYN 递归下降首个不可消费 Token；`determinism.s1` 与 `TC-UNIT-DET-0001`；阶段优先 `TC-UNIT-ORDER-0001`；源序 `TC-UNIT-ORDER-0002/0003`。

## 6. 附录 D.1 通用门禁

| 检查项 | 结果 | 依据/范围 |
| ---- | ---- | ---- |
| 80 码表未增删；码名/阶段/触发与标准一致 | **PASS** | `trace.codes`/`trace.gen_c_table`/`trace.harness_codes`；65 CE + 15 RE |
| 新增/修改用例有 `spec` 与独立评审 | **PASS** | golden 独立评审（`gate-auditor`，2026-11-15）；一致性用例表每项含 `spec`；独立门禁审计复核 S1 用例表（§12） |
| 覆盖矩阵已更新；未覆盖/挂起解释 | **PASS** | 634 槽 / 231 项：44 covered（12 L0 + 32 S1）/ 587 uncovered / 3 N/A(S0)；§11 列出本阶段外项 |
| 首诊/位置断言覆盖本阶段相关码 | **PASS** | 七码四槽；LEX/SYN 阶段与位置；`TC-UNIT-ORDER-*`；§9 记录位置约定 |
| 32/64 期望分档案；无跨档案比较 | **PASS (S1)** | S1 LEX/SYN 结果与 W 无关；本阶段没有跨档案期望或比较；W 相关静态结果从 S3 起分档案 |
| 确定性套件（重复/哈希/线程/顺序） | **PASS** | `determinism.s1`；`-j8`；`--repeat until-fail:3`；L1 重复解析 |
| 无新增警告；无标准之外诊断 | **PASS** | `-Wall -Wextra -Werror -Wpedantic`；三套构建；LEX/SYN 码闭集 |
| 构建/消毒器/符号审计 | **PASS** | ASan+UBSan 16/16；`nm` 仅 `_main`；install 0；`leaks` 0 |
| 门禁报告记录 profile/目标/工具链版本 | **PASS** | §3；reviewed code commit 见 §12 |

## 7. 附录 D.2 S1 附加检查

| 附加检查 | 结果 | 证据 |
| ---- | ---- | ---- |
| LEX 不报 SEM 码 | **PASS** | `TC-UNIT-LEX-0004`；lexer 码闭集 |
| 所有合法 EBNF 可构造 IR | **PASS** | L1 形态/opcode/接受 + L3 接受用例；S1 类型为 `TcParseInstr`（计划 S1 澄清），`TcInstr` 由 S3 物化 |
| span 行列 | **PASS** | L1 `TC-UNIT-LEX-0002/0003`、`TC-UNIT-UTF8-0001`、`TC-UNIT-SYN-0001`；L3 CRLF/CR/注释/非法字节用例 |
| 阶段边界（LEX→SYN→后续） | **PASS** | LEX 全量先于 SYN；SYN 通过后普通调用退出 70；`--syntax-only` 不宣称程序有效 |
| `not-implemented` 不计为通过 | **PASS** | `harness.conformance` 使用 adapter 且无 `--allow-not-implemented`；105/105 pass、0 not_implemented |

## 8. 测试与覆盖证据

### 8.1 L1（`frontend.unit`，683 checks）

| 领域 | 用例 |
| ---- | ---- |
| 精确字面量 | `TC-UNIT-NUM-0001`：`3e22` 精确、`1e308/1e309`、`5e-324`、`2.4e-324`、`-0.0`、float32 边界、精确 tie-to-even |
| Token/位置 | `TC-UNIT-LEX-0001/0002/0003`：关键字、助记符/后缀、注释/续行、CRLF/CR、BOM/U+0000/非 ASCII、非法数字、`-nan`、字面量范围 |
| UTF-8 span | `TC-UNIT-UTF8-0001`：非法首字节/非法续字节/截断；`valid_prefix_bytes` 与 `invalid_byte_offset`；标量列 vs 字节列（非 ASCII 注释前缀、非 BMP 标量） |
| parse IR | `TC-UNIT-SYN-0001/0002/0003`：字段路径作为值/lvalue、命名实参/字段、`sizeof` 类型参数、`switch` 目标、unsafe 深度、import 名 span、字段/base/限定类型成员 span |
| opcode | `TC-UNIT-OPCODE-0001`：51 个 base + 全部定义后缀的 op/mode |
| 诊断/顺序 | `TC-UNIT-DIAG-0001`、`TC-UNIT-ORDER-0001/0002/0003`、`TC-UNIT-LEX-0004` |
| 限制/确定性 | `TC-UNIT-LIMIT-0001/0002`、`TC-UNIT-DET-0001`、`TC-UNIT-ACCEPT-0001` |

### 8.2 L3（`harness.conformance`，105/105）

- 94 个编译诊断用例：LEX 29、SYN 65；每例断言 stage/code/primary/related 且“恰好一个”；
- 10 个接受工具用例：`--syntax-only` 退出 0、stdout/stderr 为空、两次运行逐字节一致；
- 1 个属性用例：`lex_random` 200 次确定性随机字节，不崩溃/不挂起，拒绝时恰好一个 LEX/SYN 码；
- 用例表 `tests/conformance/static/s1_cases.py` 是唯一事实源，`--check` 验证生成文件与表一致；期望值不由实现输出回填。

### 8.3 覆盖（`coverage.generated.json`）

| 类别 | Items | Slots | Covered | Uncovered | N/A(S0) |
| ---- | ---: | ---: | ---: | ---: | ---: |
| Standard chapter | 15 | 30 | 4 | 26 | 0 |
| Diagnostic code | 80 | 344 | 28 | 316 | 0 |
| Opcode | 63 | 155 | 0 | 155 | 0 |
| Design §12 item | 15 | 30 | 0 | 30 | 0 |
| First-diagnostic rule | 10 | 10 | 7 | 0 | 3 |
| Same-position priority | 5 | 5 | 5 | 0 | 0 |
| 32/64 flip | 17 | 34 | 0 | 34 | 0 |
| Annotation | 16 | 16 | 0 | 16 | 0 |
| Device policy | 4 | 4 | 0 | 4 | 0 |
| I/O source state | 4 | 4 | 0 | 4 | 0 |
| Exit strategy | 2 | 2 | 0 | 2 | 0 |
| **Total** | **231** | **634** | **44** | **587** | **3** |

S1 覆盖：§2/§14 章节正反例；七码 ×（trigger/no_false_positive/position/stage）；`first_diag_rule` 01/03/04 的 S1 真实源例（04 用 `TC-SYN-END-0001`/`TC-SYN-VAR-0001` 的专用码优先于通用 SYNTAX）。其余未覆盖项属于 S2–S9 范围（§11），不是本阶段遗漏。

### 8.4 golden

8 个 golden 全部通过 `tests/golden/validate.py`；7 个 S1 golden 由 `gate-auditor` 独立推导并批准（`review-record.md`）。没有用编译器输出回填任何期望。

## 9. 首诊与位置约定（S1 记录）

标准 §12.5.1 对多数 LEX/SYN 码给出明确主位置；对于**未逐项枚举的通用语法错误**，S1 采用并记录以下确定性约定（用例 `spec` 中标记 `@convention`）：

1. 通用 SYNTAX 的主位置是“EBNF 解析无法消费的第一个 Token”（最小畸形子构造的起点）；行末缺 Token 时定位到行终止符 Token，文件末尾缺 Token 时定位到 EOF 位置。
2. `TC_CE_MODULE_LAYER` 的主位置是破坏层序的**构造首 Token**；`#lib` 成员的可见性修饰符属于该构造，因此 `public struct` 的破坏层序位置是 `public`（独立评审认可的约定）。
3. 多个未配对 `func`/`struct`/`unsafe` 开启关键字只产生**一个** `TC_CE_MISSING_END`，位置取源位置最早者；不为每个未配对 opener 各报一条。
4. `TC_CE_SYNTAX` 是 LEX/SYN 双阶段码；S1 用例断言具体阶段（非法标量/字节/注释外 `/`/`;` 为 LEX；DISCARD 位置、未知 opcode/后缀、Token 序列为 SYN）。
5. 非法 UTF-8：主位置按最长有效前缀计算，首个非法字节占下一列；`TcSpan.valid_prefix_bytes`/`invalid_byte_offset` 均为绝对字节偏移。BOM/U+0000/非 ASCII 是“有效 UTF-8 但非法”，`invalid_byte_offset == TC_SPAN_NO_INVALID_BYTE`。
6. 列号按解码后的 Unicode 标量计数；注释内每个标量一列；Tab 计一列；CRLF 计一个行终止符；不规范化、不做大小写/全半角转换。
7. 文件缺少末尾 NL 时按严格 EBNF 报 `TC_CE_SYNTAX`（§14.1/§14.3 的 NL 要求）；空行/纯注释行不产生 Token。
8. 交叉验证：标量列与字节列在含非 ASCII 注释前缀 + 非法字节的 L1 用例中分离（`TC-UNIT-UTF8-0001`）。
9. **嵌套块与 `MISSING_END`（A1 修复后约定）**：在块内遇到模块级构造时，用 `then`/`end` 词法配对前看决定：从当前 Token 到 EOF，深度 0 的 `end` 数少于当前打开块数 → 外层块确有未配对 opener，报 `MISSING_END` 于源位置最早者；足够 → 外层块已配对，报 `SYNTAX` 于嵌套构造首 Token（如 `public`/`unsafe`/`func`）。嵌套 opener 的 `then` 会消费其后的 `end`；若没有更多 `end`，较早的外层 opener 是未配对候选（`TC-SYN-END-0008`）。回归用例：`TC-SYN-END-0006…0010`、L1 `TC-UNIT-DIAG-0001`。
10. **`#program` 的 `Self`（A7 约定）**：`#program` 中 `Self` 在任何位置（值、lvalue、类型、注解参数、`struct`/`var`/`const`/`label`/`import` 等 IDENT-only 声明名）均报 `PROGRAM_MODE_MISUSE` 于 `Self`；`#lib` 的 IDENT-only 位置仍是 `SYNTAX`（Self 是关键字，不是 IDENT）。用例：`TC-SYN-SELF-0001/0002`、`TC-SYN-MODE-0005/0006`。
11. **后缀位置（A8 约定）**：紧邻助记符的 `.` 属于助记符 Token 的一部分；若 `.` 后没有后缀词，作为无效后缀 Token，`SYNTAX` 定位到助记符首 Token（`add.` → `add`）。空白分隔的 `.wrap` 不是后缀 Token（§2.7.1 后缀不拆分），按 Token 序列错误定位到 `.`（`add .wrap` → `.`）。用例：`TC-SYN-OP-0010/0011`。

## 10. 实现上限与实现级错误

S1 文档化上限（标准 §13.5 允许；不得伪装语言诊断）：

| 上限 | 值 | 行为 |
| ---- | ---- | ---- |
| 单源文件字节数 | 4 MiB | `tc_error_internal`（退出 71） |
| 单 Token（标识符/注解/浮点）字节数 | 16 KiB | `tc_error_internal`（退出 71） |
| 超长整数字面量 | 不设额外上限 | 仍按 §2.4 词法范围报 `TC_CE_LITERAL_LEX_RANGE`（不分配无界大整数） |
| 类型/块嵌套层数 | 256 | `tc_error_internal`（退出 71） |

文件打开/读取失败：`tc_error_io`（退出 71），属实现/API 层；内部资源失败：`TC_ERR_OUT_OF_MEMORY`（独立通道）；两者都不产生语言码。`--syntax-only` 的退出 0 只表示“无 LEX/SYN 诊断”，不是有效程序声明。

## 11. 范围说明（N/A，不是阻塞项）

| 项 | S1 状态 | 关闭阶段 |
| ---- | ---- | ---- |
| 模块发现、`module_order`、IMPORT 5 码 | 未实现；`TC_CE_IMPORT_*` 覆盖行保持 uncovered | S2 |
| 名称解析、类型/转换/布局、`sizeof`/`@padding` 语义 | 未实现；SEM 码保持 uncovered | S3 |
| CFG/DI/返回/调用/安全/注解语义 | 未实现；S4 码保持 uncovered | S4 |
| CT 6 码、const/静态初值 | 未实现 | S5 |
| VM 全 opcode/trap/golden | 未实现；opcode 语义行保持 uncovered | S6 |
| AArch64 后端/工具链/差分 | 未实现 | S7–S8 |
| 32/64 静态翻转、布局/golden | S1 LEX/SYN 与 W 无关；翻转行保持 uncovered | S3/S9 |
| freestanding/裸机 | 未实现 | S9 |
| 跨文件诊断位置（导入方/被导入模块） | 未实现 | S2+ |
| 真实运行时/ABI/平台 | 未实现 | S6–S9 |

上述项不属于 S1 出口准则，均在覆盖矩阵显式 `uncovered`/`N/A(S0)`，不伪装为通过，也不构成 G1 阻塞。

## 12. 缺陷与独立审计

独立审计由 `gate-auditor`（read-only subagent，非作者、未参与实现）执行。首轮结论为 **G1 FAIL**；A1–A9 修复后复核通过。

**结论：A1–A9 复核通过；无未关闭 B1/B2/B3/B4。** 独立审计于 2026-11-15 完成；未运行编译器/VM 回填期望，7 个 S1 golden 均为独立推导批准。以下为审计发现、修复与复核结果。

### 12.1 审计发现与修复

| 编号 | 等级 | 位置 | 问题 | 修复 | 状态 |
| ---- | ---- | ---- | ---- | ---- | ---- |
| A1 | **B1** | `src/frontend/parse.c` 的嵌套块处理 | 在块内遇到 `func`/`struct`/`unsafe` 时无条件报 `MISSING_END` 于 `blocks[0]`；配对的嵌套块（有自己的 `end`）被误报，而不是在嵌套构造处报 `SYNTAX`。 | 新增 `enclosing_blocks_paired()` 词法配对前看：统计当前 Token 起 `then`/`end` 深度与深度 0 的 `end` 数；不足外层块数才报 `MISSING_END`（最早 opener），足够则报 `SYNTAX` 于嵌套构造首 Token。新增 L1/L3 回归用例（`TC-SYN-END-0006…0010`、`TC-UNIT-DIAG-0001`）。 | **复核通过** |
| A2 | **B2** | `tests/traceability/coverage.py`、覆盖报告 | `first_diag_rule:04` 误映射到 `TC-UNIT-ORDER-0003`，后者实为 §12.2 #3 同行列序；导致报告虚假声称规则 04 有 S1 真实源例。 | 规则 03 映射 `TC-UNIT-ORDER-0002/0003`；规则 04 改为真实专用码优先用例 `TC-SYN-END-0001`（`MISSING_END` 优先于通用 `SYNTAX`）与 `TC-SYN-VAR-0001`（`VAR_MISSING_INIT` 优先于通用 `SYNTAX`）；保留 L0 `TC-L0-DIAG-0009`。覆盖报告/文档同步。 | **复核通过** |
| A3 | **B2** | `tests/conformance/static/s1_cases.py` | 缺少嵌套配对/嵌套未配对块用例，导致 A1 通过全部 96 例。 | 新增 9 个 L3 用例（嵌套配对 `struct/func/struct/unsafe`、嵌套未配对 func、`#program` 的 `Self` IDENT 位置、不完整/分离后缀）；新增 L1 断言。 | **复核通过** |
| A4 | **B3** | Debug 构建/证据 | 审计时 Debug `tc-vm` 早于源码修复，出现 `_ = switch x` 退出 71（OOM）而 Release 退 0；Debug ctest/conformance 使用了陈旧工具。 | 三套构建均从当前源码重建；Debug `_ = switch x` 退 0；Debug/Release/ASan ctest 各 16/16，harness 105/105，determinism 通过（见 §3/§13）。 | **复核通过（A4 重跑证据）** |
| A5 | **B3** | `tests/adapters/tc_adapter.py.in` | 编译用例把诊断文件替换为 metadata 源名而不校验实际文件；`--syntax-only` 忽略 stdout；属性用例不校验 stage，且属性路径忽略非诊断 stderr 行（可被诊断+垃圾绕过）。 | 统一严格解析：每条非空 stderr 行必须是诊断；文件路径必须匹配；stdout 必须为空；属性路径也使用严格解析并校验 stage/code。新增 `tests/adapters/adapter_selftest.py`（fake tc-vm：干净诊断、诊断+垃圾、exit 0+垃圾、错误文件、stdout 垃圾），注册为 `adapter.selftest`。 | **复核通过（adapter.selftest）** |
| A6 | **B4** | 计划 §5 S1 第 4 条 vs `parse.h`/AGENTS | 计划字面要求 parser 直接产 `TcInstr`，实现产 `TcParseInstr` 并在 S3 物化 `TcInstr`。 | 在计划 S1 第 4 条加入“S1 扁平 IR 类型说明”：`TcParseInstr` 为前端内部扁平结构，冻结 `TcInstr` 由 S3 物化；不改变“直接产扁平 IR、不建 AST”的要求；S1 判据使用 `TcParseInstr`。报告/AGENTS/CONTRACTS 同步。 | **复核通过** |
| A7 | **B4** | `src/frontend/parse.c` `expect_ident` | `#program` 的 IDENT-only 位置（`struct Self`/`var Self`/`label Self`）报 `SYNTAX`，而非 `PROGRAM_MODE_MISUSE`；§4.2 可宽读为任一 `Self`。 | 采用宽读约定：`#program` 中 `Self` 在任何位置（含 IDENT-only 声明名）均报 `PROGRAM_MODE_MISUSE` 于 `Self`；`#lib` 保持关键字语法错误。新增 L1/L3 用例与 §9 约定。 | **复核通过** |
| A8 | **B4** | `src/frontend/lex.c`、`parse.c` | `add.`（不完整后缀）报在 `.`；`add .wrap`（空白分隔）也报在 `.`；标准 §12.5.1 的“未知后缀定位到助记符首 Token”与通用约定存在歧义。 | 紧邻助记符且无后缀词的 `.` 并入 MNEMONIC Token（`combo_valid=0`），报 `SYNTAX` 于助记符；空白分隔的 `.wrap` 不是后缀 Token（§2.7.1），按 Token 序列错误报于 `.`。新增 L1/L3 用例与 §9 约定。 | **复核通过** |
| A9 | **B4** | 本报告 §1/§12/§13 | 审计前草案的 PASS/无未关闭项声明与审计发现矛盾。 | 本报告改为 FAIL/修复/待重审；通过后仅在重审确认时改回 PASS。 | **复核通过** |

### 12.2 首轮审计的特许确认

首轮审计同时确认（不因 A1–A9 失效）：

- 首轮审计的 85 个（现 94 个）一致性用例表的 stage/code/line/column 与 §12.3/§12.5.1 推导一致，包括所有 `@convention` 位置；
- LEX 码闭集、LEX→SYN 阶段边界、实现级错误通道、CLI `--syntax-only`/默认退出码符合报告；
- 7 个 S1 golden 的独立推导批准（无 golden 缺陷）；
- 生成文件与 `s1_cases.py --check` 一致，golden/coverage 校验通过，96（现 105）例无 skipped/not_implemented。

### 12.3 复核结论

- A1（B1 嵌套块配对）、A2/A3（B2 覆盖映射与回归用例）、A4（B3 Debug 证据）、A5（B3 adapter 失败关闭）、A6–A8（B4 约定/计划澄清）、A9（报告结构）均经独立审计复核通过；
- A5 的属性路径已改为严格 stderr 解析（每条非空行必须是诊断，文件路径必须匹配，stdout 必须为空，stage/code 必须为 LEX/SYN），并新增 `tests/adapters/adapter_selftest.py` 的 fake-tool 失败关闭自测（`adapter.selftest`，16/16）；
- A9 的 §13 与本附录已恢复/更新；
- 无未关闭 B1/B2/B3/B4；G1 可判定 PASS。

## 13. 门禁判定

**G1：PASS。**

- S1 LEX/SYN 与扁平 parse IR 满足计划 §5 S1；
- 自动化证据：Debug/Release/ASan+UBSan `ctest` 各 16/16；`-j8` 与 `--repeat until-fail:3` 通过；L1 683 checks；L3 105/105（94 compile + 10 tool + 1 property，0 skipped/not_implemented）；`adapter.selftest` 通过；`determinism.s1` 通过；golden 8/8；覆盖率 44 covered / 587 uncovered / 3 N/A(S0)；`leaks` 0；`nm` 仅 `_main`（及 Mach-O 固有 `__mh_execute_header`）；install 产物 0；
- 独立 golden 评审 PASS；独立门禁审计首轮 FAIL → A1–A9 修复 → 复核 PASS；无未关闭 B1/B2/B3/B4；
- 附录 D.1 适用项与 S1 附加项全部 PASS；范围外项显式 `uncovered`/`N/A(S0)` 并注明关闭阶段。

**进入 S2：允许。**

## 附录 A. 复现命令

```sh
# Debug
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
ctest --test-dir build --output-on-failure

# Release
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j8
ctest --test-dir build-release --output-on-failure

# ASan+UBSan
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-sanitize -j8
ctest --test-dir build-sanitize --output-on-failure

# 关键单测 / 一致性 / 确定性 / golden / 覆盖
./build/tests/tc_frontend_test
python3 tests/conformance/static/s1_cases.py --check
python3 tests/harness/runner.py tests/conformance \
  --adapter build/tc_adapter.py --json-out build/harness-report.json
ctest --test-dir build -R 'determinism.s1|harness.conformance' --output-on-failure
python3 tests/golden/validate.py
python3 tests/traceability/coverage.py --check

# 符号/泄漏/安装审计
nm -gU build/src/driver/tc-vm build/src/driver/tc-aot
leaks --atExit -- ./build/tests/tc_frontend_test
cmake --install build --prefix /tmp/tc-install && find /tmp/tc-install -type f | wc -l
```

## 附录 B. reviewed commit / 证据版本

- reviewed code commit：`79dfee947c484a2887f0a5c243dc73ead6bbd6b5`（S1 实现/测试；其后提交仅记录本报告的 PASS 与 CRLF 夹具属性）。
- evidence version：`S1-2026-11-15`。
- 独立 golden 评审：2026-11-15（`gate-auditor`）。
- 独立门禁审计：首轮 2026-11-15（FAIL：A1–A9）；A1–A9 修复后复核通过（2026-11-15），无未关闭 B1/B2/B3/B4。

*— 文档结束 —*
