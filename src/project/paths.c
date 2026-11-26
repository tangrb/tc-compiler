#define _GNU_SOURCE

#include "project/paths.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void clear_error(TcProjectPathError *error) {
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }
}

static int fail(TcProjectPathError *error, TcImplError impl,
                const char *message) {
    if (error != NULL) {
        error->impl = impl;
        error->message = message;
    }
    return 0;
}

void tc_project_path_error_destroy(TcProjectPathError *error) {
    if (error != NULL) {
        free(error->detail);
        error->detail = NULL;
    }
}

static int set_detail(TcProjectPathError *error, const char *detail) {
    size_t size = strlen(detail) + 1u;
    error->detail = (char *)malloc(size);
    if (error->detail == NULL) {
        return fail(error, TC_IMPL_OOM, "out of memory");
    }
    memcpy(error->detail, detail, size);
    return 1;
}

static int synthetic_manifest(const char *root, TcProjectManifest **out,
                              TcProjectPathError *error) {
    TcProjectManifest *manifest = (TcProjectManifest *)calloc(1u, sizeof(*manifest));
    size_t size;
    if (manifest == NULL) {
        return fail(error, TC_IMPL_OOM, "out of memory");
    }
    size = strlen(root) + 1u;
    manifest->root = (char *)malloc(size);
    if (manifest->root == NULL) {
        free(manifest);
        return fail(error, TC_IMPL_OOM, "out of memory");
    }
    memcpy(manifest->root, root, size);
    manifest->profile = strdup("aarch64-linux-user");
    if (manifest->profile == NULL) {
        tc_project_manifest_destroy(manifest);
        return fail(error, TC_IMPL_OOM, "out of memory");
    }
    *out = manifest;
    return 1;
}

static char *missing_source_anchor(const char *directory) {
    char *current = strdup(directory);
    if (current == NULL) {
        return NULL;
    }
    for (;;) {
        char *candidate = tc_path_join(current, "tc.project");
        struct stat st;
        int found;
        if (candidate == NULL) {
            free(current);
            return NULL;
        }
        found = lstat(candidate, &st) == 0;
        free(candidate);
        if (found) {
            return current;
        }
        if (strcmp(current, "/") == 0) {
            return current;
        }
        {
            char *parent = NULL;
            TcImplError impl = tc_path_directory(current, &parent);
            free(current);
            if (impl != TC_IMPL_OK) {
                return NULL;
            }
            current = parent;
        }
    }
}

static int prepare_include(TcProjectPaths *paths, const char *value,
                           int relative_to_root, TcProjectPathError *error) {
    char *lexical = NULL;
    char *canonical = NULL;
    struct stat st;
    int missing = 0;
    TcImplError impl;
    if (!tc_path_text_valid(value)) {
        return fail(error, TC_IMPL_USAGE, "include path is not valid UTF-8 text");
    }
    if (relative_to_root) {
        lexical = tc_path_join(paths->manifest->root, value);
        if (lexical == NULL) {
            return fail(error, TC_IMPL_OOM, "out of memory");
        }
    } else {
        impl = tc_path_absolute(value, &lexical);
        if (impl != TC_IMPL_OK) {
            return fail(error, impl, impl == TC_IMPL_OOM ? "out of memory"
                                                         : "invalid include path");
        }
    }
    impl = tc_path_resolve(lexical, 1, &canonical, &missing);
    free(lexical);
    if (impl != TC_IMPL_OK) {
        return fail(error, impl, impl == TC_IMPL_OOM ? "out of memory"
                                                     : "invalid include path");
    }
    if (!missing) {
        if (stat(canonical, &st) != 0) {
            int error_number = errno;
            free(canonical);
            return fail(error, error_number == ENOMEM ? TC_IMPL_OOM
                                                      : TC_IMPL_IO,
                        "cannot inspect include root");
        }
        if (!S_ISDIR(st.st_mode)) {
            free(canonical);
            return fail(error, TC_IMPL_USAGE,
                        "include root is not a directory");
        }
    }
    if (!tc_path_text_valid(canonical)) {
        free(canonical);
        return fail(error, TC_IMPL_IO,
                    "include root contains invalid path text");
    }
    paths->includes[paths->ninclude_roots++] = canonical;
    return 1;
}

int tc_project_paths_prepare(const char *source,
                             const char *const *cli_include_roots,
                             size_t ncli_include_roots,
                             TcProjectPaths **out,
                             TcProjectPathError *error) {
    TcProjectPaths *paths = NULL;
    TcProjectError manifest_error;
    char *directory = NULL;
    const TcPathFile *source_file;
    struct stat st;
    TcImplError impl;
    size_t i;
    int source_missing = 0;
    int manifest_ok;
    if (out == NULL) {
        return fail(error, TC_IMPL_INTERNAL, "missing output");
    }
    *out = NULL;
    clear_error(error);
    if (!tc_path_text_valid(source)) {
        return fail(error, TC_IMPL_USAGE, "source path is not valid UTF-8 text");
    }
    if (ncli_include_roots > TC_PROJECT_MAX_INCLUDE_ROOTS) {
        return fail(error, TC_IMPL_USAGE, "too many include roots");
    }
    for (i = 0u; i < ncli_include_roots; i++) {
        if (!tc_path_text_valid(cli_include_roots[i])) {
            return fail(error, TC_IMPL_USAGE,
                        "include path is not valid UTF-8 text");
        }
    }
    paths = (TcProjectPaths *)calloc(1u, sizeof(*paths));
    if (paths == NULL) {
        return fail(error, TC_IMPL_OOM, "out of memory");
    }
    impl = tc_path_resolve(source, 0, &paths->source, &source_missing);
    if (impl != TC_IMPL_OK) {
        fail(error, impl, impl == TC_IMPL_OOM ? "out of memory"
                                              : "cannot resolve source path");
        goto failed;
    }
    if (!tc_path_text_valid(paths->source)) {
        fail(error, TC_IMPL_IO, "source path contains invalid path text");
        goto failed;
    }
    if (!source_missing) {
        if (stat(paths->source, &st) != 0) {
            fail(error, TC_IMPL_IO, "cannot inspect source path");
            goto failed;
        }
        if (!S_ISREG(st.st_mode)) {
            fail(error, TC_IMPL_IO, "source path is not a regular file");
            goto failed;
        }
    }
    impl = tc_path_directory(paths->source, &directory);
    if (impl != TC_IMPL_OK) {
        fail(error, impl, "out of memory");
        goto failed;
    }
    manifest_ok = tc_project_discover(directory, &paths->manifest,
                                      &manifest_error);
    if (!manifest_ok && !source_missing) {
        fail(error, manifest_error.impl, manifest_error.message);
        goto failed;
    }
    if (!manifest_ok && source_missing) {
        char *anchor = missing_source_anchor(directory);
        if (anchor == NULL) {
            fail(error, TC_IMPL_OOM, "out of memory");
            goto failed;
        }
        if (!synthetic_manifest(anchor, &paths->manifest, error)) {
            free(anchor);
            goto failed;
        }
        free(anchor);
    }
    impl = tc_path_context_create(paths->manifest->root, &paths->context);
    if (impl != TC_IMPL_OK) {
        fail(error, impl == TC_IMPL_OOM ? TC_IMPL_OOM : TC_IMPL_IO,
            impl == TC_IMPL_OOM ? "out of memory"
                                : "project root contains invalid path text");
        goto failed;
    }
    source_file = tc_path_context_add(paths->context, paths->source);
    if (source_file == NULL) {
        fail(error, TC_IMPL_OOM, "out of memory");
        goto failed;
    }
    if (source_missing) {
        (void)set_detail(error, source_file->display);
        fail(error, TC_IMPL_IO, "cannot read source file");
        goto failed;
    }
    if (paths->manifest->ninclude_roots > TC_PROJECT_MAX_INCLUDE_ROOTS ||
        ncli_include_roots > TC_PROJECT_MAX_INCLUDE_ROOTS -
                             paths->manifest->ninclude_roots) {
        fail(error, TC_IMPL_USAGE, "too many include roots");
        goto failed;
    }
    for (i = 0u; i < paths->manifest->ninclude_roots; i++) {
        if (!prepare_include(paths, paths->manifest->includes[i], 1, error)) {
            goto failed;
        }
    }
    for (i = 0u; i < ncli_include_roots; i++) {
        if (!prepare_include(paths, cli_include_roots[i], 0, error)) {
            goto failed;
        }
    }
    free(directory);
    *out = paths;
    return 1;

failed:
    free(directory);
    tc_project_paths_destroy(paths);
    return 0;
}

void tc_project_paths_destroy(TcProjectPaths *paths) {
    size_t i;
    if (paths == NULL) {
        return;
    }
    tc_path_context_destroy(paths->context);
    tc_project_manifest_destroy(paths->manifest);
    free(paths->source);
    for (i = 0u; i < paths->ninclude_roots; i++) {
        free(paths->includes[i]);
    }
    free(paths);
}
