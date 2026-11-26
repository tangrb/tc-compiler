#define _GNU_SOURCE

#include "base/path.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct TcPathContext {
    char *root;
    TcPathFile *files;
    size_t count;
    size_t capacity;
    uint64_t next_fallback;
};

static char *duplicate_string(const char *text) {
    size_t size = strlen(text) + 1u;
    char *copy = (char *)malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static int next_utf8(const unsigned char **cursor, const unsigned char *end,
                     uint32_t *codepoint) {
    const unsigned char *bytes = *cursor;
    uint32_t value;
    size_t length;
    size_t i;
    if (bytes >= end) {
        return 0;
    }
    if (bytes[0] < 0x80u) {
        *codepoint = bytes[0];
        *cursor = bytes + 1u;
        return 1;
    }
    if (bytes[0] >= 0xc2u && bytes[0] <= 0xdfu) {
        value = bytes[0] & 0x1fu;
        length = 2u;
    } else if (bytes[0] >= 0xe0u && bytes[0] <= 0xefu) {
        value = bytes[0] & 0x0fu;
        length = 3u;
    } else if (bytes[0] >= 0xf0u && bytes[0] <= 0xf4u) {
        value = bytes[0] & 0x07u;
        length = 4u;
    } else {
        return 0;
    }
    if ((size_t)(end - bytes) < length) {
        return 0;
    }
    for (i = 1u; i < length; i++) {
        if ((bytes[i] & 0xc0u) != 0x80u) {
            return 0;
        }
        value = (value << 6) | (bytes[i] & 0x3fu);
    }
    if ((length == 3u && value < 0x800u) ||
        (length == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu) {
        return 0;
    }
    *codepoint = value;
    *cursor = bytes + length;
    return 1;
}

static int forbidden_path_codepoint(uint32_t codepoint) {
    return codepoint == 0u || codepoint == 0x2028u || codepoint == 0x2029u ||
           (codepoint <= 0x1fu) || (codepoint >= 0x7fu && codepoint <= 0x9fu) ||
           codepoint == 0x00adu ||
           (codepoint >= 0x0600u && codepoint <= 0x0605u) ||
           codepoint == 0x061cu || codepoint == 0x06ddu || codepoint == 0x070fu ||
           (codepoint >= 0x0890u && codepoint <= 0x0891u) ||
           codepoint == 0x08e2u || codepoint == 0x180eu ||
           (codepoint >= 0x200bu && codepoint <= 0x200fu) ||
           (codepoint >= 0x202au && codepoint <= 0x202eu) ||
           (codepoint >= 0x2060u && codepoint <= 0x2064u) ||
           (codepoint >= 0x2066u && codepoint <= 0x206fu) ||
           codepoint == 0xfeffu ||
           (codepoint >= 0xfff9u && codepoint <= 0xfffbu) ||
           codepoint == 0x110bdu || codepoint == 0x110cdu ||
           (codepoint >= 0x13430u && codepoint <= 0x1343fu) ||
           (codepoint >= 0x1bca0u && codepoint <= 0x1bca3u) ||
           (codepoint >= 0x1d173u && codepoint <= 0x1d17au) ||
           codepoint == 0xe0001u || (codepoint >= 0xe0020u && codepoint <= 0xe007fu);
}

int tc_path_text_valid(const char *path) {
    const unsigned char *cursor;
    const unsigned char *end;
    uint32_t codepoint;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    cursor = (const unsigned char *)path;
    end = cursor + strlen(path);
    while (cursor < end) {
        if (!next_utf8(&cursor, end, &codepoint) ||
            forbidden_path_codepoint(codepoint)) {
            return 0;
        }
    }
    return 1;
}

char *tc_path_join(const char *directory, const char *name) {
    size_t dir_len;
    size_t name_len;
    int slash;
    char *out;
    if (directory == NULL || name == NULL) {
        return NULL;
    }
    dir_len = strlen(directory);
    name_len = strlen(name);
    slash = dir_len > 0u && directory[dir_len - 1u] != '/';
    if (dir_len > SIZE_MAX - name_len - (size_t)slash - 1u) {
        return NULL;
    }
    out = (char *)malloc(dir_len + (size_t)slash + name_len + 1u);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, directory, dir_len);
    if (slash) {
        out[dir_len++] = '/';
    }
    memcpy(out + dir_len, name, name_len + 1u);
    return out;
}

static char *normalize_absolute(char *path) {
    size_t length = strlen(path);
    size_t read_at = 1u;
    size_t write_at = 1u;
    while (read_at <= length) {
        size_t start = read_at;
        size_t component;
        while (read_at < length && path[read_at] != '/') {
            read_at++;
        }
        component = read_at - start;
        if (component == 0u || (component == 1u && path[start] == '.')) {
            read_at++;
            continue;
        }
        if (component == 2u && path[start] == '.' && path[start + 1u] == '.') {
            if (write_at > 1u) {
                write_at--;
                while (write_at > 1u && path[write_at - 1u] != '/') {
                    write_at--;
                }
            }
            read_at++;
            continue;
        }
        if (write_at > 1u) {
            path[write_at++] = '/';
        }
        memmove(path + write_at, path + start, component);
        write_at += component;
        read_at++;
    }
    path[write_at] = '\0';
    return path;
}

TcImplError tc_path_absolute(const char *path, char **out) {
    char *cwd;
    char *joined;
    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    *out = NULL;
    if (!tc_path_text_valid(path)) {
        return TC_IMPL_USAGE;
    }
    if (path[0] == '/') {
        *out = duplicate_string(path);
        return *out != NULL ? TC_IMPL_OK : TC_IMPL_OOM;
    }
    cwd = getcwd(NULL, 0u);
    if (cwd == NULL) {
        return errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO;
    }
    joined = tc_path_join(cwd, path);
    free(cwd);
    if (joined == NULL) {
        return TC_IMPL_OOM;
    }
    *out = joined;
    return TC_IMPL_OK;
}

static TcImplError resolve_failure(int error_number, int missing_is_usage) {
    if (error_number == ENOMEM) {
        return TC_IMPL_OOM;
    }
    if (missing_is_usage &&
        (error_number == ENOENT || error_number == ENOTDIR ||
         error_number == ELOOP || error_number == ENAMETOOLONG)) {
        return TC_IMPL_USAGE;
    }
    return TC_IMPL_IO;
}

static TcImplError resolve_missing(const char *absolute, int missing_is_usage,
                                   char **out) {
    char *current = duplicate_string(absolute);
    char *remainder = duplicate_string("");
    if (current == NULL || remainder == NULL) {
        free(current);
        free(remainder);
        return TC_IMPL_OOM;
    }
    for (;;) {
        char *slash = strrchr(current, '/');
        char *component;
        char *resolved;
        char *joined;
        struct stat st;
        int error_number;
        if (slash == NULL) {
            free(current);
            free(remainder);
            return TC_IMPL_INTERNAL;
        }
        component = duplicate_string(slash + 1u);
        if (component == NULL) {
            free(current);
            free(remainder);
            return TC_IMPL_OOM;
        }
        if (remainder[0] == '\0') {
            free(remainder);
            remainder = component;
        } else {
            joined = tc_path_join(component, remainder);
            free(component);
            free(remainder);
            if (joined == NULL) {
                free(current);
                return TC_IMPL_OOM;
            }
            remainder = joined;
        }
        if (slash == current) {
            current[1] = '\0';
        } else {
            *slash = '\0';
        }
        resolved = realpath(current, NULL);
        if (resolved != NULL) {
            if (stat(resolved, &st) != 0) {
                error_number = errno;
                free(resolved);
                free(current);
                free(remainder);
                return resolve_failure(error_number, missing_is_usage);
            }
            if (!S_ISDIR(st.st_mode)) {
                free(resolved);
                free(current);
                free(remainder);
                return missing_is_usage ? TC_IMPL_USAGE : TC_IMPL_IO;
            }
            joined = tc_path_join(resolved, remainder);
            free(resolved);
            free(current);
            free(remainder);
            if (joined == NULL) {
                return TC_IMPL_OOM;
            }
            normalize_absolute(joined);
            *out = joined;
            return TC_IMPL_OK;
        }
        error_number = errno;
        if (error_number != ENOENT && error_number != ENOTDIR) {
            free(current);
            free(remainder);
            return resolve_failure(error_number, missing_is_usage);
        }
        if (lstat(current, &st) == 0) {
            free(current);
            free(remainder);
            return missing_is_usage ? TC_IMPL_USAGE : TC_IMPL_IO;
        }
        if (errno != ENOENT && errno != ENOTDIR) {
            error_number = errno;
            free(current);
            free(remainder);
            return resolve_failure(error_number, missing_is_usage);
        }
        if (strcmp(current, "/") == 0) {
            free(current);
            free(remainder);
            return TC_IMPL_INTERNAL;
        }
    }
}

TcImplError tc_path_resolve(const char *path, int missing_is_usage,
                            char **out, int *out_missing) {
    char *absolute;
    char *resolved;
    int missing = 0;
    struct stat st;
    TcImplError impl;
    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    *out = NULL;
    if (out_missing != NULL) {
        *out_missing = 0;
    }
    impl = tc_path_absolute(path, &absolute);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    resolved = realpath(absolute, NULL);
    if (resolved != NULL) {
        free(absolute);
        *out = resolved;
        return TC_IMPL_OK;
    }
    if (errno == ENOMEM) {
        free(absolute);
        return TC_IMPL_OOM;
    }
    if (errno == ENOENT) {
        if (lstat(absolute, &st) == 0) {
            free(absolute);
            return missing_is_usage ? TC_IMPL_USAGE : TC_IMPL_IO;
        }
        if (errno != ENOENT && errno != ENOTDIR) {
            int error_number = errno;
            free(absolute);
            return resolve_failure(error_number, missing_is_usage);
        }
        missing = 1;
    } else if (errno != ENOTDIR) {
        int error_number = errno;
        free(absolute);
        return resolve_failure(error_number, missing_is_usage);
    } else {
        missing = 1;
    }
    impl = resolve_missing(absolute, missing_is_usage, out);
    free(absolute);
    if (impl == TC_IMPL_OK && out_missing != NULL) {
        *out_missing = missing;
    }
    return impl;
}

TcImplError tc_path_directory(const char *path, char **out) {
    const char *slash;
    size_t length;
    if (out == NULL || path == NULL) {
        return TC_IMPL_INTERNAL;
    }
    *out = NULL;
    slash = strrchr(path, '/');
    if (slash == NULL) {
        *out = duplicate_string(".");
        return *out != NULL ? TC_IMPL_OK : TC_IMPL_OOM;
    }
    length = slash == path ? 1u : (size_t)(slash - path);
    *out = (char *)malloc(length + 1u);
    if (*out == NULL) {
        return TC_IMPL_OOM;
    }
    memcpy(*out, path, length);
    (*out)[length] = '\0';
    return TC_IMPL_OK;
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1u;
}

static char *relative_display(const char *root, const char *target,
                              int *external) {
    size_t root_len = strlen(root);
    size_t target_len = strlen(target);
    size_t common = 0u;
    size_t up = 0u;
    size_t i;
    char *out;
    size_t capacity;
    size_t at = 0u;
    while (common < root_len && common < target_len &&
           root[common] == target[common]) {
        common++;
    }
    if (common < root_len &&
        (common == target_len || root[common] != target[common])) {
        while (common > 0u && root[common - 1u] != '/') {
            common--;
        }
    }
    if (common == root_len &&
        target_len != common && target[common] != '/') {
        while (common > 0u && root[common - 1u] != '/') {
            common--;
        }
    }
    if (common == root_len &&
        (root_len == 1u || target_len == common || target[common] == '/')) {
        if (target[common] == '/') {
            common++;
        }
        *external = 0;
        return duplicate_string(target + common);
    }
    *external = 1;
    for (i = common; i < root_len; i++) {
        if (root[i] != '/' &&
            (i == 0u || root[i - 1u] == '/')) {
            up++;
        }
    }
    capacity = up * 3u + target_len - common + 1u;
    out = (char *)malloc(capacity);
    if (out == NULL) {
        return NULL;
    }
    while (up-- > 0u) {
        memcpy(out + at, "../", 3u);
        at += 3u;
    }
    memcpy(out + at, target + common, target_len - common + 1u);
    return out;
}

TcImplError tc_path_context_create(const char *canonical_root,
                                   TcPathContext **out) {
    TcPathContext *context;
    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    *out = NULL;
    if (canonical_root == NULL || canonical_root[0] != '/' ||
        !tc_path_text_valid(canonical_root)) {
        return TC_IMPL_USAGE;
    }
    context = (TcPathContext *)calloc(1u, sizeof(*context));
    if (context == NULL) {
        return TC_IMPL_OOM;
    }
    context->root = duplicate_string(canonical_root);
    if (context->root == NULL) {
        free(context);
        return TC_IMPL_OOM;
    }
    context->next_fallback = 1u;
    *out = context;
    return TC_IMPL_OK;
}

void tc_path_context_destroy(TcPathContext *context) {
    size_t i;
    if (context == NULL) {
        return;
    }
    for (i = 0u; i < context->count; i++) {
        free(context->files[i].canonical);
        free(context->files[i].display);
    }
    free(context->files);
    free(context->root);
    free(context);
}

const TcPathFile *tc_path_context_find(const TcPathContext *context,
                                       const char *canonical_path) {
    size_t i;
    if (context == NULL || canonical_path == NULL) {
        return NULL;
    }
    for (i = 0u; i < context->count; i++) {
        if (strcmp(context->files[i].canonical, canonical_path) == 0) {
            return &context->files[i];
        }
    }
    return NULL;
}

const TcPathFile *tc_path_context_add(TcPathContext *context,
                                      const char *canonical_path) {
    TcPathFile *file;
    TcPathFile *fresh;
    char *display;
    int external = 0;
    const TcPathFile *existing = tc_path_context_find(context, canonical_path);
    if (existing != NULL) {
        return existing;
    }
    if (context == NULL || canonical_path == NULL || canonical_path[0] != '/' ||
        !tc_path_text_valid(canonical_path)) {
        return NULL;
    }
    display = relative_display(context->root, canonical_path, &external);
    if (display == NULL) {
        return NULL;
    }
    if (display[0] == '\0' || strncmp(display, "./", 2u) == 0) {
        const char *base = path_basename(canonical_path);
        size_t size = strlen(base) + 32u;
        char *fallback = (char *)malloc(size);
        if (fallback == NULL) {
            free(display);
            return NULL;
        }
        (void)snprintf(fallback, size, "./%s#%llu", base,
                       (unsigned long long)context->next_fallback++);
        free(display);
        display = fallback;
        external = 1;
    }
    if (context->count == context->capacity) {
        size_t capacity = context->capacity == 0u ? 8u : context->capacity * 2u;
        if (capacity < context->capacity ||
            capacity > SIZE_MAX / sizeof(*fresh)) {
            free(display);
            return NULL;
        }
        fresh = (TcPathFile *)realloc(context->files,
                                      capacity * sizeof(*fresh));
        if (fresh == NULL) {
            free(display);
            return NULL;
        }
        context->files = fresh;
        context->capacity = capacity;
    }
    file = &context->files[context->count];
    file->canonical = duplicate_string(canonical_path);
    if (file->canonical == NULL) {
        free(display);
        return NULL;
    }
    file->display = display;
    file->external = external;
    context->count++;
    return file;
}

const char *tc_path_display(const TcPathContext *context,
                            const char *canonical_path) {
    const TcPathFile *file = tc_path_context_find(context, canonical_path);
    return file != NULL ? file->display : NULL;
}
