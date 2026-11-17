# G8 门禁报告（0.0.46）

**结论：PASS。S8 出口准则满足，允许进入 S9。**

## 范围

S8 交付：全量运行时（浮点/转换/位重解释、复杂内存与指针、`make`/`copy`/`memmove`、
struct 布局/复制/bool 规范化、volatile、设备/退出策略）；全量 AArch64 后端
（AAPCS64 复合/HFA/大复合按引用指针/大 struct 间接返回、`fnptr`/`call_indirect`、`funcaddr`/
`funceq`/`funcne`、多尺寸与未对齐 `load`/`store`、`int64`/`float64`、checked
build）；入口/产物（复位入口、区基址、静态区 `.data`、代码区、ELF、禁用 ICF）；
VM/AOT 一致性（同档案区内偏移、静态初值字节、`const` 内联）；checked build
（越界/悬垂/one-past/未初始化，必选 trap 不变）。实现依据为标准 §1.2/§1.9、
§3/§6/§7/§8/§10/§11/§13，设计 §7/§8/§10/§12，以及测试计划 S8。

## 出口准则证据

| G8 准则 | 证据 | 结论 |
| --- | --- | --- |
| 全量 opcode 覆盖；全量 runtime golden 通过 | `conformance.s8_diff`：97 个 W=64 S6 运行用例 + 53 个 S8 用例（struct/HFA/大复合按引用/9–300 参数/递归/间接调用/大偏移/静态字节/`@fast_math`）共 150 例全部 VM↔AOT 一致；S6 运行时与 W=32/64 golden 通过；`aot.unit` 58 项钉扎全量 lowering 形态（浮点/内存 helper、HFA v 寄存器、x8 间接返回、checked shadow、帧描述符） | 通过 |
| 全量 VM↔AOT 差分零不一致 | `conformance.s8_diff` 150 例、`conformance.s8_abi` 9 例、`conformance.s8_consistency` 8 项、`conformance.s8_checked` 38 例 + 97 个 checked 合法夹具；生成式 ABI/布局/内存/浮点 fuzz 与静态变异双工具首诊一致（默认证集 ABI 48/布局 48/内存 48/浮点 48/静态 240）；S7 差分/ABI/工具链套件继续通过；比较 stdout 字节、首个 `TC_RE_*`、退出状态，地址仅以定义关系（`ptrdiff`/`funceq`/区内偏移）观察 | 通过 |
| §13.4 八项全部有证据；设计 §12 十五项有套件映射 | `conformance.s8_consistency` 输出八项映射（静态一致、模块顺序、CFG/DI、类型与转换、安全、浮点、I/O、平台）；`conformance.s8_artifact` 以 `-ffreestanding -nostdlib` 运行时、无解释器/无动态依赖/无 libc 符号审计覆盖设计 §12 第 4 项；覆盖矩阵 636/636 | 通过 |
| checked build 与必选 trap 分开统计，且不改变合法程序集合 | `conformance.s8_checked`：21 个 UB 检出/捕获用例（未初始化、one-past、越界、悬垂、零长度 one-past `memmove`、跨字段/填充误报回归、checked 浮点下溢、设备访问）与 17 个合法用例（padding/跨字段访问、字段 one-past 回退、参数/静态定义字节）；必选 trap 在 checked 构建中保持；全部 97 个 W=64 S6 合法夹具在 checked 下观察行为不变；对象检查按访问区间解析，指针来源限制记录于 `CONTRACTS.md` | 通过 |
| 全量 ABI 用例通过；ICF 禁用验证通过 | `conformance.s8_abi` 9 个可执行用例 + 汇编形态审计（HFA v 寄存器、x8 间接返回、16 字节栈步长、直接/间接调用、大复合调用者副本与指针传递）；`conformance.s8_interop` 以 clang 作为独立 AAPCS64 调用方验证 30 个大复合/HFA/寄存器溢出/窄整数/间接返回用例；`conformance.s8_artifact` 断言同体函数入口不同，`ld.lld --icf=none` | 通过 |
| 无开放的一级规格偏差 | 规范码集仍为 80；无警告；诊断/首诊/位置规则未改；S8 新增能力均有标准条款与用例映射；静态布局超出档案静态区在 VM/AOT 均报告实现错误（71）且不发布产物 | 通过 |

## 覆盖与测试

- 全量 `ctest`：Debug/Release/ASan+UBSan 均 51/51。
- 单元：frontend 688、sem 102、ct 370、ir 674、vm 413、aot 58 检查。
- 覆盖矩阵：636/636 covered；设计 §12 十五项均有套件映射。
- 零 pass：O0/O2 `.s` 逐字节一致；重复 `.s`/ELF/执行可重复；checked `.s` 自身可重复
  且与 release 分离；`@fast`/`@inline` 仍只记录不消费。

## 复现

需要 clang、llvm-ar（或 aarch64-elf-ar）、ld.lld 与 qemu-system-aarch64；
缺失时 CMake 配置失败；`-DTC_ENABLE_AOT_TESTS=OFF` 只关闭 lld/qemu 测试项。

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/conformance/diff/s8_cases.py --check
python3 tests/conformance/diff/s8_abi_fuzz.py --check
python3 tests/conformance/diff/s8_layout_fuzz.py --check
python3 tests/conformance/diff/s8_mem_fuzz.py --check
python3 tests/conformance/diff/s8_fp_fuzz.py --check
python3 tests/conformance/abi/s8_abi_cases.py --check
python3 tests/conformance/abi/s8_interop_cases.py --check
python3 tests/conformance/checked/s8_checked_cases.py --check
python3 tests/conformance/consistency/s8_consistency_cases.py --check
python3 tests/conformance/tools/s8_artifact_cases.py --check
python3 tests/conformance/tools/s8_static_fuzz.py --check
python3 tests/determinism/s8_determinism.py --check
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

## 当前限制

- AOT 平台实例为 qemu-system-aarch64 + semihosting（hosted `aarch64-linux-user`
  档案的 W=64 实例）。运行时已按 freestanding 编译/链接并通过符号审计，但完整
  裸机平台实例（PL011 stdio、裸机链接脚本/设备策略）与 qemu-user/EL0 运行时、
  32 位后端仍属 S9；`tc-aot` 对 `w32` 与非 `TC_DEV_REJECT` 的档案显式退出 70，
  不发布产物。
- 设备区当前只映射 `TC_DEV_REJECT`；其它三种策略需要 S9 的设备实例。
- 语言核心没有分配 API，宿主（堆）区对象由运行时库创建；从其地址访问仍按
  `TC_RE_ADDR_UNAVAILABLE` 处理。
- 绝对区基址由平台实例/链接器决定，属环境定义；VM/AOT 只保证同一档案下的区内
  偏移与静态初值字节一致，差分不比较绝对地址。
- AOT 输入缝为 QEMU loader 提供的镜像（8 字节长度 + 字节），按短读路径消费；
  空镜像为 EOF；设备端输入属 S9。
- 静态对象布局必须适配档案静态区（当前 1 MiB）；超出时两个工具都以实现错误退出
  71 且不发布产物。AOT 在编译期按最坏帧足迹检查栈预算，超出时同样退出 71。
- checked 对象检查按字节区间解析，不追踪指针来源；相邻对象/复用帧槽可能隐藏 UB，
  但不会拒绝落在同一包含对象内的合法 padding/跨字段访问。
