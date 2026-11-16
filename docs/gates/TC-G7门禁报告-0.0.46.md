# G7 门禁报告（0.0.46）

**结论：PASS。S7 出口准则满足，允许进入 S8。**

## 范围

S7 交付：AArch64 子集 lowering（整数/位/比较、控制流、直接调用、`read`/`write`/
`writeln`、`sizeof`、`copy`）；AAPCS64 子集（x0–x7/x0、16 字节栈对齐、x19/x20
callee-saved、bool/int8/int16 扩展、`D`）；帧/静态区布局与汇编发射；`tc-aot`
`--emit=asm|obj|elf`；clang 集成汇编 + lld + qemu-system 驱动；L5 差分、L6 ABI/
工具链与零 pass 证据。实现依据为标准 §1.2/§1.9、§6.3–§6.13、§10、§11、§13，
设计 §7/§8/§10/§11/§12，以及测试计划 S7。

## 出口准则证据

| G7 准则 | 证据 | 结论 |
| --- | --- | --- |
| 子集所有用例 VM↔AArch64 差分零不一致 | `conformance.s7_diff` 37 例（复用 24 个 S6 W=64 用例 + 13 个 64 位/静态/开关边界用例）；`conformance.s7_abi` 9 例；`conformance.s7_fuzz` 40 个固定种子生成程序；每例 stdout 字节、首个 `TC_RE_*`、退出状态三方一致（期望/VM/AOT） | 通过 |
| `.s` 可重复；链接/运行可重复；qemu 结果稳定 | `determinism.s7`：O0/O2 与重复构建 `.s` 逐字节一致；重复 ELF 构建逐字节一致；同一 ELF 三次执行观察值一致；`conformance.s7_tools` 检查 `--emit=obj/elf`、ELF 头、重复运行 | 通过 |
| 子集必选 trap 在 release 构建执行 | Release `ctest` 39/39；用例含整数溢出、除零、负移位、`div(INT_MIN,−1)`、显式 `trap`、`D`、`TC_RE_IO`；release AOT 无 checked-build 分支 | 通过 |
| 前端静态结果与 VM 完全一致 | `conformance.s7_static_parity`：`tests/conformance/static` 的 992 个 `.tc` 在 `tc-vm --static-only` 与 `tc-aot --static-only` 下 exit/stdout/stderr 逐字节一致（仅归一化工具名） | 通过 |
| 零 pass：O0/O2 无 IR 变换，结果一致 | O0/O2 `.s` 逐字节一致；`TC-RT-OPT-0001` 与 `s7.diff` 的 O2 用例输出一致；`@fast`/`@inline` 仍只记录不消费 | 通过 |

## 覆盖与测试

- 全量 `ctest`：Debug/Release/ASan+UBSan 均 39/39。
- 单元：frontend 688、sem 102、ct 370、ir 450、vm 413、aot 34 检查；新增
  `aot.unit` 钉扎 lowering 形态、帧 16 字节对齐、静态初值、确定性与未支持拒绝。
- 覆盖矩阵：630/636 covered；剩余 6 项为 design §12 第 2/4/10 项的 S8–S9
  套件（一致性八项、freestanding、`@fast_math`）。design §12 第 1/7/15 项由
  S7 差分/ABI/零 pass 用例覆盖。
- 子集边界由后端显式拒绝：指针/结构体/`cast`/浮点/间接调用/checked build/
  非 64 位档案均以 exit 70 失败且不发布产物（`conformance.s7_tools`）。

## 复现

需要 clang、ld.lld 与 qemu-system-aarch64（macOS 无 qemu-user，按设计 §11
使用 qemu-system 回退）：

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/conformance/diff/s7_cases.py --check
python3 tests/conformance/diff/s7_fuzz.py --check
python3 tests/conformance/abi/s7_abi_cases.py --check
python3 tests/conformance/tools/s7_toolchain_cases.py --check
python3 tests/conformance/tools/s7_static_parity.py --check
python3 tests/determinism/s7_determinism.py --check
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

## 当前限制

- AOT 仅实现 W=64 `aarch64-linux-user`；32 位后端、freestanding/PL011 与完整
  ABI（struct/HFA/浮点/间接调用）属 S8/S9。
- qemu-user 在 macOS 不可用；S7 以 qemu-system-aarch64 virt + semihosting 执行
  ELF。程序 stdout 走 semihosting 控制台，首个 trap 码与退出策略记录走 PL011
  侧信道，差分比较前者字节与后者的码/状态。
- `read` 的 AOT 平台输入缝为 QEMU loader 的 `tc-stdin.bin` 镜像（8 字节长度 +
  字节）；空镜像为 EOF。PL011 输入属 S9。
- 绝对区基址由装载器/平台实例决定；VM/AOT 只要求且已保证相同区内偏移。
  S7 子集拒绝一切地址可观察操作，差分不做地址归一化；S8 引入地址相关用例时
  必须补归一化。
- 帧/静态区上限、单源文件/Token/嵌套上限与 VM 相同；超出为 `tc_error_internal`
  （exit 71），不是语言诊断。
