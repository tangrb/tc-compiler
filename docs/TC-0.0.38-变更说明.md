# TC 0.0.38 变更说明

> **实现版本**：`0.0.38`（`src/vm/runtime/tc_version.h`）  
> **语言/编译器权威规格**：仍为 [docs/*-0.0.37.md](./TC语言标准设计说明书-0.0.37.md)  
> **基线**：`v0.0.37`（`4d1489a`）  
> **性质**：合规债修复与实现加固（无新语言特性）

---

## 1. 摘要

0.0.38 对齐 0.0.37 规范中已声明但实现不足的行为，并补齐相关回归测试与 CI。默认门禁：VM **697** / AOT **346** / Unit 全绿。

## 2. 行为变更与修复

| 主题 | 说明 |
|------|------|
| 导入库 CFG | 对每个依赖 `#lib` 执行确定初始化 / 不可达 / `MISSING_RETURN`；诊断文件名指向库自身 |
| memblock 值语义 | 赋值、传参、返回深拷贝（VM/AOT）；结果位补 `MEMBLOCK_SIZE_MISMATCH` |
| memblock 槽解析 | `store`/`copy`/`count` 使用 Pass2 持久化 binding，避免跨函数同名形参绑错槽 |
| 负移位 | 有符号负计数 → `TC_RE_NEGATIVE_SHIFT_COUNT`；常量路径映射 `CONSTANT_EXPRESSION` |
| goto/label | 标签表按 `func_id` 隔离；跨函数同名合法，函数内未找到 → `LABEL_NOT_FOUND` |
| 缩进 | 仅 U+0020、固定 4 空格一级；行首 tab → `INDENT_MIXED` |
| AOT `memcopy_unsafe` | 新增 shim 与发射，行为与 VM 对齐 |
| Embed API 域 | `tc_embed_create` / `create_aot` 无效参数改报 `TC_API_ERR_INVALID_ARGUMENT` |
| CI | `asan.yml` AOT 二进制路径；workflow 补 `check_type_fact_source` |
| 前端瑕疵 | BOM 短源、memblock 负 count、`0_5` 字面量、函数体内可见性修饰符等 |

## 3. 已知残留（非阻塞）

- 依赖库 CFG 诊断切换时 `source=NULL`，库侧错误无源码片段（文件名正确）
- AOT `memblock_clone` 相对 VM 仍缺 `count×element` 溢出护栏
- `memcopy_unsafe` 对复合元素类型仍经 `tc_type_scalar` 剥参数（VM/AOT 一致）

## 4. 测试与门禁

- 新增/更新：modules（BadLib*、ParamScope、mbsize）、static/runtime 负例、valid 深拷贝与标签跨函数正例、AOT 差分
- 同步检查：`check_rhs_coverage.py`、`check_source_naming.py`
- 建议发版后仍定期跑 `make build-asan` + `bash scripts/run_tests.sh --asan`

## 5. 文档索引

| 文档 | 角色 |
|------|------|
| 本文件 | 0.0.38 实现变更与发布说明 |
| [TC 语言标准 0.0.37](./TC语言标准设计说明书-0.0.37.md) | 语法/语义权威 |
| [设计实现合规审查 0.0.37](./设计实现合规审查报告-0.0.37.md) | 0.0.37 合规矩阵基线 |
