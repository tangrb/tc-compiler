#ifndef TC_BASE_FD_H
#define TC_BASE_FD_H

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

/* Internal descriptors must not reuse a closed standard descriptor. */
static inline int tc_fd_relocate(int fd) {
    int moved;
    int error_number;
    if (fd < 0 || fd > STDERR_FILENO) {
        return fd;
    }
    moved = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    error_number = errno;
    (void)close(fd);
    errno = error_number;
    return moved;
}

static inline int tc_fd_open(const char *path, int flags, ...) {
    va_list args;
    mode_t mode = 0u;
    if ((flags & O_CREAT) != 0) {
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    return tc_fd_relocate(open(path, flags, mode));
}

static inline int tc_fd_openat(int directory, const char *path, int flags,
                               ...) {
    va_list args;
    mode_t mode = 0u;
    if ((flags & O_CREAT) != 0) {
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    return tc_fd_relocate(openat(directory, path, flags, mode));
}

#endif
