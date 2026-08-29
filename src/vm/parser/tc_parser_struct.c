/*
 * tc_parser_struct.c — struct 类型定义语法解析（附录 A struct_definition）
 *
 * 从 tc_parser.c 拆出：struct 定义 / 字段行 / @padding 属性。
 */
#include "tc_parser_struct.h"

#include "tc_parser_internal.h"

#include <stdlib.h>
#include <string.h>
static int tc_parse_optional_padding(const TcTokenList *tokens, size_t *index, int line_no,
                                     uint64_t *out_padding, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    *out_padding = 0;
    if (tok->kind != TC_TOK_AT) {
        return 0;
    }
    (*index)++;
    tok = tc_peek(tokens, *index);
    if (tok->kind != TC_TOK_IDENTIFIER || !tc_token_is_ident_named(tok, "padding")) {
        return tc_syntax_error(diag, line_no, tok->column, "expected padding");
    }
    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    tok = tc_peek(tokens, *index);
    if (tok->kind == TC_TOK_INTEGER) {
        /* §3.9.3 / 附录 A：@padding(N) 的 N 须为无后缀非负十进制整数字面量
         * （允许 0）；负号、u/U 后缀或非十进制进制前缀由静态语义拒绝。 */
        if (tok->u.literal.negative || tok->u.literal.unsigned_suffix ||
            tok->u.literal.radix != 10) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line_no, tok->column,
                              "@padding size must be a non-negative decimal integer literal "
                              "without suffix");
            return -1;
        }
        *out_padding = tok->u.literal.magnitude;
        (*index)++;
    } else {
        return tc_syntax_error(diag, line_no, tok->column, "expected padding size");
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        return -1;
    }
    return 0;
}

static int tc_struct_field_push(TcStructField **fields, size_t *count, size_t *cap,
                                const TcStructField *field, TcDiagnostic *diag, int line_no) {
    if (*count == *cap) {
        size_t new_cap = *cap == 0 ? 4 : *cap * 2;
        TcStructField *new_fields = (TcStructField *)realloc(*fields, new_cap * sizeof(TcStructField));
        if (!new_fields) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        *fields = new_fields;
        *cap = new_cap;
    }
    (*fields)[*count] = *field;
    (*count)++;
    return 0;
}

static void tc_struct_def_fail(TcStructDef *def) {
    size_t i = 0;
    free(def->name);
    def->name = NULL;
    for (i = 0; i < def->field_count; i++) {
        free(def->fields[i].name);
        free(def->fields[i].struct_type_name);
        tc_type_free(&def->fields[i].type);
    }
    free(def->fields);
    def->fields = NULL;
    def->field_count = 0;
}

static int tc_parse_struct_field_line(TcParserCtx *ctx, const TcSourceLine *line,
                                      TcStructField *out, TcDiagnostic *diag) {
    size_t index = 0;
    const TcToken *tok = tc_peek(&line->tokens, index);
    char *struct_name = NULL;
    TcStructField field;

    memset(&field, 0, sizeof(field));
    if (tok->kind == TC_TOK_VAR) {
        field.is_var = 1;
    } else if (tok->kind == TC_TOK_LET) {
        field.is_var = 0;
    } else {
        return tc_syntax_error(diag, line->line_no, tok->column, "expected var or let field");
    }
    index++;
    tok = tc_peek(&line->tokens, index);
    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line->line_no, tok->column, "expected field name");
    }
    field.name = tc_token_strdup(tok, line->line_no, diag);
    if (!field.name) {
        return -1;
    }
    index++;
    if (tc_expect_token(&line->tokens, &index, TC_TOK_COLON, line->line_no, diag) != 0) {
        free(field.name);
        return -1;
    }
    if (tc_parse_type_syntax(&line->tokens, &index, line->line_no, 0, &field.type, &struct_name,
                             diag) != 0) {
        free(field.name);
        return -1;
    }
    field.struct_type_name = struct_name;
    if (tc_parse_optional_padding(&line->tokens, &index, line->line_no, &field.padding, diag) != 0) {
        free(field.name);
        free(field.struct_type_name);
        tc_type_free(&field.type);
        return -1;
    }
    if (tc_expect_stmt_end(&line->tokens, &index, line->line_no, diag) != 0) {
        free(field.name);
        free(field.struct_type_name);
        tc_type_free(&field.type);
        return -1;
    }
    (void)ctx;
    *out = field;
    return 0;
}

int tc_parse_struct_def(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                               size_t *index, TcModuleMode mode, const TcFileIndent *file_indent,
                               TcStatement *out, TcDiagnostic *diag) {
    TcSourceLine *struct_line = &lines[*index];
    size_t tok_index = 0;
    int base_indent = struct_line->indent;
    TcStructDef def;
    TcVisibility vis = TC_VIS_NONE;
    size_t field_cap = 0;

    memset(&def, 0, sizeof(def));
    def.line = struct_line->line_no;
    def.struct_id = -1;

    if (tc_parse_visibility_prefix(&struct_line->tokens, &tok_index, mode, &vis,
                                   mode == TC_MODULE_LIB, diag, struct_line->line_no) != 0) {
        return -1;
    }
    def.visibility = vis;
    if (tc_expect_token(&struct_line->tokens, &tok_index, TC_TOK_STRUCT, struct_line->line_no,
                        diag) != 0) {
        return -1;
    }
    {
        const TcToken *name_tok = tc_peek(&struct_line->tokens, tok_index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, struct_line->line_no, name_tok->column,
                                   "expected struct name");
        }
        def.name = tc_token_strdup(name_tok, struct_line->line_no, diag);
        if (!def.name) {
            return -1;
        }
        tok_index++;
    }
    if (tc_expect_token(&struct_line->tokens, &tok_index, TC_TOK_THEN, struct_line->line_no,
                        diag) != 0) {
        tc_struct_def_fail(&def);
        return -1;
    }
    if (tc_expect_stmt_end(&struct_line->tokens, &tok_index, struct_line->line_no, diag) != 0) {
        tc_struct_def_fail(&def);
        return -1;
    }

    (*index)++;
    while (*index < line_count && lines[*index].indent > base_indent) {
        TcStructField field;
        TcSourceLine *field_line = &lines[*index];

        if (tc_first_token_kind(field_line) == TC_TOK_END) {
            /* end 缩进比 struct 深：与 if/while 块体一致，按 §3.9.1 报对齐错误 */
            tc_struct_def_fail(&def);
            return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, field_line->line_no,
                                  "end indentation does not match struct");
        }
        if (tc_block_indent_valid(file_indent, base_indent, field_line->indent, diag,
                                  field_line->line_no) != 0) {
            tc_struct_def_fail(&def);
            return -1;
        }
        memset(&field, 0, sizeof(field));
        if (tc_parse_struct_field_line(ctx, field_line, &field, diag) != 0) {
            tc_struct_def_fail(&def);
            return -1;
        }
        if (tc_struct_field_push(&def.fields, &def.field_count, &field_cap,
                                 &field, diag, field_line->line_no) != 0) {
            free(field.name);
            free(field.struct_type_name);
            tc_type_free(&field.type);
            tc_struct_def_fail(&def);
            return -1;
        }
        (*index)++;
    }

    if (*index >= line_count || tc_first_token_kind(&lines[*index]) != TC_TOK_END) {
        tc_struct_def_fail(&def);
        return tc_indent_diag(diag, TC_CE_MISSING_END, struct_line->line_no,
                              "missing end for struct definition");
    }
    if (lines[*index].indent != base_indent) {
        tc_struct_def_fail(&def);
        return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                              "end indentation does not match struct");
    }
    if (tc_end_line_check(&lines[*index], diag) != 0) {
        tc_struct_def_fail(&def);
        return -1;
    }
    (*index)++;

    out->kind = TC_STMT_STRUCT_DEF;
    out->u.struct_def = def;
    return 0;
}
