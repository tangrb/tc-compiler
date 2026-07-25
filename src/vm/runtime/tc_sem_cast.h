/*
 * tc_sem_cast.h — 数值转换（strict cast）、窄化截断（truncate）、位重解释（bitcast）
 *
 * strict cast 检测溢出报 TC_RE_CAST_OVERFLOW；truncate 直接取低位不检溢出；
 * bitcast 要求源目标位宽相等且非 bool。Executor/AOT/const_eval 均经此接口。
 */
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
