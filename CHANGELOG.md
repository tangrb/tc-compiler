# 变更日志

[English](CHANGELOG.en.md)

本文件记录项目的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号大致遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)（工具链 tag：`vMAJOR.MINOR.PATCH`）。

## [Unreleased]

### Added

- **0.0.45 设计草案（文档先行，尚未实现）**：新增 `docs/TC语言标准设计说明书-0.0.45.md` 与 `docs/TC编译器标准设计说明书-0.0.45.md`。相对 0.0.44 的差异：`ptr<T>` → `ref<T>`（托管引用）、新增 `addr<T>`（线性地址）与 `addr_*` 指令族、`nullptr` 拆为两个空值字面量：`none`（空引用，仅 `ref<T>`）与 `nil`（空地址，仅 `addr<T>`）；`ptr_size(T, p)` → `bits_of(T)`；托管引用去掉算术/序关系与整数互转；`memcopy_unsafe` 与地址差、地址序关系、区间拷贝均不再提供（语言标准 §1.4）；空值错误码按空间拆分：`TC_RE_NULL_REFERENCE_DEREFERENCE` / `TC_RE_NULL_ADDRESS_DEREFERENCE` / `TC_RE_NULL_ADDRESS_ARITHMETIC`（不再共用空指针码）；诊断码 86 码 = 73 `TC_CE_*` + 13 `TC_RE_*`；编译器标准新增降级契约（§1.4）、局部可判定（§1.5）与调用深度上界（§8.10）。语言标准只承载现在时条款，跨版本差异记录于本文件。
- 移除历史过程性文档（0.0.44 符合性审计报告、0.0.44 符合性整改进度台账），`docs/README` 同步。

### Changed

- **0.0.45 冻结评估未通过，退回冻结候选（2026-09-26）**：两份 0.0.45 设计文档一度按冻结文本处理（状态行、效力范围、变更策略与编译器标准的成对升版条款），评审发现三处必须先闭合的缺口，故撤回冻结：① 托管引用可活过被指绑定，悬垂引用无定义语义；② 地址算术的值语义与目标字长（32/64）未唯一确定；③ 语法受限类型位置的诊断阶段与码不一致。原 `spec-0.0.45` 标签已撤销，两稿保持冻结候选。冻结后仍仅接受勘误，任何语义变更必须升版；勘误通道明确为**本文件**，不再单独保留勘误清单。同批修正：语言标准 §3.8.2 的「上表」改为显式章节引用、附录 A 注释中的产生式名与附录 A 定义对齐。

## [0.0.44] - 2026-09-21

### Changed

- GitHub Actions `ci.yml` / `asan.yml` 对 **`tc-0.0.44`** 的 push/PR 触发；覆盖率收集经 `scripts/lcov_compat.sh` 兼容 lcov 1.x、Ubuntu apt 2.0 与 Homebrew 2.5（`lcov` 与 `genhtml` 分开探测 `--ignore-errors`）。根 README、CONTRIBUTING、`docs/README` 已同步 CI 分支、lcov 依赖与 `build-coverage/` 产物路径。

- **语言规范升级至 0.0.44**（口径收敛版，不新增语言能力、不改变程序书写方式；诊断码仍 **86** 码 = 74 `TC_CE_*` + 12 `TC_RE_*`）。新增规范文本 `docs/TC语言标准设计说明书-0.0.44.md`。IEEE 754-2019 保留为唯一外部规范性引用。
- **全部下游设计文档同步至 0.0.44**：编译器标准、VM 详设、VM 命令行参考、AOT 详设、Embed 详设、libtc 设计说明书统一改名为 `*-0.0.44.md`，更新规范基线行、术语（「语法阶段受限恢复」→「结构类语法阶段诊断」）与 D1～D35 变更点（接受集、诊断归属、语义澄清），并为每份文档新增「0.0.44 同步状态」标注（文档先行）。
- 门禁 `scripts/sync/check_doc_counts.py` 的语言标准事实源改指 0.0.44，并修正提取逻辑（附录 B 终止边界、错误码种类以 `tc_types.h` 的 `TcErrorKind` 为事实源）；**门禁由预先存在失败转为全绿**。
- 元数据与导航层版本行改指 0.0.44：README（中英）、`AGENTS.md`、`docs/README`（文档地图）、`CONTRIBUTING`、`examples/README`、`.cursor` rules 与 skills；`docs/release-checklist` 的文档文件名核对项同步。
- **文档面清理**：移除已收口的历史过程文档（0.0.41 分析报告／修复计划、0.0.42 遗留问题清零计划）与 0.0.42 规范文本；勘误清单改名为不带版本号的 `TC-语言标准一致性勘误清单.md`（当时同时承载 0.0.42 首轮与 0.0.44 第二轮；该清单随后并入设计文档正文，0.0.45 起不再单独保留）。历史内容可用发布标签恢复（如 `git show v0.0.42:docs/TC语言标准设计说明书-0.0.42.md`），`docs/README` 已注明恢复方式；全仓 411 条相对链接复核无死链。
- **`#lib` 函数体 `Self.` 强制访问**：同模块访问函数外定义的 `static` 必须写 `Self.<名>`；实现原先只在赋值/`read` 目标位置报 `TC_CE_FUNCTION_SCOPE_ACCESS`，其余读位置（运算/比较/逻辑/位运算/移位、`return` 操作数、`if`/`while` 条件、输出操作数）降级为 `UNDEFINED_VARIABLE`，`memblock` 的 `N`/`count:` 更静默接受。本批统一分类，并补 `Self.<名> = v` 与 `Self.<名>.<字段> = v` 赋值目标解析（§6.2）、`TcFieldAssign.base_binding`（VM 与 AOT 共享基址解析，修 `unresolved struct base`）、以及 CFG 不再把 `static` 槽计入函数确定初始化（修函数体内读 `static var` 字段误报未初始化）。
- **`static let` 来源收紧（D35）**：`static let` 初始化器只可引用字面量与源序更早且已成功求值的 `let`/`static let`，**不得引用 `static var`**；违反时在任一位置（整条 RHS、操作数、字段读基址、`.count` 基址、构造器字段值、`cast`/`bitcast` 源、`memblock` 的 `N` 与 `count:`）统一报 `TC_CE_CONSTANT_EXPRESSION`。同批修复 4 项同族缺陷：整条 `Self.<名>` 的报错码与消息、`static let` 声明类型命名 N 从未解析（`.count` 静默为 0）、构造器 `count: <名>` 早于 Pass2 求值、`usize_operand` 裸名不回退全局表。
- **新增语言标准符合性审计**（审计报告：已于 0.0.45 周期从仓库移除，可由发布标签恢复）：以语言标准为唯一权威，核对 6 份设计文档与全部 `src/` 实现；确认 9 项既有未关闭项全部成立，并新增实现侧 70 项、文档侧 48+ 项、标准侧 4 项发现（含 2 项进程级故障：伪造指针越界读写、类型嵌套解析栈溢出）。审计未改动任何 `src/`、测试或既有文档。
- **实现侧全面落地 0.0.44**（Batch S）：① **接受集**——指针 `cast` 改为所指类型重标记、不再要求等宽（D14）；`bitcast(ptr ↔ 浮点)` 改为拒绝（D10）；`Self.<名>` 限定标识符可作运算与输出操作数（D11）；`if`／`while` 条件接受 §6.1.1 RHS 全集（只读字段读取、`Self.static let`、指针比较；D29）；② **诊断**——`const_rhs` 嵌套调用改报语法拒绝（D34）；CT 类诊断（`TC_CE_CONSTANT_*`）改为**挂起**至全部 SEM 类诊断无触发后再报告（语言标准 §11 四步规则／D19），派生失败（依赖已失败常量）不再改报 SEM 码；静态成员初始化器引用源序更晚／自身 → `TC_CE_UNDEFINED_VARIABLE`（D28）；③ **语义**——`ptr_load(bool)` 按 `0x00`／非零 → `0x01` 规范化（D16，**修复 VM 与 AOT 的行为分歧**）；`static var` 初始化器可引用更早 `static var`、准备阶段失败报 `TC_RE_*`（D16）；静态布尔三态判定原子集合扩至 §5.2.1 原子表达式（D29）；④ **语料与门禁**——新增／更正 VM 与 AOT 语料 24 项、单元断言 26 条，`test-map.md` 规模回填 966 VM / 443 AOT；VM／AOT／unit 三层与三项同步门禁全绿。
- **工具链实现版本升至 v0.0.44**（`TC_VERSION_CORE` / `TC_VERSION_EMBED`）；CLI `--version` 黄金测试同步为 `tc-vm 0.0.44` / `tc-aot 0.0.44`。

## [0.0.43] - 2026-09-01

### Added

- 开源仓库脚手架：贡献指南、行为准则、安全策略、Issue/PR 模板、Dependabot、示例与文档索引。
- 表面文档中英双语配对（文首可切换语言）。
- Cursor Agent 文档优化：加载分级、Skill 触发条件、工作流与路由去重。

### Changed

- 工具链与 Embed 实现版本升至 v0.0.43；语言规范设计书仍为 0.0.42（无语言语义变更）。

## [0.0.42] - 2026-08-30

### Added

- 新增 `TC_CE_EXTRA_ARGUMENT` 错误码（N-13）。
- 确定性的自实现浮点十进制输出（FP-4.6）。

### Fixed

- 与端序无关的 `memblock` / `struct` 布局（FP-4.5）。
- 无 FENV 环境下可移植的下溢检测（不依赖 `__int128`，N-12）。
- N-12 剩余可移植性债务（指针偏移、AOT 分配、`.count`）。

### Changed

- 设计文档与实现版本升至 v0.0.42；0.0.42 遗留问题清零收口。

## [0.0.41] - 2026-08-30

### Added

- 导入的 struct 须使用限定名。
- struct `field_access` 作为操作数的全链路支持。

### Fixed

- 语言标准符合性缺口（P0–P6 收口）。
- 菱形 import / 拓扑、const 复合类型与 `memcopy` 下标缺口。
- const 堆释放路径与 const 宽度回调。
- 静态 `let` / `var` 初始化器中 `Self.field` 求值。
- `memblock_count` 重写后保留 struct `.count` 基址。

### Changed

- 设计文档以语言标准符合性为基线重编；版本升至 v0.0.41。

## [0.0.40] - 2026-08-24

### Changed

- 实现与设计文档升至 v0.0.40。
- Cursor Agent 文档重构与特性地图拆分。
- 为 `tc-0.0.40` 分支增加 CI/ASan 触发。

## [0.0.39] - 2026-08-22

### Added

- struct 自引用 / 指针类型与 struct 形式合规（0.0.39）。
- 会话级 `-I` 包含路径（补齐 0.0.39 剩余缺口）。

### Fixed

- `memcopy_unsafe` 在 `ptr<T>` 上操作时不带 memblock 头偏移。
- 文档刷新导致的块注释损坏。

### Changed

- 从 0.0.38 线带入跨平台 CI/CD 与测试移植，纳入 0.0.39 发布线。

[Unreleased]: https://github.com/tangrb/tc-compiler/compare/v0.0.44...HEAD
[0.0.44]: https://github.com/tangrb/tc-compiler/compare/v0.0.43...v0.0.44
[0.0.43]: https://github.com/tangrb/tc-compiler/compare/v0.0.42...v0.0.43
[0.0.42]: https://github.com/tangrb/tc-compiler/compare/v0.0.41...v0.0.42
[0.0.41]: https://github.com/tangrb/tc-compiler/compare/v0.0.40...v0.0.41
[0.0.40]: https://github.com/tangrb/tc-compiler/compare/v0.0.39...v0.0.40
[0.0.39]: https://github.com/tangrb/tc-compiler/compare/v0.0.38...v0.0.39
