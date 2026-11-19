/* TC 0.0.46 module discovery (standard §4.5/§4.11.1). */

/* strdup/realpath/stat are POSIX and hidden in strict ISO C mode. */
#define _POSIX_C_SOURCE 200809L

#include "frontend/module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    TcArena *arena;
    TcModuleGraph *graph;
    const char *const *roots;
    size_t nroots;
    uint32_t *stack;
    TcSpan *stack_via; /* import edge that entered this stack entry */
    size_t nstack;
    size_t capstack;
    int failed;
} TcLoader;

static const TcAllocator *module_alloc(void) { return tc_allocator_libc(); }

static char *join_import_path(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int slash = dir_len > 0 && dir[dir_len - 1u] != '/';
    size_t total = dir_len + (size_t)slash + name_len + 4u;
    char *out = (char *)malloc(total + 1u);
    size_t at = 0;
    if (out == NULL) {
        return NULL;
    }
    memcpy(out + at, dir, dir_len);
    at += dir_len;
    if (slash) {
        out[at++] = '/';
    }
    memcpy(out + at, name, name_len);
    at += name_len;
    memcpy(out + at, ".tc", 4u);
    at += 3u;
    out[at] = '\0';
    return out;
}

static char *dir_of(const char *path) {
    const char *slash = strrchr(path, '/');
    size_t len;
    char *out;
    if (slash == NULL) {
        return strdup(".");
    }
    len = (size_t)(slash - path);
    if (len == 0) {
        len = 1;
    }
    out = (char *)malloc(len + 1u);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char *canonical_path(const char *path) {
    char *resolved = realpath(path, NULL);
    if (resolved != NULL) {
        return resolved;
    }
    return strdup(path);
}

static int is_regular_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int try_candidate(TcArena *arena, const char *candidate,
                         const char **out_path) {
    char *canonical;
    char *stored;
    if (!is_regular_file(candidate)) {
        return 0;
    }
    canonical = canonical_path(candidate);
    if (canonical == NULL) {
        return -1;
    }
    stored = tc_arena_strdup(arena, canonical);
    free(canonical);
    if (stored == NULL) {
        return -1;
    }
    *out_path = stored;
    return 1;
}

int tc_module_resolve_import(TcArena *arena, const char *importer_path,
                             const char *name,
                             const char *const *include_roots,
                             size_t ninclude_roots, const char **out_path) {
    char *dir;
    char *candidate;
    size_t i;
    int status;
    if (arena == NULL || importer_path == NULL || name == NULL ||
        out_path == NULL) {
        return -1;
    }
    *out_path = NULL;
    dir = dir_of(importer_path);
    if (dir == NULL) {
        return -1;
    }
    candidate = join_import_path(dir, name);
    free(dir);
    if (candidate == NULL) {
        return -1;
    }
    status = try_candidate(arena, candidate, out_path);
    free(candidate);
    if (status != 0) {
        return status;
    }
    for (i = 0; i < ninclude_roots; i++) {
        candidate = join_import_path(include_roots[i], name);
        if (candidate == NULL) {
            return -1;
        }
        status = try_candidate(arena, candidate, out_path);
        free(candidate);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

static TcModuleInfo *graph_module(TcModuleGraph *graph, uint32_t id) {
    return &graph->modules[id];
}

static int graph_push_module(TcModuleGraph *graph, const TcModuleInfo *info,
                             uint32_t *out_id) {
    const TcAllocator *alloc = module_alloc();
    TcModuleInfo *fresh = (TcModuleInfo *)alloc->realloc(
        alloc->ctx, graph->modules,
        (graph->nmodules + 1u) * sizeof(TcModuleInfo));
    if (fresh == NULL) {
        return 0;
    }
    graph->modules = fresh;
    fresh[graph->nmodules] = *info;
    *out_id = (uint32_t)graph->nmodules;
    graph->nmodules++;
    return 1;
}

static void loader_oom(TcLoader *loader) {
    loader->graph->impl = TC_IMPL_OOM;
    loader->failed = 1;
}

static void loader_internal(TcLoader *loader) {
    loader->graph->impl = TC_IMPL_INTERNAL;
    loader->failed = 1;
}

static int stack_push(TcLoader *loader, uint32_t id, TcSpan via) {
    const TcAllocator *alloc = module_alloc();
    if (loader->nstack == loader->capstack) {
        size_t cap = loader->capstack == 0 ? 16u : loader->capstack * 2u;
        uint32_t *ids = (uint32_t *)alloc->realloc(
            alloc->ctx, loader->stack, cap * sizeof(uint32_t));
        TcSpan *spans;
        if (ids == NULL) {
            loader_oom(loader);
            return 0;
        }
        loader->stack = ids;
        spans = (TcSpan *)alloc->realloc(alloc->ctx, loader->stack_via,
                                         cap * sizeof(TcSpan));
        if (spans == NULL) {
            loader_oom(loader);
            return 0;
        }
        loader->stack_via = spans;
        loader->capstack = cap;
    }
    loader->stack[loader->nstack] = id;
    loader->stack_via[loader->nstack] = via;
    loader->nstack++;
    return 1;
}

static void stack_pop(TcLoader *loader) {
    if (loader->nstack != 0) {
        loader->nstack--;
    }
}

static int stack_index_of(const TcLoader *loader, uint32_t id, size_t *index) {
    size_t i;
    for (i = loader->nstack; i > 0; i--) {
        if (loader->stack[i - 1u] == id) {
            *index = i - 1u;
            return 1;
        }
    }
    return 0;
}

static int find_module_path(const TcModuleGraph *graph, const char *path,
                            uint32_t *out_id) {
    size_t i;
    for (i = 0; i < graph->nmodules; i++) {
        if (strcmp(graph->modules[i].path, path) == 0) {
            *out_id = (uint32_t)i;
            return 1;
        }
    }
    return 0;
}

static int copy_lexsyn(TcModuleGraph *graph, const TcFrontendDiag *diag,
                       const char *path) {
    char *file = tc_arena_strdup(&graph->arena, path);
    if (file == NULL) {
        graph->impl = TC_IMPL_OOM;
        return 0;
    }
    graph->lexsyn = *diag;
    graph->lexsyn.span.file = file;
    if (graph->lexsyn.has_related) {
        char *related = tc_arena_strdup(&graph->arena, path);
        if (related == NULL) {
            graph->impl = TC_IMPL_OOM;
            return 0;
        }
        graph->lexsyn.related.file = related;
    }
    graph->has_lexsyn = 1;
    return 1;
}

static int parse_module_file(TcLoader *loader, const char *path, int is_root,
                             TcParseModule **out_parse) {
    TcSourceBytes source;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcParseModule *parse = NULL;
    int parsed;
    if (tc_source_read(path, &source) != TC_IMPL_OK) {
        loader->graph->impl = TC_IMPL_IO;
        loader->failed = 1;
        return 0;
    }
    parsed = tc_parse_source(path, source.bytes, source.size, &parse, &diag,
                             &impl);
    tc_source_free(&source);
    if (parsed != 0) {
        loader->graph->impl = impl != TC_IMPL_OK ? impl : TC_IMPL_INTERNAL;
        loader->failed = 1;
        return 0;
    }
    if (parse == NULL) {
        copy_lexsyn(loader->graph, &diag, path);
        loader->failed = 1;
        return 0;
    }
    if (is_root && !parse->is_program) {
        loader->graph->root_not_program = 1;
        tc_parse_module_destroy(parse);
        loader->failed = 1;
        return 0;
    }
    *out_parse = parse;
    return 1;
}

static void import_diag(TcLoader *loader, uint32_t importer, TcDiagCode code,
                        TcSpan primary, int has_related, TcSpan related) {
    TcDiagnostic diag;
    const char *file = primary.file;
    if (file == NULL) {
        file = loader->graph->modules[importer].path;
    }
    if (!tc_diag_make(&diag, code, TC_STAGE_IMPORT, importer, file,
                      primary.line, primary.column)) {
        loader_internal(loader);
        return;
    }
    if (has_related) {
        diag.has_related = 1;
        diag.related.file = related.file;
        diag.related.line = related.line;
        diag.related.column = related.column;
    }
    if (tc_diag_buffer_push(loader->graph->import_diags, &diag) != TC_IMPL_OK) {
        loader_oom(loader);
    }
}

static void cycle_diag(TcLoader *loader, uint32_t importer,
                       const TcParseImport *edge, uint32_t target) {
    size_t start = 0;
    size_t i;
    uint32_t best_module = importer;
    TcSpan best = edge->name_span;
    TcSpan related;
    if (!stack_index_of(loader, target, &start)) {
        start = 0;
    }
    /* Edges strictly inside the cycle: the import entering each node above
     * `start`, plus the current import.  The edge that entered the cycle from
     * outside is not part of it. */
    for (i = start + 1u; i < loader->nstack; i++) {
        uint32_t from = loader->stack[i - 1u];
        TcSpan via = loader->stack_via[i];
        if (from < best_module ||
            (from == best_module &&
             (via.line < best.line ||
              (via.line == best.line && via.column < best.column)))) {
            best_module = from;
            best = via;
        }
    }
    related = loader->graph->modules[target].parse->pattern_span;
    import_diag(loader, best_module, TC_CE_CIRCULAR_IMPORT, best, 1, related);
}

static int load_module(TcLoader *loader, const char *path, int is_root,
                       TcSpan via, uint32_t *out_id);

static int record_duplicate(TcLoader *loader, uint32_t id, size_t index,
                            uint32_t target, const TcParseImport *edge) {
    TcModuleInfo *module = graph_module(loader->graph, id);
    size_t j;
    for (j = 0; j < index; j++) {
        if (module->import_targets[j] == target) {
            import_diag(loader, id, TC_CE_DUPLICATE_IMPORT, edge->name_span, 1,
                        module->parse->imports[j].name_span);
            return 1;
        }
    }
    return 0;
}

static int load_module(TcLoader *loader, const char *path, int is_root,
                       TcSpan via, uint32_t *out_id) {
    TcModuleGraph *graph = loader->graph;
    TcModuleInfo info;
    TcModuleInfo *module;
    uint32_t id;
    uint32_t existing;
    TcParseModule *parse = NULL;
    size_t i;
    if (find_module_path(graph, path, &existing)) {
        *out_id = existing;
        return 1;
    }
    if (!parse_module_file(loader, path, is_root, &parse)) {
        return 0;
    }
    memset(&info, 0, sizeof(info));
    info.path = tc_arena_strdup(&graph->arena, path);
    if (info.path == NULL) {
        tc_parse_module_destroy(parse);
        loader_oom(loader);
        return 0;
    }
    info.parse = parse;
    if (!graph_push_module(graph, &info, &id)) {
        tc_parse_module_destroy(parse);
        loader_oom(loader);
        return 0;
    }
    *out_id = id;
    module = graph_module(graph, id);
    module->nimports = parse->nimports;
    module->import_targets = (uint32_t *)module_alloc()->alloc(
        module_alloc()->ctx,
        (parse->nimports == 0 ? 1u : parse->nimports) * sizeof(uint32_t));
    if (module->import_targets == NULL) {
        loader_oom(loader);
        return 0;
    }
    for (i = 0; i < parse->nimports; i++) {
        module->import_targets[i] = TC_MODULE_NO_TARGET;
    }
    if (parse->is_program && !is_root) {
        /* A #program import target is LEX/SYN-parsed but its imports are not
         * discovered (standard §4.11.1 step 3); IMPORT reports NOT_LIB. */
        return 1;
    }
    if (!stack_push(loader, id, via)) {
        return 0;
    }
    for (i = 0; i < parse->nimports; i++) {
        const TcParseImport *edge = &parse->imports[i];
        const char *resolved = NULL;
        uint32_t target = TC_MODULE_NO_TARGET;
        int status = tc_module_resolve_import(
            &graph->arena, module->path, edge->name, loader->roots,
            loader->nroots, &resolved);
        if (status < 0) {
            loader_oom(loader);
            break;
        }
        if (status == 0) {
            import_diag(loader, id, TC_CE_IMPORT_NOT_FOUND, edge->name_span, 0,
                        edge->name_span);
            continue;
        }
        if (find_module_path(graph, resolved, &target)) {
            size_t stack_pos = 0;
            if (stack_index_of(loader, target, &stack_pos)) {
                cycle_diag(loader, id, edge, target);
                continue;
            }
        } else if (!load_module(loader, resolved, 0, edge->name_span, &target)) {
            break;
        }
        module = graph_module(graph, id);
        module->import_targets[i] = target;
        record_duplicate(loader, id, i, target, edge);
        if (graph_module(graph, target)->parse->is_program) {
            import_diag(loader, id, TC_CE_IMPORT_NOT_LIB, edge->name_span, 0,
                        edge->name_span);
        }
    }
    stack_pop(loader);
    return loader->failed ? 0 : 1;
}

TcModuleGraph *tc_module_graph_load(const char *root_path,
                                    const char *const *include_roots,
                                    size_t ninclude_roots) {
    const TcAllocator *alloc = module_alloc();
    TcModuleGraph *graph =
        (TcModuleGraph *)alloc->alloc(alloc->ctx, sizeof(TcModuleGraph));
    TcLoader loader;
    uint32_t root_id = 0;
    char *root_canonical = NULL;
    if (graph == NULL || root_path == NULL) {
        alloc->free(alloc->ctx, graph);
        return NULL;
    }
    memset(graph, 0, sizeof(*graph));
    tc_arena_init(&graph->arena, alloc);
    graph->import_diags = tc_diag_buffer_create(alloc);
    if (graph->import_diags == NULL) {
        tc_module_graph_destroy(graph);
        return NULL;
    }
    memset(&loader, 0, sizeof(loader));
    loader.arena = &graph->arena;
    loader.graph = graph;
    loader.roots = include_roots;
    loader.nroots = ninclude_roots;
    root_canonical = canonical_path(root_path);
    if (root_canonical == NULL) {
        alloc->free(alloc->ctx, loader.stack);
        alloc->free(alloc->ctx, loader.stack_via);
        tc_module_graph_destroy(graph);
        return NULL;
    }
    if (!load_module(&loader, root_canonical, 1, (TcSpan){0}, &root_id) &&
        !graph->has_lexsyn && !graph->root_not_program &&
        graph->impl == TC_IMPL_OK) {
        graph->impl = TC_IMPL_INTERNAL;
    }
    free(root_canonical);
    alloc->free(alloc->ctx, loader.stack);
    alloc->free(alloc->ctx, loader.stack_via);
    return graph;
}

void tc_module_graph_destroy(TcModuleGraph *graph) {
    const TcAllocator *alloc = module_alloc();
    size_t i;
    if (graph == NULL) {
        return;
    }
    for (i = 0; i < graph->nmodules; i++) {
        alloc->free(alloc->ctx, graph->modules[i].import_targets);
        tc_parse_module_destroy(graph->modules[i].parse);
    }
    alloc->free(alloc->ctx, graph->modules);
    tc_diag_buffer_destroy(graph->import_diags);
    tc_arena_destroy(&graph->arena);
    alloc->free(alloc->ctx, graph);
}
