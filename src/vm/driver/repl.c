/*
 * repl.c — TC-VM 交互式 REPL 实现
 *
 * 逐行读取 TC 源码，经 Lexer → Parser → 增量 Analyze → Execute 执行。
 * 会话内变量定义与赋值状态跨行保留。
 *
 * 内置命令（以 ':' 开头）：
 *   :quit / :exit / :q  退出 REPL
 *   :reset              清空会话
 *   :vars               列出当前变量
 *   :help               显示帮助
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_repl.h"

#include "tc_analyzer.h"
#include "tc_diagnostic.h"
#include "tc_executor.h"
#include "tc_lexer.h"
#include "tc_parser.h"
#include "tc_semantics.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TC_REPL_PROMPT "tc> "
#define TC_REPL_LINE_MAX 4096

/*
 * @brief 判断一行是否仅含空格/制表符
 */
static int tc_repl_is_only_whitespace(const char *line) {
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
 */
static int tc_repl_is_skippable_line(const char *line) {
    const char *cursor = line;

    if (line == NULL) {
        return 1;
    }
    if (tc_repl_is_only_whitespace(line)) {
        return 1;
    }
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    return *cursor == ';' || *cursor == '\0' || *cursor == '\r' || *cursor == '\n';
}

/*
 * @brief 初始化 REPL 会话
 */
static void tc_repl_session_init(TcReplSession *session) {
    tc_symbol_table_init(&session->symbols);
    session->slots = NULL;
    session->slots_capacity = 0;
    session->line_no = 1;
}

/*
 * @brief 释放 REPL 会话资源
 */
static void tc_repl_session_free(TcReplSession *session) {
    tc_symbol_table_free(&session->symbols);
    free(session->slots);
    session->slots = NULL;
    session->slots_capacity = 0;
    session->line_no = 1;
}

/*
 * @brief 重置 REPL 会话（清空变量）
 */
static void tc_repl_session_reset(TcReplSession *session) {
    tc_repl_session_free(session);
    tc_repl_session_init(session);
}

/*
 * @brief 确保变量槽数组容量与符号表一致
 */
static int tc_repl_ensure_slots(TcReplSession *session, TcDiagnostic *diag) {
    if (session->symbols.count <= session->slots_capacity) {
        return 0;
    }

    {
        TcValue *new_slots =
            (TcValue *)realloc(session->slots, session->symbols.count * sizeof(TcValue));
        if (!new_slots) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "out of memory");
            return -1;
        }
        memset(new_slots + session->slots_capacity, 0,
               (session->symbols.count - session->slots_capacity) * sizeof(TcValue));
        session->slots = new_slots;
        session->slots_capacity = session->symbols.count;
    }
    return 0;
}

/*
 * @brief 格式化变量值到缓冲区
 */
static void tc_repl_format_value(const TcValue *value, char *buf, size_t buf_size) {
    if (tc_type_is_signed(value->type)) {
        int64_t signed_value = tc_bits_to_signed(value->type, value->bits);
        snprintf(buf, buf_size, "%" PRId64, signed_value);
    } else {
        uint64_t unsigned_value = tc_value_to_unsigned(value->type, value->bits);
        snprintf(buf, buf_size, "%" PRIu64, unsigned_value);
    }
}

/*
 * @brief 打印当前会话中所有变量
 */
static void tc_repl_print_vars(const TcReplSession *session) {
    size_t i = 0;

    if (session->symbols.count == 0) {
        printf("(no variables)\n");
        return;
    }

    for (i = 0; i < session->symbols.count; i++) {
        const TcSymbol *symbol = &session->symbols.symbols[i];
        char value_text[64];

        tc_repl_format_value(&session->slots[symbol->slot], value_text, sizeof(value_text));
        printf("%s: %s = %s\n", symbol->name, tc_int_type_name(symbol->type), value_text);
    }
}

/*
 * @brief 打印 REPL 内置命令帮助
 */
static void tc_repl_print_help(void) {
    printf("TC-VM interactive mode. Enter one TC statement per line.\n"
           "\n"
           "Meta commands:\n"
           "  :quit, :exit, :q   exit REPL\n"
           "  :reset             clear all variables\n"
           "  :vars              list current variables\n"
           "  :help              show this help\n"
           "\n"
           "Note: read() and REPL both use stdin.\n");
}

/*
 * @brief 处理 REPL 内置命令
 * @return 1 表示已处理（含退出）；0 表示非内置命令
 */
static int tc_repl_handle_meta(TcReplSession *session, const char *line, int *should_quit) {
    const char *cursor = line;

    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor != ':') {
        return 0;
    }
    cursor++;

    if (strcmp(cursor, "quit") == 0 || strcmp(cursor, "exit") == 0 || strcmp(cursor, "q") == 0) {
        *should_quit = 1;
        return 1;
    }
    if (strcmp(cursor, "reset") == 0) {
        tc_repl_session_reset(session);
        printf("session reset\n");
        return 1;
    }
    if (strcmp(cursor, "vars") == 0) {
        tc_repl_print_vars(session);
        return 1;
    }
    if (strcmp(cursor, "help") == 0) {
        tc_repl_print_help();
        return 1;
    }

    fprintf(stderr, "unknown command: :%s (try :help)\n", cursor);
    return 1;
}

/*
 * @brief 解析并执行一行 TC 源码
 */
static int tc_repl_eval_line(TcReplSession *session, const char *line, TcDiagnostic *diag) {
    TcTokenList tokens;
    TcStatement stmt;
    int rc = 0;

    tc_token_list_init(&tokens);
    if (tc_tokenize_line(line, session->line_no, &tokens, diag) != 0) {
        tc_token_list_free(&tokens);
        return -1;
    }
    if (tc_parse_statement(&tokens, session->line_no, &stmt, diag) != 0) {
        tc_token_list_free(&tokens);
        return -1;
    }
    tc_token_list_free(&tokens);

    if (tc_analyze_statement(&stmt, &session->symbols, diag) != 0) {
        tc_statement_free(&stmt);
        return -1;
    }

    if (tc_repl_ensure_slots(session, diag) != 0) {
        tc_statement_free(&stmt);
        return -1;
    }

    rc = tc_execute_statement(&stmt, session->slots, &session->symbols, diag);
    tc_statement_free(&stmt);
    return rc;
}

/*
 * @brief 去掉行尾换行符
 */
static void tc_repl_chomp(char *line) {
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

/*
 * @brief 进入交互式 REPL 主循环
 */
int tc_repl_run(TcDiagnostic *diag) {
    TcReplSession session;
    char line[TC_REPL_LINE_MAX];
    int should_quit = 0;

    tc_repl_session_init(&session);
    tc_diagnostic_set_source(diag, "<repl>", NULL);

    if (isatty(fileno(stdin)) && isatty(fileno(stderr))) {
        tc_repl_print_help();
    }

    while (!should_quit) {
        if (isatty(fileno(stderr))) {
            if (fputs(TC_REPL_PROMPT, stderr) == EOF) {
                break;
            }
            if (fflush(stderr) != 0) {
                break;
            }
        }
        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (feof(stdin)) {
                fputc('\n', stderr);
            }
            break;
        }

        tc_repl_chomp(line);
        if (tc_repl_is_skippable_line(line)) {
            continue;
        }
        if (tc_repl_handle_meta(&session, line, &should_quit)) {
            continue;
        }

        diag->source = line;
        if (tc_repl_eval_line(&session, line, diag) != 0) {
            tc_diagnostic_print(diag, stderr);
            tc_diagnostic_clear(diag);
            tc_diagnostic_set_source(diag, "<repl>", NULL);
        }
        session.line_no++;
    }

    tc_repl_session_free(&session);
    tc_diagnostic_clear(diag);
    return 0;
}
