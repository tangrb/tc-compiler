/*
 * test_module.c — Phase 2 模块系统（tc_module / tc_scope）单元测试
 *
 * 覆盖：结构检查、Self 禁用、成员索引、import 定位/歧义/环、
 * #lib 可见性、签名收集。端到端行为另见 tests/modules/ 与 tests/errors/module/。
 */
#include "tc_diagnostic.h"
#include "tc_lib.h"
#include "tc_module.h"
#include "tc_parser.h"
#include "tc_scope.h"
#include "tc_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int write_temp_file(char *path_template, const char *contents) {
    int fd = mkstemp(path_template);
    FILE *fp = NULL;

    if (fd < 0) {
        return -1;
    }
    fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(path_template);
        return -1;
    }
    if (fputs(contents, fp) < 0) {
        fclose(fp);
        unlink(path_template);
        return -1;
    }
    if (fclose(fp) != 0) {
        unlink(path_template);
        return -1;
    }
    return 0;
}

static void test_module_check_structure_program_ok(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source = "#program\nvar x: int32 = 1\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse #program body");
    check(tc_module_check_structure(&program, &diag) == 0, "structure check accepts #program");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_module_check_self_in_program(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source = "#program\nvar x: int32 = Self.k\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse Self in #program");
    check(tc_module_check_structure(&program, &diag) != 0, "Self in #program fails structure");
    check(diag.kind == TC_CE_PROGRAM_MODE_MISUSE, "Self misuse kind");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_member_index_build(void) {
    TcProgram program;
    TcDiagnostic diag;
    TcMemberIndex index;
    const TcMemberEntry *entry = NULL;
    const char *source =
        "#lib\n"
        "public struct Box then\n"
        "    var v: int32\n"
        "end\n"
        "public static let K: int32 = 1\n"
        "private static var s: int32 = 0\n"
        "public func id ( x: int32 ) int32 then\n"
        "    return x\n"
        "end\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    tc_member_index_init(&index);
    check(tc_parse_source_to_program(source, &program, &diag) == 0, "parse lib for member index");
    check(tc_member_index_build(&program, &index, &diag) == 0, "build member index");
    check(index.count == 4, "member index has 4 entries");
    entry = tc_member_index_find(&index, "id");
    check(entry != NULL && entry->kind == TC_MEMBER_FUNC && entry->visibility == TC_VIS_PUBLIC,
          "find public func id");
    entry = tc_member_index_find(&index, "K");
    check(entry != NULL && entry->kind == TC_MEMBER_STATIC_LET, "find static let K");
    entry = tc_member_index_find(&index, "s");
    check(entry != NULL && entry->kind == TC_MEMBER_STATIC_VAR &&
              entry->visibility == TC_VIS_PRIVATE,
          "find private static var s");
    entry = tc_member_index_find(&index, "Box");
    check(entry != NULL && entry->kind == TC_MEMBER_STRUCT, "find struct Box");
    entry = tc_member_index_find(&index, "missing");
    check(entry == NULL, "missing member returns NULL");
    tc_member_index_free(&index);
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

static void test_import_not_found_via_compile_file(void) {
    char path[] = "/tmp/tc-mod-missing-XXXXXX";
    TcTypedProgram out;
    TcDiagnostic diag;

    check(write_temp_file(path, "#program\nimport DoesNotExist\n") == 0,
          "write import-not-found fixture");
    tc_diagnostic_init(&diag);
    check(tc_compile_file(path, &out, &diag) == -1, "compile missing import fails");
    check(diag.kind == TC_CE_IMPORT_NOT_FOUND, "import not found kind");
    unlink(path);
    tc_diagnostic_clear(&diag);
}

static void test_import_success_and_signatures(void) {
    char dir_template[] = "/tmp/tc-mod-ok-XXXXXX";
    char *dir = mkdtemp(dir_template);
    char lib_path[256];
    char entry_path[256];
    FILE *fp = NULL;
    TcTypedProgram typed;
    TcDiagnostic diag;
    TcFuncSignatureList sigs;

    check(dir != NULL, "create temp module dir");
    if (!dir) {
        return;
    }
    snprintf(lib_path, sizeof(lib_path), "%s/Util.tc", dir);
    snprintf(entry_path, sizeof(entry_path), "%s/main.tc", dir);

    fp = fopen(lib_path, "w");
    check(fp != NULL, "open Util.tc");
    if (!fp) {
        return;
    }
    fputs("#lib\n"
          "public static let Z: int32 = 0\n"
          "public func add1 ( x: int32 ) int32 then\n"
          "    return x\n"
          "end\n",
          fp);
    fclose(fp);

    fp = fopen(entry_path, "w");
    check(fp != NULL, "open main.tc");
    if (!fp) {
        unlink(lib_path);
        rmdir(dir);
        return;
    }
    fputs("#program\nimport Util\nvar x: int32 = 1\n", fp);
    fclose(fp);

    tc_diagnostic_init(&diag);
    memset(&typed, 0, sizeof(typed));
    if (tc_compile_file(entry_path, &typed, &diag) == 0) {
        check(1, "compile successful import");
        check(typed.dep_count == 1, "one dependency loaded");
        check(typed.deps != NULL && typed.deps[0].mode == TC_MODULE_LIB, "dep is #lib");
        check(typed.deps[0].module_name != NULL && strcmp(typed.deps[0].module_name, "Util") == 0,
              "dep module name Util");

        tc_func_signature_list_init(&sigs);
        check(tc_module_collect_signatures(&typed, &sigs, &diag) == 0, "collect signatures");
        check(sigs.count == 1, "one function signature");
        if (sigs.count == 1) {
            check(strcmp(sigs.items[0].name, "add1") == 0, "signature name add1");
            check(sigs.items[0].return_type.tag == TC_INT32, "signature return int32");
            check(sigs.items[0].param_count == 1, "signature one param");
            check(sigs.items[0].visibility == TC_VIS_PUBLIC, "signature public");
        }
        tc_func_signature_list_free(&sigs);
        tc_typed_program_free(&typed);
    } else {
        check(0, "compile successful import");
        if (diag.message) {
            fprintf(stderr, "  note: %s\n", diag.message);
        }
    }
    tc_diagnostic_clear(&diag);

    unlink(lib_path);
    unlink(entry_path);
    rmdir(dir);
}

static void test_circular_import_pair(void) {
    char dir_template[] = "/tmp/tc-mod-circ-XXXXXX";
    char *dir = mkdtemp(dir_template);
    char a_path[256];
    char b_path[256];
    char entry_path[256];
    FILE *fp = NULL;
    TcTypedProgram out;
    TcDiagnostic diag;

    check(dir != NULL, "create circular module dir");
    if (!dir) {
        return;
    }
    snprintf(a_path, sizeof(a_path), "%s/A.tc", dir);
    snprintf(b_path, sizeof(b_path), "%s/B.tc", dir);
    snprintf(entry_path, sizeof(entry_path), "%s/entry.tc", dir);

    fp = fopen(a_path, "w");
    fputs("#lib\nimport B\npublic func a ( ) void then\nend\n", fp);
    fclose(fp);
    fp = fopen(b_path, "w");
    fputs("#lib\nimport A\npublic func b ( ) void then\nend\n", fp);
    fclose(fp);
    fp = fopen(entry_path, "w");
    fputs("#program\nimport A\n", fp);
    fclose(fp);

    tc_diagnostic_init(&diag);
    check(tc_compile_file(entry_path, &out, &diag) == -1, "circular import fails");
    check(diag.kind == TC_CE_CIRCULAR_IMPORT, "circular import kind");
    tc_diagnostic_clear(&diag);

    unlink(a_path);
    unlink(b_path);
    unlink(entry_path);
    rmdir(dir);
}

static void test_import_not_lib(void) {
    char dir_template[] = "/tmp/tc-mod-notlib-XXXXXX";
    char *dir = mkdtemp(dir_template);
    char lib_path[256];
    char entry_path[256];
    FILE *fp = NULL;
    TcTypedProgram out;
    TcDiagnostic diag;

    check(dir != NULL, "create not-lib dir");
    if (!dir) {
        return;
    }
    snprintf(lib_path, sizeof(lib_path), "%s/Prog.tc", dir);
    snprintf(entry_path, sizeof(entry_path), "%s/entry.tc", dir);
    fp = fopen(lib_path, "w");
    fputs("#program\nvar x: int32 = 0\n", fp);
    fclose(fp);
    fp = fopen(entry_path, "w");
    fputs("#program\nimport Prog\n", fp);
    fclose(fp);

    tc_diagnostic_init(&diag);
    check(tc_compile_file(entry_path, &out, &diag) == -1, "import #program fails");
    check(diag.kind == TC_CE_IMPORT_NOT_LIB, "import not lib kind");
    tc_diagnostic_clear(&diag);

    unlink(lib_path);
    unlink(entry_path);
    rmdir(dir);
}

static void test_duplicate_import(void) {
    char dir_template[] = "/tmp/tc-mod-dup-XXXXXX";
    char *dir = mkdtemp(dir_template);
    char lib_path[256];
    char entry_path[256];
    FILE *fp = NULL;
    TcTypedProgram out;
    TcDiagnostic diag;

    check(dir != NULL, "create dup-import dir");
    if (!dir) {
        return;
    }
    snprintf(lib_path, sizeof(lib_path), "%s/Once.tc", dir);
    snprintf(entry_path, sizeof(entry_path), "%s/entry.tc", dir);
    fp = fopen(lib_path, "w");
    fputs("#lib\npublic static let K: int32 = 1\n", fp);
    fclose(fp);
    fp = fopen(entry_path, "w");
    fputs("#program\nimport Once\nimport Once\n", fp);
    fclose(fp);

    tc_diagnostic_init(&diag);
    check(tc_compile_file(entry_path, &out, &diag) == -1, "duplicate import fails");
    check(diag.kind == TC_CE_DUPLICATE_IMPORT, "duplicate import kind");
    tc_diagnostic_clear(&diag);

    unlink(lib_path);
    unlink(entry_path);
    rmdir(dir);
}

static void test_ambiguous_import_search_paths(void) {
    char dir_a_t[] = "/tmp/tc-mod-amb-a-XXXXXX";
    char dir_b_t[] = "/tmp/tc-mod-amb-b-XXXXXX";
    char entry_t[] = "/tmp/tc-mod-amb-e-XXXXXX";
    char *dir_a = mkdtemp(dir_a_t);
    char *dir_b = mkdtemp(dir_b_t);
    char *entry_dir = mkdtemp(entry_t);
    char path_a[256];
    char path_b[256];
    char entry_path[256];
    char *paths[2];
    FILE *fp = NULL;
    TcTypedProgram out;
    TcDiagnostic diag;

    check(dir_a && dir_b && entry_dir, "create ambiguous search dirs");
    if (!dir_a || !dir_b || !entry_dir) {
        return;
    }
    snprintf(path_a, sizeof(path_a), "%s/Same.tc", dir_a);
    snprintf(path_b, sizeof(path_b), "%s/Same.tc", dir_b);
    snprintf(entry_path, sizeof(entry_path), "%s/entry.tc", entry_dir);

    fp = fopen(path_a, "w");
    fputs("#lib\npublic static let A: int32 = 1\n", fp);
    fclose(fp);
    fp = fopen(path_b, "w");
    fputs("#lib\npublic static let B: int32 = 2\n", fp);
    fclose(fp);
    fp = fopen(entry_path, "w");
    fputs("#program\nimport Same\n", fp);
    fclose(fp);

    paths[0] = dir_a;
    paths[1] = dir_b;
    tc_diagnostic_init(&diag);
    check(tc_set_module_search_paths(paths, 2, &diag) == 0, "set ambiguous search paths");
    check(tc_compile_file(entry_path, &out, &diag) == -1, "ambiguous import fails");
    check(diag.kind == TC_CE_IMPORT_AMBIGUOUS, "ambiguous import kind");
    {
        TcDiagnostic clear_diag;
        tc_diagnostic_init(&clear_diag);
        check(tc_set_module_search_paths(NULL, 0, &clear_diag) == 0, "clear search paths");
        tc_diagnostic_clear(&clear_diag);
    }
    tc_diagnostic_clear(&diag);

    unlink(path_a);
    unlink(path_b);
    unlink(entry_path);
    rmdir(dir_a);
    rmdir(dir_b);
    rmdir(entry_dir);
}

static void test_self_import_file(void) {
    char dir_template[] = "/tmp/tc-mod-self-XXXXXX";
    char *dir = mkdtemp(dir_template);
    char path[256];
    FILE *fp = NULL;
    TcTypedProgram out;
    TcDiagnostic diag;

    check(dir != NULL, "create self-import dir");
    if (!dir) {
        return;
    }
    snprintf(path, sizeof(path), "%s/SelfMod.tc", dir);
    fp = fopen(path, "w");
    fputs("#lib\nimport SelfMod\npublic func f ( ) void then\nend\n", fp);
    fclose(fp);

    tc_diagnostic_init(&diag);
    check(tc_compile_file(path, &out, &diag) == -1, "self-import fails");
    check(diag.kind == TC_CE_CIRCULAR_IMPORT, "self-import is circular");
    tc_diagnostic_clear(&diag);

    unlink(path);
    rmdir(dir);
}

static void test_lib_missing_visibility_structure(void) {
    TcProgram program;
    TcDiagnostic diag;
    const char *source = "#lib\nstruct Bad then\nend\n";

    tc_diagnostic_init(&diag);
    tc_program_init(&program);
    check(tc_parse_source_to_program(source, &program, &diag) != 0,
          "parser rejects lib without visibility");
    check(diag.kind == TC_CE_MISSING_VISIBILITY, "missing visibility from parser");
    tc_program_free(&program);
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_module_check_structure_program_ok();
    test_module_check_self_in_program();
    test_member_index_build();
    test_lib_missing_visibility_structure();
    test_import_not_found_via_compile_file();
    test_import_success_and_signatures();
    test_circular_import_pair();
    test_import_not_lib();
    test_duplicate_import();
    test_ambiguous_import_search_paths();
    test_self_import_file();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
