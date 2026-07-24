/*
 * tc_call_frame.c — 调用帧 push/pop
 */
#include "tc_call_frame.h"

#include <stdlib.h>
#include <string.h>

int tc_call_frame_push(TcCallFrame **top, int func_id) {
    TcCallFrame *frame = NULL;

    if (!top) {
        return -1;
    }
    frame = (TcCallFrame *)malloc(sizeof(TcCallFrame));
    if (!frame) {
        return -1;
    }
    memset(frame, 0, sizeof(*frame));
    frame->func_id = func_id;
    frame->prev = *top;
    *top = frame;
    return 0;
}

void tc_call_frame_pop(TcCallFrame **top) {
    TcCallFrame *frame = NULL;

    if (!top || !*top) {
        return;
    }
    frame = *top;
    *top = frame->prev;
    free(frame);
}
