/* TC 0.0.46 IR finalization (design §3): parse IR + frozen SEM facts + CT
 * results -> TcIR.
 *
 * Runs only after SEM and CT accepted the program.  The frozen type table is
 * read-only; no source names survive in operands (field paths become base vreg
 * plus field indices), while call/make argument names are kept in TcArg.name
 * as design §3.1 requires.  A reference to an aggregate `const` is inlined as
 * a `make` of the const value at the use site; `const` occupies no object
 * (standard §4.7/§9.5).  Every module instance, struct, function, const and
 * static object carries its module id.
 */
#include "frontend/irgen.h"

#include <string.h>

#include "frontend/ct.h"

typedef struct {
    uint32_t module;
    size_t decl_index;
    uint32_t object;
} StaticObject;

typedef struct {
    uint32_t module;
    const TcParseFunc *func;
    uint32_t id;
} FuncEntry;

typedef struct {
    TcAnalysis *an;
    TcIR *ir;
    TcObject *objects;
    size_t nobjects;
    size_t capobjects;
    TcConst *consts;
    size_t nconsts;
    size_t capconsts;
    StaticObject *statics;
    size_t nstatics;
    size_t capstatics;
    FuncEntry *funcs;
    size_t nfuncs;
    /* program top-level var object per body statement (SIZE_MAX = none). */
    uint32_t *prog_vars;
    size_t nprog_vars;
    TcUnit *units;
    size_t nunits;
    size_t capunits;
} IrBuild;

/* ------------------------------------------------------------------------- */
/* Growable arena-backed arrays.                                             */
/* ------------------------------------------------------------------------- */

static void *ir_alloc(TcAnalysis *an, size_t size) {
    void *p = tc_arena_alloc0(&an->arena, size == 0 ? 1u : size);
    if (p == NULL) {
        an->impl = TC_IMPL_OOM;
        longjmp(an->env, 1);
    }
    return p;
}

static void *grow_array(TcAnalysis *an, void *old, size_t old_count,
                        size_t new_count, size_t element) {
    void *fresh = ir_alloc(an, new_count * element);
    if (old_count != 0) {
        memcpy(fresh, old, old_count * element);
    }
    return fresh;
}

static uint32_t add_object(IrBuild *b, TcObject object) {
    if (b->nobjects == b->capobjects) {
        size_t cap = b->capobjects == 0 ? 16u : b->capobjects * 2u;
        TcObject *fresh = (TcObject *)ir_alloc(b->an, cap * sizeof(TcObject));
        if (b->nobjects != 0) {
            memcpy(fresh, b->objects, b->nobjects * sizeof(TcObject));
        }
        b->objects = fresh;
        b->capobjects = cap;
    }
    object.id = (uint32_t)b->nobjects;
    b->objects[b->nobjects] = object;
    return (uint32_t)b->nobjects++;
}

static uint32_t add_const(IrBuild *b, TcConst value) {
    if (b->nconsts == b->capconsts) {
        size_t cap = b->capconsts == 0 ? 16u : b->capconsts * 2u;
        TcConst *fresh = (TcConst *)ir_alloc(b->an, cap * sizeof(TcConst));
        if (b->nconsts != 0) {
            memcpy(fresh, b->consts, b->nconsts * sizeof(TcConst));
        }
        b->consts = fresh;
        b->capconsts = cap;
    }
    value.id = (uint32_t)b->nconsts;
    b->consts[b->nconsts] = value;
    return (uint32_t)b->nconsts++;
}

static void add_static_object(IrBuild *b, uint32_t module, size_t decl_index,
                              uint32_t object) {
    if (b->nstatics == b->capstatics) {
        size_t cap = b->capstatics == 0 ? 16u : b->capstatics * 2u;
        StaticObject *fresh =
            (StaticObject *)ir_alloc(b->an, cap * sizeof(StaticObject));
        if (b->nstatics != 0) {
            memcpy(fresh, b->statics,
                   b->nstatics * sizeof(StaticObject));
        }
        b->statics = fresh;
        b->capstatics = cap;
    }
    b->statics[b->nstatics].module = module;
    b->statics[b->nstatics].decl_index = decl_index;
    b->statics[b->nstatics].object = object;
    b->nstatics++;
}

static uint32_t static_object_for(const IrBuild *b, uint32_t module,
                                  size_t decl_index) {
    size_t i;
    for (i = 0; i < b->nstatics; i++) {
        if (b->statics[i].module == module &&
            b->statics[i].decl_index == decl_index) {
            return b->statics[i].object;
        }
    }
    return UINT32_MAX;
}

static uint32_t func_id_for(const IrBuild *b, uint32_t module,
                            const TcParseFunc *func) {
    size_t i;
    for (i = 0; i < b->nfuncs; i++) {
        if (b->funcs[i].module == module && b->funcs[i].func == func) {
            return b->funcs[i].id;
        }
    }
    return UINT32_MAX;
}

static TcUnit *add_unit(IrBuild *b) {
    TcUnit *unit;
    if (b->nunits == b->capunits) {
        size_t cap = b->capunits == 0 ? 8u : b->capunits * 2u;
        TcUnit *fresh = (TcUnit *)ir_alloc(b->an, cap * sizeof(TcUnit));
        if (b->nunits != 0) {
            memcpy(fresh, b->units, b->nunits * sizeof(TcUnit));
        }
        b->units = fresh;
        b->capunits = cap;
    }
    unit = &b->units[b->nunits++];
    memset(unit, 0, sizeof(*unit));
    return unit;
}

/* ------------------------------------------------------------------------- */
/* Static layout: import-DAG reverse topological order, module source order.  */
/* ------------------------------------------------------------------------- */

static size_t *reverse_module_order(IrBuild *b) {
    TcAnalysis *an = b->an;
    size_t n = an->nmodules;
    size_t *order = (size_t *)ir_alloc(an, (n == 0 ? 1u : n) * sizeof(size_t));
    uint8_t *done = (uint8_t *)ir_alloc(an, n == 0 ? 1u : n);
    size_t *indeg = (size_t *)ir_alloc(an, (n == 0 ? 1u : n) * sizeof(size_t));
    size_t produced = 0;
    size_t i;
    memset(done, 0, n == 0 ? 1u : n);
    for (i = 0; i < n; i++) {
        indeg[i] = 0;
    }
    for (i = 0; i < n; i++) {
        TcModuleInfo *info = an->modules[i].info;
        size_t k;
        for (k = 0; k < info->nimports; k++) {
            uint32_t target = info->import_targets[k];
            if (target != TC_MODULE_NO_TARGET && target < n) {
                /* Reverse graph: the imported module gains an in-edge from
                 * its importer, so importers are emitted first. */
                indeg[target]++;
            }
        }
    }
    while (produced < n) {
        size_t pick = SIZE_MAX;
        for (i = 0; i < n; i++) {
            if (!done[i] && indeg[i] == 0) {
                pick = i;
                break;
            }
        }
        if (pick == SIZE_MAX) {
            /* A cycle would have been diagnosed in IMPORT; keep determinism. */
            for (i = 0; i < n; i++) {
                if (!done[i]) {
                    pick = i;
                    break;
                }
            }
        }
        done[pick] = 1;
        order[produced++] = pick;
        {
            TcModuleInfo *info = an->modules[pick].info;
            size_t k;
            for (k = 0; k < info->nimports; k++) {
                uint32_t target = info->import_targets[k];
                if (target != TC_MODULE_NO_TARGET && target < n &&
                    !done[target] && indeg[target] != 0) {
                    indeg[target]--;
                }
            }
        }
    }
    return order;
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1u) / alignment * alignment;
}

static void build_static_objects(IrBuild *b) {
    TcAnalysis *an = b->an;
    size_t *order = reverse_module_order(b);
    uint64_t pointer_align = an->W / 8u;
    uint64_t offset = 0;
    size_t oi;
    size_t mi;
    b->prog_vars = NULL;
    b->nprog_vars = 0;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcParseModule *parse = an->modules[mi].info->parse;
        if (parse->is_program) {
            b->nprog_vars = parse->nbody;
            b->prog_vars = (uint32_t *)ir_alloc(
                an, (parse->nbody == 0 ? 1u : parse->nbody) * sizeof(uint32_t));
            {
                size_t i;
                for (i = 0; i < parse->nbody; i++) {
                    b->prog_vars[i] = UINT32_MAX;
                }
            }
        }
    }
    for (oi = 0; oi < an->nmodules; oi++) {
        uint32_t module = (uint32_t)order[oi];
        TcParseModule *parse = an->modules[module].info->parse;
        if (parse->is_program) {
            size_t i;
            for (i = 0; i < parse->nbody; i++) {
                const TcParseStmt *stmt = &parse->body[i];
                TcObject object;
                int ok = 0;
                uint64_t size;
                const TcType *type;
                if (stmt->kind != TC_STMT_VAR) {
                    continue;
                }
                /* The declared type was resolved by SEM; resolve the parse
                 * type again through the frozen table (lookup only). */
                {
                    TcParseArg arg;
                    memset(&arg, 0, sizeof(arg));
                    arg.kind = TC_PARG_TYPE;
                    arg.type = stmt->var.type;
                    type = tc_sem_resolve_type_arg(an, module, &arg);
                }
                if (type == NULL) {
                    an->impl = TC_IMPL_INTERNAL;
                    return;
                }
                size = tc_sem_type_size(an, type, &ok);
                if (!ok) {
                    an->impl = TC_IMPL_INTERNAL;
                    return;
                }
                memset(&object, 0, sizeof(object));
                object.module = module;
                object.region = TC_REG_STATIC;
                offset = align_up(offset, pointer_align);
                object.offset = offset;
                object.size = size;
                object.type = type;
                object.lifetime = TC_LIFE_PROGRAM;
                object.init = NULL;
                object.writable = 1;
                object.parent = TC_OBJECT_NO_PARENT;
                b->prog_vars[i] = add_object(b, object);
                offset += size;
            }
        } else {
            size_t i;
            for (i = 0; i < parse->nconsts; i++) {
                const TcParseConstDecl *decl = &parse->consts[i];
                TcMember *member;
                TcCtValue *value;
                TcObject object;
                int ok = 0;
                uint64_t size;
                const TcInitImage *image;
                if (!decl->is_static) {
                    continue;
                }
                member = tc_sem_find_member(&an->modules[module], decl->name);
                if (member == NULL || member->type == NULL) {
                    an->impl = TC_IMPL_INTERNAL;
                    return;
                }
                value = (TcCtValue *)tc_ct_module_value_by_member(
                    an, module, member);
                size = tc_sem_type_size(an, member->type, &ok);
                if (value == NULL || !ok) {
                    an->impl = TC_IMPL_INTERNAL;
                    return;
                }
                image = tc_ct_value_image(an, value, &ok);
                if (!ok || image == NULL) {
                    an->impl = TC_IMPL_INTERNAL;
                    return;
                }
                memset(&object, 0, sizeof(object));
                object.module = module;
                object.region = TC_REG_STATIC;
                offset = align_up(offset, pointer_align);
                object.offset = offset;
                object.size = size;
                object.type = member->type;
                object.lifetime = TC_LIFE_PROGRAM;
                object.init = image;
                object.writable = 1;
                object.parent = TC_OBJECT_NO_PARENT;
                add_static_object(b, module, i, add_object(b, object));
                offset += size;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Module, struct, function and const tables.                                */
/* ------------------------------------------------------------------------- */

static void build_modules(IrBuild *b) {
    TcAnalysis *an = b->an;
    b->ir->modules = (TcModule *)ir_alloc(
        an, (an->nmodules == 0 ? 1u : an->nmodules) * sizeof(TcModule));
    b->ir->nmodules = an->nmodules;
    {
        size_t i;
        for (i = 0; i < an->nmodules; i++) {
            b->ir->modules[i].id = (uint32_t)i;
            b->ir->modules[i].path = an->modules[i].info->path;
        }
    }
}

static void build_structs(IrBuild *b) {
    TcAnalysis *an = b->an;
    b->ir->structs = (TcStructDef *)ir_alloc(
        an, (an->nstructs == 0 ? 1u : an->nstructs) * sizeof(TcStructDef));
    b->ir->nstructs = an->nstructs;
    {
        size_t i;
        for (i = 0; i < an->nstructs; i++) {
            b->ir->structs[i] = *an->structs[i].def;
        }
    }
}

static TcFuncFlags func_flags(const TcParseFunc *func) {
    TcFuncFlags flags = 0;
    size_t i;
    for (i = 0; i < func->nannotations; i++) {
        const char *name = func->annotations[i].name;
        if (strcmp(name, "unsafe") == 0) {
            flags |= TC_FN_UNSAFE;
        } else if (strcmp(name, "safe") == 0) {
            flags |= TC_FN_SAFE;
        } else if (strcmp(name, "fast") == 0) {
            flags |= TC_FN_FAST;
        } else if (strcmp(name, "fast_math") == 0) {
            flags |= TC_FN_FAST_MATH;
        } else if (strcmp(name, "pure") == 0) {
            flags |= TC_FN_PURE;
        } else if (strcmp(name, "readonly") == 0) {
            flags |= TC_FN_READONLY;
        } else if (strcmp(name, "nounwind") == 0) {
            flags |= TC_FN_NOUNWIND;
        } else if (strcmp(name, "willreturn") == 0) {
            flags |= TC_FN_WILLRETURN;
        } else if (strcmp(name, "no_overflow") == 0) {
            flags |= TC_FN_NO_OVERFLOW;
        } else if (strcmp(name, "inline") == 0) {
            flags |= TC_FN_INLINE;
        } else if (strcmp(name, "noinline") == 0) {
            flags |= TC_FN_NOINLINE;
        }
    }
    return flags;
}

static void build_funcs(IrBuild *b) {
    TcAnalysis *an = b->an;
    size_t total = 0;
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        total += an->modules[mi].info->parse->nfuncs;
    }
    b->ir->funcs = (TcFunc *)ir_alloc(
        an, (total == 0 ? 1u : total) * sizeof(TcFunc));
    b->ir->nfuncs = 0;
    b->funcs = (FuncEntry *)ir_alloc(
        an, (total == 0 ? 1u : total) * sizeof(FuncEntry));
    b->nfuncs = 0;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcParseModule *parse = an->modules[mi].info->parse;
        size_t i;
        for (i = 0; i < parse->nfuncs; i++) {
            TcParseFunc *func = &parse->funcs[i];
            TcMember *member = tc_sem_find_member(&an->modules[mi],
                                                  func->name);
            TcFunc *out;
            if (member == NULL || member->kind != TC_MEM_FUNC ||
                member->sig.ret == NULL) {
                an->impl = TC_IMPL_INTERNAL;
                return;
            }
            out = &b->ir->funcs[b->ir->nfuncs];
            memset(out, 0, sizeof(*out));
            out->id = (uint32_t)b->ir->nfuncs;
            out->module = (uint32_t)mi;
            out->name = func->name;
            out->sig = member->sig;
            out->flags = func_flags(func);
            out->body = NULL;
            b->funcs[b->nfuncs].module = (uint32_t)mi;
            b->funcs[b->nfuncs].func = func;
            b->funcs[b->nfuncs].id = out->id;
            b->nfuncs++;
            b->ir->nfuncs++;
        }
    }
}

static void build_consts(IrBuild *b) {
    TcAnalysis *an = b->an;
    size_t mi;
    size_t total = 0;
    for (mi = 0; mi < an->nmodules; mi++) {
        total += an->modules[mi].info->parse->nconsts;
    }
    b->consts = (TcConst *)ir_alloc(
        an, (total == 0 ? 1u : total) * sizeof(TcConst));
    b->nconsts = 0;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcParseModule *parse = an->modules[mi].info->parse;
        size_t i;
        for (i = 0; i < parse->nconsts; i++) {
            const TcParseConstDecl *decl = &parse->consts[i];
            TcMember *member;
            const TcCtValue *value;
            TcConst entry;
            int ok = 0;
            if (decl->is_static) {
                continue;
            }
            member = tc_sem_find_member(&an->modules[mi], decl->name);
            if (member == NULL || member->type == NULL) {
                an->impl = TC_IMPL_INTERNAL;
                return;
            }
            value = tc_ct_module_value_by_member(an, (uint32_t)mi, member);
            if (value == NULL) {
                an->impl = TC_IMPL_INTERNAL;
                return;
            }
            memset(&entry, 0, sizeof(entry));
            entry.module = (uint32_t)mi;
            entry.name = decl->name;
            entry.type = member->type;
            if (value->kind == TC_CT_STRUCT) {
                entry.bits = 0;
                entry.init = tc_ct_value_image(an, value, &ok);
                if (!ok) {
                    an->impl = TC_IMPL_INTERNAL;
                    return;
                }
            } else {
                entry.bits = value->bits;
                entry.init = NULL;
            }
            add_const(b, entry);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Unit lowering.                                                            */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    const TcType *type;
    int is_param;
    int is_const;
    size_t stmt_index; /* SIZE_MAX for parameters */
    uint32_t vreg;
    uint32_t object;
} IrLocal;

typedef struct {
    IrBuild *b;
    TcAnalysis *an;
    TcIR *ir;
    TcUnit *unit;
    uint32_t module;
    size_t func_member; /* SIZE_MAX for the program body */
    const TcParseStmt *body;
    size_t nbody;
    const TcParseFunc *func;
    IrLocal *locals;
    size_t nlocals;
    const char **labels;
    size_t nlabels;
    uint32_t *label_block;
    TcBlock *blocks;
    size_t nblocks;
    size_t capblocks;
    uint32_t cur;
    int terminated;
    const TcType **vreg_types;
    uint32_t *vreg_objects;
    size_t nvregs;
    size_t capvregs;
    size_t cur_stmt;
    unsigned depth;
} UnitBuild;

static uint32_t alloc_vreg(UnitBuild *u, const TcType *type, uint32_t object) {
    uint32_t id;
    if (u->nvregs == u->capvregs) {
        size_t cap = u->capvregs == 0 ? 16u : u->capvregs * 2u;
        u->vreg_types = (const TcType **)grow_array(
            u->an, u->vreg_types, u->nvregs, cap, sizeof(const TcType *));
        u->vreg_objects = (uint32_t *)grow_array(
            u->an, u->vreg_objects, u->nvregs, cap, sizeof(uint32_t));
        u->capvregs = cap;
    }
    id = (uint32_t)u->nvregs;
    u->vreg_types[u->nvregs] = type;
    u->vreg_objects[u->nvregs] = object;
    u->nvregs++;
    return id;
}

static TcBlock *new_block(UnitBuild *u) {
    TcBlock *block;
    if (u->nblocks == u->capblocks) {
        size_t cap = u->capblocks == 0 ? 8u : u->capblocks * 2u;
        TcBlock *fresh = (TcBlock *)ir_alloc(u->an, cap * sizeof(TcBlock));
        if (u->nblocks != 0) {
            memcpy(fresh, u->blocks, u->nblocks * sizeof(TcBlock));
        }
        u->blocks = fresh;
        u->capblocks = cap;
    }
    block = &u->blocks[u->nblocks];
    memset(block, 0, sizeof(*block));
    block->id = (uint32_t)u->nblocks;
    u->nblocks++;
    return block;
}

static TcBlock *block_at(UnitBuild *u, uint32_t id) {
    if (id >= u->nblocks) {
        return NULL;
    }
    return &u->blocks[id];
}

static TcInstr *append_instr(UnitBuild *u, uint32_t block_id) {
    TcBlock *block = block_at(u, block_id);
    TcInstr *fresh;
    fresh = (TcInstr *)ir_alloc(u->an, (block->n + 1u) * sizeof(TcInstr));
    if (block->n != 0) {
        memcpy(fresh, block->instrs, block->n * sizeof(TcInstr));
    }
    memset(&fresh[block->n], 0, sizeof(TcInstr));
    block->instrs = fresh;
    block->n++;
    return &block->instrs[block->n - 1u];
}

static int lower_resolve_field_path(UnitBuild *u, const TcType *base,
                                    const char **names, size_t nfields,
                                    uint32_t **out_fields,
                                    const TcType **out_type) {
    const TcType *cur = base;
    uint32_t *fields = (uint32_t *)ir_alloc(
        u->an, (nfields == 0 ? 1u : nfields) * sizeof(uint32_t));
    size_t i;
    for (i = 0; i < nfields; i++) {
        TcStructDef *def = tc_sem_struct_for_type(u->an, cur);
        const TcField *field;
        if (def == NULL) {
            return 0;
        }
        field = tc_sem_find_field(def, names[i]);
        if (field == NULL) {
            return 0;
        }
        fields[i] = (uint32_t)(field - def->fields);
        cur = field->type;
        if (cur == NULL) {
            return 0;
        }
    }
    if (out_fields != NULL) {
        *out_fields = fields;
    }
    if (out_type != NULL) {
        *out_type = cur;
    }
    return 1;
}

static IrLocal *find_local(UnitBuild *u, const char *name, size_t stmt_index) {
    size_t i;
    for (i = 0; i < u->nlocals; i++) {
        IrLocal *local = &u->locals[i];
        if (strcmp(local->name, name) != 0) {
            continue;
        }
        if (local->is_param || local->stmt_index < stmt_index) {
            return local;
        }
    }
    return NULL;
}

typedef enum {
    IR_REF_LOCAL_VAR = 0,
    IR_REF_LOCAL_CONST,
    IR_REF_STATIC,
    IR_REF_CONST,
    IR_REF_FUNC
} IrRefKind;

typedef struct {
    IrRefKind kind;
    const TcType *type;
    uint32_t base_vreg;
    uint32_t *fields;
    size_t nfields;
    const TcCtValue *ct;
    uint32_t func;
} IrRef;

/* Resolve a value name path to its final SEM identity.  Like SEM, a bare name
 * first resolves a function/program local; then Self./import members. */
static int lower_resolve(UnitBuild *u, const TcParseValue *v, IrRef *out) {
    TcMember *member = NULL;
    uint32_t member_module = u->module;
    const char **fields = v->fields;
    size_t nfields = v->nfields;
    size_t consumed = 0;
    memset(out, 0, sizeof(*out));
    if (v->kind != TC_PVAL_NAME) {
        return 0;
    }
    if (strcmp(v->base, "Self") == 0) {
        if (nfields == 0) {
            return 0;
        }
        member = tc_sem_find_member(&u->an->modules[member_module], fields[0]);
        consumed = 1;
    } else {
        TcMember *import = tc_sem_find_member(&u->an->modules[u->module],
                                              v->base);
        if (import != NULL && import->kind == TC_MEM_IMPORT) {
            if (nfields == 0 ||
                import->import_target == TC_MODULE_NO_TARGET) {
                return 0;
            }
            member_module = import->import_target;
            member = tc_sem_find_member_in(u->an, member_module, fields[0]);
            consumed = 1;
        }
    }
    if (member != NULL) {
        fields += consumed;
        nfields -= consumed;
        if (member->kind == TC_MEM_FUNC) {
            uint32_t id = func_id_for(u->b, member_module, member->func);
            if (id == UINT32_MAX) {
                return 0;
            }
            out->kind = IR_REF_FUNC;
            out->func = id;
            out->type = NULL;
            return 1;
        }
        if (member->kind == TC_MEM_CONST) {
            const TcCtValue *base = tc_ct_module_value_by_member(
                u->an, member_module, member);
            const TcCtValue *cur = base;
            size_t i;
            if (base == NULL) {
                return 0;
            }
            for (i = 0; i < nfields; i++) {
                TcStructDef *def;
                const TcField *field;
                size_t index;
                if (cur->kind != TC_CT_STRUCT) {
                    return 0;
                }
                def = tc_sem_struct_for_type(u->an, cur->type);
                if (def == NULL) {
                    return 0;
                }
                field = tc_sem_find_field(def, fields[i]);
                if (field == NULL) {
                    return 0;
                }
                index = (size_t)(field - def->fields);
                if (index >= cur->nfields || cur->fields[index] == NULL) {
                    return 0;
                }
                cur = cur->fields[index];
            }
            out->kind = IR_REF_CONST;
            out->type = cur->type;
            out->ct = cur;
            return 1;
        }
        if (member->kind == TC_MEM_STATIC) {
            uint32_t object = static_object_for(u->b, member_module,
                                                member->decl_index);
            uint32_t vreg;
            uint32_t *indices = NULL;
            const TcType *type = NULL;
            if (object == UINT32_MAX || member->type == NULL) {
                return 0;
            }
            vreg = alloc_vreg(u, member->type, object);
            if (!lower_resolve_field_path(u, member->type, fields, nfields,
                                          &indices, &type)) {
                return 0;
            }
            out->kind = IR_REF_STATIC;
            out->base_vreg = vreg;
            out->fields = indices;
            out->nfields = nfields;
            out->type = type;
            return 1;
        }
        return 0;
    }
    {
        IrLocal *local = find_local(u, v->base, u->cur_stmt);
        uint32_t *indices = NULL;
        const TcType *type = NULL;
        if (local == NULL) {
            return 0;
        }
        if (local->is_const) {
            const TcCtValue *base;
            const TcCtValue *cur;
            size_t i;
            base = tc_ct_local_value(u->an, u->module, u->func_member,
                                     local->stmt_index);
            if (base == NULL) {
                return 0;
            }
            cur = base;
            for (i = 0; i < nfields; i++) {
                TcStructDef *def;
                const TcField *field;
                size_t index;
                if (cur->kind != TC_CT_STRUCT) {
                    return 0;
                }
                def = tc_sem_struct_for_type(u->an, cur->type);
                if (def == NULL) {
                    return 0;
                }
                field = tc_sem_find_field(def, fields[i]);
                if (field == NULL) {
                    return 0;
                }
                index = (size_t)(field - def->fields);
                if (index >= cur->nfields || cur->fields[index] == NULL) {
                    return 0;
                }
                cur = cur->fields[index];
            }
            out->kind = IR_REF_LOCAL_CONST;
            out->type = cur->type;
            out->ct = cur;
            return 1;
        }
        if (!lower_resolve_field_path(u, local->type, fields, nfields,
                                      &indices, &type)) {
            return 0;
        }
        out->kind = IR_REF_LOCAL_VAR;
        out->base_vreg = local->vreg;
        out->fields = indices;
        out->nfields = nfields;
        out->type = type;
        return 1;
    }
}

static uint64_t literal_bits(const TcParseValue *v, unsigned width) {
    uint64_t mask = width >= 64u ? ~(uint64_t)0
                                 : (UINT64_C(1) << width) - 1u;
    switch (v->kind) {
    case TC_PVAL_INT: {
        uint64_t bits = v->int_magnitude & mask;
        if (v->int_negative && bits != 0) {
            bits = (UINT64_C(0) - bits) & mask;
        }
        return bits;
    }
    case TC_PVAL_BOOL:
        return v->bool_value != 0 ? 1u : 0u;
    case TC_PVAL_FLOAT:
        return v->float_bits;
    case TC_PVAL_INF:
        if (width == 32) {
            return v->inf_negative ? UINT64_C(0xff800000)
                                   : UINT64_C(0x7f800000);
        }
        return v->inf_negative ? UINT64_C(0xfff0000000000000)
                               : UINT64_C(0x7ff0000000000000);
    case TC_PVAL_NAN:
        return width == 32 ? UINT64_C(0x7fc00000)
                           : UINT64_C(0x7ff8000000000000);
    case TC_PVAL_NULLPTR:
        return 0;
    default:
        return 0;
    }
}

static int emit_const_make(UnitBuild *u, const TcCtValue *value,
                           TcSpan span, uint32_t *out_vreg);
static int lower_operand(UnitBuild *u, const TcParseValue *v,
                         const TcType *expected, TcOperand *out);

static int emit_const_operand(UnitBuild *u, const TcCtValue *value,
                              TcSpan span, TcOperand *out) {
    if (value->kind == TC_CT_STRUCT) {
        uint32_t vreg = 0;
        if (!emit_const_make(u, value, span, &vreg)) {
            return 0;
        }
        memset(out, 0, sizeof(*out));
        out->kind = TC_OP_VREG;
        out->vreg = vreg;
        return 1;
    }
    memset(out, 0, sizeof(*out));
    out->kind = TC_OP_CONST;
    out->konst.bits = value->bits;
    out->konst.type = value->type;
    return 1;
}

static int emit_const_make(UnitBuild *u, const TcCtValue *value, TcSpan span,
                           uint32_t *out_vreg) {
    TcStructDef *def = tc_sem_struct_for_type(u->an, value->type);
    TcInstr instr;
    TcArg *args;
    uint32_t vreg;
    size_t i;
    if (def == NULL || def->nfields != value->nfields || u->nblocks == 0) {
        return 0;
    }
    args = (TcArg *)ir_alloc(u->an, (def->nfields == 0 ? 1u : def->nfields) *
                                          sizeof(TcArg));
    memset(args, 0, (def->nfields == 0 ? 1u : def->nfields) * sizeof(TcArg));
    for (i = 0; i < def->nfields; i++) {
        const TcCtValue *field = value->fields[i];
        if (field == NULL) {
            return 0;
        }
        if (field->kind == TC_CT_STRUCT) {
            uint32_t nested = 0;
            if (!emit_const_make(u, field, span, &nested)) {
                return 0;
            }
            args[i].value.kind = TC_OP_VREG;
            args[i].value.vreg = nested;
        } else {
            args[i].value.kind = TC_OP_CONST;
            args[i].value.konst.bits = field->bits;
            args[i].value.konst.type = field->type;
        }
        args[i].name = NULL;
    }
    vreg = alloc_vreg(u, value->type, UINT32_MAX);
    memset(&instr, 0, sizeof(instr));
    instr.op = TC_MAKE;
    instr.mode = TC_STRICT;
    instr.type = value->type;
    instr.dst.kind = TC_OP_VREG;
    instr.dst.vreg = vreg;
    instr.args = args;
    instr.nargs = def->nfields;
    instr.span = span;
    *append_instr(u, u->cur) = instr;
    *out_vreg = vreg;
    return 1;
}

static int lower_operand(UnitBuild *u, const TcParseValue *v,
                         const TcType *expected, TcOperand *out) {
    unsigned width;
    if (v->kind != TC_PVAL_NAME) {
        width = tc_sem_type_bits(u->an, expected);
        memset(out, 0, sizeof(*out));
        out->kind = TC_OP_CONST;
        out->konst.bits = literal_bits(v, width);
        out->konst.type = expected;
        return 1;
    }
    {
        IrRef ref;
        if (!lower_resolve(u, v, &ref)) {
            return 0;
        }
        if (ref.kind == IR_REF_LOCAL_CONST || ref.kind == IR_REF_CONST) {
            return emit_const_operand(u, ref.ct, v->span, out);
        }
        if (ref.kind == IR_REF_FUNC) {
            return 0;
        }
        memset(out, 0, sizeof(*out));
        if (ref.nfields == 0) {
            out->kind = TC_OP_VREG;
            out->vreg = ref.base_vreg;
        } else {
            out->kind = TC_OP_FIELD;
            out->field.base = ref.base_vreg;
            out->field.fields = ref.fields;
            out->field.nfields = ref.nfields;
        }
        return 1;
    }
}

static int lower_target(UnitBuild *u, const TcParseInstr *instr,
                        TcOperand *out) {
    TcParseValue value;
    IrRef ref;
    memset(out, 0, sizeof(*out));
    if (instr->dst_discard || instr->dst_base == NULL) {
        out->kind = TC_OP_DISCARD;
        return 1;
    }
    memset(&value, 0, sizeof(value));
    value.kind = TC_PVAL_NAME;
    value.base = instr->dst_base;
    value.base_span = instr->dst_base_span;
    value.fields = instr->dst_fields;
    value.field_spans = instr->dst_field_spans;
    value.nfields = instr->dst_nfields;
    if (!lower_resolve(u, &value, &ref)) {
        return 0;
    }
    if (ref.kind == IR_REF_LOCAL_VAR || ref.kind == IR_REF_STATIC) {
        if (ref.nfields == 0) {
            out->kind = TC_OP_VREG;
            out->vreg = ref.base_vreg;
        } else {
            out->kind = TC_OP_FIELD;
            out->field.base = ref.base_vreg;
            out->field.fields = ref.fields;
            out->field.nfields = ref.nfields;
        }
        return 1;
    }
    return 0;
}

static int label_index_of(UnitBuild *u, const char *name) {
    size_t i;
    for (i = 0; i < u->nlabels; i++) {
        if (strcmp(u->labels[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int lower_label_operand(UnitBuild *u, const TcParseValue *v,
                               TcOperand *out) {
    int index;
    if (v->kind != TC_PVAL_NAME || v->nfields != 0) {
        return 0;
    }
    index = label_index_of(u, v->base);
    if (index < 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->kind = TC_OP_LABEL;
    out->label = (uint32_t)index;
    return 1;
}

static int lower_function_operand(UnitBuild *u, const TcParseValue *v,
                                  TcOperand *out) {
    IrRef ref;
    if (!lower_resolve(u, v, &ref) || ref.kind != IR_REF_FUNC) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->kind = TC_OP_FUNC;
    out->func = ref.func;
    return 1;
}

static const TcType *fnptr_type_of(UnitBuild *u, uint32_t func_id) {
    const TcFunc *func = &u->ir->funcs[func_id];
    const TcType **params;
    size_t i;
    params = (const TcType **)ir_alloc(
        u->an, (func->sig.nparams == 0 ? 1u : func->sig.nparams) *
                    sizeof(const TcType *));
    for (i = 0; i < func->sig.nparams; i++) {
        params[i] = func->sig.params[i].type;
        if (params[i] == NULL) {
            return NULL;
        }
    }
    return tc_type_fnptr_find(u->an->types, func->sig.ret, params,
                              func->sig.nparams);
}

static int lower_value_type(UnitBuild *u, const TcParseValue *v,
                            const TcType **out) {
    IrRef ref;
    if (v->kind != TC_PVAL_NAME) {
        return 0;
    }
    if (!lower_resolve(u, v, &ref)) {
        return 0;
    }
    if (ref.kind == IR_REF_FUNC) {
        return 0;
    }
    *out = ref.type;
    return 1;
}

/* Source type of a cast/trunc/bitcast literal operand (standard §6.6.3). */
static const TcType *source_literal_type(UnitBuild *u, const TcParseInstr *instr,
                                         const TcParseValue *v,
                                         const TcType *target) {
    unsigned width = tc_sem_type_bits(u->an, target);
    if (v->kind == TC_PVAL_INT) {
        TcTypeTag tag;
        if (instr->op == TC_BITCAST) {
            switch (width) {
            case 8:
                tag = v->int_unsigned_suffix ? TC_TY_U8 : TC_TY_I8;
                break;
            case 16:
                tag = v->int_unsigned_suffix ? TC_TY_U16 : TC_TY_I16;
                break;
            case 32:
                tag = v->int_unsigned_suffix ? TC_TY_U32 : TC_TY_I32;
                break;
            default:
                tag = v->int_unsigned_suffix ? TC_TY_U64 : TC_TY_I64;
                break;
            }
        } else {
            tag = v->int_unsigned_suffix ? TC_TY_U64 : TC_TY_I64;
        }
        return tc_type_scalar(u->an->types, tag);
    }
    if (v->kind == TC_PVAL_FLOAT) {
        return tc_type_scalar(u->an->types,
                              v->float_width == 32 ? TC_TY_F32 : TC_TY_F64);
    }
    if (v->kind == TC_PVAL_BOOL) {
        return tc_type_scalar(u->an->types, TC_TY_BOOL);
    }
    if (v->kind == TC_PVAL_INF || v->kind == TC_PVAL_NAN) {
        return tc_type_scalar(u->an->types,
                              width == 32 ? TC_TY_F32 : TC_TY_F64);
    }
    return NULL;
}

static int lower_source_operand(UnitBuild *u, const TcParseInstr *instr,
                                const TcType *target, TcOperand *out) {
    const TcParseValue *v;
    const TcType *source;
    if (instr->nargs < 2 || instr->args[1].kind != TC_PARG_VALUE) {
        return 0;
    }
    v = &instr->args[1].value;
    if (v->kind == TC_PVAL_NAME) {
        return lower_operand(u, v, NULL, out);
    }
    source = source_literal_type(u, instr, v, target);
    if (source == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->kind = TC_OP_CONST;
    out->konst.bits = literal_bits(v, tc_sem_type_bits(u->an, source));
    out->konst.type = source;
    return 1;
}

static int lower_instr(UnitBuild *u, const TcParseInstr *instr,
                       const TcOperand *implicit_dst) {
    TcInstr out;
    TcArg *args = NULL;
    size_t nargs = 0;
    TcOperand dst;
    const TcType *type = NULL;
    TcOpcode op = instr->op;
    memset(&out, 0, sizeof(out));
    out.op = op;
    out.mode = instr->mode;
    out.span = instr->span;
    if (implicit_dst != NULL) {
        dst = *implicit_dst;
    } else if (!tc_opcode_has_result(op)) {
        dst.kind = TC_OP_DISCARD;
    } else if (!lower_target(u, instr, &dst)) {
        return 0;
    }
    switch (op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
    case TC_CAST:
    case TC_TRUNC:
    case TC_BITCAST:
    case TC_LOAD:
    case TC_STORE:
    case TC_MEMMOVE:
    case TC_ADDR:
    case TC_PTRADD:
    case TC_PTRSUB:
    case TC_PTRDIFF:
    case TC_PTREQ:
    case TC_PTRNE:
    case TC_PTRLT:
    case TC_PTRLE:
    case TC_PTRGT:
    case TC_PTRGE:
    case TC_SIZEOF:
    case TC_FUNCEQ:
    case TC_FUNCNE:
    case TC_MAKE:
    case TC_COPY:
    case TC_READ:
    case TC_WRITE:
    case TC_WRITELN:
        type = tc_sem_resolve_type_arg(u->an, u->module,
                                       &instr->args[0]);
        if (type == NULL) {
            return 0;
        }
        break;
    default:
        break;
    }
    out.type = type;
    switch (op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        nargs = 2;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, type, &args[0].value) ||
            !lower_operand(u, &instr->args[2].value, type, &args[1].value)) {
            return 0;
        }
        break;
    case TC_NEG:
    case TC_ABS:
    case TC_NOT:
        type = tc_sem_resolve_type_arg(u->an, u->module, &instr->args[0]);
        out.type = type;
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, type, &args[0].value)) {
            return 0;
        }
        break;
    case TC_CAST:
    case TC_TRUNC:
    case TC_BITCAST:
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        if (!lower_source_operand(u, instr, type, &args[0].value)) {
            return 0;
        }
        break;
    case TC_COPY:
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, type, &args[0].value)) {
            return 0;
        }
        break;
    case TC_LOAD:
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value,
                           tc_type_ptr_find(u->an->types, type),
                           &args[0].value)) {
            return 0;
        }
        break;
    case TC_READ:
        nargs = 0;
        break;
    case TC_STORE:
    case TC_WRITE:
    case TC_WRITELN:
        nargs = op == TC_STORE ? 2u : 1u;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (op == TC_STORE) {
            if (!lower_operand(u, &instr->args[1].value,
                               tc_type_ptr_find(u->an->types, type),
                               &args[0].value) ||
                !lower_operand(u, &instr->args[2].value, type,
                               &args[1].value)) {
                return 0;
            }
        } else if (!lower_operand(u, &instr->args[1].value, type,
                                  &args[0].value)) {
            return 0;
        }
        break;
    case TC_MEMMOVE: {
        const TcType *pointer = tc_type_ptr_find(u->an->types, type);
        const TcType *usize = tc_type_scalar(u->an->types, TC_TY_USIZE);
        nargs = 3;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, pointer,
                           &args[0].value) ||
            !lower_operand(u, &instr->args[2].value, pointer,
                           &args[1].value) ||
            !lower_operand(u, &instr->args[3].value, usize,
                           &args[2].value)) {
            return 0;
        }
        break;
    }
    case TC_ADDR:
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, type, &args[0].value)) {
            return 0;
        }
        break;
    case TC_PTRADD:
    case TC_PTRSUB: {
        const TcType *pointer = tc_type_ptr_find(u->an->types, type);
        const TcType *usize = tc_type_scalar(u->an->types, TC_TY_USIZE);
        nargs = 2;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, pointer,
                           &args[0].value) ||
            !lower_operand(u, &instr->args[2].value, usize,
                           &args[1].value)) {
            return 0;
        }
        break;
    }
    case TC_PTRDIFF: {
        const TcType *pointer = tc_type_ptr_find(u->an->types, type);
        nargs = 2;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, pointer,
                           &args[0].value) ||
            !lower_operand(u, &instr->args[2].value, pointer,
                           &args[1].value)) {
            return 0;
        }
        break;
    }
    case TC_PTREQ:
    case TC_PTRNE:
    case TC_PTRLT:
    case TC_PTRLE:
    case TC_PTRGT:
    case TC_PTRGE: {
        const TcType *pointer = tc_type_ptr_find(u->an->types, type);
        nargs = 2;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, pointer,
                           &args[0].value) ||
            !lower_operand(u, &instr->args[2].value, pointer,
                           &args[1].value)) {
            return 0;
        }
        break;
    }
    case TC_SIZEOF:
        nargs = 0;
        break;
    case TC_FUNCADDR: {
        TcOperand func;
        if (!lower_function_operand(u, &instr->args[0].value, &func)) {
            return 0;
        }
        out.type = fnptr_type_of(u, func.func);
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        args[0].value = func;
        break;
    }
    case TC_CALL: {
        TcOperand func;
        const TcFunc *callee;
        size_t i;
        if (!lower_function_operand(u, &instr->args[0].value, &func)) {
            return 0;
        }
        callee = &u->ir->funcs[func.func];
        nargs = instr->nargs;
        args = (TcArg *)ir_alloc(
            u->an, (nargs == 0 ? 1u : nargs) * sizeof(TcArg));
        memset(args, 0, (nargs == 0 ? 1u : nargs) * sizeof(TcArg));
        args[0].value = func;
        for (i = 1; i < instr->nargs; i++) {
            if (i - 1u >= callee->sig.nparams) {
                return 0;
            }
            args[i].name = instr->args[i].name;
            if (!lower_operand(u, &instr->args[i].value,
                               callee->sig.params[i - 1u].type,
                               &args[i].value)) {
                return 0;
            }
        }
        break;
    }
    case TC_CALL_INDIRECT: {
        const TcType *signature = NULL;
        size_t i;
        nargs = instr->nargs;
        args = (TcArg *)ir_alloc(
            u->an, (nargs == 0 ? 1u : nargs) * sizeof(TcArg));
        memset(args, 0, (nargs == 0 ? 1u : nargs) * sizeof(TcArg));
        if (instr->nargs == 0 ||
            !lower_value_type(u, &instr->args[0].value, &signature) ||
            signature->tag != TC_TY_FNPTR) {
            return 0;
        }
        if (!lower_operand(u, &instr->args[0].value, signature,
                           &args[0].value)) {
            return 0;
        }
        for (i = 1; i < instr->nargs; i++) {
            if (i - 1u >= signature->fnptr.n) {
                return 0;
            }
            args[i].name = instr->args[i].name;
            if (!lower_operand(u, &instr->args[i].value,
                               signature->fnptr.params[i - 1u],
                               &args[i].value)) {
                return 0;
            }
        }
        break;
    }
    case TC_FUNCEQ:
    case TC_FUNCNE:
        nargs = 2;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (!lower_operand(u, &instr->args[1].value, type,
                           &args[0].value) ||
            !lower_operand(u, &instr->args[2].value, type,
                           &args[1].value)) {
            return 0;
        }
        break;
    case TC_MAKE: {
        TcStructDef *def = tc_sem_struct_for_type(u->an, type);
        size_t i;
        if (def == NULL || instr->nargs - 1u != def->nfields) {
            return 0;
        }
        nargs = def->nfields;
        args = (TcArg *)ir_alloc(
            u->an, (nargs == 0 ? 1u : nargs) * sizeof(TcArg));
        memset(args, 0, (nargs == 0 ? 1u : nargs) * sizeof(TcArg));
        if (instr->args[1].name != NULL) {
            for (i = 1; i < instr->nargs; i++) {
                const TcField *field =
                    tc_sem_find_field(def, instr->args[i].name);
                size_t index;
                if (field == NULL) {
                    return 0;
                }
                index = (size_t)(field - def->fields);
                args[index].name = instr->args[i].name;
                if (!lower_operand(u, &instr->args[i].value, field->type,
                                   &args[index].value)) {
                    return 0;
                }
            }
        } else {
            for (i = 0; i < def->nfields; i++) {
                if (!lower_operand(u, &instr->args[i + 1u].value,
                                   def->fields[i].type,
                                   &args[i].value)) {
                    return 0;
                }
            }
        }
        break;
    }
    case TC_BR:
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        if (!lower_label_operand(u, &instr->args[0].value, &args[0].value)) {
            return 0;
        }
        break;
    case TC_BR_IF: {
        const TcType *boolean = tc_type_scalar(u->an->types, TC_TY_BOOL);
        nargs = 2;
        args = (TcArg *)ir_alloc(u->an, nargs * sizeof(TcArg));
        memset(args, 0, nargs * sizeof(TcArg));
        if (!lower_operand(u, &instr->args[0].value, boolean,
                           &args[0].value) ||
            !lower_label_operand(u, &instr->args[1].value, &args[1].value)) {
            return 0;
        }
        break;
    }
    case TC_SWITCH: {
        size_t i;
        uint32_t *targets = (uint32_t *)ir_alloc(
            u->an, (instr->ntargets == 0 ? 1u : instr->ntargets) *
                        sizeof(uint32_t));
        const TcType *selector = NULL;
        if (instr->nargs < 2 || instr->ntargets != instr->nargs - 1u) {
            return 0;
        }
        if (instr->args[0].value.kind == TC_PVAL_NAME) {
            if (!lower_value_type(u, &instr->args[0].value, &selector)) {
                return 0;
            }
        } else if (instr->args[0].value.kind == TC_PVAL_INT) {
            selector = tc_type_scalar(
                u->an->types, instr->args[0].value.int_unsigned_suffix
                                  ? TC_TY_U64
                                  : TC_TY_I64);
        } else {
            return 0;
        }
        for (i = 0; i < instr->ntargets; i++) {
            TcOperand label;
            if (!lower_label_operand(u, &instr->switch_targets[i], &label)) {
                return 0;
            }
            targets[i] = label.label;
        }
        nargs = 1;
        args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
        memset(args, 0, sizeof(TcArg));
        if (!lower_operand(u, &instr->args[0].value, selector,
                           &args[0].value)) {
            return 0;
        }
        out.switch_targets = targets;
        out.ntargets = instr->ntargets;
        break;
    }
    case TC_RETURN:
        if (instr->nargs == 1) {
            const TcType *ret = u->unit->func_id == TC_UNIT_TOP_LEVEL
                                    ? NULL
                                    : u->ir->funcs[u->unit->func_id].sig.ret;
            nargs = 1;
            args = (TcArg *)ir_alloc(u->an, sizeof(TcArg));
            memset(args, 0, sizeof(TcArg));
            if (ret == NULL) {
                return 0;
            }
            if (!lower_operand(u, &instr->args[0].value, ret,
                               &args[0].value)) {
                return 0;
            }
        }
        break;
    case TC_TRAP:
        nargs = 0;
        break;
    default:
        return 0;
    }
    out.dst = dst;
    out.args = args;
    out.nargs = nargs;
    *append_instr(u, u->cur) = out;
    if (tc_opcode_is_terminator(op)) {
        u->terminated = 1;
    }
    return 1;
}

static uint32_t frame_object(IrBuild *b, const TcType *type, int writable,
                             uint64_t *offset) {
    TcObject object;
    int ok = 0;
    uint64_t size = tc_sem_type_size(b->an, type, &ok);
    uint64_t align = b->an->W / 8u;
    if (!ok) {
        b->an->impl = TC_IMPL_INTERNAL;
        return UINT32_MAX;
    }
    memset(&object, 0, sizeof(object));
    object.module = TC_OBJECT_NO_MODULE;
    object.region = TC_REG_FRAME;
    *offset = align_up(*offset, align);
    object.offset = *offset;
    object.size = size;
    object.type = type;
    object.lifetime = TC_LIFE_FRAME;
    object.init = NULL;
    object.writable = writable;
    object.parent = TC_OBJECT_NO_PARENT;
    *offset += size;
    return add_object(b, object);
}

static int build_unit(IrBuild *b, uint32_t module, TcMember *member,
                      uint32_t func_id, const TcParseFunc *func,
                      size_t func_member, const TcParseStmt *body,
                      size_t nbody) {
    UnitBuild u;
    TcUnit *unit;
    size_t i;
    size_t nparams = func != NULL ? func->nparams : 0;
    uint64_t frame_offset = 0;
    memset(&u, 0, sizeof(u));
    u.b = b;
    u.an = b->an;
    u.ir = b->ir;
    u.module = module;
    u.func = func;
    u.func_member = func_member;
    u.body = body;
    u.nbody = nbody;
    unit = add_unit(b);
    u.unit = unit;
    /* Label table (source order). */
    for (i = 0; i < nbody; i++) {
        if (body[i].kind == TC_STMT_LABEL) {
            u.nlabels++;
        }
    }
    u.labels = (const char **)ir_alloc(
        b->an, (u.nlabels == 0 ? 1u : u.nlabels) * sizeof(const char *));
    u.label_block = (uint32_t *)ir_alloc(
        b->an, (u.nlabels == 0 ? 1u : u.nlabels) * sizeof(uint32_t));
    {
        size_t li = 0;
        for (i = 0; i < nbody; i++) {
            if (body[i].kind == TC_STMT_LABEL) {
                u.labels[li] = body[i].label.name;
                u.label_block[li] = UINT32_MAX;
                li++;
            }
        }
    }
    /* Local table: parameters first, then var/const declarations. */
    u.nlocals = nparams + nbody;
    u.locals = (IrLocal *)ir_alloc(
        b->an, (u.nlocals == 0 ? 1u : u.nlocals) * sizeof(IrLocal));
    memset(u.locals, 0, (u.nlocals == 0 ? 1u : u.nlocals) * sizeof(IrLocal));
    u.nlocals = 0;
    unit->func_id = func_id;
    unit->entry = 0;
    /* Pre-assign parameter and var vregs so references are stable. */
    for (i = 0; i < nparams; i++) {
        const TcType *type = member != NULL ? member->sig.params[i].type
                                            : NULL;
        IrLocal *local = &u.locals[u.nlocals++];
        uint32_t object;
        local->name = func->params[i].name;
        local->type = type;
        local->is_param = 1;
        local->stmt_index = SIZE_MAX;
        if (type == NULL) {
            b->an->impl = TC_IMPL_INTERNAL;
            return 0;
        }
        object = frame_object(b, type, 0, &frame_offset);
        if (object == UINT32_MAX) {
            return 0;
        }
        local->object = object;
        local->vreg = alloc_vreg(&u, type, object);
    }
    for (i = 0; i < nbody; i++) {
        const TcParseStmt *stmt = &body[i];
        if (stmt->kind != TC_STMT_VAR && stmt->kind != TC_STMT_CONST) {
            continue;
        }
        {
            TcParseArg arg;
            const TcType *type;
            IrLocal *local = &u.locals[u.nlocals++];
            memset(&arg, 0, sizeof(arg));
            arg.kind = TC_PARG_TYPE;
            arg.type = stmt->kind == TC_STMT_VAR ? stmt->var.type
                                                 : stmt->cnst.type;
            type = tc_sem_resolve_type_arg(b->an, module, &arg);
            if (type == NULL) {
                b->an->impl = TC_IMPL_INTERNAL;
                return 0;
            }
            local->name = stmt->kind == TC_STMT_VAR ? stmt->var.name
                                                    : stmt->cnst.name;
            local->type = type;
            local->is_param = 0;
            local->stmt_index = i;
            if (stmt->kind == TC_STMT_VAR) {
                uint32_t object;
                if (func == NULL) {
                    if (i >= b->nprog_vars ||
                        b->prog_vars[i] == UINT32_MAX) {
                        b->an->impl = TC_IMPL_INTERNAL;
                        return 0;
                    }
                    object = b->prog_vars[i];
                } else {
                    object = frame_object(b, type, 1, &frame_offset);
                    if (object == UINT32_MAX) {
                        return 0;
                    }
                }
                local->object = object;
                local->vreg = alloc_vreg(&u, type, object);
            } else {
                local->is_const = 1;
                local->object = UINT32_MAX;
                local->vreg = UINT32_MAX;
            }
        }
    }
    /* Blocks and instructions. */
    u.cur = 0;
    u.terminated = 0;
    new_block(&u);
    for (i = 0; i < nbody; i++) {
        const TcParseStmt *stmt = &body[i];
        u.cur_stmt = i;
        if (stmt->kind == TC_STMT_LABEL) {
            int label = label_index_of(&u, stmt->label.name);
            TcBlock *current = block_at(&u, u.cur);
            if (label < 0) {
                b->an->impl = TC_IMPL_INTERNAL;
                return 0;
            }
            if (current->n != 0 || u.terminated) {
                u.cur = (uint32_t)u.nblocks;
                new_block(&u);
                u.terminated = 0;
            }
            if (u.label_block[(size_t)label] == UINT32_MAX) {
                u.label_block[(size_t)label] = u.cur;
            }
            continue;
        }
        if (stmt->kind == TC_STMT_INSTR || stmt->kind == TC_STMT_VAR) {
            const TcParseInstr *instr =
                stmt->kind == TC_STMT_VAR ? &stmt->var.init : &stmt->instr;
            if (u.terminated) {
                u.cur = (uint32_t)u.nblocks;
                new_block(&u);
                u.terminated = 0;
            }
            if (stmt->kind == TC_STMT_VAR) {
                IrLocal *local = find_local(&u, stmt->var.name, i + 1u);
                TcOperand dst;
                if (local == NULL || local->vreg == UINT32_MAX) {
                    b->an->impl = TC_IMPL_INTERNAL;
                    return 0;
                }
                memset(&dst, 0, sizeof(dst));
                dst.kind = TC_OP_VREG;
                dst.vreg = local->vreg;
                if (!lower_instr(&u, instr, &dst)) {
                    b->an->impl = TC_IMPL_INTERNAL;
                    return 0;
                }
            } else if (!lower_instr(&u, instr, NULL)) {
                b->an->impl = TC_IMPL_INTERNAL;
                return 0;
            }
        }
    }
    unit->blocks = u.blocks;
    unit->nblocks = u.nblocks;
    unit->entry = 0;
    unit->vreg_types = u.vreg_types;
    unit->nvregs = u.nvregs;
    unit->vreg_objects = u.vreg_objects;
    unit->nvreg_objects = u.nvregs;
    /* Patch label operands (label index -> block id). */
    {
        size_t bi;
        for (bi = 0; bi < u.nblocks; bi++) {
            TcBlock *block = &u.blocks[bi];
            size_t k;
            for (k = 0; k < block->n; k++) {
                TcInstr *instr = &block->instrs[k];
                size_t ai;
                for (ai = 0; ai < instr->nargs; ai++) {
                    if (instr->args[ai].value.kind == TC_OP_LABEL) {
                        uint32_t li = instr->args[ai].value.label;
                        if (li >= u.nlabels ||
                            u.label_block[li] == UINT32_MAX) {
                            b->an->impl = TC_IMPL_INTERNAL;
                            return 0;
                        }
                        instr->args[ai].value.label = u.label_block[li];
                    }
                }
                for (ai = 0; ai < instr->ntargets; ai++) {
                    uint32_t li = instr->switch_targets[ai];
                    if (li >= u.nlabels ||
                        u.label_block[li] == UINT32_MAX) {
                        b->an->impl = TC_IMPL_INTERNAL;
                        return 0;
                    }
                    instr->switch_targets[ai] = u.label_block[li];
                }
            }
        }
    }
    /* CFG edges. */
    {
        size_t bi;
        for (bi = 0; bi < u.nblocks; bi++) {
            TcBlock *block = &u.blocks[bi];
            uint32_t succs[8];
            size_t nsucc = 0;
            uint32_t fallthrough = bi + 1u < u.nblocks
                                       ? (uint32_t)(bi + 1u)
                                       : UINT32_MAX;
            int terminates = 0;
            if (block->n != 0) {
                TcInstr *last = &block->instrs[block->n - 1u];
                switch (last->op) {
                case TC_BR:
                    if (last->nargs == 1 &&
                        last->args[0].value.kind == TC_OP_LABEL) {
                        succs[nsucc++] = last->args[0].value.label;
                    }
                    terminates = 1;
                    break;
                case TC_BR_IF:
                    if (last->nargs == 2 &&
                        last->args[1].value.kind == TC_OP_LABEL) {
                        succs[nsucc++] = last->args[1].value.label;
                    }
                    if (fallthrough != UINT32_MAX) {
                        succs[nsucc++] = fallthrough;
                    }
                    terminates = 1;
                    break;
                case TC_SWITCH: {
                    size_t k;
                    for (k = 0; k < last->ntargets; k++) {
                        succs[nsucc++] = last->switch_targets[k];
                    }
                    terminates = 1;
                    break;
                }
                case TC_RETURN:
                case TC_TRAP:
                    terminates = 1;
                    break;
                default:
                    break;
                }
            }
            if (!terminates && fallthrough != UINT32_MAX) {
                succs[nsucc++] = fallthrough;
            }
            {
                uint32_t *unique = (uint32_t *)ir_alloc(
                    b->an, (nsucc == 0 ? 1u : nsucc) * sizeof(uint32_t));
                size_t count = 0;
                size_t k;
                for (k = 0; k < nsucc; k++) {
                    size_t j;
                    int seen = 0;
                    for (j = 0; j < count; j++) {
                        if (unique[j] == succs[k]) {
                            seen = 1;
                        }
                    }
                    if (!seen) {
                        unique[count++] = succs[k];
                    }
                }
                block->succs = unique;
                block->nsucc = count;
            }
        }
        for (bi = 0; bi < u.nblocks; bi++) {
            size_t k;
            for (k = 0; k < u.blocks[bi].nsucc; k++) {
                u.blocks[u.blocks[bi].succs[k]].npred++;
            }
        }
        {
            size_t total_preds = 0;
            uint32_t *pred_pool;
            size_t offset = 0;
            for (bi = 0; bi < u.nblocks; bi++) {
                total_preds += u.blocks[bi].npred;
            }
            pred_pool = (uint32_t *)ir_alloc(
                b->an, (total_preds == 0 ? 1u : total_preds) *
                           sizeof(uint32_t));
            for (bi = 0; bi < u.nblocks; bi++) {
                u.blocks[bi].preds = pred_pool + offset;
                offset += u.blocks[bi].npred;
                u.blocks[bi].npred = 0;
            }
            for (bi = 0; bi < u.nblocks; bi++) {
                size_t k;
                for (k = 0; k < u.blocks[bi].nsucc; k++) {
                    u.blocks[u.blocks[bi].succs[k]].preds[
                        u.blocks[u.blocks[bi].succs[k]].npred++] =
                        (uint32_t)bi;
                }
            }
        }
    }
    return 1;
}

static int build_units(IrBuild *b) {
    TcAnalysis *an = b->an;
    size_t mi;
    for (mi = 0; mi < an->nmodules; mi++) {
        TcParseModule *parse = an->modules[mi].info->parse;
        if (parse->is_program) {
            if (!build_unit(b, (uint32_t)mi, NULL, TC_UNIT_TOP_LEVEL, NULL,
                            SIZE_MAX, parse->body, parse->nbody)) {
                return 0;
            }
        }
    }
    for (mi = 0; mi < an->nmodules; mi++) {
        TcParseModule *parse = an->modules[mi].info->parse;
        size_t i;
        for (i = 0; i < parse->nfuncs; i++) {
            TcMember *member = tc_sem_find_member(&an->modules[mi],
                                                  parse->funcs[i].name);
            size_t fm = SIZE_MAX;
            size_t k;
            if (member == NULL || member->kind != TC_MEM_FUNC) {
                an->impl = TC_IMPL_INTERNAL;
                return 0;
            }
            for (k = 0; k < an->modules[mi].nmembers; k++) {
                if (&an->modules[mi].members[k] == member) {
                    fm = k;
                    break;
                }
            }
            {
                uint32_t fid = func_id_for(b, (uint32_t)mi,
                                           &parse->funcs[i]);
                if (fid == UINT32_MAX ||
                    !build_unit(b, (uint32_t)mi, member, fid,
                                &parse->funcs[i], fm,
                                parse->funcs[i].body,
                                parse->funcs[i].nbody)) {
                    return 0;
                }
                b->ir->funcs[fid].body = &b->units[b->nunits - 1u];
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Entry point.                                                              */
/* ------------------------------------------------------------------------- */

int tc_ir_build(TcAnalysis *an) {
    IrBuild b;
    if (an == NULL || an->ir != NULL || an->ct == NULL) {
        return -1;
    }
    memset(&b, 0, sizeof(b));
    b.an = an;
    b.ir = (TcIR *)ir_alloc(an, sizeof(TcIR));
    memset(b.ir, 0, sizeof(*b.ir));
    b.ir->types = an->types;
    b.ir->profile = an->profile;
    build_modules(&b);
    build_structs(&b);
    build_funcs(&b);
    if (an->impl != TC_IMPL_OK) {
        return -1;
    }
    build_consts(&b);
    build_static_objects(&b);
    if (an->impl != TC_IMPL_OK) {
        return -1;
    }
    if (!build_units(&b)) {
        return -1;
    }
    if (an->impl != TC_IMPL_OK) {
        return -1;
    }
    b.ir->objects = b.objects;
    b.ir->nobjects = b.nobjects;
    b.ir->consts = b.consts;
    b.ir->nconsts = b.nconsts;
    b.ir->units = b.units;
    b.ir->nunits = b.nunits;
    an->ir = b.ir;
    return 0;
}
