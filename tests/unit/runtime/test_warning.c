/*
 * test_warning.c — 编译警告列表单元测试
 *
 * 覆盖 tc_warning_list 的初始化、添加、扩容、NULL 消息、格式化输出和释放。
 * 还包括 tc_warning_kind_name 的验证。
 */
#include "tc_warning.h"

#include <stdio.h>
#include <stdlib.h>
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

/* ================================================================== */
/*  tc_warning_list_init / free                                        */
/* ================================================================== */

static void test_warning_list_init_free(void) {
    TcWarningList list;

    tc_warning_list_init(&list);
    check(list.items == NULL, "init: items == NULL");
    check(list.count == 0, "init: count == 0");
    check(list.capacity == 0, "init: capacity == 0");

    /* 空列表 free 不应 crash */
    tc_warning_list_free(&list);
    check(list.items == NULL, "free: items == NULL");
    check(list.count == 0, "free: count == 0");
    check(list.capacity == 0, "free: capacity == 0");
}

/* ================================================================== */
/*  tc_warning_list_add — 基本添加                                     */
/* ================================================================== */

static void test_warning_list_add_basic(void) {
    TcWarningList list;
    int rc;

    tc_warning_list_init(&list);

    rc = tc_warning_list_add(&list, TC_WARN_NONE, 5, "uninitialized var 'x'");
    check(rc == 0, "add one warning returns 0");
    check(list.count == 1, "add one: count == 1");
    check(list.capacity == 8, "add one: capacity == 8 (initial)");
    check(list.items[0].kind == TC_WARN_NONE, "add one: kind == NONE");
    check(list.items[0].line == 5, "add one: line == 5");
    check(strcmp(list.items[0].message, "uninitialized var 'x'") == 0,
          "add one: message matches");

    tc_warning_list_free(&list);
}

/* ================================================================== */
/*  tc_warning_list_add — 触发扩容                                     */
/* ================================================================== */

static void test_warning_list_add_capacity_growth(void) {
    TcWarningList list;
    int rc;
    size_t i;

    tc_warning_list_init(&list);

    /* 添加 9 条，触发 8 → 16 扩容 */
    for (i = 0; i < 9; i++) {
        rc = tc_warning_list_add(&list, TC_WARN_NONE, (int)i, "warn");
        check(rc == 0, "add warning during growth");
    }
    check(list.count == 9, "growth: count == 9");
    check(list.capacity == 16, "growth: capacity == 16 (8*2)");

    /* 添加至 17 条，触发 16 → 32 扩容 */
    for (i = 9; i < 17; i++) {
        rc = tc_warning_list_add(&list, TC_WARN_NONE, (int)i, "warn");
        check(rc == 0, "add warning during second growth");
    }
    check(list.count == 17, "growth: count == 17");
    check(list.capacity == 32, "growth: capacity == 32 (16*2)");

    /* 验证所有行号正确 */
    for (i = 0; i < 17; i++) {
        check(list.items[i].line == (int)i, "growth: line number preserved");
    }

    tc_warning_list_free(&list);
}

/* ================================================================== */
/*  tc_warning_list_add — NULL 消息                                    */
/* ================================================================== */

static void test_warning_list_add_null_message(void) {
    TcWarningList list;
    int rc;

    tc_warning_list_init(&list);

    rc = tc_warning_list_add(&list, TC_WARN_NONE, 1, NULL);
    check(rc == 0, "add with NULL message returns 0");
    check(list.count == 1, "add NULL msg: count == 1");
    check(list.items[0].message == NULL, "add NULL msg: message == NULL");

    tc_warning_list_free(&list);
}

/* ================================================================== */
/*  tc_warning_list_print — 输出验证                                   */
/* ================================================================== */

static void test_warning_list_print(void) {
    TcWarningList list;
    FILE *f;
    char buf[256];
    size_t nread;

    tc_warning_list_init(&list);

    (void)tc_warning_list_add(&list, TC_WARN_NONE, 3, "test message");
    (void)tc_warning_list_add(&list, TC_WARN_NONE, 0, "no line");
    (void)tc_warning_list_add(&list, TC_WARN_NONE, 5, NULL);

    /* 输出到临时文件验证格式 */
    f = tmpfile();
    check(f != NULL, "print: tmpfile success");
    if (f) {
        tc_warning_list_print(&list, f);
        rewind(f);
        nread = fread(buf, 1, sizeof(buf) - 1, f);
        buf[nread] = '\0';
        fclose(f);
        check(strstr(buf, "warning: test message (line 3)") != NULL,
              "print: line message format");
        check(strstr(buf, "warning: no line") != NULL,
              "print: no line format");
        check(strstr(buf, "warning:  (line 5)") != NULL,
              "print: NULL message with line");
    }

    tc_warning_list_free(&list);
}

/* ================================================================== */
/*  tc_warning_kind_name — 名称映射                                    */
/* ================================================================== */

static void test_warning_kind_name(void) {
    check(strcmp(tc_warning_kind_name(TC_WARN_NONE),
                 "None") == 0,
          "TC_WARN_NONE → None");
    check(strcmp(tc_warning_kind_name((TcWarningKind)999), "UnknownWarning") == 0,
          "unknown warning kind → UnknownWarning");
}

/* ================================================================== */
/*  main                                                                */
/* ================================================================== */

int main(void) {
    test_warning_list_init_free();
    test_warning_list_add_basic();
    test_warning_list_add_capacity_growth();
    test_warning_list_add_null_message();
    test_warning_list_print();
    test_warning_kind_name();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
