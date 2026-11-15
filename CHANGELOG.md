# Changelog

## [0.0.46] - 2026-11-15

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
- **工具能力**：`--syntax-only`（LEX/SYN）与 `--static-only`（LEX/SYN/IMPORT/SEM）部分检查；
  普通调用在静态阶段后显式退出 70。新增 `w32` 静态档案（不可执行目标）。
- **测试**：`tc_frontend_test`、`tc_sem_test`、S1/S2/S3 一致性用例表、确定性套件、adapter
  失败关闭自测；当前 Debug/Release/ASan `ctest` 20/20，harness 346/346。

### Changed

- 最终 `TcInstr`/`TcIR` 物化保留在 IR 最终化阶段；S3 完成解析/校验并冻结类型表。
- 收尾流程固化：阶段/分支收尾必须清理过程/历史文档，并通过 `docs.cleanup` 门禁；
  当前约定与已知限制归并到 `src/CONTRACTS.md`。
- 诊断位置与混用形态约定见 `src/CONTRACTS.md`；`--static-only` 与普通调用都不宣称程序有效。

### Removed

- 历史实现、旧测试、旧工具、旧脚本与旧示例；历史代码仅在 git tag 中。
