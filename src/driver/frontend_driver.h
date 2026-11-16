/* TC 0.0.46 driver: static front-end invocation.
 *
 * The tools are the only entry points.  The front end provides
 * LEX/SYN/IMPORT/SEM/CT plus IR finalization; code generation is still
 * explicitly unavailable and must never be reported as supported.
 * `--syntax-only` (LEX/SYN), `--sem-only` (through SEM) and `--static-only`
 * (through CT and IR finalization) are partial checks: exit 0 never means
 * "a valid program".  A normal invocation prints the first diagnostic or exits
 * 70 (unavailable) when the implemented static stages accept the source.
 */
#ifndef TC_DRIVER_FRONTEND_DRIVER_H
#define TC_DRIVER_FRONTEND_DRIVER_H

#include "driver/cli.h"

/* Run the implemented static front end for `tool_name`.  Returns the process
 * exit status:
 *   0  --syntax-only/--sem-only/--static-only accepted (partial check only)
 *   1  a LEX/SYN/IMPORT/SEM diagnostic was printed to stderr
 *   2  CLI/API usage error (for example a non-#program root)
 *   70 the static stages accepted the source but later stages are unavailable
 *   71 implementation/API failure (I/O, OOM, limit, internal)
 * `tool_name` is used in stderr messages and must be "tc-vm" or "tc-aot". */
int tc_driver_frontend(const char *tool_name, const TcCliOptions *options);

#endif /* TC_DRIVER_FRONTEND_DRIVER_H */
