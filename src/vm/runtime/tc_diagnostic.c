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

/*
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
    diag->kind = TC_ERR_SYNTAX;
    diag->message = NULL;
    diag->filename = NULL;
    diag->snippet = NULL;
    diag->source = NULL;
    diag->line = 0;
    diag->column = TC_COLUMN_UNKNOWN;
}

void tc_diagnostic_clear(TcDiagnostic *diag) {
    free(diag->message);
    free(diag->filename);
    free(diag->snippet);
    diag->message = NULL;
    diag->filename = NULL;
    diag->snippet = NULL;
    diag->source = NULL;
    diag->line = 0;
    diag->column = TC_COLUMN_UNKNOWN;
}

void tc_diagnostic_set_source(TcDiagnostic *diag, const char *filename, const char *source) {
    free(diag->filename);
    diag->filename = filename ? strdup(filename) : NULL;
    diag->source = source;
}

void tc_diagnostic_set(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                       const char *message) {
    char line_buf[512];
    size_t line_len = 0;

    free(diag->message);
    free(diag->snippet);
    diag->kind = kind;
    diag->line = line;
    diag->column = column;
    diag->message = message ? strdup(message) : NULL;
    diag->snippet = NULL;

    /* 若 source 可用，提取出错行源码作为 snippet */
    if (diag->source && line > 0) {
        line_len = tc_extract_source_line(diag->source, line, line_buf, sizeof(line_buf));
        if (line_len > 0) {
            diag->snippet = strdup(line_buf);
        }
    }
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
    fprintf(out, ": error: %s\n", message);
    tc_diagnostic_print_snippet(diag, out);
}
