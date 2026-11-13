/* TC 0.0.46 contract: zero-pass baseline implementation. */
#include "contract/optimizer.h"

#include <string.h>

int tc_pipeline_build(TcOptLevel level, TcPassPipeline *out) {
    if (out == NULL || (level != TC_OPT_O0 && level != TC_OPT_O2)) {
        return 0;
    }
    out->level = level;
    out->pass_count = 0;
    return 1;
}

int tc_pipeline_is_zero_pass(const TcPassPipeline *pipeline) {
    return pipeline != NULL && pipeline->pass_count == 0;
}

int tc_pipeline_levels_equivalent(const TcPassPipeline *a, const TcPassPipeline *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return tc_pipeline_is_zero_pass(a) && tc_pipeline_is_zero_pass(b);
}

int tc_opt_level_parse(const char *text, TcOptLevel *out) {
    if (text == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(text, "O0") == 0 || strcmp(text, "-O0") == 0) {
        *out = TC_OPT_O0;
        return 1;
    }
    if (strcmp(text, "O2") == 0 || strcmp(text, "-O2") == 0) {
        *out = TC_OPT_O2;
        return 1;
    }
    return 0;
}
