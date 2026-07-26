/*
 * tc_aot_embed_rt.h — AOT 嵌入模式运行时 shim
 *
 * 嵌入模式生成的 C 代码使用此头文件中的非致命 abort（设置错误标记而非 exit(1)），
 * 让控制流在运行时错误后返回给 tc_embed_call，而非终止宿主进程。
 */
#ifndef TC_AOT_EMBED_RT_H
#define TC_AOT_EMBED_RT_H

#include <stdint.h>
#include "tc_diagnostic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 函数表类型（AOT codegen 在嵌入模式生成该表） ── */
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

/* ── 全局变量（由 AOT codegen 生成，非 static 以暴露给宿主） ── */
extern TcDiagnostic *tc_aot_cur_diag;

/* 嵌入模式全局错误标记 — AOT 生成代码在出错时置 1 */
extern int tc_aot_embed_error_flag;

/* 替代 tc_aot_abort：设置 diag + 错误标记，不 exit(1) */
static inline void tc_aot_embed_abort(TcDiagnostic *diag, int line) {
    (void)line;
    tc_aot_cur_diag = diag;
    tc_aot_embed_error_flag = 1;
}

/* 每次调用前由 tc_embed_call 重置 */
static inline void tc_aot_embed_reset_error(void) {
    tc_aot_embed_error_flag = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* TC_AOT_EMBED_RT_H */
