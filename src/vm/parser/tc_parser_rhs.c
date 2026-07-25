/*
 * tc_parser_rhs.c — RHS / const-RHS 语法解析
 */
#include "tc_parser_rhs.h"

#include "tc_parser_internal.h"
#include "tc_parser_free.h"
#include "tc_diagnostic.h"

#include <stdlib.h>
#include <string.h>

static void tc_string_list_free_rhs(char **items, size_t count) {
    size_t i = 0;
    if (!items) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

static int tc_parse_field_read_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcRhs *out, TcDiagnostic *diag) {
    const TcToken *tok = tc_peek(tokens, *index);
    char *base = NULL;
    char **fields = NULL;
    size_t field_count = 0;
    size_t field_cap = 0;

    if (tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, tok->column, "expected identifier");
    }
    base = tc_token_strdup(tok, line_no, diag);
    if (!base) {
        return -1;
    }
    (*index)++;

    while (tc_peek(tokens, *index)->kind == TC_TOK_DOT) {
        char *field_name = NULL;
        (*index)++;
        tok = tc_peek(tokens, *index);
        if (tok->kind != TC_TOK_IDENTIFIER) {
            free(base);
            tc_string_list_free_rhs(fields, field_count);
            return tc_syntax_error(diag, line_no, tok->column, "expected field name");
        }
        field_name = tc_token_strdup(tok, line_no, diag);
        if (!field_name) {
            free(base);
            tc_string_list_free_rhs(fields, field_count);
            return -1;
        }
        if (field_count == field_cap) {
            size_t new_cap = field_cap == 0 ? 4 : field_cap * 2;
            char **new_fields = (char **)realloc(fields, new_cap * sizeof(char *));
            if (!new_fields) {
                free(field_name);
                free(base);
                tc_string_list_free_rhs(fields, field_count);
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

    if (field_count == 1 && strcmp(fields[0], "count") == 0) {
        tc_string_list_free_rhs(fields, field_count);
        out->kind = TC_RHS_MEMBLOCK_COUNT;
        out->u.memblock_count.memblock_name = base;
        return 0;
    }

    out->kind = TC_RHS_FIELD_READ;
    out->u.field_read.base = base;
    out->u.field_read.fields = fields;
    out->u.field_read.field_count = field_count;
    return 0;
}

static int tc_parse_self_member_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                    TcRhs *out, TcDiagnostic *diag) {
    /* Self.member —— 仅 #lib 合法；#program 由 tc_scope_check_self_usage 拒绝 */
    const TcToken *member_tok = NULL;

    (*index)++; /* Self */
    if (tc_expect_token(tokens, index, TC_TOK_DOT, line_no, diag) != 0) {
        return -1;
    }
    member_tok = tc_peek(tokens, *index);
    if (member_tok->kind != TC_TOK_IDENTIFIER) {
        return tc_syntax_error(diag, line_no, member_tok->column, "expected member name");
    }
    out->kind = TC_RHS_SELF_MEMBER;
    out->u.self_member.member_name = tc_token_strdup(member_tok, line_no, diag);
    if (!out->u.self_member.member_name) {
        return -1;
    }
    (*index)++;
    return 0;
}

static int tc_parse_ptr_load_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                 TcRhs *out, TcDiagnostic *diag) {
    char *struct_name = NULL;

    (*index)++; /* ptr_load */
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    out->kind = TC_RHS_PTR_LOAD;
    memset(&out->u.ptr_load, 0, sizeof(out->u.ptr_load));
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &out->u.ptr_load.pointee_type,
                             &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.ptr_load.ptr, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_ptr_address_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                    TcRhs *out, TcDiagnostic *diag) {
    const TcToken *name_tok = NULL;
    char *struct_name = NULL;

    (*index)++; /* ptr_address */
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    out->kind = TC_RHS_PTR_ADDRESS;
    memset(&out->u.ptr_address, 0, sizeof(out->u.ptr_address));
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &out->u.ptr_address.pointee_type,
                             &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    name_tok = tc_peek(tokens, *index);
    if (name_tok->kind != TC_TOK_IDENTIFIER) {
        tc_rhs_free(out);
        return tc_syntax_error(diag, line_no, name_tok->column, "expected identifier");
    }
    out->u.ptr_address.name = tc_token_strdup(name_tok, line_no, diag);
    if (!out->u.ptr_address.name) {
        tc_rhs_free(out);
        return -1;
    }
    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_memblock_load_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                      TcRhs *out, TcDiagnostic *diag) {
    char *struct_name = NULL;

    (*index)++; /* memblock_load */
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    out->kind = TC_RHS_MEMBLOCK_LOAD;
    memset(&out->u.memblock_load, 0, sizeof(out->u.memblock_load));
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &out->u.memblock_load.element_type,
                             &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.memblock_load.memblock, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.memblock_load.index, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_ptr_arith_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                  TcRhsKind kind, TcRhs *out, TcDiagnostic *diag) {
    char *struct_name = NULL;

    (*index)++; /* ptr_add / ptr_sub */
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    out->kind = kind;
    memset(&out->u.ptr_arith, 0, sizeof(out->u.ptr_arith));
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &out->u.ptr_arith.pointee_type,
                             &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.ptr_arith.ptr, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.ptr_arith.offset, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_ptr_compare_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                    TcRhsKind kind, TcRhs *out, TcDiagnostic *diag) {
    char *struct_name = NULL;

    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    out->kind = kind;
    memset(&out->u.ptr_compare, 0, sizeof(out->u.ptr_compare));
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &out->u.ptr_compare.pointee_type,
                             &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.ptr_compare.lhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.ptr_compare.rhs, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_ptr_size_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                 TcRhs *out, TcDiagnostic *diag) {
    char *struct_name = NULL;

    (*index)++; /* ptr_size */
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    out->kind = TC_RHS_PTR_SIZE;
    memset(&out->u.ptr_size, 0, sizeof(out->u.ptr_size));
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &out->u.ptr_size.pointee_type,
                             &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_parse_operand(tokens, index, line_no, &out->u.ptr_size.ptr, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_memblock_ctor_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                      int line_no, TcRhs *out, TcDiagnostic *diag) {
    char *struct_name = NULL;
    const TcToken *count_tok = NULL;

    (*index)++; /* memblock keyword or identifier */
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        return -1;
    }
    out->kind = TC_RHS_MEMBLOCK_CONSTRUCTOR;
    memset(&out->u.memblock_ctor, 0, sizeof(out->u.memblock_ctor));
    if (tc_parse_type_syntax(tokens, index, line_no, 0, &out->u.memblock_ctor.element_type,
                             &struct_name, diag) != 0) {
        return -1;
    }
    free(struct_name);
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    count_tok = tc_peek(tokens, *index);
    if (count_tok->kind != TC_TOK_IDENTIFIER ||
        !tc_token_is_ident_named(count_tok, "count")) {
        tc_rhs_free(out);
        return tc_syntax_error(diag, line_no, count_tok->column, "expected count:");
    }
    (*index)++;
    if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    {
        const TcToken *n_tok = tc_peek(tokens, *index);
        if (n_tok->kind == TC_TOK_INTEGER) {
            out->u.memblock_ctor.count = n_tok->u.literal.magnitude;
            (*index)++;
        } else if (n_tok->kind == TC_TOK_IDENTIFIER) {
            out->u.memblock_ctor.count_name = tc_token_strdup(n_tok, line_no, diag);
            if (!out->u.memblock_ctor.count_name) {
                tc_rhs_free(out);
                return -1;
            }
            out->u.memblock_ctor.count = 0;
            (*index)++;
        } else {
            tc_rhs_free(out);
            return tc_syntax_error(diag, line_no, n_tok->column, "expected memblock count");
        }
    }
    if (tc_peek(tokens, *index)->kind == TC_TOK_RPAREN) {
        (*index)++;
        return 0;
    }
    if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    {
        const TcToken *next = tc_peek(tokens, *index);
        if (next->kind == TC_TOK_IDENTIFIER && tc_token_is_ident_named(next, "fill")) {
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
                tc_rhs_free(out);
                return -1;
            }
            out->u.memblock_ctor.is_fill = 1;
            if (tc_parse_operand(tokens, index, line_no, &out->u.memblock_ctor.fill_value,
                                 diag) != 0) {
                tc_rhs_free(out);
                return -1;
            }
            if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
                tc_rhs_free(out);
                return -1;
            }
            return 0;
        }
    }
    while (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
        TcOperand value;
        size_t cap = out->u.memblock_ctor.value_count;

        memset(&value, 0, sizeof(value));
        if (out->u.memblock_ctor.value_count == cap) {
            size_t new_cap = cap == 0 ? 4 : cap * 2;
            TcOperand *new_values =
                (TcOperand *)realloc(out->u.memblock_ctor.values, new_cap * sizeof(TcOperand));
            if (!new_values) {
                tc_rhs_free(out);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            out->u.memblock_ctor.values = new_values;
        }
        if (tc_parse_operand(tokens, index, line_no, &value, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        out->u.memblock_ctor.values[out->u.memblock_ctor.value_count++] = value;
        if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
            (*index)++;
        }
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    (void)ctx;
    return 0;
}

static int tc_is_rhs_builtin_ident(const TcToken *tok) {
    switch (tok->kind) {
    case TC_TOK_PTR_LOAD:
    case TC_TOK_PTR_ADDRESS:
    case TC_TOK_PTR_ADD:
    case TC_TOK_PTR_SUB:
    case TC_TOK_PTR_EQ:
    case TC_TOK_PTR_NE:
    case TC_TOK_PTR_LT:
    case TC_TOK_PTR_LE:
    case TC_TOK_PTR_GT:
    case TC_TOK_PTR_GE:
    case TC_TOK_PTR_SIZE:
    case TC_TOK_MEMBLOCK_LOAD:
    case TC_TOK_MEMBLOCK:
        return 1;
    default:
        return 0;
    }
}

static int tc_parse_struct_ctor_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                                    int line_no, TcRhs *out, TcDiagnostic *diag) {
    const TcToken *name_tok = tc_peek(tokens, *index);
    char *struct_name = NULL;

    if (*index + 1 < tokens->count && tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
        const TcToken *member_tok = tc_peek(tokens, *index + 2);
        size_t total = name_tok->length + 1 + member_tok->length + 1;

        if (member_tok->kind != TC_TOK_IDENTIFIER) {
            return tc_syntax_error(diag, line_no, member_tok->column, "expected struct name");
        }
        struct_name = (char *)malloc(total);
        if (!struct_name) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, name_tok->column,
                              "memory allocation failed");
            return -1;
        }
        snprintf(struct_name, total, "%.*s.%.*s", (int)name_tok->length, name_tok->start,
                 (int)member_tok->length, member_tok->start);
        (*index) += 3;
    } else {
        struct_name = tc_token_strdup(name_tok, line_no, diag);
        if (!struct_name) {
            return -1;
        }
        (*index)++;
    }
    if (tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0) {
        free(struct_name);
        return -1;
    }
    out->kind = TC_RHS_STRUCT_CONSTRUCTOR;
    memset(&out->u.struct_ctor, 0, sizeof(out->u.struct_ctor));
    out->u.struct_ctor.struct_name = struct_name;
    while (tc_peek(tokens, *index)->kind != TC_TOK_RPAREN) {
        const TcToken *field_tok = tc_peek(tokens, *index);
        const TcToken *val_tok = NULL;
        size_t cap = out->u.struct_ctor.field_count;
        size_t field_idx = 0;

        if (field_tok->kind != TC_TOK_IDENTIFIER) {
            tc_rhs_free(out);
            return tc_syntax_error(diag, line_no, field_tok->column, "expected field name");
        }
        if (out->u.struct_ctor.field_count == cap) {
            size_t new_cap = cap == 0 ? 4 : cap * 2;
            void *new_fields = realloc(out->u.struct_ctor.fields,
                                       new_cap * sizeof(*out->u.struct_ctor.fields));
            if (!new_fields) {
                tc_rhs_free(out);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            out->u.struct_ctor.fields = new_fields;
        }
        field_idx = out->u.struct_ctor.field_count++;
        memset(&out->u.struct_ctor.fields[field_idx], 0,
               sizeof(out->u.struct_ctor.fields[field_idx]));
        out->u.struct_ctor.fields[field_idx].param_name = tc_token_strdup(field_tok, line_no, diag);
        if (!out->u.struct_ctor.fields[field_idx].param_name) {
            tc_rhs_free(out);
            return -1;
        }
        (*index)++;
        if (tc_expect_token(tokens, index, TC_TOK_COLON, line_no, diag) != 0) {
            tc_rhs_free(out);
            return -1;
        }
        val_tok = tc_peek(tokens, *index);
        if (val_tok->kind == TC_TOK_INTEGER || val_tok->kind == TC_TOK_BOOL_LIT ||
            val_tok->kind == TC_TOK_FLOAT_LIT || val_tok->kind == TC_TOK_NULLPTR ||
            (val_tok->kind == TC_TOK_IDENTIFIER &&
             (*index + 1 >= tokens->count ||
              tc_peek(tokens, *index + 1)->kind != TC_TOK_LPAREN))) {
            if (tc_parse_operand(tokens, index, line_no,
                                 &out->u.struct_ctor.fields[field_idx].value_op, diag) != 0) {
                tc_rhs_free(out);
                return -1;
            }
        } else {
            out->u.struct_ctor.fields[field_idx].has_rhs = 1;
            out->u.struct_ctor.fields[field_idx].value_rhs =
                (struct TcRhs *)calloc(1, sizeof(TcRhs));
            if (!out->u.struct_ctor.fields[field_idx].value_rhs) {
                tc_rhs_free(out);
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            if (tc_parse_rhs(ctx, tokens, index, line_no,
                             (TcRhs *)out->u.struct_ctor.fields[field_idx].value_rhs, diag) != 0) {
                tc_rhs_free(out);
                return -1;
            }
        }
        if (tc_peek(tokens, *index)->kind == TC_TOK_COMMA) {
            (*index)++;
        }
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_keyword_error(TcDiagnostic *diag, int line, int column, const char *message) {
    tc_diagnostic_set(diag, TC_CE_KEYWORD, line, column, message);
    return -1;
}

static int tc_token_starts_call(TcTokenKind kind) {
    return kind == TC_TOK_ARITH_OP || kind == TC_TOK_UNARY_OP ||
           kind == TC_TOK_COMPARE_OP || kind == TC_TOK_LOGIC_OP ||
           kind == TC_TOK_BITWISE_OP || kind == TC_TOK_SHIFT_OP ||
           kind == TC_TOK_CAST || kind == TC_TOK_BITCAST;
}

static int tc_find_nested_const_call(const TcTokenList *tokens, size_t start) {
    size_t i = 0;
    int depth = 0;

    for (i = start + 1; i + 1 < tokens->count; i++) {
        if (tokens->items[i].kind == TC_TOK_LPAREN) {
            depth++;
            continue;
        }
        if (tokens->items[i].kind == TC_TOK_RPAREN) {
            depth--;
            continue;
        }
        if (depth > 0 && tc_token_starts_call(tokens->items[i].kind) &&
            tokens->items[i + 1].kind == TC_TOK_LPAREN) {
            return tokens->items[i].column;
        }
    }
    return TC_COLUMN_UNKNOWN;
}

static int tc_parse_type_token(const TcTokenList *tokens, size_t *index, int line_no,
                               TcTypeKind *out, TcDiagnostic *diag) {
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
    TcTypeKind type = TC_INT32;
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
            tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line_no, maybe_mode->column,
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
    TcTypeKind type = TC_INT32;
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
            tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line_no, maybe_mode->column,
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

/* @brief 解析 cast RHS：cast(type [,truncate,] operand) */
static int tc_parse_cast_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                             TcRhs *out, TcDiagnostic *diag) {
    TcTypeKind target = TC_INT32;
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

    /* 可选 truncate；类型与位宽约束由 Analyzer 稳定映射为 ModeMismatch。 */
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
        }
    }

    out->kind = TC_RHS_CAST;
    memset(&out->u.cast, 0, sizeof(out->u.cast));
    out->u.cast.target = target;
    out->u.cast.mode = mode;
    if (tc_parse_operand(tokens, index, line_no, &out->u.cast.source, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }

    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_rhs_free(out);
        return -1;
    }
    return 0;
}

static int tc_parse_bitcast_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                TcRhs *out, TcDiagnostic *diag) {
    TcBitcastRhs bitcast;

    memset(&bitcast, 0, sizeof(bitcast));
    if (tc_expect_token(tokens, index, TC_TOK_BITCAST, line_no, diag) != 0 ||
        tc_expect_token(tokens, index, TC_TOK_LPAREN, line_no, diag) != 0 ||
        tc_parse_type_token(tokens, index, line_no, &bitcast.target, diag) != 0 ||
        tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0 ||
        tc_parse_operand(tokens, index, line_no, &bitcast.source, diag) != 0) {
        tc_operand_free(&bitcast.source);
        return -1;
    }
    if (tc_expect_token(tokens, index, TC_TOK_RPAREN, line_no, diag) != 0) {
        tc_operand_free(&bitcast.source);
        return -1;
    }
    out->kind = TC_RHS_BITCAST;
    out->u.bitcast = bitcast;
    return 0;
}

/*
 * @brief 解析比较 RHS：eq/ne/lt/le/gt/ge(int_type, lhs, rhs)
 */
static int tc_parse_compare_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                TcRhs *out, TcDiagnostic *diag) {
    const TcToken *op_tok = tc_peek(tokens, *index);
    TcCompareOp op = op_tok->u.compare_op;
    TcTypeKind type = TC_INT32;

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
            tc_diagnostic_set(diag, TC_CE_MODE_MISMATCH, line_no, maybe_mode->column,
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
                                     TcBitwiseOp op, TcTypeKind type, TcRhs *out,
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
                                    TcTypeKind type, TcRhs *out, TcDiagnostic *diag) {
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
    TcTypeKind type = TC_INT32;
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
        if (logic_op != TC_LOGIC_AND && logic_op != TC_LOGIC_OR && logic_op != TC_LOGIC_XOR) {
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
    if (logic_op == TC_LOGIC_XOR) {
        return tc_finish_bitwise_bin_rhs(tokens, index, line_no, TC_BIT_XOR, type, out, diag);
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
    TcTypeKind type = TC_INT32;

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
    TcTypeKind type = TC_INT32;
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
    (void)is_const;
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
 * @brief 解析编译期 cast RHS：cast(type [,truncate,], const_operand)
 */
static int tc_parse_const_cast_rhs(const TcTokenList *tokens, size_t *index, int line_no,
                                   TcRhs *out, TcDiagnostic *diag) {
    TcTypeKind target = TC_INT32;

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

    out->kind = TC_RHS_CONST_CAST;
    memset(&out->u.const_cast, 0, sizeof(out->u.const_cast));
    out->u.const_cast.target = target;
    out->u.const_cast.mode = TC_TRUNC_STRICT;

    {
        const TcToken *maybe_mode = tc_peek(tokens, *index);
        if (maybe_mode->kind == TC_TOK_TRUNCATE) {
            out->u.const_cast.mode = TC_TRUNC_TRUNCATE;
            (*index)++;
            if (tc_expect_token(tokens, index, TC_TOK_COMMA, line_no, diag) != 0) {
                return -1;
            }
        } else if (maybe_mode->kind == TC_TOK_WRAP || maybe_mode->kind == TC_TOK_IEEE) {
            return tc_keyword_error(diag, line_no, maybe_mode->column,
                                    "invalid mode for cast");
        }
    }

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

int tc_parse_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index, int line_no,
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
    } else if (tok->kind == TC_TOK_NULLPTR) {
        out->kind = TC_RHS_LIT;
        memset(&out->u.lit, 0, sizeof(out->u.lit));
        out->u.lit.is_nullptr = 1;
        (*index)++;
        rc = 0;
    } else if (tok->kind == TC_TOK_SELF) {
        rc = tc_parse_self_member_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_MEMBLOCK) {
        rc = tc_parse_memblock_ctor_rhs(ctx, tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_PTR_LOAD) {
        rc = tc_parse_ptr_load_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_PTR_ADDRESS) {
        rc = tc_parse_ptr_address_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_MEMBLOCK_LOAD) {
        rc = tc_parse_memblock_load_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_PTR_ADD) {
        rc = tc_parse_ptr_arith_rhs(tokens, index, line_no, TC_RHS_PTR_ADD, out, diag);
    } else if (tok->kind == TC_TOK_PTR_SUB) {
        rc = tc_parse_ptr_arith_rhs(tokens, index, line_no, TC_RHS_PTR_SUB, out, diag);
    } else if (tok->kind == TC_TOK_PTR_EQ) {
        rc = tc_parse_ptr_compare_rhs(tokens, index, line_no, TC_RHS_PTR_EQ, out, diag);
    } else if (tok->kind == TC_TOK_PTR_NE) {
        rc = tc_parse_ptr_compare_rhs(tokens, index, line_no, TC_RHS_PTR_NE, out, diag);
    } else if (tok->kind == TC_TOK_PTR_LT) {
        rc = tc_parse_ptr_compare_rhs(tokens, index, line_no, TC_RHS_PTR_LT, out, diag);
    } else if (tok->kind == TC_TOK_PTR_LE) {
        rc = tc_parse_ptr_compare_rhs(tokens, index, line_no, TC_RHS_PTR_LE, out, diag);
    } else if (tok->kind == TC_TOK_PTR_GT) {
        rc = tc_parse_ptr_compare_rhs(tokens, index, line_no, TC_RHS_PTR_GT, out, diag);
    } else if (tok->kind == TC_TOK_PTR_GE) {
        rc = tc_parse_ptr_compare_rhs(tokens, index, line_no, TC_RHS_PTR_GE, out, diag);
    } else if (tok->kind == TC_TOK_PTR_SIZE) {
        rc = tc_parse_ptr_size_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_IDENTIFIER) {
        if (*index + 1 < tokens->count &&
                   tc_peek(tokens, *index + 1)->kind == TC_TOK_LPAREN &&
                   !tc_is_rhs_builtin_ident(tok)) {
            rc = tc_parse_struct_ctor_rhs(ctx, tokens, index, line_no, out, diag);
        } else if (*index + 3 < tokens->count &&
                   tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT &&
                   tc_peek(tokens, *index + 2)->kind == TC_TOK_IDENTIFIER &&
                   tc_peek(tokens, *index + 3)->kind == TC_TOK_LPAREN &&
                   !tc_is_rhs_builtin_ident(tok)) {
            rc = tc_parse_struct_ctor_rhs(ctx, tokens, index, line_no, out, diag);
        } else if (*index + 2 < tokens->count &&
                   tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT &&
                   tc_peek(tokens, *index + 2)->kind == TC_TOK_LPAREN &&
                   !tc_is_rhs_builtin_ident(tok)) {
            rc = tc_parse_struct_ctor_rhs(ctx, tokens, index, line_no, out, diag);
        } else if (*index + 1 < tokens->count &&
                   tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT) {
            rc = tc_parse_field_read_rhs(tokens, index, line_no, out, diag);
        } else {
            out->kind = TC_RHS_CONST_REF;
            out->u.const_ref.name = tc_token_strdup(tok, line_no, diag);
            if (!out->u.const_ref.name) {
                rc = -1;
            } else {
                (*index)++;
                rc = 0;
            }
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
        rc = tc_parse_shift_rhs(tokens, index, line_no, out, diag, 0);
    } else if (tok->kind == TC_TOK_CAST) {
        rc = tc_parse_cast_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_BITCAST) {
        rc = tc_parse_bitcast_rhs(tokens, index, line_no, out, diag);
    } else {
        rc = tc_syntax_error(diag, line_no, tok->column, "expected rhs expression");
    }

    ctx->depth--;
    return rc;
}

int tc_parse_const_rhs(TcParserCtx *ctx, const TcTokenList *tokens, size_t *index,
                              int line_no, TcRhs *out, TcDiagnostic *diag) {
    int rc;
    const TcToken *tok;

    if (ctx->depth >= TC_PARSER_MAX_DEPTH) {
        tok = tc_peek(tokens, *index);
        return tc_syntax_error(diag, line_no, tok->column, "expression too complex");
    }
    ctx->depth++;

    tok = tc_peek(tokens, *index);

    if (tc_token_starts_call(tok->kind)) {
        int nested_column = tc_find_nested_const_call(tokens, *index);

        if (nested_column != TC_COLUMN_UNKNOWN) {
            ctx->depth--;
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line_no, nested_column,
                              "nested calls are not allowed in constant expression");
            return -1;
        }
    }

    if (tok->kind == TC_TOK_INTEGER || tok->kind == TC_TOK_BOOL_LIT || tok->kind == TC_TOK_FLOAT_LIT) {
        out->kind = TC_RHS_LIT;
        out->u.lit = tok->u.literal;
        (*index)++;
        rc = 0;
    } else if (tok->kind == TC_TOK_NULLPTR) {
        out->kind = TC_RHS_LIT;
        memset(&out->u.lit, 0, sizeof(out->u.lit));
        out->u.lit.is_nullptr = 1;
        (*index)++;
        rc = 0;
    } else if (tok->kind == TC_TOK_IDENTIFIER) {
        if (*index + 1 < tokens->count &&
            tc_peek(tokens, *index + 1)->kind == TC_TOK_LPAREN &&
            !tc_is_rhs_builtin_ident(tok)) {
            rc = tc_parse_struct_ctor_rhs(ctx, tokens, index, line_no, out, diag);
        } else if (*index + 3 < tokens->count &&
                   tc_peek(tokens, *index + 1)->kind == TC_TOK_DOT &&
                   tc_peek(tokens, *index + 2)->kind == TC_TOK_IDENTIFIER &&
                   tc_peek(tokens, *index + 3)->kind == TC_TOK_LPAREN &&
                   !tc_is_rhs_builtin_ident(tok)) {
            rc = tc_parse_struct_ctor_rhs(ctx, tokens, index, line_no, out, diag);
        } else {
            out->kind = TC_RHS_CONST_REF;
            out->u.const_ref.name = strndup(tok->start, tok->length);
            if (!out->u.const_ref.name) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line_no, tok->column,
                                  "memory allocation failed");
                rc = -1;
            } else {
                (*index)++;
                rc = 0;
            }
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
    } else if (tok->kind == TC_TOK_BITCAST) {
        rc = tc_parse_bitcast_rhs(tokens, index, line_no, out, diag);
    } else if (tok->kind == TC_TOK_SELF) {
        rc = tc_parse_self_member_rhs(tokens, index, line_no, out, diag);
    } else {
        rc = tc_syntax_error(diag, line_no, tok->column, "expected constant expression");
    }

    ctx->depth--;
    return rc;
}
