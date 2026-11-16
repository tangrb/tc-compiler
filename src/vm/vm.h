/* TC 0.0.46 reference VM (design §9).
 *
 * The VM executes one finalized `TcIR` bound to its own platform profile.  It
 * implements the abstract machine of standard §1.2: five regions, storage
 * objects with field sub-objects, pointer states, the 15 runtime traps, call
 * depth D, all-or-nothing I/O records and commit semantics (standard §12.5.2).
 * Mandatory traps run in every build; `checked_build` only adds UB access
 * checks and never changes the defined-program set.
 *
 * This is an internal execution API used by `tc-vm` and the test suite.  It is
 * not a public embed API and is not installed.
 */
#ifndef TC_VM_VM_H
#define TC_VM_VM_H

#include <stdint.h>

#include "diagnostic/diagnostic.h"
#include "ir/ir.h"
#include "ir/span.h"
#include "runtime/profile.h"
#include "runtime/trap.h"

typedef struct {
    int trapped;
    TcErrorKind kind; /* valid only when trapped != 0 */
    TcSpan span;      /* triggering instruction */
} TcVmResult;

typedef enum {
    TC_VM_STATUS_OK = 0, /* program reached its natural end */
    TC_VM_STATUS_TRAP,
    TC_VM_STATUS_IMPL /* implementation/API failure, never a language code */
} TcVmStatus;

/* Execute a program.  The profile must pass tc_profile_validate_runtime();
 * `ir->profile` must be the same profile instance or an equal copy.  The VM
 * calls profile->trap->trap once for the first trap. */
TcVmStatus tc_vm_run(const TcIR *ir, const TcPlatformProfile *profile,
                     TcVmResult *out_result, TcImplError *out_impl);

const char *tc_vm_status_symbol(TcVmStatus status);

#endif /* TC_VM_VM_H */
