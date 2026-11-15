# TC 0.0.46 S3 门禁记录（G3）

**结论：G3 PASS，允许进入 S4。**

## 范围

名称、类型、转换与布局：模块成员/命名空间/可见性、类型表 intern 与 Analyze 后冻结、
字面量按 W 定型、`cast`/`trunc`/`bitcast` 矩阵、结构体布局/`@padding`、`sizeof` 类型参数、
字段/赋值路径、指令形状/类型与模式矩阵、`@align`/`@range` 的 W 范围。

## 交付与验证

| 项 | 证据 |
| ---- | ---- |
| 名称/可见性/成员 | `src/frontend/sem.*`；`TC-SEM-NAME-*` |
| 类型表/等价/冻结 | `tc_sem_test`；`TC-UNIT-TYPE-0001`、`TC-UNIT-CONV-0001@w32/@w64` |
| 字面量/转换/模式 | `TC-SEM-LIT-*`、`TC-SEM-CONV-*`、`TC-SEM-MODE-*`；36 格 bitcast 矩阵 |
| 结构体/布局/字段 | `TC-SEM-FIELD-*`、`TC-SEM-LAYOUT-*`；`TC-UNIT-LAYOUT-0001@w32/@w64` |
| 指令形状/位置 | `TC-SEM-OP-*`；命名操作数与 `OPERAND_COUNT` 助记符定位 |
| 32/64 | `flip32_64` 17 行 × w32/w64；`w32` 静态档案 |
| 覆盖 | 245 covered / 389 uncovered（未覆盖属 S4–S10）；`flip32_64` 34/34、`first_diag_rule` 10/10 |

复现：

```sh
ctest --test-dir build --output-on-failure
python3 tests/conformance/static/s2s3_cases.py --check
python3 tests/traceability/coverage.py --check
```

## 当前限制与移交

- CT、CFG/DI、调用顺序/安全、完整注解矩阵、VM 与后端尚未实现；`--static-only` 退出 0
  不代表程序有效。
- 最终 `TcInstr`/`TcIR` 物化在 IR 最终化阶段；S3 提供并冻结类型/名称/opcode/布局事实。
- `call_indirect nullptr`、无符号 `-0`、非 `@fast` 的 `.nsw/.nuw` 符号优先级在 S4 固定。
- `sem.c` 的按名称/类型/指令/布局拆分在 S4 进行。
