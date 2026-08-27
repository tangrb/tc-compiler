/*
 * tc_func_check.c — 函数签名 / funcall / return / static let/var
 *
 * 阶段 5：tc_func_check_signatures
 * 阶段 7/8：tc_func_check_funcall / tc_func_check_return
 * 形参只读、本库成员裸名分类、static let 拓扑求值与 static var 操作数校验。
 */
#include "tc_func_check.h"

#include "tc_const_eval.h"
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
     * 命名实参规则（顺序敏感）：
     * 1) 无重复实参名  2) 名须在形参表  3) 形参须齐全
     * 4) 实参顺序与形参声明顺序一致  5) 各实参类型匹配
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
            if (diag->kind == TC_CE_TYPE_MISMATCH || diag->kind == TC_CE_LITERAL_TYPE ||
                diag->kind == TC_CE_LITERAL_OUT_OF_RANGE) {
                /* 字面量专用诊断保留，不降级为 ARGUMENT_TYPE */
                return -1;
            }
            if (diag->kind == TC_CE_TYPE_MISMATCH) {
                tc_diagnostic_set(diag, TC_CE_ARGUMENT_TYPE, line, TC_COLUMN_UNKNOWN,
                                  "argument type does not match parameter type");
            }
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  static let 依赖收集 / static var 操作数校验                          */
/* ------------------------------------------------------------------ */

typedef struct {
    size_t program_index;
    const TcStaticLetDef *def;
} TcStaticLetEntry;

static int tc_collect_self_member_names(const TcRhs *rhs, char ***names, size_t *count,
                                        size_t *capacity, TcDiagnostic *diag);

static int tc_static_var_operand_valid(const TcOperand *operand, int current_stmt_index,
                                       const TcMemberIndex *members, int line,
                                       TcDiagnostic *diag);

static int tc_static_var_rhs_valid(const TcRhs *rhs, int current_stmt_index,
                                   const TcMemberIndex *members, int line, TcDiagnostic *diag);

static int tc_name_list_push(char ***names, size_t *count, size_t *capacity, const char *name,
                             TcDiagnostic *diag) {
    char *copy = NULL;
    char **items = NULL;
    size_t i = 0;

    for (i = 0; i < *count; i++) {
        if ((*names)[i] && strcmp((*names)[i], name) == 0) {
            return 0;
        }
    }
    copy = strdup(name);
    if (!copy) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    if (*count == *capacity) {
        size_t new_cap = *capacity == 0 ? 4 : *capacity * 2;
        items = (char **)realloc(*names, new_cap * sizeof(char *));
        if (!items) {
            free(copy);
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        *names = items;
        *capacity = new_cap;
    }
    (*names)[*count] = copy;
    (*count)++;
    return 0;
}

static int tc_collect_self_from_operand(const TcOperand *operand, char ***names, size_t *count,
                                        size_t *capacity, TcDiagnostic *diag) {
    const char *base = NULL;

    if (!operand || operand->kind != TC_OPERAND_FIELD_READ) {
        return 0;
    }
    base = operand->u.field_read.base;
    if (base && strncmp(base, "Self.", 5) == 0 && base[5] != '\0') {
        return tc_name_list_push(names, count, capacity, base + 5, diag);
    }
    return 0;
}

static int tc_collect_self_member_names(const TcRhs *rhs, char ***names, size_t *count,
                                        size_t *capacity, TcDiagnostic *diag) {
    if (!rhs) {
        return 0;
    }
    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        if (!rhs->u.self_member.member_name) {
            return 0;
        }
        return tc_name_list_push(names, count, capacity, rhs->u.self_member.member_name, diag);
    }
    if (rhs->kind == TC_RHS_FIELD_READ) {
        const char *base = rhs->u.field_read.base;

        if (base && strncmp(base, "Self.", 5) == 0 && base[5] != '\0') {
            return tc_name_list_push(names, count, capacity, base + 5, diag);
        }
        return 0;
    }
    /* 常量运算操作数中的 Self.<名>.field（与 tc_static_var_rhs_valid 同类 RHS 对齐） */
    switch (rhs->kind) {
    case TC_RHS_ARITH:
        if (tc_collect_self_from_operand(&rhs->u.arith.lhs, names, count, capacity, diag) != 0) {
            return -1;
        }
        return tc_collect_self_from_operand(&rhs->u.arith.rhs, names, count, capacity, diag);
    case TC_RHS_UNARY:
        return tc_collect_self_from_operand(&rhs->u.unary.operand, names, count, capacity, diag);
    case TC_RHS_COMPARE:
        if (tc_collect_self_from_operand(&rhs->u.compare.lhs, names, count, capacity, diag) != 0) {
            return -1;
        }
        return tc_collect_self_from_operand(&rhs->u.compare.rhs, names, count, capacity, diag);
    case TC_RHS_LOGIC_BIN:
        if (tc_collect_self_from_operand(&rhs->u.logic_bin.lhs, names, count, capacity, diag) !=
            0) {
            return -1;
        }
        return tc_collect_self_from_operand(&rhs->u.logic_bin.rhs, names, count, capacity, diag);
    case TC_RHS_LOGIC_UN:
        return tc_collect_self_from_operand(&rhs->u.logic_un.operand, names, count, capacity,
                                            diag);
    case TC_RHS_BITWISE_BIN:
        if (tc_collect_self_from_operand(&rhs->u.bitwise_bin.lhs, names, count, capacity, diag) !=
            0) {
            return -1;
        }
        return tc_collect_self_from_operand(&rhs->u.bitwise_bin.rhs, names, count, capacity, diag);
    case TC_RHS_BITWISE_UN:
        return tc_collect_self_from_operand(&rhs->u.bitwise_un.operand, names, count, capacity,
                                            diag);
    case TC_RHS_SHIFT:
        if (tc_collect_self_from_operand(&rhs->u.shift.value, names, count, capacity, diag) != 0) {
            return -1;
        }
        return tc_collect_self_from_operand(&rhs->u.shift.count, names, count, capacity, diag);
    case TC_RHS_CAST:
        return tc_collect_self_from_operand(&rhs->u.cast.source, names, count, capacity, diag);
    case TC_RHS_CONST_CAST:
        return tc_collect_self_from_operand(&rhs->u.const_cast.source, names, count, capacity,
                                            diag);
    case TC_RHS_FLOAT_ARITH:
        if (tc_collect_self_from_operand(&rhs->u.float_arith.lhs, names, count, capacity, diag) !=
            0) {
            return -1;
        }
        return tc_collect_self_from_operand(&rhs->u.float_arith.rhs, names, count, capacity, diag);
    case TC_RHS_FLOAT_UNARY:
        return tc_collect_self_from_operand(&rhs->u.float_unary.operand, names, count, capacity,
                                            diag);
    case TC_RHS_FLOAT_COMPARE:
        if (tc_collect_self_from_operand(&rhs->u.float_compare.lhs, names, count, capacity,
                                         diag) != 0) {
            return -1;
        }
        return tc_collect_self_from_operand(&rhs->u.float_compare.rhs, names, count, capacity,
                                            diag);
    case TC_RHS_BITCAST:
        return tc_collect_self_from_operand(&rhs->u.bitcast.source, names, count, capacity, diag);
    case TC_RHS_STRUCT_CONSTRUCTOR: {
        size_t fi = 0;

        /* 构造器字段值可为字段读操作数或嵌套 RHS（如 Pair(a: Self.o.inner)） */
        for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
            if (rhs->u.struct_ctor.fields[fi].has_rhs) {
                if (tc_collect_self_member_names(
                        (const TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs, names, count,
                        capacity, diag) != 0) {
                    return -1;
                }
            } else if (tc_collect_self_from_operand(&rhs->u.struct_ctor.fields[fi].value_op,
                                                    names, count, capacity, diag) != 0) {
                return -1;
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

static int tc_static_var_operand_valid(const TcOperand *operand, int current_stmt_index,
                                       const TcMemberIndex *members, int line,
                                       TcDiagnostic *diag) {
    /* static var 初始化器操作数：允许字面量；禁止普通变量引用 */
    if (!operand) {
        return 0;
    }
    if (operand->kind == TC_OPERAND_LIT) {
        return 0;
    }
    if (operand->kind == TC_OPERAND_VAR) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "static var initializer has invalid operand");
        (void)current_stmt_index;
        (void)members;
        return -1;
    }
    if (operand->kind == TC_OPERAND_FIELD_READ) {
        const char *base = operand->u.field_read.base;

        if (operand->u.field_read.resolved.resolved) {
            if (operand->u.field_read.resolved.base_slot >= 0) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            return 0;
        }
        /* Pass2 之前：允许 Self.<更早的 static let/var>.field */
        if (base && strncmp(base, "Self.", 5) == 0 && base[5] != '\0') {
            const TcMemberEntry *entry = tc_member_index_find(members, base + 5);

            if (entry &&
                (entry->kind == TC_MEMBER_STATIC_LET || entry->kind == TC_MEMBER_STATIC_VAR) &&
                entry->stmt_index < current_stmt_index) {
                return 0;
            }
        }
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant expression cannot reference var variable");
        return -1;
    }
    return 0;
}

static int tc_static_var_rhs_valid(const TcRhs *rhs, int current_stmt_index,
                                   const TcMemberIndex *members, int line, TcDiagnostic *diag) {
    /*
     * static var RHS：字面量 / 更早的 Self.static_* / 标量常量表达式操作数。
     * Self 成员须 stmt_index < 当前（源序在前）；禁止 funcall / 普通标识符。
     */
    if (!rhs) {
        return 0;
    }
    if (rhs->kind == TC_RHS_FUNCALL_EXPR) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "static var initializer has invalid operand");
        return -1;
    }
    if (rhs->kind == TC_RHS_LIT) {
        return 0;
    }
    if (rhs->kind == TC_RHS_CONST_REF) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "static var initializer has invalid operand");
        return -1;
    }
    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        const TcMemberEntry *entry = NULL;
        const char *member = rhs->u.self_member.member_name;

        if (!member) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "static var initializer has invalid operand");
            return -1;
        }
        entry = tc_member_index_find(members, member);
        if (!entry ||
            (entry->kind != TC_MEMBER_STATIC_LET && entry->kind != TC_MEMBER_STATIC_VAR) ||
            entry->stmt_index >= current_stmt_index) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "static var initializer has invalid operand");
            return -1;
        }
        return 0;
    }
    if (rhs->kind == TC_RHS_FIELD_READ) {
        const char *base = rhs->u.field_read.base;

        if (rhs->u.field_read.resolved.resolved) {
            if (rhs->u.field_read.resolved.base_slot >= 0) {
                tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                                  "constant expression cannot reference var variable");
                return -1;
            }
            return 0;
        }
        if (base && strncmp(base, "Self.", 5) == 0 && base[5] != '\0') {
            const TcMemberEntry *entry = tc_member_index_find(members, base + 5);

            if (entry &&
                (entry->kind == TC_MEMBER_STATIC_LET || entry->kind == TC_MEMBER_STATIC_VAR) &&
                entry->stmt_index < current_stmt_index) {
                return 0;
            }
        }
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant expression cannot reference var variable");
        return -1;
    }
    switch (rhs->kind) {
    case TC_RHS_ARITH:
        if (tc_static_var_operand_valid(&rhs->u.arith.lhs, current_stmt_index, members, line,
                                        diag) != 0) {
            return -1;
        }
        return tc_static_var_operand_valid(&rhs->u.arith.rhs, current_stmt_index, members, line,
                                           diag);
    case TC_RHS_UNARY:
        return tc_static_var_operand_valid(&rhs->u.unary.operand, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_COMPARE:
        if (tc_static_var_operand_valid(&rhs->u.compare.lhs, current_stmt_index, members, line,
                                        diag) != 0) {
            return -1;
        }
        return tc_static_var_operand_valid(&rhs->u.compare.rhs, current_stmt_index, members, line,
                                           diag);
    case TC_RHS_LOGIC_BIN:
        if (tc_static_var_operand_valid(&rhs->u.logic_bin.lhs, current_stmt_index, members, line,
                                        diag) != 0) {
            return -1;
        }
        return tc_static_var_operand_valid(&rhs->u.logic_bin.rhs, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_LOGIC_UN:
        return tc_static_var_operand_valid(&rhs->u.logic_un.operand, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_BITWISE_BIN:
        if (tc_static_var_operand_valid(&rhs->u.bitwise_bin.lhs, current_stmt_index, members,
                                        line, diag) != 0) {
            return -1;
        }
        return tc_static_var_operand_valid(&rhs->u.bitwise_bin.rhs, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_BITWISE_UN:
        return tc_static_var_operand_valid(&rhs->u.bitwise_un.operand, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_SHIFT:
        if (tc_static_var_operand_valid(&rhs->u.shift.value, current_stmt_index, members, line,
                                        diag) != 0) {
            return -1;
        }
        return tc_static_var_operand_valid(&rhs->u.shift.count, current_stmt_index, members, line,
                                           diag);
    case TC_RHS_CAST:
        return tc_static_var_operand_valid(&rhs->u.cast.source, current_stmt_index, members, line,
                                           diag);
    case TC_RHS_CONST_CAST:
        return tc_static_var_operand_valid(&rhs->u.const_cast.source, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_FLOAT_ARITH:
        if (tc_static_var_operand_valid(&rhs->u.float_arith.lhs, current_stmt_index, members,
                                        line, diag) != 0) {
            return -1;
        }
        return tc_static_var_operand_valid(&rhs->u.float_arith.rhs, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_FLOAT_UNARY:
        return tc_static_var_operand_valid(&rhs->u.float_unary.operand, current_stmt_index,
                                           members, line, diag);
    case TC_RHS_FLOAT_COMPARE:
        if (tc_static_var_operand_valid(&rhs->u.float_compare.lhs, current_stmt_index, members,
                                        line, diag) != 0) {
            return -1;
        }
        return tc_static_var_operand_valid(&rhs->u.float_compare.rhs, current_stmt_index, members,
                                           line, diag);
    case TC_RHS_BITCAST:
        return tc_static_var_operand_valid(&rhs->u.bitcast.source, current_stmt_index, members,
                                           line, diag);
    default:
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "static var initializer has invalid operand");
        return -1;
    }
}

static int tc_static_let_index_by_name(const TcStaticLetEntry *entries, size_t count,
                                       const char *name) {
    size_t i = 0;

    for (i = 0; i < count; i++) {
        if (entries[i].def->name && strcmp(entries[i].def->name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * static let 拓扑求值早于 Pass2：常量 RHS 内的字段读操作数（如
 * add(int32, Self.s.x, 1)）此时尚未固化，须在 const_eval 前按正常检查
 * 路径逐层解析（tc_struct_check_field_access），否则 const_eval 报
 * 「invalid constant expression」。类型正确性仍由 const_eval 与 Pass2
 * 复核，故 expected 允许为 NULL（延迟到求值时校验）。
 */
static int tc_static_let_resolve_field_operand(TcOperand *operand, const TcType *expected,
                                               const TcStructTable *struct_table,
                                               TcSymbolTable *symbols, size_t stmt_index,
                                               int line, TcDiagnostic *diag) {
    if (!operand || operand->kind != TC_OPERAND_FIELD_READ) {
        return 0;
    }
    if (operand->u.field_read.resolved.resolved) {
        return 0;
    }
    return tc_struct_check_field_access(&operand->u.field_read, expected, struct_table, symbols,
                                        symbols, NULL, stmt_index, line, diag, NULL, NULL);
}

static int tc_static_let_resolve_field_operands(TcRhs *rhs, const TcType *expected,
                                                const TcStructTable *struct_table,
                                                TcSymbolTable *symbols, size_t stmt_index,
                                                int line, TcDiagnostic *diag) {
    if (!rhs) {
        return 0;
    }
    switch (rhs->kind) {
    case TC_RHS_FIELD_READ: {
        if (!rhs->u.field_read.resolved.resolved) {
            TcFieldAccess access;

            memset(&access, 0, sizeof(access));
            access.base = rhs->u.field_read.base;
            access.fields = rhs->u.field_read.fields;
            access.field_count = rhs->u.field_read.field_count;
            if (tc_struct_check_field_access(&access, expected, struct_table, symbols, symbols,
                                             NULL, stmt_index, line, diag, NULL, NULL) != 0) {
                return -1;
            }
            rhs->u.field_read.resolved = access.resolved;
            rhs->u.field_read.base = access.base;
            rhs->u.field_read.fields = access.fields;
            rhs->u.field_read.field_count = access.field_count;
        }
        return 0;
    }
    case TC_RHS_ARITH:
        if (tc_static_let_resolve_field_operand(&rhs->u.arith.lhs, rhs->u.arith.type, struct_table,
                                                symbols, stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.arith.rhs, rhs->u.arith.type,
                                                   struct_table, symbols, stmt_index, line, diag);
    case TC_RHS_UNARY:
        return tc_static_let_resolve_field_operand(&rhs->u.unary.operand, rhs->u.unary.type,
                                                   struct_table, symbols, stmt_index, line, diag);
    case TC_RHS_COMPARE:
        if (tc_static_let_resolve_field_operand(&rhs->u.compare.lhs, rhs->u.compare.type,
                                                struct_table, symbols, stmt_index, line,
                                                diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.compare.rhs, rhs->u.compare.type,
                                                   struct_table, symbols, stmt_index, line, diag);
    case TC_RHS_LOGIC_BIN:
        if (tc_static_let_resolve_field_operand(&rhs->u.logic_bin.lhs,
                                                tc_type_tag_singleton(TC_BOOL), struct_table,
                                                symbols, stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.logic_bin.rhs,
                                                   tc_type_tag_singleton(TC_BOOL), struct_table,
                                                   symbols, stmt_index, line, diag);
    case TC_RHS_LOGIC_UN:
        return tc_static_let_resolve_field_operand(&rhs->u.logic_un.operand,
                                                   tc_type_tag_singleton(TC_BOOL), struct_table,
                                                   symbols, stmt_index, line, diag);
    case TC_RHS_BITWISE_BIN:
        if (tc_static_let_resolve_field_operand(&rhs->u.bitwise_bin.lhs,
                                                rhs->u.bitwise_bin.type, struct_table, symbols,
                                                stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.bitwise_bin.rhs,
                                                   rhs->u.bitwise_bin.type, struct_table, symbols,
                                                   stmt_index, line, diag);
    case TC_RHS_BITWISE_UN:
        return tc_static_let_resolve_field_operand(&rhs->u.bitwise_un.operand,
                                                   rhs->u.bitwise_un.type, struct_table, symbols,
                                                   stmt_index, line, diag);
    case TC_RHS_SHIFT:
        if (tc_static_let_resolve_field_operand(&rhs->u.shift.value, rhs->u.shift.type,
                                                struct_table, symbols, stmt_index, line,
                                                diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.shift.count, rhs->u.shift.type,
                                                   struct_table, symbols, stmt_index, line, diag);
    case TC_RHS_FLOAT_ARITH:
        if (tc_static_let_resolve_field_operand(&rhs->u.float_arith.lhs,
                                                rhs->u.float_arith.type, struct_table, symbols,
                                                stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.float_arith.rhs,
                                                   rhs->u.float_arith.type, struct_table, symbols,
                                                   stmt_index, line, diag);
    case TC_RHS_FLOAT_UNARY:
        return tc_static_let_resolve_field_operand(&rhs->u.float_unary.operand,
                                                   rhs->u.float_unary.type, struct_table, symbols,
                                                   stmt_index, line, diag);
    case TC_RHS_FLOAT_COMPARE:
        if (tc_static_let_resolve_field_operand(&rhs->u.float_compare.lhs,
                                                rhs->u.float_compare.type, struct_table, symbols,
                                                stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.float_compare.rhs,
                                                   rhs->u.float_compare.type, struct_table,
                                                   symbols, stmt_index, line, diag);
    case TC_RHS_CONST_CAST:
        return tc_static_let_resolve_field_operand(&rhs->u.const_cast.source, NULL, struct_table,
                                                   symbols, stmt_index, line, diag);
    case TC_RHS_BITCAST:
        return tc_static_let_resolve_field_operand(&rhs->u.bitcast.source, NULL, struct_table,
                                                   symbols, stmt_index, line, diag);
    case TC_RHS_STRUCT_CONSTRUCTOR: {
        size_t fi = 0;

        for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
            if (rhs->u.struct_ctor.fields[fi].has_rhs) {
                if (tc_static_let_resolve_field_operands(
                        (TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs, NULL, struct_table,
                        symbols, stmt_index, line, diag) != 0) {
                    return -1;
                }
            } else if (tc_static_let_resolve_field_operand(
                           &rhs->u.struct_ctor.fields[fi].value_op, NULL, struct_table, symbols,
                           stmt_index, line, diag) != 0) {
                return -1;
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

static int tc_eval_one_static_let(TcSymbol *sym, TcRhs *rhs, TcSymbolTable *symbols,
                                  const TcStructTable *struct_table, TcDiagnostic *diag) {
    /* Self.member：直接拷贝已求值的常量；其它 RHS 走通用 const_eval */
    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        const char *member = rhs->u.self_member.member_name;
        const TcSymbol *src = NULL;

        if (!member) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, sym->def_line, TC_COLUMN_UNKNOWN,
                              "invalid static let initializer");
            return -1;
        }
        src = tc_symbol_table_find(symbols, member);
        if (!src || !src->has_const_value) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, sym->def_line, TC_COLUMN_UNKNOWN,
                              "constant value is not available by source order");
            return -1;
        }
        if (!tc_type_equals(src->type, sym->type)) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, sym->def_line, TC_COLUMN_UNKNOWN,
                              "static let type mismatch in Self member reference");
            return -1;
        }
        sym->const_value = src->const_value;
        sym->has_const_value = 1;
        return 0;
    }
    /*
     * Self.<名>.field / 字段读（含标量 RHS 内的字段操作数）：static let 求值
     * 早于 Pass2，须在此提前固化字段访问，以便 const_eval 能从已拓扑求值的
     * 基址 const_value 取字段。
     */
    if (tc_static_let_resolve_field_operands(rhs, sym->type, struct_table, symbols,
                                             (size_t)sym->def_stmt_index, sym->def_line,
                                             diag) != 0) {
        return -1;
    }
    return tc_resolve_const_value(sym, rhs, symbols, symbols, struct_table, sym->def_line, diag);
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
                    tc_diagnostic_set(diag, TC_CE_DUPLICATE_FUNCTION, func->line,
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
        *out_sig = tc_sig_find_in_module(env->sigs, -1, func_name);
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
        tc_diagnostic_set(diag, TC_CE_FUNCALL_POSITION, line, TC_COLUMN_UNKNOWN,
                          "void function call cannot be used as value");
        return -1;
    }

    if (tc_check_funcall_args(env, sig, args, arg_count, line, visible, global, hist, stmt_index,
                              warnings, diag) != 0) {
        return -1;
    }

    if (expected && !is_void && !tc_type_equals(&sig->return_type, expected)) {
        tc_diagnostic_set(diag, TC_CE_FUNCALL_RESULT_TYPE, line, TC_COLUMN_UNKNOWN,
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
                                                        stmt_index, ret->line, diag);
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

int tc_func_eval_static_lets(TcProgram *program, TcSymbolTable *symbols,
                               const TcStructTable *struct_table, TcDiagnostic *diag) {
    TcStaticLetEntry *entries = NULL;
    size_t entry_count = 0;
    size_t entry_cap = 0;

    /*
     * H-5：收集全部 static let，按 Self 依赖拓扑序求值写入符号表。
     * 成环 → CONSTANT_EXPRESSION；边 from→to 表示 to 依赖 from。
     */
    size_t i = 0;
    int *in_degree = NULL;
    int **adj = NULL;
    size_t *adj_cap = NULL;
    size_t *adj_count = NULL;
    int *queue = NULL;
    size_t q_head = 0;
    size_t q_tail = 0;
    size_t processed = 0;
    int rc = 0;

    if (!program || !symbols || !diag) {
        return -1;
    }

    for (i = 0; i < program->count; i++) {
        if (program->items[i].kind != TC_STMT_STATIC_LET_DEF) {
            continue;
        }
        if (entry_count == entry_cap) {
            size_t new_cap = entry_cap == 0 ? 4 : entry_cap * 2;
            TcStaticLetEntry *items =
                (TcStaticLetEntry *)realloc(entries, new_cap * sizeof(TcStaticLetEntry));
            if (!items) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                rc = -1;
                goto cleanup;
            }
            entries = items;
            entry_cap = new_cap;
        }
        entries[entry_count].program_index = i;
        entries[entry_count].def = &program->items[i].u.static_let_def;
        entry_count++;
    }

    if (entry_count == 0) {
        return 0;
    }

    in_degree = (int *)calloc(entry_count, sizeof(int));
    adj = (int **)calloc(entry_count, sizeof(int *));
    adj_cap = (size_t *)calloc(entry_count, sizeof(size_t));
    adj_count = (size_t *)calloc(entry_count, sizeof(size_t));
    queue = (int *)malloc(entry_count * sizeof(int));
    if (!in_degree || !adj || !adj_cap || !adj_count || !queue) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        rc = -1;
        goto cleanup;
    }

    for (i = 0; i < entry_count; i++) {
        char **deps = NULL;
        size_t dep_count = 0;
        size_t dep_cap = 0;
        size_t d = 0;

        if (tc_collect_self_member_names(&entries[i].def->rhs, &deps, &dep_count, &dep_cap,
                                         diag) != 0) {
            for (d = 0; d < dep_count; d++) {
                free(deps[d]);
            }
            free(deps);
            rc = -1;
            goto cleanup;
        }
        for (d = 0; d < dep_count; d++) {
            int from = tc_static_let_index_by_name(entries, entry_count, deps[d]);
            int to = (int)i;
            int *edge_items = NULL;

            if (from < 0) {
                free(deps[d]);
                continue;
            }
            if (adj_count[from] == adj_cap[from]) {
                size_t new_cap = adj_cap[from] == 0 ? 4 : adj_cap[from] * 2;
                edge_items = (int *)realloc(adj[from], new_cap * sizeof(int));
                if (!edge_items) {
                    size_t k = 0;
                    for (k = d; k < dep_count; k++) {
                        free(deps[k]);
                    }
                    free(deps);
                    tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                                      "memory allocation failed");
                    rc = -1;
                    goto cleanup;
                }
                adj[from] = edge_items;
                adj_cap[from] = new_cap;
            }
            adj[from][adj_count[from]++] = to;
            in_degree[to]++;
            free(deps[d]);
        }
        free(deps);
    }

    for (i = 0; i < entry_count; i++) {
        if (in_degree[i] == 0) {
            queue[q_tail++] = (int)i;
        }
    }

    while (q_head < q_tail) {
        int idx = queue[q_head++];
        TcSymbol *sym = NULL;
        size_t e = 0;

        sym = tc_symbol_table_find_mut(symbols, entries[idx].def->name);
        if (!sym) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, entries[idx].def->line,
                              TC_COLUMN_UNKNOWN, "static let symbol not found");
            rc = -1;
            goto cleanup;
        }
        if (tc_eval_one_static_let(sym, (TcRhs *)&entries[idx].def->rhs, symbols, struct_table,
                                   diag) != 0) {
            rc = -1;
            goto cleanup;
        }
        processed++;
        for (e = 0; e < adj_count[idx]; e++) {
            int to = adj[idx][e];
            in_degree[to]--;
            if (in_degree[to] == 0) {
                queue[q_tail++] = to;
            }
        }
    }

    if (processed != entry_count) {
        int line = entries[0].def ? entries[0].def->line : 1;
        size_t ei = 0;

        for (ei = 0; ei < entry_count; ei++) {
            if (entries[ei].def && entries[ei].def->line < line) {
                line = entries[ei].def->line;
            }
        }
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "circular static let dependency");
        rc = -1;
    }

cleanup:
    if (adj) {
        for (i = 0; i < entry_count; i++) {
            free(adj[i]);
        }
    }
    free(entries);
    free(in_degree);
    free(adj);
    free(adj_cap);
    free(adj_count);
    free(queue);
    return rc;
}

int tc_func_check_static_vars(TcProgram *program, const TcMemberIndex *members,
                              TcSymbolTable *symbols, const TcStructTable *struct_table,
                              TcDiagnostic *diag) {
    size_t i = 0;

    /* H-6：不执行运行时求值；校验初始化器操作数合法性并固化字段读，
     * 供 VM/AOT 运行期直接消费 resolved 元数据（base_slot / const_bits）。 */
    if (!program || !members || !symbols || !struct_table || !diag) {
        return -1;
    }
    for (i = 0; i < program->count; i++) {
        TcStatement *stmt = &program->items[i];

        if (stmt->kind != TC_STMT_STATIC_VAR_DEF) {
            continue;
        }
        if (tc_static_var_rhs_valid(&stmt->u.static_var_def.rhs, (int)i, members,
                                    stmt->u.static_var_def.line, diag) != 0) {
            return -1;
        }
        if (tc_static_let_resolve_field_operands((TcRhs *)&stmt->u.static_var_def.rhs,
                                                 &stmt->u.static_var_def.type, struct_table,
                                                 symbols, i, stmt->u.static_var_def.line,
                                                 diag) != 0) {
            return -1;
        }
    }
    return 0;
}
