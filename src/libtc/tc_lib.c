/*
 * tc_lib.c — libtc 编译入口（Parse + Analyze）
 *
 * 提供完整的编译流水线：读源（字符串/文件）→ 逐行 Lex+Parse → Analyze，
 * 可选输出各阶段耗时（环境变量 TC_BENCH=1 启用）。
 * 执行入口 tc_run_program 委托 tc_execute。
 *
 * tc_compile_file_opts 经 tc_analyze_ex 解析可达 #lib（会话 opts 携带 -I）；
 * tc_compile_source（无路径）仅做结构检查、不解析 import。
 */
#include "tc_lib.h"

#include "tc_lexer.h"
#include "tc_module.h"
#include "tc_parser.h"
#include "tc_warning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static double tc_bench_now(void) {
#ifdef _WIN32
    /* MinGW-w64（Debian 打包，msvcrt 运行时）无 clock_gettime；GetTickCount64
     * 需 _WIN32_WINNT>=0x0600 才声明，GetTickCount 始终可用（相对计时足够） */
    return (double)GetTickCount() / 1000.0;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
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

static int tc_source_scan_utf8(const char *source, size_t length, TcDiagnostic *diag) {
    size_t i = 0;
    int line = 1;
    int col = 1;

    if (!source) {
        return -1;
    }
    while (i < length) {
        unsigned char c = (unsigned char)source[i];
        int cp = 0;
        int n = 0;

        if (c == 0) {
            tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col,
                              "null character (U+0000) not allowed in source");
            return -1;
        }
        if (c <= 0x7F) {
            n = 1;
            cp = c;
        } else if (c <= 0xBF) {
            tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
            return -1;
        } else if (c <= 0xDF) {
            if (i + 1 >= length || ((unsigned char)source[i + 1] & 0xC0) != 0x80) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
                return -1;
            }
            cp = ((c & 0x1F) << 6) | ((unsigned char)source[i + 1] & 0x3F);
            if (cp < 0x80) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
                return -1;
            }
            n = 2;
        } else if (c <= 0xEF) {
            if (i + 2 >= length || ((unsigned char)source[i + 1] & 0xC0) != 0x80 ||
                ((unsigned char)source[i + 2] & 0xC0) != 0x80) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
                return -1;
            }
            cp = ((c & 0x0F) << 12) | (((unsigned char)source[i + 1] & 0x3F) << 6) |
                 ((unsigned char)source[i + 2] & 0x3F);
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
                return -1;
            }
            n = 3;
        } else if (c <= 0xF4) {
            if (i + 3 >= length || ((unsigned char)source[i + 1] & 0xC0) != 0x80 ||
                ((unsigned char)source[i + 2] & 0xC0) != 0x80 ||
                ((unsigned char)source[i + 3] & 0xC0) != 0x80) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
                return -1;
            }
            cp = ((c & 0x07) << 18) | (((unsigned char)source[i + 1] & 0x3F) << 12) |
                 (((unsigned char)source[i + 2] & 0x3F) << 6) | ((unsigned char)source[i + 3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
                return -1;
            }
            n = 4;
        } else {
            tc_diagnostic_set(diag, TC_CE_SYNTAX, line, col, "invalid UTF-8 in source");
            return -1;
        }
        if (n == 1 && c == '\r' && i + 1 < length && source[i + 1] == '\n') {
            line++;
            col = 1;
            i += 2;
            continue;
        }
        if (n == 1 && c == '\n') {
            line++;
            col = 1;
            i++;
            continue;
        }
        i += (size_t)n;
        col++;
    }
    return 0;
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

    /* 检查文件起始 UTF-8 BOM */
    if (read_size >= 3 &&
        (unsigned char)buffer[0] == 0xEF &&
        (unsigned char)buffer[1] == 0xBB &&
        (unsigned char)buffer[2] == 0xBF) {
        free(buffer);
        fclose(file);
        tc_diagnostic_set(diag, TC_CE_SYNTAX, 1, 1, "UTF-8 BOM not allowed in source file");
        return NULL;
    }

    if (tc_source_scan_utf8(buffer, read_size, diag) != 0) {
        free(buffer);
        fclose(file);
        return NULL;
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

int tc_compile_source(const char *source, const char *name,
                      TcTypedProgram *out, TcDiagnostic *diag) {
    TcProgram program;
    TcTypedProgram typed;
    double t0;
    if (!diag) {
        return -1;
    }
    if (!source || !name || !out) {
        return tc_invalid_argument(diag, "source, name, and output program must not be null");
    }
    if (tc_diagnostic_set_source(diag, name, source) != 0) {
        return -1;
    }
    if (tc_source_scan_utf8(source, strlen(source), diag) != 0) {
        return -1;
    }

    if (tc_parse_source(source, &program, diag) != 0) {
        return -1;
    }

    t0 = tc_bench_now();
    if (tc_analyze(&program, &typed, diag) != 0) {
        return -1;
    }
    /* 无路径的内存源：仅做结构检查；导入解析在 tc_compile_file_opts */
    tc_bench_report("analyze", tc_bench_now() - t0);
    *out = typed;
    return 0;
}

int tc_compile_file_opts(const char *path, const TcCompileOptions *opts,
                         TcTypedProgram *out, TcDiagnostic *diag) {
    char *source = NULL;
    TcProgram program;
    TcTypedProgram typed;
    TcModuleSearchPaths local;
    const TcModuleSearchPaths *search = NULL;
    double t0;
    TcCompileOptions empty_opts;

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
    /* 会话搜索路径：opts 提供则借用之（调用期间有效）；否则本次编译无额外路径。
     * 无进程级全局状态（多编译单元 / 多线程嵌入场景各自携带）。 */
    if (opts && opts->search_paths && opts->search_path_count > 0) {
        memset(&local, 0, sizeof(local));
        local.paths = (char **)(uintptr_t)opts->search_paths;
        local.count = opts->search_path_count;
        search = &local;
    } else {
        memset(&empty_opts, 0, sizeof(empty_opts));
        search = NULL;
    }
    if (tc_analyze_ex(&program, &typed, path, search, diag) != 0) {
        return -1;
    }
    tc_bench_report("analyze+modules", tc_bench_now() - t0);
    *out = typed;
    return 0;
}

int tc_run_program(const TcTypedProgram *program, TcDiagnostic *diag) {
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
