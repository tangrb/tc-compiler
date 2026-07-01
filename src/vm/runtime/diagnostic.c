/*
 * diagnostic.c — 错误诊断的实现
 *
 * 管理 TcDiagnostic 结构体的生命周期：初始化、设置错误信息（深拷贝消息字符串）、
 * 格式化输出，以及释放动态分配的 message / filename 字段。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_diagnostic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * @brief 从 source 中提取 1-based 行号的文本
 * @param source  完整源文本
 * @param line_no 要提取的行号（1-based）
 * @param buf     输出缓冲区，写入提取的行文本（不含换行符）
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

    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' && len + 1 < buf_size) {
        buf[len++] = *cursor++;
    }
    buf[len] = '\0';
    return len;
}

/*
 * @brief 初始化诊断结构为默认空状态
 * @param diag 待初始化的诊断对象指针
 */
void tc_diagnostic_init(TcDiagnostic *diag) {
    diag->kind = TC_ERR_SYNTAX;
    diag->message = NULL;
    diag->filename = NULL;
    diag->snippet = NULL;
    diag->source = NULL;
    diag->line = 0;
    diag->column = TC_COLUMN_UNKNOWN;
}

/*
 * @brief 释放诊断对象的堆字段并清空位置信息
 * @param diag 待清除的诊断对象指针
 * @note 可重复调用；调用 tc_diagnostic_init 或 tc_diagnostic_set 前无需手动清除
 */
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

/*
 * @brief 绑定诊断所对应的源文件路径与完整源文本
 * @param diag     诊断对象指针
 * @param filename 源文件路径（会被 strdup 复制）
 * @param source   完整源文本（仅保存指针，调用方须保证其在诊断打印前有效）
 */
void tc_diagnostic_set_source(TcDiagnostic *diag, const char *filename, const char *source) {
    free(diag->filename);
    diag->filename = filename ? strdup(filename) : NULL;
    diag->source = source;
}

/*
 * @brief 设置一条新的诊断信息
 * @param diag    诊断对象指针
 * @param kind    错误种类枚举值
 * @param line    出错行号（1-based），0 表示无行号信息
 * @param column  出错列号（1-based），可传入 TC_COLUMN_UNKNOWN
 * @param message 错误描述消息（会被 strdup 复制，调用方无需保证其生命周期）
 * @note 若 source 可用且 line > 0，同时捕获出错行源码到 snippet 字段
 */
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

    if (diag->source && line > 0) {
        line_len = tc_extract_source_line(diag->source, line, line_buf, sizeof(line_buf));
        if (line_len > 0) {
            diag->snippet = strdup(line_buf);
        }
    }
}

/*
 * @brief 打印出错行源码与列指示符（caret）
 * @param diag 诊断对象指针（使用 snippet 和 column 字段）
 * @param out  输出文件流
 * @note 若无 snippet 或无有效列号则不输出 caret 指示符
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

    fputs("  ", out);
    for (int i = 1; i < caret_col; i++) {
        fputc(line_text[i - 1] == '\t' ? '\t' : ' ', out);
    }
    fputs("^\n", out);
}

/*
 * @brief 将诊断信息格式化输出到指定流（类 GCC/clang 格式）
 * @param diag 诊断对象指针
 * @param out  输出文件流（通常为 stderr）
 * @note 格式：
 *   <file>:<line>:<column>: error: <message>
 *   <file>:<line>: error: <message>          （无列号）
 *   <file>: error: <message>                  （无行号，如 I/O 错误）
 *   随后附加出错行源码与 caret 指示符（若有 snippet）
 */
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
