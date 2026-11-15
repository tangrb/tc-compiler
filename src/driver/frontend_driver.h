/* TC 0.0.46 driver: S1 front-end invocation (LEX/SYN only).
 *
 * The tools are the only entry points.  S1 provides LEX/SYN; later phases
 * (IMPORT/SEM/CT/codegen) are still explicitly unavailable and must never be
 * reported as supported.  `--syntax-only` is a deliberately partial check:
 * exit 0 means "no LEX/SYN diagnostic", not "a valid program".  The default
 * invocation reports the same LEX/SYN diagnostic and then exits 70
 * (unavailable) when the front-end accepts the source.
 */
#ifndef TC_DRIVER_FRONTEND_DRIVER_H
#define TC_DRIVER_FRONTEND_DRIVER_H

#include "driver/cli.h"

/* Run the S1 front-end for `tool_name`.  Returns the process exit status:
 *   0  --syntax-only accepted the source (partial check only)
 *   1  a LEX/SYN diagnostic was printed to stderr
 *   70 the front-end accepted the source but later phases are unavailable
 *   71 implementation/API failure (I/O, OOM, limit, internal)
 * `tool_name` is used in stderr messages and must be "tc-vm" or "tc-aot". */
int tc_driver_frontend(const char *tool_name, const TcCliOptions *options);

#endif /* TC_DRIVER_FRONTEND_DRIVER_H */
