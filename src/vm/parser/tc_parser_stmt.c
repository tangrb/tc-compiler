/*
 * tc_parser_stmt.c — 语句级语法解析（statement）
 *
 * 从 tc_parser.c 拆出：var/let、static、import、return、funcall、
 * field assign、ptr/memblock/memcopy 语句与 I/O 语句。
 */
#include "tc_parser_stmt.h"

#include "tc_parser_internal.h"
#include "tc_parser_rhs.h"
#include "tc_parser_type.h"

#include <stdlib.h>
#include <string.h>

static int tc_rhs_kind_is_arg_value(TcRhsKind kind) {
    switch (kind) {
    case TC_RHS_LIT:
    case TC_RHS_CONST_REF:
    case TC_RHS_FIELD_READ:
    case TC_RHS_SELF_MEMBER:
    case TC_RHS_MEMBLOCK_CONSTRUCTOR:
    case TC_RHS_MEMBLOCK_COUNT:
    case TC_RHS_STRUCT_CONSTRUCTOR:
        return 1;
    default:
        return 0;
    }
}

static int tc_parse_named_arg_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                  int line_no, TcRhs *out, TcDiagnostic *diag) {
    if (tc_parse_rhs(ctx, tokens, index, line_no, out, diag) != 0) {
        return -1;
    }
    if (!tc_rhs_kind_is_arg_value(out->kind)) {
        tc_rhs_free(out);
        memset(out, 0, sizeof(*out));
        return tc_syntax_error(diag, line_no, TC_COLUMN_UNKNOWN,
                               "expected operand, memblock constructor, or struct constructor");
    }
    return 0;
}

int tc_parse_io_write_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcIoWrite *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = tc_type_tag_singleton(TC_INT32);
    memset(&out->fmt, 0, sizeof(out->fmt));
    out->fmt.spec = TC_FMT_NONE;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (!tc_token_is_type(type_tok)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = tc_type_tag_singleton(type_tok->u.int_type);
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    /* 可选的格式说明符（位于第二参数位置，后跟逗号和操作数） */
    {
        const TcToken *next = tc_peek(tokens, *index);
        if (next->kind == TC_TOK_FORMAT_SPEC) {
            out->fmt = next->u.format_spec;
            (*index)++;
            next = tc_peek(tokens, *index);
            if (next->kind == TC_TOK_RPAREN) {
                return tc_operand_count_error(diag, line_no, next->column, "operand count error");
            }
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
            next = tc_peek(tokens, *index);
            if (next->kind == TC_TOK_RPAREN) {
                return tc_operand_count_error(diag, line_no, next->column, "operand count error");
            }
        }
    }

    memset(&out->operand, 0, sizeof(out->operand));
    if (tc_parse_operand(tokens, index, line_no, &out->operand, diag) != 0) {
        return -1;
    }

    /* 检查是否有多余的操作数 */
    {
        const TcToken *tail = tc_peek(tokens, *index);
        if (tail->kind == TC_TOK_COMMA) {
            return tc_operand_count_error(diag, line_no, tail->column, "operand count error");
        }
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_operand_free(&out->operand);
        return -1;
    }
    return 0;
}

int tc_parse_var_or_const_def(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                     int line_no, int is_const, TcStatement *out,
                                     TcDiagnostic *diag) {
    char *name = NULL;
    char *struct_name = NULL;
    TcType full_type;
    TcRhs rhs;

    (*index)++;

    {
        const TcToken *name_tok = tc_peek(tokens, *index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
        }
        name = tc_token_strdup(name_tok, line_no, diag);
        if (!name) {
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
        free(name);
        return -1;
    }

    if (tc_parse_type_syntax(tokens, index, line_no, 0, &full_type, &struct_name, diag) != 0) {
        free(name);
        return -1;
    }

    {
        const TcToken *maybe_eq = tc_peek(tokens, *index);
        if (maybe_eq->kind == TC_TOK_EQUAL) {
            (*index)++;
            if (tc_peek(tokens, *index)->kind == TC_TOK_EOF ||
                tc_peek(tokens, *index)->kind == TC_TOK_SEMICOLON) {
                free(name);
                free(struct_name);
                tc_type_free(&full_type);
                if (!is_const) {
                    tc_diagnostic_set(diag, TC_CE_VAR_MISSING_INIT, line_no, maybe_eq->column,
                                      "variable definition requires initializer");
                    return -1;
                }
                return tc_syntax_error(diag, line_no, maybe_eq->column,
                                       "constant definition requires initializer");
            }
            memset(&rhs, 0, sizeof(rhs));
            if (is_const) {
                if (tc_parse_const_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                    free(name);
                    free(struct_name);
                    tc_type_free(&full_type);
                    tc_rhs_free(&rhs);
                    return -1;
                }
            } else if (tc_peek(tokens, *index)->kind == TC_TOK_FUNCALL) {
                if (tc_parse_funcall_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                    free(name);
                    free(struct_name);
                    tc_type_free(&full_type);
                    tc_rhs_free(&rhs);
                    return -1;
                }
            } else if (tc_parse_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                free(name);
                free(struct_name);
                tc_type_free(&full_type);
                tc_rhs_free(&rhs);
                return -1;
            }
        } else {
            free(name);
            free(struct_name);
            tc_type_free(&full_type);
            if (is_const) {
                return tc_syntax_error(diag, line_no, maybe_eq->column,
                                       "constant definition requires initializer");
            }
            tc_diagnostic_set(diag, TC_CE_VAR_MISSING_INIT, line_no, maybe_eq->column,
                              "variable definition requires initializer");
            return -1;
        }
    }

    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        tc_rhs_free(&rhs);
        return -1;
    }

    if (is_const) {
        out->kind = TC_STMT_CONST_DEF;
        out->u.const_def.line = line_no;
        out->u.const_def.name = name;
        out->u.const_def.full_type = full_type;
        out->u.const_def.struct_type_name = struct_name;
        out->u.const_def.rhs = rhs;
    } else {
        out->kind = TC_STMT_VAR_DEF;
        out->u.var_def.line = line_no;
        out->u.var_def.name = name;
        out->u.var_def.full_type = full_type;
        out->u.var_def.struct_type_name = struct_name;
        out->u.var_def.rhs = rhs;
    }
    return 0;
}

int tc_parse_static_def(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                               int line_no, TcModuleMode mode, TcVisibility vis,
                               TcStatement *out, TcDiagnostic *diag) {
    int is_const = 0;
    char *name = NULL;
    char *struct_name = NULL;
    TcType full_type;
    TcRhs rhs;

    if (mode == TC_MODULE_PROGRAM) {
        const TcToken *tok = tc_peek(tokens, *index);
        return tc_module_diag(diag, TC_CE_PROGRAM_MODE_MISUSE, line_no, tok->column,
                              "static is not allowed in #program mode");
    }
    if (mode == TC_MODULE_LIB && vis == TC_VIS_NONE) {
        const TcToken *tok = tc_peek(tokens, *index);
        return tc_module_diag(diag, TC_CE_MISSING_VISIBILITY, line_no, tok->column,
                              "missing public or private visibility");
    }

    (*index)++; /* static */
    {
        const TcToken *kind_tok = tc_peek(tokens, *index);
        if (kind_tok->kind == TC_TOK_VAR) {
            is_const = 0;
        } else if (kind_tok->kind == TC_TOK_LET) {
            is_const = 1;
        } else {
            return tc_syntax_error(diag, line_no, kind_tok->column, "expected var or let after static");
        }
        (*index)++;
    }

    {
        const TcToken *name_tok = tc_peek(tokens, *index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
        }
        name = tc_token_strdup(name_tok, line_no, diag);
        if (!name) {
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
        free(name);
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &full_type, &struct_name, diag) != 0) {
        free(name);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_EQUAL, line_no, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        return -1;
    }
    memset(&rhs, 0, sizeof(rhs));
    if (is_const) {
        if (tc_parse_const_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
            free(name);
            free(struct_name);
            tc_type_free(&full_type);
            return -1;
        }
    } else if (tc_parse_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(name);
        free(struct_name);
        tc_type_free(&full_type);
        tc_rhs_free(&rhs);
        return -1;
    }

    if (is_const) {
        out->kind = TC_STMT_STATIC_LET_DEF;
        out->u.static_let_def.line = line_no;
        out->u.static_let_def.visibility = vis;
        out->u.static_let_def.name = name;
        out->u.static_let_def.type = full_type;
        out->u.static_let_def.struct_type_name = struct_name;
        out->u.static_let_def.rhs = rhs;
    } else {
        out->kind = TC_STMT_STATIC_VAR_DEF;
        out->u.static_var_def.line = line_no;
        out->u.static_var_def.visibility = vis;
        out->u.static_var_def.name = name;
        out->u.static_var_def.type = full_type;
        out->u.static_var_def.struct_type_name = struct_name;
        out->u.static_var_def.rhs = rhs;
        out->u.static_var_def.static_slot = -1;
    }
    return 0;
}

int tc_parse_import_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                TcStatement *out, TcDiagnostic *diag) {
    const TcToken *name_tok = NULL;

    (*index)++; /* import */
    name_tok = tc_peek(tokens, *index);
    if (name_tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, name_tok->column, "expected module name");
    }
    out->kind = TC_STMT_IMPORT;
    out->u.import_stmt.line = line_no;
    out->u.import_stmt.module_name = tc_token_strdup(name_tok, line_no, diag);
    if (!out->u.import_stmt.module_name) {
        return -1;
    }
    (*index)++;
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(out->u.import_stmt.module_name);
        out->u.import_stmt.module_name = NULL;
        return -1;
    }
    return 0;
}

int tc_parse_return_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                TcStatement *out, TcDiagnostic *diag) {
    TcReturnStmt ret;

    (*index)++; /* return */
    memset(&ret, 0, sizeof(ret));
    ret.line = line_no;
    {
        const TcToken *tail = tc_peek(tokens, *index);
        if (tail->kind != TC_TOK_EOF && tail->kind != TC_TOK_SEMICOLON) {
            ret.has_value = 1;
            if (tc_parse_operand(tokens, index, line_no, &ret.value, diag) != 0) {
                return -1;
            }
        }
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        if (ret.has_value) {
            tc_operand_free(&ret.value);
        }
        return -1;
    }
    out->kind = TC_STMT_RETURN;
    out->u.return_stmt = ret;
    return 0;
}

static int tc_parse_funcall_target(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcFuncallStmt *out, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    memset(out, 0, sizeof(*out));
    out->line = line_no;
    out->resolved_func_id = -1;

    if (tok->kind == TC_TOK_SELF) {
        const TcToken *member_tok = NULL;

        if (tc_peek(tokens, *index + 1)->kind != TC_TOK_DOT) {
            return tc_syntax_error(diag, line_no, tok->column, "expected . after Self");
        }
        (*index) += 2;
        member_tok = tc_peek(tokens, *index);
        if (member_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member_tok->column, "expected member name");
        }
        out->is_self = 1;
        out->qualifier = strdup("Self");
        out->member_name = tc_token_strdup(member_tok, line_no, diag);
        if (!out->qualifier || !out->member_name) {
            free(out->qualifier);
            free(out->member_name);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, member_tok->column,
                              "memory allocation failed");
            return -1;
        }
        {
            size_t target_len = 4 + member_tok->length + 1;
            out->target = (char *)malloc(target_len);
            if (!out->target) {
                free(out->qualifier);
                free(out->member_name);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, member_tok->column,
                                  "memory allocation failed");
                return -1;
            }
            snprintf(out->target, target_len, "Self.%.*s", (int)member_tok->length, member_tok->start);
        }
        (*index)++;
        return 0;
    }

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected funcall target");
    }

    if (tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
        const TcToken *qual_tok = tok;
        const TcToken *member_tok = tc_peek(tokens, *index + 2);

        if (member_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member_tok->column, "expected member name");
        }
        out->qualifier = tc_token_strdup(qual_tok, line_no, diag);
        out->member_name = tc_token_strdup(member_tok, line_no, diag);
        if (!out->qualifier || !out->member_name) {
            free(out->qualifier);
            free(out->member_name);
            return -1;
        }
        {
            size_t target_len = qual_tok->length + 1 + member_tok->length + 1;
            out->target = (char *)malloc(target_len);
            if (!out->target) {
                free(out->qualifier);
                free(out->member_name);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                                  "memory allocation failed");
                return -1;
            }
            snprintf(out->target, target_len, "%.*s.%.*s", (int)qual_tok->length, qual_tok->start,
                     (int)member_tok->length, member_tok->start);
        }
        (*index) += 3;
        return 0;
    }

    out->target = tc_token_strdup(tok, line_no, diag);
    if (!out->target) {
        return -1;
    }
    (*index)++;
    return 0;
}

static void tc_funcall_stmt_free_partial(TcFuncallStmt *stmt) {
    free(stmt->target);
    free(stmt->qualifier);
    free(stmt->member_name);
    stmt->target = NULL;
    stmt->qualifier = NULL;
    stmt->member_name = NULL;
}

int tc_parse_funcall_stmt(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                 int line_no, TcModuleMode mode, TcStatement *out,
                                 TcDiagnostic *diag) {
    TcFuncallStmt call;
    TcNamedArg *args = NULL;
    size_t arg_count = 0;
    size_t arg_cap = 0;

    (void)mode;
    (*index)++; /* funcall */
    memset(&call, 0, sizeof(call));
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_funcall_target(tokens, index, line_no, &call, diag) != 0) {
        return -1;
    }
    /* 允许零实参：funcall(Self.f)；有实参时 target 后须有逗号 */
    if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
        (*index)++;
        while (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
            const TcToken *param_tok = tc_peek(tokens, *index);
            TcNamedArg arg;

            memset(&arg, 0, sizeof(arg));
            if (param_tok->kind != TC_TOK_IDENTIFIER) {
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, param_tok->column, "expected parameter name");
            }
            arg.param_name = tc_token_strdup(param_tok, line_no, diag);
            if (!arg.param_name) {
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return -1;
            }
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return -1;
            }
            if (tc_parse_named_arg_rhs(ctx, tokens, index, line_no, &arg.value, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return -1;
            }
            if (arg_count == arg_cap) {
                size_t new_cap = arg_cap == 0 ? 4 : arg_cap * 2;
                TcNamedArg *new_args = (TcNamedArg *)realloc(args, new_cap * sizeof(TcNamedArg));
                if (!new_args) {
                    free(arg.param_name);
                    tc_rhs_free(&arg.value);
                    tc_funcall_stmt_free_partial(&call);
                    for (size_t j = 0; j < arg_count; j++) {
                        free(args[j].param_name);
                        tc_rhs_free(&args[j].value);
                    }
                    free(args);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, param_tok->column,
                                      "memory allocation failed");
                    return -1;
                }
                args = new_args;
                arg_cap = new_cap;
            }
            args[arg_count++] = arg;

            if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
                (*index)++;
            } else if (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
                tc_funcall_stmt_free_partial(&call);
                for (size_t j = 0; j < arg_count; j++) {
                    free(args[j].param_name);
                    tc_rhs_free(&args[j].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, tc_peek(tokens, *index)->column,
                                       "expected , or )");
            }
        }
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        size_t i = 0;
        for (i = 0; i < arg_count; i++) {
            free(args[i].param_name);
            tc_rhs_free(&args[i].value);
        }
        free(args);
        tc_funcall_stmt_free_partial(&call);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        size_t i = 0;
        for (i = 0; i < arg_count; i++) {
            free(args[i].param_name);
            tc_rhs_free(&args[i].value);
        }
        free(args);
        tc_funcall_stmt_free_partial(&call);
        return -1;
    }

    call.args = args;
    call.arg_count = arg_count;
    out->kind = TC_STMT_FUNCALL;
    out->u.funcall_stmt = call;
    return 0;
}

int tc_parse_funcall_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                int line_no, TcRhs *out, TcDiagnostic *diag) {
    TcFuncallStmt call;
    TcNamedArg *args = NULL;
    size_t arg_count = 0;
    size_t arg_cap = 0;
    size_t i = 0;

    (*index)++; /* funcall */
    memset(&call, 0, sizeof(call));
    memset(out, 0, sizeof(*out));
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_funcall_target(tokens, index, line_no, &call, diag) != 0) {
        return -1;
    }
    if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
        (*index)++;
        while (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
            const TcToken *param_tok = tc_peek(tokens, *index);
            TcNamedArg arg;

            memset(&arg, 0, sizeof(arg));
            if (param_tok->kind != TC_TOK_IDENTIFIER) {
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, param_tok->column, "expected parameter name");
            }
            arg.param_name = tc_token_strdup(param_tok, line_no, diag);
            if (!arg.param_name) {
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return -1;
            }
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return -1;
            }
            if (tc_parse_named_arg_rhs(ctx, tokens, index, line_no, &arg.value, diag) != 0) {
                free(arg.param_name);
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return -1;
            }
            if (arg_count == arg_cap) {
                size_t new_cap = arg_cap == 0 ? 4 : arg_cap * 2;
                TcNamedArg *new_args = (TcNamedArg *)realloc(args, new_cap * sizeof(TcNamedArg));
                if (!new_args) {
                    free(arg.param_name);
                    tc_rhs_free(&arg.value);
                    tc_funcall_stmt_free_partial(&call);
                    for (i = 0; i < arg_count; i++) {
                        free(args[i].param_name);
                        tc_rhs_free(&args[i].value);
                    }
                    free(args);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, param_tok->column,
                                      "memory allocation failed");
                    return -1;
                }
                args = new_args;
                arg_cap = new_cap;
            }
            args[arg_count++] = arg;
            if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
                (*index)++;
            } else if (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
                tc_funcall_stmt_free_partial(&call);
                for (i = 0; i < arg_count; i++) {
                    free(args[i].param_name);
                    tc_rhs_free(&args[i].value);
                }
                free(args);
                return tc_syntax_error(diag, line_no, tc_peek(tokens, *index)->column,
                                       "expected , or )");
            }
        }
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        for (i = 0; i < arg_count; i++) {
            free(args[i].param_name);
            tc_rhs_free(&args[i].value);
        }
        free(args);
        tc_funcall_stmt_free_partial(&call);
        return -1;
    }

    out->kind = TC_RHS_FUNCALL_EXPR;
    out->u.funcall_expr.target = call.target;
    out->u.funcall_expr.is_self = call.is_self;
    out->u.funcall_expr.qualifier = call.qualifier;
    out->u.funcall_expr.member_name = call.member_name;
    out->u.funcall_expr.resolved_func_id = -1;
    out->u.funcall_expr.arg_count = arg_count;
    if (arg_count > 0) {
        out->u.funcall_expr.args =
            calloc(arg_count, sizeof(*out->u.funcall_expr.args));
        if (!out->u.funcall_expr.args) {
            for (i = 0; i < arg_count; i++) {
                free(args[i].param_name);
                tc_rhs_free(&args[i].value);
            }
            free(args);
            tc_funcall_stmt_free_partial(&call);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        for (i = 0; i < arg_count; i++) {
            TcRhs *value_copy = (TcRhs *)malloc(sizeof(TcRhs));

            out->u.funcall_expr.args[i].param_name = args[i].param_name;
            if (!value_copy) {
                size_t k = 0;
                for (k = 0; k < i; k++) {
                    free(out->u.funcall_expr.args[k].param_name);
                    tc_rhs_free((TcRhs *)out->u.funcall_expr.args[k].value);
                    free(out->u.funcall_expr.args[k].value);
                }
                free(out->u.funcall_expr.args);
                for (k = i; k < arg_count; k++) {
                    free(args[k].param_name);
                    tc_rhs_free(&args[k].value);
                }
                free(args);
                free(call.target);
                free(call.qualifier);
                free(call.member_name);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            *value_copy = args[i].value;
            out->u.funcall_expr.args[i].value = (struct TcRhs *)value_copy;
        }
        free(args);
    } else {
        out->u.funcall_expr.args = NULL;
    }
    return 0;
}

int tc_parse_field_assign_stmt(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                      int line_no, TcStatement *out, TcDiagnostic *diag) {
    TcFieldAssign fa;
    char *base = NULL;
    char **fields = NULL;
    size_t field_count = 0;

    memset(&fa, 0, sizeof(fa));
    fa.line = line_no;
    if (tc_parse_field_chain(tokens, index, line_no, &base, &fields, &field_count, diag) != 0) {
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_EQUAL, line_no, diag) != 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        return -1;
    }
    if (tc_parse_rhs(ctx, tokens, index, line_no, &fa.rhs, diag) != 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(base);
        tc_string_list_free_local(fields, field_count);
        tc_rhs_free(&fa.rhs);
        return -1;
    }
    fa.base = base;
    fa.fields = fields;
    fa.field_count = field_count;
    out->kind = TC_STMT_FIELD_ASSIGN;
    out->u.field_assign = fa;
    return 0;
}

int tc_parse_ptr_store_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcStatement *out, TcDiagnostic *diag) {
    TcPtrStoreStmt stmt;
    char *struct_name = NULL;

    (*index)++; /* ptr_store identifier token consumed by caller */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.pointee_type, &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.ptr, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.value, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        tc_operand_free(&stmt.value);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.pointee_type);
        tc_operand_free(&stmt.ptr);
        tc_operand_free(&stmt.value);
        return -1;
    }
    out->kind = TC_STMT_PTR_STORE;
    out->u.ptr_store = stmt;
    return 0;
}

int tc_parse_memblock_store_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                        TcStatement *out, TcDiagnostic *diag) {
    TcMemblockStoreStmt stmt;
    char *struct_name = NULL;

    (*index)++; /* memblock_store */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.element_type, &struct_name, diag) !=
        0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_parse_binding_name(tokens, index, line_no, &stmt.memblock_name, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.value, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        tc_operand_free(&stmt.value);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.memblock_name);
        tc_operand_free(&stmt.index);
        tc_operand_free(&stmt.value);
        return -1;
    }
    out->kind = TC_STMT_MEMBLOCK_STORE;
    out->u.memblock_store = stmt;
    return 0;
}

int tc_parse_memblock_copy_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                       TcStatement *out, TcDiagnostic *diag) {
    TcMemblockCopyStmt stmt;
    char *struct_name = NULL;

    (*index)++; /* memblock_copy */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.element_type, &struct_name, diag) !=
        0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_parse_binding_name(tokens, index, line_no, &stmt.dst_name, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.dst_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_parse_binding_name(tokens, index, line_no, &stmt.src_name, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.src_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.length, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        free(stmt.dst_name);
        free(stmt.src_name);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    out->kind = TC_STMT_MEMBLOCK_COPY;
    out->u.memblock_copy = stmt;
    return 0;
}

int tc_parse_memcopy_unsafe_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                        TcStatement *out, TcDiagnostic *diag) {
    TcMemcopyUnsafeStmt stmt;
    char *struct_name = NULL;

    (*index)++; /* memcopy_unsafe */
    memset(&stmt, 0, sizeof(stmt));
    stmt.line = line_no;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &stmt.element_type, &struct_name, diag) !=
        0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.dst_ptr, diag) != 0) {
        tc_type_free(&stmt.element_type);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.dst_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.src_ptr, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.src_index, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &stmt.length, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        tc_type_free(&stmt.element_type);
        tc_operand_free(&stmt.dst_ptr);
        tc_operand_free(&stmt.dst_index);
        tc_operand_free(&stmt.src_ptr);
        tc_operand_free(&stmt.src_index);
        tc_operand_free(&stmt.length);
        return -1;
    }
    out->kind = TC_STMT_MEMCOPY_UNSAFE;
    out->u.memcopy_unsafe = stmt;
    return 0;
}
