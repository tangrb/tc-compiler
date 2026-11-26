#ifndef TC_TEST_TEMP_FILE_H
#define TC_TEST_TEMP_FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "base/fd.h"

static inline FILE *tc_test_tmpfile(void) {
    char path[1024];
    int length = snprintf(path, sizeof(path), TC_TEST_TMP_ROOT "/test-XXXXXX");
    int fd;
    FILE *file;
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return NULL;
    }
    fd = tc_fd_relocate(mkstemp(path));
    if (fd < 0) {
        return NULL;
    }
    file = fdopen(fd, "w+b");
    if (file == NULL) {
        close(fd);
        unlink(path);
        return NULL;
    }
    if (unlink(path) != 0) {
        fclose(file);
        return NULL;
    }
    return file;
}

#endif
