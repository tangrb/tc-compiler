#define _GNU_SOURCE

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
#include <unistd.h>

#include "project/manifest.h"
#include "project/name.h"

static int failures;
static size_t checks;
static char temp_root[PATH_MAX];

#define CHECK(condition)                                                          \
    do {                                                                          \
        checks++;                                                                 \
        if (!(condition)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
            failures++;                                                           \
        }                                                                         \
    } while (0)

static int path_join(char *out, size_t size, const char *base, const char *name) {
    int length = snprintf(out, size, "%s/%s", base, name);
    return length >= 0 && (size_t)length < size;
}

static int make_dir(const char *path) {
    struct stat st;
    if (mkdir(path, 0700) == 0) {
        return 1;
    }
    return errno == EEXIST && lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int write_bytes(const char *path, const void *bytes, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    size_t total = 0u;
    int ok = fd >= 0;
    while (ok && total < size) {
        ssize_t got = write(fd, (const unsigned char *)bytes + total, size - total);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
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

static int write_text(const char *path, const char *text) {
    return write_bytes(path, text, strlen(text));
}

static int sparse_file(const char *path, off_t size, unsigned char first) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    int ok = fd >= 0;
    if (ok && write(fd, &first, 1u) != 1) {
        ok = 0;
    }
    if (ok && ftruncate(fd, size) != 0) {
        ok = 0;
    }
    if (fd >= 0 && close(fd) != 0) {
        ok = 0;
    }
    return ok;
}

static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *directory = opendir(path);
        if (directory != NULL) {
            struct dirent *entry;
            while ((entry = readdir(directory)) != NULL) {
                char child[PATH_MAX];
                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                if (path_join(child, sizeof(child), path, entry->d_name)) {
                    remove_tree(child);
                }
            }
            (void)closedir(directory);
        }
        (void)rmdir(path);
    } else {
        (void)unlink(path);
    }
}

static void expect_load_error(const char *path, const char *root, TcImplError impl,
                              const char *key) {
    TcProjectManifest *manifest = NULL;
    TcProjectError error;
    int ok = tc_project_manifest_load(path, root, &manifest, &error);
    CHECK(!ok);
    CHECK(manifest == NULL);
    if (error.impl != impl) {
        struct stat st;
        int present = lstat(path, &st) == 0;
        printf("  %s: expected %d, got %d (%s), present=%d mode=%o size=%lld\n",
               path, (int)impl, (int)error.impl,
               error.message == NULL ? "null" : error.message, present,
               present ? (unsigned int)st.st_mode : 0u,
               present ? (long long)st.st_size : -1LL);
    }
    CHECK(error.impl == impl);
    if (key != NULL) {
        if (error.key == NULL || strcmp(error.key, key) != 0) {
            printf("  key expected %s, got %s (%s)\n", key,
                   error.key == NULL ? "null" : error.key, error.message);
        }
        CHECK(error.key != NULL && strcmp(error.key, key) == 0);
    }
    CHECK(error.message != NULL);
    tc_project_manifest_destroy(manifest);
}

static TcProjectManifest *load_ok(const char *path, const char *root) {
    TcProjectManifest *manifest = NULL;
    TcProjectError error;
    CHECK(tc_project_manifest_load(path, root, &manifest, &error));
    if (error.impl != TC_IMPL_OK) {
        printf("  %s: %s (key=%s line=%zu)\n", path, error.message,
               error.key == NULL ? "null" : error.key, error.line);
    }
    CHECK(error.impl == TC_IMPL_OK);
    CHECK(manifest != NULL);
    return manifest;
}

static TcProjectManifest *discover_ok(const char *start, int present) {
    TcProjectManifest *manifest = NULL;
    TcProjectError error;
    CHECK(tc_project_discover(start, &manifest, &error));
    CHECK(error.impl == TC_IMPL_OK);
    CHECK(manifest != NULL);
    if (manifest != NULL) {
        CHECK(manifest->manifest_present == present);
    }
    return manifest;
}

static int canonical_equals(const char *actual, const char *path) {
    char *canonical = realpath(path, NULL);
    int equal;
    if (canonical == NULL) {
        return 0;
    }
    equal = strcmp(actual, canonical) == 0;
    free(canonical);
    return equal;
}

static void test_values_and_defaults(void) {
    char path[PATH_MAX];
    TcProjectManifest *manifest;
    CHECK(path_join(path, sizeof(path), temp_root, "tc.project"));
    CHECK(write_text(path,
            "# comment\n"
            "tc-project\t1 # version\n"
            "name = \"de#=x\"\n"
            "entry = \"src\\\\main.tc\"\n"
            "include = \"./src\"\n"
            "include = \"../outside\"\n"
            "include = \"a\\\"b\"\n"
            "profile = \"riscv32-baremetal\"\n"));
    manifest = load_ok(path, temp_root);
    if (manifest != NULL) {
        CHECK(strcmp(manifest->name, "de#=x") == 0);
        CHECK(strcmp(manifest->entry, "src\\main.tc") == 0);
        CHECK(strcmp(manifest->profile, "riscv32-baremetal") == 0);
        CHECK(manifest->ninclude_roots == 3u);
        CHECK(strcmp(manifest->includes[0], "./src") == 0);
        CHECK(strcmp(manifest->includes[1], "../outside") == 0);
        CHECK(strcmp(manifest->includes[2], "a\"b") == 0);
        tc_project_manifest_destroy(manifest);
    }

    CHECK(write_text(path, "tc-project 1\n"));
    manifest = load_ok(path, temp_root);
    if (manifest != NULL) {
        const char *slash = strrchr(temp_root, '/');
        CHECK(strcmp(manifest->name, slash == NULL ? temp_root : slash + 1) == 0);
        CHECK(strcmp(manifest->entry, "src/main.tc") == 0);
        CHECK(strcmp(manifest->profile, "aarch64-linux-user") == 0);
        CHECK(manifest->ninclude_roots == 1u);
        CHECK(strcmp(manifest->includes[0], "src") == 0);
        tc_project_manifest_destroy(manifest);
    }

    CHECK(write_text(path,
            "\xef\xbb\xbf# BOM and CRLF\r\n"
            "\t\r\n"
            "tc-project 1\r\n"
            "name = \"\xe4\xb8\xad\xe6\x96\x87\"\r\n"
            "entry = \"a/../main.tc\" # comment\r\n"
            "include = \"..\"\r\n"
            "profile = \"w32\""));
    manifest = load_ok(path, temp_root);
    if (manifest != NULL) {
        CHECK(strcmp(manifest->name, "中文") == 0);
        CHECK(strcmp(manifest->entry, "a/../main.tc") == 0);
        CHECK(manifest->ninclude_roots == 1u);
        CHECK(strcmp(manifest->includes[0], "..") == 0);
        CHECK(strcmp(manifest->profile, "w32") == 0);
        tc_project_manifest_destroy(manifest);
    }
}

static void test_profiles(void) {
    static const char *const profiles[] = {
        "aarch64-linux-user", "aarch64-baremetal", "arm32-baremetal",
        "riscv32-baremetal", "w32"
    };
    char path[PATH_MAX];
    char text[160];
    size_t i;
    CHECK(path_join(path, sizeof(path), temp_root, "tc.project"));
    for (i = 0u; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        TcProjectManifest *manifest;
        (void)snprintf(text, sizeof(text), "tc-project 1\nprofile = \"%s\"\n",
                       profiles[i]);
        CHECK(write_text(path, text));
        manifest = load_ok(path, temp_root);
        if (manifest != NULL) {
            CHECK(strcmp(manifest->profile, profiles[i]) == 0);
            tc_project_manifest_destroy(manifest);
        }
    }
    CHECK(write_text(path, "tc-project 1\nprofile = \"w64\"\n"));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "profile");
}

static void test_syntax_errors(void) {
    static const struct {
        const char *text;
        const char *key;
    } cases[] = {
        { "", "tc-project" },
        { "# only\n\t\n", "tc-project" },
        { "name = \"x\"\ntc-project 1\n", "tc-project" },
        { "tc-project 01\n", "tc-project" },
        { "tc-project +1\n", "tc-project" },
        { "tc-project 2\n", "tc-project" },
        { "tc-project 1 extra\n", "tc-project" },
        { "tc-project = \"1\"\n", "tc-project" },
        { "tc-project 1\nname \"x\"\n", NULL },
        { "tc-project 1\nname = x\n", "name" },
        { "tc-project 1\nname = \"\"\n", "name" },
        { "tc-project 1\nname = \"x\\n\"\n", "name" },
        { "tc-project 1\nname = \"x\\\"\n", "name" },
        { "tc-project 1\nname = \"x\" junk\n", "name" },
        { "tc-project 1\nunknown = \"x\"\n", "unknown" },
        { "tc-project 1\nname = \"x\"\nname = \"y\"\n", "name" },
        { "tc-project 1\nentry = \"a\"\nentry = \"b\"\n", "entry" },
        { "tc-project 1\nprofile = \"w32\"\nprofile = \"w32\"\n", "profile" },
        { "tc-project 1\ntc-project 1\n", "tc-project" },
        { "tc-project 1\rname = \"x\"\r", NULL },
        { "tc-project 1\nname = \"x\x01\"\n", NULL },
        { "tc-project 1\n# \xe2\x80\x8f\n", NULL },
        { "tc-project 1\nname = \"x\xe2\x80\x8f\"\n", "name" },
        { "tc-project 1\nname = \"x\\\"\"\n", "name" },
        { "\xef\xbb\xbf# no header\n", "tc-project" },
        { "tc-project 1\n= \"x\"\n", NULL }
    };
    char path[PATH_MAX];
    size_t i;
    CHECK(path_join(path, sizeof(path), temp_root, "tc.project"));
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(write_text(path, cases[i].text));
        expect_load_error(path, temp_root, TC_IMPL_USAGE, cases[i].key);
    }
    {
        static const unsigned char nul_name[] =
            "tc-project 1\nname = \"ok\x00hidden\"\n";
        static const unsigned char nul_profile[] =
            "tc-project 1\nprofile = \"w32\x00hidden\"\n";
        CHECK(write_bytes(path, nul_name, sizeof(nul_name) - 1u));
        expect_load_error(path, temp_root, TC_IMPL_USAGE, NULL);
        CHECK(write_bytes(path, nul_profile, sizeof(nul_profile) - 1u));
        expect_load_error(path, temp_root, TC_IMPL_USAGE, NULL);
    }
    {
        static const unsigned char invalid[] = {
            't', 'c', '-', 'p', 'r', 'o', 'j', 'e', 'c', 't', ' ', '1', '\n',
            '#', 0xff, '\n'
        };
        CHECK(write_bytes(path, invalid, sizeof(invalid)));
        expect_load_error(path, temp_root, TC_IMPL_USAGE, NULL);
    }
}

static void test_paths_and_names(void) {
    char path[PATH_MAX];
    CHECK(path_join(path, sizeof(path), temp_root, "tc.project"));
    CHECK(write_text(path, "tc-project 1\nentry = \"/absolute\"\n"));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "entry");
    CHECK(write_text(path, "tc-project 1\ninclude = \"/absolute\"\n"));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "include");
    CHECK(write_text(path, "tc-project 1\ninclude = \"a//b/\"\n"));
    {
        TcProjectManifest *manifest = load_ok(path, temp_root);
        if (manifest != NULL) {
            CHECK(strcmp(manifest->includes[0], "a//b/") == 0);
            tc_project_manifest_destroy(manifest);
        }
    }
    CHECK(write_text(path, "tc-project 1\nname = \".\"\n"));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "name");
    CHECK(write_text(path, "tc-project 1\nname = \"..\"\n"));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "name");
    CHECK(write_text(path, "tc-project 1\nname = \"a/b\"\n"));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "name");
    CHECK(write_text(path, "tc-project 1\nname = \"a\\\\b\"\n"));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "name");
}

static void test_include_limit(void) {
    char path[PATH_MAX];
    char *text;
    size_t capacity = 32u * 24u + 32u;
    size_t used = (size_t)snprintf(NULL, 0u, "tc-project 1\n");
    size_t i;
    CHECK(path_join(path, sizeof(path), temp_root, "tc.project"));
    text = malloc(capacity);
    CHECK(text != NULL);
    if (text == NULL) {
        return;
    }
    (void)snprintf(text, capacity, "tc-project 1\n");
    for (i = 0u; i < TC_PROJECT_MAX_INCLUDE_ROOTS; i++) {
        int length = snprintf(text + used, capacity - used, "include = \"root%zu\"\n", i);
        CHECK(length > 0 && (size_t)length < capacity - used);
        used += (size_t)length;
    }
    CHECK(write_bytes(path, text, used));
    {
        TcProjectManifest *manifest = load_ok(path, temp_root);
        if (manifest != NULL) {
            CHECK(manifest->ninclude_roots == TC_PROJECT_MAX_INCLUDE_ROOTS);
            CHECK(strcmp(manifest->includes[31], "root31") == 0);
            tc_project_manifest_destroy(manifest);
        }
    }
    (void)snprintf(text + used, capacity - used, "include = \"one-too-many\"\n");
    used += strlen(text + used);
    CHECK(write_bytes(path, text, used));
    expect_load_error(path, temp_root, TC_IMPL_USAGE, "include");
    free(text);
}

static void test_size_and_priority(void) {
    char path[PATH_MAX];
    unsigned char *bytes;
    size_t size = TC_PROJECT_MAX_MANIFEST_BYTES;
    CHECK(path_join(path, sizeof(path), temp_root, "tc.project"));
    CHECK(sparse_file(path, (off_t)TC_PROJECT_MAX_MANIFEST_BYTES + 1, 0xffu));
    expect_load_error(path, temp_root, TC_IMPL_INTERNAL, NULL);

    bytes = malloc(size);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    memcpy(bytes, "tc-project 1\n#", sizeof("tc-project 1\n#") - 1u);
    memset(bytes + sizeof("tc-project 1\n#") - 1u, 'x', size - sizeof("tc-project 1\n#"));
    bytes[size - 1u] = '\n';
    CHECK(write_bytes(path, bytes, size));
    {
        TcProjectManifest *manifest = load_ok(path, temp_root);
        tc_project_manifest_destroy(manifest);
    }
    CHECK(sparse_file(path, (off_t)TC_PROJECT_MAX_MANIFEST_BYTES + 1, 0xffu));
    expect_load_error(path, temp_root, TC_IMPL_INTERNAL, NULL);
    free(bytes);

    CHECK(unlink(path) == 0);
    expect_load_error(path, temp_root, TC_IMPL_IO, NULL);
}

static void test_non_regular_and_symlink(void) {
    char path[PATH_MAX];
    char directory[PATH_MAX];
    char target[PATH_MAX];
    char link[PATH_MAX];
    TcProjectManifest *manifest;
    CHECK(path_join(path, sizeof(path), temp_root, "tc.project"));
    CHECK(make_dir(path));
    expect_load_error(path, temp_root, TC_IMPL_IO, NULL);
    CHECK(rmdir(path) == 0);
    CHECK(mkfifo(path, 0600) == 0);
    expect_load_error(path, temp_root, TC_IMPL_IO, NULL);
    CHECK(unlink(path) == 0);
    CHECK(symlink("missing-target", path) == 0);
    expect_load_error(path, temp_root, TC_IMPL_IO, NULL);
    CHECK(unlink(path) == 0);

    CHECK(path_join(directory, sizeof(directory), temp_root, "anchor"));
    CHECK(path_join(target, sizeof(target), temp_root, "elsewhere.project"));
    CHECK(path_join(link, sizeof(link), directory, "tc.project"));
    CHECK(make_dir(directory));
    CHECK(write_text(target, "tc-project 1\nname = \"linked\"\n"));
    CHECK(symlink("../elsewhere.project", link) == 0);
    manifest = discover_ok(directory, 1);
    if (manifest != NULL) {
        char *canonical = realpath(directory, NULL);
        CHECK(canonical_equals(manifest->root, directory));
        CHECK(strcmp(manifest->name, "linked") == 0);
        if (canonical != NULL) {
            char expected[PATH_MAX];
            int length = snprintf(expected, sizeof(expected), "%s/tc.project",
                                  canonical);
            CHECK(length > 0 && (size_t)length < sizeof(expected));
            CHECK(strcmp(manifest->manifest_path, expected) == 0);
        }
        free(canonical);
        tc_project_manifest_destroy(manifest);
    }
}

static void test_root_discovery(void) {
    char outer[PATH_MAX];
    char nested[PATH_MAX];
    char deep[PATH_MAX];
    char manifest[PATH_MAX];
    char start_link[PATH_MAX];
    TcProjectManifest *project;
    CHECK(path_join(outer, sizeof(outer), temp_root, "discovery"));
    CHECK(path_join(nested, sizeof(nested), outer, "nested"));
    CHECK(path_join(deep, sizeof(deep), nested, "deep"));
    CHECK(make_dir(outer));
    CHECK(make_dir(nested));
    CHECK(make_dir(deep));
    project = discover_ok(deep, 0);
    if (project != NULL) {
        CHECK(canonical_equals(project->root, deep));
        CHECK(strcmp(project->entry, "src/main.tc") == 0);
        CHECK(strcmp(project->profile, "aarch64-linux-user") == 0);
        CHECK(project->name != NULL && tc_project_name_valid(project->name));
        CHECK(project->ninclude_roots == 0u);
        tc_project_manifest_destroy(project);
    }

    CHECK(path_join(manifest, sizeof(manifest), outer, "tc.project"));
    CHECK(write_text(manifest, "tc-project 1\nname = \"outer\"\n"));
    project = discover_ok(deep, 1);
    if (project != NULL) {
        CHECK(strcmp(project->name, "outer") == 0);
        CHECK(canonical_equals(project->root, outer));
        tc_project_manifest_destroy(project);
    }

    CHECK(path_join(manifest, sizeof(manifest), nested, "tc.project"));
    CHECK(write_text(manifest, "tc-project 1\nname = \"inner\"\n"));
    project = discover_ok(deep, 1);
    if (project != NULL) {
        CHECK(strcmp(project->name, "inner") == 0);
        CHECK(canonical_equals(project->root, nested));
        tc_project_manifest_destroy(project);
    }

    CHECK(write_text(manifest, "tc-project 1\nunknown = \"x\"\n"));
    {
        TcProjectError error;
        project = NULL;
        CHECK(!tc_project_discover(deep, &project, &error));
        CHECK(project == NULL);
        CHECK(error.impl == TC_IMPL_USAGE);
        CHECK(error.key != NULL && strcmp(error.key, "unknown") == 0);
    }
    CHECK(unlink(manifest) == 0);
    CHECK(sparse_file(manifest, (off_t)TC_PROJECT_MAX_MANIFEST_BYTES + 1, 0xffu));
    {
        TcProjectError error;
        project = NULL;
        CHECK(!tc_project_discover(deep, &project, &error));
        CHECK(project == NULL);
        CHECK(error.impl == TC_IMPL_INTERNAL);
    }
    CHECK(unlink(manifest) == 0);
    CHECK(make_dir(manifest));
    {
        TcProjectError error;
        project = NULL;
        CHECK(!tc_project_discover(deep, &project, &error));
        CHECK(project == NULL);
        if (error.impl != TC_IMPL_IO) {
            printf("directory anchor: %s\n", error.message);
        }
        CHECK(error.impl == TC_IMPL_IO);
    }
    CHECK(rmdir(manifest) == 0);
    {
        int linked = symlink("missing", manifest);
        if (linked != 0) {
            printf("symlink failed: %s\n", strerror(errno));
        }
        CHECK(linked == 0);
    }
    {
        TcProjectError error;
        project = NULL;
        CHECK(!tc_project_discover(deep, &project, &error));
        CHECK(project == NULL);
        if (error.impl != TC_IMPL_IO) {
            printf("dangling anchor: %s\n", error.message);
        }
        CHECK(error.impl == TC_IMPL_IO);
    }
    CHECK(unlink(manifest) == 0);

    CHECK(path_join(start_link, sizeof(start_link), outer, "deep-link"));
    CHECK(symlink("nested/deep", start_link) == 0);
    project = discover_ok(start_link, 1);
    if (project != NULL) {
        CHECK(strcmp(project->name, "outer") == 0);
        CHECK(canonical_equals(project->root, outer));
        tc_project_manifest_destroy(project);
    }
    CHECK(tc_project_discover(manifest, NULL, NULL) == 0);
    {
        TcProjectError error;
        project = NULL;
        CHECK(tc_project_discover(outer, &project, &error));
        CHECK(project != NULL);
        CHECK(error.impl == TC_IMPL_OK);
        if (project != NULL) {
            CHECK(strcmp(project->name, "outer") == 0);
        }
        tc_project_manifest_destroy(project);
    }
}

int main(void) {
    char pattern[PATH_MAX];
    char invalid_root[PATH_MAX];
    char invalid_manifest[PATH_MAX];
    (void)snprintf(pattern, sizeof(pattern), TC_TEST_TMP_ROOT "/tc-project-manifest-XXXXXX");
    if (mkdtemp(pattern) == NULL) {
        printf("FAIL: cannot create temporary directory\n");
        return 1;
    }
    (void)snprintf(temp_root, sizeof(temp_root), "%s", pattern);

    test_values_and_defaults();
    test_profiles();
    test_syntax_errors();
    test_paths_and_names();
    test_include_limit();
    test_size_and_priority();
    test_non_regular_and_symlink();
    test_root_discovery();

    CHECK(path_join(invalid_root, sizeof(invalid_root), temp_root, "bad\nname"));
    CHECK(path_join(invalid_manifest, sizeof(invalid_manifest), invalid_root,
                    "tc.project"));
    CHECK(make_dir(invalid_root));
    CHECK(write_text(invalid_manifest, "tc-project 1\n"));
    expect_load_error(invalid_manifest, invalid_root, TC_IMPL_USAGE, "name");

    remove_tree(temp_root);
    if (failures != 0) {
        printf("%zu checks, %d failures\n", checks, failures);
        return 1;
    }
    printf("%zu project manifest checks passed\n", checks);
    return 0;
}
