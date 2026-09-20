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

char *tc_strndup(const char *s, size_t n) {
    size_t len = 0;
    char *copy;

    if (s == NULL) {
        return NULL;
    }
    while (len < n && s[len] != '\0') {
        len++;
    }
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
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
    memset(&diag->deferred, 0, sizeof(diag->deferred));
    memset(&diag->deferred_sem, 0, sizeof(diag->deferred_sem));
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
    tc_diagnostic_clear_deferred(diag);
    tc_diagnostic_clear_deferred_sem(diag);
}

int tc_diagnostic_is_set(const TcDiagnostic *diag) {
    return diag && diag->domain != TC_DIAG_NONE;
}

/* ------------------------------------------------------------------ */
/*  挂起的 CT 类诊断（语言标准 §11「阶段优先」）              */
/* ------------------------------------------------------------------ */

void tc_diagnostic_clear_deferred(TcDiagnostic *diag) {
    TcDeferredDiagnostic *pending = NULL;

    if (!diag) {
        return;
    }
    pending = &diag->deferred;
    free(pending->message);
    free(pending->filename);
    free(pending->source);
    memset(pending, 0, sizeof(*pending));
}

/* §11 第 2 条：同阶段按源序位置（行号升序，同行按 Token 次序）。 */
static int tc_diagnostic_defer_precedes(int new_line, int new_column, int old_line,
                                        int old_column) {
    if (new_line != old_line) {
        return new_line < old_line;
    }
    if (new_column == old_column) {
        return 0;
    }
    /* 无列号（TC_COLUMN_UNKNOWN）视为同行最靠前，保持「先到先得」。 */
    if (new_column == TC_COLUMN_UNKNOWN) {
        return 1;
    }
    if (old_column == TC_COLUMN_UNKNOWN) {
        return 0;
    }
    return new_column < old_column;
}

static int tc_diagnostic_defer_into(TcDiagnostic *diag, TcDeferredDiagnostic *pending,
                                    TcErrorKind kind, int line, int column,
                                    const char *message) {
    char *new_message = NULL;
    char *new_filename = NULL;
    char *new_source = NULL;

    if (!diag || !pending) {
        return -1;
    }
    if (pending->active &&
        !tc_diagnostic_defer_precedes(line, column, pending->line, pending->column)) {
        /* 已有源序更靠前的挂起诊断：保留原诊断，仍然报告失败。 */
        return -1;
    }

    if (message) {
        new_message = tc_diagnostic_strdup(message);
        if (!new_message) {
            tc_diagnostic_mark_oom(diag);
            return -1;
        }
    }
    /* 源文件绑定随挂起诊断一并保存，供后续 flush 时恢复（多模块编译）。 */
    if (diag->filename) {
        new_filename = tc_diagnostic_strdup(diag->filename);
        if (!new_filename) {
            free(new_message);
            tc_diagnostic_mark_oom(diag);
            return -1;
        }
    }
    if (diag->source) {
        new_source = tc_diagnostic_strdup(diag->source);
        if (!new_source) {
            free(new_message);
            free(new_filename);
            tc_diagnostic_mark_oom(diag);
            return -1;
        }
    }

    free(pending->message);
    free(pending->filename);
    free(pending->source);
    pending->active = 1;
    pending->kind = kind;
    pending->line = line;
    pending->column = column;
    pending->message = new_message;
    pending->filename = new_filename;
    pending->source = new_source;
    return 0;
}

int tc_diagnostic_defer(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                        const char *message) {
    (void)tc_diagnostic_defer_into(diag, diag ? &diag->deferred : NULL, kind, line, column,
                                   message);
    return -1;
}

int tc_diagnostic_has_deferred(const TcDiagnostic *diag) {
    return diag && diag->deferred.active;
}

int tc_diagnostic_flush_deferred(TcDiagnostic *diag) {
    TcDeferredDiagnostic pending;
    int rc = 0;

    if (!diag || !diag->deferred.active) {
        return 0;
    }
    if (tc_diagnostic_is_set(diag)) {
        /* 已有真实诊断：CT 类诊断优先级更低，直接丢弃。 */
        tc_diagnostic_drop_deferred(diag);
        return 0;
    }
    /* 取走所有权，避免 set_source / set 内部释放时与 pending 冲突。 */
    pending = diag->deferred;
    memset(&diag->deferred, 0, sizeof(diag->deferred));

    if (pending.filename || pending.source) {
        if (tc_diagnostic_set_source(diag, pending.filename, pending.source) != 0) {
            free(pending.message);
            free(pending.filename);
            free(pending.source);
            return 0; /* OOM 诊断已由 set_source 写入 */
        }
    }
    (void)tc_diagnostic_set(diag, pending.kind, pending.line, pending.column, pending.message);
    rc = 1;
    free(pending.message);
    free(pending.filename);
    free(pending.source);
    return rc;
}

void tc_diagnostic_drop_deferred(TcDiagnostic *diag) {
    tc_diagnostic_clear_deferred(diag);
}

/* ── 挂起的 SEM 类诊断（B-40） ── */

void tc_diagnostic_clear_deferred_sem(TcDiagnostic *diag) {
    TcDeferredDiagnostic *pending = NULL;

    if (!diag) {
        return;
    }
    pending = &diag->deferred_sem;
    free(pending->message);
    free(pending->filename);
    free(pending->source);
    memset(pending, 0, sizeof(*pending));
}

int tc_diagnostic_defer_sem(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                            const char *message) {
    return tc_diagnostic_defer_into(diag, diag ? &diag->deferred_sem : NULL, kind, line, column,
                                    message);
}

int tc_diagnostic_has_deferred_sem(const TcDiagnostic *diag) {
    return diag && diag->deferred_sem.active;
}

static int tc_diagnostic_same_file(const TcDiagnostic *diag, const TcDeferredDiagnostic *pending) {
    if (!diag->filename || !pending->filename) {
        return diag->filename == pending->filename;
    }
    return strcmp(diag->filename, pending->filename) == 0;
}

int tc_diagnostic_publish_deferred_sem(TcDiagnostic *diag) {
    TcDeferredDiagnostic pending;
    int replace = 0;

    if (!diag || !diag->deferred_sem.active) {
        return 0;
    }
    if (tc_diagnostic_is_set(diag)) {
        /*
         * 已有真实诊断：仅当它与挂起项同属一个源文件、且挂起项源序更靠前时替换
         *（§11 第 2 条）。跨文件（依赖模块）无从比较源序位置，沿用「先到先得」。
         *
         * 同行比较有个例外：挂起项带列号、真实诊断无列号（行级诊断，如
         * MemblockSizeMismatch）时，挂起项定位到具体 Token，按 §11「同行按 Token
         * 次序」应先于行级诊断，故视为更靠前。
         */
        if (tc_diagnostic_same_file(diag, &diag->deferred_sem)) {
            int pend_same_line = diag->deferred_sem.line == diag->line;

            if (diag->deferred_sem.line < diag->line ||
                (pend_same_line && diag->deferred_sem.column != TC_COLUMN_UNKNOWN &&
                 diag->column == TC_COLUMN_UNKNOWN) ||
                tc_diagnostic_defer_precedes(diag->deferred_sem.line, diag->deferred_sem.column,
                                             diag->line, diag->column)) {
                replace = 1;
            }
        }
        if (!replace) {
            tc_diagnostic_clear_deferred_sem(diag);
            return 0;
        }
    }
    /* 取走所有权，避免 set_source / set 内部释放时与 pending 冲突。 */
    pending = diag->deferred_sem;
    memset(&diag->deferred_sem, 0, sizeof(diag->deferred_sem));

    if (replace) {
        tc_diagnostic_clear(diag);
    }
    if (pending.filename || pending.source) {
        if (tc_diagnostic_set_source(diag, pending.filename, pending.source) != 0) {
            free(pending.message);
            free(pending.filename);
            free(pending.source);
            return 0; /* OOM 诊断已由 set_source 写入 */
        }
    }
    (void)tc_diagnostic_set(diag, pending.kind, pending.line, pending.column, pending.message);
    free(pending.message);
    free(pending.filename);
    free(pending.source);
    return 1;
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

int tc_diagnostic_use_source(TcDiagnostic *diag, const char *filename, const char *source) {
    const char *cur_file = NULL;
    const char *cur_source = NULL;

    if (!diag) {
        return -1;
    }
    tc_diagnostic_get_source(diag, &cur_file, &cur_source);
    if (((cur_file == filename) || (cur_file && filename && strcmp(cur_file, filename) == 0)) &&
        ((cur_source == source) || (cur_source && source && strcmp(cur_source, source) == 0))) {
        return 0;
    }
    return tc_diagnostic_set_source(diag, filename, source);
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

static void tc_diagnostic_print_ex(const TcDiagnostic *diag, FILE *out, int with_code);

void tc_diagnostic_print(const TcDiagnostic *diag, FILE *out) {
    tc_diagnostic_print_ex(diag, out, 0);
}

/* 同 tc_diagnostic_print，但普通诊断首行附错误码名（CLI --print-error-code）。
 * API/实现域本已打印码名，不受影响。 */
void tc_diagnostic_print_with_code(const TcDiagnostic *diag, FILE *out) {
    tc_diagnostic_print_ex(diag, out, 1);
}

static void tc_diagnostic_print_ex(const TcDiagnostic *diag, FILE *out, int with_code) {
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
        /*
         * B-18：实现域只打印**实现专用**码名（当前仅 TC_ERR_OUT_OF_MEMORY）。
         * 实现缺陷（`internal error: …`）不携带语言错误码，不得把占位的
         * TC_CE_SYNTAX 打印成 `SyntaxError` 伪装成语言诊断（语言标准 §1.3 只
         * 承认实现资源失败一类实现侧失败）。
         */
        if (diag->kind >= TC_ERR_OUT_OF_MEMORY) {
            fprintf(out, ": implementation error: %s: %s\n", tc_error_kind_name(diag->kind),
                    message);
        } else {
            fprintf(out, ": implementation error: %s\n", message);
        }
    } else if (with_code) {
        fprintf(out, ": error [%s]: %s\n", tc_error_kind_name(diag->kind), message);
        tc_diagnostic_print_snippet(diag, out);
    } else {
        fprintf(out, ": error: %s\n", message);
        tc_diagnostic_print_snippet(diag, out);
    }
}
