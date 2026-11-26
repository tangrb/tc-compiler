#ifndef TC_BASE_PATH_H
#define TC_BASE_PATH_H

#include <stddef.h>

#include "diagnostic/diagnostic.h"

typedef struct TcPathContext TcPathContext;

typedef struct {
    char *canonical;
    char *display;
    int external;
} TcPathFile;

int tc_path_text_valid(const char *path);
char *tc_path_join(const char *directory, const char *name);
TcImplError tc_path_absolute(const char *path, char **out);
TcImplError tc_path_resolve(const char *path, int missing_is_usage,
                            char **out, int *out_missing);
TcImplError tc_path_directory(const char *path, char **out);
TcImplError tc_path_context_create(const char *canonical_root,
                                   TcPathContext **out);
void tc_path_context_destroy(TcPathContext *context);
const TcPathFile *tc_path_context_add(TcPathContext *context,
                                      const char *canonical_path);
const TcPathFile *tc_path_context_find(const TcPathContext *context,
                                       const char *canonical_path);
const char *tc_path_display(const TcPathContext *context,
                            const char *canonical_path);

#endif
