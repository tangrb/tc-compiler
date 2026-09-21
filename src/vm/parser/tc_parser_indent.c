/*
 * tc_parser_indent.c — 缩进测量、skippable 行、块体解析
 */
#include "tc_parser_indent.h"

#include "tc_parser.h"
#include "tc_parser_internal.h"
#include "tc_parser_free.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int tc_is_skippable_line(const char *line) {
    if (line == NULL) {
        return 1;
    }
    if (tc_is_only_whitespace(line)) {
        return 1;
    }
    return tc_is_comment_only_line(line);
}

int tc_indent_diag(TcDiagnostic *diag, TcErrorKind kind, int line_no, const char *message) {
    tc_diagnostic_set(diag, kind, line_no, TC_COLUMN_UNKNOWN, message);
    return -1;
}

/* 行首缩进测量：缩进只能由 ASCII 空格 U+0020 组成，每 4 个空格为一级，
 * 行首空格总数必须能被 4 整除；行首出现水平制表符 U+0009 一律报
 * TC_CE_INDENT_MIXED（无论是否与空格混用）。 */
int tc_measure_line_indent(const char *line, int line_no, TcDiagnostic *diag,
                             int *out_indent) {
    int spaces = 0;
    const char *cursor = line;

    while (*cursor == ' ' || *cursor == '\t') {
        if (*cursor == '\t') {
            return tc_indent_diag(diag, TC_CE_INDENT_MIXED, line_no,
                                  "mixed spaces and tabs in indentation");
        }
        spaces++;
        cursor++;
    }

    if (spaces % 4 != 0) {
        return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, line_no,
                              "insufficient indentation in block");
    }
    *out_indent = spaces;
    return 0;
}

void tc_source_lines_free(TcSourceLine *lines, size_t count) {
    size_t i = 0;

    if (!lines) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(lines[i].text);
        lines[i].text = NULL;
        tc_token_list_free(&lines[i].tokens);
    }
    free(lines);
}

/* 块内语句缩进校验：块内语句相对块头必须恰好增加一级（固定 4 空格）。
 * 一次增加多级（如 8 空格）、或增加不足一级（如 2 空格）均报缩进错误；
 * 回退到不属于本块的级别由调用方按 end/else 对齐检查判定。 */
int tc_block_indent_valid(const TcFileIndent *file_indent, int base_indent, int indent,
                                 TcDiagnostic *diag, int line_no) {
    int delta = indent - base_indent;

    if (delta <= 0) {
        return 0; /* 块结束或不属于本块，由调用方判定 */
    }
    if (delta != file_indent->indent_width) {
        return tc_indent_diag(diag, TC_CE_INDENT_INSUFFICIENT, line_no,
                              "insufficient indentation in block");
    }
    return 0;
}

void tc_stmt_block_init(TcStmtBlock *block) {
    block->items = NULL;
    block->count = 0;
    block->capacity = 0;
}

void tc_stmt_block_free(TcStmtBlock *block) {
    size_t i = 0;

    for (i = 0; i < block->count; i++) {
        tc_statement_free(&block->items[i]);
    }
    free(block->items);
    block->items = NULL;
    block->count = 0;
    block->capacity = 0;
}

static int tc_stmt_block_push(TcStmtBlock *block, const TcStatement *stmt, TcDiagnostic *diag,
                              int line_no) {
    if (block->count == block->capacity) {
        size_t new_cap = block->capacity == 0 ? 4 : block->capacity * 2;
        TcStatement *items =
            (TcStatement *)realloc(block->items, new_cap * sizeof(TcStatement));

        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        block->items = items;
        block->capacity = new_cap;
    }
    block->items[block->count++] = *stmt;
    return 0;
}

int tc_first_token_kind(const TcSourceLine *line) {
    if (line->tokens.count == 0) {
        return TC_TOK_EOF;
    }
    return (int)line->tokens.items[0].kind;
}

/** end 行尾随 token 检查：`end`（可选分号）后必须行尾；
 * 尾随 token 报 TC_CE_SYNTAX（附录 A：块以 `end` 收尾）。
 * 行 token 列表末尾含 TC_TOK_EOF 哨兵，需跳过。 */
int tc_end_line_check(const TcSourceLine *line, TcDiagnostic *diag) {
    size_t i = 1;

    if (line->tokens.count > 1 && line->tokens.items[1].kind == TC_TOK_SEMICOLON) {
        i = 2;
    }
    while (i < line->tokens.count && line->tokens.items[i].kind == TC_TOK_EOF) {
        i++;
    }
    if (i < line->tokens.count) {
        return tc_syntax_error(diag, line->line_no, line->tokens.items[i].column,
                               "unexpected trailing tokens after end");
    }
    return 0;
}

int tc_parse_block_body_mode(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                                    size_t *index, int base_indent,
                                    const TcFileIndent *file_indent, TcModuleMode mode,
                                    const char *header_keyword, TcStmtBlock *block,
                                    TcDiagnostic *diag) {
    while (*index < line_count) {
        TcSourceLine *line = &lines[*index];
        TcStatement stmt;
        int first_kind = 0;

        if (line->indent <= base_indent) {
            break;
        }
        first_kind = tc_first_token_kind(line);
        /*
         * `else` / `end` 只要与对应块头不对齐（过深或过浅）一律报
         * TC_CE_INDENT_ELSE_END。附录 A.2 末段与附录 B.1 把「else/end 对不齐」
         * 从 INDENT_INSUFFICIENT 中单列，故对齐判定先于通用缩进增量判定
         * （§11 第 3 条：专用码优先）。
         */
        if (first_kind == TC_TOK_ELSE) {
            return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, line->line_no,
                                  "else must appear at same indentation as if");
        }
        if (first_kind == TC_TOK_END) {
            char msg[96];

            (void)snprintf(msg, sizeof(msg), "end indentation does not match %s",
                           header_keyword ? header_keyword : "block header");
            return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, line->line_no, msg);
        }
        if (tc_block_indent_valid(file_indent, base_indent, line->indent, diag,
                                  line->line_no) != 0) {
            return -1;
        }

        memset(&stmt, 0, sizeof(stmt));
        if (first_kind == TC_TOK_IF) {
            if (tc_parse_if_stmt(ctx, lines, line_count, index, file_indent, &stmt, diag) != 0) {
                return -1;
            }
        } else if (first_kind == TC_TOK_WHILE) {
            if (tc_parse_while_stmt(ctx, lines, line_count, index, file_indent, &stmt, diag) != 0) {
                return -1;
            }
        } else {
            if (tc_parse_statement_mode(ctx, &line->tokens, line->line_no, mode, &stmt, diag) != 0) {
                return -1;
            }
            (*index)++;
        }

        if (tc_stmt_block_push(block, &stmt, diag, line->line_no) != 0) {
            tc_statement_free(&stmt);
            return -1;
        }
    }
    return 0;
}

int tc_parse_block_body(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                               size_t *index, int base_indent,
                               const TcFileIndent *file_indent, const char *header_keyword,
                               TcStmtBlock *block, TcDiagnostic *diag) {
    return tc_parse_block_body_mode(ctx, lines, line_count, index, base_indent, file_indent,
                                    TC_MODULE_PROGRAM, header_keyword, block, diag);
}
