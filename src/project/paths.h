#ifndef TC_PROJECT_PATHS_H
#define TC_PROJECT_PATHS_H

#include <stddef.h>

#include "base/path.h"
#include "diagnostic/diagnostic.h"
#include "project/manifest.h"

typedef struct {
    TcImplError impl;
    const char *message;
    char *detail;
} TcProjectPathError;

typedef struct {
    TcPathContext *context;
    TcProjectManifest *manifest;
    char *source;
    char *includes[TC_PROJECT_MAX_INCLUDE_ROOTS];
    size_t ninclude_roots;
} TcProjectPaths;

void tc_project_path_error_destroy(TcProjectPathError *error);
int tc_project_paths_prepare(const char *source,
                             const char *const *cli_include_roots,
                             size_t ncli_include_roots,
                             TcProjectPaths **out,
                             TcProjectPathError *error);
void tc_project_paths_destroy(TcProjectPaths *paths);

#endif
