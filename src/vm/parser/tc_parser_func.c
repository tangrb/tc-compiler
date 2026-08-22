/*
 * tc_parser_func.c — 函数定义语法解析（function_definition）
 *
 * 从 tc_parser.c 拆出：形参列表 / 函数定义。
 */
#include "tc_parser_func.h"

#include "tc_parser_internal.h"

#include <stdlib.h>
#include <string.h>
static int tc_func_param_push(TcFuncParam **params, size_t *count, size_t *cap,
                              const TcFuncParam *param, TcDiagnostic *diag, int line_no) {
    if (*count == *cap) {
        size_t new_cap = *cap == 0 ? 4 : *cap * 2;
        TcFuncParam *new_params = (TcFuncParam *)realloc(*params, new_cap * sizeof(TcFuncParam));
        if (!new_params) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        *params = new_params;
        *cap = new_cap;
    }
    (*params)[*count] = *param;
    (*count)++;
    return 0;
}

static void tc_func_def_fail(TcFuncDef *def) {
    size_t i = 0;
    free(def->name);
    free(def->return_struct_name);
    def->name = NULL;
    def->return_struct_name = NULL;
    tc_type_free(&def->return_type);
    for (i = 0; i < def->param_count; i++) {
        free(def->params[i].name);
        free(def->params[i].struct_type_name);
        tc_type_free(&def->params[i].type);
    }
    free(def->params);
    def->params = NULL;
    for (i = 0; i < def->body_count; i++) {
        tc_statement_free(&def->body[i]);
    }
    free(def->body);
    def->body = NULL;
}

int tc_parse_func_def(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                             size_t *index, TcModuleMode mode, const TcFileIndent *file_indent,
                             TcStatement *out, TcDiagnostic *diag) {
    TcSourceLine *func_line = &lines[*index];
    size_t tok_index = 0;
    int base_indent = func_line->indent;
    TcFuncDef def;
    TcVisibility vis = TC_VIS_NONE;
    size_t param_cap = 0;
    TcStmtBlock body;

    if (mode == TC_MODULE_PROGRAM) {
        const TcToken *tok = tc_peek(&func_line->tokens, tok_index);
        return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, func_line->line_no, tok->column,
                              "func is not allowed in #program mode");
    }

    memset(&def, 0, sizeof(def));
    def.line = func_line->line_no;
    def.func_id = -1;
    tc_stmt_block_init(&body);

    if (tc_parse_visibility_prefix(&func_line->tokens, &tok_index, mode, &vis, 1, diag,
                                   func_line->line_no) != 0) {
        return -1;
    }
    def.visibility = vis;
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_FUNC, func_line->line_no, diag) != 0) {
        return -1;
    }
    {
        const TcToken *name_tok = tc_peek(&func_line->tokens, tok_index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, func_line->line_no, name_tok->column, "expected function name");
        }
        def.name = tc_token_strdup(name_tok, func_line->line_no, diag);
        if (!def.name) {
            return -1;
        }
        tok_index++;
    }
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_LPAREN, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    while (tc_peek(&func_line->tokens, tok_index)->kind != TC_TOK_RPAREN) {
        TcFuncParam param;
        const TcToken *param_tok = tc_peek(&func_line->tokens, tok_index);

        memset(&param, 0, sizeof(param));
        if (param_tok->kind != TC_TOK_IDENTIFIER) {
            tc_func_def_fail(&def);
            return tc_syntax_error(diag, func_line->line_no, param_tok->column, "expected parameter name");
        }
        param.name = tc_token_strdup(param_tok, func_line->line_no, diag);
        if (!param.name) {
            tc_func_def_fail(&def);
            return -1;
        }
        tok_index++;
        if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_COLON, func_line->line_no, diag) != 0) {
            free(param.name);
            tc_func_def_fail(&def);
            return -1;
        }
        if (tc_parse_type_syntax(&func_line->tokens, &tok_index, func_line->line_no, 0, &param.type,
                                 &param.struct_type_name, diag) != 0) {
            free(param.name);
            tc_func_def_fail(&def);
            return -1;
        }
        if (tc_func_param_push(&def.params, &def.param_count, &param_cap, &param, diag,
                               func_line->line_no) != 0) {
            free(param.name);
            free(param.struct_type_name);
            tc_type_free(&param.type);
            tc_func_def_fail(&def);
            return -1;
        }
        if (tc_peek(&func_line->tokens, tok_index)->kind == TC_TOK_COMMA) {
            tok_index++;
        }
    }
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_RPAREN, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    if (tc_parse_type_syntax(&func_line->tokens, &tok_index, func_line->line_no, 1,
                             &def.return_type, &def.return_struct_name, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    if (tc_expect_token(&func_line->tokens, &tok_index, TC_TOK_THEN, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }
    if (tc_expect_stmt_end(&func_line->tokens, &tok_index, func_line->line_no, diag) != 0) {
        tc_func_def_fail(&def);
        return -1;
    }

    (*index)++;
    if (tc_parse_block_body_mode(ctx, lines, line_count, index, base_indent, file_indent,
                                 TC_MODULE_FUNC_BODY, &body, diag) != 0) {
        tc_func_def_fail(&def);
        tc_stmt_block_free(&body);
        return -1;
    }
    if (*index >= line_count || tc_first_token_kind(&lines[*index]) != TC_TOK_END) {
        tc_func_def_fail(&def);
        tc_stmt_block_free(&body);
        return tc_indent_diag(diag, TC_CE_MISSING_END, func_line->line_no,
                              "missing end for function definition");
    }
    if (lines[*index].indent != base_indent) {
        tc_func_def_fail(&def);
        tc_stmt_block_free(&body);
        return tc_indent_diag(diag, TC_CE_INDENT_ELSE_END, lines[*index].line_no,
                              "end indentation does not match function");
    }
    if (tc_end_line_check(&lines[*index], diag) != 0) {
        tc_func_def_fail(&def);
        tc_stmt_block_free(&body);
        return -1;
    }
    (*index)++;

    def.body = body.items;
    def.body_count = body.count;
    body.items = NULL;
    out->kind = TC_STMT_FUNC_DEF;
    out->u.func_def = def;
    return 0;
}
