#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vm/gdb_journal.h"

static int failures;
static size_t checks;

#define CHECK(condition)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
            failures++;                                                        \
        }                                                                      \
    } while (0)

typedef struct {
    char path[PATH_MAX];
} Fixture;

static int join_path(char *out, size_t size, const char *base,
                     const char *name) {
    int length = snprintf(out, size, "%s/%s", base, name);
    return length >= 0 && (size_t)length < size;
}

static int remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0;
    }
    {
        DIR *directory = opendir(path);
        struct dirent *entry;
        int ok = directory != NULL;
        while (ok) {
            char child[PATH_MAX];
            errno = 0;
            entry = readdir(directory);
            if (entry == NULL) {
                if (errno != 0) {
                    ok = 0;
                }
                break;
            }
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            ok = join_path(child, sizeof(child), path, entry->d_name) &&
                 remove_tree(child);
        }
        if (directory != NULL) {
            (void)closedir(directory);
        }
        if (!ok) {
            return 0;
        }
    }
    return rmdir(path) == 0;
}

static int fixture_init(Fixture *fixture) {
    static const char root[] = TC_TEST_TMP_ROOT "/tc-gdb-journal-XXXXXX";
    (void)snprintf(fixture->path, sizeof(fixture->path), "%s", root);
    return mkdtemp(fixture->path) != NULL;
}

static int write_file(const char *path, const void *bytes, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    size_t total = 0u;
    int ok = fd >= 0;
    while (ok && total < size) {
        ssize_t written = write(fd, (const uint8_t *)bytes + total,
                                size - total);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = 0;
        } else if (written == 0) {
            ok = 0;
        } else {
            total += (size_t)written;
        }
    }
    if (fd >= 0 && close(fd) != 0) {
        ok = 0;
    }
    return ok;
}

static int write_text(const char *path, const char *text) {
    return write_file(path, text, strlen(text));
}

static int read_text(const char *path, char *text, size_t capacity) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t got;
    if (fd < 0) {
        return 0;
    }
    got = read(fd, text, capacity - 1u);
    if (got < 0) {
        (void)close(fd);
        return 0;
    }
    text[got] = '\0';
    return close(fd) == 0;
}

static int read_bytes(const char *path, uint8_t *bytes, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    size_t total = 0u;
    int ok = fd >= 0;
    while (ok && total < size) {
        ssize_t got = read(fd, bytes + total, size - total);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = 0;
        } else if (got == 0) {
            ok = 0;
        } else {
            total += (size_t)got;
        }
    }
    if (fd >= 0 && close(fd) != 0) {
        ok = 0;
    }
    return ok;
}

static int exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

static void check_records_and_reopen(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    char high_water[32];
    uint8_t bytes[512];
    static const uint8_t first[] = {'A', 0x00u, 0xffu, '\n'};
    static const uint8_t second[] = {'B', 'C'};
    CHECK(fixture_init(&fixture));
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(journal != NULL);
    CHECK(tc_gdb_journal_high_water(journal) == 0u);
    CHECK(!tc_gdb_journal_failed(journal));
    {
        TcGdbJournal *competitor = NULL;
        CHECK(tc_gdb_journal_open(&competitor, fixture.path) == TC_IMPL_IO);
        CHECK(competitor == NULL);
    }
    tc_gdb_journal_close(journal);
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(tc_gdb_journal_high_water(journal) == 0u);
    CHECK(tc_gdb_journal_write(journal, first, sizeof(first)) == 0);
    CHECK(tc_gdb_journal_high_water(journal) == 1u);
    CHECK(tc_gdb_journal_write(journal, NULL, 0u) == 0);
    CHECK(tc_gdb_journal_high_water(journal) == 2u);
    tc_gdb_journal_close(journal);
    journal = NULL;

    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next"));
    CHECK(read_text(path, high_water, sizeof(high_water)));
    CHECK(strcmp(high_water,
                 "00000000000000000002") == 0);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000001.bin"));
    CHECK(read_bytes(path, bytes, sizeof(first)));
    CHECK(memcmp(bytes, first, sizeof(first)) == 0);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000002.bin"));
    {
        struct stat st;
        CHECK(lstat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0);
    }
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.probe.tmp"));
    CHECK(!exists(path));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.probe.bin"));
    CHECK(!exists(path));

    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(tc_gdb_journal_high_water(journal) == 2u);
    CHECK(tc_gdb_journal_write(journal, second, sizeof(second)) == 0);
    CHECK(tc_gdb_journal_high_water(journal) == 3u);
    tc_gdb_journal_close(journal);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000003.bin"));
    CHECK(read_bytes(path, bytes, sizeof(second)));
    CHECK(memcmp(bytes, second, sizeof(second)) == 0);
    CHECK(remove_tree(fixture.path));
}

static void check_lock_competition(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    int ready[2];
    int release[2];
    pid_t child;
    int status;
    char token = '0';
    CHECK(fixture_init(&fixture));
    CHECK(pipe(ready) == 0 && pipe(release) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        TcGdbJournal *held = NULL;
        char value = '0';
        (void)close(ready[0]);
        (void)close(release[1]);
        if (tc_gdb_journal_open(&held, fixture.path) == TC_IMPL_OK) {
            value = '1';
        }
        (void)write(ready[1], &value, 1u);
        (void)read(release[0], &value, 1u);
        tc_gdb_journal_close(held);
        _exit(0);
    }
    CHECK(child > 0);
    if (child > 0) {
        (void)close(ready[1]);
        (void)close(release[0]);
        CHECK(read(ready[0], &token, 1u) == 1 && token == '1');
        CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
        CHECK(journal == NULL);
        token = '1';
        CHECK(write(release[1], &token, 1u) == 1);
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        (void)close(ready[0]);
        (void)close(release[1]);
    }
    tc_gdb_journal_close(journal);
    CHECK(remove_tree(fixture.path));
}

static void check_high_water_validation(void) {
    static const char *const invalid[] = {
        "0000000000000000001",
        "000000000000000000001",
        "0000000000000000000x",
        "18446744073709551616"
    };
    size_t index;
    for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
        Fixture fixture;
        TcGdbJournal *journal = NULL;
        char path[PATH_MAX];
        CHECK(fixture_init(&fixture));
        CHECK(join_path(path, sizeof(path), fixture.path,
                        ".tc-gdb-stdout.next"));
        CHECK(write_text(path, invalid[index]));
        CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
        CHECK(journal == NULL);
        CHECK(remove_tree(fixture.path));
    }
    {
        Fixture fixture;
        TcGdbJournal *journal = NULL;
        char path[PATH_MAX];
        CHECK(fixture_init(&fixture));
        CHECK(join_path(path, sizeof(path), fixture.path,
                        "record-00000000000000000007.bin"));
        CHECK(write_text(path, "record"));
        CHECK(join_path(path, sizeof(path), fixture.path,
                        ".tc-gdb-stdout.next"));
        CHECK(write_text(path, "00000000000000000003"));
        CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
        CHECK(journal == NULL);
        CHECK(remove_tree(fixture.path));
    }
    {
        Fixture fixture;
        TcGdbJournal *journal = NULL;
        char path[PATH_MAX];
        CHECK(fixture_init(&fixture));
        CHECK(join_path(path, sizeof(path), fixture.path,
                        "record-00000000000000000000.bin"));
        CHECK(write_text(path, "zero"));
        CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
        CHECK(journal == NULL);
        CHECK(remove_tree(fixture.path));
    }
    {
        Fixture fixture;
        TcGdbJournal *journal = NULL;
        char path[PATH_MAX];
        CHECK(fixture_init(&fixture));
        CHECK(join_path(path, sizeof(path), fixture.path,
                        "record-00000000000000000007.bin"));
        CHECK(symlink("target", path) == 0);
        CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
        CHECK(journal == NULL);
        CHECK(remove_tree(fixture.path));
    }
    {
        Fixture fixture;
        TcGdbJournal *journal = NULL;
        char path[PATH_MAX];
        CHECK(fixture_init(&fixture));
        CHECK(join_path(path, sizeof(path), fixture.path,
                        "record-00000000000000000009.bin"));
        CHECK(write_text(path, "old"));
        CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
        CHECK(tc_gdb_journal_high_water(journal) == 9u);
        CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"x", 1u) == 0);
        CHECK(tc_gdb_journal_high_water(journal) == 10u);
        tc_gdb_journal_close(journal);
        CHECK(remove_tree(fixture.path));
    }
}

static void check_conflict_and_permanent_failure(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    CHECK(fixture_init(&fixture));
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000001.bin"));
    CHECK(write_text(path, "conflict"));
    CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"x", 1u) == -1);
    CHECK(tc_gdb_journal_failed(journal));
    CHECK(tc_gdb_journal_high_water(journal) == 0u);
    CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"y", 1u) == -1);
    CHECK(tc_gdb_journal_high_water(journal) == 0u);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next"));
    {
        char text[32];
        CHECK(read_text(path, text, sizeof(text)));
        CHECK(strcmp(text, "00000000000000000000") == 0);
    }
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000001.bin"));
    {
        char text[16];
        CHECK(read_text(path, text, sizeof(text)));
        CHECK(strcmp(text, "conflict") == 0);
    }
    tc_gdb_journal_close(journal);
    CHECK(remove_tree(fixture.path));
}

static void check_reservation_after_record_failure(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    char text[32];
    CHECK(fixture_init(&fixture));
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000001.tmp"));
    CHECK(write_text(path, "foreign temporary"));
    CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"x", 1u) == -1);
    CHECK(tc_gdb_journal_failed(journal));
    CHECK(tc_gdb_journal_high_water(journal) == 1u);
    CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"y", 1u) == -1);
    CHECK(tc_gdb_journal_high_water(journal) == 1u);
    CHECK(read_text(path, text, sizeof(text)));
    CHECK(strcmp(text, "foreign temporary") == 0);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next"));
    CHECK(read_text(path, text, sizeof(text)));
    CHECK(strcmp(text, "00000000000000000001") == 0);
    tc_gdb_journal_close(journal);

    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(tc_gdb_journal_high_water(journal) == 1u);
    CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"z", 1u) == 0);
    CHECK(tc_gdb_journal_high_water(journal) == 2u);
    tc_gdb_journal_close(journal);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000002.bin"));
    CHECK(read_text(path, text, sizeof(text)));
    CHECK(strcmp(text, "z") == 0);
    CHECK(remove_tree(fixture.path));
}

static void check_changed_high_water(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    CHECK(fixture_init(&fixture));
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next"));
    CHECK(unlink(path) == 0);
    CHECK(write_text(path, "not-a-number"));
    CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"x", 1u) == -1);
    CHECK(tc_gdb_journal_failed(journal));
    CHECK(tc_gdb_journal_high_water(journal) == 0u);
    tc_gdb_journal_close(journal);
    CHECK(remove_tree(fixture.path));
}

static void check_recovery(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    CHECK(fixture_init(&fixture));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next.tmp"));
    CHECK(write_text(path, "stale high water"));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.probe.tmp"));
    CHECK(write_text(path, "stale probe temporary"));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.probe.bin"));
    CHECK(write_text(path, "stale probe"));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000012.tmp"));
    CHECK(write_text(path, "stale record"));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-not-reserved.tmp"));
    CHECK(write_text(path, "unrelated"));
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(tc_gdb_journal_high_water(journal) == 0u);
    tc_gdb_journal_close(journal);
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next.tmp"));
    CHECK(!exists(path));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.probe.tmp"));
    CHECK(!exists(path));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.probe.bin"));
    CHECK(!exists(path));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-00000000000000000012.tmp"));
    CHECK(!exists(path));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    "record-not-reserved.tmp"));
    CHECK(exists(path));
    CHECK(remove_tree(fixture.path));
}

static void check_abnormal_recovery(void) {
    static const char *const names[] = {
        ".tc-gdb-stdout.next.tmp",
        ".tc-gdb-stdout.probe.tmp",
        ".tc-gdb-stdout.probe.bin",
        "record-00000000000000000001.tmp"
    };
    size_t index;
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        Fixture fixture;
        TcGdbJournal *journal = NULL;
        char path[PATH_MAX];
        CHECK(fixture_init(&fixture));
        CHECK(join_path(path, sizeof(path), fixture.path, names[index]));
        struct stat st;
        CHECK(symlink("target", path) == 0);
        CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
        CHECK(journal == NULL);
        CHECK(lstat(path, &st) == 0 && S_ISLNK(st.st_mode));
        CHECK(remove_tree(fixture.path));
    }
}

static void check_directory_validation(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    char intermediate[PATH_MAX];
    char target[PATH_MAX];
    CHECK(tc_gdb_journal_open(&journal, NULL) == TC_IMPL_USAGE);
    CHECK(tc_gdb_journal_open(&journal, "") == TC_IMPL_USAGE);
    CHECK(journal == NULL);
    CHECK(fixture_init(&fixture));
    CHECK(join_path(path, sizeof(path), fixture.path, "file"));
    CHECK(write_text(path, "not a directory"));
    CHECK(tc_gdb_journal_open(&journal, path) == TC_IMPL_USAGE);
    CHECK(journal == NULL);
    CHECK(join_path(path, sizeof(path), fixture.path, "link"));
    CHECK(symlink(fixture.path, path) == 0);
    CHECK(tc_gdb_journal_open(&journal, path) == TC_IMPL_USAGE);
    CHECK(journal == NULL);
    CHECK(join_path(target, sizeof(target), fixture.path, "target"));
    CHECK(mkdir(target, 0700) == 0);
    CHECK(join_path(intermediate, sizeof(intermediate), fixture.path,
                    "intermediate"));
    CHECK(symlink("target", intermediate) == 0);
    CHECK(join_path(path, sizeof(path), intermediate, "journal"));
    CHECK(mkdir(path, 0700) == 0);
    CHECK(tc_gdb_journal_open(&journal, path) == TC_IMPL_OK);
    tc_gdb_journal_close(journal);
    CHECK(remove_tree(fixture.path));
}

static void check_nonregular_entries(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    CHECK(fixture_init(&fixture));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.lock"));
    CHECK(mkfifo(path, 0600) == 0);
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
    CHECK(journal == NULL);
    CHECK(remove_tree(fixture.path));

    CHECK(fixture_init(&fixture));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next"));
    CHECK(mkfifo(path, 0600) == 0);
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
    CHECK(journal == NULL);
    CHECK(remove_tree(fixture.path));

    CHECK(fixture_init(&fixture));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next"));
    CHECK(symlink("target", path) == 0);
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
    CHECK(journal == NULL);
    CHECK(remove_tree(fixture.path));

    CHECK(fixture_init(&fixture));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.lock"));
    CHECK(symlink("target", path) == 0);
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_IO);
    CHECK(journal == NULL);
    CHECK(remove_tree(fixture.path));
}

static void check_exhaustion(void) {
    Fixture fixture;
    TcGdbJournal *journal = NULL;
    char path[PATH_MAX];
    CHECK(fixture_init(&fixture));
    CHECK(join_path(path, sizeof(path), fixture.path,
                    ".tc-gdb-stdout.next"));
    CHECK(write_text(path, "18446744073709551615"));
    CHECK(tc_gdb_journal_open(&journal, fixture.path) == TC_IMPL_OK);
    CHECK(tc_gdb_journal_high_water(journal) == UINT64_MAX);
    CHECK(tc_gdb_journal_write(journal, (const uint8_t *)"x", 1u) == -1);
    CHECK(tc_gdb_journal_failed(journal));
    tc_gdb_journal_close(journal);
    CHECK(remove_tree(fixture.path));
}

int main(void) {
    check_records_and_reopen();
    check_lock_competition();
    check_high_water_validation();
    check_conflict_and_permanent_failure();
    check_reservation_after_record_failure();
    check_changed_high_water();
    check_recovery();
    check_abnormal_recovery();
    check_directory_validation();
    check_nonregular_entries();
    check_exhaustion();
    printf("gdb_journal: %zu checks, %d failures\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
