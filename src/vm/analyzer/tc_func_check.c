/*
 * tc_func_check.c — 函数签名 / funcall / return / 形参只读
 *
 * 阶段 5：tc_func_check_signatures
 * 阶段 7/8：tc_func_check_funcall / tc_func_check_return
 * 形参只读、本库成员裸名分类。static let/var 见 tc_static_init.c。
 */
#include "tc_func_check.h"

#include "tc_analyzer_internal.h"
#include "tc_diagnostic.h"
#include "tc_symbol.h"
#include "tc_type_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  内部辅助                                                            */
/* ------------------------------------------------------------------ */

static int tc_entry_module_index(const TcProgram *program) {
    /* 入口为 #lib 时签名 module_index=-1；#program 入口无本库函数签名（-2 哨兵） */
    if (program && program->mode == TC_MODULE_LIB) {
        return -1;
    }
    return -2;
}

/** 供 Pass2 编排按模块切换 `Self.<函数名>` 解析上下文（入口 / 各 dep）。 */
int tc_func_env_module_index(const TcProgram *program) {
    return tc_entry_module_index(program);
}

static const TcFuncSignature *tc_sig_find_in_module(const TcFuncSignatureList *sigs,
                                                    int module_index, const char *name) {
    size_t i = 0;

    if (!sigs || !name) {
        return NULL;
    }
    for (i = 0; i < sigs->count; i++) {
        if (sigs->items[i].module_index == module_index &&
            sigs->items[i].name && strcmp(sigs->items[i].name, name) == 0) {
            return &sigs->items[i];
        }
    }
    return NULL;
}

static int tc_func_name_in_module(const TcFuncSignatureList *sigs, int module_index,
                                  const char *name) {
    return tc_sig_find_in_module(sigs, module_index, name) != NULL;
}

static int tc_dep_index_by_name(const TcTypedProgram *prog, const char *qualifier) {
    size_t i = 0;

    if (!prog || !qualifier) {
        return -1;
    }
    for (i = 0; i < prog->dep_count; i++) {
        if (prog->deps[i].module_name &&
            strcmp(prog->deps[i].module_name, qualifier) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static const char *tc_func_call_name(const char *member_name, const char *bare_target) {
    if (member_name && member_name[0] != '\0') {
        return member_name;
    }
    return bare_target;
}

static int tc_is_value_binding_stmt(TcStmtKind kind) {
    return kind == TC_STMT_VAR_DEF || kind == TC_STMT_CONST_DEF ||
           kind == TC_STMT_STATIC_VAR_DEF || kind == TC_STMT_STATIC_LET_DEF;
}

static const char *tc_value_binding_name(const TcStatement *stmt) {
    switch (stmt->kind) {
    case TC_STMT_VAR_DEF:
        return stmt->u.var_def.name;
    case TC_STMT_CONST_DEF:
        return stmt->u.const_def.name;
    case TC_STMT_STATIC_VAR_DEF:
        return stmt->u.static_var_def.name;
    case TC_STMT_STATIC_LET_DEF:
        return stmt->u.static_let_def.name;
    default:
        return NULL;
    }
}

static int tc_value_binding_line(const TcStatement *stmt) {
    switch (stmt->kind) {
    case TC_STMT_VAR_DEF:
        return stmt->u.var_def.line;
    case TC_STMT_CONST_DEF:
        return stmt->u.const_def.line;
    case TC_STMT_STATIC_VAR_DEF:
        return stmt->u.static_var_def.line;
    case TC_STMT_STATIC_LET_DEF:
        return stmt->u.static_let_def.line;
    default:
        return 0;
    }
}

static int tc_check_func_params(const TcFuncDef *func, const TcFuncSignatureList *sigs,
                                int module_index, TcDiagnostic *diag) {
    size_t i = 0;
    size_t j = 0;
    char msg[128];

    for (i = 0; i < func->param_count; i++) {
        for (j = i + 1; j < func->param_count; j++) {
            if (func->params[i].name && func->params[j].name &&
                strcmp(func->params[i].name, func->params[j].name) == 0) {
                (void)snprintf(msg, sizeof(msg), "duplicate parameter '%s'",
                               func->params[i].name);
                tc_diagnostic_set(diag, TC_CE_DUPLICATE_PARAMETER, func->line,
                                  TC_COLUMN_UNKNOWN, msg);
                return -1;
            }
        }
    }
    for (i = 0; i < func->param_count; i++) {
        if (func->params[i].name &&
            tc_func_name_in_module(sigs, module_index, func->params[i].name)) {
            (void)snprintf(msg, sizeof(msg), "parameter '%s' conflicts with function name",
                           func->params[i].name);
            tc_diagnostic_set(diag, TC_CE_FUNCTION_NAME_CONFLICT, func->line,
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }
    return 0;
}

static int tc_check_value_binding_func_conflicts(const TcProgram *program,
                                                 const TcFuncSignatureList *sigs,
                                                 int module_index, TcDiagnostic *diag) {
    size_t i = 0;
    char msg[128];

    if (module_index < -1) {
        return 0;
    }
    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];
        const char *name = NULL;

        if (!tc_is_value_binding_stmt(stmt->kind)) {
            continue;
        }
        name = tc_value_binding_name(stmt);
        if (!name) {
            continue;
        }
        if (tc_func_name_in_module(sigs, module_index, name)) {
            (void)snprintf(msg, sizeof(msg),
                           "function name conflicts with value binding '%s'", name);
            tc_diagnostic_set(diag, TC_CE_FUNCTION_NAME_CONFLICT, tc_value_binding_line(stmt),
                              TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }
    return 0;
}

static int tc_check_funcall_args(const TcFuncCheckEnv *env, const TcFuncSignature *sig,
                                 TcNamedArg *args, size_t arg_count, int line,
                                 const TcSymbolTable *visible, const TcSymbolTable *global,
                                 TcInitHistory *hist, size_t stmt_index,
                                 TcWarningList *warnings, TcDiagnostic *diag) {
    size_t i = 0;
    size_t j = 0;
    size_t pi = 0;
    char msg[128];

    /*
     * 命名实参规则（顺序敏感，编译器标准 §8.2 第 3 条）：
     * 1) 无重复实参名  2) 名须在形参表  3) 形参须齐全  4) 数量超限
     * 5) 实参顺序与形参声明顺序一致  6) 各实参类型匹配
     * 第 4 步仅在全部实参名称已知时判定（§8.2.2），不得抢先于 1)~3)。
     */
    for (i = 0; i < arg_count; i++) {
        for (j = i + 1; j < arg_count; j++) {
            if (args[i].param_name && args[j].param_name &&
                strcmp(args[i].param_name, args[j].param_name) == 0) {
                (void)snprintf(msg, sizeof(msg), "duplicate argument '%s'",
                               args[j].param_name);
                tc_diagnostic_set(diag, TC_CE_DUPLICATE_ARGUMENT, line, TC_COLUMN_UNKNOWN,
                                  msg);
                return -1;
            }
        }
    }

    for (i = 0; i < arg_count; i++) {
        int known = 0;

        if (!args[i].param_name) {
            tc_diagnostic_set(diag, TC_CE_UNKNOWN_ARGUMENT, line, TC_COLUMN_UNKNOWN,
                              "unknown argument");
            return -1;
        }
        for (pi = 0; pi < sig->param_count; pi++) {
            if (sig->params[pi].name &&
                strcmp(args[i].param_name, sig->params[pi].name) == 0) {
                known = 1;
                break;
            }
        }
        if (!known) {
            (void)snprintf(msg, sizeof(msg), "unknown argument '%s'", args[i].param_name);
            tc_diagnostic_set(diag, TC_CE_UNKNOWN_ARGUMENT, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }

    for (pi = 0; pi < sig->param_count; pi++) {
        int present = 0;

        for (i = 0; i < arg_count; i++) {
            if (sig->params[pi].name && args[i].param_name &&
                strcmp(args[i].param_name, sig->params[pi].name) == 0) {
                present = 1;
                break;
            }
        }
        if (!present) {
            (void)snprintf(msg, sizeof(msg), "missing argument '%s'", sig->params[pi].name);
            tc_diagnostic_set(diag, TC_CE_MISSING_ARGUMENT, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }

    if (arg_count > sig->param_count) {
        /* 实参个数多于形参，且到此已通过重复/未知/缺失检查（即全部名称已知）：
         * 多余实参专用码（语言标准 §8.2.2 表、附录 B.12）。 */
        tc_diagnostic_set(diag, TC_CE_EXTRA_ARGUMENT, line, TC_COLUMN_UNKNOWN,
                          "too many arguments for function");
        return -1;
    }

    for (i = 0; i < arg_count; i++) {
        if (i >= sig->param_count || !sig->params[i].name || !args[i].param_name ||
            strcmp(args[i].param_name, sig->params[i].name) != 0) {
            tc_diagnostic_set(diag, TC_CE_ARGUMENT_ORDER, line, TC_COLUMN_UNKNOWN,
                              "argument order does not match parameter order");
            return -1;
        }
    }

    for (i = 0; i < arg_count; i++) {
        TcRhs *value = &args[i].value;

        if (tc_type_check_rhs(value, &sig->params[i].type, visible, global, env->struct_table,
                              hist, stmt_index, line, diag, warnings, NULL) != 0) {
            return -1;
        }
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/*  公开接口                                                            */
/* ------------------------------------------------------------------ */

int tc_func_check_signatures(TcTypedProgram *prog, const TcFuncSignatureList *sigs,
                             TcDiagnostic *diag) {
    size_t i = 0;
    int module_index = 0;
    char msg[128];

    if (!prog || !sigs || !diag) {
        return -1;
    }
    module_index = tc_entry_module_index(&prog->program);

    for (i = 0; i < prog->program.count; i++) {
        TcStatement *stmt = &prog->program.items[i];

        if (stmt->kind != TC_STMT_FUNC_DEF) {
            continue;
        }
        {
            TcFuncDef *func = &stmt->u.func_def;
            size_t j = 0;

            for (j = 0; j < i; j++) {
                const TcStatement *prev = &prog->program.items[j];
                if (prev->kind == TC_STMT_FUNC_DEF &&
                    prev->u.func_def.name && func->name &&
                    strcmp(prev->u.func_def.name, func->name) == 0) {
                    (void)snprintf(msg, sizeof(msg), "duplicate function '%s'", func->name);
                    tc_diagnostic_set(diag, TC_CE_FUNCTION_NAME_CONFLICT, func->line,
                                      TC_COLUMN_UNKNOWN, msg);
                    return -1;
                }
            }
            if (tc_check_func_params(func, sigs, module_index, diag) != 0) {
                return -1;
            }
        }
    }

    return tc_check_value_binding_func_conflicts(&prog->program, sigs, module_index, diag);
}

int tc_func_resolve_call_target(const TcFuncCheckEnv *env, int is_self, const char *qualifier,
                                const char *member_name, const char *bare_target, int line,
                                const TcFuncSignature **out_sig, TcDiagnostic *diag) {
    const char *func_name = tc_func_call_name(member_name, bare_target);
    char msg[128];

    /*
     * 解析优先级：
     *   Self.f     → 本库成员索引 + module_index=-1 签名
     *   Mod.f      → deps 中模块 + 拒绝 private
     *   裸名 f     → 若撞本库成员则 FUNCTION_SCOPE_ACCESS，否则 UNDEFINED_FUNCTION
     * （#program 顶层无本库 Self；跨库裸名调用不允许）
     */
    if (!env || !out_sig || !diag || !func_name) {
        return -1;
    }
    *out_sig = NULL;

    if (is_self) {
        const TcMemberEntry *entry = NULL;

        if (!env->members) {
            (void)snprintf(msg, sizeof(msg), "undefined function '%s'", func_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_FUNCTION, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        entry = tc_member_index_find(env->members, func_name);
        if (!entry || entry->kind != TC_MEMBER_FUNC) {
            (void)snprintf(msg, sizeof(msg), "undefined function '%s'", func_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_FUNCTION, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        *out_sig = tc_sig_find_in_module(env->sigs, env->module_index, func_name);
        if (!*out_sig) {
            (void)snprintf(msg, sizeof(msg), "undefined function '%s'", func_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_FUNCTION, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        return 0;
    }

    if (qualifier && qualifier[0] != '\0') {
        int dep_index = tc_dep_index_by_name(env->prog, qualifier);
        const TcFuncSignature *sig = NULL;

        if (dep_index < 0) {
            (void)snprintf(msg, sizeof(msg), "undefined function '%s'", func_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_FUNCTION, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        sig = tc_sig_find_in_module(env->sigs, dep_index, func_name);
        if (!sig) {
            (void)snprintf(msg, sizeof(msg), "undefined function '%s'", func_name);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_FUNCTION, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
        if (sig->visibility == TC_VIS_PRIVATE) {
            tc_diagnostic_set(diag, TC_CE_PRIVATE_MEMBER_ACCESS, line, TC_COLUMN_UNKNOWN,
                              "private member access");
            return -1;
        }
        *out_sig = sig;
        return 0;
    }

    if (env->members) {
        const TcMemberEntry *entry = tc_member_index_find(env->members, func_name);
        if (entry && entry->kind == TC_MEMBER_FUNC) {
            (void)snprintf(msg, sizeof(msg), "function scope access: use Self.%s", func_name);
            tc_diagnostic_set(diag, TC_CE_FUNCTION_SCOPE_ACCESS, line, TC_COLUMN_UNKNOWN, msg);
            return -1;
        }
    }

    (void)snprintf(msg, sizeof(msg), "undefined function '%s'", func_name);
    tc_diagnostic_set(diag, TC_CE_UNDEFINED_FUNCTION, line, TC_COLUMN_UNKNOWN, msg);
    return -1;
}

int tc_func_check_funcall(const TcFuncCheckEnv *env, int is_self, const char *qualifier,
                          const char *member_name, const char *bare_target, TcNamedArg *args,
                          size_t arg_count, int position, const TcType *expected, int line,
                          const TcSymbolTable *visible, const TcSymbolTable *global,
                          TcInitHistory *hist, size_t stmt_index, TcWarningList *warnings,
                          int *resolved_func_id, TcDiagnostic *diag) {
    const TcFuncSignature *sig = NULL;
    int is_void = 0;

    /*
     * position=0：独立语句 → 返回类型须 void
     * position=1：值位置（var/赋值）→ 不可为 void，且匹配 expected
     */
    if (!env || !diag) {
        return -1;
    }

    if (tc_func_resolve_call_target(env, is_self, qualifier, member_name, bare_target, line, &sig,
                                    diag) != 0) {
        return -1;
    }

    is_void = tc_type_is_void(sig->return_type.tag);
    if (position == 0 && !is_void) {
        tc_diagnostic_set(diag, TC_CE_FUNCALL_POSITION, line, TC_COLUMN_UNKNOWN,
                          "non-void function call must be used as initializer or assignment");
        return -1;
    }
    if (position == 1 && is_void) {
        tc_diagnostic_set(diag, TC_CE_FUNCALL_RESULT_TYPE, line, TC_COLUMN_UNKNOWN,
                          "void function call cannot be used as value");
        return -1;
    }

    if (tc_check_funcall_args(env, sig, args, arg_count, line, visible, global, hist, stmt_index,
                              warnings, diag) != 0) {
        return -1;
    }

    if (expected && !is_void && !tc_type_equals(&sig->return_type, expected)) {
        tc_diagnostic_set(diag, TC_CE_TYPE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "function call result type does not match");
        return -1;
    }
    /* memblock 返回值：N 规划个数必须与接收类型一致（§6.7.1），
     * tc_type_equals 忽略 N，需在此补充检查 */
    if (expected && !is_void && tc_type_memblock_count_mismatch(&sig->return_type, expected)) {
        tc_diagnostic_set(diag, TC_CE_MEMBLOCK_SIZE_MISMATCH, line, TC_COLUMN_UNKNOWN,
                          "memblock size mismatch in function call result");
        return -1;
    }
    if (resolved_func_id) {
        *resolved_func_id = sig->func_id;
    }
    return 0;
}

int tc_func_check_return(const TcFuncCheckEnv *env, TcReturnStmt *ret,
                         const TcSymbolTable *visible, const TcSymbolTable *global,
                         TcInitHistory *hist, size_t stmt_index, TcWarningList *warnings,
                         TcDiagnostic *diag) {
    const TcType *return_type = NULL;
    int is_void = 0;

    /* 须在函数体内；void ↔ 有/无返回值形态；有值时类型匹配 return_type */
    if (!env || !ret || !diag) {
        return -1;
    }
    if (!env->current_func) {
        tc_diagnostic_set(diag, TC_CE_RETURN_OUTSIDE_FUNCTION, ret->line, TC_COLUMN_UNKNOWN,
                          "return outside function");
        return -1;
    }

    return_type = &env->current_func->return_type;
    is_void = tc_type_is_void(return_type->tag);

    if (is_void && ret->has_value) {
        tc_diagnostic_set(diag, TC_CE_RETURN_FORM, ret->line, TC_COLUMN_UNKNOWN,
                          "void function cannot return a value");
        return -1;
    }
    if (!is_void && !ret->has_value) {
        tc_diagnostic_set(diag, TC_CE_RETURN_FORM, ret->line, TC_COLUMN_UNKNOWN,
                          "non-void function must return a value");
        return -1;
    }

    if (!ret->has_value) {
        return 0;
    }

    if (ret->value.kind == TC_OPERAND_LIT) {
        return tc_type_check_literal(&ret->value.u.lit, return_type, ret->line, diag);
    }
    if (ret->value.kind == TC_OPERAND_VAR) {
        const TcSymbol *sym = tc_resolve_visible_symbol(visible, global, ret->value.u.name,
                                                        stmt_index, ret->line, diag,
                                                        tc_hist_name_members(hist),
                                                        tc_hist_name_in_function(hist));
        if (!sym) {
            return -1;
        }
        if (!tc_type_equals(sym->type, return_type)) {
            tc_diagnostic_set(diag, TC_CE_RETURN_TYPE, ret->line, TC_COLUMN_UNKNOWN,
                              "return type does not match function return type");
            return -1;
        }
        /* memblock 返回值：N 规划个数必须与函数返回类型一致（§8.3.1），
         * tc_type_equals 忽略 N，需在此补充检查 */
        if (tc_type_memblock_count_mismatch(sym->type, return_type)) {
            tc_diagnostic_set(diag, TC_CE_MEMBLOCK_SIZE_MISMATCH, ret->line, TC_COLUMN_UNKNOWN,
                              "memblock size mismatch in return value");
            return -1;
        }
        tc_resolved_binding_set(&ret->value.binding, sym);
        if (tc_check_operand_init(hist, sym, stmt_index, ret->line, diag) != 0) {
            return -1;
        }
        return 0;
    }

    return tc_check_operand(&ret->value, return_type->tag, visible, global, env->struct_table, hist,
                            stmt_index, ret->line, diag, warnings, NULL, TC_CE_RETURN_TYPE);
}

int tc_func_check_writable_target(const TcSymbol *target, int line, TcDiagnostic *diag) {
    if (!target || !diag) {
        return -1;
    }
    if (target->slot_domain == TC_SLOT_PARAM) {
        tc_diagnostic_set(diag, TC_CE_PARAMETER_ASSIGNMENT, line, TC_COLUMN_UNKNOWN,
                          "cannot assign to function parameter");
        return -1;
    }
    return 0;
}

int tc_func_try_function_scope_access(const TcMemberIndex *members, const char *name, int line,
                                      TcDiagnostic *diag) {
    const TcMemberEntry *entry = NULL;
    char msg[128];

    if (!members || !name || !diag) {
        return 0;
    }
    entry = tc_member_index_find(members, name);
    if (!entry) {
        return 0;
    }
    if (entry->kind == TC_MEMBER_FUNC || entry->kind == TC_MEMBER_STATIC_VAR ||
        entry->kind == TC_MEMBER_STATIC_LET) {
        (void)snprintf(msg, sizeof(msg), "function scope access: use Self.%s", name);
        tc_diagnostic_set(diag, TC_CE_FUNCTION_SCOPE_ACCESS, line, TC_COLUMN_UNKNOWN, msg);
        return 1;
    }
    return 0;
}
