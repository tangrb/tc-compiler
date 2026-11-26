/* TC 0.0.46 driver: front-end and reference-VM invocation.
 *
 * The compiler entry points are tc-vm and tc-aot.  The front end provides
 * LEX/SYN/IMPORT/SEM/CT plus IR finalization.  `--syntax-only` (LEX/SYN),
 * `--sem-only` (through SEM) and `--static-only` (through CT and IR
 * finalization) are partial checks: exit 0 never means "a valid program".
 * After the static stages accept the source, tc-vm executes the reference VM
 * (first trap as error[RT]) while tc-aot emits or links the selected native
 * target artifact.
 */
#ifndef TC_DRIVER_FRONTEND_DRIVER_H
#define TC_DRIVER_FRONTEND_DRIVER_H

#include "driver/cli.h"

/* Run the implemented static front end for `tool_name`.  Returns the process
 * exit status:
 *   0  partial check accepted, or tc-vm normal termination
 *   1  a static diagnostic was printed, or tc-vm reported its first runtime trap
 *   2  CLI/API usage error (for example a non-#program root)
 *   70 host I/O, unavailable capability or actual OOM
 *   71 internal contract violation or documented implementation limit
 * `tool_name` is used in stderr messages and must be "tc-vm" or "tc-aot". */
int tc_driver_frontend(const char *tool_name, const TcCliOptions *options);
int tc_driver_cli_guard(const TcCliOptions *options);
int tc_driver_report_cli_error(const char *tool_name,
                               const TcCliOptions *options);

#endif /* TC_DRIVER_FRONTEND_DRIVER_H */
