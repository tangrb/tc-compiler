/* Private tc.project parser and nearest-ancestor root discovery. */
#define _GNU_SOURCE

#include "project/manifest.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "project/name.h"

typedef struct {
    const unsigned char *bytes;
    size_t size;
    size_t pos;
} TcManifestCursor;

static void clear_error(TcProjectError *error) {
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }
}

static const char *stable_key(const char *key) {
    static const char *const names[] = {
        "tc-project", "name", "entry", "include", "profile"
    };
    size_t i;
    if (key == NULL) {
        return NULL;
    }
    for (i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(key, names[i]) == 0) {
            return names[i];
        }
    }
    return "unknown";
}

static int set_error(TcProjectError *error, TcImplError impl, const char *message,
                     const char *key, size_t line) {
    if (error != NULL) {
        error->impl = impl;
        error->message = message;
        error->key = stable_key(key);
        error->line = line;
    }
    return 0;
}

static char *duplicate_string(const char *text) {
    size_t size = strlen(text) + 1u;
    char *copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static int ascii_space(unsigned char byte) {
    return byte == (unsigned char)' ' || byte == (unsigned char)'\t';
}

static void skip_space(TcManifestCursor *cursor) {
    while (cursor->pos < cursor->size && ascii_space(cursor->bytes[cursor->pos])) {
        cursor->pos++;
    }
}

static int span_equals(const unsigned char *bytes, size_t size, const char *text) {
    size_t length = strlen(text);
    return size == length && memcmp(bytes, text, length) == 0;
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

static int valid_utf8(const unsigned char *bytes, size_t size) {
    const unsigned char *cursor = bytes;
    const unsigned char *end = bytes + size;
    uint32_t codepoint;
    while (cursor < end) {
        if (!next_utf8(&cursor, end, &codepoint)) {
            return 0;
        }
    }
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

static int valid_path_text(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    const unsigned char *end = cursor + strlen(text);
    uint32_t codepoint;
    if (text[0] == '/') {
        return 0;
    }
    while (cursor < end) {
        if (!next_utf8(&cursor, end, &codepoint) ||
            forbidden_path_codepoint(codepoint)) {
            return 0;
        }
    }
    return 1;
}

static int valid_unquoted_text(const unsigned char *bytes, size_t start, size_t end,
                               TcProjectError *error, size_t line) {
    const unsigned char *cursor = bytes + start;
    const unsigned char *limit = bytes + end;
    int quoted = 0;
    int comment = 0;
    while (cursor < limit) {
        uint32_t codepoint;
        if (!next_utf8(&cursor, limit, &codepoint)) {
            return 0;
        }
        if (quoted) {
            if (codepoint == '"') {
                quoted = 0;
            } else if (codepoint == '\\' && cursor < limit) {
                cursor++;
            }
            continue;
        }
        if (!comment && codepoint == '#') {
            comment = 1;
            continue;
        }
        if (!comment && codepoint == '"') {
            quoted = 1;
            continue;
        }
        if (codepoint != '\t' && forbidden_path_codepoint(codepoint)) {
            return set_error(error, TC_IMPL_USAGE,
                             "control character outside a string", NULL, line);
        }
    }
    return 1;
}

static int valid_profile(const char *profile) {
    static const char *const profiles[] = {
        "aarch64-linux-user", "aarch64-baremetal", "arm32-baremetal",
        "riscv32-baremetal", "w32"
    };
    size_t i;
    for (i = 0u; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        if (strcmp(profile, profiles[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static TcImplError read_error(TcProjectError *error, TcImplError impl,
                              const char *message) {
    (void)set_error(error, impl, message, NULL, 0u);
    return impl;
}

static TcImplError read_manifest(const char *path, unsigned char **out,
                                 size_t *out_size, TcProjectError *error) {
    int fd;
    struct stat before;
    struct stat after;
    unsigned char *bytes;
    size_t expected;
    size_t total = 0u;
    int failed = 0;
    int failure_errno = 0;

    *out = NULL;
    *out_size = 0u;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return read_error(error, errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                          "cannot open tc.project");
    }
    if (fstat(fd, &before) != 0) {
        failure_errno = errno;
        (void)close(fd);
        return read_error(error,
                          failure_errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                          "cannot inspect tc.project");
    }
    if (!S_ISREG(before.st_mode)) {
        (void)close(fd);
        return read_error(error, TC_IMPL_IO, "tc.project is not a regular file");
    }
    if (before.st_size < 0) {
        (void)close(fd);
        return read_error(error, TC_IMPL_IO, "invalid tc.project size");
    }
    if ((uintmax_t)before.st_size > (uintmax_t)TC_PROJECT_MAX_MANIFEST_BYTES) {
        (void)close(fd);
        return read_error(error, TC_IMPL_INTERNAL,
                          "tc.project exceeds the 4 MiB limit");
    }
    expected = (size_t)before.st_size;
    bytes = malloc(expected + 1u);
    if (bytes == NULL) {
        (void)close(fd);
        return read_error(error, TC_IMPL_OOM,
                          "out of memory reading tc.project");
    }
    while (total < expected) {
        ssize_t got = read(fd, bytes + total, expected - total);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            failed = 1;
            failure_errno = errno;
            break;
        }
        if (got == 0) {
            break;
        }
        total += (size_t)got;
    }
    if (!failed && total == expected) {
        ssize_t got;
        do {
            got = read(fd, bytes + total, 1u);
        } while (got < 0 && errno == EINTR);
        if (got < 0) {
            failed = 1;
            failure_errno = errno;
        } else if (got != 0) {
            total++;
        }
    }
    if (fstat(fd, &after) != 0) {
        failed = 1;
        failure_errno = errno;
    } else if (!S_ISREG(after.st_mode) || after.st_size != before.st_size ||
               total > expected) {
        /* Size changes and a non-EOF bounded read are implementation-limit
         * failures, never an invitation to parse a partial manifest. */
        free(bytes);
        (void)close(fd);
        return read_error(error, TC_IMPL_INTERNAL,
                          "tc.project changed or grew while being read");
    } else if (total < expected) {
        failed = 1;
    }
    if (close(fd) != 0) {
        if (!failed) {
            failure_errno = errno;
        }
        failed = 1;
    }
    if (failed) {
        free(bytes);
        return read_error(error,
                          failure_errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                          "cannot read tc.project");
    }
    bytes[expected] = '\0';
    *out = bytes;
    *out_size = expected;
    return TC_IMPL_OK;
}

static int parse_string(TcManifestCursor *cursor, char **out,
                        TcProjectError *error, const char *key, size_t line) {
    size_t start;
    char *text;
    skip_space(cursor);
    if (cursor->pos >= cursor->size || cursor->bytes[cursor->pos] != '"') {
        return set_error(error, TC_IMPL_USAGE, "expected a quoted string", key,
                         line);
    }
    cursor->pos++;
    start = cursor->pos;
    while (cursor->pos < cursor->size) {
        unsigned char byte = cursor->bytes[cursor->pos++];
        if (byte == '"') {
            size_t end = cursor->pos - 1u;
            size_t i;
            size_t output = 0u;
            if (end == start) {
                return set_error(error, TC_IMPL_USAGE, "empty string value", key,
                                 line);
            }
            text = malloc((end - start) + 1u);
            if (text == NULL) {
                return set_error(error, TC_IMPL_OOM, "out of memory", key, line);
            }
            for (i = start; i < end; i++) {
                if (cursor->bytes[i] == '\\') {
                    i++;
                    if (i >= end || (cursor->bytes[i] != '\\' &&
                                     cursor->bytes[i] != '"')) {
                        free(text);
                        return set_error(error, TC_IMPL_USAGE,
                                         "unsupported string escape", key, line);
                    }
                }
                text[output++] = (char)cursor->bytes[i];
            }
            text[output] = '\0';
            skip_space(cursor);
            if (cursor->pos != cursor->size) {
                free(text);
                return set_error(error, TC_IMPL_USAGE,
                                 "unexpected text after string", key, line);
            }
            *out = text;
            return 1;
        }
        if (byte == '\\') {
            if (cursor->pos >= cursor->size ||
                (cursor->bytes[cursor->pos] != '\\' &&
                 cursor->bytes[cursor->pos] != '"')) {
                return set_error(error, TC_IMPL_USAGE, "unsupported string escape",
                                 key, line);
            }
            cursor->pos++;
        }
    }
    return set_error(error, TC_IMPL_USAGE, "unterminated string", key, line);
}

static int store_field(TcProjectManifest *manifest, const char *key, char *value,
                       TcProjectError *error, size_t line) {
    if (strcmp(key, "name") == 0) {
        if (manifest->name != NULL) {
            return set_error(error, TC_IMPL_USAGE, "duplicate key", key, line);
        }
        if (!tc_project_name_valid(value)) {
            return set_error(error, TC_IMPL_USAGE, "invalid project name", key,
                             line);
        }
        manifest->name = value;
        return 1;
    }
    if (strcmp(key, "entry") == 0 || strcmp(key, "include") == 0) {
        char **slot;
        if (strcmp(key, "entry") == 0) {
            if (manifest->entry != NULL) {
                return set_error(error, TC_IMPL_USAGE, "duplicate key", key,
                                 line);
            }
            slot = &manifest->entry;
        } else {
            if (manifest->ninclude_roots >= TC_PROJECT_MAX_INCLUDE_ROOTS) {
                return set_error(error, TC_IMPL_USAGE,
                                 "more than 32 include roots", key, line);
            }
            slot = &manifest->includes[manifest->ninclude_roots];
        }
        if (!valid_path_text(value)) {
            return set_error(error, TC_IMPL_USAGE,
                             "path must be relative, valid UTF-8 text", key, line);
        }
        *slot = value;
        if (strcmp(key, "include") == 0) {
            manifest->ninclude_roots++;
        }
        return 1;
    }
    if (strcmp(key, "profile") == 0) {
        if (manifest->profile != NULL) {
            return set_error(error, TC_IMPL_USAGE, "duplicate key", key, line);
        }
        if (!valid_profile(value)) {
            return set_error(error, TC_IMPL_USAGE, "unknown profile", key, line);
        }
        manifest->profile = value;
        return 1;
    }
    return set_error(error, TC_IMPL_USAGE, "unknown key", key, line);
}

static int parse_manifest(const unsigned char *bytes, size_t size,
                          TcProjectManifest *manifest, TcProjectError *error) {
    size_t pos = 0u;
    size_t line = 1u;
    int header_seen = 0;
    if (size >= 3u && bytes[0] == 0xefu && bytes[1] == 0xbbu &&
        bytes[2] == 0xbfu) {
        pos = 3u;
    }
    if (!valid_utf8(bytes + pos, size - pos)) {
        return set_error(error, TC_IMPL_USAGE, "tc.project is not valid UTF-8",
                         NULL, line);
    }
    while (pos < size) {
        size_t start = pos;
        size_t end;
        size_t logical_end;
        int newline = 0;
        int quoted = 0;
        size_t i;
        TcManifestCursor cursor;
        const unsigned char *key;
        size_t key_size;
        char *value = NULL;

        while (pos < size && bytes[pos] != '\n') {
            pos++;
        }
        end = pos;
        if (pos < size) {
            newline = 1;
            pos++;
        }
        logical_end = end;
        if (newline && logical_end > start && bytes[logical_end - 1u] == '\r') {
            logical_end--;
        }
        for (i = start; i < logical_end; i++) {
            unsigned char byte = bytes[i];
            if ((byte < 0x20u && byte != '\t') || byte == 0x7fu) {
                return set_error(error, TC_IMPL_USAGE,
                                 "control character outside a string", NULL, line);
            }
        }
        if (!valid_unquoted_text(bytes, start, logical_end, error, line)) {
            return 0;
        }
        quoted = 0;
        for (i = start; i < end; i++) {
            if (bytes[i] == '"') {
                quoted = !quoted;
            } else if (bytes[i] == '\\' && quoted && i + 1u < end) {
                i++;
            } else if (bytes[i] == '#' && !quoted) {
                logical_end = i;
                break;
            }
        }
        cursor.bytes = bytes;
        cursor.size = logical_end;
        cursor.pos = start;
        skip_space(&cursor);
        if (cursor.pos == cursor.size) {
            line++;
            continue;
        }
        if (!header_seen) {
            static const char header[] = "tc-project";
            skip_space(&cursor);
            if (cursor.size - cursor.pos < sizeof(header) - 1u ||
                memcmp(cursor.bytes + cursor.pos, header, sizeof(header) - 1u) !=
                    0) {
                return set_error(error, TC_IMPL_USAGE,
                                 "first non-comment line must be tc-project 1",
                                 "tc-project", line);
            }
            cursor.pos += sizeof(header) - 1u;
            if (cursor.pos == cursor.size || !ascii_space(cursor.bytes[cursor.pos])) {
                return set_error(error, TC_IMPL_USAGE, "malformed tc-project header",
                                 "tc-project", line);
            }
            skip_space(&cursor);
            key = cursor.bytes + cursor.pos;
            while (cursor.pos < cursor.size &&
                   !ascii_space(cursor.bytes[cursor.pos])) {
                cursor.pos++;
            }
            key_size = (size_t)(cursor.bytes + cursor.pos - key);
            skip_space(&cursor);
            if (!span_equals(key, key_size, "1") || cursor.pos != cursor.size) {
                return set_error(error, TC_IMPL_USAGE,
                                 "tc-project version must be exactly 1",
                                 "tc-project", line);
            }
            header_seen = 1;
            line++;
            continue;
        }
        key = cursor.bytes + cursor.pos;
        while (cursor.pos < cursor.size &&
               !ascii_space(cursor.bytes[cursor.pos]) &&
               cursor.bytes[cursor.pos] != '=') {
            cursor.pos++;
        }
        key_size = (size_t)(cursor.bytes + cursor.pos - key);
        if (key_size == 0u) {
            return set_error(error, TC_IMPL_USAGE, "missing key", NULL, line);
        }
        if (span_equals(key, key_size, "tc-project")) {
            return set_error(error, TC_IMPL_USAGE, "duplicate key", "tc-project",
                             line);
        }
        skip_space(&cursor);
        if (cursor.pos == cursor.size || cursor.bytes[cursor.pos] != '=') {
            return set_error(error, TC_IMPL_USAGE, "missing '='", NULL, line);
        }
        cursor.pos++;
        {
            char key_text[16];
            if (key_size >= sizeof(key_text)) {
                return set_error(error, TC_IMPL_USAGE, "unknown key", NULL, line);
            }
            memcpy(key_text, key, key_size);
            key_text[key_size] = '\0';
            if (!parse_string(&cursor, &value, error, key_text, line)) {
                return 0;
            }
            if (!store_field(manifest, key_text, value, error, line)) {
                free(value);
                return 0;
            }
        }
        line++;
    }
    if (!header_seen) {
        return set_error(error, TC_IMPL_USAGE, "missing tc-project header",
                         "tc-project", 1u);
    }
    return 1;
}

static const char *root_basename(const char *root) {
    const char *slash = strrchr(root, '/');
    return slash == NULL ? root : slash + 1;
}

static int apply_defaults(TcProjectManifest *manifest, int include_default,
                          TcProjectError *error) {
    if (manifest->name == NULL) {
        manifest->name = duplicate_string(root_basename(manifest->root));
        if (manifest->name == NULL) {
            return set_error(error, TC_IMPL_OOM, "out of memory", "name", 0u);
        }
        if (!tc_project_name_valid(manifest->name)) {
            return set_error(error, TC_IMPL_USAGE, "invalid default project name",
                             "name", 0u);
        }
    }
    if (manifest->entry == NULL) {
        manifest->entry = duplicate_string("src/main.tc");
        if (manifest->entry == NULL) {
            return set_error(error, TC_IMPL_OOM, "out of memory", "entry", 0u);
        }
    }
    if (manifest->profile == NULL) {
        manifest->profile = duplicate_string("aarch64-linux-user");
        if (manifest->profile == NULL) {
            return set_error(error, TC_IMPL_OOM, "out of memory", "profile", 0u);
        }
    }
    if (include_default && manifest->ninclude_roots == 0u) {
        manifest->includes[0] = duplicate_string("src");
        if (manifest->includes[0] == NULL) {
            return set_error(error, TC_IMPL_OOM, "out of memory", "include", 0u);
        }
        manifest->ninclude_roots = 1u;
    }
    return 1;
}

int tc_project_manifest_load(const char *manifest_path, const char *project_root,
                             TcProjectManifest **out, TcProjectError *error) {
    TcProjectManifest *manifest;
    unsigned char *bytes;
    size_t size;
    TcImplError impl;
    if (out == NULL) {
        return set_error(error, TC_IMPL_INTERNAL, "missing output", NULL, 0u);
    }
    *out = NULL;
    clear_error(error);
    if (manifest_path == NULL || project_root == NULL || project_root[0] == '\0') {
        return set_error(error, TC_IMPL_INTERNAL, "invalid manifest arguments",
                         NULL, 0u);
    }
    manifest = calloc(1u, sizeof(*manifest));
    if (manifest == NULL) {
        return set_error(error, TC_IMPL_OOM, "out of memory", NULL, 0u);
    }
    manifest->root = duplicate_string(project_root);
    manifest->manifest_path = duplicate_string(manifest_path);
    if (manifest->root == NULL || manifest->manifest_path == NULL) {
        tc_project_manifest_destroy(manifest);
        return set_error(error, TC_IMPL_OOM, "out of memory", NULL, 0u);
    }
    impl = read_manifest(manifest_path, &bytes, &size, error);
    if (impl != TC_IMPL_OK) {
        tc_project_manifest_destroy(manifest);
        return 0;
    }
    manifest->manifest_present = 1;
    if (!parse_manifest(bytes, size, manifest, error)) {
        free(bytes);
        tc_project_manifest_destroy(manifest);
        return 0;
    }
    free(bytes);
    if (!apply_defaults(manifest, 1, error)) {
        tc_project_manifest_destroy(manifest);
        return 0;
    }
    *out = manifest;
    return 1;
}

static char *join_manifest_path(const char *directory) {
    size_t length = strlen(directory);
    int slash = length == 0u || directory[length - 1u] != '/';
    static const char name[] = "tc.project";
    char *path = malloc(length + (size_t)slash + sizeof(name));
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, directory, length);
    if (slash) {
        path[length++] = '/';
    }
    memcpy(path + length, name, sizeof(name));
    return path;
}

int tc_project_discover(const char *start_directory, TcProjectManifest **out,
                        TcProjectError *error) {
    char *resolved;
    char *start;
    char *current;
    struct stat st;
    if (out == NULL) {
        return set_error(error, TC_IMPL_INTERNAL, "missing output", NULL, 0u);
    }
    *out = NULL;
    clear_error(error);
    if (start_directory == NULL || start_directory[0] == '\0') {
        return set_error(error, TC_IMPL_INTERNAL, "missing start directory", NULL,
                         0u);
    }
    resolved = realpath(start_directory, NULL);
    if (resolved == NULL) {
        return set_error(error, errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                         "cannot canonicalize start directory", NULL, 0u);
    }
    if (stat(resolved, &st) != 0) {
        int error_number = errno;
        free(resolved);
        return set_error(error,
                         error_number == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                         "cannot inspect start directory", NULL, 0u);
    }
    if (!S_ISDIR(st.st_mode)) {
        free(resolved);
        return set_error(error, TC_IMPL_IO, "start path is not a directory", NULL,
                         0u);
    }
    start = duplicate_string(resolved);
    current = duplicate_string(resolved);
    free(resolved);
    if (start == NULL || current == NULL) {
        free(start);
        free(current);
        return set_error(error, TC_IMPL_OOM, "out of memory", NULL, 0u);
    }
    for (;;) {
        char *candidate = join_manifest_path(current);
        struct stat entry;
        int found;
        if (candidate == NULL) {
            free(start);
            free(current);
            return set_error(error, TC_IMPL_OOM, "out of memory", NULL, 0u);
        }
        found = lstat(candidate, &entry) == 0;
        if (!found && errno != ENOENT && errno != ENOTDIR) {
            int error_number = errno;
            free(candidate);
            free(start);
            free(current);
            return set_error(error,
                             error_number == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                             "cannot search for tc.project", NULL, 0u);
        }
        if (found) {
            TcProjectManifest *manifest;
            int ok = tc_project_manifest_load(candidate, current, &manifest, error);
            free(candidate);
            free(start);
            free(current);
            if (!ok) {
                return 0;
            }
            *out = manifest;
            return 1;
        }
        free(candidate);
        if (strcmp(current, "/") == 0) {
            TcProjectManifest *manifest = calloc(1u, sizeof(*manifest));
            if (manifest == NULL) {
                free(start);
                free(current);
                return set_error(error, TC_IMPL_OOM, "out of memory", NULL, 0u);
            }
            manifest->root = start;
            free(current);
            if (!apply_defaults(manifest, 0, error)) {
                tc_project_manifest_destroy(manifest);
                return 0;
            }
            *out = manifest;
            return 1;
        }
        {
            char *slash = strrchr(current, '/');
            if (slash == current) {
                current[1] = '\0';
            } else if (slash != NULL) {
                *slash = '\0';
            } else {
                free(start);
                free(current);
                return set_error(error, TC_IMPL_INTERNAL, "invalid canonical root",
                                 NULL, 0u);
            }
        }
    }
}

void tc_project_manifest_destroy(TcProjectManifest *manifest) {
    size_t i;
    if (manifest == NULL) {
        return;
    }
    free(manifest->root);
    free(manifest->manifest_path);
    free(manifest->name);
    free(manifest->entry);
    free(manifest->profile);
    for (i = 0u; i < manifest->ninclude_roots; i++) {
        free(manifest->includes[i]);
    }
    free(manifest);
}
