# TC 语言语法摘要（v0.0.41）

**何时读**：快速查 EBNF 结构。完整语法：`docs/TC语言标准设计说明书-0.0.41.md`。

一行一句；分号可选；注释以 `;` 开头；模块化程序；块级作用域；受限 `goto`/`label` 与 while 范式隔离。**无 REPL**。

```
文件 = 模块头 { 顶层声明 | 语句 [;] | 注释 | 空行 }

模块头 = #program name | #lib name
顶层   = import … | struct … | static let/var … | func … | 语句

语句 = var id: type = rhs
     | let id: type = const_rhs
     | id = rhs | field_assign | ptr/memblock 存储语句
     | write/writeln(type [, fmt,] operand)
     | read(type, id)
     | if_stmt | while_stmt | break | continue
     | label_def | goto_stmt          /* 仅函数内 */
     | funcall | return

if_stmt / while_stmt / indented_block — 同前（缩进 R1–R7；R7=label 不增层级）

rhs = 字面量 | true | false | nullptr
    | 算术/一元/比较/逻辑/按位/移位/cast/bitcast/浮点 …
    | memblock/ptr/struct 构造与运算 | field_read | Self.member | funcall_expr

operand = identifier | literal | field_access | memblock_count_access | …
          /* field_access：cur.score / a.b.c / Self.x.y 作运算实参，非嵌套 RHS */

const_rhs = 字面量 | const_ref | 单层标量运算 | struct ctor | field_read
          /* 禁 FUNCALL_EXPR / 多数 ptr·memblock 路径；let px = p.x 合法 */

类型 = int8|…|uint64|bool|float32|float64|isize|usize|void
     | ptr(T) | memblock(T, N) | 本模块 struct | Mod.Struct
     /* 导入结构体须 <模块名>.<结构体名>；裸名不解析为已 import 的 struct */
格式 = %d %i %u %x %X %o %b %t %f %e %E %g %G
```

## 关键语义

- **模块**：`#program` 入口 / `#lib` 库；`import` 经 `-I` 搜索；`Self` 仅 `#lib`；导入成员（含 struct 类型名/构造器）须 `<模块名>.<名>`
- **函数**：`func` / `funcall` / `return`；递归环静态拒绝；goto/label 仅函数内
- **var 强制初始化**：缺 `=` → `VarMissingInitializer`
- **移位 / 按位重载 / 短路读集 / cast·truncate·bitcast**：同标量规则（见 features §）
- **if / while**：条件须 bool；块级作用域；AOT 原生 C 控制流
- **goto / label**：平级/向外；禁跳入子块/兄弟块；while 内禁止；顶层禁止
- **确定初始化**：多域 CFG 固定点（顶层 + 每函数）；`MISSING_RETURN`
- **Embed**：宿主经 `tc_embed_*` 调已编译函数（非语言语法）

权威来源：`docs/TC语言标准设计说明书-0.0.41.md` · 诊断：[errors.md](errors.md) · 模块/函数：[kg-module.md](kg-module.md) / [kg-func.md](kg-func.md)
