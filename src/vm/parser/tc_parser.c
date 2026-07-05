/*
 * tc_parser.c — TC 语法分析器实现
 *
 * 消费 tc_tokenize_line 产出的 TcTokenList，按 TC 语言语法规则
 * 将单行 Token 流解析为一条 TcStatement（AST 节点）。
 * 支持 6 种语句：var、let、赋值、write、writeln、read。
 */
#include "tc_parser.h"

#include "tc_diagnostic.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void tc_operand_free(TcOperand *operand);
void tc_rhs_free(TcRhs *rhs);

/* ------------------------------------------------------------------ */
/*  便捷错误报告辅助函数                                                 */
/* ------------------------------------------------------------------ */

static int tc_syntax_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_ERR_SYNTAX, line, column, message);
    return -1;
}

static int tc_keyword_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_ERR_KEYWORD, line, column, message);
    return -1;
}

static int tc_operand_count_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_ERR_OPERAND_COUNT, line, column, message);
    return -1;
}

/* ------------------------------------------------------------------ */
/*  底层解析工具函数                                                     */
/* ------------------------------------------------------------------ */

/** 读取 Token 列表中的第 index 个 Token（不做越界检查） */
static const TcToken *tc_peek(const TcTokenList *tokens, size_t index) {
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

    if (tok->kind == TC_TOK_BOOL_LIT) {
        out->kind = TC_OPERAND_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        return 0;
    }

    return tc_syntax_error(diag, line_no, tok->column, "expected operand");
}

/** 断言当前位置的 Token 种类与期望的一致，然后推进 index */
static int tc_expect_token(const TcTokenList *tokens, size_t *index, TcTokenKind kind,
                           int line_no, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    if (tok->kind != kind) {
        return tc_syntax_error(diag, line_no, tok->column, "unexpected token");
    }
    (*index)++;
    return 0;
}

/** 检查语句结尾：允许可选的分号后紧跟 EOF */
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

/* ------------------------------------------------------------------ */
/*  RHS 解析：算术 / 单目 / cast / 字面量                                */
/* ------------------------------------------------------------------ */

/*
 * @brief 解析算术 RHS：add/sub/mul/div/mod(type [,wrap,] lhs, rhs)
 */
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
        if (type_tok->kind != TC_TOK_INT_TYPE || tc_type_is_bool(type_tok->u.int_type)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected integer type");
        }
        type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    /* 可选的 wrap 关键字作为第二参数 */
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

/*
 * @brief 解析单目 RHS：abs/neg(type [,wrap,] operand)
 */
static int tc_parse_unary_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                              TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcUnaryOp op = op_tok->u.unary_op;
    TcIntType type = TC_INT32;
    TcWrapMode mode = TC_ARITH_STRICT;

    if (op_tok->kind != TC_TOK_UNARY_OP) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected unary operation");
    }
    (*index)++;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE || tc_type_is_bool(type_tok->u.int_type)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected integer type");
        }
        type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    /* 可选的 wrap 关键字作为第二参数 */
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

    out->kind = TC_RHS_UNARY;
    out->u.unary.op = op;
    out->u.unary.type = type;
    out->u.unary.mode = mode;
    memset(&out->u.unary.operand, 0, sizeof(out->u.unary.operand));

    if (tc_parse_operand(tokens, index, line_no, &out->u.unary.operand, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

/*
 * @brief 解析 cast RHS：cast(type [,truncate,] source_var)
 *
 * cast 的源操作数必须是变量（不允许字面量），这是 TC 语言标准的规定。
 */
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

    /* 可选的 truncate 关键字作为第二参数（默认 strict） */
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

/*
 * @brief 解析比较 RHS：eq/ne/lt/le/gt/ge(int_type, lhs, rhs)
 */
static int tc_parse_compare_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcCompareOp op = op_tok->u.compare_op;
    TcIntType type = TC_INT32;

    if (op_tok->kind != TC_TOK_COMPARE_OP) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected compare operation");
    }
    (*index)++;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE || tc_type_is_bool(type_tok->u.int_type)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected integer type");
        }
        type = type_tok->u.int_type;
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    out->kind = TC_RHS_COMPARE;
    out->u.compare.op = op;
    out->u.compare.type = type;
    memset(&out->u.compare.lhs, 0, sizeof(out->u.compare.lhs));
    memset(&out->u.compare.rhs, 0, sizeof(out->u.compare.rhs));

    if (tc_parse_operand(tokens, index, line_no, &out->u.compare.lhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.compare.rhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

/*
 * @brief 解析双目逻辑 RHS：and/or(bool, lhs, rhs)
 */
static int tc_parse_logic_bin_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcLogicOp op = op_tok->u.logic_op;

    if (op_tok->kind != TC_TOK_LOGIC_OP || op == TC_LOGIC_NOT) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected binary logic operation");
    }
    (*index)++;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE || !tc_type_is_bool(type_tok->u.int_type)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected bool type");
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    out->kind = TC_RHS_LOGIC_BIN;
    out->u.logic_bin.op = op;
    memset(&out->u.logic_bin.lhs, 0, sizeof(out->u.logic_bin.lhs));
    memset(&out->u.logic_bin.rhs, 0, sizeof(out->u.logic_bin.rhs));

    if (tc_parse_operand(tokens, index, line_no, &out->u.logic_bin.lhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.logic_bin.rhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

/*
 * @brief 解析单目逻辑 RHS：not(bool, operand)
 */
static int tc_parse_logic_un_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                 TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);

    if (op_tok->kind != TC_TOK_LOGIC_OP || op_tok->u.logic_op != TC_LOGIC_NOT) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected not operation");
    }
    (*index)++;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        const TcToken *type_tok = tc_peek(tokens, *index);
        if (type_tok->kind != TC_TOK_INT_TYPE || !tc_type_is_bool(type_tok->u.int_type)) {
            return tc_syntax_error(diag, line_no, type_tok->column, "expected bool type");
        }
        (*index)++;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    out->kind = TC_RHS_LOGIC_UN;
    out->u.logic_un.op = TC_LOGIC_NOT;
    memset(&out->u.logic_un.operand, 0, sizeof(out->u.logic_un.operand));

    if (tc_parse_operand(tokens, index, line_no, &out->u.logic_un.operand, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

/*
 * @brief 解析编译期 cast RHS：cast(type, const_operand)，禁止 truncate
 */
static int tc_parse_const_cast_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcRhs *out, TcDiagnostic *diag) {
    TcIntType target = TC_INT32;

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
        if (maybe_mode->kind == TC_TOK_TRUNCATE || maybe_mode->kind == TC_TOK_WRAP) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "truncate cannot be used in constant expression");
        }
    }

    out->kind = TC_RHS_CONST_CAST;
    out->u.const_cast.target = target;
    memset(&out->u.const_cast.source, 0, sizeof(out->u.const_cast.source));

    if (tc_parse_operand(tokens, index, line_no, &out->u.const_cast.source, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  RHS 入口：分派到具体解析子函数                                         */
/* ------------------------------------------------------------------ */

static int tc_parse_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index, int line_no,
                        TcRhs *out, TcDiagnostic *diag) {
    int rc;
    const TcToken *tok;

    if (ctx->depth >= TC_PARSER_MAX_DEPTH) {
        tok = tc_peek(tokens, *index);
        return tc_syntax_error(diag, line_no, tok->column, "expression too complex");
    }
    ctx->depth++;

    tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_INTEGER) {
        out->kind = TC_RHS_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        rc = 0;
    } else if (tok->kind == TC_TOK_BOOL_LIT) {
        out->kind = TC_RHS_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        rc = 0;
    } else if (tok->kind == TC_TOK_ARITH_OP) {
        rc = tc_parse_arith_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_UNARY_OP) {
        rc = tc_parse_unary_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_COMPARE_OP) {
        rc = tc_parse_compare_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_LOGIC_OP) {
        if (tok->u.logic_op == TC_LOGIC_NOT) {
            rc = tc_parse_logic_un_rhs(tokens, index, line_no, out, diag);
        } else {
            rc = tc_parse_logic_bin_rhs(tokens, index, line_no, out, diag);
        }
    } else if (tok->kind == TC_TOK_CAST) {
        rc = tc_parse_cast_rhs(tokens, index, line_no, out, diag);
    } else {
        rc = tc_syntax_error(diag, line_no, tok->column, "expected rhs expression");
    }

    ctx->depth--;
    return rc;
}

static int tc_parse_const_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                              int line_no, TcRhs *out, TcDiagnostic *diag) {
    int rc;
    const TcToken *tok;

    if (ctx->depth >= TC_PARSER_MAX_DEPTH) {
        tok = tc_peek(tokens, *index);
        return tc_syntax_error(diag, line_no, tok->column, "expression too complex");
    }
    ctx->depth++;

    tok = tc_peek(tokens, *index);

    if (tok->kind == TC_TOK_INTEGER || tok->kind == TC_TOK_BOOL_LIT) {
        out->kind = TC_RHS_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        rc = 0;
    } else if (tok->kind == TC_TOK_IDENTIFIER) {
        out->kind = TC_RHS_CONST_REF;
        out->u.const_ref.name = strndup(tok->start, tok->length);
        if (!out->u.const_ref.name) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, line_no, tok->column, "out of memory");
            rc = -1;
        } else {
            (*index)++;
            rc = 0;
        }
    } else if (tok->kind == TC_TOK_ARITH_OP) {
        rc = tc_parse_arith_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_UNARY_OP) {
        rc = tc_parse_unary_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_COMPARE_OP) {
        rc = tc_parse_compare_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_LOGIC_OP) {
        if (tok->u.logic_op == TC_LOGIC_NOT) {
            rc = tc_parse_logic_un_rhs(tokens, index, line_no, out, diag);
        } else {
            rc = tc_parse_logic_bin_rhs(tokens, index, line_no, out, diag);
        }
    } else if (tok->kind == TC_TOK_CAST) {
        rc = tc_parse_const_cast_rhs(tokens, index, line_no, out, diag);
    } else {
        rc = tc_syntax_error(diag, line_no, tok->column, "expected constant expression");
    }

    ctx->depth--;
    return rc;
}

/* ------------------------------------------------------------------ */
/*  语句解析：write / writeln / read / var / let / 赋值                  */
/* ------------------------------------------------------------------ */

/*
 * @brief 解析 write/writeln 语句的括号内参数
 *
 * 语法：write/writeln(type [, fmt,] operand)
 *   - type 必选
 *   - fmt 可选（%d/%u/%x/%X/%o/%b）
 *   - operand 必选（变量或字面量）
 *   额外操作数报 TC_ERR_OPERAND_COUNT
 */
static int tc_parse_io_write_stmt(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcIoWrite *out, TcDiagnostic *diag) {
    out->line = line_no;
    out->type = TC_INT32;
    out->fmt = TC_FMT_NONE;

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

/* 解析 read(type, id) 语句 */
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

/*
 * @brief 解析 var 或 let 定义
 * @param is_const 1 表示 let 常量，0 表示 var 变量
 *
 * 语法：
 *   var id: type [= rhs]
 *   let id: type = rhs（必须初始化）
 */
static int tc_parse_var_or_const_def(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                     int line_no, int is_const, TcStatement *out,
                                     TcDiagnostic *diag) {
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

    /* 可选的 = rhs 初始化 */
    {
        const TcToken *maybe_eq = tc_peek(tokens, *index);
        if (maybe_eq->kind == TC_TOK_EQUAL) {
            (*index)++;
            memset(&rhs, 0, sizeof(rhs));
            if (is_const) {
                if (tc_parse_const_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                    free(name);
                    tc_rhs_free(&rhs);
                    return -1;
                }
            } else if (tc_parse_rhs(ctx, tokens, index, line_no, &rhs, diag) != 0) {
                free(name);
                tc_rhs_free(&rhs);
                return -1;
            }
            has_rhs = 1;
        } else if (is_const) {
            /* let 常量必须初始化 */
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

/* ------------------------------------------------------------------ */
/*  内存释放函数                                                        */
/* ------------------------------------------------------------------ */

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
    } else if (rhs->kind == TC_RHS_UNARY) {
        tc_operand_free(&rhs->u.unary.operand);
    } else if (rhs->kind == TC_RHS_COMPARE) {
        tc_operand_free(&rhs->u.compare.lhs);
        tc_operand_free(&rhs->u.compare.rhs);
    } else if (rhs->kind == TC_RHS_LOGIC_BIN) {
        tc_operand_free(&rhs->u.logic_bin.lhs);
        tc_operand_free(&rhs->u.logic_bin.rhs);
    } else if (rhs->kind == TC_RHS_LOGIC_UN) {
        tc_operand_free(&rhs->u.logic_un.operand);
    } else if (rhs->kind == TC_RHS_CAST) {
        free(rhs->u.cast.source);
        rhs->u.cast.source = NULL;
    } else if (rhs->kind == TC_RHS_CONST_CAST) {
        tc_operand_free(&rhs->u.const_cast.source);
    } else if (rhs->kind == TC_RHS_CONST_REF) {
        free(rhs->u.const_ref.name);
        rhs->u.const_ref.name = NULL;
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

/* ------------------------------------------------------------------ */
/*  TcProgram 动态数组管理                                              */
/* ------------------------------------------------------------------ */

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

int tc_program_push(TcProgram *program, const TcStatement *stmt, TcDiagnostic *diag) {
    if (program->count == program->capacity) {
        size_t new_cap = program->capacity == 0 ? 8 : program->capacity * 2;
        TcStatement *items = (TcStatement *)realloc(program->items, new_cap * sizeof(TcStatement));
        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_SYNTAX, 0, TC_COLUMN_UNKNOWN, "out of memory");
            return -1;
        }
        program->items = items;
        program->capacity = new_cap;
    }
    program->items[program->count++] = *stmt;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  tc_parse_statement — 语法分析入口                                   */
/* ------------------------------------------------------------------ */

int tc_parse_statement(TcParserCtx *ctx, const TcTokenList *tokens, int line_no, TcStatement *out,
                       TcDiagnostic *diag) {
    size_t index = 0;
    const TcToken *first = NULL;

    memset(out, 0, sizeof(*out));
    first = tc_peek(tokens, index);

    if (first->kind == TC_TOK_VAR) {
        return tc_parse_var_or_const_def(ctx, tokens, &index, line_no, 0, out, diag);
    }

    if (first->kind == TC_TOK_LET) {
        return tc_parse_var_or_const_def(ctx, tokens, &index, line_no, 1, out, diag);
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
        if (tc_parse_rhs(ctx, tokens, &index, line_no, &assign.rhs, diag) != 0) {
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
