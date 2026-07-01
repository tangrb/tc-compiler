/*
 * driver.c — TC-VM 驱动层实现
 *
 * 负责：
 *   1. 文件 I/O（读取 .tc 源文件）
 *   2. 按行驱动 Lexer + Parser，跳过空白行与注释行
 *   3. 调用 Analyzer 与 Executor 完成「先检后跑」
 *
 * TC 源文件模型：一行一条语句；以 ';' 开头的行为注释。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_driver.h"

#include "tc_analyzer.h"
#include "tc_diagnostic.h"
#include "tc_executor.h"
#include "tc_lexer.h"
#include "tc_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * @brief 判断一行是否仅含空格/制表符
 * @param line 源行文本
 * @return 仅含空白字符返回 1；包含其他字符返回 0
 */
static int tc_is_only_whitespace(const char *line) {
    while (*line != '\0' && *line != '\r' && *line != '\n') {
        if (*line != ' ' && *line != '\t') {
            return 0;
        }
        line++;
    }
    return 1;
}

/*
 * @brief 判断是否为注释行或空行
 * @param line 源行文本
 * @return 跳过前导空白后，以 ';' 开头或已到行尾则返回 1；否则返回 0
 */
static int tc_is_comment_only_line(const char *line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return *line == ';' || *line == '\0' || *line == '\r' || *line == '\n';
}

/*
 * @brief 判断整行是否可跳过（空行、空白行或注释行）
 * @param line 源行文本
 * @return 可跳过返回 1；否则返回 0
 */
static int tc_is_skippable_line(const char *line) {
    if (line == NULL) {
        return 1;
    }
    if (tc_is_only_whitespace(line)) {
        return 1;
    }
    return tc_is_comment_only_line(line);
}

/*
 * @brief 将整个文件读入内存
 * @param path 文件路径
 * @param diag 诊断对象
 * @return 以 null 结尾的字符串指针（调用方需 free）；失败返回 NULL 并设置 diag
 */
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

/*
 * @brief 解析完整源字符串为 TcProgram
 * @param source  源字符串
 * @param program 输出参数，解析后的 TcProgram
 * @param diag    诊断对象
 * @return 成功返回 0；解析失败返回 -1 并设置 diag
 * @note 按 \\n/\\r\\n 分行，非空非注释行依次：词法化 → 语法化 → 追加到 program
 */
static int tc_parse_source(const char *source, TcProgram *program, TcDiagnostic *diag) {
    const char *cursor = source;
    int line_no = 1;

    tc_program_init(program);

    while (*cursor != '\0') {
        const char *line_start = cursor;
        const char *line_end = cursor;
        char *line_copy = NULL;
        TcTokenList tokens;
        TcStatement stmt;

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
                tc_program_free(program);
                return -1;
            }
            if (tc_parse_statement(&tokens, line_no, &stmt, diag) != 0) {
                free(line_copy);
                tc_token_list_free(&tokens);
                tc_program_free(program);
                return -1;
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

        free(line_copy);

        /* 处理 \r\n 或 \n 换行 */
        if (*line_end == '\r') {
            line_end++;
        }
        if (*line_end == '\n') {
            line_end++;
        }
        cursor = line_end;
        line_no++;
    }

    return 0;
}

/*
 * @brief 完整流水线：Parse → Analyze → [Execute]
 * @param source     源字符串
 * @param check_only 为真时 Analyze 成功后即返回，不进入 Executor
 * @param diag       诊断对象
 * @return 成功返回 0；任何阶段失败返回 -1 并设置 diag
 */
int tc_run_source(const char *source, int check_only, TcDiagnostic *diag) {
    TcProgram program;
    TcTypedProgram typed;
    int rc = 0;

    diag->source = source;

    if (tc_parse_source(source, &program, diag) != 0) {
        return -1;
    }

    if (tc_analyze(&program, &typed, diag) != 0) {
        return -1;
    }

    if (check_only) {
        tc_typed_program_free(&typed);
        return 0;
    }

    rc = tc_execute(&typed, diag);
    tc_typed_program_free(&typed);
    return rc;
}

/*
 * @brief 从文件路径读取源码并调用 tc_run_source
 * @param path       源文件路径
 * @param check_only 仅静态分析标志
 * @param diag       诊断对象
 * @return 成功返回 0；文件 I/O 失败或运行失败返回 -1 并设置 diag
 */
int tc_run_file(const char *path, int check_only, TcDiagnostic *diag) {
    char *source = NULL;
    int rc = 0;

    tc_diagnostic_set_source(diag, path, NULL);
    source = tc_read_file(path, diag);
    if (!source) {
        return -1;
    }

    rc = tc_run_source(source, check_only, diag);
    free(source);
    return rc;
}
