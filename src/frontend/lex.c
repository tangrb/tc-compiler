/* TC 0.0.46 LEX implementation (standard §2, §14.2). */
#include "frontend/lex.h"

#include <string.h>

#include "frontend/num.h"

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
    const char *file;
    uint32_t line;
    uint32_t column;
    int line_has_token;
    TcArena *arena;
    TcLexResult *out;
} TcLexer;

/* ------------------------------------------------------------------------- */
/* Token names (debugging / unit tests).                                     */
/* ------------------------------------------------------------------------- */

const char *tc_token_kind_name(TcTokenKind kind) {
    switch (kind) {
    case TC_TOK_EOF: return "EOF";
    case TC_TOK_NL: return "NL";
    case TC_TOK_IDENT: return "IDENT";
    case TC_TOK_INT: return "INT";
    case TC_TOK_FLOAT: return "FLOAT";
    case TC_TOK_INF: return "INF";
    case TC_TOK_DISCARD: return "DISCARD";
    case TC_TOK_ANNOTATION: return "ANNOTATION";
    case TC_TOK_MNEMONIC: return "MNEMONIC";
    case TC_TOK_PROGRAM: return "#program";
    case TC_TOK_LIB: return "#lib";
    case TC_TOK_VAR: return "var";
    case TC_TOK_CONST: return "const";
    case TC_TOK_STRUCT: return "struct";
    case TC_TOK_FUNC: return "func";
    case TC_TOK_IMPORT: return "import";
    case TC_TOK_PUBLIC: return "public";
    case TC_TOK_PRIVATE: return "private";
    case TC_TOK_READONLY: return "readonly";
    case TC_TOK_STATIC: return "static";
    case TC_TOK_LABEL: return "label";
    case TC_TOK_THEN: return "then";
    case TC_TOK_END: return "end";
    case TC_TOK_UNSAFE: return "unsafe";
    case TC_TOK_VOID: return "void";
    case TC_TOK_BOOL: return "bool";
    case TC_TOK_INT8: return "int8";
    case TC_TOK_INT16: return "int16";
    case TC_TOK_INT32: return "int32";
    case TC_TOK_INT64: return "int64";
    case TC_TOK_UINT8: return "uint8";
    case TC_TOK_UINT16: return "uint16";
    case TC_TOK_UINT32: return "uint32";
    case TC_TOK_UINT64: return "uint64";
    case TC_TOK_ISIZE: return "isize";
    case TC_TOK_USIZE: return "usize";
    case TC_TOK_FLOAT32: return "float32";
    case TC_TOK_FLOAT64: return "float64";
    case TC_TOK_PTR: return "ptr";
    case TC_TOK_FNPTR: return "fnptr";
    case TC_TOK_TRUE: return "true";
    case TC_TOK_FALSE: return "false";
    case TC_TOK_NULLPTR: return "nullptr";
    case TC_TOK_INF_KW: return "inf";
    case TC_TOK_NAN: return "nan";
    case TC_TOK_SELF: return "Self";
    case TC_TOK_EQ: return "=";
    case TC_TOK_COLON: return ":";
    case TC_TOK_COMMA: return ",";
    case TC_TOK_LPAREN: return "(";
    case TC_TOK_RPAREN: return ")";
    case TC_TOK_LT: return "<";
    case TC_TOK_GT: return ">";
    case TC_TOK_DOT: return ".";
    case TC_TOK_COUNT: return "<count>";
    default: return "<unknown>";
    }
}

/* ------------------------------------------------------------------------- */
/* Keyword and mnemonic tables.                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *text;
    TcTokenKind kind;
} TcKeywordEntry;

static const TcKeywordEntry tc_keywords[] = {
    {"var", TC_TOK_VAR},
    {"const", TC_TOK_CONST},
    {"struct", TC_TOK_STRUCT},
    {"func", TC_TOK_FUNC},
    {"import", TC_TOK_IMPORT},
    {"public", TC_TOK_PUBLIC},
    {"private", TC_TOK_PRIVATE},
    {"readonly", TC_TOK_READONLY},
    {"static", TC_TOK_STATIC},
    {"label", TC_TOK_LABEL},
    {"then", TC_TOK_THEN},
    {"end", TC_TOK_END},
    {"unsafe", TC_TOK_UNSAFE},
    {"void", TC_TOK_VOID},
    {"bool", TC_TOK_BOOL},
    {"int8", TC_TOK_INT8},
    {"int16", TC_TOK_INT16},
    {"int32", TC_TOK_INT32},
    {"int64", TC_TOK_INT64},
    {"uint8", TC_TOK_UINT8},
    {"uint16", TC_TOK_UINT16},
    {"uint32", TC_TOK_UINT32},
    {"uint64", TC_TOK_UINT64},
    {"isize", TC_TOK_ISIZE},
    {"usize", TC_TOK_USIZE},
    {"float32", TC_TOK_FLOAT32},
    {"float64", TC_TOK_FLOAT64},
    {"ptr", TC_TOK_PTR},
    {"fnptr", TC_TOK_FNPTR},
    {"true", TC_TOK_TRUE},
    {"false", TC_TOK_FALSE},
    {"nullptr", TC_TOK_NULLPTR},
    {"inf", TC_TOK_INF_KW},
    {"nan", TC_TOK_NAN},
    {"Self", TC_TOK_SELF},
};

typedef struct {
    const char *text;
    TcOpcode op;
} TcMnemonicEntry;

static const TcMnemonicEntry tc_mnemonics[] = {
    {"add", TC_ADD},
    {"sub", TC_SUB},
    {"mul", TC_MUL},
    {"div", TC_DIV},
    {"mod", TC_MOD},
    {"neg", TC_NEG},
    {"abs", TC_ABS},
    {"shl", TC_SHL},
    {"shr", TC_SHR},
    {"and", TC_AND},
    {"or", TC_OR},
    {"xor", TC_XOR},
    {"not", TC_NOT},
    {"eq", TC_EQ},
    {"ne", TC_NE},
    {"lt", TC_LT},
    {"le", TC_LE},
    {"gt", TC_GT},
    {"ge", TC_GE},
    {"cast", TC_CAST},
    {"trunc", TC_TRUNC},
    {"bitcast", TC_BITCAST},
    {"copy", TC_COPY},
    {"load", TC_LOAD},
    {"store", TC_STORE},
    {"memmove", TC_MEMMOVE},
    {"addr", TC_ADDR},
    {"ptradd", TC_PTRADD},
    {"ptrsub", TC_PTRSUB},
    {"ptrdiff", TC_PTRDIFF},
    {"ptreq", TC_PTREQ},
    {"ptrne", TC_PTRNE},
    {"ptrlt", TC_PTRLT},
    {"ptrle", TC_PTRLE},
    {"ptrgt", TC_PTRGT},
    {"ptrge", TC_PTRGE},
    {"funcaddr", TC_FUNCADDR},
    {"funceq", TC_FUNCEQ},
    {"funcne", TC_FUNCNE},
    {"call", TC_CALL},
    {"call_indirect", TC_CALL_INDIRECT},
    {"make", TC_MAKE},
    {"sizeof", TC_SIZEOF},
    {"br", TC_BR},
    {"br_if", TC_BR_IF},
    {"switch", TC_SWITCH},
    {"return", TC_RETURN},
    {"trap", TC_TRAP},
    {"read", TC_READ},
    {"write", TC_WRITE},
    {"writeln", TC_WRITELN},
};

typedef struct {
    const char *text;
    TcOpMode mode;
} TcSuffixEntry;

static const TcSuffixEntry tc_suffixes[] = {
    {"wrap", TC_WRAP},
    {"ieee", TC_IEEE},
    {"volatile", TC_VOLATILE},
    {"nsw", TC_NSW},
    {"nuw", TC_NUW},
};

static int word_equals(const uint8_t *bytes, size_t start, size_t len,
                       const char *word) {
    size_t word_len = strlen(word);
    return len == word_len && memcmp(bytes + start, word, word_len) == 0;
}

static TcTokenKind keyword_kind(const uint8_t *bytes, size_t start, size_t len) {
    size_t i;
    for (i = 0; i < sizeof(tc_keywords) / sizeof(tc_keywords[0]); i++) {
        if (word_equals(bytes, start, len, tc_keywords[i].text)) {
            return tc_keywords[i].kind;
        }
    }
    return TC_TOK_COUNT;
}

static int mnemonic_lookup(const uint8_t *bytes, size_t start, size_t len,
                           TcOpcode *op) {
    size_t i;
    for (i = 0; i < sizeof(tc_mnemonics) / sizeof(tc_mnemonics[0]); i++) {
        if (word_equals(bytes, start, len, tc_mnemonics[i].text)) {
            *op = tc_mnemonics[i].op;
            return 1;
        }
    }
    return 0;
}

static int suffix_lookup(const uint8_t *bytes, size_t start, size_t len,
                         TcOpMode *mode) {
    size_t i;
    for (i = 0; i < sizeof(tc_suffixes) / sizeof(tc_suffixes[0]); i++) {
        if (word_equals(bytes, start, len, tc_suffixes[i].text)) {
            *mode = tc_suffixes[i].mode;
            return 1;
        }
    }
    return 0;
}

static int suffix_combo_valid(TcOpcode op, TcOpMode mode) {
    switch (mode) {
    case TC_WRAP:
        return op == TC_ADD || op == TC_SUB || op == TC_MUL || op == TC_NEG ||
               op == TC_SHL;
    case TC_IEEE:
        return op == TC_ADD || op == TC_SUB || op == TC_MUL || op == TC_DIV ||
               op == TC_MOD;
    case TC_VOLATILE:
        return op == TC_LOAD || op == TC_STORE;
    case TC_NSW:
    case TC_NUW:
        return op == TC_ADD || op == TC_SUB || op == TC_MUL || op == TC_SHL;
    case TC_STRICT:
    default:
        return 1;
    }
}

/* ------------------------------------------------------------------------- */
/* Character helpers.                                                        */
/* ------------------------------------------------------------------------- */

static int is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

static int is_letter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_ident_char(uint8_t c) {
    return is_letter(c) || is_digit(c) || c == '_';
}

static int is_ident_start(uint8_t c) {
    return is_letter(c);
}

/* ------------------------------------------------------------------------- */
/* Decoding and diagnostics.                                                 */
/* ------------------------------------------------------------------------- */

typedef enum {
    TC_DECODE_OK = 0,
    TC_DECODE_INVALID,
    TC_DECODE_TRUNCATED
} TcDecodeStatus;

/* Decode one UTF-8 scalar at offset.  On failure *prefix_bytes is the number
 * of bytes at `offset` that belong to the longest valid prefix of the
 * sequence. */
static TcDecodeStatus decode_scalar(const uint8_t *bytes, size_t size,
                                    size_t offset, uint32_t *scalar,
                                    size_t *length, size_t *prefix_bytes) {
    uint8_t lead;
    size_t need;
    size_t i;
    uint32_t value;
    *prefix_bytes = 0;
    *length = 0;
    if (offset >= size) {
        return TC_DECODE_TRUNCATED;
    }
    lead = bytes[offset];
    if (lead < 0x80u) {
        *scalar = lead;
        *length = 1;
        return TC_DECODE_OK;
    }
    if (lead >= 0xC2u && lead <= 0xDFu) {
        need = 1;
        value = lead & 0x1Fu;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
        need = 2;
        value = lead & 0x0Fu;
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
        need = 3;
        value = lead & 0x07u;
    } else {
        return TC_DECODE_INVALID;
    }
    for (i = 1; i <= need; i++) {
        uint8_t cont;
        if (offset + i >= size) {
            *prefix_bytes = i;
            return TC_DECODE_TRUNCATED;
        }
        cont = bytes[offset + i];
        if (cont < 0x80u || cont > 0xBFu) {
            *prefix_bytes = i;
            return TC_DECODE_INVALID;
        }
        if (i == 1) {
            if (lead == 0xE0u && (cont < 0xA0u || cont > 0xBFu)) {
                *prefix_bytes = 1u;
                return TC_DECODE_INVALID;
            }
            if (lead == 0xEDu && (cont < 0x80u || cont > 0x9Fu)) {
                *prefix_bytes = 1u;
                return TC_DECODE_INVALID;
            }
            if (lead == 0xF0u && (cont < 0x90u || cont > 0xBFu)) {
                *prefix_bytes = 1u;
                return TC_DECODE_INVALID;
            }
            if (lead == 0xF4u && (cont < 0x80u || cont > 0x8Fu)) {
                *prefix_bytes = 1u;
                return TC_DECODE_INVALID;
            }
        }
        value = (value << 6) | (uint32_t)(cont & 0x3Fu);
    }
    *scalar = value;
    *length = need + 1u;
    return TC_DECODE_OK;
}

static void lex_impl_fail(TcLexer *lx, TcImplError error) {
    lx->out->impl = error;
    lx->out->has_diag = 1;
    lx->out->diag.code = TC_CE_SYNTAX;
    lx->out->diag.stage = TC_STAGE_LEX;
    lx->out->diag.span.file = lx->file;
    lx->out->diag.span.line = lx->line;
    lx->out->diag.span.column = lx->column;
    lx->out->diag.span.valid_prefix_bytes = 0;
    lx->out->diag.span.invalid_byte_offset = TC_SPAN_NO_INVALID_BYTE;
}

static void lex_fail(TcLexer *lx, TcDiagCode code, uint32_t line,
                     uint32_t column) {
    lx->out->impl = TC_IMPL_OK;
    lx->out->has_diag = 1;
    lx->out->diag.code = code;
    lx->out->diag.stage = TC_STAGE_LEX;
    lx->out->diag.span.file = lx->file;
    lx->out->diag.span.line = line;
    lx->out->diag.span.column = column;
    lx->out->diag.span.valid_prefix_bytes = 0;
    lx->out->diag.span.invalid_byte_offset = TC_SPAN_NO_INVALID_BYTE;
    lx->out->diag.has_related = 0;
}

static void lex_fail_utf8(TcLexer *lx, uint32_t line, uint32_t column,
                          uint32_t valid_prefix, uint32_t invalid_offset) {
    lex_fail(lx, TC_CE_SYNTAX, line, column);
    lx->out->diag.span.valid_prefix_bytes = valid_prefix;
    lx->out->diag.span.invalid_byte_offset = invalid_offset;
}

static int push_token(TcLexer *lx, const TcToken *token) {
    TcLexResult *out = lx->out;
    if (out->count == out->capacity) {
        size_t capacity = out->capacity == 0 ? 128u : out->capacity * 2u;
        TcToken *fresh =
            (TcToken *)tc_arena_alloc(lx->arena, capacity * sizeof(TcToken));
        if (fresh == NULL) {
            lex_impl_fail(lx, TC_IMPL_OOM);
            return -1;
        }
        if (out->count != 0) {
            memcpy(fresh, out->tokens, out->count * sizeof(TcToken));
        }
        out->tokens = fresh;
        out->capacity = capacity;
    }
    out->tokens[out->count++] = *token;
    lx->line_has_token = 1;
    return 0;
}

static TcToken token_at(TcLexer *lx, TcTokenKind kind, uint32_t line,
                        uint32_t column) {
    TcToken token;
    memset(&token, 0, sizeof(token));
    token.kind = kind;
    token.span.file = lx->file;
    token.span.line = line;
    token.span.column = column;
    token.span.valid_prefix_bytes = 0;
    token.span.invalid_byte_offset = TC_SPAN_NO_INVALID_BYTE;
    return token;
}

static void advance_ascii(TcLexer *lx) {
    lx->offset++;
    lx->column++;
}

static void advance_scalar(TcLexer *lx, size_t length) {
    lx->offset += length;
    lx->column++;
}

static void consume_line_terminator(TcLexer *lx) {
    const uint8_t *bytes = lx->bytes;
    if (bytes[lx->offset] == '\r' && lx->offset + 1u < lx->size &&
        bytes[lx->offset + 1u] == '\n') {
        lx->offset += 2u;
    } else {
        lx->offset += 1u;
    }
    lx->line++;
    lx->column = 1u;
}

/* ------------------------------------------------------------------------- */
/* Comments, continuations, patterns, annotations.                           */
/* ------------------------------------------------------------------------- */

static int lex_comment(TcLexer *lx) {
    /* At '/' with "///".  Consume the three slashes and then every decoded
     * scalar until a line terminator; Unicode is legal in comments, U+0000 is
     * not. */
    lx->offset += 3u;
    lx->column += 3u;
    while (lx->offset < lx->size) {
        uint8_t c = lx->bytes[lx->offset];
        if (c == '\n' || c == '\r') {
            return 0;
        }
        if (c < 0x80u) {
            if (c == 0u) {
                lex_fail(lx, TC_CE_SYNTAX, lx->line, lx->column);
                return -1;
            }
            advance_ascii(lx);
        } else {
            uint32_t scalar = 0;
            size_t length = 0;
            size_t prefix = 0;
            TcDecodeStatus status = decode_scalar(lx->bytes, lx->size, lx->offset,
                                                  &scalar, &length, &prefix);
            if (status != TC_DECODE_OK) {
                uint32_t invalid_offset = (uint32_t)(lx->offset + prefix);
                lex_fail_utf8(lx, lx->line, lx->column + (uint32_t)prefix,
                              invalid_offset, invalid_offset);
                return -1;
            }
            if (scalar == 0u) {
                lex_fail(lx, TC_CE_SYNTAX, lx->line, lx->column);
                return -1;
            }
            advance_scalar(lx, length);
        }
    }
    return 0;
}

static int lex_continuation(TcLexer *lx) {
    /* At '\\'.  Only a backslash immediately followed by a line terminator is
     * a valid continuation; anything else (including whitespace before the
     * terminator, a trailing comment, or EOF) is TC_CE_SYNTAX at the '\'. */
    const uint8_t *bytes = lx->bytes;
    if (lx->offset + 1u < lx->size) {
        uint8_t next = bytes[lx->offset + 1u];
        if (next == '\n' || next == '\r') {
            lx->offset += 1u; /* the backslash */
            consume_line_terminator(lx);
            return 0;
        }
    }
    lex_fail(lx, TC_CE_SYNTAX, lx->line, lx->column);
    return -1;
}

static int lex_pattern(TcLexer *lx) {
    size_t start = lx->offset;
    uint32_t line = lx->line;
    uint32_t column = lx->column;
    TcToken token;
    size_t word_start = start + 1u;
    size_t word_len;
    uint8_t after;
    if (lx->size - word_start >= 7u &&
        memcmp(lx->bytes + word_start, "program", 7u) == 0) {
        word_len = 7u;
        token = token_at(lx, TC_TOK_PROGRAM, line, column);
    } else if (lx->size - word_start >= 3u &&
               memcmp(lx->bytes + word_start, "lib", 3u) == 0) {
        word_len = 3u;
        token = token_at(lx, TC_TOK_LIB, line, column);
    } else {
        lex_fail(lx, TC_CE_SYNTAX, line, column);
        return -1;
    }
    if (word_start + word_len < lx->size) {
        after = lx->bytes[word_start + word_len];
        if (is_ident_char(after)) {
            lex_fail(lx, TC_CE_SYNTAX, line, column);
            return -1;
        }
    }
    lx->offset = word_start + word_len;
    lx->column = column + (uint32_t)(1u + word_len);
    return push_token(lx, &token);
}

static int lex_annotation(TcLexer *lx) {
    size_t start = lx->offset;
    uint32_t line = lx->line;
    uint32_t column = lx->column;
    size_t name_start;
    size_t name_len;
    TcToken token;
    char *name;
    if (start + 1u >= lx->size) {
        lex_fail(lx, TC_CE_SYNTAX, line, column);
        return -1;
    }
    name_start = start + 1u;
    if (is_ident_start(lx->bytes[name_start])) {
        name_len = 1u;
        while (name_start + name_len < lx->size &&
               is_ident_char(lx->bytes[name_start + name_len])) {
            name_len++;
        }
    } else if (lx->bytes[name_start] == '_' && name_start + 1u < lx->size &&
               is_ident_char(lx->bytes[name_start + 1u])) {
        name_len = 1u;
        while (name_start + name_len < lx->size &&
               is_ident_char(lx->bytes[name_start + name_len])) {
            name_len++;
        }
    } else {
        lex_fail(lx, TC_CE_SYNTAX, line, column);
        return -1;
    }
    if (name_len > TC_FRONTEND_MAX_TOKEN_BYTES) {
        lex_impl_fail(lx, TC_IMPL_INTERNAL);
        return -1;
    }
    {
        TcTokenKind keyword = keyword_kind(lx->bytes, name_start, name_len);
        if (keyword != TC_TOK_COUNT && keyword != TC_TOK_UNSAFE &&
            keyword != TC_TOK_READONLY) {
            lex_fail(lx, TC_CE_SYNTAX, line, column);
            return -1;
        }
        if (keyword == TC_TOK_UNSAFE || keyword == TC_TOK_READONLY) {
            /* allowed annotation names */
        } else if (keyword != TC_TOK_COUNT) {
            lex_fail(lx, TC_CE_SYNTAX, line, column);
            return -1;
        }
    }
    name = tc_arena_strndup(lx->arena, (const char *)lx->bytes + name_start,
                            name_len);
    if (name == NULL) {
        lex_impl_fail(lx, TC_IMPL_OOM);
        return -1;
    }
    token = token_at(lx, TC_TOK_ANNOTATION, line, column);
    token.text = name;
    token.text_len = name_len;
    lx->offset = name_start + name_len;
    lx->column = column + (uint32_t)(1u + name_len);
    return push_token(lx, &token);
}

/* ------------------------------------------------------------------------- */
/* Numbers.                                                                  */
/* ------------------------------------------------------------------------- */

static int failure_exit(TcLexer *lx) {
    return lx->out->impl == TC_IMPL_OK ? 0 : -1;
}

static int fail_at_token_start(TcLexer *lx, uint32_t line, uint32_t column) {
    lex_fail(lx, TC_CE_SYNTAX, line, column);
    return -1;
}

static int lex_range_fail(TcLexer *lx, uint32_t line, uint32_t column) {
    lex_fail(lx, TC_CE_LITERAL_LEX_RANGE, line, column);
    return -1;
}

static int scan_digits_with_underscores(TcLexer *lx, int base, int *ok,
                                        uint64_t *magnitude, int *overflow) {
    size_t start = lx->offset;
    int saw_digit = 0;
    int last_underscore = 0;
    *magnitude = 0;
    *overflow = 0;
    while (lx->offset < lx->size) {
        uint8_t c = lx->bytes[lx->offset];
        int value = -1;
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        }
        if (value >= 0) {
            if (value >= base) {
                break;
            }
            saw_digit = 1;
            last_underscore = 0;
            if (!*overflow) {
                uint64_t next = *magnitude * (uint64_t)base + (uint64_t)value;
                if (*magnitude > (UINT64_MAX - (uint64_t)value) / (uint64_t)base) {
                    *overflow = 1;
                } else {
                    *magnitude = next;
                }
            }
            advance_ascii(lx);
        } else if (c == '_') {
            if (!saw_digit || last_underscore) {
                *ok = 0;
                return -1;
            }
            last_underscore = 1;
            advance_ascii(lx);
        } else {
            break;
        }
    }
    if (!saw_digit || last_underscore) {
        *ok = 0;
        return -1;
    }
    *ok = 1;
    (void)start;
    return 0;
}

static int candidate_continues(TcLexer *lx) {
    if (lx->offset >= lx->size) {
        return 0;
    }
    {
        uint8_t c = lx->bytes[lx->offset];
        return is_ident_char(c) || c == '.';
    }
}

static int parse_exponent_value(TcLexer *lx, int *negative) {
    int value = 0;
    int digits = 0;
    if (lx->offset < lx->size &&
        (lx->bytes[lx->offset] == '+' || lx->bytes[lx->offset] == '-')) {
        *negative = lx->bytes[lx->offset] == '-';
        advance_ascii(lx);
    }
    while (lx->offset < lx->size && is_digit(lx->bytes[lx->offset])) {
        if (value < 1000000) {
            value = value * 10 + (int)(lx->bytes[lx->offset] - '0');
        }
        digits++;
        advance_ascii(lx);
    }
    if (digits == 0) {
        return -1;
    }
    if (lx->offset < lx->size && lx->bytes[lx->offset] == '_') {
        return -1;
    }
    return value;
}

static int lex_number(TcLexer *lx) {
    size_t start = lx->offset;
    uint32_t line = lx->line;
    uint32_t column = lx->column;
    int negative = 0;
    uint8_t c;
    TcToken token;

    if (lx->bytes[lx->offset] == '-') {
        /* Only -<digits> or -inf can start a literal. */
        if (lx->offset + 1u >= lx->size) {
            return fail_at_token_start(lx, line, column);
        }
        if (lx->bytes[lx->offset + 1u] == 'i' &&
            lx->size - (lx->offset + 1u) >= 3u &&
            memcmp(lx->bytes + lx->offset + 1u, "inf", 3u) == 0) {
            size_t after = lx->offset + 4u;
            if (after < lx->size && is_ident_char(lx->bytes[after])) {
                return fail_at_token_start(lx, line, column);
            }
            token = token_at(lx, TC_TOK_INF, line, column);
            token.u.inf.negative = 1;
            lx->offset = after;
            lx->column = column + 4u;
            return push_token(lx, &token);
        }
        if (lx->bytes[lx->offset + 1u] == 'n' &&
            lx->size - (lx->offset + 1u) >= 3u &&
            memcmp(lx->bytes + lx->offset + 1u, "nan", 3u) == 0) {
            /* -nan is explicitly rejected (standard §2.4/§2.7.1). */
            return fail_at_token_start(lx, line, column);
        }
        if (!is_digit(lx->bytes[lx->offset + 1u])) {
            return fail_at_token_start(lx, line, column);
        }
        negative = 1;
        advance_ascii(lx);
    }

    c = lx->bytes[lx->offset];
    if (c == '0' && lx->offset + 1u < lx->size) {
        uint8_t base_char = lx->bytes[lx->offset + 1u];
        if (base_char == 'x' || base_char == 'X' || base_char == 'b' ||
            base_char == 'B' || base_char == 'o' || base_char == 'O') {
            int base = (base_char == 'x' || base_char == 'X') ? 16
                     : (base_char == 'b' || base_char == 'B') ? 2
                                                              : 8;
            int ok = 0;
            int overflow = 0;
            uint64_t magnitude = 0;
            int has_u = 0;
            advance_ascii(lx);
            advance_ascii(lx);
            if (scan_digits_with_underscores(lx, base, &ok, &magnitude,
                                             &overflow) != 0 ||
                !ok) {
                return fail_at_token_start(lx, line, column);
            }
            if (lx->offset < lx->size &&
                (lx->bytes[lx->offset] == 'u' || lx->bytes[lx->offset] == 'U')) {
                if (negative) {
                    return fail_at_token_start(lx, line, column);
                }
                has_u = 1;
                advance_ascii(lx);
            }
            if (candidate_continues(lx)) {
                return fail_at_token_start(lx, line, column);
            }
            if (overflow) {
                return lex_range_fail(lx, line, column);
            }
            token = token_at(lx, TC_TOK_INT, line, column);
            token.u.integer.negative = negative;
            token.u.integer.magnitude = magnitude;
            token.u.integer.has_unsigned_suffix = has_u;
            (void)start;
            return push_token(lx, &token);
        }
    }

    /* Decimal integer or float. */
    {
        size_t int_start;
        size_t int_len;
        size_t frac_start = 0;
        size_t frac_len = 0;
        int has_dot = 0;
        int has_exp = 0;
        int exp_negative = 0;
        int exp_value = 0;
        int has_f_suffix = 0;
        int has_u = 0;
        int is_float;
        int i;
        uint64_t magnitude = 0;
        int overflow = 0;
        int int_had_underscore = 0;
        size_t int_digits = 0;

        int_start = lx->offset;
        while (lx->offset < lx->size && is_digit(lx->bytes[lx->offset])) {
            uint64_t digit = (uint64_t)(lx->bytes[lx->offset] - '0');
            if (!overflow) {
                if (magnitude > (UINT64_MAX - digit) / 10u) {
                    overflow = 1;
                } else {
                    magnitude = magnitude * 10u + digit;
                }
            }
            advance_ascii(lx);
        }
        int_len = lx->offset - int_start;
        int_digits = int_len;
        if (int_len == 0) {
            return fail_at_token_start(lx, line, column);
        }
        if (lx->offset < lx->size && lx->bytes[lx->offset] == '_') {
            /* Integer underscores are validated below for the integer case;
             * a float can never contain one. */
        }
        /* Consume integer-part underscores (integer token candidate). */
        {
            size_t scan = lx->offset;
            while (scan < lx->size &&
                   (is_digit(lx->bytes[scan]) || lx->bytes[scan] == '_')) {
                scan++;
            }
            if (scan > lx->offset) {
                /* Validate underscore placement in the integer part. */
                size_t j;
                int prev_digit = 1;
                for (j = lx->offset; j < scan; j++) {
                    if (lx->bytes[j] == '_') {
                        if (!prev_digit || j + 1u >= scan ||
                            !is_digit(lx->bytes[j + 1u])) {
                            return fail_at_token_start(lx, line, column);
                        }
                        int_had_underscore = 1;
                        prev_digit = 0;
                    } else {
                        int_digits++;
                        prev_digit = 1;
                        if (!overflow) {
                            uint64_t digit =
                                (uint64_t)(lx->bytes[j] - '0');
                            if (magnitude >
                                (UINT64_MAX - digit) / 10u) {
                                overflow = 1;
                            } else {
                                magnitude = magnitude * 10u + digit;
                            }
                        }
                    }
                }
                lx->column += (uint32_t)(scan - lx->offset);
                lx->offset = scan;
            }
        }
        if (lx->offset < lx->size && lx->bytes[lx->offset] == '.') {
            has_dot = 1;
            advance_ascii(lx);
            frac_start = lx->offset;
            while (lx->offset < lx->size &&
                   is_digit(lx->bytes[lx->offset])) {
                advance_ascii(lx);
            }
            frac_len = lx->offset - frac_start;
            if (frac_len == 0) {
                return fail_at_token_start(lx, line, column);
            }
            if (lx->offset < lx->size && lx->bytes[lx->offset] == '_') {
                return fail_at_token_start(lx, line, column);
            }
        }
        if (lx->offset < lx->size &&
            (lx->bytes[lx->offset] == 'e' || lx->bytes[lx->offset] == 'E')) {
            has_exp = 1;
            advance_ascii(lx);
            exp_value = parse_exponent_value(lx, &exp_negative);
            if (exp_value < 0) {
                return fail_at_token_start(lx, line, column);
            }
        }
        is_float = has_dot || has_exp;
        if (is_float && int_had_underscore) {
            return fail_at_token_start(lx, line, column);
        }
        if (lx->offset < lx->size) {
            uint8_t suffix = lx->bytes[lx->offset];
            if (suffix == 'f' || suffix == 'F') {
                if (!is_float) {
                    return fail_at_token_start(lx, line, column);
                }
                has_f_suffix = 1;
                advance_ascii(lx);
            } else if (suffix == 'u' || suffix == 'U') {
                if (is_float || negative) {
                    return fail_at_token_start(lx, line, column);
                }
                has_u = 1;
                advance_ascii(lx);
            }
        }
        if (candidate_continues(lx)) {
            return fail_at_token_start(lx, line, column);
        }
        if (!is_float) {
            /* Decimal integers may not have a leading zero (except "0"). */
            if (int_digits > 1u && lx->bytes[int_start] == '0') {
                return fail_at_token_start(lx, line, column);
            }
            if (overflow) {
                return lex_range_fail(lx, line, column);
            }
            token = token_at(lx, TC_TOK_INT, line, column);
            token.u.integer.negative = negative;
            token.u.integer.magnitude = magnitude;
            token.u.integer.has_unsigned_suffix = has_u;
            return push_token(lx, &token);
        } else {
            /* Build the exact decimal digit string: integer part without
             * underscores followed by the fraction digits. */
            size_t digits_len = 0;
            if (lx->offset - start > TC_FRONTEND_MAX_TOKEN_BYTES) {
                /* Documented implementation limit: a pathological float token
                 * must not make the exact converter allocate unbounded
                 * bignums.  Integer tokens are still diagnosed by the lexical
                 * range rule instead. */
                lex_impl_fail(lx, TC_IMPL_INTERNAL);
                return -1;
            }
            char *digits;
            size_t out_index = 0;
            size_t j;
            int exp10;
            uint64_t bits = 0;
            TcFloatRange range = TC_FLOAT_OK;
            unsigned width = has_f_suffix ? 32u : 64u;
            for (j = 0; j < int_len; j++) {
                uint8_t ch = lx->bytes[int_start + j];
                if (is_digit(ch)) {
                    digits_len++;
                }
            }
            digits_len += frac_len;
            digits = (char *)tc_arena_alloc(lx->arena, digits_len + 1u);
            if (digits == NULL) {
                lex_impl_fail(lx, TC_IMPL_OOM);
                return -1;
            }
            for (j = 0; j < int_len; j++) {
                uint8_t ch = lx->bytes[int_start + j];
                if (is_digit(ch)) {
                    digits[out_index++] = (char)ch;
                }
            }
            for (j = 0; j < frac_len; j++) {
                digits[out_index++] =
                    (char)lx->bytes[frac_start + j];
            }
            digits[out_index] = '\0';
            exp10 = (exp_negative ? -exp_value : exp_value) - (int)frac_len;
            if (tc_decimal_to_float(digits, out_index, exp10, negative, width,
                                    &bits, &range) != 0) {
                lex_impl_fail(lx, TC_IMPL_OOM);
                return -1;
            }
            if (range != TC_FLOAT_OK) {
                return lex_range_fail(lx, line, column);
            }
            token = token_at(lx, TC_TOK_FLOAT, line, column);
            token.u.flt.negative = negative;
            token.u.flt.bits = bits;
            token.u.flt.width = width;
            (void)i;
            return push_token(lx, &token);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Identifiers, keywords and mnemonics.                                      */
/* ------------------------------------------------------------------------- */

static int lex_ident(TcLexer *lx) {
    size_t start = lx->offset;
    uint32_t line = lx->line;
    uint32_t column = lx->column;
    size_t len;
    TcToken token;
    TcTokenKind keyword;

    if (lx->bytes[start] == '_' && (start + 1u >= lx->size ||
                                    !is_ident_char(lx->bytes[start + 1u]))) {
        token = token_at(lx, TC_TOK_DISCARD, line, column);
        advance_ascii(lx);
        return push_token(lx, &token);
    }
    len = 1u;
    while (start + len < lx->size && is_ident_char(lx->bytes[start + len])) {
        len++;
    }
    if (len > TC_FRONTEND_MAX_TOKEN_BYTES) {
        lex_impl_fail(lx, TC_IMPL_INTERNAL);
        return -1;
    }
    keyword = keyword_kind(lx->bytes, start, len);
    if (keyword != TC_TOK_COUNT) {
        token = token_at(lx, keyword, line, column);
        lx->offset = start + len;
        lx->column = column + (uint32_t)len;
        return push_token(lx, &token);
    }
    {
        TcOpcode op;
        if (mnemonic_lookup(lx->bytes, start, len, &op)) {
            size_t after = start + len;
            token = token_at(lx, TC_TOK_MNEMONIC, line, column);
            token.u.mnemonic.op = op;
            token.u.mnemonic.mode = TC_STRICT;
            token.u.mnemonic.has_suffix = 0;
            token.u.mnemonic.combo_valid = 1;
            if (after < lx->size && lx->bytes[after] == '.' &&
                !(after + 1u < lx->size &&
                  (is_letter(lx->bytes[after + 1u]) ||
                   (lx->bytes[after + 1u] == '_' && after + 2u < lx->size &&
                    is_ident_char(lx->bytes[after + 2u]))))) {
                /* An immediate dot with no suffix word: the suffix is
                 * incomplete.  Keep it part of the mnemonic token so that
                 * §12.5.1's "unknown suffix at the mnemonic's first token"
                 * position rule applies. */
                token.u.mnemonic.has_suffix = 1;
                token.u.mnemonic.combo_valid = 0;
                after += 1u;
            } else if (after + 1u < lx->size && lx->bytes[after] == '.' &&
                       (is_letter(lx->bytes[after + 1u]) ||
                        (lx->bytes[after + 1u] == '_' &&
                         after + 2u < lx->size &&
                         is_ident_char(lx->bytes[after + 2u])))) {
                size_t suffix_start = after + 1u;
                size_t suffix_len = 1u;
                TcOpMode mode = TC_STRICT;
                while (suffix_start + suffix_len < lx->size &&
                       is_ident_char(lx->bytes[suffix_start + suffix_len])) {
                    suffix_len++;
                }
                token.u.mnemonic.has_suffix = 1;
                if (suffix_lookup(lx->bytes, suffix_start, suffix_len, &mode) &&
                    suffix_combo_valid(op, mode)) {
                    token.u.mnemonic.mode = mode;
                    token.u.mnemonic.combo_valid = 1;
                } else {
                    token.u.mnemonic.mode = TC_STRICT;
                    token.u.mnemonic.combo_valid = 0;
                }
                after = suffix_start + suffix_len;
            }
            lx->offset = after;
            lx->column = column + (uint32_t)(after - start);
            return push_token(lx, &token);
        }
    }
    {
        char *text = tc_arena_strndup(lx->arena, (const char *)lx->bytes + start,
                                      len);
        if (text == NULL) {
            lex_impl_fail(lx, TC_IMPL_OOM);
            return -1;
        }
        token = token_at(lx, TC_TOK_IDENT, line, column);
        token.text = text;
        token.text_len = len;
        lx->offset = start + len;
        lx->column = column + (uint32_t)len;
        return push_token(lx, &token);
    }
}

/* ------------------------------------------------------------------------- */
/* Main loop.                                                                */
/* ------------------------------------------------------------------------- */

static int lex_line_terminator(TcLexer *lx) {
    uint32_t line = lx->line;
    uint32_t column = lx->column;
    if (lx->line_has_token) {
        TcToken token = token_at(lx, TC_TOK_NL, line, column);
        if (push_token(lx, &token) != 0) {
            return -1;
        }
        lx->line_has_token = 1; /* push_token set it; reset below */
    }
    consume_line_terminator(lx);
    lx->line_has_token = 0;
    return 0;
}

int tc_lex(const char *file, const uint8_t *bytes, size_t size, TcArena *arena,
           TcLexResult *out) {
    TcLexer lx;
    if (out == NULL || arena == NULL || (bytes == NULL && size != 0)) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->impl = TC_IMPL_OK;
    memset(&lx, 0, sizeof(lx));
    lx.bytes = bytes;
    lx.size = size;
    lx.offset = 0;
    lx.file = file;
    lx.line = 1;
    lx.column = 1;
    lx.line_has_token = 0;
    lx.arena = arena;
    lx.out = out;

    if (tc_source_limit_violation(size) != NULL) {
        lex_impl_fail(&lx, TC_IMPL_INTERNAL);
        return -1;
    }

    while (lx.offset < lx.size) {
        uint8_t c = lx.bytes[lx.offset];
        if (c == '\n' || c == '\r') {
            if (lex_line_terminator(&lx) != 0) {
                return -1;
            }
            continue;
        }
        if (c == ' ' || c == '\t') {
            advance_ascii(&lx);
            continue;
        }
        if (c == '\\') {
            if (lex_continuation(&lx) != 0) {
                return failure_exit(&lx);
            }
            continue;
        }
        if (c == '/') {
            if (lx.offset + 2u < lx.size && lx.bytes[lx.offset + 1u] == '/' &&
                lx.bytes[lx.offset + 2u] == '/') {
                if (lex_comment(&lx) != 0) {
                    return failure_exit(&lx);
                }
                continue;
            }
            lex_fail(&lx, TC_CE_SYNTAX, lx.line, lx.column);
            return 0;
        }
        if (c == '#') {
            if (lex_pattern(&lx) != 0) {
                return failure_exit(&lx);
            }
            continue;
        }
        if (c == '@') {
            if (lex_annotation(&lx) != 0) {
                return failure_exit(&lx);
            }
            continue;
        }
        if (c == '_' || is_ident_start(c)) {
            if (lex_ident(&lx) != 0) {
                return failure_exit(&lx);
            }
            continue;
        }
        if (is_digit(c) || c == '-') {
            if (lex_number(&lx) != 0) {
                return failure_exit(&lx);
            }
            continue;
        }
        if (c == 0u) {
            lex_fail(&lx, TC_CE_SYNTAX, lx.line, lx.column);
            return 0;
        }
        switch (c) {
        case '=': {
            TcToken token = token_at(&lx, TC_TOK_EQ, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        case ':': {
            TcToken token = token_at(&lx, TC_TOK_COLON, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        case ',': {
            TcToken token = token_at(&lx, TC_TOK_COMMA, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        case '(': {
            TcToken token = token_at(&lx, TC_TOK_LPAREN, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        case ')': {
            TcToken token = token_at(&lx, TC_TOK_RPAREN, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        case '<': {
            TcToken token = token_at(&lx, TC_TOK_LT, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        case '>': {
            TcToken token = token_at(&lx, TC_TOK_GT, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        case '.': {
            TcToken token = token_at(&lx, TC_TOK_DOT, lx.line, lx.column);
            advance_ascii(&lx);
            if (push_token(&lx, &token) != 0) {
                return -1;
            }
            continue;
        }
        default:
            break;
        }
        if (c >= 0x80u) {
            uint32_t scalar = 0;
            size_t length = 0;
            size_t prefix = 0;
            TcDecodeStatus status = decode_scalar(lx.bytes, lx.size, lx.offset,
                                                  &scalar, &length, &prefix);
            if (status != TC_DECODE_OK) {
                uint32_t invalid_offset = (uint32_t)(lx.offset + prefix);
                lex_fail_utf8(&lx, lx.line, lx.column + (uint32_t)prefix,
                              invalid_offset, invalid_offset);
                return 0;
            }
            if (scalar == 0u) {
                lex_fail(&lx, TC_CE_SYNTAX, lx.line, lx.column);
                return 0;
            }
            lex_fail(&lx, TC_CE_SYNTAX, lx.line, lx.column);
            return 0;
        }
        lex_fail(&lx, TC_CE_SYNTAX, lx.line, lx.column);
        return 0;
    }

    {
        TcToken token = token_at(&lx, TC_TOK_EOF, lx.line, lx.column);
        if (push_token(&lx, &token) != 0) {
            return -1;
        }
    }
    out->impl = TC_IMPL_OK;
    return 0;
}
