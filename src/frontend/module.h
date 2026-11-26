/* TC 0.0.46 module discovery and IMPORT facts (standard §4.5/§4.11).
 *
 * The loader performs the LEX/SYN closure in import source order (depth
 * first), fixes the module order, and records IMPORT-stage candidates without
 * reporting them.  IMPORT diagnostics are emitted only after the whole
 * closure has passed LEX/SYN, so a later module's LEX/SYN error still wins
 * (standard §1.8/§12.2 stage priority).
 *
 * Module identity is the bound parent-directory and entry identity. Canonical
 * paths remain separate for import resolution and display-path calculation.
 */
#ifndef TC_FRONTEND_MODULE_H
#define TC_FRONTEND_MODULE_H

#include <stddef.h>
#include <stdint.h>

#include "base/path.h"
#include "diagnostic/diagnostic.h"
#include "frontend/diag.h"
#include "frontend/input.h"
#include "frontend/parse.h"

#define TC_MODULE_NO_TARGET UINT32_MAX

typedef struct {
    uint32_t id;             /* module_order index */
    const char *path;         /* canonical referent path */
    const char *identity;     /* bound parent directories and actual entry names */
    const char *display_path; /* project-root-relative output path */
    TcParseModule *parse;
    uint32_t *import_targets; /* per import; TC_MODULE_NO_TARGET if unresolved */
    size_t nimports;
} TcModuleInfo;

typedef struct {
    int fd;                  /* borrowed private root snapshot */
    int parent_fd;           /* borrowed canonical referent directory */
    const char *path;        /* original canonical entry path */
    const char *identity;    /* original bound entry identity */
    const int *include_fds;  /* borrowed, parallel to include_roots */
    size_t ninclude_fds;
} TcModuleRootSource;

typedef struct {
    TcModuleInfo *modules;
    size_t nmodules;
    TcArena arena;              /* canonical paths and owned strings */
    TcPathContext *paths;       /* borrowed shared display context */
    TcDiagBuffer *import_diags; /* IMPORT-stage candidates, not yet reported */
    TcFrontendDiag lexsyn;      /* valid when has_lexsyn */
    int has_lexsyn;
    int root_not_program;       /* API error, never a language diagnostic */
    TcImplError impl;
    const char *failed_display; /* arena-owned failing module display path */
    const char *failed_path;    /* canonical failing module path */
} TcModuleGraph;

/* Load the root module and its transitive imports.  `include_roots` are the
 * environment-defined search roots after the importing file's directory.
 * Returns NULL on implementation failure (graph->impl) or LEX/SYN failure
 * (graph->has_lexsyn); the graph is still usable for the diagnostic and must
 * be destroyed by the caller. */
TcModuleGraph *tc_module_graph_load(const char *root_path,
                                    const char *const *include_roots,
                                    size_t ninclude_roots);
TcModuleGraph *tc_module_graph_load_with_paths(
    const char *root_path, const char *const *include_roots,
    size_t ninclude_roots, TcPathContext *paths);
TcModuleGraph *tc_module_graph_load_source(
    const TcModuleRootSource *source, const char *const *include_roots,
    size_t ninclude_roots, TcPathContext *paths);
void tc_module_graph_destroy(TcModuleGraph *graph);

/* Resolve `import name` for `importer_path`, writing the canonical path to the
 * arena and returning 1.  Returns 0 when no candidate file exists. */
/* Bind a root candidate and return its owned canonical referent path. Returns
 * 1 on success, -1 on OOM, and -2 on host/type/identity I/O failure. */
int tc_module_bind_root(const char *path, int *out_fd, char **out_path);
int tc_module_bound_identity(int fd, const char *canonical_path, char **out);
int tc_module_bound_parent(int fd, const char *canonical_path, int *out_parent_fd);

/* Resolve `import name`, returning a bound regular-file fd, its canonical
 * referent path, its actual entry identity, and the lexical failing candidate
 * on a host/type/identity error. Paths are written to the arena. Returns 1 when
 * bound, 0 when genuinely absent, -1 on OOM, and -2 on host/type/identity I/O.
 * The caller owns fd. */
int tc_module_resolve_import_fd(TcArena *arena, const char *importer_path,
                                const char *name,
                                const char *const *include_roots,
                                size_t ninclude_roots, int *out_fd,
                                const char **out_path, const char **out_identity,
                                const char **out_failed_path);
int tc_module_resolve_import_at_fd(
    TcArena *arena, int importer_parent_fd, const char *importer_path,
    const char *name, const char *const *include_roots,
    const int *include_fds, size_t ninclude_roots, int *out_fd,
    const char **out_path, const char **out_identity,
    const char **out_failed_path, int *out_parent_fd);
int tc_module_resolve_import(TcArena *arena, const char *importer_path,
                             const char *name,
                             const char *const *include_roots,
                             size_t ninclude_roots, const char **out_path);

#endif /* TC_FRONTEND_MODULE_H */
