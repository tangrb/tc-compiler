# Changelog

## [0.0.47] - Unreleased

### Added

- **P0/T0 治理**：工具链版本 0.0.47 与语言/契约版本 0.0.46/1 分离；新增私有 `tc`
  工程工具入口，提供 CLI 语法、`--help` 与 `--version`。
- **P1 工程初始化**：`tc --init-project` 在当前 canonical 目录生成完整工程模板，
  仅创建缺失目标，支持写入前身份联合检查、失败回滚与提交后报告。
- **P1 manifest API**：`src/project/manifest.c` 实现 `tc.project` v1 解析、缺省值、
  有界读取与最近祖先根发现。
- **P2 路径模型**：编译器消费 manifest/include/profile，分离 canonical 与工程根
  相对显示路径；诊断、IR、VM trap 与 AOT 源字符串使用显示路径。模块身份绑定
  父目录与实际目录项名，根/import 源从绑定 fd 非阻塞有界读取。
- **P3 工程测试 runner**：`tc --test` 实现一层发现/筛选、fd 锚定 beneath 解析、
  严格侧车校验、私有只读快照、串行 fork worker、超时、流捕获、结构化结果、
  报告与退出映射。
- **P4a AOT 调试路径**：共享确定性行表与跨工程根相对/外部显示路径；私有 AOT 调试模式
  按最终 IR 顺序发射 `.file`/`.asciz`/`.loc`、源注释与最小 DWARF v4 CU，clang 生成
  `.debug_line`；固定 `comp_dir=.`、root display name、局部代码边界标签与 `.debug_str`
  重定位，并加固诊断/输出别名优先级、关闭 fd、保留输出清理与潜在 import 保护。
- **P4a 测试**：新增并注册 `linemap.unit` 与 `p4a.debug`；覆盖 JSON v3 稳定顺序、外部
  回退路径、IR 到 `.loc`/源注释顺序、DWARF v4 验证、非 debug 字节、跨根确定性与
  VM/AOT 输出保护边界。测试临时脚本与编译产物统一置于项目 `.tmp/`。
- **统一实现级错误**：所有入口共用 `TcImplError` 退出码映射；宿主 IO、能力不可用
  与实际 OOM 退出 70，用法错误退出 2，内部契约破坏与实现上限退出 71。
- **契约验证**：三入口精确版本输出、`tc` 参数/终止符/uint64 超时语法、
  `--test` 发现/筛选与失败关闭行为、缺文件与输出 IO、发布审计与测试标签。
- **P1 测试**：新增并注册 `project.manifest`、`project.init`、`project.init_fault`，
  更新 `cli.contract`；覆盖 manifest/根发现、完整模板、预检/身份/别名与逐故障点回滚。
- **P2 测试**：新增并注册 `project.paths_unit`、`project.paths`、`project.paths_aot`，
  覆盖根相对/外部/回退显示、模块身份、include/profile、错误优先级、源边界与 AOT 路径。
- **P3 测试**：新增并注册 `project.test_runner`，覆盖发现/筛选、路径边界、严格侧车、
  私有快照、fork worker、超时、流捕获、结构化结果、报告与退出映射。
- **故障路径验证**：SEM、VM 与三个后端逐分配点 OOM 故障注入及分配清理校验、关闭 stdout/stderr
  与无副作用检查、发布审计变异自测；OOM 统一映射与未来扩展边界记入
  `src/CONTRACTS.md`。

### 状态

- 0.0.47 CTest 注册总数 82（P0 基线 73 + P1 三项 + P2 三项 + P3 一项 + P4a 两项）；Debug、
  Release 与 ASan+UBSan 全量 CTest 均为 82/82，50 项收尾检查全部通过。0.0.46 已验收
  harness 873/873、覆盖矩阵 636/636，全部收尾检查与发布审计通过。
- P0/T0、P1、P2、P3 与 P4a 已完成；P4b、P5 与 T1–T6 尚未开始，G11 未完成。
  `tc --init-project`、编译器路径消费与 `tc --test` 已可用；P4a 调试发射为私有实现路径。
- P4b 的 VM 调试 ELF 内部布局不得回写冻结规范。

## [0.0.46] - 2026-11-19

### 状态

- **G10 PASS，0.0.46 阶段出口完成**：S8 AArch64 全量语义/ABI、S9
  freestanding/ARM32、S10 `riscv32-baremetal` 与长期回归均已通过门禁。
- 当前验证：Debug/Release/ASan+UBSan `ctest` **71/71**；harness **873/873**；
  覆盖矩阵 **636/636**；`docs.cleanup`、`release.s10_audit`、golden 全部通过。

### Added

- **0.0.46 clean-sheet 实现**：按标准与设计重写的私有 `tc_core`，以及 `tc-vm`/`tc-aot`
  两个唯一工具入口；不提供 C 宿主公共库、嵌入 API/ABI。
- **契约与测试基建**：类型/IR/对象/区/平台档案/诊断/零 pass 契约；80 码闭集与首诊比较器；
  harness、golden、覆盖矩阵；L0/L1 测试。
- **LEX/SYN 与扁平 parse IR**：UTF-8/注释/续行/最长匹配、精确十进制字面量、关键字/助记符/
  后缀/注解、EBNF、模式/层序、字段路径、命名实参、`switch` 目标、span；七个 LEX/SYN 码。
- **模块发现与 IMPORT**：import 源序 DFS、导入目录→include roots、解析路径实例键、
  `module_order`、五个 IMPORT 码与导入名冲突。
- **名称、类型、转换与布局**：成员/命名空间/可见性、类型表 intern 与冻结、字面量按 W 定型、
  `cast`/`trunc`/`bitcast` 矩阵、结构体布局/`@padding`、`sizeof`、字段/赋值路径、指令形状/
  类型、模式矩阵、32/64 静态档案。
- **const_expr 形态**：`const`/`static var` 初始化器的 §3.10 正向 op 集、
  const-only 值引用、禁止后缀/指针位重解释/var/参数/static 引用，统一 `CONST_FORM`。
- **CFG、DI、返回、调用、安全与注解**：每个程序/函数独立封闭 CFG、全边与可达性、
  `U[n]` 初始化与 DI 最大固定点、`MISSING_RETURN`；标签/控制形态；直接/间接调用的
  目标→安全→LHS→实参→接收者顺序与调用安全；safe/unsafe 上下文与 `@safe` 优先；
  函数/形参/字段注解矩阵、`@align`/`@range` 的端点常量（`lo`/`hi` 为同型整数
  常量、`N` 为正 2 的幂）、固定 binary32/64 的 NaN/下溢/cast 规则（不依赖宿主
  `fenv`/`long double`）、strict 失败不产生 SEM 注解诊断、64 位 `@align`/`@padding`
  元数据、承诺违反不静态拒绝；重复标签/畸形分支按保守 fallthrough 参与首诊，
  缺失标签不再抑制已知边上的 DI。
- **CT 求值**：`const`/`static var`/局部 `const` 的编译期求值；§3.10 正向 allowlist、
  §6.3–§6.6 的严格整数/浮点语义与 `CONST_*` 错误映射、精确 `mod`、下溢按
  tininess-after-rounding 判定；按 `module_order`/模块源序按需求值，成功/失败缓存，
  首个失败归属目标声明且无级联；聚合 const 字段树、静态初值 `TcInitImage`
  （按档案端序、padding 为 0、bool 0x00/0x01）。
- **IR 最终化与冻结**：`TcIR`/`TcUnit`/`TcBlock`/`TcInstr` 物化；字段路径变为
  vreg+字段索引，标签/`switch` 目标使用块 id，命名实参保留，span 保留；聚合 const
  在使用处内联为 `make`，`const` 不占对象；静态/帧对象表、模块实例 id、按 profile 的
  布局与冻结类型表；`tc_ir_print` 确定性文本转储。
- **运行时语义核与确定性 I/O**：W 相关的 strict/wrap 整数、除/模/移位边界、
  binary32/64 固定舍入与异常优先级、精确下溢、`cast`/`trunc`/`bitcast`；CT 与 VM
  共用该语义核（CT 映射到六个 `TC_CE_CONST_*`）；§11.2 六位有效数字精确渲染
  （不调用宿主 `printf`）与 §11.3 完整 Token 输入（EOF/短读/错误）。
- **参考 VM（W=32/64）**：五区地址空间、运行时对象与字段子对象、指针状态、
  全部 51 个 opcode、调用/`D`、尾调用不改变深度、I/O 整条提交、15 个
  `TC_RE_*` 必选 trap、trap 前已提交输出不回滚；`--checked` 增加 UB 越界检查；
  `aarch64-linux-user`、`aarch64-baremetal` 与 `w32` 档案。
- **AArch64 W=64 全量后端与工具链**：全部 51 个 opcode 与全部值类型（整数/位/
  比较、浮点、转换、指针/对象、`struct`/`make`/`copy`/`memmove`、直接/间接调用、
  `funcaddr`/`funceq`/`funcne`、`sizeof`、I/O、控制流）的 lowering；AAPCS64
  复合/HFA/大复合按引用/大 struct 间接返回、`fnptr`/`call_indirect`、多尺寸与未对齐访问；
  按 `TcObject` 偏移的帧/静态区布局、对象描述符表、静态初值 `.data`、复位入口、
  区基址、确定性汇编发射、无 regalloc；`tc-aot --emit=asm|obj|elf`、
  clang/lld 驱动、`--icf=none` 与 `aarch64-none-elf` target triple 映射；
  freestanding AArch64 运行时（启动、semihosting 输出、确定性格式化/输入、
  对象/指针检查、checked shadow、trap/退出、深度计数、运行时分配器）。
- **全量运行时与差分/一致性验证**：S6 W=64 全部运行时用例复用 + struct/HFA/
  大复合按引用/多参数/递归/间接调用/指针/大偏移/静态字节/`@fast_math` 的新差分用例；
  全量 AAPCS64 ABI、汇编形态审计与 clang C 调用者互操作；生成式 ABI/布局/内存/
  浮点 fuzz 与静态变异双工具首诊；标准 §13.4 八项一致性套件；checked build（越界/
  悬垂/one-past/未初始化）与必选 trap 分离统计、合法程序集合不变；ELF 入口/区布局/
  符号/静态字节/ICF 与确定性套件；S7 差分/ABI/工具链/静态一致/零 pass 套件继续通过；
  全部静态 `.tc` 的 VM/AOT 首诊逐字节一致；release 与 ASan+UBSan 均通过。
- **freestanding/裸机与 32 位后端**：目标无关 freestanding 运行时核与平台缝
  （对象表/必选定检/checked shadow、全部设备策略、确定性 I/O、分配器、trap/退出记录）；
  `aarch64-baremetal`（PL011 UART0 stdout/分帧 stdin、UART1 退出记录、PSCI SYSTEM_OFF、
  无 semihosting、ELF/flat/符号/区布局审计）；首个 32 位目标
  `arm32-baremetal`（Cortex-M7 MPS2 AN500、`thumbv7em-none-eabihf`、base
  AAPCS32/ILP32 softfp、独立 ARM32 后端与全部 51 个 opcode、复合实参按
  Rule C.4/C.5 字传递、64 位返回对/间接返回、`int64` 辅助、软件 binary64、未对齐
  访问、checked build、ARM EABI `__aeabi_*` 除法/内存辅助、`qemu-system-arm`
  执行）；`tc-aot --profile=` 目标分派与 `--device=` 平台实例策略覆盖；32/64
  分档案静态结果、golden 与差分保持隔离。
- **S10 多目标扩展与长期回归**：新增 `riscv32-baremetal` 目标（RV32IMAFDC、
  ILP32 soft-float ABI、`TcTargetDesc`/`TcABIDesc`、独立 RISC-V 32 后端与全部
  51 个 opcode、a0-a7 参数/16 字节栈对齐/2×XLEN 复合按值/更大复合按引用/
  隐藏指针间接返回、64 位除法辅助、NS16550A UART0 stdout 与分帧 stdin、
  semihosting 退出记录、SiFive test finisher、`qemu-system-riscv32` virt）；
  S10 RISC-V 平台/ELF/设备策略/checked/UART 输入套件、32 个 C caller 与 14 个
  TC caller 的 RISC-V ILP32 互操作用例、1037 个静态源 VM/AOT 首诊一致、
  149×2 个长期回归静态源、16×3 个差分样本、15 个 fuzz 回归用例与 3 目标平台
  语料、RISC-V 重复构建/执行与 O0/O2 确定性；发布审计检查 80 码闭集、私有
  `tc_core`、两个 CLI、freestanding 符号、版本与文档一致性。
- **S10 对抗性扩展**：新增生成式 ABI fuzz（随机标量/复合/padding 签名，C→TC
  与 TC→C 双向，三个 AOT 目标）、RISC-V 实现边界/鲁棒性用例（帧与静态区预算、
  `D=63/64`、设备窗口末端、空输入、flat 执行、长路径 trap 记录）与
  RISC-V 静态变异套件；
  长路径 trap/exit 记录缓冲增至 8192 字节以保留 `TC-EXIT`；C 互操作 harness 对
  未对齐 packed 复合使用 `-mstrict-align`。同时移除前端 arena 对单次分配误加的
  4 MiB 上限（约 3.3 万 token 的合法源会被误报 OOM），新增 800-case switch
  前端规模回归，并新增 39 个 RISC-V checked UB 用例的 VM/AOT 一致性套件与
  30000 指令 far-branch 长跳转回归；RISC-V 的 `br`/`br_if`/`switch`/`return`
  在超过 1 MiB 距离时使用 auipc/jalr，避免汇编器 fixup 越界；AArch64/ARM32
  条件远分支改为条件跳过 + 无条件转移。三个目标的复位入口在 `tc_main` 前清零
  BSS，使 ELF 与 flat 镜像都从确定的零运行时状态开始；ARM32 1 MiB 代码窗口
  仍是明确实现边界。全量 `ctest` 增至 71 项。
- **工具能力**：`--syntax-only`（LEX/SYN）、`--sem-only`（LEX/SYN/IMPORT/SEM）与
  `--static-only`（含 CT 与 IR 最终化）部分检查；普通 `tc-vm` 调用执行参考 VM，
  首个 trap 以 `error[RT]` 报告并退出 1；普通 `tc-aot` 调用发射目标汇编，
  `--emit=asm|obj|elf|flat` 生成汇编/ELF/裸二进制产物，`--checked` 生成 checked build，
  `--profile=`/`--device=` 选择目标档案与设备策略；无映射的平台能力显式退出 70
  且不发布错误产物。
- **测试**：`tc_frontend_test`、`tc_sem_test`、`tc_ct_test`、`tc_ir_test`、`tc_vm_test`、
  `tc_aot_test`、S1/S2/S3/S4/S5/S6 一致性用例表、S7 差分/ABI/工具链/静态一致/
  确定性套件、S8 全量差分/ABI/C 互操作/生成式 fuzz/checked/一致性/产物/确定性/
  静态变异套件、W=32/64 运行时 golden、S9 裸机/ARM32/互操作/静态一致/
  分档案/确定性套件、AAPCS64/AAPCS32 双向 interop、ABI mutation 审计与容量报告、
  adapter 失败关闭自测；当前 Debug/Release/ASan `ctest` 71/71，
  harness 873/873（608 编译、131 工具、133 运行、1 属性），单元检查 frontend 688、
  sem 102、ct 370、ir 674、vm 413、aot 81；S9 ARM32 差分 29 个固定用例 +
  296 个 S6/S8 语料，C 调用者 AAPCS32 14 例、AAPCS64 33 例，反向 ABI 19 例，
  1037 个静态源 × 2 个裸机档案；S10 RISC-V 29 个固定用例 + 296 个 S6/S8
  语料、32 个 C caller/14 个反向 ABI 用例、1037 个静态源、149×2 个长期回归
  静态源与 15 个 fuzz 用例；5/5 ABI mutant 被 interop 发现。

### Changed

- 诊断收口：裸 `NAME` 的注解按语法类型判定，`@align`/`@nonnull`/`@no_alias`
  非指针及 `@range` 非整数统一在注解 Token 报 `TC_CE_ANNOTATION`；非法 UTF-8
  的 0 基字节偏移在默认、`--sem-only`、`--static-only`、`--syntax-only` 与 AOT
  诊断路径均呈现；新增 S4 注解矩阵、CLI 全路径/畸形序列/导入/管道检查及
  adapter `byte_offset` 期望，并拒绝未声明 offset、校验 note 的文件/位置/阶段。
- SEM 完成解析/校验并冻结类型表；CT 与 IR 最终化在其后运行，只使用只读类型查找，
  不再 intern；SEM 的 CFG/DI 事实在 IR 最终化时重新物化为显式块边；`TcFunc.body`
  在 unit 数组增长结束后统一校正，稳定指向最终 `TcIR.units`。
- 收尾流程固化：阶段/分支收尾必须清理过程/历史文档，并通过 `docs.cleanup` 门禁；
  当前约定与已知限制归并到 `src/CONTRACTS.md`。
- 诊断位置、CT 顺序/缓存/错误映射、静态布局、IR 操作数与运行时/VM 约定见
  `src/CONTRACTS.md`；`--sem-only`/`--static-only` 都不宣称程序有效，普通
  `tc-vm` 运行也只报告首个 trap 或正常终止。

### Removed

- 历史实现、旧测试、旧工具、旧脚本与旧示例；历史代码仅在 git tag 中。
