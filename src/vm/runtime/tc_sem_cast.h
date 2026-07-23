/* tc_sem_cast.h — shared numeric, truncating, and bit-preserving conversions */
#ifndef TC_SEM_CAST_H
#define TC_SEM_CAST_H

#include "tc_types.h"

int tc_exec_cast(TcTypeKind target, const TcValue *source, TcValue *out,
                 TcDiagnostic *diag, int line);
int tc_exec_truncate(TcTypeKind target, const TcValue *source, TcValue *out,
                     TcDiagnostic *diag, int line);
int tc_exec_bitcast(TcTypeKind target, const TcValue *source, TcValue *out,
                    TcDiagnostic *diag, int line);

#endif /* TC_SEM_CAST_H */
