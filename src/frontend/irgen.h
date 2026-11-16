/* TC 0.0.46 IR finalization (design §3).  Private to `frontend/`. */
#ifndef TC_FRONTEND_IRGEN_H
#define TC_FRONTEND_IRGEN_H

#include "frontend/sem_internal.h"

/* Materialize `analysis->ir` from the frozen SEM facts and CT results.
 * Returns 0 on success, -1 on an implementation failure (analysis->impl set). */
int tc_ir_build(TcAnalysis *an);

#endif /* TC_FRONTEND_IRGEN_H */
