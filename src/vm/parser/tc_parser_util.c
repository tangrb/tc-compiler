/*
 * tc_parser_util.c — 语法错误、expect、operand、binding_name、field_chain
 */
#include "tc_parser_util.h"

#include "tc_parser_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tc_parse_field_access_base(const TcTokenList *tokens, size_t *index, int line_no,
                                      char **out_base, TcDiagnostic *diag);


/* ------------------------------------------------------------------ */
/*  便捷错误报告辅助函数                                                 */
/* ------------------------------------------------------------------ */

int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_CE_SYNTAX, line, column, message);
    return -1;
}


int tc_operand_count_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_CE_OPERAND_COUNT, line, column, message);
    return -1;
}

int tc_expect_comma_or_operand_count(const TcTokenList *tokens, size_t *index, int line_no,
                                     TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_RPAREN) {
        /* 还应有操作数却已到 `)`：个数不足 */
        return tc_operand_count_error(diag, line_no, tok->column, "operand count error");
    }
    if (tok->kind != TC_TOK_COMMA) {
        return tc_syntax_error(diag, line_no, tok->column, "expected ,");
    }
    (*index)++;
    if (tc_peek(tokens, *index)->kind == TC_TOK_RPAREN) {
        return tc_operand_count_error(diag, line_no, tc_peek(tokens, *index)->column,
                                      "operand count error");
    }
    return 0;
}

int tc_expect_rparen_or_operand_count(const TcTokenList *tokens, size_t *index, int line_no,
                                      TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_COMMA) {
        /* 操作数已齐全却仍有 `,`：个数超出 */
        return tc_operand_count_error(diag, line_no, tok->column, "operand count error");
    }
    return tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag);
}

/* ------------------------------------------------------------------ */
/*  底层解析工具函数                                                     */
/* ------------------------------------------------------------------ */

/** 读取 Token 列表中的第 index 个 Token（不做越界检查） */
const TcToken *tc_peek(const TcTokenList *tokens, size_t index) {
    assert(tokens->count > 0);
    assert(index < tokens->count);
    return &tokens->items[index];
}

/*
 * @brief 解析一个操作数：变量引用或字面量
 * @param tokens  Token 列表
 * @param index   当前读取位置（解析成功后被推进）
 * @param line_no 当前行号
 * @param out     输出：解析结果 TcOperand
 * @param diag    诊断对象
 * @return 成功返回 0；失败返回 -1
 */
int tc_parse_operand(const TcTokenList *tokens, size_t *index, int line_no,
                            TcOperand *out, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_IDENTIFIER) {
        if (*index + 1 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
            return tc_parse_field_access_operand(tokens, index, line_no, out, diag);
        }
        out->kind = TC_OPERAND_VAR;
        out->u.name = tc_strndup(tok->start, tok->length);
        if (!out->u.name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
            return -1;
        }
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_SELF &&
        *index + 1 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
        /*
         * Self.<名> 后无 `.` → 限定标识符操作数（附录 A 的 operand 产生式
         * 含 qualified_identifier；语言标准 §6.1.1、§6.1.2）。
         * 有 `.` → 继续读字段链（Self.<名>.<字段…>）。
         * 限定名的绑定解析（static let/var、可见性、读写规则）由分析器完成。
         */
        int has_field = *index + 3 < tokens->count &&
                        tc_peek(tokens, *index + 3)->kind == TC_TOK_DOT;

        if (!has_field) {
            char *qualified = NULL;

            if (tc_parse_field_access_base(tokens, index, line_no, &qualified, diag) != 0) {
                return -1;
            }
            out->kind = TC_OPERAND_VAR;
            out->u.name = qualified;
            return 0;
        }
        return tc_parse_field_access_operand(tokens, index, line_no, out, diag);
    }

    if (tok->kind == TC_TOK_INTEGER) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_BOOL_LIT) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_FLOAT_LIT) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_NULLPTR) {
        out->kind = TC_OPERAND_LIT;
        memset(&out->u.lit, 0, sizeof(out->u.lit));
        out->u.lit.is_nullptr = 1;
        (*index)++;
        return 0;
    }

    return tc_syntax_error(diag, line_no, tok->column, "expected operand");
}

/** 断言当前位置的 Token 种类与期望的一致，然后推进 index */
int tc_expect_token(const TcTokenList *tokens, size_t *index, TcTokenKind kind,
                           int line_no, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    if (tok->kind != kind) {
        return tc_syntax_error(diag, line_no, tok->column, "unexpected token");
    }
    (*index)++;
    return 0;
}

/** 检查语句结尾：允许可选的分号后紧跟 EOF */
int tc_expect_stmt_end(const TcTokenList *tokens, size_t *index, int line_no,
                              TcDiagnostic *diag) {
    const TcToken *tail = tc_peek(tokens, *index);
    if (tail->kind == TC_TOK_SEMICOLON) {
        (*index)++;
        tail = tc_peek(tokens, *index);
    }
    if (tail->kind != TC_TOK_EOF) {
        return tc_syntax_error(diag, line_no, tail->column, "unexpected trailing tokens");
    }
    return 0;
}

int tc_token_is_type(const TcToken *tok) {
    return tok->kind == TC_TOK_INT_TYPE || tok->kind == TC_TOK_FLOAT_TYPE;
}

char *tc_token_strdup(const TcToken *tok, int line_no, TcDiagnostic *diag) {
    char *copy = NULL;

    if (!tok) {
        return NULL;
    }
    copy = (char *)tc_strndup(tok->start, tok->length);
    if (!copy) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
    }
    return copy;
}

int tc_token_is_ident_named(const TcToken *tok, const char *name) {
    size_t name_len = 0;

    if (!tok || !name || tok->kind != TC_TOK_IDENTIFIER) {
        return 0;
    }
    name_len = strlen(name);
    return tok->length == name_len && strncmp(tok->start, name, name_len) == 0;
}

int tc_parse_binding_name(const TcTokenList *tokens, size_t *index, int line_no,
                          char **out_name, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    const TcToken *member = NULL;
    size_t total = 0;
    char *name = NULL;

    if (!out_name) {
        return -1;
    }
    *out_name = NULL;

    if (tok->kind == TC_TOK_SELF) {
        if (*index + 2 >= tokens->count) {
            return tc_syntax_error(diag, line_no, tok->column, "expected Self.member");
        }
        if (tc_peek(tokens, *index + 1)->kind != TC_TOK_DOT) {
            return tc_syntax_error(diag, line_no, tok->column, "expected . after Self");
        }
        member = tc_peek(tokens, *index + 2);
        if (member->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member->column, "expected member name");
        }
        total = 5 + 1 + member->length + 1;
        name = (char *)malloc(total);
        if (!name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(name, total, "Self.%.*s", (int)member->length, member->start);
        *out_name = name;
        *index += 3;
        return 0;
    }

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected identifier");
    }
    if (*index + 2 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT &&
        tc_peek(tokens, *index + 2)->kind == TC_TOK_IDENTIFIER) {
        member = tc_peek(tokens, *index + 2);
        total = tok->length + 1 + member->length + 1;
        name = (char *)malloc(total);
        if (!name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(name, total, "%.*s.%.*s", (int)tok->length, tok->start, (int)member->length,
                 member->start);
        *out_name = name;
        *index += 3;
        return 0;
    }
    name = tc_token_strdup(tok, line_no, diag);
    if (!name) {
        return -1;
    }
    *out_name = name;
    (*index)++;
    return 0;
}

int tc_module_diag(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                          const char *message) {
    /* 模块语义错误：写入指定 TcErrorKind（非一律 SYNTAX） */
    tc_diagnostic_set(diag, kind, line, column, message);
    return -1;
}

void tc_string_list_free_local(char **items, size_t count) {
    size_t i = 0;
    if (!items) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

int tc_parse_field_chain(const TcTokenList *tokens, size_t *index, int line_no,
                                char **out_base, char ***out_fields, size_t *out_field_count,
                                TcDiagnostic *diag) {
    TcOperand operand;
    size_t saved = *index;

    memset(&operand, 0, sizeof(operand));
    *out_base = NULL;
    *out_fields = NULL;
    *out_field_count = 0;

    if (tc_parse_field_access_operand(tokens, index, line_no, &operand, diag) != 0) {
        return -1;
    }
    if (operand.kind != TC_OPERAND_FIELD_READ) {
        *index = saved;
        tc_operand_free(&operand);
        return tc_syntax_error(diag, line_no, TC_COLUMN_UNKNOWN, "expected field access");
    }
    *out_base = operand.u.field_read.base;
    *out_fields = operand.u.field_read.fields;
    *out_field_count = operand.u.field_read.field_count;
    operand.u.field_read.base = NULL;
    operand.u.field_read.fields = NULL;
    operand.u.field_read.field_count = 0;
    return 0;
}

int tc_parse_field_access_base(const TcTokenList *tokens, size_t *index, int line_no,
                                    char **out_base, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    *out_base = NULL;
    if (tok->kind == TC_TOK_SELF) {
        const TcToken *member_tok = NULL;
        size_t base_len = 0;

        if (tc_peek(tokens, *index + 1)->kind != TC_TOK_DOT) {
            return tc_syntax_error(diag, line_no, tok->column, "expected . after Self");
        }
        (*index) += 2;
        member_tok = tc_peek(tokens, *index);
        if (member_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member_tok->column, "expected member name");
        }
        base_len = 5 + member_tok->length + 1;
        *out_base = (char *)malloc(base_len);
        if (!*out_base) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, member_tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(*out_base, base_len, "Self.%.*s", (int)member_tok->length, member_tok->start);
        (*index)++;
        return 0;
    }

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected identifier");
    }

    if (*index + 3 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT &&
        tc_peek(tokens, *index + 2)->kind == TC_TOK_IDENTIFIER &&
        tc_peek(tokens, *index + 3)->kind == TC_TOK_DOT &&
        tok->length > 0 && tok->start[0] >= 'A' && tok->start[0] <= 'Z') {
        const TcToken *qual_tok = tok;
        const TcToken *member_tok = tc_peek(tokens, *index + 2);
        size_t base_len = qual_tok->length + 1 + member_tok->length + 1;

        *out_base = (char *)malloc(base_len);
        if (!*out_base) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(*out_base, base_len, "%.*s.%.*s", (int)qual_tok->length, qual_tok->start,
                 (int)member_tok->length, member_tok->start);
        *index += 3;
        return 0;
    }

    *out_base = tc_token_strdup(tok, line_no, diag);
    if (!*out_base) {
        return -1;
    }
    (*index)++;
    return 0;
}

int tc_parse_field_access_operand(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcOperand *out, TcDiagnostic *diag) {
    char *base = NULL;
    char **fields = NULL;
    size_t field_count = 0;
    size_t field_cap = 0;

    if (tc_parse_field_access_base(tokens, index, line_no, &base, diag) != 0) {
        return -1;
    }

    while (tc_peek(tokens, *index)->kind == TC_TOK_DOT) {
        char *field_name = NULL;
        const TcToken *tok = NULL;

        (*index)++;
        tok = tc_peek(tokens, *index);
        if (tok->kind != TC_TOK_IDENTIFIER) {
            free(base);
            tc_string_list_free_local(fields, field_count);
            return tc_syntax_error(diag, line_no, tok->column, "expected field name");
        }
        field_name = tc_token_strdup(tok, line_no, diag);
        if (!field_name) {
            free(base);
            tc_string_list_free_local(fields, field_count);
            return -1;
        }
        if (field_count == field_cap) {
            size_t new_cap = field_cap == 0 ? 4 : field_cap * 2;
            char **new_fields = (char **)realloc(fields, new_cap * sizeof(char *));

            if (!new_fields) {
                free(field_name);
                free(base);
                tc_string_list_free_local(fields, field_count);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                                  "memory allocation failed");
                return -1;
            }
            fields = new_fields;
            field_cap = new_cap;
        }
        fields[field_count++] = field_name;
        (*index)++;
    }

    if (field_count == 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        return tc_syntax_error(diag, line_no, TC_COLUMN_UNKNOWN, "expected field name");
    }

    if (out) {
        memset(&out->u.field_read.resolved, 0, sizeof(out->u.field_read.resolved));
        out->kind = TC_OPERAND_FIELD_READ;
        out->u.field_read.base = base;
        out->u.field_read.fields = fields;
        out->u.field_read.field_count = field_count;
    } else {
        free(base);
        tc_string_list_free_local(fields, field_count);
    }
    return 0;
}

/**
 * 解析可选的 public/private 前缀。
 * #program 禁止可见性；#lib 在 require_vis=1 时缺失则报 MISSING_VISIBILITY。
 */
int tc_parse_visibility_prefix(const TcTokenList *tokens, size_t *index,
                                      TcModuleMode mode, TcVisibility *out_vis,
                                      int require_vis, TcDiagnostic *diag, int line_no) {
    const TcToken *tok = tc_peek(tokens, *index);

    *out_vis = TC_VIS_NONE;
    if (tok->kind == TC_TOK_PUBLIC) {
        if (mode == TC_MODULE_PROGRAM) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "public is not allowed in #program mode");
        }
        /* 仅 #lib 模块顶层允许可见性；函数体等其它上下文一律拒绝（附录 A：suite
         * 的 statement 不含可见性前缀）。 */
        if (mode != TC_MODULE_LIB) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "visibility modifier is not allowed inside a function body");
        }
        *out_vis = TC_VIS_PUBLIC;
        (*index)++;
        return 0;
    }
    if (tok->kind == TC_TOK_PRIVATE) {
        if (mode == TC_MODULE_PROGRAM) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "private is not allowed in #program mode");
        }
        if (mode != TC_MODULE_LIB) {
            return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                                  "visibility modifier is not allowed inside a function body");
        }
        *out_vis = TC_VIS_PRIVATE;
        (*index)++;
        return 0;
    }
    if (require_vis && mode == TC_MODULE_LIB) {
        return tc_module_diag(diag, TC_CE_MISSING_VISIBILITY, line_no, tok->column,
                              "missing public or private visibility");
    }
    return 0;
}
