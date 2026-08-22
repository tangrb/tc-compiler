/*
 * tc_type_check.h — 完整类型 RHS/字面量检查入口
 *
 * 按期望 TcType 分派到 ptr / memblock / struct 专用检查器；
 * 标量走 tc_check_rhs / tc_check_literal。
 *
 * 调用方：tc_analyzer_pass2（var/let/赋值等需要完整类型的位置）。
 */
#ifndef TC_TYPE_CHECK_H
#define TC_TYPE_CHECK_H

#include "tc_struct_check.h"
#include "tc_types.h"
#include "tc_diagnostic.h"
#include "tc_warning.h"
#include "tc_symbol.h"
#include "tc_analyzer_internal.h"

/**
 * 按期望完整类型校验字面量。
 * 覆盖 nullptr（仅 ptr）、浮点/bool 上下文、无符号后缀与有符号冲突、范围检查。
 */
int tc_type_check_literal(const TcLiteral *lit, const TcType *expected, int line,
                          TcDiagnostic *diag);

/**
 * 按期望完整类型校验 RHS。
 * ptr/memblock/struct 相关 kind 委托专用模块；其余标量委托 tc_check_rhs。
 * @param self_name 正在初始化的绑定名（用于禁止自引用）；可为 NULL
 */
int tc_type_check_rhs(TcRhs *rhs, const TcType *expected, const TcSymbolTable *visible,
                      const TcSymbolTable *global, const TcStructTable *struct_table,
                      TcInitHistory *hist, size_t stmt_index, int line, TcDiagnostic *diag,
                      TcWarningList *warnings, const char *self_name);

#endif /* TC_TYPE_CHECK_H */
