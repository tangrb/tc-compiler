# TC 0.0.46 S1 门禁记录（G1）

**结论：G1 PASS，允许进入 S2。**

## 范围

LEX/SYN 与扁平 parse IR：UTF-8 读取/解码、精确十进制字面量转换、注释/续行、最长匹配、
关键字/助记符/后缀/注解、EBNF、模式/层序/可见性、字段路径、命名实参、`switch` 目标、
span 与行列；只报告七个 LEX/SYN 码。

## 交付与验证

| 项 | 证据 |
| ---- | ---- |
| 读取/LEX/SYN/parse IR | `src/frontend/input.*`、`num.*`、`lex.*`、`parse.*` |
| LEX/SYN 诊断与位置 | `frontend/diag.h`；`tests/conformance/static/{lex,syn,accept}` |
| L1 | `tc_frontend_test`：词法/语法、全部 opcode、parse IR 形态、位置、限制、确定性 |
| L3 | `s1_cases.py` 生成并 `--check`；诊断/接受/属性用例 |
| 工具 | `--syntax-only` 部分检查；普通调用退出 70，不宣称可执行 |

复现：`ctest --test-dir build -R 'frontend.unit|conformance.s1_cases|determinism.s1' --output-on-failure`。
