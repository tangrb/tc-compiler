/* TC 0.0.46 SYN: EBNF parser and flat parse IR.
 *
 * Standard §14 and §4.2/§4.11.3.  The parser directly builds the flat TAC
 * shape (one source line = one op, no nested expressions, no AST).  Source
 * names and type syntax are retained for SEM, because:
 *   - the opcode arity/type/security table is SEM work (standard §6.15.1),
 *   - names are not resolved before SEM (standard §5.6),
 *   - struct type identity is a SEM/IMPORT fact (standard §3.9, §4.11.2).
 * The frozen final IR (`TcInstr`/`TcIR`, src/ir) is produced by SEM.
 *
 * Field paths are kept as `TcFieldRef`-shaped name vectors; named arguments
 * keep their names; every construct keeps a 1-based line/column span and an
 * end position (standard §12.5.1).
 */
#ifndef TC_FRONTEND_PARSE_H
#define TC_FRONTEND_PARSE_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "frontend/diag.h"
#include "frontend/input.h"
#include "frontend/lex.h"
#include "ir/type.h"

typedef enum {
    TC_PTYPE_SCALAR = 0,
    TC_PTYPE_VOID,
    TC_PTYPE_PTR,
    TC_PTYPE_FNPTR,
    TC_PTYPE_NAME /* struct name: base [ "." member ] */
} TcParseTypeKind;

typedef struct TcParseType {
    TcParseTypeKind kind;
    TcTypeTag scalar;
    struct TcParseType *pointee;
    struct TcParseType *ret;
    struct TcParseType **params;
    size_t nparams;
    const char *base;
    const char *member;
    TcSpan span;        /* base token */
    TcSpan member_span; /* qualified member token, if member != NULL */
} TcParseType;

typedef enum {
    TC_PVAL_NAME = 0,
    TC_PVAL_INT,
    TC_PVAL_FLOAT,
    TC_PVAL_INF,
    TC_PVAL_BOOL,
    TC_PVAL_NULLPTR,
    TC_PVAL_NAN
} TcParseValueKind;

typedef struct {
    TcParseValueKind kind;
    TcSpan span;
    /* NAME: the frozen TcFieldRef shape, plus retained token spans. */
    const char *base;
    TcSpan base_span;
    const char **fields;
    TcSpan *field_spans;
    size_t nfields;
    /* INT */
    int int_negative;
    uint64_t int_magnitude;
    int int_unsigned_suffix;
    /* FLOAT */
    int float_negative;
    uint64_t float_bits;
    unsigned float_width;
    /* BOOL / INF */
    int bool_value;
    int inf_negative;
} TcParseValue;

typedef enum { TC_PARG_VALUE = 0, TC_PARG_TYPE } TcParseArgKind;

typedef struct {
    TcParseArgKind kind;
    const char *name; /* named argument/field; NULL for positional */
    TcSpan span;
    TcParseValue value;
    TcParseType type;
} TcParseArg;

typedef struct {
    TcOpcode op;
    TcOpMode mode;
    int has_suffix;
    TcSpan span;     /* mnemonic token, diagnostic anchor */
    TcSpan end_span; /* last token of the op_form */
    int dst_discard;
    const char *dst_base;
    TcSpan dst_base_span;
    const char **dst_fields;
    TcSpan *dst_field_spans;
    size_t dst_nfields;
    TcParseArg *args;
    size_t nargs;
    /* Explicit switch targets retained by the parser (standard §6.2.8/
     * design §3.2): targets 0..d-1 in source order, the last one is the
     * default.  Empty for non-switch instructions. */
    TcParseValue *switch_targets;
    size_t ntargets;
} TcParseInstr;

typedef struct {
    int is_op;
    TcParseInstr op;
    TcParseValue value;
    TcSpan span;
} TcParseConstInit;

typedef enum {
    TC_STMT_INSTR = 0,
    TC_STMT_VAR,
    TC_STMT_CONST,
    TC_STMT_LABEL,
    TC_STMT_UNSAFE_BEGIN,
    TC_STMT_UNSAFE_END
} TcStmtKind;

typedef struct {
    TcStmtKind kind;
    TcSpan span;
    TcSpan end_span;
    unsigned unsafe_depth;
    TcParseInstr instr;
    struct {
        const char *name;
        TcSpan name_span;
        TcParseType type;
        TcParseInstr init;
    } var;
    struct {
        const char *name;
        TcSpan name_span;
        TcParseType type;
        TcParseConstInit init;
    } cnst;
    struct {
        const char *name;
        TcSpan name_span;
    } label;
} TcParseStmt;

typedef struct {
    const char *name;
    TcSpan name_span;
    TcSpan span; /* import keyword */
} TcParseImport;

typedef struct {
    TcSpan span; /* annotation token */
    const char *name;
    TcParseValue *args;
    size_t nargs;
} TcParseAnnotation;

typedef struct {
    const char *name;
    TcSpan name_span;
    TcSpan span; /* readonly|var */
    TcParseType type;
    int readonly;
    int has_padding;
    TcParseAnnotation padding;
} TcParseField;

typedef struct {
    const char *name;
    TcSpan name_span;
    TcSpan span; /* struct keyword */
    TcSpan end_span;
    int is_public;
    int has_visibility;
    TcSpan visibility_span;
    TcParseField *fields;
    size_t nfields;
} TcParseStruct;

typedef struct {
    const char *name;
    TcSpan name_span;
    TcParseAnnotation *annotations;
    size_t nannotations;
    TcParseType type;
} TcParseParam;

typedef struct {
    const char *name;
    TcSpan name_span;
    TcSpan span; /* func keyword */
    TcSpan end_span;
    int is_public;
    int has_visibility;
    TcSpan visibility_span;
    TcParseAnnotation *annotations;
    size_t nannotations;
    TcParseType ret;
    TcParseParam *params;
    size_t nparams;
    TcParseStmt *body;
    size_t nbody;
} TcParseFunc;

typedef struct {
    const char *name;
    TcSpan name_span;
    TcSpan span; /* const|static keyword */
    int is_static;
    int is_public;
    int has_visibility;
    TcSpan visibility_span;
    TcParseType type;
    TcParseConstInit init;
} TcParseConstDecl;

typedef struct {
    const char *path;
    int is_program;
    TcSpan pattern_span;
    TcParseImport *imports;
    size_t nimports;
    TcParseStruct *structs;
    size_t nstructs;
    TcParseConstDecl *consts; /* #lib const / static var region */
    size_t nconsts;
    TcParseFunc *funcs;
    size_t nfuncs;
    TcParseStmt *body; /* #program top-level statements */
    size_t nbody;
    TcArena arena;
} TcParseModule;

/* Parse one source file (one module).  `path` is copied into the module arena.
 * Returns 0 on success with *out_module set, 0 with out_diag->code set on a
 * LEX/SYN language diagnostic (*out_module NULL), or -1 on implementation
 * failure (*out_impl set; out_diag may be unspecified). */
int tc_parse_source(const char *path, const uint8_t *bytes, size_t size,
                    TcParseModule **out_module, TcFrontendDiag *out_diag,
                    TcImplError *out_impl);

void tc_parse_module_destroy(TcParseModule *module);

/* Name helpers used by tests and SEM. */
const char *tc_parse_type_name(const TcParseType *type);
const char *tc_parse_value_name(const TcParseValue *value);

#endif /* TC_FRONTEND_PARSE_H */
