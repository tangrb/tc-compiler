/*
 * tc_embed_aot.c — TC-Embed AOT 桥接实现
 *
 * 提供 tc_embed_create_aot：从 AOT 生成的全局数据创建嵌入上下文。
 * 分离自 tc_embed.c，与设计文档 §15.11 步骤 7 对应。
 */
#include "tc_embed_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include "tc_diagnostic.h"  /* tc_diagnostic_init, tc_diagnostic_clear, tc_diagnostic_set */

TcEmbedCtx *tc_embed_create_aot(uint64_t *slots, size_t slot_count,
                                 const tc_aot_func_entry *func_table,
                                 int (*init_fn)(TcDiagnostic *diag),
                                 const TcTypedProgram *program,
                                 TcDiagnostic *diag) {
    TcEmbedCtx *ctx = NULL;

    if (!program || !slots) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "invalid argument: program or slots is NULL");
        return NULL;
    }

    ctx = (TcEmbedCtx *)calloc(1, sizeof(TcEmbedCtx));
    if (!ctx) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return NULL;
    }

    ctx->is_aot = 1;
    ctx->program = program;
    ctx->aot_slots = slots;
    ctx->aot_slot_count = slot_count;
    ctx->aot_func_table = func_table;
    ctx->tmp_top = (int)slot_count;
    tc_diagnostic_init(&ctx->diag);

    /* 调用 AOT 初始化（初始化 slots + static var） */
    if (init_fn) {
        TcDiagnostic init_diag;
        tc_diagnostic_init(&init_diag);
        if (init_fn(&init_diag) != 0) {
            tc_diagnostic_set(diag, init_diag.domain, init_diag.line, init_diag.column,
                              init_diag.message);
            tc_diagnostic_clear(&init_diag);
            tc_embed_destroy(ctx);
            return NULL;
        }
        tc_diagnostic_clear(&init_diag);
    }

    /* 初始化 VM exec_ctx 为无害值（AOT 模式不使用 executor） */
    ctx->exec_ctx.slots = NULL;
    ctx->exec_ctx.program = NULL;
    ctx->exec_ctx.current_func_id = -1;

    /* 构建函数索引（同 VM 模式，从 TcTypedProgram 提取 metadata） */
    if (tc_embed_build_func_index(ctx) != 0) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          ctx->error_message);
        tc_embed_destroy(ctx);
        return NULL;
    }

    return ctx;
}
