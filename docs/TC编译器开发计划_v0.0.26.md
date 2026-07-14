# TC 编译器开发计划 — v0.0.26

> **目标版本**：v0.0.26  
> **交付状态**：**已交付（2026-07-14）** — M1–M9 完成；合规 375/375；全量回归 384 VM + 857 unit + 210 AOT  
> **依据**：[TC语言标准设计说明书.md](./TC语言标准设计说明书.md) v0.0.26（现行权威；由 v0.0.25 升版）  
> **复验**：[设计实现合规审查报告.md](./设计实现合规审查报告.md)  
> **范围**：libtc（编译 API）+ TC-VM（解释器）+ TC-AOT（静态编译）  
> **说明**：本文档保留为里程碑与设计决策档案；下文「目标」列均已实现。

---

## 交付对照（v0.0.25 → v0.0.26）

| 特性 | v0.0.25（已实现） | v0.0.26（目标） |
|---|---|---|
| 基础类型（8 整数 + 2 浮点 + bool） | ✅ | ✅ |
| 算术/位/比较/逻辑运算 | ✅ | ✅ |
| 类型转换（含 truncate） | ✅ | ✅ |
| I/O（write/writeln/read + 13 格式符） | ✅ | ✅ |
| if-then-else + 缩进引擎 + 块级作用域 | ✅ | ✅ |
| let 编译期常量表达式 | ✅ | ✅ |
| fail-fast + TcDiagnostic 单槽 | ✅ | ✅ |
| **未初始化读取** | ⚠️ **编译警告**（`TC_WARN_UNINITIALIZED_VARIABLE`） | ❌ **静态编译错误**（`TC_ERR_UNINITIALIZED_VARIABLE`） |
| **数据流分析（所有路径已初始化）** | ❌ 仅单路径线性扫描 | ✅ 控制流合并分析 |
| **goto 语句**（受限跳转） | ❌ | ✅ |
| **label 定义** | ❌ | ✅ |
| **标签块级作用域** | ❌ | ✅ |
| 错误码 `TC_ERR_LABEL_NOT_FOUND` | ❌ | ✅ |
| 错误码 `TC_ERR_DUPLICATE_LABEL` | ❌ | ✅ |
| 错误码 `TC_ERR_JUMP_INTO_BLOCK` | ❌ | ✅ |
| 错误码 `TC_ERR_JUMP_TO_SIBLING_BLOCK` | ❌ | ✅ |

---

## 里程碑总览（全部 ✅）

| M | 内容 | 前置 | 状态 |
|---|---|---|---|
| M1 | **基础设施**：TcStmtKind / TcErrorKind 枚举、TcWarningKind 移除、版本号 | — | ✅ |
| M2 | **词法/语法**：goto/label 关键字 Token、AST 构建、缩进规则 | M1 | ✅ |
| M3 | **符号表**：标签表管理（TcSymbolTable 扩展）、块级作用域 push/pop | M1 | ✅ |
| M4 | **标签验证**：标签解析 + 4 种跳转合法性判定（块路径栈） | M2, M3 | ✅ |
| M5 | **数据流分析**：未初始化变量 → 静态错误（控制流合并、goto CFG） | M3 | ✅ |
| M6 | **VM 执行**：goto IP 跳转、标签零成本执行 | M4 | ✅ |
| M7 | **AOT 代码生成**：标签/跳转指令 | M4 | ✅ |
| M8 | **测试**：正/反向 goto 用例、未初始化迁移、全量通过 | M5, M6, M7 | ✅ |
| M9 | **文档同步**：标准说明书、VM 详设、知识图谱 | M8 | ✅ |

---

## M1：基础设施

### 目标

完成新增枚举、AST 节点类型、错误码、版本号的定义。

### 改动文件

```
src/vm/runtime/tc_types.h       — TcStmtKind + TcErrorKind 更新 + TcWarningKind 移除
src/vm/runtime/tc_types.c       — tc_error_kind_name() 更新 + tc_warning_kind_name() 清理
src/vm/runtime/tc_warning.h     — 告知本警告类型已移除
src/vm/driver/tc_version.h      — 0.0.25 → 0.0.26
tests/unit/runtime/test_types.c — 新增 5 个错误码的 name 映射测试
```

### 任务

#### 1.1 `TcStmtKind` 新增枚举值（`tc_types.h`，现有第 346–354 行）

在 `TC_STMT_IF` 之后追加：

```c
TC_STMT_LABEL_DEF,   /* label name: 定义标签 */
TC_STMT_GOTO         /* goto name  无条件跳转 */
```

注意：`TC_STMT_GOTO` 在枚举末尾，其后的 `TcStatement` 联合体无需变动（label/goto 属于新成员，需新增结构体）。

#### 1.2 `TcStatement` 联合体新增成员（`tc_types.h`，现有第 401–412 行）

新增 `TcLabelDef` 和 `TcGoto` 结构体：

```c
typedef struct {
    int line;
    char *name;        /* 标签名，堆分配 */
} TcLabelDef;

typedef struct {
    int line;
    char *target;      /* 目标标签名，堆分配 */
} TcGoto;
```

`struct TcStatement` 联合体中新增：

```c
TcLabelDef label_def;
TcGoto goto_stmt;
```

#### 1.3 `TcErrorKind` 新增 5 个枚举值（`tc_types.h`，现有第 191 行之后）

```c
TC_ERR_UNINITIALIZED_VARIABLE,  /* §4.2 读取未初始化变量 */
TC_ERR_LABEL_NOT_FOUND,         /* §4.8.3 goto 引用未定义标签 */
TC_ERR_DUPLICATE_LABEL,         /* §4.8.3 同一作用域重定义标签 */
TC_ERR_JUMP_INTO_BLOCK,         /* §4.8.3 跳入内层子块 */
TC_ERR_JUMP_TO_SIBLING_BLOCK    /* §4.8.3 跳入兄弟分支 */
```

**放置位置**：放在 `TC_ERR_MODE_MISMATCH` 之后，作为枚举末尾条目（注意检查枚举末尾无逗号）。

#### 1.4 `TcWarningKind` 移除

将 `TcWarningKind` 枚举（现有第 194–197 行）标记为**已废弃**，注释说明：

```c
/* v0.0.26 移除：TC_WARN_UNINITIALIZED_VARIABLE 升级为 TC_ERR_UNINITIALIZED_VARIABLE */
/* typedef enum { ... } TcWarningKind; — 枚举保留但空置，等待未来新警告类型 */
```

或彻底删除 `TcWarningKind` 枚举和 `TcWarningList` 类型（需确认无其他使用）。评估后：**保留空枚举**，避免波及 TcTypedProgram 中的 warnings 字段。后续若新增警告类型再恢复。

#### 1.5 `tc_error_kind_name()` 新增映射

`tc_types.c` 中 `tc_error_kind_name()` 函数新增 5 个 case：

```c
case TC_ERR_UNINITIALIZED_VARIABLE:  return "UninitializedVariable";
case TC_ERR_LABEL_NOT_FOUND:         return "LabelNotFound";
case TC_ERR_DUPLICATE_LABEL:         return "DuplicateLabel";
case TC_ERR_JUMP_INTO_BLOCK:         return "JumpIntoBlockError";
case TC_ERR_JUMP_TO_SIBLING_BLOCK:   return "JumpToSiblingBlockError";
```

#### 1.6 `tc_version.h` 更新

```c
#define TC_VM_VERSION "0.0.26"
```

#### 1.7 单元测试更新

`tests/unit/runtime/test_types.c` 中关于 `TC_WARN_UNINITIALIZED_VARIABLE` 的 name 测试改为测试 `TC_ERR_UNINITIALIZED_VARIABLE`。新增 5 个 `tc_error_kind_name()` 的字符串对照测试（参考现有 `TC_ERR_SYNTAX → "SyntaxError"` 的模式）。

#### 1.8 `tc_stmt_subtree_index_count` 更新

`tc_stmt_index.c` 中 `tc_stmt_subtree_index_count()` 函数（现有第 29–39 行），`TC_STMT_LABEL_DEF` 和 `TC_STMT_GOTO` 各占 1 个序号（与 if 外的其他语句一致）。无需额外分支：fall-through 到 `return 1`。

---

## M2：词法与语法解析

### 目标

词法器识别 `goto`/`label` 关键字；语法器构建 `TC_STMT_LABEL_DEF`/`TC_STMT_GOTO` AST 节点，落实缩进规则。

### 改动文件

```
src/vm/lexer/tc_lexer.c           — 关键字表新增 goto/label
src/vm/lexer/tc_lexer.h           — TcTokenKind 校验（若有 TOK 枚举则加）
src/vm/parser/tc_parser.c         — parse_stmt 分发 + 2 个新产生式解析
src/vm/parser/tc_parser.h         — 无变动（TcStatement 已涵盖）
```

### 任务

#### 2.1 词法：新增 Token 种类

若 `tc_lexer.h` 中 `TcTokenKind` 有显式枚举 → 新增 `TC_TOK_GOTO` 和 `TC_TOK_LABEL`。

若 `TcTokenKind` 通过 `tc_keyword_token()` 隐式匹配 → 查阅后确认 TC 使用 `TC_TOK_IDENTIFIER` + 关键字表方式。**需确认现有机制**。

**tc_keyword_token() 新增分支**（`tc_lexer.c`，现有第 417 行起，在 `strcmp(buf, "not")` 附近）：

```c
if (strcmp(buf, "goto") == 0) {
    token->kind = TC_TOK_GOTO;
    return 1;
}
if (strcmp(buf, "label") == 0) {
    token->kind = TC_TOK_LABEL;
    return 1;
}
```

同时将 `"goto"`、`"label"` 加入 `tc_lexer.h` 的 `TcTokenKind` 枚举（放在 `TC_TOK_END` 之后，或按字母顺序）。

#### 2.2 语法：`tc_parse_statement()` 新增分发分支

`tc_parser.c` `tc_parse_statement()`（现有第 1437 行），在 `TC_TOK_READ` 分支之后新增：

```c
if (first->kind == TC_TOK_GOTO) {
    TcGoto goto_stmt;
    memset(&goto_stmt, 0, sizeof(goto_stmt));
    /* 解析: "goto" identifier */
    index++;  /* 跳过 goto */
    if (!tc_is_identifier(tc_peek(tokens, index))) {
        return tc_syntax_error(diag, line_no, tc_column(tokens, index),
                               "expected identifier after 'goto'");
    }
    goto_stmt.target = strdup(tc_peek(tokens, index)->text);
    index++;
    if (!goto_stmt.target) {
        return tc_oom_error(diag);
    }
    if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
        free(goto_stmt.target);
        return -1;
    }
    out->kind = TC_STMT_GOTO;
    out->u.goto_stmt = goto_stmt;
    return 0;
}
```

```c
if (first->kind == TC_TOK_LABEL) {
    TcLabelDef label_def;
    memset(&label_def, 0, sizeof(label_def));
    /* 解析: "label" identifier ":" */
    index++;  /* 跳过 label */
    if (!tc_is_identifier(tc_peek(tokens, index))) {
        return tc_syntax_error(diag, line_no, tc_column(tokens, index),
                               "expected identifier after 'label'");
    }
    label_def.name = strdup(tc_peek(tokens, index)->text);
    index++;
    if (!label_def.name) {
        return tc_oom_error(diag);
    }
    /* 期望 ":" */
    if (!tc_peek_is_colon(tc_peek(tokens, index))) {
        free(label_def.name);
        return tc_syntax_error(diag, line_no, tc_column(tokens, index),
                               "expected ':' after label name");
    }
    index++;  /* 跳过 ":" */
    if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
        free(label_def.name);
        return -1;
    }
    out->kind = TC_STMT_LABEL_DEF;
    out->u.label_def = label_def;
    return 0;
}
```

#### 2.3 缩进处理

- `label` 行视为块内普通语句：缩进 ≥ 当前块级缩进，与赋值、`var` 等一致（现有缩进引擎自动覆盖）
- `goto` 行视为块内普通语句：缩进 ≥ 当前块级缩进
- `label name:` 行的 `:` 在 token 级别处理，不进入缩进引擎的判定逻辑
- **注意**：`tc_parse_line_program()`（if 解析器使用的多行上下文）应能识别 `TC_TOK_GOTO` 和 `TC_TOK_LABEL`，将它们分发到 `tc_parse_statement()`。检查现有 if 块内的语句解析是否已覆盖——若 `tc_parse_statement()` 已处理全部 Token 种类，则无需额外修改。

#### 2.4 `tc_parser.c` 辅助函数检查

确认 `tc_is_identifier()` 或等效函数是否存在（用于验证 token 类型）。若无，可使用 `tc_peek(tokens, index)->kind == TC_TOK_IDENTIFIER`。

新增 `tc_peek_is_colon()` 或使用 `tc_peek(tokens, index)->kind == TC_TOK_COLON`（若 TcTokenKind 包含 `TC_TOK_COLON`）。若冒号在现有词法分析中未单独成 token，需在词法器中增加对 `:` 的识别（`tc_tokenize_line()` 中单字符 token 分支）。

#### 2.5 错误消息（OOM 模式）

参考现有 `tc_oom_error()` / `tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, ...)` 模式。在 `strdup` 失败时：

```c
tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                  "memory allocation failed");
return -1;
```

---

## M3：符号表 — 标签作用域管理

### 目标

在符号表中新增标签管理能力，支持块级作用域的标签 push/pop、重复标签检测、自当前作用域向上遍历查找。

### 改动文件

```
src/vm/runtime/tc_symbol.h    — TcLabelEntry 结构体 + 新增 API 声明
src/vm/runtime/tc_symbol.c    — 标签表实现 + push/pop 集成
src/vm/parser/tc_parser.c     — Pass 1 中注册标签
```

### 任务

#### 3.1 数据结构定义（`tc_symbol.h`）

```c
/** 标签表条目 */
typedef struct {
    char *name;              /* 标签名，堆分配 */
    int stmt_index;          /* 标签语句的扁平序号 */
    int block_depth;         /* 标签所在的作用域深度 */
    int def_line;            /* 定义行号（用于错误报告） */
} TcLabelEntry;
```

在 `TcSymbolTable` 结构体中新增标签表：

```c
typedef struct {
    /* 原有 symbols 相关字段... */
    
    /* v0.0.26 新增：标签表 */
    TcLabelEntry *labels;
    size_t label_count;
    size_t label_capacity;
} TcSymbolTable;
```

#### 3.2 API 声明（`tc_symbol.h`）

```c
/**
 * 在当前作用域添加标签。
 * @param name      标签名
 * @param stmt_index 标签语句的 stmt_index（由 analyzer 调用时填入）
 * @param line      定义行号
 * @param diag      错误输出
 * @return 成功 0；重复标签或 OOM 返回 -1
 */
int tc_symbol_table_add_label(TcSymbolTable *table, const char *name, int stmt_index,
                               int line, TcDiagnostic *diag);

/**
 * 自当前作用域向上（祖先方向）查找标签。
 * @param name 标签名
 * @return TcLabelEntry* 或 NULL（未找到）
 */
const TcLabelEntry *tc_symbol_table_find_label(const TcSymbolTable *table, const char *name);

/**
 * 移除当前块深度内的所有标签（作用域退出时调用）。
 * @param table 符号表
 */
void tc_symbol_table_pop_labels(TcSymbolTable *table);
```

#### 3.3 实现细节（`tc_symbol.c`）

**`tc_symbol_table_init()`**：初始化 `labels = NULL; label_count = 0; label_capacity = 0;`

**`tc_symbol_table_free()`**：遍历 labels 释放 `name` → free labels 数组

**`tc_symbol_table_add_label()`**：
1. 在当前块深度 `tc_symbol_table_current_scope(table)` 内搜索同名标签
   - 遍历 `labels[0..label_count-1]`，找到 `block_depth == current_depth && strcmp(name, ...) == 0` → 重复标签错误
   - 只在**当前块深度**检查，不检查祖先（不同块允许同名）
2. 动态数组添加：OOM → `TC_ERR_OUT_OF_MEMORY`
3. 设置 `block_depth = current_scope_level`

**`tc_symbol_table_find_label()`**：
1. 从 `labels[label_count-1]` 向前遍历到 `labels[0]`
2. 返回第一个 `strcmp(name, ...) == 0` 的条目
3. 返回 NULL 表示未找到
4. **为什么自底向上就够了**：标签查找规则是"自当前块向上遍历"，自底向上自然优先匹配当前块，再匹配外层块。外层块深度更小，但不影响——因为同深度优先匹配已满足"当前块优先"。

**`tc_symbol_table_pop_labels()`**：
1. 从 `labels[label_count-1]` 向前遍历
2. 删除所有 `block_depth == current_scope_level` 的条目
3. 更新 `label_count`
4. 在 `tc_symbol_table_pop_scope()` 内部自动调用

**集成到现有 scope 机制**：在 `tc_symbol_table_pop_scope()` 末尾自动调用 `tc_symbol_table_pop_labels()`。

#### 3.4 Pass 1 中的标签注册

`tc_analyzer.c` `tc_pass1_collect_stmt()` 中新增 `TC_STMT_LABEL_DEF` 分支：

```c
if (stmt->kind == TC_STMT_LABEL_DEF) {
    int idx = tc_stmt_index_take(&ctx->index);
    /* 标签仅注册到标签表，不分配 slot */
    return 0;
}
if (stmt->kind == TC_STMT_GOTO) {
    tc_stmt_index_take(&ctx->index);
    return 0;
}
```

注意：`tc_stmt_index_take()` 应该只在 Pass 1 中被调用一次——现有代码中 Pass 1 和 Pass 2 各自有独立的序号分配。检查现有模式，确认 Pass 1 是否已分配序号。

#### 3.5 `tc_label_free()` / `TcStatement` 的 free 函数更新

在 `tc_parser.c` 的 `tc_rhs_free()` 或 `tc_statement_free()` 类函数中，新增对 `TC_STMT_LABEL_DEF` 和 `TC_STMT_GOTO` 的释放逻辑：

```c
if (stmt->kind == TC_STMT_LABEL_DEF) {
    free(stmt->u.label_def.name);
}
if (stmt->kind == TC_STMT_GOTO) {
    free(stmt->u.goto_stmt.target);
}
```

---

## M4：静态分析 — 标签解析与跳转合法性

### 目标

在 Pass 2 中验证每个 `TC_STMT_GOTO`：目标标签是否存在、跳转是否合法。

### 改动文件

```
src/vm/analyzer/tc_analyzer.c — Pass 2 类型检查中插入标签验证
```

### 任务

#### 4.1 块路径数据结构

分析器需要一个「块路径」来表示 goto 和 label 所在的作用域嵌套路径。路径由 `if` 的索引标识：

```c
/* 在 tc_analyzer.c 内部定义 */
typedef struct {
    int *path;         /* 块路径数组：path[0..depth-1] 每层 if 的 stmt_index 或块 ID */
    int depth;         /* 当前路径深度，0 = 全局 */
    int capacity;
} TcBlockPath;
```

**路径编码方案**（选择一个）：

**方案 A（推荐）**：使用 if 语句的 stmt_index 作为块标识。每个 `if` 进入时 push `if_stmt_index`；`end` 退出时 pop。标签保存其所在 `if` 的 stmt_index 列表。

**方案 B**：使用分析器递归时的深度层级计数器。每个 `if` 进入时 depth++，`end` 退出时 depth--。标签保存定义时的 depth（同 M3 的 `block_depth`）。但仅靠 depth 无法区分兄弟块——方案 B 需要额外信息。

**推荐方案 A 的跳转判定**：

```
goto 的块路径 = [G1, G2, ..., Gn]  (n = goto 所在块深度)
label 的块路径 = [L1, L2, ..., Lm] (m = label 所在块深度)

比较规则：
1. 若 n == m 且 G==L 逐层相等            → 平级跳转 ✅
2. 若 m < n 且 G[0..m-1] == L[0..m-1]    → 向外跳转 ✅（祖先）
3. 若 m > n 且 L[0..n-1] == G[0..n-1]    → 跳入子块 ❌ TC_ERR_JUMP_INTO_BLOCK
4. 其他情况                                → 兄弟跳转 ❌ TC_ERR_JUMP_TO_SIBLING_BLOCK
```

实际实现时，若 M3 的 `tc_symbol_table_find_label()` 自底向上查找，找到的标签一定是当前块或祖先块（因为同块优先，祖先次之）。不会找到子块或兄弟块中的标签。

**简化实现**：

```c
/* 找到标签后的判定 */
static int tc_check_goto_jump(const TcBlockPath *goto_path, const TcBlockPath *label_path,
                              const char *label_name, int line, TcDiagnostic *diag) {
    if (goto_path->depth == label_path->depth) {
        /* 检查是否在同一块 */
        int same_block = 1;
        for (int i = 0; i < goto_path->depth; i++) {
            if (goto_path->path[i] != label_path->path[i]) {
                same_block = 0;
                break;
            }
        }
        if (same_block) return 0;  /* 平级跳转 ✅ */
        tc_diagnostic_set(diag, TC_ERR_JUMP_TO_SIBLING_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into sibling block");
        return -1;
    }
    
    /* 检查是否祖先（向外跳转） */
    /* 即 goto_path 的前 label_path->depth 层与 label_path 完全一致 */
    if (goto_path->depth > label_path->depth) {
        int is_ancestor = 1;
        for (int i = 0; i < label_path->depth; i++) {
            if (goto_path->path[i] != label_path->path[i]) {
                is_ancestor = 0;
                break;
            }
        }
        if (is_ancestor) return 0;  /* 向外跳转 ✅ */
        tc_diagnostic_set(diag, TC_ERR_JUMP_TO_SIBLING_BLOCK, line, TC_COLUMN_UNKNOWN,
                          "cannot jump into sibling block");
        return -1;
    }
    
    /* goto_path->depth < label_path->depth */
    /* 跳入子块 */
    tc_diagnostic_set(diag, TC_ERR_JUMP_INTO_BLOCK, line, TC_COLUMN_UNKNOWN,
                      "cannot jump into inner block");
    return -1;
}
```

#### 4.2 分析器集成

在 `tc_pass2_check_stmt()` 中：

**`TC_STMT_LABEL_DEF`**（在 `tc_stmt_index_take` 之前的 if 外层）：
```c
if (stmt->kind == TC_STMT_LABEL_DEF) {
    /* 注册标签到符号表 */
    int stmt_index_val = tc_stmt_index_take(&ctx->index);
    /* 同时记录标签的块路径和 stmt_index */
    return 0;
}
```

**`TC_STMT_GOTO`**（在 `tc_stmt_index_take` 之前的 if 外层）：
```c
if (stmt->kind == TC_STMT_GOTO) {
    tc_stmt_index_take(&ctx->index);
    /* 查找标签 → 跳转合法性检查 */
    const TcLabelEntry *entry = tc_symbol_table_find_label(symbols, stmt->u.goto_stmt.target);
    if (!entry) {
        tc_diagnostic_set(diag, TC_ERR_LABEL_NOT_FOUND, stmt->u.goto_stmt.line,
                          TC_COLUMN_UNKNOWN, "label '%s' not found", stmt->u.goto_stmt.target);
        return -1;
    }
    /* 这里需要 entry 的块路径与当前 goto 的块路径比较 */
    /* 若 TcLabelEntry 也保存了块路径，则调用 tc_check_goto_jump() */
    return 0;
}
```

#### 4.3 标签存储块路径

`TcLabelEntry` 需要额外字段存储定义时的块路径：

```c
typedef struct {
    char *name;
    int stmt_index;
    int block_depth;
    int *block_path;     /* 块路径数组（堆分配），长度 = block_depth */
    int def_line;
} TcLabelEntry;
```

`tc_symbol_table_add_label()` 需要接受块路径参数，并进行深度拷贝。

#### 4.4 if 块路径维护

在 `tc_pass2_check_stmt()` 的 `TC_STMT_IF` 分支中（现有第 895–933 行），进入时 push if_stmt_index 到块路径栈，退出时 pop：

```c
if (stmt->kind == TC_STMT_IF) {
    int if_stmt_index = tc_stmt_index_take(&ctx->index);
    /* push if_stmt_index 到块路径栈 */
    ...
    /* then 和 else 检查 */
    ...
    /* pop 块路径栈 */
    return 0;
}
```

---

## M5：静态分析 — 数据流分析

### 目标

将未初始化变量读取从编译警告升级为静态编译错误。核心改动：引入分支感知的数据流分析，在 if 合并点做状态合并。

### 改动文件

```
src/vm/analyzer/tc_analyzer.c  — 数据流分析核心（控制流合并）
src/vm/runtime/tc_warning.c   — 移除未初始化警告相关代码
src/vm/runtime/tc_warning.h   — 移除 TC_WARN_UNINITIALIZED_VARIABLE
```

### 任务

#### 5.1 数据流状态表示

```c
/* 每个 slot 的初始化状态 */
typedef enum {
    TC_INIT_UNKNOWN,      /* 不确定（用于不动点迭代初始值） */
    TC_INIT_UNINIT,       /* 未初始化 */
    TC_INIT_INIT          /* 已初始化 */
} TcInitState;
```

在 `TcAnalyzeCtx` 中新增：

```c
typedef struct {
    TcProgram *program;
    int *last_init;             /* 旧有：单路径扫描用 */
    TcStmtIndexCursor index;
    TcInitState *init_states;   /* slot → 当前分析路径上的状态 */
    int num_slots;
    int in_goto_target;         /* 当前是否在 goto 目标位置 */
} TcAnalyzeCtx;
```

#### 5.2 状态管理函数

```c
/** 重置所有 slot 为给定状态 */
static void tc_init_states_reset(TcInitState *states, int num_slots, TcInitState s);

/** 复制状态数组 */
static void tc_init_states_copy(TcInitState *dst, const TcInitState *src, int num_slots);

/** 合并两个分支的状态：只有在两条路径都 INIT 时才 INIT，否则 UNINIT */
static void tc_init_states_merge(TcInitState *merged, const TcInitState *a, 
                                  const TcInitState *b, int num_slots);
```

合并规则：
```
merged[s] = (a[s] == TC_INIT_INIT && b[s] == TC_INIT_INIT) ? TC_INIT_INIT : TC_INIT_UNINIT
```

#### 5.3 Pass 2 改进：if 分支数据流

替换现有 `tc_pass2_check_stmt` 中 `TC_STMT_IF` 分支的逻辑（现有第 895–933 行）：

```c
if (stmt->kind == TC_STMT_IF) {
    /* 1. 保存 if 前的状态快照 */
    TcInitState *before_then = malloc(num_slots * sizeof(TcInitState));
    tc_init_states_copy(before_then, ctx->init_states, num_slots);
    
    /* 2. 检查条件表达式（现有逻辑） */
    int stmt_index_val = tc_stmt_index_take(&ctx->index);
    if (tc_check_if_condition(...) != 0) { free(before_then); return -1; }
    
    /* 3. 检查 then 分支，状态在 ctx->init_states 中演进 */
    for (i = 0; i < if_stmt->then_count; i++) {
        if (tc_pass2_check_stmt(...) != 0) { free(before_then); return -1; }
    }
    TcInitState *after_then = malloc(num_slots * sizeof(TcInitState));
    tc_init_states_copy(after_then, ctx->init_states, num_slots);
    
    /* 4. 从 before_then 恢复，检查 else 分支 */
    tc_init_states_copy(ctx->init_states, before_then, num_slots);
    for (i = 0; i < if_stmt->else_count; i++) {
        if (tc_pass2_check_stmt(...) != 0) { free(before_then); free(after_then); return -1; }
    }
    
    /* 5. 合并 then 和 else 的状态 */
    if (if_stmt->else_count > 0) {
        tc_init_states_merge(ctx->init_states, after_then, ctx->init_states, num_slots);
    } else {
        /* 无 else 时，合并 then 后的状态与 if 前的状态 */
        tc_init_states_merge(ctx->init_states, after_then, before_then, num_slots);
    }
    
    free(before_then);
    free(after_then);
    return 0;
}
```

#### 5.4 操作数检查升级（替换 `tc_maybe_warn_uninitialized`）

现有 `tc_maybe_warn_uninitialized()`（第 117 行）记录警告。改为直接报错：

```c
static int tc_check_operand_init(const TcSymbol *sym, int stmt_index, int line,
                                  TcInitState *init_states, TcDiagnostic *diag) {
    if (sym->sym_kind == TC_SYM_CONSTANT) return 0;  /* let 常量忽略 */
    if (sym->initialized) return 0;                    /* 定义时有值 */
    if (init_states[sym->slot] == TC_INIT_INIT) return 0;  /* 已初始化 */
    
    tc_diagnostic_set(diag, TC_ERR_UNINITIALIZED_VARIABLE, line, TC_COLUMN_UNKNOWN,
                      "use of uninitialized variable '%s'", sym->name);
    return -1;
}
```

所有调用 `tc_maybe_warn_uninitialized()` 的地方改为调用新的报错函数。

#### 5.5 赋值/read 更新状态

在 Pass 2 的 `TC_STMT_ASSIGN` 和 `TC_STMT_READ` 分支中，设置：

```c
if (sym) {
    ctx->init_states[sym->slot] = TC_INIT_INIT;
}
```

#### 5.6 短路求值兼容

对于 `and(bool, flag, uninit)`，分析器需要知道短路求值的特殊性。方案：

- 在 `tc_check_rhs()` 的 `TC_RHS_LOGIC_BIN` 分支中，先检查左侧操作数
- 若左侧为 `TC_INIT_INIT` 或 `TC_INIT_UNINIT`，右侧仍需检查（无法静态确定 false）
- 特例：若左侧是 `let` 常量 `false` 或字面量 `false`，右侧可跳过

实现：

```c
/* 在 tc_check_rhs 的 logic_bin case 中 */
if (op == TC_LOGIC_AND || op == TC_LOGIC_OR) {
    /* 先检查左操作数（始终检查） */
    /* 然后检查右操作数：仅当左操作数可能为 true(and)/false(or) 时需要检查 */
    int check_rhs = 1;
    if (rhs->u.logic_bin.lhs.type == TC_OPERAND_LITERAL) {
        TcLiteral *lit = &rhs->u.logic_bin.lhs.u.literal;
        if (lit->kind == TC_LIT_BOOL) {
            if ((op == TC_LOGIC_AND && !lit->u.bool_val) ||
                (op == TC_LOGIC_OR && lit->u.bool_val)) {
                check_rhs = 0;  /* 短路，右操作数不被读取 */
            }
        }
    }
    if (check_rhs) {
        /* 检查右操作数初始化 */
    }
}
```

#### 5.7 var 定义有 RHS 的状态更新

在 `tc_pass2_check_stmt()` 的 `TC_STMT_VAR_DEF` 分支中，`tc_var_def_init_rhs` 检查通过后：

```c
if (var_def->has_rhs) {
    TcSymbol *sym = tc_symbol_for_assign_target(...);
    if (sym) ctx->init_states[sym->slot] = TC_INIT_INIT;
}
```

#### 5.8 清理警告代码

- `tc_warning_list_add()` 中不再被未初始化检查调用
- `tc_warning.h` 注释更新：警告类型列表移除 `TC_WARN_UNINITIALIZED_VARIABLE`
- `TcTypedProgram` 中的 `TcWarningList warnings` 字段可保留（未来可能有新警告）
- 所有 `tc_maybe_warn_uninitialized()` 相关函数可删除，由 `tc_check_operand_init()` 替代

#### 5.9 goto 对数据流的影响

- `goto` 语句后的代码在当前路径上**不可达**（直到目标标签）
- 分析策略：
  - 遇到 `TC_STMT_GOTO`，停止当前顺序路径分析
  - 标签位置：分析器需要知道所有标签的 stmt_index，在顺序扫描到标签时恢复分析
  - 向后跳转的处理：采用**限定次数的迭代**（上限 3–5 轮），从标签处重新分析，合并 goto 时的状态与标签之前的状态
- 简化实现（首版）：
  - 将 goto 视为"不改变状态，但终止当前路径"
  - 从 goto 之后到目标标签之间的代码，若存在变量赋值，goto 跳过的路径视为"未经过这些赋值"
  - 向后跳转的循环场景，保留变量的状态按最坏情况处理

---

## M6：VM 执行器 — goto 跳转

### 目标

VM 执行器支持 `TC_STMT_GOTO` 的无条件跳转和 `TC_STMT_LABEL_DEF` 的零成本标签。

### 改动文件

```
src/vm/executor/tc_executor.c — goto/label 执行分支
```

### 任务

#### 6.1 执行器分发

在 `tc_execute_statement_impl()` 中新增分支（参考现有第 349 行起的 if-else 链模式）：

**`TC_STMT_LABEL_DEF`**（在 `TC_STMT_IF` 的 if 外层）：

```c
if (stmt->kind == TC_STMT_LABEL_DEF) {
    tc_stmt_index_take(&ctx->index);  /* 消耗一个序号 */
    return 0;                          /* 零成本操作 */
}
```

**`TC_STMT_GOTO`**（在 `TC_STMT_IF` 的 if 外层）：

```c
if (stmt->kind == TC_STMT_GOTO) {
    tc_stmt_index_take(&ctx->index);  /* 消耗当前语句的序号 */
    /* 查找目标标签的 stmt_index */
    const TcLabelEntry *entry = tc_symbol_table_find_label(symbols, stmt->u.goto_stmt.target);
    if (!entry) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, stmt->u.goto_stmt.line, TC_COLUMN_UNKNOWN,
                          "internal error: label '%s' not resolved", stmt->u.goto_stmt.target);
        return -1;
    }
    /* 设置 stmt_index 到目标标签的下一条语句 */
    ctx->index.next = entry->stmt_index + 1;
    return 0;
}
```

#### 6.2 stmt_index 标签映射构建

执行器需要一个从标签名到 stmt_index 的快速映射。有两种实现方式：

**方式 A（推荐）**：在执行器初始化阶段，遍历符号表的 labels 数组，构建哈希表或线性搜索数组。

**方式 B**：在分析阶段（M4）建立映射，存储在 `TcTypedProgram` 或 `TcSymbolTable` 中。

推荐方式 B——在 M4 的标签验证阶段已经知道每个标签的 stmt_index，可以直接将 `TcLabelEntry.stmt_index` 赋值。

#### 6.3 边界情况

| 场景 | 处理方式 |
|---|---|
| 向后跳转（循环） | `ctx->index.next` 变小，外层 for 循环继续执行 |
| 向前跳转（跳过代码） | `ctx->index.next` 变大，被跳过语句的序号不会被分配 |
| 跳出 if 块 | `ctx->index.next` 设为标签的 stmt_index + 1，位于 end 之后 |
| 标签在 if 块外，goto 在块内 | 执行到 `goto` 时 IP 跳转，不再执行 `end` 的 scope 清理（变量槽已由分析器验证） |

#### 6.4 模块化注意事项

- 执行器可调用 `tc_symbol_table_find_label()`（符号表已在 `tc_execute()` 参数中传入）
- 不需要新增 `TcExecuteCtx` 字段（`ctx->index.next` 直接修改即可）

---

## M7：AOT 代码生成 — 标签/跳转

### 目标

AOT 编译器为 `TC_STMT_LABEL_DEF` 和 `TC_STMT_GOTO` 生成原生 C 代码。

### 改动文件

```
src/aot/tc_aot_codegen.c — goto/label 代码生成
```

### 任务

#### 7.1 AOT 标签命名

使用唯一标签名避免与 C 代码冲突：

```c
/* 标签名修饰：tc_l_<原始标签名>_<hash> */
/* 或用全局计数器：tc_l_<counter> */
static int tc_aot_label_counter = 0;
```

#### 7.2 代码生成

在 `tc_aot_emit_statement_impl()` 中新增分支（参考现有第 514 行起的模式）：

**`TC_STMT_LABEL_DEF`**：

```c
if (stmt->kind == TC_STMT_LABEL_DEF) {
    int stmt_index_val = tc_stmt_index_take(&ctx->index);
    /* 生成 C 标签：goto 目标 */
    fprintf(out, "%stc_l_%s_%d:\n", indent, stmt->u.label_def.name, stmt_index_val);
    /* 需要建立标签名 → 生成的 C 标签名的映射 */
    return 0;
}
```

**`TC_STMT_GOTO`**：

```c
if (stmt->kind == TC_STMT_GOTO) {
    tc_stmt_index_take(&ctx->index);
    /* 查找对应 label 的 stmt_index，生成 goto 语句 */
    const TcLabelEntry *entry = tc_symbol_table_find_label(symbols, stmt->u.goto_stmt.target);
    if (entry) {
        fprintf(out, "%sgoto tc_l_%s_%d;\n", indent, stmt->u.goto_stmt.target,
                entry->stmt_index);
    }
    return 0;
}
```

#### 7.3 AOT 标签映射表

在 `TcAotEmitCtx` 中新增标签名 → 生成的 C 标签名的映射：

```c
typedef struct {
    /* 现有字段 */
    TcStmtIndexCursor index;
    const char **label_names;   /* 生成的 C 标签名数组 */
    size_t label_count;
    size_t label_capacity;
    /* ... */
} TcAotEmitCtx;
```

简化实现：使用标签的 `stmt_index` 嵌入 C 标签名即可（无需额外映射表）：

```c
/* label "foo" at stmt_index 5 → tc_label_5: */
/* goto "foo" → goto tc_label_5; */
```

通过 `tc_symbol_table_find_label()` 获取目标标签的 stmt_index 来构造 goto 目标名。

#### 7.4 变量声明插入

AOT 模式下，若变量在 label 后首次使用，需确保 C 编译器在对应作用域内可见。goto 跳过某些变量声明会导致 C 编译错误 (`jump to label crosses initialization of...`)。

解决方案：若 AOT 引擎将所有 `var` 声明提升到函数顶部（VM 的 slot 模型天然支持），则 goto 跳过声明不会触发 C 错误。检查现有 AOT 是否已将 `var` 声明统一放在顶部——若是，则无问题。

---

## M8：测试

### 目标

全部新特性测试通过，旧 warning 测试迁移为 error 测试，在 `run_tests.sh` 中注册。

### 改动文件

```
tests/vm/goto_*.tc              — goto 测试（约 10 个文件）
tests/errors/static/goto_*.tc   — goto 静态错误测试（约 4 个文件）
tests/errors/static/uninit_*.tc — 未初始化变量错误测试（约 5 个文件）
scripts/vm/run_tests.sh         — 注册新测试
tests/unit/runtime/test_types.c — error_kind_name 单元测试
test-map.md                     — 更新测试映射
```

### 正向测试（验证正确执行）

所有 `.tc` 文件放到 `tests/vm/` 目录下，使用 `scripts/vm/run_tests.sh` 注册。

**1. `goto_simple.tc`** — 平级向后跳转模拟循环

```text
; 向后平级跳转（模拟循环）
label start:
var i: int32 = 0
i = add(int32, i, 1)
if lt(int32, i, 10) then
    goto start
end
writeln(int32, %d, i)  ; 10
```
- 预期输出：`10`

**2. `goto_forward.tc`** — 向前跳转跳过代码

```text
; 向前跳转
goto skip
var x: int32 = 42      ; 被跳过
label skip:
var y: int32 = 10
writeln(int32, %d, y)  ; 10
```
- 预期输出：`10`

**3. `goto_out_of_if.tc`** — 跳出 if 块

```text
var x: int32 = 5
if gt(int32, x, 0) then
    x = mul(int32, x, 2)
    goto after_if
end
label after_if:
writeln(int32, %d, x)  ; 10
```
- 预期输出：`10`

**4. `goto_nested_out.tc`** — 跨多层向外跳转

```text
label done:
var a: int32 = 1
if eq(int32, a, 0) then
    ; 不进入
else
    if eq(int32, a, 1) then
        goto done
    end
end
writeln(int32, %d, 99)  ; 99
```
- 预期输出：`99`

**5. `goto_label_same_name.tc`** — 不同块同名标签

```text
var result: int32 = 0
if eq(int32, 1, 0) then
    label L:
    result = 10
else
    label L:            ; 合法，互斥作用域
    result = 20
end
writeln(int32, %d, result)  ; 20
```
- 预期输出：`20`

### 负向测试（验证静态错误）

所有 `.tc` 文件放到 `tests/errors/static/` 下，注册到 `scripts/vm/run_tests.sh` 的 error 分区。

**6. `goto_undefined.tc`** → 错误码 `TC_ERR_LABEL_NOT_FOUND`

```text
goto nonexistent
```
- 预期：`LabelNotFound`

**7. `label_duplicate.tc`** → 错误码 `TC_ERR_DUPLICATE_LABEL`

```text
label dup:
label dup:
```
- 预期：`DuplicateLabel`

**8. `goto_into_block.tc`** → 错误码 `TC_ERR_JUMP_INTO_BLOCK`

```text
goto inner
if eq(int32, 1, 1) then
    label inner:
end
```
- 预期：`JumpIntoBlockError`

**9. `goto_sibling.tc`** → 错误码 `TC_ERR_JUMP_TO_SIBLING_BLOCK`

```text
if eq(int32, 1, 1) then
    goto else_branch
else
    label else_branch:
end
```
- 预期：`JumpToSiblingBlockError`

### 未初始化变量测试（升级为错误）

**10. `uninit_simple.tc`** — 定义无 RHS 后直接读取 → 错误

```text
var x: int32
var y: int32 = add(int32, x, 1)
```
- 预期：`UninitializedVariable`

**11. `uninit_if_path.tc`** — if 中一条路径未初始化 → 错误

```text
var x: int32
var flag: bool = true
if flag then
    x = 10
end
var y: int32 = add(int32, x, 1)  ; x 在 else 路径中未初始化
```
- 预期：`UninitializedVariable`

**12. `uninit_both_paths.tc`** — then 和 else 均初始化 → 合法

```text
var x: int32
var flag: bool = true
if flag then
    x = 10
else
    x = 20
end
var y: int32 = add(int32, x, 1)  ; 两条路径均已初始化
```
- 预期正常执行，输出省略

**13. `uninit_shortcircuit.tc`** — 短路求值不触发错误

```text
var flag: bool = false
var uninit: bool
var result: bool = and(bool, flag, uninit)  ; 合法，右侧不被读取
writeln(bool, %t, result)  ; false
```
- 预期输出：`false`

**14. `uninit_goto_skip_init.tc`** — goto 跳过赋值 → 错误

```text
var x: int32
goto label_skip
x = 42
label skip:
var y: int32 = add(int32, x, 1)  ; x 被 goto 跳过了赋值
```
- 预期：`UninitializedVariable`

### 通用注册任务

- 注册所有 `.tc` 测试到 `scripts/vm/run_tests.sh`（参考现有 `if_*` 测试的注册模式）
- 更新 `test-map.md`——新增 goto/label 测试映射条目
- 更新 `tests/unit/runtime/test_types.c`——新增 5 个 error_kind_name 单元测试
- 更新 `tests/unit/runtime/test_analyzer.c` 或 `test_warning.c`——将警告测试改为错误测试

### 验收标准

1. `make test` 全量通过（VM + unit + AOT）
2. `python3 scripts/sync/check_rhs_coverage.py` 无错误（无 TcRhsKind 变动则跳过）
3. `python3 scripts/sync/check_source_naming.py` 无错误
4. 所有 `.tc` 正向测试输出与注释中的预期一致
5. 所有 `.tc` 负向测试的错误码与 §11.4 一致

---

## M9：文档同步

### 目标

将草案正式化为标准说明书，更新实现文档和架构图谱。

### 改动文件

```
docs/TC语言标准设计说明书.md                       — v0.0.25 → v0.0.26
docs/TC-VM详细设计说明书.md                        — 新增 goto/label 执行器章节
docs/TC-AOT详细设计说明书.md                       — 新增 AOT 标签/跳转章节（如适用）
docs/设计实现合规审查报告.md                        — 更新审查项（新特性 + 新增错误码）
.cursor/skills/tc-architecture/features.md          — goto/label 特性条目
.cursor/skills/tc-architecture/errors.md            — 新增 5 个错误码 + 移除警告码
.cursor/skills/tc-architecture/locations.md         — 新模块入口（tc_symbol 标签管理）
.cursor/rules/knowledge-graph.mdc                   — goto/label + 数据流分析子图
```

### 任务

- [x] 基于 `_0.0.26.md` 草案同步 `TC语言标准设计说明书.md`（升为现行标准）
- [x] VM 详设：goto/label 执行器章节（`tc_executor.c` 的 IP 跳转、stmt_index 操作）— 见 `docs/TC-VM详细设计说明书.md` §6.1.7/§7.4.3/§7.7/§8.6；实现状态 ✅
- [x] 错误文档：5 个新错误码 + `tc_error_kind_name()` 打印名 — `errors.md` / 标准 §11.4
- [x] 特性文档：goto/label 受限跳转（4 种判定规则）、标签块级作用域 — `features.md`
- [x] 位置文档：符号表标签管理（M3）、分析器跳转判定（M4）、数据流分析（M5）— `locations.md`
- [x] 知识图谱：goto/label 子图（作用域、跳转判定、执行跳转）+ 数据流分析子图
- [x] AOT 详设：标签/跳转章节 — 见 `docs/TC-AOT详细设计说明书.md` §4.7；实现状态 ✅
- [x] 合规审查报告 / AGENTS / syntax / pipeline / test-map 同步 v0.0.26

---

## 依赖关系图

```
M1（基础设施）
├──→ M2（词法/语法）─→ ─┐
├──→ M3（符号表）───────→ M4（标签验证）─→ ─┐
│                                          ├──→ M6（VM 执行器）
│     M5（数据流分析）─────────────────────────── → ─┤
│                                          ├──→ M7（AOT）
│                                          └──→ M8（测试）
│
M8 ──→ M9（文档同步）
```

M5（数据流分析）与 M2/M4 无实质依赖，可与 M2/M3/M4 并行开发。

---

## 各里程碑交付清单

| M | 涉及文件 | 核心产出 |
|---|---|---|
| M1 | `tc_types.h`、`tc_types.c`、`tc_warning.h`、`tc_version.h`、`test_types.c` | 枚举定义、错误码映射、版本号 |
| M2 | `tc_lexer.c`、`tc_lexer.h`、`tc_parser.c` | 关键字 Token、AST 节点（label_def/goto_stmt） |
| M3 | `tc_symbol.h`、`tc_symbol.c` | `TcLabelEntry`、`tc_symbol_table_add_label/find_label/pop_labels` |
| M4 | `tc_analyzer.c` | 标签解析 + 4 种跳转判定（TcBlockPath） |
| M5 | `tc_analyzer.c`、`tc_warning.c`、`tc_warning.h` | 数据流分析（TcInitState、if 合并、短路兼容） |
| M6 | `tc_executor.c` | goto IP 跳转（`ctx->index.next` 直接修改） |
| M7 | `tc_aot_codegen.c` | C 标签 `tc_label_<n>:`, `goto tc_label_<n>;` |
| M8 | `tests/vm/goto_*.tc`、`tests/errors/static/goto_*.tc`、`tests/errors/static/uninit_*.tc`、`run_tests.sh` | 全量测试通过 |
| M9 | 说明书 + 配套文档（约 8 个文件） | 文档与实现一致 |

---

## 附录 A：标准 §4.8 goto 语义 → 实现映射

| §4.8 规则 | 里程碑 | 实现位置 | 实现方式 |
|---|---|---|---|
| 标签定义 `label name:` | M2 | `tc_parser.c` | 解析 `TC_TOK_LABEL` + identifier + `:` → `TC_STMT_LABEL_DEF` |
| 跳转语句 `goto name` | M2 | `tc_parser.c` | 解析 `TC_TOK_GOTO` + identifier → `TC_STMT_GOTO` |
| 标签作用域（与 var/let 一致） | M3 | `tc_symbol.c` | `pop_labels()` 在 `pop_scope()` 中自动调用 |
| 标签查找（自 Src 向上） | M4 | `tc_symbol.c` | `find_label()` 自底向上遍历标签表 |
| 平级跳转 ✅ | M4 | `tc_analyzer.c` | 块路径比较：`depth == depth && path == path` |
| 向外跳转 ✅ | M4 | `tc_analyzer.c` | 块路径比较：goto 的前 label.depth 层等于 label 路径 |
| 跳入子块 ❌ | M4 | `tc_analyzer.c` | label 深度 > goto 深度 → `TC_ERR_JUMP_INTO_BLOCK` |
| 跳入兄弟 ❌ | M4 | `tc_analyzer.c` | 非祖先/后代/同级 → `TC_ERR_JUMP_TO_SIBLING_BLOCK` |
| 标签未找到 ❌ | M4 | `tc_analyzer.c` | `find_label()` 返回 NULL → `TC_ERR_LABEL_NOT_FOUND` |
| 标签重复定义 ❌ | M3 | `tc_symbol.c` | `add_label()` 检查当前块深度内重名 |
| goto 执行（零成本标签） | M6 | `tc_executor.c` | label 仅 `stmt_index_take()` + return 0 |
| goto 跳转执行 | M6 | `tc_executor.c` | `ctx->index.next = entry->stmt_index + 1` |
| AOT 标签 | M7 | `tc_aot_codegen.c` | 生成 `tc_label_<stmt_index>:` |
| AOT 跳转 | M7 | `tc_aot_codegen.c` | 生成 `goto tc_label_<stmt_index>;` |

## 附录 B：标准 §4.2 数据流分析 → 实现映射

| §4.2 规则 | 里程碑 | 实现位置 | 实现方式 |
|---|---|---|---|
| var 定义无 RHS → 未初始化 | M5 | `tc_analyzer.c` | `init_states[sym->slot] = TC_INIT_UNINIT` |
| 赋值/read → 已初始化 | M5 | `tc_analyzer.c` | `init_states[sym->slot] = TC_INIT_INIT` |
| if 分支合并（存在未初始化 → UNINIT） | M5 | `tc_analyzer.c` | `tc_init_states_merge()`：`a && b → INIT` |
| 无 else 时保持 if 前状态 | M5 | `tc_analyzer.c` | 合并 then 状态与 before_then 状态 |
| goto 跳过初始化 | M5 | `tc_analyzer.c` | 向后跳转采用迭代法合并状态 |
| 短路求值不触发错误 | M5 | `tc_analyzer.c` | `check_rhs` 中检查字面量短路条件 |
| 读取未初始化 → 静态错误 | M5 | `tc_analyzer.c` | `tc_check_operand_init()` → `TC_ERR_UNINITIALIZED_VARIABLE` |

---

*— 文档结束 —*
