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
- **工具能力**：`--syntax-only`（LEX/SYN）、`--sem-only`（LEX/SYN/IMPORT/SEM）与
  `--static-only`（含 CT 与 IR 最终化）部分检查；普通调用在静态阶段后显式退出 70。
  新增 `w32` 静态档案（不可执行目标）。
- **测试**：`tc_frontend_test`、`tc_sem_test`、`tc_ct_test`、`tc_ir_test`、S1/S2/S3/S4/S5
  一致性用例表（S4 为 163 编译 + 44 接受；S5 为 48 编译 + 23 接受）、确定性套件、
  adapter 失败关闭自测；当前 Debug/Release/ASan `ctest` 26/26，harness 624/624。

### Changed

- SEM 完成解析/校验并冻结类型表；CT 与 IR 最终化在其后运行，只使用只读类型查找，
  不再 intern；SEM 的 CFG/DI 事实在 IR 最终化时重新物化为显式块边。
- 收尾流程固化：阶段/分支收尾必须清理过程/历史文档，并通过 `docs.cleanup` 门禁；
  当前约定与已知限制归并到 `src/CONTRACTS.md`。
- 诊断位置、CT 顺序/缓存/错误映射、静态布局与 IR 操作数约定见 `src/CONTRACTS.md`；
  `--sem-only`/`--static-only` 与普通调用都不宣称程序有效。

### Removed

- 历史实现、旧测试、旧工具、旧脚本与旧示例；历史代码仅在 git tag 中。
