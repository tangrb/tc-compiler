/*
 * test_io.c — 统一 I/O 模块单元测试
 *
 * 覆盖 tc_io.c 中所有公开函数：
 *   - tc_io_write_formatted — 13 种格式符 × 整数/bool/float32/float64
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

#ifdef TC_HAVE_FENV
#include <fenv.h>
#endif
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

typedef struct {
    const char *name;
    TcType type;
    TcFormatSpec fmt;
    uint64_t bits;
    const char *expected;
} FormatCase;

static void test_all_format_specifiers_table(void) {
    const TcValue pi64 = fp64_from_double(3.14159265);
    static const FormatCase integer_cases[] = {
        {"%d", TC_INT32, TC_FMT_D, UINT64_C(0xFFFFFFD6), "-42"},
        {"%i", TC_INT32, TC_FMT_I, UINT64_C(0xFFFFFFD6), "-42"},
        {"%u", TC_UINT32, TC_FMT_U, UINT64_C(42), "42"},
        {"%x", TC_INT8, TC_FMT_X, UINT64_C(0xFF), "ff"},
        {"%X", TC_INT8, TC_FMT_XU, UINT64_C(0xFF), "FF"},
        {"%o", TC_INT8, TC_FMT_O, UINT64_C(0xFF), "377"},
        {"%b", TC_UINT8, TC_FMT_B, UINT64_C(5), "101"},
        {"%t", TC_BOOL, TC_FMT_T, UINT64_C(1), "true"},
    };
    static const TcFormatSpec float_formats[] = {
        TC_FMT_F, TC_FMT_E, TC_FMT_EU, TC_FMT_G, TC_FMT_GU,
    };
    static const char *float_names[] = {"%f", "%e", "%E", "%g", "%G"};
    static const char *float64_expected[] = {
        "3.141593", "3.141593e+00", "3.141593E+00", "3.14159", "3.14159",
    };
    static const char *float32_expected[] = {
        "1.500000", "1.500000e+00", "1.500000E+00", "1.5", "1.5",
    };
    size_t i = 0;

    for (i = 0; i < sizeof(integer_cases) / sizeof(integer_cases[0]); i++) {
        const FormatCase *test = &integer_cases[i];
        TcValue value = tc_value_make(test->type, test->bits);
        FILE *out = open_capture();
        char *actual = NULL;
        char message[96];

        snprintf(message, sizeof(message), "format table %s returns success", test->name);
        check(tc_io_write_formatted(test->type, test->fmt, &value, out) == 0, message);
        actual = close_capture(out);
        snprintf(message, sizeof(message), "format table %s exact bytes", test->name);
        check(strcmp(actual, test->expected) == 0, message);
        free(actual);
    }

    for (i = 0; i < sizeof(float_formats) / sizeof(float_formats[0]); i++) {
        TcValue f32 = fp32_from_double(1.5);
        FILE *out64 = open_capture();
        FILE *out32 = open_capture();
        char *actual64 = NULL;
        char *actual32 = NULL;
        char message[96];

        check(tc_io_write_formatted(TC_FLOAT64, float_formats[i], &pi64, out64) == 0,
              "float64 format table returns success");
        check(tc_io_write_formatted(TC_FLOAT32, float_formats[i], &f32, out32) == 0,
              "float32 format table returns success");
        actual64 = close_capture(out64);
        actual32 = close_capture(out32);
        snprintf(message, sizeof(message), "float64 %s exact bytes", float_names[i]);
        check(strcmp(actual64, float64_expected[i]) == 0, message);
        snprintf(message, sizeof(message), "float32 %s exact bytes", float_names[i]);
        check(strcmp(actual32, float32_expected[i]) == 0, message);
        free(actual64);
        free(actual32);
    }
}

static void test_float_special_text_table(void) {
    static const FormatCase cases[] = {
        {"negative zero %f", TC_FLOAT64, TC_FMT_F, UINT64_C(0x8000000000000000),
         "-0.000000"},
        {"negative zero %g", TC_FLOAT64, TC_FMT_G, UINT64_C(0x8000000000000000),
         "-0"},
        {"positive infinity %e", TC_FLOAT64, TC_FMT_E, UINT64_C(0x7FF0000000000000),
         "inf"},
        {"negative infinity %f", TC_FLOAT64, TC_FMT_F, UINT64_C(0xFFF0000000000000),
         "-inf"},
        {"infinity %E", TC_FLOAT64, TC_FMT_EU, UINT64_C(0x7FF0000000000000),
         "INF"},
        {"NaN payload hidden %g", TC_FLOAT64, TC_FMT_G, UINT64_C(0x7FF8000000001234),
         "nan"},
        {"NaN payload hidden %G", TC_FLOAT64, TC_FMT_GU, UINT64_C(0x7FF8000000001234),
         "NAN"},
        {"float32 infinity %G", TC_FLOAT32, TC_FMT_GU, UINT64_C(0x7F800000), "INF"},
        {"float32 NaN %f", TC_FLOAT32, TC_FMT_F, UINT64_C(0x7FC01234), "nan"},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const FormatCase *test = &cases[i];
        TcValue value = tc_value_make(test->type, test->bits);
        FILE *out = open_capture();
        char *actual = NULL;

        check(tc_io_write_formatted(test->type, test->fmt, &value, out) == 0, test->name);
        actual = close_capture(out);
        check(strcmp(actual, test->expected) == 0, test->name);
        free(actual);
    }
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

    out = open_capture();
    check(tc_io_write_formatted(TC_INT32, TC_FMT_U, &val, out) != 0,
          "write_fmt %%u on int32 -> fail");
    fclose(out);

    val = tc_value_make(TC_BOOL, 1);
    out = open_capture();
    check(tc_io_write_formatted(TC_BOOL, TC_FMT_D, &val, out) != 0,
          "write_fmt %%d on bool -> fail");
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

static void test_write_stream_failure(void) {
    TcValue value = tc_value_make(TC_INT32, 42);
    FILE *read_only = fopen(__FILE__, "r");

    check(read_only != NULL, "open read-only stream for output failure");
    if (!read_only) {
        return;
    }
    check(tc_io_write_value(&value, TC_FMT_NONE, 0, read_only) != 0,
          "write reports stream failure");
    fclose(read_only);
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

static TcType g_read_type = TC_INT32;
static uint64_t g_read_expected_bits = 0;
static const char *g_read_error = NULL;

static int read_table_case_fn(void) {
    const uint64_t sentinel = UINT64_C(0xA5A5A5A5A5A5A5A5);
    uint64_t bits = sentinel;
    TcDiagnostic diag;
    int rc = 0;

    tc_diagnostic_init(&diag);
    rc = tc_io_read_value(g_read_type, &bits, &diag, 17);
    if (!g_read_error) {
        if (rc != 0 || bits != g_read_expected_bits) {
            tc_diagnostic_clear(&diag);
            return -1;
        }
    } else if (rc == 0 || bits != sentinel || diag.kind != TC_ERR_IO || !diag.message ||
               strstr(diag.message, g_read_error) == NULL) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    tc_diagnostic_clear(&diag);
    return 0;
}

static void check_read_case(const char *name, const char *input, TcType type,
                            uint64_t expected_bits, const char *error) {
    g_read_type = type;
    g_read_expected_bits = expected_bits;
    g_read_error = error;
    check(with_stdin(input, read_table_case_fn) == 0, name);
}

static void test_read_contract_table(void) {
    check_read_case("read skips all ASCII whitespace", "\t\v\f\r\n 42\n", TC_INT32,
                    UINT64_C(42), NULL);
    check_read_case("read signed integer exact token", "-42\n", TC_INT32,
                    tc_signed_to_bits(TC_INT32, -42), NULL);
    check_read_case("read uint8 boundary", "255\n", TC_UINT8, UINT64_C(255), NULL);
    check_read_case("read rejects integer suffix", "12abc\n", TC_INT32, 0, "invalid input");
    check_read_case("read rejects signed plus", "+1\n", TC_INT32, 0, "invalid input");
    check_read_case("read rejects negative unsigned", "-1\n", TC_UINT32, 0, "invalid input");
    check_read_case("read reports EOF", "", TC_INT32, 0, "unexpected end of input");
    check_read_case("read float64 infinity", "inf\n", TC_FLOAT64,
                    UINT64_C(0x7FF0000000000000), NULL);
    check_read_case("read float64 negative infinity", "-inf\n", TC_FLOAT64,
                    UINT64_C(0xFFF0000000000000), NULL);
    check_read_case("read float64 canonical NaN", "nan\n", TC_FLOAT64,
                    UINT64_C(0x7FF8000000000000), NULL);
    check_read_case("read float32 canonical NaN", "nan\n", TC_FLOAT32,
                    UINT64_C(0x7FC00000), NULL);
    check_read_case("read preserves negative zero", "-0.0\n", TC_FLOAT64,
                    UINT64_C(0x8000000000000000), NULL);
    check_read_case("read float32 direct minimum subnormal",
                    "1.401298464324817070923729583289916131280e-45\n", TC_FLOAT32,
                    UINT64_C(1), NULL);
    check_read_case("read rejects float plus", "+1.0\n", TC_FLOAT64, 0, "invalid input");
    check_read_case("read rejects uppercase special", "INF\n", TC_FLOAT64, 0,
                    "invalid input");
    check_read_case("read rejects float suffix", "1.0x\n", TC_FLOAT64, 0,
                    "invalid input");
    check_read_case("read rejects float64 underflow to zero", "1e-9999\n", TC_FLOAT64, 0,
                    "input value out of range");
    check_read_case("read rejects float32 underflow to zero", "1e-999\n", TC_FLOAT32, 0,
                    "input value out of range");
#ifdef TC_HAVE_FENV
    {
        int saved_round = fegetround();

        check(fesetround(FE_UPWARD) == 0, "set upward rounding for read isolation");
        check_read_case("read float32 always uses roundTiesToEven",
                        "1.000000059604644775390625\n", TC_FLOAT32,
                        UINT64_C(0x3F800000), NULL);
        if (saved_round != -1) {
            check(fesetround(saved_round) == 0, "restore rounding after read isolation");
        }
    }
#endif
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

static int read_float64_fn(void) {
    uint64_t bits = 0;
    TcDiagnostic diag;
    double actual = 0.0;

    tc_diagnostic_init(&diag);
    if (tc_io_read_value(TC_FLOAT64, &bits, &diag, 1) != 0) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    memcpy(&actual, &bits, sizeof(actual));
    tc_diagnostic_clear(&diag);
    return fabs(actual - 3.14) < 1e-9 ? 0 : -1;
}

static int read_float32_fn(void) {
    uint64_t bits = 0;
    TcDiagnostic diag;
    float actual = 0.0f;

    tc_diagnostic_init(&diag);
    if (tc_io_read_value(TC_FLOAT32, &bits, &diag, 1) != 0) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    memcpy(&actual, &bits, sizeof(actual));
    tc_diagnostic_clear(&diag);
    return fabs((double)actual - 1.5) < 1e-6 ? 0 : -1;
}

static int read_float_scientific_fn(void) {
    uint64_t bits = 0;
    TcDiagnostic diag;
    double actual = 0.0;

    tc_diagnostic_init(&diag);
    if (tc_io_read_value(TC_FLOAT64, &bits, &diag, 1) != 0) {
        tc_diagnostic_clear(&diag);
        return -1;
    }
    memcpy(&actual, &bits, sizeof(actual));
    tc_diagnostic_clear(&diag);
    return fabs(actual - 120000.0) < 1e-3 ? 0 : -1;
}

static int read_float_invalid_fn(void) {
    TcDiagnostic diag;
    uint64_t bits = 0;

    tc_diagnostic_init(&diag);
    if (tc_io_read_value(TC_FLOAT64, &bits, &diag, 1) == 0) {
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

static void test_read_float_stdin(void) {
    check(with_stdin("  3.14\n", read_float64_fn) == 0, "read float64 decimal from stdin");
    check(with_stdin("1.5\n", read_float32_fn) == 0, "read float32 decimal from stdin");
    check(with_stdin("1.2E+5\n", read_float_scientific_fn) == 0, "read float64 scientific from stdin");
    check(with_stdin("abc\n", read_float_invalid_fn) == 0, "read float reject abc");
    check(with_stdin("3.14u\n", read_float_invalid_fn) == 0, "read float reject 3.14u");
}

/* ================================================================== */
/*  主入口                                                              */
/* ================================================================== */

int main(void) {
    /* 格式化输出 */
    test_all_format_specifiers_table();
    test_float_special_text_table();
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
    test_write_stream_failure();

    test_read_bool_stdin();
    test_read_float_stdin();
    test_read_contract_table();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
