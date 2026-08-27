/*
 * tc_func_check.h — 函数签名 / funcall / return / 形参只读 / static
 *
 * 阶段 5：tc_func_check_signatures
 * 阶段 7/8：tc_func_check_funcall / tc_func_check_return（由 Pass2 调用）
 * 形参只读：tc_func_check_writable_target
 * static let/var：tc_func_eval_static_lets / tc_func_check_static_vars
 */
#ifndef TC_FUNC_CHECK_H
#define TC_FUNC_CHECK_H

#include "tc_analyzer_internal.h"
#include "tc_module.h"
#include "tc_scope.h"
#include "tc_struct_check.h"
#include "tc_types.h"
#include "tc_warning.h"

/** Pass2 / 调用图共享的函数检查环境 */
struct TcFuncCheckEnv {
    TcTypedProgram *prog;
    TcFuncSignatureList *sigs;
    TcMemberIndex *members;
    const TcFuncSignature *current_func; /* 当前函数体；顶层为 NULL */
    TcStructTable *struct_table;
};

/**
 * 阶段 5：函数重名、与顶层值绑定冲突、形参重名、形参名撞函数名。
 * 仅检查入口程序（本文件）；跨模块同名允许。
 */
int tc_func_check_signatures(TcTypedProgram *prog, const TcFuncSignatureList *sigs,
                             TcDiagnostic *diag);

/**
 * 解析调用目标：成功时 *out_sig 非空；失败已设 diag。
 * position：0=独立语句，1=var 初始化 / 赋值 RHS。
 */
int tc_func_resolve_call_target(const TcFuncCheckEnv *env, int is_self, const char *qualifier,
                                const char *member_name, const char *bare_target, int line,
                                const TcFuncSignature **out_sig, TcDiagnostic *diag);

/**
 * 检查一次 funcall（语句或 RHS）。
 * @param position 0=独立语句；1=接收非 void 的 var/赋值位置
 * @param expected 非 NULL 时校验结果类型（FUNCALL_RESULT_TYPE / 字面量类）
 */
int tc_func_check_funcall(const TcFuncCheckEnv *env, int is_self, const char *qualifier,
                          const char *member_name, const char *bare_target, TcNamedArg *args,
                          size_t arg_count, int position, const TcType *expected, int line,
                          const TcSymbolTable *visible, const TcSymbolTable *global,
                          TcInitHistory *hist, size_t stmt_index, TcWarningList *warnings,
                          int *resolved_func_id, TcDiagnostic *diag);

int tc_func_check_return(const TcFuncCheckEnv *env, TcReturnStmt *ret,
                         const TcSymbolTable *visible, const TcSymbolTable *global,
                         TcInitHistory *hist, size_t stmt_index, TcWarningList *warnings,
                         TcDiagnostic *diag);

/** 赋值 / read 目标不可为形参（PARAMETER_ASSIGNMENT） */
int tc_func_check_writable_target(const TcSymbol *target, int line, TcDiagnostic *diag);

/**
 * 裸名查找失败后的分类：命中本库 func/static → FUNCTION_SCOPE_ACCESS；
 * 否则 UNDEFINED_VARIABLE（由调用方沿用）。返回 1 表示已设 FUNCTION_SCOPE_ACCESS。
 */
int tc_func_try_function_scope_access(const TcMemberIndex *members, const char *name, int line,
                                      TcDiagnostic *diag);

struct TcStructTable;

/** H-5：对本库 static let 按依赖拓扑求值并写入符号表 */
int tc_func_eval_static_lets(TcProgram *program, TcSymbolTable *symbols,
                               const struct TcStructTable *struct_table, TcDiagnostic *diag);

/** H-6：校验 static var 初始化器操作数来源（不执行运行时求值） */
int tc_func_check_static_vars(TcProgram *program, const TcMemberIndex *members,
                              TcDiagnostic *diag);

#endif /* TC_FUNC_CHECK_H */
