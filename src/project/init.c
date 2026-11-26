#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "base/fd.h"
#include "project/init_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "project/name.h"

#define INIT_DIR_COUNT 3
#define INIT_TARGET_COUNT 6
#define INIT_JOURNAL_MAX 9

static const char *const init_dir_names[INIT_DIR_COUNT] = {
    "src", "tests", "tests/smoke"
};

static const char *const init_target_paths[INIT_TARGET_COUNT] = {
    "tc.project",
    "src/main.tc",
    "tests/smoke/main.tc",
    "tests/smoke/stdout.bin",
    ".gitignore",
    ".gitattributes"
};

static const char init_program[] = "#program\n_ = writeln int32, 1\n";
static const unsigned char init_stdout[] = {'1', '\n'};
static const char init_gitignore[] = "build/\n";
static const char init_gitattributes[] = "*.bin -text\n";

typedef enum {
    DIR_MISSING,
    DIR_PRESENT,
    DIR_BLOCKED
} InitDirState;

typedef enum {
    FILE_MISSING,
    FILE_PRESENT,
    FILE_BLOCKED
} InitFileState;

typedef struct {
    const char *path;
    int dir_index;
    InitFileState state;
    int has_identity;
    int identity_unknown;
    struct stat identity;
    int created;
} InitTarget;

typedef struct {
    char path[64];
    int is_dir;
    int identity_known;
    dev_t dev;
    ino_t ino;
    int cleanup_failed;
} InitJournalEntry;

typedef struct {
    const char *name;
    FILE *stdout;
    FILE *stderr;
    const TcProjectInitOps *ops;
    TcImplError error;
    char detail[256];
    int root_fd;
    int dir_fd[INIT_DIR_COUNT];
    InitDirState dir_state[INIT_DIR_COUNT];
    InitTarget targets[INIT_TARGET_COUNT];
    InitJournalEntry journal[INIT_JOURNAL_MAX];
    size_t journal_count;
    int stderr_safe;
} InitContext;

static int default_mkdir(int parent_fd, const char *name, mode_t mode) {
    return mkdirat(parent_fd, name, mode);
}

static int default_create(int parent_fd, const char *name, mode_t mode) {
    return tc_fd_openat(parent_fd, name,
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
}

static int default_open_dir(int parent_fd, const char *name) {
    return tc_fd_openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static ssize_t default_write(int fd, const void *data, size_t length) {
    return write(fd, data, length);
}

static int default_close(int fd) {
    return close(fd);
}

static int default_remove_file(int parent_fd, const char *name) {
    return unlinkat(parent_fd, name, 0);
}

static int default_remove_dir(int parent_fd, const char *name) {
    return unlinkat(parent_fd, name, AT_REMOVEDIR);
}

static const TcProjectInitOps default_ops = {
    default_mkdir, default_create, default_open_dir, default_write,
    default_close, default_remove_file, default_remove_dir
};

static void set_error(InitContext *ctx, TcImplError error, const char *detail) {
    if (ctx->error != TC_IMPL_OK) {
        return;
    }
    ctx->error = error;
    (void)snprintf(ctx->detail, sizeof(ctx->detail), "%s", detail);
}

static void set_io_error(InitContext *ctx, const char *action,
                         const char *path, int error_number) {
    char detail[256];
    if (error_number == ENOMEM) {
        set_error(ctx, TC_IMPL_OOM, TC_ERR_OUT_OF_MEMORY_MESSAGE);
        return;
    }
    if (path != NULL) {
        (void)snprintf(detail, sizeof(detail),
                       "%s %s: %s", action, path, strerror(error_number));
    } else {
        (void)snprintf(detail, sizeof(detail), "%s: %s", action,
                       strerror(error_number));
    }
    set_error(ctx, TC_IMPL_IO, detail);
}

static int same_identity(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int missing_entry(int error_number) {
    return error_number == ENOENT || error_number == ENOTDIR;
}

static int relocate_internal_fd(InitContext *ctx, int fd, const char *path) {
    int moved;
    if (fd < 0 || fd > 2) {
        return fd;
    }
    moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (moved < 0) {
        int error_number = errno;
        (void)close(fd);
        set_io_error(ctx, "cannot relocate internal descriptor", path,
                     error_number);
        return -1;
    }
    if (close(fd) != 0) {
        int error_number = errno;
        (void)close(moved);
        set_io_error(ctx, "cannot close relocated descriptor", path,
                     error_number);
        return -1;
    }
    return moved;
}

static void probe_dir(InitContext *ctx, int index, int parent_fd) {
    const char *name = index == 2 ? "smoke" : init_dir_names[index];
    struct stat st;
    if (fstatat(parent_fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (missing_entry(errno)) {
            ctx->dir_state[index] = DIR_MISSING;
        } else {
            ctx->dir_state[index] = DIR_BLOCKED;
            set_io_error(ctx, "cannot inspect", init_dir_names[index], errno);
        }
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        ctx->dir_state[index] = DIR_BLOCKED;
        return;
    }
    ctx->dir_fd[index] = relocate_internal_fd(
        ctx, ctx->ops->open_dir(parent_fd, name), init_dir_names[index]);
    if (ctx->dir_fd[index] < 0) {
        ctx->dir_state[index] = DIR_BLOCKED;
        if (ctx->error == TC_IMPL_OK) {
            set_io_error(ctx, "cannot open directory", init_dir_names[index],
                         errno);
        }
        return;
    }
    ctx->dir_state[index] = DIR_PRESENT;
}

static void probe_target(InitContext *ctx, int index, int parent_fd,
                         const char *leaf) {
    InitTarget *target = &ctx->targets[index];
    struct stat st;
    target->path = init_target_paths[index];
    target->dir_index = index == 1 ? 0 : (index == 2 || index == 3 ? 2 : -1);
    if (target->dir_index >= 0) {
        InitDirState state = ctx->dir_state[target->dir_index];
        if (state == DIR_BLOCKED) {
            target->state = FILE_BLOCKED;
            return;
        }
        if (state == DIR_MISSING) {
            target->state = FILE_MISSING;
            return;
        }
        parent_fd = ctx->dir_fd[target->dir_index];
    }
    if (fstatat(parent_fd, leaf, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (missing_entry(errno)) {
            target->state = FILE_MISSING;
        } else {
            target->state = FILE_BLOCKED;
            target->identity_unknown = 1;
            set_io_error(ctx, "cannot inspect", target->path, errno);
        }
        return;
    }
    target->state = FILE_PRESENT;
    if (index == 0) {
        set_error(ctx, TC_IMPL_USAGE, "tc.project already exists");
    }
    if (S_ISLNK(st.st_mode)) {
        if (fstatat(parent_fd, leaf, &st, 0) != 0) {
            if (missing_entry(errno) || errno == ELOOP) {
                return;
            }
            target->identity_unknown = 1;
            set_io_error(ctx, "cannot resolve", target->path, errno);
            return;
        }
    }
    target->has_identity = 1;
    target->identity = st;
}

static int discover_targets(InitContext *ctx) {
    probe_target(ctx, 0, ctx->root_fd, "tc.project");
    probe_dir(ctx, 1, ctx->root_fd);
    if (ctx->dir_state[1] == DIR_BLOCKED && ctx->error == TC_IMPL_OK) {
        set_error(ctx, TC_IMPL_USAGE, "tests must be a real directory");
    }
    if (ctx->dir_state[1] == DIR_PRESENT) {
        probe_dir(ctx, 2, ctx->dir_fd[1]);
    } else if (ctx->dir_state[1] == DIR_MISSING) {
        ctx->dir_state[2] = DIR_MISSING;
    } else {
        ctx->dir_state[2] = DIR_BLOCKED;
    }
    probe_dir(ctx, 0, ctx->root_fd);

    probe_target(ctx, 1, ctx->root_fd, "main.tc");
    probe_target(ctx, 2, ctx->root_fd, "main.tc");
    probe_target(ctx, 3, ctx->root_fd, "stdout.bin");
    probe_target(ctx, 4, ctx->root_fd, ".gitignore");
    probe_target(ctx, 5, ctx->root_fd, ".gitattributes");
    return 0;
}

static void probe_skipped_referents(InitContext *ctx) {
    int i;
    for (i = 0; i < INIT_TARGET_COUNT; i++) {
        struct stat st;
        if (ctx->targets[i].has_identity) {
            continue;
        }
        if (fstatat(ctx->root_fd, init_target_paths[i], &st, 0) == 0) {
            ctx->targets[i].has_identity = 1;
            ctx->targets[i].identity = st;
        } else if (!missing_entry(errno) && errno != ELOOP) {
            ctx->targets[i].identity_unknown = 1;
            set_io_error(ctx, "cannot resolve", init_target_paths[i], errno);
        }
    }
}

static int check_identities(InitContext *ctx) {
    struct stat fd_identity[2];
    int fd_known[2] = {0, 0};
    int descriptors[2];
    int i;
    int j;
    int alias = 0;

    descriptors[0] = fileno(ctx->stdout);
    descriptors[1] = fileno(ctx->stderr);
    ctx->stderr_safe = 1;
    for (i = 0; i < 2; i++) {
        if (fstat(descriptors[i], &fd_identity[i]) == 0) {
            fd_known[i] = 1;
        } else if (errno != EBADF) {
            if (i == 1) {
                ctx->stderr_safe = 0;
            }
            set_io_error(ctx, "cannot inspect standard stream", NULL, errno);
        }
    }
    if (fd_known[0] && fd_known[1] &&
        same_identity(&fd_identity[0], &fd_identity[1])) {
        alias = 1;
        ctx->stderr_safe = 0;
    }
    for (i = 0; i < INIT_TARGET_COUNT; i++) {
        if (ctx->targets[i].identity_unknown) {
            ctx->stderr_safe = 0;
        }
    }
    for (i = 0; i < 2; i++) {
        if (!fd_known[i]) {
            continue;
        }
        for (j = 0; j < INIT_TARGET_COUNT; j++) {
            if (ctx->targets[j].has_identity &&
                same_identity(&fd_identity[i], &ctx->targets[j].identity)) {
                alias = 1;
                if (i == 1) {
                    ctx->stderr_safe = 0;
                }
            }
        }
    }
    for (i = 0; i < INIT_TARGET_COUNT; i++) {
        if (!ctx->targets[i].has_identity) {
            continue;
        }
        for (j = i + 1; j < INIT_TARGET_COUNT; j++) {
            if (ctx->targets[j].has_identity &&
                same_identity(&ctx->targets[i].identity,
                              &ctx->targets[j].identity)) {
                alias = 1;
            }
        }
    }
    if (alias && ctx->error == TC_IMPL_OK) {
        set_error(ctx, TC_IMPL_USAGE,
                  "initialization targets alias standard streams or each other");
    }
    return alias;
}

static void journal_add(InitContext *ctx, const char *path, int is_dir,
                        const struct stat *identity, int identity_known) {
    InitJournalEntry *entry;
    if (ctx->journal_count >= INIT_JOURNAL_MAX) {
        set_error(ctx, TC_IMPL_INTERNAL, "initialization journal overflow");
        return;
    }
    entry = &ctx->journal[ctx->journal_count++];
    (void)snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->is_dir = is_dir;
    entry->identity_known = identity_known;
    if (identity_known) {
        entry->dev = identity->st_dev;
        entry->ino = identity->st_ino;
    }
    entry->cleanup_failed = 0;
}

static int ensure_dir(InitContext *ctx, int index) {
    const char *path = init_dir_names[index];
    const char *leaf = index == 2 ? "smoke" : path;
    int parent_fd = index == 2 ? ctx->dir_fd[1] : ctx->root_fd;
    struct stat st;
    if (ctx->dir_state[index] == DIR_BLOCKED) {
        return 0;
    }
    if (ctx->dir_state[index] == DIR_MISSING) {
        if (ctx->ops->mkdir_entry(parent_fd, leaf, 0777) != 0) {
            set_io_error(ctx, "cannot create directory", path, errno);
            return -1;
        }
        journal_add(ctx, path, 1, NULL, 0);
        if (fstatat(parent_fd, leaf, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISDIR(st.st_mode)) {
            set_io_error(ctx, "cannot confirm directory", path,
                         errno == 0 ? EIO : errno);
            return -1;
        }
        ctx->journal[ctx->journal_count - 1].identity_known = 1;
        ctx->journal[ctx->journal_count - 1].dev = st.st_dev;
        ctx->journal[ctx->journal_count - 1].ino = st.st_ino;
        ctx->dir_fd[index] = relocate_internal_fd(
            ctx, ctx->ops->open_dir(parent_fd, leaf), path);
        if (ctx->dir_fd[index] < 0) {
            if (ctx->error == TC_IMPL_OK) {
                set_io_error(ctx, "cannot open directory", path, errno);
            }
            return -1;
        }
        ctx->dir_state[index] = DIR_PRESENT;
    }
    return 0;
}

static int write_all(InitContext *ctx, int fd, const void *data, size_t length) {
    const unsigned char *cursor = data;
    while (length > 0) {
        ssize_t written = ctx->ops->write_fd(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        cursor += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

static int create_target(InitContext *ctx, int index, const void *data,
                         size_t length) {
    InitTarget *target = &ctx->targets[index];
    const char *path = target->path;
    const char *leaf = strrchr(path, '/');
    int parent_fd = target->dir_index >= 0 ? ctx->dir_fd[target->dir_index]
                                           : ctx->root_fd;
    int fd;
    struct stat st;
    if (target->state != FILE_MISSING) {
        return 0;
    }
    leaf = leaf == NULL ? path : leaf + 1;
    fd = ctx->ops->create_file(parent_fd, leaf, 0666);
    if (fd < 0) {
        set_io_error(ctx, "cannot create file", path, errno);
        return -1;
    }
    target->created = 1;
    if (fstat(fd, &st) != 0) {
        journal_add(ctx, path, 0, NULL, 0);
        set_io_error(ctx, "cannot confirm file", path, errno);
        (void)ctx->ops->close_fd(fd);
        return -1;
    }
    journal_add(ctx, path, 0, &st, 1);
    fd = relocate_internal_fd(ctx, fd, path);
    if (fd < 0) {
        return -1;
    }
    if (write_all(ctx, fd, data, length) != 0) {
        set_io_error(ctx, "cannot write file", path, errno);
        (void)ctx->ops->close_fd(fd);
        return -1;
    }
    if (ctx->ops->close_fd(fd) != 0) {
        set_io_error(ctx, "cannot close file", path, errno);
        return -1;
    }
    return 0;
}

static int close_directories(InitContext *ctx) {
    int i;
    for (i = 0; i < INIT_DIR_COUNT; i++) {
        if (ctx->dir_fd[i] >= 0) {
            if (ctx->ops->close_fd(ctx->dir_fd[i]) != 0) {
                set_io_error(ctx, "cannot close directory", init_dir_names[i],
                             errno);
                ctx->dir_fd[i] = -1;
                return -1;
            }
            ctx->dir_fd[i] = -1;
        }
    }
    return 0;
}

static int cleanup_parent(InitContext *ctx, const char *path) {
    const char *slash = strchr(path, '/');
    if (slash == NULL) {
        return ctx->root_fd;
    }
    if (strcmp(path, "tests/smoke") == 0) {
        return relocate_internal_fd(ctx, ctx->ops->open_dir(ctx->root_fd, "tests"),
                                    "tests");
    }
    if (strncmp(path, "tests/smoke/", sizeof("tests/smoke/") - 1) == 0) {
        int fd = relocate_internal_fd(ctx, ctx->ops->open_dir(ctx->root_fd, "tests"),
                                      "tests");
        if (fd < 0) {
            return -1;
        }
        {
            int smoke = relocate_internal_fd(ctx, ctx->ops->open_dir(fd, "smoke"),
                                             "tests/smoke");
            int saved_errno = errno;
            (void)ctx->ops->close_fd(fd);
            errno = saved_errno;
            return smoke;
        }
    }
    return relocate_internal_fd(ctx, ctx->ops->open_dir(ctx->root_fd, "src"),
                                "src");
}

static void rollback(InitContext *ctx) {
    size_t i;
    for (i = ctx->journal_count; i > 0; i--) {
        InitJournalEntry *entry = &ctx->journal[i - 1];
        const char *slash = strrchr(entry->path, '/');
        const char *leaf = slash == NULL ? entry->path : slash + 1;
        int parent_fd = cleanup_parent(ctx, entry->path);
        int removed = 0;
        if (parent_fd < 0) {
            entry->cleanup_failed = 1;
            continue;
        }
        if (!entry->identity_known) {
            entry->cleanup_failed = 1;
        } else {
            struct stat st;
            if (fstatat(parent_fd, leaf, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                if (!missing_entry(errno)) {
                    entry->cleanup_failed = 1;
                }
            } else if (!same_identity(&st, &(struct stat){
                           .st_dev = entry->dev, .st_ino = entry->ino})) {
                entry->cleanup_failed = 1;
            } else {
                removed = entry->is_dir
                    ? ctx->ops->remove_dir(parent_fd, leaf) == 0
                    : ctx->ops->remove_file(parent_fd, leaf) == 0;
                if (!removed) {
                    entry->cleanup_failed = 1;
                }
            }
        }
        if (!(slash == NULL && parent_fd == ctx->root_fd)) {
            (void)ctx->ops->close_fd(parent_fd);
        }
    }
}

static int report_error(InitContext *ctx) {
    const char *prefix = ctx->error == TC_IMPL_IO ? "tc: tc_error_io: "
                      : ctx->error == TC_IMPL_USAGE ? "tc: "
                      : ctx->error == TC_IMPL_OOM
                          ? "tc: " TC_ERR_OUT_OF_MEMORY_SYMBOL ": "
                            TC_ERR_OUT_OF_MEMORY_PRINT_NAME ": "
                      : "tc: tc_error_internal: ";
    size_t i;
    if (!ctx->stderr_safe) {
        return 0;
    }
    if (ctx->error == TC_IMPL_OOM) {
        if (fprintf(ctx->stderr, "%s%s\n", prefix,
                    TC_ERR_OUT_OF_MEMORY_MESSAGE) < 0) {
            return -1;
        }
    } else if (fprintf(ctx->stderr, "%s%s\n", prefix, ctx->detail) < 0) {
        return -1;
    }
    for (i = 0; i < ctx->journal_count; i++) {
        if (ctx->journal[i].cleanup_failed &&
            fprintf(ctx->stderr, "cleanup failed: %s\n",
                    ctx->journal[i].path) < 0) {
            return -1;
        }
    }
    return fflush(ctx->stderr) == 0 ? 0 : -1;
}

static char *make_manifest(InitContext *ctx) {
    const char *format =
        "tc-project 1\n"
        "name = \"%s\"\n"
        "entry = \"src/main.tc\"\n"
        "include = \"src\"\n"
        "profile = \"aarch64-linux-user\"\n";
    int length = snprintf(NULL, 0, format, ctx->name);
    char *manifest;
    if (length < 0) {
        set_error(ctx, TC_IMPL_INTERNAL, "cannot format tc.project");
        return NULL;
    }
    manifest = malloc((size_t)length + 1u);
    if (manifest == NULL) {
        set_error(ctx, TC_IMPL_OOM, "memory allocation failed");
        return NULL;
    }
    if (snprintf(manifest, (size_t)length + 1u, format, ctx->name) != length) {
        free(manifest);
        set_error(ctx, TC_IMPL_INTERNAL, "cannot format tc.project");
        return NULL;
    }
    return manifest;
}

static int write_report(InitContext *ctx) {
    int i;
    if (fprintf(ctx->stdout, "initialized project \"%s\"\n", ctx->name) < 0) {
        return -1;
    }
    for (i = 0; i < INIT_TARGET_COUNT; i++) {
        const char *status = ctx->targets[i].created ? "created" : "skipped";
        if (fprintf(ctx->stdout, "%s %s\n", status, init_target_paths[i]) < 0) {
            return -1;
        }
    }
    if (fprintf(ctx->stdout, "next: tc --test\n") < 0) {
        return -1;
    }
    return fflush(ctx->stdout) == 0 ? 0 : -1;
}

static TcImplError init_impl(InitContext *ctx) {
    char *canonical = NULL;
    char *manifest = NULL;
    int alias;
    int i;
    TcImplError result;

    if (ctx->name == NULL || !tc_project_name_valid(ctx->name)) {
        set_error(ctx, TC_IMPL_USAGE, "invalid project name");
    }
    canonical = realpath(".", NULL);
    if (canonical == NULL) {
        set_io_error(ctx, "cannot canonicalize current directory", NULL, errno);
        return ctx->error;
    }
    ctx->root_fd = relocate_internal_fd(
        ctx, tc_fd_open(canonical,
                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC), NULL);
    free(canonical);
    if (ctx->root_fd < 0) {
        if (ctx->error == TC_IMPL_OK) {
            set_io_error(ctx, "cannot open current directory", NULL, errno);
        }
        return ctx->error;
    }

    (void)discover_targets(ctx);
    (void)probe_skipped_referents(ctx);
    alias = check_identities(ctx);
    if (ctx->error != TC_IMPL_OK) {
        int report_failed;
        result = ctx->error;
        report_failed = report_error(ctx) != 0;
        (void)close_directories(ctx);
        (void)close(ctx->root_fd);
        ctx->root_fd = -1;
        return report_failed ? TC_IMPL_IO : result;
    }
    if (alias) {
        (void)close_directories(ctx);
        (void)close(ctx->root_fd);
        ctx->root_fd = -1;
        return TC_IMPL_USAGE;
    }

    manifest = make_manifest(ctx);
    if (manifest == NULL) {
        result = ctx->error;
        (void)report_error(ctx);
        (void)close_directories(ctx);
        (void)close(ctx->root_fd);
        ctx->root_fd = -1;
        return result;
    }
    for (i = 0; i < INIT_DIR_COUNT; i++) {
        if (ensure_dir(ctx, i) != 0) {
            goto fail;
        }
    }
    if (create_target(ctx, 0, manifest, strlen(manifest)) != 0 ||
        create_target(ctx, 1, init_program, sizeof(init_program) - 1u) != 0 ||
        create_target(ctx, 2, init_program, sizeof(init_program) - 1u) != 0 ||
        create_target(ctx, 3, init_stdout, sizeof(init_stdout)) != 0 ||
        create_target(ctx, 4, init_gitignore, sizeof(init_gitignore) - 1u) != 0 ||
        create_target(ctx, 5, init_gitattributes,
                      sizeof(init_gitattributes) - 1u) != 0 ||
        close_directories(ctx) != 0) {
        goto fail;
    }
    free(manifest);
    (void)close(ctx->root_fd);
    ctx->root_fd = -1;
    if (write_report(ctx) != 0) {
        if (ctx->stderr_safe) {
            (void)fprintf(ctx->stderr,
                          "tc: tc_error_io: failed to write initialization report\n");
            (void)fflush(ctx->stderr);
        }
        return TC_IMPL_IO;
    }
    return TC_IMPL_OK;

fail:
    free(manifest);
    rollback(ctx);
    (void)close_directories(ctx);
    result = ctx->error;
    if (report_error(ctx) != 0) {
        result = TC_IMPL_IO;
    }
    (void)close(ctx->root_fd);
    ctx->root_fd = -1;
    return result;
}

TcImplError tc_project_init_with_ops(const char *project_name, FILE *stdout,
                                     FILE *stderr, const TcProjectInitOps *ops) {
    struct sigaction ignore;
    InitContext ctx;
    TcImplError result;
    int i;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = project_name;
    ctx.stdout = stdout;
    ctx.stderr = stderr;
    ctx.root_fd = -1;
    ctx.stderr_safe = 1;
    for (i = 0; i < INIT_DIR_COUNT; i++) {
        ctx.dir_fd[i] = -1;
    }
    ctx.ops = ops != NULL ? ops : &default_ops;
    memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
#ifdef SIGPIPE
    /* Keep SIGPIPE ignored after return: a failed stdio flush may retry during
     * process teardown, after the initialization operation has reported 70. */
    (void)sigaction(SIGPIPE, &ignore, NULL);
#endif
    result = init_impl(&ctx);
    return result;
}

TcImplError tc_project_init(const char *project_name, FILE *stdout, FILE *stderr) {
    return tc_project_init_with_ops(project_name, stdout, stderr, NULL);
}
