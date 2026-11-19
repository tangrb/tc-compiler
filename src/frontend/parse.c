/* TC 0.0.46 SYN parser (standard §14, §4.2, §4.11.3). */
#include "frontend/parse.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    void *items;
    size_t count;
    size_t cap;
} TcVec;

typedef struct {
    TcTokenKind kind;
    TcSpan span;
} TcOpenBlock;

typedef struct {
    TcArena *arena;
    TcParseModule *module;
    TcToken *tokens;
    size_t ntokens;
    size_t pos;
    int is_program;
    TcFrontendDiag diag;
    int has_diag;
    TcImplError impl;
    jmp_buf env;
    TcVec blocks;
    unsigned unsafe_depth;
} TcParser;

/* ------------------------------------------------------------------------- */
/* Vector and failure helpers.                                               */
/* ------------------------------------------------------------------------- */

static void parse_oom(TcParser *p) {
    p->impl = TC_IMPL_OOM;
    longjmp(p->env, 1);
}

static void parse_impl_limit(TcParser *p) {
    p->impl = TC_IMPL_INTERNAL;
    longjmp(p->env, 1);
}

static void vec_push(TcParser *p, TcVec *vec, const void *item, size_t size) {
    if (vec->count == vec->cap) {
        size_t cap = vec->cap == 0 ? 8u : vec->cap * 2u;
        void *fresh = tc_arena_alloc(p->arena, cap * size);
        if (fresh == NULL) {
            parse_oom(p);
        }
        if (vec->count != 0) {
            memcpy(fresh, vec->items, vec->count * size);
        }
        vec->items = fresh;
        vec->cap = cap;
    }
    memcpy((char *)vec->items + vec->count * size, item, size);
    vec->count++;
}

static void parse_fail_span(TcParser *p, TcDiagCode code, TcSpan span) {
    memset(&p->diag, 0, sizeof(p->diag));
    p->diag.code = code;
    p->diag.stage = TC_STAGE_SYN;
    p->diag.span = span;
    p->has_diag = 1;
    longjmp(p->env, 1);
}

static void parse_fail(TcParser *p, TcDiagCode code, const TcToken *token) {
    parse_fail_span(p, code, token->span);
}

static TcToken *peek(TcParser *p) {
    if (p->pos < p->ntokens) {
        return &p->tokens[p->pos];
    }
    return &p->tokens[p->ntokens - 1u];
}

static TcToken *peek2(TcParser *p) {
    if (p->pos + 1u < p->ntokens) {
        return &p->tokens[p->pos + 1u];
    }
    return &p->tokens[p->ntokens - 1u];
}

static TcToken *advance(TcParser *p) {
    TcToken *token = peek(p);
    if (token->kind != TC_TOK_EOF) {
        p->pos++;
    }
    return token;
}

static TcToken *expect(TcParser *p, TcTokenKind kind) {
    TcToken *token = peek(p);
    if (token->kind != kind) {
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    return advance(p);
}

static TcToken *expect_ident(TcParser *p) {
    TcToken *token = peek(p);
    if (token->kind == TC_TOK_SELF && p->is_program) {
        /* §4.2/§4.8: #program may not use Self anywhere.  The rule is applied
         * uniformly, including IDENT-only declaration positions. */
        parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
    }
    if (token->kind != TC_TOK_IDENT) {
        /* DISCARD in an IDENT-only position is TC_CE_SYNTAX at the '_'
         * (standard §12.5.1), which is exactly this position. */
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    return advance(p);
}

static void expect_nl(TcParser *p) {
    TcToken *token = peek(p);
    if (token->kind != TC_TOK_NL) {
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    advance(p);
}

static void push_block(TcParser *p, TcTokenKind kind, TcSpan span) {
    TcOpenBlock block;
    if (p->blocks.count >= TC_FRONTEND_MAX_NESTING) {
        parse_impl_limit(p);
    }
    block.kind = kind;
    block.span = span;
    vec_push(p, &p->blocks, &block, sizeof(block));
}

static void pop_block(TcParser *p) {
    if (p->blocks.count == 0) {
        parse_impl_limit(p);
    }
    p->blocks.count--;
}

static void fail_missing_end(TcParser *p) {
    TcOpenBlock *blocks = (TcOpenBlock *)p->blocks.items;
    if (p->blocks.count == 0) {
        parse_fail(p, TC_CE_SYNTAX, peek(p));
    }
    parse_fail_span(p, TC_CE_MISSING_END, blocks[0].span);
}

/* Count the `then`/`end` balance from the current token to EOF: `ends` is the
 * number of `end` tokens that close one of the blocks already open on the
 * parser stack (i.e. `end`s seen at nesting depth 0 after pairing nested
 * `then`s).  This is a lexical pairing lookahead, not a grammar parse.
 *
 * If fewer ends remain than open enclosing blocks, at least one enclosing
 * opener is genuinely unpaired and MISSING_END (at the earliest opener) is
 * the right diagnostic.  If enough ends remain, the enclosing blocks are
 * paired; the unexpected module-level construct is an ordinary syntax error
 * at its own first token (e.g. an illegal nested `func`/`struct` inside a
 * struct or function that does have its own `end`). */
static int enclosing_blocks_paired(TcParser *p) {
    size_t i;
    int depth = 0;
    size_t ends = 0;
    for (i = p->pos; i < p->ntokens; i++) {
        TcTokenKind kind = p->tokens[i].kind;
        if (kind == TC_TOK_EOF || kind == TC_TOK_PROGRAM ||
            kind == TC_TOK_LIB) {
            /* A new module pattern cannot close the current block. */
            break;
        }
        if (kind == TC_TOK_THEN) {
            depth++;
        } else if (kind == TC_TOK_END) {
            if (depth > 0) {
                depth--;
            } else {
                ends++;
            }
        }
    }
    return ends >= p->blocks.count;
}

static void fail_member_or_syntax(TcParser *p, const TcToken *token) {
    if (!enclosing_blocks_paired(p)) {
        fail_missing_end(p);
    }
    parse_fail(p, TC_CE_SYNTAX, token);
}

static int is_module_keyword(TcTokenKind kind) {
    switch (kind) {
    case TC_TOK_FUNC:
    case TC_TOK_STRUCT:
    case TC_TOK_PUBLIC:
    case TC_TOK_PRIVATE:
    case TC_TOK_STATIC:
    case TC_TOK_IMPORT:
    case TC_TOK_PROGRAM:
    case TC_TOK_LIB:
    case TC_TOK_END:
        return 1;
    default:
        return 0;
    }
}

static int is_type_start(TcTokenKind kind) {
    switch (kind) {
    case TC_TOK_VOID:
    case TC_TOK_BOOL:
    case TC_TOK_INT8:
    case TC_TOK_INT16:
    case TC_TOK_INT32:
    case TC_TOK_INT64:
    case TC_TOK_UINT8:
    case TC_TOK_UINT16:
    case TC_TOK_UINT32:
    case TC_TOK_UINT64:
    case TC_TOK_ISIZE:
    case TC_TOK_USIZE:
    case TC_TOK_FLOAT32:
    case TC_TOK_FLOAT64:
    case TC_TOK_PTR:
    case TC_TOK_FNPTR:
        return 1;
    default:
        return 0;
    }
}

static int is_value_start(TcTokenKind kind) {
    switch (kind) {
    case TC_TOK_IDENT:
    case TC_TOK_SELF:
    case TC_TOK_INT:
    case TC_TOK_FLOAT:
    case TC_TOK_INF:
    case TC_TOK_INF_KW:
    case TC_TOK_TRUE:
    case TC_TOK_FALSE:
    case TC_TOK_NULLPTR:
    case TC_TOK_NAN:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Types and values.                                                         */
/* ------------------------------------------------------------------------- */

static TcTypeTag scalar_tag(TcTokenKind kind) {
    switch (kind) {
    case TC_TOK_INT8: return TC_TY_I8;
    case TC_TOK_INT16: return TC_TY_I16;
    case TC_TOK_INT32: return TC_TY_I32;
    case TC_TOK_INT64: return TC_TY_I64;
    case TC_TOK_UINT8: return TC_TY_U8;
    case TC_TOK_UINT16: return TC_TY_U16;
    case TC_TOK_UINT32: return TC_TY_U32;
    case TC_TOK_UINT64: return TC_TY_U64;
    case TC_TOK_ISIZE: return TC_TY_ISIZE;
    case TC_TOK_USIZE: return TC_TY_USIZE;
    case TC_TOK_FLOAT32: return TC_TY_F32;
    case TC_TOK_FLOAT64: return TC_TY_F64;
    case TC_TOK_BOOL: return TC_TY_BOOL;
    default: return TC_TY_VOID;
    }
}

static TcParseType *new_type(TcParser *p, TcParseTypeKind kind, TcSpan span) {
    TcParseType *type = (TcParseType *)tc_arena_alloc0(p->arena, sizeof(*type));
    if (type == NULL) {
        parse_oom(p);
    }
    type->kind = kind;
    type->span = span;
    return type;
}

static TcParseType *parse_type_depth(TcParser *p, unsigned depth);

static TcParseType *parse_type_fnptr(TcParser *p, TcToken *keyword,
                                     unsigned depth) {
    TcParseType *type = new_type(p, TC_PTYPE_FNPTR, keyword->span);
    TcVec params = {0};
    expect(p, TC_TOK_LT);
    type->ret = parse_type_depth(p, depth + 1u);
    if (peek(p)->kind == TC_TOK_COMMA) {
        for (;;) {
            TcParseType *param;
            advance(p);
            if (peek(p)->kind == TC_TOK_GT) {
                parse_fail(p, TC_CE_SYNTAX, peek(p));
            }
            param = parse_type_depth(p, depth + 1u);
            vec_push(p, &params, &param, sizeof(param));
            if (peek(p)->kind != TC_TOK_COMMA) {
                break;
            }
        }
    }
    expect(p, TC_TOK_GT);
    type->params = (TcParseType **)params.items;
    type->nparams = params.count;
    return type;
}

static TcParseType *parse_type_depth(TcParser *p, unsigned depth) {
    TcToken *token = peek(p);
    if (depth > TC_FRONTEND_MAX_NESTING) {
        parse_impl_limit(p);
    }
    if (token->kind >= TC_TOK_VOID && token->kind <= TC_TOK_FNPTR) {
        advance(p);
        if (token->kind == TC_TOK_VOID) {
            return new_type(p, TC_PTYPE_VOID, token->span);
        }
        if (token->kind == TC_TOK_PTR) {
            TcParseType *type = new_type(p, TC_PTYPE_PTR, token->span);
            expect(p, TC_TOK_LT);
            type->pointee = parse_type_depth(p, depth + 1u);
            expect(p, TC_TOK_GT);
            return type;
        }
        if (token->kind == TC_TOK_FNPTR) {
            return parse_type_fnptr(p, token, depth);
        }
        {
            TcParseType *type = new_type(p, TC_PTYPE_SCALAR, token->span);
            type->scalar = scalar_tag(token->kind);
            return type;
        }
    }
    if (token->kind == TC_TOK_IDENT) {
        TcParseType *type = new_type(p, TC_PTYPE_NAME, token->span);
        type->base = token->text;
        advance(p);
        if (peek(p)->kind == TC_TOK_DOT) {
            TcToken *member;
            advance(p);
            member = expect_ident(p);
            type->member = member->text;
            type->member_span = member->span;
            if (peek(p)->kind == TC_TOK_DOT) {
                parse_fail(p, TC_CE_SYNTAX, peek(p));
            }
        }
        return type;
    }
    if (token->kind == TC_TOK_SELF && p->is_program) {
        parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
    }
    parse_fail(p, TC_CE_SYNTAX, token);
    return NULL;
}

static TcParseType *parse_type(TcParser *p) {
    return parse_type_depth(p, 0);
}

static TcParseValue parse_value(TcParser *p) {
    TcToken *token = peek(p);
    TcParseValue value;
    memset(&value, 0, sizeof(value));
    value.span = token->span;
    switch (token->kind) {
    case TC_TOK_INT:
        value.kind = TC_PVAL_INT;
        value.int_negative = token->u.integer.negative;
        value.int_magnitude = token->u.integer.magnitude;
        value.int_unsigned_suffix = token->u.integer.has_unsigned_suffix;
        value.int_radix = token->u.integer.radix;
        advance(p);
        return value;
    case TC_TOK_FLOAT:
        value.kind = TC_PVAL_FLOAT;
        value.float_negative = token->u.flt.negative;
        value.float_bits = token->u.flt.bits;
        value.float_width = token->u.flt.width;
        advance(p);
        return value;
    case TC_TOK_INF:
        value.kind = TC_PVAL_INF;
        value.inf_negative = token->u.inf.negative;
        advance(p);
        return value;
    case TC_TOK_INF_KW:
        value.kind = TC_PVAL_INF;
        value.inf_negative = 0;
        advance(p);
        return value;
    case TC_TOK_NAN:
        value.kind = TC_PVAL_NAN;
        advance(p);
        return value;
    case TC_TOK_TRUE:
    case TC_TOK_FALSE:
        value.kind = TC_PVAL_BOOL;
        value.bool_value = token->kind == TC_TOK_TRUE;
        advance(p);
        return value;
    case TC_TOK_NULLPTR:
        value.kind = TC_PVAL_NULLPTR;
        advance(p);
        return value;
    case TC_TOK_IDENT: {
        TcVec fields = {0};
        TcVec field_spans = {0};
        value.kind = TC_PVAL_NAME;
        value.base = token->text;
        value.base_span = token->span;
        advance(p);
        while (peek(p)->kind == TC_TOK_DOT) {
            TcToken *field_token;
            advance(p);
            field_token = expect_ident(p);
            vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
            vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
        }
        value.fields = (const char **)fields.items;
        value.field_spans = (TcSpan *)field_spans.items;
        value.nfields = fields.count;
        return value;
    }
    case TC_TOK_SELF: {
        TcVec fields = {0};
        TcVec field_spans = {0};
        TcToken *field_token;
        if (p->is_program) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
        }
        value.kind = TC_PVAL_NAME;
        value.base = "Self";
        value.base_span = token->span;
        advance(p);
        if (peek(p)->kind != TC_TOK_DOT) {
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        advance(p);
        field_token = expect_ident(p);
        vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
        vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
        while (peek(p)->kind == TC_TOK_DOT) {
            advance(p);
            field_token = expect_ident(p);
            vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
            vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
        }
        value.fields = (const char **)fields.items;
        value.field_spans = (TcSpan *)field_spans.items;
        value.nfields = fields.count;
        return value;
    }
    case TC_TOK_DISCARD:
        parse_fail(p, TC_CE_SYNTAX, token);
        break;
    default:
        break;
    }
    parse_fail(p, TC_CE_SYNTAX, token);
    return value;
}

static void parse_lvalue(TcParser *p, TcParseInstr *instr) {
    TcToken *token = peek(p);
    if (token->kind == TC_TOK_DISCARD) {
        instr->dst_discard = 1;
        advance(p);
        return;
    }
    if (token->kind == TC_TOK_IDENT) {
        TcVec fields = {0};
        TcVec field_spans = {0};
        instr->dst_base = token->text;
        instr->dst_base_span = token->span;
        advance(p);
        while (peek(p)->kind == TC_TOK_DOT) {
            TcToken *field_token;
            advance(p);
            field_token = expect_ident(p);
            vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
            vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
        }
        instr->dst_fields = (const char **)fields.items;
        instr->dst_field_spans = (TcSpan *)field_spans.items;
        instr->dst_nfields = fields.count;
        return;
    }
    if (token->kind == TC_TOK_SELF) {
        TcVec fields = {0};
        TcVec field_spans = {0};
        TcToken *field_token;
        if (p->is_program) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
        }
        instr->dst_base = "Self";
        instr->dst_base_span = token->span;
        advance(p);
        if (peek(p)->kind != TC_TOK_DOT) {
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        advance(p);
        field_token = expect_ident(p);
        vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
        vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
        while (peek(p)->kind == TC_TOK_DOT) {
            advance(p);
            field_token = expect_ident(p);
            vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
            vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
        }
        instr->dst_fields = (const char **)fields.items;
        instr->dst_field_spans = (TcSpan *)field_spans.items;
        instr->dst_nfields = fields.count;
        return;
    }
    parse_fail(p, TC_CE_SYNTAX, token);
}

/* ------------------------------------------------------------------------- */
/* Instructions.                                                             */
/* ------------------------------------------------------------------------- */

static TcParseArg parse_arg(TcParser *p) {
    TcParseArg arg;
    memset(&arg, 0, sizeof(arg));
    arg.span = peek(p)->span;
    if (peek(p)->kind == TC_TOK_IDENT && peek2(p)->kind == TC_TOK_COLON) {
        TcToken *name = advance(p);
        advance(p);
        arg.name = name->text;
        arg.kind = TC_PARG_VALUE;
        arg.value = parse_value(p);
        arg.span = name->span;
        return arg;
    }
    if (is_type_start(peek(p)->kind)) {
        arg.kind = TC_PARG_TYPE;
        arg.type = *parse_type(p);
        arg.span = arg.type.span;
        return arg;
    }
    if (is_value_start(peek(p)->kind)) {
        arg.kind = TC_PARG_VALUE;
        arg.value = parse_value(p);
        arg.span = arg.value.span;
        return arg;
    }
    parse_fail(p, TC_CE_SYNTAX, peek(p));
    return arg;
}

static void parse_op_form(TcParser *p, TcParseInstr *instr) {
    TcToken *token = peek(p);
    TcVec args = {0};
    /* Keep the caller-provided LHS (dst_*); only the op form fields are
     * replaced. */
    instr->args = NULL;
    instr->nargs = 0;
    if (token->kind == TC_TOK_SELF && p->is_program) {
        /* A bare-value initializer/assignment starting with Self is also a
         * #program Self misuse; the specific code wins over generic SYNTAX
         * at the same position (standard §4.2/§12.2 #4). */
        parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
    }
    if (token->kind != TC_TOK_MNEMONIC) {
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    if (!token->u.mnemonic.combo_valid) {
        /* Unknown opcode suffix or undefined (base, suffix) combination:
         * TC_CE_SYNTAX at the mnemonic's first token (standard §12.5.1). */
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    advance(p);
    instr->op = token->u.mnemonic.op;
    instr->mode = token->u.mnemonic.mode;
    instr->has_suffix = token->u.mnemonic.has_suffix;
    instr->span = token->span;
    instr->suffix_span = token->u.mnemonic.suffix_span;
    instr->end_span = token->span;
    while (peek(p)->kind != TC_TOK_NL && peek(p)->kind != TC_TOK_EOF) {
        TcParseArg arg;
        if (peek(p)->kind == TC_TOK_COMMA) {
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        arg = parse_arg(p);
        vec_push(p, &args, &arg, sizeof(arg));
        instr->end_span = p->tokens[p->pos - 1u].span;
        if (peek(p)->kind == TC_TOK_COMMA) {
            advance(p);
            if (peek(p)->kind == TC_TOK_NL || peek(p)->kind == TC_TOK_EOF) {
                parse_fail(p, TC_CE_SYNTAX, peek(p));
            }
            continue;
        }
        break;
    }
    instr->args = (TcParseArg *)args.items;
    instr->nargs = args.count;
    if (instr->op == TC_SWITCH && instr->nargs > 1) {
        size_t i;
        TcParseValue *targets = (TcParseValue *)tc_arena_alloc(
            p->arena, (instr->nargs - 1u) * sizeof(TcParseValue));
        if (targets == NULL) {
            parse_oom(p);
        }
        for (i = 1; i < instr->nargs; i++) {
            targets[i - 1u] = instr->args[i].value;
        }
        instr->switch_targets = targets;
        instr->ntargets = instr->nargs - 1u;
    }
}

static TcParseConstInit parse_const_init(TcParser *p) {
    TcParseConstInit init;
    memset(&init, 0, sizeof(init));
    init.span = peek(p)->span;
    if (peek(p)->kind == TC_TOK_MNEMONIC) {
        init.is_op = 1;
        parse_op_form(p, &init.op);
    } else if (is_value_start(peek(p)->kind)) {
        init.is_op = 0;
        init.value = parse_value(p);
    } else {
        parse_fail(p, TC_CE_SYNTAX, peek(p));
    }
    return init;
}

/* ------------------------------------------------------------------------- */
/* Annotations.                                                              */
/* ------------------------------------------------------------------------- */

static TcParseValue parse_annotation_arg(TcParser *p) {
    TcToken *token = peek(p);
    TcParseValue value;
    memset(&value, 0, sizeof(value));
    value.span = token->span;
    if (token->kind == TC_TOK_INT) {
        value.kind = TC_PVAL_INT;
        value.int_negative = token->u.integer.negative;
        value.int_magnitude = token->u.integer.magnitude;
        value.int_unsigned_suffix = token->u.integer.has_unsigned_suffix;
        value.int_radix = token->u.integer.radix;
        advance(p);
        return value;
    }
    if (token->kind == TC_TOK_IDENT || token->kind == TC_TOK_SELF) {
        TcVec fields = {0};
        TcVec field_spans = {0};
        TcToken *field_token;
        value.kind = TC_PVAL_NAME;
        value.base_span = token->span;
        if (token->kind == TC_TOK_SELF) {
            if (p->is_program) {
                parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
            }
            value.base = "Self";
            advance(p);
            if (peek(p)->kind != TC_TOK_DOT) {
                parse_fail(p, TC_CE_SYNTAX, peek(p));
            }
            advance(p);
            field_token = expect_ident(p);
            vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
            vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
        } else {
            value.base = token->text;
            advance(p);
            if (peek(p)->kind == TC_TOK_DOT) {
                advance(p);
                field_token = expect_ident(p);
                vec_push(p, &fields, &field_token->text, sizeof(field_token->text));
                vec_push(p, &field_spans, &field_token->span, sizeof(field_token->span));
            }
        }
        if (peek(p)->kind == TC_TOK_DOT) {
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        value.fields = (const char **)fields.items;
        value.field_spans = (TcSpan *)field_spans.items;
        value.nfields = fields.count;
        return value;
    }
    parse_fail(p, TC_CE_SYNTAX, token);
    return value;
}

static TcParseAnnotation parse_annotation(TcParser *p) {
    TcToken *token = peek(p);
    TcParseAnnotation annotation;
    TcVec args = {0};
    memset(&annotation, 0, sizeof(annotation));
    if (token->kind != TC_TOK_ANNOTATION) {
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    annotation.span = token->span;
    annotation.name = token->text;
    advance(p);
    if (peek(p)->kind == TC_TOK_LPAREN) {
        advance(p);
        if (peek(p)->kind == TC_TOK_RPAREN) {
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        for (;;) {
            TcParseValue value = parse_annotation_arg(p);
            vec_push(p, &args, &value, sizeof(value));
            if (peek(p)->kind == TC_TOK_COMMA) {
                advance(p);
                if (peek(p)->kind == TC_TOK_RPAREN) {
                    parse_fail(p, TC_CE_SYNTAX, peek(p));
                }
                continue;
            }
            break;
        }
        expect(p, TC_TOK_RPAREN);
    }
    annotation.args = (TcParseValue *)args.items;
    annotation.nargs = args.count;
    return annotation;
}

static void parse_annotations(TcParser *p, TcVec *out) {
    while (peek(p)->kind == TC_TOK_ANNOTATION) {
        TcParseAnnotation annotation = parse_annotation(p);
        vec_push(p, out, &annotation, sizeof(annotation));
    }
}

/* ------------------------------------------------------------------------- */
/* Declarations.                                                             */
/* ------------------------------------------------------------------------- */

static TcParseImport parse_import(TcParser *p) {
    TcToken *keyword = expect(p, TC_TOK_IMPORT);
    TcToken *name = expect_ident(p);
    TcParseImport import_decl;
    import_decl.name = name->text;
    import_decl.name_span = name->span;
    import_decl.span = keyword->span;
    expect_nl(p);
    return import_decl;
}

static TcParseField parse_field(TcParser *p) {
    TcToken *modifier = peek(p);
    TcParseField field;
    memset(&field, 0, sizeof(field));
    if (modifier->kind == TC_TOK_READONLY) {
        field.readonly = 1;
        advance(p);
    } else if (modifier->kind == TC_TOK_VAR) {
        field.readonly = 0;
        advance(p);
    } else {
        parse_fail(p, TC_CE_SYNTAX, modifier);
    }
    field.span = modifier->span;
    {
        TcToken *name = expect_ident(p);
        field.name = name->text;
        field.name_span = name->span;
    }
    expect(p, TC_TOK_COLON);
    field.type = *parse_type(p);
    /* SYN accepts every ANNOTATION(INT) in the field_padding shape.  The
     * "@padding only" name rule and N constraints are SEM (standard §2.6:
     * unknown/illegal-position annotations -> TC_CE_ANNOTATION; wider INT
     * forms -> TC_CE_CONST_FORM).  A bare annotation without "(INT)" is not a
     * field_padding shape and remains TC_CE_SYNTAX (§14.3). */
    if (peek(p)->kind == TC_TOK_ANNOTATION) {
        TcParseAnnotation annotation;
        TcVec args = {0};
        memset(&annotation, 0, sizeof(annotation));
        annotation.span = peek(p)->span;
        annotation.name = peek(p)->text;
        advance(p);
        expect(p, TC_TOK_LPAREN);
        {
            TcToken *arg = expect(p, TC_TOK_INT);
            TcParseValue value;
            memset(&value, 0, sizeof(value));
            value.kind = TC_PVAL_INT;
            value.int_negative = arg->u.integer.negative;
            value.int_magnitude = arg->u.integer.magnitude;
            value.int_unsigned_suffix = arg->u.integer.has_unsigned_suffix;
            value.int_radix = arg->u.integer.radix;
            value.span = arg->span;
            vec_push(p, &args, &value, sizeof(value));
        }
        expect(p, TC_TOK_RPAREN);
        annotation.args = (TcParseValue *)args.items;
        annotation.nargs = args.count;
        field.has_padding = 1;
        field.padding = annotation;
    }
    expect_nl(p);
    return field;
}

static TcParseStruct parse_struct(TcParser *p, int is_public, int has_visibility,
                                  TcSpan visibility_span) {
    TcToken *keyword = expect(p, TC_TOK_STRUCT);
    TcParseStruct structure;
    TcVec fields = {0};
    memset(&structure, 0, sizeof(structure));
    structure.span = keyword->span;
    structure.is_public = is_public;
    structure.has_visibility = has_visibility;
    structure.visibility_span = visibility_span;
    push_block(p, TC_TOK_STRUCT, keyword->span);
    {
        TcToken *name = expect_ident(p);
        structure.name = name->text;
        structure.name_span = name->span;
    }
    expect(p, TC_TOK_THEN);
    expect_nl(p);
    for (;;) {
        TcToken *token = peek(p);
        if (token->kind == TC_TOK_END) {
            break;
        }
        if (token->kind == TC_TOK_EOF) {
            fail_missing_end(p);
        }
        if (token->kind == TC_TOK_READONLY || token->kind == TC_TOK_VAR) {
            TcParseField field = parse_field(p);
            vec_push(p, &fields, &field, sizeof(field));
            continue;
        }
        if (is_module_keyword(token->kind) || token->kind == TC_TOK_CONST ||
            token->kind == TC_TOK_UNSAFE) {
            fail_member_or_syntax(p, token);
        }
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    {
        TcToken *end = expect(p, TC_TOK_END);
        structure.end_span = end->span;
    }
    expect_nl(p);
    pop_block(p);
    structure.fields = (TcParseField *)fields.items;
    structure.nfields = fields.count;
    return structure;
}

static TcParseParam parse_param(TcParser *p) {
    TcParseParam param;
    TcVec annotations = {0};
    memset(&param, 0, sizeof(param));
    parse_annotations(p, &annotations);
    {
        TcToken *name = expect_ident(p);
        param.name = name->text;
        param.name_span = name->span;
    }
    expect(p, TC_TOK_COLON);
    param.type = *parse_type(p);
    param.annotations = (TcParseAnnotation *)annotations.items;
    param.nannotations = annotations.count;
    return param;
}

static void parse_statement_into(TcParser *p, TcVec *body);

static TcParseFunc parse_func(TcParser *p, int is_public, int has_visibility,
                              TcSpan visibility_span, TcVec *annotations) {
    TcToken *keyword = expect(p, TC_TOK_FUNC);
    TcParseFunc function;
    TcVec params = {0};
    TcVec body = {0};
    memset(&function, 0, sizeof(function));
    function.span = keyword->span;
    function.is_public = is_public;
    function.has_visibility = has_visibility;
    function.visibility_span = visibility_span;
    function.annotations = (TcParseAnnotation *)annotations->items;
    function.nannotations = annotations->count;
    push_block(p, TC_TOK_FUNC, keyword->span);
    {
        TcToken *name = expect_ident(p);
        function.name = name->text;
        function.name_span = name->span;
    }
    expect(p, TC_TOK_LPAREN);
    if (peek(p)->kind != TC_TOK_RPAREN) {
        for (;;) {
            TcParseParam param = parse_param(p);
            vec_push(p, &params, &param, sizeof(param));
            if (peek(p)->kind == TC_TOK_COMMA) {
                advance(p);
                if (peek(p)->kind == TC_TOK_RPAREN) {
                    parse_fail(p, TC_CE_SYNTAX, peek(p));
                }
                continue;
            }
            break;
        }
    }
    expect(p, TC_TOK_RPAREN);
    function.ret = *parse_type(p);
    expect(p, TC_TOK_THEN);
    expect_nl(p);
    for (;;) {
        TcToken *token = peek(p);
        if (token->kind == TC_TOK_END) {
            break;
        }
        if (token->kind == TC_TOK_EOF) {
            fail_missing_end(p);
        }
        if (is_module_keyword(token->kind)) {
            fail_member_or_syntax(p, token);
        }
        parse_statement_into(p, &body);
    }
    {
        TcToken *end = expect(p, TC_TOK_END);
        function.end_span = end->span;
    }
    expect_nl(p);
    pop_block(p);
    function.params = (TcParseParam *)params.items;
    function.nparams = params.count;
    function.body = (TcParseStmt *)body.items;
    function.nbody = body.count;
    return function;
}

/* ------------------------------------------------------------------------- */
/* Statements.                                                               */
/* ------------------------------------------------------------------------- */

static TcParseStmt parse_var_statement(TcParser *p) {
    TcToken *keyword = expect(p, TC_TOK_VAR);
    TcParseStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = TC_STMT_VAR;
    stmt.span = keyword->span;
    stmt.unsafe_depth = p->unsafe_depth;
    {
        TcToken *name = expect_ident(p);
        stmt.var.name = name->text;
        stmt.var.name_span = name->span;
        expect(p, TC_TOK_COLON);
        stmt.var.type = *parse_type(p);
        if (peek(p)->kind != TC_TOK_EQ) {
            parse_fail(p, TC_CE_VAR_MISSING_INIT, name);
        }
        advance(p);
        if (peek(p)->kind == TC_TOK_NL || peek(p)->kind == TC_TOK_EOF) {
            parse_fail(p, TC_CE_VAR_MISSING_INIT, name);
        }
        if (peek(p)->kind == TC_TOK_SELF && p->is_program) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, peek(p));
        }
        if (peek(p)->kind != TC_TOK_MNEMONIC) {
            /* The initializer must be one op, not a bare value (standard
             * §4.11.3/§6.12). */
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        parse_op_form(p, &stmt.var.init);
    }
    expect_nl(p);
    stmt.end_span = p->tokens[p->pos - 1u].span;
    return stmt;
}

static TcParseStmt parse_const_statement(TcParser *p) {
    TcToken *keyword = expect(p, TC_TOK_CONST);
    TcParseStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = TC_STMT_CONST;
    stmt.span = keyword->span;
    stmt.unsafe_depth = p->unsafe_depth;
    {
        TcToken *name = expect_ident(p);
        stmt.cnst.name = name->text;
        stmt.cnst.name_span = name->span;
        expect(p, TC_TOK_COLON);
        stmt.cnst.type = *parse_type(p);
        if (peek(p)->kind != TC_TOK_EQ) {
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        advance(p);
        if (peek(p)->kind == TC_TOK_NL || peek(p)->kind == TC_TOK_EOF) {
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        stmt.cnst.init = parse_const_init(p);
    }
    expect_nl(p);
    stmt.end_span = p->tokens[p->pos - 1u].span;
    return stmt;
}

static TcParseStmt parse_label_statement(TcParser *p) {
    TcToken *keyword = expect(p, TC_TOK_LABEL);
    TcParseStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = TC_STMT_LABEL;
    stmt.span = keyword->span;
    stmt.unsafe_depth = p->unsafe_depth;
    {
        TcToken *name = expect_ident(p);
        stmt.label.name = name->text;
        stmt.label.name_span = name->span;
    }
    expect(p, TC_TOK_COLON);
    expect_nl(p);
    stmt.end_span = p->tokens[p->pos - 1u].span;
    return stmt;
}

static TcParseStmt parse_instruction_statement(TcParser *p) {
    TcParseStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = TC_STMT_INSTR;
    stmt.span = peek(p)->span;
    stmt.unsafe_depth = p->unsafe_depth;
    parse_lvalue(p, &stmt.instr);
    if (peek(p)->kind != TC_TOK_EQ) {
        parse_fail(p, TC_CE_SYNTAX, peek(p));
    }
    advance(p);
    if (peek(p)->kind == TC_TOK_NL || peek(p)->kind == TC_TOK_EOF) {
        parse_fail(p, TC_CE_SYNTAX, peek(p));
    }
    parse_op_form(p, &stmt.instr);
    expect_nl(p);
    stmt.end_span = p->tokens[p->pos - 1u].span;
    return stmt;
}

static void parse_unsafe_statement(TcParser *p, TcVec *body) {
    TcToken *keyword = expect(p, TC_TOK_UNSAFE);
    TcParseStmt begin;
    memset(&begin, 0, sizeof(begin));
    begin.kind = TC_STMT_UNSAFE_BEGIN;
    begin.span = keyword->span;
    begin.unsafe_depth = p->unsafe_depth;
    expect(p, TC_TOK_THEN);
    expect_nl(p);
    begin.end_span = p->tokens[p->pos - 1u].span;
    vec_push(p, body, &begin, sizeof(begin));
    push_block(p, TC_TOK_UNSAFE, keyword->span);
    p->unsafe_depth++;
    for (;;) {
        TcToken *token = peek(p);
        if (token->kind == TC_TOK_END) {
            break;
        }
        if (token->kind == TC_TOK_EOF) {
            fail_missing_end(p);
        }
        if (is_module_keyword(token->kind)) {
            fail_member_or_syntax(p, token);
        }
        parse_statement_into(p, body);
    }
    {
        TcToken *end = expect(p, TC_TOK_END);
        TcParseStmt finish;
        memset(&finish, 0, sizeof(finish));
        finish.kind = TC_STMT_UNSAFE_END;
        finish.span = end->span;
        p->unsafe_depth--;
        finish.unsafe_depth = p->unsafe_depth;
        expect_nl(p);
        finish.end_span = p->tokens[p->pos - 1u].span;
        pop_block(p);
        vec_push(p, body, &finish, sizeof(finish));
    }
}

static void parse_statement_into(TcParser *p, TcVec *body) {
    TcToken *token = peek(p);
    TcParseStmt stmt;
    switch (token->kind) {
    case TC_TOK_VAR:
        stmt = parse_var_statement(p);
        vec_push(p, body, &stmt, sizeof(stmt));
        return;
    case TC_TOK_CONST:
        stmt = parse_const_statement(p);
        vec_push(p, body, &stmt, sizeof(stmt));
        return;
    case TC_TOK_LABEL:
        stmt = parse_label_statement(p);
        vec_push(p, body, &stmt, sizeof(stmt));
        return;
    case TC_TOK_UNSAFE:
        parse_unsafe_statement(p, body);
        return;
    case TC_TOK_DISCARD:
    case TC_TOK_IDENT:
    case TC_TOK_SELF:
        if (token->kind == TC_TOK_SELF && p->is_program) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
        }
        stmt = parse_instruction_statement(p);
        vec_push(p, body, &stmt, sizeof(stmt));
        return;
    case TC_TOK_END:
        parse_fail(p, TC_CE_SYNTAX, token);
        return;
    case TC_TOK_EOF:
        fail_missing_end(p);
        return;
    default:
        break;
    }
    if (is_module_keyword(token->kind)) {
        if (p->blocks.count != 0) {
            fail_member_or_syntax(p, token);
        }
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    parse_fail(p, TC_CE_SYNTAX, token);
}

/* ------------------------------------------------------------------------- */
/* Module structure.                                                         */
/* ------------------------------------------------------------------------- */

static void parse_pattern(TcParser *p, TcParseModule *module) {
    TcToken *token = peek(p);
    if (token->kind == TC_TOK_PROGRAM) {
        module->is_program = 1;
    } else if (token->kind == TC_TOK_LIB) {
        module->is_program = 0;
    } else {
        parse_fail(p, TC_CE_SYNTAX, token);
    }
    module->pattern_span = token->span;
    p->is_program = module->is_program;
    advance(p);
    expect_nl(p);
}

static void parse_const_decl(TcParser *p, int is_static, int is_public,
                             int has_visibility, TcSpan visibility_span,
                             TcParseConstDecl *out) {
    TcToken *keyword;
    TcToken *name;
    memset(out, 0, sizeof(*out));
    if (is_static) {
        keyword = expect(p, TC_TOK_STATIC);
        expect(p, TC_TOK_VAR);
    } else {
        keyword = expect(p, TC_TOK_CONST);
    }
    out->span = keyword->span;
    out->is_static = is_static;
    out->is_public = is_public;
    out->has_visibility = has_visibility;
    out->visibility_span = visibility_span;
    name = expect_ident(p);
    out->name = name->text;
    out->name_span = name->span;
    expect(p, TC_TOK_COLON);
    out->type = *parse_type(p);
    if (peek(p)->kind != TC_TOK_EQ) {
        if (is_static) {
            parse_fail(p, TC_CE_VAR_MISSING_INIT, name);
        }
        parse_fail(p, TC_CE_SYNTAX, peek(p));
    }
    advance(p);
    if (peek(p)->kind == TC_TOK_NL || peek(p)->kind == TC_TOK_EOF) {
        if (is_static) {
            parse_fail(p, TC_CE_VAR_MISSING_INIT, name);
        }
        parse_fail(p, TC_CE_SYNTAX, peek(p));
    }
    out->init = parse_const_init(p);
    expect_nl(p);
}

static void parse_program(TcParser *p, TcVec *imports, TcVec *structs,
                          TcVec *body) {
    while (peek(p)->kind == TC_TOK_IMPORT) {
        TcParseImport import_decl = parse_import(p);
        vec_push(p, imports, &import_decl, sizeof(import_decl));
    }
    while (peek(p)->kind == TC_TOK_STRUCT) {
        TcParseStruct structure = parse_struct(p, 0, 0, peek(p)->span);
        vec_push(p, structs, &structure, sizeof(structure));
    }
    for (;;) {
        TcToken *token = peek(p);
        if (token->kind == TC_TOK_EOF) {
            break;
        }
        if (token->kind == TC_TOK_STRUCT || token->kind == TC_TOK_IMPORT) {
            parse_fail(p, TC_CE_MODULE_LAYER, token);
        }
        if (token->kind == TC_TOK_FUNC || token->kind == TC_TOK_STATIC ||
            token->kind == TC_TOK_PUBLIC || token->kind == TC_TOK_PRIVATE) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
        }
        if (token->kind == TC_TOK_SELF) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
        }
        if (token->kind == TC_TOK_PROGRAM || token->kind == TC_TOK_LIB) {
            parse_fail(p, TC_CE_SYNTAX, token);
        }
        parse_statement_into(p, body);
    }
}

static void parse_library(TcParser *p, TcVec *imports, TcVec *structs,
                          TcVec *consts, TcVec *funcs) {
    int layer = 0;
    while (peek(p)->kind == TC_TOK_IMPORT) {
        TcParseImport import_decl = parse_import(p);
        vec_push(p, imports, &import_decl, sizeof(import_decl));
        layer = 2;
    }
    for (;;) {
        TcToken *token = peek(p);
        if (token->kind == TC_TOK_EOF) {
            break;
        }
        if (token->kind == TC_TOK_IMPORT) {
            parse_fail(p, TC_CE_MODULE_LAYER, token);
        }
        if (token->kind == TC_TOK_STRUCT) {
            parse_fail(p, TC_CE_MISSING_VISIBILITY, token);
        }
        if (token->kind == TC_TOK_CONST || token->kind == TC_TOK_STATIC ||
            token->kind == TC_TOK_FUNC) {
            parse_fail(p, TC_CE_MISSING_VISIBILITY, token);
        }
        if (token->kind == TC_TOK_VAR) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
        }
        if (token->kind == TC_TOK_PROGRAM || token->kind == TC_TOK_LIB) {
            parse_fail(p, TC_CE_SYNTAX, token);
        }
        if (token->kind == TC_TOK_PUBLIC || token->kind == TC_TOK_PRIVATE) {
            TcToken *visibility = advance(p);
            int is_public = visibility->kind == TC_TOK_PUBLIC;
            TcToken *member = peek(p);
            if (member->kind == TC_TOK_STRUCT) {
                TcParseStruct structure;
                if (layer > 3) {
                    parse_fail(p, TC_CE_MODULE_LAYER, visibility);
                }
                structure = parse_struct(p, is_public, 1, visibility->span);
                vec_push(p, structs, &structure, sizeof(structure));
                layer = 3;
                continue;
            }
            if (member->kind == TC_TOK_CONST || member->kind == TC_TOK_STATIC) {
                TcParseConstDecl declaration;
                if (layer > 4) {
                    parse_fail(p, TC_CE_MODULE_LAYER, visibility);
                }
                parse_const_decl(p, member->kind == TC_TOK_STATIC, is_public, 1,
                                 visibility->span, &declaration);
                vec_push(p, consts, &declaration, sizeof(declaration));
                layer = 4;
                continue;
            }
            if (member->kind == TC_TOK_ANNOTATION ||
                member->kind == TC_TOK_FUNC) {
                TcVec annotations = {0};
                TcParseFunc function;
                if (layer > 5) {
                    parse_fail(p, TC_CE_MODULE_LAYER, visibility);
                }
                parse_annotations(p, &annotations);
                function = parse_func(p, is_public, 1, visibility->span,
                                      &annotations);
                vec_push(p, funcs, &function, sizeof(function));
                layer = 5;
                continue;
            }
            if (member->kind == TC_TOK_VAR) {
                parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, member);
            }
            if (member->kind == TC_TOK_PUBLIC || member->kind == TC_TOK_PRIVATE ||
                member->kind == TC_TOK_IMPORT) {
                parse_fail(p, TC_CE_SYNTAX, member);
            }
            parse_fail(p, TC_CE_SYNTAX, member);
        }
        if (token->kind == TC_TOK_ANNOTATION) {
            TcVec annotations = {0};
            parse_annotations(p, &annotations);
            if (peek(p)->kind == TC_TOK_FUNC) {
                parse_fail(p, TC_CE_MISSING_VISIBILITY, peek(p));
            }
            parse_fail(p, TC_CE_SYNTAX, peek(p));
        }
        if (token->kind == TC_TOK_LABEL || token->kind == TC_TOK_UNSAFE ||
            token->kind == TC_TOK_DISCARD || token->kind == TC_TOK_IDENT ||
            token->kind == TC_TOK_SELF || token->kind == TC_TOK_MNEMONIC) {
            parse_fail(p, TC_CE_PROGRAM_MODE_MISUSE, token);
        }
        parse_fail(p, TC_CE_SYNTAX, token);
    }
}

static void parse_module_body(TcParser *p, TcParseModule *module) {
    TcVec imports = {0};
    TcVec structs = {0};
    TcVec consts = {0};
    TcVec funcs = {0};
    TcVec body = {0};

    parse_pattern(p, module);
    if (module->is_program) {
        parse_program(p, &imports, &structs, &body);
    } else {
        parse_library(p, &imports, &structs, &consts, &funcs);
    }
    if (peek(p)->kind != TC_TOK_EOF) {
        parse_fail(p, TC_CE_SYNTAX, peek(p));
    }
    module->imports = (TcParseImport *)imports.items;
    module->nimports = imports.count;
    module->structs = (TcParseStruct *)structs.items;
    module->nstructs = structs.count;
    module->consts = (TcParseConstDecl *)consts.items;
    module->nconsts = consts.count;
    module->funcs = (TcParseFunc *)funcs.items;
    module->nfuncs = funcs.count;
    module->body = (TcParseStmt *)body.items;
    module->nbody = body.count;
}

/* ------------------------------------------------------------------------- */
/* Public API.                                                               */
/* ------------------------------------------------------------------------- */

const char *tc_parse_type_name(const TcParseType *type) {
    if (type == NULL) {
        return NULL;
    }
    if (type->kind == TC_PTYPE_NAME) {
        return type->member != NULL ? type->member : type->base;
    }
    return NULL;
}

const char *tc_parse_value_name(const TcParseValue *value) {
    if (value == NULL || value->kind != TC_PVAL_NAME) {
        return NULL;
    }
    return value->base;
}

void tc_parse_module_destroy(TcParseModule *module) {
    if (module == NULL) {
        return;
    }
    tc_arena_destroy(&module->arena);
    tc_allocator_libc()->free(tc_allocator_libc()->ctx, module);
}

int tc_parse_source(const char *path, const uint8_t *bytes, size_t size,
                    TcParseModule **out_module, TcFrontendDiag *out_diag,
                    TcImplError *out_impl) {
    const TcAllocator *alloc = tc_allocator_libc();
    TcParseModule *module;
    TcLexResult lex;
    TcParser *parser;
    int failed;
    if (out_module == NULL || out_diag == NULL || out_impl == NULL ||
        path == NULL) {
        return -1;
    }
    memset(out_diag, 0, sizeof(*out_diag));
    *out_module = NULL;
    *out_impl = TC_IMPL_OK;

    module = (TcParseModule *)alloc->alloc(alloc->ctx, sizeof(*module));
    if (module == NULL) {
        *out_impl = TC_IMPL_OOM;
        return -1;
    }
    memset(module, 0, sizeof(*module));
    tc_arena_init(&module->arena, alloc);
    module->path = tc_arena_strdup(&module->arena, path);
    if (module->path == NULL) {
        *out_impl = TC_IMPL_OOM;
        tc_parse_module_destroy(module);
        return -1;
    }
    if (tc_lex(module->path, bytes, size, &module->arena, &lex) != 0) {
        *out_impl = lex.impl != TC_IMPL_OK ? lex.impl : TC_IMPL_INTERNAL;
        tc_parse_module_destroy(module);
        return -1;
    }
    if (lex.has_diag) {
        *out_diag = lex.diag;
        out_diag->span.file = path;
        if (lex.impl != TC_IMPL_OK) {
            *out_impl = lex.impl;
            tc_parse_module_destroy(module);
            return -1;
        }
        tc_parse_module_destroy(module);
        return 0;
    }

    parser = (TcParser *)alloc->alloc(alloc->ctx, sizeof(*parser));
    if (parser == NULL) {
        *out_impl = TC_IMPL_OOM;
        tc_parse_module_destroy(module);
        return -1;
    }
    memset(parser, 0, sizeof(*parser));
    parser->arena = &module->arena;
    parser->module = module;
    parser->tokens = lex.tokens;
    parser->ntokens = lex.count;
    parser->pos = 0;
    parser->impl = TC_IMPL_OK;

    failed = setjmp(parser->env);
    if (failed) {
        TcImplError failure_impl = parser->impl;
        *out_impl = failure_impl;
        if (parser->has_diag) {
            *out_diag = parser->diag;
            out_diag->span.file = path;
            if (parser->diag.has_related) {
                out_diag->related.file = path;
            }
        }
        alloc->free(alloc->ctx, parser);
        tc_parse_module_destroy(module);
        return failure_impl == TC_IMPL_OK ? 0 : -1;
    }
    parse_module_body(parser, module);
    alloc->free(alloc->ctx, parser);
    *out_module = module;
    return 0;
}
