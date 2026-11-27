#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "vm/gdb_journal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/fd.h"

#define JOURNAL_LOCK ".tc-gdb-stdout.lock"
#define JOURNAL_NEXT ".tc-gdb-stdout.next"
#define JOURNAL_NEXT_TMP ".tc-gdb-stdout.next.tmp"
#define JOURNAL_PROBE_TMP ".tc-gdb-stdout.probe.tmp"
#define JOURNAL_PROBE ".tc-gdb-stdout.probe.bin"
#define JOURNAL_RECORD_PREFIX "record-"
#define JOURNAL_RECORD_SUFFIX ".bin"
#define JOURNAL_TMP_SUFFIX ".tmp"
#define JOURNAL_NAME_DIGITS 20u
#define JOURNAL_PROBE_SIZE 32u

struct TcGdbJournal {
    int directory_fd;
    int lock_fd;
    uint64_t high_water;
    int failed;
};

static const uint8_t journal_probe_bytes[JOURNAL_PROBE_SIZE] = {
    0x00u, 0x01u, 0x02u, 0x03u, 0x41u, 0x42u, 0x43u, 0x7fu,
    0x80u, 0xfeu, 0xffu, 0x10u, 0x0au, 0x0du, 0x20u, 0x00u,
    0x54u, 0x43u, 0x47u, 0x44u, 0x42u, 0x4au, 0x52u, 0x4eu,
    0x55u, 0x4du, 0x42u, 0x45u, 0x52u, 0x31u, 0x32u, 0x33u
};

static TcImplError journal_error(int error_number) {
    return error_number == ENOMEM ? TC_IMPL_OOM : TC_IMPL_IO;
}

static int missing_entry(int error_number) {
    return error_number == ENOENT || error_number == ENOTDIR;
}

static int same_identity(const struct stat *left, const struct stat *right) {
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int open_private(int directory_fd, const char *name) {
    return tc_fd_openat(directory_fd, name,
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                        0600);
}

static int open_readonly(int fd, const char *name) {
    return tc_fd_openat(fd, name,
                        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
}

/* A successful link is the no-replace commit point on systems without an
 * atomic exclusive rename.  Its source is a private regular file. */
static int commit_new(int directory_fd, const char *temporary,
                      const char *final_name, int record_commit) {
#ifdef __APPLE__
    (void)record_commit;
    return renameatx_np(directory_fd, temporary, directory_fd, final_name,
                        RENAME_EXCL) == 0;
#else
    (void)record_commit;
    if (linkat(directory_fd, temporary, directory_fd, final_name, 0) != 0) {
        return 0;
    }
    (void)unlinkat(directory_fd, temporary, 0);
    return 1;
#endif
}

static int write_all(int fd, const uint8_t *bytes, size_t size) {
    size_t total = 0u;
    while (total < size) {
        ssize_t written = write(fd, bytes + total, size - total);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (written == 0) {
            return 0;
        }
        total += (size_t)written;
    }
    return 1;
}

static int read_exact(int fd, uint8_t *bytes, size_t size) {
    size_t total = 0u;
    while (total < size) {
        ssize_t got = read(fd, bytes + total, size - total);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (got == 0) {
            return 0;
        }
        total += (size_t)got;
    }
    return 1;
}

/* Compare before unlink so a replaced object is never removed by name. */
static int remove_identity(int directory_fd, const char *name,
                           const struct stat *identity) {
    struct stat current;
    if (fstatat(directory_fd, name, &current, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(current.st_mode) || !same_identity(identity, &current)) {
        return 0;
    }
    return unlinkat(directory_fd, name, 0) == 0;
}

static void format_number(uint64_t number, char text[JOURNAL_NAME_DIGITS + 1u]) {
    int position = (int)JOURNAL_NAME_DIGITS - 1;
    text[JOURNAL_NAME_DIGITS] = '\0';
    while (position >= 0) {
        text[position] = (char)('0' + (unsigned)(number % 10u));
        number /= 10u;
        position--;
    }
}

static int format_record_name(uint64_t number, char *name, size_t capacity,
                              const char *suffix) {
    static const char prefix[] = JOURNAL_RECORD_PREFIX;
    size_t suffix_size = strlen(suffix);
    int length;
    if (number == 0u || capacity < sizeof(prefix) - 1u + JOURNAL_NAME_DIGITS +
                               suffix_size + 1u) {
        return 0;
    }
    memcpy(name, prefix, sizeof(prefix) - 1u);
    format_number(number, name + sizeof(prefix) - 1u);
    memcpy(name + sizeof(prefix) - 1u + JOURNAL_NAME_DIGITS, suffix,
           suffix_size + 1u);
    length = (int)(sizeof(prefix) - 1u + JOURNAL_NAME_DIGITS + suffix_size);
    return length > 0;
}

/* Return 1 for a matching exact name, 0 otherwise, and -1 for overflow. */
static int parse_record_name(const char *name, size_t length,
                             const char *suffix, uint64_t *number) {
    size_t suffix_size = strlen(suffix);
    size_t index;
    uint64_t value = 0u;
    if (length != sizeof(JOURNAL_RECORD_PREFIX) - 1u + JOURNAL_NAME_DIGITS +
                      suffix_size ||
        memcmp(name, JOURNAL_RECORD_PREFIX,
               sizeof(JOURNAL_RECORD_PREFIX) - 1u) != 0 ||
        memcmp(name + sizeof(JOURNAL_RECORD_PREFIX) - 1u + JOURNAL_NAME_DIGITS,
               suffix, suffix_size) != 0) {
        return 0;
    }
    for (index = sizeof(JOURNAL_RECORD_PREFIX) - 1u;
         index < sizeof(JOURNAL_RECORD_PREFIX) - 1u + JOURNAL_NAME_DIGITS;
         index++) {
        unsigned char ch = (unsigned char)name[index];
        unsigned digit;
        if (ch < (unsigned char)'0' || ch > (unsigned char)'9') {
            return 0;
        }
        digit = (unsigned)(ch - (unsigned char)'0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return -1;
        }
        value = value * 10u + digit;
    }
    if (value == 0u) {
        return -1;
    }
    *number = value;
    return 1;
}

static TcImplError open_directory(const char *path, int *out_fd) {
    char *copy;
    char *cursor;
    int current = AT_FDCWD;
    int result = TC_IMPL_OK;
    if (path == NULL || path[0] == '\0') {
        return TC_IMPL_USAGE;
    }
    copy = malloc(strlen(path) + 1u);
    if (copy == NULL) {
        return TC_IMPL_OOM;
    }
    (void)strcpy(copy, path);
    cursor = copy;
    if (*cursor == '/') {
        current = tc_fd_open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (current < 0) {
            free(copy);
            return journal_error(errno);
        }
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            *out_fd = current;
            free(copy);
            return TC_IMPL_OK;
        }
    }
    while (*cursor != '\0') {
        char *component = cursor;
        char *next;
        int flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
        int fd;
        while (*cursor != '/' && *cursor != '\0') {
            cursor++;
        }
        next = cursor;
        while (*next == '/') {
            next++;
        }
        if (*next == '\0') {
            flags |= O_NOFOLLOW;
        }
        if (*cursor == '/') {
            *cursor = '\0';
        }
        fd = tc_fd_openat(current, component, flags);
        if (fd < 0) {
            int error_number = errno;
            if (error_number == ENOENT || error_number == ENOTDIR ||
                error_number == ELOOP || error_number == ENAMETOOLONG ||
                error_number == EINVAL) {
                result = TC_IMPL_USAGE;
            } else {
                result = journal_error(error_number);
            }
        } else if (*next == '\0') {
            if (current != AT_FDCWD) {
                (void)close(current);
            }
            *out_fd = fd;
            free(copy);
            return TC_IMPL_OK;
        }
        if (current != AT_FDCWD) {
            (void)close(current);
        }
        current = fd;
        if (result != TC_IMPL_OK) {
            break;
        }
        cursor = next;
    }
    if (result == TC_IMPL_OK) {
        if (strcmp(path, "/") == 0) {
            *out_fd = current;
            current = -1;
        } else if (current < 0) {
            result = TC_IMPL_USAGE;
        }
    }
    if (current >= 0 && current != AT_FDCWD) {
        (void)close(current);
    }
    free(copy);
    return result;
}

static TcImplError lock_directory(int directory_fd, int *lock_fd) {
    struct stat st;
    int fd = tc_fd_openat(directory_fd, JOURNAL_LOCK,
                          O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                          0600);
    if (fd < 0) {
        return journal_error(errno);
    }
    if (fstat(fd, &st) != 0) {
        TcImplError error = journal_error(errno);
        (void)close(fd);
        return error;
    }
    if (!S_ISREG(st.st_mode)) {
        (void)close(fd);
        return TC_IMPL_IO;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        TcImplError error = journal_error(errno);
        (void)close(fd);
        return error;
    }
    *lock_fd = fd;
    return TC_IMPL_OK;
}

static TcImplError remove_temporary(int directory_fd, const char *name) {
    int fd = open_readonly(directory_fd, name);
    struct stat st;
    TcImplError error;
    if (fd < 0) {
        if (missing_entry(errno)) {
            return TC_IMPL_OK;
        }
        return journal_error(errno);
    }
    if (fstat(fd, &st) != 0) {
        error = journal_error(errno);
    } else if (!S_ISREG(st.st_mode)) {
        error = TC_IMPL_IO;
    } else {
        error = remove_identity(directory_fd, name, &st) ? TC_IMPL_OK
                                                         : TC_IMPL_IO;
    }
    (void)close(fd);
    return error;
}

static TcImplError recover_temporaries(int directory_fd) {
    static const char *const fixed[] = {
        JOURNAL_NEXT_TMP, JOURNAL_PROBE_TMP, JOURNAL_PROBE
    };
    size_t index;
    int scan_fd = fcntl(directory_fd, F_DUPFD_CLOEXEC, 3);
    DIR *directory;
    struct dirent *entry;
    TcImplError error = TC_IMPL_OK;
    if (scan_fd < 0) {
        return journal_error(errno);
    }
    directory = fdopendir(scan_fd);
    if (directory == NULL) {
        error = journal_error(errno);
        (void)close(scan_fd);
        return error;
    }
    for (;;) {
        uint64_t number;
        char name[sizeof(JOURNAL_RECORD_PREFIX) + JOURNAL_NAME_DIGITS +
                  sizeof(JOURNAL_TMP_SUFFIX)];
        int matched;
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                error = journal_error(errno);
            }
            break;
        }
        matched = parse_record_name(entry->d_name, strlen(entry->d_name),
                                    JOURNAL_TMP_SUFFIX, &number);
        if (matched < 0) {
            error = TC_IMPL_IO;
            break;
        }
        if (matched == 0 ||
            !format_record_name(number, name, sizeof(name), JOURNAL_TMP_SUFFIX)) {
            continue;
        }
        error = remove_temporary(directory_fd, name);
        if (error != TC_IMPL_OK) {
            break;
        }
    }
    if (closedir(directory) != 0 && error == TC_IMPL_OK) {
        error = journal_error(errno);
    }
    for (index = 0u; index < sizeof(fixed) / sizeof(fixed[0]); index++) {
        if (error == TC_IMPL_OK) {
            error = remove_temporary(directory_fd, fixed[index]);
        }
    }
    return error;
}

static TcImplError scan_records(int directory_fd, uint64_t *maximum) {
    int scan_fd = fcntl(directory_fd, F_DUPFD_CLOEXEC, 3);
    DIR *directory;
    struct dirent *entry;
    TcImplError error = TC_IMPL_OK;
    uint64_t largest = 0u;
    if (scan_fd < 0) {
        return journal_error(errno);
    }
    directory = fdopendir(scan_fd);
    if (directory == NULL) {
        error = journal_error(errno);
        (void)close(scan_fd);
        return error;
    }
    rewinddir(directory);
    for (;;) {
        uint64_t number;
        int matched;
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                error = journal_error(errno);
            }
            break;
        }
        matched = parse_record_name(entry->d_name, strlen(entry->d_name),
                                    JOURNAL_RECORD_SUFFIX, &number);
        if (matched < 0) {
            error = TC_IMPL_IO;
            break;
        }
        if (matched == 1) {
            struct stat st;
            if (fstatat(directory_fd, entry->d_name, &st,
                        AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) {
                error = TC_IMPL_IO;
                break;
            }
        }
        if (matched == 1 && number > largest) {
            largest = number;
        }
    }
    if (closedir(directory) != 0 && error == TC_IMPL_OK) {
        error = journal_error(errno);
    }
    *maximum = largest;
    return error;
}

static TcImplError read_high_water(int directory_fd, uint64_t *high_water,
                                   int *present) {
    int fd = open_readonly(directory_fd, JOURNAL_NEXT);
    struct stat st;    uint8_t bytes[JOURNAL_NAME_DIGITS + 1u];
    ssize_t got;
    size_t index;
    uint64_t value = 0u;
    TcImplError error = TC_IMPL_OK;
    *present = 0;
    if (fd < 0) {
        if (errno == ENOENT) {
            return TC_IMPL_OK;
        }
        return journal_error(errno);
    }
    if (fstat(fd, &st) != 0) {
        error = journal_error(errno);
    } else if (!S_ISREG(st.st_mode) || st.st_size != (off_t)JOURNAL_NAME_DIGITS) {
        error = TC_IMPL_IO;
    } else if (!read_exact(fd, bytes, JOURNAL_NAME_DIGITS)) {
        error = journal_error(errno == 0 ? EIO : errno);
    } else {
        got = read(fd, bytes + JOURNAL_NAME_DIGITS, 1u);
        if (got < 0) {
            error = journal_error(errno);
        } else if (got != 0) {
            error = TC_IMPL_IO;
        }
    }
    if (error == TC_IMPL_OK) {
        for (index = 0u; index < JOURNAL_NAME_DIGITS; index++) {
            unsigned char ch = bytes[index];
            unsigned digit;
            if (ch < (unsigned char)'0' || ch > (unsigned char)'9') {
                error = TC_IMPL_IO;
                break;
            }
            digit = (unsigned)(ch - (unsigned char)'0');
            if (value > (UINT64_MAX - digit) / 10u) {
                error = TC_IMPL_IO;
                break;
            }
            value = value * 10u + digit;
        }
    }
    (void)close(fd);
    if (error == TC_IMPL_OK) {
        *high_water = value;
        *present = 1;
    }
    return error;
}

static TcImplError create_high_water(int directory_fd, uint64_t high_water,
                                     int exclusive) {
    char text[JOURNAL_NAME_DIGITS + 1u];
    char temporary[] = JOURNAL_NEXT_TMP;
    int fd = open_private(directory_fd, temporary);
    struct stat identity;
    int has_identity = 0;
    TcImplError error = TC_IMPL_OK;
    if (fd < 0) {
        return journal_error(errno);
    }
    if (fstat(fd, &identity) != 0) {
        error = journal_error(errno);
    } else {
        has_identity = 1;
        if (!S_ISREG(identity.st_mode)) {
            error = TC_IMPL_IO;
        }
    }
    if (error == TC_IMPL_OK) {
        format_number(high_water, text);
        if (!write_all(fd, (const uint8_t *)text, JOURNAL_NAME_DIGITS)) {
            error = journal_error(errno == 0 ? EIO : errno);
        }
    }
    if (close(fd) != 0 && error == TC_IMPL_OK) {
        error = journal_error(errno);
    }
    if (error == TC_IMPL_OK) {
        int committed = exclusive ? commit_new(directory_fd, temporary,
                                               JOURNAL_NEXT, 0)
                                  : renameat(directory_fd, temporary,
                                             directory_fd, JOURNAL_NEXT) == 0;
        if (!committed) {
            error = journal_error(errno);
        }
    }
    if (error != TC_IMPL_OK && has_identity) {
        if (!remove_identity(directory_fd, temporary, &identity)) {
            error = TC_IMPL_IO;
        }
    }
    return error;
}

static TcImplError restore_high_water(int directory_fd, uint64_t *high_water) {
    uint64_t maximum;
    uint64_t stored = 0u;
    int present = 0;
    TcImplError error = scan_records(directory_fd, &maximum);
    if (error != TC_IMPL_OK) {
        return error;
    }
    error = read_high_water(directory_fd, &stored, &present);
    if (error != TC_IMPL_OK) {
        return error;
    }
    if (present) {
        if (stored < maximum) {
            return TC_IMPL_IO;
        }
        *high_water = stored;
        return TC_IMPL_OK;
    }
    *high_water = maximum;
    return create_high_water(directory_fd, maximum, 1);
}

static TcImplError run_probe(int directory_fd) {
    char temporary[] = JOURNAL_PROBE_TMP;
    char final_name[] = JOURNAL_PROBE;
    uint8_t bytes[JOURNAL_PROBE_SIZE];
    struct stat identity;
    int has_identity = 0;
    int fd = open_private(directory_fd, temporary);
    TcImplError error = TC_IMPL_OK;
    if (fd < 0) {
        return journal_error(errno);
    }
    if (fstat(fd, &identity) != 0) {
        error = journal_error(errno);
    } else {
        has_identity = 1;
        if (!S_ISREG(identity.st_mode)) {
            error = TC_IMPL_IO;
        } else if (!write_all(fd, journal_probe_bytes, sizeof(journal_probe_bytes))) {
            error = journal_error(errno == 0 ? EIO : errno);
        }
    }
    if (close(fd) != 0 && error == TC_IMPL_OK) {
        error = journal_error(errno);
    }
    if (error == TC_IMPL_OK &&
        !commit_new(directory_fd, temporary, final_name, 0)) {
        error = journal_error(errno);
    }
    if (error == TC_IMPL_OK) {
        fd = open_readonly(directory_fd, final_name);
        if (fd < 0) {
            error = journal_error(errno);
        } else {
            struct stat current;
            if (fstat(fd, &current) != 0) {
                error = journal_error(errno);
            } else if (!S_ISREG(current.st_mode) ||
                       !same_identity(&identity, &current) ||
                       current.st_size != (off_t)sizeof(bytes) ||
                       !read_exact(fd, bytes, sizeof(bytes)) ||
                       memcmp(bytes, journal_probe_bytes, sizeof(bytes)) != 0) {
                error = TC_IMPL_IO;
            }
            if (close(fd) != 0 && error == TC_IMPL_OK) {
                error = journal_error(errno);
            }
            if (error == TC_IMPL_OK &&
                !remove_identity(directory_fd, final_name, &identity)) {
                error = TC_IMPL_IO;
            }
        }
    }
    if (error != TC_IMPL_OK && has_identity) {
        (void)remove_identity(directory_fd, temporary, &identity);
    }
    return error;
}

static int check_current_high_water(TcGdbJournal *journal) {
    uint64_t stored = 0u;
    int present = 0;
    return read_high_water(journal->directory_fd, &stored, &present) == TC_IMPL_OK &&
           present && stored == journal->high_water;
}

TcImplError tc_gdb_journal_open(TcGdbJournal **out, const char *directory) {
    TcGdbJournal *journal;
    TcImplError error;
    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    *out = NULL;
    if (directory == NULL || directory[0] == '\0') {
        return TC_IMPL_USAGE;
    }
    journal = calloc(1u, sizeof(*journal));
    if (journal == NULL) {
        return TC_IMPL_OOM;
    }
    journal->directory_fd = -1;
    journal->lock_fd = -1;
    error = open_directory(directory, &journal->directory_fd);
    if (error == TC_IMPL_OK) {
        error = lock_directory(journal->directory_fd, &journal->lock_fd);
    }
    if (error == TC_IMPL_OK) {
        error = recover_temporaries(journal->directory_fd);
    }
    if (error == TC_IMPL_OK) {
        error = restore_high_water(journal->directory_fd,
                                   &journal->high_water);
    }
    if (error == TC_IMPL_OK) {
        error = run_probe(journal->directory_fd);
    }
    if (error != TC_IMPL_OK) {
        if (journal->lock_fd >= 0) {
            (void)close(journal->lock_fd);
        }
        if (journal->directory_fd >= 0) {
            (void)close(journal->directory_fd);
        }
        free(journal);
        return error;
    }
    *out = journal;
    return TC_IMPL_OK;
}

void tc_gdb_journal_close(TcGdbJournal *journal) {
    if (journal == NULL) {
        return;
    }
    if (journal->lock_fd >= 0) {
        (void)close(journal->lock_fd);
    }
    if (journal->directory_fd >= 0) {
        (void)close(journal->directory_fd);
    }
    free(journal);
}

int tc_gdb_journal_write(TcGdbJournal *journal, const uint8_t *bytes, size_t size) {
    char final_name[sizeof(JOURNAL_RECORD_PREFIX) + JOURNAL_NAME_DIGITS +
                    sizeof(JOURNAL_RECORD_SUFFIX)];
    char temporary[sizeof(JOURNAL_RECORD_PREFIX) + JOURNAL_NAME_DIGITS +
                   sizeof(JOURNAL_TMP_SUFFIX)];
    struct stat final_identity;
    struct stat identity;
    uint64_t number;
    int fd;
    if (journal == NULL || journal->failed) {
        return -1;
    }
    if (journal->high_water == UINT64_MAX) {
        journal->failed = 1;
        return -1;
    }
    if ((bytes == NULL && size != 0u) ||
        !format_record_name(journal->high_water + 1u, final_name,
                            sizeof(final_name), JOURNAL_RECORD_SUFFIX) ||
        !format_record_name(journal->high_water + 1u, temporary,
                            sizeof(temporary), JOURNAL_TMP_SUFFIX)) {
        journal->failed = 1;
        return -1;
    }
    if (!check_current_high_water(journal)) {
        journal->failed = 1;
        return -1;
    }
    if (fstatat(journal->directory_fd, final_name, &final_identity,
                AT_SYMLINK_NOFOLLOW) == 0) {
        journal->failed = 1;
        return -1;
    }
    if (errno != ENOENT) {
        journal->failed = 1;
        return -1;
    }
    number = journal->high_water + 1u;
    if (create_high_water(journal->directory_fd, number, 0) != TC_IMPL_OK) {
        journal->failed = 1;
        return -1;
    }
    journal->high_water = number;
    fd = open_private(journal->directory_fd, temporary);
    if (fd < 0) {
        journal->failed = 1;
        return -1;
    }
    memset(&identity, 0, sizeof(identity));
    if (fstat(fd, &identity) != 0 || !S_ISREG(identity.st_mode) ||
        !write_all(fd, bytes, size)) {
        journal->failed = 1;
    }
    if (close(fd) != 0) {
        journal->failed = 1;
    }
    if (journal->failed) {
        (void)remove_identity(journal->directory_fd, temporary, &identity);
        return -1;
    }
    if (!commit_new(journal->directory_fd, temporary, final_name, 1)) {
        journal->failed = 1;
        (void)remove_identity(journal->directory_fd, temporary, &identity);
        return -1;
    }
    return 0;
}

uint64_t tc_gdb_journal_high_water(const TcGdbJournal *journal) {
    return journal != NULL ? journal->high_water : 0u;
}

int tc_gdb_journal_failed(const TcGdbJournal *journal) {
    return journal != NULL ? journal->failed : 1;
}
