#ifndef TC_VM_GDB_H
#define TC_VM_GDB_H

#include "ir/linemap.h"
#include "runtime/profile.h"
#include "vm/observe.h"
#include "vm/vm.h"

typedef struct {
    const char *stdout_journal;
    int allow_write;
    unsigned gdb_port;
} TcGdbOptions;

TcVmStatus tc_vm_run_gdb(const TcIR *ir, const TcPlatformProfile *profile,
                         TcVmResult *out_result, TcImplError *out_impl,
                         const TcVmObserver *observer, TcVmStats *out_stats,
                         const TcLineMap *linemap,
                         const TcGdbOptions *options);

#endif
