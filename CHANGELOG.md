# Changelog

## [0.0.46] - 2026-11-15

### Added

- **S2 模块发现与 IMPORT**：`src/frontend/module.{h,c}` 实现根 `#program` 校验、按 import 源序 DFS、导入目录→include roots 的确定性搜索、解析路径实例键、`module_order` 固定、循环/重复/缺失/非库/导入名冲突的 IMPORT 缓冲；`#program` 导入目标仍做 LEX/SYN 但不递归。
- **S3 名称、类型、转换与布局**：`src/frontend/sem.{h,c}` 实现模块成员/命名空间与可见性、`Self`/`m`/裸名规则、类型表 intern 与 Analyze 后冻结、标量/ptr/fnptr/struct 等价、字面量按 W 的类别/后缀/符号/范围、`cast`/`trunc`/`bitcast` 类别矩阵先于位宽、结构体字段/`@padding`/布局/`LAYOUT_OVERFLOW`、`sizeof` 类型参数、字段赋值路径与 readonly、`make`/`call` 实参形状与类型、模式矩阵与 `.nsw/.nuw` 的 `@fast` 约束、`@align`/`@range` 的 W 范围检查。
- `tc-vm`/`tc-aot` 新增 `--static-only` 部分检查（LEX/SYN/IMPORT/SEM；退出 0 仍不宣称程序有效）；新增 `w32` 静态档案（`tc_profile_init_generic_w32`），用于 32/64 矩阵，不绑定可执行目标。
- 测试：`tc_sem_test`（58 项 L1 检查：类型身份、布局/偏移、W 尺寸、类型表冻结、钻石 module_order、同名不同路径实例）；`tests/conformance/static/s2s3_cases.py` 生成 160 个 SEM/翻转、45 个 import/库夹具、34 个接受工具用例；`determinism.s2s3`；adapter 支持 `--static-only`、imported-module 位置、related note 与 include roots。
- G2/G3 门禁报告；覆盖矩阵更新为 245 covered / 389 uncovered / 0 suspended（`flip32_64` 34/34、`first_diag_rule` 10/10）。

### Changed

- 普通调用在静态阶段（LEX/SYN/IMPORT/SEM）通过后仍显式退出 70；CT/CFG/DI/安全/注解/VM/后端仍未实现，未写入“已支持”。
- 编译一致性用例改为走完整静态管线，以验证 LEX/SYN 阶段优先于已记录的 IMPORT 错误；接受工具用例仍显式区分 `--syntax-only`/`--static-only`。
- 最终 `TcInstr`/`TcIR` 物化仍在 IR 最终化阶段；SEM 负责解析/校验并冻结类型表。

### Fixed

- 修复 `resolve_fields` 未知字段后未清除 `ok` 导致的 `TYPE_MISMATCH` 级联；修复同模块 private/#program struct 被误判为不可访问；补齐无后缀非负字面量对 signed 上界的检查。
- 门禁后修复 B1-5：无结果指令作 `var` 初始化器（`store`/`trap`/`br`/`br_if`/`switch`/`memmove`/`write`/`writeln`/`store.volatile`）必须报 `OPERAND_COUNT`；新增 `TC-SEM-OP-0004…0012` 与 `#lib` 函数体 `TC-SEM-OP-0101`。
- 修复 root 路径未规范化导致的模块实例重复；修复 `Self.f`/`m.f` 在值位置应报 `TYPE_MISMATCH` 而非 `FUNCTION_SCOPE_ACCESS`；修复 `.nsw/.nuw` 后缀位置与 padding 进制/后缀/负号检查。

- **S1 LEX/SYN 与扁平 parse IR**：`src/frontend/` 新增 `input`（arena/源读取/文档化上限）、`num`（精确十进制→binary32/64，roundTiesToEven，纯整数运算）、`lex`（UTF-8 解码、BOM/U+0000/非 ASCII 拒绝、`///` 注释、`\` 续行、最长匹配、助记符/后缀、注解、DISCARD、字面量、行列 span）与 `parse`（§14 EBNF、模式/层序/可见性、块结构、全部声明/语句/类型/指令形态、字段路径、命名实参、`switch` 目标、unsafe 深度）。
- LEX/SYN 七码（`TC_CE_SYNTAX`、`TC_CE_MISSING_END`、`TC_CE_MODULE_LAYER`、`TC_CE_MISSING_VISIBILITY`、`TC_CE_PROGRAM_MODE_MISUSE`、`TC_CE_VAR_MISSING_INIT`、`TC_CE_LITERAL_LEX_RANGE`）的触发、不误报、位置与阶段覆盖；`TC_CE_SYNTAX` 严格区分 LEX/SYN 阶段。
- `tc-vm`/`tc-aot` 的 `--syntax-only` 部分检查（退出 0 = 无 LEX/SYN 诊断，不宣称程序有效）；普通调用在 LEX/SYN 通过后仍显式退出 70。诊断行格式固定为 `<file>:<line>:<column>: error[<STAGE>]: <CODE>: <name>`；文件/资源失败使用 `tc_error_io`/`tc_error_internal`/`TC_ERR_OUT_OF_MEMORY`，不伪装语言诊断。
- 测试：`tc_frontend_test`（688 项 L1 检查，含全部 51 opcode/后缀、解析 IR 形态、UTF-8 前缀/偏移、首诊阶段/源序、文档化上限、重复解析确定性）；`tests/conformance/`（96 个 LEX/SYN 诊断用例 + 10 个接受用例 + 1 个随机字节属性用例，均由 `s1_cases.py` 表生成并 `--check`）；`tests/determinism/`（重复运行、哈希种子、参数顺序）；CMake 生成的 S1 harness adapter、`adapter.selftest`（fake tool 失败关闭）与 `harness.conformance`/`determinism.s1` 门禁。
- S1 golden：7 个规范推导的 LEX/SYN 期望（非 ASCII 列计数、负字面量词法范围、最早缺失 `end`、模块层序、DISCARD 字段名、`static var` 缺初始化、`#program` 的 `Self`）。

### Changed

- LEX/SYN 在 `tc_core` 内实现（`frontend/` 四个模块），仍不安装、不导出公共 API/ABI；仅 `tc-vm`/`tc-aot` 两个入口。
- `src/CONTRACTS.md` 记录 S1 前端边界：解析 IR 为前端内部结构（源名/类型语法保留给 SEM），冻结 `TcInstr`/`TcIR` 由 S3 物化；`--syntax-only` 为部分能力。
- 覆盖矩阵更新为 44 covered / 587 uncovered / 3 N/A(S0)；S1 覆盖 §2/§14 章节行、七码四槽与首诊阶段/源序行。
- 门禁报告统一迁入 `docs/gates/`（G0/G1 + 目录索引），README/AGENTS/文档地图与报告内部相对链接同步。
- 代码注释清理：移除对阶段编号（S0/S1…）、门禁报告、测试计划与审计结果的引用，改用标准/设计条款或自包含说明；CLI `--version` 后缀改为 `(LEX/SYN frontend)`。

### Fixed

- 修复解析失败路径在释放 parser 后读取 `impl` 的 use-after-free；嵌套深度/源文件/Token 上限统一返回实现级错误，不产生语言码。
- CRLF/CR 夹具在 `.gitattributes` 中标记 `-text`，避免 Git 行尾规范化丢失字节；`s1_cases.py --check` 按字节比较。
- S2 前置钉扎：新增 `1.5.5` 畸形数字候选 L3 用例与 L1 `1..5`/`1.5e5.5` 断言，记录“畸形数字候选按一个 LEX Token”的恢复约定；新增 L3 非法 UTF-8 字节夹具（注释内非 ASCII 标量后，断言标量列）；新增字段 padding `ANNOTATION(INT)` 形状用例与 S4 字段注解清单。
- G1 独立审计 A1–A9：修复嵌套配对块被误报 `MISSING_END`（改用 `then`/`end` 词法配对前看，配对时在嵌套构造处报 `SYNTAX`）；修正首诊规则 04 的覆盖映射（改用 `TC-SYN-END-0001`/`TC-SYN-VAR-0001` 专用码优先）；新增嵌套块/`Self`/后缀回归用例；Debug/Release/ASan 三套从当前源码重跑；adapter 增加诊断文件校验、stdout 为空与属性 stage 校验；澄清计划 S1 的 `TcParseInstr`/`TcInstr` 分工；统一 `#program` 中 IDENT-only 位置 `Self` 的 `PROGRAM_MODE_MISUSE` 约定与不完整/空白分隔后缀的定位约定。

## [0.0.46] - 2026-11-13

### Added

- **S0 契约冻结与测试基建（G0 PASS）**：类型/IR/对象/档案/诊断/零 pass/构建契约头文件与 `tc_core` 私有库。
- 80 码机器可读表（`tests/traceability/codes.json`）与生成的 C 表；65 `TC_CE_*` + 15 `TC_RE_*` 由标准 §12.3/§12.5.1 校验。
- 首诊比较器、诊断缓冲、`TC_ERR_OUT_OF_MEMORY` 独立通道。
- `tc-vm`/`tc-aot` CLI 骨架：参数与档案校验为真，前端/代码生成未实现时显式退出 70。
- L0 契约测试、Python 用例 harness（23 个失败关闭自测、7 判定器）、覆盖矩阵骨架、golden 校验/负例自测、CLI 契约测试。
- 指针状态契约 `src/ir/pointer_state.{h,c}`（§3.12 四态矩阵）与 `TC-L0-OBJ-0001`。
- `src/CONTRACTS.md` 记录契约属主、冻结边界与后续语义表归属。

### Changed

- 构建标准升为 C11；根 CMake 启用测试并只产出 `tc_core`、`tc-vm`、`tc-aot`。
- 重组 `src/`：跨阶段基础设施移入 `base/`（断言/版本）、`contract/`（契约检查/零 pass）、`diagnostic/`（80 码表/首诊选择）；`frontend/` 保留给 S1 起的 LEX/SYN 流水线；`runtime/profiles.c` 更名为 `profile.c`；两个 CLI 的档案选择合并到 `driver/profile_select.c`。

### Fixed

- G0 审计 F1/F2：补充 §12.2 #9/#4 键序文档说明；新增 `origin×rank` 合成 L0 用例（`TC-L0-DIAG-0001`）。
- `TC_CE_SAFE_VIOLATION` 写入 `same_site_rank=60`（`UNSAFE_*`/`FAST_CALL` 为 50），新增四码同位置优先 L0 用例（`TC-L0-DIAG-0002`）；覆盖矩阵新增 5 个 `same_site_priority` 行并标记 covered。
- 修复 `tc_instr_lhs_is_valid()`：有结果指令允许 `_` 丢弃（§6.15.1），同步修正错误测试并补 `TC-L0-IR-0001`。
- 补齐 §12.2 #10 调用/字段顺序、§8.8.2 调用安全优先、§12.2 #5 负移位优先的排序键与 L0 用例（`TC-L0-DIAG-0003`…`0006`）。
- 类型表创建时预创建 14 个标量单例，冻结后不再新增节点；补 `TC-L0-TYPE-0001`。
- 首诊 CALL/MAKE 增加 `site_id` 与 §8.8.2 检查阶段（目标/可见性 → 安全 → LHS/返回形态 → 实参 → 接收者），补 `TC-L0-DIAG-0007/0008`；诊断缓冲深拷贝文件名（生命周期测试）。
- 补齐 IR/对象/档案/标志/单位静态检查（`TC-L0-CONTRACT-0001`）、首诊规则 01–05/09–10 的 L0 映射（`TC-L0-DIAG-0009`）、harness IMPORT 阶段与空套件/路径失败关闭、golden 独立评审与强化校验。覆盖矩阵现为 634 槽、231 项：12 L0 covered / 619 uncovered / 3 个 N/A(S0)（首诊规则 06–08，S3/S4 关闭）。

## [0.0.46] - 2026-11-12

### Changed

- 分支切换为 **0.0.46 clean-sheet**：只保留 0.0.46 标准、设计文档与实现骨架。
- `src/` 按设计说明书 §14 重建为 `frontend` / `ir` / `runtime` / `vm` / `backend` / `driver` 目录骨架，无实现、无 `tc-vm`/`tc-aot` 目标。
- 根 `CMakeLists.txt` 改为骨架配置；`make`、`ctest`、VM/AOT 测试与示例流程已移除。
- README、AGENTS、CONTRIBUTING、SECURITY、docs/README 统一为 0.0.46 文档/骨架口径。
- 32/64 位目标与 `usize`/`isize` 平台字长口径写入仓库入口；标准与设计已按 32/64 双字长修订（字长相关类型、`sizeof`/`bitcast`/`trunc`/布局/诊断与平台档案）。

### Removed

- 历史实现、旧测试与旧构建配置。
- 旧 C 宿主公共库/嵌入 API、嵌入模式与其槽位桥接。
- 旧设计文档与旧发布清单。
- `tests/`、`scripts/`、`examples/`、`tools/`、`.cursor/`、`.github/`、`Makefile` 与历史构建目录。
- 旧版本号、CLI golden、旧错误码数量与旧实现导航。

### Kept

- `docs/TC语言及编译器标准-0.0.46.md`（80 码、语言/编译器规则）。
- `docs/TC原生编译器设计说明书-0.0.46.md`（目标架构）。
- `LICENSE`、`CODE_OF_CONDUCT*`、`CONTRIBUTING*`、`SECURITY*`。

历史实现可从 git tag 恢复；本分支不再维护。
