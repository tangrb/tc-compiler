/* Private tc.project parser and nearest-ancestor root discovery. */
#ifndef TC_PROJECT_MANIFEST_H
#define TC_PROJECT_MANIFEST_H

#include <stddef.h>

#include "diagnostic/diagnostic.h"

#define TC_PROJECT_MAX_MANIFEST_BYTES (4u * 1024u * 1024u)
#define TC_PROJECT_MAX_INCLUDE_ROOTS 32u

typedef struct {
    TcImplError impl;       /* map exits through tc_impl_error_exit */
    const char *message;    /* static text, valid until the next API call */
    const char *key;        /* static field name, or NULL */
    size_t line;            /* one-based manifest line; zero for host errors */
} TcProjectError;

typedef struct {
    char *root;             /* canonical anchor directory */
    char *manifest_path;    /* canonical root plus the tc.project entry */
    int manifest_present;
    char *name;
    char *entry;
    char *profile;
    char *includes[TC_PROJECT_MAX_INCLUDE_ROOTS];
    size_t ninclude_roots;  /* manifest roots, or the manifest's default src */
} TcProjectManifest;

/* project_root must be the canonical directory that owns the manifest entry.
 * A manifest symlink is followed for content, never for the project root. */
int tc_project_manifest_load(const char *manifest_path, const char *project_root,
                             TcProjectManifest **out, TcProjectError *error);
int tc_project_manifest_load_fd(int fd, const char *project_root,
                                TcProjectManifest **out, TcProjectError *error);

/* start_directory is canonicalized, then tc.project entries are searched from
 * that directory through its parents.  With no entry, root is start_directory
 * and only command-line include roots apply (ninclude_roots is zero). */
int tc_project_discover(const char *start_directory, TcProjectManifest **out,
                        TcProjectError *error);

/* Frees the manifest and every string/array element owned by it. */
void tc_project_manifest_destroy(TcProjectManifest *manifest);

#endif
