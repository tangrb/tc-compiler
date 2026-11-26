/* TC 0.0.46 tc-aot backend driver (design §11).
 *
 * Pipeline: finalized TcIR -> AArch64 `.s` -> clang integrated assembler ->
 * lld (+ freestanding runtime archive) -> ELF.  The driver keeps the static
 * front end authoritative: it only runs after LEX/SYN/IMPORT/SEM/CT and IR
 * finalization succeeded, and it never reports a language diagnostic.
 */
/* fork/exec/execvp/waitpid are POSIX; mkstemps is a GNU extension; both are
 * hidden in strict ISO C mode. */
#define _GNU_SOURCE

#include "driver/aot_driver.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/fd.h"
#include "backend/aarch64/emit.h"
#include "backend/arm32/emit.h"
#include "backend/common/toolchain.h"
#include "backend/riscv32/emit.h"
#include "diagnostic/diagnostic.h"
#include "frontend/input.h"
#include "frontend/lex.h"
#include "ir/linemap.h"
#include "driver/observe.h"

#ifndef TC_AARCH64_RUNTIME_LIB
#define TC_AARCH64_RUNTIME_LIB ""
#endif
#ifndef TC_AARCH64_BAREMETAL_RUNTIME_LIB
#define TC_AARCH64_BAREMETAL_RUNTIME_LIB ""
#endif
#ifndef TC_ARM32_RUNTIME_LIB
#define TC_ARM32_RUNTIME_LIB ""
#endif
#ifndef TC_RISCV32_RUNTIME_LIB
#define TC_RISCV32_RUNTIME_LIB ""
#endif
#ifndef TC_AARCH64_LINKER_SCRIPT
#define TC_AARCH64_LINKER_SCRIPT ""
#endif
#ifndef TC_ARM32_LINKER_SCRIPT
#define TC_ARM32_LINKER_SCRIPT ""
#endif
#ifndef TC_RISCV32_LINKER_SCRIPT
#define TC_RISCV32_LINKER_SCRIPT ""
#endif
#ifndef TC_TARGET_ASSEMBLER
#define TC_TARGET_ASSEMBLER "clang"
#endif
#ifndef TC_TARGET_LINKER
#define TC_TARGET_LINKER "ld.lld"
#endif

/* One supported target/platform-instance binding (design §10.2/§11). */
typedef struct {
    const char *name;
    const char *default_runtime;
    const char *default_linker_script;
    const char *linker_emulation;
    int is_arm32;
    int is_riscv32;
    const char *const *assembler_flags;
    const char *const *linker_flags;
} TcTargetInstance;

static const char *const riscv32_assembler_flags[] = {
    "-march=rv32imafdc", "-mabi=ilp32", "-mno-relax", NULL
};
static const char *const riscv32_linker_flags[] = { "--no-relax", NULL };

static int select_instance(const TcPlatformProfile *profile,
                           TcTargetInstance *out) {
    const char *name = profile != NULL && profile->target != NULL
                           ? profile->target->name
                           : NULL;
    if (name == NULL) {
        return 0;
    }
    out->is_arm32 = 0;
    out->is_riscv32 = 0;
    out->assembler_flags = NULL;
    out->linker_flags = NULL;
    if (strcmp(name, "aarch64-linux-user") == 0) {
        out->name = name;
        out->default_runtime = TC_AARCH64_RUNTIME_LIB;
        out->default_linker_script = TC_AARCH64_LINKER_SCRIPT;
        out->linker_emulation = "aarch64elf";
        return 1;
    }
    if (strcmp(name, "aarch64-baremetal") == 0) {
        out->name = name;
        out->default_runtime = TC_AARCH64_BAREMETAL_RUNTIME_LIB;
        out->default_linker_script = TC_AARCH64_LINKER_SCRIPT;
        out->linker_emulation = "aarch64elf";
        return 1;
    }
    if (strcmp(name, "arm32-baremetal") == 0) {
        out->name = name;
        out->default_runtime = TC_ARM32_RUNTIME_LIB;
        out->default_linker_script = TC_ARM32_LINKER_SCRIPT;
        out->linker_emulation = "armelf";
        out->is_arm32 = 1;
        return 1;
    }
    if (strcmp(name, "riscv32-baremetal") == 0) {
        out->name = name;
        out->default_runtime = TC_RISCV32_RUNTIME_LIB;
        out->default_linker_script = TC_RISCV32_LINKER_SCRIPT;
        out->linker_emulation = "elf32lriscv";
        out->is_riscv32 = 1;
        out->assembler_flags = riscv32_assembler_flags;
        out->linker_flags = riscv32_linker_flags;
        return 1;
    }
    return 0;
}

static int report(TcImplError error, const char *detail) {
    int failed;
    if (error == TC_IMPL_OOM) {
        failed = tc_cli_write(stderr, "tc-aot: %s: %s: %s\n",
                              tc_impl_error_symbol(error),
                              tc_impl_error_print_name(error),
                              tc_impl_error_message(error));
    } else {
        failed = tc_cli_write(stderr, "tc-aot: %s: %s",
                              tc_impl_error_symbol(error),
                              tc_impl_error_message(error));
        if (failed == 0 && detail != NULL) {
            failed = tc_cli_write(stderr, ": %s", detail);
        }
        if (failed == 0) {
            failed = tc_cli_write(stderr, "\n");
        }
    }
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return tc_impl_error_exit(error);
}

static int report_unavailable(const char *detail) {
    return report(TC_IMPL_UNAVAILABLE, detail);
}


static int run_tool(char *const argv[]) {
    pid_t pid;
    int status = 0;
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static int same_identity(const struct stat *left, const struct stat *right) {
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static TcImplError open_error(void) {
    return errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO;
}

static TcImplError resolve_output_path(const char *path, char **out);


static TcImplError input_matches(const char *path, const struct stat *identity,
                                 const char *resolved_path, int *matches) {
    int fd = tc_fd_open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
    struct stat st;
    TcImplError impl;
    *matches = 0;
    if (fd < 0) {
        if (errno != ENOENT && errno != ENOTDIR) {
            return open_error();
        }
    } else {
        if (fstat(fd, &st) != 0) {
            impl = open_error();
            close(fd);
            return impl;
        }
        *matches = same_identity(&st, identity);
        close(fd);
        if (*matches) {
            return TC_IMPL_OK;
        }
    }
    if (resolved_path != NULL) {
        char *canonical = NULL;
        impl = resolve_output_path(path, &canonical);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
        *matches = strcmp(canonical, resolved_path) == 0;
        free(canonical);
    }
    return TC_IMPL_OK;
}

static TcImplError discard_output(TcAotOutput *output) {
    TcImplError impl = TC_IMPL_OK;
    if (output->fd >= 0) {
        if (close(output->fd) != 0) {
            impl = open_error();
        }
        output->fd = -1;
    }
    if (output->created) {
        struct stat st;
        if (lstat(output->path, &st) == 0) {
            if (same_identity(&st, &output->identity)) {
                if (unlink(output->path) != 0) {
                    impl = TC_IMPL_IO;
                }
            } else {
                impl = TC_IMPL_IO;
            }
        } else if (errno != ENOENT) {
            impl = TC_IMPL_IO;
        }
        output->created = 0;
    }
    free(output->path);
    output->path = NULL;
    return impl;
}

static int discard_output_or_report(TcAotOutput *output) {
    TcImplError impl = discard_output(output);
    return impl == TC_IMPL_OK ? 0 : report(impl, "cannot clean output reservation");
}

static int release_output(TcAotOutput *output) {
    output->created = 0;
    return discard_output_or_report(output);
}

static size_t input_count(const TcAnalysis *analysis, size_t nextra_inputs) {
    return tc_analysis_input_count(analysis) + nextra_inputs;
}

static const char *input_path(const TcAnalysis *analysis,
                              const char *const *extra_inputs, size_t index) {
    size_t module_count = tc_analysis_input_count(analysis);
    return index < module_count ? tc_analysis_input_path(analysis, index)
                                : extra_inputs[index - module_count];
}

void tc_aot_stdio_capture(TcAotStdio *stdio) {
    size_t i;
    memset(stdio, 0, sizeof(*stdio));
    for (i = 0u; i < 3u; i++) {
        stdio->descriptors[i].open =
            fstat((int)i, &stdio->descriptors[i].identity) == 0;
    }
}

static TcImplError check_descriptor_inputs(const TcAotStdio *stdio,
                                           size_t descriptor,
                                           const TcAnalysis *analysis,
                                           const char *const *extra_inputs,
                                           size_t nextra_inputs) {
    size_t i;
    if (!stdio->descriptors[descriptor].open) {
        return TC_IMPL_OK;
    }
    for (i = 0u; i < input_count(analysis, nextra_inputs); i++) {
        const char *path = input_path(analysis, extra_inputs, i);
        int matches;
        TcImplError impl;
        if (path == NULL) {
            return TC_IMPL_INTERNAL;
        }
        impl = input_matches(path, &stdio->descriptors[descriptor].identity,
                             NULL, &matches);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
        if (matches) {
            return TC_IMPL_USAGE;
        }
    }
    return TC_IMPL_OK;
}

static TcImplError check_stdio(const TcAotStdio *stdio,
                               const TcAnalysis *analysis,
                               const char *const *extra_inputs,
                               size_t nextra_inputs, int protect_stdout) {
    TcImplError impl;
    if (stdio->descriptors[0].open && stdio->descriptors[2].open &&
        same_identity(&stdio->descriptors[0].identity,
                      &stdio->descriptors[2].identity)) {
        return TC_IMPL_USAGE;
    }
    impl = check_descriptor_inputs(stdio, 2u, analysis, extra_inputs,
                                   nextra_inputs);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    if (protect_stdout) {
        if (stdio->descriptors[1].open &&
            ((stdio->descriptors[0].open &&
              same_identity(&stdio->descriptors[0].identity,
                            &stdio->descriptors[1].identity)) ||
             (stdio->descriptors[2].open &&
              same_identity(&stdio->descriptors[2].identity,
                            &stdio->descriptors[1].identity)))) {
            return TC_IMPL_USAGE;
        }
        return check_descriptor_inputs(stdio, 1u, analysis, extra_inputs,
                                       nextra_inputs);
    }
    return TC_IMPL_OK;
}

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
} TcInputQueue;

static void input_queue_destroy(TcInputQueue *queue) {
    size_t i;
    for (i = 0u; i < queue->count; i++) {
        free(queue->paths[i]);
    }
    free(queue->paths);
    memset(queue, 0, sizeof(*queue));
}

static int input_queue_seen(const TcInputQueue *queue,
                            const struct stat *identity) {
    size_t i;
    struct stat st;
    for (i = 0u; i < queue->count; i++) {
        if (stat(queue->paths[i], &st) == 0 && same_identity(&st, identity)) {
            return 1;
        }
    }
    return 0;
}

static TcImplError input_queue_add(TcInputQueue *queue, const char *path) {
    char *canonical;
    char **fresh;
    size_t capacity;
    struct stat st;
    TcImplError impl;
    if (path == NULL) {
        return TC_IMPL_OK;
    }
    if (stat(path, &st) != 0) {
        return errno == ENOENT || errno == ENOTDIR ? TC_IMPL_OK
                                                   : open_error();
    }
    if (!S_ISREG(st.st_mode)) {
        return TC_IMPL_OK;
    }
    if (input_queue_seen(queue, &st)) {
        return TC_IMPL_OK;
    }
    impl = tc_path_resolve(path, 0, &canonical, NULL);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    if (queue->count == queue->capacity) {
        capacity = queue->capacity == 0u ? 8u : queue->capacity * 2u;
        if (capacity < queue->capacity ||
            capacity > SIZE_MAX / sizeof(*fresh)) {
            free(canonical);
            return TC_IMPL_OOM;
        }
        fresh = (char **)realloc(queue->paths, capacity * sizeof(*fresh));
        if (fresh == NULL) {
            free(canonical);
            return TC_IMPL_OOM;
        }
        queue->paths = fresh;
        queue->capacity = capacity;
    }
    queue->paths[queue->count++] = canonical;
    return TC_IMPL_OK;
}

static TcImplError scan_import_line(const uint8_t *bytes, size_t size,
                                    const char *display, TcArena *arena,
                                    const char *directory,
                                    const char *const *include_roots,
                                    size_t ninclude_roots,
                                    const struct stat *identity,
                                    const char *output_path,
                                    TcInputQueue *queue) {
    TcLexResult lexed;
    TcImplError impl = TC_IMPL_OK;
    size_t i;
    int parsed;
    memset(&lexed, 0, sizeof(lexed));
    parsed = tc_lex(display, bytes, size, arena, &lexed);
    if (parsed != 0) {
        return lexed.impl;
    }
    if (lexed.count >= 2u && lexed.tokens[0].kind == TC_TOK_IMPORT &&
        lexed.tokens[1].kind == TC_TOK_IDENT) {
        const char *name = lexed.tokens[1].text;
        size_t length = strlen(name);
        char *named = (char *)malloc(length + 4u);
        if (named == NULL) {
            return TC_IMPL_OOM;
        }
        memcpy(named, name, length);
        memcpy(named + length, ".tc", 4u);
        for (i = 0u; i <= ninclude_roots; i++) {
            char *candidate = tc_path_join(i == 0u ? directory
                                                   : include_roots[i - 1u],
                                           named);
            struct stat st;
            int exists;
            int matches;
            if (candidate == NULL) {
                impl = TC_IMPL_OOM;
                break;
            }
            impl = input_matches(candidate, identity, output_path, &matches);
            if (impl != TC_IMPL_OK) {
                free(candidate);
                break;
            }
            if (matches) {
                impl = TC_IMPL_USAGE;
                free(candidate);
                break;
            }
            exists = stat(candidate, &st) == 0;
            if (!exists && errno != ENOENT && errno != ENOTDIR) {
                impl = open_error();
                free(candidate);
                break;
            }
            if (exists) {
                if (same_identity(&st, identity)) {
                    impl = TC_IMPL_USAGE;
                } else if (S_ISREG(st.st_mode)) {
                    impl = input_queue_add(queue, candidate);
                }
                free(candidate);
                break;
            }
            free(candidate);
        }
        free(named);
    }
    return impl;
}

static TcImplError scan_imports(const char *path, const char *const *include_roots,
                                size_t ninclude_roots,
                                const struct stat *identity,
                                const char *output_path,
                                TcInputQueue *queue) {
    TcSourceBytes source;
    TcArena arena;
    char *directory = NULL;
    size_t start = 0u;
    TcImplError impl;
    memset(&source, 0, sizeof(source));
    tc_arena_init(&arena, NULL);
    impl = tc_source_read(path, &source);
    if (impl == TC_IMPL_INTERNAL) {
        tc_arena_destroy(&arena);
        return TC_IMPL_OK;
    }
    if (impl != TC_IMPL_OK) {
        tc_arena_destroy(&arena);
        return impl;
    }
    impl = tc_path_directory(path, &directory);
    if (impl != TC_IMPL_OK) {
        tc_source_free(&source);
        tc_arena_destroy(&arena);
        return impl;
    }
    while (start < source.size) {
        size_t end = start;
        size_t length;
        while (end < source.size) {
            uint8_t c = source.bytes[end];
            if (c == '\\' && end + 1u < source.size &&
                (source.bytes[end + 1u] == '\n' ||
                 source.bytes[end + 1u] == '\r')) {
                end += source.bytes[end + 1u] == '\r' &&
                               end + 2u < source.size &&
                               source.bytes[end + 2u] == '\n'
                           ? 3u
                           : 2u;
                continue;
            }
            if (c == '\n' || c == '\r') {
                break;
            }
            end++;
        }
        length = end - start;
        if (length != 0u) {
            impl = scan_import_line(source.bytes + start, length, path, &arena,
                                    directory, include_roots, ninclude_roots,
                                    identity, output_path, queue);
        }
        if (impl != TC_IMPL_OK) {
            break;
        }
        start = end;
        if (start < source.size) {
            start += source.bytes[start] == '\r' &&
                             start + 1u < source.size &&
                             source.bytes[start + 1u] == '\n'
                         ? 2u
                         : 1u;
        }
    }
    free(directory);
    tc_source_free(&source);
    tc_arena_destroy(&arena);
    return impl;
}

static TcImplError check_potential_inputs(const struct stat *identity,
                                          const char *resolved_path,
                                          const TcAnalysis *analysis,
                                          const char *source,
                                          const char *const *include_roots,
                                          size_t ninclude_roots) {
    TcInputQueue queue;
    size_t i;
    TcImplError impl = TC_IMPL_OK;
    memset(&queue, 0, sizeof(queue));
    impl = input_queue_add(&queue, source);
    if (impl == TC_IMPL_OK) {
        for (i = 0u; i < tc_analysis_input_count(analysis); i++) {
            impl = input_queue_add(&queue,
                                   tc_analysis_input_path(analysis, i));
            if (impl != TC_IMPL_OK) {
                break;
            }
        }
    }
    for (i = 0u; impl == TC_IMPL_OK && i < queue.count; i++) {
        struct stat st;
        if (stat(queue.paths[i], &st) != 0) {
            impl = open_error();
            break;
        }
        if (same_identity(&st, identity)) {
            impl = TC_IMPL_USAGE;
            break;
        }
        impl = scan_imports(queue.paths[i], include_roots, ninclude_roots,
                            identity, resolved_path, &queue);
    }
    input_queue_destroy(&queue);
    return impl;
}

static TcImplError check_output_identity(const TcAotStdio *stdio,
                                         const TcAnalysis *analysis,
                                         TcAnalysisStatus analysis_status,
                                         const char *const *extra_inputs,
                                         size_t nextra_inputs,
                                         const char *source,
                                         const char *const *include_roots,
                                         size_t ninclude_roots,
                                         const struct stat *identity,
                                         const char *resolved_path) {
    TcImplError impl;
    size_t i;
    if (stdio->descriptors[0].open &&
        same_identity(identity, &stdio->descriptors[0].identity)) {
        return TC_IMPL_USAGE;
    }
    if (stdio->descriptors[2].open &&
        same_identity(identity, &stdio->descriptors[2].identity)) {
        return TC_IMPL_USAGE;
    }
    if (source != NULL) {
        int matches;
        impl = input_matches(source, identity, resolved_path, &matches);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
        if (matches) {
            return TC_IMPL_USAGE;
        }
    }
    for (i = 0u; i < input_count(analysis, nextra_inputs); i++) {
        const char *path = input_path(analysis, extra_inputs, i);
        int matches;
        if (path == NULL) {
            return TC_IMPL_INTERNAL;
        }
        impl = input_matches(path, identity, resolved_path, &matches);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
        if (matches) {
            return TC_IMPL_USAGE;
        }
    }
    if (analysis_status != TC_ANALYSIS_OK) {
        impl = check_potential_inputs(identity, resolved_path, analysis, source,
                                      include_roots, ninclude_roots);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
    }
    return TC_IMPL_OK;
}

static TcImplError resolve_output_path(const char *path, char **out) {
    char *absolute = NULL;
    char *target = NULL;
    char *resolved = NULL;
    size_t links = 0u;
    int missing = 0;
    TcImplError impl;
    *out = NULL;
    impl = tc_path_absolute(path, &absolute);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    for (;;) {
        struct stat st;
        ssize_t length;
        size_t capacity = 256u;
        if (lstat(absolute, &st) != 0) {
            if (errno == ELOOP || errno == ENAMETOOLONG) {
                free(absolute);
                return TC_IMPL_USAGE;
            }
            if (errno != ENOENT && errno != ENOTDIR) {
                impl = errno == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO;
                free(absolute);
                return impl;
            }
            break;
        }
        if (!S_ISLNK(st.st_mode)) {
            break;
        }
        if (++links > 40u) {
            free(absolute);
            return TC_IMPL_USAGE;
        }
        for (;;) {
            target = (char *)malloc(capacity + 1u);
            if (target == NULL) {
                free(absolute);
                return TC_IMPL_OOM;
            }
            length = readlink(absolute, target, capacity);
            if (length < 0) {
                free(target);
                free(absolute);
                return open_error();
            }
            if ((size_t)length < capacity) {
                target[length] = '\0';
                break;
            }
            free(target);
            if (capacity > SIZE_MAX / 2u - 1u) {
                free(absolute);
                return TC_IMPL_OOM;
            }
            capacity *= 2u;
        }
        if (target[0] == '/') {
            free(absolute);
            absolute = target;
        } else {
            char *directory = NULL;
            char *joined;
            impl = tc_path_directory(absolute, &directory);
            if (impl != TC_IMPL_OK) {
                free(target);
                free(absolute);
                return impl;
            }
            joined = tc_path_join(directory, target);
            free(directory);
            free(target);
            free(absolute);
            if (joined == NULL) {
                return TC_IMPL_OOM;
            }
            absolute = joined;
        }
        target = NULL;
    }
    impl = tc_path_resolve(absolute, 0, &resolved, &missing);
    free(absolute);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    (void)missing;
    *out = resolved;
    return TC_IMPL_OK;
}

static int open_output_fd(const char *path, int flags, int created,
                          TcAotOutput *output) {
    int fd = tc_fd_open(path, flags, 0666);
    int error_number;
    if (fd < 0) {
        return -1;
    }
    output->created = created;
    if (fstat(fd, &output->identity) != 0) {
        struct stat st;
        error_number = errno;
        if (created && lstat(path, &st) == 0) {
            output->identity = st;
        }
        close(fd);
        errno = error_number;
        return -1;
    }
    output->fd = tc_fd_relocate(fd);
    return output->fd;
}

static TcImplError preflight_one_output(const TcAotStdio *stdio,
                                        const TcAnalysis *analysis,
                                        TcAnalysisStatus analysis_status,
                                        const char *path,
                                        const char *const *extra_inputs,
                                        size_t nextra_inputs,
                                        const char *source,
                                        const char *const *include_roots,
                                        size_t ninclude_roots,
                                        TcAotOutput *output, int create_output,
                                        int allow_stdout) {
    struct stat path_stat;
    int exists;
    int stdout_alias = 0;
    TcImplError impl;
    memset(output, 0, sizeof(*output));
    output->fd = -1;
    if (analysis_status != TC_ANALYSIS_OK && stdio->descriptors[2].open) {
        impl = check_potential_inputs(&stdio->descriptors[2].identity, NULL,
                                      analysis, source, include_roots,
                                      ninclude_roots);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
    }
    if (path == NULL) {
        if (!allow_stdout) {
            return TC_IMPL_OK;
        }
        return check_output_identity(stdio, analysis, analysis_status,
                                     extra_inputs, nextra_inputs, source,
                                     include_roots, ninclude_roots,
                                     &stdio->descriptors[1].identity, NULL);
    }
    if (output->path == NULL) {
        impl = resolve_output_path(path, &output->path);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
    }
    exists = stat(output->path, &path_stat) == 0;
    if (!exists) {
        if (errno == ELOOP) {
            return TC_IMPL_USAGE;
        }
        if (errno != ENOENT && errno != ENOTDIR) {
            return open_error();
        }
        if (!create_output) {
            return TC_IMPL_OK;
        }
    } else if (!S_ISREG(path_stat.st_mode)) {
        if (S_ISDIR(path_stat.st_mode) || S_ISSOCK(path_stat.st_mode)) {
            return TC_IMPL_USAGE;
        }
        if (open_output_fd(output->path,
                           O_WRONLY | O_NONBLOCK | O_CLOEXEC, 0, output) < 0) {
            return errno == ENXIO ? TC_IMPL_USAGE : open_error();
        }
        if (!allow_stdout || !stdio->descriptors[1].open ||
            !same_identity(&output->identity, &stdio->descriptors[1].identity)) {
            TcImplError cleanup = discard_output(output);
            return cleanup != TC_IMPL_OK ? cleanup : TC_IMPL_USAGE;
        }
        stdout_alias = 1;
    }
    if (!stdout_alias) {
        int flags = O_WRONLY | O_NONBLOCK | O_CLOEXEC;
        if (!exists && create_output) {
            flags |= O_CREAT | O_EXCL | O_NOFOLLOW;
        }
        if (open_output_fd(output->path, flags, !exists && create_output,
                           output) < 0 && create_output && !exists &&
            errno == EEXIST) {
            exists = stat(output->path, &path_stat) == 0;
            if (!exists || !S_ISREG(path_stat.st_mode)) {
                return exists ? TC_IMPL_USAGE
                              : (errno == ELOOP ? TC_IMPL_USAGE : open_error());
            }
            if (open_output_fd(output->path,
                               O_WRONLY | O_NONBLOCK | O_CLOEXEC, 0, output) < 0) {
                return open_error();
            }
        }
        if (output->fd < 0) {
            if (!exists && create_output && output->created) {
                impl = discard_output(output);
                return impl != TC_IMPL_OK ? impl : open_error();
            }
            return exists || create_output ? open_error() : TC_IMPL_OK;
        }
        if (!S_ISREG(output->identity.st_mode) ||
            (exists && !same_identity(&output->identity, &path_stat))) {
            impl = discard_output(output);
            return impl != TC_IMPL_OK ? impl : TC_IMPL_IO;
        }
    }
    impl = check_output_identity(stdio, analysis, analysis_status, extra_inputs,
                                 nextra_inputs, source, include_roots,
                                 ninclude_roots, &output->identity, output->path);
    if (impl == TC_IMPL_OK && !allow_stdout && stdio->descriptors[1].open &&
        same_identity(&output->identity, &stdio->descriptors[1].identity)) {
        impl = TC_IMPL_USAGE;
    }
    if (impl != TC_IMPL_OK) {
        TcImplError cleanup = discard_output(output);
        return cleanup != TC_IMPL_OK ? cleanup : impl;
    }
    if (!create_output) {
        return discard_output(output);
    }
    return TC_IMPL_OK;
}

TcImplError tc_aot_driver_stderr_guard(const TcAotStdio *stdio,
                                       const TcAnalysis *analysis,
                                       TcAnalysisStatus analysis_status,
                                       const char *const *extra_inputs,
                                       size_t nextra_inputs,
                                       const char *source,
                                       const char *const *include_roots,
                                       size_t ninclude_roots) {
    TcImplError impl = check_stdio(stdio, analysis, extra_inputs, nextra_inputs,
                                   0);
    if (impl != TC_IMPL_OK || analysis_status == TC_ANALYSIS_OK ||
        !stdio->descriptors[2].open) {
        return impl;
    }
    return check_potential_inputs(&stdio->descriptors[2].identity, NULL, analysis,
                                  source, include_roots, ninclude_roots);
}

TcImplError tc_aot_driver_guard(const TcAotStdio *stdio,
                                const TcAnalysis *analysis,
                                TcAnalysisStatus analysis_status,
                                const TcCliOptions *options,
                                const char *const *extra_inputs,
                                size_t nextra_inputs,
                                const char *source,
                                const char *const *include_roots,
                                size_t ninclude_roots,
                                int aot,
                                int create_output) {
    if (!aot) {
        TcImplError impl = check_stdio(stdio, analysis, extra_inputs,
                                       nextra_inputs, 1);
        size_t descriptor;
        if (impl != TC_IMPL_OK || analysis_status == TC_ANALYSIS_OK) {
            return impl;
        }
        for (descriptor = 1u; descriptor <= 2u; descriptor++) {
            if (stdio->descriptors[descriptor].open) {
                impl = check_potential_inputs(
                    &stdio->descriptors[descriptor].identity, NULL, analysis,
                    source, include_roots, ninclude_roots);
                if (impl != TC_IMPL_OK) {
                    return impl;
                }
            }
        }
        return TC_IMPL_OK;
    }
    {
        TcAotOutput output;
        TcImplError impl = check_stdio(stdio, analysis, extra_inputs,
                                       nextra_inputs, 0);
        if (impl == TC_IMPL_OK) {
            impl = preflight_one_output(
                stdio, analysis, analysis_status, options->output, extra_inputs,
                nextra_inputs, source, include_roots, ninclude_roots, &output,
                create_output, 1);
        } else {
            memset(&output, 0, sizeof(output));
            output.fd = -1;
        }
        TcImplError cleanup = discard_output(&output);
        return cleanup != TC_IMPL_OK ? cleanup : impl;
    }
}

void tc_aot_outputs_init(TcAotOutputs *outputs) {
    size_t i;
    if (outputs == NULL) {
        return;
    }
    memset(outputs, 0, sizeof(*outputs));
    for (i = 0u; i < TC_AOT_OUTPUT_COUNT; i++) {
        outputs->output[i].fd = -1;
    }
}

TcImplError tc_aot_outputs_discard(TcAotOutputs *outputs) {
    TcImplError first = TC_IMPL_OK;
    size_t i;
    if (outputs == NULL) {
        return TC_IMPL_OK;
    }
    for (i = 0u; i < TC_AOT_OUTPUT_COUNT; i++) {
        TcImplError impl = discard_output(&outputs->output[i]);
        if (first == TC_IMPL_OK && impl != TC_IMPL_OK) {
            first = impl;
        }
    }
    return first;
}

TcImplError tc_aot_outputs_release(TcAotOutputs *outputs) {
    size_t i;
    if (outputs == NULL) {
        return TC_IMPL_OK;
    }
    for (i = 0u; i < TC_AOT_OUTPUT_COUNT; i++) {
        outputs->output[i].created = 0;
    }
    return tc_aot_outputs_discard(outputs);
}

TcImplError tc_aot_outputs_release_observations(TcAotOutputs *outputs) {
    size_t i;
    TcImplError impl = TC_IMPL_OK;
    if (outputs == NULL) {
        return TC_IMPL_OK;
    }
    for (i = TC_AOT_OUTPUT_DUMP; i <= TC_AOT_OUTPUT_STATS; i++) {
        TcImplError current = release_output(&outputs->output[i]);
        if (impl == TC_IMPL_OK && current != TC_IMPL_OK) {
            impl = current;
        }
    }
    return impl;
}

static int outputs_alias(const TcAotOutput *left, const TcAotOutput *right) {
    if (left->path == NULL || right->path == NULL) {
        return left->path == NULL && right->path == NULL;
    }
    if (left->fd >= 0 && right->fd >= 0) {
        return same_identity(&left->identity, &right->identity);
    }
    return strcmp(left->path, right->path) == 0;
}

TcImplError tc_aot_outputs_verify(const TcAotStdio *stdio,
                                  const TcAnalysis *analysis,
                                  const char *const *extra_inputs,
                                  size_t nextra_inputs,
                                  int aot,
                                  const TcAotOutputs *outputs) {
    TcImplError impl;
    size_t i;
    if (stdio == NULL || analysis == NULL || outputs == NULL ||
        (nextra_inputs != 0u && extra_inputs == NULL)) {
        return TC_IMPL_INTERNAL;
    }
    impl = check_stdio(stdio, analysis, extra_inputs, nextra_inputs, !aot);
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    for (i = 0u; i < TC_AOT_OUTPUT_COUNT; i++) {
        const TcAotOutput *output = &outputs->output[i];
        struct stat current;
        size_t j;
        if (output->path == NULL) {
            continue;
        }
        if (output->fd < 0 || fstat(output->fd, &current) != 0 ||
            !same_identity(&current, &output->identity) ||
            (!S_ISREG(current.st_mode) &&
             !(i == TC_AOT_OUTPUT_RESULT && stdio->descriptors[1].open &&
               same_identity(&current, &stdio->descriptors[1].identity)))) {
            return TC_IMPL_IO;
        }
        impl = check_output_identity(stdio, analysis, TC_ANALYSIS_OK,
                                     extra_inputs, nextra_inputs, NULL, NULL, 0u,
                                     &output->identity, output->path);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
        for (j = 0u; j < i; j++) {
            if (outputs_alias(output, &outputs->output[j])) {
                return TC_IMPL_USAGE;
            }
        }
    }
    return TC_IMPL_OK;
}

static TcImplError prescan_output_targets(const TcAotStdio *stdio,
                                          const TcAnalysis *analysis,
                                          TcAnalysisStatus analysis_status,
                                          const char *const *extra_inputs,
                                          size_t nextra_inputs,
                                          const char *source,
                                          const char *const *include_roots,
                                          size_t ninclude_roots,
                                          const char *const *paths,
                                          const int *enabled,
                                          TcAotOutputs *outputs) {
    size_t i;
    for (i = 0u; i < TC_AOT_OUTPUT_COUNT; i++) {
        TcAotOutput *output = &outputs->output[i];
        struct stat st;
        int exists;
        TcImplError impl;
        size_t j;
        if (!enabled[i] || paths[i] == NULL) {
            continue;
        }
        impl = resolve_output_path(paths[i], &output->path);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
        exists = stat(output->path, &st) == 0;
        if (!exists) {
            if (errno != ENOENT && errno != ENOTDIR) {
                return errno == ELOOP ? TC_IMPL_USAGE : open_error();
            }
            memset(&output->identity, 0, sizeof(output->identity));
        } else {
            output->identity = st;
            if (!S_ISREG(st.st_mode) &&
                !(i == TC_AOT_OUTPUT_RESULT && stdio->descriptors[1].open &&
                  !S_ISDIR(st.st_mode) && !S_ISSOCK(st.st_mode))) {
                return TC_IMPL_USAGE;
            }
            if (i != TC_AOT_OUTPUT_RESULT && stdio->descriptors[1].open &&
                same_identity(&st, &stdio->descriptors[1].identity)) {
                return TC_IMPL_USAGE;
            }
        }
        impl = check_output_identity(stdio, analysis, analysis_status,
                                     extra_inputs, nextra_inputs, source,
                                     include_roots, ninclude_roots,
                                     &output->identity, output->path);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
        for (j = 0u; j < i; j++) {
            const TcAotOutput *prior = &outputs->output[j];
            struct stat prior_st;
            int prior_exists = prior->path != NULL &&
                               stat(prior->path, &prior_st) == 0;
            if (prior->path != NULL &&
                (strcmp(output->path, prior->path) == 0 ||
                 (exists && prior_exists && same_identity(&st, &prior_st)))) {
                return TC_IMPL_USAGE;
            }
        }
    }
    return TC_IMPL_OK;
}

TcImplError tc_aot_outputs_preflight(const TcAotStdio *stdio,
                                     const TcAnalysis *analysis,
                                     TcAnalysisStatus analysis_status,
                                     const TcCliOptions *options,
                                     const char *const *extra_inputs,
                                     size_t nextra_inputs,
                                     const char *source,
                                     const char *const *include_roots,
                                     size_t ninclude_roots,
                                     int aot, int create_result,
                                     TcAotOutputs *outputs) {
    static const size_t slots[] = {
        TC_AOT_OUTPUT_DUMP, TC_AOT_OUTPUT_TRACE, TC_AOT_OUTPUT_STATS,
        TC_AOT_OUTPUT_RESULT
    };
    const char *paths[TC_AOT_OUTPUT_COUNT] = { NULL, NULL, NULL, NULL };
    int enabled[TC_AOT_OUTPUT_COUNT] = { 0, 0, 0, 0 };
    TcImplError impl = TC_IMPL_OK;
    size_t i;
    if (stdio == NULL || options == NULL || outputs == NULL ||
        (nextra_inputs != 0u && extra_inputs == NULL)) {
        return TC_IMPL_INTERNAL;
    }
    tc_aot_outputs_init(outputs);
    impl = check_stdio(stdio, analysis, extra_inputs, nextra_inputs,
                       !aot || (create_result &&
                                (options->output == NULL ||
                                 options->output[0] == '\0')));
    if (impl != TC_IMPL_OK) {
        return impl;
    }
    if (analysis_status != TC_ANALYSIS_OK && stdio->descriptors[2].open) {
        impl = check_potential_inputs(&stdio->descriptors[2].identity, NULL,
                                      analysis, source, include_roots,
                                      ninclude_roots);
        if (impl != TC_IMPL_OK) {
            return impl;
        }
    }
    if (options->dump_ir_seen) {
        paths[TC_AOT_OUTPUT_DUMP] = options->dump_ir[0] != '\0'
                                       ? options->dump_ir : NULL;
        enabled[TC_AOT_OUTPUT_DUMP] = 1;
    }
    if (options->trace_seen) {
        paths[TC_AOT_OUTPUT_TRACE] = options->trace_out;
        enabled[TC_AOT_OUTPUT_TRACE] = 1;
    }
    if (options->stats) {
        paths[TC_AOT_OUTPUT_STATS] = options->stats_out;
        enabled[TC_AOT_OUTPUT_STATS] = 1;
    }
    if (aot) {
        paths[TC_AOT_OUTPUT_RESULT] = options->output;
        enabled[TC_AOT_OUTPUT_RESULT] = 1;
    }
    impl = prescan_output_targets(stdio, analysis, analysis_status, extra_inputs,
                                  nextra_inputs, source, include_roots,
                                  ninclude_roots, paths, enabled, outputs);
    if (impl != TC_IMPL_OK) {
        TcImplError cleanup = tc_aot_outputs_discard(outputs);
        return cleanup != TC_IMPL_OK ? cleanup : impl;
    }
    for (i = 0u; i < sizeof(slots) / sizeof(slots[0]); i++) {
        size_t slot = slots[i];
        TcAotOutput *output = &outputs->output[slot];
        size_t j;
        if (!enabled[slot]) {
            continue;
        }
        impl = preflight_one_output(
            stdio, analysis, analysis_status, paths[slot], extra_inputs,
            nextra_inputs, source, include_roots, ninclude_roots, output,
            paths[slot] != NULL || (slot == TC_AOT_OUTPUT_RESULT && create_result),
            slot == TC_AOT_OUTPUT_RESULT);
        if (impl != TC_IMPL_OK) {
            TcImplError cleanup = tc_aot_outputs_discard(outputs);
            return cleanup != TC_IMPL_OK ? cleanup : impl;
        }
        if (paths[slot] != NULL) {
            for (j = 0u; j < slot; j++) {
                if (enabled[j] && paths[j] != NULL &&
                    outputs_alias(output, &outputs->output[j])) {
                    TcImplError cleanup = tc_aot_outputs_discard(outputs);
                    return cleanup != TC_IMPL_OK ? cleanup : TC_IMPL_USAGE;
                }
            }
        }
    }
    return TC_IMPL_OK;
}

static int copy_file_to_output(const char *source_path, TcAotOutput *output,
                               int executable) {
    int input_fd = tc_fd_open(source_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
    FILE *input_file = input_fd < 0 ? NULL : fdopen(input_fd, "rb");
    FILE *output_file;
    char buffer[8192];
    size_t bytes_read;
    struct stat current;
    if (input_file == NULL) {
        if (input_fd >= 0) {
            close(input_fd);
        }
        return -1;
    }
    if (fstat(output->fd, &current) != 0 ||
        !same_identity(&current, &output->identity) ||
        (S_ISREG(current.st_mode) &&
         (ftruncate(output->fd, 0) != 0 || lseek(output->fd, 0, SEEK_SET) < 0)) ||
        (executable && S_ISREG(current.st_mode) &&
         fchmod(output->fd, 0755) != 0)) {
        fclose(input_file);
        return -1;
    }
    output_file = fdopen(output->fd, "wb");
    if (output_file == NULL) {
        fclose(input_file);
        return -1;
    }
    output->fd = -1;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), input_file)) != 0) {
        if (fwrite(buffer, 1, bytes_read, output_file) != bytes_read) {
            fclose(input_file);
            fclose(output_file);
            return -1;
        }
    }
    if (ferror(input_file)) {
        fclose(input_file);
        fclose(output_file);
        return -1;
    }
    fclose(input_file);
    return fclose(output_file) == 0 ? 0 : -1;
}

static int make_temp(char *path, size_t path_capacity, const char *suffix) {
    const char *temporary_directory = getenv("TMPDIR");
    int file_descriptor;
    if (temporary_directory == NULL || temporary_directory[0] == '\0') {
        temporary_directory = "/tmp";
    }
    if (snprintf(path, path_capacity, "%s/tc-aot-XXXXXX%s",
                 temporary_directory, suffix) >= (int)path_capacity) {
        return -1;
    }
    file_descriptor = tc_fd_relocate(mkstemps(path, (int)strlen(suffix)));
    if (file_descriptor < 0) {
        return -1;
    }
    close(file_descriptor);
    return 0;
}

static int emit_assembly_to(const TcIR *ir,
                            const TcPlatformProfile *profile,
                            const TcTargetInstance *instance,
                            const TcLineMap *linemap,
                            const char *path, char *error_message,
                            size_t error_capacity, int *is_unsupported,
                            TcTargetStats *target_stats) {
    FILE *output_file;
    int output_fd = -1;
    int unsupported = 0;
    int io_error = 0;
    int oom_error = 0;
    int ok = 0;
    *is_unsupported = 0;
    if (path != NULL) {
        output_fd = tc_fd_open(path, O_WRONLY | O_TRUNC | O_CLOEXEC, 0);
        output_file = output_fd < 0 ? NULL : fdopen(output_fd, "wb");
        if (output_file == NULL && output_fd >= 0) {
            close(output_fd);
        }
    } else {
        output_file = stdout;
    }
    if (output_file == NULL) {
        return report(TC_IMPL_IO, path);
    }
    if (instance->is_arm32) {
        TcArm32EmitStatus status =
            tc_arm32_emit_asm(output_file, ir, profile, profile->target, linemap,
                              error_message, error_capacity, target_stats);
        unsupported = status == TC_ARM32_EMIT_UNSUPPORTED;
        io_error = status == TC_ARM32_EMIT_IO;
        oom_error = status == TC_ARM32_EMIT_OOM;
        ok = status == TC_ARM32_EMIT_OK;
    } else if (instance->is_riscv32) {
        TcRiscv32EmitStatus status =
            tc_riscv32_emit_asm(output_file, ir, profile, profile->target, linemap,
                                error_message, error_capacity, target_stats);
        unsupported = status == TC_RISCV32_EMIT_UNSUPPORTED;
        io_error = status == TC_RISCV32_EMIT_IO;
        oom_error = status == TC_RISCV32_EMIT_OOM;
        ok = status == TC_RISCV32_EMIT_OK;
    } else {
        TcAArch64EmitStatus status =
            tc_aarch64_emit_asm(output_file, ir, profile, profile->target, linemap,
                                error_message, error_capacity, target_stats);
        unsupported = status == TC_AARCH64_EMIT_UNSUPPORTED;
        io_error = status == TC_AARCH64_EMIT_IO;
        oom_error = status == TC_AARCH64_EMIT_OOM;
        ok = status == TC_AARCH64_EMIT_OK;
    }
    if (output_file != stdout && fclose(output_file) != 0) {
        return report(TC_IMPL_IO, path);
    }
    if (output_file == stdout && tc_cli_flush(stdout) != 0) {
        return report(TC_IMPL_IO, "cannot write assembly to stdout");
    }
    if (unsupported) {
        *is_unsupported = 1;
        return tc_impl_error_exit(TC_IMPL_UNAVAILABLE);
    }
    if (io_error) {
        return report(TC_IMPL_IO,
                      error_message[0] != '\0' ? error_message : NULL);
    }
    if (oom_error) {
        return report(TC_IMPL_OOM, NULL);
    }
    if (!ok) {
        return report(TC_IMPL_INTERNAL,
                      error_message[0] != '\0' ? error_message : NULL);
    }
    return 0;
}

static const char *runtime_archive_path(const TcCliOptions *options,
                                        const TcTargetInstance *instance) {
    if (options->runtime_path != NULL) {
        return options->runtime_path;
    }
    return instance->default_runtime;
}

static const char *linker_script_path(const TcCliOptions *options,
                                      const TcTargetInstance *instance) {
    if (options->linker_script != NULL) {
        return options->linker_script;
    }
    return instance->default_linker_script;
}

static int finish_ir_stats(TcAotObserveStats *stats) {
    TcImplError impl = tc_observe_stats_finish_aot(stats, NULL);
    return impl == TC_IMPL_OK ? 0 : report(impl, "cannot finish statistics");
}

static int run_aot(const TcAnalysis *analysis,
                   const TcPlatformProfile *profile,
                   const TcCliOptions *options,
                   const TcLineMap *linemap,
                   TcAotOutputs *outputs,
                   TcAotObserveStats *observe_stats) {
    const TcIR *ir;
    char error_message[512];
    char asm_path[512];
    char obj_path[512];
    char out_path[512];
    int is_unsupported = 0;
    int exit_status;
    int keep_temporaries;
    const char *emit_kind;
    const char *assembler;
    const char *linker;
    const char *target_triple;
    TcTargetInstance instance;
    TcAotOutput *output;
    TcTargetStats target_stats;

    if (analysis == NULL || profile == NULL || options == NULL ||
        outputs == NULL || observe_stats == NULL) {
        return report(TC_IMPL_INTERNAL, "missing AOT driver argument");
    }
    output = &outputs->output[TC_AOT_OUTPUT_RESULT];
    memset(&target_stats, 0, sizeof(target_stats));
    if (!select_instance(profile, &instance)) {
        if (discard_output_or_report(output) != 0) {
            return TC_EXIT_UNAVAILABLE;
        }
        {
            int stats_status = finish_ir_stats(observe_stats);
            if (stats_status != 0) {
                return stats_status;
            }
        }
        return report_unavailable(
            "no backend/toolchain is bound to the selected target descriptor");
    }
    ir = tc_analysis_ir(analysis);
    if (ir == NULL) {
        int stats_status;
        (void)discard_output(output);
        stats_status = finish_ir_stats(observe_stats);
        return stats_status != 0
                   ? stats_status
                   : report(TC_IMPL_INTERNAL, "analysis has no finalized IR");
    }
    emit_kind = options->emit_kind != NULL ? options->emit_kind : "asm";
    if (strcmp(emit_kind, "asm") != 0 && strcmp(emit_kind, "obj") != 0 &&
        strcmp(emit_kind, "elf") != 0 && strcmp(emit_kind, "flat") != 0) {
        (void)discard_output(output);
        {
            int stats_status = finish_ir_stats(observe_stats);
            if (stats_status != 0) {
                return stats_status;
            }
        }
        return report(TC_IMPL_USAGE, "unknown --emit value");
    }
    assembler = options->assembler != NULL
                    ? options->assembler
                    : TC_TARGET_ASSEMBLER;
    linker = options->linker != NULL ? options->linker : TC_TARGET_LINKER;
    target_triple = tc_aot_target_triple(profile->target);
    if (target_triple == NULL) {
        int stats_status;
        (void)discard_output(output);
        stats_status = finish_ir_stats(observe_stats);
        return stats_status != 0
                   ? stats_status
                   : report_unavailable(
                         "no toolchain mapping for the selected target descriptor");
    }
    keep_temporaries = options->keep_temps;
    error_message[0] = '\0';

    if (strcmp(emit_kind, "asm") == 0 && options->output == NULL) {
        int unsupported = 0;
        int status = emit_assembly_to(ir, profile, &instance, linemap, NULL,
                                      error_message, sizeof(error_message),
                                      &unsupported,
                                      options->stats ? &target_stats : NULL);
        if (status != 0) {
            int stats_status = finish_ir_stats(observe_stats);
            if (stats_status != 0) {
                return stats_status;
            }
            if (unsupported) {
                return report_unavailable(
                    error_message[0] != '\0' ? error_message : NULL);
            }
            return status;
        }
        {
            int stats_status = tc_observe_stats_finish_aot(
                observe_stats, options->stats ? &target_stats : NULL);
            if (stats_status != 0) {
                return report(stats_status, "cannot finish statistics");
            }
        }
        return 0;
    }

    if (options->output == NULL) {
        int stats_status = finish_ir_stats(observe_stats);
        return stats_status != 0
                   ? stats_status
                   : report(TC_IMPL_USAGE,
                            "--output is required for --emit=obj/elf/flat");
    }
    if (make_temp(asm_path, sizeof(asm_path), ".s") != 0) {
        if (discard_output_or_report(output) != 0) {
            return TC_EXIT_UNAVAILABLE;
        }
        {
            int stats_status = finish_ir_stats(observe_stats);
            if (stats_status != 0) {
                return stats_status;
            }
        }
        return report(TC_IMPL_IO, "cannot create temporary assembly file");
    }
    exit_status = emit_assembly_to(ir, profile, &instance, linemap, asm_path,
                                   error_message, sizeof(error_message),
                                   &is_unsupported,
                                   options->stats ? &target_stats : NULL);
    if (exit_status != 0) {
        int stats_status;
        if (discard_output_or_report(output) != 0) {
            return TC_EXIT_UNAVAILABLE;
        }
        unlink(asm_path);
        stats_status = finish_ir_stats(observe_stats);
        if (stats_status != 0) {
            return stats_status;
        }
        if (is_unsupported) {
            return report_unavailable(
                error_message[0] != '\0' ? error_message : NULL);
        }
        return exit_status;
    }
    {
        int stats_status = tc_observe_stats_finish_aot(
            observe_stats, options->stats ? &target_stats : NULL);
        if (stats_status != 0) {
            unlink(asm_path);
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            return report(stats_status, "cannot finish statistics");
        }
    }
    if (strcmp(emit_kind, "asm") == 0) {
        if (copy_file_to_output(asm_path, output, 0) != 0) {
            unlink(asm_path);
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep_temporaries) {
            unlink(asm_path);
        }
        if (release_output(output) != 0) {
            return TC_EXIT_UNAVAILABLE;
        }
        return 0;
    }

    if (make_temp(obj_path, sizeof(obj_path), ".o") != 0) {
        unlink(asm_path);
        if (discard_output_or_report(output) != 0) {
            return TC_EXIT_UNAVAILABLE;
        }
        return report(TC_IMPL_IO, "cannot create temporary object file");
    }
    {
        char *argv[16];
        size_t argument_count = 0;
        const char *const *flag;
        int status;
        argv[argument_count++] = (char *)assembler;
        argv[argument_count++] = "-target";
        argv[argument_count++] = (char *)target_triple;
        for (flag = instance.assembler_flags;
             flag != NULL && *flag != NULL; ++flag) {
            argv[argument_count++] = (char *)*flag;
        }
        argv[argument_count++] = "-c";
        if (options->debug) {
            argv[argument_count++] = "-gdwarf-4";
            argv[argument_count++] = "-fdebug-compilation-dir=.";
        }
        argv[argument_count++] = asm_path;
        argv[argument_count++] = "-o";
        argv[argument_count++] = obj_path;
        argv[argument_count] = NULL;
        status = run_tool(argv);
        if (status != 0) {
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            unlink(asm_path);
            unlink(obj_path);
            return report(TC_IMPL_UNAVAILABLE,
                          "clang could not assemble the emitted .s");
        }
    }
    if (strcmp(emit_kind, "obj") == 0) {
        if (copy_file_to_output(obj_path, output, 0) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep_temporaries) {
            unlink(asm_path);
            unlink(obj_path);
        }
        if (release_output(output) != 0) {
            return TC_EXIT_UNAVAILABLE;
        }
        return 0;
    }

    {
        const char *runtime_archive =
            runtime_archive_path(options, &instance);
        const char *linker_script = linker_script_path(options, &instance);
        char *argv[24];
        size_t argument_index = 0;
        const char *const *flag;
        int status;
        if (runtime_archive == NULL || runtime_archive[0] == '\0' ||
            access(runtime_archive, R_OK) != 0) {
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            unlink(asm_path);
            unlink(obj_path);
            return report_unavailable("target runtime archive is unavailable");
        }
        if (linker_script == NULL || linker_script[0] == '\0' ||
            access(linker_script, R_OK) != 0) {
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            unlink(asm_path);
            unlink(obj_path);
            return report_unavailable("target linker script is unavailable");
        }
        int flat = strcmp(emit_kind, "flat") == 0;
        if (make_temp(out_path, sizeof(out_path), flat ? ".bin" : ".elf") != 0) {
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            unlink(asm_path);
            unlink(obj_path);
            return report(TC_IMPL_IO, "cannot create temporary image file");
        }
        argv[argument_index++] = (char *)linker;
        argv[argument_index++] = "-m";
        argv[argument_index++] = (char *)instance.linker_emulation;
        argv[argument_index++] = "-static";
        argv[argument_index++] = "-nostdlib";
        for (flag = instance.linker_flags;
             flag != NULL && *flag != NULL; ++flag) {
            argv[argument_index++] = (char *)*flag;
        }
        /* Standard §3.13.3: distinct function entry addresses; never fold
         * identical code or merge entries. */
        argv[argument_index++] = "--icf=none";
        if (flat) {
            argv[argument_index++] = "--oformat=binary";
        }
        argv[argument_index++] = "-T";
        argv[argument_index++] = (char *)linker_script;
        argv[argument_index++] = "-o";
        argv[argument_index++] = out_path;
        argv[argument_index++] = obj_path;
        argv[argument_index++] = (char *)runtime_archive;
        argv[argument_index] = NULL;
        status = run_tool(argv);
        if (status != 0) {
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
            return report(TC_IMPL_UNAVAILABLE,
                          "ld.lld could not link the target image");
        }
        if (copy_file_to_output(out_path, output, !flat) != 0) {
            if (discard_output_or_report(output) != 0) {
                return TC_EXIT_UNAVAILABLE;
            }
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep_temporaries) {
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
        }
    }
    if (release_output(output) != 0) {
        return TC_EXIT_UNAVAILABLE;
    }
    return 0;
}

int tc_aot_driver_run(const TcAnalysis *analysis,
                      const TcPlatformProfile *profile,
                      const TcCliOptions *options) {
    return tc_aot_driver_run_with_inputs(analysis, profile, options, NULL, 0u);
}

int tc_aot_driver_run_prepared(const TcAotStdio *stdio,
                               const TcAnalysis *analysis,
                               const TcPlatformProfile *profile,
                               const TcCliOptions *options,
                               const char *const *extra_inputs,
                               size_t nextra_inputs,
                               TcAotOutputs *outputs) {
    TcLineMap *linemap = NULL;
    TcAotObserveStats observe_stats;
    TcImplError impl;
    int result;
    if (stdio == NULL || analysis == NULL || profile == NULL || options == NULL ||
        outputs == NULL || (nextra_inputs != 0u && extra_inputs == NULL)) {
        return report(TC_IMPL_INTERNAL, "missing AOT driver argument");
    }
    impl = tc_aot_outputs_verify(stdio, analysis, extra_inputs, nextra_inputs, 1,
                                 outputs);
    if (impl != TC_IMPL_OK) {
        return impl == TC_IMPL_USAGE ? TC_EXIT_USAGE
                                    : tc_impl_error_exit(impl);
    }
    if (options->dump_ir_seen) {
        impl = tc_observe_dump_ir(tc_analysis_ir(analysis), outputs);
        if (impl != TC_IMPL_OK) {
            return tc_impl_error_exit(impl);
        }
    }
    impl = tc_observe_stats_begin_ir(tc_analysis_ir(analysis), profile, options,
                                     outputs, &observe_stats);
    if (impl != TC_IMPL_OK) {
        return tc_impl_error_exit(impl);
    }
    if (options->static_only) {
        impl = tc_observe_stats_finish_aot(&observe_stats, NULL);
        if (impl == TC_IMPL_OK) {
            impl = tc_aot_outputs_release_observations(outputs);
        }
        return tc_impl_error_exit(impl);
    }
    if (options->debug) {
        impl = tc_linemap_build(tc_analysis_ir(analysis), &linemap);
        if (impl != TC_IMPL_OK) {
            TcImplError stats_impl = tc_observe_stats_finish_aot(&observe_stats,
                                                                 NULL);
            if (stats_impl == TC_IMPL_OK) {
                stats_impl = tc_aot_outputs_release_observations(outputs);
            }
            return report(stats_impl != TC_IMPL_OK ? stats_impl : impl, NULL);
        }
    }
    result = run_aot(analysis, profile, options, linemap, outputs,
                     &observe_stats);
    if (observe_stats.stream.file != NULL) {
        TcImplError stats_impl = tc_observe_stats_finish_aot(&observe_stats, NULL);
        if (stats_impl != TC_IMPL_OK) {
            result = report(stats_impl, "cannot finish statistics");
        }
    }
    impl = tc_aot_outputs_release_observations(outputs);
    if (impl != TC_IMPL_OK) {
        result = report(impl, "cannot release observation outputs");
    }
    tc_linemap_destroy(linemap);
    return result;
}

int tc_aot_driver_run_with_stdio(const TcAotStdio *stdio,
                                 const TcAnalysis *analysis,
                                 const TcPlatformProfile *profile,
                                 const TcCliOptions *options,
                                 const char *const *extra_inputs,
                                 size_t nextra_inputs) {
    TcAotOutputs outputs;
    TcImplError impl;
    int result;
    if (analysis == NULL || profile == NULL || options == NULL || stdio == NULL ||
        (nextra_inputs != 0u && extra_inputs == NULL)) {
        return report(TC_IMPL_INTERNAL, "missing AOT driver argument");
    }
    impl = tc_aot_outputs_preflight(stdio, analysis, TC_ANALYSIS_OK, options,
                                    extra_inputs, nextra_inputs, NULL, NULL, 0u,
                                    1, !options->static_only, &outputs);
    if (impl != TC_IMPL_OK) {
        return impl == TC_IMPL_USAGE ? TC_EXIT_USAGE
                                    : report(impl, "cannot preflight output target");
    }
    result = tc_aot_driver_run_prepared(stdio, analysis, profile, options,
                                        extra_inputs, nextra_inputs, &outputs);
    impl = result == TC_EXIT_OK ? tc_aot_outputs_release(&outputs)
                                : tc_aot_outputs_discard(&outputs);
    return impl == TC_IMPL_OK ? result : tc_impl_error_exit(impl);
}

int tc_aot_driver_run_with_inputs(const TcAnalysis *analysis,
                                  const TcPlatformProfile *profile,
                                  const TcCliOptions *options,
                                  const char *const *extra_inputs,
                                  size_t nextra_inputs) {
    TcAotStdio stdio;
    tc_aot_stdio_capture(&stdio);
    return tc_aot_driver_run_with_stdio(&stdio, analysis, profile, options,
                                        extra_inputs, nextra_inputs);
}
