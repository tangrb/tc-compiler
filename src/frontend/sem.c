/* TC 0.0.46 IMPORT and SEM implementation (standard §3–§6, §13.2). */
#include "frontend/sem.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/module.h"
#include "ir/ir.h"

/* ------------------------------------------------------------------------- */
/* Analysis state.                                                           */
/* ------------------------------------------------------------------------- */

typedef enum {
    TC_MEM_STRUCT = 0,
    TC_MEM_CONST,
    TC_MEM_STATIC,
    TC_MEM_FUNC,
    TC_MEM_IMPORT
} TcMemberKind;

typedef struct {
    const char *name;
    TcSpan span; /* declaration name / import name */
    int is_public;
    TcMemberKind kind;
    TcParseStruct *structure;
    TcParseConstDecl *value_decl;
    TcParseFunc *func;
    uint32_t import_target;
    size_t decl_index; /* index in parse->consts for const/static */
    TcStructDef *sdef;
    const TcType *type;
    TcFuncSig sig;
} TcMember;

typedef struct {
    TcModuleInfo *info;
    TcMember *members;
    size_t nmembers;
} TcModuleSem;

typedef struct {
    TcStructDef *def;
    uint32_t module;
    TcParseStruct *structure;
    int state; /* 0 unresolved, 1 resolving, 2 complete */
    uint64_t *paddings;
} TcStructSlot;

struct TcAnalysis {
    const TcAllocator *alloc;
    TcArena arena;
    TcTypeTable *types;
    unsigned W;
    TcModuleGraph *graph;
    TcModuleSem *modules;
    size_t nmodules;
    TcStructSlot *structs;
    size_t nstructs;
    TcDiagBuffer *diags;
    TcImplError impl;
    jmp_buf env;
    uint32_t next_site_id;
    int import_checked;
    TcAnalysisDiag result_diag;
};

typedef enum { TC_LOC_VAR = 0, TC_LOC_CONST, TC_LOC_PARAM } TcLocalKind;

typedef struct {
    const char *name;
    TcSpan span;
    TcLocalKind kind;
    const TcType *type;
    int visible_from; /* body statement index; -1 for parameters */
    int type_failed;
} TcLocal;

typedef struct {
    TcAnalysis *an;
    uint32_t module_index;
    TcModuleSem *module;
    TcParseFunc *func; /* NULL for the program body or module-level consts */
    int is_program;
    int fast;       /* function is a @fast unsafe context */
    int fn_unsafe;  /* function is an @unsafe unsafe context */
    int fn_safe;    /* explicit @safe assertion */
    int unsafe_context; /* current statement is lexically unsafe */
    int skip_safety;    /* const_expr shapes are checked as CONST_FORM later */
    int cfg_incomplete; /* a control-flow edge could not be resolved */
    const TcType *ret;
    TcLocal *locals;
    size_t nlocals;
    const char **labels;
    TcSpan *label_spans;
    size_t nlabels;
    TcSpan mnemonic_span;
    int stmt_index;
    int module_const_init;
    size_t const_decl_index;
    int public_position;
    const char *declaring_name; /* name being initialized (self-reference) */
} Sem;

typedef struct {
    const TcType *type;
    int ok;
} TypeResult;

typedef struct {
    const TcType *type;
    int ok;
    int writable;
    int is_const;
    int is_param;
    const char *name;
    TcSpan span;
} Ref;

/* ------------------------------------------------------------------------- */
/* Allocation and diagnostics.                                               */
/* ------------------------------------------------------------------------- */

static void *an_alloc(TcAnalysis *an, size_t size) {
    void *p = tc_arena_alloc0(&an->arena, size);
    if (p == NULL) {
        an->impl = TC_IMPL_OOM;
        longjmp(an->env, 1);
    }
    return p;
}

static char *an_strdup(TcAnalysis *an, const char *text) {
    char *p = tc_arena_strdup(&an->arena, text);
    if (p == NULL) {
        an->impl = TC_IMPL_OOM;
        longjmp(an->env, 1);
    }
    return p;
}

static void sem_diag(Sem *s, TcDiagCode code, TcSpan span, const TcSpan *related,
                     TcDiagOrigin origin, TcDiagSite site, TcDiagPhase phase,
                     uint32_t site_id) {
    TcDiagnostic diag;
    const char *file = span.file;
    if (file == NULL) {
        file = s->an->graph->modules[s->module_index].path;
    }
    if (!tc_diag_make(&diag, code, TC_STAGE_SEM, s->module_index, file,
                      span.line, span.column)) {
        s->an->impl = TC_IMPL_INTERNAL;
        longjmp(s->an->env, 1);
    }
    diag.origin = origin;
    diag.site = site;
    diag.site_id = site_id;
    diag.site_phase = phase;
    if (related != NULL) {
        diag.has_related = 1;
        diag.related.file = related->file;
        diag.related.line = related->line;
        diag.related.column = related->column;
    }
    if (tc_diag_buffer_push(s->an->diags, &diag) != TC_IMPL_OK) {
        s->an->impl = TC_IMPL_OOM;
        longjmp(s->an->env, 1);
    }
}

static void sem_diag_simple(Sem *s, TcDiagCode code, TcSpan span) {
    sem_diag(s, code, span, NULL, TC_DIAG_ORIGIN_OPERAND, TC_DIAG_SITE_NONE,
             TC_DIAG_PHASE_NONE, 0);
}

static void sem_diag_related(Sem *s, TcDiagCode code, TcSpan span,
                             TcSpan related) {
    sem_diag(s, code, span, &related, TC_DIAG_ORIGIN_OPERAND,
             TC_DIAG_SITE_NONE, TC_DIAG_PHASE_NONE, 0);
}

static void parse_func_effects(const TcParseFunc *func, int *unsafe,
                               int *fast, int *safe_);

/* Safety diagnostics are construction-level: the operand checks of the same
 * construction still precede them at one position (standard §12.2 #9).  Call
 * safety is the §8.8.2 exception and is emitted in the call-site SAFETY phase
 * so it precedes the LHS and argument checks. */
static void sem_diag_safety(Sem *s, TcDiagCode code, TcSpan span,
                            int is_call, uint32_t site_id) {
    if (is_call) {
        sem_diag(s, code, span, NULL, TC_DIAG_ORIGIN_CONSTRUCT,
                 TC_DIAG_SITE_CALL, TC_DIAG_PHASE_SAFETY, site_id);
    } else {
        sem_diag(s, code, span, NULL, TC_DIAG_ORIGIN_CONSTRUCT,
                 TC_DIAG_SITE_NONE, TC_DIAG_PHASE_NONE, 0);
    }
}

/* Returns 0 after reporting an unsafe instruction used outside an unsafe
 * context.  const_expr shapes are deferred to CONST_FORM. */
static int check_unsafe_operation(Sem *s, TcSpan span) {
    TcDiagCode code;
    if (s->skip_safety || s->unsafe_context) {
        return 1;
    }
    code = s->fn_safe ? TC_CE_SAFE_VIOLATION : TC_CE_UNSAFE_OPERATION;
    sem_diag_safety(s, code, span, 0, 0);
    return 0;
}

/* Direct call safety: target existence is checked before this (§8.8.2).  The
 * @safe assertion wins over UNSAFE_CALL/FAST_CALL (standard §9.3/§9.8). */
static int check_call_safety(Sem *s, const TcMember *func, TcSpan span,
                             uint32_t site_id) {
    int callee_unsafe;
    int callee_fast;
    int callee_safe;
    TcDiagCode code;
    if (s->skip_safety || s->unsafe_context || func->kind != TC_MEM_FUNC) {
        return 1;
    }
    parse_func_effects(func->func, &callee_unsafe, &callee_fast, &callee_safe);
    (void)callee_safe;
    if (callee_unsafe) {
        code = TC_CE_UNSAFE_CALL;
    } else if (callee_fast) {
        code = TC_CE_FAST_CALL;
    } else {
        return 1;
    }
    if (s->fn_safe) {
        code = TC_CE_SAFE_VIOLATION;
    }
    sem_diag_safety(s, code, span, 1, site_id);
    return 0;
}

static void sem_diag_flow(Sem *s, TcDiagCode code, TcSpan span) {
    sem_diag(s, code, span, NULL, TC_DIAG_ORIGIN_CONSTRUCT,
             TC_DIAG_SITE_NONE, TC_DIAG_PHASE_NONE, 0);
}

static int check_indirect_call_safety(Sem *s, TcSpan span, uint32_t site_id) {
    TcDiagCode code;
    if (s->skip_safety || s->unsafe_context) {
        return 1;
    }
    code = s->fn_safe ? TC_CE_SAFE_VIOLATION : TC_CE_UNSAFE_OPERATION;
    sem_diag_safety(s, code, span, 1, site_id);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Small type helpers.                                                       */
/* ------------------------------------------------------------------------- */

static int type_is_integer(const TcType *t) {
    return t != NULL && tc_type_is_integer(t);
}

static int type_is_signed_integer(const TcType *t) {
    return t != NULL && tc_type_is_signed_integer(t);
}

static int type_is_float(const TcType *t) {
    return t != NULL && (t->tag == TC_TY_F32 || t->tag == TC_TY_F64);
}

static int type_is_bool(const TcType *t) { return t != NULL && t->tag == TC_TY_BOOL; }

static int type_is_ptr(const TcType *t) { return t != NULL && t->tag == TC_TY_PTR; }

static int type_is_fnptr(const TcType *t) {
    return t != NULL && t->tag == TC_TY_FNPTR;
}

static int type_is_struct(const TcType *t) {
    return t != NULL && t->tag == TC_TY_STRUCT;
}

static int type_is_void(const TcType *t) { return t != NULL && t->tag == TC_TY_VOID; }

static int parse_func_has_annotation(const TcParseFunc *func, const char *name) {
    size_t i;
    for (i = 0; i < func->nannotations; i++) {
        if (strcmp(func->annotations[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Lexical safety/optimization effects of a function annotation set.  The
 * annotation matrix itself is validated separately; callers only need the
 * context a function body establishes and the caller requirements a target
 * imposes (standard §8.6/§9.3/§9.6). */
static void parse_func_effects(const TcParseFunc *func, int *unsafe,
                               int *fast, int *safe_) {
    *unsafe = parse_func_has_annotation(func, "unsafe");
    *fast = parse_func_has_annotation(func, "fast") ||
            parse_func_has_annotation(func, "fast_math");
    *safe_ = parse_func_has_annotation(func, "safe");
}

static uint64_t width_max(unsigned bits) {
    if (bits >= 64u) {
        return UINT64_MAX;
    }
    return (1ull << bits) - 1ull;
}

static uint64_t signed_min_magnitude(unsigned bits) {
    if (bits >= 64u) {
        return 1ull << 63;
    }
    return 1ull << (bits - 1u);
}

static uint64_t signed_max(unsigned bits) {
    if (bits >= 64u) {
        return (1ull << 63) - 1ull;
    }
    return (1ull << (bits - 1u)) - 1ull;
}

static unsigned type_bits(const TcAnalysis *an, const TcType *t) {
    return tc_type_bits(t, an->W);
}

static TcStructSlot *struct_slot(TcAnalysis *an, uint32_t id) {
    if (id >= an->nstructs) {
        return NULL;
    }
    return &an->structs[id];
}

static const TcField *find_struct_field(const TcStructDef *def,
                                        const char *name) {
    size_t i;
    if (def == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < def->nfields; i++) {
        if (strcmp(def->fields[i].name, name) == 0) {
            return &def->fields[i];
        }
    }
    return NULL;
}

/* Size of a value type in bytes; 0 with *ok = 0 for void/invalid. */
static uint64_t type_size(TcAnalysis *an, const TcType *t, int *ok) {
    uint64_t bits;
    if (t == NULL) {
        *ok = 0;
        return 0;
    }
    if (t->tag == TC_TY_STRUCT) {
        TcStructSlot *slot = struct_slot(an, t->struct_.struct_id);
        if (slot == NULL || slot->state != 2) {
            *ok = 0;
            return 0;
        }
        *ok = 1;
        return slot->def->size;
    }
    if (t->tag == TC_TY_VOID) {
        *ok = 0;
        return 0;
    }
    bits = tc_type_bits(t, an->W);
    if (bits == 0) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return (uint64_t)bits / 8u;
}

static int value_fits_signed(uint64_t magnitude, int negative, unsigned bits) {
    if (bits == 0 || bits > 64u) {
        return 0;
    }
    if (negative) {
        return magnitude <= signed_min_magnitude(bits);
    }
    return magnitude <= signed_max(bits);
}

static int value_fits_unsigned(uint64_t magnitude, int negative, unsigned bits) {
    if (bits == 0 || bits > 64u || negative) {
        return 0;
    }
    return magnitude <= width_max(bits);
}

/* ------------------------------------------------------------------------- */
/* Member tables.                                                            */
/* ------------------------------------------------------------------------- */

static TcMember *find_member(TcModuleSem *module, const char *name) {
    size_t i;
    if (module == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < module->nmembers; i++) {
        if (strcmp(module->members[i].name, name) == 0) {
            return &module->members[i];
        }
    }
    return NULL;
}

static TcMember *find_member_in(TcAnalysis *an, uint32_t module,
                                const char *name) {
    if (module >= an->nmodules) {
        return NULL;
    }
    return find_member(&an->modules[module], name);
}

typedef struct {
    TcMember *members;
    size_t count;
    size_t cap;
} MemberVec;

static void member_push(TcAnalysis *an, MemberVec *vec, const TcMember *member) {
    if (vec->count == vec->cap) {
        size_t cap = vec->cap == 0 ? 8u : vec->cap * 2u;
        TcMember *fresh = (TcMember *)an_alloc(an, cap * sizeof(TcMember));
        if (vec->count != 0) {
            memcpy(fresh, vec->members, vec->count * sizeof(TcMember));
        }
        vec->members = fresh;
        vec->cap = cap;
    }
    vec->members[vec->count++] = *member;
}

static int span_before(TcSpan a, TcSpan b) {
    if (a.line != b.line) {
        return a.line < b.line;
    }
    return a.column < b.column;
}

static void build_members(TcAnalysis *an) {
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        TcParseModule *parse = module->info->parse;
        MemberVec vec;
        size_t i;
        memset(&vec, 0, sizeof(vec));
        for (i = 0; i < parse->nimports; i++) {
            TcMember m;
            memset(&m, 0, sizeof(m));
            m.name = parse->imports[i].name;
            m.span = parse->imports[i].name_span;
            m.is_public = 1;
            m.kind = TC_MEM_IMPORT;
            m.import_target = module->info->import_targets[i];
            member_push(an, &vec, &m);
        }
        for (i = 0; i < parse->nstructs; i++) {
            TcMember m;
            TcStructDef *def;
            size_t id = an->nstructs;
            memset(&m, 0, sizeof(m));
            m.name = parse->structs[i].name;
            m.span = parse->structs[i].name_span;
            m.is_public = parse->structs[i].is_public;
            m.kind = TC_MEM_STRUCT;
            m.structure = &parse->structs[i];
            def = (TcStructDef *)an_alloc(an, sizeof(TcStructDef));
            memset(def, 0, sizeof(*def));
            def->id = (uint32_t)id;
            def->module = (uint32_t)mi;
            def->name = an_strdup(an, parse->structs[i].name);
            an->structs[id].def = def;
            an->structs[id].module = (uint32_t)mi;
            an->structs[id].structure = &parse->structs[i];
            an->structs[id].state = 0;
            m.sdef = def;
            m.type = tc_type_struct(an->types, (uint32_t)id);
            if (m.type == NULL) {
                an->impl = TC_IMPL_OOM;
                longjmp(an->env, 1);
            }
            an->nstructs++;
            member_push(an, &vec, &m);
        }
        for (i = 0; i < parse->nconsts; i++) {
            TcMember m;
            memset(&m, 0, sizeof(m));
            m.name = parse->consts[i].name;
            m.span = parse->consts[i].name_span;
            m.is_public = parse->consts[i].is_public;
            m.kind = parse->consts[i].is_static ? TC_MEM_STATIC : TC_MEM_CONST;
            m.value_decl = &parse->consts[i];
            m.decl_index = i;
            member_push(an, &vec, &m);
        }
        for (i = 0; i < parse->nfuncs; i++) {
            TcMember m;
            memset(&m, 0, sizeof(m));
            m.name = parse->funcs[i].name;
            m.span = parse->funcs[i].name_span;
            m.is_public = parse->funcs[i].is_public;
            m.kind = TC_MEM_FUNC;
            m.func = &parse->funcs[i];
            member_push(an, &vec, &m);
        }
        module->members = vec.members;
        module->nmembers = vec.count;
    }
}

static void check_duplicate_members(TcAnalysis *an) {
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        Sem s;
        size_t j;
        memset(&s, 0, sizeof(s));
        s.an = an;
        s.module_index = (uint32_t)mi;
        s.module = module;
        for (j = 0; j < module->nmembers; j++) {
            size_t i;
            TcMember *later = &module->members[j];
            TcMember *first = NULL;
            for (i = 0; i < module->nmembers; i++) {
                TcMember *other = &module->members[i];
                if (i == j || strcmp(other->name, later->name) != 0) {
                    continue;
                }
                if (!span_before(other->span, later->span)) {
                    continue;
                }
                if (first == NULL || span_before(other->span, first->span)) {
                    first = other;
                }
            }
            if (first != NULL) {
                sem_diag_related(&s, TC_CE_DUPLICATE_MEMBER, later->span,
                                 first->span);
            }
        }
    }
}

static int body_has_local_named(const TcParseStmt *body, size_t n,
                                const char *name, TcSpan *out_span) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (body[i].kind == TC_STMT_VAR || body[i].kind == TC_STMT_CONST) {
            const char *local = body[i].kind == TC_STMT_VAR
                                    ? body[i].var.name
                                    : body[i].cnst.name;
            if (strcmp(local, name) == 0) {
                *out_span = body[i].kind == TC_STMT_VAR
                                ? body[i].var.name_span
                                : body[i].cnst.name_span;
                return 1;
            }
        }
    }
    return 0;
}

static int func_has_name(const TcParseFunc *func, const char *name,
                         TcSpan *out_span) {
    size_t i;
    for (i = 0; i < func->nparams; i++) {
        if (strcmp(func->params[i].name, name) == 0) {
            *out_span = func->params[i].name_span;
            return 1;
        }
    }
    return body_has_local_named(func->body, func->nbody, name, out_span);
}

/* IMPORT stage: an import name occupies the module namespace.  It must not
 * collide with another import or with a member/local name of this module. */
static void check_import_name_conflicts(TcAnalysis *an) {
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        TcParseModule *parse = module->info->parse;
        size_t i;
        for (i = 0; i < parse->nimports; i++) {
            const char *name = parse->imports[i].name;
            TcSpan span = parse->imports[i].name_span;
            size_t j;
            int emitted = 0;
            for (j = 0; j < i && !emitted; j++) {
                if (strcmp(parse->imports[j].name, name) == 0) {
                    TcDiagnostic diag;
                    if (!tc_diag_make(&diag, TC_CE_IMPORT_NAME_CONFLICT,
                                      TC_STAGE_IMPORT, (uint32_t)mi, span.file,
                                      span.line, span.column)) {
                        an->impl = TC_IMPL_INTERNAL;
                        longjmp(an->env, 1);
                    }
                    diag.has_related = 1;
                    diag.related.file = parse->imports[j].name_span.file;
                    diag.related.line = parse->imports[j].name_span.line;
                    diag.related.column = parse->imports[j].name_span.column;
                    if (tc_diag_buffer_push(an->graph->import_diags, &diag) !=
                        TC_IMPL_OK) {
                        an->impl = TC_IMPL_OOM;
                        longjmp(an->env, 1);
                    }
                    emitted = 1;
                }
            }
            for (j = 0; j < module->nmembers && !emitted; j++) {
                TcMember *m = &module->members[j];
                if (m->kind == TC_MEM_IMPORT) {
                    continue;
                }
                if (strcmp(m->name, name) == 0) {
                    TcDiagnostic diag;
                    if (!tc_diag_make(&diag, TC_CE_IMPORT_NAME_CONFLICT,
                                      TC_STAGE_IMPORT, (uint32_t)mi, span.file,
                                      span.line, span.column)) {
                        an->impl = TC_IMPL_INTERNAL;
                        longjmp(an->env, 1);
                    }
                    diag.has_related = 1;
                    diag.related.file = m->span.file;
                    diag.related.line = m->span.line;
                    diag.related.column = m->span.column;
                    if (tc_diag_buffer_push(an->graph->import_diags, &diag) !=
                        TC_IMPL_OK) {
                        an->impl = TC_IMPL_OOM;
                        longjmp(an->env, 1);
                    }
                    emitted = 1;
                }
            }
            for (j = 0; j < parse->nfuncs && !emitted; j++) {
                TcSpan local_span;
                if (func_has_name(&parse->funcs[j], name, &local_span)) {
                    TcDiagnostic diag;
                    if (!tc_diag_make(&diag, TC_CE_IMPORT_NAME_CONFLICT,
                                      TC_STAGE_IMPORT, (uint32_t)mi, span.file,
                                      span.line, span.column)) {
                        an->impl = TC_IMPL_INTERNAL;
                        longjmp(an->env, 1);
                    }
                    diag.has_related = 1;
                    diag.related.file = local_span.file;
                    diag.related.line = local_span.line;
                    diag.related.column = local_span.column;
                    if (tc_diag_buffer_push(an->graph->import_diags, &diag) !=
                        TC_IMPL_OK) {
                        an->impl = TC_IMPL_OOM;
                        longjmp(an->env, 1);
                    }
                    emitted = 1;
                }
            }
            {
                TcSpan local_span;
                if (!emitted &&
                    body_has_local_named(parse->body, parse->nbody, name,
                                         &local_span)) {
                    TcDiagnostic diag;
                    if (!tc_diag_make(&diag, TC_CE_IMPORT_NAME_CONFLICT,
                                      TC_STAGE_IMPORT, (uint32_t)mi, span.file,
                                      span.line, span.column)) {
                        an->impl = TC_IMPL_INTERNAL;
                        longjmp(an->env, 1);
                    }
                    diag.has_related = 1;
                    diag.related.file = local_span.file;
                    diag.related.line = local_span.line;
                    diag.related.column = local_span.column;
                    if (tc_diag_buffer_push(an->graph->import_diags, &diag) !=
                        TC_IMPL_OK) {
                        an->impl = TC_IMPL_OOM;
                        longjmp(an->env, 1);
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Name resolution.                                                          */
/* ------------------------------------------------------------------------- */

static TcLocal *lookup_local(Sem *s, const char *name) {
    size_t i;
    for (i = 0; i < s->nlocals; i++) {
        TcLocal *local = &s->locals[i];
        int visible = local->visible_from < s->stmt_index;
        if (strcmp(local->name, name) == 0 && visible) {
            return local;
        }
    }
    return NULL;
}

/* Resolve a module member reference.  `base_is_self` selects the current
 * module; otherwise `module_id` is the imported instance. */
static TcMember *lookup_module_member(Sem *s, uint32_t module_id,
                                      const char *name, TcSpan name_span,
                                      int write, int qualified,
                                      TcSpan *field_spans, size_t nfields,
                                      size_t *next_field, Ref *out) {
    TcMember *m = find_member_in(s->an, module_id, name);
    int same = module_id == s->module_index;
    if (m == NULL) {
        sem_diag_simple(s, TC_CE_UNDEFINED_NAME,
                        nfields > 0 ? name_span : name_span);
        return NULL;
    }
    if (!same && !m->is_public) {
        sem_diag_related(s, TC_CE_PRIVATE_ACCESS, name_span, m->span);
        return NULL;
    }
    if (m->kind == TC_MEM_FUNC) {
        TcDiagCode code = same && !qualified ? TC_CE_FUNCTION_SCOPE_ACCESS
                                             : TC_CE_TYPE_MISMATCH;
        sem_diag_simple(s, code, name_span);
        return NULL;
    }
    if (m->kind == TC_MEM_STRUCT || m->kind == TC_MEM_IMPORT) {
        sem_diag_simple(s, TC_CE_TYPE_MISMATCH, name_span);
        return NULL;
    }
    if (m->kind == TC_MEM_CONST) {
        if (s->module_const_init && same &&
            m->decl_index >= s->const_decl_index) {
            sem_diag_simple(s, TC_CE_UNDEFINED_NAME, name_span);
            return NULL;
        }
        out->writable = 0;
        out->is_const = 1;
    } else {
        out->writable = 1;
        out->is_const = 0;
    }
    if (write && out->is_const) {
        sem_diag_simple(s, TC_CE_CONST_ASSIGNMENT, name_span);
        return NULL;
    }
    out->type = m->type;
    out->ok = out->type != NULL;
    out->name = name;
    out->span = name_span;
    (void)field_spans;
    (void)next_field;
    if (!out->ok) {
        return NULL;
    }
    return m;
}

static int resolve_fields(Sem *s, Ref *ref, const char **fields,
                          TcSpan *field_spans, size_t nfields,
                          int for_write) {
    size_t i;
    const TcType *cur;
    /* §5.7.3 steps 3 then 4: existence and intermediate-struct checks for the
     * whole path come before the readonly path rule. */
    cur = ref->type;
    for (i = 0; i < nfields; i++) {
        TcStructSlot *slot;
        const TcField *field;
        if (!ref->ok || cur == NULL) {
            return 0;
        }
        if (!type_is_struct(cur)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, field_spans[i]);
            ref->ok = 0;
            return 0;
        }
        slot = struct_slot(s->an, cur->struct_.struct_id);
        if (slot == NULL || slot->state != 2) {
            ref->ok = 0;
            return 0;
        }
        field = find_struct_field(slot->def, fields[i]);
        if (field == NULL) {
            sem_diag_simple(s, TC_CE_UNKNOWN_FIELD, field_spans[i]);
            ref->ok = 0;
            return 0;
        }
        cur = field->type;
        if (cur == NULL) {
            ref->ok = 0;
            return 0;
        }
    }
    if (for_write) {
        const TcType *path = ref->type;
        for (i = 0; i < nfields; i++) {
            TcStructSlot *slot = struct_slot(s->an, path->struct_.struct_id);
            const TcField *field = find_struct_field(slot->def, fields[i]);
            if (field->readonly) {
                sem_diag_simple(s, TC_CE_IMMUTABLE_FIELD, field_spans[i]);
                ref->ok = 0;
                return 0;
            }
            path = field->type;
        }
    }
    ref->type = cur;
    ref->ok = cur != NULL;
    return ref->ok;
}

static Ref resolve_value(Sem *s, const TcParseValue *value) {
    Ref ref;
    memset(&ref, 0, sizeof(ref));
    if (value->kind != TC_PVAL_NAME) {
        ref.ok = 0;
        return ref;
    }
    if (s->declaring_name != NULL &&
        strcmp(value->base, s->declaring_name) == 0) {
        sem_diag_simple(s, TC_CE_UNDEFINED_NAME, value->base_span);
        return ref;
    }
    if (strcmp(value->base, "Self") == 0) {
        if (value->nfields == 0) {
            sem_diag_simple(s, TC_CE_SYNTAX, value->base_span);
            return ref;
        }
        lookup_module_member(s, s->module_index, value->fields[0],
                             value->field_spans[0], 0, 1,
                             value->field_spans + 1, value->nfields - 1, NULL,
                             &ref);
        if (ref.ok) {
            resolve_fields(s, &ref, value->fields + 1,
                           value->field_spans + 1, value->nfields - 1, 0);
        }
        return ref;
    }
    {
        TcMember *import = find_member(s->module, value->base);
        if (import != NULL && import->kind == TC_MEM_IMPORT) {
            if (value->nfields == 0) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, value->base_span);
                return ref;
            }
            if (import->import_target == TC_MODULE_NO_TARGET) {
                ref.ok = 0;
                return ref;
            }
            lookup_module_member(s, import->import_target, value->fields[0],
                                 value->field_spans[0], 0, 1,
                                 value->field_spans + 1, value->nfields - 1,
                                 NULL, &ref);
            if (ref.ok) {
                resolve_fields(s, &ref, value->fields + 1,
                               value->field_spans + 1, value->nfields - 1, 0);
            }
            return ref;
        }
    }
    {
        TcLocal *local = lookup_local(s, value->base);
        if (local != NULL) {
            ref.type = local->type;
            ref.ok = local->type != NULL;
            ref.writable = local->kind == TC_LOC_VAR;
            ref.is_const = local->kind == TC_LOC_CONST;
            ref.is_param = local->kind == TC_LOC_PARAM;
            ref.name = value->base;
            ref.span = value->base_span;
            if (!ref.ok) {
                return ref;
            }
            resolve_fields(s, &ref, value->fields, value->field_spans,
                           value->nfields, 0);
            return ref;
        }
    }
    /* Module const/static value member: bare access is only legal in a
     * module-level const initializer; in a body it is FUNCTION_SCOPE_ACCESS
     * for value members and TYPE_MISMATCH for a struct name. */
    {
        TcMember *m = find_member(s->module, value->base);
        if (m != NULL) {
            if (m->kind == TC_MEM_STRUCT || m->kind == TC_MEM_IMPORT) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, value->base_span);
                return ref;
            }
            if ((m->kind == TC_MEM_CONST || m->kind == TC_MEM_STATIC ||
                 m->kind == TC_MEM_FUNC) &&
                !s->module_const_init) {
                sem_diag_simple(s, TC_CE_FUNCTION_SCOPE_ACCESS,
                                value->base_span);
                return ref;
            }
            if (m->kind == TC_MEM_CONST || m->kind == TC_MEM_STATIC) {
                lookup_module_member(s, s->module_index, value->base,
                                     value->base_span, 0, 0,
                                     value->field_spans, value->nfields, NULL,
                                     &ref);
                if (ref.ok) {
                    resolve_fields(s, &ref, value->fields, value->field_spans,
                                   value->nfields, 0);
                }
                return ref;
            }
        }
    }
    sem_diag_simple(s, TC_CE_UNDEFINED_NAME, value->base_span);
    return ref;
}

static Ref resolve_target(Sem *s, const TcParseInstr *instr) {
    Ref ref;
    const char **fields = instr->dst_fields;
    TcSpan *field_spans = instr->dst_field_spans;
    size_t nfields = instr->dst_nfields;
    size_t first_field = 0;
    memset(&ref, 0, sizeof(ref));
    if (strcmp(instr->dst_base, "Self") == 0) {
        if (nfields == 0) {
            sem_diag_simple(s, TC_CE_SYNTAX, instr->dst_base_span);
            return ref;
        }
        lookup_module_member(s, s->module_index, fields[0], field_spans[0], 1,
                             1, field_spans + 1, nfields - 1, NULL, &ref);
        first_field = 1;
    } else {
        TcMember *import = find_member(s->module, instr->dst_base);
        if (import != NULL && import->kind == TC_MEM_IMPORT) {
            if (nfields == 0) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->dst_base_span);
                return ref;
            }
            if (import->import_target == TC_MODULE_NO_TARGET) {
                return ref;
            }
            lookup_module_member(s, import->import_target, fields[0],
                                 field_spans[0], 1, 1, field_spans + 1,
                                 nfields - 1, NULL, &ref);
            first_field = 1;
        } else {
            TcLocal *local = lookup_local(s, instr->dst_base);
            if (local != NULL) {
                if (local->kind == TC_LOC_CONST) {
                    sem_diag_simple(s, TC_CE_CONST_ASSIGNMENT,
                                    instr->dst_base_span);
                    return ref;
                }
                if (local->kind == TC_LOC_PARAM) {
                    sem_diag_simple(s, TC_CE_PARAMETER_ASSIGNMENT,
                                    instr->dst_base_span);
                    return ref;
                }
                ref.type = local->type;
                ref.ok = local->type != NULL;
                ref.writable = 1;
                ref.name = local->name;
                ref.span = instr->dst_base_span;
            } else {
                TcMember *m = find_member(s->module, instr->dst_base);
                if (m == NULL) {
                    sem_diag_simple(s, TC_CE_UNDEFINED_NAME,
                                    instr->dst_base_span);
                    return ref;
                }
                if (m->kind == TC_MEM_FUNC) {
                    sem_diag_simple(s, TC_CE_FUNCTION_SCOPE_ACCESS,
                                    instr->dst_base_span);
                    return ref;
                }
                if (m->kind == TC_MEM_CONST || m->kind == TC_MEM_STATIC) {
                    /* Module value members require Self./m.; a bare const or
                     * static var as an assignment target is
                     * FUNCTION_SCOPE_ACCESS (standard §5.6/§6.14). */
                    sem_diag_simple(s, TC_CE_FUNCTION_SCOPE_ACCESS,
                                    instr->dst_base_span);
                    return ref;
                }
                if (m->kind == TC_MEM_STRUCT || m->kind == TC_MEM_IMPORT) {
                    sem_diag_simple(s, TC_CE_TYPE_MISMATCH,
                                    instr->dst_base_span);
                    return ref;
                }
                ref.type = m->type;
                ref.ok = m->type != NULL;
                ref.writable = 1;
                ref.name = m->name;
                ref.span = m->span;
            }
        }
    }
    if (ref.ok) {
        resolve_fields(s, &ref, fields + first_field,
                       field_spans + first_field, nfields - first_field, 1);
    }
    return ref;
}

/* ------------------------------------------------------------------------- */
/* Type syntax resolution and interning.                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    int public_position;
    int in_struct_field;
    int self_allowed; /* inside ptr/fnptr of the struct being defined */
    uint32_t self_id;
} TypeCtx;

static const TcType *resolve_type(Sem *s, const TcParseType *type,
                                  TypeCtx ctx);
static void check_annotations(TcAnalysis *an);

static const TcType *resolve_named_type(Sem *s, const TcParseType *type,
                                        TypeCtx ctx) {
    const char *member_name = type->member;
    TcSpan name_span = type->member != NULL ? type->member_span : type->span;
    TcSpan base_span = type->span;
    TcMember *member = NULL;
    TcMember *import = NULL;
    uint32_t target_module = s->module_index;
    int same_module = 1;
    int qualified = 0;
    if (strcmp(type->base, "Self") == 0) {
        if (member_name == NULL) {
            sem_diag_simple(s, TC_CE_SYNTAX, base_span);
            return NULL;
        }
        qualified = 1;
        member = find_member(s->module, member_name);
    } else {
        import = find_member(s->module, type->base);
        if (import != NULL && import->kind == TC_MEM_IMPORT) {
            if (member_name == NULL) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, base_span);
                return NULL;
            }
            if (import->import_target == TC_MODULE_NO_TARGET) {
                return NULL;
            }
            target_module = import->import_target;
            same_module = 0;
            qualified = 1;
            member = find_member_in(s->an, target_module, member_name);
        } else if (member_name != NULL) {
            /* `x.T`: the first name is not an imported module.  A known value
             * or module name in a type position is a type mismatch. */
            TcLocal *local = lookup_local(s, type->base);
            TcMember *value = find_member(s->module, type->base);
            if (local != NULL || value != NULL) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, base_span);
            } else {
                sem_diag_simple(s, TC_CE_UNDEFINED_NAME, base_span);
            }
            return NULL;
        } else {
            member = find_member(s->module, type->base);
        }
    }
    if (member == NULL) {
        if (qualified) {
            /* A qualified member that does not exist is unresolved; a
             * same-named local is unrelated (standard §5.6/§6.13). */
            sem_diag_simple(s, TC_CE_UNDEFINED_NAME, name_span);
            return NULL;
        }
        /* A known value/label name in a type position is a type mismatch; an
         * unresolved bare name is UNDEFINED_NAME. */
        TcLocal *local = lookup_local(s, type->base);
        TcMember *value = find_member(s->module, type->base);
        size_t li;
        int is_label = 0;
        for (li = 0; li < s->nlabels; li++) {
            if (strcmp(s->labels[li], type->base) == 0) {
                is_label = 1;
                break;
            }
        }
        if (local != NULL || is_label ||
            (value != NULL && value->kind != TC_MEM_STRUCT) ||
            (import != NULL && import->kind == TC_MEM_IMPORT)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, base_span);
        } else {
            sem_diag_simple(s, TC_CE_UNDEFINED_NAME, base_span);
        }
        return NULL;
    }
    if (!same_module && !member->is_public) {
        if (member->kind == TC_MEM_STRUCT && ctx.public_position) {
            sem_diag_related(s, TC_CE_PRIVATE_TYPE, name_span, member->span);
        } else {
            sem_diag_related(s, TC_CE_PRIVATE_ACCESS, name_span, member->span);
        }
        return NULL;
    }
    if (member->kind != TC_MEM_STRUCT) {
        sem_diag_simple(s, TC_CE_TYPE_MISMATCH, name_span);
        return NULL;
    }
    if (!member->is_public && ctx.public_position) {
        sem_diag_related(s, TC_CE_PRIVATE_TYPE, name_span, member->span);
        return NULL;
    }
    {
        uint32_t id = member->sdef->id;
        TcStructSlot *slot = struct_slot(s->an, id);
        if (slot == NULL) {
            return NULL;
        }
        if (ctx.in_struct_field && slot->state != 2) {
            if (slot->state == 1 && id == ctx.self_id) {
                if (ctx.self_allowed) {
                    return member->type;
                }
                sem_diag_simple(s, TC_CE_STRUCT_SELF_VALUE, name_span);
                return NULL;
            }
            sem_diag_simple(s, TC_CE_UNDEFINED_NAME, name_span);
            return NULL;
        }
        if (slot->state != 2) {
            sem_diag_simple(s, TC_CE_UNDEFINED_NAME, name_span);
            return NULL;
        }
        return member->type;
    }
}

static const TcType *resolve_type(Sem *s, const TcParseType *type,
                                  TypeCtx ctx) {
    if (type == NULL) {
        return NULL;
    }
    switch (type->kind) {
    case TC_PTYPE_SCALAR:
        return tc_type_scalar(s->an->types, type->scalar);
    case TC_PTYPE_VOID:
        return tc_type_scalar(s->an->types, TC_TY_VOID);
    case TC_PTYPE_PTR: {
        TypeCtx inner = ctx;
        const TcType *pointee;
        inner.self_allowed = 1;
        pointee = resolve_type(s, type->pointee, inner);
        if (pointee == NULL) {
            return NULL;
        }
        if (type_is_void(pointee)) {
            sem_diag_simple(s, TC_CE_VOID, type->pointee->span);
            return NULL;
        }
        return tc_type_ptr(s->an->types, pointee);
    }
    case TC_PTYPE_FNPTR: {
        TypeCtx inner = ctx;
        const TcType *ret;
        const TcType **params;
        size_t n = type->nparams;
        size_t i;
        inner.self_allowed = 1;
        ret = resolve_type(s, type->ret, inner);
        if (ret == NULL) {
            return NULL;
        }
        params = (const TcType **)an_alloc(
            s->an, (n == 0 ? 1u : n) * sizeof(const TcType *));
        for (i = 0; i < n; i++) {
            params[i] = resolve_type(s, type->params[i], inner);
            if (params[i] == NULL) {
                return NULL;
            }
            if (type_is_void(params[i])) {
                sem_diag_simple(s, TC_CE_VOID, type->params[i]->span);
                return NULL;
            }
        }
        return tc_type_fnptr(s->an->types, ret, params, n);
    }
    case TC_PTYPE_NAME:
        return resolve_named_type(s, type, ctx);
    default:
        return NULL;
    }
}

/* Resolve a declared type and reject void where a value type is required. */
static const TcType *resolve_value_type(Sem *s, const TcParseType *type,
                                        int public_position) {
    TypeCtx ctx;
    const TcType *t;
    ctx.public_position = public_position;
    ctx.in_struct_field = 0;
    ctx.self_allowed = 0;
    ctx.self_id = UINT32_MAX;
    t = resolve_type(s, type, ctx);
    if (t == NULL) {
        return NULL;
    }
    if (type_is_void(t)) {
        sem_diag_simple(s, TC_CE_VOID, type->span);
        return NULL;
    }
    return t;
}

static int read_padding(Sem *s, const TcParseField *field, uint64_t *out) {
    const TcParseAnnotation *ann = &field->padding;
    *out = 0;
    if (!field->has_padding) {
        return 1;
    }
    if (strcmp(ann->name, "padding") != 0 || ann->nargs != 1) {
        /* Non-@padding field annotations and argument counts belong to the
         * annotation validation stage; layout simply has no padding. */
        return 1;
    }
    {
        const TcParseValue *v = &ann->args[0];
        if (v->kind != TC_PVAL_INT || v->int_negative ||
            v->int_unsigned_suffix || v->int_radix != 10) {
            sem_diag_simple(s, TC_CE_CONST_FORM, v->span);
            return 0;
        }
        *out = v->int_magnitude;
    }
    return 1;
}

static int layout_add(TcAnalysis *an, uint64_t *total, uint64_t add) {
    uint64_t max = width_max(an->W);
    if (add > max || *total > max - add) {
        return 0;
    }
    *total += add;
    return 1;
}

static void layout_order_visit(TcAnalysis *an, uint32_t module,
                              unsigned char *visited, uint32_t *order,
                              size_t *norder) {
    size_t i;
    if (module >= an->nmodules || visited[module]) {
        return;
    }
    visited[module] = 1;
    for (i = 0; i < an->modules[module].info->nimports; i++) {
        uint32_t target = an->modules[module].info->import_targets[i];
        if (target != TC_MODULE_NO_TARGET) {
            layout_order_visit(an, target, visited, order, norder);
        }
    }
    order[(*norder)++] = module;
}

/* Dependency-first module order: a DFS postorder puts every imported module
 * before its importers, including diamond cross-edges.  Diagnostics still use
 * module_order; this order only fixes layout/type-definition visits. */
static uint32_t *layout_order(TcAnalysis *an, size_t *nout) {
    unsigned char *visited = (unsigned char *)an_alloc(
        an, an->nmodules == 0 ? 1u : an->nmodules);
    uint32_t *order = (uint32_t *)an_alloc(
        an, (an->nmodules == 0 ? 1u : an->nmodules) * sizeof(uint32_t));
    size_t norder = 0;
    size_t i;
    memset(visited, 0, an->nmodules == 0 ? 1u : an->nmodules);
    for (i = 0; i < an->nmodules; i++) {
        layout_order_visit(an, (uint32_t)i, visited, order, &norder);
    }
    *nout = norder;
    return order;
}

static void resolve_structs(TcAnalysis *an) {
    uint32_t *order;
    size_t norder = 0;
    size_t oi;
    order = layout_order(an, &norder);
    for (oi = 0; oi < norder; oi++) {
        uint32_t module = order[oi];
        size_t id;
        for (id = 0; id < an->nstructs; id++) {
        TcStructSlot *slot = &an->structs[id];
        if (slot->module != module) {
            continue;
        }
        TcParseStruct *structure = slot->structure;
        TcStructDef *def = slot->def;
        Sem s;
        size_t i;
        uint64_t total = 0;
        int overflow = 0;
        memset(&s, 0, sizeof(s));
        s.an = an;
        s.module_index = slot->module;
        s.module = &an->modules[slot->module];
        s.public_position = structure->is_public;
        slot->state = 1;
        def->fields = (TcField *)an_alloc(
            an, (structure->nfields == 0 ? 1u : structure->nfields) *
                    sizeof(TcField));
        slot->paddings = (uint64_t *)an_alloc(
            an, (structure->nfields == 0 ? 1u : structure->nfields) *
                    sizeof(uint64_t));
        memset(def->fields, 0,
               (structure->nfields == 0 ? 1u : structure->nfields) *
                   sizeof(TcField));
        def->nfields = structure->nfields;
        if (structure->nfields == 0) {
            sem_diag_simple(&s, TC_CE_EMPTY_STRUCT, structure->name_span);
        }
        for (i = 0; i < structure->nfields; i++) {
            const TcParseField *field = &structure->fields[i];
            size_t j;
            TypeCtx ctx;
            const TcType *field_type;
            uint64_t padding = 0;
            int padding_ok;
            def->fields[i].name = an_strdup(an, field->name);
            def->fields[i].readonly = field->readonly;
            for (j = 0; j < i; j++) {
                if (strcmp(structure->fields[j].name, field->name) == 0) {
                    sem_diag_related(&s, TC_CE_DUPLICATE_FIELD,
                                     field->name_span,
                                     structure->fields[j].name_span);
                    break;
                }
            }
            if (field->has_padding &&
                strcmp(field->padding.name, "padding") != 0) {
                /* §14.3: the field_padding shape admits one ANNOTATION(INT),
                 * but only @padding is a field annotation (standard §2.6/
                 * §8.6); a non-@padding name is ANNOTATION, not syntax. */
                sem_diag_simple(&s, TC_CE_ANNOTATION, field->padding.span);
            }
            ctx.public_position = structure->is_public;
            ctx.in_struct_field = 1;
            ctx.self_allowed = 0;
            ctx.self_id = (uint32_t)id;
            field_type = resolve_type(&s, &field->type, ctx);
            if (field_type != NULL && type_is_void(field_type)) {
                sem_diag_simple(&s, TC_CE_VOID, field->type.span);
                field_type = NULL;
            }
            def->fields[i].type = field_type;
            padding_ok = read_padding(&s, field, &padding);
            slot->paddings[i] = padding_ok ? padding : 0;
            if (padding > UINT32_MAX) {
                def->fields[i].padding = UINT32_MAX;
            } else {
                def->fields[i].padding = (uint32_t)padding;
            }
            def->fields[i].offset = total;
            if (field_type != NULL) {
                int size_ok = 0;
                uint64_t size = type_size(an, field_type, &size_ok);
                if (size_ok && !layout_add(an, &total, size)) {
                    overflow = 1;
                }
            }
            if (!overflow && !layout_add(an, &total, slot->paddings[i])) {
                overflow = 1;
            }
            if (overflow) {
                sem_diag_simple(&s, TC_CE_LAYOUT_OVERFLOW, field->name_span);
                break;
            }
        }
        def->size = overflow ? width_max(an->W) : total;
        slot->state = 2;
        }
    }
}

static void resolve_signatures(TcAnalysis *an) {
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        size_t i;
        for (i = 0; i < module->nmembers; i++) {
            TcMember *m = &module->members[i];
            Sem s;
            TcParseFunc *func;
            size_t p;
            if (m->kind != TC_MEM_FUNC) {
                continue;
            }
            func = m->func;
            memset(&s, 0, sizeof(s));
            s.an = an;
            s.module_index = (uint32_t)mi;
            s.module = module;
            m->sig.ret = resolve_type(&s, &func->ret,
                                      (TypeCtx){func->is_public, 0, 0,
                                                UINT32_MAX});
            m->sig.nparams = func->nparams;
            m->sig.params = (TcParam *)an_alloc(
                an, (func->nparams == 0 ? 1u : func->nparams) *
                        sizeof(TcParam));
            memset(m->sig.params, 0,
                   (func->nparams == 0 ? 1u : func->nparams) * sizeof(TcParam));
            for (p = 0; p < func->nparams; p++) {
                const TcType *param_type = resolve_value_type(
                    &s, &func->params[p].type, func->is_public);
                m->sig.params[p].name = an_strdup(an, func->params[p].name);
                m->sig.params[p].type = param_type;
            }
        }
    }
}

static void resolve_module_value_types(TcAnalysis *an) {
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        size_t i;
        Sem s;
        memset(&s, 0, sizeof(s));
        s.an = an;
        s.module_index = (uint32_t)mi;
        s.module = module;
        for (i = 0; i < module->nmembers; i++) {
            TcMember *m = &module->members[i];
            if (m->kind != TC_MEM_CONST && m->kind != TC_MEM_STATIC) {
                continue;
            }
            m->type = resolve_value_type(&s, &m->value_decl->type,
                                         m->is_public);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Literals and conversion matrices.                                         */
/* ------------------------------------------------------------------------- */

typedef enum {
    TC_CAT_INT = 0,
    TC_CAT_FLOAT,
    TC_CAT_BOOL,
    TC_CAT_PTR,
    TC_CAT_FNPTR,
    TC_CAT_STRUCT,
    TC_CAT_VOID,
    TC_CAT_INVALID
} TcTypeCat;

static TcTypeCat type_cat(const TcType *t) {
    if (t == NULL) {
        return TC_CAT_INVALID;
    }
    switch (t->tag) {
    case TC_TY_I8:
    case TC_TY_I16:
    case TC_TY_I32:
    case TC_TY_I64:
    case TC_TY_U8:
    case TC_TY_U16:
    case TC_TY_U32:
    case TC_TY_U64:
    case TC_TY_ISIZE:
    case TC_TY_USIZE:
        return TC_CAT_INT;
    case TC_TY_F32:
    case TC_TY_F64:
        return TC_CAT_FLOAT;
    case TC_TY_BOOL:
        return TC_CAT_BOOL;
    case TC_TY_PTR:
        return TC_CAT_PTR;
    case TC_TY_FNPTR:
        return TC_CAT_FNPTR;
    case TC_TY_STRUCT:
        return TC_CAT_STRUCT;
    case TC_TY_VOID:
        return TC_CAT_VOID;
    default:
        return TC_CAT_INVALID;
    }
}

static int int_literal_fits(Sem *s, const TcParseValue *v,
                            const TcType *expected) {
    unsigned bits = type_bits(s->an, expected);
    if (v->int_unsigned_suffix) {
        return value_fits_unsigned(v->int_magnitude, 0, bits);
    }
    if (v->int_negative) {
        return value_fits_signed(v->int_magnitude, 1, bits);
    }
    if (type_is_signed_integer(expected)) {
        return value_fits_signed(v->int_magnitude, 0, bits);
    }
    return value_fits_unsigned(v->int_magnitude, 0, bits);
}

static int check_literal_expected(Sem *s, const TcParseValue *v,
                                  const TcType *expected) {
    if (expected == NULL) {
        sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
        return 0;
    }
    switch (v->kind) {
    case TC_PVAL_INT:
        if (!type_is_integer(expected)) {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
            return 0;
        }
        if (v->int_unsigned_suffix && type_is_signed_integer(expected)) {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
            return 0;
        }
        if (v->int_negative && !type_is_signed_integer(expected)) {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
            return 0;
        }
        if (!int_literal_fits(s, v, expected)) {
            sem_diag_simple(s, TC_CE_LITERAL_RANGE, v->span);
            return 0;
        }
        return 1;
    case TC_PVAL_FLOAT:
        if (type_is_float(expected) && type_bits(s->an, expected) ==
                                           v->float_width) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
        return 0;
    case TC_PVAL_INF:
    case TC_PVAL_NAN:
        if (type_is_float(expected)) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
        return 0;
    case TC_PVAL_BOOL:
        if (type_is_bool(expected)) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
        return 0;
    case TC_PVAL_NULLPTR:
        if (type_is_ptr(expected) || type_is_fnptr(expected)) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
        return 0;
    case TC_PVAL_NAME:
    default:
        sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
        return 0;
    }
}

static int check_value_expected(Sem *s, const TcParseValue *v,
                                const TcType *expected,
                                const TcType **out_type) {
    if (out_type != NULL) {
        *out_type = NULL;
    }
    if (v->kind == TC_PVAL_NAME) {
        Ref ref = resolve_value(s, v);
        if (!ref.ok) {
            return 0;
        }
        if (expected != NULL && !tc_type_equal(ref.type, expected)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, v->base_span);
            return 0;
        }
        if (out_type != NULL) {
            *out_type = ref.type;
        }
        return 1;
    }
    if (!check_literal_expected(s, v, expected)) {
        return 0;
    }
    if (out_type != NULL) {
        *out_type = expected;
    }
    return 1;
}

static int check_arg_value(Sem *s, const TcParseArg *arg,
                           const TcType *expected) {
    if (arg->kind != TC_PARG_VALUE) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
        return 0;
    }
    return check_value_expected(s, &arg->value, expected, NULL);
}

static int check_operand(Sem *s, const TcParseArg *arg, const TcType *expected,
                         const TcType **out_type) {
    if (arg->kind != TC_PARG_VALUE || arg->name != NULL) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
        return 0;
    }
    return check_value_expected(s, &arg->value, expected, out_type);
}

static int type_arg_value(const TcParseArg *arg) {
    return arg->kind == TC_PARG_VALUE && arg->value.kind == TC_PVAL_NAME;
}

static const TcType *check_type_arg(Sem *s, const TcParseArg *arg) {
    TcParseType type;
    if (arg->kind == TC_PARG_TYPE) {
        TypeCtx ctx = {s->public_position, 0, 0, UINT32_MAX};
        return resolve_type(s, &arg->type, ctx);
    }
    if (!type_arg_value(arg)) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
        return NULL;
    }
    if (arg->value.nfields > 1 ||
        (arg->value.nfields == 1 && strcmp(arg->value.base, "Self") == 0)) {
        /* Type syntax has struct_name = IDENT | IDENT "." IDENT; Self is a
         * keyword, not an IDENT, so Self.S is not a type argument spelling. */
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
        return NULL;
    }
    memset(&type, 0, sizeof(type));
    type.kind = TC_PTYPE_NAME;
    type.base = arg->value.base;
    type.span = arg->value.base_span;
    if (arg->value.nfields == 1) {
        type.member = arg->value.fields[0];
        type.member_span = arg->value.field_spans[0];
    }
    {
        TypeCtx ctx = {s->public_position, 0, 0, UINT32_MAX};
        return resolve_type(s, &type, ctx);
    }
}

static int out_type_is_pointer_like(const TcType *t);

/* Construct the fnptr<sig> for a directly referenced function. */
static const TcType *function_pointer_type(TcAnalysis *an, const TcFuncSig *sig) {
    const TcType **params;
    size_t i;
    if (sig->ret == NULL) {
        return NULL;
    }
    params = (const TcType **)an_alloc(
        an, (sig->nparams == 0 ? 1u : sig->nparams) * sizeof(const TcType *));
    for (i = 0; i < sig->nparams; i++) {
        if (sig->params[i].type == NULL) {
            return NULL;
        }
        params[i] = sig->params[i].type;
    }
    return tc_type_fnptr(an->types, sig->ret, params, sig->nparams);
}

static TcMember *resolve_function_arg(Sem *s, const TcParseArg *arg,
                                      TcSpan *name_span) {
    const TcParseValue *v;
    TcMember *import;
    TcMember *target;
    if (arg->kind != TC_PARG_VALUE || arg->value.kind != TC_PVAL_NAME) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
        return NULL;
    }
    v = &arg->value;
    if (v->nfields != 1) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
        return NULL;
    }
    *name_span = v->field_spans[0];
    if (strcmp(v->base, "Self") == 0) {
        target = find_member(s->module, v->fields[0]);
    } else {
        import = find_member(s->module, v->base);
        if (import == NULL || import->kind != TC_MEM_IMPORT ||
            import->import_target == TC_MODULE_NO_TARGET) {
            sem_diag_simple(s, TC_CE_UNDEFINED_FUNCTION, *name_span);
            return NULL;
        }
        target = find_member_in(s->an, import->import_target, v->fields[0]);
    }
    if (target == NULL) {
        sem_diag_simple(s, TC_CE_UNDEFINED_FUNCTION, *name_span);
        return NULL;
    }
    /* A private imported member is PRIVATE_ACCESS even if its kind is not a
     * function; §4.11.3 forbids downgrading it to UNDEFINED_FUNCTION. */
    if (strcmp(v->base, "Self") != 0 && !target->is_public) {
        sem_diag_related(s, TC_CE_PRIVATE_ACCESS, *name_span, target->span);
        return NULL;
    }
    if (target->kind != TC_MEM_FUNC) {
        sem_diag_simple(s, TC_CE_UNDEFINED_FUNCTION, *name_span);
        return NULL;
    }
    return target;
}

/* Unsuffixed integer literal source type for cast/trunc is int64; `u` is
 * uint64 (standard §6.6.3). */
static int literal_source_integer(Sem *s, const TcParseValue *v,
                                  const TcType **out_type, int *out_negative) {
    const TcType *source;
    unsigned bits = 64;
    int negative = v->int_negative;
    source = tc_type_scalar(s->an->types, negative ? TC_TY_I64 : TC_TY_U64);
    if (!v->int_unsigned_suffix) {
        source = tc_type_scalar(s->an->types, TC_TY_I64);
        if (!value_fits_signed(v->int_magnitude, negative, bits)) {
            sem_diag_simple(s, TC_CE_LITERAL_RANGE, v->span);
            return 0;
        }
    } else {
        if (!value_fits_unsigned(v->int_magnitude, 0, bits)) {
            sem_diag_simple(s, TC_CE_LITERAL_RANGE, v->span);
            return 0;
        }
    }
    *out_type = source;
    if (out_negative != NULL) {
        *out_negative = negative;
    }
    return 1;
}

static int scalar_for_bits(Sem *s, unsigned bits, int is_signed,
                           const TcType **out) {
    TcTypeTag tag;
    switch (bits) {
    case 8: tag = is_signed ? TC_TY_I8 : TC_TY_U8; break;
    case 16: tag = is_signed ? TC_TY_I16 : TC_TY_U16; break;
    case 32: tag = is_signed ? TC_TY_I32 : TC_TY_U32; break;
    case 64: tag = is_signed ? TC_TY_I64 : TC_TY_U64; break;
    default: return 0;
    }
    *out = tc_type_scalar(s->an->types, tag);
    return 1;
}

static int bitcast_category_allowed(TcTypeCat src, TcTypeCat dst) {
    switch (src) {
    case TC_CAT_INT:
        return dst == TC_CAT_INT || dst == TC_CAT_FLOAT || dst == TC_CAT_PTR ||
               dst == TC_CAT_FNPTR;
    case TC_CAT_FLOAT:
        return dst == TC_CAT_INT || dst == TC_CAT_FLOAT;
    case TC_CAT_PTR:
        return dst == TC_CAT_PTR || dst == TC_CAT_INT;
    case TC_CAT_FNPTR:
        return dst == TC_CAT_FNPTR || dst == TC_CAT_INT;
    default:
        return 0;
    }
}

static const TcType *check_cast(Sem *s, const TcParseInstr *instr) {
    const TcType *target;
    TcTypeCat tc;
    const TcType *source = NULL;
    const TcParseArg *arg = &instr->args[1];
    if (instr->nargs != 2) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
        return NULL;
    }
    target = check_type_arg(s, &instr->args[0]);
    if (target == NULL) {
        return NULL;
    }
    tc = type_cat(target);
    if (tc == TC_CAT_VOID) {
        sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
        return NULL;
    }
    if (tc != TC_CAT_INT && tc != TC_CAT_FLOAT && tc != TC_CAT_BOOL) {
        sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[0].span);
        return NULL;
    }
    if (arg->kind == TC_PARG_VALUE && arg->value.kind != TC_PVAL_NAME) {
        switch (arg->value.kind) {
        case TC_PVAL_INT:
            if (!literal_source_integer(s, &arg->value, &source, NULL)) {
                return NULL;
            }
            break;
        case TC_PVAL_FLOAT:
            source = tc_type_scalar(s->an->types,
                                    arg->value.float_width == 32 ? TC_TY_F32
                                                                 : TC_TY_F64);
            break;
        case TC_PVAL_INF:
        case TC_PVAL_NAN:
            if (!type_is_float(target)) {
                sem_diag_simple(s, TC_CE_LITERAL_TYPE, arg->value.span);
                return NULL;
            }
            source = target;
            break;
        case TC_PVAL_BOOL:
            source = tc_type_scalar(s->an->types, TC_TY_BOOL);
            break;
        case TC_PVAL_NULLPTR:
        default:
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, arg->value.span);
            return NULL;
        }
        if (out_type_is_pointer_like(source)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, arg->value.span);
            return NULL;
        }
    } else {
        if (arg->kind != TC_PARG_VALUE || arg->name != NULL) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
            return NULL;
        }
        {
            Ref ref = resolve_value(s, &arg->value);
            if (!ref.ok) {
                return NULL;
            }
            source = ref.type;
        }
        if (type_cat(source) != TC_CAT_INT &&
            type_cat(source) != TC_CAT_FLOAT &&
            type_cat(source) != TC_CAT_BOOL) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, arg->span);
            return NULL;
        }
    }
    (void)source;
    return target;
}

static int out_type_is_pointer_like(const TcType *t) {
    return type_is_ptr(t) || type_is_fnptr(t) || type_is_struct(t);
}

static const TcType *check_trunc(Sem *s, const TcParseInstr *instr) {
    const TcType *target;
    const TcType *source = NULL;
    const TcParseArg *arg = &instr->args[1];
    unsigned target_bits;
    unsigned source_bits;
    if (instr->nargs != 2) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
        return NULL;
    }
    target = check_type_arg(s, &instr->args[0]);
    if (target == NULL) {
        return NULL;
    }
    if (type_is_void(target)) {
        sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
        return NULL;
    }
    if (!type_is_integer(target)) {
        sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[0].span);
        return NULL;
    }
    if (arg->kind == TC_PARG_VALUE && arg->value.kind != TC_PVAL_NAME) {
        if (arg->value.kind != TC_PVAL_INT) {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, arg->value.span);
            return NULL;
        }
        if (!literal_source_integer(s, &arg->value, &source, NULL)) {
            return NULL;
        }
    } else {
        Ref ref;
        if (arg->kind != TC_PARG_VALUE || arg->name != NULL) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
            return NULL;
        }
        ref = resolve_value(s, &arg->value);
        if (!ref.ok) {
            return NULL;
        }
        if (!type_is_integer(ref.type)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, arg->span);
            return NULL;
        }
        source = ref.type;
    }
    target_bits = type_bits(s->an, target);
    source_bits = type_bits(s->an, source);
    if (target_bits >= source_bits) {
        sem_diag_simple(s, TC_CE_TRUNC_WIDTH, instr->args[0].span);
        return NULL;
    }
    return target;
}

static const TcType *check_bitcast(Sem *s, const TcParseInstr *instr,
                                   int *unsafe_out) {
    const TcType *target;
    const TcType *source = NULL;
    const TcParseArg *arg = &instr->args[1];
    TcTypeCat sc;
    TcTypeCat tc;
    unsigned target_bits;
    unsigned source_bits;
    if (instr->nargs != 2) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
        return NULL;
    }
    target = check_type_arg(s, &instr->args[0]);
    if (target == NULL) {
        return NULL;
    }
    tc = type_cat(target);
    if (tc == TC_CAT_VOID) {
        sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
        return NULL;
    }
    if (arg->kind == TC_PARG_VALUE && arg->value.kind != TC_PVAL_NAME) {
        const TcParseValue *v = &arg->value;
        if (v->kind == TC_PVAL_NULLPTR) {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
            return NULL;
        }
        if (v->kind == TC_PVAL_INT) {
            unsigned width;
            if (tc == TC_CAT_INT) {
                width = type_bits(s->an, target);
            } else if (tc == TC_CAT_PTR || tc == TC_CAT_FNPTR) {
                width = s->an->W;
            } else if (tc == TC_CAT_FLOAT) {
                width = type_bits(s->an, target);
            } else {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, v->span);
                return NULL;
            }
            if (!scalar_for_bits(s, width, !v->int_unsigned_suffix, &source)) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, v->span);
                return NULL;
            }
            if (v->int_unsigned_suffix
                    ? !value_fits_unsigned(v->int_magnitude, 0, width)
                    : !value_fits_signed(v->int_magnitude, v->int_negative,
                                         width)) {
                sem_diag_simple(s, TC_CE_LITERAL_RANGE, v->span);
                return NULL;
            }
        } else if (v->kind == TC_PVAL_FLOAT) {
            source = tc_type_scalar(s->an->types,
                                    v->float_width == 32 ? TC_TY_F32 : TC_TY_F64);
        } else if (v->kind == TC_PVAL_INF || v->kind == TC_PVAL_NAN) {
            unsigned width;
            if (tc == TC_CAT_BOOL || tc == TC_CAT_STRUCT || tc == TC_CAT_VOID) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, v->span);
                return NULL;
            }
            width = tc == TC_CAT_FNPTR || tc == TC_CAT_PTR
                        ? s->an->W
                        : type_bits(s->an, target);
            if (width == 32) {
                source = tc_type_scalar(s->an->types, TC_TY_F32);
            } else if (width == 64) {
                source = tc_type_scalar(s->an->types, TC_TY_F64);
            } else {
                sem_diag_simple(s, TC_CE_BITCAST_WIDTH, v->span);
                return NULL;
            }
        } else if (v->kind == TC_PVAL_BOOL) {
            source = tc_type_scalar(s->an->types, TC_TY_BOOL);
        } else {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, v->span);
            return NULL;
        }
    } else {
        Ref ref;
        if (arg->kind != TC_PARG_VALUE || arg->name != NULL) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
            return NULL;
        }
        ref = resolve_value(s, &arg->value);
        if (!ref.ok) {
            return NULL;
        }
        source = ref.type;
    }
    sc = type_cat(source);
    if (!bitcast_category_allowed(sc, tc)) {
        sem_diag_simple(s, TC_CE_TYPE_MISMATCH, arg->span);
        return NULL;
    }
    if (sc == TC_CAT_FNPTR && tc == TC_CAT_FNPTR) {
        if (!tc_type_equal(source, target)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, arg->span);
            return NULL;
        }
    }
    source_bits = type_bits(s->an, source);
    target_bits = type_bits(s->an, target);
    if (source_bits == 0 || target_bits == 0 || source_bits != target_bits) {
        sem_diag_simple(s, TC_CE_BITCAST_WIDTH, arg->span);
        return NULL;
    }
    /* §9.2: pointer<->integer and fnptr<->integer bit reinterpretation is
     * unsafe; numeric, ptr<->ptr and same-signature fnptr<->fnptr are safe. */
    *unsafe_out = (sc == TC_CAT_INT &&
                   (tc == TC_CAT_PTR || tc == TC_CAT_FNPTR)) ||
                  ((sc == TC_CAT_PTR || sc == TC_CAT_FNPTR) &&
                   tc == TC_CAT_INT);
    return target;
}

/* ------------------------------------------------------------------------- */
/* Instruction shape and type checking.                                      */
/* ------------------------------------------------------------------------- */

static int is_arith3(TcOpcode op) {
    return op == TC_ADD || op == TC_SUB || op == TC_MUL;
}

static int is_divmod(TcOpcode op) { return op == TC_DIV || op == TC_MOD; }

static int is_negabs(TcOpcode op) { return op == TC_NEG || op == TC_ABS; }

static int is_compare(TcOpcode op) {
    return op == TC_EQ || op == TC_NE || op == TC_LT || op == TC_LE ||
           op == TC_GT || op == TC_GE;
}

static int is_bitop(TcOpcode op) {
    return op == TC_AND || op == TC_OR || op == TC_XOR || op == TC_NOT;
}

static TcSpan suffix_span(const TcParseInstr *instr) {
    return instr->has_suffix ? instr->suffix_span : instr->span;
}

static int check_mode(Sem *s, const TcParseInstr *instr, const TcType *type,
                      int *type_family_ok) {
    TcOpMode mode = instr->mode;
    TcTypeCat cat = type_cat(type);
    *type_family_ok = 1;
    /* Type-family validity is decided before suffix applicability so a
     * pointer/struct/bool operand is TYPE_MISMATCH even when the suffix would
     * also be misplaced. */
    if (is_arith3(instr->op) || is_divmod(instr->op) ||
        is_negabs(instr->op)) {
        if (cat != TC_CAT_INT && cat != TC_CAT_FLOAT) {
            *type_family_ok = 0;
            return 0;
        }
    } else if (instr->op == TC_SHL || instr->op == TC_SHR) {
        if (cat != TC_CAT_INT) {
            *type_family_ok = 0;
            return 0;
        }
    } else if (is_bitop(instr->op)) {
        if (cat != TC_CAT_INT && cat != TC_CAT_BOOL) {
            *type_family_ok = 0;
            return 0;
        }
    } else if (is_compare(instr->op)) {
        if (cat != TC_CAT_INT && cat != TC_CAT_FLOAT &&
            !(cat == TC_CAT_BOOL &&
              (instr->op == TC_EQ || instr->op == TC_NE))) {
            *type_family_ok = 0;
            return 0;
        }
    }
    if (mode == TC_NSW || mode == TC_NUW) {
        /* §10.9: type/sign mismatch is MODE; a legal integer base outside
         * @fast is ANNOTATION.  The more specific mismatch is checked first
         * when both conditions hold. */
        if (!type_is_integer(type)) {
            sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
            return 0;
        }
        if (type_is_signed_integer(type) && mode == TC_NUW) {
            sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
            return 0;
        }
        if (!type_is_signed_integer(type) && mode == TC_NSW) {
            sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
            return 0;
        }
        if (!s->fast) {
            sem_diag_simple(s, TC_CE_ANNOTATION, suffix_span(instr));
            return 0;
        }
    }
    if (is_arith3(instr->op)) {
        if (cat == TC_CAT_FLOAT) {
            if (mode == TC_STRICT || mode == TC_IEEE) {
                return 1;
            }
            sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
            return 0;
        }
        if (mode == TC_STRICT) {
            return 1;
        }
        if (mode == TC_WRAP) {
            if (!type_is_signed_integer(type)) {
                sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
                return 0;
            }
            return 1;
        }
        if (mode == TC_NSW || mode == TC_NUW) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
        return 0;
    }
    if (is_divmod(instr->op)) {
        if (cat == TC_CAT_FLOAT) {
            if (mode == TC_STRICT || mode == TC_IEEE) {
                return 1;
            }
        } else if (mode == TC_STRICT) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
        return 0;
    }
    if (is_negabs(instr->op)) {
        if (cat == TC_CAT_FLOAT && mode == TC_STRICT) {
            return 1;
        }
        if (cat == TC_CAT_INT) {
            if (mode == TC_STRICT) {
                return 1;
            }
            if (instr->op == TC_NEG && mode == TC_WRAP &&
                type_is_signed_integer(type)) {
                return 1;
            }
        }
        sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
        return 0;
    }
    if (instr->op == TC_SHL || instr->op == TC_SHR) {
        if (instr->op == TC_SHL) {
            if (mode == TC_STRICT || mode == TC_WRAP || mode == TC_NSW ||
                mode == TC_NUW) {
                return 1;
            }
        } else if (mode == TC_STRICT) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
        return 0;
    }
    if (is_bitop(instr->op) || is_compare(instr->op)) {
        if (mode == TC_STRICT) {
            return 1;
        }
        sem_diag_simple(s, TC_CE_MODE, suffix_span(instr));
        return 0;
    }
    return 1;
}

static const TcType *check_arith(Sem *s, const TcParseInstr *instr) {
    const TcType *type;
    size_t operands;
    int family_ok = 1;
    if (instr->nargs < 1) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
        return NULL;
    }
    type = check_type_arg(s, &instr->args[0]);
    if (type == NULL) {
        return NULL;
    }
    if (type_is_void(type)) {
        sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
        return NULL;
    }
    operands = instr->nargs - 1u;
    {
        size_t expected = (instr->op == TC_NEG || instr->op == TC_ABS ||
                           instr->op == TC_NOT)
                              ? 1u
                              : 2u;
        if (operands != expected) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
            return NULL;
        }
    }
    if (!check_mode(s, instr, type, &family_ok)) {
        if (!family_ok) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[0].span);
        }
        return NULL;
    }
    {
        size_t i;
        for (i = 1; i < instr->nargs; i++) {
            if (!check_operand(s, &instr->args[i], type, NULL)) {
                return NULL;
            }
        }
    }
    return is_compare(instr->op)
               ? tc_type_scalar(s->an->types, TC_TY_BOOL)
               : type;
}

static const TcType *check_instr(Sem *s, const TcParseInstr *instr,
                                 const TcType *expected_result,
                                 int implicit_result);

static const TcType *check_make(Sem *s, const TcParseInstr *instr) {
    const TcType *type;
    TcStructSlot *slot;
    const TcStructDef *def;
    size_t nfields = instr->nargs > 0 ? instr->nargs - 1u : 0;
    size_t i;
    int any_named = 0;
    int any_positional = 0;
    uint32_t site_id = ++s->an->next_site_id;
    if (instr->nargs < 1) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
        return NULL;
    }
    if (instr->args[0].name != NULL) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
        return NULL;
    }
    type = check_type_arg(s, &instr->args[0]);
    if (type == NULL) {
        return NULL;
    }
    if (type_is_void(type)) {
        sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
        return NULL;
    }
    if (!type_is_struct(type)) {
        sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[0].span);
        return NULL;
    }
    slot = struct_slot(s->an, type->struct_.struct_id);
    if (slot == NULL || slot->state != 2) {
        return NULL;
    }
    def = slot->def;
    for (i = 1; i < instr->nargs; i++) {
        if (instr->args[i].name != NULL) {
            any_named = 1;
        } else {
            any_positional = 1;
        }
    }
    if (any_named) {
        /* duplicate -> unknown first, also for mixed forms (standard §6.2.7/
         * §12.2 #10). */
        for (i = 1; i < instr->nargs; i++) {
            size_t g;
            if (instr->args[i].name == NULL) {
                continue;
            }
            for (g = 1; g < i; g++) {
                if (instr->args[g].name != NULL &&
                    strcmp(instr->args[g].name, instr->args[i].name) == 0) {
                    sem_diag(s, TC_CE_DUPLICATE_FIELD, instr->args[i].span,
                             &instr->args[g].span, TC_DIAG_ORIGIN_CONSTRUCT,
                             TC_DIAG_SITE_MAKE, TC_DIAG_PHASE_ARGUMENTS,
                             site_id);
                    return NULL;
                }
            }
        }
        for (i = 1; i < instr->nargs; i++) {
            if (instr->args[i].name != NULL &&
                find_struct_field(def, instr->args[i].name) == NULL) {
                sem_diag(s, TC_CE_UNKNOWN_FIELD, instr->args[i].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_MAKE,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
    }
    if (any_named && any_positional) {
        size_t positional = 0;
        int *covered = (int *)an_alloc(
            s->an, (def->nfields == 0 ? 1u : def->nfields) * sizeof(int));
        memset(covered, 0,
               (def->nfields == 0 ? 1u : def->nfields) * sizeof(int));
        for (i = 1; i < instr->nargs; i++) {
            if (instr->args[i].name == NULL) {
                positional++;
            }
        }
        if (positional > def->nfields) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
            return NULL;
        }
        for (i = 0; i < positional; i++) {
            covered[i] = 1;
        }
        for (i = 1; i < instr->nargs; i++) {
            const TcField *f;
            size_t fi;
            if (instr->args[i].name == NULL) {
                continue;
            }
            f = find_struct_field(def, instr->args[i].name);
            for (fi = 0; fi < def->nfields; fi++) {
                if (&def->fields[fi] == f) {
                    covered[fi] = 1;
                    break;
                }
            }
        }
        for (i = 0; i < def->nfields; i++) {
            if (!covered[i]) {
                sem_diag(s, TC_CE_MISSING_FIELD, instr->args[0].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_MAKE,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
        for (i = 1; i < instr->nargs; i++) {
            if (instr->args[i].name != NULL) {
                /* Mixed form: the named field is the locatable offender
                 * (standard §12.5.1 names a field/argument name). */
                sem_diag(s, TC_CE_FIELD_ORDER, instr->args[i].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_MAKE,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
        return NULL;
    }
    if (any_named) {
        if (instr->nargs - 1u < def->nfields) {
            sem_diag(s, TC_CE_MISSING_FIELD, instr->args[0].span, NULL,
                     TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_MAKE,
                     TC_DIAG_PHASE_ARGUMENTS, site_id);
            return NULL;
        }
        if (instr->nargs - 1u > def->nfields) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
            return NULL;
        }
        for (i = 1; i < instr->nargs; i++) {
            const TcField *f = find_struct_field(def, instr->args[i].name);
            if (f != &def->fields[i - 1u]) {
                sem_diag(s, TC_CE_FIELD_ORDER, instr->args[i].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_MAKE,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
        for (i = 1; i < instr->nargs; i++) {
            if (!check_arg_value(s, &instr->args[i],
                                 def->fields[i - 1u].type)) {
                return NULL;
            }
        }
        return type;
    }
    /* Positional fields. */
    if (nfields > def->nfields) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, instr->span);
        return NULL;
    }
    if (nfields < def->nfields) {
        sem_diag(s, TC_CE_MISSING_FIELD, instr->args[0].span, NULL,
                 TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_MAKE,
                 TC_DIAG_PHASE_ARGUMENTS, site_id);
        return NULL;
    }
    for (i = 0; i < nfields; i++) {
        if (!check_operand(s, &instr->args[i + 1u], def->fields[i].type,
                           NULL)) {
            return NULL;
        }
    }
    return type;
}

static const TcType *check_call_args(Sem *s, const TcParseInstr *instr,
                                     const TcFuncSig *sig, size_t first_arg,
                                     int indirect, uint32_t site_id,
                                     const TcParseFunc *callee) {
    size_t nargs = instr->nargs - first_arg;
    size_t i;
    int any_named = 0;
    int any_positional = 0;
    for (i = first_arg; i < instr->nargs; i++) {
        if (instr->args[i].name != NULL) {
            any_named = 1;
        } else {
            any_positional = 1;
        }
    }
    if (indirect && any_named) {
        for (i = first_arg; i < instr->nargs; i++) {
            if (instr->args[i].name != NULL) {
                sem_diag(s, TC_CE_UNKNOWN_ARGUMENT, instr->args[i].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
    }
    if (any_named) {
        /* duplicate -> unknown first, also for mixed forms (§12.2 #10). */
        for (i = first_arg; i < instr->nargs; i++) {
            size_t g;
            if (instr->args[i].name == NULL) {
                continue;
            }
            for (g = first_arg; g < i; g++) {
                if (instr->args[g].name != NULL &&
                    strcmp(instr->args[g].name, instr->args[i].name) == 0) {
                    sem_diag(s, TC_CE_DUPLICATE_ARGUMENT, instr->args[i].span,
                             &instr->args[g].span, TC_DIAG_ORIGIN_CONSTRUCT,
                             TC_DIAG_SITE_CALL, TC_DIAG_PHASE_ARGUMENTS,
                             site_id);
                    return NULL;
                }
            }
        }
        for (i = first_arg; i < instr->nargs; i++) {
            size_t p;
            int found = 0;
            if (instr->args[i].name == NULL) {
                continue;
            }
            for (p = 0; p < sig->nparams; p++) {
                if (strcmp(sig->params[p].name, instr->args[i].name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                sem_diag(s, TC_CE_UNKNOWN_ARGUMENT, instr->args[i].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
    }
    if (any_named && any_positional) {
        size_t positional = 0;
        int *covered = (int *)an_alloc(
            s->an, (sig->nparams == 0 ? 1u : sig->nparams) * sizeof(int));
        memset(covered, 0,
               (sig->nparams == 0 ? 1u : sig->nparams) * sizeof(int));
        for (i = first_arg; i < instr->nargs; i++) {
            if (instr->args[i].name == NULL) {
                positional++;
            }
        }
        if (positional > sig->nparams) {
            sem_diag(s, TC_CE_ARGUMENT_COUNT, instr->span, NULL,
                     TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                     TC_DIAG_PHASE_ARGUMENTS, site_id);
            return NULL;
        }
        for (i = 0; i < positional; i++) {
            covered[i] = 1;
        }
        for (i = first_arg; i < instr->nargs; i++) {
            size_t p;
            if (instr->args[i].name == NULL) {
                continue;
            }
            for (p = 0; p < sig->nparams; p++) {
                if (strcmp(sig->params[p].name, instr->args[i].name) == 0) {
                    covered[p] = 1;
                    break;
                }
            }
        }
        for (i = 0; i < sig->nparams; i++) {
            if (!covered[i]) {
                const TcSpan *related =
                    (callee != NULL && i < callee->nparams)
                        ? &callee->params[i].name_span
                        : NULL;
                sem_diag(s, TC_CE_MISSING_ARGUMENT, instr->span, related,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
        for (i = first_arg; i < instr->nargs; i++) {
            if (instr->args[i].name != NULL) {
                /* Mixed form: the named argument is the locatable offender
                 * (standard §12.5.1 names an argument name). */
                sem_diag(s, TC_CE_ARGUMENT_ORDER, instr->args[i].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
        return NULL;
    }
    if (any_named) {
        if (nargs < sig->nparams) {
            size_t missing = 0;
            size_t p;
            for (p = 0; p < sig->nparams; p++) {
                int present = 0;
                for (i = first_arg; i < instr->nargs; i++) {
                    if (instr->args[i].name != NULL &&
                        sig->params[p].name != NULL &&
                        strcmp(instr->args[i].name, sig->params[p].name) ==
                            0) {
                        present = 1;
                        break;
                    }
                }
                if (!present) {
                    missing = p;
                    break;
                }
            }
            {
                const TcSpan *related =
                    (callee != NULL && missing < callee->nparams)
                        ? &callee->params[missing].name_span
                        : NULL;
                sem_diag(s, TC_CE_MISSING_ARGUMENT, instr->span, related,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
            }
            return NULL;
        }
        if (nargs > sig->nparams) {
            sem_diag(s, TC_CE_ARGUMENT_COUNT, instr->span, NULL,
                     TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                     TC_DIAG_PHASE_ARGUMENTS, site_id);
            return NULL;
        }
        for (i = first_arg; i < instr->nargs; i++) {
            size_t p;
            for (p = 0; p < sig->nparams; p++) {
                if (strcmp(sig->params[p].name, instr->args[i].name) == 0) {
                    break;
                }
            }
            if (p != i - first_arg) {
                sem_diag(s, TC_CE_ARGUMENT_ORDER, instr->args[i].span, NULL,
                         TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                         TC_DIAG_PHASE_ARGUMENTS, site_id);
                return NULL;
            }
        }
        for (i = first_arg; i < instr->nargs; i++) {
            size_t p = i - first_arg;
            if (!check_arg_value(s, &instr->args[i], sig->params[p].type)) {
                return NULL;
            }
        }
        return sig->ret;
    }
    if (nargs != sig->nparams) {
        sem_diag(s, TC_CE_ARGUMENT_COUNT, instr->span, NULL,
                 TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                 TC_DIAG_PHASE_ARGUMENTS, site_id);
        return NULL;
    }
    for (i = 0; i < nargs; i++) {
        if (!check_operand(s, &instr->args[first_arg + i],
                           sig->params[i].type, NULL)) {
            return NULL;
        }
    }
    return sig->ret;
}

/* §8.8.2 call phases: result shape (void/`_`) precedes the argument checks;
 * the receiver/result type is checked only after them. */
static int check_call_result_form(Sem *s, const TcType *ret, int has_target,
                                  const TcType *expected, TcSpan tspan,
                                  uint32_t site_id) {
    if (ret == NULL) {
        return 1;
    }
    if (type_is_void(ret) && (has_target || expected != NULL)) {
        sem_diag(s, TC_CE_VOID, tspan, NULL, TC_DIAG_ORIGIN_CONSTRUCT,
                 TC_DIAG_SITE_CALL, TC_DIAG_PHASE_RESULT_SHAPE, site_id);
        return 0;
    }
    return 1;
}

static int check_call_receiver(Sem *s, const TcType *ret, int has_target,
                               const Ref *target, const TcType *expected,
                               TcSpan tspan, uint32_t site_id) {
    if (ret == NULL || type_is_void(ret)) {
        return 1;
    }
    if (expected != NULL && !tc_type_equal(ret, expected)) {
        sem_diag(s, TC_CE_TYPE_MISMATCH, tspan, NULL,
                 TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                 TC_DIAG_PHASE_RECEIVER, site_id);
        return 0;
    }
    if (has_target && !tc_type_equal(ret, target->type)) {
        sem_diag(s, TC_CE_TYPE_MISMATCH, tspan, NULL,
                 TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_CALL,
                 TC_DIAG_PHASE_RECEIVER, site_id);
        return 0;
    }
    return 1;
}

static int find_label_index(const Sem *s, const char *name) {
    size_t i;
    for (i = 0; i < s->nlabels; i++) {
        if (strcmp(s->labels[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int check_label_arg(Sem *s, const TcParseArg *arg) {
    if (arg->kind != TC_PARG_VALUE || arg->name != NULL ||
        arg->value.kind != TC_PVAL_NAME || arg->value.nfields != 0) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
        return 0;
    }
    if (find_label_index(s, arg->value.base) < 0) {
        sem_diag_simple(s, TC_CE_LABEL_NOT_FOUND, arg->value.base_span);
        return 0;
    }
    return 1;
}

static const TcType *check_instr(Sem *s, const TcParseInstr *instr,
                                 const TcType *expected_result,
                                 int implicit_result) {
    const TcType *result = NULL;
    Ref target;
    int has_target = 0;
    int result_checked = 0;
    int has_result = tc_opcode_has_result(instr->op);
    int explicit_target = !instr->dst_discard && instr->dst_base != NULL;
    TcSpan tspan = instr->span;
    s->mnemonic_span = tspan;
    memset(&target, 0, sizeof(target));
    switch (instr->op) {
    case TC_LOAD:
    case TC_STORE:
    case TC_MEMMOVE:
    case TC_PTRADD:
    case TC_PTRSUB:
        if (!check_unsafe_operation(s, tspan)) {
            return NULL;
        }
        break;
    default:
        break;
    }
    /* A var declaration target is the instruction's LHS even though the parse
     * IR keeps dst_base empty for an initializer; a no-result op there has no
     * legal `_` and is OPERAND_COUNT (standard §6.15.1). */
    if (!has_result && (implicit_result || explicit_target)) {
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
        return NULL;
    }
    if (has_result && explicit_target && instr->op != TC_CALL &&
        instr->op != TC_CALL_INDIRECT) {
        target = resolve_target(s, instr);
        if (!target.ok) {
            return NULL;
        }
        has_target = 1;
    }
    if (instr->op != TC_CALL && instr->op != TC_CALL_INDIRECT &&
        instr->op != TC_MAKE) {
        size_t ai;
        for (ai = 0; ai < instr->nargs; ai++) {
            if (instr->args[ai].name != NULL) {
                /* Named operands exist only for call arguments and make fields
                 * (standard §6.1 rules 8-9). */
                sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
                return NULL;
            }
        }
    }
    switch (instr->op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_NEG:
    case TC_ABS:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
    case TC_NOT:
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        result = check_arith(s, instr);
        break;
    case TC_CAST:
        result = check_cast(s, instr);
        break;
    case TC_TRUNC:
        result = check_trunc(s, instr);
        break;
    case TC_BITCAST: {
        int unsafe_cast = 0;
        result = check_bitcast(s, instr, &unsafe_cast);
        if (result != NULL && unsafe_cast &&
            !check_unsafe_operation(s, tspan)) {
            return NULL;
        }
        break;
    }
    case TC_LOAD: {
        const TcType *type;
        if (instr->nargs != 2) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        {
            const TcType *pointer = tc_type_ptr(s->an->types, type);
            if (pointer == NULL || !check_operand(s, &instr->args[1], pointer,
                                                   NULL)) {
                break;
            }
        }
        result = type;
        break;
    }
    case TC_STORE: {
        const TcType *type;
        if (instr->nargs != 3) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        if (!check_operand(s, &instr->args[1], tc_type_ptr(s->an->types, type),
                           NULL)) {
            break;
        }
        check_operand(s, &instr->args[2], type, NULL);
        break;
    }
    case TC_MEMMOVE: {
        const TcType *type;
        const TcType *usize;
        if (instr->nargs != 4) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        usize = tc_type_scalar(s->an->types, TC_TY_USIZE);
        if (!check_operand(s, &instr->args[1], tc_type_ptr(s->an->types, type),
                           NULL) ||
            !check_operand(s, &instr->args[2], tc_type_ptr(s->an->types, type),
                           NULL) ||
            !check_operand(s, &instr->args[3], usize, NULL)) {
            break;
        }
        break;
    }
    case TC_ADDR: {
        const TcType *type;
        Ref ref;
        if (instr->nargs != 2) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        if (instr->args[1].kind != TC_PARG_VALUE ||
            instr->args[1].value.kind != TC_PVAL_NAME) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
            break;
        }
        ref = resolve_value(s, &instr->args[1].value);
        if (!ref.ok) {
            break;
        }
        if (ref.is_const) {
            sem_diag_simple(s, TC_CE_ADDR_OF_CONST, ref.span);
            break;
        }
        if (!tc_type_equal(ref.type, type)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[1].span);
            break;
        }
        result = tc_type_ptr(s->an->types, type);
        break;
    }
    case TC_PTRADD:
    case TC_PTRSUB: {
        const TcType *type;
        const TcType *usize;
        if (instr->nargs != 3) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        usize = tc_type_scalar(s->an->types, TC_TY_USIZE);
        if (!check_operand(s, &instr->args[1], tc_type_ptr(s->an->types, type),
                           NULL) ||
            !check_operand(s, &instr->args[2], usize, NULL)) {
            break;
        }
        result = tc_type_ptr(s->an->types, type);
        break;
    }
    case TC_PTRDIFF: {
        const TcType *type;
        if (instr->nargs != 3) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        if (!check_operand(s, &instr->args[1], tc_type_ptr(s->an->types, type),
                           NULL) ||
            !check_operand(s, &instr->args[2], tc_type_ptr(s->an->types, type),
                           NULL)) {
            break;
        }
        result = tc_type_scalar(s->an->types, TC_TY_ISIZE);
        break;
    }
    case TC_PTREQ:
    case TC_PTRNE:
    case TC_PTRLT:
    case TC_PTRLE:
    case TC_PTRGT:
    case TC_PTRGE: {
        const TcType *type;
        const TcType *pointer;
        if (instr->nargs != 3) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        pointer = tc_type_ptr(s->an->types, type);
        if (pointer == NULL ||
            !check_operand(s, &instr->args[1], pointer, NULL) ||
            !check_operand(s, &instr->args[2], pointer, NULL)) {
            break;
        }
        result = tc_type_scalar(s->an->types, TC_TY_BOOL);
        break;
    }
    case TC_SIZEOF: {
        const TcType *type;
        if (instr->nargs != 1) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        result = tc_type_scalar(s->an->types, TC_TY_USIZE);
        break;
    }
    case TC_FUNCADDR: {
        TcSpan name_span;
        TcMember *func;
        const TcType *pointer;
        if (instr->nargs != 1) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        func = resolve_function_arg(s, &instr->args[0], &name_span);
        if (func == NULL) {
            break;
        }
        pointer = function_pointer_type(s->an, &func->sig);
        if (pointer == NULL) {
            break;
        }
        result = pointer;
        break;
    }
    case TC_CALL: {
        TcSpan name_span;
        TcMember *func;
        const TcType *ret;
        uint32_t site_id = ++s->an->next_site_id;
        if (instr->nargs < 1) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        if (instr->args[0].name != NULL) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        /* §8.8.2: target existence/visibility, then call safety, then the
         * call-site LHS and arguments. */
        func = resolve_function_arg(s, &instr->args[0], &name_span);
        if (func == NULL) {
            break;
        }
        if (!check_call_safety(s, func, tspan, site_id)) {
            break;
        }
        if (explicit_target) {
            target = resolve_target(s, instr);
            if (!target.ok) {
                break;
            }
            has_target = 1;
        }
        ret = func->sig.ret;
        if (!check_call_result_form(s, ret, has_target, expected_result,
                                    tspan, site_id)) {
            break;
        }
        if (check_call_args(s, instr, &func->sig, 1, 0, site_id,
                            func->func) == NULL) {
            break;
        }
        if (!check_call_receiver(s, ret, has_target, &target, expected_result,
                                 tspan, site_id)) {
            break;
        }
        result = ret;
        result_checked = 1;
        break;
    }
    case TC_CALL_INDIRECT: {
        const TcType *signature = NULL;
        const TcType *ret;
        uint32_t site_id = ++s->an->next_site_id;
        if (instr->nargs < 1) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        if (instr->args[0].kind != TC_PARG_VALUE ||
            instr->args[0].name != NULL) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        if (instr->args[0].value.kind == TC_PVAL_NAME) {
            Ref ref = resolve_value(s, &instr->args[0].value);
            if (!ref.ok) {
                break;
            }
            if (!type_is_fnptr(ref.type)) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH,
                                instr->args[0].span);
                break;
            }
            signature = ref.type;
        } else {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, instr->args[0].span);
            break;
        }
        if (!check_indirect_call_safety(s, tspan, site_id)) {
            break;
        }
        if (explicit_target) {
            target = resolve_target(s, instr);
            if (!target.ok) {
                break;
            }
            has_target = 1;
        }
        ret = signature->fnptr.ret;
        if (!check_call_result_form(s, ret, has_target, expected_result,
                                    tspan, site_id)) {
            break;
        }
        {
            TcFuncSig sig;
            size_t i;
            sig.ret = ret;
            sig.nparams = signature->fnptr.n;
            sig.params = (TcParam *)an_alloc(
                s->an, (sig.nparams == 0 ? 1u : sig.nparams) * sizeof(TcParam));
            memset(sig.params, 0,
                   (sig.nparams == 0 ? 1u : sig.nparams) * sizeof(TcParam));
            for (i = 0; i < sig.nparams; i++) {
                sig.params[i].type = signature->fnptr.params[i];
            }
            if (check_call_args(s, instr, &sig, 1, 1, site_id, NULL) == NULL) {
                break;
            }
        }
        if (!check_call_receiver(s, ret, has_target, &target, expected_result,
                                 tspan, site_id)) {
            break;
        }
        result = ret;
        result_checked = 1;
        break;
    }
    case TC_FUNCEQ:
    case TC_FUNCNE: {
        const TcType *type;
        if (instr->nargs != 3) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        if (!type_is_fnptr(type)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[0].span);
            break;
        }
        if (!check_operand(s, &instr->args[1], type, NULL) ||
            !check_operand(s, &instr->args[2], type, NULL)) {
            break;
        }
        result = tc_type_scalar(s->an->types, TC_TY_BOOL);
        break;
    }
    case TC_MAKE:
        result = check_make(s, instr);
        break;
    case TC_COPY: {
        const TcType *type;
        if (instr->nargs != 2) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        if (!check_operand(s, &instr->args[1], type, NULL)) {
            break;
        }
        result = type;
        break;
    }
    case TC_BR:
        if (instr->nargs != 1) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            s->cfg_incomplete = 1;
            break;
        }
        if (!check_label_arg(s, &instr->args[0])) {
            s->cfg_incomplete = 1;
        }
        break;
    case TC_BR_IF: {
        const TcType *boolean;
        if (instr->nargs != 2) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            s->cfg_incomplete = 1;
            break;
        }
        boolean = tc_type_scalar(s->an->types, TC_TY_BOOL);
        if (instr->args[0].kind != TC_PARG_VALUE ||
            instr->args[0].name != NULL) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
            break;
        }
        if (instr->args[0].value.kind == TC_PVAL_NAME) {
            Ref ref = resolve_value(s, &instr->args[0].value);
            if (!ref.ok) {
                break;
            }
            if (!tc_type_equal(ref.type, boolean)) {
                sem_diag_simple(s, TC_CE_CONDITION_TYPE,
                                instr->args[0].span);
                break;
            }
        } else if (!check_literal_expected(s, &instr->args[0].value,
                                           boolean)) {
            break;
        }
        if (!check_label_arg(s, &instr->args[1])) {
            s->cfg_incomplete = 1;
        }
        break;
    }
    case TC_SWITCH: {
        const TcType *cat_type = NULL;
        size_t i;
        if (instr->nargs < 2) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        if (instr->args[0].kind != TC_PARG_VALUE ||
            instr->args[0].name != NULL) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, s->mnemonic_span);
            break;
        }
        if (instr->args[0].value.kind == TC_PVAL_NAME) {
            Ref ref = resolve_value(s, &instr->args[0].value);
            if (!ref.ok) {
                break;
            }
            if (!type_is_integer(ref.type)) {
                sem_diag_simple(s, TC_CE_SWITCH_TYPE, instr->args[0].span);
                break;
            }
            cat_type = ref.type;
        } else if (instr->args[0].value.kind == TC_PVAL_INT) {
            const TcType *source = NULL;
            /* A switch selector has no declared expected type, so an integer
             * literal is typed by its own source type (int64/uint64) before
             * the integer check (standard §6.6.3/§12.2 #9). */
            if (!literal_source_integer(s, &instr->args[0].value, &source,
                                        NULL)) {
                break;
            }
            cat_type = source;
        } else {
            sem_diag_simple(s, TC_CE_LITERAL_TYPE, instr->args[0].span);
            break;
        }
        (void)cat_type;
        for (i = 1; i < instr->nargs; i++) {
            if (!check_label_arg(s, &instr->args[i])) {
                s->cfg_incomplete = 1;
                return NULL;
            }
        }
        break;
    }
    case TC_RETURN:
        if (s->func == NULL) {
            sem_diag_simple(s, TC_CE_RETURN_OUTSIDE_FUNCTION, tspan);
            break;
        }
        if (s->ret == NULL) {
            break;
        }
        if (type_is_void(s->ret)) {
            if (instr->nargs != 0) {
                sem_diag_simple(s, TC_CE_RETURN_FORM, tspan);
                break;
            }
        } else {
            if (instr->nargs != 1) {
                sem_diag_simple(s, TC_CE_RETURN_FORM, tspan);
                break;
            }
            if (instr->args[0].kind != TC_PARG_VALUE) {
                sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
                break;
            }
            if (instr->args[0].value.kind == TC_PVAL_NAME) {
                Ref ref = resolve_value(s, &instr->args[0].value);
                if (!ref.ok) {
                    break;
                }
                if (!tc_type_equal(ref.type, s->ret)) {
                    sem_diag_simple(s, TC_CE_RETURN_TYPE,
                                    instr->args[0].span);
                    break;
                }
            } else if (!check_literal_expected(s, &instr->args[0].value,
                                               s->ret)) {
                break;
            }
        }
        break;
    case TC_TRAP:
        if (instr->nargs != 0) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        break;
    case TC_READ: {
        const TcType *type;
        if (instr->nargs != 1) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        if (!tc_type_is_scalar(type)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[0].span);
            break;
        }
        result = type;
        break;
    }
    case TC_WRITE:
    case TC_WRITELN: {
        const TcType *type;
        if (instr->nargs != 2) {
            sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
            break;
        }
        type = check_type_arg(s, &instr->args[0]);
        if (type == NULL) {
            break;
        }
        if (type_is_void(type)) {
            sem_diag_simple(s, TC_CE_VOID, instr->args[0].span);
            break;
        }
        if (!tc_type_is_scalar(type)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, instr->args[0].span);
            break;
        }
        check_operand(s, &instr->args[1], type, NULL);
        break;
    }
    case TC_OPCODE_COUNT:
    default:
        sem_diag_simple(s, TC_CE_OPERAND_COUNT, tspan);
        break;
    }
    if (result != NULL && !result_checked) {
        if (type_is_void(result) && (expected_result != NULL || has_target)) {
            sem_diag_simple(s, TC_CE_VOID, tspan);
            return NULL;
        }
        if (expected_result != NULL &&
            !tc_type_equal(result, expected_result)) {
            sem_diag_simple(s, TC_CE_TYPE_MISMATCH, tspan);
            return NULL;
        }
        if (has_target) {
            if (type_is_void(result)) {
                sem_diag_simple(s, TC_CE_VOID, tspan);
                return NULL;
            }
            if (!tc_type_equal(result, target.type)) {
                sem_diag_simple(s, TC_CE_TYPE_MISMATCH, tspan);
                return NULL;
            }
        }
    }
    return result;
}

static void check_const_init(Sem *s, const TcParseConstInit *init,
                             const TcType *expected) {
    if (init->is_op) {
        check_instr(s, &init->op, expected, 0);
    } else if (init->value.kind == TC_PVAL_NAME) {
        check_value_expected(s, &init->value, expected, NULL);
    } else if (init->value.kind != TC_PVAL_NAME) {
        check_value_expected(s, &init->value, expected, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Units: local declarations and statement bodies.                           */
/* ------------------------------------------------------------------------- */

static void add_local(Sem *s, TcLocal local) {
    s->locals[s->nlocals++] = local;
}

static void build_locals(Sem *s, const TcParseFunc *func, const TcFuncSig *sig,
                         const TcParseStmt *body, size_t nbody) {
    size_t i;
    size_t cap = (func == NULL ? 0 : func->nparams) + nbody + 1u;
    size_t nlabels = 0;
    s->locals = (TcLocal *)an_alloc(s->an, cap * sizeof(TcLocal));
    s->nlocals = 0;
    for (i = 0; i < nbody; i++) {
        if (body[i].kind == TC_STMT_LABEL) {
            nlabels++;
        }
    }
    s->labels = (const char **)an_alloc(
        s->an, (nlabels == 0 ? 1u : nlabels) * sizeof(const char *));
    s->label_spans = (TcSpan *)an_alloc(
        s->an, (nlabels == 0 ? 1u : nlabels) * sizeof(TcSpan));
    s->nlabels = 0;
    for (i = 0; i < nbody; i++) {
        size_t j;
        if (body[i].kind != TC_STMT_LABEL) {
            continue;
        }
        for (j = 0; j < s->nlabels; j++) {
            if (strcmp(s->labels[j], body[i].label.name) == 0) {
                sem_diag_related(s, TC_CE_DUPLICATE_LABEL,
                                 body[i].label.name_span, s->label_spans[j]);
                s->cfg_incomplete = 1;
                break;
            }
        }
        s->labels[s->nlabels] = body[i].label.name;
        s->label_spans[s->nlabels] = body[i].label.name_span;
        s->nlabels++;
    }
    if (func != NULL && sig != NULL) {
        for (i = 0; i < func->nparams; i++) {
            TcLocal local;
            size_t j;
            memset(&local, 0, sizeof(local));
            local.name = func->params[i].name;
            local.span = func->params[i].name_span;
            local.kind = TC_LOC_PARAM;
            local.type = sig->params[i].type;
            local.visible_from = -1;
            for (j = 0; j < i; j++) {
                if (strcmp(func->params[j].name, func->params[i].name) == 0) {
                    sem_diag_related(s, TC_CE_DUPLICATE_PARAMETER,
                                     func->params[i].name_span,
                                     func->params[j].name_span);
                    break;
                }
            }
            add_local(s, local);
        }
    }
    for (i = 0; i < nbody; i++) {
        TcLocal local;
        const char *name = NULL;
        TcSpan span = body[i].span;
        TcLocalKind kind;
        size_t j;
        if (body[i].kind == TC_STMT_VAR) {
            name = body[i].var.name;
            span = body[i].var.name_span;
            kind = TC_LOC_VAR;
        } else if (body[i].kind == TC_STMT_CONST) {
            name = body[i].cnst.name;
            span = body[i].cnst.name_span;
            kind = TC_LOC_CONST;
        } else {
            continue;
        }
        for (j = 0; j < s->nlocals; j++) {
            if (strcmp(s->locals[j].name, name) == 0) {
                sem_diag_related(s, TC_CE_DUPLICATE_NAME, span,
                                 s->locals[j].span);
                break;
            }
        }
        memset(&local, 0, sizeof(local));
        local.name = name;
        local.span = span;
        local.kind = kind;
        local.type = NULL;
        local.visible_from = (int)i;
        add_local(s, local);
    }
}

static TcLocal *local_slot(Sem *s, const char *name, int visible_from) {
    size_t i;
    for (i = 0; i < s->nlocals; i++) {
        if (s->locals[i].visible_from == visible_from &&
            strcmp(s->locals[i].name, name) == 0) {
            return &s->locals[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Control-flow graph, reachability and definite initialization.            */
/*                                                                          */
/* Standard §5.7.1/§7.7: each program body and each function body is one     */
/* closed unit.  Nodes are source-level statements (instructions and        */
/* declarations); label declarations are block entries.  The DI sets are    */
/* the standard's maximum fixed point, initialized from U[n] for non-entry   */
/* reachable nodes and from the parameter set at the unit entry.            */
/* ------------------------------------------------------------------------- */

typedef struct {
    size_t nsucc;
    size_t npred;
    uint32_t *succs;
    uint32_t *preds;
    uint64_t *in;
    uint64_t *out;
} TcFlowNode;

typedef struct {
    TcFlowNode *nodes; /* index `end` (== nnodes) is the function/program end */
    size_t nnodes;
    uint32_t end;
    size_t *node_stmt;  /* node -> body statement index */
    size_t *stmt_node;  /* body statement index -> node or SIZE_MAX */
    uint32_t *label_node; /* label index -> node or UINT32_MAX */
    int *local_bit;     /* local index -> DI bit or -1 */
    int *stmt_bit;      /* var statement -> DI bit or -1 */
    size_t nbits;
    size_t nwords;
    uint64_t *universe;
    uint64_t *param_mask;
    uint8_t *reachable;
} TcFlow;

static void bits_set(uint64_t *bits, size_t bit) {
    bits[bit >> 6] |= (uint64_t)1u << (bit & 63u);
}

static int bits_get(const uint64_t *bits, size_t bit) {
    return (int)((bits[bit >> 6] >> (bit & 63u)) & 1u);
}

static void bits_fill(uint64_t *bits, size_t words, uint64_t value) {
    size_t i;
    for (i = 0; i < words; i++) {
        bits[i] = value;
    }
}

static void bits_copy(uint64_t *dst, const uint64_t *src, size_t words) {
    memcpy(dst, src, words * sizeof(uint64_t));
}

static int bits_equal(const uint64_t *a, const uint64_t *b, size_t words) {
    return memcmp(a, b, words * sizeof(uint64_t)) == 0;
}

static int flow_stmt_is_node(TcStmtKind kind) {
    return kind == TC_STMT_INSTR || kind == TC_STMT_VAR ||
           kind == TC_STMT_CONST || kind == TC_STMT_LABEL;
}

static int flow_label_arg(const TcParseArg *arg) {
    return arg->kind == TC_PARG_VALUE && arg->name == NULL &&
           arg->value.kind == TC_PVAL_NAME && arg->value.nfields == 0;
}

static uint32_t flow_label_target(const TcFlow *flow, const Sem *s,
                                  const TcParseArg *arg) {
    int label;
    if (!flow_label_arg(arg)) {
        return UINT32_MAX;
    }
    label = find_label_index(s, arg->value.base);
    if (label < 0) {
        return UINT32_MAX;
    }
    return flow->label_node[(size_t)label];
}

static uint32_t flow_next_node(const TcFlow *flow, size_t node) {
    return node + 1u < flow->nnodes ? (uint32_t)(node + 1u) : flow->end;
}

/* Duplicate switch targets denote one CFG edge (standard §7.7.1 item 3):
 * every listed target is an edge, but repeated labels add no new successor. */
static int flow_switch_seen(const TcFlow *flow, const Sem *s,
                            const TcParseInstr *instr, size_t upto) {
    uint32_t target = flow_label_target(flow, s, &instr->args[upto]);
    size_t i;
    if (target == UINT32_MAX) {
        return 0;
    }
    for (i = 1; i < upto; i++) {
        if (flow_label_target(flow, s, &instr->args[i]) == target) {
            return 1;
        }
    }
    return 0;
}

static size_t flow_succ_count(const TcFlow *flow, const Sem *s,
                              const TcParseStmt *body, size_t node) {
    const TcParseStmt *stmt = &body[flow->node_stmt[node]];
    size_t i;
    if (stmt->kind != TC_STMT_INSTR) {
        return 1;
    }
    switch (stmt->instr.op) {
    case TC_BR:
        return flow_label_target(flow, s, &stmt->instr.args[0]) == UINT32_MAX
                   ? 0u
                   : 1u;
    case TC_BR_IF:
        return (flow_label_target(flow, s, &stmt->instr.args[1]) == UINT32_MAX
                    ? 0u
                    : 1u) +
               1u;
    case TC_SWITCH: {
        size_t count = 0;
        for (i = 1; i < stmt->instr.nargs; i++) {
            if (flow_label_target(flow, s, &stmt->instr.args[i]) !=
                    UINT32_MAX &&
                !flow_switch_seen(flow, s, &stmt->instr, i)) {
                count++;
            }
        }
        return count;
    }
    case TC_RETURN:
    case TC_TRAP:
        return 0;
    default:
        return 1;
    }
}

static void flow_fill_succs(const TcFlow *flow, const Sem *s,
                            const TcParseStmt *body, size_t node) {
    const TcParseStmt *stmt = &body[flow->node_stmt[node]];
    TcFlowNode *n = &flow->nodes[node];
    size_t i;
    n->nsucc = 0;
    if (stmt->kind != TC_STMT_INSTR) {
        n->succs[n->nsucc++] = flow_next_node(flow, node);
        return;
    }
    switch (stmt->instr.op) {
    case TC_BR: {
        uint32_t target = flow_label_target(flow, s, &stmt->instr.args[0]);
        if (target != UINT32_MAX) {
            n->succs[n->nsucc++] = target;
        }
        return;
    }
    case TC_BR_IF: {
        uint32_t target = flow_label_target(flow, s, &stmt->instr.args[1]);
        if (target != UINT32_MAX) {
            n->succs[n->nsucc++] = target;
        }
        n->succs[n->nsucc++] = flow_next_node(flow, node);
        return;
    }
    case TC_SWITCH:
        for (i = 1; i < stmt->instr.nargs; i++) {
            uint32_t target =
                flow_label_target(flow, s, &stmt->instr.args[i]);
            if (target != UINT32_MAX &&
                !flow_switch_seen(flow, s, &stmt->instr, i)) {
                n->succs[n->nsucc++] = target;
            }
        }
        return;
    case TC_RETURN:
    case TC_TRAP:
        return;
    default:
        n->succs[n->nsucc++] = flow_next_node(flow, node);
        return;
    }
}

/* DI read: a var base (including a field path base) must be initialized on
 * every reachable path.  Parameters/consts and module statics do not carry a
 * DI bit (standard §5.7.1 U[n]). */
static void flow_check_value(Sem *s, const TcFlow *flow, size_t node,
                             const TcParseValue *value) {
    TcLocal *local;
    int bit;
    if (value->kind != TC_PVAL_NAME) {
        return;
    }
    if (strcmp(value->base, "Self") == 0) {
        return;
    }
    {
        TcMember *member = find_member(s->module, value->base);
        if (member != NULL && member->kind == TC_MEM_IMPORT) {
            return;
        }
    }
    local = lookup_local(s, value->base);
    if (local == NULL || local->kind != TC_LOC_VAR) {
        return;
    }
    bit = flow->local_bit[(size_t)(local - s->locals)];
    if (bit < 0) {
        return;
    }
    if (!bits_get(flow->nodes[node].in, (size_t)bit)) {
        sem_diag_flow(s, TC_CE_UNINITIALIZED, value->base_span);
    }
}

static void flow_check_target(Sem *s, const TcFlow *flow, size_t node,
                              const TcParseInstr *instr) {
    TcLocal *local;
    int bit;
    if (instr->dst_discard || instr->dst_base == NULL) {
        return;
    }
    if (strcmp(instr->dst_base, "Self") == 0) {
        return;
    }
    {
        TcMember *member = find_member(s->module, instr->dst_base);
        if (member != NULL && member->kind == TC_MEM_IMPORT) {
            return;
        }
    }
    local = lookup_local(s, instr->dst_base);
    if (local == NULL || local->kind != TC_LOC_VAR) {
        return;
    }
    bit = flow->local_bit[(size_t)(local - s->locals)];
    if (bit < 0) {
        return;
    }
    if (!bits_get(flow->nodes[node].in, (size_t)bit)) {
        sem_diag_flow(s, TC_CE_UNINITIALIZED, instr->dst_base_span);
    }
}

static void flow_check_reads(Sem *s, const TcFlow *flow, size_t node,
                             const TcParseInstr *instr) {
    size_t i;
    size_t first = 0;
    switch (instr->op) {
    case TC_BR:
    case TC_ADDR:
        return;
    case TC_BR_IF:
    case TC_SWITCH:
        /* args[0] is the condition/selector; the remaining args are labels. */
        for (i = 0; i < instr->nargs && i < 1u; i++) {
            if (instr->args[i].kind == TC_PARG_VALUE) {
                flow_check_value(s, flow, node, &instr->args[i].value);
            }
        }
        return;
    case TC_CALL:
    case TC_FUNCADDR:
        first = 1;
        break;
    default:
        break;
    }
    for (i = first; i < instr->nargs; i++) {
        if (instr->args[i].kind == TC_PARG_VALUE) {
            flow_check_value(s, flow, node, &instr->args[i].value);
        }
    }
}

static void flow_report_missing_return(Sem *s) {
    TcSpan related;
    if (s->func == NULL || s->ret == NULL || type_is_void(s->ret)) {
        return;
    }
    related = s->func->end_span;
    sem_diag(s, TC_CE_MISSING_RETURN, s->func->ret.span, &related,
             TC_DIAG_ORIGIN_CONSTRUCT, TC_DIAG_SITE_NONE,
             TC_DIAG_PHASE_NONE, 0);
}

static void check_unit_flow(Sem *s, const TcParseStmt *body, size_t nbody) {
    TcFlow flow;
    size_t i;
    size_t nnodes = 0;
    size_t total_succs = 0;
    size_t *fill;
    uint32_t *succ_pool;
    uint32_t *pred_pool;
    uint32_t *queue;
    size_t top;
    size_t nbits = 0;
    uint64_t *scratch_in;
    uint64_t *scratch_out;

    memset(&flow, 0, sizeof(flow));
    if (s->cfg_incomplete) {
        return;
    }
    for (i = 0; i < nbody; i++) {
        if (flow_stmt_is_node(body[i].kind)) {
            nnodes++;
        }
    }
    if (nnodes == 0) {
        /* Empty (or only lexical-marker) body: no entry node.  A non-void
         * function has no path that returns (standard §7.7.3). */
        if (s->func != NULL && s->ret != NULL && !type_is_void(s->ret)) {
            flow_report_missing_return(s);
        }
        return;
    }
    flow.nnodes = nnodes;
    flow.end = (uint32_t)nnodes;
    flow.nodes = (TcFlowNode *)an_alloc(
        s->an, (nnodes + 1u) * sizeof(TcFlowNode));
    flow.node_stmt = (size_t *)an_alloc(s->an, nnodes * sizeof(size_t));
    flow.stmt_node = (size_t *)an_alloc(
        s->an, (nbody == 0 ? 1u : nbody) * sizeof(size_t));
    flow.label_node = (uint32_t *)an_alloc(
        s->an, (s->nlabels == 0 ? 1u : s->nlabels) * sizeof(uint32_t));
    for (i = 0; i < nbody; i++) {
        flow.stmt_node[i] = SIZE_MAX;
    }
    for (i = 0; i < s->nlabels; i++) {
        flow.label_node[i] = UINT32_MAX;
    }
    {
        size_t node = 0;
        for (i = 0; i < nbody; i++) {
            if (!flow_stmt_is_node(body[i].kind)) {
                continue;
            }
            flow.node_stmt[node] = i;
            flow.stmt_node[i] = node;
            if (body[i].kind == TC_STMT_LABEL) {
                int label = find_label_index(s, body[i].label.name);
                if (label >= 0 && flow.label_node[(size_t)label] ==
                                      UINT32_MAX) {
                    flow.label_node[(size_t)label] = (uint32_t)node;
                }
            }
            node++;
        }
    }
    for (i = 0; i < nnodes; i++) {
        total_succs += flow_succ_count(&flow, s, body, i);
    }
    succ_pool = (uint32_t *)an_alloc(
        s->an, (total_succs == 0 ? 1u : total_succs) * sizeof(uint32_t));
    {
        size_t offset = 0;
        for (i = 0; i < nnodes; i++) {
            flow.nodes[i].succs = succ_pool + offset;
            flow_fill_succs(&flow, s, body, i);
            offset += flow.nodes[i].nsucc;
        }
    }
    for (i = 0; i < nnodes; i++) {
        size_t k;
        for (k = 0; k < flow.nodes[i].nsucc; k++) {
            flow.nodes[flow.nodes[i].succs[k]].npred++;
        }
    }
    pred_pool = (uint32_t *)an_alloc(
        s->an, (total_succs == 0 ? 1u : total_succs) * sizeof(uint32_t));
    fill = (size_t *)an_alloc(s->an, (nnodes + 1u) * sizeof(size_t));
    for (i = 0; i <= nnodes; i++) {
        fill[i] = 0;
    }
    /* Assign predecessor slices after the total counts are known. */
    {
        size_t offset = 0;
        for (i = 0; i <= nnodes; i++) {
            flow.nodes[i].preds = pred_pool + offset;
            offset += flow.nodes[i].npred;
        }
    }
    for (i = 0; i < nnodes; i++) {
        size_t k;
        for (k = 0; k < flow.nodes[i].nsucc; k++) {
            uint32_t target = flow.nodes[i].succs[k];
            flow.nodes[target].preds[fill[target]++] = (uint32_t)i;
        }
    }

    flow.reachable = (uint8_t *)an_alloc(s->an, nnodes + 1u);
    queue = (uint32_t *)an_alloc(s->an, (nnodes + 1u) * sizeof(uint32_t));
    top = 0;
    flow.reachable[0] = 1;
    queue[top++] = 0;
    while (top > 0) {
        uint32_t node = queue[--top];
        size_t k;
        for (k = 0; k < flow.nodes[node].nsucc; k++) {
            uint32_t target = flow.nodes[node].succs[k];
            if (!flow.reachable[target]) {
                flow.reachable[target] = 1;
                queue[top++] = target;
            }
        }
    }

    for (i = 0; i < s->nlocals; i++) {
        if (s->locals[i].kind == TC_LOC_PARAM ||
            s->locals[i].kind == TC_LOC_VAR) {
            nbits++;
        }
    }
    flow.nbits = nbits;
    flow.nwords = (nbits + 63u) / 64u;
    if (flow.nwords == 0) {
        flow.nwords = 1;
    }
    flow.universe = (uint64_t *)an_alloc(
        s->an, flow.nwords * sizeof(uint64_t));
    flow.param_mask = (uint64_t *)an_alloc(
        s->an, flow.nwords * sizeof(uint64_t));
    bits_fill(flow.universe, flow.nwords, 0);
    bits_fill(flow.param_mask, flow.nwords, 0);
    flow.local_bit = (int *)an_alloc(
        s->an, (s->nlocals == 0 ? 1u : s->nlocals) * sizeof(int));
    flow.stmt_bit = (int *)an_alloc(
        s->an, (nbody == 0 ? 1u : nbody) * sizeof(int));
    for (i = 0; i < s->nlocals; i++) {
        flow.local_bit[i] = -1;
    }
    for (i = 0; i < nbody; i++) {
        flow.stmt_bit[i] = -1;
    }
    {
        size_t bit = 0;
        for (i = 0; i < s->nlocals; i++) {
            if (s->locals[i].kind != TC_LOC_PARAM &&
                s->locals[i].kind != TC_LOC_VAR) {
                continue;
            }
            flow.local_bit[i] = (int)bit;
            bits_set(flow.universe, bit);
            if (s->locals[i].kind == TC_LOC_PARAM) {
                bits_set(flow.param_mask, bit);
            } else if (s->locals[i].visible_from >= 0 &&
                       s->locals[i].visible_from < (int)nbody &&
                       flow.stmt_bit[s->locals[i].visible_from] < 0) {
                flow.stmt_bit[s->locals[i].visible_from] = (int)bit;
            }
            bit++;
        }
    }
    {
        uint64_t *in_pool = (uint64_t *)an_alloc(
            s->an, (nnodes + 1u) * flow.nwords * sizeof(uint64_t));
        uint64_t *out_pool = (uint64_t *)an_alloc(
            s->an, (nnodes + 1u) * flow.nwords * sizeof(uint64_t));
        for (i = 0; i <= nnodes; i++) {
            flow.nodes[i].in = in_pool + i * flow.nwords;
            flow.nodes[i].out = out_pool + i * flow.nwords;
            bits_fill(flow.nodes[i].in, flow.nwords, 0);
            bits_fill(flow.nodes[i].out, flow.nwords, 0);
        }
    }
    for (i = 0; i < nnodes; i++) {
        if (!flow.reachable[i]) {
            continue;
        }
        if (i == 0) {
            bits_fill(flow.nodes[i].in, flow.nwords, 0);
            bits_copy(flow.nodes[i].in, flow.param_mask, flow.nwords);
        } else {
            bits_fill(flow.nodes[i].in, flow.nwords, ~(uint64_t)0);
        }
        bits_fill(flow.nodes[i].out, flow.nwords, ~(uint64_t)0);
    }
    scratch_in = (uint64_t *)an_alloc(
        s->an, flow.nwords * sizeof(uint64_t));
    scratch_out = (uint64_t *)an_alloc(
        s->an, flow.nwords * sizeof(uint64_t));
    for (;;) {
        int changed = 0;
        for (i = 0; i < nnodes; i++) {
            size_t k;
            if (!flow.reachable[i]) {
                continue;
            }
            if (i == 0) {
                bits_copy(scratch_in, flow.param_mask, flow.nwords);
            } else {
                bits_fill(scratch_in, flow.nwords, ~(uint64_t)0);
                for (k = 0; k < flow.nodes[i].npred; k++) {
                    uint32_t pred = flow.nodes[i].preds[k];
                    size_t w;
                    if (!flow.reachable[pred]) {
                        continue;
                    }
                    for (w = 0; w < flow.nwords; w++) {
                        scratch_in[w] &= flow.nodes[pred].out[w];
                    }
                }
            }
            bits_copy(scratch_out, scratch_in, flow.nwords);
            if (body[flow.node_stmt[i]].kind == TC_STMT_VAR) {
                int bit = flow.stmt_bit[flow.node_stmt[i]];
                if (bit >= 0) {
                    bits_set(scratch_out, (size_t)bit);
                }
            }
            if (!bits_equal(flow.nodes[i].in, scratch_in, flow.nwords) ||
                !bits_equal(flow.nodes[i].out, scratch_out, flow.nwords)) {
                bits_copy(flow.nodes[i].in, scratch_in, flow.nwords);
                bits_copy(flow.nodes[i].out, scratch_out, flow.nwords);
                changed = 1;
            }
        }
        if (!changed) {
            break;
        }
    }
    for (i = 0; i < nnodes; i++) {
        size_t si = flow.node_stmt[i];
        if (!flow.reachable[i]) {
            continue;
        }
        s->stmt_index = (int)si;
        if (body[si].kind == TC_STMT_INSTR) {
            flow_check_target(s, &flow, i, &body[si].instr);
            flow_check_reads(s, &flow, i, &body[si].instr);
        } else if (body[si].kind == TC_STMT_VAR) {
            flow_check_reads(s, &flow, i, &body[si].var.init);
        }
    }
    if (s->func != NULL && s->ret != NULL && !type_is_void(s->ret) &&
        flow.reachable[flow.end]) {
        flow_report_missing_return(s);
    }
}

static void check_body(Sem *s, const TcParseFunc *func, const TcFuncSig *sig,
                       const TcParseStmt *body, size_t nbody) {
    size_t i;
    build_locals(s, func, sig, body, nbody);
    for (i = 0; i < nbody; i++) {
        s->stmt_index = (int)i;
        s->module_const_init = 0;
        s->skip_safety = body[i].kind == TC_STMT_CONST;
        s->unsafe_context = s->fn_unsafe || s->fast ||
                            body[i].unsafe_depth > 0;
        switch (body[i].kind) {
        case TC_STMT_INSTR:
            check_instr(s, &body[i].instr, NULL, 0);
            break;
        case TC_STMT_VAR: {
            const TcType *declared =
                resolve_value_type(s, &body[i].var.type, 0);
            TcLocal *slot = local_slot(s, body[i].var.name, (int)i);
            if (slot != NULL) {
                slot->type = declared;
            }
            if (declared != NULL) {
                s->declaring_name = body[i].var.name;
                check_instr(s, &body[i].var.init, declared, 1);
                s->declaring_name = NULL;
            }
            break;
        }
        case TC_STMT_CONST: {
            const TcType *declared =
                resolve_value_type(s, &body[i].cnst.type, 0);
            TcLocal *slot = local_slot(s, body[i].cnst.name, (int)i);
            if (slot != NULL) {
                slot->type = declared;
            }
            if (declared != NULL) {
                s->declaring_name = body[i].cnst.name;
                check_const_init(s, &body[i].cnst.init, declared);
                s->declaring_name = NULL;
            }
            break;
        }
        case TC_STMT_UNSAFE_BEGIN:
            /* @safe forbids every unsafe block, including an empty one; this
             * violation outranks UNSAFE_* and FAST_CALL (standard §9.3/§9.8). */
            if (s->fn_safe) {
                sem_diag_flow(s, TC_CE_SAFE_VIOLATION, body[i].span);
            }
            break;
        case TC_STMT_LABEL:
        case TC_STMT_UNSAFE_END:
            break;
        }
    }
    s->skip_safety = 0;
    s->unsafe_context = 0;
    check_unit_flow(s, body, nbody);
}

static TcMember *value_member_at(TcModuleSem *module, size_t decl_index) {
    size_t i;
    for (i = 0; i < module->nmembers; i++) {
        TcMember *m = &module->members[i];
        if ((m->kind == TC_MEM_CONST || m->kind == TC_MEM_STATIC) &&
            m->decl_index == decl_index) {
            return m;
        }
    }
    return NULL;
}

static void check_module_values(Sem *s) {
    TcParseModule *parse = s->module->info->parse;
    size_t i;
    for (i = 0; i < parse->nconsts; i++) {
        TcMember *m = value_member_at(s->module, i);
        if (m == NULL || m->type == NULL) {
            continue;
        }
        s->module_const_init = 1;
        s->skip_safety = 1;
        s->const_decl_index = i;
        s->public_position = m->is_public;
        s->stmt_index = 0;
        s->declaring_name = m->name;
        check_const_init(s, &parse->consts[i].init, m->type);
        s->declaring_name = NULL;
    }
}

static void check_all_units(TcAnalysis *an) {
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        TcParseModule *parse = module->info->parse;
        Sem s;
        size_t i;
        memset(&s, 0, sizeof(s));
        s.an = an;
        s.module_index = (uint32_t)mi;
        s.module = module;
        s.is_program = parse->is_program;
        s.ret = NULL;
        check_module_values(&s);
        if (parse->is_program) {
            memset(&s, 0, sizeof(s));
            s.an = an;
            s.module_index = (uint32_t)mi;
            s.module = module;
            s.is_program = 1;
            s.func = NULL;
            s.ret = NULL;
            check_body(&s, NULL, NULL, parse->body, parse->nbody);
        } else {
            for (i = 0; i < module->nmembers; i++) {
                TcMember *m = &module->members[i];
                Sem fs;
                if (m->kind != TC_MEM_FUNC) {
                    continue;
                }
                memset(&fs, 0, sizeof(fs));
                fs.an = an;
                fs.module_index = (uint32_t)mi;
                fs.module = module;
                fs.func = m->func;
                fs.ret = m->sig.ret;
                /* A public signature is a public position; the body is not.
                 * PRIVATE_TYPE applies to public signatures, public struct
                 * fields and public const/static declarations/initializers
                 * (standard §4.6). */
                fs.public_position = 0;
                parse_func_effects(m->func, &fs.fn_unsafe, &fs.fast,
                                   &fs.fn_safe);
                check_body(&fs, m->func, &m->sig, m->func->body,
                           m->func->nbody);
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Analysis entry point.                                                     */
/* ------------------------------------------------------------------------- */

static void set_result_diag(TcAnalysis *an, const TcDiagnostic *diag) {
    TcAnalysisDiag *out = &an->result_diag;
    if (diag == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->code = diag->code;
    out->stage = diag->stage;
    out->span = (TcSpan){0};
    out->span.file = an_strdup(an, diag->primary.file);
    out->span.line = diag->primary.line;
    out->span.column = diag->primary.column;
    if (diag->has_related) {
        out->has_related = 1;
        out->related = (TcSpan){0};
        out->related.file = an_strdup(an, diag->related.file);
        out->related.line = diag->related.line;
        out->related.column = diag->related.column;
    }
}

static void set_lexsyn_diag(TcAnalysis *an, const TcFrontendDiag *diag) {
    TcAnalysisDiag *out = &an->result_diag;
    memset(out, 0, sizeof(*out));
    out->code = diag->code;
    out->stage = diag->stage;
    out->span.file = an_strdup(an, diag->span.file);
    out->span.line = diag->span.line;
    out->span.column = diag->span.column;
    if (diag->has_related) {
        out->has_related = 1;
        out->related.file = an_strdup(an, diag->related.file);
        out->related.line = diag->related.line;
        out->related.column = diag->related.column;
    }
}

TcAnalysis *tc_analyze(const char *root_path, unsigned addr_width,
                       const char *const *include_roots,
                       size_t ninclude_roots, TcAnalysisStatus *out_status,
                       TcAnalysisDiag *out_diag, TcImplError *out_impl) {
    const TcAllocator *alloc = tc_allocator_libc();
    TcAnalysis *an;
    TcModuleGraph *graph;
    size_t total_structs = 0;
    size_t mi;
    int jumped;
    if (out_status == NULL || out_diag == NULL || out_impl == NULL ||
        root_path == NULL || (addr_width != 32 && addr_width != 64)) {
        if (out_impl != NULL) {
            *out_impl = TC_IMPL_USAGE;
        }
        return NULL;
    }
    memset(out_diag, 0, sizeof(*out_diag));
    *out_impl = TC_IMPL_OK;
    graph = tc_module_graph_load(root_path, include_roots, ninclude_roots);
    if (graph == NULL) {
        *out_status = TC_ANALYSIS_IMPL;
        *out_impl = TC_IMPL_OOM;
        return NULL;
    }
    an = (TcAnalysis *)alloc->alloc(alloc->ctx, sizeof(TcAnalysis));
    if (an == NULL) {
        tc_module_graph_destroy(graph);
        *out_status = TC_ANALYSIS_IMPL;
        *out_impl = TC_IMPL_OOM;
        return NULL;
    }
    memset(an, 0, sizeof(*an));
    an->alloc = alloc;
    tc_arena_init(&an->arena, alloc);
    an->W = addr_width;
    an->graph = graph;
    an->diags = tc_diag_buffer_create(alloc);
    an->types = tc_type_table_create(alloc);
    if (an->diags == NULL || an->types == NULL) {
        tc_analysis_destroy(an);
        *out_status = TC_ANALYSIS_IMPL;
        *out_impl = TC_IMPL_OOM;
        return NULL;
    }
    jumped = setjmp(an->env);
    if (jumped != 0) {
        *out_status = TC_ANALYSIS_IMPL;
        *out_impl = an->impl != TC_IMPL_OK ? an->impl : TC_IMPL_INTERNAL;
        return an;
    }
    if (graph->impl != TC_IMPL_OK) {
        *out_status = TC_ANALYSIS_IMPL;
        *out_impl = graph->impl;
        return an;
    }
    if (graph->has_lexsyn) {
        set_lexsyn_diag(an, &graph->lexsyn);
        *out_status = TC_ANALYSIS_DIAGNOSTIC;
        *out_diag = an->result_diag;
        return an;
    }
    if (graph->root_not_program) {
        *out_status = TC_ANALYSIS_ROOT_NOT_PROGRAM;
        *out_impl = TC_IMPL_USAGE;
        return an;
    }
    an->nmodules = graph->nmodules;
    an->modules = (TcModuleSem *)an_alloc(
        an, (an->nmodules == 0 ? 1u : an->nmodules) * sizeof(TcModuleSem));
    memset(an->modules, 0,
           (an->nmodules == 0 ? 1u : an->nmodules) * sizeof(TcModuleSem));
    for (mi = 0; mi < an->nmodules; mi++) {
        an->modules[mi].info = &graph->modules[mi];
        total_structs += graph->modules[mi].parse->nstructs;
    }
    an->structs = (TcStructSlot *)an_alloc(
        an, (total_structs == 0 ? 1u : total_structs) * sizeof(TcStructSlot));
    memset(an->structs, 0,
           (total_structs == 0 ? 1u : total_structs) * sizeof(TcStructSlot));
    build_members(an);
    check_duplicate_members(an);
    check_import_name_conflicts(an);
    {
        const TcDiagnostic *first = tc_diag_buffer_first(graph->import_diags);
        if (first != NULL) {
            set_result_diag(an, first);
            *out_status = TC_ANALYSIS_DIAGNOSTIC;
            *out_diag = an->result_diag;
            return an;
        }
    }
    resolve_structs(an);
    resolve_signatures(an);
    resolve_module_value_types(an);
    check_annotations(an);
    check_all_units(an);
    if (!tc_type_table_freeze(an->types)) {
        an->impl = TC_IMPL_INTERNAL;
        longjmp(an->env, 1);
    }
    {
        const TcDiagnostic *first = tc_diag_buffer_first(an->diags);
        if (first != NULL) {
            set_result_diag(an, first);
            *out_status = TC_ANALYSIS_DIAGNOSTIC;
            *out_diag = an->result_diag;
            return an;
        }
    }
    *out_status = TC_ANALYSIS_OK;
    return an;
}

void tc_analysis_destroy(TcAnalysis *analysis) {
    const TcAllocator *alloc;
    size_t mi;
    if (analysis == NULL) {
        return;
    }
    alloc = analysis->alloc;
    (void)mi;
    tc_type_table_destroy(analysis->types);
    tc_diag_buffer_destroy(analysis->diags);
    if (analysis->graph != NULL) {
        tc_module_graph_destroy(analysis->graph);
    }
    tc_arena_destroy(&analysis->arena);
    alloc->free(alloc->ctx, analysis);
}

const TcTypeTable *tc_analysis_type_table(const TcAnalysis *analysis) {
    return analysis == NULL ? NULL : analysis->types;
}

int tc_analysis_type_table_frozen(const TcAnalysis *analysis) {
    return analysis != NULL && analysis->types != NULL &&
           tc_type_table_is_frozen(analysis->types);
}

unsigned tc_analysis_addr_width(const TcAnalysis *analysis) {
    return analysis == NULL ? 0 : analysis->W;
}

size_t tc_analysis_module_count(const TcAnalysis *analysis) {
    return analysis == NULL ? 0 : analysis->nmodules;
}

const char *tc_analysis_module_path(const TcAnalysis *analysis, size_t index) {
    if (analysis == NULL || index >= analysis->nmodules) {
        return NULL;
    }
    return analysis->modules[index].info->path;
}

const TcStructDef *tc_analysis_find_struct(const TcAnalysis *analysis,
                                           const char *module_path,
                                           const char *struct_name) {
    size_t mi;
    if (analysis == NULL || struct_name == NULL) {
        return NULL;
    }
    for (mi = 0; mi < analysis->nmodules; mi++) {
        TcModuleSem *module = &analysis->modules[mi];
        if (module_path != NULL &&
            strcmp(module->info->path, module_path) != 0) {
            continue;
        }
        {
            TcMember *m = find_member(module, struct_name);
            if (m != NULL && m->kind == TC_MEM_STRUCT) {
                return m->sdef;
            }
        }
    }
    return NULL;
}

const char *tc_analysis_member_module(const TcAnalysis *analysis,
                                      const char *module_path,
                                      const char *member_name) {
    size_t mi;
    if (analysis == NULL || member_name == NULL) {
        return NULL;
    }
    for (mi = 0; mi < analysis->nmodules; mi++) {
        TcModuleSem *module = &analysis->modules[mi];
        if (module_path != NULL &&
            strcmp(module->info->path, module_path) != 0) {
            continue;
        }
        if (find_member(module, member_name) != NULL) {
            return module->info->path;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Annotation matrix (standard §2.6/§8.6).                                   */
/*                                                                          */
/* Positions, allowed names, mutual exclusion and parameter ranges are SEM   */
/* facts.  Parameter values may be INT literals or const paths: literals are */
/* checked here; const-path values that need compile-time evaluation are      */
/* completed by CT without changing the annotation diagnostics below.        */
/* Promise violations (@pure store, @nounwind trap, ...) are UB, never a     */
/* static rejection (standard §8.6/§10.2).                                    */
/* ------------------------------------------------------------------------- */

static const char *const tc_func_annotations[] = {
    "unsafe", "safe", "fast", "fast_math", "pure", "readonly",
    "nounwind", "willreturn", "no_overflow", "inline", "noinline"
};

static const char *const tc_param_annotations[] = {
    "nonnull", "no_alias", "align", "range"
};

static int annotation_name_in(const char *name, const char *const *names,
                              size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (strcmp(name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int literal_to_signed(const TcParseValue *v, int64_t *out) {
    if (v->int_magnitude > (uint64_t)INT64_MAX +
                               (v->int_negative ? 1ull : 0ull)) {
        return 0;
    }
    if (v->int_negative) {
        if (v->int_magnitude == (1ull << 63)) {
            *out = INT64_MIN;
        } else {
            *out = -(int64_t)v->int_magnitude;
        }
    } else {
        *out = (int64_t)v->int_magnitude;
    }
    return 1;
}

static uint64_t integer_literal_bits(const TcParseValue *v, unsigned bits) {
    uint64_t mask = bits >= 64 ? ~(uint64_t)0
                               : (((uint64_t)1u << bits) - 1u);
    uint64_t magnitude = v->int_magnitude & mask;
    if (v->int_negative && magnitude != 0) {
        magnitude = (uint64_t)(0u - magnitude) & mask;
    }
    return magnitude;
}

/* Resolve a const_path annotation argument.  Bare names and Self.K denote
 * the current module; m.K requires a public const (standard §3.10/§4.6). */
static const TcType *check_annotation_const(Sem *s, const TcParseValue *v,
                                            int require_integer) {
    TcMember *member = NULL;
    TcSpan name_span = v->base_span;
    if (v->kind != TC_PVAL_NAME) {
        sem_diag_simple(s, TC_CE_ANNOTATION, v->span);
        return NULL;
    }
    if (v->nfields == 0) {
        member = find_member(s->module, v->base);
    } else if (v->nfields == 1) {
        name_span = v->field_spans[0];
        if (strcmp(v->base, "Self") == 0) {
            member = find_member(s->module, v->fields[0]);
        } else {
            TcMember *import = find_member(s->module, v->base);
            if (import == NULL || import->kind != TC_MEM_IMPORT) {
                sem_diag_simple(s, TC_CE_UNDEFINED_NAME, v->base_span);
                return NULL;
            }
            if (import->import_target == TC_MODULE_NO_TARGET) {
                sem_diag_simple(s, TC_CE_UNDEFINED_NAME, v->base_span);
                return NULL;
            }
            member = find_member_in(s->an, import->import_target,
                                    v->fields[0]);
            if (member != NULL && !member->is_public) {
                sem_diag_related(s, TC_CE_PRIVATE_ACCESS, name_span,
                                 member->span);
                return NULL;
            }
        }
    } else {
        sem_diag_simple(s, TC_CE_ANNOTATION, v->span);
        return NULL;
    }
    if (member == NULL) {
        sem_diag_simple(s, TC_CE_UNDEFINED_NAME, name_span);
        return NULL;
    }
    if (member->kind != TC_MEM_CONST) {
        sem_diag_simple(s, TC_CE_ANNOTATION, v->span);
        return NULL;
    }
    if (member->type == NULL) {
        return NULL;
    }
    if (require_integer && !type_is_integer(member->type)) {
        sem_diag_simple(s, TC_CE_ANNOTATION, v->span);
        return NULL;
    }
    return member->type;
}

static void check_func_annotations(Sem *s, const TcParseFunc *func) {
    size_t i;
    size_t j;
    int has_fast = parse_func_has_annotation(func, "fast");
    for (i = 0; i < func->nannotations; i++) {
        const TcParseAnnotation *ann = &func->annotations[i];
        if (!annotation_name_in(ann->name, tc_func_annotations,
                                sizeof(tc_func_annotations) /
                                    sizeof(tc_func_annotations[0]))) {
            sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
            continue;
        }
        if (ann->nargs != 0) {
            sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
            continue;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(func->annotations[j].name, ann->name) == 0) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                break;
            }
        }
        if (j < i) {
            continue;
        }
        if (strcmp(ann->name, "unsafe") == 0 ||
            strcmp(ann->name, "safe") == 0 ||
            strcmp(ann->name, "fast") == 0) {
            for (j = 0; j < i; j++) {
                const char *other = func->annotations[j].name;
                if (strcmp(other, "unsafe") == 0 ||
                    strcmp(other, "safe") == 0 ||
                    strcmp(other, "fast") == 0) {
                    sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                    break;
                }
            }
        } else if (strcmp(ann->name, "inline") == 0) {
            for (j = 0; j < i; j++) {
                if (strcmp(func->annotations[j].name, "noinline") == 0) {
                    sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                    break;
                }
            }
        } else if (strcmp(ann->name, "noinline") == 0) {
            for (j = 0; j < i; j++) {
                if (strcmp(func->annotations[j].name, "inline") == 0) {
                    sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                    break;
                }
            }
        } else if (strcmp(ann->name, "fast_math") == 0) {
            if (!has_fast) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
            }
        } else if (strcmp(ann->name, "pure") == 0 ||
                   strcmp(ann->name, "readonly") == 0 ||
                   strcmp(ann->name, "nounwind") == 0 ||
                   strcmp(ann->name, "willreturn") == 0 ||
                   strcmp(ann->name, "no_overflow") == 0) {
            if (!has_fast) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
            }
        }
    }
}

static void check_param_annotations(Sem *s, const TcParseParam *param,
                                    TcParam *out, const TcType *param_type,
                                    int fast) {
    size_t a;
    for (a = 0; a < param->nannotations; a++) {
        const TcParseAnnotation *ann = &param->annotations[a];
        size_t j;
        if (!annotation_name_in(ann->name, tc_param_annotations,
                                sizeof(tc_param_annotations) /
                                    sizeof(tc_param_annotations[0]))) {
            sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
            continue;
        }
        if (!fast) {
            sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
            continue;
        }
        for (j = 0; j < a; j++) {
            if (strcmp(param->annotations[j].name, ann->name) == 0) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                break;
            }
        }
        if (j < a) {
            continue;
        }
        if (strcmp(ann->name, "nonnull") == 0 ||
            strcmp(ann->name, "no_alias") == 0) {
            if (ann->nargs != 0) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                continue;
            }
            if (param_type == NULL) {
                continue;
            }
            if (!type_is_ptr(param_type)) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                continue;
            }
            if (strcmp(ann->name, "nonnull") == 0) {
                out->flags |= TC_PARAM_NONNULL;
            } else {
                out->flags |= TC_PARAM_NO_ALIAS;
            }
        } else if (strcmp(ann->name, "align") == 0) {
            const TcParseValue *v;
            uint64_t n;
            if (ann->nargs != 1) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                continue;
            }
            if (param_type == NULL) {
                continue;
            }
            if (!type_is_ptr(param_type)) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                continue;
            }
            v = &ann->args[0];
            if (v->kind == TC_PVAL_NAME) {
                if (check_annotation_const(s, v, 1) != NULL) {
                    out->flags |= TC_PARAM_ALIGN;
                }
                continue;
            }
            if (v->kind != TC_PVAL_INT) {
                sem_diag_simple(s, TC_CE_ANNOTATION, v->span);
                continue;
            }
            n = v->int_magnitude;
            if (v->int_negative || n == 0 || (n & (n - 1ull)) != 0) {
                sem_diag_simple(s, TC_CE_ANNOTATION, v->span);
                continue;
            }
            if (n > width_max(s->an->W)) {
                sem_diag_simple(s, TC_CE_ANNOTATION, v->span);
                continue;
            }
            out->flags |= TC_PARAM_ALIGN;
            out->align =
                n > UINT32_MAX ? UINT32_MAX : (uint32_t)n;
        } else if (strcmp(ann->name, "range") == 0) {
            const TcParseValue *lo;
            const TcParseValue *hi;
            if (ann->nargs != 2) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                continue;
            }
            if (param_type == NULL) {
                continue;
            }
            if (!type_is_integer(param_type)) {
                sem_diag_simple(s, TC_CE_ANNOTATION, ann->span);
                continue;
            }
            lo = &ann->args[0];
            hi = &ann->args[1];
            if (lo->kind == TC_PVAL_NAME || hi->kind == TC_PVAL_NAME) {
                const TcType *lo_type = NULL;
                const TcType *hi_type = NULL;
                int bad = 0;
                if (lo->kind == TC_PVAL_NAME) {
                    lo_type = check_annotation_const(s, lo, 1);
                    if (lo_type == NULL) {
                        bad = 1;
                    } else if (!tc_type_equal(lo_type, param_type)) {
                        sem_diag_simple(s, TC_CE_ANNOTATION, lo->span);
                        bad = 1;
                    }
                } else if (lo->kind != TC_PVAL_INT ||
                           !check_literal_expected(s, lo, param_type)) {
                    bad = 1;
                }
                if (!bad) {
                    if (hi->kind == TC_PVAL_NAME) {
                        hi_type = check_annotation_const(s, hi, 1);
                        if (hi_type == NULL) {
                            bad = 1;
                        } else if (!tc_type_equal(hi_type, param_type)) {
                            sem_diag_simple(s, TC_CE_ANNOTATION, hi->span);
                            bad = 1;
                        }
                    } else if (hi->kind != TC_PVAL_INT ||
                               !check_literal_expected(s, hi, param_type)) {
                        bad = 1;
                    }
                }
                if (!bad) {
                    /* At least one endpoint is a const path: value ordering
                     * and bit patterns are completed by CT (standard §3.10). */
                    out->flags |= TC_PARAM_RANGE;
                }
                continue;
            }
            if (lo->kind != TC_PVAL_INT || hi->kind != TC_PVAL_INT) {
                sem_diag_simple(s, TC_CE_ANNOTATION,
                                lo->kind != TC_PVAL_INT ? lo->span
                                                        : hi->span);
                continue;
            }
            if (!check_literal_expected(s, lo, param_type) ||
                !check_literal_expected(s, hi, param_type)) {
                continue;
            }
            if (type_is_signed_integer(param_type)) {
                int64_t l;
                int64_t h;
                if (!literal_to_signed(lo, &l) ||
                    !literal_to_signed(hi, &h)) {
                    sem_diag_simple(s, TC_CE_ANNOTATION, hi->span);
                    continue;
                }
                if (l > h) {
                    sem_diag_simple(s, TC_CE_ANNOTATION, hi->span);
                    continue;
                }
            } else if (lo->int_magnitude > hi->int_magnitude) {
                sem_diag_simple(s, TC_CE_ANNOTATION, hi->span);
                continue;
            }
            out->flags |= TC_PARAM_RANGE;
            out->lo = integer_literal_bits(lo, type_bits(s->an, param_type));
            out->hi = integer_literal_bits(hi, type_bits(s->an, param_type));
        }
    }
}

static void check_annotations(TcAnalysis *an) {
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcModuleSem *module = &an->modules[mi];
        size_t i;
        for (i = 0; i < module->nmembers; i++) {
            TcMember *m = &module->members[i];
            Sem s;
            size_t p;
            int fast;
            if (m->kind != TC_MEM_FUNC) {
                continue;
            }
            memset(&s, 0, sizeof(s));
            s.an = an;
            s.module_index = (uint32_t)mi;
            s.module = module;
            s.func = m->func;
            s.ret = m->sig.ret;
            check_func_annotations(&s, m->func);
            fast = parse_func_has_annotation(m->func, "fast");
            for (p = 0; p < m->func->nparams; p++) {
                check_param_annotations(&s, &m->func->params[p],
                                        &m->sig.params[p],
                                        m->sig.params[p].type, fast);
            }
        }
    }
}
