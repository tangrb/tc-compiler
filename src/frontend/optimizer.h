/* TC 0.0.46 contract: zero-pass optimization baseline (design §10.1, §15).
 *
 * O0 and O2 both run zero IR passes in 0.0.46.  @fast/@fast_math promises and
 * @inline/@noinline are recorded on TcFuncFlags but not consumed.  SSA,
 * regalloc and inlining are not part of this baseline; introducing them first
 * requires a declared pass pipeline and standard §10.8 invariant tests.
 */
#ifndef TC_FRONTEND_OPTIMIZER_H
#define TC_FRONTEND_OPTIMIZER_H

#include <stddef.h>

typedef enum {
    TC_OPT_O0 = 0,
    TC_OPT_O2
} TcOptLevel;

typedef struct {
    TcOptLevel level;
    size_t pass_count; /* 0 in the 0.0.46 baseline */
} TcPassPipeline;

/* Returns 1 and fills out on success; 0 for an unknown level. */
int tc_pipeline_build(TcOptLevel level, TcPassPipeline *out);

/* A zero-pass pipeline performs no IR transformation. */
int tc_pipeline_is_zero_pass(const TcPassPipeline *pipeline);

/* O0 and O2 are semantically identical under the zero-pass baseline. */
int tc_pipeline_levels_equivalent(const TcPassPipeline *a, const TcPassPipeline *b);

/* Parse "O0"/"O2" (also accepts "-O0"/"-O2"); returns 0 on failure. */
int tc_opt_level_parse(const char *text, TcOptLevel *out);

#endif /* TC_FRONTEND_OPTIMIZER_H */
