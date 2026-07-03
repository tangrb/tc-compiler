/*
 * compile.c — libtc 编译入口（Parse + Analyze）
 */
#include "tc_lib.h"

#include "tc_lexer.h"
#include "tc_parser.h"
#include "tc_warning.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static int tc_is_only_whitespace(const char *line) {
    while (*line != '\0' && *line != '\r' && *line != '\n') {
        if (*line != ' ' && *line != '\t') {
            return 0;
        }
        line++;
    }
    return 1;
}

static int tc_is_comment_only_line(const char *line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return *line == ';' || *line == '\0' || *line == '\r' || *line == '\n';
}

static int tc_is_skippable_line(const char *line) {
    if (line == NULL) {
        return 1;
    }
    if (tc_is_only_whitespace(line)) {
        return 1;
    }
    return tc_is_comment_only_line(line);
}

static int tc_parse_source(const char *source, TcProgram *program, TcDiagnostic *diag) {
    const char *cursor = source;
    int line_no = 1;
    int had_syntax_error = 0;
    double t0 = tc_bench_now();

    tc_program_init(program);

    while (*cursor != '\0') {
        const char *line_start = cursor;
        const char *line_end = cursor;
        char *line_copy = NULL;
        TcTokenList tokens;

        while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        line_copy = (char *)malloc((size_t)(line_end - line_start) + 1);
        if (!line_copy) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, TC_COLUMN_UNKNOWN, "out of memory");
            tc_program_free(program);
            return -1;
        }
        memcpy(line_copy, line_start, (size_t)(line_end - line_start));
        line_copy[line_end - line_start] = '\0';

        if (!tc_is_skippable_line(line_copy)) {
            tc_token_list_init(&tokens);
            if (tc_tokenize_line(line_copy, line_no, &tokens, diag) != 0) {
                free(line_copy);
                tc_token_list_free(&tokens);
                had_syntax_error = 1;
                goto advance_line;
            }
            {
                TcStatement stmt;
                memset(&stmt, 0, sizeof(stmt));
                if (tc_parse_statement(&tokens, line_no, &stmt, diag) != 0) {
                    tc_statement_free(&stmt);
                    free(line_copy);
                    tc_token_list_free(&tokens);
                    had_syntax_error = 1;
                    goto advance_line;
                }
                tc_token_list_free(&tokens);
                if (tc_program_push(program, &stmt) != 0) {
                    tc_statement_free(&stmt);
                    free(line_copy);
                    tc_program_free(program);
                    tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, TC_COLUMN_UNKNOWN, "out of memory");
                    return -1;
                }
            }
        }

        free(line_copy);

advance_line:
        if (*line_end == '\r') {
            line_end++;
        }
        if (*line_end == '\n') {
            line_end++;
        }
        cursor = line_end;
        line_no++;
    }

    tc_bench_report("parse", tc_bench_now() - t0);

    if (had_syntax_error) {
        tc_program_free(program);
        return -1;
    }
    return 0;
}

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
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "out of memory");
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read_size] = '\0';
    return buffer;
}

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
