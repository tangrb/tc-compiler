#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "driver/cli.h"
#include "project/init_internal.h"

static unsigned long call_count;
static unsigned long fail_at;
static unsigned long cleanup_call_count;
static unsigned long fail_cleanup_at;
static int failed;
static int replaced;
static int failure_errno = EIO;

static int inject(const char *operation) {
    int saved_errno;
    call_count++;
    saved_errno = errno;
    {
        const char *trace = getenv("TC_INIT_TRACE");
        if (trace != NULL) {
            int fd = open(trace, O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (fd >= 0) {
                char line[64];
                int length = snprintf(line, sizeof(line), "%lu %s\n",
                                      call_count, operation);
                if (length > 0) {
                    (void)write(fd, line, (size_t)length);
                }
                (void)close(fd);
            }
        }
    }
    errno = saved_errno;
    if (!failed && fail_at != 0 && call_count == fail_at) {
        failed = 1;
        errno = failure_errno;
        return 1;
    }
    return 0;
}

static int fault_mkdir(int parent_fd, const char *name, mode_t mode) {
    if (inject("mkdir")) {
        return -1;
    }
    return mkdirat(parent_fd, name, mode);
}

static int fault_create(int parent_fd, const char *name, mode_t mode) {
    int fd;
    if (inject("create")) {
        return -1;
    }
    fd = openat(parent_fd, name,
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd >= 0 && !replaced && getenv("TC_INIT_REPLACE_FIRST") != NULL) {
        static const char replacement[] = "replacement";
        if (unlinkat(parent_fd, name, 0) == 0) {
            int other = openat(parent_fd, name,
                               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                               0600);
            if (other >= 0) {
                (void)write(other, replacement, sizeof(replacement) - 1u);
                (void)close(other);
            }
        }
        replaced = 1;
    }
    return fd;
}

static int fault_open_dir(int parent_fd, const char *name) {
    if (inject("open_dir")) {
        return -1;
    }
    return openat(parent_fd, name,
                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static ssize_t fault_write(int fd, const void *data, size_t length) {
    if (inject("write")) {
        return -1;
    }
    return write(fd, data, length);
}

static int fault_close(int fd) {
    int result = close(fd);
    int saved_errno = errno;
    if (inject("close")) {
        return -1;
    }
    errno = saved_errno;
    return result;
}

static int fail_cleanup(const char *operation) {
    int saved_errno;
    cleanup_call_count++;
    saved_errno = errno;
    {
        const char *trace = getenv("TC_INIT_TRACE");
        if (trace != NULL) {
            int fd = open(trace, O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (fd >= 0) {
                char line[64];
                int length = snprintf(line, sizeof(line), "%lu %s\n",
                                      call_count + cleanup_call_count, operation);
                if (length > 0) {
                    (void)write(fd, line, (size_t)length);
                }
                (void)close(fd);
            }
        }
    }
    errno = saved_errno;
    if (fail_cleanup_at != 0 && cleanup_call_count == fail_cleanup_at) {
        errno = EIO;
        return 1;
    }
    return 0;
}

static int fault_remove_file(int parent_fd, const char *name) {
    if (fail_cleanup("unlink")) {
        return -1;
    }
    return unlinkat(parent_fd, name, 0);
}

static int fault_remove_dir(int parent_fd, const char *name) {
    if (fail_cleanup("rmdir")) {
        return -1;
    }
    return unlinkat(parent_fd, name, AT_REMOVEDIR);
}

int main(int argc, char **argv) {
    TcProjectInitOps ops = {
        fault_mkdir, fault_create, fault_open_dir, fault_write,
        fault_close, fault_remove_file, fault_remove_dir
    };
    TcImplError result;
    const char *name;
    const char *cleanup_value;
    if (argc >= 2 && strcmp(argv[1], "--init-project") == 0) {
        int index = 2;
        if (index < argc && strcmp(argv[index], "--") == 0) {
            index++;
        }
        if (index + 1 != argc) {
            return 2;
        }
        name = argv[index];
    } else {
        if (argc < 2 || argc > 3) {
            return 2;
        }
        name = argv[1];
        fail_at = argc == 3 ? strtoul(argv[2], NULL, 10) : 0;
    }
    cleanup_value = getenv("TC_INIT_FAIL_CLEANUP_AT");
    if (cleanup_value != NULL) {
        fail_cleanup_at = strtoul(cleanup_value, NULL, 10);
    }
    if (getenv("TC_INIT_FAIL_ENOMEM") != NULL) {
        failure_errno = ENOMEM;
    }
    result = tc_project_init_with_ops(name, stdout, stderr, &ops);
    return tc_impl_error_exit(result);
}
