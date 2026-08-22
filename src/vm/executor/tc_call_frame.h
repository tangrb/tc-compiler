/*
 * tc_call_frame.h — 函数调用帧栈
 *
 * Analyzer 为形参/局部分配全局唯一槽，且递归为 CE；帧本身只跟踪调用链与返回值。
 */
#ifndef TC_CALL_FRAME_H
#define TC_CALL_FRAME_H

#include "tc_types.h"

typedef struct TcCallFrame {
    int func_id;
    int return_stmt_index; /* reserved / unused if using nested execute */
    TcValue return_value;
    int has_return_value;
    struct TcCallFrame *prev;
} TcCallFrame;

/**
 * Push a new call frame.
 * @return 0 on success; -1 on OOM (diag not set — caller must diagnose)
 */
int tc_call_frame_push(TcCallFrame **top, int func_id);
void tc_call_frame_pop(TcCallFrame **top);

#endif /* TC_CALL_FRAME_H */
