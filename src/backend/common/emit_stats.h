#ifndef TC_BACKEND_COMMON_EMIT_STATS_H
#define TC_BACKEND_COMMON_EMIT_STATS_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    TC_EMIT_TARGET_FIXED4,
    TC_EMIT_TARGET_RISCV32
} TcEmitTarget;

typedef struct {
    uint64_t code_bytes;
    TcEmitTarget target;
    int active;
    int in_text;
    char *pending;
    size_t pending_size;
    size_t pending_capacity;
} TcEmitCounter;

typedef struct {
    uint64_t frame_max;
    uint64_t code_bytes;
} TcTargetStats;

int tc_emit_stats_write(FILE *out, const char *format, va_list args,
                        TcEmitCounter *counter);
void tc_emit_stats_free(TcEmitCounter *counter);

#endif /* TC_BACKEND_COMMON_EMIT_STATS_H */
