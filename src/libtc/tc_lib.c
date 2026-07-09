/*
 * tc_lib.c — libtc 编译入口（Parse + Analyze）
 *
 * 提供完整的编译流水线：读源（字符串/文件）→ 逐行 Lex+Parse → Analyze，
 * 可选输出各阶段耗时（环境变量 TC_BENCH=1 启用）。
 * 执行入口 tc_run_typed 委托 tc_execute。
 */
#include "tc_lib.h"

#include "tc_lexer.h"
#include "tc_parser.h"
#include "tc_warning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  性能计时辅助（环境变量 TC_BENCH=1 启用）                               */
/* ------------------------------------------------------------------ */

static double tc_bench_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void tc_bench_report(const char *phase, double seconds) {
    if (getenv("TC_BENCH") != NULL) {
        fprintf(stderr, "bench %s: %.6f s\n", phase, seconds);
    }
}

/* ------------------------------------------------------------------ */
/*  逐行解析源文本                                                       */
/* ------------------------------------------------------------------ */

static int tc_parse_source(const char *source, TcProgram *program, TcDiagnostic *diag) {
    double t0 = tc_bench_now();
    int rc = tc_parse_source_to_program(source, program, diag);

    tc_bench_report("parse", tc_bench_now() - t0);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  文件读取                                                           */
/* ------------------------------------------------------------------ */

static char *tc_read_file(const char *path, TcDiagnostic *diag) {
    FILE *file = fopen(path, "rb");
    char *buffer = NULL;
    long size = 0;
    size_t read_size = 0;

    if (!file) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "cannot open input file");
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "cannot read input file");
        return NULL;
    }

    size = ftell(file);
    if (size < 0) {
        fclose(file);
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "cannot read input file");
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "cannot read input file");
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read_size] = '\0';
    return buffer;
}

/* ------------------------------------------------------------------ */
/*  对外 API                                                            */
/* ------------------------------------------------------------------ */

int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag) {
    TcProgram program;
    double t0;

    diag->source = source;

    if (tc_parse_source(source, &program, diag) != 0) {
        return -1;
    }

    t0 = tc_bench_now();
    if (tc_analyze(&program, out, diag) != 0) {
        return -1;
    }
    tc_bench_report("analyze", tc_bench_now() - t0);
    return 0;
}

int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag) {
    char *source = NULL;
    int rc = 0;

    tc_diagnostic_set_source(diag, path, NULL);
    source = tc_read_file(path, diag);
    if (!source) {
        return -1;
    }

    rc = tc_compile_source(source, out, diag);
    free(source);
    return rc;
}

int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag) {
    double t0 = tc_bench_now();
    int rc = tc_execute(program, diag);
    tc_bench_report("execute", tc_bench_now() - t0);
    return rc;
}
