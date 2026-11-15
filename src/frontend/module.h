/* TC 0.0.46 module discovery and IMPORT facts (standard §4.5/§4.11).
 *
 * The loader performs the LEX/SYN closure in import source order (depth
 * first), fixes the module order, and records IMPORT-stage candidates without
 * reporting them.  IMPORT diagnostics are emitted only after the whole
 * closure has passed LEX/SYN, so a later module's LEX/SYN error still wins
 * (standard §1.8/§12.2 stage priority).
 *
 * Module identity is the resolved file path: equal names at different paths
 * are distinct instances, and one resolved instance has one static storage.
 */
#ifndef TC_FRONTEND_MODULE_H
#define TC_FRONTEND_MODULE_H

#include <stddef.h>
#include <stdint.h>

#include "diagnostic/diagnostic.h"
#include "frontend/diag.h"
#include "frontend/input.h"
#include "frontend/parse.h"

#define TC_MODULE_NO_TARGET UINT32_MAX

typedef struct {
    uint32_t id;             /* module_order index */
    const char *path;        /* resolved file path (module instance key) */
    TcParseModule *parse;
    uint32_t *import_targets; /* per import; TC_MODULE_NO_TARGET if unresolved */
    size_t nimports;
} TcModuleInfo;

typedef struct {
    TcModuleInfo *modules;
    size_t nmodules;
    TcArena arena;              /* canonical paths and owned strings */
    TcDiagBuffer *import_diags; /* IMPORT-stage candidates, not yet reported */
    TcFrontendDiag lexsyn;      /* valid when has_lexsyn */
    int has_lexsyn;
    int root_not_program;       /* API error, never a language diagnostic */
    TcImplError impl;
} TcModuleGraph;

/* Load the root module and its transitive imports.  `include_roots` are the
 * environment-defined search roots after the importing file's directory.
 * Returns NULL on implementation failure (graph->impl) or LEX/SYN failure
 * (graph->has_lexsyn); the graph is still usable for the diagnostic and must
 * be destroyed by the caller. */
TcModuleGraph *tc_module_graph_load(const char *root_path,
                                    const char *const *include_roots,
                                    size_t ninclude_roots);
void tc_module_graph_destroy(TcModuleGraph *graph);

/* Resolve `import name` for `importer_path`, writing the canonical path to the
 * arena and returning 1.  Returns 0 when no candidate file exists. */
int tc_module_resolve_import(TcArena *arena, const char *importer_path,
                             const char *name,
                             const char *const *include_roots,
                             size_t ninclude_roots, const char **out_path);

#endif /* TC_FRONTEND_MODULE_H */
