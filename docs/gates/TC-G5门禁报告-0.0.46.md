# G5 门禁报告（0.0.46）

**结论：PASS。S5 出口准则满足，允许进入 S6。**

## 范围

S5 交付：CT 求值（allowlist、严格语义、错误映射）、跨模块按需求值与缓存、`const`/`static var`
静态初值、IR 最终化/冻结、IR 打印。实现依据为
[标准](../TC语言及编译器标准-0.0.46.md) §3.10/§3.11/§4.7/§4.10/§4.11.4、§6.3–§6.6、
§12.5.1、§13.2.2，以及[设计](../TC原生编译器设计说明书-0.0.46.md) §2.2/§3.1–§3.4/§5。

## 出口准则证据

| G5 准则 | 证据 | 结论 |
| --- | --- | --- |
| 6 个 CT 码全部覆盖、位置正确 | `TC-CT-OVERFLOW-0001`…`0004`、`TC-CT-DIVZERO-0001`…`0003`、`TC-CT-SHIFT-0001`/`0002`、`TC-CT-FLOATINVALID-0001`/`0002`、`TC-CT-FLOATUNDERFLOW-0001`/`0002`、`TC-CT-CAST-0001`…`0005`、`TC-CT-STATIC-0001`、`TC-CT-LOCAL-0001`（局部 const）；位置断言为目标声明 | 通过 |
| CT 在 SEM 全过后启动、无级联 | `TC-CT-STAGE-0001`（SEM 优先）、`TC-CT-ORDER-0001`…`0007`（前向/自引用、源序、模块序、引用失败归属、`make` 字段顺序） | 通过 |
| 同一常量只求值一次、成功/失败缓存 | `ct.unit` `TC-UNIT-CT-0002`…`0004`（钻石依赖计数、失败一次即停、局部 const） | 通过 |
| IR 形态覆盖设计 §12.11 | `ir.unit` `TC-UNIT-IR-0001`…`0007`：字段值/lvalue、`addr T, s.f`、命名实参、`switch` 目标、span、无源名、块边、不可达块、聚合 const 内联、打印 | 通过 |
| 类型表冻结后只读 | `sem.unit`、`ir.unit`（`ir->types` 同一冻结表；冻结后只使用 `tc_type_*_find`）；`CT`/IR 不再 intern | 通过 |
| 静态初值 | `TC-CT-STATIC-0001`、`TC-TOOL-CT-STATIC-0001`；`ir.unit` 校验 `TcIR.consts` 标量 bits/聚合 image、`TcObject.init`、W=32/64 布局与对齐 | 通过（VM/AOT 字节一致性属 S8） |
| 32/64 分档案 | `TC-CT-OVERFLOW-0004`/`TC-TOOL-CT-WCAST-0001`、`TC-CT-CAST-0004`/`0005` 与 `WCAST`/`WCASTVICE`、`TC-CT-LITERAL-0001`/`0002`、`TC-CT-WIDTH-0001`…`0003`；`ct.unit` W 矩阵；`ir.unit` 32/64 布局 | 通过 |

## 覆盖

- 一致性用例：`ct.unit` 370 checks、`ir.unit` 437 checks；S5 表 48 个编译用例、23 个接受用例；
  harness 全量 624/624，无 `not_implemented`。
- 覆盖矩阵：636 槽中 376 covered / 260 uncovered / 0 suspended；未覆盖项属于 S6–S10
  （VM/后端/ABI/平台/freestanding），不是 S5 遗漏。
- 确定性：S5 源在重复运行、不同 `PYTHONHASHSEED` 与反向用例顺序下 JSON 一致。

## 复现

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/conformance/static/s1_cases.py --check
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/conformance/static/s4_cases.py --check
python3 tests/conformance/static/s5_cases.py --check
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

## 当前限制

- VM/AOT 未实现：普通调用与 `--static-only` 只覆盖静态阶段（含 CT 与 IR 最终化），退出 0
  不表示程序有效；静态初值的 VM 预载/AOT `.data` 字节一致性按测试计划在 S8 关闭。
- SEM 部分检查使用 `--sem-only`；S4 接受用例使用该模式，S5/编译用例使用
  `--static-only`。
- `call_indirect nullptr` 的静态定型与 `-0` 用于无符号期望类型的读法仍按
  [CONTRACTS](../../src/CONTRACTS.md) 的约定挂起，属后续阶段澄清项。
- 文档化实现上限（4 MiB 源文件、16 KiB Token、256 层嵌套）仍返回
  `tc_error_internal`（退出 71），不是语言诊断。
- CT 浮点求值要求宿主 `FLT_EVAL_METHOD == 0` 且构建禁用 FMA 收缩；不满足的平台在编译期
  拒绝，以避免档案相关结果。
