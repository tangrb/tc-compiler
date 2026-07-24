/* test_diagnostic.c — diagnostic domain and ownership contracts */
#include "tc_diagnostic.h"
#include "tc_types.h"

#include <stdio.h>
#include <string.h>

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

static int print_contains(const TcDiagnostic *diag, const char *needle) {
    FILE *stream = tmpfile();
    char buffer[512];
    size_t count = 0;

    if (!stream) {
        return 0;
    }
    tc_diagnostic_print(diag, stream);
    rewind(stream);
    count = fread(buffer, 1, sizeof(buffer) - 1, stream);
    buffer[count] = '\0';
    fclose(stream);
    return strstr(buffer, needle) != NULL;
}

static void test_domain_lifecycle(void) {
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    check(diag.domain == TC_DIAG_NONE, "init domain is none");
    check(diag.api_code == TC_API_ERR_NONE, "init api code is none");

    tc_diagnostic_set(&diag, TC_CE_TYPE_MISMATCH, 2, 3, "bad type");
    check(diag.domain == TC_DIAG_LANGUAGE, "language setter selects language domain");
    check(diag.api_code == TC_API_ERR_NONE, "language setter clears api code");
    check(print_contains(&diag, ": error: bad type"), "language print remains compatible");

    tc_diagnostic_set_api(&diag, TC_API_ERR_FILE_OPEN, "cannot open source");
    check(diag.domain == TC_DIAG_API, "api setter selects api domain");
    check(diag.api_code == TC_API_ERR_FILE_OPEN, "api setter stores code");
    check(print_contains(&diag, ": api error: FileOpen: cannot open source"),
          "api print includes domain and code");

    tc_diagnostic_set_api(&diag, TC_API_ERR_INVALID_ARGUMENT, "invalid output pointer");
    check(diag.domain == TC_DIAG_API, "invalid argument remains in api domain");
    check(diag.api_code == TC_API_ERR_INVALID_ARGUMENT,
          "api setter stores InvalidArgument code");
    check(print_contains(&diag, ": api error: InvalidArgument: invalid output pointer"),
          "invalid argument print includes api code");

    tc_diagnostic_set_api(&diag, TC_API_ERR_FILE_READ, "cannot read source");
    check(diag.api_code == TC_API_ERR_FILE_READ, "api setter stores FileRead code");
    check(print_contains(&diag, ": api error: FileRead: cannot read source"),
          "file read print includes api code");

    tc_diagnostic_set(&diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                      "memory allocation failed");
    check(diag.domain == TC_DIAG_IMPLEMENTATION, "oom selects implementation domain");
    check(print_contains(&diag, ": implementation error: OutOfMemory: memory allocation failed"),
          "implementation print includes domain and code");

    tc_diagnostic_clear(&diag);
    check(diag.domain == TC_DIAG_NONE, "clear resets domain");
    check(diag.api_code == TC_API_ERR_NONE, "clear resets api code");
    check(diag.message == NULL && diag.snippet == NULL, "clear releases owned text");
}

static void test_allocation_failures_become_oom(void) {
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    tc_diagnostic_test_fail_alloc_after(0);
    tc_diagnostic_set_source(&diag, "input.tc", "#program\nvar x: int32 = 1\n");
    check(diag.domain == TC_DIAG_IMPLEMENTATION && diag.kind == TC_ERR_OUT_OF_MEMORY,
          "source capture allocation failure becomes implementation OOM");
    check(diag.message != NULL && strcmp(diag.message, "memory allocation failed") == 0,
          "OOM keeps the fixed message when diagnostic allocation also fails");
    check(diag.filename == NULL && diag.source == NULL,
          "failed source capture does not publish partial ownership");
    tc_diagnostic_test_fail_alloc_after(-1);
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    tc_diagnostic_test_fail_alloc_after(0);
    tc_diagnostic_set(&diag, TC_CE_TYPE_MISMATCH, 1, 1, "bad type");
    check(diag.domain == TC_DIAG_IMPLEMENTATION && diag.kind == TC_ERR_OUT_OF_MEMORY,
          "language message allocation failure becomes implementation OOM");
    tc_diagnostic_test_fail_alloc_after(-1);
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    tc_diagnostic_set_source(&diag, "input.tc", "#program\nvar x: int32 = true\n");
    tc_diagnostic_test_fail_alloc_after(1);
    tc_diagnostic_set(&diag, TC_CE_TYPE_MISMATCH, 1, 1, "bad type");
    check(diag.domain == TC_DIAG_IMPLEMENTATION && diag.kind == TC_ERR_OUT_OF_MEMORY,
          "snippet allocation failure becomes implementation OOM");
    tc_diagnostic_test_fail_alloc_after(-1);
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    tc_diagnostic_test_fail_alloc_after(0);
    tc_diagnostic_set_api(&diag, TC_API_ERR_FILE_OPEN, "cannot open source");
    check(diag.domain == TC_DIAG_IMPLEMENTATION && diag.kind == TC_ERR_OUT_OF_MEMORY,
          "API message allocation failure becomes implementation OOM");
    tc_diagnostic_test_fail_alloc_after(-1);
    tc_diagnostic_clear(&diag);
}

static void test_oom_not_used_as_size_limit_proxy(void) {
    /* D-15：OutOfMemory 仅表示真实分配失败；打印名/消息固定，且不得
     * 被误当成「规模限制」类语言错误的别名。公开规模上限（如 CLI -I）
     * 由 CLI 测试断言使用非 OutOfMemory 文案。 */
    TcDiagnostic diag;

    check(strcmp(tc_error_kind_name(TC_ERR_OUT_OF_MEMORY), "OutOfMemory") == 0,
          "D-15 OOM print name is OutOfMemory");
    check(strcmp(tc_error_kind_name(TC_CE_SYNTAX), "SyntaxError") == 0,
          "D-15 language size/form errors keep distinct names");
    check(strcmp(tc_error_kind_name(TC_CE_FORMAT_SPECIFIER), "FormatSpecifierError") == 0,
          "D-15 format limits use FormatSpecifierError not OutOfMemory");
    check(strcmp(tc_error_kind_name(TC_CE_LITERAL_OUT_OF_RANGE), "LiteralOutOfRange") == 0,
          "D-15 literal limits use LiteralOutOfRange not OutOfMemory");

    tc_diagnostic_init(&diag);
    tc_diagnostic_set(&diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                      "memory allocation failed");
    check(diag.domain == TC_DIAG_IMPLEMENTATION, "D-15 OOM stays implementation domain");
    check(diag.kind == TC_ERR_OUT_OF_MEMORY, "D-15 OOM kind is TC_ERR_OUT_OF_MEMORY");
    check(diag.message != NULL && strcmp(diag.message, "memory allocation failed") == 0,
          "D-15 OOM message is fixed to memory allocation failed");
    check(!print_contains(&diag, "too many") && !print_contains(&diag, "limit"),
          "D-15 OOM message is not a size-limit phrase");
    tc_diagnostic_clear(&diag);

    tc_diagnostic_init(&diag);
    tc_diagnostic_set(&diag, TC_CE_SYNTAX, 1, 1, "unexpected token");
    check(diag.domain == TC_DIAG_LANGUAGE && diag.kind != TC_ERR_OUT_OF_MEMORY,
          "D-15 syntax rejection is language domain not OOM");
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_domain_lifecycle();
    test_allocation_failures_become_oom();
    test_oom_not_used_as_size_limit_proxy();
    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
