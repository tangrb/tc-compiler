#ifndef TC_DRIVER_OBSERVE_H
#define TC_DRIVER_OBSERVE_H

#include <stdint.h>
#include <stdio.h>

#include "backend/common/emit_stats.h"
#include "driver/aot_driver.h"
#include "frontend/sem.h"
#include "ir/linemap.h"
#include "runtime/profile.h"
#include "vm/observe.h"

typedef struct {
    FILE *file;
    int failed;
    int close_file;
} TcObserveStream;

typedef struct {
    TcObserveStream stream;
    const TcIR *ir;
    uint64_t funcs;
    uint64_t ir_instrs;
    uint64_t static_bytes;
} TcAotObserveStats;

int tc_observe_json_string(FILE *out, const char *value);
TcImplError tc_observe_dump_ir(const TcIR *ir, TcAotOutputs *outputs);
TcImplError tc_observe_stats_begin_ir(const TcIR *ir,
                                      const TcPlatformProfile *profile,
                                      const TcCliOptions *options,
                                      TcAotOutputs *outputs,
                                      TcAotObserveStats *stats);
TcImplError tc_observe_stats_finish_aot(TcAotObserveStats *stats,
                                        const TcTargetStats *target);
TcImplError tc_observe_stats_vm(const TcCliOptions *options,
                                const TcPlatformProfile *profile,
                                const TcVmStats *stats, TcAotOutputs *outputs);
TcImplError tc_observe_trace_begin(TcAotOutputs *outputs,
                                   TcObserveStream *stream);
TcImplError tc_observe_stream_finish(TcObserveStream *stream);
void tc_observe_instr_name(const TcVmResult *result, const TcLineMap *linemap,
                           char *buffer, size_t capacity);

#endif /* TC_DRIVER_OBSERVE_H */
