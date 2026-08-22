/*
 * tc_analyzer_pass2_rhs.h — Pass2 RHS 语义检查（tc_analyzer_pass2_rhs.c）
 *
 * 非公共 API；供 tc_analyzer_pass2.c 的语句检查分发调用。
 */
#ifndef TC_ANALYZER_PASS2_RHS_H
#define TC_ANALYZER_PASS2_RHS_H

#include "tc_analyzer_internal.h" /* TcAnalyzeCtx / TcInitHistory */
#include "tc_scope.h" /* TcMemberIndex */
#include "tc_symbol.h"
#include "tc_types.h"
#include "tc_warning.h"
#include "tc_diagnostic.h"

const TcSymbol *tc_resolve_visible_symbol_scoped(const TcSymbolTable *visible,
                                       const TcSymbolTable *global, const char *name,
                                       size_t stmt_index, int line, TcDiagnostic *diag,
                                       const TcMemberIndex *members, int in_function);
const TcSymbol *tc_find_symbol_by_def_index(const TcSymbolTable *global, const char *name,
                                            int def_stmt_index);
int tc_pass2_resolve_target_type(TcInitHistory *hist, const TcType *owned,
                                const TcType **out, int line, TcDiagnostic *diag);
int tc_precheck_rhs_names(TcRhs *rhs, const TcSymbolTable *visible,
                          const TcSymbolTable *global, size_t stmt_index, int line,
                          TcDiagnostic *diag, const char *self_name);
int tc_check_operand(TcOperand *operand, TcTypeTag expected,
                     const TcSymbolTable *visible, const TcSymbolTable *global,
                     TcInitHistory *hist, size_t stmt_index, int line,
                     TcDiagnostic *diag, TcWarningList *warnings,
                     const char *self_name, TcErrorKind type_err);
int tc_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                 const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                 int line, TcDiagnostic *diag, TcWarningList *warnings,
                 const char *self_name);
int tc_check_condition(TcRhs *rhs, const TcSymbolTable *visible,
                       const TcSymbolTable *global, TcInitHistory *hist,
                       size_t stmt_index, int line, const char *owner,
                       TcDiagnostic *diag, TcWarningList *warnings);
int tc_visible_copy_from(const TcSymbolTable *src, TcSymbolTable *dst,
                         TcDiagnostic *diag);
int tc_visible_add_from_global(const TcSymbolTable *global, const char *name,
                               int def_stmt_index, TcSymbolTable *visible,
                               TcDiagnostic *diag);
int tc_pass2_check_funcall_rhs(TcRhs *rhs, const TcType *expected, int position,
                               TcAnalyzeCtx *ctx, const TcSymbolTable *visible,
                               const TcSymbolTable *symbols, TcInitHistory *hist,
                               size_t stmt_index, int line, TcWarningList *warnings,
                               TcDiagnostic *diag);

#endif /* TC_ANALYZER_PASS2_RHS_H */
