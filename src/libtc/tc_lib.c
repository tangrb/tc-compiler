/*
 * tc_lib.c — libtc 编译入口（Parse + Analyze）
 *
 * 提供完整的编译流水线：读源（字符串/文件）→ 逐行 Lex+Parse → Analyze，
 * 可选输出各阶段耗时（环境变量 TC_BENCH=1 启用）。
 * 执行入口 tc_run_typed 委托 tc_execute。
 */
#include "tc_lib.h"

#include "tc_lexer.h"
#include "tc_module.h"
#include "tc_parser.h"
#include "tc_warning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static TcModuleSearchPaths g_module_search_paths;

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

static int tc_invalid_argument(TcDiagnostic *diag, const char *message) {
    if (diag) {
        tc_diagnostic_set_api(diag, TC_API_ERR_INVALID_ARGUMENT, message);
    }
    return -1;
}

static char *tc_file_read_error(FILE *file, char *buffer, TcDiagnostic *diag) {
    free(buffer);
    if (file) {
        fclose(file);
    }
    tc_diagnostic_set_api(diag, TC_API_ERR_FILE_READ, "cannot read input file");
    return NULL;
}

static char *tc_read_file(const char *path, TcDiagnostic *diag) {
    FILE *file = fopen(path, "rb");
    char *buffer = NULL;
    long size = 0;
    size_t read_size = 0;

    if (!file) {
        tc_diagnostic_set_api(diag, TC_API_ERR_FILE_OPEN, "cannot open input file");
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        return tc_file_read_error(file, buffer, diag);
    }

    size = ftell(file);
    if (size < 0) {
        return tc_file_read_error(file, buffer, diag);
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        return tc_file_read_error(file, buffer, diag);
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t)size, file);
    if (read_size != (size_t)size || ferror(file)) {
        return tc_file_read_error(file, buffer, diag);
    }
    {
        int first = fgetc(file);
        if (first != EOF || ferror(file)) {
            return tc_file_read_error(file, buffer, diag);
        }
    }
    if (fclose(file) != 0) {
        free(buffer);
        tc_diagnostic_set_api(diag, TC_API_ERR_FILE_READ, "cannot read input file");
        return NULL;
    }
    buffer[read_size] = '\0';
    return buffer;
}

/* ------------------------------------------------------------------ */
/*  对外 API                                                            */
/* ------------------------------------------------------------------ */

int tc_compile_source(const char *source, TcTypedProgram *out, TcDiagnostic *diag) {
    TcProgram program;
    TcTypedProgram typed;
    double t0;

    if (!diag) {
        return -1;
    }
    if (!source || !out) {
        return tc_invalid_argument(diag, "source and output program must not be null");
    }
    if (tc_diagnostic_set_source(diag, diag->filename, source) != 0) {
        return -1;
    }

    if (tc_parse_source(source, &program, diag) != 0) {
        return -1;
    }

    t0 = tc_bench_now();
    if (tc_analyze(&program, &typed, diag) != 0) {
        return -1;
    }
    /* 无路径的内存源：仅做结构检查；导入解析在 tc_compile_file */
    tc_bench_report("analyze", tc_bench_now() - t0);
    *out = typed;
    return 0;
}

int tc_set_module_search_paths(char *const *paths, size_t count, TcDiagnostic *diag) {
    if (!diag) {
        return -1;
    }
    return tc_module_search_paths_set(&g_module_search_paths, paths, count, diag);
}

int tc_compile_file(const char *path, TcTypedProgram *out, TcDiagnostic *diag) {
    char *source = NULL;
    TcProgram program;
    TcTypedProgram typed;
    double t0;

    if (!diag) {
        return -1;
    }
    if (!path || path[0] == '\0' || !out) {
        return tc_invalid_argument(diag, "path and output program must not be null");
    }
    if (tc_diagnostic_set_source(diag, path, NULL) != 0) {
        return -1;
    }
    source = tc_read_file(path, diag);
    if (!source) {
        return -1;
    }
    if (tc_diagnostic_set_source(diag, path, source) != 0) {
        free(source);
        return -1;
    }

    if (tc_parse_source(source, &program, diag) != 0) {
        free(source);
        return -1;
    }
    free(source);

    t0 = tc_bench_now();
    if (tc_analyze(&program, &typed, diag) != 0) {
        return -1;
    }
    if (tc_module_resolve_imports(&typed, path, &g_module_search_paths, diag) != 0) {
        tc_typed_program_free(&typed);
        return -1;
    }
    tc_bench_report("analyze+modules", tc_bench_now() - t0);
    *out = typed;
    return 0;
}

int tc_run_typed(const TcTypedProgram *program, TcDiagnostic *diag) {
    double t0;
    int rc;

    if (!diag) {
        return -1;
    }
    if (!program) {
        return tc_invalid_argument(diag, "typed program must not be null");
    }
    t0 = tc_bench_now();
    rc = tc_execute(program, diag);
    tc_bench_report("execute", tc_bench_now() - t0);
    return rc;
}
