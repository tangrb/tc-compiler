/*
 * parser.c — TC 语法分析器实现（v0.0.14）
 */
#include "tc_parser.h"

#include "tc_diagnostic.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void tc_operand_free(TcOperand *operand);
void tc_rhs_free(TcRhs *rhs);


static int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, message);
    return -1;
}

static int tc_keyword_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_ERR_KEYWORD, line, column, message);
    return -1;
}

static const TcToken *tc_peek(const TcTokenList *tokens, size_t index) {
    assert(tokens->count > 0);
    assert(index < tokens->count);
    return &tokens->items[index];
}

static int tc_parse_operand(const TcTokenList *tokens, size_t *index, int line_no,
                            TcOperand *out, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_IDENTIFIER) {
        out->kind = TC_OPERAND_VAR;
        out->u.name = strndup(tok->start, tok->length);
        if (!out->u.name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok->column, "out of memory");
            return -1;
        }
        (*index)++;
        return 0;
    }

    if (tok->kind == TC_TOK_INTEGER) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    return tc_syntax_error(diag, line_no, tok->column, "expected operand");
}

static int tc_expect_token(const TcTokenList *tokens, size_t *index, TcTokenKind kind,
                           int line_no, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    if (tok->kind != kind) {
        return tc_syntax_error(diag, line_no, tok->column, "unexpected token");
    }
    (*index)++;
    return 0;
}

static int tc_parse_arith_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                              TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcArithOp op = op_tok->u.arith_op;
    TcIntType type = TC_INT32;
    TcWrapMode mode = TC_ARITH_STRICT;

    if (op_tok->kind != TC_TOK_ARITH_OP) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected arithmetic operation");
    }
    (*index)++;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *maybe_mode = tc_peek(tokens, *index);
        if (maybe_mode->kind == TC_TOK_WRAP) {
            mode = TC_ARITH_WRAP;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        } else if (maybe_mode->kind == TC_TOK_TRUNCATE) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "truncate cannot be used with arithmetic operations");
        }
    }

    out->kind = TC_RHS_ARITH;
    out->u.arith.op = op;
    out->u.arith.type = type;
    out->u.arith.mode = mode;
    memset(&out->u.arith.lhs, 0, sizeof(out->u.arith.lhs));
    memset(&out->u.arith.rhs, 0, sizeof(out->u.arith.rhs));

    if (tc_parse_operand(tokens, index, line_no, &out->u.arith.lhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.arith.rhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_cast_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                             TcRhs *out, TcDiagnostic *diag) {
    TcIntType target = TC_INT32;
    TcTruncateMode mode = TC_TRUNC_STRICT;

    if (tc_expect_token(tokens, index, TC_TOK_CAST, line_no, diag) != 0) {
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        target = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *maybe_mode = tc_peek(tokens, *index);
        if (maybe_mode->kind == TC_TOK_TRUNCATE) {
            mode = TC_TRUNC_TRUNCATE;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        } else if (maybe_mode->kind == TC_TOK_WRAP) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "wrap cannot be used with cast");
        } else if (maybe_mode->kind == TC_TOK_IDENTIFIER) {
            /* strict cast: cast(type, source) */
        } else {
            return tc_syntax_error(diag, line_no, maybe_mode->column,
                                   "cast source must be a variable");
        }
    }

    {
        const TcToken *src_tok = tc_peek(tokens, *index);
        if (src_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, src_tok->column,
                                   "cast source must be a variable");
        }
        out->kind = TC_RHS_CAST;
        out->u.cast.target = target;
        out->u.cast.mode = mode;
        out->u.cast.source = strndup(src_tok->start, src_tok->length);
        if (!out->u.cast.source) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, src_tok->column, "out of memory");
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_io_write_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcIoWrite *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = TC_INT32;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    memset(&out->operand, 0, sizeof(out->operand));
    if (tc_parse_operand(tokens, index, line_no, &out->operand, diag) != 0) {
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_operand_free(&out->operand);
        return -1;
    }
    return 0;
}

static int tc_parse_read_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                              TcRead *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = TC_INT32;
    out->name = NULL;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        out->type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *name_tok = tc_peek(tokens, *index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
        }
        out->name = strndup(name_tok->start, name_tok->length);
        if (!out->name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, name_tok->column, "out of memory");
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        free(out->name);
        out->name = NULL;
        return -1;
    }
    return 0;
}

static int tc_expect_stmt_end(const TcTokenList *tokens, size_t *index, int line_no,
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

static int tc_parse_rhs(const TcTokenList *tokens, size_t *index, int line_no, TcRhs *out,
                        TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_INTEGER) {
        out->kind = TC_RHS_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }
    if (tok->kind == TC_TOK_ARITH_OP) {
        return tc_parse_arith_rhs(tokens, index, line_no, out, diag);
    }
    if (tok->kind == TC_TOK_CAST) {
        return tc_parse_cast_rhs(tokens, index, line_no, out, diag);
    }
    return tc_syntax_error(diag, line_no, tok->column, "expected rhs expression");
}

static void tc_operand_free(TcOperand *operand) {
    if (operand->kind == TC_OPERAND_VAR) {
        free(operand->u.name);
        operand->u.name = NULL;
    }
}

void tc_rhs_free(TcRhs *rhs) {
    if (!rhs) {
        return;
    }
    if (rhs->kind == TC_RHS_ARITH) {
        tc_operand_free(&rhs->u.arith.lhs);
        tc_operand_free(&rhs->u.arith.rhs);
    } else if (rhs->kind == TC_RHS_CAST) {
        free(rhs->u.cast.source);
        rhs->u.cast.source = NULL;
    }
}

void tc_statement_free(TcStatement *stmt) {
    if (!stmt) {
        return;
    }
    if (stmt->kind == TC_STMT_VAR_DEF) {
        free(stmt->u.var_def.name);
        stmt->u.var_def.name = NULL;
        if (stmt->u.var_def.has_rhs) {
            tc_rhs_free(&stmt->u.var_def.rhs);
        }
    } else if (stmt->kind == TC_STMT_CONST_DEF) {
        free(stmt->u.const_def.name);
        stmt->u.const_def.name = NULL;
        tc_rhs_free(&stmt->u.const_def.rhs);
    } else if (stmt->kind == TC_STMT_ASSIGN) {
        free(stmt->u.assign.name);
        stmt->u.assign.name = NULL;
        tc_rhs_free(&stmt->u.assign.rhs);
    } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        tc_operand_free(&stmt->u.io_write.operand);
    } else if (stmt->kind == TC_STMT_READ) {
        free(stmt->u.io_read.name);
        stmt->u.io_read.name = NULL;
    }
}

void tc_program_init(TcProgram *program) {
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;
}

void tc_program_free(TcProgram *program) {
    size_t i = 0;
    for (i = 0; i < program->count; i++) {
        tc_statement_free(&program->items[i]);
    }
    free(program->items);
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;
}

int tc_program_push(TcProgram *program, const TcStatement *stmt) {
    if (program->count == program->capacity) {
        size_t new_cap = program->capacity == 0 ? 8 : program->capacity * 2;
        TcStatement *items = (TcStatement *)realloc(program->items, new_cap * sizeof(TcStatement));
        if (!items) {
            return -1;
        }
        program->items = items;
        program->capacity = new_cap;
    }
    program->items[program->count++] = *stmt;
    return 0;
}

static int tc_parse_var_or_const_def(const TcTokenList *tokens, size_t *index, int line_no,
                                     int is_const, TcStatement *out, TcDiagnostic *diag) {
    char *name = NULL;
    TcIntType type = TC_INT32;
    TcRhs rhs;
    int has_rhs = 0;

    (*index)++;

    {
        const TcToken *name_tok = tc_peek(tokens, *index);
        if (name_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
        }
        name = strndup(name_tok->start, name_tok->length);
        if (!name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, name_tok->column, "out of memory");
            return -1;
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
        free(name);
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE) {
            free(name);
            return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
        }
        type = type_tok->u.int_type;
        (*index)++;
    }

    {
        const TcToken *maybe_eq = tc_peek(tokens, *index);
        if (maybe_eq->kind == TC_TOK_EQUAL) {
            (*index)++;
            memset(&rhs, 0, sizeof(rhs));
            if (tc_parse_rhs(tokens, index, line_no, &rhs, diag) != 0) {
                free(name);
                tc_rhs_free(&rhs);
                return -1;
            }
            has_rhs = 1;
        } else if (is_const) {
            free(name);
            return tc_syntax_error(diag, line_no, maybe_eq->column,
                                   "constant definition requires initializer");
        }
    }

    if (tc_expect_stmt_end(tokens, index, line_no, diag) != 0) {
        free(name);
        if (has_rhs) {
            tc_rhs_free(&rhs);
        }
        return -1;
    }

    if (is_const) {
        out->kind = TC_STMT_CONST_DEF;
        out->u.const_def.line = line_no;
        out->u.const_def.name = name;
        out->u.const_def.type = type;
        out->u.const_def.rhs = rhs;
    } else {
        out->kind = TC_STMT_VAR_DEF;
        out->u.var_def.line = line_no;
        out->u.var_def.name = name;
        out->u.var_def.type = type;
        out->u.var_def.has_rhs = has_rhs;
        out->u.var_def.rhs = rhs;
    }
    return 0;
}

int tc_parse_statement(const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag) {
    size_t index = 0;
    const TcToken *first = NULL;

    memset(out, 0, sizeof(*out));
    first = tc_peek(tokens, index);

    if (first->kind == TC_TOK_VAR) {
        return tc_parse_var_or_const_def(tokens, &index, line_no, 0, out, diag);
    }

    if (first->kind == TC_TOK_LET) {
        return tc_parse_var_or_const_def(tokens, &index, line_no, 1, out, diag);
    }

    if (first->kind == TC_TOK_WRITE || first->kind == TC_TOK_WRITELN) {
        TcIoWrite io_write;
        TcStmtKind kind = first->kind == TC_TOK_WRITE ? TC_STMT_WRITE : TC_STMT_WRITELN;

        index++;
        memset(&io_write, 0, sizeof(io_write));
        if (tc_parse_io_write_stmt(tokens, &index, line_no, &io_write, diag) != 0) {
            tc_operand_free(&io_write.operand);
            return -1;
        }
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            tc_operand_free(&io_write.operand);
            return -1;
        }
        out->kind = kind;
        out->u.io_write = io_write;
        return 0;
    }

    if (first->kind == TC_TOK_READ) {
        TcRead io_read;

        index++;
        if (tc_parse_read_stmt(tokens, &index, line_no, &io_read, diag) != 0) {
            return -1;
        }
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            free(io_read.name);
            return -1;
        }
        out->kind = TC_STMT_READ;
        out->u.io_read = io_read;
        return 0;
    }

    if (first->kind == TC_TOK_IDENTIFIER) {
        TcAssign assign;
        assign.line = line_no;
        assign.name = strndup(first->start, first->length);
        if (!assign.name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, first->column, "out of memory");
            return -1;
        }
        index++;
        if (tc_expect_token(tokens, &index, TC_TOK_EQUAL, line_no, diag) != 0) {
            free(assign.name);
            return -1;
        }
        memset(&assign.rhs, 0, sizeof(assign.rhs));
        if (tc_parse_rhs(tokens, &index, line_no, &assign.rhs, diag) != 0) {
            free(assign.name);
            tc_rhs_free(&assign.rhs);
            return -1;
        }
        if (tc_expect_stmt_end(tokens, &index, line_no, diag) != 0) {
            free(assign.name);
            tc_rhs_free(&assign.rhs);
            return -1;
        }
        out->kind = TC_STMT_ASSIGN;
        out->u.assign = assign;
        return 0;
    }

    return tc_syntax_error(diag, line_no, first->column, "expected statement");
}
