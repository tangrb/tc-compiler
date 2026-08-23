# TC 0.0.39 编译器完整开发计划

> **计划日期**：2026-07-23
> **目标**：构建完整的 TC 0.0.39 编译器（VM 解释器 + AOT C99 编译器 + libtc 嵌入库）
> **现状**：v0.0.31 已有 Lexer/Parser/Analyzer/CFG/Executor/AOT 等核心基础
> **新增**：模块系统、函数系统、ptr<T>/memblock<T,N>/struct 类型系统、13 阶段编译管线

---

## 第一部分：系统架构与全局视图

### 1. TC 0.0.39 语言能力全景

TC 是面向教学与 AI 自动化编程的指令式语言。本节定义 0.0.39 版本的完整语言能力范围（不是增量，是本版本作为独立产品的完整规格）。

```
┌──────────────────────────────────────────────────────────────┐
│                    TC 0.0.39 语言边界                          │
├──────────────┬───────────────────────────────────────────────┤
│ 类型系统      │ int8~uint64, isize/usize, float32/64, bool   │
│              │ ptr<T>, memblock<T,N>, struct, void(仅返回)     │
├──────────────┼───────────────────────────────────────────────┤
│ 运算         │ 算术: add/sub/mul/div/mod/abs/neg              │
│              │ 位: and/or/xor/not/shl/shr                     │
│              │ 比较: eq/ne/lt/le/gt/ge                        │
│              │ 逻辑: and/or/xor/not (bool 重载)               │
│              │ 转换: cast / cast(truncate) / bitcast           │
│              │ 指针: ptr_load/store/address/add/sub/eq~ge/size │
│              │ 内存: memblock_load/store/copy, memcopy_unsafe  │
│              │ 结构体: 构造器, .field 读/写                    │
│              │ 查询: .count, ptr_size                          │
├──────────────┼───────────────────────────────────────────────┤
│ 模式         │ strict(默认), wrap(整数有符号+shl), ieee(浮点) │
├──────────────┼───────────────────────────────────────────────┤
│ 控制流       │ if/then/else/end, while/then/end               │
│              │ break, continue, goto, label(函数内,while外)    │
│              │ 静态布尔三态(true/false/unknown)               │
│              │ 确定初始化(最大固定点)                          │
├──────────────┼───────────────────────────────────────────────┤
│ 函数         │ func 定义(#lib内), funcall(命名实参)           │
│              │ return(有值/void), 按值只读形参                 │
│              │ 调用图无环(递归=error)                         │
├──────────────┼───────────────────────────────────────────────┤
│ 模块         │ #program(入口), #lib(库)                       │
│              │ import / public / private / Self               │
│              │ static var(拓扑初始化), static let(内联)        │
│              │ DAG 依赖图(循环导入=error)                     │
├──────────────┼───────────────────────────────────────────────┤
│ 声明         │ var(强制初始化), let(编译期求值)                │
│              │ op(T, a, b) 内建调用外壳                       │
├──────────────┼───────────────────────────────────────────────┤
│ I/O          │ write/writeln(13种格式符), read(标量)          │
├──────────────┼───────────────────────────────────────────────┤
│ 诊断         │ 70+ 错误码(CE静态 / RE运行时), 无编译警告       │
│              │ fail-fast 单槽, 阶段优先→源位置优先→规则优先    │
└──────────────┴───────────────────────────────────────────────┘
```

### 2. 编译器系统架构

```
                             .tc 源文件
                                 │
                    ┌────────────┴────────────┐
                    │     libtc (嵌入库)       │
                    │  tc_compile_file_opts /       │
                    │  tc_compile_source  │
                    └────────────┬────────────┘
                                 │
              ┌──────────────────┼──────────────────┐
              │  13 阶段确定性编译管线                │
              │                                     │
              │  阶段1: UTF-8解码                    │
              │  阶段2: 词法+缩进(Token)              │
              │  阶段3: 语法解析(AST)                 │
              │  阶段4: 模块解析(4a→4b→4c→4d)        │
              │  阶段5: 函数签名检查                  │
              │  阶段6: 名称/作用域/类型(6a→6b→6c→6d→6e)│
              │  阶段7: funcall 检查                  │
              │  阶段8: return 检查                   │
              │  阶段9: let/static let 求值           │
              │  阶段10: 静态布尔+读边判定             │
              │  阶段11: CFG+确定初始化(多域)          │
              │  阶段12: 调用图环检查                 │
              │                                     │
              └──────────────────┬──────────────────┘
                                 │
                         TcTypedProgram
                    (静态验证完成的程序)
                                 │
              ┌──────────────────┼──────────────────┐
              │                  │                  │
     ┌────────┴────────┐  ┌─────┴─────┐  ┌────────┴────────┐
     │   TC-VM 执行器   │  │ TC-AOT    │  │  常量求值器      │
     │  (解释执行)      │  │ (C99生成) │  │  (let/static)    │
     └─────────────────┘  └───────────┘  └─────────────────┘
              │                  │                  │
              └──────────────────┼──────────────────┘
                                 │
                    共享运行时语义层
        ┌────────────┬───────────┬───────────┬───────────┐
        │ tc_sem_int │ tc_sem_fp │tc_sem_cast│ tc_sem_   │
        │ (整数)     │ (浮点)    │ (转换)    │ bitwise   │
        └────────────┴───────────┴───────────┴───────────┘
        ┌────────────────────────────────────────────────┐
        │  tc_io (I/O)  │ tc_diagnostic (单槽诊断)       │
        └────────────────────────────────────────────────┘
```

### 3. 模块组织

```
src/
├── vm/                           # TC-VM 解释器
│   ├── driver/                   # CLI入口（批量文件模式）
│   │   ├── main.c                # tc-vm CLI
│   │   └── tc_driver.c/h         # 文件执行协调
│   ├── lexer/                    # 词法分析器
│   │   ├── tc_lexer.c/h          # 最长匹配, 缩进栈, Token生成
│   │   └── tc_token.h            # Token类型枚举
│   ├── parser/                   # 语法解析器
│   │   ├── tc_parser.c/h         # 主解析(入口, 模块, 语句分发, 块, 缩进)
│   │   ├── tc_parser_struct.c/h  # struct 定义/字段/@padding
│   │   ├── tc_parser_type.c/h    # 类型语法 tc_parse_type_syntax
│   │   ├── tc_parser_func.c/h    # 函数定义
│   │   ├── tc_parser_stmt.c/h    # var/static/import/return/funcall/字段/ptr/memblock 语句
│   │   ├── tc_parser_rhs.c/h     # RHS表达式解析
│   │   └── tc_parser_free.c/h    # AST递归释放
│   ├── analyzer/                 # 静态分析器
│   │   ├── tc_analyzer.c/h       # 分析协调(13阶段调度)
│   │   ├── tc_analyzer_pass2.c/h # Pass2 语句遍历/goto/label
│   │   ├── tc_analyzer_pass2_rhs.c/h # Pass2 RHS 类型检查（tc_check_rhs）
│   │   ├── tc_scope.c/h          # 作用域与符号表 [新增]
│   │   ├── tc_module.c/h         # 模块加载/DAG/导入 [新增]
│   │   ├── tc_type_check.c/h     # 类型检查主逻辑
│   │   ├── tc_memblock_check.c   # memblock验证 [新增]
│   │   ├── tc_ptr_check.c        # 指针验证 [新增]
│   │   ├── tc_struct_check.c     # 结构体验证 [新增]
│   │   ├── tc_func_check.c/h     # 函数签名/funcall/return [新增]
│   │   ├── tc_cfg.c/h            # CFG构建+确定初始化
│   │   ├── tc_const_eval.c/h     # let/static let常量求值
│   │   └── tc_callgraph.c/h      # 调用图环检查 [新增]
│   ├── executor/                 # 执行器
│   │   ├── tc_executor.c/h       # 执行主循环
│   │   ├── tc_call_frame.c/h     # 调用帧管理 [新增]
│   │   ├── tc_memblock_exec.c    # memblock运行时 [新增]
│   │   ├── tc_ptr_exec.c         # 指针运行时 [新增]
│   │   └── tc_struct_exec.c/h    # struct运行时 [新增]
│   └── runtime/                  # 共享运行时
│       ├── tc_version.h          # 版本号 v0.0.39（含 Embed）
│       ├── tc_types.c/h          # 类型系统(TcTypeTag, TcType, 等价, 宽度)
│       ├── tc_error.c/h          # 错误码(TcErrorKind, 打印名)
│       ├── tc_symbol.c/h         # 符号表
│       ├── tc_diagnostic.c/h     # 单槽诊断
│       ├── tc_sem_int.c/h        # 整数语义(strict/wrap)
│       ├── tc_sem_fp.c/h         # 浮点语义(strict/ieee)
│       ├── tc_sem_cast.c/h       # 转换语义
│       ├── tc_sem_bitwise.c/h   # 位运算语义
│       ├── tc_io.c/h             # I/O(13种格式符)
│       └── tc_stmt_index.h       # 语句序号
├── aot/                          # TC-AOT 编译器
│   ├── main.c                    # tc-aot CLI
│   ├── tc_aot_codegen.c/h        # TcTypedProgram → C99
│   └── tc_aot_rt.c/h             # 运行时shim(ptr/memblock/struct)
└── libtc/                        # 嵌入库入口
    └── tc_lib.c/h                # tc_compile_*_opts/run
```

### 4. 13 阶段编译管线（完整系统视图）

```
       源文件 (.tc)
           │
  ┌────────┴─────────────────────────────────────────────────┐
  │  阶段1: UTF-8 解码                                        │
  │  - 拒绝 BOM、U+0000、非法 UTF-8 → TC_CE_SYNTAX            │
  │  - 文件打开失败 → API层诊断                                │
  ├──────────────────────────────────────────────────────────┤
  │  阶段2: 词法与缩进扫描                                     │
  │  - 最长匹配词法分析器                                      │
  │  - 缩进级别栈 (4空格=1级, INDENT/DEDENT)                  │
  │  - 字面量Token自身上限检查 → TC_CE_LITERAL_OUT_OF_RANGE    │
  │  - 30+ 保留关键字识别                                      │
  │  - 特殊Token: nullptr, inf, -inf, nan, @padding          │
  ├──────────────────────────────────────────────────────────┤
  │  阶段3: 语法解析                                           │
  │  - 附录A权威EBNF匹配                                       │
  │  - 受限恢复: TC_CE_MODULE_LAYER, TC_CE_MISSING_VISIBILITY  │
  │  - 操作数数量权威表验证                                    │
  │  - 模块头(#program/#lib), import, func, funcall, return   │
  │  - struct定义, memblock<T,N>, ptr<T>, static声明          │
  │  - 语法拒绝的程序不进入后续阶段                             │
  ├──────────────────────────────────────────────────────────┤
  │  阶段4: 模块结构与导入解析                                  │
  │  ├─ 4a: 单文件结构                                         │
  │  │  五层排序验证 (模式指令→import→struct→值声明→函数/语句)   │
  │  │  #lib成员可见性检查 (TC_CE_MISSING_VISIBILITY)           │
  │  │  #program误用检查 (TC_CE_PROGRAM_MODE_MISUSE)            │
  │  ├─ 4b: 导入解析                                           │
  │  │  目标定位 NOT_FOUND / NOT_LIB / AMBIGUOUS / DUPLICATE  │
  │  │  名称冲突冲突 (IMPORT_NAME_CONFLICT)                     │
  │  ├─ 4c: 依赖图环检查                                       │
  │  │  DAG验证 → TC_CE_CIRCULAR_IMPORT                        │
  │  └─ 4d: 收集函数签名                                       │
  │      全部可达#lib的函数签名收集                             │
  ├──────────────────────────────────────────────────────────┤
  │  阶段5: 函数重名与签名检查                                  │
  │  - 重复定义 → TC_CE_DUPLICATE_FUNCTION                     │
  │  - 函数名冲突 → TC_CE_FUNCTION_NAME_CONFLICT               │
  │  - 参数重名 → TC_CE_DUPLICATE_PARAMETER                    │
  │  - 全局函数名保护 (参数名与全局函数冲突)                     │
  ├──────────────────────────────────────────────────────────┤
  │  阶段6: 名称/作用域/类型语义                                 │
  │  ├─ 6a: 控制流上下文                                       │
  │  │  goto/label func祖先检查 / while祖先检查                │
  │  │  break/continue while外检查                             │
  │  ├─ 6b: 名称作用域预建                                     │
  │  │  函数表/标签表/本库成员索引建立                          │
  │  │  全局函数名冲突检查(块内var/let vs func)                 │
  │  ├─ 6c: goto/label名称解析                                 │
  │  │  祖先链标签查找, 跨控制流域判定                          │
  │  ├─ 6d: 类型/模式/字面量检查                                │
  │  │  RHS类型验证, 字面量上下文检查                           │
  │  │  memblock N比较, 构造器验证                             │
  │  │  struct构造器全字段验证                                  │
  │  │  指针类型验证, 可变性, nullptr定型                       │
  │  │  Self解析, private访问检查                              │
  │  └─ 6e: I/O格式检查                                        │
  │      格式说明符解析, 类型兼容性验证                          │
  ├──────────────────────────────────────────────────────────┤
  │  阶段7: funcall 检查                                       │
  │  - 目标存在/可见性 (UNDEFINED_FUNCTION/SCOPE_ACCESS/PRIVATE)
  │  - 调用位置 (FUNCALL_POSITION)                             │
  │  - 实参检查 (MISSING/DUPLICATE/UNKNOWN/ORDER/TYPE)         │
  │  - 接收类型检查 (FUNCALL_RESULT_TYPE)                      │
  ├──────────────────────────────────────────────────────────┤
  │  阶段8: return 检查                                        │
  │  - 函数体内位置 (RETURN_OUTSIDE_FUNCTION)                  │
  │  - 有值/无值形式匹配 (RETURN_FORM)                         │
  │  - 操作数类型匹配 (RETURN_TYPE)                            │
  ├──────────────────────────────────────────────────────────┤
  │  阶段9: let/static let求值 + static var初始化器验证         │
  │  - 编译期常量表达式形态验证                                  │
  │  - 按源序求值全部let                                       │
  │  - 浮点精度规则(binary32/64, roundTiesToEven, 禁止FMA)     │
  │  - static let按依赖拓扑求值                                 │
  │  - static var初始化器编译期约束验证                         │
  ├──────────────────────────────────────────────────────────┤
  │  阶段10: 静态布尔三态 + 逻辑读边判定                        │
  │  - if/while条件: static true/false/unknown                │
  │  - and/or 短路裁剪                                         │
  ├──────────────────────────────────────────────────────────┤
  │  阶段11: CFG构建 + 可达性 + 确定初始化(多域)               │
  │  - 顶层CFG (IN=∅, 无return/goto/label)                    │
  │  - 函数CFG (IN=全部形参, 含return/goto/label/break/continue)
  │  - 传递函数 OUT[var]=IN∪{x}, OUT[let]=IN                   │
  │  - 最大固定点语义 (=路径语义)                              │
  │  - 静态条件边裁剪 (true→删else边, false→删then边)           │
  │  - 诊断: UNREACHABLE_STATEMENT, UNINITIALIZED_VARIABLE     │
  │  - 诊断: MISSING_RETURN (函数末尾可达)                     │
  ├──────────────────────────────────────────────────────────┤
  │  阶段12: 调用图递归环检查                                   │
  │  - 有向调用图构建 (函数为顶点, funcall为边)                 │
  │  - 自调用(长度1) + 间接递归 → TC_CE_RECURSION              │
  │  - 多环确定性选择算法 (编译器标准§8.9)                      │
  └──────────────────────────────────────────────────────────┘
           │
  ┌────────┴─────────────────────────────────────────────────┐
  │  阶段13: 代码生成 / 执行                                    │
  │  ├─ TC-VM: 解释执行 (调用帧 / 控制信号 / 槽管理)            │
  │  └─ TC-AOT: C99代码生成 (槽布局 / shim / 差分验证)          │
  └──────────────────────────────────────────────────────────┘
```

### 5. 错误码全集（70+ 个）

```
┌──────────────────────────────────────────────────────────────────┐
│                        静态错误 (TC_CE_*)                         │
├──────────────┬──────────────────┬────────────────────────────────┤
│ 通用与核心    │ 语法层            │ SYNTAX, KEYWORD, OPERAND_COUNT  │
│              │ 名称层            │ UNDEFINED_VARIABLE              │
│              │                  │ DUPLICATE_DEFINITION            │
│              │ 类型层            │ TYPE_MISMATCH                   │
│              │                  │ COMPARISON_TYPE_MISMATCH        │
│              │                  │ CONDITION_TYPE                  │
│              │                  │ MODE_MISMATCH, BITCAST_WIDTH    │
│              │ 字面量层          │ LITERAL_OUT_OF_RANGE            │
│              │                  │ LITERAL_TYPE                    │
│              │ 常量层            │ CONSTANT_ASSIGNMENT             │
│              │                  │ CONSTANT_EXPRESSION (5子条件)   │
│              │                  │ CONSTANT_OVERFLOW               │
│              │                  │ CONSTANT_DIV_ZERO               │
│              │                  │ CONSTANT_CAST_OVERFLOW          │
│              │ 格式层            │ FORMAT_SPECIFIER                │
│              │                  │ FORMAT_TYPE_MISMATCH            │
│              │ 缩进/块           │ INDENT_MIXED/INSUFFICIENT       │
│              │                  │ INDENT_ELSE_END                 │
│              │                  │ MISSING_END, ELSE_POSITION      │
│              │ 控制流            │ BREAK/CONTINUE_OUTSIDE_LOOP     │
│              │                  │ LABEL_NOT_FOUND                 │
│              │                  │ DUPLICATE_LABEL                 │
│              │                  │ JUMP_INTO/INCOMPATIBLE_BLOCK     │
│              │                  │ CROSS_CONTROL_FLOW_JUMP          │
│              │                  │ UNREACHABLE_STATEMENT           │
│              │                  │ UNINITIALIZED_VARIABLE          │
│              │                  │ VAR_MISSING_INIT                │
├──────────────┼──────────────────┼────────────────────────────────┤
│ 函数诊断      │ 签名层            │ DUPLICATE_FUNCTION              │
│  (20个)      │                  │ FUNCTION_NAME_CONFLICT          │
│              │                  │ DUPLICATE_PARAMETER             │
│              │ funcall层         │ UNDEFINED_FUNCTION              │
│              │                  │ FUNCTION_SCOPE_ACCESS           │
│              │                  │ MISSING/DUPLICATE/UNKNOWN_ARG   │
│              │                  │ ARGUMENT_ORDER/TYPE             │
│              │                  │ FUNCALL_POSITION/RESULT_TYPE    │
│              │ return层          │ RETURN_OUTSIDE_FUNCTION         │
│              │                  │ RETURN_FORM, RETURN_TYPE        │
│              │ CFG层             │ MISSING_RETURN                  │
│              │ 调用图层          │ RECURSION                      │
│              │ 形参层            │ PARAMETER_ASSIGNMENT            │
├──────────────┼──────────────────┼────────────────────────────────┤
│ memblock     │ 构造              │ MEMBLOCK_ELEMENT_COUNT_MISMATCH │
│ 诊断(4个)    │ 赋值/传参         │ MEMBLOCK_SIZE_MISMATCH          │
│              │ 越界(静/动)       │ MEMBLOCK_INDEX_OUT_OF_RANGE ×2  │
├──────────────┼──────────────────┼────────────────────────────────┤
│ 结构体        │ 定义              │ DUPLICATE_STRUCT               │
│ 诊断(8个)    │                  │ UNDEFINED_STRUCT                │
│              │ 字段类型位置规则   │ STRUCT_VALUE_SELF_REF           │
│              │ 构造器            │ STRUCT_MISSING_FIELD            │
│              │                  │ STRUCT_UNKNOWN_FIELD            │
│              │                  │ STRUCT_DUPLICATE_FIELD          │
│              │                  │ STRUCT_FIELD_ORDER              │
│              │ 可变性            │ STRUCT_IMMUTABLE_FIELD          │
├──────────────┼──────────────────┼────────────────────────────────┤
│ 模块诊断      │ 结构              │ MODULE_LAYER                   │
│  (10个)      │                  │ MISSING_VISIBILITY              │
│              │                  │ PROGRAM_MODE_MISUSE             │
│              │ 导入              │ IMPORT_NOT_FOUND/NOT_LIB        │
│              │                  │ IMPORT_AMBIGUOUS                │
│              │                  │ DUPLICATE_IMPORT                │
│              │                  │ IMPORT_NAME_CONFLICT            │
│              │ 依赖图            │ CIRCULAR_IMPORT                │
│              │ 可见性            │ PRIVATE_MEMBER_ACCESS           │
├──────────────┼──────────────────┼────────────────────────────────┤
│ 指针/memcopy │                  │ MEMCOPY_UNSAFE_INVALID_RANGE    │
│ 诊断(4个)    │                  │ NULL_POINTER_DEREFERENCE (RE)   │
│              │                  │ NULL_POINTER_ARITHMETIC (RE)    │
│              │                  │ MEMCOPY_UNSAFE_INVALID_RANGE(RE)│
├──────────────┴──────────────────┴────────────────────────────────┤
│                        运行时错误 (TC_RE_*)                        │
│  DIVISION_BY_ZERO  │  INTEGER_OVERFLOW │ NEGATIVE_SHIFT_COUNT     │
│  FLOAT_OVERFLOW    │  FLOAT_UNDERFLOW  │ FLOAT_INVALID            │
│  CAST_OVERFLOW     │  IO               │ MEMBLOCK_INDEX_OUT_RANGE │
├────────────────────┴───────────────────┴─────────────────────────┤
│                      实现错误                                     │
│  TC_ERR_OUT_OF_MEMORY (实现资源失败, 非语言规范错误)               │
└──────────────────────────────────────────────────────────────────┘
```

---

## 第二部分：分模块详细开发计划

### 模块 A：类型系统内核 (types/IR)

> **目标**：建立 TC 0.0.39 完整类型表示、等价判定与宽度计算

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| A-1 | 扩展 `TcTypeTag` 枚举 | `TC_ISIZE, TC_USIZE, TC_PTR, TC_MEMBLOCK, TC_STRUCT, TC_VOID` | 枚举覆盖所有设计类型 |
| A-2 | 设计 `TcType` 联合体 | `ptr_type(pointee), memblock_type(element,count), struct_type(struct_id)` | 所有类型参数可编码 |
| A-3 | 实现 `tc_type_equals()` | 类型等价判定函数，memblock 仅按 T 等价、ptr 按 T 等价、struct 按 id | 单元测试覆盖全部等价组合 |
| A-4 | 实现 `sizeof_bits()` | 宽度计算与 [编译器标准 §3.0.1] 速查表一致（标量固定、isize/usize=平台字长、ptr=平台字长、memblock=头部+数据、struct=Σ字段+padding） | 单元测试验证各宽度 |
| A-5 | 扩展 `TcStmtKind` 枚举 | 新增 14 种: FUNC_DEF, FUNCALL, RETURN, MEMBLOCK_STORE/COPY, PTR_STORE, MEMCOPY_UNSAFE, STRUCT_DEF, STATIC_VAR/LET_DEF, IMPORT 等 | check_rhs_coverage.py 通过 |
| A-6 | 扩展 `TcRhsKind` 枚举 | 新增 18 种: MEMBLOCK_LOAD/CONSTRUCTOR/COUNT, STRUCT_CONSTRUCTOR, FIELD_READ, PTR_LOAD/ADDRESS/ADD/SUB/EQ~GE/SIZE, FUNCALL_EXPR, SELF_MEMBER | check_rhs_coverage.py 通过 |
| A-7 | 扩展 `TcErrorKind` 枚举 | 新增 ~30 个错误码（函数20+memblock4+struct7+模块10+指针4） | tc_error_kind_name() 所有打印名唯一 |
| A-8 | 扩展槽位模型 | 顶层var槽、static_slots[]全程序唯一槽、函数形参/局部槽；memblock/struct 值以堆块指针存于 `slots[]`/`static_slots[]`（实现未采用独立 `struct_storage[]` 字节数组，与 AOT 详设早期草稿不同） | Executor/AOT 消费一致 |
| A-9 | 类型单一事实源收敛 | `TcValue`/`TcSymbol`/`TcResolvedBinding` 存 `const TcType*`；`TcTypeTable` intern；删除符号层 `full_type` 深拷贝与 `memblock_count`/`struct_id` 冗余；`cast`/`bitcast` 完整类型语法 | `check_type_fact_source.py`；`phase5_ptr_cast*`；unit `test_types`/`test_analyzer` |

> **状态（类型内核）**：A-1～A-8 已完成；**A-9（双轨收敛）已落地**（符号/值/绑定统一 `const TcType*`，Parse 期 AST 按值拥有为过渡所有权层）。

### 模块 B：词法分析器 (Lexer)

> **目标**：识别 TC 0.0.39 全部 Token 类型  
> **状态**：**Phase 2 已完成（2026-07-23）**

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| B-1 | 新增保留关键字 | ptr, memblock, struct, func, funcall, return, void, isize, usize, Self, public, private, static, import (14个) | 词法测试覆盖所有关键字 |
| B-2 | 新增模式指令 Token | #program, #lib | Lexer 正确识别 |
| B-3 | 新增特殊字面量 Token | nullptr, inf, -inf, nan (float_special) | Token 类型正确 |
| B-4 | 新增属性 Token | @padding(N) (仅 struct 字段声明中) | Token 类型正确 |
| B-5 | 浮点后缀支持 | f/F 后缀 → float32 源类型标记 | 词法测试 |
| B-6 | 字面量第2阶段检查 | 整数绝对值 ≤ 2^64-1; 有限浮点舍入为零/无穷 → TC_CE_LITERAL_OUT_OF_RANGE | 错误测试用例 |

### 模块 C：语法解析器 (Parser)

> **目标**：解析 TC 0.0.39 全部语法构造为 AST  
> **状态**：**Phase 2 已完成（2026-07-23）** — 函数签名为 `) return_type then`（无 `->`）

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| C-1 | 模块头解析 | #program / #lib 模式指令, import 声明区, 五层结构排序 | 解析测试 |
| C-2 | 函数定义解析 | func name(params) -> return_type then ... end (visibility, void返回) | 解析测试 |
| C-3 | funcall 解析 | funcall(target, name: expr, ...) 命名实参形式 | 解析测试 |
| C-4 | return 解析 | return / return operand 有值/无值 | 解析测试 |
| C-5 | struct 定义解析 | struct Name then let/var field: type @padding(N) ... end | 解析测试 |
| C-6 | 类型语法解析 | memblock<T,N>, ptr<T>, isize, usize, void | RHS 解析测试 |
| C-7 | static 声明解析 | public/private static let/var (仅 #lib) | 解析测试 |
| C-8 | AST 递归释放 | 所有新增 statement/RHS kind 的 free 分发 | Valgrind 无泄漏 |

### 模块 D：模块系统

> **目标**：实现多文件模块加载、导入解析、DAG 验证、可见性检查、static var/let 管理  
> **状态**：**Phase 2 已完成（2026-07-23）** — `src/vm/analyzer/tc_module.c` / `tc_scope.c`；**D-7/D-8/D-9/D-10 随 Phase 4（2026-07-24）补齐**

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| D-1 | 模块数据结构 | TcModule (模式、可见性、成员列表、依赖图), 模块命名空间 | 结构完整 |
| D-2 | 单文件结构检查 (4a) | 五层排序验证 → TC_CE_MODULE_LAYER; #lib 成员可见性 → TC_CE_MISSING_VISIBILITY; #program 误用 → TC_CE_PROGRAM_MODE_MISUSE | 错误测试用例 |
| D-3 | 导入解析 (4b) | 模块路径定位 → NOT_FOUND/NOT_LIB/AMBIGUOUS; 重复导入 → DUPLICATE_IMPORT; 名称冲突 → IMPORT_NAME_CONFLICT | 错误测试用例 |
| D-4 | 依赖图环检查 (4c) | DAG 验证 → TC_CE_CIRCULAR_IMPORT (含自导入) | 错误测试用例 |
| D-5 | 函数签名收集 (4d) | 全部可达 #lib 模块函数签名收集 | 签名列表完整 |
| D-6 | Self 解析 | #lib 内 Self.成员名访问; #program 中 Self → TC_CE_PROGRAM_MODE_MISUSE | 解析测试 |
| D-7 | 可见性检查 | public/private 区分; private 外部访问 → TC_CE_PRIVATE_MEMBER_ACCESS | 错误测试用例 |
| D-8 | 本库成员索引 | 收集 func/static let/static var 声明名; 函数内裸名 → TC_CE_FUNCTION_SCOPE_ACCESS | 错误测试用例 |
| D-9 | static let 编译期求值 | 按依赖拓扑序求值, 所有引用处内联 | 常量测试 |
| D-10 | static var 初始化 | 初始化器约束验证 (仅字面量/更早Self成员/导入公开static), 按依赖拓扑序 | 集成测试 |

### 模块 E：类型系统验证

> **目标**：实现 memblock/ptr/struct 三种新类型的完整静态验证  
> **状态**：**Phase 3 已完成（2026-07-24）** — `tc_type_check` / `tc_memblock_check` / `tc_ptr_check` / `tc_struct_check`

#### E.1 memblock 类型验证

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| E1-1 | 类型等价实现 | memblock<T,N> 仅由 T 决定; N 不参与等价; tc_type_equals() 实现 | 类型等价测试 |
| E1-2 | N 记录与比较 | 符号表记录 N 数学值; 赋值/传参比较 N → TC_CE_MEMBLOCK_SIZE_MISMATCH | 错误测试 |
| E1-3 | .count 编译期常量 | mb.count 返回声明 N 值; 可作为 const_operand | 常量测试 |
| E1-4 | 构造器验证 | count: 合法性(usize常量≥1); 逐值数量=n → TC_CE_MEMBLOCK_ELEMENT_COUNT_MISMATCH; 填充形式; 类型一致 | 构造器测试 |
| E1-5 | 边界检查 | load/store: 0≤i<N; copy: 半开区间; 编译期可确定→TC_CE_, 运行时→TC_RE_ | 越界测试 |

#### E.2 指针类型验证

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| E2-1 | 类型携带 T | ptr<int32> ≠ ptr<float64>; 赋值/传参须严格同型 | 类型测试 |
| E2-2 | nullptr 定型 | 由期望 ptr<T> 唯一确定所指类型; 非指针上下文 → TC_CE_TYPE_MISMATCH | 类型测试 |
| E2-3 | 禁止通用标量运算 | ptr<T> 进入 add/sub 等 → TC_CE_TYPE_MISMATCH | 类型测试 |
| E2-4 | ptr_address 可变性 | 仅 var/static var/形参; let → TC_CE_CONSTANT_ASSIGNMENT | 错误测试 |
| E2-5 | ptr_store 可变性 | 所指外层只读/形参 → TC_CE_CONSTANT/PARAMETER_ASSIGNMENT | 错误测试 |
| E2-6 | 空指针分类 | 解引用类 → TC_RE_NULL_POINTER_DEREFERENCE; 算术类 → TC_RE_NULL_POINTER_ARITHMETIC | 运行时错误测试 |
| E2-7 | I/O 排除 | write/read 以 ptr<T> 为显式类型 → TC_CE_SYNTAX | 语法测试 |
| E2-8 | 指针操作验证 | ptr_load/store/address/add/sub/eq~ge/size 全部类型参数一致性与操作数约束 | 全覆盖测试 |

#### E.3 结构体类型验证

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| E3-1 | 定义唯一性 | 同名 → TC_CE_DUPLICATE_STRUCT; 未定义引用 → TC_CE_UNDEFINED_STRUCT | 错误测试 |
| E3-2 | 字段类型检查 | 标量/memblock/ptr/已定义struct；值位置禁止自引用与前向引用；指针所指位置允许 `ptr<本结构体>`（§3.9.1）；禁止 void | 类型测试 |
| E3-3 | 字段声明约束 | 每行1字段; 至少1字段; 字段名唯一→DUPLICATE_FIELD; @padding(N)可选且须为无后缀非负十进制字面量（负号/后缀/进制前缀→CONSTANT_EXPRESSION）；end 与 struct 对齐（INDENT_ELSE_END）且不得有尾随 token | 解析+类型测试（`struct_dup_field` / `struct_padding_*` / `struct_end_*`） |
| E3-4 | 字段可变性 | let 字段(构造后不可修改) / var 字段 | 可变性测试 |
| E3-5 | 值构造器验证 | 全字段必填→MISSING_FIELD; 未知→UNKNOWN_FIELD; 重复→DUPLICATE_FIELD; 顺序→FIELD_ORDER; 类型一致 | 全错误码测试 |
| E3-6 | 双层可变性 | 外层绑定×字段let/var 矩阵(6种组合) | 所有组合测试 |
| E3-7 | 嵌套字段访问 | a.b.c 中间结果须为struct类型 | 解析+类型测试 |

#### E.4 字面量统一检查

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| E4-1 | 检查顺序实现 | 1.类别/符号性 → 2.源类型与期望类型 → 3.值范围 | 各优先级测试 |
| E4-2 | 无后缀整数 | 有符号上下文范围检查; 无符号要求非负 | 上下文字面量测试 |
| E4-3 | u/U 后缀整数 | 仅无符号上下文; 带-非法; 误用→TC_CE_LITERAL_TYPE | 字面量测试 |
| E4-4 | 普通浮点 | 无后缀→float64, f/F→float32; 源类型与期望类型一致性 | 浮点上下文测试 |
| E4-5 | float_special | inf/-inf/nan 以期望类型定型; 非浮点上下文→TC_CE_LITERAL_TYPE | 特殊值测试 |
| E4-6 | bool 字面量 | 仅 bool 上下文; 误用→TC_CE_LITERAL_TYPE | 布尔测试 |

### 模块 F：函数与调用模型

> **目标**：实现函数签名收集、funcall 检查、return 检查、调用图环检查  
> **状态**：**Phase 4 已完成（2026-07-24）** — `tc_func_check` / `tc_callgraph`；阶段 5/7/8/12 接入 `tc_analyze_ex`

#### F.1 函数签名检查 (阶段5)

| ID | 任务 | 产出 | 验证标准 |
| -- | ---- | ---- | -------- |
| F1-1 | 函数重名检查 | 同模块同名 → TC_CE_DUPLICATE_FUNCTION | 错误测试 |
| F1-2 | 函数名冲突检查 | 任何值绑定与全局函数同名 → TC_CE_FUNCTION_NAME_CONFLICT | 错误测试 |
| F1-3 | 参数重名检查 | 同签名参数重名 → TC_CE_DUPLICATE_PARAMETER | 错误测试 |
| F1-4 | 参数名保护 | 参数名与全局函数冲突; 函数内var/let与形参同名→DUPLICATE_DEFINITION | 错误测试 |

#### F.2 funcall 检查 (阶段7)

| ID | 任务 | 优先级 | 错误码 |
| -- | ---- | ------ | ------ |
| F2-1 | 目标存在检查 | 1 | TC_CE_UNDEFINED_FUNCTION |
| F2-2 | 裸名命中本库 func | 1(同级) | TC_CE_FUNCTION_SCOPE_ACCESS |
| F2-3 | 私有函数外部调用 | 1(同级) | TC_CE_PRIVATE_MEMBER_ACCESS |
| F2-4 | 调用位置检查 | 2 | TC_CE_FUNCALL_POSITION |
| F2-5 | 实参重复检查 | 3 | TC_CE_DUPLICATE_ARGUMENT |
| F2-6 | 未知实参检查 | 4 | TC_CE_UNKNOWN_ARGUMENT |
| F2-7 | 实参缺失检查 | 5 | TC_CE_MISSING_ARGUMENT |
| F2-8 | 实参顺序检查 | 6 | TC_CE_ARGUMENT_ORDER |
| F2-9 | 实参类型检查 | 7 | TC_CE_ARGUMENT_TYPE |
| F2-10 | 接收类型检查 | 8 | TC_CE_FUNCALL_RESULT_TYPE |

#### F.3 return 检查 (阶段8)

| ID | 任务 | 优先级 | 错误码 |
| -- | ---- | ------ | ------ |
| F3-1 | 函数体内位置 | 1 | TC_CE_RETURN_OUTSIDE_FUNCTION |
| F3-2 | 有值/无值形式 | 2 | TC_CE_RETURN_FORM |
| F3-3 | 操作数名称合法 | 3 | 名称类错误 |
| F3-4 | 返回类型匹配 | 4 | TC_CE_RETURN_TYPE |
| F3-5 | 字面量合法 | 5 | 字面量专用错误 |

#### F.4 形参类型检查

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| F4-1 | 标量形参 | operand/字面量, 复制值 |
| F4-2 | memblock 形参 | 标识符/构造器; 深拷贝(含长度头部); 比较 N → TC_CE_MEMBLOCK_SIZE_MISMATCH |
| F4-3 | ptr 形参 | 同型标识符/nullptr; 复制指针值(共享所指对象) |
| F4-4 | struct 形参 | 同型标识符/构造器; 整块深拷贝 |
| F4-5 | 形参只读约束 | 形参赋值/read目标 → TC_CE_PARAMETER_ASSIGNMENT |

#### F.5 调用图环检查 (阶段12)

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| F5-1 | 调用图构建 | 全部函数体内 funcall 建立有向图 |
| F5-2 | 环检测 | 自调用(长度1) + 间接递归 → TC_CE_RECURSION |
| F5-3 | 多环确定性选择 | 编译器标准 §8.9 算法 (源位置最早函数+最早调用边+最少边路径) |

### 模块 G：控制流分析 (CFG + 确定初始化)

> **目标**：实现多域 CFG 构建、可达性分析、确定初始化最大固定点求解  
> **状态**：**Phase 3 已完成（2026-07-24）** — `TcCfgSet` 顶层+函数域；G1 goto/label 仅函数内；MISSING_RETURN / 结构不可达

#### G.1 控制流上下文检查 (阶段6a)

| ID | 任务 | 错误码 |
| -- | ---- | ------ |
| G1-1 | goto 无 func 祖先 | TC_CE_GOTO_OUTSIDE_FUNCTION |
| G1-2 | label 无 func 祖先 | TC_CE_LABEL_OUTSIDE_FUNCTION |
| G1-3 | goto 有 while 祖先 | TC_CE_GOTO_INSIDE_LOOP |
| G1-4 | label 有 while 祖先 | TC_CE_LABEL_INSIDE_LOOP |
| G1-5 | break 不在 while 内 | TC_CE_BREAK_OUTSIDE_LOOP |
| G1-6 | continue 不在 while 内 | TC_CE_CONTINUE_OUTSIDE_LOOP |

#### G.2 作用域预建与 goto/label 解析 (阶段6b-6c)

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| G2-1 | 多级作用域建立 | 全局函数签名表 + 模块顶层命名空间 + 顶层值作用域 + 函数作用域 + 块级作用域 + 标签作用域 |
| G2-2 | 本库成员索引 | 收集 func/static let/static var 声明名 (仅用于错误分类) |
| G2-3 | goto 标签查找 | 沿祖先链由内向外, 首个同名标签胜出 |
| G2-4 | 跨控制流域判定 | 跳入子块→TC_CE_JUMP_INTO_BLOCK, 不可比块→TC_CE_JUMP_INCOMPATIBLE_BLOCK, 跨函数→TC_CE_CROSS_CONTROL_FLOW_JUMP |
| G2-5 | 同块标签检查 | 同块禁止同名标签→TC_CE_DUPLICATE_LABEL |

#### G.3 静态布尔 + 逻辑读边 (阶段10)

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| G3-1 | 三态判定 | static true/false/unknown；形态归类见 [语言标准 §5.2.2]；合法常量求值错误不降级为 unknown |
| G3-2 | 边裁剪 | true→删 else 边; false→删 then 边; unknown→保留两边 |
| G3-3 | 逻辑短路 | and(bool,false,x) 右操作数需合法但不执行; xor 不短路 |

#### G.4 多域 CFG 与确定初始化 (阶段11)

| ID | 任务 | 产出 | 验证 |
| -- | ---- | ---- | ---- |
| G4-1 | 顶层 CFG 构建 | IN=∅, 无 return/goto/label, funcall 为原子节点 | 顶层 CFG 测试 |
| G4-2 | 函数 CFG 构建 | IN=全部形参, 含 return/goto/label/break/continue | 函数 CFG 测试 |
| G4-3 | 可达性计算 | 从入口 BFS/DFS, 不可达语句→TC_CE_UNREACHABLE_STATEMENT | 可达性测试 |
| G4-4 | 传递函数实现 | OUT[var]=IN∪{x}, OUT[let]=IN, OUT[其他]=IN | 数据流测试 |
| G4-5 | 边规范化 | 离开块→清除退出绑定; 进入块→重置局部绑定 | 生命周期测试 |
| G4-6 | 最大固定点求解 | 等价于路径语义; 工作队列/遍历顺序无关 | 初始化测试 |
| G4-7 | 确定初始化诊断 | 读取未初始化变量→TC_CE_UNINITIALIZED_VARIABLE | 错误测试 |
| G4-8 | 函数末尾检查 | 末尾可达且无 return→TC_CE_MISSING_RETURN | 错误测试 |
| G4-9 | 多域独立性 | 顶层与各函数 CFG 不拼接/不共享状态 | 隔离测试 |

### 模块 H：常量求值

> **目标**：实现 let/static let 编译期求值, 含浮点精度规则  
> **状态**：**Phase 4 已完成（2026-07-24）** — `#program let`（既有）+ static let 拓扑求值（H-5）+ static var 初始化器约束（H-6）；D-7/D-8 可见性分类已接入

| ID | 任务 | 产出 | 验证 |
| -- | ---- | ---- | ---- |
| H-1 | const_rhs 形态验证 | 拒绝嵌套调用→CONSTANT_EXPRESSION(nested call); 拒绝 var/形参→(references runtime variable); 拒绝非法形态→(invalid form) | 形态测试 |
| H-2 | 编译期求值器 | 按源序求值全部let; 算术/位/移位/比较/逻辑/转换 | 求值测试 |
| H-3 | 浮点精度规则 | binary32/64 独立精度; roundTiesToEven; 禁止 FMA; 禁止 FTZ/DAZ; 保留非规格化数 | 浮点精度测试 |
| H-4 | 错误映射 | 溢出→CONSTANT_OVERFLOW; 除零→CONSTANT_DIV_ZERO; 转换溢出→CONSTANT_CAST_OVERFLOW; 无效操作/负移位→CONSTANT_EXPRESSION | 错误测试 |
| H-5 | static let 求值 | 按依赖拓扑序求值; 所有引用处内联 | 集成测试 |
| H-6 | static var 验证 | 初始化器编译期约束验证 (不执行求值); 操作数来源检查 | 初始化器测试 |
| H-7 | 共享语义核心 | let 求值器与 Executor 共用 tc_sem_int/fp/cast/bitwise | 一致性测试 |

### 模块 I：VM 执行器 (Executor)

> **目标**：实现完整的 TC-VM 解释执行引擎  
> **状态**：**Phase 5 已完成（2026-07-24）** — 调用帧（扁平全局槽 + CE 禁递归）、funcall/return、static var 初始化、ptr/memblock 运行时；**struct 运行时已补齐（2026-07-24）**

#### I.1 核心执行框架

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| I1-1 | 控制信号模型 | TC_EXEC_NORMAL/BREAK/CONTINUE/GOTO/RETURN/ERROR |
| I1-2 | 调用帧实现 | TcCallFrame (func_id, return_stmt_index, param_slots, local_slots, block_path) |
| I1-3 | 顶层执行 | 按依赖拓扑序初始化 static var; 按源序遍历顶层语句 |
| I1-4 | 函数执行 | 实参求值→帧建立→执行函数体→return 销毁帧→返回值传播 |
| I1-5 | 运行时错误 | 首个错误立即终止程序 (fail-fast); 提交规则 (不部分写入) |

#### I.2 控制流执行

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| I2-1 | while 循环 | 条件求值→体执行→信号处理(break/continue/goto/return) |
| I2-2 | goto/label | 零成本标签; 向后跳覆盖固定槽; 向外跳清除绑定 |
| I2-3 | if/else | 条件分支; 无 else 时条件假则跳过 |
| I2-4 | 静态初始化 | 全部 static var 按依赖拓扑序在顶层执行前初始化 |

#### I.3 数值与运算语义

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| I3-1 | 整数 strict | 有符号 add/sub/mul/neg/shl 检查范围; div(INT_MIN/-1)/abs(INT_MIN)→溢出 |
| I3-2 | 整数 wrap | add/sub/mul/neg/shl 按目标位宽回绕; div/mod/abs/shr 不接受 wrap |
| I3-3 | 无符号 | 固定模 2^n 回绕; 不接受模式关键字 |
| I3-4 | isize/usize | 同字长 int*/uint* 语义 |
| I3-5 | 浮点 strict | 检测除零/上溢/下溢/无效操作; 异常优先级 Inv→DivBy→Over→Under |
| I3-6 | 浮点 ieee | IEEE 754 结果 (inf/nan 不报错) |
| I3-7 | 浮点精度 | 每步按声明精度舍入 (roundTiesToEven); 禁止 FMA |
| I3-8 | cast | 严格范围检查→TC_RE_CAST_OVERFLOW; truncate 仅整数缩窄; bool↔整数/浮点规范字节 |
| I3-9 | bitcast | 源目标等宽 (Analyzer 保证); memcpy 位模式复制; bool 不参与 |
| I3-10 | shl/shr 移位 | 先检查负移位→TC_RE_NEGATIVE_SHIFT_COUNT; 再检查 shl 溢出 |

#### I.4 指针运行时操作

| ID | 指令 | 运行时语义 |
| -- | ---- | ---------- |
| I4-1 | ptr_load(T,ptr) | nullptr→TC_RE_NULL_POINTER_DEREFERENCE; 否则按值复制所指对象 |
| I4-2 | ptr_store(T,ptr,val) | nullptr→TC_RE_NULL_POINTER_DEREFERENCE; 否则覆盖写入(可变性 Analyzer 保证) |
| I4-3 | ptr_address(T,ident) | 返回绑定槽的抽象地址 (仅 var/static var/形参, Analyzer 保证) |
| I4-4 | ptr_add(T,ptr,offset) | nullptr→TC_RE_NULL_POINTER_ARITHMETIC; ptr+offset*sizeof_bits(T) (不检查越界) |
| I4-5 | ptr_sub(T,ptr,offset) | nullptr→TC_RE_NULL_POINTER_ARITHMETIC; ptr-offset*sizeof_bits(T) |
| I4-6 | ptr_eq/ne(T,p1,p2) | 允许 nullptr 比较; 两个 nullptr→true |
| I4-7 | ptr_lt/le/gt/ge(T,p1,p2) | nullptr→TC_RE_NULL_POINTER_DEREFERENCE; 比较抽象地址序 |
| I4-8 | ptr_size(T,ptr) | 编译期内联 sizeof_bits(T); nullptr 合法 |

#### I.5 memblock 运行时操作

| ID | 指令 | 运行时语义 |
| -- | ---- | ---------- |
| I5-1 | memblock_load(T,mb,idx) | 边界检查 0≤idx<N→越界; 读取元素T[idx] |
| I5-2 | memblock_store(T,mb,idx,val) | 边界检查→写入; 越界不修改; bool 规范化 |
| I5-3 | memblock_copy(T,dst,di,src,si,len) | 区间检查→先拷入临时缓冲再写目标(memmove); 越界不修改 |
| I5-4 | memcopy_unsafe(T,dst,di,src,si,len) | nullptr→解引用; len<0→无效; 不检查越界; memmove 语义 |

#### I.6 结构体运行时操作

| ID | 任务 | 运行时语义 |
| -- | ---- | ---------- |
| I6-1 | 构造器 | 按布局分配堆块；写入各字段（标量/嵌套 struct/memblock/ptr）；槽存堆指针 |
| I6-2 | 字段读取 | 按偏移读出；嵌套 struct 字段按值深拷贝抽出 |
| I6-3 | 字段写入 | 按偏移写入（可变性由 Analyzer 保证） |
| I6-4 | 整块赋值/传参 | 深拷贝堆块；形参只读（赋值由 CE 拒绝） |
| I6-5 | 与 ptr/memblock 字段 | 字段可为 ptr/memblock；嵌套复合类型按各自语义复制 |

### 模块 J：AOT 代码生成 (C99)

> **目标**：将 TcTypedProgram 确定性转译为可移植 C99 代码  
> **状态**：**Phase 5 已完成（2026-07-24）** — 函数 codegen、ptr/memblock shim、static var 初始化、与 VM 差分；**struct 构造器/字段读写/深拷贝已补齐（2026-07-24）**

#### J.1 C99 目标骨架

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| J1-1 | 槽位布局 | slots[] + static_slots[]；memblock/struct 堆指针存于槽内（非独立 struct_storage[]） |
| J1-2 | memblock/struct 堆管理 | tc_aot_memblock_* / tc_aot_struct_*；程序结束统一释放 |
| J1-3 | static var 初始化 | tc_init_static_vars() 按拓扑序生成初始化代码 |
| J1-4 | main() 入口 | 槽初始化 → static var 初始化 → 顶层语句 → 清理 |

#### J.2 语句代码生成

| ID | TC 语句 | 目标 C |
| -- | ------- | ------ |
| J2-1 | var x:T = rhs | RHS求值→slots[X]; 错误guard |
| J2-2 | static var | RHS→static_slots[id] (在 tc_init_static_vars 中) |
| J2-3 | let/static let | 不生成运行时语句; 内联十六进制位模式 |
| J2-4 | x = rhs | RHS→已有slot; 目标须已初始化(Analyzer保证) |
| J2-5 | a.b = rhs | tc_aot_struct_store_bits / memcpy_field（按字段偏移写入堆块） |
| J2-6 | write/writeln | tc_aot_write + abort guard |
| J2-7 | read | tc_aot_read + abort guard |
| J2-8 | if | 条件RHS + 原生 C if/else |
| J2-9 | while | for(;;)+每次迭代显式条件求值 |
| J2-10 | break/continue | 原生 C break/continue |
| J2-11 | goto/label | 唯一 C 标签 (函数内, while 外) |
| J2-12 | funcall(void) | tc_func_<id>(&diag) + abort guard |
| J2-13 | funcall(非void) | + slots[X] = tc_ret_<id> |
| J2-14 | return | return 或 *retval=value; return |
| J2-15 | memblock_store/copy | 对应 shim + abort guard |
| J2-16 | ptr_store | tc_aot_ptr_store + abort guard |
| J2-17 | memcopy_unsafe | tc_aot_memcopy_unsafe + abort guard |

#### J.3 函数代码生成

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| J3-1 | 函数声明 | static void tc_func_<id>(TcDiagnostic *diag) |
| J3-2 | 形参/局部槽 | uint64_t params[PC]/locals[LC]/retval |
| J3-3 | 调用约定 | 调用侧: 填充 params→tc_func_<id>(&diag)→检查 diag→读取 retval |
| J3-4 | memblock 传参 | 调用前深拷贝 |
| J3-5 | struct 传参 | 整块字节复制 |

#### J.4 运行时 shim

| ID | shim | 功能 |
| -- | ---- | ---- |
| J4-1 | tc_aot_ptr_load | 指针解引用读取 |
| J4-2 | tc_aot_ptr_store | 指针解引用写入 |
| J4-3 | tc_aot_ptr_address | 取地址 |
| J4-4 | tc_aot_ptr_add/sub | 指针算术 |
| J4-5 | tc_aot_ptr_eq/ne | 等值比较 |
| J4-6 | tc_aot_ptr_lt/le/gt/ge | 序关系比较 |
| J4-7 | tc_aot_memblock_load | memblock 读取+边界 |
| J4-8 | tc_aot_memblock_store | memblock 写入+边界 |
| J4-9 | tc_aot_memblock_copy | memblock 区间拷贝 |
| J4-10 | tc_aot_memcopy_unsafe | 原始内存块拷贝 |
| J4-11 | tc_aot_struct_alloc/clone | struct 堆分配与深拷贝 |
| J4-12 | tc_aot_struct_load/store_bits | 标量字段读写 |
| J4-13 | tc_aot_struct_memcpy_field | 复合字段按字节写入 |
| J4-14 | tc_aot_struct_extract | 嵌套 struct 字段抽出（深拷贝） |
| J4-15 | tc_aot_struct_heap_free_all | 程序结束释放 |

#### J.5 数值一致性

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| J5-1 | bitcast | memcpy 位模式复制 (禁止 union punning) |
| J5-2 | 浮点 | 每步精度舍入; 禁止 FMA; 保留非规格化数 |
| J5-3 | 算术/转换 | 委托共享 tc_sem_int/fp/cast/bitwise |
| J5-4 | let 常量 | 发射十六进制位模式 (不用十进制让 host 重新舍入) |
| J5-5 | C99 编译 | -std=c99 -Wall -Wextra -Werror -pedantic |

### 模块 K：CLI、API、测试与清理

> **目标**：完成 CLI 更新、libtc API 更新、全量测试覆盖、REPL 移除、版本号更新  
> **状态**：**Phase 6 已完成（2026-07-24）** — CLI `-I`/无 REPL、libtc `name`/`tc_run_program`、版本 v0.0.39；struct 运行时已补齐

#### K.1 CLI 更新

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| K1-1 | tc-vm CLI | -c/--check, -I/--include <path>, -h, -V; 移除 REPL 模式 |
| K1-2 | tc-aot CLI | -o/--output, -c/--check, -r/--run, -h, -V |
| K1-3 | 模块搜索路径 | TcCompileOptions 会话路径（-I）; 入口文件目录→会话路径→默认路径 |
| K1-4 | 多文件入口 | tc-vm main.tc 自动加载所有 import 的 #lib 模块 |

#### K.2 libtc API 更新

| ID | API | 变更 |
| -- | --- | ---- |
| K2-1 | tc_compile_source | source + name（无路径源，无搜索路径参数） |
| K2-2 | tc_compile_file_opts | 自动加载所有可达模块; 会话搜索路径 |
| K2-3 | TcCompileOptions | 会话级 -I 路径（无进程级全局） |
| K2-4 | tc_run_program | 含 static var 拓扑初始化 |

#### K.3 测试覆盖 (全量)

> **状态（2026-07-24）**：VM/AOT/unit 门禁已落地（见 `test-map.md` 规模）；下表为规划估算，**非**尚未实现的任务清单。struct 双层可变性矩阵与合规证据随本收尾补齐。

| 测试层 | 测试内容 | 预计数量 |
| ------ | -------- | -------- |
| **Lexer** | 14个新关键字, nullptr/inf/-inf/nan/float_special, @padding | ~30 |
| **Parser** | 模块头, import, func/funcall/return, struct, memblock<T,N>, ptr<T>, static | ~50 |
| **模块系统** | 单文件结构, 导入解析(6种错误), DAG环, 可见性, Self | ~40 |
| **函数系统** | 签名检查(4种), funcall(10级优先级), return(5级), 调用图环 | ~50 |
| **类型系统** | memblock N, ptr 同型/nullptr, struct 全字段/双层可变性, 字面量 | ~60 |
| **控制流** | goto/label 上下文(6种), 多域CFG, 确定初始化, 三态判定 | ~40 |
| **常量求值** | let形态验证, 浮点精度, static let拓扑, 错误映射 | ~30 |
| **数值语义** | 整数strict/wrap, 浮点strict/ieee, cast/truncate/bitcast | ~40 |
| **指针操作** | 8种ptr_*指令 (正常+错误) | ~40 |
| **memblock** | load/store/copy/memcopy_unsafe (正常+越界) | ~30 |
| **struct** | 构造器, 字段读写, 双层可变性, 嵌套 | ~30 |
| **I/O** | 13种格式符, read验证, 格式控制 | ~30 |
| **Executor** | 调用帧, return传播, static var拓扑 | ~30 |
| **AOT 差分** | VM vs AOT 输出/退出码/位模式 | 全量基线 |
| **libtc** | API契约, 所有权, 诊断域, OOM | ~20 |
| **错误码** | 全部70+错误码触发测试 | ~70 |

#### K.4 同步与清理

| ID | 任务 | 产出 |
| -- | ---- | ---- |
| K4-1 | check_rhs_coverage.py | 所有新增 TcRhsKind 覆盖检查 |
| K4-2 | check_source_naming.py | 所有新增模块命名检查 |
| K4-3 | test-map.md | 0.0.39 测试映射更新 |
| K4-4 | REPL 移除 | 删除 tc_repl.c/h; 移除 --repl 选项 |
| K4-5 | 版本号 | src/vm/runtime/tc_version.h → v0.0.39; src/aot/main.c → TC_AOT_VERSION |
| K4-6 | 文档同步 | 9份文档版本标记一致；合规审查证据回填（2026-07-24） |

---

## 第三部分：项目执行路线图

### 阶段划分与依赖关系

```
Phase 1 (基础)     Phase 2 (模块)     Phase 3 (类型)     Phase 4 (函数)
┌──────────┐      ┌──────────┐       ┌──────────┐       ┌──────────┐
│ A: 类型内核│ ───→│ B: Lexer  │───→  │E:类型验证│───→   │ F: 函数   │
│  2-3天    │      │ C: Parser │       │ 6-8天    │       │  5-6天    │
└──────────┘      │  6-8天    │       └──────────┘       └──────────┘
                   └──────────┘             │                   │
                         │                  │                   │
                         ▼                  ▼                   ▼
                   ┌──────────┐       ┌──────────┐       ┌──────────┐
                   │ D: 模块  │       │ G: CFG   │       │ I: Exec  │
                   │  6-8天    │       │  5-6天    │       │  6-8天    │
                   └──────────┘       └──────────┘       └──────────┘
                         │                  │                   │
                         └──────┬───────────┘                   │
                                ▼                               │
                          ┌──────────┐                          │
                          │ H: 常量   │                          │
                          │  3-4天    │                          │
                          └──────────┘                          │
                                │                               │
                                ▼                               ▼
                          ┌──────────┐                    ┌──────────┐
                          │ J: AOT   │                    │ K: 测试  │
                          │  7-9天    │                    │  5-7天    │
                          └──────────┘                    └──────────┘
```

### 时间线估算

| 阶段 | 模块 | 预计天数 | 关键依赖 |
| ---- | ---- | -------- | -------- |
| P1 | A: 类型内核 | 2-3 | 无 | **已完成（2026-07-23）** |
| P2 | B: Lexer + C: Parser + D: 模块 | 12-16 | A | **已完成（2026-07-23）** |
| P3 | E: 类型验证 + G: CFG | 11-14 | B,C,D | **已完成（2026-07-24）** |
| P4 | H: 常量 + F: 函数 | 8-10 | C,E | **已完成（2026-07-24）** |
| P5 | I: Executor + J: AOT | 13-17 | F,G,H | **已完成（2026-07-24）** — I+J；struct 运行时已于同日补齐 |
| P6 | K: CLI/API/测试/清理 | 5-7 | I,J | **已完成（2026-07-24）** |
| **合计** | | **51-67 天** | |

### 关键风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
| ---- | ---- | ---- | ---- |
| 多域 CFG 最大固定点语义 | 中 | 确定初始化正确性 | 先写路径语义测试,实现验证 |
| 浮点 VM/AOT 一致性 | 中 | 差分验证失败 | 共享 tc_sem_fp, 不使用宿主 fenv |
| struct 双层可变性矩阵 | 低 | 字段赋值逻辑复杂 | 穷举6种组合测试 |
| 模块 DAG 拓扑序 | 低 | static var 初始化顺序 | 先构建依赖图再拓扑排序 |
| nullptr 分类正确性 | 低 | 解引用/算术混码 | 严格按操作类型分码 |
| 13 阶段诊断确定性 | 中 | 首个诊断选择不稳定 | 严格按编译器标准 §1.3 三级优先级 |

---

## 第四部分：合规审查对照

本计划覆盖合规审查报告全部 ~182 项检查点：

| 审查域 | 检查点 ID | 数量 | 对应本计划模块 |
| ------ | --------- | ---- | -------------- |
| 词法/语法/IR | L-01~L-15 | 15 | B, C |
| 类型系统 | T-01~T-20 | 20 | A, E |
| 模块系统 | M-01~M-15 | 15 | D |
| 函数与调用 | F-01~F-18 | 18 | F |
| 控制流/初始化 | C-01~C-15 | 15 | G |
| 表达式与运算 | E-01~E-25 | 25 | E, I |
| 常量求值 | K-01~K-12 | 12 | H |
| I/O | I-01~I-10 | 10 | E, I |
| VM 执行器 | V-01~V-12 | 12 | I |
| AOT 代码生成 | A-01~A-12 | 12 | J |
| libtc/诊断 | D-01~D-18 | 18 | K |
| 发布/文档 | R-01~R-10 | 10 | K |
| **合计** | | **~182** | |

---

*本计划基于 TC 0.0.39 全套设计文档（语言标准、编译器标准、VM 设计、AOT 设计、libtc 设计、libtc API、VM 命令参考、合规审查报告）。所有规范要求以原设计文档为准。*

---

## 第五部分：合规审查修复计划

> **审查日期**：2026-07-25  
> **落地提交**：`40a518b`（R-1 `TC_CE_IMPORT_NAME_CONFLICT` + R-2 关键字提权）  
> **审查范围**：对照 `TC语言标准设计说明书-0.0.39.md` 和 `TC编译器标准设计说明书-0.0.39.md` 两份文档的逐项检查  
> **审查结论**：整体实现完整度约 99%，全部测试通过（VM + AOT + Unit）；当时发现的 1 个中等合规偏差与 1 个可选优化项**均已修复完成**

### 审查结果总览

| 审查维度 | 检查项数 | 通过 | 偏差 |
|---------|---------|------|------|
| 关键字与词法 (75+) | 88 | 88 | 0（R-2 已完成） |
| 错误码覆盖 | 91 | 91 | 0 |
| RHS 分发覆盖 | 34 | 34 | 0（8 个分发点全覆盖，`check_rhs_coverage.py`） |
| 13 阶段管线 | 13 | 13 | 0 |
| 子阶段实现 | 23 | 23 | 0（R-1 已完成） |
| 类型系统 | 18 | 18 | 0 |
| 模块系统 | 14 | 14 | 0（R-1 已完成） |
| 控制流与函数 | 22 | 22 | 0 |
| I/O | 10 | 10 | 0 |
| 诊断优先级 | 9 | 9 | 0 |
| **合计** | **~320** | **~320** | **0** |

---

### 修复任务清单

#### R-1 [中] ✅ 已完成 — 阶段 4b：`TC_CE_IMPORT_NAME_CONFLICT` 独立检查

| 项目 | 说明 |
|------|------|
| **规范依据** | 编译器标准 §1.2 子阶段 4b：*"导入名与本模块任何顶层名称冲突 → `TC_CE_IMPORT_NAME_CONFLICT`（完整范围见 §4.6）"* |
| **落地状态** | **已完成**（`40a518b`）：`tc_module_check_import_name_conflict()`；用例 `import_name_conflict_{program,lib}.tc` |
| **预期行为** | 在 4b 导入解析阶段，对每条 `import` 语句，检查导入名是否与本模块已收集的顶层名称冲突。对于 `#program`，检查 `var`/`let` 名；对于 `#lib`，检查 `func`/`struct`/`static let`/`static var` 名。若冲突则报告 `TC_CE_IMPORT_NAME_CONFLICT` |
| **修复位置** | `src/vm/analyzer/tc_module.c` → `tc_module_resolve_imports()` |

<details>
<summary>历史修复步骤（已落地，仅作归档）</summary>

**步骤 1**：在 `tc_module_resolve_imports()` 中，调用 4b 各子检查后，新增冲突检查调用：

```c
/* 4b: 导入名与本模块名称冲突检查 */
if (!tc_is_error(diag)) {
    if (tc_module_check_import_name_conflicts(prog, ctx, diag) != 0) {
        return -1;
    }
}
```

**步骤 2**：实现 `tc_module_check_import_name_conflicts()` 函数：

```c
/**
 * 4b 子阶段：检查 import 名是否与本模块顶层名称冲突
 * 
 * #program 模式：检查顶层 var/let 名
 * #lib 模式：检查 func/struct/static let/static var 名
 */
static int tc_module_check_import_name_conflicts(
        const TcProgram *prog, TcAnalyzerContext *ctx, TcDiagnostic *diag)
{
    /* 遍历所有 import 语句 */
    for (int i = 0; i < ctx->import_count; i++) {
        const char *import_name = ctx->imports[i].module_name;
        
        /* 检查是否与本模块已有顶层名称冲突 */
        if (tc_module_is_top_level_name(prog, import_name)) {
            /* 定位到该 import 的模块名 Token，关联位置为冲突的本模块成员名 */
            tc_diagnostic_set(diag, TC_CE_IMPORT_NAME_CONFLICT,
                ctx->imports[i].name_line, ctx->imports[i].name_col,
                "import name '%s' conflicts with a top-level declaration "
                "in this module", import_name);
            return -1;
        }
    }
    return 0;
}
```

**步骤 3**：实现辅助函数 `tc_module_is_top_level_name()`，根据模块模式返回不同集合：

| 模式 | 检查范围 |
|------|---------|
| `#program` | 顶层 `var`/`let` 名（含 `var_funcall_def` 引入的名称） |
| `#lib` | `func` 名 / `struct` 名 / `static let` 名 / `static var` 名 |

**步骤 4**：确保 4b 子阶段的优先级顺序为：
```
NOT_FOUND → NOT_LIB → AMBIGUOUS → DUPLICATE_IMPORT → IMPORT_NAME_CONFLICT
```
规范 §4.1 要求同一 `import` 上按此顺序选择首个错误。

**步骤 5**：添加测试用例：

```
tests/errors/module/import_name_conflict_program.tc
tests/errors/module/import_name_conflict_lib.tc
```

- `#program` 场景：顶层有 `var math: int32 = 42`，`import math`
- `#lib` 场景：有 `public func math() int32 then end`，`import math`
- 验证：产生 `TC_CE_IMPORT_NAME_CONFLICT`，而非 `TC_CE_DUPLICATE_DEFINITION` 或其他错误

</details>

---

#### R-2 [低/可选] ✅ 已完成 — 词法分析器：`ptr_*` / `memblock_*` / `memcopy_unsafe` 关键字提权

| 项目 | 说明 |
|------|------|
| **规范依据** | 语言标准 §2.7 关键字列表明确包含全部 `ptr_add`、`memblock_load`、`memcopy_unsafe` 等 16 个名称 |
| **落地状态** | **已完成**（`40a518b`）：16 个独立 Token（`TC_TOK_PTR_*` / `TC_TOK_MEMBLOCK_*` / `TC_TOK_MEMCOPY_UNSAFE`）；词法 `tc_keyword_token()` 识别；解析器按 Token 类型分派 |
| **建议行为** | 将这些名称提升为词法层关键字，分配独立的 Token 类型（如 `TC_TOK_PTR_ADD` 等），在 `tc_keyword_token()` 中统一识别，解析器直接检查 Token 类型而非字符串 |
| **影响评估** | 不改变语义行为，不影响测试结果。属代码质量优化，非合规问题 |

<details>
<summary>涉及的关键字清单（16 个，已落地）</summary>

| 关键字 | 当前识别方式 | 当前 Token 类型 | 建议 Token 类型 |
|--------|------------|----------------|-----------------|
| `ptr_add` | Token 类型 | `TC_TOK_PTR_ADD` | `TC_TOK_PTR_ADD` |
| `ptr_sub` | Token 类型 | `TC_TOK_PTR_SUB` | `TC_TOK_PTR_SUB` |
| `ptr_load` | Token 类型 | `TC_TOK_PTR_LOAD` | `TC_TOK_PTR_LOAD` |
| `ptr_store` | Token 类型 | `TC_TOK_PTR_STORE` | `TC_TOK_PTR_STORE` |
| `ptr_address` | Token 类型 | `TC_TOK_PTR_ADDRESS` | `TC_TOK_PTR_ADDRESS` |
| `ptr_size` | Token 类型 | `TC_TOK_PTR_SIZE` | `TC_TOK_PTR_SIZE` |
| `ptr_eq` | Token 类型 | `TC_TOK_PTR_EQ` | `TC_TOK_PTR_EQ` |
| `ptr_ne` | Token 类型 | `TC_TOK_PTR_NE` | `TC_TOK_PTR_NE` |
| `ptr_lt` | Token 类型 | `TC_TOK_PTR_LT` | `TC_TOK_PTR_LT` |
| `ptr_le` | Token 类型 | `TC_TOK_PTR_LE` | `TC_TOK_PTR_LE` |
| `ptr_gt` | Token 类型 | `TC_TOK_PTR_GT` | `TC_TOK_PTR_GT` |
| `ptr_ge` | Token 类型 | `TC_TOK_PTR_GE` | `TC_TOK_PTR_GE` |
| `memblock_load` | Token 类型 | `TC_TOK_MEMBLOCK_LOAD` | `TC_TOK_MEMBLOCK_LOAD` |
| `memblock_store` | Token 类型 | `TC_TOK_MEMBLOCK_STORE` | `TC_TOK_MEMBLOCK_STORE` |
| `memblock_copy` | Token 类型 | `TC_TOK_MEMBLOCK_COPY` | `TC_TOK_MEMBLOCK_COPY` |
| `memcopy_unsafe` | Token 类型 | `TC_TOK_MEMCOPY_UNSAFE` | `TC_TOK_MEMCOPY_UNSAFE` |

</details>

---

### 修复优先级与排期

| 编号 | 任务 | 严重程度 | 预计工时 | 依赖 | 排期 |
|------|------|---------|---------|------|------|
| R-1 | 4b `TC_CE_IMPORT_NAME_CONFLICT` | **中**（合规偏差） | 2-3h | 无 | ✅ 已完成（`40a518b`） |
| R-2 | `ptr_*`/`memblock_*` 关键字提权 | **低**（可选优化） | 3-4h | 无 | ✅ 已完成（`40a518b`） |

---

### 验证标准

| 编号 | 验证项 | 状态 |
|------|-------|------|
| R-1-V1 | `#program` 模式：`import <名>` 与顶层 `var`/`let` 同名 → 产生 `TC_CE_IMPORT_NAME_CONFLICT`，且优先于后续阶段的通用错误 | ✅ |
| R-1-V2 | `#lib` 模式：`import <名>` 与 `func`/`struct`/`static let`/`static var` 同名 → 产生 `TC_CE_IMPORT_NAME_CONFLICT` | ✅ |
| R-1-V3 | 不冲突的合法 import 不受影响（回归测试全部通过） | ✅ |
| R-1-V4 | `TC_CE_IMPORT_NAME_CONFLICT` 与 `TC_CE_DUPLICATE_IMPORT` 在同一 import 上的优先级正确（后者优先） | ✅ |
| R-2-V1 | 所有 16 个名称在词法层被正确识别为新 Token 类型 | ✅ |
| R-2-V2 | 解析器中所有 `tc_token_is_ident_named("ptr_add", ...)` 调用替换为 Token 类型检查 | ✅ |
| R-2-V3 | 回归测试全部通过，无行为变化 | ✅ |

---

### 已确认无需修复的项

以下检查发现的问题经分析确认无需修复：

| 项 | 描述 | 无需修复的理由 |
|----|------|---------------|
| `padding` 关键字 | 词法层将 `padding` 识别为 `TC_TOK_PADDING`，但规范关键字列表中无此词 | `padding` 仅作为 `@padding(N)` 属性名出现，按普通标识符拼写匹配，但词法检测为独立 Token 类型是合理的实现选择。语言标准 §3.9 明确 `padding` 不是保留关键字 |
| 阶段 7/8 子阶段边界 | funcall/return 检查在 Pass2 中内联执行，未完全分离为独立函数调用 | 功能正确，流水线满足 fail-fast 语义，诊断优先级与规范一致 |
| `static var` 初始化器运行时执行 | `tc_func_check_static_vars()` 仅校验不执行，运行时由 executor 完成 | 符合规范 §9 "仅检查 static var 初始化器的编译期约束" 的描述 |

---

*本修复计划基于 2026-07-25 合规审查结果；R-1 / R-2 已于 `40a518b` 落地，本节标记为已完成。*

---

## 附录：Agent 与实现文档索引（非语言标准）

日常开发与 Cursor Agent 上下文由以下文档维护（**语言合法性与可观察语义仍以语言标准为准**）：

| 文档 | 职责 |
| ---- | ---- |
| [AGENTS.md](../AGENTS.md) | Agent 入口（精简，始终加载） |
| [.cursor/README.md](../.cursor/README.md) | 加载分级、文档地图、意图速查 |
| `.cursor/skills/tc-architecture/` | 架构路由、`features/*.md` 特性地图、`test-map.md` 测试账本 |
| `.cursor/rules/` | 编码/测试规范（Glob 或 `@knowledge-graph` 触发） |

文档数字校验：`python3 scripts/sync/check_doc_counts.py`（VM/AOT 注册 vs `test-map.md`）。

