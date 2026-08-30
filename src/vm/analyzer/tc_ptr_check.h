/*
 * tc_ptr_check.h — 指针 RHS/语句静态验证
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

struct TcStructTable;

/** 校验指针相关 RHS；结果类型写入期望检查（load→pointee，算术→ptr，比较→bool 等） */
int tc_ptr_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                     const TcSymbolTable *global, const struct TcStructTable *struct_table,
                     TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                     TcWarningList *warnings, const char *self_name);

/**
 * 校验 ptr_store：指针绑定不可为常量/形参等只读；值类型匹配 pointee。
 */
int tc_ptr_check_store(const TcPtrStoreStmt *stmt, const TcSymbolTable *visible,
                       const TcSymbolTable *global, const struct TcStructTable *struct_table,
                       TcInitHistory *hist, size_t stmt_index, TcDiagnostic *diag,
                       TcWarningList *warnings);

/**
 * 从 RHS 推断 ptr 值所指外层绑定是否只读（let / static let / 形参）。
 * nullptr、函数返回值、未跟踪来源返回 0（允许 store，避免误报）。
 */
int tc_ptr_rhs_target_readonly(const TcRhs *rhs, const TcSymbolTable *visible,
                               const TcSymbolTable *global, size_t stmt_index);

/** ptr 操作数所指是否只读；无法解析时返回 0。 */
int tc_ptr_operand_target_readonly(const TcOperand *operand, const TcSymbolTable *visible,
                                   const TcSymbolTable *global, size_t stmt_index);

#endif /* TC_PTR_CHECK_H */
