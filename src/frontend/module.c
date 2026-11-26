/* TC 0.0.46 module discovery (standard §4.5/§4.11.1). */

/* strdup/realpath/stat need POSIX/GNU exposure; strict ISO C hides them
 * (glibc gates realpath on more than POSIX 2008). */
#define _GNU_SOURCE

#include "frontend/module.h"

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TC_IMPORT_IO (-2)

typedef struct {
    TcArena *arena;
    TcModuleGraph *graph;
    const char *const *roots;
    const int *root_fds;
    size_t nroots;
    uint32_t *stack;
    TcSpan *stack_via; /* import edge that entered this stack entry */
    size_t nstack;
    size_t capstack;
    int failed;
} TcLoader;

static const TcAllocator *module_alloc(void) { return tc_allocator_libc(); }

static char *join_import_path(const char *dir, const char *name) {
    size_t name_len = strlen(name);
    char *named = (char *)malloc(name_len + 4u);
    char *joined;
    if (named == NULL) {
        return NULL;
    }
    memcpy(named, name, name_len);
    memcpy(named + name_len, ".tc", 4u);
    joined = tc_path_join(dir, named);
    free(named);
    return joined;
}

static int same_identity(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int candidate_open_at(int dir_fd, const char *name, int *out_fd) {
    int attempt;
    int saw_existing = 0;
    for (attempt = 0; attempt < 2; attempt++) {
        struct stat queried;
        struct stat opened;
        int query_error = 0;
        int fd;
        if (fstatat(dir_fd, name, &queried, AT_SYMLINK_NOFOLLOW) != 0) {
            query_error = errno;
            if (query_error != ENOENT && query_error != ENOTDIR) {
                return query_error == ENOMEM ? -1 : TC_IMPORT_IO;
            }
        } else {
            saw_existing = 1;
            if (!S_ISLNK(queried.st_mode) && !S_ISREG(queried.st_mode)) {
                return TC_IMPORT_IO;
            }
        }
        fd = openat(dir_fd, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            int error_number = errno;
            if (query_error == 0) {
                if (error_number != ENOENT && error_number != ENOTDIR) {
                    return error_number == ENOMEM ? -1 : TC_IMPORT_IO;
                }
                continue;
            }
            if (error_number == ENOENT || error_number == ENOTDIR) {
                return saw_existing ? TC_IMPORT_IO : 0;
            }
            return error_number == ENOMEM ? -1 : TC_IMPORT_IO;
        }
        if (fstat(fd, &opened) != 0) {
            int error_number = errno;
            (void)close(fd);
            return error_number == ENOMEM ? -1 : TC_IMPORT_IO;
        }
        if (!S_ISREG(opened.st_mode)) {
            (void)close(fd);
            return TC_IMPORT_IO;
        }
        if (query_error == 0 && !S_ISLNK(queried.st_mode) &&
            !same_identity(&queried, &opened)) {
            (void)close(fd);
            continue;
        }
        *out_fd = fd;
        return 1;
    }
    return TC_IMPORT_IO;
}

static int candidate_open(const char *candidate, int *out_fd) {
    char *directory = NULL;
    char *canonical_directory = NULL;
    const char *name;
    int dir_fd = -1;
    int missing = 0;
    int status;
    TcImplError impl = tc_path_directory(candidate, &directory);
    if (impl != TC_IMPL_OK) {
        return impl == TC_IMPL_OOM ? -1 : TC_IMPORT_IO;
    }
    /* Resolve symlinked ancestors before nofollow binds the parent entry. */
    impl = tc_path_resolve(directory, 1, &canonical_directory, &missing);
    free(directory);
    if (impl != TC_IMPL_OK) {
        return impl == TC_IMPL_OOM ? -1 : TC_IMPORT_IO;
    }
    name = strrchr(candidate, '/');
    name = name == NULL ? candidate : name + 1u;
    dir_fd = open(canonical_directory,
                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    free(canonical_directory);
    if (dir_fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return 0;
        }
        return errno == ENOMEM ? -1 : TC_IMPORT_IO;
    }
    status = candidate_open_at(dir_fd, name, out_fd);
    (void)close(dir_fd);
    return status;
}

static int candidate_canonical(const char *candidate, int fd, char **out) {
    char *resolved;
    struct stat canonical_st;
    struct stat opened_st;
    if (fstat(fd, &opened_st) != 0) {
        return errno == ENOMEM ? -1 : TC_IMPORT_IO;
    }
    resolved = realpath(candidate, NULL);
    if (resolved != NULL) {
        if (stat(resolved, &canonical_st) == 0 &&
            same_identity(&opened_st, &canonical_st)) {
            *out = resolved;
            return 1;
        }
        free(resolved);
    } else if (errno == ENOMEM) {
        return -1;
    }
#ifdef F_GETPATH
    {
        char path[PATH_MAX];
        if (fcntl(fd, F_GETPATH, path) == 0 && path[0] == '/' &&
            stat(path, &canonical_st) == 0 &&
            same_identity(&opened_st, &canonical_st)) {
            resolved = strdup(path);
            if (resolved == NULL) {
                return -1;
            }
            *out = resolved;
            return 1;
        }
    }
#endif
#if defined(__linux__)
    {
        char path[PATH_MAX];
        char link[64];
        ssize_t length;
        (void)snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
        length = readlink(link, path, sizeof(path) - 1u);
        if (length >= 0) {
            path[length] = '\0';
            if (path[0] == '/' && stat(path, &canonical_st) == 0 &&
                same_identity(&opened_st, &canonical_st)) {
                resolved = strdup(path);
                if (resolved == NULL) {
                    return -1;
                }
                *out = resolved;
                return 1;
            }
        }
    }
#endif
    return TC_IMPORT_IO;
}

static char *actual_entry_name(int dir_fd, const struct stat *target,
                               const char *preferred) {
    int scan_fd;
    DIR *directory;
    struct dirent *entry;
    char *first = NULL;
    int preferred_bound = 0;
    if (preferred != NULL) {
        struct stat st;
        if (fstatat(dir_fd, preferred, &st, AT_SYMLINK_NOFOLLOW) == 0) {
            if (!same_identity(&st, target)) {
                errno = EIO;
                return NULL;
            }
            preferred_bound = 1;
        } else if (errno != ENOENT && errno != ENOTDIR) {
            return NULL;
        }
    }
    scan_fd = dup(dir_fd);
    if (scan_fd < 0) {
        return NULL;
    }
    directory = fdopendir(scan_fd);
    if (directory == NULL) {
        int error_number = errno;
        (void)close(scan_fd);
        errno = error_number;
        return NULL;
    }
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        struct stat st;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            errno = 0;
            continue;
        }
        if (fstatat(dir_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno != ENOENT && errno != ENOTDIR) {
                int error_number = errno;
                free(first);
                (void)closedir(directory);
                errno = error_number;
                return NULL;
            }
            errno = 0;
            continue;
        }
        if (same_identity(&st, target)) {
            if (preferred != NULL && strcmp(entry->d_name, preferred) == 0) {
                free(first);
                first = strdup(entry->d_name);
                (void)closedir(directory);
                return first;
            }
            if (first == NULL) {
                first = strdup(entry->d_name);
                if (first == NULL) {
                    (void)closedir(directory);
                    return NULL;
                }
            }
        }
        errno = 0;
    }
    if (errno != 0) {
        int error_number = errno;
        free(first);
        (void)closedir(directory);
        errno = error_number;
        return NULL;
    }
    if (closedir(directory) != 0) {
        free(first);
        return NULL;
    }
    if (first != NULL) {
        return first;
    }
    if (preferred_bound) {
        return strdup(preferred);
    }
    errno = ENOENT;
    return NULL;
}

static int candidate_identity(int fd, const char *canonical, char **out,
                              int *out_parent_fd) {
    char *path = strdup(canonical);
    char *identity = NULL;
    char *cursor;
    int parent_fd = -1;
    int status = TC_IMPORT_IO;
    struct stat file_st;
    if (path == NULL) {
        return -1;
    }
    if (canonical[0] != '/' || fstat(fd, &file_st) != 0) {
        free(path);
        return errno == ENOMEM ? -1 : TC_IMPORT_IO;
    }
    parent_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd < 0) {
        free(path);
        return errno == ENOMEM ? -1 : TC_IMPORT_IO;
    }
    identity = strdup("/");
    if (identity == NULL) {
        status = -1;
        goto done;
    }
    cursor = path + 1;
    for (;;) {
        char *slash = strchr(cursor, '/');
        char *name;
        char *joined;
        const struct stat *target = &file_st;
        int child_fd = -1;
        struct stat child_st;
        if (slash != NULL) {
            *slash = '\0';
            child_fd = openat(parent_fd, cursor,
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child_fd < 0) {
                status = errno == ENOMEM ? -1 : TC_IMPORT_IO;
                goto done;
            }
            if (fstat(child_fd, &child_st) != 0) {
                int error_number = errno;
                (void)close(child_fd);
                errno = error_number;
                status = error_number == ENOMEM ? -1 : TC_IMPORT_IO;
                goto done;
            }
            target = &child_st;
        }
        name = actual_entry_name(parent_fd, target, cursor);
        if (name == NULL) {
            int error_number = errno;
            if (child_fd >= 0) {
                (void)close(child_fd);
            }
            errno = error_number;
            status = error_number == ENOMEM ? -1 : TC_IMPORT_IO;
            goto done;
        }
        joined = tc_path_join(identity, name);
        free(name);
        if (joined == NULL) {
            if (child_fd >= 0) {
                (void)close(child_fd);
            }
            status = -1;
            goto done;
        }
        free(identity);
        identity = joined;
        if (slash == NULL) {
            break;
        }
        (void)close(parent_fd);
        parent_fd = child_fd;
        cursor = slash + 1;
    }
    *out = identity;
    identity = NULL;
    if (out_parent_fd != NULL) {
        *out_parent_fd = parent_fd;
        parent_fd = -1;
    }
    status = 1;

done:
    if (parent_fd >= 0) {
        (void)close(parent_fd);
    }
    free(identity);
    free(path);
    return status;
}

int tc_module_bind_root(const char *path, int *out_fd, char **out_path) {
    int fd = -1;
    int status;
    if (path == NULL || out_fd == NULL || out_path == NULL) {
        return -1;
    }
    *out_fd = -1;
    *out_path = NULL;
    status = candidate_open(path, &fd);
    if (status <= 0) {
        return status == 0 ? TC_IMPORT_IO : status;
    }
    status = candidate_canonical(path, fd, out_path);
    if (status <= 0) {
        (void)close(fd);
        return status;
    }
    *out_fd = fd;
    return 1;
}

int tc_module_bound_identity(int fd, const char *canonical_path, char **out) {
    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (fd < 0 || canonical_path == NULL) {
        return TC_IMPORT_IO;
    }
    return candidate_identity(fd, canonical_path, out, NULL);
}

int tc_module_bound_parent(int fd, const char *canonical_path, int *out_parent_fd) {
    char *identity = NULL;
    int parent_fd = -1;
    int status;
    if (out_parent_fd == NULL) {
        return TC_IMPORT_IO;
    }
    *out_parent_fd = -1;
    status = candidate_identity(fd, canonical_path, &identity, &parent_fd);
    free(identity);
    if (status <= 0) {
        if (parent_fd >= 0) {
            (void)close(parent_fd);
        }
        return status;
    }
    *out_parent_fd = parent_fd;
    return 1;
}

static int try_candidate_at(TcArena *arena, int dir_fd, const char *name,
                           const char *candidate, int *out_fd,
                           const char **out_path, const char **out_identity,
                           const char **out_failed_path, int *out_parent_fd) {
    int fd = -1;
    int parent_fd = -1;
    int status;
    char *canonical = NULL;
    char *identity = NULL;
    char *stored_path = NULL;
    char *stored_identity = NULL;
    if (out_parent_fd != NULL) {
        *out_parent_fd = -1;
    }
    status = candidate_open_at(dir_fd, name, &fd);
    if (status <= 0) {
        if (status == TC_IMPORT_IO && out_failed_path != NULL) {
            stored_path = tc_arena_strdup(arena, candidate);
            if (stored_path == NULL) {
                return -1;
            }
            *out_failed_path = stored_path;
        }
        return status;
    }
    status = candidate_canonical(candidate, fd, &canonical);
    if (status <= 0) {
        (void)close(fd);
        if (status == TC_IMPORT_IO && out_failed_path != NULL) {
            stored_path = tc_arena_strdup(arena, candidate);
            if (stored_path == NULL) {
                return -1;
            }
            *out_failed_path = stored_path;
        }
        return status;
    }
    status = candidate_identity(fd, canonical, &identity, &parent_fd);
    if (status <= 0) {
        (void)close(fd);
        free(canonical);
        if (status == TC_IMPORT_IO && out_failed_path != NULL) {
            stored_path = tc_arena_strdup(arena, candidate);
            if (stored_path == NULL) {
                return -1;
            }
            *out_failed_path = stored_path;
        }
        return status;
    }
    stored_path = tc_arena_strdup(arena, canonical);
    stored_identity = tc_arena_strdup(arena, identity);
    free(canonical);
    free(identity);
    if (stored_path == NULL || stored_identity == NULL) {
        (void)close(fd);
        if (parent_fd >= 0) {
            (void)close(parent_fd);
        }
        return -1;
    }
    *out_fd = fd;
    *out_path = stored_path;
    *out_identity = stored_identity;
    if (out_parent_fd != NULL) {
        *out_parent_fd = parent_fd;
    }
    return 1;
}

static int try_candidate_fd(const char *candidate, int *out_fd,
                            const char **out_path, const char **out_identity,
                            const char **out_failed_path, TcArena *arena) {
    char *directory = NULL;
    char *canonical_directory = NULL;
    const char *name;
    int dir_fd = -1;
    int missing = 0;
    int status;
    TcImplError impl = tc_path_directory(candidate, &directory);
    if (impl != TC_IMPL_OK) {
        return impl == TC_IMPL_OOM ? -1 : TC_IMPORT_IO;
    }
    impl = tc_path_resolve(directory, 1, &canonical_directory, &missing);
    free(directory);
    if (impl != TC_IMPL_OK) {
        return impl == TC_IMPL_OOM ? -1 : TC_IMPORT_IO;
    }
    name = strrchr(candidate, '/');
    name = name == NULL ? candidate : name + 1u;
    dir_fd = open(canonical_directory,
                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    free(canonical_directory);
    if (dir_fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return 0;
        }
        return errno == ENOMEM ? -1 : TC_IMPORT_IO;
    }
    status = try_candidate_at(arena, dir_fd, name, candidate, out_fd, out_path,
                              out_identity, out_failed_path, NULL);
    (void)close(dir_fd);
    return status;
}

int tc_module_resolve_import_at_fd(
    TcArena *arena, int importer_parent_fd, const char *importer_path,
    const char *name, const char *const *include_roots,
    const int *include_fds, size_t ninclude_roots, int *out_fd,
    const char **out_path, const char **out_identity,
    const char **out_failed_path, int *out_parent_fd) {
    char *dir = NULL;
    char *candidate = NULL;
    char *named = NULL;
    size_t i;
    int status;
    if (arena == NULL || importer_parent_fd < 0 || importer_path == NULL ||
        name == NULL || out_path == NULL || out_fd == NULL ||
        out_identity == NULL || out_failed_path == NULL ||
        out_parent_fd == NULL) {
        return -1;
    }
    *out_path = NULL;
    *out_identity = NULL;
    *out_failed_path = NULL;
    *out_fd = -1;
    *out_parent_fd = -1;
    status = tc_path_directory(importer_path, &dir);
    if (status != TC_IMPL_OK) {
        return status == TC_IMPL_OOM ? -1 : TC_IMPORT_IO;
    }
    named = malloc(strlen(name) + 4u);
    if (named == NULL) {
        free(dir);
        return -1;
    }
    strcpy(named, name);
    strcat(named, ".tc");
    candidate = tc_path_join(dir, named);
    free(dir);
    if (candidate == NULL) {
        free(named);
        return -1;
    }
    status = try_candidate_at(arena, importer_parent_fd, named, candidate,
                              out_fd, out_path, out_identity,
                              out_failed_path, out_parent_fd);
    free(named);
    free(candidate);
    if (status != 0) {
        return status;
    }
    for (i = 0; i < ninclude_roots; i++) {
        int owned_fd = -1;
        int dir_fd = include_fds != NULL ? include_fds[i] : -1;
        candidate = join_import_path(include_roots[i], name);
        if (candidate == NULL) {
            return -1;
        }
        if (include_fds != NULL) {
            if (dir_fd < 0) {
                free(candidate);
                continue;
            }
        } else if (dir_fd < 0) {
            dir_fd = open(include_roots[i],
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (dir_fd < 0) {
                int error_number = errno;
                free(candidate);
                if (error_number == ENOENT || error_number == ENOTDIR) {
                    continue;
                }
                return error_number == ENOMEM ? -1 : TC_IMPORT_IO;
            }
            owned_fd = dir_fd;
        }
        {
            const char *leaf = strrchr(candidate, '/');
            leaf = leaf == NULL ? candidate : leaf + 1u;
            status = try_candidate_at(arena, dir_fd, leaf, candidate, out_fd,
                                      out_path, out_identity,
                                      out_failed_path, out_parent_fd);
        }
        if (owned_fd >= 0) {
            (void)close(owned_fd);
        }
        free(candidate);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

int tc_module_resolve_import_fd(TcArena *arena, const char *importer_path,
                                const char *name,
                                const char *const *include_roots,
                                size_t ninclude_roots, int *out_fd,
                                const char **out_path, const char **out_identity,
                                const char **out_failed_path) {
    char *dir = NULL;
    char *candidate = NULL;
    size_t i;
    int status;
    if (arena == NULL || importer_path == NULL || name == NULL ||
        out_path == NULL || out_fd == NULL || out_identity == NULL ||
        out_failed_path == NULL) {
        return -1;
    }
    *out_path = NULL;
    *out_identity = NULL;
    *out_failed_path = NULL;
    *out_fd = -1;
    status = tc_path_directory(importer_path, &dir);
    if (status != TC_IMPL_OK) {
        return status == TC_IMPL_OOM ? -1 : TC_IMPORT_IO;
    }
    candidate = join_import_path(dir, name);
    free(dir);
    if (candidate == NULL) {
        return -1;
    }
    status = try_candidate_fd(candidate, out_fd, out_path, out_identity,
                              out_failed_path, arena);
    free(candidate);
    if (status != 0) {
        return status;
    }
    for (i = 0; i < ninclude_roots; i++) {
        candidate = join_import_path(include_roots[i], name);
        if (candidate == NULL) {
            return -1;
        }
        status = try_candidate_fd(candidate, out_fd, out_path, out_identity,
                                  out_failed_path, arena);
        free(candidate);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

int tc_module_resolve_import(TcArena *arena, const char *importer_path,
                             const char *name,
                             const char *const *include_roots,
                             size_t ninclude_roots, const char **out_path) {
    int fd = -1;
    const char *identity = NULL;
    const char *failed_path = NULL;
    int status = tc_module_resolve_import_fd(
        arena, importer_path, name, include_roots, ninclude_roots, &fd,
        out_path, &identity, &failed_path);
    if (fd >= 0) {
        (void)close(fd);
    }
    return status;
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

static int find_module_identity(const TcModuleGraph *graph, const char *identity,
                                uint32_t *out_id) {
    size_t i;
    for (i = 0; i < graph->nmodules; i++) {
        if (strcmp(graph->modules[i].identity, identity) == 0) {
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

static int parse_module_file(TcLoader *loader, const char *display_path,
                             int fd, int is_root, TcParseModule **out_parse) {
    TcSourceBytes source;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcImplError read_error;
    TcParseModule *parse = NULL;
    int parsed;
    read_error = tc_source_read_fd(fd, &source);
    if (read_error != TC_IMPL_OK) {
        loader->graph->impl = read_error;
        loader->failed = 1;
        return 0;
    }
    parsed = tc_parse_source(display_path, source.bytes, source.size, &parse,
                             &diag, &impl);
    tc_source_free(&source);
    if (parsed != 0) {
        loader->graph->impl = impl != TC_IMPL_OK ? impl : TC_IMPL_INTERNAL;
        loader->failed = 1;
        return 0;
    }
    if (parse == NULL) {
        copy_lexsyn(loader->graph, &diag, display_path);
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
        file = loader->graph->modules[importer].display_path;
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

static int load_module(TcLoader *loader, const char *path, const char *identity,
                       int parent_fd, int fd, int is_root, TcSpan via,
                       uint32_t *out_id);

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

static int load_module(TcLoader *loader, const char *path, const char *identity,
                       int parent_fd, int fd, int is_root, TcSpan via,
                       uint32_t *out_id) {
    TcModuleGraph *graph = loader->graph;
    TcModuleInfo info;
    TcModuleInfo *module;
    uint32_t id;
    uint32_t existing;
    TcParseModule *parse = NULL;
    const char *display_path;
    char *display_owned;
    const TcPathFile *path_file;
    size_t i;
    if (find_module_identity(graph, identity, &existing)) {
        (void)close(fd);
        *out_id = existing;
        return 1;
    }
    if (graph->paths != NULL) {
        if (!tc_path_text_valid(path)) {
            graph->impl = TC_IMPL_IO;
            graph->failed_display = ".";
            (void)close(fd);
            loader->failed = 1;
            return 0;
        }
        path_file = tc_path_context_add(graph->paths, path);
        if (path_file == NULL) {
            (void)close(fd);
            loader_oom(loader);
            return 0;
        }
        display_path = path_file->display;
    } else {
        display_path = path;
    }
    display_owned = tc_arena_strdup(&graph->arena, display_path);
    if (display_owned == NULL) {
        (void)close(fd);
        loader_oom(loader);
        return 0;
    }
    display_path = display_owned;
    graph->failed_display = display_path;
    if (!parse_module_file(loader, display_path, fd, is_root, &parse)) {
        return 0;
    }
    graph->failed_display = NULL;
    memset(&info, 0, sizeof(info));
    info.path = tc_arena_strdup(&graph->arena, path);
    info.identity = tc_arena_strdup(&graph->arena, identity);
    info.display_path = display_owned;
    if (info.path == NULL || info.identity == NULL) {
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
        const char *resolved_identity = NULL;
        const char *failed_candidate = NULL;
        int resolved_fd = -1;
        int resolved_parent_fd = -1;
        uint32_t target = TC_MODULE_NO_TARGET;
        int status = tc_module_resolve_import_at_fd(
            &graph->arena, parent_fd, module->path, edge->name, loader->roots,
            loader->root_fds, loader->nroots, &resolved_fd, &resolved,
            &resolved_identity, &failed_candidate, &resolved_parent_fd);
        if (status == -2) {
            const char *failure = failed_candidate != NULL
                                      ? failed_candidate
                                      : module->display_path;
            if (graph->paths != NULL && tc_path_text_valid(failure)) {
                const TcPathFile *failed_file =
                    tc_path_context_add(graph->paths, failure);
                if (failed_file == NULL) {
                    loader_oom(loader);
                    break;
                }
                graph->failed_display = failed_file->display;
            } else {
                graph->failed_display = module->display_path;
            }
            graph->impl = TC_IMPL_IO;
            loader->failed = 1;
            break;
        }
        if (status < 0) {
            loader_oom(loader);
            break;
        }
        if (status == 0) {
            import_diag(loader, id, TC_CE_IMPORT_NOT_FOUND, edge->name_span, 0,
                        edge->name_span);
            continue;
        }
        if (find_module_identity(graph, resolved_identity, &target)) {
            size_t stack_pos = 0;
            (void)close(resolved_fd);
            if (stack_index_of(loader, target, &stack_pos)) {
                cycle_diag(loader, id, edge, target);
                continue;
            }
        } else if (!load_module(loader, resolved, resolved_identity,
                                resolved_parent_fd, resolved_fd, 0,
                                edge->name_span, &target)) {
            if (resolved_parent_fd >= 0) {
                (void)close(resolved_parent_fd);
            }
            break;
        }
        if (resolved_parent_fd >= 0) {
            (void)close(resolved_parent_fd);
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

static TcModuleGraph *graph_load_impl(const char *root_path,
                                      const char *const *include_roots,
                                      size_t ninclude_roots, TcPathContext *paths,
                                      const TcModuleRootSource *source) {
    const TcAllocator *alloc = module_alloc();
    TcModuleGraph *graph =
        (TcModuleGraph *)alloc->alloc(alloc->ctx, sizeof(TcModuleGraph));
    TcLoader loader;
    uint32_t root_id = 0;
    char *root_bound = NULL;
    char *root_identity = NULL;
    int root_fd = -1;
    int load_fd = -1;
    int parent_fd = -1;
    int owns_root_fd = 0;
    int owns_parent_fd = 0;
    int status;
    if (graph == NULL || root_path == NULL ||
        (source != NULL && (source->fd < 0 || source->parent_fd < 0 ||
                            source->path == NULL || source->identity == NULL ||
                            source->ninclude_fds != ninclude_roots ||
                            (source->ninclude_fds != 0u &&
                             source->include_fds == NULL)))) {
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
    graph->paths = paths;
    if (source != NULL) {
        root_fd = dup(source->fd);
        parent_fd = source->parent_fd;
        root_bound = strdup(source->path);
        root_identity = strdup(source->identity);
        loader.root_fds = source->include_fds;
        loader.nroots = source->ninclude_fds;
        owns_root_fd = 1;
        if (root_fd < 0 || root_bound == NULL || root_identity == NULL) {
            graph->impl = TC_IMPL_OOM;
        }
    } else {
        status = tc_module_bind_root(root_path, &root_fd, &root_bound);
        owns_root_fd = 1;
        if (status < 0) {
            graph->impl = TC_IMPL_OOM;
        } else if (status == TC_IMPORT_IO) {
            graph->impl = TC_IMPL_IO;
        } else {
            status = candidate_identity(root_fd, root_bound, &root_identity,
                                        &parent_fd);
            if (status < 0) {
                graph->impl = TC_IMPL_OOM;
            } else if (status == TC_IMPORT_IO) {
                graph->impl = TC_IMPL_IO;
            } else {
                owns_parent_fd = 1;
            }
        }
    }
    if (graph->impl == TC_IMPL_OK) {
        load_fd = dup(root_fd);
        if (load_fd < 0) {
            graph->impl = errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO;
        } else {
            (void)load_module(&loader, root_bound, root_identity, parent_fd,
                              load_fd, 1, (TcSpan){0}, &root_id);
            load_fd = -1;
        }
    }
    if (load_fd >= 0) {
        (void)close(load_fd);
    }
    if (owns_root_fd && root_fd >= 0) {
        (void)close(root_fd);
    }
    if (owns_parent_fd && parent_fd >= 0) {
        (void)close(parent_fd);
    }
    if (graph->impl == TC_IMPL_OK && !graph->has_lexsyn &&
        !graph->root_not_program && loader.failed) {
        graph->impl = TC_IMPL_INTERNAL;
    }
    free(root_bound);
    free(root_identity);
    alloc->free(alloc->ctx, loader.stack);
    alloc->free(alloc->ctx, loader.stack_via);
    return graph;
}

static TcModuleGraph *graph_load(const char *root_path,
                                 const char *const *include_roots,
                                 size_t ninclude_roots, TcPathContext *paths) {
    return graph_load_impl(root_path, include_roots, ninclude_roots, paths,
                           NULL);
}

TcModuleGraph *tc_module_graph_load_source(
    const TcModuleRootSource *source, const char *const *include_roots,
    size_t ninclude_roots, TcPathContext *paths) {
    return source == NULL ? NULL
                          : graph_load_impl(source->path, include_roots,
                                            ninclude_roots, paths, source);
}

TcModuleGraph *tc_module_graph_load_with_paths(
    const char *root_path, const char *const *include_roots,
    size_t ninclude_roots, TcPathContext *paths) {
    return graph_load(root_path, include_roots, ninclude_roots, paths);
}

TcModuleGraph *tc_module_graph_load(const char *root_path,
                                    const char *const *include_roots,
                                    size_t ninclude_roots) {
    return graph_load(root_path, include_roots, ninclude_roots, NULL);
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
