# TC 0.0.46 S2 门禁记录（G2）

**结论：G2 PASS，允许进入 S3。**

## 范围

模块发现与 IMPORT：根 `#program` 校验、import 源序 DFS、导入目录→include roots 搜索、
解析路径实例键、`module_order` 固定、五个 IMPORT 码与导入名冲突；`#program` 导入目标
仍做 LEX/SYN 但不递归。

## 交付与验证

| 项 | 证据 |
| ---- | ---- |
| 发现/搜索/实例 | `src/frontend/module.*`；`TC-UNIT-MODULE-0001/0002` |
| 五个 IMPORT 码 | `TC-IMP-0001…0005`；阶段/位置/关联位置断言 |
| 阶段优先 | `TC-IMP-0006/0007`（IMPORT 先于 SEM；LEX/SYN 先于已记录 IMPORT） |
| 钻石/同名实例 | `TC-TOOL-SEM-0006`、`TC-TOOL-SEM-0024`、`TC-UNIT-MODULE-0002` |
| 确定性 | `determinism.s2s3`；harness JSON 哈希种子/顺序扰动一致 |

复现：`ctest --test-dir build -R 'sem.unit|determinism.s2s3' --output-on-failure`。
