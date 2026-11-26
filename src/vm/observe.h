#ifndef TC_VM_OBSERVE_H
#define TC_VM_OBSERVE_H

#include <stdint.h>
#include <stdio.h>

#include "diagnostic/diagnostic.h"
#include "ir/linemap.h"
#include "vm/vm.h"

#define TC_TRACE_IR (1u << 0)
#define TC_TRACE_CALL (1u << 1)
#define TC_TRACE_MEM (1u << 2)
#define TC_TRACE_IO (1u << 3)
#define TC_TRACE_TRAP (1u << 4)

#define TC_VM_OP_SLOT_COUNT ((size_t)TC_OPCODE_COUNT * 6u)

typedef struct {
    uint64_t executed;
    uint64_t depth_max;
    uint64_t region_peak[TC_REGION_COUNT];
    uint64_t allocations;
    uint64_t io_bytes;
    uint64_t op_counts[TC_VM_OP_SLOT_COUNT];
    int trapped;
    TcErrorKind first_trap;
} TcVmStats;

typedef struct {
    const TcLineMap *linemap;
    uint32_t kinds;
    uint64_t limit;
    int jsonl;
    FILE *trace;
    int stats;
    TcVmStats *stats_out;
} TcVmObserver;

TcVmStatus tc_vm_run_observed(const TcIR *ir, const TcPlatformProfile *profile,
                              TcVmResult *out_result, TcImplError *out_impl,
                              const TcVmObserver *observer,
                              TcVmStats *out_stats);

#endif
