/*
 * tc_diagnostic.c — 错误诊断的实现
 *
 * 管理 TcDiagnostic 结构体的生命周期：初始化、错误设置（深拷贝消息字符串）、
 * 格式化输出（类 GCC/clang 风格）、以及释放动态内存。
 */
#include "tc_diagnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TC_DIAGNOSTIC_TESTING
static int tc_diagnostic_allocations_before_failure = -1;

void tc_diagnostic_test_fail_alloc_after(int successful_allocations) {
    tc_diagnostic_allocations_before_failure = successful_allocations;
}
#endif

static char *tc_diagnostic_strdup(const char *text) {
#ifdef TC_DIAGNOSTIC_TESTING
    if (tc_diagnostic_allocations_before_failure == 0) {
        return NULL;
    }
    if (tc_diagnostic_allocations_before_failure > 0) {
        tc_diagnostic_allocations_before_failure--;
    }
#endif
    return strdup(text);
}

static char tc_diagnostic_oom_message[] = "memory allocation failed";

static void tc_diagnostic_free_message(char *message) {
    if (message != tc_diagnostic_oom_message) {
        free(message);
    }
}

static void tc_diagnostic_mark_oom(TcDiagnostic *diag) {
    tc_diagnostic_free_message(diag->message);
    free(diag->snippet);
    diag->domain = TC_DIAG_IMPLEMENTATION;
    diag->api_code = TC_API_ERR_NONE;
    diag->kind = TC_ERR_OUT_OF_MEMORY;
    diag->line = 0;
    diag->column = TC_COLUMN_UNKNOWN;
    diag->message = tc_diagnostic_oom_message;
    diag->snippet = NULL;
}

/**
 * @brief 从 source 中提取 1-based 行号的文本
 * @param source   完整源文本
 * @param line_no  要提取的行号（1-based）
 * @param buf      输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 写入 buf 的字符长度（不含 null 终止符）；行不存在或参数非法返回 0
 */
static size_t tc_extract_source_line(const char *source, int line_no, char *buf, size_t buf_size) {
    const char *cursor = source;
    int current_line = 1;
    size_t len = 0;

    if (!source || line_no <= 0 || buf_size == 0) {
        return 0;
    }

    /* 跳过目标行之前的所有行 */
    while (current_line < line_no && *cursor != '\0') {
        while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
            cursor++;
        }
        if (*cursor == '\r') {
            cursor++;
        }
        if (*cursor == '\n') {
            cursor++;
        }
        current_line++;
    }

    if (current_line != line_no || *cursor == '\0') {
        buf[0] = '\0';
        return 0;
    }

    /* 复制目标行内容直到行尾或缓冲区满 */
    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' && len + 1 < buf_size) {
        buf[len++] = *cursor++;
    }
    buf[len] = '\0';
    return len;
}

void tc_diagnostic_init(TcDiagnostic *diag) {
    diag->domain = TC_DIAG_NONE;
    diag->api_code = TC_API_ERR_NONE;
    diag->kind = TC_CE_SYNTAX;
    diag->message = NULL;
    diag->filename = NULL;
    diag->snippet = NULL;
    diag->source = NULL;
    diag->line = 0;
    diag->column = TC_COLUMN_UNKNOWN;
}

void tc_diagnostic_clear(TcDiagnostic *diag) {
    tc_diagnostic_free_message(diag->message);
    free(diag->filename);
    free(diag->snippet);
    free(diag->source);
    diag->message = NULL;
    diag->filename = NULL;
    diag->snippet = NULL;
    diag->source = NULL;
    diag->domain = TC_DIAG_NONE;
    diag->api_code = TC_API_ERR_NONE;
    diag->kind = TC_CE_SYNTAX;
    diag->line = 0;
    diag->column = TC_COLUMN_UNKNOWN;
}

void tc_diagnostic_get_source(const TcDiagnostic *diag, const char **filename,
                              const char **source) {
    if (filename) {
        *filename = diag ? diag->filename : NULL;
    }
    if (source) {
        *source = diag ? diag->source : NULL;
    }
}

int tc_diagnostic_set_source(TcDiagnostic *diag, const char *filename, const char *source) {
    char *new_filename = NULL;
    char *new_source = NULL;

    if (filename) {
        new_filename = tc_diagnostic_strdup(filename);
        if (!new_filename) {
            tc_diagnostic_mark_oom(diag);
            return -1;
        }
    }
    if (source) {
        new_source = tc_diagnostic_strdup(source);
        if (!new_source) {
            free(new_filename);
            tc_diagnostic_mark_oom(diag);
            return -1;
        }
    }

    free(diag->filename);
    free(diag->source);
    diag->filename = new_filename;
    diag->source = new_source;
    return 0;
}

int tc_diagnostic_set(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                      const char *message) {
    char line_buf[512];
    size_t line_len = 0;
    char *new_message = NULL;
    char *new_snippet = NULL;

    if (message) {
        new_message = tc_diagnostic_strdup(message);
        if (!new_message) {
            tc_diagnostic_mark_oom(diag);
            return -1;
        }
    }
    if (diag->source && line > 0) {
        line_len = tc_extract_source_line(diag->source, line, line_buf, sizeof(line_buf));
        if (line_len > 0) {
            new_snippet = tc_diagnostic_strdup(line_buf);
            if (!new_snippet) {
                free(new_message);
                tc_diagnostic_mark_oom(diag);
                return -1;
            }
        }
    }

    tc_diagnostic_free_message(diag->message);
    free(diag->snippet);
    diag->domain = kind == TC_ERR_OUT_OF_MEMORY ? TC_DIAG_IMPLEMENTATION : TC_DIAG_LANGUAGE;
    diag->api_code = TC_API_ERR_NONE;
    diag->kind = kind;
    diag->line = line;
    diag->column = column;
    diag->message = new_message;
    diag->snippet = new_snippet;
    return 0;
}

int tc_diagnostic_set_api(TcDiagnostic *diag, TcApiErrorCode code, const char *message) {
    char *new_message = NULL;

    if (message) {
        new_message = tc_diagnostic_strdup(message);
        if (!new_message) {
            tc_diagnostic_mark_oom(diag);
            return -1;
        }
    }
    tc_diagnostic_free_message(diag->message);
    free(diag->snippet);
    diag->domain = TC_DIAG_API;
    diag->api_code = code;
    diag->kind = TC_CE_SYNTAX;
    diag->line = 0;
    diag->column = TC_COLUMN_UNKNOWN;
    diag->message = new_message;
    diag->snippet = NULL;
    return 0;
}

/*
 * @brief 打印出错行源码与列指示符（caret）
 * @param diag 使用 snippet 和 column 字段
 * @param out  输出文件流
 */
static void tc_diagnostic_print_snippet(const TcDiagnostic *diag, FILE *out) {
    const char *line_text = diag->snippet;
    size_t line_len = 0;
    int caret_col = 0;

    if (!line_text) {
        return;
    }

    line_len = strlen(line_text);
    fprintf(out, "  %s\n", line_text);

    if (diag->column < 1) {
        return;
    }

    caret_col = diag->column;
    if ((size_t)caret_col > line_len + 1) {
        caret_col = (int)line_len + 1;
    }

    /* 输出 ^ 指示符，跳过前 caret_col-1 个字符 */
    fputs("  ", out);
    for (int i = 1; i < caret_col; i++) {
        fputc(line_text[i - 1] == '\t' ? '\t' : ' ', out);
    }
    fputs("^\n", out);
}

void tc_diagnostic_print(const TcDiagnostic *diag, FILE *out) {
    const char *location = diag->filename ? diag->filename : "<source>";
    const char *message = diag->message ? diag->message : "";

    fputs(location, out);
    if (diag->line > 0) {
        fprintf(out, ":%d", diag->line);
        if (diag->column >= 0) {
            fprintf(out, ":%d", diag->column);
        }
    }
    if (diag->domain == TC_DIAG_API) {
        fprintf(out, ": api error: %s: %s\n", tc_api_error_code_name(diag->api_code), message);
    } else if (diag->domain == TC_DIAG_IMPLEMENTATION) {
        fprintf(out, ": implementation error: %s: %s\n", tc_error_kind_name(diag->kind),
                message);
    } else {
        fprintf(out, ": error: %s\n", message);
        tc_diagnostic_print_snippet(diag, out);
    }
}
