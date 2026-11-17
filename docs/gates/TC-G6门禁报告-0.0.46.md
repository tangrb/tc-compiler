# G6 门禁报告（0.0.46）

**结论：PASS。S6 出口准则满足，允许进入 S7。**

## 范围

S6 交付：W 相关运行时语义核（整数 strict/wrap、除/模/移位边界、binary32/64
算术与异常优先级、精确下溢、`cast`/`trunc`/`bitcast`）；确定性 §11.2 格式化与
§11.3 输入；参考 VM（五区、运行时对象与字段子对象、指针状态、全部 51 个 opcode、
调用/`D`、I/O 记录、必选 trap、提交语义）；`tc-vm` 执行与 `error[RT]` 报告；
`aarch64-linux-user`、`aarch64-baremetal` 与 `w32` 档案。实现依据为
[标准](../TC语言及编译器标准-0.0.46.md) §1.2/§1.3、§3.4/§3.11/§3.12、§6.2–§6.13、
§11、§12.5.2、§13.2，以及[设计](../TC原生编译器设计说明书-0.0.46.md) §4/§7/§8/§9。

## 出口准则证据

| G6 准则 | 证据 | 结论 |
| --- | --- | --- |
| 全部 opcode 正常/边界/trap 用例通过 | 67 个 L4 运行时用例 + 63 个逐 opcode 静态形状用例；覆盖矩阵的 63 个 opcode 项 **63 normal + 29 trap + 63 static_error 槽全部 covered**；边界含 `INT_MIN`/`INT_MAX`、`div(INT_MIN,−1)`、`mod(INT_MIN,−1)`、`k>=n` 移位、NaN/±0/±inf/subnormal、自拷贝 `memmove`、one-past `ptradd`/`ptrsub`、`D` 边界 | 通过 |
| 15 个 `TC_RE_*` 均有触发用例；必选 trap 在 release VM 执行 | 每个码都有释放构建下的运行用例（`TC-RT-INT/F...`、`TC-RT-MEM...`、`TC-RT-CALL...`、`TC-RT-IO...`）；`TC-UNIT-VM-0006` 断言全部 15 个码的指令位置；必选 trap 不受 `--checked` 影响 | 通过 |
| VM golden 在 W=32/64 分别建立且不交叉比较 | `tests/golden/w64/` 8 个、`tests/golden/w32/` 3 个 `kind: run` golden；`golden.run` 按目录分别执行，无跨档案比较；独立复核记录见 `tests/golden/review-record.md` | 通过 |
| 提交/终止语义用例通过 | `TC-RT-CTRL-0002`（trap 前 stdout 保留、之后不提交）、`TC-RT-INT-0004/0005/0006`、`TC-RT-MEM-0005…0017`、`TC-RT-CALL-0002/0004`、`TC-RT-IO-0003/0004`；单元测试覆盖 sink 失败贡献 0 字节、`read` 失败不改目标 | 通过 |
| VM 可作为 S7 差分参考：同源同档案输出可重复 | `determinism.s6`：67 个用例各重复 3 次逐字节一致；harness 在 `PYTHONHASHSEED` 0/1/42 与反向用例顺序下 JSON 一致；W=32/64 分档案 | 通过 |

## 覆盖与测试

- `ctest` 32/32；harness 754/754（557 编译、129 工具、67 运行、1 属性）。
- 单元检查：frontend 688、sem 102、ct 370、ir 437、vm 413。
- 覆盖矩阵：636 槽中 626 covered / 10 uncovered / 0 suspended。未覆盖仅为
  design §12 的差分/ABI/freestanding/`@fast_math` 项（S7–S9；后续阶段已关闭）。
- 计划 §5 的 `@fast_math` 运行时边界（直接/间接调用不扩散）由 `TC-RT-CALL-0006`
  覆盖：零 pass 基线下直接调用、`fnptr` 间接调用、非 `@fast_math` 调用者与调用者
  自有浮点运算均保持 §6.3 严格语义；design §12 第 10 项的完整套件
  （`runtime/float` L4/L5 与内联相关验证）仍按计划 §8.2 归 S8。
- 构建/测试：Debug、Release、ASan+UBSan 全量 `ctest` 均为 32/32；输出不受
  locale/hash/用例顺序影响。

## 复现

验证环境：cmake 4.4.2、Apple clang 21.0.0（arm64-apple-darwin27.0.0）。
Debug 之外的两种构建配置（CMake 无内置消毒器开关，必须显式传编译/链接旗标；
均复跑同一全量 `ctest`）：

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/conformance/static/s1_cases.py --check
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/conformance/static/s4_cases.py --check
python3 tests/conformance/static/s5_cases.py --check
python3 tests/conformance/runtime/s6_cases.py --check
python3 tests/conformance/static/s6_static_cases.py --check
python3 tests/golden/validate.py
python3 tests/golden/run.py build/src/driver/tc-vm
python3 tests/traceability/coverage.py --check
python3 tests/traceability/check_case_ids.py
python3 tests/tools/docs_cleanup_check.py
```

## 当前限制

- 原生后端未实现：普通 `tc-aot` 仍在静态阶段后退出 70；VM↔AOT 差分属 S7。
- `--checked` 已实现对象越界/one-past 的 `TC_RE_ADDR_UNAVAILABLE` 安全检测，但
  poison/栈水位与未初始化读取检测属 S8；释放构建对 UB 访问可继续执行。
- 核心不创建宿主（堆）对象（分配 API 属运行时库）；堆区非对象地址按
  `TC_RE_ADDR_UNAVAILABLE` 安全处理，运行时库接入在 S8/S9 关闭。
- 指针状态由目标地址与对象表判定，不携带独立 provenance；one-past 与相邻对象
  起始地址重合时以最内层 live 对象为准。已定义的 `ptradd 0`/`ptrsub` 回对象用例
  通过，边界歧义记录于 [CONTRACTS](../../src/CONTRACTS.md)。
- 内置档案帧区/栈为 16 MiB；静态帧足迹耗尽该区域是实现错误（退出 71），不是
  语言诊断。设备区基址/大小/策略与正常/trap 退出状态属 §13.1 环境定义项，按
  [CONTRACTS](../../src/CONTRACTS.md) 记录的内置档案实例化。
- 相邻结构体字段是独立子对象：跨兄弟字段的 `memmove T` 且 `n>1` 不构成单个
  live 对象，按 UB 处理；字段 one-past 的 `ptradd 0`/`ptrsub` 回字段/`ptrdiff`
  仍在同一子对象内，属已定义行为。
- `call_indirect` 的签名不符/伪造为 UB（不检查）；未类型化 `nullptr` 仍在 SEM
  拒绝，已定型空 `fnptr` 在运行时报 `TC_RE_NULL_FUNCTION_POINTER`。
- `-0` 用于无符号期望类型的读法仍按 [CONTRACTS](../../src/CONTRACTS.md) 挂起。
