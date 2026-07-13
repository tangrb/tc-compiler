/*
 * tc_parser.c — TC 语法分析器实现
 *
 * 消费 tc_tokenize_line 产出的 TcTokenList，按 TC 语言语法规则
 * 将单行 Token 流解析为一条 TcStatement（AST 节点）。
 * 支持 6 种语句：var、let、赋值、write、writeln、read；if 由 tc_parse_if_stmt 处理。
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
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
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

    if (tok->kind == TC_TOK_FLOAT_LIT) {
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

static int tc_token_is_type(const TcToken *tok) {
    return tok->kind == TC_TOK_INT_TYPE || tok->kind == TC_TOK_FLOAT_TYPE;
}

static int tc_parse_type_token(const TcTokenList *tokens, size_t *index, int line_no,
                               TcIntType *out, TcDiagnostic *diag) {
    const TcToken *type_tok = tc_peek(tokens, *index);

    if (!tc_token_is_type(type_tok)) {
        return tc_syntax_error(diag, line_no, type_tok->column, "expected type");
    }
    *out = type_tok->u.int_type;
    (*index)++;
    return 0;
}

static int tc_parse_optional_float_mode(const TcTokenList *tokens, size_t *index, int line_no,
                                        TcFloatMode *mode, TcDiagnostic *diag) {
    const TcToken *maybe_mode = tc_peek(tokens, *index);

    *mode = TC_FLOAT_STRICT;
    if (maybe_mode->kind == TC_TOK_IEEE) {
        *mode = TC_FLOAT_IEEE;
        (*index)++;
        if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
            return -1;
        }
    } else if (maybe_mode->kind == TC_TOK_WRAP) {
        *mode = TC_FLOAT_WRAP;
        (*index)++;
        if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
            return -1;
        }
    } else if (maybe_mode->kind == TC_TOK_TRUNCATE) {
        return tc_keyword_error(diag, line_no, maybe_mode->column,
                                "truncate cannot be used with arithmetic operations");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  RHS 解析：算术 / 单目 / cast / 字面量                                */
/* ------------------------------------------------------------------ */

/*
 * @brief 解析算术 RHS：add/sub/mul/div/mod(type [,wrap,] lhs, rhs)
 *
 * wrap 亦适用于左移 shl（§5.1）；其余非算术运算见 tc_parse_shift_rhs / 按位路径。
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

    if (tc_parse_type_token(tokens, index, line_no, &type, diag) != 0) {
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    if (tc_type_is_float(type)) {
        TcFloatMode fp_mode = TC_FLOAT_STRICT;

        if (tc_parse_optional_float_mode(tokens, index, line_no, &fp_mode, diag) != 0) {
            return -1;
        }

        out->kind = TC_RHS_FLOAT_ARITH;
        out->u.float_arith.op = op;
        out->u.float_arith.type = type;
        out->u.float_arith.mode = fp_mode;
        memset(&out->u.float_arith.lhs, 0, sizeof(out->u.float_arith.lhs));
        memset(&out->u.float_arith.rhs, 0, sizeof(out->u.float_arith.rhs));

        if (tc_parse_operand(tokens, index, line_no, &out->u.float_arith.lhs, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        if (tc_parse_operand(tokens, index, line_no, &out->u.float_arith.rhs, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        return 0;
    }

    if (tc_type_is_bool(type)) {
        return tc_syntax_error(diag, line_no, tc_peek(tokens, *index - 1)->column,
                               "expected integer or float type");
    }

    /* 可选的 wrap 关键字作为第二参数（整数路径） */
    {
        const TcToken *maybe_mode = tc_peek(tokens, *index);
        if (maybe_mode->kind == TC_TOK_IEEE) {
            tc_diagnostic_set(diag, TC_ERR_MODE_MISMATCH, line_no, maybe_mode->column,
                              "ieee mode is only allowed for float operations");
            return -1;
        }
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

    if (tc_parse_type_token(tokens, index, line_no, &type, diag) != 0) {
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    if (tc_type_is_float(type)) {
        TcFloatMode fp_mode = TC_FLOAT_STRICT;

        if (tc_parse_optional_float_mode(tokens, index, line_no, &fp_mode, diag) != 0) {
            return -1;
        }

        out->kind = TC_RHS_FLOAT_UNARY;
        out->u.float_unary.op = op;
        out->u.float_unary.type = type;
        out->u.float_unary.mode = fp_mode;
        memset(&out->u.float_unary.operand, 0, sizeof(out->u.float_unary.operand));

        if (tc_parse_operand(tokens, index, line_no, &out->u.float_unary.operand, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        return 0;
    }

    if (tc_type_is_bool(type)) {
        return tc_syntax_error(diag, line_no, tc_peek(tokens, *index - 1)->column,
                               "expected integer or float type");
    }

    /* 可选的 wrap 关键字作为第二参数（整数路径） */
    {
        const TcToken *maybe_mode = tc_peek(tokens, *index);
        if (maybe_mode->kind == TC_TOK_IEEE) {
            tc_diagnostic_set(diag, TC_ERR_MODE_MISMATCH, line_no, maybe_mode->column,
                              "ieee mode is only allowed for float operations");
            return -1;
        }
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

    if (tc_parse_type_token(tokens, index, line_no, &target, diag) != 0) {
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    if (tc_type_is_float(target)) {
        /* 可选的 truncate 关键字（浮点 cast 位重解释） */
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
            } else if (maybe_mode->kind == TC_TOK_IEEE) {
                return tc_keyword_error(diag, line_no, maybe_mode->column,
                                        "ieee cannot be used with cast");
            } else if (maybe_mode->kind == TC_TOK_IDENTIFIER) {
                /* strict cast: cast(float64, source) */
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
            out->kind = TC_RHS_FLOAT_CAST;
            out->u.float_cast.target = target;
            out->u.float_cast.mode = mode;
            out->u.float_cast.source = strndup(src_tok->start, src_tok->length);
            if (!out->u.float_cast.source) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, src_tok->column,
                                  "memory allocation failed");
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

    /* 可选的 truncate 关键字作为第二参数（默认 strict；仅整数→整数，§8.5） */
    {
        const TcToken *maybe_mode = tc_peek(tokens, *index);
        if (maybe_mode->kind == TC_TOK_TRUNCATE) {
            if (tc_type_is_bool(target)) {
                return tc_keyword_error(diag, line_no, maybe_mode->column,
                                        "truncate is only allowed for integer to integer conversion");
            }
            mode = TC_TRUNC_TRUNCATE;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        } else if (maybe_mode->kind == TC_TOK_WRAP) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "wrap cannot be used with cast");
        } else if (maybe_mode->kind == TC_TOK_IEEE) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "ieee cannot be used with cast");
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
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, src_tok->column, "memory allocation failed");
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

    if (tc_parse_type_token(tokens, index, line_no, &type, diag) != 0) {
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    if (tc_type_is_float(type)) {
        TcFloatMode fp_mode = TC_FLOAT_STRICT;
        const TcToken *maybe_mode = tc_peek(tokens, *index);

        if (maybe_mode->kind == TC_TOK_IEEE) {
            fp_mode = TC_FLOAT_IEEE;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        } else if (maybe_mode->kind == TC_TOK_WRAP) {
            tc_diagnostic_set(diag, TC_ERR_MODE_MISMATCH, line_no, maybe_mode->column,
                              "wrap mode is not allowed for float comparison");
            return -1;
        } else if (maybe_mode->kind == TC_TOK_TRUNCATE) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "truncate cannot be used with compare operations");
        }

        out->kind = TC_RHS_FLOAT_COMPARE;
        out->u.float_compare.op = op;
        out->u.float_compare.type = type;
        out->u.float_compare.mode = fp_mode;
        memset(&out->u.float_compare.lhs, 0, sizeof(out->u.float_compare.lhs));
        memset(&out->u.float_compare.rhs, 0, sizeof(out->u.float_compare.rhs));

        if (tc_parse_operand(tokens, index, line_no, &out->u.float_compare.lhs, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        if (tc_parse_operand(tokens, index, line_no, &out->u.float_compare.rhs, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        return 0;
    }

    if (tc_type_is_bool(type)) {
        return tc_syntax_error(diag, line_no, tc_peek(tokens, *index - 1)->column,
                               "expected integer or float type");
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
 * @brief 完成双目逻辑 RHS 操作数解析（类型参数 bool 已读）
 */
static int tc_finish_logic_bin_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcLogicOp op, TcRhs *out, TcDiagnostic *diag) {
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
 * @brief 完成单目逻辑 RHS 操作数解析（类型参数 bool 已读）
 */
static int tc_finish_logic_un_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcRhs *out, TcDiagnostic *diag) {
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
 * @brief 完成双目按位 RHS 操作数解析（类型参数整数已读）
 */
static int tc_finish_bitwise_bin_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                     TcBitwiseOp op, TcIntType type, TcRhs *out,
                                     TcDiagnostic *diag) {
    out->kind = TC_RHS_BITWISE_BIN;
    out->u.bitwise_bin.op = op;
    out->u.bitwise_bin.type = type;
    memset(&out->u.bitwise_bin.lhs, 0, sizeof(out->u.bitwise_bin.lhs));
    memset(&out->u.bitwise_bin.rhs, 0, sizeof(out->u.bitwise_bin.rhs));

    if (tc_parse_operand(tokens, index, line_no, &out->u.bitwise_bin.lhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.bitwise_bin.rhs, diag) != 0) {
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
 * @brief 完成单目按位 RHS 操作数解析（类型参数整数已读）
 */
static int tc_finish_bitwise_un_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                    TcIntType type, TcRhs *out, TcDiagnostic *diag) {
    out->kind = TC_RHS_BITWISE_UN;
    out->u.bitwise_un.type = type;
    memset(&out->u.bitwise_un.operand, 0, sizeof(out->u.bitwise_un.operand));

    if (tc_parse_operand(tokens, index, line_no, &out->u.bitwise_un.operand, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

/** 按位运算禁止 wrap/truncate 关键字 */
static int tc_reject_bitwise_mode_keyword(const TcTokenList *tokens, size_t index, int line_no,
                                          TcDiagnostic *diag) {
    const TcToken *mode_tok = tc_peek(tokens, index);

    if (mode_tok->kind == TC_TOK_WRAP) {
        return tc_keyword_error(diag, line_no, mode_tok->column,
                                "wrap cannot be used with bitwise operations");
    }
    if (mode_tok->kind == TC_TOK_TRUNCATE) {
        return tc_keyword_error(diag, line_no, mode_tok->column,
                                "truncate cannot be used with bitwise operations");
    }
    return 0;
}

/*
 * @brief 解析 and/or/not：读类型参数后分派逻辑或按位路径
 */
static int tc_parse_and_or_not_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcLogicOp logic_op = op_tok->u.logic_op;
    TcIntType type = TC_INT32;
    int is_not = (logic_op == TC_LOGIC_NOT);

    if (op_tok->kind != TC_TOK_LOGIC_OP) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected logic operation");
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

    if (tc_type_is_bool(type)) {
        if (is_not) {
            return tc_finish_logic_un_rhs(tokens, index, line_no, out, diag);
        }
        if (logic_op != TC_LOGIC_AND && logic_op != TC_LOGIC_OR) {
            const TcToken *tok = tc_peek(tokens, *index);
            return tc_syntax_error(diag, line_no, tok->column, "expected binary logic operation");
        }
        return tc_finish_logic_bin_rhs(tokens, index, line_no, logic_op, out, diag);
    }

    if (tc_reject_bitwise_mode_keyword(tokens, *index, line_no, diag) != 0) {
        return -1;
    }

    if (is_not) {
        return tc_finish_bitwise_un_rhs(tokens, index, line_no, type, out, diag);
    }
    if (logic_op == TC_LOGIC_AND) {
        return tc_finish_bitwise_bin_rhs(tokens, index, line_no, TC_BIT_AND, type, out, diag);
    }
    if (logic_op == TC_LOGIC_OR) {
        return tc_finish_bitwise_bin_rhs(tokens, index, line_no, TC_BIT_OR, type, out, diag);
    }

    {
        const TcToken *tok = tc_peek(tokens, *index);
        return tc_syntax_error(diag, line_no, tok->column, "expected binary logic operation");
    }
}

/*
 * @brief 解析双目按位 RHS：xor(int_type, op1, op2)
 */
static int tc_parse_bitwise_bin_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                    TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcBitwiseOp op = op_tok->u.bitwise_op;
    TcIntType type = TC_INT32;

    if (op_tok->kind != TC_TOK_BITWISE_OP) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected bitwise operation");
    }
    (*index)++;

    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }

    {
        if (tc_parse_type_token(tokens, index, line_no, &type, diag) != 0) {
            return -1;
        }
    }

    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        return -1;
    }

    if (tc_reject_bitwise_mode_keyword(tokens, *index, line_no, diag) != 0) {
        return -1;
    }

    return tc_finish_bitwise_bin_rhs(tokens, index, line_no, op, type, out, diag);
}

/*
 * @brief 解析移位 RHS：shl(int [,wrap,] val, cnt) / shr(int, val, cnt)
 */
static int tc_parse_shift_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                              TcRhs *out, TcDiagnostic *diag, int is_const) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcShiftOp op = op_tok->u.shift_op;
    TcIntType type = TC_INT32;
    TcWrapMode mode = TC_ARITH_STRICT;

    if (op_tok->kind != TC_TOK_SHIFT_OP) {
        return tc_syntax_error(diag, line_no, op_tok->column, "expected shift operation");
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

    {
        const TcToken *maybe_mode = tc_peek(tokens, *index);
        if (maybe_mode->kind == TC_TOK_WRAP) {
            if (op == TC_SHIFT_SHR) {
                return tc_keyword_error(diag, line_no, maybe_mode->column,
                                        "wrap cannot be used with shift operations");
            }
            if (is_const) {
                return tc_keyword_error(diag, line_no, maybe_mode->column,
                                        "wrap cannot be used in constant expression");
            }
            mode = TC_ARITH_WRAP;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        } else if (maybe_mode->kind == TC_TOK_TRUNCATE) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "truncate cannot be used with shift operations");
        }
    }

    out->kind = TC_RHS_SHIFT;
    out->u.shift.op = op;
    out->u.shift.type = type;
    out->u.shift.mode = mode;
    memset(&out->u.shift.value, 0, sizeof(out->u.shift.value));
    memset(&out->u.shift.count, 0, sizeof(out->u.shift.count));

    if (tc_parse_operand(tokens, index, line_no, &out->u.shift.value, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.shift.count, diag) != 0) {
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
        if (!tc_token_is_type(type_tok)) {
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
    } else if (tok->kind == TC_TOK_FLOAT_LIT) {
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
        rc = tc_parse_and_or_not_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_BITWISE_OP) {
        rc = tc_parse_bitwise_bin_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_SHIFT_OP) {
        rc = tc_parse_shift_rhs(tokens, index, line_no, out, diag, 0);
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

    if (tok->kind == TC_TOK_INTEGER || tok->kind == TC_TOK_BOOL_LIT || tok->kind == TC_TOK_FLOAT_LIT) {
        out->kind = TC_RHS_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        rc = 0;
    } else if (tok->kind == TC_TOK_IDENTIFIER) {
        out->kind = TC_RHS_CONST_REF;
        out->u.const_ref.name = strndup(tok->start, tok->length);
        if (!out->u.const_ref.name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column, "memory allocation failed");
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
        rc = tc_parse_and_or_not_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_BITWISE_OP) {
        rc = tc_parse_bitwise_bin_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_SHIFT_OP) {
        rc = tc_parse_shift_rhs(tokens, index, line_no, out, diag, 1);
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
        if (!tc_token_is_type(type_tok)) {
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
        if (!tc_token_is_type(type_tok)) {
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
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, name_tok->column, "memory allocation failed");
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
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, name_tok->column, "memory allocation failed");
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
        if (!tc_token_is_type(type_tok)) {
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
    if (!operand) {
        return;
    }
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
    } else if (rhs->kind == TC_RHS_BITWISE_BIN) {
        tc_operand_free(&rhs->u.bitwise_bin.lhs);
        tc_operand_free(&rhs->u.bitwise_bin.rhs);
    } else if (rhs->kind == TC_RHS_BITWISE_UN) {
        tc_operand_free(&rhs->u.bitwise_un.operand);
    } else if (rhs->kind == TC_RHS_SHIFT) {
        tc_operand_free(&rhs->u.shift.value);
        tc_operand_free(&rhs->u.shift.count);
    } else if (rhs->kind == TC_RHS_CAST) {
        if (rhs->u.cast.source) {
            free(rhs->u.cast.source);
            rhs->u.cast.source = NULL;
        }
    } else if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        tc_operand_free(&rhs->u.float_arith.lhs);
        tc_operand_free(&rhs->u.float_arith.rhs);
    } else if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        tc_operand_free(&rhs->u.float_unary.operand);
    } else if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        tc_operand_free(&rhs->u.float_compare.lhs);
        tc_operand_free(&rhs->u.float_compare.rhs);
    } else if (rhs->kind == TC_RHS_FLOAT_CAST) {
        if (rhs->u.float_cast.source) {
            free(rhs->u.float_cast.source);
            rhs->u.float_cast.source = NULL;
        }
    } else if (rhs->kind == TC_RHS_CONST_CAST) {
        tc_operand_free(&rhs->u.const_cast.source);
    } else if (rhs->kind == TC_RHS_CONST_REF) {
        if (rhs->u.const_ref.name) {
            free(rhs->u.const_ref.name);
            rhs->u.const_ref.name = NULL;
        }
    } else {
        /* unknown RHS kind, skip */
    }
}

void tc_statement_free(TcStatement *stmt) {
    if (!stmt) {
        return;
    }
    if (stmt->kind == TC_STMT_VAR_DEF) {
        if (stmt->u.var_def.name) {
            free(stmt->u.var_def.name);
            stmt->u.var_def.name = NULL;
        }
        if (stmt->u.var_def.has_rhs) {
            tc_rhs_free(&stmt->u.var_def.rhs);
        }
    } else if (stmt->kind == TC_STMT_CONST_DEF) {
        if (stmt->u.const_def.name) {
            free(stmt->u.const_def.name);
            stmt->u.const_def.name = NULL;
        }
        tc_rhs_free(&stmt->u.const_def.rhs);
    } else if (stmt->kind == TC_STMT_ASSIGN) {
        if (stmt->u.assign.name) {
            free(stmt->u.assign.name);
            stmt->u.assign.name = NULL;
        }
        tc_rhs_free(&stmt->u.assign.rhs);
    } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        tc_operand_free(&stmt->u.io_write.operand);
    } else if (stmt->kind == TC_STMT_READ) {
        if (stmt->u.io_read.name) {
            free(stmt->u.io_read.name);
            stmt->u.io_read.name = NULL;
        }
    } else if (stmt->kind == TC_STMT_IF) {
        size_t i = 0;

        tc_rhs_free(&stmt->u.if_stmt.condition);
        for (i = 0; i < stmt->u.if_stmt.then_count; i++) {
            tc_statement_free(&stmt->u.if_stmt.then_body[i]);
        }
        free(stmt->u.if_stmt.then_body);
        stmt->u.if_stmt.then_body = NULL;
        stmt->u.if_stmt.then_count = 0;
        for (i = 0; i < stmt->u.if_stmt.else_count; i++) {
            tc_statement_free(&stmt->u.if_stmt.else_body[i]);
        }
        free(stmt->u.if_stmt.else_body);
        stmt->u.if_stmt.else_body = NULL;
        stmt->u.if_stmt.else_count = 0;
    } else {
        /* unknown STMT kind, skip */
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
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        program->items = items;
        program->capacity = new_cap;
    }
    program->items[program->count++] = *stmt;
    return 0;
}

/*
 * 语句语法分析入口。
 * 根据首个 Token 的种类 dispatch 到对应解析逻辑：
 *   TC_TOK_VAR / TC_TOK_LET           → tc_parse_var_or_const_def
 *   TC_TOK_WRITE / TC_TOK_WRITELN      → tc_parse_io_write_stmt
 *   TC_TOK_READ                        → tc_parse_read_stmt
 *   TC_TOK_IDENTIFIER                  → 赋值语句（= RHS）
 *   其它                               → SyntaxError
 *
 * 所有子函数均通过 *diag 输出错误，调用方通过返回值判断成败。
 */
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
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, first->column, "memory allocation failed");
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

/* ------------------------------------------------------------------ */
/*  缩进引擎与 if 语句（多行 parse）                                      */
/* ------------------------------------------------------------------ */

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

static int tc_is_skippable_line(const char *line) {
    if (line == NULL) {
        return 1;
    }
    if (tc_is_only_whitespace(line)) {
        return 1;
    }
    return tc_is_comment_only_line(line);
}

static int tc_indent_diag(TcDiagnostic *diag, TcErrorKind kind, int line_no, const char *message) {
    tc_diagnostic_set(diag, kind, line_no, TC_COLUMN_UNKNOWN, message);
    return -1;
}

static int tc_measure_line_indent(const char *line, TcFileIndent *file_indent, int line_no,
                                  TcDiagnostic *diag, int *out_indent) {
    int spaces = 0;
    int tabs = 0;
    const char *cursor = line;

    while (*cursor == ' ' || *cursor == '\t') {
        if (*cursor == ' ') {
            spaces++;
        } else {
            tabs++;
        }
        cursor++;
    }

    if (spaces > 0 && tabs > 0) {
        return tc_indent_diag(diag, TC_ERR_INDENT_MIXED, line_no,
                              "mixed spaces and tabs in indentation");
    }

    if (spaces > 0) {
        if (file_indent->indent_char == '\0') {
            file_indent->indent_char = ' ';
        } else if (file_indent->indent_char != ' ') {
            return tc_indent_diag(diag, TC_ERR_INDENT_MIXED, line_no,
                                  "mixed spaces and tabs in indentation");
        }
        *out_indent = spaces;
        return 0;
    }

    if (tabs > 0) {
        if (file_indent->indent_char == '\0') {
            file_indent->indent_char = '\t';
            file_indent->indent_width = 1;
        } else if (file_indent->indent_char != '\t') {
            return tc_indent_diag(diag, TC_ERR_INDENT_MIXED, line_no,
                                  "mixed spaces and tabs in indentation");
        }
        *out_indent = tabs;
        return 0;
    }

    *out_indent = 0;
    return 0;
}

static void tc_source_lines_free(TcSourceLine *lines, size_t count) {
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

static int tc_detect_indent_width(const TcSourceLine *lines, size_t count, char indent_char) {
    size_t i = 0;

    if (indent_char == '\t') {
        return 1;
    }

    for (i = 0; i < count; i++) {
        size_t j = 0;

        if (lines[i].tokens.count == 0 ||
            lines[i].tokens.items[0].kind != TC_TOK_IF) {
            continue;
        }
        for (j = i + 1; j < count; j++) {
            if (lines[j].indent <= lines[i].indent) {
                break;
            }
            return lines[j].indent - lines[i].indent;
        }
    }
    return 4;
}

static int tc_block_indent_valid(const TcFileIndent *file_indent, int base_indent, int indent,
                                 TcDiagnostic *diag, int line_no) {
    int delta = 0;

    if (indent <= base_indent) {
        return 0;
    }
    delta = indent - base_indent;
    if (file_indent->indent_char == '\t') {
        if (delta < file_indent->indent_width) {
            return tc_indent_diag(diag, TC_ERR_INDENT_INSUFFICIENT, line_no,
                                  "insufficient indentation in block");
        }
        return 0;
    }

    if (file_indent->indent_char == '\0' || file_indent->indent_char == ' ') {
        if (delta < file_indent->indent_width ||
            (delta % file_indent->indent_width) != 0) {
            return tc_indent_diag(diag, TC_ERR_INDENT_INSUFFICIENT, line_no,
                                  "insufficient indentation in block");
        }
        return 0;
    }

    return 0;
}

typedef struct {
    TcStatement *items;
    size_t count;
    size_t capacity;
} TcStmtBlock;

static void tc_stmt_block_init(TcStmtBlock *block) {
    block->items = NULL;
    block->count = 0;
    block->capacity = 0;
}

static void tc_stmt_block_free(TcStmtBlock *block) {
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

static int tc_first_token_kind(const TcSourceLine *line) {
    if (line->tokens.count == 0) {
        return TC_TOK_EOF;
    }
    return (int)line->tokens.items[0].kind;
}

static int tc_parse_block_body(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                               size_t *index, int base_indent,
                               const TcFileIndent *file_indent, TcStmtBlock *block,
                               TcDiagnostic *diag) {
    while (*index < line_count) {
        TcSourceLine *line = &lines[*index];
        TcStatement stmt;
        int first_kind = 0;

        if (line->indent <= base_indent) {
            break;
        }
        if (tc_block_indent_valid(file_indent, base_indent, line->indent, diag,
                                  line->line_no) != 0) {
            return -1;
        }

        first_kind = tc_first_token_kind(line);
        if (first_kind == TC_TOK_ELSE) {
            return tc_indent_diag(diag, TC_ERR_ELSE_POSITION, line->line_no,
                                  "else must appear at same indentation as if");
        }
        if (first_kind == TC_TOK_END) {
            return tc_indent_diag(diag, TC_ERR_INDENT_ELSE_END, line->line_no,
                                  "end indentation does not match if");
        }

        memset(&stmt, 0, sizeof(stmt));
        if (first_kind == TC_TOK_IF) {
            if (tc_parse_if_stmt(ctx, lines, line_count, index, file_indent, &stmt, diag) != 0) {
                return -1;
            }
        } else {
            if (tc_parse_statement(ctx, &line->tokens, line->line_no, &stmt, diag) != 0) {
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

int tc_parse_if_stmt(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count, size_t *index,
                     const TcFileIndent *file_indent, TcStatement *out, TcDiagnostic *diag) {
    TcSourceLine *if_line = NULL;
    size_t tok_index = 0;
    int base_indent = 0;
    TcIfStmt if_stmt;
    TcStmtBlock then_block;
    TcStmtBlock else_block;
    const TcToken *tok = NULL;

    if (*index >= line_count) {
        return tc_syntax_error(diag, 0, TC_COLUMN_UNKNOWN, "unexpected end of file");
    }

    if_line = &lines[*index];
    base_indent = if_line->indent;
    memset(&if_stmt, 0, sizeof(if_stmt));
    if_stmt.line = if_line->line_no;
    tc_stmt_block_init(&then_block);
    tc_stmt_block_init(&else_block);

    tok = tc_peek(&if_line->tokens, tok_index);
    if (tok->kind != TC_TOK_IF) {
        return tc_syntax_error(diag, if_line->line_no, tok->column, "expected if");
    }
    tok_index++;

    if (tc_parse_rhs(ctx, &if_line->tokens, &tok_index, if_line->line_no, &if_stmt.condition,
                     diag) != 0) {
        goto fail;
    }

    if (tc_expect_token(&if_line->tokens, &tok_index, TC_TOK_THEN, if_line->line_no, diag) != 0) {
        goto fail;
    }
    if (tc_expect_stmt_end(&if_line->tokens, &tok_index, if_line->line_no, diag) != 0) {
        goto fail;
    }

    (*index)++;
    if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent, &then_block,
                            diag) != 0) {
        goto fail;
    }

    if (*index >= line_count) {
        tc_indent_diag(diag, TC_ERR_MISSING_END, if_line->line_no, "missing end for if statement");
        goto fail;
    }

    if (tc_first_token_kind(&lines[*index]) == TC_TOK_ELSE) {
        if (lines[*index].indent != base_indent) {
            tc_indent_diag(diag, TC_ERR_INDENT_ELSE_END, lines[*index].line_no,
                           "else indentation does not match if");
            goto fail;
        }
        (*index)++;
        if (tc_parse_block_body(ctx, lines, line_count, index, base_indent, file_indent,
                                &else_block, diag) != 0) {
            goto fail;
        }
    }

    if (*index >= line_count) {
        tc_indent_diag(diag, TC_ERR_MISSING_END, if_line->line_no, "missing end for if statement");
        goto fail;
    }

    if (tc_first_token_kind(&lines[*index]) != TC_TOK_END) {
        tc_indent_diag(diag, TC_ERR_MISSING_END, if_line->line_no, "missing end for if statement");
        goto fail;
    }
    if (lines[*index].indent != base_indent) {
        tc_indent_diag(diag, TC_ERR_INDENT_ELSE_END, lines[*index].line_no,
                        "end indentation does not match if");
        goto fail;
    }
    (*index)++;

    if_stmt.then_body = then_block.items;
    if_stmt.then_count = then_block.count;
    if_stmt.else_body = else_block.items;
    if_stmt.else_count = else_block.count;
    then_block.items = NULL;
    else_block.items = NULL;

    out->kind = TC_STMT_IF;
    out->u.if_stmt = if_stmt;
    return 0;

fail:
    tc_rhs_free(&if_stmt.condition);
    tc_stmt_block_free(&then_block);
    tc_stmt_block_free(&else_block);
    return -1;
}

static int tc_collect_source_lines(const char *source, TcSourceLine **out_lines, size_t *out_count,
                                   TcFileIndent *file_indent, TcDiagnostic *diag) {
    const char *cursor = source;
    int line_no = 1;
    TcSourceLine *lines = NULL;
    size_t count = 0;
    size_t capacity = 0;

    *out_lines = NULL;
    *out_count = 0;
    file_indent->indent_char = '\0';
    file_indent->indent_width = 4;

    while (*cursor != '\0') {
        const char *line_start = cursor;
        const char *line_end = cursor;
        char *line_copy = NULL;
        int indent = 0;

        while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        line_copy = (char *)malloc((size_t)(line_end - line_start) + 1);
        if (!line_copy) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
            tc_source_lines_free(lines, count);
            return -1;
        }
        memcpy(line_copy, line_start, (size_t)(line_end - line_start));
        line_copy[line_end - line_start] = '\0';

        if (!tc_is_skippable_line(line_copy)) {
            TcSourceLine entry;

            if (tc_measure_line_indent(line_copy, file_indent, line_no, diag, &indent) != 0) {
                free(line_copy);
                tc_source_lines_free(lines, count);
                return -1;
            }

            memset(&entry, 0, sizeof(entry));
            entry.line_no = line_no;
            entry.indent = indent;
            entry.text = strdup(line_copy);
            if (!entry.text) {
                free(line_copy);
                tc_source_lines_free(lines, count);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN, "memory allocation failed");
                return -1;
            }
            tc_token_list_init(&entry.tokens);
            if (tc_tokenize_line(entry.text, line_no, &entry.tokens, diag) != 0) {
                free(line_copy);
                free(entry.text);
                tc_token_list_free(&entry.tokens);
                tc_source_lines_free(lines, count);
                return -1;
            }

            if (count == capacity) {
                size_t new_cap = capacity == 0 ? 8 : capacity * 2;
                TcSourceLine *new_lines =
                    (TcSourceLine *)realloc(lines, new_cap * sizeof(TcSourceLine));

                if (!new_lines) {
                    free(line_copy);
                    free(entry.text);
                    tc_token_list_free(&entry.tokens);
                    tc_source_lines_free(lines, count);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                                      "memory allocation failed");
                    return -1;
                }
                lines = new_lines;
                capacity = new_cap;
            }
            lines[count++] = entry;
        }

        free(line_copy);

        if (*line_end == '\r') {
            line_end++;
        }
        if (*line_end == '\n') {
            line_end++;
        }
        cursor = line_end;
        line_no++;
    }

    if (file_indent->indent_char == ' ') {
        file_indent->indent_width = tc_detect_indent_width(lines, count, file_indent->indent_char);
    }

    *out_lines = lines;
    *out_count = count;
    return 0;
}

static int tc_parse_line_program(TcParserCtx *ctx, TcSourceLine *lines, size_t line_count,
                                 const TcFileIndent *file_indent, TcProgram *program,
                                 TcDiagnostic *diag) {
    size_t index = 0;

    while (index < line_count) {
        TcStatement stmt;
        TcSourceLine *line = &lines[index];

        memset(&stmt, 0, sizeof(stmt));
        if (tc_first_token_kind(line) == TC_TOK_IF) {
            if (tc_parse_if_stmt(ctx, lines, line_count, &index, file_indent, &stmt, diag) != 0) {
                return -1;
            }
        } else {
            if (tc_parse_statement(ctx, &line->tokens, line->line_no, &stmt, diag) != 0) {
                return -1;
            }
            index++;
        }

        if (tc_program_push(program, &stmt, diag) != 0) {
            tc_statement_free(&stmt);
            return -1;
        }
    }
    return 0;
}

int tc_parse_source_to_program(const char *source, TcProgram *program, TcDiagnostic *diag) {
    TcSourceLine *lines = NULL;
    size_t line_count = 0;
    TcFileIndent file_indent;
    TcParserCtx ctx;
    int rc = 0;

    tc_program_init(program);
    memset(&file_indent, 0, sizeof(file_indent));
    file_indent.indent_width = 4;

    rc = tc_collect_source_lines(source, &lines, &line_count, &file_indent, diag);
    if (rc != 0) {
        tc_program_free(program);
        return -1;
    }

    ctx.depth = 0;
    rc = tc_parse_line_program(&ctx, lines, line_count, &file_indent, program, diag);
    tc_source_lines_free(lines, line_count);
    if (rc != 0) {
        tc_program_free(program);
        return -1;
    }
    return 0;
}
