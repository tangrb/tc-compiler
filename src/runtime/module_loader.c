/* TC 0.0.46 contract: module loader absent instance. */
#include "runtime/module_loader.h"

static TcModuleStatus absent_resolve(void *ctx, const char *importer_path,
                                     const char *module_name, const char **out_path) {
    (void)ctx;
    (void)importer_path;
    (void)module_name;
    (void)out_path;
    return TC_MODULE_ERROR;
}

const TcModuleLoader tc_module_loader_absent = {absent_resolve, 0};
