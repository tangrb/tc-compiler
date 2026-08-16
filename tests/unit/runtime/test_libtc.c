/* test_libtc.c — libtc transactional ownership and repeatable consumption */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tc_aot_codegen.h"
#include "tc_lib.h"
#include "tc_test_port.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_passed = 0;
static int g_failed = 0;

static void check(int condition, const char *message) {
    if (condition) {
        g_passed++;
    } else {
        g_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static int streams_equal(FILE *lhs, FILE *rhs) {
    int lhs_ch = 0;
    int rhs_ch = 0;

    rewind(lhs);
    rewind(rhs);
    do {
        lhs_ch = fgetc(lhs);
        rhs_ch = fgetc(rhs);
        if (lhs_ch != rhs_ch) {
            return 0;
        }
    } while (lhs_ch != EOF);
    return 1;
}

static void check_failure_preserves_out(const char *source, TcErrorKind expected_kind,
                                        const char *message) {
    TcTypedProgram out;
    TcTypedProgram before;
    TcDiagnostic diag;

    memset(&out, 0xa5, sizeof(out));
    memcpy(&before, &out, sizeof(before));
    tc_diagnostic_init(&diag);
    check(tc_compile_source(source, "<test>", &out, &diag) == -1, message);
    check(diag.domain == TC_DIAG_LANGUAGE, "compile failure remains in language domain");
    check(diag.kind == expected_kind, "compile failure preserves the expected language kind");
    check(memcmp(&out, &before, sizeof(out)) == 0,
          "failed compilation leaves caller output byte-for-byte unchanged");
    tc_diagnostic_clear(&diag);
}

static void test_compile_failures_are_transactional(void) {
    check_failure_preserves_out("#program\nvar x int32 = 1\n", TC_CE_SYNTAX,
                                "parser failure returns -1");
    check_failure_preserves_out("#program\nvar x: int32 = missing\n", TC_CE_UNDEFINED_VARIABLE,
                                "binder failure returns -1");
    check_failure_preserves_out("#program\nlet X: int32 = div(int32, 10, 0)\n",
                                TC_CE_CONSTANT_DIV_ZERO,
                                "constant evaluation failure returns -1");
    check_failure_preserves_out("#lib\npublic func f() void then\n"
                                "    goto use_a\n"
                                "    var a: int32 = 0\n"
                                "    label use_a:\n"
                                "    var b: int32 = add(int32, a, 0)\n"
                                "    return\n"
                                "end\n",
                                TC_CE_UNINITIALIZED_VARIABLE,
                                "CFG dataflow failure returns -1");
}

static void test_source_lifetime_and_repeated_execution(void) {
    static const char text[] = "#program\nvar value: int32 = 7\n";
    char *source = (char *)malloc(sizeof(text));
    TcTypedProgram program;
    TcDiagnostic diag;

    check(source != NULL, "allocate caller-owned source");
    if (!source) {
        return;
    }
    memcpy(source, text, sizeof(text));
    tc_diagnostic_init(&diag);
    check(tc_compile_source(source, "<test>", &program, &diag) == 0,
          "compile caller-owned source successfully");
    free(source);
    check(tc_run_program(&program, &diag) == 0,
          "typed program remains executable after source is freed");
    check(tc_run_program(&program, &diag) == 0,
          "typed program supports repeated execution with fresh slots");
    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_file_lifetime(void) {
    char path[] = "/tmp/tc-libtc-source-XXXXXX";
    int fd = tc_test_mkstemps(path, 0);
    FILE *file = NULL;
    TcTypedProgram program;
    TcDiagnostic diag;

    check(fd >= 0, "create temporary TC source file");
    if (fd < 0) {
        return;
    }
    file = fdopen(fd, "w");
    check(file != NULL, "open temporary TC source stream");
    if (!file) {
        close(fd);
        unlink(path);
        return;
    }
    check(fputs("#program\nvar value: int32 = 9\n", file) >= 0,
          "write temporary TC source");
    check(fclose(file) == 0, "close temporary TC source");

    tc_diagnostic_init(&diag);
    check(tc_compile_file(path, &program, &diag) == 0,
          "compile file into caller-owned typed program");
    unlink(path);
    check(tc_run_program(&program, &diag) == 0,
          "typed program remains executable after source file is removed");
    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_repeated_aot_consumption(void) {
    TcTypedProgram program;
    TcDiagnostic diag;
    FILE *first = tmpfile();
    FILE *second = tmpfile();

    check(first != NULL && second != NULL, "create repeated AOT output streams");
    if (!first || !second) {
        if (first) {
            fclose(first);
        }
        if (second) {
            fclose(second);
        }
        return;
    }

    tc_diagnostic_init(&diag);
    check(tc_compile_source("#program\nvar value: int32 = 11\n", "<test>", &program, &diag) == 0,
          "compile program for repeated AOT consumption");
    check(tc_aot_emit_c(first, &program, "<first>", 0) == 0,
          "first AOT emission succeeds");
    check(tc_aot_emit_c(second, &program, "<first>", 0) == 0,
          "second AOT emission succeeds");
    check(streams_equal(first, second), "repeated AOT emissions are byte-identical");
    check(tc_run_program(&program, &diag) == 0,
          "AOT emission does not consume or mutate typed program");
    fclose(first);
    fclose(second);
    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_typed_program_free_clears_all_roots(void) {
    TcTypedProgram program;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tc_compile_source("#program\nvar value: int32 = 13\n", "<test>", &program, &diag) == 0,
          "compile program before ownership cleanup");
    check(program.program.items != NULL && program.symbols.symbols != NULL &&
              program.cfg != NULL,
          "successful typed program owns AST, symbols, and CFG");
    tc_typed_program_free(&program);
    check(program.program.items == NULL && program.program.count == 0 &&
              program.program.capacity == 0,
          "typed program free clears AST ownership");
    check(program.symbols.symbols == NULL && program.symbols.scopes == NULL &&
              program.symbols.labels == NULL && program.symbols.count == 0,
          "typed program free clears symbol ownership");
    check(program.cfg == NULL, "typed program free clears CFG ownership");
    check(program.warnings.items == NULL && program.warnings.count == 0,
          "typed program free clears warning ownership");
    tc_typed_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_file_errors_use_api_domain(void) {
    char missing_path[] = "/tmp/tc-libtc-missing-XXXXXX";
    char unreadable_path[] = "/tmp/tc-libtc-unreadable-XXXXXX";
    int missing_fd = tc_test_mkstemps(missing_path, 0);
    int unreadable_fd = -1;
#ifndef _WIN32
    int fifo_keepalive = -1;
#endif
    TcTypedProgram out;
    TcTypedProgram before;
    TcDiagnostic diag;

    check(missing_fd >= 0, "create unique missing-file path");
    if (missing_fd < 0) {
        return;
    }
    close(missing_fd);
    unlink(missing_path);
    memset(&out, 0x5a, sizeof(out));
    memcpy(&before, &out, sizeof(before));
    tc_diagnostic_init(&diag);

    check(tc_compile_file(missing_path, &out, &diag) == -1,
          "missing file compilation fails");
    check(diag.domain == TC_DIAG_API, "missing file uses API diagnostic domain");
    check(diag.api_code == TC_API_ERR_FILE_OPEN, "missing file uses FileOpen code");
    check(memcmp(&out, &before, sizeof(out)) == 0,
          "missing file leaves caller output unchanged");

    tc_diagnostic_clear(&diag);
    tc_diagnostic_init(&diag);
    unreadable_fd = tc_test_mkstemps(unreadable_path, 0);
    check(unreadable_fd >= 0, "create unique FileRead path");
    if (unreadable_fd < 0) {
        tc_diagnostic_clear(&diag);
        return;
    }
    close(unreadable_fd);
    unlink(unreadable_path);
#ifndef _WIN32
    /* Windows 无 FIFO（mkfifo）/O_NONBLOCK 语义，非 seekable 源场景仅 POSIX 可测 */
    check(mkfifo(unreadable_path, 0600) == 0, "create non-seekable source FIFO");
    fifo_keepalive = open(unreadable_path, O_RDWR | O_NONBLOCK);
    check(fifo_keepalive >= 0, "open source FIFO keepalive descriptor");
    if (fifo_keepalive < 0) {
        unlink(unreadable_path);
        tc_diagnostic_clear(&diag);
        return;
    }
    check(tc_compile_file(unreadable_path, &out, &diag) == -1,
          "non-seekable source fails with FileRead");
    check(diag.domain == TC_DIAG_API, "file read failure uses API diagnostic domain");
    check(diag.api_code == TC_API_ERR_FILE_READ, "file read failure uses FileRead code");
    check(memcmp(&out, &before, sizeof(out)) == 0,
          "file read failure leaves caller output unchanged");
    close(fifo_keepalive);
    unlink(unreadable_path);
#endif
    tc_diagnostic_clear(&diag);
}

static void test_invalid_arguments_use_api_domain(void) {
    TcTypedProgram program;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tc_compile_source("#program\nvar", "<test>", NULL, &diag) == -1,
          "null compile_source output is rejected before parsing");
    check(diag.domain == TC_DIAG_API,
          "null compile_source output uses API diagnostic domain");
    check(diag.api_code == TC_API_ERR_INVALID_ARGUMENT,
          "null compile_source output uses InvalidArgument code");

    tc_diagnostic_clear(&diag);
    tc_diagnostic_init(&diag);
    check(tc_compile_source("#program\nvar x: int32 = 1\n", NULL, &program, &diag) == -1,
          "null compile_source name is rejected");
    check(diag.domain == TC_DIAG_API && diag.api_code == TC_API_ERR_INVALID_ARGUMENT,
          "null compile_source name uses InvalidArgument code");

    tc_diagnostic_clear(&diag);
    tc_diagnostic_init(&diag);
    check(tc_compile_file("/tmp/tc-libtc-definitely-missing.tc", NULL, &diag) == -1,
          "null compile_file output is rejected before opening the file");
    check(diag.domain == TC_DIAG_API,
          "null compile_file output uses API diagnostic domain");
    check(diag.api_code == TC_API_ERR_INVALID_ARGUMENT,
          "null compile_file output uses InvalidArgument code");
    tc_diagnostic_clear(&diag);
}

static void test_compile_source_name_appears_in_diagnostics(void) {
    TcTypedProgram out;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(tc_compile_source("#program\nvar value: int32 = missing\n", "mem://unit", &out,
                            &diag) == -1,
          "compile_source reports undefined variable");
    check(diag.domain == TC_DIAG_LANGUAGE, "name test remains language domain");
    check(diag.filename != NULL && strcmp(diag.filename, "mem://unit") == 0,
          "compile_source uses caller-provided name in diagnostics");
    tc_diagnostic_clear(&diag);
}

static void test_null_diagnostic_and_program_do_not_crash(void) {
    TcTypedProgram program;
    TcDiagnostic diag;

    check(tc_compile_source("#program\nvar value: int32 = 1\n", "<test>", &program, NULL) == -1,
          "null diagnostic returns -1 without crashing");
    tc_diagnostic_init(&diag);
    check(tc_compile_source(NULL, "<test>", &program, &diag) == -1,
          "null source returns -1 without crashing");
    check(diag.domain == TC_DIAG_API && diag.api_code == TC_API_ERR_INVALID_ARGUMENT,
          "null source uses InvalidArgument API diagnostic");
    check(tc_run_program(NULL, &diag) == -1,
          "null typed program returns -1 without crashing");
    check(diag.domain == TC_DIAG_API && diag.api_code == TC_API_ERR_INVALID_ARGUMENT,
          "null typed program uses InvalidArgument API diagnostic");
    tc_diagnostic_clear(&diag);
}

static void test_source_and_file_language_kinds_match(void) {
    static const char source[] = "#program\nvar value: int32 = missing\n";
    char path[] = "/tmp/tc-libtc-language-kind-XXXXXX";
    int fd = tc_test_mkstemps(path, 0);
    FILE *file = NULL;
    TcTypedProgram out;
    TcDiagnostic source_diag;
    TcDiagnostic file_diag;

    check(fd >= 0, "create language-kind comparison file");
    if (fd < 0) {
        return;
    }
    file = fdopen(fd, "w");
    check(file != NULL, "open language-kind comparison stream");
    if (!file) {
        close(fd);
        unlink(path);
        return;
    }
    check(fputs(source, file) >= 0, "write language-kind comparison source");
    check(fclose(file) == 0, "close language-kind comparison source");

    tc_diagnostic_init(&source_diag);
    tc_diagnostic_init(&file_diag);
    check(tc_compile_source(source, "<test>", &out, &source_diag) == -1,
          "compile_source rejects comparison source");
    check(tc_compile_file(path, &out, &file_diag) == -1,
          "compile_file rejects comparison source");
    check(source_diag.domain == TC_DIAG_LANGUAGE && file_diag.domain == TC_DIAG_LANGUAGE,
          "source and file compilation report language domain after successful read");
    check(source_diag.kind == file_diag.kind,
          "source and file compilation preserve the same language error kind");
    unlink(path);
    tc_diagnostic_clear(&source_diag);
    tc_diagnostic_clear(&file_diag);
}

static void test_module_search_paths_resolve_import(void) {
    static const char *entry =
        "#program\n"
        "import ExtraLib\n"
        "var x: int32 = funcall(ExtraLib.extra_answer)\n"
        "writeln(int32, x)\n";
    char path[] = "/tmp/tc-libtc-search-XXXXXX";
    int fd = tc_test_mkstemps(path, 0);
    FILE *file = NULL;
    char *paths[1];
    char extra_dir[512];
    TcTypedProgram program;
    TcDiagnostic diag;
    const char *root = getenv("TC_TEST_ROOT");

    check(fd >= 0, "create search-path entry file");
    if (fd < 0) {
        return;
    }
    file = fdopen(fd, "w");
    check(file != NULL, "open search-path entry stream");
    if (!file) {
        close(fd);
        unlink(path);
        return;
    }
    check(fputs(entry, file) >= 0, "write search-path entry source");
    check(fclose(file) == 0, "close search-path entry source");

    if (!root || root[0] == '\0') {
        root = ".";
    }
    snprintf(extra_dir, sizeof(extra_dir), "%s/tests/modules/extra_libs", root);
    paths[0] = extra_dir;

    tc_diagnostic_init(&diag);
    check(tc_set_module_search_paths(paths, 1, &diag) == 0, "set ExtraLib search path");
    check(tc_compile_file(path, &program, &diag) == 0,
          "compile_file finds ExtraLib via search path");
    check(tc_run_program(&program, &diag) == 0, "run ExtraLib import program");
    tc_typed_program_free(&program);

    check(tc_set_module_search_paths(NULL, 0, &diag) == 0, "clear module search paths");
    check(tc_compile_file(path, &program, &diag) == -1,
          "cleared search paths make ExtraLib unresolved");
    check(diag.kind == TC_CE_IMPORT_NOT_FOUND, "cleared paths → IMPORT_NOT_FOUND");
    unlink(path);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_compile_failures_are_transactional();
    test_source_lifetime_and_repeated_execution();
    test_file_lifetime();
    test_repeated_aot_consumption();
    test_typed_program_free_clears_all_roots();
    test_file_errors_use_api_domain();
    test_invalid_arguments_use_api_domain();
    test_compile_source_name_appears_in_diagnostics();
    test_null_diagnostic_and_program_do_not_crash();
    test_source_and_file_language_kinds_match();
    test_module_search_paths_resolve_import();
    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
