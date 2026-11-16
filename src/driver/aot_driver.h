/* TC 0.0.46 tc-aot backend driver (design §11).
 *
 * The static front end is shared with tc-vm.  After IR finalization this
 * driver emits the AArch64 subset assembly and, on request, assembles and
 * links it with the freestanding AArch64 runtime.  Toolchain failures are
 * implementation/usage errors, never language diagnostics; an unsupported
 * opcode is reported as "unavailable" and no artifact is published.
 */
#ifndef TC_DRIVER_AOT_DRIVER_H
#define TC_DRIVER_AOT_DRIVER_H

#include "driver/cli.h"
#include "frontend/sem.h"
#include "runtime/profile.h"

/* Run code generation for an accepted analysis.  Returns a process exit
 * status: 0 on success, 2 on usage errors, 70 when the program is outside the
 * implemented subset or the toolchain is unavailable, 71 on internal/I/O
 * failures. */
int tc_aot_driver_run(const TcAnalysis *analysis,
                      const TcPlatformProfile *profile,
                      const TcCliOptions *options);

#endif /* TC_DRIVER_AOT_DRIVER_H */
