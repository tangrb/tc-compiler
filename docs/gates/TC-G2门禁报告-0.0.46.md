# TC 0.0.46 S2 门禁报告（G2）

> **阶段**：S2 模块发现与 IMPORT（对应设计 §13 P1 的模块/导入部分）
> **依据**：[开发测试计划](../TC开发测试计划-0.0.46.md) §5 S2、§7、附录 D；
> [标准](../TC语言及编译器标准-0.0.46.md) §1.6–§1.8/§4.1–§4.11/§12.2/§12.5.1/§13.5；
> [设计](../TC原生编译器设计说明书-0.0.46.md) §3.4/§5/§6/§14。
> **执行方式**：agent 执行；独立只读审计由 `gate-auditor` 执行（非作者、未参与实现）。
> **结论**：**G2 PASS；允许进入 S3。**（独立审计结论见 §10；若审计发现未关闭，以 §10 为准。）

## 1. 结论

| 项 | 结果 |
| ---- | ---- |
| S2 开发交付 | **PASS** — 根 `#program` 校验、import 源序 DFS、导入目录→include roots 搜索、解析路径实例键、`module_order` 固定、五个 IMPORT 码与导入名冲突缓冲、成员/签名入口事实收集 |
| S2 测试活动 | **PASS** — 28 个 import/库夹具 L3 用例；L1 `TC-UNIT-MODULE-0001/0002`；阶段优先组合用例；`determinism.s2s3` |
| 自动化证据 | **PASS** — Debug/Sanitizer 构建与 `ctest` 通过；`--check` 生成器与覆盖矩阵一致 |
| 独立门禁审计 | **PASS** — 无 S2 阻塞发现；S3 布局问题记入 G3 并已关闭（§10） |
| **G2** | **PASS** |
| 进入 S3 | **允许** |

## 2. 范围与依据

本阶段只实现并验证**模块发现与 IMPORT**：

- 模块发现：根必须是 `#program`；按 `import` 源序深度优先；每个成功解析的模块只做一次 LEX/SYN 并加入 `module_order`；LEX/SYN 错误立即停止，导入错误先缓冲（标准 §4.11.1）。
- 搜索与实例：导入文件所在目录 → include roots（按顺序）→ 首个匹配；模块身份是解析后的文件路径；同名不同路径是不同实例；钻石依赖复用同一实例（标准 §4.5/§4.11.2）。
- IMPORT 校验：目标存在性、目标必须 `#lib`、循环优先于重复、同一导入者重复目标、导入名占用模块命名空间；错误在 IMPORT 阶段报告（标准 §4.5/§4.11.2/§12.2）。
- 成员/签名入口：导入成功后进入成员表构建；模块成员命名空间冲突、可见性事实和函数签名类型在后续 SEM 阶段解析。IMPORT 本身不报告名称/类型/CFG 诊断（标准 §4.11.2 第 7 条）。
- `#program` 导入目标：仍执行 LEX/SYN，但不递归其 import，IMPORT 报 `TC_CE_IMPORT_NOT_LIB`（标准 §4.11.1 第 3 条）。

不在本阶段范围：名称解析/类型/布局（S3）、CFG/DI/调用/安全/注解（S4）、CT（S5）、VM/AOT（S6+）、静态存储实际分配（S5）。这些在覆盖矩阵中保持显式未覆盖。

## 3. 环境与可重复性

| 项 | 值 |
| ---- | ---- |
| 宿主 | macOS arm64（Apple Silicon） |
| C 编译器 | Apple clang 21.0.0.21000334 |
| CMake | 4.4.2 |
| Python | 3.14.7（仅标准库，无网络） |
| 构建 | Debug、ASan+UBSan（`-fsanitize=address,undefined -fno-omit-frame-pointer`） |
| 固定环境 | `LC_ALL=C`、`LANG=C`、`TZ=UTC`；每用例独立临时目录；无网络 |
| 工具入口 | `tc-vm`/`tc-aot`；`--static-only` 为部分检查，普通调用在静态阶段后仍退出 70 |

`determinism.s2s3` 对 S2/S3 源各重复运行 3 次，并以 `PYTHONHASHSEED=0/1/42` 和逆序 YAML 参数运行 harness；harness JSON 逐字节一致。

## 4. 交付物与证据

| S2 交付 | 位置 | 证据 |
| ---- | ---- | ---- |
| 模块发现/DFS/`module_order` | `src/frontend/module.h`、`module.c` | `TC-UNIT-MODULE-0001`（钻石 root→a→c / root→b→c = root,a,c,b）；`TC-TOOL-SEM-0006` |
| 搜索顺序/include roots | `module.c` | `TC-TOOL-SEM-0007`（importer 目录后有 include root）；`TC-TOOL-SEM-0024`（left/right 两个 include 根绑定不同 `shared.tc`） |
| 解析路径实例键 | `module.c`；`TcModuleInfo.path` | `TC-UNIT-MODULE-0002`（同名 `shared.tc` 在 left/right 是两个实例，5 个模块）；`TC-TOOL-SEM-0024` |
| `IMPORT_NOT_FOUND` | `module.c` | `TC-IMP-0001`（位置=import 模块名） |
| `IMPORT_NOT_LIB` | `module.c` | `TC-IMP-0002`（目标 `#program` 仍 LEX/SYN，不递归） |
| `DUPLICATE_IMPORT` | `module.c` | `TC-IMP-0003`（第二个 import 定位，关联首次） |
| `CIRCULAR_IMPORT` | `module.c` | `TC-IMP-0004`（root→a→root；最早 import 边；循环优先于重复） |
| `IMPORT_NAME_CONFLICT` | `module.c` | `TC-IMP-0005`（导入名与本地成员/局部名冲突，与文本位置无关） |
| 阶段边界 | `module.c`、`sem.c` | `TC-IMP-0006`（IMPORT 码先于被导入模块 SEM 错误）；`TC-IMP-0007`（后解析模块的 LEX/SYN 错误先于已记录 IMPORT 错误） |
| 成员/可见性入口 | `src/frontend/sem.c` | S3 使用同一成员表；S2 不报告名称/类型诊断 |
| 覆盖矩阵 | `tests/traceability/coverage.*` | 5 码 × 4 槽 + `chapter:§4` + `first_diag_rule:07` + `design12:13` 已 covered |

## 5. G2 出口准则（计划 §5 S2）

1. **5 个 `TC_CE_IMPORT_*` 码全部覆盖，且均在 IMPORT 阶段报告**：**PASS**。`TC-IMP-0001`…`0005` 断言 `stage: IMPORT`、码与主位置；覆盖矩阵四槽均 covered。
2. **钻石依赖/循环/重复/同名不同路径用例全部通过**：**PASS**。`TC-TOOL-SEM-0006`、`TC-IMP-0003/0004`、`TC-UNIT-MODULE-0001/0002`、`TC-TOOL-SEM-0024`。
3. **`module_order` 在扰动下稳定**：**PASS**。DFS 源序、路径规范化、线性路径索引；`determinism.s2s3` 与 `TC-UNIT-MODULE-0001`。
4. **IMPORT 阶段不产生 SEM 名称/类型/CFG 诊断**：**PASS**。IMPORT 缓冲只含五个 IMPORT 码；`TC-IMP-0006` 证明 IMPORT 先于 SEM，且 SEM 错误不抢先。
5. **模块实例键控（路径）在签名、静态存储、成员表上一致**：**PASS（S2 范围）**。`TcModuleInfo.path` 为解析路径；左/右同名实例分离已测。静态存储对象与 const 实例化属 S5，本阶段只固定实例键。

## 6. 附录 D.1 通用门禁

| 检查项 | 结果 | 依据/范围 |
| ---- | ---- | ---- |
| 80 码表未增删 | **PASS** | `trace.codes`/`trace.gen_c_table`/`trace.harness_codes` |
| 新增用例有 `spec` 与可追溯 | **PASS** | `s2s3_cases.py` 每项含 `spec`；生成文件 `--check` |
| 覆盖矩阵已更新；未覆盖解释 | **PASS** | 5 码、§4、rule 07、design12 13 已覆盖；S4+ 显式未覆盖 |
| 首诊/位置断言覆盖本阶段相关码 | **PASS** | 五码 trigger/position/stage；导入名/第二次 import/最早环边定位 |
| 32/64 分档案；无跨档案比较 | **N/A (S2)** | S2 结果与 W 无关；不比较跨档案 |
| 确定性套件 | **PASS** | `determinism.s2s3`；harness JSON 哈希种子/顺序扰动 |
| 无新增警告/标准外诊断 | **PASS** | `-Wall -Wextra -Werror -Wpedantic`；IMPORT 缓冲只接受五个码 |
| 构建/消毒器/符号审计 | **PASS** | Debug + ASan+UBSan `ctest`；无安装/公共 ABI |
| 报告记录工具链/环境 | **PASS** | §3 |

## 7. 附录 D.2 S2 附加检查

| 附加检查 | 结果 | 证据 |
| ---- | ---- | ---- |
| IMPORT 码在 IMPORT 阶段 | **PASS** | `TC-IMP-0001`…`0007` |
| `module_order` 稳定 | **PASS** | `TC-UNIT-MODULE-0001`；determinism |
| 实例键（解析路径） | **PASS** | `TC-UNIT-MODULE-0002`、`TC-TOOL-SEM-0024` |
| `#program` 导入目标不递归 | **PASS** | `TC-IMP-0002` |
| 循环优先于重复 | **PASS** | `TC-IMP-0004`（root→a→root 同时构成重复引用，首诊仍为 CIRCULAR） |

## 8. 测试与覆盖证据

### 8.1 L1

| 用例 | 内容 |
| ---- | ---- |
| `TC-UNIT-MODULE-0001` | 钻石依赖 `root, a, c, b`；`c` 只解析一次 |
| `TC-UNIT-MODULE-0002` | left/right 两个 `shared.tc` 实例分离；`module_order` 含 5 个实例 |
| `TC-UNIT-LAYOUT-0001@w32/@w64` | 类型表在 Analyze 后冻结（S2 导入闭包进入 S3 的前置） |

### 8.2 L3（S2 部分）

`sem.unit`、`conformance.s2s3_cases`、`harness.conformance` 共 183 个 S2/S3 用例通过；其中 S2 相关 28 个 import/库夹具用例与 2 个阶段组合用例：

- 五码：`TC-IMP-0001`…`0005`；
- 阶段：`TC-IMP-0006/0007`；
- 钻石/实例/接受：`TC-TOOL-SEM-0006/0007/0024`；
- 同名不同路径：`TC-UNIT-MODULE-0002`。

### 8.3 覆盖

`coverage.py --check` 当前 245 covered / 389 uncovered / 0 suspended（634 slots）。S2 覆盖 20 个码槽、§4 正/反例、rule 07、design12 13。未覆盖项均属 S4–S10。

## 9. 范围说明（不是阻塞项）

- 静态存储对象的实际分配、跨分支复用与初值属 S5；S2 只固定实例键与成员入口。
- 函数签名的完整类型解析在 S3 的 Analyze 首过中完成；S2 收集可供其使用的模块成员/函数声明入口，且 IMPORT 不产生名称/类型诊断。
- 名称/类型/字段/布局诊断属 S3；CFG/DI/调用顺序/安全/注解属 S4。
- `w32` 是静态分析档案，不是可执行目标；32 位 ABI 目标选择属 S9。

## 10. 独立审计

独立只读代理 `gate-auditor`（非作者、未参与实现、未修改工作树）从标准/设计独立推导，运行完整 `ctest`、`s2s3_cases.py --check`、`coverage.py --check`，并用 `/tmp` 下的临时 `.tc` 探测。

- 审计范围：S2 模块发现/IMPORT。
- 复核项：五个 IMPORT 码的阶段/位置/关联位置；not-found/not-lib/duplicate/circular/name-conflict；循环优先于重复；钻石 `root,a,c,b`；复用目标不重复解析；include roots 顺序改变绑定；同名不同路径实例分离；IMPORT 先于 SEM、LEX/SYN 先于 IMPORT；`module_order` 确定性。
- 结论：**G2 PASS**。审计未发现 S2 的 B1/B2/B3/B4；审计首轮发现的布局顺序问题属 S3，已记入 G3 并在 G3 复核关闭。独立审计同时确认实现注释未引用阶段编号/门禁/测试计划。

## 11. 门禁判定

**G2 PASS。** 五个 IMPORT 码、顺序/循环/重复/同名实例、阶段边界与确定性均满足计划 §5 S2 与附录 D.2 S2 附加检查；未发现阻塞性 B1/B2/B3/B4。允许进入 S3，S3 证据见 [G3 报告](TC-G3门禁报告-0.0.46.md)。

## 附录 A. 复现命令

```sh
cmake -S . -B build && cmake --build build -j8
ctest --test-dir build --output-on-failure
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/traceability/coverage.py --check
ctest --test-dir build -R 'sem.unit|conformance.s2s3_cases' --output-on-failure
ctest --test-dir build -R 'determinism.s2s3|harness.conformance' --output-on-failure
```

## 附录 B. reviewed commit / 证据版本

- reviewed commit：`b7b9704`（S2/S3 实现）；基线 `3a12ab3`。
- 独立审计：`gate-auditor` 复核 G2 PASS；S3 布局问题由 G3 关闭。
- 生成文件指纹由 `s2s3_cases.py --check` 与 `coverage.py --check` 固定。
- 报告时间：2026-11-15。
