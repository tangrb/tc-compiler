# TC-Compiler 0.0.46 Structural Coverage Matrix

> Generated skeleton from the 0.0.46 standard, design and test plan.
> **Every row starts as `uncovered`; a row is `covered` only when it
> carries at least one `case_ids` entry.** S0 L0 rows named in
> `tests/unit/README.md` are explicitly covered; conformance cases remain
> uncovered until S1+. Missing coverage is explicit (test plan §3.6/§7.5).

- Language version: `0.0.46`
- Spec: docs/TC语言及编译器标准-0.0.46.md §12.3; §12.5.1; docs/TC开发测试计划-0.0.46.md 附录 A
- Rows: **634** total, **619** uncovered, **12** covered, **3** N/A (S0)

## Summary by category

| Category | Items | Slots | Covered | Uncovered | N/A (S0) |
|---|---:|---:|---:|---:|---:|
| Standard chapter (`chapter`) | 15 | 30 | 0 | 30 | 0 |
| Diagnostic code (`code`) | 80 | 344 | 0 | 344 | 0 |
| Opcode (`opcode`) | 63 | 155 | 0 | 155 | 0 |
| Design §12 item (`design12`) | 15 | 30 | 0 | 30 | 0 |
| First-diagnostic rule (§12.2) (`first_diag_rule`) | 10 | 10 | 7 | 0 | 3 |
| Same-position code priority (`same_site_priority`) | 5 | 5 | 5 | 0 | 0 |
| 32/64 flip (`flip32_64`) | 17 | 34 | 0 | 34 | 0 |
| Annotation (`annotation`) | 16 | 16 | 0 | 16 | 0 |
| Device policy (`device_policy`) | 4 | 4 | 0 | 4 | 0 |
| I/O source state (`io_source_state`) | 4 | 4 | 0 | 4 | 0 |
| Exit strategy (`exit_strategy`) | 2 | 2 | 0 | 2 | 0 |
| **Total** | **231** | **634** | **12** | **619** | **3** |

## Coverage rows

### Standard chapter (`chapter`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `chapter:§0:positive` | §0 原则/字长/目标相关静态结果 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§0:negative` | §0 原则/字长/目标相关静态结果 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§1:positive` | §1 抽象机/可观察行为/阶段/顺序 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§1:negative` | §1 抽象机/可观察行为/阶段/顺序 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§2:positive` | §2 词法 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§2:negative` | §2 词法 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§3:positive` | §3 类型/转换/对象/指针状态 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§3:negative` | §3 类型/转换/对象/指针状态 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§4:positive` | §4 程序结构/模块/导入/可见性/CT 顺序 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§4:negative` | §4 程序结构/模块/导入/可见性/CT 顺序 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§5:positive` | §5 存储对象/作用域/DI | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§5:negative` | §5 存储对象/作用域/DI | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§6:positive` | §6 指令集/语义/验证 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§6:negative` | §6 指令集/语义/验证 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§7:positive` | §7 控制流核心/CFG | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§7:negative` | §7 控制流核心/CFG | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§8:positive` | §8 函数/调用/注解 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§8:negative` | §8 函数/调用/注解 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§9:positive` | §9 安全模型 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§9:negative` | §9 安全模型 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§10:positive` | §10 优化契约 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§10:negative` | §10 优化契约 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§11:positive` | §11 标准 I/O | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§11:negative` | §11 标准 I/O | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§12:positive` | §12 诊断/首诊/位置/80 码 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§12:negative` | §12 诊断/首诊/位置/80 码 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§13:positive` | §13 平台档案/验证要求 | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§13:negative` | §13 平台档案/验证要求 | negative | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§14:positive` | §14 EBNF | positive | **uncovered** | — | test plan §3.6/§8.1 |
| `chapter:§14:negative` | §14 EBNF | negative | **uncovered** | — | test plan §3.6/§8.1 |

### Diagnostic code (`code`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `code:TC_CE_SYNTAX:trigger` | TC_CE_SYNTAX | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SYNTAX:no_false_positive` | TC_CE_SYNTAX | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SYNTAX:position` | TC_CE_SYNTAX | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SYNTAX:stage` | TC_CE_SYNTAX | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_END:trigger` | TC_CE_MISSING_END | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_END:no_false_positive` | TC_CE_MISSING_END | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_END:position` | TC_CE_MISSING_END | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_END:stage` | TC_CE_MISSING_END | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MODULE_LAYER:trigger` | TC_CE_MODULE_LAYER | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MODULE_LAYER:no_false_positive` | TC_CE_MODULE_LAYER | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MODULE_LAYER:position` | TC_CE_MODULE_LAYER | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MODULE_LAYER:stage` | TC_CE_MODULE_LAYER | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_VISIBILITY:trigger` | TC_CE_MISSING_VISIBILITY | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_VISIBILITY:no_false_positive` | TC_CE_MISSING_VISIBILITY | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_VISIBILITY:position` | TC_CE_MISSING_VISIBILITY | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_VISIBILITY:stage` | TC_CE_MISSING_VISIBILITY | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PROGRAM_MODE_MISUSE:trigger` | TC_CE_PROGRAM_MODE_MISUSE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PROGRAM_MODE_MISUSE:no_false_positive` | TC_CE_PROGRAM_MODE_MISUSE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PROGRAM_MODE_MISUSE:position` | TC_CE_PROGRAM_MODE_MISUSE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PROGRAM_MODE_MISUSE:stage` | TC_CE_PROGRAM_MODE_MISUSE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VAR_MISSING_INIT:trigger` | TC_CE_VAR_MISSING_INIT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VAR_MISSING_INIT:no_false_positive` | TC_CE_VAR_MISSING_INIT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VAR_MISSING_INIT:position` | TC_CE_VAR_MISSING_INIT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VAR_MISSING_INIT:stage` | TC_CE_VAR_MISSING_INIT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_LEX_RANGE:trigger` | TC_CE_LITERAL_LEX_RANGE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_LEX_RANGE:no_false_positive` | TC_CE_LITERAL_LEX_RANGE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_LEX_RANGE:position` | TC_CE_LITERAL_LEX_RANGE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_LEX_RANGE:stage` | TC_CE_LITERAL_LEX_RANGE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_NAME:trigger` | TC_CE_UNDEFINED_NAME | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_NAME:no_false_positive` | TC_CE_UNDEFINED_NAME | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_NAME:position` | TC_CE_UNDEFINED_NAME | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_NAME:stage` | TC_CE_UNDEFINED_NAME | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_NAME:trigger` | TC_CE_DUPLICATE_NAME | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_NAME:no_false_positive` | TC_CE_DUPLICATE_NAME | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_NAME:position` | TC_CE_DUPLICATE_NAME | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_NAME:stage` | TC_CE_DUPLICATE_NAME | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_MEMBER:trigger` | TC_CE_DUPLICATE_MEMBER | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_MEMBER:no_false_positive` | TC_CE_DUPLICATE_MEMBER | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_MEMBER:position` | TC_CE_DUPLICATE_MEMBER | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_MEMBER:stage` | TC_CE_DUPLICATE_MEMBER | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_PARAMETER:trigger` | TC_CE_DUPLICATE_PARAMETER | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_PARAMETER:no_false_positive` | TC_CE_DUPLICATE_PARAMETER | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_PARAMETER:position` | TC_CE_DUPLICATE_PARAMETER | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_PARAMETER:stage` | TC_CE_DUPLICATE_PARAMETER | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FUNCTION_SCOPE_ACCESS:trigger` | TC_CE_FUNCTION_SCOPE_ACCESS | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FUNCTION_SCOPE_ACCESS:no_false_positive` | TC_CE_FUNCTION_SCOPE_ACCESS | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FUNCTION_SCOPE_ACCESS:position` | TC_CE_FUNCTION_SCOPE_ACCESS | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FUNCTION_SCOPE_ACCESS:stage` | TC_CE_FUNCTION_SCOPE_ACCESS | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_FUNCTION:trigger` | TC_CE_UNDEFINED_FUNCTION | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_FUNCTION:no_false_positive` | TC_CE_UNDEFINED_FUNCTION | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_FUNCTION:position` | TC_CE_UNDEFINED_FUNCTION | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNDEFINED_FUNCTION:stage` | TC_CE_UNDEFINED_FUNCTION | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_ACCESS:trigger` | TC_CE_PRIVATE_ACCESS | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_ACCESS:no_false_positive` | TC_CE_PRIVATE_ACCESS | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_ACCESS:position` | TC_CE_PRIVATE_ACCESS | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_ACCESS:stage` | TC_CE_PRIVATE_ACCESS | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_TYPE:trigger` | TC_CE_PRIVATE_TYPE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_TYPE:no_false_positive` | TC_CE_PRIVATE_TYPE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_TYPE:position` | TC_CE_PRIVATE_TYPE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PRIVATE_TYPE:stage` | TC_CE_PRIVATE_TYPE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_FOUND:trigger` | TC_CE_IMPORT_NOT_FOUND | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_FOUND:no_false_positive` | TC_CE_IMPORT_NOT_FOUND | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_FOUND:position` | TC_CE_IMPORT_NOT_FOUND | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_FOUND:stage` | TC_CE_IMPORT_NOT_FOUND | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_LIB:trigger` | TC_CE_IMPORT_NOT_LIB | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_LIB:no_false_positive` | TC_CE_IMPORT_NOT_LIB | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_LIB:position` | TC_CE_IMPORT_NOT_LIB | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NOT_LIB:stage` | TC_CE_IMPORT_NOT_LIB | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_IMPORT:trigger` | TC_CE_DUPLICATE_IMPORT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_IMPORT:no_false_positive` | TC_CE_DUPLICATE_IMPORT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_IMPORT:position` | TC_CE_DUPLICATE_IMPORT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_IMPORT:stage` | TC_CE_DUPLICATE_IMPORT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CIRCULAR_IMPORT:trigger` | TC_CE_CIRCULAR_IMPORT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CIRCULAR_IMPORT:no_false_positive` | TC_CE_CIRCULAR_IMPORT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CIRCULAR_IMPORT:position` | TC_CE_CIRCULAR_IMPORT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CIRCULAR_IMPORT:stage` | TC_CE_CIRCULAR_IMPORT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NAME_CONFLICT:trigger` | TC_CE_IMPORT_NAME_CONFLICT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NAME_CONFLICT:no_false_positive` | TC_CE_IMPORT_NAME_CONFLICT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NAME_CONFLICT:position` | TC_CE_IMPORT_NAME_CONFLICT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMPORT_NAME_CONFLICT:stage` | TC_CE_IMPORT_NAME_CONFLICT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_LABEL:trigger` | TC_CE_DUPLICATE_LABEL | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_LABEL:no_false_positive` | TC_CE_DUPLICATE_LABEL | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_LABEL:position` | TC_CE_DUPLICATE_LABEL | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_LABEL:stage` | TC_CE_DUPLICATE_LABEL | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LABEL_NOT_FOUND:trigger` | TC_CE_LABEL_NOT_FOUND | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LABEL_NOT_FOUND:no_false_positive` | TC_CE_LABEL_NOT_FOUND | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LABEL_NOT_FOUND:position` | TC_CE_LABEL_NOT_FOUND | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LABEL_NOT_FOUND:stage` | TC_CE_LABEL_NOT_FOUND | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TYPE_MISMATCH:trigger` | TC_CE_TYPE_MISMATCH | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TYPE_MISMATCH:no_false_positive` | TC_CE_TYPE_MISMATCH | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TYPE_MISMATCH:position` | TC_CE_TYPE_MISMATCH | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TYPE_MISMATCH:stage` | TC_CE_TYPE_MISMATCH | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_TYPE:trigger` | TC_CE_LITERAL_TYPE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_TYPE:no_false_positive` | TC_CE_LITERAL_TYPE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_TYPE:position` | TC_CE_LITERAL_TYPE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_TYPE:stage` | TC_CE_LITERAL_TYPE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_RANGE:trigger` | TC_CE_LITERAL_RANGE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_RANGE:no_false_positive` | TC_CE_LITERAL_RANGE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_RANGE:position` | TC_CE_LITERAL_RANGE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_RANGE:stage` | TC_CE_LITERAL_RANGE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LITERAL_RANGE:w32` | TC_CE_LITERAL_RANGE | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_LITERAL_RANGE:w64` | TC_CE_LITERAL_RANGE | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_MODE:trigger` | TC_CE_MODE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MODE:no_false_positive` | TC_CE_MODE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MODE:position` | TC_CE_MODE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MODE:stage` | TC_CE_MODE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ANNOTATION:trigger` | TC_CE_ANNOTATION | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ANNOTATION:no_false_positive` | TC_CE_ANNOTATION | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ANNOTATION:position` | TC_CE_ANNOTATION | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ANNOTATION:stage` | TC_CE_ANNOTATION | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ANNOTATION:w32` | TC_CE_ANNOTATION | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_ANNOTATION:w64` | TC_CE_ANNOTATION | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_OPERAND_COUNT:trigger` | TC_CE_OPERAND_COUNT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_OPERAND_COUNT:no_false_positive` | TC_CE_OPERAND_COUNT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_OPERAND_COUNT:position` | TC_CE_OPERAND_COUNT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_OPERAND_COUNT:stage` | TC_CE_OPERAND_COUNT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONDITION_TYPE:trigger` | TC_CE_CONDITION_TYPE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONDITION_TYPE:no_false_positive` | TC_CE_CONDITION_TYPE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONDITION_TYPE:position` | TC_CE_CONDITION_TYPE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONDITION_TYPE:stage` | TC_CE_CONDITION_TYPE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_TYPE:trigger` | TC_CE_RETURN_TYPE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_TYPE:no_false_positive` | TC_CE_RETURN_TYPE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_TYPE:position` | TC_CE_RETURN_TYPE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_TYPE:stage` | TC_CE_RETURN_TYPE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_FORM:trigger` | TC_CE_RETURN_FORM | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_FORM:no_false_positive` | TC_CE_RETURN_FORM | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_FORM:position` | TC_CE_RETURN_FORM | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_FORM:stage` | TC_CE_RETURN_FORM | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_OUTSIDE_FUNCTION:trigger` | TC_CE_RETURN_OUTSIDE_FUNCTION | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_OUTSIDE_FUNCTION:no_false_positive` | TC_CE_RETURN_OUTSIDE_FUNCTION | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_OUTSIDE_FUNCTION:position` | TC_CE_RETURN_OUTSIDE_FUNCTION | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_RETURN_OUTSIDE_FUNCTION:stage` | TC_CE_RETURN_OUTSIDE_FUNCTION | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_RETURN:trigger` | TC_CE_MISSING_RETURN | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_RETURN:no_false_positive` | TC_CE_MISSING_RETURN | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_RETURN:position` | TC_CE_MISSING_RETURN | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_RETURN:stage` | TC_CE_MISSING_RETURN | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_COUNT:trigger` | TC_CE_ARGUMENT_COUNT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_COUNT:no_false_positive` | TC_CE_ARGUMENT_COUNT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_COUNT:position` | TC_CE_ARGUMENT_COUNT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_COUNT:stage` | TC_CE_ARGUMENT_COUNT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_ORDER:trigger` | TC_CE_ARGUMENT_ORDER | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_ORDER:no_false_positive` | TC_CE_ARGUMENT_ORDER | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_ORDER:position` | TC_CE_ARGUMENT_ORDER | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ARGUMENT_ORDER:stage` | TC_CE_ARGUMENT_ORDER | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_ARGUMENT:trigger` | TC_CE_UNKNOWN_ARGUMENT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_ARGUMENT:no_false_positive` | TC_CE_UNKNOWN_ARGUMENT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_ARGUMENT:position` | TC_CE_UNKNOWN_ARGUMENT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_ARGUMENT:stage` | TC_CE_UNKNOWN_ARGUMENT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_ARGUMENT:trigger` | TC_CE_DUPLICATE_ARGUMENT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_ARGUMENT:no_false_positive` | TC_CE_DUPLICATE_ARGUMENT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_ARGUMENT:position` | TC_CE_DUPLICATE_ARGUMENT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_ARGUMENT:stage` | TC_CE_DUPLICATE_ARGUMENT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_ARGUMENT:trigger` | TC_CE_MISSING_ARGUMENT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_ARGUMENT:no_false_positive` | TC_CE_MISSING_ARGUMENT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_ARGUMENT:position` | TC_CE_MISSING_ARGUMENT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_ARGUMENT:stage` | TC_CE_MISSING_ARGUMENT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_FIELD:trigger` | TC_CE_UNKNOWN_FIELD | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_FIELD:no_false_positive` | TC_CE_UNKNOWN_FIELD | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_FIELD:position` | TC_CE_UNKNOWN_FIELD | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNKNOWN_FIELD:stage` | TC_CE_UNKNOWN_FIELD | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_FIELD:trigger` | TC_CE_MISSING_FIELD | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_FIELD:no_false_positive` | TC_CE_MISSING_FIELD | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_FIELD:position` | TC_CE_MISSING_FIELD | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_MISSING_FIELD:stage` | TC_CE_MISSING_FIELD | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_FIELD:trigger` | TC_CE_DUPLICATE_FIELD | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_FIELD:no_false_positive` | TC_CE_DUPLICATE_FIELD | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_FIELD:position` | TC_CE_DUPLICATE_FIELD | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_DUPLICATE_FIELD:stage` | TC_CE_DUPLICATE_FIELD | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_STRUCT_SELF_VALUE:trigger` | TC_CE_STRUCT_SELF_VALUE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_STRUCT_SELF_VALUE:no_false_positive` | TC_CE_STRUCT_SELF_VALUE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_STRUCT_SELF_VALUE:position` | TC_CE_STRUCT_SELF_VALUE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_STRUCT_SELF_VALUE:stage` | TC_CE_STRUCT_SELF_VALUE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_EMPTY_STRUCT:trigger` | TC_CE_EMPTY_STRUCT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_EMPTY_STRUCT:no_false_positive` | TC_CE_EMPTY_STRUCT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_EMPTY_STRUCT:position` | TC_CE_EMPTY_STRUCT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_EMPTY_STRUCT:stage` | TC_CE_EMPTY_STRUCT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LAYOUT_OVERFLOW:trigger` | TC_CE_LAYOUT_OVERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LAYOUT_OVERFLOW:no_false_positive` | TC_CE_LAYOUT_OVERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LAYOUT_OVERFLOW:position` | TC_CE_LAYOUT_OVERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LAYOUT_OVERFLOW:stage` | TC_CE_LAYOUT_OVERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_LAYOUT_OVERFLOW:w32` | TC_CE_LAYOUT_OVERFLOW | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_LAYOUT_OVERFLOW:w64` | TC_CE_LAYOUT_OVERFLOW | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_FIELD_ORDER:trigger` | TC_CE_FIELD_ORDER | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FIELD_ORDER:no_false_positive` | TC_CE_FIELD_ORDER | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FIELD_ORDER:position` | TC_CE_FIELD_ORDER | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FIELD_ORDER:stage` | TC_CE_FIELD_ORDER | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMMUTABLE_FIELD:trigger` | TC_CE_IMMUTABLE_FIELD | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMMUTABLE_FIELD:no_false_positive` | TC_CE_IMMUTABLE_FIELD | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMMUTABLE_FIELD:position` | TC_CE_IMMUTABLE_FIELD | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_IMMUTABLE_FIELD:stage` | TC_CE_IMMUTABLE_FIELD | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_ASSIGNMENT:trigger` | TC_CE_CONST_ASSIGNMENT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_ASSIGNMENT:no_false_positive` | TC_CE_CONST_ASSIGNMENT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_ASSIGNMENT:position` | TC_CE_CONST_ASSIGNMENT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_ASSIGNMENT:stage` | TC_CE_CONST_ASSIGNMENT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PARAMETER_ASSIGNMENT:trigger` | TC_CE_PARAMETER_ASSIGNMENT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PARAMETER_ASSIGNMENT:no_false_positive` | TC_CE_PARAMETER_ASSIGNMENT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PARAMETER_ASSIGNMENT:position` | TC_CE_PARAMETER_ASSIGNMENT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_PARAMETER_ASSIGNMENT:stage` | TC_CE_PARAMETER_ASSIGNMENT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNINITIALIZED:trigger` | TC_CE_UNINITIALIZED | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNINITIALIZED:no_false_positive` | TC_CE_UNINITIALIZED | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNINITIALIZED:position` | TC_CE_UNINITIALIZED | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNINITIALIZED:stage` | TC_CE_UNINITIALIZED | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ADDR_OF_CONST:trigger` | TC_CE_ADDR_OF_CONST | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ADDR_OF_CONST:no_false_positive` | TC_CE_ADDR_OF_CONST | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ADDR_OF_CONST:position` | TC_CE_ADDR_OF_CONST | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_ADDR_OF_CONST:stage` | TC_CE_ADDR_OF_CONST | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_BITCAST_WIDTH:trigger` | TC_CE_BITCAST_WIDTH | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_BITCAST_WIDTH:no_false_positive` | TC_CE_BITCAST_WIDTH | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_BITCAST_WIDTH:position` | TC_CE_BITCAST_WIDTH | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_BITCAST_WIDTH:stage` | TC_CE_BITCAST_WIDTH | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_BITCAST_WIDTH:w32` | TC_CE_BITCAST_WIDTH | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_BITCAST_WIDTH:w64` | TC_CE_BITCAST_WIDTH | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_TRUNC_WIDTH:trigger` | TC_CE_TRUNC_WIDTH | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TRUNC_WIDTH:no_false_positive` | TC_CE_TRUNC_WIDTH | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TRUNC_WIDTH:position` | TC_CE_TRUNC_WIDTH | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TRUNC_WIDTH:stage` | TC_CE_TRUNC_WIDTH | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_TRUNC_WIDTH:w32` | TC_CE_TRUNC_WIDTH | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_TRUNC_WIDTH:w64` | TC_CE_TRUNC_WIDTH | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_SWITCH_TYPE:trigger` | TC_CE_SWITCH_TYPE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SWITCH_TYPE:no_false_positive` | TC_CE_SWITCH_TYPE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SWITCH_TYPE:position` | TC_CE_SWITCH_TYPE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SWITCH_TYPE:stage` | TC_CE_SWITCH_TYPE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VOID:trigger` | TC_CE_VOID | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VOID:no_false_positive` | TC_CE_VOID | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VOID:position` | TC_CE_VOID | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_VOID:stage` | TC_CE_VOID | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_OPERATION:trigger` | TC_CE_UNSAFE_OPERATION | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_OPERATION:no_false_positive` | TC_CE_UNSAFE_OPERATION | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_OPERATION:position` | TC_CE_UNSAFE_OPERATION | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_OPERATION:stage` | TC_CE_UNSAFE_OPERATION | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_CALL:trigger` | TC_CE_UNSAFE_CALL | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_CALL:no_false_positive` | TC_CE_UNSAFE_CALL | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_CALL:position` | TC_CE_UNSAFE_CALL | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_UNSAFE_CALL:stage` | TC_CE_UNSAFE_CALL | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FAST_CALL:trigger` | TC_CE_FAST_CALL | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FAST_CALL:no_false_positive` | TC_CE_FAST_CALL | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FAST_CALL:position` | TC_CE_FAST_CALL | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_FAST_CALL:stage` | TC_CE_FAST_CALL | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SAFE_VIOLATION:trigger` | TC_CE_SAFE_VIOLATION | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SAFE_VIOLATION:no_false_positive` | TC_CE_SAFE_VIOLATION | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SAFE_VIOLATION:position` | TC_CE_SAFE_VIOLATION | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_SAFE_VIOLATION:stage` | TC_CE_SAFE_VIOLATION | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FORM:trigger` | TC_CE_CONST_FORM | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FORM:no_false_positive` | TC_CE_CONST_FORM | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FORM:position` | TC_CE_CONST_FORM | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FORM:stage` | TC_CE_CONST_FORM | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_OVERFLOW:trigger` | TC_CE_CONST_OVERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_OVERFLOW:no_false_positive` | TC_CE_CONST_OVERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_OVERFLOW:position` | TC_CE_CONST_OVERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_OVERFLOW:stage` | TC_CE_CONST_OVERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_OVERFLOW:w32` | TC_CE_CONST_OVERFLOW | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_CONST_OVERFLOW:w64` | TC_CE_CONST_OVERFLOW | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_CONST_DIV_ZERO:trigger` | TC_CE_CONST_DIV_ZERO | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_DIV_ZERO:no_false_positive` | TC_CE_CONST_DIV_ZERO | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_DIV_ZERO:position` | TC_CE_CONST_DIV_ZERO | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_DIV_ZERO:stage` | TC_CE_CONST_DIV_ZERO | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_SHIFT:trigger` | TC_CE_CONST_SHIFT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_SHIFT:no_false_positive` | TC_CE_CONST_SHIFT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_SHIFT:position` | TC_CE_CONST_SHIFT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_SHIFT:stage` | TC_CE_CONST_SHIFT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_INVALID:trigger` | TC_CE_CONST_FLOAT_INVALID | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_INVALID:no_false_positive` | TC_CE_CONST_FLOAT_INVALID | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_INVALID:position` | TC_CE_CONST_FLOAT_INVALID | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_INVALID:stage` | TC_CE_CONST_FLOAT_INVALID | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_UNDERFLOW:trigger` | TC_CE_CONST_FLOAT_UNDERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_UNDERFLOW:no_false_positive` | TC_CE_CONST_FLOAT_UNDERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_UNDERFLOW:position` | TC_CE_CONST_FLOAT_UNDERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_FLOAT_UNDERFLOW:stage` | TC_CE_CONST_FLOAT_UNDERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_CAST:trigger` | TC_CE_CONST_CAST | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_CAST:no_false_positive` | TC_CE_CONST_CAST | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_CAST:position` | TC_CE_CONST_CAST | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_CAST:stage` | TC_CE_CONST_CAST | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_CE_CONST_CAST:w32` | TC_CE_CONST_CAST | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_CE_CONST_CAST:w64` | TC_CE_CONST_CAST | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_INTEGER_OVERFLOW:trigger` | TC_RE_INTEGER_OVERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_INTEGER_OVERFLOW:no_false_positive` | TC_RE_INTEGER_OVERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_INTEGER_OVERFLOW:position` | TC_RE_INTEGER_OVERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_INTEGER_OVERFLOW:stage` | TC_RE_INTEGER_OVERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_INTEGER_OVERFLOW:w32` | TC_RE_INTEGER_OVERFLOW | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_INTEGER_OVERFLOW:w64` | TC_RE_INTEGER_OVERFLOW | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_DIV_ZERO:trigger` | TC_RE_DIV_ZERO | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_DIV_ZERO:no_false_positive` | TC_RE_DIV_ZERO | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_DIV_ZERO:position` | TC_RE_DIV_ZERO | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_DIV_ZERO:stage` | TC_RE_DIV_ZERO | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NEGATIVE_SHIFT:trigger` | TC_RE_NEGATIVE_SHIFT | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NEGATIVE_SHIFT:no_false_positive` | TC_RE_NEGATIVE_SHIFT | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NEGATIVE_SHIFT:position` | TC_RE_NEGATIVE_SHIFT | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NEGATIVE_SHIFT:stage` | TC_RE_NEGATIVE_SHIFT | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_OVERFLOW:trigger` | TC_RE_FLOAT_OVERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_OVERFLOW:no_false_positive` | TC_RE_FLOAT_OVERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_OVERFLOW:position` | TC_RE_FLOAT_OVERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_OVERFLOW:stage` | TC_RE_FLOAT_OVERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_UNDERFLOW:trigger` | TC_RE_FLOAT_UNDERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_UNDERFLOW:no_false_positive` | TC_RE_FLOAT_UNDERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_UNDERFLOW:position` | TC_RE_FLOAT_UNDERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_UNDERFLOW:stage` | TC_RE_FLOAT_UNDERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_INVALID:trigger` | TC_RE_FLOAT_INVALID | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_INVALID:no_false_positive` | TC_RE_FLOAT_INVALID | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_INVALID:position` | TC_RE_FLOAT_INVALID | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_FLOAT_INVALID:stage` | TC_RE_FLOAT_INVALID | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_CAST_OVERFLOW:trigger` | TC_RE_CAST_OVERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_CAST_OVERFLOW:no_false_positive` | TC_RE_CAST_OVERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_CAST_OVERFLOW:position` | TC_RE_CAST_OVERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_CAST_OVERFLOW:stage` | TC_RE_CAST_OVERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_CAST_OVERFLOW:w32` | TC_RE_CAST_OVERFLOW | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_CAST_OVERFLOW:w64` | TC_RE_CAST_OVERFLOW | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_NULL_POINTER:trigger` | TC_RE_NULL_POINTER | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NULL_POINTER:no_false_positive` | TC_RE_NULL_POINTER | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NULL_POINTER:position` | TC_RE_NULL_POINTER | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NULL_POINTER:stage` | TC_RE_NULL_POINTER | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_PTRDIFF_OVERFLOW:trigger` | TC_RE_PTRDIFF_OVERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_PTRDIFF_OVERFLOW:no_false_positive` | TC_RE_PTRDIFF_OVERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_PTRDIFF_OVERFLOW:position` | TC_RE_PTRDIFF_OVERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_PTRDIFF_OVERFLOW:stage` | TC_RE_PTRDIFF_OVERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_PTRDIFF_OVERFLOW:w32` | TC_RE_PTRDIFF_OVERFLOW | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_PTRDIFF_OVERFLOW:w64` | TC_RE_PTRDIFF_OVERFLOW | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_WRITE_PROTECTED:trigger` | TC_RE_WRITE_PROTECTED | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_WRITE_PROTECTED:no_false_positive` | TC_RE_WRITE_PROTECTED | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_WRITE_PROTECTED:position` | TC_RE_WRITE_PROTECTED | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_WRITE_PROTECTED:stage` | TC_RE_WRITE_PROTECTED | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_ADDR_UNAVAILABLE:trigger` | TC_RE_ADDR_UNAVAILABLE | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_ADDR_UNAVAILABLE:no_false_positive` | TC_RE_ADDR_UNAVAILABLE | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_ADDR_UNAVAILABLE:position` | TC_RE_ADDR_UNAVAILABLE | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_ADDR_UNAVAILABLE:stage` | TC_RE_ADDR_UNAVAILABLE | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_ADDR_UNAVAILABLE:w32` | TC_RE_ADDR_UNAVAILABLE | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_ADDR_UNAVAILABLE:w64` | TC_RE_ADDR_UNAVAILABLE | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_STACK_OVERFLOW:trigger` | TC_RE_STACK_OVERFLOW | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_STACK_OVERFLOW:no_false_positive` | TC_RE_STACK_OVERFLOW | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_STACK_OVERFLOW:position` | TC_RE_STACK_OVERFLOW | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_STACK_OVERFLOW:stage` | TC_RE_STACK_OVERFLOW | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NULL_FUNCTION_POINTER:trigger` | TC_RE_NULL_FUNCTION_POINTER | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NULL_FUNCTION_POINTER:no_false_positive` | TC_RE_NULL_FUNCTION_POINTER | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NULL_FUNCTION_POINTER:position` | TC_RE_NULL_FUNCTION_POINTER | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_NULL_FUNCTION_POINTER:stage` | TC_RE_NULL_FUNCTION_POINTER | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_IO:trigger` | TC_RE_IO | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_IO:no_false_positive` | TC_RE_IO | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_IO:position` | TC_RE_IO | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_IO:stage` | TC_RE_IO | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_IO:w32` | TC_RE_IO | w32 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_IO:w64` | TC_RE_IO | w64 | **uncovered** | — | standard §13.2.2 word-size instantiation; test plan §3.6 |
| `code:TC_RE_TRAP:trigger` | TC_RE_TRAP | trigger | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_TRAP:no_false_positive` | TC_RE_TRAP | no_false_positive | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_TRAP:position` | TC_RE_TRAP | position | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |
| `code:TC_RE_TRAP:stage` | TC_RE_TRAP | stage | **uncovered** | — | §12.3 trigger/print name/stage; §12.5.1 position; test plan §3.6 |

### Opcode (`opcode`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `opcode:add:normal` | add | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:add:static_error` | add | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:add:trap` | add | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:add.wrap:normal` | add.wrap | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:add.wrap:static_error` | add.wrap | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:add.ieee:normal` | add.ieee | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:add.ieee:static_error` | add.ieee | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sub:normal` | sub | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sub:static_error` | sub | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sub:trap` | sub | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:sub.wrap:normal` | sub.wrap | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sub.wrap:static_error` | sub.wrap | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sub.wrap:trap` | sub.wrap | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:sub.ieee:normal` | sub.ieee | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sub.ieee:static_error` | sub.ieee | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sub.ieee:trap` | sub.ieee | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:mul:normal` | mul | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mul:static_error` | mul | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mul:trap` | mul | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:mul.wrap:normal` | mul.wrap | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mul.wrap:static_error` | mul.wrap | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mul.wrap:trap` | mul.wrap | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:mul.ieee:normal` | mul.ieee | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mul.ieee:static_error` | mul.ieee | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mul.ieee:trap` | mul.ieee | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:div:normal` | div | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:div:static_error` | div | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:div:trap` | div | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:div.ieee:normal` | div.ieee | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:div.ieee:static_error` | div.ieee | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mod:normal` | mod | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mod:static_error` | mod | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mod:trap` | mod | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:mod.ieee:normal` | mod.ieee | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:mod.ieee:static_error` | mod.ieee | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:neg:normal` | neg | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:neg:static_error` | neg | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:neg:trap` | neg | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:neg.wrap:normal` | neg.wrap | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:neg.wrap:static_error` | neg.wrap | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:abs:normal` | abs | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:abs:static_error` | abs | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:abs:trap` | abs | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:shl:normal` | shl | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:shl:static_error` | shl | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:shl:trap` | shl | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:shl.wrap:normal` | shl.wrap | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:shl.wrap:static_error` | shl.wrap | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:shl.wrap:trap` | shl.wrap | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:shr:normal` | shr | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:shr:static_error` | shr | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:shr:trap` | shr | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:and:normal` | and | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:and:static_error` | and | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:or:normal` | or | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:or:static_error` | or | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:xor:normal` | xor | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:xor:static_error` | xor | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:not:normal` | not | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:not:static_error` | not | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:eq:normal` | eq | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:eq:static_error` | eq | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ne:normal` | ne | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ne:static_error` | ne | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:lt:normal` | lt | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:lt:static_error` | lt | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:le:normal` | le | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:le:static_error` | le | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:gt:normal` | gt | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:gt:static_error` | gt | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ge:normal` | ge | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ge:static_error` | ge | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:cast:normal` | cast | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:cast:static_error` | cast | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:cast:trap` | cast | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:trunc:normal` | trunc | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:trunc:static_error` | trunc | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:bitcast:normal` | bitcast | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:bitcast:static_error` | bitcast | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:load:normal` | load | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:load:static_error` | load | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:load:trap` | load | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:load.volatile:normal` | load.volatile | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:load.volatile:static_error` | load.volatile | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:load.volatile:trap` | load.volatile | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:store:normal` | store | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:store:static_error` | store | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:store:trap` | store | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:store.volatile:normal` | store.volatile | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:store.volatile:static_error` | store.volatile | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:store.volatile:trap` | store.volatile | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:memmove:normal` | memmove | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:memmove:static_error` | memmove | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:memmove:trap` | memmove | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:addr:normal` | addr | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:addr:static_error` | addr | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptradd:normal` | ptradd | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptradd:static_error` | ptradd | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptradd:trap` | ptradd | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:ptrsub:normal` | ptrsub | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrsub:static_error` | ptrsub | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrsub:trap` | ptrsub | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:ptrdiff:normal` | ptrdiff | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrdiff:static_error` | ptrdiff | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrdiff:trap` | ptrdiff | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:ptreq:normal` | ptreq | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptreq:static_error` | ptreq | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrne:normal` | ptrne | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrne:static_error` | ptrne | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrlt:normal` | ptrlt | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrlt:static_error` | ptrlt | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrle:normal` | ptrle | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrle:static_error` | ptrle | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrgt:normal` | ptrgt | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrgt:static_error` | ptrgt | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrge:normal` | ptrge | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:ptrge:static_error` | ptrge | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sizeof:normal` | sizeof | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:sizeof:static_error` | sizeof | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:funcaddr:normal` | funcaddr | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:funcaddr:static_error` | funcaddr | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:call:normal` | call | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:call:static_error` | call | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:call:trap` | call | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:call_indirect:normal` | call_indirect | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:call_indirect:static_error` | call_indirect | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:call_indirect:trap` | call_indirect | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:funceq:normal` | funceq | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:funceq:static_error` | funceq | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:funcne:normal` | funcne | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:funcne:static_error` | funcne | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:make:normal` | make | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:make:static_error` | make | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:copy:normal` | copy | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:copy:static_error` | copy | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:br:normal` | br | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:br:static_error` | br | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:br_if:normal` | br_if | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:br_if:static_error` | br_if | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:switch:normal` | switch | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:switch:static_error` | switch | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:return:normal` | return | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:return:static_error` | return | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:trap:normal` | trap | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:trap:static_error` | trap | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:trap:trap` | trap | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:read:normal` | read | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:read:static_error` | read | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:read:trap` | read | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:write:normal` | write | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:write:static_error` | write | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:write:trap` | write | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |
| `opcode:writeln:normal` | writeln | normal | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:writeln:static_error` | writeln | static_error | **uncovered** | — | standard §6.2, design §3.2; test plan §3.6 |
| `opcode:writeln:trap` | writeln | trap | **uncovered** | — | standard §6.2 trap column; test plan §3.6 |

### Design §12 item (`design12`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `design12:01:suite_mapping` | 1. VM↔AOT 差分 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:01:gate_case` | 1. VM↔AOT 差分 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:02:suite_mapping` | 2. 标准 §13.4 八项一致性 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:02:gate_case` | 2. 标准 §13.4 八项一致性 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:03:suite_mapping` | 3. golden 向量 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:03:gate_case` | 3. golden 向量 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:04:suite_mapping` | 4. freestanding 编译门禁 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:04:gate_case` | 4. freestanding 编译门禁 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:05:suite_mapping` | 5. checked build | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:05:gate_case` | 5. checked build | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:06:suite_mapping` | 6. 32/64 静态矩阵 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:06:gate_case` | 6. 32/64 静态矩阵 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:07:suite_mapping` | 7. ABI/调用 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:07:gate_case` | 7. ABI/调用 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:08:suite_mapping` | 8. 对象/内存 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:08:gate_case` | 8. 对象/内存 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:09:suite_mapping` | 9. 诊断/语法 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:09:gate_case` | 9. 诊断/语法 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:10:suite_mapping` | 10. `@fast_math` | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:10:gate_case` | 10. `@fast_math` | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:11:suite_mapping` | 11. IR 形态 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:11:gate_case` | 11. IR 形态 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:12:suite_mapping` | 12. const/静态初值 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:12:gate_case` | 12. const/静态初值 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:13:suite_mapping` | 13. 类型表/模块实例 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:13:gate_case` | 13. 类型表/模块实例 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:14:suite_mapping` | 14. 退出/设备/输入 | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:14:gate_case` | 14. 退出/设备/输入 | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:15:suite_mapping` | 15. 零 pass | suite_mapping | **uncovered** | — | design §12, test plan §3.6/§8.2 |
| `design12:15:gate_case` | 15. 零 pass | gate_case | **uncovered** | — | design §12, test plan §3.6/§8.2 |

### First-diagnostic rule (§12.2) (`first_diag_rule`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `first_diag_rule:01:combination_case` | 1. 阶段优先 | combination_case | **covered** | TC-L0-DIAG-0009 | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:02:combination_case` | 2. 模块遍历顺序 | combination_case | **covered** | TC-L0-DIAG-0009 | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:03:combination_case` | 3. 同模块源序 | combination_case | **covered** | TC-L0-DIAG-0009 | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:04:combination_case` | 4. 同位置专用码优先 | combination_case | **covered** | TC-L0-DIAG-0009 | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:05:combination_case` | 5. 条款内顺序优先 | combination_case | **covered** | TC-L0-DIAG-0006 | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:06:combination_case` | 6. 布局诊断归属 SEM | combination_case | **suspended** | — | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:07:combination_case` | 7. 遍历无关 | combination_case | **suspended** | — | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:08:combination_case` | 8. 档案字长 | combination_case | **suspended** | — | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:09:combination_case` | 9. 操作数内部优先 | combination_case | **covered** | TC-L0-DIAG-0001 | standard §12.2, test plan §3.6/§6.1 |
| `first_diag_rule:10:combination_case` | 10. call/make 参数顺序 | combination_case | **covered** | TC-L0-DIAG-0003, TC-L0-DIAG-0004, TC-L0-DIAG-0007 | standard §12.2, test plan §3.6/§6.1 |

### Same-position code priority (`same_site_priority`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `same_site_priority:SAFE_VIOLATION_OVER_UNSAFE:priority` | SAFE_VIOLATION over UNSAFE_*/FAST_CALL | priority | **covered** | TC-L0-DIAG-0002 | standard §12.3 SAFE_VIOLATION trigger; §9.1/§9.3; test plan §3.6/§6.1 |
| `same_site_priority:CALL_ARGUMENT_ORDER:priority` | call argument order | priority | **covered** | TC-L0-DIAG-0003 | standard §12.2 #10; test plan §3.6/§6.1 |
| `same_site_priority:MAKE_FIELD_ORDER:priority` | make field order | priority | **covered** | TC-L0-DIAG-0004 | standard §12.2 #10; test plan §3.6/§6.1 |
| `same_site_priority:CALL_SAFETY_FIRST:priority` | call safety before arguments | priority | **covered** | TC-L0-DIAG-0005 | standard §8.8.2/§12.2 #9 exception; test plan §3.6/§6.1 |
| `same_site_priority:NEGATIVE_SHIFT_BEFORE_OVERFLOW:priority` | negative shift before shl overflow | priority | **covered** | TC-L0-DIAG-0006 | standard §12.2 #5; test plan §3.6/§6.1 |

### 32/64 flip (`flip32_64`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `flip32_64:01:w32` | `isize`/`usize` 宽度 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:01:w64` | `isize`/`usize` 宽度 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:02:w32` | `ptr<T>`/`fnptr` 宽度 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:02:w64` | `ptr<T>`/`fnptr` 宽度 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:03:w32` | sizeof isize`/`usize`/`ptr`/`fnptr | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:03:w64` | sizeof isize`/`usize`/`ptr`/`fnptr | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:04:w32` | `ptrdiff` 结果 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:04:w64` | `ptrdiff` 结果 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:05:w32` | bitcast ptr<T> ↔ int` / `bitcast fnptr<sig> ↔ int | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:05:w64` | bitcast ptr<T> ↔ int` / `bitcast fnptr<sig> ↔ int | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:06:w32` | bitcast isize ↔ int32 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:06:w64` | bitcast isize ↔ int32 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:07:w32` | bitcast isize ↔ int64 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:07:w64` | bitcast isize ↔ int64 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:08:w32` | bitcast usize ↔ uint32 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:08:w64` | bitcast usize ↔ uint32 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:09:w32` | bitcast usize ↔ uint64 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:09:w64` | bitcast usize ↔ uint64 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:10:w32` | trunc isize, int64 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:10:w64` | trunc isize, int64 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:11:w32` | trunc int32, isize | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:11:w64` | trunc int32, isize | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:12:w32` | trunc usize, int64 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:12:w64` | trunc usize, int64 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:13:w32` | trunc int32, usize | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:13:w64` | trunc int32, usize | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:14:w32` | `isize`/`usize` 字面量范围 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:14:w64` | `isize`/`usize` 字面量范围 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:15:w32` | `@padding`/结构体求和 | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:15:w64` | `@padding`/结构体求和 | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:16:w32` | @range` 的 `isize`/`usize` 参数；`@align` 的 `N | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:16:w64` | @range` 的 `isize`/`usize` 参数；`@align` 的 `N | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:17:w32` | read`/`write` 的 `isize`/`usize | w32 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |
| `flip32_64:17:w64` | read`/`write` 的 `isize`/`usize | w64 | **uncovered** | — | standard §13.2.2, test plan §3.6/§8.2 |

### Annotation (`annotation`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `annotation:@unsafe:coverage` | @unsafe | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@safe:coverage` | @safe | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@fast:coverage` | @fast | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@fast_math:coverage` | @fast_math | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@pure:coverage` | @pure | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@readonly:coverage` | @readonly | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@nounwind:coverage` | @nounwind | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@willreturn:coverage` | @willreturn | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@no_overflow:coverage` | @no_overflow | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@no_alias:coverage` | @no_alias | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@nonnull:coverage` | @nonnull | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@align:coverage` | @align | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@range:coverage` | @range | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@padding:coverage` | @padding | coverage | **uncovered** | — | §2.3, §2.6; test plan §3.6 |
| `annotation:@inline:coverage` | @inline | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |
| `annotation:@noinline:coverage` | @noinline | coverage | **uncovered** | — | §2.6, §8.6; test plan §3.6 |

### Device policy (`device_policy`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `device_policy:TC_DEV_REJECT:trap_mapping` | TC_DEV_REJECT | trap_mapping | **uncovered** | — | design §8 (TcDevicePolicy), test plan §3.6/§7.5 |
| `device_policy:TC_DEV_READ:trap_mapping` | TC_DEV_READ | trap_mapping | **uncovered** | — | design §8 (TcDevicePolicy), test plan §3.6/§7.5 |
| `device_policy:TC_DEV_READWRITE:trap_mapping` | TC_DEV_READWRITE | trap_mapping | **uncovered** | — | design §8 (TcDevicePolicy), test plan §3.6/§7.5 |
| `device_policy:TC_DEV_VOLATILE:trap_mapping` | TC_DEV_VOLATILE | trap_mapping | **uncovered** | — | design §8 (TcDevicePolicy), test plan §3.6/§7.5 |

### I/O source state (`io_source_state`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `io_source_state:TC_IO_OK:path` | TC_IO_OK | path | **uncovered** | — | design §7.2 (TcIoSrc), test plan §3.6/§7.5 |
| `io_source_state:TC_IO_EOF:path` | TC_IO_EOF | path | **uncovered** | — | design §7.2 (TcIoSrc), test plan §3.6/§7.5 |
| `io_source_state:TC_IO_ERROR:path` | TC_IO_ERROR | path | **uncovered** | — | design §7.2 (TcIoSrc), test plan §3.6/§7.5 |
| `io_source_state:SHORT_READ:path` | TC_IO_OK (short read) | path | **uncovered** | — | design §7.2 (TcIoSrc), test plan §3.6/§7.5 |

### Exit strategy (`exit_strategy`)

| Row ID | Item | Slot | Status | Case IDs | Spec |
|---|---|---|---|---|---|
| `exit_strategy:normal_exit:branch` | normal_exit | branch | **uncovered** | — | design §8 (TcExitPolicy), standard §13.1, test plan §3.6 |
| `exit_strategy:trap_exit:branch` | trap_exit | branch | **uncovered** | — | design §8 (TcExitPolicy), standard §13.1, test plan §3.6 |
