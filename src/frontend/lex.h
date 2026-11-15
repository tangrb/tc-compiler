/* TC 0.0.46 LEX: character decoding and tokenization.
 *
 * Standard §2 and §14.2.  The lexer:
 *   - validates UTF-8, rejects BOM, U+0000 and non-ASCII outside comments,
 *   - implements the only comment form `///` and the logical-line
 *     continuation `\`,
 *   - performs longest-match tokenization of identifiers, keywords,
 *     instruction mnemonics (with `.wrap/.ieee/.volatile/.nsw/.nuw`),
 *     annotations, DISCARD and integer/floating literals,
 *   - reports only LEX codes (TC_CE_SYNTAX / TC_CE_LITERAL_LEX_RANGE) and
 *     stops at the first error.
 *
 * The lexer never reports SEM/CT codes.  Literal type/range checks relative to
 * an expected type are SEM (standard §2.7.3).
 */
#ifndef TC_FRONTEND_LEX_H
#define TC_FRONTEND_LEX_H

#include <stddef.h>
#include <stdint.h>

#include "frontend/diag.h"
#include "frontend/input.h"
#include "ir/instr.h"

typedef enum {
    TC_TOK_EOF = 0,
    TC_TOK_NL,
    TC_TOK_IDENT,
    TC_TOK_INT,
    TC_TOK_FLOAT,
    TC_TOK_INF,
    TC_TOK_DISCARD,
    TC_TOK_ANNOTATION,
    TC_TOK_MNEMONIC,

    /* pattern declarations */
    TC_TOK_PROGRAM,
    TC_TOK_LIB,

    /* declaration keywords */
    TC_TOK_VAR,
    TC_TOK_CONST,
    TC_TOK_STRUCT,
    TC_TOK_FUNC,
    TC_TOK_IMPORT,
    TC_TOK_PUBLIC,
    TC_TOK_PRIVATE,
    TC_TOK_READONLY,
    TC_TOK_STATIC,
    TC_TOK_LABEL,
    TC_TOK_THEN,
    TC_TOK_END,
    TC_TOK_UNSAFE,

    /* type keywords */
    TC_TOK_VOID,
    TC_TOK_BOOL,
    TC_TOK_INT8,
    TC_TOK_INT16,
    TC_TOK_INT32,
    TC_TOK_INT64,
    TC_TOK_UINT8,
    TC_TOK_UINT16,
    TC_TOK_UINT32,
    TC_TOK_UINT64,
    TC_TOK_ISIZE,
    TC_TOK_USIZE,
    TC_TOK_FLOAT32,
    TC_TOK_FLOAT64,
    TC_TOK_PTR,
    TC_TOK_FNPTR,

    /* literal keywords and namespaces */
    TC_TOK_TRUE,
    TC_TOK_FALSE,
    TC_TOK_NULLPTR,
    TC_TOK_INF_KW,
    TC_TOK_NAN,
    TC_TOK_SELF,

    /* punctuation */
    TC_TOK_EQ,
    TC_TOK_COLON,
    TC_TOK_COMMA,
    TC_TOK_LPAREN,
    TC_TOK_RPAREN,
    TC_TOK_LT,
    TC_TOK_GT,
    TC_TOK_DOT,

    TC_TOK_COUNT
} TcTokenKind;

typedef struct {
    int negative;
    uint64_t magnitude;
    int has_unsigned_suffix;
    unsigned radix; /* 10 unless the source used 0x/0b/0o */
} TcIntLiteral;

typedef struct {
    int negative;
    uint64_t bits;
    unsigned width; /* 32 or 64 */
} TcFloatLiteral;

typedef struct {
    TcTokenKind kind;
    TcSpan span;
    const char *text; /* IDENT/ANNOTATION name; NUL terminated, arena owned */
    size_t text_len;
    union {
        TcIntLiteral integer;
        TcFloatLiteral flt;
        struct {
            int negative;
        } inf;
        struct {
            TcOpcode op;
            TcOpMode mode;
            int has_suffix;
            int combo_valid;
            TcSpan suffix_span; /* suffix token; unused when has_suffix == 0 */
        } mnemonic;
    } u;
} TcToken;

typedef struct {
    TcToken *tokens;
    size_t count;
    size_t capacity;
    TcFrontendDiag diag;
    int has_diag;
    TcImplError impl; /* TC_IMPL_OK on success; OOM/INTERNAL otherwise */
} TcLexResult;

/* Tokenize source bytes for `file`.  All returned memory is arena owned.
 * Returns 0 on success (including the case where a language diagnostic was
 * produced; check has_diag), -1 on implementation failure (check impl).
 * `file` must remain valid while the result is used. */
int tc_lex(const char *file, const uint8_t *bytes, size_t size, TcArena *arena,
           TcLexResult *out);

const char *tc_token_kind_name(TcTokenKind kind);

#endif /* TC_FRONTEND_LEX_H */
