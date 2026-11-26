#define _GNU_SOURCE
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "driver/test_runner.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "base/fd.h"
#include "base/path.h"
#include "driver/cli.h"
#include "driver/profile_select.h"
#include "frontend/input.h"
#include "frontend/module.h"
#include "frontend/sem.h"
#include "project/manifest.h"
#include "runtime/io.h"
#include "vm/vm.h"

#define TC_TEST_MAGIC 0x54435452u
#define TC_TEST_PROTOCOL 1u
#define TC_TEST_MAX_MAIN (4u * 1024u * 1024u)
#define TC_TEST_MAX_TEXT (4u * 1024u)
#define TC_TEST_STDERR_KEEP (64u * 1024u)
#define TC_TEST_STDOUT_WINDOW (32u * 1024u)
#define TC_TEST_IO_CHUNK (64u * 1024u)

typedef enum {
    TC_RESOLVE_FOUND = 0,
    TC_RESOLVE_MISSING,
    TC_RESOLVE_USAGE,
    TC_RESOLVE_INVALID,
    TC_RESOLVE_IO,
    TC_RESOLVE_OOM
} TcResolveStatus;

typedef struct {
    char *name;
    char *logical;
    char *canonical;
    char *identity;
    struct stat dir_stat;
    struct stat main_stat;
    uint64_t main_size;
    uint64_t main_hash;
    int64_t main_generation;
    int mode;
    int sidecar_state[4];
    struct stat sidecar_stat[4];
} TcTestCase;

typedef struct {
    struct stat stat;
    int valid;
} TcIdentity;

typedef struct {
    TcProjectManifest *manifest;
    TcPathContext *paths;
    TcPlatformProfile profile;
    int root_fd;
    int tests_fd;
    char **includes;
    int *include_fds;
    size_t nincludes;
    TcTestCase *tests;
    size_t ntests;
    char **selected_names;
    size_t nselected_names;
    TcIdentity *identities;
    size_t nidentities;
    const char *const *filters;
    size_t nfilters;
    uint64_t timeout_seconds;
    const char *timeout_text;
    TcImplError error;
    char detail[TC_TEST_MAX_TEXT + 512u];
    int stderr_alias;
    int preflight_done;
    TcIdentity stdio_identity[3];
} TcTestRunner;

typedef struct {
    int fd;
    uint64_t size;
    uint64_t hash;
    int64_t generation;
    struct stat stat;
} TcSnapshot;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t status;
    int32_t impl;
    uint32_t trap;
    uint32_t exit_status;
} TcWorkerResult;

_Static_assert(sizeof(TcWorkerResult) == 24u, "worker result frame must be 24 bytes");

typedef struct {
    int expected_fd;
    int actual_fd;
    uint64_t expected_size;
    uint64_t actual_size;
    uint64_t compared;
    uint64_t first_difference;
    int has_difference;
    int eof_difference;
    unsigned char *stderr;
    size_t stderr_size;
    uint64_t stderr_total;
} TcCapture;

static const char *const sidecar_names[4] = {
    "stdout.bin", "stdin.bin", "trap", "exit"
};

static int same_identity(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int private_temp_path(char path[PATH_MAX]) {
    const char *directory = getenv("TMPDIR");
    int length;
    if (directory == NULL || directory[0] == '\0') {
        directory = "/tmp";
    }
    length = snprintf(path, PATH_MAX, "%s/tc-test-XXXXXX", directory);
    return length >= 0 && (size_t)length < PATH_MAX;
}

static int missing_errno(int error_number) {
    return error_number == ENOENT || error_number == ENOTDIR;
}

static int write_all_fd(int fd, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    while (size != 0u) {
        ssize_t wrote = write(fd, bytes, size);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (wrote == 0) {
            errno = EIO;
            return 0;
        }
        bytes += (size_t)wrote;
        size -= (size_t)wrote;
    }
    return 1;
}

static int read_all_fd(int fd, void *data, size_t size) {
    unsigned char *bytes = (unsigned char *)data;
    while (size != 0u) {
        ssize_t got = read(fd, bytes, size);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (got == 0) {
            return 0;
        }
        bytes += (size_t)got;
        size -= (size_t)got;
    }
    return 1;
}

static int stream_write(FILE *stream, const void *data, size_t size) {
    return size == 0u || fwrite(data, 1u, size, stream) == size;
}

static uint64_t hash_bytes(uint64_t hash, const unsigned char *bytes,
                           size_t size) {
    size_t i;
    for (i = 0u; i < size; i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int hash_fd(int fd, uint64_t limit, uint64_t *hash_out,
                   uint64_t *size_out) {
    unsigned char buffer[TC_TEST_IO_CHUNK];
    uint64_t hash = UINT64_C(1469598103934665603);
    uint64_t total = 0u;
    for (;;) {
        size_t want = sizeof(buffer);
        ssize_t got;
        if (limit != UINT64_MAX) {
            uint64_t remaining = limit - total;
            if (remaining < want) {
                want = (size_t)remaining + 1u;
            }
        }
        got = read(fd, buffer, want);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (got == 0) {
            break;
        }
        hash = hash_bytes(hash, buffer, (size_t)got);
        total += (uint64_t)got;
        if (limit != UINT64_MAX && total > limit) {
            errno = EFBIG;
            return 0;
        }
    }
    *size_out = total;
    *hash_out = hash;
    return 1;
}

static int64_t file_generation(const struct stat *st) {
#ifdef __APPLE__
    return (int64_t)st->st_gen;
#else
    return (int64_t)st->st_mtim.tv_sec * INT64_C(1000000000) +
           (int64_t)st->st_mtim.tv_nsec;
#endif
}

static int stat_changed(const struct stat *before, const struct stat *after,
                        int check_generation) {
    if (!same_identity(before, after) || before->st_size != after->st_size) {
        return 1;
    }
    return check_generation &&
           file_generation(before) != file_generation(after);
}

static char *fd_path(int fd, struct stat *expected) {
    char path[PATH_MAX];
#ifdef F_GETPATH
    if (fcntl(fd, F_GETPATH, path) == 0 && path[0] == '/' &&
        stat(path, expected) == 0) {
        return strdup(path);
    }
#else
    (void)expected;
#endif
#if defined(__linux__)
    {
        char link[64];
        ssize_t length;
        (void)snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
        length = readlink(link, path, sizeof(path) - 1u);
        if (length >= 0) {
            struct stat st;
            path[length] = '\0';
            if (path[0] == '/' && stat(path, &st) == 0 &&
                same_identity(&st, expected)) {
                return strdup(path);
            }
        }
    }
#endif
    return NULL;
}

static int dir_within(int dir_fd, int boundary_fd) {
    int current = dup(dir_fd);
    struct stat boundary;
    int result = 0;
    if (current < 0 || fstat(boundary_fd, &boundary) != 0) {
        if (current >= 0) {
            (void)close(current);
        }
        return 0;
    }
    for (;;) {
        struct stat st;
        int parent;
        if (fstat(current, &st) != 0) {
            break;
        }
        if (same_identity(&st, &boundary)) {
            result = 1;
            break;
        }
        parent = tc_fd_openat(current, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                         O_NOFOLLOW);
        (void)close(current);
        if (parent < 0) {
            break;
        }
        {
            struct stat parent_stat;
            if (fstat(parent, &parent_stat) != 0 ||
                same_identity(&st, &parent_stat)) {
                (void)close(parent);
                break;
            }
        }
        current = parent;
    }
    if (current >= 0) {
        (void)close(current);
    }
    return result;
}

static char *prepend_components(const char *target, const char *remainder) {
    size_t a = strlen(target);
    size_t b = strlen(remainder);
    char *joined;
    if (a > SIZE_MAX - b - 2u) {
        return NULL;
    }
    joined = (char *)malloc(a + b + 2u);
    if (joined == NULL) {
        return NULL;
    }
    memcpy(joined, target, a);
    joined[a] = '/';
    memcpy(joined + a + 1u, remainder, b + 1u);
    return joined;
}

static TcResolveStatus resolve_error(int error_number) {
    if (error_number == ENOMEM) {
        return TC_RESOLVE_OOM;
    }
    if (error_number == ELOOP || error_number == ENOTDIR ||
        error_number == ENAMETOOLONG) {
        return TC_RESOLVE_INVALID;
    }
    return TC_RESOLVE_IO;
}

static TcResolveStatus resolve_path(int anchor_fd, int boundary_fd,
                                    const char *path, int *out_fd,
                                    int *out_missing) {
    int current = dup(anchor_fd);
    char *queue;
    unsigned links = 0u;
    TcResolveStatus result = TC_RESOLVE_IO;
    *out_fd = -1;
    *out_missing = 0;
    if (path[0] == '/') {
        if (boundary_fd >= 0) {
            if (current >= 0) {
                (void)close(current);
            }
            return TC_RESOLVE_USAGE;
        }
        (void)close(current);
        current = tc_fd_open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (current < 0) {
        return resolve_error(errno);
    }
    queue = strdup(path);
    if (queue == NULL) {
        (void)close(current);
        return TC_RESOLVE_OOM;
    }
    while (queue[0] != '\0') {
        char *slash = strchr(queue, '/');
        char *remainder;
        char *component;
        struct stat st;
        if (slash != NULL) {
            *slash = '\0';
            remainder = slash + 1u;
        } else {
            remainder = queue + strlen(queue);
        }
        component = queue;
        if (component[0] == '\0' || strcmp(component, ".") == 0) {
            if (slash == NULL) {
                queue[0] = '\0';
                continue;
            }
            memmove(queue, remainder, strlen(remainder) + 1u);
            continue;
        }
        if (strcmp(component, "..") == 0) {
            int parent = tc_fd_openat(current, "..", O_RDONLY | O_DIRECTORY |
                                                 O_CLOEXEC | O_NOFOLLOW);
            if (parent < 0) {
                result = resolve_error(errno);
                goto done;
            }
            if (boundary_fd >= 0 && !dir_within(parent, boundary_fd)) {
                (void)close(parent);
                result = TC_RESOLVE_USAGE;
                goto done;
            }
            (void)close(current);
            current = parent;
        } else if (fstatat(current, component, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            if (missing_errno(errno) &&
                !(boundary_fd < 0 && errno == ENOTDIR)) {
                *out_missing = 1;
                result = TC_RESOLVE_MISSING;
                goto done;
            }
            result = resolve_error(errno);
            goto done;
        } else if (S_ISLNK(st.st_mode)) {
            char target[PATH_MAX + 1u];
            ssize_t length = readlinkat(current, component, target,
                                        sizeof(target) - 1u);
            char *joined;
            if (length < 0) {
                result = resolve_error(errno);
                goto done;
            }
            target[length] = '\0';
            if (target[0] == '/') {
                if (boundary_fd >= 0) {
                    result = TC_RESOLVE_USAGE;
                    goto done;
                }
                (void)close(current);
                current = tc_fd_open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                         O_NOFOLLOW);
                if (current < 0) {
                    result = resolve_error(errno);
                    goto done;
                }
                {
                    char *absolute = prepend_components(target + 1, remainder);
                    if (absolute == NULL) {
                        result = TC_RESOLVE_OOM;
                        goto done;
                    }
                    free(queue);
                    queue = absolute;
                }
                continue;
            }
            if (++links > 40u) {
                result = TC_RESOLVE_INVALID;
                goto done;
            }
            joined = prepend_components(target, remainder);
            if (joined == NULL) {
                result = TC_RESOLVE_OOM;
                goto done;
            }
            free(queue);
            queue = joined;
            continue;
        } else if (remainder[0] != '\0') {
            int child = tc_fd_openat(current, component,
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child < 0) {
                result = resolve_error(errno);
                goto done;
            }
            if (boundary_fd >= 0 && !dir_within(child, boundary_fd)) {
                (void)close(child);
                result = TC_RESOLVE_USAGE;
                goto done;
            }
            (void)close(current);
            current = child;
        } else {
            int fd = tc_fd_openat(current, component,
                            O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) {
                if (errno == ELOOP) {
                    result = TC_RESOLVE_IO;
                } else if (missing_errno(errno) &&
                           !(boundary_fd < 0 && errno == ENOTDIR)) {
                    *out_missing = 1;
                    result = TC_RESOLVE_MISSING;
                } else {
                    result = resolve_error(errno);
                }
                goto done;
            }
            if (fstat(fd, &st) != 0) {
                int error_number = errno;
                (void)close(fd);
                result = resolve_error(error_number);
                goto done;
            }
            *out_fd = fd;
            result = TC_RESOLVE_FOUND;
            goto done;
        }
        if (slash == NULL) {
            queue[0] = '\0';
        } else {
            memmove(queue, remainder, strlen(remainder) + 1u);
        }
    }
    {
        struct stat st;
        if (fstat(current, &st) != 0) {
            result = resolve_error(errno);
        } else {
            *out_fd = current;
            current = -1;
            result = TC_RESOLVE_FOUND;
        }
    }

done:
    if (current >= 0) {
        (void)close(current);
    }
    free(queue);
    return result;
}

static TcResolveStatus resolve_beneath(int anchor_fd, int boundary_fd,
                                       const char *path, int *out_fd,
                                       int *out_missing) {
    return resolve_path(anchor_fd, boundary_fd, path, out_fd, out_missing);
}

static int runner_fail(TcTestRunner *runner, TcImplError error,
                       const char *detail) {
    if (runner->error == TC_IMPL_OK) {
        runner->error = error;
        if (detail != NULL) {
            (void)snprintf(runner->detail, sizeof(runner->detail), "%s",
                           detail);
        }
    }
    return 0;
}

static int add_identity(TcTestRunner *runner, const struct stat *st) {
    TcIdentity *fresh;
    size_t i;
    for (i = 0u; i < runner->nidentities; i++) {
        if (same_identity(&runner->identities[i].stat, st)) {
            return 1;
        }
    }
    fresh = (TcIdentity *)realloc(
        runner->identities, (runner->nidentities + 1u) * sizeof(*fresh));
    if (fresh == NULL) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    runner->identities = fresh;
    fresh[runner->nidentities].stat = *st;
    fresh[runner->nidentities].valid = 1;
    runner->nidentities++;
    return 1;
}

static int identity_index(const TcTestRunner *runner, const struct stat *st) {
    size_t i;
    for (i = 0u; i < runner->nidentities; i++) {
        if (same_identity(&runner->identities[i].stat, st)) {
            return (int)i;
        }
    }
    return -1;
}

static int check_output_aliases(TcTestRunner *runner) {
    struct stat input;
    struct stat output0;
    struct stat output1;
    int valid_input;
    int valid0;
    int valid1;
    valid_input = runner->stdio_identity[STDIN_FILENO].valid;
    input = runner->stdio_identity[STDIN_FILENO].stat;
    valid0 = runner->stdio_identity[STDOUT_FILENO].valid;
    output0 = runner->stdio_identity[STDOUT_FILENO].stat;
    valid1 = runner->stdio_identity[STDERR_FILENO].valid;
    output1 = runner->stdio_identity[STDERR_FILENO].stat;
    if (valid_input && valid1 && same_identity(&input, &output1)) {
        runner->error = TC_IMPL_USAGE;
        runner->detail[0] = '\0';
        runner->stderr_alias = 1;
        return 0;
    }
    if (valid0 && identity_index(runner, &output0) >= 0) {
        runner->error = TC_IMPL_USAGE;
        runner->detail[0] = '\0';
        runner->stderr_alias = 1;
        return 0;
    }
    if (valid1 && identity_index(runner, &output1) >= 0) {
        runner->error = TC_IMPL_USAGE;
        runner->detail[0] = '\0';
        runner->stderr_alias = 1;
        return 0;
    }
    return 1;
}

typedef struct {
    int file_fd;
    int parent_fd;
} TcImportScan;

static TcImplError import_scan_parent(int fd, int *out_parent_fd,
                                      TcTestRunner *runner) {
    struct stat st;
    char *canonical;
    int parent_fd = -1;
    int status;
    *out_parent_fd = -1;
    errno = 0;
    canonical = fd_path(fd, &st);
    if (canonical == NULL) {
        TcImplError error = errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO;
        (void)runner_fail(runner, error,
                          error == TC_IMPL_OOM ? "out of memory"
                                                : "cannot bind source parent");
        return error;
    }
    status = tc_module_bound_parent(fd, canonical, &parent_fd);
    free(canonical);
    if (status < 0) {
        TcImplError error = status == -1 ? TC_IMPL_OOM : TC_IMPL_IO;
        (void)runner_fail(runner, error,
                          error == TC_IMPL_OOM ? "out of memory"
                                                : "cannot bind source parent");
        return error;
    }
    if (status == 0) {
        (void)runner_fail(runner, TC_IMPL_INTERNAL,
                          "cannot verify source parent identity");
        return TC_IMPL_INTERNAL;
    }
    *out_parent_fd = parent_fd;
    return TC_IMPL_OK;
}

static int scan_source_bytes(const unsigned char *bytes, size_t size,
                             TcTestRunner *runner, int parent_fd,
                             int *root_fds, size_t nroot_fds,
                             TcImportScan **queue,
                             size_t *count, size_t *capacity,
                             TcIdentity **visited, size_t *nvisited,
                             size_t *visited_capacity) {
    size_t offset = 0u;
    int ok = 1;
    while (ok && offset < size) {
        TcArena arena;
        TcLexResult lex;
        size_t chunk_start = offset;
        size_t end = offset;
        size_t chunk_end;
        size_t i;
        for (;;) {
            while (end < size && bytes[end] != '\n' && bytes[end] != '\r') {
                end++;
            }
            if (end < size && end > chunk_start && bytes[end - 1u] == '\\') {
                if (bytes[end] == '\r' && end + 1u < size &&
                    bytes[end + 1u] == '\n') {
                    end += 2u;
                } else {
                    end++;
                }
                continue;
            }
            break;
        }
        chunk_end = end;
        if (end < size) {
            if (bytes[end] == '\r' && end + 1u < size &&
                bytes[end + 1u] == '\n') {
                chunk_end = end + 2u;
            } else {
                chunk_end = end + 1u;
            }
        }
        memset(&lex, 0, sizeof(lex));
        tc_arena_init(&arena, NULL);
        (void)tc_lex("", bytes + chunk_start, chunk_end - chunk_start,
                     &arena, &lex);
        if (lex.impl == TC_IMPL_OOM) {
            ok = runner_fail(runner, TC_IMPL_OOM, "out of memory");
            goto done;
        }
        for (i = 0u; ok && i + 1u < lex.count; i++) {
        const TcToken *keyword = &lex.tokens[i];
        const TcToken *name = &lex.tokens[i + 1u];
        char *named;
        size_t root;
        size_t visited_index;
        int fd;
        struct stat st;
        if (keyword->kind != TC_TOK_IMPORT || name->kind != TC_TOK_IDENT) {
            continue;
        }
        named = (char *)malloc(name->text_len + 4u);
        if (named == NULL) {
            ok = runner_fail(runner, TC_IMPL_OOM, "out of memory");
            break;
        }
        memcpy(named, name->text, name->text_len);
        memcpy(named + name->text_len, ".tc", 4u);
        for (root = 0u; ok && root <= nroot_fds; root++) {
            int search_fd = root == 0u ? parent_fd : root_fds[root - 1u];
            if (search_fd < 0) {
                continue;
            }
            fd = tc_fd_openat(search_fd, named,
                        O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                int error_number = errno;
                if (error_number != ENOENT && error_number != ENOTDIR) {
                    ok = runner_fail(runner, error_number == ENOMEM
                                                   ? TC_IMPL_OOM : TC_IMPL_IO,
                                     "cannot inspect import candidate");
                    break;
                }
                continue;
            }
            if (fstat(fd, &st) != 0) {
                int error_number = errno;
                (void)close(fd);
                ok = runner_fail(runner, error_number == ENOMEM
                                               ? TC_IMPL_OOM : TC_IMPL_IO,
                                 "cannot inspect import candidate");
                break;
            }
            if (!add_identity(runner, &st)) {
                (void)close(fd);
                ok = 0;
                break;
            }
            if (!S_ISREG(st.st_mode) || st.st_size < 0 ||
                (uint64_t)st.st_size > TC_TEST_MAX_MAIN) {
                (void)close(fd);
                continue;
            }
            for (visited_index = 0u; visited_index < *nvisited;
                 visited_index++) {
                if (same_identity(&(*visited)[visited_index].stat, &st)) {
                    break;
                }
            }
            if (visited_index != *nvisited) {
                (void)close(fd);
                continue;
            }
            if (*nvisited == *visited_capacity) {
                size_t next = *visited_capacity == 0u
                                  ? 16u : *visited_capacity * 2u;
                TcIdentity *fresh = (TcIdentity *)realloc(
                    *visited, next * sizeof(*fresh));
                if (fresh == NULL) {
                    (void)close(fd);
                    ok = runner_fail(runner, TC_IMPL_OOM, "out of memory");
                    break;
                }
                *visited = fresh;
                *visited_capacity = next;
            }
            (*visited)[*nvisited].stat = st;
            (*visited)[*nvisited].valid = 1;
            (*nvisited)++;
            if (*count == *capacity) {
                size_t next = *capacity == 0u ? 16u : *capacity * 2u;
                TcImportScan *fresh = (TcImportScan *)realloc(
                    *queue, next * sizeof(*fresh));
                if (fresh == NULL) {
                    (void)close(fd);
                    ok = runner_fail(runner, TC_IMPL_OOM, "out of memory");
                    break;
                }
                *queue = fresh;
                *capacity = next;
            }
            {
                TcImplError parent_error = import_scan_parent(
                    fd, &(*queue)[*count].parent_fd, runner);
                if (parent_error != TC_IMPL_OK) {
                    (void)close(fd);
                    ok = 0;
                    break;
                }
            }
            (*queue)[*count].file_fd = fd;
            (*count)++;
        }
        free(named);
    }
done:
        tc_arena_destroy(&arena);
        offset = chunk_end;
    }
    return ok;
}

static int read_scan_source(int fd, unsigned char **out, size_t *out_size) {
    struct stat st;
    unsigned char *bytes;
    size_t expected;
    size_t total = 0u;
    *out = NULL;
    *out_size = 0u;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > TC_TEST_MAX_MAIN) {
        return 0;
    }
    expected = (size_t)st.st_size;
    bytes = (unsigned char *)malloc(expected + 1u);
    if (bytes == NULL) {
        return -1;
    }
    while (total < expected) {
        ssize_t got = pread(fd, bytes + total, expected - total, (off_t)total);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            free(bytes);
            return 0;
        }
        total += (size_t)got;
    }
    bytes[expected] = 0u;
    *out = bytes;
    *out_size = expected;
    return 1;
}

static int preflight_output_aliases(TcTestRunner *runner) {
    int *root_fds = NULL;
    size_t nroot_fds = 0u;
    size_t root_capacity = 0u;
    TcImportScan *queue = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    size_t cursor = 0u;
    TcIdentity *visited = NULL;
    size_t nvisited = 0u;
    size_t visited_capacity = 0u;
    size_t i;
    int ok = 1;
    for (i = 0u; i < runner->nselected_names; i++) {
        int dir_fd = -1;
        int main_fd = -1;
        int missing = 0;
        TcResolveStatus status = resolve_beneath(
            runner->tests_fd, runner->tests_fd, runner->selected_names[i],
            &dir_fd, &missing);
        if (status != TC_RESOLVE_FOUND) {
            TcImplError error = status == TC_RESOLVE_OOM ? TC_IMPL_OOM
                                 : status == TC_RESOLVE_IO ? TC_IMPL_IO
                                                          : TC_IMPL_INTERNAL;
            ok = runner_fail(runner, error,
                             status == TC_RESOLVE_MISSING
                                 ? "test directory changed before preflight"
                                 : "cannot resolve test directory");
            goto done;
        }
        if (nroot_fds == root_capacity) {
            size_t next = root_capacity == 0u ? 16u : root_capacity * 2u;
            int *fresh = (int *)realloc(root_fds, next * sizeof(*fresh));
            if (fresh == NULL) {
                (void)close(dir_fd);
                ok = runner_fail(runner, TC_IMPL_OOM, "out of memory");
                goto done;
            }
            root_fds = fresh;
            root_capacity = next;
        }
        root_fds[nroot_fds] = dup(dir_fd);
        if (root_fds[nroot_fds] < 0) {
            int error_number = errno;
            (void)close(dir_fd);
            ok = runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                            : TC_IMPL_IO,
                             error_number == ENOMEM ? "out of memory"
                                                    : "cannot duplicate root fd");
            goto done;
        }
        nroot_fds++;
        {
            unsigned sidecar;
            for (sidecar = 0u; sidecar < 4u; sidecar++) {
                int fd = -1;
                struct stat st;
                status = resolve_beneath(dir_fd, dir_fd,
                                         sidecar_names[sidecar], &fd, &missing);
                if (status != TC_RESOLVE_FOUND && status != TC_RESOLVE_MISSING) {
                    if (status != TC_RESOLVE_INVALID && status != TC_RESOLVE_USAGE) {
                        TcImplError error = status == TC_RESOLVE_OOM
                                                ? TC_IMPL_OOM : TC_IMPL_IO;
                        (void)close(dir_fd);
                        ok = runner_fail(runner, error,
                                         error == TC_IMPL_OOM
                                             ? "out of memory"
                                             : "cannot resolve sidecar");
                        goto done;
                    }
                    continue;
                }
                if (status == TC_RESOLVE_FOUND && fstat(fd, &st) != 0) {
                    int error_number = errno;
                    (void)close(fd);
                    (void)close(dir_fd);
                    ok = runner_fail(runner, error_number == ENOMEM
                                                   ? TC_IMPL_OOM : TC_IMPL_IO,
                                     "cannot inspect sidecar");
                    goto done;
                }
                if (status == TC_RESOLVE_FOUND) {
                    if (!add_identity(runner, &st)) {
                        (void)close(fd);
                        (void)close(dir_fd);
                        ok = 0;
                        goto done;
                    }
                }
                if (fd >= 0) {
                    (void)close(fd);
                }
            }
        }
        status = resolve_beneath(dir_fd, dir_fd, "main.tc", &main_fd, &missing);
        if (status != TC_RESOLVE_FOUND && status != TC_RESOLVE_MISSING) {
            if (status != TC_RESOLVE_INVALID && status != TC_RESOLVE_USAGE) {
                TcImplError error = status == TC_RESOLVE_OOM
                                        ? TC_IMPL_OOM : TC_IMPL_IO;
                (void)close(dir_fd);
                ok = runner_fail(runner, error,
                                 error == TC_IMPL_OOM ? "out of memory"
                                                       : "cannot resolve main.tc");
                goto done;
            }
            (void)close(dir_fd);
            continue;
        }
        if (status == TC_RESOLVE_FOUND) {
            struct stat st;
            if (fstat(main_fd, &st) != 0) {
                int error_number = errno;
                (void)close(main_fd);
                (void)close(dir_fd);
                ok = runner_fail(runner, error_number == ENOMEM
                                               ? TC_IMPL_OOM : TC_IMPL_IO,
                                 "cannot inspect main.tc");
                goto done;
            }
            if (S_ISREG(st.st_mode)) {
                if (!add_identity(runner, &st)) {
                    (void)close(main_fd);
                    (void)close(dir_fd);
                    ok = 0;
                    goto done;
                }
                if (count == capacity) {
                    size_t next = capacity == 0u ? 16u : capacity * 2u;
                    TcImportScan *fresh = (TcImportScan *)realloc(
                        queue, next * sizeof(*fresh));
                    if (fresh == NULL) {
                        (void)close(main_fd);
                        (void)close(dir_fd);
                        ok = runner_fail(runner, TC_IMPL_OOM,
                                         "out of memory");
                        goto done;
                    }
                    queue = fresh;
                    capacity = next;
                }
                queue[count].file_fd = main_fd;
                TcImplError parent_error = import_scan_parent(
                    main_fd, &queue[count].parent_fd, runner);
                if (parent_error != TC_IMPL_OK) {
                    (void)close(main_fd);
                    (void)close(dir_fd);
                    ok = 0;
                    goto done;
                }
                count++;
            } else {
                (void)close(main_fd);
            }
        }
        (void)close(dir_fd);
    }
    for (i = 0u; i < runner->nincludes; i++) {
        if (runner->include_fds[i] < 0) {
            continue;
        }
        if (nroot_fds == root_capacity) {
            size_t next = root_capacity == 0u ? 16u : root_capacity * 2u;
            int *fresh = (int *)realloc(root_fds, next * sizeof(*fresh));
            if (fresh == NULL) {
                ok = runner_fail(runner, TC_IMPL_OOM, "out of memory");
                goto done;
            }
            root_fds = fresh;
            root_capacity = next;
        }
        root_fds[nroot_fds] = dup(runner->include_fds[i]);
        if (root_fds[nroot_fds] < 0) {
            int error_number = errno;
            ok = runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                            : TC_IMPL_IO,
                             error_number == ENOMEM ? "out of memory"
                                                    : "cannot duplicate root fd");
            goto done;
        }
        nroot_fds++;
    }
    while (cursor < count && ok) {
        unsigned char *bytes = NULL;
        size_t size = 0u;
        int status = read_scan_source(queue[cursor].file_fd, &bytes, &size);
        if (status < 0) {
            ok = runner_fail(runner, TC_IMPL_OOM, "out of memory");
        } else if (status > 0) {
            ok = scan_source_bytes(bytes, size, runner,
                                   queue[cursor].parent_fd, root_fds,
                                   nroot_fds, &queue, &count, &capacity,
                                   &visited,
                                   &nvisited, &visited_capacity);
        } else {
            ok = runner_fail(runner, TC_IMPL_INTERNAL,
                             "cannot read source for alias preflight");
        }
        free(bytes);
        (void)close(queue[cursor].file_fd);
        (void)close(queue[cursor].parent_fd);
        cursor++;
    }
    if (ok) {
        runner->preflight_done = 1;
        ok = check_output_aliases(runner);
    }
done:
    while (cursor < count) {
        (void)close(queue[cursor].file_fd);
        (void)close(queue[cursor].parent_fd);
        cursor++;
    }
    free(queue);
    free(visited);
    for (i = 0u; i < nroot_fds; i++) {
        (void)close(root_fds[i]);
    }
    free(root_fds);
    return ok;
}

static void close_snapshot(TcSnapshot *snapshot) {
    if (snapshot->fd >= 0) {
        (void)close(snapshot->fd);
    }
    snapshot->fd = -1;
}

static int bind_private_snapshot(const char *path, int writer_fd,
                                 int *readonly_fd) {
    struct stat writer_stat;
    struct stat readonly_stat;
    int fd = -1;
    if (fchmod(writer_fd, S_IRUSR) != 0 ||
        fcntl(writer_fd, F_SETFD, FD_CLOEXEC) < 0 ||
        fstat(writer_fd, &writer_stat) != 0) {
        return 0;
    }
#ifdef __linux__
    char fd_link[64];
    if (unlink(path) != 0) {
        return 0;
    }
    (void)snprintf(fd_link, sizeof(fd_link), "/proc/self/fd/%d", writer_fd);
    fd = tc_fd_open(fd_link, O_RDONLY | O_CLOEXEC);
#else
    fd = tc_fd_open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0 && unlink(path) != 0) {
        int error_number = errno;
        (void)close(fd);
        errno = error_number;
        return 0;
    }
#endif
    if (fd < 0 || fstat(fd, &readonly_stat) != 0 ||
        !same_identity(&readonly_stat, &writer_stat) ||
        (fcntl(fd, F_GETFL) & O_ACCMODE) != O_RDONLY) {
        int error_number = errno;
        if (fd >= 0) {
            (void)close(fd);
        }
#ifndef __linux__
        (void)unlink(path);
#endif
        errno = error_number;
        return 0;
    }
    *readonly_fd = fd;
    return 1;
}

static int snapshot_from_fd(TcSnapshot *snapshot, int source_fd,
                            uint64_t limit, TcImplError mutation_error,
                            TcTestRunner *runner) {
    char path[PATH_MAX];
    struct stat before;
    struct stat after;
    int fd = -1;
    int readonly_fd = -1;
    unsigned char buffer[TC_TEST_IO_CHUNK];
    uint64_t total = 0u;
    uint64_t hash = UINT64_C(1469598103934665603);
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->fd = -1;
    if (fstat(source_fd, &before) != 0) {
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect input file");
    }
    if (!S_ISREG(before.st_mode) || before.st_size < 0) {
        return runner_fail(runner, TC_IMPL_IO, "input is not a regular file");
    }
    if (limit != UINT64_MAX && (uint64_t)before.st_size > limit) {
        return runner_fail(runner, limit == TC_TEST_MAX_MAIN
                                       ? TC_IMPL_INTERNAL : TC_IMPL_USAGE,
                           limit == TC_TEST_MAX_MAIN
                               ? "source exceeds 4 MiB"
                               : "sidecar exceeds 4 KiB");
    }
    if (!private_temp_path(path)) {
        return runner_fail(runner, TC_IMPL_IO,
                           "temporary directory path is too long");
    }
    fd = tc_fd_relocate(mkstemp(path));
    if (fd < 0) {
        return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                   : TC_IMPL_IO,
                           "cannot create private snapshot");
    }
    if (!bind_private_snapshot(path, fd, &readonly_fd)) {
        int error_number = errno;
        (void)close(fd);
        return runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                          : TC_IMPL_IO,
                           "cannot bind read-only snapshot");
    }
    for (;;) {
        size_t want = sizeof(buffer);
        ssize_t got;
        if (limit != UINT64_MAX && limit - total < want) {
            want = (size_t)(limit - total) + 1u;
        }
        if (total > (uint64_t)INT64_MAX) {
            (void)close(readonly_fd);
            (void)close(fd);
            return runner_fail(runner, TC_IMPL_IO,
                               "snapshot exceeds supported offset range");
        }
        got = pread(source_fd, buffer, want, (off_t)total);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)close(readonly_fd);
            (void)close(fd);
            return runner_fail(runner, TC_IMPL_IO, "cannot read input file");
        }
        if (got == 0) {
            break;
        }
        total += (uint64_t)got;
        if (limit != UINT64_MAX && total > limit) {
            (void)close(readonly_fd);
            (void)close(fd);
            return runner_fail(runner, limit == TC_TEST_MAX_MAIN
                                           ? TC_IMPL_INTERNAL
                                           : TC_IMPL_USAGE,
                               limit == TC_TEST_MAX_MAIN
                                   ? "source exceeds 4 MiB"
                                   : "sidecar exceeds 4 KiB");
        }
        hash = hash_bytes(hash, buffer, (size_t)got);
        if (!write_all_fd(fd, buffer, (size_t)got)) {
            (void)close(readonly_fd);
            (void)close(fd);
            return runner_fail(runner, TC_IMPL_IO,
                               "cannot write private snapshot");
        }
    }
    if (fstat(source_fd, &after) != 0 || stat_changed(&before, &after, 1) ||
        (uint64_t)after.st_size != total) {
        (void)close(readonly_fd);
        (void)close(fd);
        return runner_fail(runner, mutation_error,
                           "input changed while being snapshotted");
    }
    if (close(fd) != 0) {
        int error_number = errno;
        (void)close(readonly_fd);
        return runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                          : TC_IMPL_IO,
                           "cannot close snapshot writer");
    }
    snapshot->fd = readonly_fd;
    snapshot->size = total;
    snapshot->hash = hash;
    snapshot->generation = file_generation(&after);
    snapshot->stat = after;
    return 1;
}

static int read_small_file(int fd, unsigned char **out, size_t *out_size,
                           uint64_t limit, TcTestRunner *runner,
                           int usage_on_limit) {
    struct stat st;
    struct stat after;
    unsigned char *bytes;
    size_t expected;
    size_t total = 0u;
    *out = NULL;
    *out_size = 0u;
    if (fstat(fd, &st) != 0) {
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect input file");
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0) {
        return runner_fail(runner, TC_IMPL_IO, "input is not a regular file");
    }
    if ((uint64_t)st.st_size > limit) {
        return runner_fail(runner, usage_on_limit ? TC_IMPL_USAGE
                                                 : TC_IMPL_INTERNAL,
                           usage_on_limit ? "sidecar exceeds 4 KiB"
                                          : "source exceeds 4 MiB");
    }
    expected = (size_t)st.st_size;
    bytes = (unsigned char *)malloc(expected + 1u);
    if (bytes == NULL) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    while (total <= expected) {
        size_t want = total < expected ? expected - total : 1u;
        ssize_t got = read(fd, bytes + total, want);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(bytes);
            return runner_fail(runner, TC_IMPL_IO, "cannot read input file");
        }
        if (got == 0) {
            break;
        }
        total += (size_t)got;
    }
    if (fstat(fd, &after) != 0) {
        free(bytes);
        return runner_fail(runner, TC_IMPL_IO, "cannot recheck input file");
    }
    if (total > expected || stat_changed(&st, &after, 1)) {
        free(bytes);
        return runner_fail(runner, total > expected && usage_on_limit
                                       ? TC_IMPL_USAGE : TC_IMPL_INTERNAL,
                           total > expected && usage_on_limit
                               ? "sidecar exceeds 4 KiB"
                               : "input changed while being read");
    }
    if (total != expected) {
        free(bytes);
        return runner_fail(runner, TC_IMPL_IO, "short input read");
    }
    bytes[expected] = 0u;
    *out = bytes;
    *out_size = expected;
    return 1;
}

static int mode_token(const unsigned char *bytes, size_t at, size_t end,
                      const char *token, size_t length) {
    return at + length <= end && memcmp(bytes + at, token, length) == 0 &&
           (at + length == end ||
            !((bytes[at + length] >= 'A' && bytes[at + length] <= 'Z') ||
              (bytes[at + length] >= 'a' && bytes[at + length] <= 'z') ||
              (bytes[at + length] >= '0' && bytes[at + length] <= '9') ||
              bytes[at + length] == '_' || bytes[at + length] >= 0x80u));
}

static int detect_mode(const unsigned char *bytes, size_t size) {
    size_t at = 0u;
    while (at < size) {
        size_t end = at;
        size_t start;
        while (end < size && bytes[end] != '\n' && bytes[end] != '\r') {
            end++;
        }
        start = at;
        while (start < end && (bytes[start] == ' ' || bytes[start] == '\t')) {
            start++;
        }
        if (start + 3u <= end && bytes[start] == '/' &&
            bytes[start + 1u] == '/' && bytes[start + 2u] == '/') {
            start = end;
        }
        if (start < end) {
            if (mode_token(bytes, start, end, "#program", 8u)) {
                return 1;
            }
            if (mode_token(bytes, start, end, "#lib", 4u)) {
                return 2;
            }
            return 0;
        }
        if (end == size) {
            break;
        }
        if (bytes[end] == '\r' && end + 1u < size && bytes[end + 1u] == '\n') {
            end++;
        }
        at = end + 1u;
    }
    return 0;
}

static int name_selected(const TcTestRunner *runner, const char *name) {
    size_t i;
    if (runner->nfilters == 0u) {
        return 1;
    }
    for (i = 0u; i < runner->nfilters; i++) {
        if (strcmp(runner->filters[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int validate_one(TcTestRunner *runner, const char *name,
                        TcTestCase *test) {
    int dir_fd = -1;
    int main_fd = -1;
    int main_is_link = 0;
    int missing = 0;
    int direct_is_link = 0;
    int status;
    struct stat dir_stat;
    struct stat main_stat;
    struct stat main_entry;
    struct stat path_stat;
    unsigned char *bytes = NULL;
    size_t size = 0u;
    char *identity = NULL;
    char *canonical = NULL;
    TcResolveStatus resolved;
    memset(test, 0, sizeof(*test));
    test->name = strdup(name);
    test->logical = (char *)malloc(strlen(name) + 7u);
    if (test->name == NULL || test->logical == NULL) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    strcpy(test->logical, "tests/");
    strcat(test->logical, name);
    if (fstatat(runner->tests_fd, name, &dir_stat,
                AT_SYMLINK_NOFOLLOW) != 0) {
        if (missing_errno(errno)) {
            return 1;
        }
        return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                   : TC_IMPL_IO,
                           "cannot inspect test entry");
    }
    direct_is_link = S_ISLNK(dir_stat.st_mode);
    resolved = resolve_beneath(runner->tests_fd, runner->tests_fd, name,
                               &dir_fd, &missing);
    if (resolved == TC_RESOLVE_OOM) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    if (resolved == TC_RESOLVE_USAGE) {
        return runner_fail(runner, TC_IMPL_USAGE,
                           "test directory escapes tests tree");
    }
    if (resolved == TC_RESOLVE_IO || resolved == TC_RESOLVE_INVALID) {
        return runner_fail(runner, TC_IMPL_IO, "cannot resolve test directory");
    }
    if (resolved == TC_RESOLVE_MISSING) {
        if (direct_is_link) {
            return runner_fail(runner, TC_IMPL_USAGE,
                               "dangling test directory");
        }
        return 1;
    }
    if (fstat(dir_fd, &dir_stat) != 0) {
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect test directory");
    }
    if (!S_ISDIR(dir_stat.st_mode)) {
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_USAGE,
                           "test entry is not a real directory");
    }
    if (fstatat(dir_fd, "main.tc", &main_entry, AT_SYMLINK_NOFOLLOW) == 0) {
        main_is_link = S_ISLNK(main_entry.st_mode);
    } else if (!missing_errno(errno)) {
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect main.tc");
    }
    resolved = resolve_beneath(dir_fd, dir_fd, "main.tc", &main_fd, &missing);
    if (resolved == TC_RESOLVE_OOM) {
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    if (resolved == TC_RESOLVE_USAGE) {
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_USAGE,
                           "main.tc escapes its test directory");
    }
    if (resolved == TC_RESOLVE_IO || resolved == TC_RESOLVE_INVALID) {
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_IO, "cannot resolve main.tc");
    }
    if (resolved == TC_RESOLVE_MISSING) {
        (void)close(dir_fd);
        if (main_is_link) {
            return runner_fail(runner, TC_IMPL_IO, "cannot resolve main.tc");
        }
        return 1;
    }
    if (fstat(main_fd, &main_stat) != 0) {
        (void)close(main_fd);
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect main.tc");
    }
    if (!S_ISREG(main_stat.st_mode)) {
        (void)close(main_fd);
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_IO, "main.tc is not a regular file");
    }
    if (!read_small_file(main_fd, &bytes, &size, TC_TEST_MAX_MAIN, runner, 0)) {
        (void)close(main_fd);
        (void)close(dir_fd);
        return 0;
    }
    if (lseek(main_fd, 0u, SEEK_SET) < 0 ||
        !hash_fd(main_fd, TC_TEST_MAX_MAIN, &test->main_hash,
                 &test->main_size) ||
        test->main_size != (uint64_t)size) {
        free(bytes);
        (void)close(main_fd);
        (void)close(dir_fd);
        return runner_fail(runner, errno == EFBIG ? TC_IMPL_INTERNAL
                                                  : TC_IMPL_IO,
                           "cannot fingerprint main.tc");
    }
    test->main_hash = hash_bytes(UINT64_C(1469598103934665603), bytes, size);
    test->main_generation = file_generation(&main_stat);
    test->mode = detect_mode(bytes, size);
    free(bytes);
    if (test->mode == 2) {
        (void)close(main_fd);
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_USAGE,
                           "root module must be #program");
    }
    canonical = fd_path(main_fd, &path_stat);
    if (canonical == NULL || !same_identity(&path_stat, &main_stat)) {
        free(canonical);
        (void)close(main_fd);
        (void)close(dir_fd);
        return runner_fail(runner, TC_IMPL_IO,
                           "cannot determine main.tc referent path");
    }
    {
        int parent_fd = -1;
        int bound = tc_module_bound_parent(main_fd, canonical, &parent_fd);
        int within = bound > 0 && dir_within(parent_fd, dir_fd);
        if (parent_fd >= 0) {
            (void)close(parent_fd);
        }
        if (!within) {
            free(canonical);
            (void)close(main_fd);
            (void)close(dir_fd);
            return runner_fail(runner, bound < 0 ? TC_IMPL_OOM : TC_IMPL_IO,
                               "cannot bind main.tc referent directory");
        }
    }
    status = tc_module_bound_identity(main_fd, canonical, &identity);
    if (status <= 0) {
        free(canonical);
        (void)close(main_fd);
        (void)close(dir_fd);
        return runner_fail(runner, status < 0 ? TC_IMPL_OOM : TC_IMPL_IO,
                           "cannot bind main.tc identity");
    }
    test->canonical = canonical;
    test->identity = identity;
    test->dir_stat = dir_stat;
    test->main_stat = main_stat;
    if (!add_identity(runner, &dir_stat) || !add_identity(runner, &main_stat)) {
        (void)close(main_fd);
        (void)close(dir_fd);
        return 0;
    }
    (void)close(main_fd);
    (void)close(dir_fd);
    return 2;
}

static int compare_name_pointers(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int append_name(char ***names, size_t *count, size_t *capacity,
                       const char *name) {
    char *copy;
    if (*count == *capacity) {
        size_t next = *capacity == 0u ? 16u : *capacity * 2u;
        char **fresh = (char **)realloc(*names, next * sizeof(**names));
        if (fresh == NULL) {
            return 0;
        }
        *names = fresh;
        *capacity = next;
    }
    copy = strdup(name);
    if (copy == NULL) {
        return 0;
    }
    (*names)[(*count)++] = copy;
    return 1;
}

static void free_names(char **names, size_t count) {
    size_t i;
    for (i = 0u; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

static int discover_tests(TcTestRunner *runner) {
    DIR *directory = NULL;
    struct dirent *entry;
    char **names = NULL;
    char **known_names = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    size_t known_count = 0u;
    size_t known_capacity = 0u;
    size_t i;
    int scan_fd = dup(runner->tests_fd);
    if (scan_fd < 0) {
        return runner_fail(runner, TC_IMPL_IO, "cannot duplicate tests fd");
    }
    directory = fdopendir(scan_fd);
    if (directory == NULL) {
        int error_number = errno;
        (void)close(scan_fd);
        return runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                          : TC_IMPL_IO,
                           "cannot enumerate tests");
    }
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        struct stat st;
        int known;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            errno = 0;
            continue;
        }
        if (!tc_path_text_valid(entry->d_name)) {
            (void)closedir(directory);
            free_names(names, count);
            free_names(known_names, known_count);
            return runner_fail(runner, TC_IMPL_USAGE,
                               "test name is not valid UTF-8 text");
        }
        if (fstatat(runner->tests_fd, entry->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            int error_number = errno;
            (void)closedir(directory);
            free_names(names, count);
            free_names(known_names, known_count);
            return runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                              : TC_IMPL_IO,
                               "cannot inspect test entry");
        }
        known = S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode);
        if (known && !append_name(&known_names, &known_count, &known_capacity,
                                  entry->d_name)) {
            (void)closedir(directory);
            free_names(names, count);
            free_names(known_names, known_count);
            return runner_fail(runner, TC_IMPL_OOM, "out of memory");
        }
        if (name_selected(runner, entry->d_name) &&
            (runner->nfilters == 0u || known) &&
            !append_name(&names, &count, &capacity, entry->d_name)) {
            (void)closedir(directory);
            free_names(names, count);
            free_names(known_names, known_count);
            return runner_fail(runner, TC_IMPL_OOM, "out of memory");
        }
        errno = 0;
    }
    if (errno != 0) {
        int error_number = errno;
        (void)closedir(directory);
        free_names(names, count);
        free_names(known_names, known_count);
        return runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                          : TC_IMPL_IO,
                           "cannot enumerate tests");
    }
    if (closedir(directory) != 0) {
        free_names(names, count);
        free_names(known_names, known_count);
        return runner_fail(runner, TC_IMPL_IO, "cannot close tests directory");
    }
    for (i = 0u; i < runner->nfilters; i++) {
        size_t j;
        int found = 0;
        if (!tc_path_text_valid(runner->filters[i])) {
            free_names(names, count);
            free_names(known_names, known_count);
            return runner_fail(runner, TC_IMPL_USAGE,
                               "test filter is not valid UTF-8 text");
        }
        for (j = 0u; j < known_count; j++) {
            if (strcmp(known_names[j], runner->filters[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            free_names(names, count);
            free_names(known_names, known_count);
            return runner_fail(runner, TC_IMPL_USAGE, "unknown test name");
        }
    }
    free_names(known_names, known_count);
    qsort(names, count, sizeof(*names), compare_name_pointers);
    runner->tests = (TcTestCase *)calloc(count == 0u ? 1u : count,
                                         sizeof(*runner->tests));
    runner->selected_names = (char **)calloc(count == 0u ? 1u : count,
                                              sizeof(*runner->selected_names));
    if (runner->tests == NULL || runner->selected_names == NULL) {
        free(runner->tests);
        runner->tests = NULL;
        free_names(names, count);
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    for (i = 0u; i < count; i++) {
        runner->selected_names[runner->nselected_names] = strdup(names[i]);
        if (runner->selected_names[runner->nselected_names] == NULL) {
            size_t remaining;
            for (remaining = i; remaining < count; remaining++) {
                free(names[remaining]);
            }
            free(names);
            return runner_fail(runner, TC_IMPL_OOM, "out of memory");
        }
        runner->nselected_names++;
    }
    for (i = 0u; i < count; i++) {
        TcTestCase *current = &runner->tests[runner->ntests];
        int validated = validate_one(runner, names[i], current);
        free(names[i]);
        if (validated == 0) {
            size_t remaining;
            free(current->name);
            free(current->logical);
            free(current->canonical);
            free(current->identity);
            for (remaining = i + 1u; remaining < count; remaining++) {
                free(names[remaining]);
            }
            free(names);
            return 0;
        }
        if (validated == 2) {
            runner->ntests++;
        } else {
            free(current->name);
            free(current->logical);
        }
    }
    free(names);
    return 1;
}

static int discover_project(TcTestRunner *runner) {
    int current = tc_fd_open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) {
        return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                   : TC_IMPL_IO,
                           "cannot bind current directory");
    }
    for (;;) {
        struct stat root_stat;
        struct stat entry;
        struct stat manifest_stat;
        char *root = fd_path(current, &root_stat);
        TcProjectError error;
        int found;
        if (root == NULL) {
            (void)close(current);
            return runner_fail(runner, TC_IMPL_IO,
                               "cannot determine project directory");
        }
        found = fstatat(current, "tc.project", &entry,
                       AT_SYMLINK_NOFOLLOW) == 0;
        if (!found && !missing_errno(errno)) {
            free(root);
            (void)close(current);
            return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                       : TC_IMPL_IO,
                               "cannot search for tc.project");
        }
        if (found) {
            int manifest_fd = tc_fd_openat(current, "tc.project",
                                     O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            int ok;
            if (manifest_fd < 0) {
                free(root);
                (void)close(current);
                return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                           : TC_IMPL_IO,
                                   "cannot open tc.project");
            }
            ok = tc_project_manifest_load_fd(manifest_fd, root,
                                             &runner->manifest, &error);
            if (fstat(manifest_fd, &manifest_stat) != 0) {
                ok = 0;
                error.impl = TC_IMPL_IO;
                error.message = "cannot inspect tc.project";
            }
            if (close(manifest_fd) != 0 && ok) {
                ok = 0;
                error.impl = TC_IMPL_IO;
                error.message = "cannot close tc.project";
            }
            free(root);
            if (!ok) {
                (void)close(current);
                return runner_fail(runner, error.impl, error.message);
            }
            runner->root_fd = current;
            if (!add_identity(runner, &root_stat) ||
                !add_identity(runner, &manifest_stat)) {
                return 0;
            }
            return 1;
        }
        free(root);
        {
            int parent = tc_fd_openat(current, "..",
                                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (parent < 0) {
                (void)close(current);
                return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                           : TC_IMPL_IO,
                                   "cannot search parent directory");
            }
            if (fstat(parent, &entry) != 0) {
                int error_number = errno;
                (void)close(parent);
                (void)close(current);
                return runner_fail(runner,
                                   error_number == ENOMEM ? TC_IMPL_OOM
                                                          : TC_IMPL_IO,
                                   "cannot inspect parent directory");
            }
            if (same_identity(&root_stat, &entry)) {
                (void)close(parent);
                (void)close(current);
                return runner_fail(runner, TC_IMPL_USAGE,
                                   "project test requires tc.project");
            }
            (void)close(current);
            current = parent;
        }
    }
}

static int bind_include(TcTestRunner *runner, const char *include,
                        char **out_path, int *out_fd) {
    int fd = -1;
    int missing = 0;
    TcResolveStatus status = resolve_path(runner->root_fd, -1, include,
                                          &fd, &missing);
    *out_path = NULL;
    *out_fd = -1;
    if (status == TC_RESOLVE_OOM) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    if (status == TC_RESOLVE_USAGE || status == TC_RESOLVE_INVALID) {
        return runner_fail(runner, TC_IMPL_USAGE, "invalid include path");
    }
    if (status == TC_RESOLVE_IO) {
        return runner_fail(runner, TC_IMPL_IO, "cannot resolve include path");
    }
    if (status == TC_RESOLVE_MISSING) {
        char *lexical = tc_path_join(runner->manifest->root, include);
        TcImplError impl;
        int absent = 0;
        if (lexical == NULL) {
            return runner_fail(runner, TC_IMPL_OOM, "out of memory");
        }
        impl = tc_path_resolve(lexical, 1, out_path, &absent);
        free(lexical);
        if (impl != TC_IMPL_OK || !absent) {
            return runner_fail(runner, impl == TC_IMPL_OOM ? TC_IMPL_OOM
                                                          : TC_IMPL_USAGE,
                               impl == TC_IMPL_OOM ? "out of memory"
                                                   : "invalid include path");
        }
        return 1;
    }
    {
        struct stat st;
        if (fstat(fd, &st) != 0) {
            (void)close(fd);
            return runner_fail(runner, TC_IMPL_IO, "cannot inspect include root");
        }
        if (!S_ISDIR(st.st_mode)) {
            (void)close(fd);
            return runner_fail(runner, TC_IMPL_USAGE,
                               "include root is not a directory");
        }
        *out_path = fd_path(fd, &st);
        if (*out_path == NULL) {
            (void)close(fd);
            return runner_fail(runner, TC_IMPL_IO,
                               "cannot determine include root path");
        }
    }
    *out_fd = fd;
    return 1;
}

static int prepare_runner(TcTestRunner *runner, const char *const *filters,
                          size_t nfilters, uint64_t timeout_seconds,
                          const char *timeout_text) {
    size_t i;
    int found;
    struct stat st;
    runner->filters = filters;
    runner->nfilters = nfilters;
    runner->timeout_seconds = timeout_seconds;
    runner->timeout_text = timeout_text;
    if (!discover_project(runner)) {
        return 0;
    }
    found = fstatat(runner->root_fd, "tests", &st, AT_SYMLINK_NOFOLLOW) == 0;
    if (found && S_ISLNK(st.st_mode)) {
        return runner_fail(runner, TC_IMPL_USAGE,
                           "tests must be a real directory");
    }
    if (!found && !missing_errno(errno)) {
        return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                   : TC_IMPL_IO,
                           "cannot inspect tests directory");
    }
    runner->tests_fd = tc_fd_openat(runner->root_fd, "tests",
                              O_RDONLY | O_DIRECTORY | O_NONBLOCK |
                                  O_CLOEXEC | O_NOFOLLOW);
    if (runner->tests_fd < 0) {
        if (missing_errno(errno)) {
            if (nfilters != 0u) {
                return runner_fail(runner, TC_IMPL_USAGE,
                                   "unknown test name");
            }
        } else {
            return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                       : TC_IMPL_IO,
                               "cannot open tests directory");
        }
    } else if (fstat(runner->tests_fd, &st) != 0 ||
               !add_identity(runner, &st)) {
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect tests directory");
    }
    if (tc_path_context_create(runner->manifest->root, &runner->paths) !=
        TC_IMPL_OK) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    if (!tc_driver_select_profile(runner->manifest->profile, &runner->profile)) {
        return runner_fail(runner, TC_IMPL_USAGE,
                           "project profile is not executable");
    }
    runner->includes = (char **)calloc(
        runner->manifest->ninclude_roots == 0u
            ? 1u : runner->manifest->ninclude_roots,
        sizeof(*runner->includes));
    runner->include_fds = (int *)malloc(
        (runner->manifest->ninclude_roots == 0u
             ? 1u : runner->manifest->ninclude_roots) * sizeof(int));
    if (runner->includes == NULL || runner->include_fds == NULL) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    for (i = 0u; i < runner->manifest->ninclude_roots; i++) {
        if (!bind_include(runner, runner->manifest->includes[i],
                          &runner->includes[runner->nincludes],
                          &runner->include_fds[runner->nincludes])) {
            return 0;
        }
        runner->nincludes++;
    }
    {
        int discovered = 1;
        if (runner->tests_fd >= 0) {
            discovered = discover_tests(runner);
        }
        if (!preflight_output_aliases(runner)) {
            return 0;
        }
        return discovered && check_output_aliases(runner);
    }
}

static void destroy_runner(TcTestRunner *runner) {
    size_t i;
    if (runner->tests != NULL) {
        for (i = 0u; i < runner->ntests; i++) {
            free(runner->tests[i].name);
            free(runner->tests[i].logical);
            free(runner->tests[i].canonical);
            free(runner->tests[i].identity);
        }
    }
    free(runner->tests);
    free_names(runner->selected_names, runner->nselected_names);
    for (i = 0u; i < runner->nincludes; i++) {
        free(runner->includes[i]);
        if (runner->include_fds[i] >= 0) {
            (void)close(runner->include_fds[i]);
        }
    }
    free(runner->includes);
    free(runner->include_fds);
    free(runner->identities);
    tc_path_context_destroy(runner->paths);
    tc_project_manifest_destroy(runner->manifest);
    if (runner->root_fd >= 0) {
        (void)close(runner->root_fd);
    }
    if (runner->tests_fd >= 0) {
        (void)close(runner->tests_fd);
    }
}

static int print_runner_error(TcTestRunner *runner) {
    const char *symbol;
    const char *name;
    const char *message;
    int failed;
    if (runner->error == TC_IMPL_OK || runner->stderr_alias ||
        !runner->preflight_done) {
        return 1;
    }
    symbol = tc_impl_error_symbol(runner->error);
    name = tc_impl_error_print_name(runner->error);
    message = tc_impl_error_message(runner->error);
    if (runner->error == TC_IMPL_OOM) {
        failed = tc_cli_write(stderr, "tc: %s: %s: %s\n", symbol, name,
                              message);
    } else if (runner->detail[0] != '\0') {
        failed = tc_cli_write(stderr, "tc: %s: %s: %s\n", symbol, message,
                              runner->detail);
    } else {
        failed = tc_cli_write(stderr, "tc: %s: %s\n", symbol, message);
    }
    return failed == 0 && tc_cli_flush(stderr) == 0;
}

static const char *stage_name(TcStage stage) {
    switch (stage) {
    case TC_STAGE_LEX: return "LEX";
    case TC_STAGE_SYN: return "SYN";
    case TC_STAGE_IMPORT: return "IMPORT";
    case TC_STAGE_SEM: return "SEM";
    case TC_STAGE_CT: return "CT";
    case TC_STAGE_RT: return "RT";
    default: return "?";
    }
}

static int worker_analysis_diag(const TcAnalysisDiag *diag) {
    const TcDiagInfo *info = tc_diag_info(diag->code);
    int failed = tc_cli_write(
        stderr, "%s:%u:%u: error[%s]: %s: %s\n",
        diag->span.file != NULL ? diag->span.file : "<source>",
        diag->span.line, diag->span.column, stage_name(diag->stage),
        info != NULL ? info->symbol : "TC_CE_SYNTAX",
        info != NULL ? info->print_name : "SyntaxError");
    if (failed == 0 && diag->has_related) {
        failed = tc_cli_write(stderr, "%s:%u:%u: note: related position\n",
                              diag->related.file != NULL
                                  ? diag->related.file : "<source>",
                              diag->related.line, diag->related.column);
    }
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return 0;
    }
    return 1;
}

static int worker_impl_error(TcImplError impl, const char *detail) {
    const char *symbol = tc_impl_error_symbol(impl);
    const char *name = tc_impl_error_print_name(impl);
    const char *message = tc_impl_error_message(impl);
    int failed;
    if (impl == TC_IMPL_OOM) {
        failed = tc_cli_write(stderr, "tc: %s: %s: %s\n", symbol, name,
                              message);
    } else if (detail != NULL) {
        failed = tc_cli_write(stderr, "tc: %s: %s: %s\n", symbol, message,
                              detail);
    } else {
        failed = tc_cli_write(stderr, "tc: %s: %s\n", symbol, message);
    }
    return failed == 0 && tc_cli_flush(stderr) == 0;
}

typedef struct {
    int fd;
} TcWorkerSink;

typedef struct {
    int fd;
} TcWorkerSource;

static int worker_sink_write(void *ctx, const uint8_t *bytes, size_t size) {
    TcWorkerSink *sink = (TcWorkerSink *)ctx;
    return write_all_fd(sink->fd, bytes, size) ? 0 : -1;
}

static int worker_sink_flush(void *ctx) {
    (void)ctx;
    return 0;
}

static int worker_source_read(void *ctx, uint8_t *buf, size_t cap, size_t *n,
                              TcIoStatus *status) {
    TcWorkerSource *source = (TcWorkerSource *)ctx;
    ssize_t got;
    if (buf == NULL || n == NULL || status == NULL || cap == 0u) {
        return -1;
    }
    do {
        got = read(source->fd, buf, cap);
    } while (got < 0 && errno == EINTR);
    if (got < 0) {
        return -1;
    }
    *n = (size_t)got;
    *status = got == 0 ? TC_IO_EOF : TC_IO_OK;
    return 0;
}

static void worker_trap(void *ctx, TcErrorKind kind, int line) {
    (void)ctx;
    (void)kind;
    (void)line;
}

static int close_except(int *keep, size_t nkeep) {
    DIR *directory = opendir(
#ifdef __linux__
        "/proc/self/fd"
#else
        "/dev/fd"
#endif
    );
    struct dirent *entry;
    int directory_fd;
    int failed = 0;
    if (directory == NULL) {
        return 0;
    }
    directory_fd = dirfd(directory);
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        size_t i;
        int preserved = 0;
        if (end == entry->d_name || *end != '\0' || value < 3) {
            errno = 0;
            continue;
        }
        if (value == directory_fd) {
            errno = 0;
            continue;
        }
        for (i = 0u; i < nkeep; i++) {
            if (value == keep[i]) {
                preserved = 1;
                break;
            }
        }
        if (!preserved && close((int)value) != 0 && errno != EBADF) {
            failed = 1;
        }
        errno = 0;
    }
    if (errno != 0 || closedir(directory) != 0) {
        failed = 1;
    }
    return !failed;
}

static void worker_main(TcTestRunner *runner, const TcTestCase *test,
                        int main_fd, int parent_fd, int test_dir_fd, int stdin_fd,
                        int stdout_fd, int stderr_fd, int result_fd,
                        const char *const *include_paths,
                        const int *include_fds, size_t ninclude_roots) {
    TcModuleRootSource source;
    TcAnalysis *analysis;
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcWorkerResult result;
    TcIo io;
    TcIoSink sink;
    TcIoSrc src;
    TcTrapHandler trap;
    TcWorkerSink sink_ctx;
    TcWorkerSource source_ctx;
    TcVmResult vm_result;
    TcImplError vm_impl = TC_IMPL_OK;
    TcVmStatus vm_status;
    int keep[64];
    size_t nkeep = 0u;
    size_t i;
    memset(&result, 0, sizeof(result));
    result.magic = TC_TEST_MAGIC;
    result.version = TC_TEST_PROTOCOL;
    if (dup2(stdout_fd, STDOUT_FILENO) < 0 ||
        dup2(stderr_fd, STDERR_FILENO) < 0 ||
        dup2(stdin_fd, STDIN_FILENO) < 0 ||
        fchdir(runner->root_fd) != 0) {
        result.status = 2u;
        result.impl = TC_IMPL_IO;
        goto send;
    }
    keep[nkeep++] = runner->root_fd;
    keep[nkeep++] = parent_fd;
    keep[nkeep++] = test_dir_fd;
    keep[nkeep++] = main_fd;
    keep[nkeep++] = STDIN_FILENO;
    keep[nkeep++] = STDOUT_FILENO;
    keep[nkeep++] = STDERR_FILENO;
    keep[nkeep++] = result_fd;
    for (i = 0u; i < ninclude_roots; i++) {
        if (include_fds[i] >= 0) {
            keep[nkeep++] = include_fds[i];
        }
    }
    if (!close_except(keep, nkeep)) {
        result.status = 2u;
        result.impl = TC_IMPL_IO;
        (void)worker_impl_error(TC_IMPL_IO, "cannot isolate worker descriptors");
        goto send;
    }
    memset(&source, 0, sizeof(source));
    source.fd = main_fd;
    source.parent_fd = parent_fd;
    source.path = test->canonical;
    source.identity = test->identity;
    source.include_fds = include_fds;
    source.ninclude_fds = ninclude_roots;
    analysis = tc_analyze_source(&source, &runner->profile,
                                 runner->profile.addr_width, include_paths,
                                 ninclude_roots, runner->paths, &status, &diag,
                                 &impl);
    if (analysis == NULL) {
        result.status = 2u;
        result.impl = impl != TC_IMPL_OK ? impl : TC_IMPL_INTERNAL;
        (void)worker_impl_error(result.impl, test->canonical);
        goto send;
    }
    if (status == TC_ANALYSIS_IMPL) {
        const char *failure = tc_analysis_failure_path(analysis);
        result.status = 2u;
        result.impl = impl != TC_IMPL_OK ? impl : TC_IMPL_INTERNAL;
        (void)worker_impl_error(result.impl, failure);
        tc_analysis_destroy(analysis);
        goto send;
    }
    if (status == TC_ANALYSIS_ROOT_NOT_PROGRAM) {
        result.status = 3u;
        result.impl = TC_IMPL_INTERNAL;
        tc_analysis_destroy(analysis);
        goto send;
    }
    if (status == TC_ANALYSIS_DIAGNOSTIC) {
        result.status = 1u;
        (void)worker_analysis_diag(&diag);
        tc_analysis_destroy(analysis);
        goto send;
    }
    sink_ctx.fd = STDOUT_FILENO;
    source_ctx.fd = STDIN_FILENO;
    sink.write = worker_sink_write;
    sink.flush = worker_sink_flush;
    sink.ctx = &sink_ctx;
    src.read = worker_source_read;
    src.ctx = &source_ctx;
    trap.trap = worker_trap;
    trap.ctx = NULL;
    io.out = &sink;
    io.in = &src;
    runner->profile.io = &io;
    runner->profile.alloc = (TcAllocator *)tc_allocator_libc();
    runner->profile.trap = &trap;
    vm_status = tc_vm_run(tc_analysis_ir(analysis), &runner->profile, &vm_result,
                          &vm_impl);
    if (io.out != NULL && io.out->flush != NULL &&
        io.out->flush(io.out->ctx) != 0 && vm_status == TC_VM_STATUS_OK) {
        vm_status = TC_VM_STATUS_IMPL;
        vm_impl = TC_IMPL_IO;
    }
    if (vm_status == TC_VM_STATUS_OK) {
        result.status = 0u;
        result.exit_status = (uint32_t)runner->profile.exit.normal_status;
    } else if (vm_status == TC_VM_STATUS_TRAP) {
        const TcDiagInfo *info = tc_diag_info((TcDiagCode)vm_result.kind);
        result.status = 0u;
        result.trap = (uint32_t)vm_result.kind;
        result.exit_status = (uint32_t)runner->profile.exit.trap_status;
        (void)tc_cli_write(stderr, "%s:%u:%u: error[RT]: %s: %s\n",
                           vm_result.span.file != NULL
                               ? vm_result.span.file : "<source>",
                           vm_result.span.line, vm_result.span.column,
                           info != NULL ? info->symbol : "TC_RE_TRAP",
                           info != NULL ? info->print_name : "Trap");
        (void)tc_cli_flush(stderr);
    } else {
        result.status = 2u;
        result.impl = vm_impl != TC_IMPL_OK ? vm_impl : TC_IMPL_INTERNAL;
        (void)worker_impl_error(result.impl, NULL);
    }
    tc_analysis_destroy(analysis);

send:
    (void)write_all_fd(result_fd, &result, sizeof(result));
    _exit(0);
}

static int empty_snapshot(TcSnapshot *snapshot, TcTestRunner *runner) {
    char path[PATH_MAX];
    int fd;
    int readonly_fd = -1;
    if (!private_temp_path(path)) {
        return runner_fail(runner, TC_IMPL_IO,
                           "temporary directory path is too long");
    }
    fd = tc_fd_relocate(mkstemp(path));
    if (fd < 0) {
        return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                   : TC_IMPL_IO,
                           "cannot create empty snapshot");
    }
    if (!bind_private_snapshot(path, fd, &readonly_fd)) {
        int error_number = errno;
        (void)close(fd);
        return runner_fail(runner, error_number == ENOMEM ? TC_IMPL_OOM
                                                          : TC_IMPL_IO,
                           "cannot bind empty snapshot");
    }
    if (close(fd) != 0) {
        (void)close(readonly_fd);
        return runner_fail(runner, TC_IMPL_IO, "cannot close empty snapshot");
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->fd = readonly_fd;
    return 1;
}

static int open_sidecar(TcTestRunner *runner, int test_dir_fd,
                        unsigned index, TcSnapshot *snapshot,
                        struct stat *identity, int *present) {
    int fd = -1;
    int missing = 0;
    int direct_exists = 0;
    struct stat direct;
    TcResolveStatus status;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->fd = -1;
    *present = 0;
    if (fstatat(test_dir_fd, sidecar_names[index], &direct,
                AT_SYMLINK_NOFOLLOW) == 0) {
        direct_exists = 1;
    } else if (!missing_errno(errno)) {
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect sidecar");
    }
    status = resolve_beneath(test_dir_fd, test_dir_fd, sidecar_names[index],
                             &fd, &missing);
    if (status == TC_RESOLVE_OOM) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    if (status == TC_RESOLVE_USAGE) {
        return runner_fail(runner, TC_IMPL_USAGE,
                           "sidecar escapes its test directory");
    }
    if (status == TC_RESOLVE_IO || status == TC_RESOLVE_INVALID ||
        (status == TC_RESOLVE_MISSING && direct_exists)) {
        return runner_fail(runner, TC_IMPL_IO, "cannot resolve sidecar");
    }
    if (status == TC_RESOLVE_MISSING) {
        return 1;
    }
    if (fstat(fd, identity) != 0) {
        (void)close(fd);
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect sidecar");
    }
    if (!S_ISREG(identity->st_mode)) {
        (void)close(fd);
        return runner_fail(runner, TC_IMPL_IO,
                           "sidecar is not a regular file");
    }
    *present = 1;
    if (!add_identity(runner, identity)) {
        (void)close(fd);
        return 0;
    }
    if (!snapshot_from_fd(snapshot, fd,
                          index < 2u ? UINT64_MAX : TC_TEST_MAX_TEXT,
                          TC_IMPL_IO, runner)) {
        (void)close(fd);
        return 0;
    }
    if (close(fd) != 0) {
        return runner_fail(runner, TC_IMPL_IO, "cannot close sidecar");
    }
    return 1;
}

static int strip_one_eol(const unsigned char *bytes, size_t *size) {
    if (*size != 0u && bytes[*size - 1u] == '\n') {
        (*size)--;
        if (*size != 0u && bytes[*size - 1u] == '\r') {
            (*size)--;
        }
    }
    return 1;
}

static int valid_trap(const unsigned char *bytes, size_t size) {
    size_t i;
    if (size <= 6u || memcmp(bytes, "TC_RE_", 6u) != 0) {
        return 0;
    }
    for (i = 6u; i < size; i++) {
        unsigned char c = bytes[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int parse_exit(const unsigned char *bytes, size_t size,
                      unsigned *out_value) {
    unsigned value = 0u;
    size_t i;
    if (size == 0u) {
        return 0;
    }
    for (i = 0u; i < size; i++) {
        if (bytes[i] < '0' || bytes[i] > '9') {
            return 0;
        }
        if (value > 25u) {
            return 0;
        }
        value = value * 10u + (unsigned)(bytes[i] - '0');
        if (value > 255u) {
            return 0;
        }
    }
    *out_value = value;
    return 1;
}

static int load_sidecars(TcTestRunner *runner, TcTestCase *test,
                         int test_dir_fd, TcSnapshot snapshots[4],
                         int present[4], unsigned *expected_exit,
                         int *has_expected_trap, char *expected_trap,
                         size_t trap_cap) {
    unsigned i;
    unsigned explicit_exit = 0u;
    int has_explicit_exit = 0;
    memset(snapshots, 0, 4u * sizeof(*snapshots));
    for (i = 0u; i < 4u; i++) {
        snapshots[i].fd = -1;
        if (!open_sidecar(runner, test_dir_fd, i, &snapshots[i],
                          &test->sidecar_stat[i], &present[i])) {
            return 0;
        }
        if (!present[i]) {
            if (i < 2u && !empty_snapshot(&snapshots[i], runner)) {
                return 0;
            }
            continue;
        }
        if (i >= 2u) {
            unsigned char *bytes = NULL;
            size_t size = 0u;
            if (lseek(snapshots[i].fd, 0u, SEEK_SET) < 0 ||
                !read_small_file(snapshots[i].fd, &bytes, &size,
                                 TC_TEST_MAX_TEXT, runner, 1)) {
                return 0;
            }
            strip_one_eol(bytes, &size);
            if (i == 2u) {
                if (!valid_trap(bytes, size)) {
                    free(bytes);
                    return runner_fail(runner, TC_IMPL_USAGE,
                                       "invalid trap sidecar");
                }
                if (size + 1u > trap_cap) {
                    free(bytes);
                    return runner_fail(runner, TC_IMPL_INTERNAL,
                                       "trap sidecar buffer is too small");
                }
                memcpy(expected_trap, bytes, size);
                expected_trap[size] = '\0';
                *has_expected_trap = 1;
            } else if (!parse_exit(bytes, size, &explicit_exit)) {
                free(bytes);
                return runner_fail(runner, TC_IMPL_USAGE,
                                   "invalid exit sidecar");
            } else {
                has_explicit_exit = 1;
            }
            free(bytes);
        }
    }
    *expected_exit = has_explicit_exit
                         ? explicit_exit
                         : (*has_expected_trap
                                ? (unsigned)runner->profile.exit.trap_status
                                : 0u);
    return 1;
}

static int reopen_test(TcTestRunner *runner, const TcTestCase *test,
                       int *test_dir_fd, int *main_fd, int *parent_fd) {
    int missing = 0;
    struct stat dir_stat;
    struct stat main_stat;
    TcResolveStatus status;
    *test_dir_fd = -1;
    *main_fd = -1;
    *parent_fd = -1;
    status = resolve_beneath(runner->tests_fd, runner->tests_fd, test->name,
                             test_dir_fd, &missing);
    if (status == TC_RESOLVE_OOM) {
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    if (status == TC_RESOLVE_IO || status == TC_RESOLVE_INVALID) {
        return runner_fail(runner, TC_IMPL_IO, "cannot reopen test directory");
    }
    if (status != TC_RESOLVE_FOUND) {
        return runner_fail(runner, TC_IMPL_INTERNAL,
                           "test directory identity changed");
    }
    if (fstat(*test_dir_fd, &dir_stat) != 0) {
        (void)close(*test_dir_fd);
        *test_dir_fd = -1;
        return runner_fail(runner, TC_IMPL_IO,
                           "cannot inspect reopened test directory");
    }
    if (!same_identity(&dir_stat, &test->dir_stat)) {
        (void)close(*test_dir_fd);
        *test_dir_fd = -1;
        return runner_fail(runner, TC_IMPL_INTERNAL,
                           "test directory identity changed");
    }
    status = resolve_beneath(*test_dir_fd, *test_dir_fd, "main.tc", main_fd,
                             &missing);
    if (status == TC_RESOLVE_OOM) {
        (void)close(*test_dir_fd);
        *test_dir_fd = -1;
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    if (status == TC_RESOLVE_IO || status == TC_RESOLVE_INVALID) {
        (void)close(*test_dir_fd);
        *test_dir_fd = -1;
        return runner_fail(runner, TC_IMPL_IO, "cannot reopen main.tc");
    }
    if (status != TC_RESOLVE_FOUND) {
        (void)close(*test_dir_fd);
        *test_dir_fd = -1;
        return runner_fail(runner, TC_IMPL_INTERNAL,
                           "main.tc identity changed");
    }
    if (fstat(*main_fd, &main_stat) != 0) {
        (void)close(*main_fd);
        *main_fd = -1;
        (void)close(*test_dir_fd);
        *test_dir_fd = -1;
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect reopened main.tc");
    }
    if (!same_identity(&main_stat, &test->main_stat)) {
        (void)close(*main_fd);
        *main_fd = -1;
        (void)close(*test_dir_fd);
        *test_dir_fd = -1;
        return runner_fail(runner, TC_IMPL_INTERNAL,
                           "main.tc identity changed");
    }
    {
        uint64_t size = 0u;
        uint64_t hash;
        int hashed;
        int error_number = 0;
        if (lseek(*main_fd, 0u, SEEK_SET) < 0) {
            (void)close(*main_fd);
            *main_fd = -1;
            (void)close(*test_dir_fd);
            *test_dir_fd = -1;
            return runner_fail(runner, TC_IMPL_IO, "cannot recheck main.tc");
        }
        hashed = hash_fd(*main_fd, TC_TEST_MAX_MAIN, &hash, &size);
        if (!hashed) {
            error_number = errno;
        }
        if (!hashed) {
            (void)close(*main_fd);
            *main_fd = -1;
            (void)close(*test_dir_fd);
            *test_dir_fd = -1;
            return runner_fail(runner, error_number == EFBIG
                                         ? TC_IMPL_INTERNAL : TC_IMPL_IO,
                               "cannot fingerprint reopened main.tc");
        }
        if (size != test->main_size || hash != test->main_hash ||
            file_generation(&main_stat) != test->main_generation) {
            (void)close(*main_fd);
            *main_fd = -1;
            (void)close(*test_dir_fd);
            *test_dir_fd = -1;
            return runner_fail(runner, TC_IMPL_INTERNAL,
                               "main.tc content changed");
        }
    }
    {
        int status = tc_module_bound_parent(*main_fd, test->canonical, parent_fd);
        if (status <= 0) {
            (void)close(*main_fd);
            *main_fd = -1;
            (void)close(*test_dir_fd);
            *test_dir_fd = -1;
            return runner_fail(runner, status < 0
                                         ? (status == -1 ? TC_IMPL_OOM
                                                         : TC_IMPL_IO)
                                         : TC_IMPL_INTERNAL,
                               "cannot rebind main.tc parent directory");
        }
    }
    return 1;
}

static int make_capture(TcCapture *capture, int expected_fd,
                        TcTestRunner *runner) {
    char path[PATH_MAX];
    struct stat st;
    memset(capture, 0, sizeof(*capture));
    capture->expected_fd = expected_fd;
    capture->actual_fd = -1;
    if (fstat(expected_fd, &st) != 0 || st.st_size < 0) {
        return runner_fail(runner, TC_IMPL_IO, "cannot inspect stdout.bin");
    }
    capture->expected_size = (uint64_t)st.st_size;
    if (!private_temp_path(path)) {
        return runner_fail(runner, TC_IMPL_IO,
                           "temporary directory path is too long");
    }
    capture->actual_fd = tc_fd_relocate(mkstemp(path));
    if (capture->actual_fd < 0) {
        return runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM
                                                   : TC_IMPL_IO,
                           "cannot create stdout capture");
    }
    if (unlink(path) != 0 ||
        fcntl(capture->actual_fd, F_SETFD, FD_CLOEXEC) < 0) {
        (void)close(capture->actual_fd);
        capture->actual_fd = -1;
        return runner_fail(runner, TC_IMPL_IO, "cannot privatize stdout capture");
    }
    capture->stderr = (unsigned char *)malloc(TC_TEST_STDERR_KEEP);
    if (capture->stderr == NULL) {
        (void)close(capture->actual_fd);
        capture->actual_fd = -1;
        return runner_fail(runner, TC_IMPL_OOM, "out of memory");
    }
    return 1;
}

static void destroy_capture(TcCapture *capture) {
    free(capture->stderr);
    capture->stderr = NULL;
    if (capture->actual_fd >= 0) {
        (void)close(capture->actual_fd);
    }
    capture->actual_fd = -1;
}

static int capture_stdout(TcCapture *capture, const unsigned char *bytes,
                          size_t size) {
    size_t consumed = 0u;
    if (!write_all_fd(capture->actual_fd, bytes, size)) {
        return 0;
    }
    capture->actual_size += (uint64_t)size;
    while (consumed < size && !capture->has_difference) {
        unsigned char expected[TC_TEST_IO_CHUNK];
        size_t remaining = size - consumed;
        ssize_t got;
        size_t i;
        if (capture->compared >= capture->expected_size) {
            capture->first_difference = capture->compared;
            capture->has_difference = 1;
            capture->eof_difference = 1;
            break;
        }
        if (remaining > capture->expected_size - capture->compared) {
            remaining = (size_t)(capture->expected_size - capture->compared);
        }
        if (remaining > sizeof(expected)) {
            remaining = sizeof(expected);
        }
        do {
            got = pread(capture->expected_fd, expected, remaining,
                        (off_t)capture->compared);
        } while (got < 0 && errno == EINTR);
        if (got <= 0) {
            return 0;
        }
        for (i = 0u; i < (size_t)got; i++) {
            if (expected[i] != bytes[consumed + i]) {
                capture->first_difference = capture->compared + (uint64_t)i;
                capture->has_difference = 1;
                break;
            }
        }
        capture->compared += (uint64_t)got;
        consumed += (size_t)got;
    }
    return 1;
}

static void capture_stderr(TcCapture *capture, const unsigned char *bytes,
                           size_t size) {
    size_t keep = size;
    capture->stderr_total += (uint64_t)size;
    if (keep > TC_TEST_STDERR_KEEP - capture->stderr_size) {
        keep = TC_TEST_STDERR_KEEP - capture->stderr_size;
    }
    if (keep != 0u) {
        memcpy(capture->stderr + capture->stderr_size, bytes, keep);
        capture->stderr_size += keep;
    }
}

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
           (uint64_t)ts.tv_nsec;
}

static uint64_t deadline_delta(uint64_t seconds) {
    uint64_t maximum = UINT64_MAX / UINT64_C(1000000000);
    return seconds > maximum ? UINT64_MAX
                             : seconds * UINT64_C(1000000000);
}

static const char *trap_symbol(uint32_t code) {
    const TcDiagInfo *info;
    if (code == 0u) {
        return "none";
    }
    info = tc_diag_info((TcDiagCode)code);
    return info != NULL ? info->symbol : "none";
}

static int print_raw_section(FILE *stream, const char *label, int fd,
                             uint64_t start, uint64_t end,
                             uint64_t total) {
    unsigned char buffer[TC_TEST_IO_CHUNK];
    uint64_t at = start;
    if (tc_cli_write(stream, "%s", label) != 0) {
        return 0;
    }
    while (at < end) {
        size_t want = end - at > sizeof(buffer)
                          ? sizeof(buffer) : (size_t)(end - at);
        ssize_t got = pread(fd, buffer, want, (off_t)at);
        if (got <= 0) {
            return 0;
        }
        if (!stream_write(stream, buffer, (size_t)got)) {
            return 0;
        }
        at += (uint64_t)got;
    }
    if (tc_cli_write(stream, "\n") != 0) {
        return 0;
    }
    if (end < total &&
        tc_cli_write(stream, "... truncated: %llu bytes omitted\n",
                     (unsigned long long)(total - end)) != 0) {
        return 0;
    }
    return 1;
}

static void stdout_range(uint64_t length, uint64_t difference, int eof,
                         uint64_t *start, uint64_t *end) {
    if (eof) {
        *start = length > TC_TEST_STDOUT_WINDOW
                     ? length - TC_TEST_STDOUT_WINDOW : 0u;
        *end = length;
    } else {
        uint64_t before = difference > 16384u ? 16384u : difference;
        *start = difference - before;
        if (length > TC_TEST_STDOUT_WINDOW &&
            *start > length - TC_TEST_STDOUT_WINDOW) {
            *start = length - TC_TEST_STDOUT_WINDOW;
        }
        *end = *start + TC_TEST_STDOUT_WINDOW;
        if (*end > length) {
            *end = length;
        }
    }
}

static int print_failure_detail(TcTestRunner *runner, const TcTestCase *test,
                                const char *reason, int compile_error,
                                int timeout, int has_result,
                                unsigned expected_exit, int has_expected_trap,
                                const char *expected_trap,
                                const TcWorkerResult *result,
                                const TcCapture *capture) {
    char line[1024];
    int failed = tc_cli_write(stderr, "detail %s: %s\n", test->logical,
                              reason);
    if (failed == 0 && has_result && !compile_error) {
        const char *got_trap = trap_symbol(result->trap);
        failed = tc_cli_write(stderr,
                              "[result]\n"
                              "trap: expected %s, got %s\n"
                              "exit: expected %u, got %u\n",
                              has_expected_trap ? expected_trap : "none",
                              got_trap, expected_exit, result->exit_status);
    }
    if (failed == 0 && !compile_error && !timeout && capture != NULL &&
        capture->has_difference) {
        uint64_t want_start;
        uint64_t want_end;
        uint64_t got_start;
        uint64_t got_end;
        char label[128];
        stdout_range(capture->expected_size, capture->first_difference,
                     capture->eof_difference, &want_start, &want_end);
        stdout_range(capture->actual_size, capture->first_difference,
                     capture->eof_difference, &got_start, &got_end);
        (void)snprintf(line, sizeof(line),
                       "[stdout]\n"
                       "first difference at offset %llu, expected %llu bytes, "
                       "got %llu bytes\n",
                       (unsigned long long)capture->first_difference,
                       (unsigned long long)capture->expected_size,
                       (unsigned long long)capture->actual_size);
        failed = tc_cli_write(stderr, "%s", line);
        (void)snprintf(label, sizeof(label),
                       "--- expected stdout [%llu,%llu) ---\n",
                       (unsigned long long)want_start,
                       (unsigned long long)want_end);
        if (failed == 0) {
            failed = print_raw_section(stderr, label, capture->expected_fd,
                                       want_start, want_end,
                                       capture->expected_size) ? 0 : 1;
        }
        (void)snprintf(label, sizeof(label),
                       "--- actual stdout [%llu,%llu) ---\n",
                       (unsigned long long)got_start,
                       (unsigned long long)got_end);
        if (failed == 0) {
            failed = print_raw_section(stderr, label, capture->actual_fd,
                                       got_start, got_end,
                                       capture->actual_size) ? 0 : 1;
        }
    }
    if (failed == 0 && capture != NULL &&
        (capture->stderr_size != 0u || compile_error)) {
        char label[128];
        (void)snprintf(label, sizeof(label),
                       "[stderr]\n--- stderr [0,%zu) ---\n",
                       capture->stderr_size);
        failed = tc_cli_write(stderr, "%s", label);
        if (failed == 0 && capture->stderr_size != 0u &&
            !stream_write(stderr, capture->stderr, capture->stderr_size)) {
            failed = 1;
        }
        if (failed == 0 && tc_cli_write(stderr, "\n") != 0) {
            failed = 1;
        }
        if (failed == 0 && capture->stderr_total > capture->stderr_size &&
            tc_cli_write(stderr, "... truncated: %llu bytes omitted\n",
                         (unsigned long long)(capture->stderr_total -
                                              capture->stderr_size)) != 0) {
            failed = 1;
        }
    }
    (void)runner;
    return failed == 0 && tc_cli_flush(stderr) == 0;
}

static int valid_worker_result(const TcWorkerResult *result,
                               const TcPlatformProfile *profile) {
    const TcDiagInfo *info;
    if (result->magic != TC_TEST_MAGIC ||
        result->version != TC_TEST_PROTOCOL || result->status > 3u) {
        return 0;
    }
    if (result->status == 0u) {
        if (result->impl != TC_IMPL_OK) {
            return 0;
        }
        if (result->trap == 0u) {
            return result->exit_status == 0u;
        }
        info = tc_diag_info((TcDiagCode)result->trap);
        return info != NULL && (info->report_stage & TC_STAGE_MASK_RT) != 0u &&
               result->exit_status == (uint32_t)profile->exit.trap_status;
    }
    if (result->trap != 0u || result->exit_status != 0u) {
        return 0;
    }
    if (result->status == 1u) {
        return result->impl == TC_IMPL_OK;
    }
    if (result->status == 2u) {
        return result->impl == TC_IMPL_OOM || result->impl == TC_IMPL_IO ||
               result->impl == TC_IMPL_UNAVAILABLE;
    }
    return result->impl == TC_IMPL_INTERNAL;
}

static int read_worker_result(int fd, TcWorkerResult *result) {
    unsigned char extra;
    ssize_t got;
    if (!read_all_fd(fd, result, sizeof(*result))) {
        return 0;
    }
    do {
        got = read(fd, &extra, 1u);
    } while (got < 0 && errno == EINTR);
    return got == 0;
}

static int execute_test(TcTestRunner *runner, TcTestCase *test,
                        size_t *passed, size_t *failed_count) {
    TcSnapshot snapshots[4];
    int present[4] = {0, 0, 0, 0};
    TcSnapshot main_snapshot;
    TcCapture capture;
    unsigned expected_exit = 0u;
    int has_expected_trap = 0;
    char expected_trap[TC_TEST_MAX_TEXT + 1u];
    char **include_paths = NULL;
    int *include_fds = NULL;
    size_t ninclude_roots = 0u;
    int test_dir_fd = -1;
    int main_fd = -1;
    int parent_fd = -1;
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int result_pipe[2] = {-1, -1};
    pid_t pid = -1;
    int timed_out = 0;
    int worker_internal = 0;
    int worker_io = 0;
    int protocol_ok = 0;
    int worker_status = 0;
    int worker_reaped = 0;
    struct sigaction child_action;
    struct sigaction previous_child_action;
    int child_action_set = 0;
    TcWorkerResult result;
    uint64_t started = 0u;
    uint64_t deadline = UINT64_MAX;
    const char *reason = NULL;
    char *timeout_reason = NULL;
    int compile_error = 0;
    int test_failed = 0;
    int i;
    memset(snapshots, 0, sizeof(snapshots));
    for (i = 0; i < 4; i++) {
        snapshots[i].fd = -1;
    }
    memset(&main_snapshot, 0, sizeof(main_snapshot));
    main_snapshot.fd = -1;
    memset(&capture, 0, sizeof(capture));
    capture.expected_fd = -1;
    capture.actual_fd = -1;
    memset(&result, 0, sizeof(result));
    expected_trap[0] = '\0';
    if (!reopen_test(runner, test, &test_dir_fd, &main_fd, &parent_fd) ||
        !load_sidecars(runner, test, test_dir_fd, snapshots, present,
                       &expected_exit, &has_expected_trap, expected_trap,
                       sizeof(expected_trap))) {
        goto cleanup;
    }
    if (!snapshot_from_fd(&main_snapshot, main_fd, TC_TEST_MAX_MAIN,
                          TC_IMPL_INTERNAL, runner)) {
        goto cleanup;
    }
    if (main_snapshot.size != test->main_size ||
        main_snapshot.hash != test->main_hash ||
        main_snapshot.generation != test->main_generation) {
        runner_fail(runner, TC_IMPL_INTERNAL, "main.tc snapshot changed");
        goto cleanup;
    }
    if (!make_capture(&capture, snapshots[0].fd, runner) ||
        pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0 ||
        pipe(result_pipe) != 0) {
        if (runner->error == TC_IMPL_OK) {
            runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                        "cannot create worker pipes");
        }
        goto cleanup;
    }
    ninclude_roots = runner->nincludes + 1u;
    include_paths = (char **)calloc(ninclude_roots, sizeof(*include_paths));
    include_fds = (int *)calloc(ninclude_roots, sizeof(*include_fds));
    if (include_paths == NULL || include_fds == NULL) {
        runner_fail(runner, TC_IMPL_OOM, "out of memory");
        goto cleanup;
    }
    {
        struct stat test_dir_stat;
        include_paths[0] = fd_path(test_dir_fd, &test_dir_stat);
        if (include_paths[0] == NULL) {
            runner_fail(runner, TC_IMPL_IO,
                        "cannot determine test include root");
            goto cleanup;
        }
        include_fds[0] = test_dir_fd;
        for (i = 0; (size_t)i < runner->nincludes; i++) {
            include_paths[(size_t)i + 1u] = runner->includes[i];
            include_fds[(size_t)i + 1u] = runner->include_fds[i];
        }
    }
    if (runner->timeout_seconds != 0u) {
        started = monotonic_ns();
        if (started == 0u) {
            runner_fail(runner, TC_IMPL_IO, "cannot read monotonic clock");
            goto cleanup;
        }
        deadline = started > UINT64_MAX - deadline_delta(runner->timeout_seconds)
                       ? UINT64_MAX
                       : started + deadline_delta(runner->timeout_seconds);
    }
    memset(&child_action, 0, sizeof(child_action));
    child_action.sa_handler = SIG_DFL;
    (void)sigemptyset(&child_action.sa_mask);
    if (sigaction(SIGCHLD, &child_action, &previous_child_action) != 0) {
        runner_fail(runner, TC_IMPL_IO, "cannot configure worker reaping");
        goto cleanup;
    }
    child_action_set = 1;
    /* An ignored SIGCHLD disposition must stay displaced until waitpid. */
    pid = fork();
    if (pid < 0) {
        runner_fail(runner, errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO,
                    "cannot fork test worker");
        goto cleanup;
    }
    if (pid == 0) {
        child_action_set = 0;
        (void)close(stdout_pipe[0]);
        (void)close(stderr_pipe[0]);
        (void)close(result_pipe[0]);
        worker_main(runner, test, main_snapshot.fd, parent_fd, test_dir_fd,
                    snapshots[1].fd, stdout_pipe[1], stderr_pipe[1],
                    result_pipe[1], (const char *const *)include_paths,
                    include_fds, ninclude_roots);
    }
    (void)close(stdout_pipe[1]);
    stdout_pipe[1] = -1;
    (void)close(stderr_pipe[1]);
    stderr_pipe[1] = -1;
    (void)close(result_pipe[1]);
    result_pipe[1] = -1;
    {
        struct pollfd fds[2];
        int stdout_open = 1;
        int stderr_open = 1;
        int reaped = 0;
        fds[0].fd = stdout_pipe[0];
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = stderr_pipe[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        while (stdout_open || stderr_open || !reaped) {
            int wait_status = 0;
            int poll_timeout = -1;
            int polled;
            pid_t waited;
            if (!reaped) {
                waited = waitpid(pid, &wait_status, WNOHANG);
                if (waited == pid) {
                    reaped = 1;
                    worker_reaped = 1;
                    worker_status = wait_status;
                } else if (waited < 0 && errno != EINTR) {
                    runner_fail(runner, TC_IMPL_INTERNAL,
                                "cannot reap test worker");
                    goto cleanup;
                }
            }
            if (!reaped && runner->timeout_seconds != 0u) {
                uint64_t now = monotonic_ns();
                if (now == 0u) {
                    runner_fail(runner, TC_IMPL_IO,
                                "cannot read monotonic clock");
                    goto cleanup;
                }
                if (deadline != UINT64_MAX &&
                    (now >= deadline || now - started >=
                        deadline_delta(runner->timeout_seconds))) {
                    if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
                        runner_fail(runner, TC_IMPL_INTERNAL,
                                    "cannot kill timed out worker");
                        goto cleanup;
                    }
                    timed_out = 1;
                }
            }
            if (stdout_open || stderr_open) {
                fds[0].fd = stdout_open ? stdout_pipe[0] : -1;
                fds[1].fd = stderr_open ? stderr_pipe[0] : -1;
                if (!reaped && runner->timeout_seconds != 0u &&
                    !timed_out && deadline != UINT64_MAX) {
                    uint64_t now = monotonic_ns();
                    uint64_t remaining;
                    uint64_t delta;
                    if (now == 0u) {
                        runner_fail(runner, TC_IMPL_IO,
                                    "cannot read monotonic clock");
                        goto cleanup;
                    }
                    delta = deadline_delta(runner->timeout_seconds);
                    if (now < started || delta == UINT64_MAX ||
                        now - started >= delta) {
                        remaining = 0u;
                    } else {
                        remaining = delta - (now - started);
                    }
                    poll_timeout = remaining > 100u
                                       ? 100 : (int)remaining;
                } else if (timed_out) {
                    poll_timeout = 100;
                }
                if (!stdout_open && !stderr_open && !reaped && poll_timeout < 0) {
                    poll_timeout = 10;
                }
                polled = poll(fds, 2, poll_timeout);
                if (polled < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    runner_fail(runner, TC_IMPL_IO, "cannot poll worker pipes");
                    goto cleanup;
                }
                if (fds[0].revents != 0) {
                    unsigned char buffer[TC_TEST_IO_CHUNK];
                    ssize_t got = read(stdout_pipe[0], buffer,
                                       sizeof(buffer));
                    if (got > 0) {
                        if (!timed_out &&
                            !capture_stdout(&capture, buffer, (size_t)got)) {
                            runner_fail(runner, TC_IMPL_IO,
                                        "cannot capture worker stdout");
                            goto cleanup;
                        }
                    } else if (got == 0) {
                        stdout_open = 0;
                    } else if (errno != EINTR && errno != EAGAIN) {
                        runner_fail(runner, TC_IMPL_IO,
                                    "cannot read worker stdout");
                        goto cleanup;
                    }
                }
                if (fds[1].revents != 0) {
                    unsigned char buffer[TC_TEST_IO_CHUNK];
                    ssize_t got = read(stderr_pipe[0], buffer,
                                       sizeof(buffer));
                    if (got > 0) {
                        capture_stderr(&capture, buffer, (size_t)got);
                    } else if (got == 0) {
                        stderr_open = 0;
                    } else if (errno != EINTR && errno != EAGAIN) {
                        runner_fail(runner, TC_IMPL_IO,
                                    "cannot read worker stderr");
                        goto cleanup;
                    }
                }
            }
        }
        if (!reaped) {
            runner_fail(runner, TC_IMPL_INTERNAL, "cannot reap test worker");
            goto cleanup;
        }
        pid = -1;
    }
    if (timed_out) {
        reason = "timeout";
    } else {
        if (!read_worker_result(result_pipe[0], &result) ||
            !valid_worker_result(&result, &runner->profile)) {
            worker_internal = 1;
        } else {
            protocol_ok = 1;
            if (!WIFEXITED(worker_status) || WEXITSTATUS(worker_status) != 0) {
                worker_internal = 1;
            } else if (result.status == 3u) {
                worker_internal = 1;
            } else if (result.status == 2u) {
                if (result.impl == TC_IMPL_OOM || result.impl == TC_IMPL_IO ||
                    result.impl == TC_IMPL_UNAVAILABLE) {
                    worker_io = 1;
                    runner_fail(runner, (TcImplError)result.impl,
                                "test worker environment failure");
                } else {
                    worker_internal = 1;
                }
            }
        }
        if (capture.expected_size != capture.actual_size &&
            !capture.has_difference) {
            capture.first_difference = capture.expected_size < capture.actual_size
                                           ? capture.expected_size
                                           : capture.actual_size;
            capture.has_difference = 1;
            capture.eof_difference = 1;
        }
    }
    if (worker_internal) {
        runner_fail(runner, TC_IMPL_INTERNAL, "test worker failed internally");
        goto cleanup;
    }
    if (worker_io) {
        goto cleanup;
    }
    if (timed_out) {
        test_failed = 1;
        if (runner->timeout_text != NULL) {
            size_t length = strlen(runner->timeout_text);
            if (length > SIZE_MAX - sizeof("timeout after s")) {
                runner_fail(runner, TC_IMPL_INTERNAL,
                            "timeout reason exceeds supported size");
                goto cleanup;
            }
            timeout_reason = (char *)malloc(length + sizeof("timeout after s"));
            if (timeout_reason == NULL) {
                runner_fail(runner, TC_IMPL_OOM, "out of memory");
                goto cleanup;
            }
            (void)snprintf(timeout_reason, length + sizeof("timeout after s"),
                           "timeout after %ss", runner->timeout_text);
            reason = timeout_reason;
        }
    } else if (protocol_ok && result.status == 1u) {
        compile_error = 1;
        test_failed = 1;
        reason = "compile error";
    } else if (protocol_ok &&
               ((has_expected_trap ? strcmp(expected_trap,
                                             trap_symbol(result.trap))
                                   : result.trap != 0u) != 0)) {
        (void)snprintf(runner->detail, sizeof(runner->detail),
                       "trap expected %s, got %s",
                       has_expected_trap ? expected_trap : "none",
                       trap_symbol(result.trap));
        reason = runner->detail;
        test_failed = 1;
    } else if (protocol_ok && result.exit_status != expected_exit) {
        (void)snprintf(runner->detail, sizeof(runner->detail),
                       "exit expected %u, got %u", expected_exit,
                       result.exit_status);
        reason = runner->detail;
        test_failed = 1;
    } else if (capture.has_difference) {
        reason = "stdout mismatch";
        test_failed = 1;
    }
    if (test_failed) {
        if (tc_cli_write(stdout, "FAIL %s: %s\n", test->logical, reason) != 0 ||
            tc_cli_flush(stdout) != 0) {
            runner_fail(runner, TC_IMPL_IO, "cannot write test report");
            goto cleanup;
        }
        if (!print_failure_detail(runner, test, reason, compile_error,
                                  timed_out, protocol_ok && !timed_out,
                                  expected_exit, has_expected_trap,
                                  expected_trap, &result, &capture)) {
            runner_fail(runner, TC_IMPL_IO, "cannot write failure detail");
            goto cleanup;
        }
        (*failed_count)++;
    } else {
        if (tc_cli_write(stdout, "ok   %s\n", test->logical) != 0 ||
            tc_cli_flush(stdout) != 0) {
            runner_fail(runner, TC_IMPL_IO, "cannot write test report");
            goto cleanup;
        }
        (*passed)++;
    }

cleanup:
    if (pid > 0 && !worker_reaped) {
        (void)kill(pid, SIGKILL);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    if (child_action_set &&
        sigaction(SIGCHLD, &previous_child_action, NULL) != 0 &&
        runner->error == TC_IMPL_OK) {
        runner_fail(runner, TC_IMPL_IO, "cannot restore worker reaping");
    }
    for (i = 0; i < 2; i++) {
        if (stdout_pipe[i] >= 0) (void)close(stdout_pipe[i]);
        if (stderr_pipe[i] >= 0) (void)close(stderr_pipe[i]);
        if (result_pipe[i] >= 0) (void)close(result_pipe[i]);
    }
    if (main_fd >= 0) (void)close(main_fd);
    if (parent_fd >= 0) (void)close(parent_fd);
    if (test_dir_fd >= 0) (void)close(test_dir_fd);
    close_snapshot(&main_snapshot);
    for (i = 0; i < 4; i++) {
        close_snapshot(&snapshots[i]);
    }
    destroy_capture(&capture);
    free(include_paths);
    free(include_fds);
    free(timeout_reason);
    return runner->error == TC_IMPL_OK;
}

static int ensure_standard_fds(TcTestRunner *runner) {
    int fd;
    for (fd = 0; fd < 3; fd++) {
        struct stat original;
        if (fstat(fd, &original) == 0) {
            runner->stdio_identity[fd].stat = original;
            runner->stdio_identity[fd].valid = 1;
            continue;
        }
        if (errno != EBADF) {
            return runner_fail(runner, TC_IMPL_IO,
                               "cannot inspect standard descriptor");
        }
        runner_fail(runner, TC_IMPL_IO,
                    "standard input/output/error was closed");
        return 0;
    }
    return 1;
}

int tc_test_runner_run(const char *const *test_names, size_t ntest_names,
                       uint64_t timeout_seconds, const char *timeout_text) {
    TcTestRunner runner;
    size_t passed = 0u;
    size_t failed = 0u;
    size_t i;
    int ok;
    (void)signal(SIGPIPE, SIG_IGN);
    memset(&runner, 0, sizeof(runner));
    runner.root_fd = -1;
    runner.tests_fd = -1;
    ok = ensure_standard_fds(&runner);
    if (ok) {
        ok = prepare_runner(&runner, test_names, ntest_names, timeout_seconds,
                            timeout_text);
        if (!check_output_aliases(&runner)) {
            ok = 0;
        }
    }
    if (ok && runner.error == TC_IMPL_OK) {
        if (tc_cli_write(stdout, "project \"%s\" (%s)\n",
                         runner.manifest->name, runner.manifest->profile) !=
                0 ||
            tc_cli_flush(stdout) != 0) {
            runner_fail(&runner, TC_IMPL_IO, "cannot write test header");
        } else if (runner.ntests == 0u) {
            if (tc_cli_write(stdout, "0 tests\n") != 0 ||
                tc_cli_flush(stdout) != 0) {
                runner_fail(&runner, TC_IMPL_IO, "cannot write test summary");
            }
        } else {
            for (i = 0u; i < runner.ntests; i++) {
                if (!execute_test(&runner, &runner.tests[i], &passed, &failed)) {
                    break;
                }
            }
            if (runner.error == TC_IMPL_OK &&
                (tc_cli_write(stdout, "%zu passed, %zu failed\n", passed,
                              failed) != 0 ||
                 tc_cli_flush(stdout) != 0)) {
                runner_fail(&runner, TC_IMPL_IO, "cannot write test summary");
            }
        }
    }
    if (runner.error == TC_IMPL_OK && !check_output_aliases(&runner)) {
        ok = 0;
    }
    ok = ok && runner.error == TC_IMPL_OK;
    {
        int result = runner.error == TC_IMPL_OK
                         ? (failed == 0u ? TC_EXIT_OK : TC_EXIT_DIAGNOSTIC)
                         : tc_impl_error_exit(runner.error);
        if (!ok && !print_runner_error(&runner)) {
            result = tc_impl_error_exit(TC_IMPL_IO);
        }
        destroy_runner(&runner);
        if (fclose(stdout) != 0) {
            result = tc_impl_error_exit(TC_IMPL_IO);
        }
        if (fclose(stderr) != 0) {
            result = tc_impl_error_exit(TC_IMPL_IO);
        }
        return result;
    }
}
