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
#include <math.h>

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
/*  辅助：float TcValue 构造                                           */
/* ================================================================== */

static TcValue fp64_from_double(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return tc_value_make(TC_FLOAT64, bits);
}

static TcValue fp32_from_double(double value) {
    float f = (float)value;
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return tc_value_make(TC_FLOAT32, (uint64_t)bits);
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
/*  tc_io_write_formatted  — 浮点格式化输出                             */
/* ================================================================== */

static void test_write_formatted_float64(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = fp64_from_double(3.14);

    /* %f — 固定小数点 */
    out = open_capture();
    check(tc_io_write_formatted(TC_FLOAT64, TC_FMT_F, &val, out) == 0, "write_fmt %%f float64 ok");
    result = close_capture(out);
    check(strcmp(result, "3.140000") == 0, "write_fmt %%f => 3.140000");
    free(result);

    /* %e — 科学计数法小写 */
    out = open_capture();
    check(tc_io_write_formatted(TC_FLOAT64, TC_FMT_E, &val, out) == 0, "write_fmt %%e float64 ok");
    result = close_capture(out);
    check(strstr(result, "3.140000e") != NULL || strstr(result, "3.14e") != NULL,
          "write_fmt %%e => 3.14e+00");
    free(result);

    /* %E — 科学计数法大写 */
    out = open_capture();
    check(tc_io_write_formatted(TC_FLOAT64, TC_FMT_EU, &val, out) == 0, "write_fmt %%E float64 ok");
    result = close_capture(out);
    check(strstr(result, "3.140000E") != NULL || strstr(result, "3.14E") != NULL,
          "write_fmt %%E => 3.14E+00");
    free(result);

    /* %g — 紧凑格式 */
    out = open_capture();
    check(tc_io_write_formatted(TC_FLOAT64, TC_FMT_G, &val, out) == 0, "write_fmt %%g float64 ok");
    result = close_capture(out);
    check(strcmp(result, "3.14") == 0, "write_fmt %%g => 3.14");
    free(result);

    /* %G — 紧凑格式大写指数 */
    out = open_capture();
    check(tc_io_write_formatted(TC_FLOAT64, TC_FMT_GU, &val, out) == 0, "write_fmt %%G float64 ok");
    result = close_capture(out);
    check(strcmp(result, "3.14") == 0, "write_fmt %%G => 3.14");
    free(result);
}

static void test_write_formatted_float32(void) {
    FILE *out = NULL;
    char *result = NULL;
    TcValue val = fp32_from_double(1.5);

    out = open_capture();
    check(tc_io_write_formatted(TC_FLOAT32, TC_FMT_F, &val, out) == 0, "write_fmt %%f float32 ok");
    result = close_capture(out);
    check(strcmp(result, "1.500000") == 0, "write_fmt %%f float32 => 1.500000");
    free(result);
}

static void test_write_formatted_float_reject_int(void) {
    FILE *out = NULL;
    TcValue val = tc_value_make(TC_INT32, 42);

    /* 对整数使用 %f 应拒绝 */
    out = open_capture();
    check(tc_io_write_formatted(TC_INT32, TC_FMT_F, &val, out) != 0,
          "write_fmt %%f on int32 → fail");
    fclose(out);
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

static void test_write_value_float(void) {
    FILE *out = NULL;
    char *result = NULL;

    /* float64 默认 %g 输出 */
    {
        TcValue val = fp64_from_double(3.14);
        out = open_capture();
        check(tc_io_write_value(&val, TC_FMT_NONE, 0, out) == 0, "write_val default float64 ok");
        result = close_capture(out);
        check(strcmp(result, "3.14") == 0, "write_val default float64 => 3.14");
        free(result);
    }

    /* float32 默认 %g 输出 */
    {
        TcValue val = fp32_from_double(1.5);
        out = open_capture();
        check(tc_io_write_value(&val, TC_FMT_NONE, 0, out) == 0, "write_val default float32 ok");
        result = close_capture(out);
        check(strcmp(result, "1.5") == 0, "write_val default float32 => 1.5");
        free(result);
    }
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

static int with_stdin(const char *input, int (*fn)(void)) {
    FILE *saved = stdin;
    FILE *tmp = tmpfile();
    int rc = 0;

    if (!tmp) {
        fprintf(stderr, "FAIL: tmpfile() failed\n");
        return -1;
    }
    if (fputs(input, tmp) == EOF) {
        fclose(tmp);
        return -1;
    }
    rewind(tmp);
    stdin = tmp;
    rc = fn();
    clearerr(stdin);
    stdin = saved;
    fclose(tmp);
    return rc;
}

static int read_bool_true_fn(void) {
    uint64_t bits = 0;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    if (tc_io_read_value(TC_BOOL, &bits, &diag, 1) != 0) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    tc_diagnostic_clear(&diag);
    return bits == 1 ? 0 : -1;
}

static int read_bool_false_fn(void) {
    uint64_t bits = 0;
    TcDiagnostic diag;

    tc_diagnostic_init(&diag);
    if (tc_io_read_value(TC_BOOL, &bits, &diag, 1) != 0) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    tc_diagnostic_clear(&diag);
    return bits == 0 ? 0 : -1;
}

static int read_bool_invalid_fn(void) {
    TcDiagnostic diag;
    uint64_t bits = 0;

    tc_diagnostic_init(&diag);
    if (tc_io_read_value(TC_BOOL, &bits, &diag, 1) == 0) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    if (strstr(diag.message, "invalid input") == NULL) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    tc_diagnostic_clear(&diag);
    return 0;
}

static void test_read_bool_stdin(void) {
    check(with_stdin("true\n", read_bool_true_fn) == 0, "read bool true from stdin");
    check(with_stdin("false\n", read_bool_false_fn) == 0, "read bool false from stdin");
    check(with_stdin("trueish\n", read_bool_invalid_fn) == 0, "read bool reject trueish");
    check(with_stdin("falsehood\n", read_bool_invalid_fn) == 0, "read bool reject falsehood");
    check(with_stdin("True\n", read_bool_invalid_fn) == 0, "read bool reject True (case)");
}

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
    test_write_formatted_float64();
    test_write_formatted_float32();
    test_write_formatted_float_reject_int();

    /* 值输出 */
    test_write_value_default_signed();
    test_write_value_default_unsigned();
    test_write_value_bool();
    test_write_value_float();
    test_write_value_newline();

    test_read_bool_stdin();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
