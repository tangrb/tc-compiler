#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/path.h"
#include "frontend/module.h"
#include "frontend/sem.h"
#include "ir/ir.h"
#include "runtime/profile.h"

static int failures;
static int checks;

#define CHECK(condition)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void check_text(void) {
    CHECK(tc_path_text_valid("src/main.tc"));
    CHECK(tc_path_text_valid("工程/主.tc"));
    CHECK(!tc_path_text_valid(NULL));
    CHECK(!tc_path_text_valid(""));
    CHECK(!tc_path_text_valid("bad\npath"));
    CHECK(!tc_path_text_valid("bad\x7f"));
    CHECK(!tc_path_text_valid("bad\xe2\x80\x8d"));
    CHECK(!tc_path_text_valid("bad\xe2\x80\xa8"));
    CHECK(!tc_path_text_valid("bad\xe2\x80\xa9"));
    CHECK(!tc_path_text_valid("bad\xc0\xaf"));
    CHECK(!tc_path_text_valid("bad\xed\xa0\x80"));
}

static void check_context(void) {
    TcPathContext *context = NULL;
    const TcPathFile *internal;
    const TcPathFile *external;
    const TcPathFile *sibling;
    const TcPathFile *again;
    const TcPathFile *fallback;
    CHECK(tc_path_context_create("/project", &context) == TC_IMPL_OK);
    if (context == NULL) {
        return;
    }
    internal = tc_path_context_add(context, "/project/src/main.tc");
    CHECK(internal != NULL);
    CHECK(internal != NULL && strcmp(internal->display, "src/main.tc") == 0);
    CHECK(internal != NULL && internal->external == 0);
    external = tc_path_context_add(context, "/outside/lib.tc");
    CHECK(external != NULL);
    CHECK(external != NULL && strcmp(external->display, "../outside/lib.tc") == 0);
    CHECK(external != NULL && external->external == 1);
    sibling = tc_path_context_add(context, "/project-other/lib.tc");
    CHECK(sibling != NULL);
    CHECK(sibling != NULL &&
          strcmp(sibling->display, "../project-other/lib.tc") == 0);
    CHECK(sibling != NULL && sibling->external == 1);
    again = tc_path_context_add(context, "/project/src/main.tc");
    CHECK(again == internal);
    CHECK(strcmp(tc_path_display(context, "/outside/lib.tc"),
                 "../outside/lib.tc") == 0);
    CHECK(tc_path_context_find(context, "/absent.tc") == NULL);
    fallback = tc_path_context_add(context, "/project");
    CHECK(fallback != NULL);
    CHECK(fallback != NULL && strcmp(fallback->display, "./project#1") == 0);
    CHECK(fallback != NULL && fallback->external == 1);
    CHECK(tc_path_context_add(context, "/project") == fallback);
    tc_path_context_destroy(context);
}

static void check_ir_modules(void) {
    const char *fixture = TC_TEST_SOURCE_DIR
        "/conformance/static/accept_s3/tc-tool-sem-0024";
    TcPathContext *context = NULL;
    TcPlatformProfile profile;
    TcAnalysisStatus status = TC_ANALYSIS_IMPL;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcAnalysis *analysis;
    char *root = tc_path_join(fixture, "root.tc");
    char *left = tc_path_join(fixture, "left");
    char *right = tc_path_join(fixture, "right");
    const char *roots[2];
    const TcIR *ir;
    CHECK(root != NULL && left != NULL && right != NULL);
    CHECK(tc_path_context_create(fixture, &context) == TC_IMPL_OK);
    CHECK(tc_profile_init_aarch64_linux_user(&profile));
    roots[0] = left;
    roots[1] = right;
    analysis = tc_analyze_with_paths(root, &profile, 64, roots, 2, context,
                                     &status, &diag, &impl);
    CHECK(analysis != NULL);
    CHECK(status == TC_ANALYSIS_OK);
    ir = tc_analysis_ir(analysis);
    CHECK(ir != NULL);
    CHECK(ir != NULL && ir->nmodules == 5);
    if (ir != NULL && ir->nmodules == 5) {
        CHECK(strcmp(ir->modules[0].path, "root.tc") == 0);
        CHECK(strcmp(ir->modules[1].path, "left/a.tc") == 0);
        CHECK(strcmp(ir->modules[2].path, "left/shared.tc") == 0);
        CHECK(strcmp(ir->modules[3].path, "right/b.tc") == 0);
        CHECK(strcmp(ir->modules[4].path, "right/shared.tc") == 0);
    }
    tc_analysis_destroy(analysis);
    tc_path_context_destroy(context);
    free(right);
    free(left);
    free(root);
}

static int write_fixture(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    int ok;
    if (file == NULL) {
        return 0;
    }
    ok = fputs(text, file) >= 0;
    return fclose(file) == 0 && ok;
}

static void check_module_identity(void) {
    char directory[] = TC_TEST_TMP_ROOT "/tc-path-identity-XXXXXX";
    char *root = NULL;
    char *a = NULL;
    char *b = NULL;
    char *c = NULL;
    char *canonical_a = NULL;
    TcPathContext *context = NULL;
    TcModuleGraph *graph;
    char *made;
    CHECK((made = mkdtemp(directory)) != NULL);
    if (made == NULL) {
        return;
    }
    root = tc_path_join(directory, "root.tc");
    a = tc_path_join(directory, "a.tc");
    b = tc_path_join(directory, "b.tc");
    c = tc_path_join(directory, "c.tc");
    CHECK(root != NULL && a != NULL && b != NULL && c != NULL);
    CHECK(tc_path_context_create(directory, &context) == TC_IMPL_OK);
    CHECK(write_fixture(a, "#lib\npublic const K: int32 = 1\n"));
    CHECK(link(a, b) == 0);
    CHECK(write_fixture(root,
                        "#program\nimport a\nimport b\n"
                        "_ = writeln int32, 1\n"));
    graph = tc_module_graph_load_with_paths(root, NULL, 0, context);
    CHECK(graph != NULL);
    CHECK(graph != NULL && graph->impl == TC_IMPL_OK);
    CHECK(graph != NULL && !graph->has_lexsyn);
    CHECK(graph != NULL && graph->nmodules == 3);
    CHECK(graph != NULL && graph->nmodules == 3 &&
          strcmp(graph->modules[1].identity, graph->modules[2].identity) != 0);
    tc_module_graph_destroy(graph);

    CHECK(symlink("a.tc", c) == 0);
    canonical_a = realpath(a, NULL);
    CHECK(canonical_a != NULL);
    CHECK(write_fixture(root,
                        "#program\nimport a\nimport c\n"
                        "_ = writeln int32, 1\n"));
    graph = tc_module_graph_load_with_paths(root, NULL, 0, context);
    CHECK(graph != NULL);
    CHECK(graph != NULL && graph->impl == TC_IMPL_OK);
    CHECK(graph != NULL && !graph->has_lexsyn);
    CHECK(graph != NULL && graph->nmodules == 2);
    CHECK(graph != NULL && graph->nmodules == 2 &&
          strcmp(graph->modules[1].path, canonical_a) == 0);
    tc_module_graph_destroy(graph);

    tc_path_context_destroy(context);
    (void)unlink(c);
    (void)unlink(b);
    (void)unlink(a);
    (void)unlink(root);
    (void)rmdir(directory);
    free(canonical_a);
    free(c);
    free(b);
    free(a);
    free(root);
}

static void check_symlink_parent(void) {
    char directory[] = TC_TEST_TMP_ROOT "/tc-path-parent-XXXXXX";
    char *real = NULL;
    char *alias = NULL;
    char *source = NULL;
    char *bound = NULL;
    int fd = -1;
    int status;
    CHECK(mkdtemp(directory) != NULL);
    real = tc_path_join(directory, "real");
    alias = tc_path_join(directory, "alias");
    CHECK(real != NULL && alias != NULL);
    CHECK(real != NULL && mkdir(real, 0700) == 0);
    CHECK(alias != NULL && symlink("real", alias) == 0);
    source = tc_path_join(alias, "main.tc");
    CHECK(source != NULL);
    CHECK(source != NULL && write_fixture(source, "#program\n"));
    status = tc_module_bind_root(source, &fd, &bound);
    CHECK(status == 1);
    CHECK(bound != NULL);
    if (fd >= 0) {
        CHECK(close(fd) == 0);
    }
    if (source != NULL) {
        (void)unlink(source);
    }
    if (alias != NULL) {
        (void)unlink(alias);
    }
    if (real != NULL) {
        (void)rmdir(real);
    }
    (void)rmdir(directory);
    free(bound);
    free(source);
    free(alias);
    free(real);
}

int main(void) {
    check_text();
    check_context();
    check_ir_modules();
    check_module_identity();
    check_symlink_parent();
    if (failures != 0) {
        printf("project.paths_unit: %d failure(s), %d checks\n", failures,
               checks);
        return 1;
    }
    printf("project.paths_unit: %d checks passed\n", checks);
    return 0;
}
