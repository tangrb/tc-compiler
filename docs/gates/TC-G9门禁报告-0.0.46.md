# TC 0.0.46 S9 门禁记录（G9）

**结论：G9 PASS，S9 出口准则满足，允许进入 S10。**

## 范围

S9 交付：无 libc 的 freestanding 运行时核/平台缝；`aarch64-baremetal` 平台实例
（PL011 UART0 stdout、UART1 退出记录、PSCI SYSTEM_OFF、注入式 stdin、无
semihosting）；首个 32 位目标 `arm32-baremetal`（Cortex-M7 MPS2 AN500、
`thumbv7em-none-eabihf`、base AAPCS32/ILP32 softfp、独立 ARM32 后端、ARM EABI
64 位除法/内存辅助）；`tc-aot` 目标分派、`--device=` 设备策略、裸机链接脚本/区
布局；四设备策略、退出策略、checked build 与 32/64 分档案验证。实现依据为标准
§6/§8/§10/§11/§13，设计 §7/§8/§10/§11/§14，以及测试计划 S9。

## 出口准则证据

| G9 准则 | 证据 | 结论 |
| --- | --- | --- |
| freestanding 构建/运行门禁通过；符号审计无 libc | `conformance.s9_baremetal`：aarch64-baremetal ELF/区/符号/无未解析符号/无动态段/无 `INTERP`/无 semihosting、UART 输出、trap、注入输入、静态 `.data` 字节、四设备策略；`conformance.s9_arm32`：ARM32 ELF 头/入口/符号/无动态依赖/无 libc 导入；两个目标运行时均由 `-ffreestanding -nostdlib` 构建并链接 | 通过 |
| 32 位目标选定并有 ABI/后端实现；32 位 ABI/语义套件通过 | 目标 `arm32-baremetal`（Cortex-M7/AAPCS32-ILP32，triple `thumbv7em-none-eabihf`，qemu-system-arm MPS2 AN500）记录于 `src/CONTRACTS.md`；`conformance.s9_arm32` 的 25 个固定语义/ABI 用例与 260 个 S6/S8 同档案语料全部 VM↔AOT 一致；`conformance.s9_arm32_interop` 以 clang 独立 AAPCS32 调用方通过 14 个复合/64 位/浮点/窄值用例；`aot.unit` 与 `conformance.s9_static_parity` 覆盖发射形态与静态一致 | 通过 |
| 32/64 分档案 golden 与差分隔离 | `conformance.s9_profiles` 钉扎 §13.2.2 的 `sizeof`/字面量范围/`bitcast`/`trunc` 差异；`conformance.s9_static_parity` 分别在 `arm32-baremetal` 与 `aarch64-baremetal` 下比较 VM/AOT 首诊，跨档案不比较；`tests/golden/w32` 与 `w64` 分别执行；所有 S9 差分只比较同一档案 | 通过 |
| 四种设备策略、退出策略、UART/heap/trap 全部覆盖 | `conformance.s9_baremetal` 的 reject/read/readwrite/volatile 矩阵（普通/volatile 读与写）与退出记录；PL011 UART0/UART1 分离；4000 位前导零 token 经 freestanding arena（`TC-ARM32-S9-0021`、bare-metal 输入用例）；必选 trap 与 checked build 在 ARM32 上执行（`TC-ARM32-S9-0014`…`0019`）；8 MiB/128 KiB 运行时 arena | 通过 |
| 32 位 `int64`/`float64` 辅助与未对齐访问用例通过 | `conformance.s9_arm32` 覆盖 `int64`/`uint64` 乘除/移位/比较、binary64 算术/异常/格式化、32 位 `usize`/`isize`/`ptrdiff`/`memmove`/`n×sizeof T`；ARM EABI `__aeabi_uldivmod`/`__aeabi_ldivmod`/内存辅助由独立 `aeabi` 实现；未对齐 `int64`/`float64` 字段访问经共享对象/运行时路径 | 通过 |
| 无开放 B1 级规格偏差 | 80 码集未增删；无警告；诊断/首诊/位置规则未改；32 位复合实参按 AAPCS32 Rule C.4/C.5 传字、>4 字节返回经隐藏指针；VFP 变体寄存器类未使用；架构/ABI/区/运行时缝约定记录于 `src/CONTRACTS.md` | 通过 |

## 覆盖与测试

- 全量 `ctest`：58/58（Debug、Release、ASan+UBSan）。
- S9 套件：`conformance.s9_baremetal`、`conformance.s9_arm32`（25 固定 + 260 语料）、
  `conformance.s9_arm32_interop`（14）、`conformance.s9_static_parity`
  （1033 源 × 2 档案）、`conformance.s9_profiles`（7）、`determinism.s9`。
- 覆盖矩阵：636/636；设计 §12 十五项均有套件映射（第 4/7/14/15 项纳入 S9 用例）。
- 确定性：O0/O2 `.s` 逐字节一致；重复 `.s`/ELF/执行一致；checked `.s` 可重复且与
  release 分离；32/64 产物与结果不跨档案比较。

## 复现

需要 clang、llvm-ar（或 aarch64-elf-ar）、ld.lld、qemu-system-aarch64 与
qemu-system-arm；缺失时 CMake 配置失败。

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/conformance/platform/s9_baremetal_cases.py --check
python3 tests/conformance/platform/s9_arm32_cases.py --check
python3 tests/conformance/abi/s9_arm32_interop.py --check
python3 tests/conformance/abi/reverse_interop_cases.py --check
python3 tests/tools/abi_mutation_audit.py --check
python3 tests/tools/s9_capacity_report.py --check
python3 tests/conformance/tools/s9_static_parity.py --check
python3 tests/conformance/platform/s9_profiles.py --check
python3 tests/determinism/s9_determinism.py --check
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

## 补充验证（G9 后）

- 反向 interop：19 例 TC caller → C callee 覆盖整数/栈参数、int64 对、float32/float64、
  HFA 构造与返回、复合按引用/按值、间接返回与窄值。当前实现按符号性扩展
  ARM32 caller 的窄实参；AArch64 33 例、ARM32 14 例 C-caller 互操作与全部
  S8/S9 差分保持通过。
- ABI mutation 审计：B.4 阈值、HFA 寄存器耗尽、复合 `NGRN` 计数、ARM32 Rule C.5
  拆分四类注入均被 interop 套件发现，4/4 killed；工具为手动审计，不进入 ctest。
- 容量：900005 字节静态对象在 AArch64 的 static span 为 900016、ARM32 为 900012；
  ELF 分别约 1.01 MB/1.04 MB，AArch64 image_end 0x40906fe1、ARM32 0x21e541。
  1 MiB 静态区对当前最大用例仍有余量，但已接近上限；输入镜像窗口 1 MiB 属测试装入缝。
- ABI 参考与范围已固定到 `src/CONTRACTS.md`：AAPCS64 LP64 / 基 AAPCS32 的 2024Q3
  版本；over-aligned 类型、bit-field、vector、variadic 不在 TC 可表达 ABI 范围。

## 当前限制

- RISC-V 等后续目标与可选优化管线（SSA/regalloc/内联）属 S10；0.0.46 保持零 pass。
- 32 位目标使用 Cortex-M7 FPv5-D 内部浮点单元与 softfp ABI；VFP 寄存器类调用变体
  未实现。`arm32-baremetal` 的 stdout/终止使用 semihosting，退出记录使用 CMSDK UART0。
- qemu-user/Linux-user 不在门禁路径；门禁使用 qemu-system。
- `arm32-baremetal` 的栈为 1 MiB、`D=32`；静态区 1 MiB、运行时 arena 128 KiB。
  AArch64 freestanding 实例 arena 8 MiB、帧区/栈按档案定义。超出属实现错误（71）。
- S9 checked build 覆盖合法访问与必选 trap；完整 checked UB 矩阵以 AArch64 S8 证据为准。
- 900005 字节静态对象已占 1 MiB 静态区的约 86%；若后续要求更大的静态数据或
  精确 pointer-provenance checked 语义，需要新的 profile/ABI 设计，不在 0.0.46 范围。
