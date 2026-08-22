# 模块系统 — #program / #lib / import / Self / 搜索路径

由 `@knowledge-graph` 索引指向；勿与其它 `kg-*.md` 同时整读。

> **Phase 2（B+C+D）已落地**。规格：语言标准-0.0.39 · 编译器标准阶段 4。

## 关键文件

| 文件 | 职责 |
|------|------|
| `tc_module.c` | 4a 结构 · 4b/4c import 解析 · 4d 签名收集 |
| `tc_scope.c` | 成员索引 · `Self` 仅 `#lib` |
| `tc_lexer.c` / `tc_parser.c` | `#program`/`#lib`/`import`/`@`/`Self` Token 与 AST |
| `tc_lib.c` / `tc_driver.c` | `TcCompileOptions` 会话路径（`-I`）/ `tc_compile_file_opts` |

## Analyze 顺序（模块段）

```
tc_module_check_structure          /* 4a：五层顺序、可见性、#program 误用 */
→ tc_module_resolve_imports        /* 4b/4c：-I 搜索、去重、成环（需 entry_path） */
→ tc_struct_table_register_program /* deps + 入口 */
→ tc_module_collect_signatures     /* 4d：签名 + func_id */
→ … Pass1 / member_index / …
```

`tc_analyze`（无 path）不做 import；`tc_analyze_ex` / `tc_compile_file_opts` 才解析依赖。

## 导出 API（速查）

```
tc_module_search_paths_init/free/set
tc_module_check_structure
tc_module_resolve_imports
tc_module_collect_signatures
tc_func_signature_list_init/free

tc_member_index_init/free/build/find
tc_scope_check_self_usage
```

## 常见错误

| 场景 | 典型码 |
|------|--------|
| 无 `#program`/`#lib` 头 | 结构检查 |
| import 未找到 / 歧义 | 搜索路径 / 双 `-I` |
| import 环 | 成环检测 |
| `#program` 内 `Self` | `tc_scope_check_self_usage` |
| private 成员跨模块 | PRIVATE / 可见性 |

## 测试

`test_module.c` / check-module · `tests/errors/module/` · `tests/modules/` · CLI `include_search_ok` / `import_ambiguous`  
账本：[test-map.md](test-map.md) Phase 2
