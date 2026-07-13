/*
 * tc_repl.c — TC-VM 交互式 REPL 实现
 *
 * 逐行读取 TC 源码，经 Lexer → Parser → 增量 Analyze → Execute 单条执行。
 * 会话内变量定义与赋值状态跨行保留。
 *
 * 内置命令（以 ':' 开头）：
 *   :quit / :exit / :q  退出 REPL
 *   :reset              清空会话（变量、历史）
 *   :vars               列出当前所有变量及其值
 *   :help               显示帮助信息
 */
#include "tc_repl.h"

#include "tc_analyzer.h"
#include "tc_diagnostic.h"
#include "tc_executor.h"
#include "tc_lexer.h"
#include "tc_parser.h"
#include "tc_semantics.h"
#include "tc_symbol.h"
#include "tc_warning.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TC_REPL_PROMPT "tc> "
#define TC_REPL_INIT_NONE (-1)

/* ------------------------------------------------------------------ */
/*  行类型判断                                                          */
/* ------------------------------------------------------------------ */

static int tc_repl_is_only_whitespace(const char *line) {
    while (*line != '\0' && *line != '\r' && *line != '\n') {
        if (*line != ' ' && *line != '\t') {
            return 0;
        }
        line++;
    }
    return 1;
}

/** 判断是否为注释行（';' 开头）、空行或全空白行 */
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

/* ------------------------------------------------------------------ */
/*  会话生命周期管理                                                     */
/* ------------------------------------------------------------------ */

static void tc_repl_analyze_ctx_init(TcReplAnalyzeCtx *ctx) {
    ctx->stmt_count = 0;
    ctx->last_init_stmt_index = NULL;
    ctx->last_init_capacity = 0;
}

static void tc_repl_analyze_ctx_free(TcReplAnalyzeCtx *ctx) {
    free(ctx->last_init_stmt_index);
    ctx->last_init_stmt_index = NULL;
    ctx->last_init_capacity = 0;
    ctx->stmt_count = 0;
}

static void tc_repl_session_init(TcReplSession *session) {
    tc_symbol_table_init(&session->symbols);
    tc_repl_analyze_ctx_init(&session->analyze_ctx);
    session->slots = NULL;
    session->slots_capacity = 0;
    session->line_no = 1;
}

static void tc_repl_session_free(TcReplSession *session) {
    tc_symbol_table_free(&session->symbols);
    tc_repl_analyze_ctx_free(&session->analyze_ctx);
    free(session->slots);
    session->slots = NULL;
    session->slots_capacity = 0;
    session->line_no = 1;
}

static void tc_repl_session_reset(TcReplSession *session) {
    tc_repl_session_free(session);
    tc_repl_session_init(session);
}

/* ------------------------------------------------------------------ */
/*  容量保障与初始化追踪                                                 */
/* ------------------------------------------------------------------ */

/** 确保初始化追踪数组与符号表槽位容量一致 */
static int tc_repl_ensure_init_tracking(TcReplSession *session, TcDiagnostic *diag) {
    if (session->symbols.count <= session->analyze_ctx.last_init_capacity) {
        return 0;
    }

    {
        size_t old_cap = session->analyze_ctx.last_init_capacity;
        size_t new_cap = session->symbols.count;
        int *new_track =
            (int *)realloc(session->analyze_ctx.last_init_stmt_index, new_cap * sizeof(int));
        size_t i = 0;

        if (!new_track) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        for (i = old_cap; i < new_cap; i++) {
            new_track[i] = TC_REPL_INIT_NONE;
        }
        session->analyze_ctx.last_init_stmt_index = new_track;
        session->analyze_ctx.last_init_capacity = new_cap;
    }
    return 0;
}

/** 确保变量槽数组容量与符号表一致 */
static int tc_repl_ensure_slots(TcReplSession *session, TcDiagnostic *diag) {
    if (tc_repl_ensure_init_tracking(session, diag) != 0) {
        return -1;
    }
    if (session->symbols.count <= session->slots_capacity) {
        return 0;
    }

    {
        TcValue *new_slots =
            (TcValue *)realloc(session->slots, session->symbols.count * sizeof(TcValue));
        if (!new_slots) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        tc_slots_init_uninitialized(new_slots + session->slots_capacity,
                                    session->symbols.count - session->slots_capacity);
        session->slots = new_slots;
        session->slots_capacity = session->symbols.count;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  语句执行后的副作用记录                                                */
/* ------------------------------------------------------------------ */

/** 记录成功执行语句的初始化副作用：更新 last_init_stmt_index */
static void tc_repl_record_stmt(TcReplSession *session, const TcStatement *stmt) {
    size_t stmt_index = session->analyze_ctx.stmt_count;

    if (stmt->kind == TC_STMT_ASSIGN) {
        const TcSymbol *sym =
            tc_symbol_table_find(&session->symbols, stmt->u.assign.name);
        if (sym != NULL) {
            session->analyze_ctx.last_init_stmt_index[sym->slot] = (int)stmt_index;
        }
    } else if (stmt->kind == TC_STMT_READ) {
        const TcSymbol *sym = tc_symbol_table_find(&session->symbols, stmt->u.io_read.name);
        if (sym != NULL) {
            session->analyze_ctx.last_init_stmt_index[sym->slot] = (int)stmt_index;
        }
    }
    session->analyze_ctx.stmt_count++;
}

/* ------------------------------------------------------------------ */
/*  变量列表与帮助输出                                                    */
/* ------------------------------------------------------------------ */

/** 格式化变量值到缓冲区 */
static void tc_repl_format_value(const TcValue *value, char *buf, size_t buf_size) {
    if (tc_type_is_signed(value->type)) {
        int64_t signed_value = tc_bits_to_signed(value->type, value->bits);
        snprintf(buf, buf_size, "%" PRId64, signed_value);
    } else {
        uint64_t unsigned_value = tc_value_to_unsigned(value->type, value->bits);
        snprintf(buf, buf_size, "%" PRIu64, unsigned_value);
    }
}

/** 打印当前会话中所有变量 */
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
        printf("%s: %s = %s\n", symbol->name, tc_type_name(symbol->type), value_text);
    }
}

/** 打印内置命令帮助 */
static void tc_repl_print_help(void) {
    printf("TC-VM interactive mode. Enter one TC statement per line.\n"
           "\n"
           "Meta commands:\n"
           "  :quit, :exit, :q   exit REPL\n"
           "  :reset             clear all variables and session history\n"
           "  :vars              list current variables\n"
           "  :help              show this help\n"
           "\n"
           "Tip: use :reset during long sessions to reclaim memory.\n"
           "Note: read() and REPL both use stdin.\n");
}

/* ------------------------------------------------------------------ */
/*  内置命令处理                                                        */
/* ------------------------------------------------------------------ */

/**
 * 处理 REPL 内置命令。
 * @return 1 表示已处理（含退出请求）；0 表示非内置命令（应为 TC 语句）
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

/* ------------------------------------------------------------------ */
/*  单行求值                                                           */
/* ------------------------------------------------------------------ */

/** 解析并执行一行 TC 源码（Lexer → Parser → Analyze → Execute） */
static int tc_repl_eval_line(TcReplSession *session, const char *line, TcDiagnostic *diag) {
    TcTokenList tokens;
    TcStatement stmt;
    TcWarningList warnings;
    TcParserCtx parse_ctx;
    int added_symbol = 0;
    int rc = 0;

    tc_token_list_init(&tokens);
    tc_warning_list_init(&warnings);
    if (tc_tokenize_line(line, session->line_no, &tokens, diag) != 0) {
        tc_token_list_free(&tokens);
        tc_warning_list_free(&warnings);
        return -1;
    }
    if (tokens.count > 0 && tokens.items[0].kind == TC_TOK_IF) {
        tc_diagnostic_set(diag, TC_ERR_SYNTAX, session->line_no, tokens.items[0].column,
                          "if statements are not supported in REPL mode");
        tc_token_list_free(&tokens);
        tc_warning_list_free(&warnings);
        return -1;
    }
    parse_ctx.depth = 0;
    if (tc_parse_statement(&parse_ctx, &tokens, session->line_no, &stmt, diag) != 0) {
        tc_statement_free(&stmt);
        tc_token_list_free(&tokens);
        tc_warning_list_free(&warnings);
        return -1;
    }
    tc_token_list_free(&tokens);

    /* 增量静态分析 */
    if (tc_analyze_statement(&stmt, &session->symbols, &session->analyze_ctx, &warnings, diag) !=
        0) {
        tc_statement_free(&stmt);
        tc_warning_list_free(&warnings);
        return -1;
    }

    if (stmt.kind == TC_STMT_VAR_DEF || stmt.kind == TC_STMT_CONST_DEF) {
        added_symbol = 1;
    }

    /* 打印警告（警告不阻断执行） */
    if (warnings.count > 0) {
        tc_warning_list_print(&warnings, stderr);
    }
    tc_warning_list_free(&warnings);

    if (tc_repl_ensure_slots(session, diag) != 0) {
        if (added_symbol) {
            tc_symbol_table_pop_last(&session->symbols);
        }
        tc_statement_free(&stmt);
        return -1;
    }

    /* 执行 */
    rc = tc_execute_statement(&stmt, session->slots, &session->symbols, diag);
    if (rc != 0) {
        tc_statement_free(&stmt);
        return -1;
    }

    tc_repl_record_stmt(session, &stmt);
    tc_statement_free(&stmt);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  行尾处理                                                           */
/* ------------------------------------------------------------------ */

/** 去掉字符串末尾的换行符 */
static void tc_repl_chomp(char *line) {
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

/* ------------------------------------------------------------------ */
/*  REPL 主循环                                                         */
/* ------------------------------------------------------------------ */

int tc_repl_run(TcDiagnostic *diag) {
    TcReplSession session;
    char *line = NULL;
    size_t line_size = 0;
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
        if (getline(&line, &line_size, stdin) == -1) {
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

        tc_diagnostic_set_source(diag, "<repl>", line);
        if (tc_repl_eval_line(&session, line, diag) != 0) {
            tc_diagnostic_print(diag, stderr);
            tc_diagnostic_clear(diag);
            tc_diagnostic_set_source(diag, "<repl>", NULL);
        }
        session.line_no++;
    }

    free(line);
    tc_repl_session_free(&session);
    tc_diagnostic_clear(diag);
    return 0;
}
