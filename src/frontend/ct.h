/* TC 0.0.46 compile-time (CT) evaluation (standard §3.10, §4.10, §13.2.2).
 *
 * CT runs only after SEM has accepted the whole program and the type table is
 * frozen.  It evaluates `const` and `static var` initializers on demand in
 * `module_order`/module source order, caches success and failure, and reports
 * the first failure against the target declaration (standard §4.10/§12.5.1).
 *
 * This module is private to `frontend/`.  Values are type-labelled: scalars
 * keep the bit pattern of their own type width, structs keep a field tree in
 * declaration order.  `TcInitImage` byte images (target endianness) are
 * materialized only for `TcIR.consts`/`TcObject.init`.
 */
#ifndef TC_FRONTEND_CT_H
#define TC_FRONTEND_CT_H

#include "frontend/sem_internal.h"

typedef enum { TC_CT_SCALAR = 0, TC_CT_STRUCT } TcCtValueKind;

typedef struct TcCtValue {
    const TcType *type;
    TcCtValueKind kind;
    uint64_t bits;             /* scalar: type-width bit pattern */
    struct TcCtValue **fields; /* struct: declaration order */
    size_t nfields;
    const TcInitImage *image;  /* cached byte image; filled on demand */
} TcCtValue;

typedef struct {
    TcDiagCode code;
    TcSpan span; /* target declaration */
} TcCtDiagnostic;

/* Evaluate the whole CT stage.  Returns 1 on success, 0 on a language
 * diagnostic (written to *out_diag), and -1 on an implementation failure
 * (analysis->impl set; the caller must not report a language diagnostic). */
int tc_ct_run(TcAnalysis *an, TcCtDiagnostic *out_diag);

/* CT result lookup for IR finalization and unit tests.  Module-level values
 * are keyed by (module instance, declaration index in parse->consts); local
 * consts by (module, function member index or SIZE_MAX for the program body,
 * statement index). */
const TcCtValue *tc_ct_module_value(const TcAnalysis *an, uint32_t module,
                                    size_t decl_index);
const TcCtValue *tc_ct_local_value(const TcAnalysis *an, uint32_t module,
                                   size_t func_member, size_t stmt_index);

/* Successful declaration evaluations actually performed (cache misses). */
size_t tc_ct_eval_count(const TcAnalysis *an);

/* Materialize the value as a target byte image (cached in the value). */
const TcInitImage *tc_ct_value_image(TcAnalysis *an, const TcCtValue *value,
                                     int *ok);

const TcCtValue *tc_ct_module_value_by_member(const TcAnalysis *an,
                                              uint32_t module,
                                              const TcMember *member);

/* 1 for `static var`, 0 for `const`, -1 when the declaration is unknown. */
int tc_ct_decl_is_static(const TcAnalysis *an, uint32_t module,
                         size_t decl_index);

#endif /* TC_FRONTEND_CT_H */
