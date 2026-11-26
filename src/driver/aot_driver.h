/* TC 0.0.46 tc-aot backend driver (design §11).
 *
 * The static front end is shared with tc-vm.  After IR finalization this
 * driver emits target assembly and, on request, assembles and links it with
 * the freestanding AArch64 runtime.  Toolchain failures are implementation/
 * usage errors, never language diagnostics; an unsupported platform instance
 * is reported as "unavailable" and no artifact is published.
 */
#ifndef TC_DRIVER_AOT_DRIVER_H
#define TC_DRIVER_AOT_DRIVER_H

#include <stddef.h>
#include <sys/stat.h>

#include "driver/cli.h"
#include "frontend/sem.h"
#include "runtime/profile.h"

typedef struct TcAotOutputs TcAotOutputs;
typedef struct TcAotStdio TcAotStdio;

/* Run code generation for an accepted analysis.  Returns a process exit
 * status: 0 on success, 2 on usage errors, 70 on I/O, OOM, unavailable
 * platform instances or toolchains, and 71 on internal failures. */
int tc_aot_driver_run(const TcAnalysis *analysis,
                      const TcPlatformProfile *profile,
                      const TcCliOptions *options);
int tc_aot_driver_run_with_inputs(const TcAnalysis *analysis,
                                  const TcPlatformProfile *profile,
                                  const TcCliOptions *options,
                                  const char *const *extra_inputs,
                                  size_t nextra_inputs);

typedef struct {
    int fd;
    int created;
    char *path;
    struct stat identity;
} TcAotOutput;

#define TC_AOT_OUTPUT_DUMP 0u
#define TC_AOT_OUTPUT_TRACE 1u
#define TC_AOT_OUTPUT_STATS 2u
#define TC_AOT_OUTPUT_RESULT 3u
#define TC_AOT_OUTPUT_COUNT 4u

struct TcAotOutputs {
    TcAotOutput output[TC_AOT_OUTPUT_COUNT];
};

void tc_aot_outputs_init(TcAotOutputs *outputs);
TcImplError tc_aot_outputs_discard(TcAotOutputs *outputs);
TcImplError tc_aot_outputs_release(TcAotOutputs *outputs);
TcImplError tc_aot_outputs_release_observations(TcAotOutputs *outputs);
TcImplError tc_aot_outputs_verify(const TcAotStdio *stdio,
                                  const TcAnalysis *analysis,
                                  const char *const *extra_inputs,
                                  size_t nextra_inputs,
                                  int aot,
                                  const TcAotOutputs *outputs);

typedef struct {
    int open;
    struct stat identity;
} TcAotStdioDescriptor;

struct TcAotStdio {
    TcAotStdioDescriptor descriptors[3];
};

int tc_aot_driver_run_with_stdio(const TcAotStdio *stdio,
                                 const TcAnalysis *analysis,
                                 const TcPlatformProfile *profile,
                                 const TcCliOptions *options,
                                 const char *const *extra_inputs,
                                 size_t nextra_inputs);
int tc_aot_driver_run_prepared(const TcAotStdio *stdio,
                               const TcAnalysis *analysis,
                               const TcPlatformProfile *profile,
                               const TcCliOptions *options,
                               const char *const *extra_inputs,
                               size_t nextra_inputs,
                               TcAotOutputs *outputs);
void tc_aot_stdio_capture(TcAotStdio *stdio);
TcImplError tc_aot_driver_stderr_guard(const TcAotStdio *stdio,
                                       const TcAnalysis *analysis,
                                       TcAnalysisStatus analysis_status,
                                       const char *const *extra_inputs,
                                       size_t nextra_inputs,
                                       const char *source,
                                       const char *const *include_roots,
                                       size_t ninclude_roots);
TcImplError tc_aot_driver_guard(const TcAotStdio *stdio,
                                const TcAnalysis *analysis,
                                TcAnalysisStatus analysis_status,
                                const TcCliOptions *options,
                                const char *const *extra_inputs,
                                size_t nextra_inputs,
                                const char *source,
                                const char *const *include_roots,
                                size_t ninclude_roots,
                                int aot,
                                int create_output);
TcImplError tc_aot_outputs_preflight(const TcAotStdio *stdio,
                                     const TcAnalysis *analysis,
                                     TcAnalysisStatus analysis_status,
                                     const TcCliOptions *options,
                                     const char *const *extra_inputs,
                                     size_t nextra_inputs,
                                     const char *source,
                                     const char *const *include_roots,
                                     size_t ninclude_roots,
                                     int aot, int create_result,
                                     TcAotOutputs *outputs);

#endif /* TC_DRIVER_AOT_DRIVER_H */
