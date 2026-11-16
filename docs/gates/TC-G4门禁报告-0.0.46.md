# G4 门禁报告（0.0.46）

**结论：PASS。S4 出口准则满足，允许进入 S5。**

## 范围

S4 交付：CFG/可达性、DI 最大固定点、`MISSING_RETURN`、标签与控制指令、
调用检查顺序与调用安全、`unsafe` 上下文矩阵、注解静态矩阵。实现依据为
[标准](../TC语言及编译器标准-0.0.46.md) §5.4/§5.7、§6.15、§7、§8、§9、§10.9、
§12，以及[设计](../TC原生编译器设计说明书-0.0.46.md) §3.3/§5/§6。

## 出口准则证据

| G4 准则 | 证据 | 结论 |
| --- | --- | --- |
| DI 固定点与路径语义 | `TC-SEM-DI-0001`…`0009`/`TC-SEM-CFG-0024`/`0029`（分支/汇合/回边/switch/字段/赋值/read 目标/函数单元、标签错误与 DI 首诊）、`TC-TOOL-SEM-0101`/`0102`/`0115`/`0117`/`0123` | 通过 |
| `MISSING_RETURN` | `TC-SEM-CFG-0001`…`0028`（空体、`br_if` 常量边、函数源序、模块序、标签/控制/调用原子性、重复标签/畸形分支的首诊）、`TC-TOOL-SEM-0104`（void/`trap`/无出口循环） | 通过 |
| 调用参数顺序 | `TC-SEM-CALL-0101`…`0127`（重复/未知/缺失/数量/顺序/混用、安全相位、间接调用）、`TC-TOOL-SEM-0106` | 通过 |
| 调用安全优先 | `TC-SEM-CALL-0107`/`0114`/`0124`/`0126`、`TC-SEM-SAFE-0114` | 通过 |
| 标签/控制 | `TC-SEM-CFG-0005`…`0029`、`TC-TOOL-SEM-0112`/`0118` | 通过 |
| 安全静态保证 | `TC-SEM-SAFE-0101`…`0117`、`TC-SEM-CALL-0108`…`0111`/`0121`、`TC-TOOL-SEM-0107`/`0108`/`0113`/`0114`/`0119` | 通过 |
| `@safe` 优先与空 unsafe 块 | `TC-SEM-SAFE-0109`…`0111`/`0114`/`0116` | 通过 |
| 注解矩阵与 const 端点 | `TC-SEM-ANN-0201`…`0258`、`TC-SEM-CONST-0004`…`0026`、`TC-TOOL-SEM-0109`/`0111`/`0116`/`0124`/`0125`/`0126`/`0127`/`0128`/`0129`/`0130`/`0131`/`0132`/`0133`/`0134`/`0135`/`0137`/`0138`/`0139`/`0140`/`0141`/`0142`/`0143`/`0144`/`0145`、`TC-UNIT-ANN-0001` | 通过 |
| 承诺违反不静态拒绝 | `TC-TOOL-SEM-0110` | 通过 |
| 首诊组合（§12.2 #1–#10） | L0 比较器测试；S4 组合 `TC-SEM-CFG-0003`/`0004`/`0016`/`0017`/`0022`/`0023`/`0024`/`0027`/`0028`/`0029`、`TC-SEM-CALL-0101`/`0105`/`0106`/`0107`/`0114` | 通过 |
| 32/64 注解参数 | `TC-SEM-ANN-0213`/`TC-TOOL-SEM-0120`（w32）、`TC-TOOL-SEM-0017`/`0121`（w64） | 通过 |

## 覆盖

- 一致性用例：S4 表 163 个编译用例、44 个接受用例；S1/S2/S3 表继续通过。
- 80 码：S4 涉及的 `DUPLICATE_LABEL`、`LABEL_NOT_FOUND`、`CONDITION_TYPE`、
  `SWITCH_TYPE`、`RETURN_TYPE`、`RETURN_FORM`、`RETURN_OUTSIDE_FUNCTION`、
  `MISSING_RETURN`、五个 `ARGUMENT_*`、`UNINITIALIZED`、四个安全码、
  `ANNOTATION`、`MODE` 均有触发/位置/阶段与正例。
- 覆盖矩阵：636 槽中 344 covered / 292 uncovered / 0 suspended；未覆盖项属于
  S5–S10（CT、IR 最终化、opcode 运行时、VM/后端、平台）。
- 确定性：S4 用例在重复运行、不同 `PYTHONHASHSEED` 与反向用例顺序下 JSON 一致。

## 复现

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/conformance/static/s1_cases.py --check
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/conformance/static/s4_cases.py --check
python3 tests/traceability/coverage.py --check
python3 tests/tools/docs_cleanup_check.py
```

## 当前限制

- `@align`/`@range` 的常量路径端点由 SEM 常量解析器求值：字面量（含
  `inf`/`nan`）、`Self.K`/`m.K`、const 结构体字段读取、整数纯运算（含六种
  比较）、`sizeof`、数值/布尔 `cast`/`trunc`/`bitcast`、标量浮点
  算术/转换（含 `mod`、int→float 链）。合法值按 W 校验并写入 `TcParam`；
  strict 求值失败返回未解析，不产生 SEM 注解诊断，由 CT 报
  `TC_CE_CONST_*`。浮点求值使用固定 binary32/64 位规则，不依赖宿主
  `fenv`/`long double`；异类型 NaN canonical、精确非规格化保留；见
  [CONTRACTS](../../src/CONTRACTS.md)。
- `call_indirect nullptr` 的无签名静态定型仍按 `LITERAL_TYPE` 拒绝；运行时
  `TC_RE_NULL_FUNCTION_POINTER` 路径属于 VM 阶段。
- CFG/DI 是前端内部逐单元事实，尚未物化为 `TcUnit`；IR 最终化属于 S5。
- 实现资源上限（4 MiB 源文件、16 KiB Token、256 层嵌套）仍返回
  `tc_error_internal`（退出 71），不是语言诊断。
