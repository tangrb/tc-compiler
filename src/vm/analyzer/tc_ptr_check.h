/*
 * tc_ptr_check.h — 指针 RHS/语句验证（Phase 3）
 *
 * 覆盖：ptr_load / ptr_address / 指针算术与比较 / ptr_size / ptr_store。
 * nullptr 仅允许作为指针操作数；不可对 let/static let 取址或经只读绑定 store。
 */
#ifndef TC_PTR_CHECK_H
#define TC_PTR_CHECK_H

#include "tc_types.h"
#include "tc_diagnostic.h"
#include "tc_warning.h"
#include "tc_symbol.h"
#include "tc_analyzer_internal.h"

/** 校验指针相关 RHS；结果类型写入期望检查（load→pointee，算术→ptr，比较→bool 等） */
int tc_ptr_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                     const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                     int line, TcDiagnostic *diag, TcWarningList *warnings,
                     const char *self_name);

/**
 * 校验 ptr_store：指针绑定不可为常量/形参等只读；值类型匹配 pointee。
 */
int tc_ptr_check_store(const TcPtrStoreStmt *stmt, const TcSymbolTable *visible,
                       const TcSymbolTable *global, TcInitHistory *hist, size_t stmt_index,
                       TcDiagnostic *diag, TcWarningList *warnings);

#endif /* TC_PTR_CHECK_H */
