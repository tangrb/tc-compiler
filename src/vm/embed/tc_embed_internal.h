/*
 * tc_embed_internal.h — TC-Embed 内部结构共享声明
 *
 * 供 tc_embed.c（通用实现）和 tc_embed_aot.c（AOT 桥接）共享。
 * 不对外暴露 TcEmbedCtx 内部布局。
 */
#ifndef TC_EMBED_INTERNAL_H
#define TC_EMBED_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

#include "tc_types.h"
#include "tc_executor_internal.h"  /* TcExecuteCtx（完整定义） */
#include "tc_embed.h"              /* TcEmbedFuncInfo, tc_aot_func_entry */
#include "tc_analyzer.h"    /* TcTypedProgram */
#include "tc_diagnostic.h"  /* TcDiagnostic */

#ifdef __cplusplus
extern "C" {
#endif

/* ── 不透明上下文内部布局（对外仅 opaque 指针） ── */
struct TcEmbedCtx {
    int is_aot;                      /* 0 = VM 模式, 1 = AOT 模式 */

    /* VM 模式字段 */
    TcExecuteCtx exec_ctx;

    /* AOT 模式字段 */
    uint64_t *aot_slots;
    size_t aot_slot_count;
    const tc_aot_func_entry *aot_func_table;

    /* 通用字段 */
    const TcTypedProgram *program;
    TcEmbedFuncInfo *funcs;
    size_t func_count;
    TcDiagnostic diag;
    int error_flag;
    char error_message[512];
};

/* ── 内部辅助函数（供 tc_embed.c + tc_embed_aot.c 共享） ── */
void tc_embed_set_error(TcEmbedCtx *ctx, const char *msg);
int tc_embed_build_func_index(TcEmbedCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* TC_EMBED_INTERNAL_H */
