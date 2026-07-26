/*
 * tc_embed.h — TC 嵌入式运行时 API（v0.0.36）
 *
 * C 宿主程序调用 TC 编译产物的最小化运行时 API。
 * 通过共享 slots[] 数组实现零拷贝互操作。
 *
 * 支持两种模式：
 *   VM 模式（tc_embed_create）：基于 TcTypedProgram + Executor 解释执行。
 *   AOT 模式（tc_embed_create_aot）：基于 AOT 生成的 C 代码直调执行。
 *
 * 同一套 tc_embed_call / tc_embed_slot_* / tc_embed_ptr_* API 在两模式下 API 兼容。
 */
#ifndef TC_EMBED_H
#define TC_EMBED_H

#include <stddef.h>
#include <stdint.h>

#include "tc_types.h"           /* TcValue, TcTypeKind */
#include "tc_executor.h"        /* TcExecuteCtx, tc_exec_call_function_public */
#include "tc_analyzer.h"        /* TcTypedProgram */
#include "tc_diagnostic.h"      /* TcDiagnostic */

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
    int return_type;            /* TcTypeKind；void 返回时无意义 */
    int *param_slots;           /* 各形参的 slot 号，长度 param_count */
    int *param_types;           /* 各形参的 TcTypeKind */
    size_t param_count;
} TcEmbedFuncInfo;

/* ── AOT 函数表条目（与 AOT codegen 生成的表结构一致） ── */
#ifndef TC_AOT_FUNC_ENTRY_T_DEFINED
#define TC_AOT_FUNC_ENTRY_T_DEFINED
typedef void (*tc_aot_func_entry_t)(TcDiagnostic *diag);
#endif

#ifndef TC_AOT_FUNC_ENTRY_DEFINED
#define TC_AOT_FUNC_ENTRY_DEFINED
typedef struct {
    int func_id;
    tc_aot_func_entry_t entry;
    uint64_t *ret_ptr;          /* 指向 tc_aot_ret_N 全局变量（void 函数为 NULL） */
} tc_aot_func_entry;
#endif

/* ── 生命周期 ── */

/** VM 模式：从编译产物创建 embed 上下文 */
TcEmbedCtx *tc_embed_create(const TcTypedProgram *program, TcDiagnostic *diag);

/** AOT 模式：从 AOT 生成的全局 slots + 函数表创建 embed 上下文 */
TcEmbedCtx *tc_embed_create_aot(uint64_t *slots, size_t slot_count,
                                 const tc_aot_func_entry *func_table,
                                 int (*init_fn)(TcDiagnostic *diag),
                                 const TcTypedProgram *program,
                                 TcDiagnostic *diag);

void tc_embed_destroy(TcEmbedCtx *ctx);

/* ── 符号查询 ── */
const TcEmbedFuncInfo *tc_embed_func_info(const TcEmbedCtx *ctx,
                                           const char *module,
                                           const char *func_name);

int tc_embed_top_var_slot(const TcEmbedCtx *ctx, const char *name);
int tc_embed_self_var_slot(const TcEmbedCtx *ctx, const char *name);

/* ── 槽位信息与直接读写 ── */
size_t tc_embed_slot_count(const TcEmbedCtx *ctx);
int tc_embed_slot_write(TcEmbedCtx *ctx, int slot, TcValue value);
int tc_embed_slot_read(const TcEmbedCtx *ctx, int slot, TcValue *out);

/* ── ptr<T> 编码 ── */
static inline TcValue tc_embed_ptr_encode(int slot) {
    TcValue v;
    v.type = TC_PTR;
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
