# Tests

测试层级、覆盖清单与门禁要求见
[TC 开发测试计划 0.0.46](../docs/TC开发测试计划-0.0.46.md)；当前门禁状态见
[门禁状态](../docs/gates/README.md)。

| Suite | Location | Level | Content |
| --- | --- | --- | --- |
| Contract/unit | `unit/` | L0/L1/L2 | 契约检查；`tc_frontend_test`（LEX/SYN、parse IR、位置/限制）；`tc_sem_test`（类型身份、布局、W 尺寸、冻结、模块顺序/实例）；`tc_ct_test`（CT 向量/缓存/局部 const）；`tc_ir_test`（最终 IR 形态/CFG/打印）；`tc_vm_test`（语义核、格式化、输入、内存/指针、trap/位置、调用/`D`、设备策略、W 矩阵） |
| Conformance | `conformance/` | L3/L4/L5/L6 | 静态 LEX/SYN/IMPORT/SEM/CFG-DI/调用/安全/注解/CT 用例、接受工具用例、属性用例；VM 运行时用例（`runtime/`）；S7 VM↔AOT 差分（`diff/`）、AAPCS64 子集 ABI（`abi/`）、工具链/ELF/静态一致（`tools/`）；见 [conformance/README.md](conformance/README.md) |
| Harness | `harness/` | — | 元数据解析、7 判定器、失败关闭自测、`--syntax-only`/`--static-only` adapter |
| Traceability | `traceability/` | — | 80 码表、结构性覆盖矩阵、生成器检查、矩阵 case id 存在性交叉检查 |
| Golden | `golden/` | L3/L4 | 规范推导的期望值及规范来源；`validate.py` 校验诊断与运行时 golden，`run.py` 执行 w32/w64 运行时 golden |
| Tools | `tools/` | L6 | `tc-vm`/`tc-aot` CLI 契约（用法、档案、部分检查、诊断、实现错误、AOT 产物/显式拒绝） |
| Determinism | `determinism/` | L7 | 重复运行、哈希种子/顺序扰动、逐字节一致的 harness JSON（S1/S2-S3/S4/S5/S6）；S7 零 pass/ELF 构建/执行稳定性 |
| Docs | `tools/docs_cleanup_check.py` | — | 文档收尾门禁：无过程/历史叙事、门禁记录简短、changelog 版本不重复 |

运行：

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

门禁使用 adapter 且不允许 `not_implemented`；缺失、跳过、超时、崩溃均为失败。
每次阶段/分支收尾还必须通过 `docs.cleanup`（见 `AGENTS.md` 的“收尾工作”）。
一致性用例按规范对象组织，期望必须引用标准条款，不得由编译器输出回填。
S1/S2/S3/S4/S5 用例表分别由 `tests/conformance/static/s1_cases.py`、
`s2s3_cases.py`、`s4_cases.py`、`s5_cases.py` 生成并 `--check`；S6 运行时用例表由
`tests/conformance/runtime/s6_cases.py`、逐 opcode 静态形状用例由
`tests/conformance/static/s6_static_cases.py` 生成并 `--check`。
S4 接受用例使用 `--sem-only`（SEM 阶段的部分检查）；编译用例与 S5 接受用例使用
`--static-only`（含 CT 与 IR 最终化）；S6 运行用例调用 `tc-vm` 并断言 stdout 字节、
首个 `TC_RE_*`、退出状态与终止方式。

S7 差分用例调用 `tc-aot --emit=elf`，再用 qemu-system-aarch64 执行 ELF，比较程序
stdout、首个 trap 码与退出策略；`s7_cases.py` 复用 S6 中落在子集内的 W=64 用例，
`s7_fuzz.py` 用固定种子生成整数程序，`s7_abi_cases.py` 覆盖 AAPCS64 子集，
`s7_toolchain_cases.py` 覆盖 `.s`/`.o`/ELF、显式拒绝与静态一致，
`s7_static_parity.py` 对全部静态 `.tc` 比较两个工具的首诊，
`s7_determinism.py` 覆盖 O0/O2、重复构建与重复执行。这些测试需要 `ld.lld` 与
`qemu-system-aarch64`；CMake 找到两者时才注册。
32/64 结果分档案保存；差分只在同一档案内比较 VM 与 AOT。

当前全量：`ctest` 39/39（Debug/Release/ASan+UBSan）；harness 754/754
（557 编译、129 工具、67 运行、1 属性）；单元检查 frontend 688、sem 102、
ct 370、ir 450、vm 413、aot 34。
