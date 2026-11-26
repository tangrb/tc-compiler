#define _GNU_SOURCE

#include "base/fd.h"
#include "frontend/input.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static TcImplError fd_error(int error_number) {
    return error_number == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO;
}

static TcImplError fd_fail(int fd, TcImplError error) {
    (void)close(fd);
    return error;
}

TcImplError tc_source_read_fd(int fd, TcSourceBytes *out) {
    struct stat before;
    struct stat after;
    uint8_t *bytes;
    size_t expected;
    size_t total = 0u;
    int failed = 0;
    int failure_errno = 0;
    TcImplError result = TC_IMPL_OK;
    if (out == NULL || fd < 0) {
        return TC_IMPL_IO;
    }
    out->bytes = NULL;
    out->size = 0u;
    if (fstat(fd, &before) != 0) {
        return fd_fail(fd, fd_error(errno));
    }
    if (!S_ISREG(before.st_mode)) {
        return fd_fail(fd, TC_IMPL_IO);
    }
    if (before.st_size < 0) {
        return fd_fail(fd, TC_IMPL_IO);
    }
    if ((uintmax_t)before.st_size > (uintmax_t)TC_FRONTEND_MAX_SOURCE_BYTES) {
        return fd_fail(fd, TC_IMPL_INTERNAL);
    }
    expected = (size_t)before.st_size;
    bytes = (uint8_t *)tc_allocator_libc()->alloc(
        tc_allocator_libc()->ctx, expected + 1u);
    if (bytes == NULL) {
        return fd_fail(fd, TC_IMPL_OOM);
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
        result = TC_IMPL_INTERNAL;
    } else if (total < expected) {
        failed = 1;
    }
    if (close(fd) != 0 && !failed && result == TC_IMPL_OK) {
        failed = 1;
        failure_errno = errno;
    }
    if (failed || result != TC_IMPL_OK) {
        tc_allocator_libc()->free(tc_allocator_libc()->ctx, bytes);
        return result != TC_IMPL_OK ? result : fd_error(failure_errno);
    }
    bytes[expected] = '\0';
    out->bytes = bytes;
    out->size = expected;
    return TC_IMPL_OK;
}

TcImplError tc_source_read(const char *path, TcSourceBytes *out) {
    int fd;
    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    out->bytes = NULL;
    out->size = 0u;
    if (path == NULL) {
        return TC_IMPL_IO;
    }
    fd = tc_fd_open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
    if (fd < 0) {
        return fd_error(errno);
    }
    return tc_source_read_fd(fd, out);
}

void tc_source_free(TcSourceBytes *source) {
    if (source == NULL || source->bytes == NULL) {
        return;
    }
    tc_allocator_libc()->free(tc_allocator_libc()->ctx, source->bytes);
    source->bytes = NULL;
    source->size = 0u;
}

const char *tc_source_limit_violation(size_t size) {
    if (size > TC_FRONTEND_MAX_SOURCE_BYTES) {
        return "source file exceeds the 4 MiB limit";
    }
    return NULL;
}
