/* TC 0.0.46 frontend-internal analysis facts (standard §3-§5, §13).
 *
 * This header is private to `frontend/`: it is the analysis object and the
 * read-only facts that the CT evaluator and IR finalization consume after SEM
 * has accepted the program.  It is not a frozen contract and must not be
 * included outside `src/frontend/`.
 *
 * Ownership:
 *   - `sem.c` owns the analysis object, member tables, type interning, layout,
 *     SEM diagnostics and the final freeze;
 *   - `ct.c` owns the CT evaluation table (`analysis->ct`) and const/env
 *     values;
 *   - `irgen.c` owns the finalized `TcIR` (`analysis->ir`) built from the
 *     frozen type table, layout, CT results and the parse IR.
 */
#ifndef TC_FRONTEND_SEM_INTERNAL_H
#define TC_FRONTEND_SEM_INTERNAL_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "diagnostic/diagnostic.h"
#include "frontend/diag.h"
#include "frontend/module.h"
#include "frontend/parse.h"
#include "frontend/sem.h"
#include "ir/ir.h"
#include "ir/structs.h"
#include "ir/type.h"
#include "runtime/profile.h"

typedef enum {
    TC_MEM_STRUCT = 0,
    TC_MEM_CONST,
    TC_MEM_STATIC,
    TC_MEM_FUNC,
    TC_MEM_IMPORT
} TcMemberKind;

/* One declaration in a module namespace (standard §4.8). */
typedef struct TcMember {
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
    /* CT and IR stage state, owned by ct.c/irgen.c. */
    void *ct;
    TcIR *ir;
    TcPlatformProfile *profile; /* arena-owned copy bound to TcIR */
};

/* --------------------------------------------------------------------- */
/* Read-only facts published by sem.c for the later frontend stages.      */
/* --------------------------------------------------------------------- */

TcMember *tc_sem_find_member(TcModuleSem *module, const char *name);
TcMember *tc_sem_find_member_in(TcAnalysis *an, uint32_t module,
                                const char *name);
const TcField *tc_sem_find_field(const TcStructDef *def, const char *name);
TcStructDef *tc_sem_struct_for_type(TcAnalysis *an, const TcType *type);
unsigned tc_sem_type_bits(const TcAnalysis *an, const TcType *type);
uint64_t tc_sem_type_size(TcAnalysis *an, const TcType *type, int *ok);

/* Resolve a const_expr value path to its const declaration plus the remaining
 * field names (standard §3.10/§4.10).  Returns 0 when the value is not a
 * const reference.  Name/visibility/forward-reference diagnostics are SEM's;
 * after a successful analysis this only identifies the target. */
int tc_sem_resolve_const_path(TcAnalysis *an, uint32_t module_index,
                              const TcParseValue *v, TcMember **out_member,
                              uint32_t *out_module, const char ***out_fields,
                              size_t *out_nfields);
/* Resolve an opcode type argument after SEM.  NULL only on an internal
 * inconsistency (SEM already validated the argument). */
const TcType *tc_sem_resolve_type_arg(TcAnalysis *an, uint32_t module_index,
                                      const TcParseArg *arg);

/* Set the analysis result diagnostic.  Used by the CT stage (stage CT, no
 * related position).  `span->file` is copied into the analysis arena. */
void tc_sem_set_result_diag(TcAnalysis *an, TcDiagCode code, TcStage stage,
                            const TcSpan *span);

#endif /* TC_FRONTEND_SEM_INTERNAL_H */
