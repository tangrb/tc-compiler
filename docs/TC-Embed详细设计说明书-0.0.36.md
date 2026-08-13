# TC-Embed 详细设计说明书

> **规范基线（唯一权威）**：[TC 语言标准 0.0.35](./TC语言标准设计说明书-0.0.35.md) · [TC 编译器标准 0.0.35](./TC编译器标准设计说明书-0.0.35.md)
>
> **当前实现基线**：TC-Embed v0.0.36（新模块）
>
> **状态**：v0.0.36 C 调用 TC 嵌入式运行时设计。VM 模式完整设计，AOT 模式扩展设计（**已落地 v0.0.36**）。以 `ptr<T>` 槽位编码为互操作原语。运行时便捷层（类型化参数 / 临时槽位区 / `make_ptr` / `call_typed`）见 **§16**。
>
> **上游契约**：[TC-VM 详细设计说明书](./TC-VM详细设计说明书-0.0.35.md) 的执行器与槽位系统 · [libtc 设计说明书](./libtc设计说明书-0.0.35.md) 的编译管线 · [TC-AOT 详细设计说明书](./TC-AOT详细设计说明书-0.0.35.md) 的代码生成模型

---

## 目录

1. [边界与目标](#1-边界与目标)
2. [设计动机：`ptr<T>` 即互操作原语](#2-设计动机ptrt-即互操作原语)
3. [总体架构](#3-总体架构)
4. [`TcEmbedCtx` 生命周期与数据契约](#4-tcembedctx-生命周期与数据契约)
5. [共享槽位模型](#5-共享槽位模型)
6. [符号查询 API](#6-符号查询-api)
7. [函数调用 API](#7-函数调用-api)
8. [`ptr<T>` 的 C 侧构造与使用](#8-ptrt-的-c-侧构造与使用)
9. [值桥接辅助函数](#9-值桥接辅助函数)
10. [错误模型与诊断](#10-错误模型与诊断)
11. [与现有 API 的关系](#11-与现有-api-的关系)
12. [使用示例](#12-使用示例)
13. [实施路径与改动清单](#13-实施路径与改动清单)
14. [验证策略](#14-验证策略)
15. [AOT 模式扩展](#15-aot-模式扩展)
    - [15.1 当前 AOT 代码生成的真实输出](#151-当前-aot-代码生成的真实输出)
    - [15.2 总体方案：两种输出模式](#152-总体方案两种输出模式)
    - [15.3 嵌入模式生成的代码布局](#153-嵌入模式生成的代码布局)
    - [15.4 函数表与头文件生成](#154-函数表与头文件生成)
    - [15.5 TcEmbedCtx 的 AOT 实现](#155-tcembedctx-的-aot-实现)
    - [15.6 构建流程与产物](#156-构建流程与产物)
    - [15.7 对现有 AOT 代码的改动](#157-对现有-aot-代码的改动)
    - [15.8 完整使用示例（AOT 嵌入模式）](#158-完整使用示例aot-嵌入模式)
    - [15.9 性能分析](#159-性能分析)
    - [15.10 验证策略](#1510-验证策略)
    - [15.11 实施步骤](#1511-实施步骤)
    - [15.12 与 VM 模式的 API 兼容性总表](#1512-与-vm-模式的-api-兼容性总表)
16. [运行时便捷层（v0.0.36 扩展）](#16-运行时便捷层v0036-扩展)
    - [16.1 设计动机](#161-设计动机)
    - [16.2 新增 API 总览](#162-新增-api-总览)
    - [16.3 类型化参数 TcEmbedArg](#163-类型化参数-tcembedarg)
    - [16.4 临时槽位区](#164-临时槽位区)
    - [16.5 C 数组一键映射为 ptr\<T\>](#165-c-数组一键映射为-ptrt)
    - [16.6 签名感知的类型化调用](#166-签名感知的类型化调用)
    - [16.7 使用示例](#167-使用示例)
    - [16.8 与现有 API 的关系与限制](#168-与现有-api-的关系与限制)

---

## 1. 边界与目标

### 1.1 版本基线

| 维度 | 版本 | 说明 |
| ---- | ---- | ---- |
| 目标语言规范 | 0.0.35 | TC 语言语法与语义 |
| 编译器规范 | 0.0.35 | 13 阶段编译管线 |
| 本文 | 0.0.36 设计 | TC-Embed 模块新增设计 |

### 1.2 目标

- 提供 C 宿主程序调用 TC 编译产物的最小化运行时 API。
- 实现 **C 和 TC 共享同一 `TcValue slots[]` 数组**，零拷贝互操作。
- 以 `ptr<T>` 槽位编码 `(slot << 1) | 1` 作为 C↔TC 之间传递变量引用的统一句柄。
- 支持多次重复调用同一 TC 函数，`static var` 状态在调用间持久化。
- 支持按名称查询函数签名与变量槽位索引。
- 支持 C 侧直接读写 slots，通过 `ptr<T>` 将数据传递给 TC 函数。
- 错误通过返回值传播，不引入异常机制。

### 1.3 非目标

- 不实现 TC 调用外部 C 函数（TC → C FFI），此为独立问题。
- 不修改 TC 语言标准或现有编译管线。
- 0.0.36 不改变 VM、Executor 或 libtc 的任何现有行为。
- **AOT 模式 C→TC 互操作已全部落地**（设计见 §15）。
- 不支持多线程并发调用同一 `TcEmbedCtx`（无锁、非线程安全）。
- 不新增 TC 语言层面的关键字或类型。

### 1.4 设计原则

1. **ptr<T> 即句柄**：`(slot << 1) | 1` 是唯一跨边界形式，C 和 TC 使用完全相同的编码/解码逻辑。
2. **共享内存**：`slots[]` 是 C 和 TC 的共享数据平面，无序列化、无类型转换、无中间表示。
3. **最小 API 表面积**：只暴露 slot 读写 + 符号查询 + 函数调用三个核心能力。
4. **复用现有执行器**：函数调用走 `tc_exec_call_function` 现有路径，不发明新的调度器。

---

## 2. 设计动机：`ptr<T>` 即互操作原语

### 2.1 槽位编码回顾

TC 内部 `ptr<T>` 的值编码为：

```
ptr_bits = (slot_index << 1) | 1
nullptr  = 0
```

这一编码是**公开的、确定的算法**。语言标准明确声明：

> 指针值（`ptr<T>`）以目标平台指针宽度的抽象位串存储；位模式对程序不可见。

TC 源码中的 `ptr_load`/`ptr_store` 只关心语义——解码出 slot 索引，读写 `slots[slot]`。**不区分 ptr 的来源**（TC 内 `ptr_address` 创建，还是 C 侧编码创建）。

### 2.2 零拷贝互操作模型

```
  ┌────────────────── TcValue slots[] ──────────────────┐
  │ slot[0]    slot[1]    slot[2]    slot[3]    ...     │
  │ [int32]    [int32]    [float64]  [ptr<int32>]       │
  │    ↑           ↑           ↑           ↑            │
  │    │           │           │           │            │
  │  C 写入     C 读取     TC 计算    TC ptr_load       │
  │  输入值     结果值     中间结果    指向 slot[0]      │
  └─────────────────────────────────────────────────────┘
            ▲                              │
            │         ptr<int32>           │
            │    (0 << 1) | 1 = 0x1        │
            │                              ▼
        C 代码 ──────────────────────► TC 函数调用
        构造 ptr                            参数
```

所有数据操作直接在 `slots[]` 数组上进行——C 写入输入，TC 函数处理，C 读取输出。

### 2.3 与其他互操作模型的对比

| | JNI | Lua C API | TC-Embed |
|---|---|---|---|
| 值传递方式 | `jvalue` union 逐值复制 | 虚拟栈 push/pop | 直接写 `slots[slot]`，传 slot 编码 |
| 是否有中间表示 | `jint/jlong/jobject` 类型桥 | lua_Number/lua_Integer | 无——统一 `TcValue` |
| 是否零拷贝 | 否（需要 Get/Set 字段） | 否（栈传输） | 是（C 和 TC 操作同一内存） |
| 是否需要引用管理 | 需要（DeleteLocalRef 等） | 不需要（值语义） | 不需要——随 ctx 销毁统一释放 |
| 是否需要反射查找 | FindClass/GetMethodID | lua_getglobal | 编译后槽位固定，O(1) 按索引访问 |

---

## 3. 总体架构

### 3.1 层次关系（VM 模式）

```
  ┌────────────────────────────────────────┐
  │          C 宿主程序                     │
  │  tc_embed_call / slot_write / ptr_encode │
  └──────────────┬─────────────────────────┘
                 │  C API
  ┌──────────────▼─────────────────────────┐
  │         tc_embed.h / tc_embed.c         │  ← 本模块
  │  TcEmbedCtx 生命周期                     │
  │  符号查询 · 槽位读写 · 函数调用组装       │
  └──────────────┬─────────────────────────┘
                 │
  ┌──────────────▼─────────────────────────┐
  │   libtc (tc_lib.c)                     │  ← 复用，不改
  │   tc_compile_file / tc_compile_source   │
  └──────────────┬─────────────────────────┘
                 │
  ┌──────────────▼─────────────────────────┐
  │   Executor (tc_executor.c)             │  ← 复用，小改
  │   tc_exec_call_function（公开）          │
  │   slots[] / memblock heap / struct heap │
  └─────────────────────────────────────────┘
```

### 3.1.1 层次关系（AOT 模式）

AOT 模式通过统一的 `TcEmbedCtx` API 和全局 `slots[]` 模型，让 C 宿主程序**不感知 VM/AOT 差异**。

```
  ┌────────────────────────────────────────┐
  │          C 宿主程序                     │
  │  tc_embed_call / slot_write / ptr_encode │  ← 与 VM 模式完全相同
  └──────────────┬─────────────────────────┘
                 │  C API（不变）
  ┌──────────────▼─────────────────────────┐
  │         tc_embed.h / tc_embed.c         │
  │  TcEmbedCtx 生命周期                     │
  │  符号查询 · 槽位读写 · 函数调用组装       │
  └──────────────┬─────────────────────────┘
          ┌──────┴──────┐
          │             │
  ┌───────▼──────┐ ┌───▼───────────────────┐
  │  VM 模式     │ │  AOT 模式               │
  │  Executor    │ │  aot_embed 桥接        │
  │  slots[]     │ │  slots[] + 函数表      │
  └──────────────┘ └───────────────────────┘
```

### 3.2 模块文件

| 文件 | 版本 | 责任 |
| ---- | ---- | ---- |
| `src/vm/embed/tc_embed.h` | 0.0.36 | 公共头文件：类型定义与 API 声明 |
| `src/vm/embed/tc_embed.c` | 0.0.36 | 实现：TcEmbedCtx 生命周期、符号索引、槽位访问、函数调用组装（VM 路径） |
| `src/vm/embed/tc_value_bridge.h` | 0.0.36 | 值桥接辅助宏/内联函数（`tc_value_from_*` / `tc_value_to_*`） |
| `src/aot/tc_aot_embed_rt.h` | 0.0.36 | 嵌入模式运行时 shim：非致命 abort、函数表类型声明、错误标记 |
| `src/vm/embed/tc_embed_aot.h` | 0.0.36 | AOT 桥接头文件（含 tc_embed.h 即可） |
| `src/vm/embed/tc_embed_aot.c` | 0.0.36 | AOT 桥接实现：`tc_embed_create_aot` |

### 3.3 公共头文件骨架

```c
#ifndef TC_EMBED_H
#define TC_EMBED_H

#include <stddef.h>
#include <stdint.h>

#include "tc_executor.h"       /* TcExecuteCtx, TcValue */
#include "tc_analyzer.h"       /* TcTypedProgram */
#include "tc_diagnostic.h"     /* TcDiagnostic */

#ifdef __cplusplus
extern "C" {
#endif

/* ── 不透明上下文 ── */
typedef struct TcEmbedCtx TcEmbedCtx;

/* ── 函数信息（只读，由 TcEmbedCtx 管理生命周期） ── */
typedef struct {
    const char *module_name;
    const char *func_name;
    int func_id;
    int has_return;
    int return_type;          /* TcTypeTag 投影；void 返回时无意义 */
    int *param_slots;         /* 各形参的 slot 号，长度 param_count */
    int *param_types;         /* 各形参的 TcTypeTag 投影（标量） */
    size_t param_count;
} TcEmbedFuncInfo;

/* ── 生命周期 ── */
TcEmbedCtx *tc_embed_create(const TcTypedProgram *program, TcDiagnostic *diag);
void tc_embed_destroy(TcEmbedCtx *ctx);

/* ── 符号查询 ── */
const TcEmbedFuncInfo *tc_embed_func_info(const TcEmbedCtx *ctx,
                                           const char *module,
                                           const char *func_name);

int tc_embed_top_var_slot(const TcEmbedCtx *ctx, const char *name);
int tc_embed_self_var_slot(const TcEmbedCtx *ctx, const char *name);

/* ── 槽位直接读写 ── */
int tc_embed_slot_write(TcEmbedCtx *ctx, int slot, TcValue value);
int tc_embed_slot_read(const TcEmbedCtx *ctx, int slot, TcValue *out);

/* ── ptr<T> 编码 ── */
static inline TcValue tc_embed_ptr_encode(int slot) {
    TcValue v;
    v.type = tc_type_tag_singleton(TC_PTR); /* 标签单例；完整 pointee 由调用上下文决定 */
    v.bits = ((uint64_t)(uint32_t)slot << 1) | 1ULL;
    return v;
}

static inline int tc_embed_ptr_is_null(TcValue v) {
    return v.bits == 0;
}

static inline int tc_embed_ptr_decode_slot(TcValue v, int *slot) {
    if (v.bits == 0 || (v.bits & 1ULL) == 0) return -1;
    *slot = (int)(v.bits >> 1);
    return 0;
}

/* ── 函数调用 ── */
int tc_embed_call(TcEmbedCtx *ctx, const char *module, const char *func,
                  int nargs, const TcValue *args, TcValue *result);

/* ── 错误查询 ── */
const char *tc_embed_get_error(const TcEmbedCtx *ctx);
int tc_embed_had_error(const TcEmbedCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* TC_EMBED_H */
```

---

## 4. `TcEmbedCtx` 生命周期与数据契约

### 4.1 内部结构

```c
struct TcEmbedCtx {
    TcExecuteCtx exec_ctx;            /* 复用的执行器上下文 */
    const TcTypedProgram *program;    /* 编译产物（不拥有所有权） */
    TcEmbedFuncInfo *funcs;           /* 函数索引数组 */
    size_t func_count;
    TcDiagnostic diag;                /* 持久化诊断 */
    int error_flag;                   /* 上次调用是否失败 */
    char error_message[512];          /* 上次错误的格式化消息 */
};
```

### 4.2 `tc_embed_create`

```c
TcEmbedCtx *tc_embed_create(const TcTypedProgram *program, TcDiagnostic *diag);
```

**行为**：

1. 是 OOM → 设置 `diag`，返回 NULL。
2. 分配 `TcEmbedCtx`。
3. 从 `program->symbols` 计算 `slot_count`。
4. 分配 `slots[]` 并初始化为未初始化哨兵。
5. 初始化 `memblock_heap` 与 `struct_heap`。
6. 调用 `tc_exec_init_all_static_vars` 初始化所有 `static var`。
7. 遍历符号表构建函数索引：收集每个 `TC_SYM_FUNCTION` 的 `func_id`、模块名、形参列表、返回类型、各形参的 `slot` 号。
8. 初始化内部 `diag`。
9. 返回 `ctx`。

**前置条件**：`program` 是成功编译的产物，调用方保持其生存期覆盖本 `ctx` 的整个使用期。`program` 本身的所有权仍归调用方（与 `tc_run_program` 一致）。

**失败回滚**：任何 malloc 失败或 `static var` 初始化失败时，释放已分配资源，设置 `diag`，返回 NULL。

### 4.3 `tc_embed_destroy`

```c
void tc_embed_destroy(TcEmbedCtx *ctx);
```

释放 `slots[]`、memblock heap、struct heap、函数索引中动态分配的内存（`param_slots`、`param_types` 数组）以及 `ctx` 自身。不释放 `program`（由调用方通过 `tc_typed_program_free` 释放）。

### 4.4 所有权总览

| 对象 | 所有者 | 释放方式 |
| ---- | ------ | -------- |
| `TcTypedProgram` | 调用方 | `tc_typed_program_free`（在 `tc_embed_destroy` 之后） |
| `TcEmbedCtx` | 调用方 | `tc_embed_destroy` |
| `slots[]` | `TcEmbedCtx` | 随 `tc_embed_destroy` |
| `memblock_heap` | `TcEmbedCtx` | 随 `tc_embed_destroy` |
| `struct_heap` | `TcEmbedCtx` | 随 `tc_embed_destroy` |
| 函数索引 | `TcEmbedCtx` | 随 `tc_embed_destroy` |

典型生命周期：

```c
TcTypedProgram prog;
tc_compile_file("mylib.tc", &prog, &diag);

TcEmbedCtx *ctx = tc_embed_create(&prog, &diag);
/* ... 多次 tc_embed_call ... */

tc_embed_destroy(ctx);       /* 先销毁 ctx */
tc_typed_program_free(&prog); /* 再释放 program */
```

---

## 5. 共享槽位模型

### 5.1 槽位数组

`ctx->exec_ctx.slots` 是一个 `TcValue` 数组，长度由 `tc_symbol_table_runtime_slot_count` 在创建时确定。该数组是 C 和 TC 共享的唯一数据平面。

### 5.2 槽位域

所有域（`TC_SLOT_TOPLEVEL`、`TC_SLOT_STATIC`、`TC_SLOT_PARAM`、`TC_SLOT_LOCAL`）的变量共用同一个 `slots[]` 数组。形参和局部变量的 slot 在编译期已由 Analyzer 分配为固定索引。

**重要**：TC 无运行时栈帧——参数/局部变量的 slot 是固定的。由于 TC 禁止递归（编译期调用图 DAG 检查保证），同一时刻只有一个函数调用帧活跃，参数/局部槽位不会被并发使用。这意味着 C 对 `slots[]` 的写入在执行 TC 函数调用时不会与嵌套调用冲突。

### 5.3 C 侧填充形参槽位

对于值类型形参（`int32`、`float64` 等），可以直接写入形参对应的 slot：

```c
int slot = func_info->param_slots[0];
tc_embed_slot_write(ctx, slot, tc_value_from_int32(42));
```

对于 `ptr<T>` 形参，写入编码后的指针值：

```c
int data_slot = /* C 侧提前准备的槽位 */;
tc_embed_slot_write(ctx, func_info->param_slots[0],
                     tc_embed_ptr_encode(data_slot));
```

### 5.4 C 侧准备数据数组

C 可以将任意大的数据平铺在连续的 slots 中，通过 `ptr<T>` + `ptr_add` 让 TC 函数遍历：

```c
/* C 侧：在 slots[base..base+N-1] 写入 N 个 int32 */
int base_slot = find_free_slot_block(ctx, N);
for (int i = 0; i < N; i++) {
    tc_embed_slot_write(ctx, base_slot + i, tc_value_from_int32(c_data[i]));
}

/* 传 ptr<int32> 给 TC 函数 */
TcValue args = tc_embed_ptr_encode(base_slot);
tc_embed_call(ctx, "mylib", "process_array", 1, &args, NULL);
```

TC 函数内部可以用 `ptr_add(ptr, i)` 遍历整个数组，语义完全一致。

### 5.5 越界处理

- `tc_embed_slot_write` 和 `tc_embed_slot_read` 在 `slot` 超出 `[0, slot_count)` 时返回错误。
- TC 内部 `ptr_add` 产生越界槽索引时，行为为实现定义（与语言标准一致），不保证运行时报错。
- C 调用方有责任保证写入的槽位在有效范围内。

---

## 6. 符号查询 API

### 6.1 函数信息查询

```c
const TcEmbedFuncInfo *tc_embed_func_info(const TcEmbedCtx *ctx,
                                           const char *module,
                                           const char *func_name);
```

- 按模块名 + 函数名精确匹配。
- 入口模块（`#program`）函数查询时 `module` 传 `NULL` 或 `""`。
- `#lib` 模块函数查询时 `module` 传模块名（与源文件名基名一致）。
- `public` 和 `private` 函数均可查询（宿主程序有完全访问权）。
- 返回的 `TcEmbedFuncInfo*` 生命周期绑定到 `TcEmbedCtx`。
- 未找到返回 `NULL`。

**返回结构体**：

```c
typedef struct {
    const char *module_name;   /* 所属模块名 */
    const char *func_name;     /* 函数名 */
    int func_id;               /* 全局唯一函数 ID */
    int has_return;            /* 是否有返回值（非 void） */
    int return_type;           /* 返回类型 TcTypeTag 投影 */
    int *param_slots;          /* 各形参 slot 索引，长度 param_count */
    int *param_types;          /* 各形参 TcTypeTag 投影（标量） */
    size_t param_count;        /* 形参数量 */
} TcEmbedFuncInfo;
```

### 6.2 顶层变量查询

```c
int tc_embed_top_var_slot(const TcEmbedCtx *ctx, const char *name);
```

返回入口模块中顶层 `var` 的 slot 索引。用于 C 侧直接读写全局变量。未找到返回 `-1`。

### 6.3 模块级静态变量查询

```c
int tc_embed_self_var_slot(const TcEmbedCtx *ctx, const char *name);
```

返回指定模块中 `static var` / `static let` 的 slot 索引。内部同时覆盖了 `TC_SYM_VARIABLE + TC_SLOT_STATIC`（当前编译器对 `static var` 的实际编码）、`TC_SYM_STATIC_VAR` 和 `TC_SYM_STATIC_LET` 三种情况进行兼容。`static let` 的常量值在编译期已确定，仍占据一个 slot（用于 `ptr_address` 取地址），但值为编译期位模式。

### 6.4 构建时机

索引在 `tc_embed_create` 内构建，遍历 `TcTypedProgram` 的全部符号表条目：

```
for each TcSymbol in program->symbols:
    if sym_kind == TC_SYM_FUNCTION:
        记录 func_id, 模块成员关系, 形参列表, 返回类型
        找到每个形参的 slot（通过 def_line + param_name 匹配）
    if sym_kind == TC_SYM_VARIABLE && slot_domain == TC_SLOT_TOPLEVEL:
        记录 name → slot 映射
    if (sym_kind == TC_SYM_VARIABLE && slot_domain == TC_SLOT_STATIC
        && sym->slot >= 0)
        || sym_kind == TC_SYM_STATIC_VAR || sym_kind == TC_SYM_STATIC_LET:
        按模块记录 name → slot 映射
```

---

## 7. 函数调用 API

### 7.1 `tc_embed_call`

```c
int tc_embed_call(TcEmbedCtx *ctx, const char *module, const char *func,
                  int nargs, const TcValue *args, TcValue *result);
```

**语义**：

1. 通过 `tc_embed_func_info` 查找目标函数，获取 `TcEmbedFuncInfo`。
2. 验证 `nargs == info->param_count`，不匹配返回 -1。
3. 将实参按顺序写入对应形参的 slot：
   ```c
   for (i = 0; i < nargs; i++) {
       ctx->exec_ctx.slots[info->param_slots[i]] = args[i];
   }
   ```
4. 调用 `tc_exec_call_function(info->func_id, ...)` 执行函数体。
5. 若函数有返回值，从函数返回的 `TcValue` 写入 `*result`。
6. 若 TC 执行中产生运行时错误，通过 diag 传播。
7. 返回 0 成功，-1 失败。

**关于 `tc_exec_call_function` 的公开**：

当前 `tc_exec_call_function` 是 `tc_executor.c` 内部的 `static` 函数。为支持 TC-Embed，需将其提升为公共函数，或在 `tc_executor.c` 中新增一个薄的公共包装：

```c
/* tc_executor.h 新增 */
int tc_exec_call_function_public(int func_id, TcExecuteCtx *ctx,
                                  TcValue *ret_out, int want_return,
                                  TcDiagnostic *diag, int line);
```

由于实参已在调用前写入形参 slot，该包装不需要传递 `args`/`arg_rhs` 参数。

### 7.2 重复调用

```c
/* 同一函数多次调用，static var 状态在两次调用间保持 */
tc_embed_call(ctx, NULL, "increment", 0, NULL, &result);
tc_embed_call(ctx, NULL, "increment", 0, NULL, &result);
/* 若 TC 内部 static var counter 从 0 开始，两次调用后 counter == 2 */
```

每次调用复用同一个 `slots[]` 和 `exec_ctx`。除 `static var` 外，每次调用前 C 需重新写入形参 slot（因为 TC 不会自动重置参数槽位）。

### 7.3 调用顺序约束

TC 调用图是 DAG（编译期保证），不存在递归。`tc_embed_call` 内部可能触发对其他 TC 函数的嵌套调用（如 `func_a` 内部调用了 `func_b`），执行器自动处理调用帧切换。

对外层 C 调用者而言，一次 `tc_embed_call` 是同步阻塞的。函数返回后，所有嵌套调用的副作用已完成。

### 7.4 非 void 返回值的读取

```c
TcValue result;
if (tc_embed_call(ctx, "math", "add", 2, args, &result) != 0) {
    /* 错误处理 */
}
int64_t sum;
tc_value_to_int64(result, &sum);
```

---

## 8. `ptr<T>` 的 C 侧构造与使用

### 8.1 编码规范

C 侧构造 `ptr<T>` 值必须严格遵循 TC 内部的编码规则：

```c
/* 合法 ptr：LSB = 1，高 63 位存 slot 索引 */
ptr.bits = ((uint64_t)(uint32_t)slot << 1) | 1ULL;

/* nullptr：bits == 0 */
ptr.bits = 0;

/* 无效模式：LSB = 0 且 bits ≠ 0 —— TC 执行器会拒绝 */
```

`tc_embed_ptr_encode(slot)` 和 `tc_embed_ptr_is_null(v)` 内联函数封装了这一编码。

### 8.2 与 TC 对端语义对齐

| C 操作 | 等效 TC 语义 |
| ------ | ------------ |
| `tc_embed_slot_write(ctx, s, v)` | 等同于对 slot `s` 赋值 |
| `tc_embed_ptr_encode(s)` | 等同于 `ptr_address(T, &var_at_slot_s)` |
| `ptr_load(p)` 在 TC 内 | 解码→`slots[slot]`，与 C 侧 `slots[slot]` 完全一致 |
| `ptr_store(p, v)` 在 TC 内 | 解码→写 `slots[slot]`，与 C 侧 `slots[slot] = v` 完全一致 |
| `ptr_add(p, n)` 在 TC 内 | `slot + n` → 重新编码，无需 `sizeof(T)` 乘法 |

### 8.3 memblock / struct 的 ptr 使用

`memcopy_unsafe` 语句将 `ptr<T>` 解码为 slot 索引，然后从该 slot 读取 memblock 堆指针进行原始内存复制。C 侧可以通过在 slot 中放置 memblock 值（指向外部内存块），再通过 `ptr` 传递给 TC 使用——但此场景在 0.0.36 作为未来扩展预留，初版以标量 `ptr<T>` 数组互操作优先。

---

## 9. 值桥接辅助函数

### 9.1 设计

值桥接函数是纯数据构造/解构，不涉及 heap 分配或 I/O。全部为 `static inline`，放在 `tc_value_bridge.h` 中。

### 9.2 整型

```c
static inline TcValue tc_value_from_int64(int64_t val) {
    TcValue v = { .type = tc_type_tag_singleton(TC_INT64), .bits = (uint64_t)val };
    return v;
}

static inline int tc_value_to_int64(TcValue v, int64_t *out) {
    /* 不做类型校验——C 侧调用者保证类型正确 */
    *out = (int64_t)v.bits;
    return 0;
}

static inline TcValue tc_value_from_uint64(uint64_t val) {
    TcValue v = { .type = tc_type_tag_singleton(TC_UINT64), .bits = val };
    return v;
}

/* int32 / uint32 等类型同理，bits 按位宽掩码规范化 */
static inline TcValue tc_value_from_int32(int32_t val) {
    TcValue v = { .type = tc_type_tag_singleton(TC_INT32),
                  .bits = (uint64_t)(uint32_t)val };
    return v;
}
```

`TcValue.type` 为 `const TcType*`（标量用进程内单例）。桥接层构造标量值时必须写单例指针，不得把裸 `TcTypeTag` 赋给 `type`。

### 9.3 浮点

```c
static inline TcValue tc_value_from_double(double val) {
    TcValue v = { .type = tc_type_tag_singleton(TC_FLOAT64) };
    memcpy(&v.bits, &val, sizeof(double));
    return v;
}

static inline int tc_value_to_double(TcValue v, double *out) {
    memcpy(out, &v.bits, sizeof(double));
    return 0;
}

static inline TcValue tc_value_from_float(float val) {
    TcValue v = { .type = tc_type_tag_singleton(TC_FLOAT32) };
    uint32_t bits;
    memcpy(&bits, &val, sizeof(float));
    v.bits = (uint64_t)bits;
    return v;
}
```

**位模式策略**：浮点值通过 `memcpy` 在位模式与宿主类型之间转换，避免严格的别名违规，也确保与 TC 内部 `uint64_t` 槽表示一致。

### 9.4 布尔

```c
static inline TcValue tc_value_from_bool(int val) {
    TcValue v = { .type = tc_type_tag_singleton(TC_BOOL), .bits = val ? 1ULL : 0ULL };
    return v;
}

static inline int tc_value_to_bool(TcValue v) {
    return v.bits != 0;
}
```

### 9.5 非标量类型（初版不实现，预留接口）

以下函数签名预留在头文件中，标记为 "v0.0.36 reserved"，供后续版本实现 memblock/struct 互操作：

```c
/* v0.0.36 reserved */
/* TcValue tc_value_wrap_external_memblock(void *data, size_t elem_size,
                                           uint64_t count, TcTypeTag elem_type,
                                           TcEmbedCtx *ctx); */
/* TcValue tc_value_from_struct_raw(const void *data, size_t byte_size); */
```

---

## 10. 错误模型与诊断

### 10.1 错误传播

TC 不引入异常机制。错误以返回值形式传播：

| 返回码 | 含义 |
| ------ | ---- |
| `0` | 成功 |
| `-1` | 错误（具体信息通过 `tc_embed_get_error` 获取） |

`TcEmbedCtx` 内部维护最近一次错误的状态：

```c
ctx->error_flag = 1;
/* 将 diag 消息格式化到 ctx->error_message */
```

### 10.2 错误查询

```c
const char *tc_embed_get_error(const TcEmbedCtx *ctx);
int tc_embed_had_error(const TcEmbedCtx *ctx);
```

- `tc_embed_get_error` 返回最近一次错误的描述字符串。若 ctx 为 NULL，返回 `"context is null"`。
- `tc_embed_had_error` 返回最近操作是否失败。
- 每次成功的操作将 `error_flag` 重置为 0。
- 消息格式与 `TcDiagnostic` 一致：`"<domain>: <kind>: <message>"`。

### 10.3 错误种类

| 错误场景 | 错误消息示例 |
| -------- | ------------ |
| 函数未找到 | `"function not found: <module>::<name>"` |
| 参数数量错误 | `"wrong argument count for <func>: expected N, got M"` |
| slot 越界 | `"slot index N out of range [0, M)"` |
| 执行时除零 | TC 运行时错误（由 executor 设置 diag 传播） |
| OOM | `"memory allocation failed"` |

### 10.4 与 TcDiagnostic 的关系

`TcEmbedCtx` 内部持有一个 `TcDiagnostic`，每次 TC 操作失败时由 executor 写入。`tc_embed_get_error` 读取其 message 字段。调用方不需要单独管理 `TcDiagnostic`——embed 层已封装。

---

## 11. 与现有 API 的关系

### 11.1 对 libtc API 的影响

**无影响**。`tc_lib.h` 中的 `tc_compile_source`、`tc_compile_file`、`tc_run_program`、`tc_typed_program_free`、`tc_set_module_search_paths` 保持不变。

`tc_run_program` 和 `tc_embed_create` + `tc_embed_call` 是互补关系，而非替代关系：

- `tc_run_program`：执行入口模块的顶层语句块（全程序执行）。
- `tc_embed_call`：按名称调用 `#lib` 模块中的单个函数（选择性执行）。

### 11.2 对 Executor 的影响

**最小改动**：将 `tc_exec_call_function`（`tc_executor.c` 中的 `static` 函数）提升为公共符号，或在同文件中新增公共包装。不改动函数内部逻辑。

**不需要改动的部分**：
- `tc_execute` 顶层入口 —— 不变。
- `tc_execute_block` / `tc_execute_statement` —— 不变。
- `tc_exec_init_all_static_vars` —— 不变（在 `tc_embed_create` 中复用）。
- `TcExecuteCtx` 结构体 —— 不变。

### 11.3 AOT 兼容性

AOT 模式下的 C→TC 互操作已落地，包括 AOT codegen 的 `embed_mode` 参数、非致命 abort（`tc_aot_embed_rt.h`）、函数表生成、`tc_embed_create_aot` + `tc_embed_call` 的 AOT 双路径、CLI `--embed` / `-H` 等。`tc_embed.h` 的公共 API 在 VM 和 AOT 两模式下完全兼容，宿主程序代码无需感知差异。

---

## 12. 使用示例

### 12.1 最小示例：标量参数与返回值

TC 代码（`math.tc`）：

```tc
#lib
public func add(a: int32, b: int32) -> int32 {
    return a + b
}
```

C 代码：

```c
#include <stdio.h>
#include "tc_lib.h"
#include "tc_embed.h"

int main(void) {
    TcDiagnostic diag;
    TcTypedProgram prog;
    tc_diagnostic_init(&diag);

    if (tc_compile_file("math.tc", &prog, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    TcEmbedCtx *ctx = tc_embed_create(&prog, &diag);
    if (!ctx) {
        tc_diagnostic_print(&diag, stderr);
        tc_typed_program_free(&prog);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    TcValue args[2];
    args[0] = tc_value_from_int32(3);
    args[1] = tc_value_from_int32(4);
    TcValue result;

    if (tc_embed_call(ctx, "math", "add", 2, args, &result) != 0) {
        fprintf(stderr, "error: %s\n", tc_embed_get_error(ctx));
    } else {
        int64_t sum;
        tc_value_to_int64(result, &sum);
        printf("3 + 4 = %lld\n", (long long)sum);
    }

    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_diagnostic_clear(&diag);
    return 0;
}
```

输出：

```text
3 + 4 = 7
```

### 12.2 ptr<T> 数组处理

TC 代码（`stats.tc`）：

```tc
#lib
public func sum(data: ptr<int32>, len: int32) -> int32 {
    var total: int32 = 0
    var i: int32 = 0
    while (i < len) {
        total = total + ptr_load(data + i)
        i = i + 1
    }
    return total
}
```

C 代码：

```c
#include <stdio.h>
#include "tc_lib.h"
#include "tc_embed.h"

int main(void) {
    /* 编译和创建 ctx 略，同上 */

    /* 1. 查函数信息，确定形参的 slot */
    const TcEmbedFuncInfo *info = tc_embed_func_info(ctx, "stats", "sum");

    /* 2. 在连续的 slots 中写入输入数据 */
    int data_slot = info->param_slots[0];   /* ptr<int32> data 的 slot */
    int32_t input[] = {1, 2, 3, 4, 5};
    for (int i = 0; i < 5; i++) {
        tc_embed_slot_write(ctx, data_slot + i,
                            tc_value_from_int32(input[i]));
    }

    /* 3. 构造 ptr<int32> 指向 data[0] */
    TcValue args[2];
    args[0] = tc_embed_ptr_encode(data_slot);   /* ptr<int32> = (slot<<1)|1 */
    args[1] = tc_value_from_int32(5);            /* len = 5 */

    /* 4. 调用 TC 函数 */
    TcValue result;
    if (tc_embed_call(ctx, "stats", "sum", 2, args, &result) != 0) {
        fprintf(stderr, "error: %s\n", tc_embed_get_error(ctx));
        return 1;
    }

    int64_t total;
    tc_value_to_int64(result, &total);
    printf("sum = %lld\n", (long long)total);
    /* 输出: sum = 15 */

    /* 清理略 */
    return 0;
}
```

### 12.3 重复调用与 static var 持久化

TC 代码（`counter.tc`）：

```tc
#lib
static var count: int32 = 0

public func increment_and_get() -> int32 {
    count = count + 1
    return count
}
```

C 代码：

```c
TcEmbedCtx *ctx = tc_embed_create(&prog, &diag);
TcValue result;

tc_embed_call(ctx, "counter", "increment_and_get", 0, NULL, &result);
/* result.bits == 1 */

tc_embed_call(ctx, "counter", "increment_and_get", 0, NULL, &result);
/* result.bits == 2 —— static var 持久化 */

tc_embed_call(ctx, "counter", "increment_and_get", 0, NULL, &result);
/* result.bits == 3 */
```

---

## 13. 实施路径与改动清单

### 13.1 实施步骤

| 步骤 | 内容 | 预计改动 |
| ---- | ---- | -------- |
| 1. 公开 `tc_exec_call_function` | 在 `tc_executor.h` 中声明 `tc_exec_call_function_public`，在 `tc_executor.c` 中添加薄的公共包装 | `tc_executor.c/h`（~10 行） |
| 2. 创建 `tc_embed.h` | 类型定义 + 公共 API 声明 + 内联辅助函数 | 新文件 |
| 3. 创建 `tc_value_bridge.h` | 值桥接内联函数族 | 新文件 |
| 4. 创建 `tc_embed.c` | 实现 TcEmbedCtx 生命周期、符号索引、槽位访问、函数调用组装 | 新文件（~250 行） |
| 5. 更新 CMakeLists.txt | 添加 `src/vm/embed/` 目录和 `tc_embed` 库目标 | `src/vm/CMakeLists.txt`、顶层 `CMakeLists.txt` |
| 6. 编写单元测试 | 覆盖标量参数/返回值、ptr<T> 数组处理、static var 持久化、错误路径 | `tests/unit/runtime/test_embed.c` |
| 7. 编写 .tc 用例 | 覆盖 ptr_load/store/add、函数嵌套调用、多模块 | `tests/vm/embed/` |
| 8. 文档同步 | 更新 AGENTS.md 索引、test-map.md | 多文件 |

### 13.2 文件清单

| 文件 | 操作 | 说明 |
| ---- | ---- | ---- |
| `src/vm/embed/tc_embed.h` | 新增 | 公共头文件 |
| `src/vm/embed/tc_embed.c` | 新增 | 实现 |
| `src/vm/embed/tc_value_bridge.h` | 新增 | 值桥接内联函数 |
| `src/vm/executor/tc_executor.h` | 修改 | 新增 `tc_exec_call_function_public` 声明 |
| `src/vm/executor/tc_executor.c` | 修改 | 新增公共包装函数 |
| `src/vm/CMakeLists.txt` | 修改 | 添加 embed 子目录 |
| `tests/unit/runtime/test_embed.c` | 新增 | 单元测试 |
| `tests/vm/embed/` | 新增目录 | VM 级测试用例 |
| `scripts/vm/run_tests.sh` | 修改 | 注册新用例 |

### 13.3 不修改的文件

- `src/vm/analyzer/`：全部不变。
- `src/vm/parser/`：全部不变。
- `src/aot/`：全部不变。
- `src/libtc/tc_lib.h` / `tc_lib.c`：全部不变。
- `docs/TC语言标准设计说明书-0.0.35.md`：TC 语言无变化。
- `docs/TC编译器标准设计说明书-0.0.35.md`：编译器管线不变。

---

## 14. 验证策略

### 14.1 单元测试（C）

| 测试 | 验证点 |
| ---- | ------ |
| `test_embed_create_destroy` | 创建/销毁生命周期，NULL program 拒绝 |
| `test_embed_call_scalar_args` | int32/float64/bool 参数传入与返回值读出 |
| `test_embed_call_no_args_void_return` | 无参 void 函数调用 |
| `test_embed_call_wrong_arg_count` | 参数数量不匹配时返回错误 |
| `test_embed_call_func_not_found` | 函数不存在时返回错误 |
| `test_embed_ptr_array_sum` | C 侧平铺数据→ptr 编码→TC ptr_load 遍历 |
| `test_embed_ptr_store_readback` | C 写 slot→TC ptr_store→C 读回（双向验证） |
| `test_embed_static_var_persist` | 重复调用保持 static var 值 |
| `test_embed_slot_write_read` | 槽位读写往返 |
| `test_embed_slot_out_of_range` | 越界槽位拒绝 |
| `test_embed_error_message` | 错误消息可读 |

### 14.2 TC 用例

在 `tests/vm/embed/` 下创建 `.tc` 文件，测试 TC 侧使用 C 传入的 ptr：

| 用例 | TC 函数逻辑 |
| ---- | ----------- |
| `ptr_sum.tc` | `sum(data: ptr<int32>, n: int32) → int32`：遍历 ptr_add 累加 |
| `ptr_inplace.tc` | `increment_all(data: ptr<int32>, n: int32)`：ptr_store 原地修改 |
| `ptr_loop.tc` | `count_positive(data: ptr<int32>, n: int32) → int32`：条件统计 |
| `nested_call.tc` | `outer` 调 `inner`：验证嵌套 funcall 在 embed 下正常 |

### 14.3 集成测试

在 `scripts/vm/run_tests.sh` 中注册新用例组，确保全量测试基线不回退。

---

## 15. AOT 模式扩展

> **状态**：已落地 v0.0.36。基于当前 AOT 代码生成的真实实现，描述 AOT 模式下 C→TC 互操作的完整设计。

### 15.1 当前 AOT 代码生成的真实输出

#### 15.1.1 全局槽位数组

当前 AOT 已使用单一扁平 `slots[]` 数组覆盖所有域。Analyzer 在 Pass1 阶段用一个全局递增计数器 `next_slot` 顺序分配 TOPLEVEL、STATIC、PARAM、LOCAL 槽位：

```277:295:src/vm/analyzer/tc_analyzer_pass1.c
int tc_pass1_collect_symbols(TcProgram *program, TcSymbolTable *symbols,
                                    TcDiagnostic *diag) {
    TcAnalyzeCtx ctx;
    size_t i = 0;
    int next_slot = (int)tc_symbol_table_runtime_slot_count(symbols);

    ctx.program = program;
    ctx.last_init = NULL;
    ctx.next_loop_id = 0;
    tc_stmt_index_reset(&ctx.index);

    for (i = 0; i < program->count; i++) {
        if (tc_pass1_collect_stmt(&program->items[i], symbols, &next_slot, TC_SLOT_TOPLEVEL,
                                   &ctx, diag) != 0) {
            return -1;
        }
    }
    return 0;
}
```

`tc_symbol_table_runtime_slot_count` 遍历全部符号取 `max(slot+1)`，返回值覆盖所有域。

```120:133:src/vm/runtime/tc_symbol.c
size_t tc_symbol_table_runtime_slot_count(const TcSymbolTable *table) {
    size_t i = 0;
    size_t count = 0;

    for (i = 0; i < table->count; i++) {
        const TcSymbol *sym = &table->symbols[i];

        if (sym->sym_kind == TC_SYM_VARIABLE && sym->slot >= 0 &&
            (size_t)(sym->slot + 1) > count) {
            count = (size_t)(sym->slot + 1);
        }
    }
    return count;
}
```

AOT codegen 直接使用此值声明 `slots[]`：

```c
/* AOT 生成代码中的全局声明 */
static uint64_t slots[SLOT_COUNT];   /* SLOT_COUNT = tc_symbol_table_runtime_slot_count() */
```

`TcSlotDomain` 枚举存在于符号元数据中，但生成的 C 代码**不使用**它——所有 slot 统一按 `slots[N]` 访问。

#### 15.1.2 函数生成（无局部数组）

当前 `tc_aot_emit_function` 生成的函数体**没有** `params[]` 或 `locals[]` 局部声明：

```1685:1711:src/aot/tc_aot_codegen.c
static int tc_aot_emit_function(FILE *out, const TcFuncDef *func, const TcProgram *module,
                                TcAotEmitCtx *ctx) {
    int body_start = 0;
    int body_end = 0;
    size_t i = 0;

    (void)body_end;
    if (tc_aot_func_body_index_range(module, func->func_id, &body_start, &body_end) != 0) {
        return -1;
    }
    fprintf(out, "static void tc_func_%d(TcDiagnostic *diag) {\n    tc_aot_cur_diag = diag;\n", func->func_id);
    ctx->current_func_id = func->func_id;
    ctx->current_return_type = func->return_type.tag;
    ctx->block_path.depth = 0;
    ctx->loops.depth = 0;
    tc_stmt_index_reset(&ctx->index);
    ctx->index.next = body_start;
    for (i = 0; i < func->body_count; i++) {
        if (tc_aot_emit_statement_impl(out, &func->body[i], ctx, "    ") != 0) {
            ctx->current_func_id = -1;
            return -1;
        }
    }
    ctx->current_func_id = -1;
    fprintf(out, "}\n\n");
    return 0;
}
```

生成的函数签名只有：

```c
static void tc_func_N(TcDiagnostic *diag) {
    tc_aot_cur_diag = diag;
    /* ... 函数体直接读写 slots[PARAM_M] / slots[LOCAL_K] ... */
}
```

这个设计与嵌入需求**天然吻合**：所有数据都在全局 `slots[]` 中，C 宿主程序完全可以直接读写。

#### 15.1.3 函数调用（已经写槽位）

内部 funcall 的代码生成已经在调用前将实参写入被调用者的形参 slot：

```519:565:src/aot/tc_aot_codegen.c
static int tc_aot_emit_funcall(FILE *out, int func_id, const TcNamedArg *stmt_args,
                               size_t stmt_arg_count, const TcAotFuncallExprArg *expr_args,
                               size_t expr_arg_count, int use_expr_args, const char *indent,
                               TcAotEmitCtx *ctx, int stmt_index, int line, int want_result,
                               const char *dst_expr) {
    const TcProgram *module = NULL;
    const TcFuncDef *func = NULL;
    char abort_indent[64];
    size_t pi = 0;

    func = tc_aot_find_func_def(ctx->program, func_id, &module);
    if (!func) {
        return -1;
    }
    tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);

    for (pi = 0; pi < func->param_count; pi++) {
        const TcFuncParam *param = &func->params[pi];
        const TcRhs *arg_rhs = NULL;
        int param_slot = -1;

        if (use_expr_args) {
            arg_rhs = tc_aot_find_expr_arg_rhs(param->name, expr_args, expr_arg_count);
        } else {
            arg_rhs = tc_aot_find_named_arg_rhs(param->name, stmt_args, stmt_arg_count);
        }
        if (!arg_rhs) {
            return -1;
        }
        if (tc_aot_param_slot(&ctx->program->symbols, func, param->name, &param_slot) != 0 ||
            param_slot < 0) {
            return -1;
        }
        if (tc_aot_emit_rhs_slot(out, arg_rhs, param->type.tag, param_slot, indent, ctx,
                                 stmt_index, line) != 0) {
            return -1;
        }
    }

    fprintf(out, "%stc_func_%d(tc_aot_cur_diag);\n", indent, func_id);
    fprintf(out, "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, %d);\n",
            abort_indent, line);
    if (want_result && dst_expr) {
        fprintf(out, "%s%s = tc_ret_%d;\n", indent, dst_expr, func_id);
    }
    return 0;
}
```

**这已经是嵌入所需的调用模式**：`slots[param_slot] = arg; tc_func_N(diag); if (error) abort; result = tc_ret_N;`。

#### 15.1.4 返回值

每个非 void 函数有独立的全局变量：

```c
static uint64_t tc_ret_0;    /* func_id=0 的返回值 */
static uint64_t tc_ret_3;    /* func_id=3 的返回值 */
static uint64_t tc_ret_5;    /* func_id=5 的返回值 */
```

调用者通过 `tc_ret_N` 读取返回值。由于 TC 禁止递归且调用图是 DAG，同一时刻只有一个函数返回活跃。

#### 15.1.5 ptr<T> 编码

```316:318:src/aot/tc_aot_rt.c
uint64_t tc_aot_ptr_address(int slot) {
    return ((uint64_t)slot << 1) | TC_AOT_PTR_TAG;
}
```

```308:314:src/aot/tc_aot_rt.c
static int tc_aot_ptr_decode(uint64_t bits, int *slot) {
    if (bits == 0 || (bits & TC_AOT_PTR_TAG) == 0) {
        return -1;
    }
    *slot = (int)(bits >> 1);
    return 0;
}
```

编码与 VM 的 `tc_embed_ptr_encode` 完全一致：`(slot << 1) | 1`。

#### 15.1.6 现有问题：不适合嵌入

| 问题 | 当前行为 | 嵌入需要 |
| ---- | -------- | -------- |
| 函数可见性 | `static void tc_func_N(...)` | 需要暴露给外部 C 宿主程序 |
| 错误处理 | `tc_aot_abort()` 内部调 `exit(1)` | 返回错误码，不终止进程 |
| 函数查找 | 无运行时查找表 | 需要 func_id → 函数指针映射 |
| `main()` | 自动生成 | 嵌入模式跳过 main() |
| 静态初始化 | `tc_init_static_vars()` 在 main() 中调用 | 暴露为公共入口，宿主程序手动调用 |
| 内存释放 | 在 main() 末尾释放 | 暴露 `tc_aot_cleanup()` 给宿主程序调用 |

> **v0.0.36 已解决：以上所有问题均已落地。** 嵌入模式通过 `tc_aot_emit_c(..., /* embed_mode= */ 1)` 触发：函数和全局符号取消 `static`、`tc_aot_abort` 宏替换为非致命 `tc_aot_embed_abort`、不生成 `main()`、`tc_aot_init()` / `tc_aot_cleanup()` 暴露为公共接口、`tc_aot_func_table` 提供 func_id → 函数指针映射。

---

### 15.2 总体方案：两种输出模式

`tc_aot_emit_c` 增加一个 `embed` 标志，控制生成代码的形态：

```c
/* 修改后的公共接口 */
int tc_aot_emit_c(FILE *out,
                  const TcTypedProgram *program,
                  const char *source_name,
                  int embed_mode);    /* 新增：0 = 独立程序, 1 = 嵌入库 */
```

| 生成内容 | `embed_mode = 0`（现有） | `embed_mode = 1`（嵌入） |
| -------- | ----------------------- | ------------------------ |
| `slots[]` 声明 | `static uint64_t slots[N]` | 非 `static`：`uint64_t slots[N]` |
| `tc_ret_N` 声明 | `static uint64_t tc_ret_N` | 非 `static` |
| 函数声明 | `static void tc_func_N(...)` | 非 `static`：`void tc_func_N(...)` |
| 错误处理 | `tc_aot_abort(&diag, line)` → `exit(1)` | `tc_aot_embed_abort(diag)` → 设置错误标记，`return` |
| `main()` | 生成完整的 main() | **不生成** main() |
| `tc_init_static_vars()` | 在 main() 中调用 | 暴露为公共函数 `tc_aot_init()` |
| 内存清理 | `tc_aot_*_heap_free_all()` 在 main() 末尾 | 暴露为 `tc_aot_cleanup()` |
| 函数表 | 不生成 | 生成（见 §15.4） |
| 头文件 | 不生成 | 生成独立 `.h` 文件（见 §15.4） |

---

### 15.3 嵌入模式生成的代码布局

#### 15.3.1 生成的 `.c` 文件骨架（embed_mode = 1）

```c
/* 由 tc-aot --embed 生成 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tc_aot_rt.h"
#include "tc_aot_embed_rt.h"   /* 新增：嵌入模式运行时 shim */

/* ── 全局槽位（非 static，外部可见） ── */
#define TC_AOT_SLOT_COUNT   42
#define TC_AOT_MEMBLOCK_SLOT_COUNT  2
#define TC_AOT_STRUCT_SLOT_COUNT    1

uint64_t slots[TC_AOT_SLOT_COUNT];
static void *tc_aot_memblock_storage[TC_AOT_MEMBLOCK_SLOT_COUNT];
static uint8_t tc_aot_struct_storage[TC_AOT_STRUCT_TOTAL_BYTES];

static int tc_aot_initialized = 0;

/* ── 返回值全局变量 ── */
uint64_t tc_aot_ret_3;
uint64_t tc_aot_ret_5;
/* ... */

/* ── 前向声明（非 static） ── */
void tc_aot_func_3(TcDiagnostic *diag);
void tc_aot_func_5(TcDiagnostic *diag);
/* ... */

/* ── 静态初始化 ── */
int tc_aot_init(TcDiagnostic *diag) {
    tc_diagnostic_init(diag);
    tc_aot_cur_diag = diag;

    if (tc_aot_initialized) return 0;  /* 幂等 */

    tc_aot_init_slots(slots, TC_AOT_SLOT_COUNT);

    /* static var 初始化（按依赖拓扑序） */
    /* 如有 OOM / 运行时错误，设置 diag 并返回 -1 */
    if (tc_aot_emit_static_init_rhs(diag) != 0) return -1;

    tc_aot_initialized = 1;
    return 0;
}

static int tc_aot_emit_static_init_rhs(TcDiagnostic *diag) {
    /* 每个 static var 的初始化 RHS */
    /* ... (与现有 tc_init_static_vars 相同) ... */
    return 0;
}

/* ── 函数定义 ── */
void tc_aot_func_3(TcDiagnostic *diag) {
    tc_aot_cur_diag = diag;
    /* ... 函数体，直接读写 slots[] ... */
}

/* ── 函数表（见 §15.4） ── */
#include "tc_aot_func_table.inc"   /* 或内联生成 */

/* ── 清理 ── */
void tc_aot_cleanup(void) {
    if (!tc_aot_initialized) return;
    tc_aot_memblock_heap_free_all();
    tc_aot_struct_heap_free_all();
    tc_aot_initialized = 0;
}
```

#### 15.3.2 非致命错误处理

现有 `tc_aot_abort` 调 `exit(1)`，嵌入模式不能终止宿主进程。替换为：

```c
/* src/aot/tc_aot_embed_rt.h — 新增文件 */

/* 嵌入模式诊断上下文 */
extern TcDiagnostic *tc_aot_cur_diag;
extern int tc_aot_embed_error;

/* 替代 tc_aot_abort：设置错误标记，返回而不 exit */
static inline void tc_aot_embed_abort(TcDiagnostic *diag, int line) {
    (void)line;
    tc_aot_cur_diag = diag;
    tc_aot_embed_error = 1;
    /* 不调 exit(1) — 控制权返回给调用者 */
    /* 所有可能失败的操作在 diag 设置后立即通过 guard 返回 */
}
```

所有生成的错误 guard 从：

```c
if (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, line);
```

改为：

```c
if (tc_aot_cur_diag->domain != TC_DIAG_NONE) {
    tc_aot_embed_abort(tc_aot_cur_diag, line);
    return;
}
```

由于每个 shim 调用后已有 guard，错误会沿调用链逐层向上传播。`tc_embed_call` 在函数返回后检查 `diag.domain`：

```c
/* tc_embed_call 中（AOT 路径） */
entry(diag);
if (diag->domain != TC_DIAG_NONE) {
    tc_embed_set_error(ctx, diag->message);
    return -1;
}
```

---

### 15.4 函数表与头文件生成

#### 15.4.1 函数表结构

生成的 C 代码中的函数表仅包含调度所需的最小字段：

```c
/* 由 AOT codegen 生成的函数表类型（定义见 tc_aot_embed_rt.h） */
typedef void (*tc_aot_func_entry_t)(TcDiagnostic *diag);

typedef struct {
    int func_id;
    tc_aot_func_entry_t entry;
    uint64_t *ret_ptr;          /* 指向 tc_aot_ret_N 全局变量（void 函数为 NULL） */
} tc_aot_func_entry;

/* 全局函数表，以哨兵条目 { -1, NULL, NULL } 终止 */
const tc_aot_func_entry tc_aot_func_table[] = {
    { 3, tc_aot_func_3, &tc_aot_ret_3 },
    { 5, tc_aot_func_5, NULL },      /* void 函数 */
    { -1, NULL, NULL }               /* 哨兵 */
};
```

**架构决策**：函数表不包含模块名、函数名、形参列表、返回类型等元数据。这些元数据由 `tc_embed_create_aot` 通过 `TcTypedProgram` 提取，构建为统一的 `TcEmbedFuncInfo` 索引。这样设计的好处是生成的 C 代码不冗余携带元数据，元数据统一由编译器管线维护。

#### 15.4.2 生成的头文件（`mylib.tc.h`）

AOT 在嵌入模式下额外生成一个 `.h` 文件，声明所有公共符号。生成逻辑见 `tc_aot_emit_embed_header`（`tc_aot_codegen.c`）：

```c
/* 由 tc-aot --embed mylib.tc -H mylib.h 生成 */
#ifndef TC_AOT_EMBED_H
#define TC_AOT_EMBED_H

#include <stddef.h>
#include <stdint.h>
#include "tc_diagnostic.h"
#include "tc_aot_embed_rt.h"

/* ── 槽位维度 ── */
#define TC_AOT_SLOT_COUNT       42

/* ── 全局数据 ── */
extern uint64_t slots[TC_AOT_SLOT_COUNT];

/* ── 公共函数 ── */
int tc_aot_init(TcDiagnostic *diag);
void tc_aot_cleanup(void);

/* ── 函数表（供 tc_embed 内部使用，类型定义见 tc_aot_embed_rt.h） ── */
/* typedef void (*tc_aot_func_entry_t)(TcDiagnostic *diag); */
/* typedef struct { int func_id; tc_aot_func_entry_t entry; uint64_t *ret_ptr; } tc_aot_func_entry; */

extern const tc_aot_func_entry tc_aot_func_table[];

#endif /* TC_AOT_EMBED_H */
```

#### 15.4.3 类型安全的便捷封装（可选）

AOT 还可以生成类型安全的 C 内联封装，让宿主程序直接调用原生类型：

```c
/* 方便宿主程序的类型安全封装 */
static inline int32_t mylib_add(int32_t a, int32_t b, TcDiagnostic *diag) {
    /* 写入实参到形参 slot */
    slots[12] = (uint64_t)(uint32_t)a;   /* param_slots[0] */
    slots[13] = (uint64_t)(uint32_t)b;   /* param_slots[1] */

    /* 调用生成的函数 */
    tc_aot_func_3(diag);

    /* 检查错误 */
    if (diag->domain != TC_DIAG_NONE) return 0;

    /* 返回结果（不做额外类型转换，位模式即值） */
    return (int32_t)tc_aot_ret_3;
}
```

这是一个便利层，可选生成（通过 AOT flag `--embed-stubs`）。与 `tc_embed_call` 相比牺牲了通用性，但提供了编译期类型安全和零查找开销。

---

### 15.5 TcEmbedCtx 的 AOT 实现

#### 15.5.1 内部结构（AOT 字段）

```c
struct TcEmbedCtx {
    /* ── 模式标识 ── */
    int is_aot;                      /* 0 = VM 模式, 1 = AOT 模式 */

    /* ── VM 模式字段 ── */
    TcExecuteCtx exec_ctx;

    /* ── AOT 模式字段 ── */
    uint64_t *aot_slots;                   /* 指向 AOT 生成的全局 slots[]（非 TcValue，直接为 uint64_t） */
    size_t aot_slot_count;
    const tc_aot_func_entry *aot_func_table;  /* 指向 AOT 生成的函数表（以 func_id < 0 哨兵终止） */

    /* ── 通用字段 ── */
    const TcTypedProgram *program;    /* 编译产物：VM 模式用于执行器；AOT 模式用于构建函数元数据索引 */
    TcEmbedFuncInfo *funcs;           /* 统一索引 */
    size_t func_count;
    TcDiagnostic diag;
    int error_flag;
    char error_message[512];
};
```

注：`aot_slots` 类型为 `uint64_t *` 而非 `TcValue *`，因为 AOT 生成的全局 slots 数组声明为 `uint64_t slots[N]`。函数表以哨兵条目 `{ -1, NULL, NULL }` 终止，不显式存储 `aot_func_count`。

#### 15.5.2 创建函数

```c
TcEmbedCtx *tc_embed_create_aot(uint64_t *slots, size_t slot_count,
                                 const tc_aot_func_entry *func_table,
                                 int (*init_fn)(TcDiagnostic *diag),
                                 const TcTypedProgram *program,
                                 TcDiagnostic *diag);
```

**行为**：

1. 分配 `TcEmbedCtx`，`is_aot = 1`。
2. `aot_slots` 指向传入的 `slots`（不拥有所有权—由 AOT 生成的全局数组管理生命周期）。
3. `aot_func_table` 指向传入的函数表（以 `func_id < 0` 的哨兵条目终止）。
4. 调用 `init_fn(diag)` 初始化 slots 和 static var（传入回调而非内部直接调用 `tc_aot_init`，因为 AOT 生成的符号需经 `dlsym` 加载）。
5. 同时接收 `const TcTypedProgram *program`，用于从符号表提取函数元数据（函数名、模块名、形参列表、返回类型等），构建为统一的 `TcEmbedFuncInfo` 索引。
6. 返回 ctx，失败（含 OOM 或 `init_fn` 失败）返回 NULL。

**所有权**：`slots` 和 `func_table` 由生成的 `.c` 文件的全局数据区持有，`TcEmbedCtx` 不负责释放它们。`tc_embed_destroy` 在 AOT 模式下释放 ctx 自身 + 索引数组（`funcs`），但不释放 `slots`。

#### 15.5.3 `tc_embed_call` 的 AOT 路径

```c
int tc_embed_call(TcEmbedCtx *ctx, const char *module, const char *func,
                  int nargs, const TcValue *args, TcValue *result) {
    const TcEmbedFuncInfo *info = tc_embed_func_info(ctx, module, func);
    if (!info) return -1;
    if ((size_t)nargs != info->param_count) return -1;

    /* 写入实参到形参 slot */
    for (int i = 0; i < nargs; i++) {
        tc_embed_slot_write(ctx, info->param_slots[i], args[i]);
    }

    if (ctx->is_aot) {
        /* ── AOT 路径 ── */
        const tc_aot_func_entry *entry = tc_embed_find_aot_entry(ctx, info->func_id);
        if (!entry) {
            tc_embed_set_error(ctx, "internal error: AOT function entry not found");
            return -1;
        }

        tc_diagnostic_init(&ctx->diag);     /* 每次调用重置诊断 */
        entry->entry(&ctx->diag);           /* 直接调用生成的 C 函数 */

        if (ctx->diag.domain != TC_DIAG_NONE) {
            tc_embed_set_error(ctx, ctx->diag.message);
            return -1;
        }

        /* 读取返回值：通过 tc_aot_func_entry.ret_ptr 指向的 tc_aot_ret_N 全局变量 */
        if (info->has_return && result && entry->ret_ptr) {
            result->bits = *entry->ret_ptr;
            result->type = tc_type_tag_singleton((TcTypeTag)info->return_type);
        }
    } else {
        /* ── VM 路径 ── */
        TcValue ret;
        int rc = tc_exec_call_function_public(info->func_id, &ctx->exec_ctx,
                                               &ret, info->has_return,
                                               &ctx->diag, 0);
        if (rc != 0) {
            tc_embed_set_error(ctx, ctx->diag.message);
            return -1;
        }
        if (info->has_return && result) *result = ret;
    }
    return 0;
}
```

#### 15.5.4 函数表查询

```c
/* 在 AOT 函数表中按 func_id 查找，函数表以 { -1, NULL, NULL } 哨兵终止 */
static const tc_aot_func_entry *tc_embed_find_aot_entry(
        const TcEmbedCtx *ctx, int func_id) {
    const tc_aot_func_entry *entry = NULL;
    size_t i = 0;

    if (!ctx->aot_func_table) return NULL;
    for (i = 0; ; i++) {
        entry = &ctx->aot_func_table[i];
        if (entry->func_id < 0) break;    /* 哨兵 */
        if (entry->func_id == func_id) return entry;
    }
    return NULL;
}
```

函数表大小通常 < 100 项，哨兵终止的线性扫描可接受。如需更高性能，AOT codegen 可生成按 `func_id` 排序的函数表，启用二分查找。

---

### 15.6 构建流程与产物

#### 15.6.1 编译命令

```bash
# 独立程序模式（现有，不变）
tc-aot mylib.tc -o mylib.c
cc -std=c99 -Wall -Wextra -Werror -pedantic \
   -I$AOT_RT_DIR -I$VM_RUNTIME_DIR \
   mylib.c tc_aot_rt.c tc_types.c tc_diagnostic.c \
   tc_semantics.c tc_sem_int.c tc_sem_fp.c tc_sem_cast.c tc_sem_bitwise.c tc_io.c \
   -o mylib.out

# 嵌入库模式（新增 --embed）
tc-aot --embed mylib.tc -o mylib_tc.c -H mylib_tc.h

# 宿主程序编译
cc -std=c99 -Wall -Wextra -Werror -pedantic \
   -I$EMBED_DIR -I$AOT_RT_DIR -I$VM_RUNTIME_DIR \
   host_program.c mylib_tc.c \
   tc_aot_rt.c tc_aot_embed.c tc_embed.c tc_types.c tc_diagnostic.c \
   tc_semantics.c tc_sem_int.c tc_sem_fp.c tc_sem_cast.c tc_sem_bitwise.c tc_io.c \
   -o host_program
```

#### 15.6.2 文件清单

| 文件 | 来源 | 说明 |
| ---- | ---- | ---- |
| `mylib_tc.c` | AOT codegen 生成 | 生成的 TC 函数 + slots + 函数表 |
| `mylib_tc.h` | AOT codegen 生成 | 类型声明与便捷封装（可选） |
| `tc_aot_rt.c/h` | 编译器自带 | AOT 运行时 shim（算术/浮点/cast等） |
| `tc_aot_embed_rt.h` | 新增 | 嵌入模式专用：非致命 abort、错误传播 |
| `tc_embed.c/h` | TC-Embed 模块 | 统一 API（VM + AOT 双路径） |
| `tc_embed_aot.c` | AOT 桥接层 | `tc_embed_create_aot` 实现 |
| `tc_value_bridge.h` | TC-Embed 模块 | 值桥接内联函数 |
| `tc_diagnostic.c/h` | 编译器自带 | 诊断管理 |
| `tc_types.c/h` | 编译器自带 | 类型常量与宽度表 |
| `tc_semantics.c/h` | 编译器自带 | slot 初始化等 |
| `tc_sem_int/fp/cast/bitwise.c` | 编译器自带 | 共享数值语义 |
| `tc_io.c` | 编译器自带 | 共享 I/O |

---

### 15.7 对现有 AOT 代码的改动

#### 15.7.1 改动文件清单

| 文件 | 改动 | 说明 |
| ---- | ---- | ---- |
| `src/aot/tc_aot_codegen.h` | 修改 | `tc_aot_emit_c` 新增 `embed_mode` 参数 |
| `src/aot/tc_aot_codegen.c` | 修改 | 根据 `embed_mode` 控制 static/non-static、main()生成、abort 模式、函数表生成 |
| `src/aot/tc_aot_rt.h` | 修改 | 声明 `tc_aot_embed_abort`（新增） |
| `src/aot/tc_aot_rt.c` | 修改 | 实现 `tc_aot_embed_abort`（设置错误标记而非 exit） |
| `src/aot/main.c` | 修改 | CLI 新增 `--embed` 和 `-H` 选项；嵌入模式构建链接 `tc_embed` 而非生成 main() |
| `src/vm/embed/tc_embed.h` | 修改 | 声明 `tc_embed_create_aot`；`TcEmbedCtx` 内部扩展 AOT 字段 |
| `src/vm/embed/tc_embed.c` | 修改 | `tc_embed_call` 新增 AOT 路径；`tc_embed_create_aot` 实现 |
| `src/vm/embed/tc_embed_aot.c` | 新增 | AOT 专用函数：`tc_embed_create_aot` + 函数表查询 |
| `CMakeLists.txt` | 修改 | 新增 `tc_embed_aot` target |

#### 15.7.2 codegen 改动详细伪码

`tc_aot_emit_c` 的改动逻辑：

```c
int tc_aot_emit_c(FILE *out, const TcTypedProgram *program,
                  const char *source_name, int embed_mode) {
    /* ... preamble ... */

    /* 1. slots 声明 */
    if (embed_mode) {
        fprintf(out, "uint64_t slots[%zu];\n", slot_count);
        fprintf(out, "static int tc_aot_initialized = 0;\n");
    } else {
        fprintf(out, "static uint64_t slots[%zu];\n", slot_count);
    }

    /* 2. 返回值变量 */
    for each non-void function:
        if (embed_mode)
            fprintf(out, "uint64_t tc_aot_ret_%d;\n", func_id);
        else
            fprintf(out, "static uint64_t tc_ret_%d;\n", func_id);

    /* 3. 函数声明 */
    for each function:
        fprintf(out, "%svoid tc_aot_func_%d(TcDiagnostic *diag);\n",
                embed_mode ? "" : "static ", func_id);

    /* 4. 函数定义（abort 模式条件化） */
    for each function:
        fprintf(out, "%svoid tc_aot_func_%d(TcDiagnostic *diag) {\n",
                embed_mode ? "" : "static ", func_id);
        fprintf(out, "    tc_aot_cur_diag = diag;\n");
        /* 生成函数体，错误 guard 使用 embed_mode 条件：*/
        /* if (embed_mode) → tc_aot_embed_abort; else → tc_aot_abort */

    /* 5. static var 初始化 */
    if (embed_mode)
        fprintf(out, "int tc_aot_init(TcDiagnostic *diag) {\n    ...\n}\n");
    else
        fprintf(out, "static void tc_init_static_vars(TcDiagnostic *diag) {\n    ...\n}\n");

    /* 6. 函数表（仅 embed_mode） */
    if (embed_mode) {
        tc_aot_emit_func_table(out, program);
    }

    /* 7. 清理函数（仅 embed_mode） */
    if (embed_mode) {
        fprintf(out, "void tc_aot_cleanup(void) {\n");
        fprintf(out, "    tc_aot_memblock_heap_free_all();\n");
        fprintf(out, "    tc_aot_struct_heap_free_all();\n");
        fprintf(out, "}\n");
    }

    /* 8. main()（非 embed_mode） */
    if (!embed_mode) {
        fprintf(out, "int main(void) {\n    ...\n}\n");
    }

    return 0;
}
```

#### 15.7.3 运行时 shim 改动

```c
/* src/aot/tc_aot_rt.h 新增 */

/* 嵌入模式全局错误标记 */
extern int tc_aot_embed_error_flag;

/* 嵌入模式非致命 abort：设置 diag + 标记，不 exit */
#define tc_aot_embed_abort(diag, line) do { \
    tc_aot_cur_diag = (diag); \
    tc_aot_embed_error_flag = 1; \
} while (0)
```

```c
/* src/aot/tc_aot_rt.c 新增 */

int tc_aot_embed_error_flag = 0;
```

生成的错误 guard 模式（嵌入模式）：

```c
/* embed_mode = 1 时生成的错误 guard */
if (tc_aot_arith(..., &tc_aot_cur_diag, line) != 0) {
    tc_aot_embed_abort(tc_aot_cur_diag, line);
    return;
}
```

在 void 函数中，`return` 即可；在有返回值的函数中，guard 后面需要额外处理（如 `return 0`），但函数签名是 `void` 返回类型（返回值通过 `tc_ret_N` 全局变量），所以直接 `return` 即可。

---

### 15.8 完整使用示例（AOT 嵌入模式）

#### 15.8.1 TC 源码

`mylib.tc`：

```tc
#lib

public func add(a: int32, b: int32) -> int32 {
    return a + b
}

public func scale(data: ptr<int32>, n: int32, factor: int32) {
    var i: int32 = 0
    while (i < n) {
        ptr_store(data + i, ptr_load(data + i) * factor)
        i = i + 1
    }
}
```

#### 15.8.2 编译 TC 为 AOT 嵌入库

```bash
tc-aot --embed mylib.tc -o mylib_tc.c -H mylib_tc.h
```

#### 15.8.3 C 宿主程序（使用 tc_embed 通用 API）

```c
/* host.c */
#include <stdio.h>
#include "tc_diagnostic.h"
#include "tc_embed.h"
#include "mylib_tc.h"         /* AOT 生成的声明 */

int main(void) {
    TcDiagnostic diag;
    TcTypedProgram prog;

    /* 1. 编译 TC 源码 */
    tc_diagnostic_init(&diag);
    if (tc_compile_file("mylib.tc", &prog, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        return 1;
    }

    /* 2. 创建 AOT 嵌入上下文 */
    TcEmbedCtx *ctx = tc_embed_create_aot(
        slots, TC_AOT_SLOT_COUNT,   /* AOT 生成的全局 slots[] */
        tc_aot_func_table,          /* AOT 生成的函数表（哨兵终止） */
        tc_aot_init,                /* AOT 生成的初始化函数 */
        &prog,                      /* 用于提取函数元数据 */
        &diag);
    if (!ctx) {
        fprintf(stderr, "init failed: %s\n", diag.message);
        tc_typed_program_free(&prog);
        return 1;
    }

    /* ── 调用 add（标量参数） ── */
    TcValue args[2];
    args[0] = tc_value_from_int32(3);
    args[1] = tc_value_from_int32(4);
    TcValue result;

    if (tc_embed_call(ctx, "mylib", "add", 2, args, &result) != 0) {
        fprintf(stderr, "add failed: %s\n", tc_embed_get_error(ctx));
    } else {
        int64_t sum;
        tc_value_to_int64(result, &sum);
        printf("3 + 4 = %lld\n", (long long)sum);  /* 输出: 3 + 4 = 7 */
    }

    /* ── 调用 scale（ptr 参数） ── */
    const TcEmbedFuncInfo *info = tc_embed_func_info(ctx, "mylib", "scale");

    /* 在 slots 中平铺数据 */
    int data_slot = info->param_slots[0];  /* data: ptr<int32> */
    int32_t data[] = {1, 2, 3, 4, 5};
    for (int i = 0; i < 5; i++) {
        tc_embed_slot_write(ctx, data_slot + i,
                            tc_value_from_int32(data[i]));
    }

    TcValue scale_args[3];
    scale_args[0] = tc_embed_ptr_encode(data_slot);  /* ptr<int32> */
    scale_args[1] = tc_value_from_int32(5);           /* n = 5 */
    scale_args[2] = tc_value_from_int32(2);           /* factor = 2 */

    if (tc_embed_call(ctx, "mylib", "scale", 3, scale_args, NULL) != 0) {
        fprintf(stderr, "scale failed: %s\n", tc_embed_get_error(ctx));
    } else {
        /* 直接从 slots 读取结果 — ptr_store 后数据已在槽中 */
        printf("scaled: ");
        for (int i = 0; i < 5; i++) {
            TcValue v;
            tc_embed_slot_read(ctx, data_slot + i, &v);
            printf("%lld ", (long long)v.bits);
        }
        printf("\n");  /* 输出: scaled: 2 4 6 8 10 */
    }

    /* 清理（先销毁 ctx，再释放 program，最后调 AOT 清理） */
    tc_embed_destroy(ctx);
    tc_typed_program_free(&prog);
    tc_aot_cleanup();
    tc_diagnostic_clear(&diag);

    return 0;
}
```

---

### 15.9 性能分析

#### 15.9.1 调用开销分解

| 步骤 | VM 模式耗时 | AOT 模式耗时 | 加速 |
| ---- | ----------- | ------------ | ---- |
| 函数名查找 | O(n) 符号表遍历 | O(n) 函数表遍历（可优化为 O(log n)） | ~1x |
| 实参写入 | N 次 `TcValue` 赋值 | N 次 `uint64_t` 赋值 | ~1x |
| 函数调度 | `tc_exec_call_function`：帧管理 + AST 解释循环 | 直接 C 函数调用 (`call` 指令) | **10–100x** |
| 语句执行 | 每语句 `switch(stmt->kind)` + 解释 | 原生 C 编译代码 | **10–100x** |
| 表达式求值 | RHS 分发 + shim 调用 | 内联算术 + shim 调用 | 2–5x |
| 错误传播 | `TcExecControl` 控制流 | 返回 + diag 检查 | ~2x |
| 返回值 | `TcExecControl.return_value` | 读 `tc_ret_N` | ~1x |

**关键**：AOT 最大的优势在于**消除了解释循环**——每个 `var`、赋值、`while` 条件、`if` 分支都不再需要 `switch(stmt->kind)` 和 AST 遍历，而是原生编译的 C 控制流。

#### 15.9.2 典型场景预估

| 场景 | VM 模式 | AOT 模式 | 加速比 |
| ---- | ------- | -------- | ------ |
| 简单算术函数（add） | ~2µs | ~0.05µs | **40x** |
| while 循环 1000 次 | ~500µs | ~5µs | **100x** |
| ptr 数组遍历 10000 次 | ~5000µs | ~50µs | **100x** |
| 嵌套 funcall（3 层） | ~10µs | ~0.2µs | **50x** |

*注：具体数值因平台和编译器优化级别而异，比例关系反映消除解释开销后的数量级差异。*

---

### 15.10 验证策略

#### 15.10.1 差分测试

AOT 嵌入模式的正确性核心证据：同一 TC 源文件，通过 VM Embed 和 AOT Embed 调用同一函数，产生完全相同的：

- 返回值（位模式）
- 通过 `ptr_store` 写入的副作用数据
- 错误种类和关键消息（发生错误时）
- `static var` 持久化行为

测试框架：

```c
/* test_aot_vm_equivalence.c */
void test_aot_vm_behavior(const char *tc_source) {
    /* 1. 编译 → VM Embed */
    tc_compile_source(tc_source, "test", &prog, &diag);
    TcEmbedCtx *vm_ctx = tc_embed_create(&prog, &diag);

    /* 2. 编译 → AOT Embed */
    tc_compile_source(tc_source, "test", &prog2, &diag);
    tc_aot_emit_c(aot_file, &prog2, "test", 1 /* embed_mode */);
    /* 编译 AOT 产物 → 动态库 → dlopen 加载 */
    TcEmbedCtx *aot_ctx = tc_embed_create_aot(...);

    /* 3. 相同输入 → 相同输出 */
    for each test case:
        vm_result = tc_embed_call(vm_ctx, ...);
        aot_result = tc_embed_call(aot_ctx, ...);
        assert(vm_result == aot_result);
        assert(vm_return_slots_equal);
        assert(vm_side_effects_equal);
}
```

#### 15.10.2 AOT 嵌入专项测试

| 测试 | 验证点 |
| ---- | ------ |
| `test_aot_embed_init_cleanup` | `tc_aot_init` 幂等性，`tc_aot_cleanup` 后可重新 init |
| `test_aot_embed_call_scalar` | 各标量类型参数和返回值 |
| `test_aot_embed_call_ptr_array` | C 侧平铺数据→ptr→TC ptr_load/ptr_store→C 读回 |
| `test_aot_embed_call_nested` | AOT 内部 funcall 嵌套 |
| `test_aot_embed_error` | 除零等运行时错误不终止进程，正确返回错误码 |
| `test_aot_embed_static_var` | static var 在多次调用间持久化 |
| `test_aot_embed_vm_equiv` | 全量等价性测试，逐一与 VM Embed 对比 |

---

### 15.11 实施步骤

| 步骤 | 内容 | 改动文件 | 预计工作量 |
| ---- | ---- | -------- | ---------- |
| 1 | VM Embed 稳定（0.0.36） | — | 前置 |
| 2 | `tc_aot_embed_rt.h`：非致命 abort shim | 新文件 `src/aot/tc_aot_embed_rt.h` | 小 (~30 行) |
| 3 | `tc_aot_emit_c` 新增 `embed_mode` 参数 | `src/aot/tc_aot_codegen.c/h` | 中 (~80 行改动) |
| 4 | 函数表生成 | `src/aot/tc_aot_codegen.c` 新增 `tc_aot_emit_func_table` | 中 (~60 行) |
| 5 | 头文件生成 | `src/aot/tc_aot_codegen.c` 新增 `tc_aot_emit_embed_header` | 中 (~80 行) |
| 6 | CLI 新增 `--embed` 和 `-H` 选项 | `src/aot/main.c` | 小 (~30 行) |
| 7 | `tc_embed_create_aot` 实现 | `src/vm/embed/tc_embed_aot.c`（新文件） | 中 (~100 行) |
| 8 | `tc_embed_call` AOT 路径 | `src/vm/embed/tc_embed.c` 分支 | 小 (~20 行) |
| 9 | CMake 构建集成 | `CMakeLists.txt` | 小 |
| 10 | 差分测试框架 | `tests/unit/runtime/test_embed_aot.c` | 大 (~200 行) |

---

### 15.12 与 VM 模式的 API 兼容性总表

核心承诺：C 宿主程序调用 tc_embed 的代码**在 VM 和 AOT 之间可以无缝切换**。

| API | VM 模式 | AOT 模式 |
| --- | ------- | -------- |
| `tc_embed_create` | 从 `TcTypedProgram` 分配 slots + Executor | N/A（使用 `tc_embed_create_aot`） |
| `tc_embed_create_aot` | N/A | 从 AOT 全局数据创建，不依赖 `TcTypedProgram` |
| `tc_embed_destroy` | 释放 slots + heap + 索引 | 释放索引 + ctx，不释放 slots（归 AOT 全局数据） |
| `tc_embed_func_info` | 从 `TcSymbolTable` 查询 | 从 `tc_aot_func_table` 查询 |
| `tc_embed_call` | `tc_exec_call_function_public` | 直调 `tc_aot_func_N(diag)` |
| `tc_embed_slot_write` | `exec_ctx.slots[slot] = value` | `aot_slots[slot] = value.bits` |
| `tc_embed_slot_read` | `*out = exec_ctx.slots[slot]` | `out->bits = aot_slots[slot]` |
| `tc_embed_ptr_encode` | `(slot << 1) \| 1` | 完全相同的编码 |
| `tc_embed_get_error` | 读 `ctx->diag.message` | 完全相同 |
| `tc_embed_had_error` | 读 `ctx->error_flag` | 完全相同 |
| 额外清理 | 无 | `tc_aot_cleanup()`（释放 memblock/struct 堆） |

---

## 16. 运行时便捷层（v0.0.36 扩展）

> **状态**：已落地 v0.0.36。在 §7–§9 的内核 API 之上新增一层运行时便捷 API，隐藏 `TcValue` 桥接样板与槽位布局细节。VM / AOT 两模式透明（全部经通用 `tc_embed_call` 路径）。

### 16.1 设计动机

C 调用 TC 的基本方案可行，但将以下细节暴露给 C 宿主程序，显得繁琐：

| 繁琐点 | 现状示例 |
| ------ | -------- |
| 参数构造样板 | 每参 `tc_value_from_int32(...)`，返回再 `tc_value_to_int64(...)`，类型一一手工对应 |
| 数组互操作泄漏槽位布局 | 查 `param_slots` → 手工算 `data_base` → 循环 `tc_embed_slot_write` 平铺 → `tc_embed_ptr_encode` |
| 字符串函数名 + 手工 nargs | `tc_embed_call(ctx, "stats", "sum", 2, args, &result)`，参数个数/顺序无编译期检查 |
| 错误检查重复 | 每个调用判断返回值并取 `tc_embed_get_error` |

便捷层不改变内核 API，也不触碰编译器管线：全部由 `tc_embed` 模块在运行时完成。

### 16.2 新增 API 总览

| API | 职责 |
| --- | ---- |
| `TcEmbedArg` + `tc_embed_arg_*` | 携带类型标签的实参，位模式与 `TcValue.bits` 一致 |
| `TC_EMBED_ARGS(...)` | 由参数列表自动推导 nargs 的宏 |
| `tc_embed_tmp_begin` / `tc_embed_tmp_end` | 临时槽位区栈式分配/释放 |
| `tc_embed_make_ptr` | C 数组一键平铺为 `ptr<T>` |
| `tc_embed_call_typed` | 签名感知的类型化调用（校验参数个数与类型） |

### 16.3 类型化参数 TcEmbedArg

```c
typedef struct {
    TcTypeTag type; /* 宿主投影：仅标量/void 标签；完整类型事实在 TcValue.type */
    uint64_t bits;  /* 与 TcValue.bits 一致（按位宽规范化） */
} TcEmbedArg;

static inline TcEmbedArg tc_embed_arg_i32(int32_t v) {
    TcEmbedArg a = { TC_INT32, tc_value_from_int32(v).bits };
    return a;
}
/* i8/u8/i16/u16/i32/u32/i64/u64/isize/usize/f32/f64/bool 同理；
 * tc_embed_arg_ptr(slot) 直接构造 ptr；tc_embed_arg_value(TcValue) 包装既有值 */
```

`TcEmbedArg` / `TcEmbedFuncInfo` 的 `TcTypeTag`（或 `int` 存标签）是 **C 宿主 ABI 投影**，仅覆盖标量与「仅标签」场景；内部槽位与 `tc_embed_call` 路径仍使用 `TcValue`（`const TcType*`）。复合类型互操作走专用 API / reserved，不得假设 `TcEmbedArg.type` 能表达 `ptr`/`memblock`/`struct` 参数。

`tc_embed_arg_*` 复用 `tc_value_bridge.h` 的位模式，保证与 `TcValue` 槽位值完全一致。

### 16.4 临时槽位区

```c
int  tc_embed_tmp_begin(TcEmbedCtx *ctx, size_t n, int *base_slot_out);
void tc_embed_tmp_end(TcEmbedCtx *ctx);
```

- 从槽位数组末端向下分配（`base = tmp_top - n`），栈式嵌套，`end` 回退最近一层。
- `TcEmbedCtx` 内部维护 `tmp_top` / `tmp_marks[]` / `tmp_depth`，初始 `tmp_top = slot_count`。
- 最大嵌套深度 16（`TC_EMBED_TMP_MAX_DEPTH`）；不足时报错 `"temporary slot region exhausted"`。
- **注意**：临时区与符号槽位共享同一 `slots[]` 数组。模块符号槽位通常稀疏（从低端分配），临时区从末端分配，二者一般不冲突；若符号槽位密集，调用方须自行确保数据区不与活跃符号重叠。

### 16.5 C 数组一键映射为 ptr\<T\>

```c
int tc_embed_make_ptr(TcEmbedCtx *ctx, TcTypeTag elem_type,
                      const void *data, size_t count, TcValue *out);
```

- 内部在临时区平铺 `count` 个元素（按 `elem_type` 用 `memcpy` 读取，避免未对齐访问），再编码为 `ptr<T>`。
- `count == 0` 或 `data == NULL` 时返回 `nullptr`（bits = 0）。
- 支持元素类型：`TC_INT8..TC_UINT64`、`TC_BOOL`、`TC_FLOAT32/64`、`TC_ISIZE/USIZE`；其余拒绝。
- 平铺后调用方应在 TC 函数返回后调用 `tc_embed_tmp_end` 释放临时区。

### 16.6 签名感知的类型化调用

```c
int tc_embed_call_typed(TcEmbedCtx *ctx, const TcEmbedFuncInfo *info,
                        const TcEmbedArg *args, size_t nargs,
                        TcValue *result);
```

- `info == NULL` 报 `"function not found"`。
- `nargs != info->param_count` 报参数个数错误。
- 逐参校验 `args[i].type == param_types[i]`，不匹配报期望/实际类型。
- 内部组装 `TcValue` 数组后复用 `tc_embed_call`，VM / AOT 双路径天然透明。

nargs 自动推导宏：

```c
#define TC_EMBED_ARGS(...) \
    ((TcEmbedArg[]){ __VA_ARGS__ }), \
    (sizeof((TcEmbedArg[]){ __VA_ARGS__ }) / sizeof(TcEmbedArg))
```

实参须为无副作用的纯字面量构造（如 `tc_embed_arg_i32(3)`）。

### 16.7 使用示例

标量调用（此前约 10 行样板，现 4 行）：

```c
const TcEmbedFuncInfo *info = tc_embed_func_info(ctx, "math", "add");
TcValue result;
if (tc_embed_call_typed(ctx, info,
                        TC_EMBED_ARGS(tc_embed_arg_i32(3), tc_embed_arg_i32(4)),
                        &result) == 0) {
    int64_t sum;
    tc_value_to_int64(result, &sum);
}
```

数组互操作（此前 13 行手工平铺，现 3 行）：

```c
int32_t input[] = {1, 2, 3, 4, 5};
TcValue data_ptr;
tc_embed_make_ptr(ctx, TC_INT32, input, 5, &data_ptr);   /* 平铺 + ptr 编码 */

TcValue result;
tc_embed_call_typed(ctx, tc_embed_func_info(ctx, "stats", "sum"),
                    TC_EMBED_ARGS(tc_embed_arg_value(data_ptr)),
                    &result);
tc_embed_tmp_end(ctx);                                    /* 释放临时区 */
```

### 16.8 与现有 API 的关系与限制

- **兼容**：§7–§9 内核 API 不变；便捷层全部基于公共 API 实现，宿主程序可按需混用。
- **透明**：`tc_embed_make_ptr` / `tc_embed_call_typed` 在 VM 与 AOT 两模式行为一致（内部统一走 `tc_embed_slot_write` + `tc_embed_call`）。
- **限制**：
  - 临时区深度 ≤ 16，且与符号槽位共享数组（见 §16.4）。
  - `TC_EMBED_ARGS` 宏内实参不应含副作用表达式。
  - 便捷层仍以标量与 `ptr<T>` 数组为主；memblock / struct 互操作留给后续版本。
- **不触碰**：编译器管线、语言标准、AOT codegen 均无改动；新增测试见 `test_embed.c`（`test_embed_tmp_begin_end`、`test_embed_make_ptr_*`、`test_embed_call_typed_*`）。

---

*语言合法性与可观察语义以 [TC 语言标准 0.0.35](./TC语言标准设计说明书-0.0.35.md) 与 [TC 编译器标准 0.0.35](./TC编译器标准设计说明书-0.0.35.md) 为准。*

