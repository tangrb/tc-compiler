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

/*
 * 语言标准 §11「阶段优先」：CT 类诊断须挂起，待全部 SEM 类
 * 诊断无触发后再发布。本组断言覆盖挂起槽的生命周期、首位置选取与发布规则。
 */
static void test_deferred_ct_diagnostic(void) {
    TcDiagnostic diag;

    /* 挂起不写入主槽，但可查询；位置更靠后的挂起不覆盖更靠前的 */
    tc_diagnostic_init(&diag);
    check(!tc_diagnostic_is_set(&diag), "init leaves no active diagnostic");
    check(!tc_diagnostic_has_deferred(&diag), "init leaves no deferred diagnostic");
    check(tc_diagnostic_defer(&diag, TC_CE_CONSTANT_DIV_ZERO, 8, TC_COLUMN_UNKNOWN,
                              "constant division by zero") == -1,
          "defer reports failure to the caller");
    check(!tc_diagnostic_is_set(&diag), "deferred diagnostic does not set the main slot");
    check(tc_diagnostic_has_deferred(&diag), "deferred diagnostic is queryable");
    check(tc_diagnostic_defer(&diag, TC_CE_CONSTANT_OVERFLOW, 12, TC_COLUMN_UNKNOWN,
                              "constant overflow") == -1,
          "later deferral still reports failure");
    check(tc_diagnostic_flush_deferred(&diag) == 1, "flush publishes the deferred diagnostic");
    check(diag.kind == TC_CE_CONSTANT_DIV_ZERO, "flush keeps the earliest deferred kind");
    check(diag.line == 8, "flush keeps the earliest deferred line");
    check(print_contains(&diag, "constant division by zero"),
          "flush publishes the earliest deferred message");
    check(!tc_diagnostic_has_deferred(&diag), "flush clears the deferred slot");
    tc_diagnostic_clear(&diag);

    /* 同行按列序 */
    tc_diagnostic_init(&diag);
    (void)tc_diagnostic_defer(&diag, TC_CE_CONSTANT_OVERFLOW, 5, 30, "second column");
    (void)tc_diagnostic_defer(&diag, TC_CE_CONSTANT_DIV_ZERO, 5, 10, "first column");
    check(tc_diagnostic_flush_deferred(&diag) == 1, "same-line flush publishes");
    check(print_contains(&diag, "first column"), "same-line ordering uses the column");
    tc_diagnostic_clear(&diag);

    /* 已有 SEM 类诊断时丢弃挂起的 CT 类诊断（阶段优先：SEM 胜出） */
    tc_diagnostic_init(&diag);
    (void)tc_diagnostic_defer(&diag, TC_CE_CONSTANT_DIV_ZERO, 3, TC_COLUMN_UNKNOWN,
                              "constant division by zero");
    tc_diagnostic_set(&diag, TC_CE_UNINITIALIZED_VARIABLE, 9, TC_COLUMN_UNKNOWN,
                      "use of uninitialized variable");
    check(tc_diagnostic_flush_deferred(&diag) == 0, "flush is a no-op when a real diagnostic exists");
    check(diag.kind == TC_CE_UNINITIALIZED_VARIABLE, "SEM diagnostic is preserved");
    check(!tc_diagnostic_has_deferred(&diag), "superseded deferred diagnostic is dropped");
    tc_diagnostic_clear(&diag);

    /* 无挂起诊断时 flush 为空操作 */
    tc_diagnostic_init(&diag);
    check(tc_diagnostic_flush_deferred(&diag) == 0, "flush without deferral is a no-op");
    check(!tc_diagnostic_is_set(&diag), "empty flush leaves the main slot unset");
    tc_diagnostic_clear(&diag);
}

int main(void) {
    test_domain_lifecycle();
    test_allocation_failures_become_oom();
    test_oom_not_used_as_size_limit_proxy();
    test_deferred_ct_diagnostic();
    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
