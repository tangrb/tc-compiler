/* TC 0.0.46 front-end diagnostics (S1).
 *
 * LEX/SYN report the first diagnostic immediately (standard §1.8/§12.1).
 * The position is always the primary position from standard §12.5.1; the
 * optional related position is reserved for later phases (S3/S4) and is not
 * produced by S1.
 */
#ifndef TC_FRONTEND_DIAG_H
#define TC_FRONTEND_DIAG_H

#include "diagnostic/codes.h"
#include "ir/span.h"

typedef struct {
    TcDiagCode code;
    TcStage stage; /* TC_STAGE_LEX or TC_STAGE_SYN */
    TcSpan span;
    int has_related;
    TcSpan related;
} TcFrontendDiag;

#endif /* TC_FRONTEND_DIAG_H */
