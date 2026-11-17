# Changelog

## [0.0.46] - 2026-11-16

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
- **AArch64 W=64 子集后端与工具链**：标量整数/位/比较、`br`/`br_if`/`switch`/
  `return`/`trap`、直接调用、整数/bool `read`/`write`/`writeln`、`sizeof`、`copy`
  的 lowering；AAPCS64 子集（x0–x7/x0、16 字节栈对齐、callee-saved 帧/静态基址、
  bool/int8/int16 扩展、`D`）；按 `TcObject` 偏移的帧/静态区布局、静态初值
  `.data`、确定性汇编发射、无 regalloc；`tc-aot --emit=asm|obj|elf`、
  clang/lld 驱动与 `aarch64-none-elf` target triple 映射；freestanding AArch64
  运行时（启动、semihosting 输出、整数/bool 格式化/输入、trap/退出、深度计数）。
- **S7 VM↔AOT 差分与 ABI/工具链验证**：S6 W=64 子集用例复用、64 位/静态/开关
  边界用例、固定种子整数 fuzz、AAPCS64 子集 ABI、`.s`/ELF 可重复、O0/O2 零 pass、
  全部静态 `.tc` 的 VM/AOT 首诊逐字节一致、未支持 opcode/checked build/非目标档案的
  显式 exit 70 拒绝；release 与 ASan+UBSan 均通过。
- **S7 加固**：switch CFG 后继缓冲区按目标数动态分配；AOT 输入支持任意前导零；
  ELF 产物设置可执行权限；CMake 要求 clang/llvm-ar/lld/qemu 并拒绝静默降级；
  输入/栈地址改由链接脚本提供并加重叠断言；新增 switch 后继与 `TcFunc.body`
  回归测试；测试适配器对符号链接工作区使用 canonical 路径；G7 证据区分
  61 个实际差分与 1 个显式拒绝。
- **工具能力**：`--syntax-only`（LEX/SYN）、`--sem-only`（LEX/SYN/IMPORT/SEM）与
  `--static-only`（含 CT 与 IR 最终化）部分检查；普通 `tc-vm` 调用执行参考 VM，
  首个 trap 以 `error[RT]` 报告并退出 1；普通 `tc-aot` 调用发射 AArch64 子集汇编，
  `--emit=obj|elf` 生成 ELF 产物；子集外能力显式退出 70 且不发布错误产物。
- **测试**：`tc_frontend_test`、`tc_sem_test`、`tc_ct_test`、`tc_ir_test`、`tc_vm_test`、
  `tc_aot_test`、S1/S2/S3/S4/S5/S6 一致性用例表、S7 差分/ABI/工具链/静态一致/
  确定性套件、W=32/64 运行时 golden、确定性套件、adapter 失败关闭自测；当前
  Debug/Release/ASan `ctest` 39/39，harness 754/754（557 编译、129 工具、67 运行、
  1 属性），单元检查 frontend 688、sem 102、ct 370、ir 674、vm 413、aot 34。

### Changed

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
