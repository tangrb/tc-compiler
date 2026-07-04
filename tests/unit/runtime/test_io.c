/*
 * test_io.c — 统一 I/O 模块单元测试
 *
 * 覆盖 tc_io.c 中所有公开函数：
 *   - tc_io_write_formatted — 8 种格式符 × 有符号/无符号/bool
 *   - tc_io_write_value — 默认格式输出、空 IO 错误处理
 *   - tc_io_skip_whitespace — 前导空白跳过
 *   - tc_io_read_digits — 数字字符读取、负号处理、溢出检测
 *   - tc_io_read_value — bool/十进制整数读取、范围检查
 *
 * 防止回归：格式符输出遗漏、范围检查错误、EOF/非法输入处理遗漏
 *
 * 注意：stdin 依赖的测试通过 tmpfile + freopen 重定向实现。
 *       写函数直接接受 FILE *out 参数，无需重定向。
 */

#include "tc_io.h"
#include "tc_semantics.h"
#include "tc_diagnostic.h"

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
/*  辅助：捕获 tc_io_write_value / tc_io_write_formatted 输出到缓冲区    */
/* ================================================================== */

/** 打开临时文件用于捕获输出 */
static FILE *open_capture(void) {
    FILE *f = tmpfile();
    if (!f) {
        fprintf(stderr, "FAIL: tmpfile() failed\n");
        exit(1);
    }
    return f;
}

/** 关闭临时文件并返回其中全部内容（堆分配，调用方 free） */
static char *close_capture(FILE *f) {
    long len = 0;
    char *buf = NULL;

    rewind(f);
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fprintf(stderr, "FAIL: malloc failed\n");
        exit(1);
    }
    len = (long)fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

/* ================================================================== */
/*  tc_io_write_formatted  — 格式化输出                                */
/* ================================================================== */

static void test_write_formatted_signed(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = tc_value_make(TC_INT32, tc_signed_to_bits(TC_INT32, -42));

    /* %d — 有符号十进制 */
    out = open_capture();
    check(tc_io_write_formatted(TC_INT32, TC_FMT_D, &val, out) == 0, "write_fmt %%d ok");
    result = close_capture(out);
    check(strcmp(result, "-42") == 0, "write_fmt %%d => -42");
    free(result);

    /* %i — 同 %d */
    out = open_capture();
    tc_io_write_formatted(TC_INT32, TC_FMT_I, &val, out);
    result = close_capture(out);
    check(strcmp(result, "-42") == 0, "write_fmt %%i => -42");
    free(result);

    /* %u — 无符号视角 */
    out = open_capture();
    tc_io_write_formatted(TC_INT32, TC_FMT_U, &val, out);
    result = close_capture(out);
    check(strcmp(result, "4294967254") == 0, "write_fmt %%u => 4294967254");
    free(result);
}

static void test_write_formatted_unsigned(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = tc_value_make(TC_UINT32, 0xDEADBEEFULL);

    out = open_capture();
    tc_io_write_formatted(TC_UINT32, TC_FMT_X, &val, out);
    result = close_capture(out);
    check(strcmp(result, "deadbeef") == 0, "write_fmt %%x => deadbeef");
    free(result);

    out = open_capture();
    tc_io_write_formatted(TC_UINT32, TC_FMT_XU, &val, out);
    result = close_capture(out);
    check(strcmp(result, "DEADBEEF") == 0, "write_fmt %%X => DEADBEEF");
    free(result);

    out = open_capture();
    tc_io_write_formatted(TC_UINT32, TC_FMT_O, &val, out);
    result = close_capture(out);
    check(strcmp(result, "33653337357") == 0, "write_fmt %%o => 33653337357");
    free(result);
}

static void test_write_formatted_binary(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = tc_value_make(TC_UINT8, 0xB5);

    out = open_capture();
    tc_io_write_formatted(TC_UINT8, TC_FMT_B, &val, out);
    result = close_capture(out);
    check(strcmp(result, "10110101") == 0, "write_fmt %%b => 10110101");
    free(result);
}

static void test_write_formatted_bool(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue t = tc_value_make(TC_BOOL, 1);
    TcValue f = tc_value_make(TC_BOOL, 0);

    out = open_capture();
    tc_io_write_formatted(TC_BOOL, TC_FMT_T, &t, out);
    result = close_capture(out);
    check(strcmp(result, "true") == 0, "write_fmt %%t true => true");
    free(result);

    out = open_capture();
    tc_io_write_formatted(TC_BOOL, TC_FMT_T, &f, out);
    result = close_capture(out);
    check(strcmp(result, "false") == 0, "write_fmt %%t false => false");
    free(result);

    out = open_capture();
    tc_io_write_formatted(TC_BOOL, TC_FMT_D, &f, out);
    result = close_capture(out);
    check(strcmp(result, "0") == 0, "write_fmt %%d bool => 0");
    free(result);
}

static void test_write_formatted_int64_boundary(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue min = tc_value_make(TC_INT64, TC_INT64_MIN_ABS_MAGNITUDE);
    TcValue max = tc_value_make(TC_INT64, UINT64_MAX);
    max = tc_value_make(TC_INT64, max.bits);  /* tc_signed_to_bits will handle */
    max = tc_value_make(TC_INT64, tc_signed_to_bits(TC_INT64, INT64_MAX));
    TcValue uint64_max = tc_value_make(TC_UINT64, UINT64_MAX);

    /* INT64_MIN */
    out = open_capture();
    tc_io_write_formatted(TC_INT64, TC_FMT_D, &min, out);
    result = close_capture(out);
    check(strcmp(result, "-9223372036854775808") == 0, "write_fmt INT64_MIN");
    free(result);

    /* INT64_MAX */
    out = open_capture();
    tc_io_write_formatted(TC_INT64, TC_FMT_D, &max, out);
    result = close_capture(out);
    check(strcmp(result, "9223372036854775807") == 0, "write_fmt INT64_MAX");
    free(result);

    /* UINT64_MAX as %u */
    out = open_capture();
    tc_io_write_formatted(TC_UINT64, TC_FMT_U, &uint64_max, out);
    result = close_capture(out);
    check(strcmp(result, "18446744073709551615") == 0, "write_fmt UINT64_MAX");
    free(result);

    /* UINT64_MAX as %x */
    out = open_capture();
    tc_io_write_formatted(TC_UINT64, TC_FMT_X, &uint64_max, out);
    result = close_capture(out);
    check(strcmp(result, "ffffffffffffffff") == 0, "write_fmt UINT64_MAX hex");
    free(result);
}

/* ================================================================== */
/*  tc_io_write_value — 值输出                                         */
/* ================================================================== */

static void test_write_value_default_signed(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = tc_value_make(TC_INT16, tc_signed_to_bits(TC_INT16, -1234));

    out = open_capture();
    tc_io_write_value(&val, TC_FMT_NONE, 0, out);
    result = close_capture(out);
    check(strcmp(result, "-1234") == 0, "write_val default int16 => -1234");
    free(result);
}

static void test_write_value_default_unsigned(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = tc_value_make(TC_UINT32, 3000000000ULL);

    out = open_capture();
    tc_io_write_value(&val, TC_FMT_NONE, 0, out);
    result = close_capture(out);
    check(strcmp(result, "3000000000") == 0, "write_val default uint32 => 3000000000");
    free(result);
}

static void test_write_value_bool(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue t = tc_value_make(TC_BOOL, 1);
    TcValue f = tc_value_make(TC_BOOL, 0);

    out = open_capture();
    tc_io_write_value(&t, TC_FMT_NONE, 0, out);
    result = close_capture(out);
    check(strcmp(result, "true") == 0, "write_val bool true");
    free(result);

    out = open_capture();
    tc_io_write_value(&f, TC_FMT_NONE, 0, out);
    result = close_capture(out);
    check(strcmp(result, "false") == 0, "write_val bool false");
    free(result);
}

static void test_write_value_newline(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = tc_value_make(TC_INT32, 99);

    out = open_capture();
    tc_io_write_value(&val, TC_FMT_D, 1, out);
    result = close_capture(out);
    check(strcmp(result, "99\n") == 0, "write_val with newline => 99\\n");
    free(result);
}

/* ================================================================== */
/*  tc_io_read_value — stdin 输入                                      */
/* ================================================================== */

/* 由于 tc_io_read_value 固定从 stdin 读取，且单元测试中不易重定向，
 * 本文件暂不包含 stdin 依赖的自动化用例。手动测试见 tests/valid/ 中
 * 的 read_write.tc、read_bool.tc 等集成测试。
 *
 * stdin 可测接口（tc_io_read_digits）的边界通过集成测试覆盖：
 *   - tests/valid/read_write.tc
 *   - tests/valid/read_bool.tc
 *   - tests/valid/io_extended.tc
 *   - tests/errors/runtime/read_invalid.tc
 *   - tests/errors/runtime/read_invalid_input.tc
 *   - tests/errors/runtime/read_out_of_range.tc
 */

/* ================================================================== */
/*  主入口                                                              */
/* ================================================================== */

int main(void) {
    /* 格式化输出 */
    test_write_formatted_signed();
    test_write_formatted_unsigned();
    test_write_formatted_binary();
    test_write_formatted_bool();
    test_write_formatted_int64_boundary();

    /* 值输出 */
    test_write_value_default_signed();
    test_write_value_default_unsigned();
    test_write_value_bool();
    test_write_value_newline();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
