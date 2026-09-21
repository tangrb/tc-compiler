/*
 * tc_static_init.c — static let 拓扑求值与 static var 初始化校验
 */
#include "tc_static_init.h"

#include "tc_const_eval.h"
#include "tc_analyzer_internal.h"
#include "tc_memblock_check.h"
#include "tc_diagnostic.h"
#include "tc_symbol.h"
#include "tc_type_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* 去重追加 Self 成员名（strdup）。失败时 diag 已填 OOM。 */
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

/*
 * 释放 names[0..count) 与数组本身。逐项 free 后须把槽位置 NULL，
 * 以便本函数在 error / 循环结束时收回剩余项且不 double-free。
 * cleanup: 不负责 deps —— 任何 goto cleanup 前必须先调用本函数。
 */
static void tc_name_list_free(char **names, size_t count) {
    size_t i = 0;

    if (!names) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
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

/*
 * static var 初始化器的源序可见性判定（语言标准 §4.2；编译器标准 §4.3）。
 *
 * 初始化器的操作数只可为字面量、当前源序中更早已成功初始化的 `Self` 成员
 * （`static let` / `static var`），以及经导入限定解析到的公开 `static let` /
 * `static var`。引用本模块中**源序更晚（或自身）**的静态成员时，该名称在
 * 源序可见性上尚未建立 → `TC_CE_UNDEFINED_VARIABLE`（与 §5.2.1 的前向引用
 * 口径一致，故静态成员之间不形成初始化环）；名称可见但不属于上述允许来源时
 * → `TC_CE_CONSTANT_EXPRESSION`。
 *
 * @return 1 表示已按 UNDEFINED_VARIABLE 报告（调用方应 return -1）；
 *         0 表示名称不是「更晚或自身」的静态成员（由调用方按各自规则处理）
 */
static int tc_static_var_report_forward_member(const char *member, int current_stmt_index,
                                               const TcMemberIndex *members, int line,
                                               TcDiagnostic *diag) {
    const TcMemberEntry *entry = NULL;
    char msg[128];

    if (!member || !members) {
        return 0;
    }
    entry = tc_member_index_find(members, member);
    if (!entry ||
        (entry->kind != TC_MEMBER_STATIC_LET && entry->kind != TC_MEMBER_STATIC_VAR)) {
        return 0;
    }
    if (entry->stmt_index < current_stmt_index) {
        return 0;
    }
    (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", member);
    tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, line, TC_COLUMN_UNKNOWN, msg);
    return 1;
}

/* 名称是否为「更早已成功初始化」的本模块静态成员（§4.2 允许来源之一）。 */
static int tc_static_var_member_is_prior(const char *member, int current_stmt_index,
                                         const TcMemberIndex *members) {
    const TcMemberEntry *entry = NULL;

    if (!member || !members) {
        return 0;
    }
    entry = tc_member_index_find(members, member);
    return entry &&
           (entry->kind == TC_MEMBER_STATIC_LET || entry->kind == TC_MEMBER_STATIC_VAR) &&
           entry->stmt_index < current_stmt_index;
}

/*
 * `Self.<名>` 限定标识符操作数（附录 A 的 operand 产生式、语言标准 §6.1.2）：
 * 按 §4.2 的源序可见性与允许来源判定。
 * 裸名不在 §4.2 的允许来源之列（只可为字面量、`Self.<更早的静态成员>` 与
 * 经导入限定解析到的公开静态成员），故一律报形态类诊断。
 * @return 0 允许；-1 已报告诊断
 */
static int tc_static_var_member_operand_valid(const char *name, int current_stmt_index,
                                              const TcMemberIndex *members, int line,
                                              TcDiagnostic *diag) {
    if (name && strncmp(name, "Self.", 5) == 0 && name[5] != '\0') {
        const char *member = name + 5;

        if (tc_static_var_member_is_prior(member, current_stmt_index, members)) {
            return 0;
        }
        /* 源序更晚（或自身）的本模块静态成员 → UNDEFINED_VARIABLE。 */
        if (tc_static_var_report_forward_member(member, current_stmt_index, members, line, diag)) {
            return -1;
        }
    }
    tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                      "static var initializer has invalid operand");
    return -1;
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
        return tc_static_var_member_operand_valid(operand->u.name, current_stmt_index, members,
                                                  line, diag);
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
            if (tc_static_var_member_is_prior(base + 5, current_stmt_index, members)) {
                return 0;
            }
            /* 源序更晚（或自身）的静态成员基址 → UNDEFINED_VARIABLE。 */
            if (tc_static_var_report_forward_member(base + 5, current_stmt_index, members, line,
                                                    diag)) {
                return -1;
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
        const char *member = rhs->u.self_member.member_name;

        if (!member) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                              "static var initializer has invalid operand");
            return -1;
        }
        if (tc_static_var_member_is_prior(member, current_stmt_index, members)) {
            return 0;
        }
        /* 源序更晚（或自身）的静态成员 → TC_CE_UNDEFINED_VARIABLE。 */
        if (tc_static_var_report_forward_member(member, current_stmt_index, members, line, diag)) {
            return -1;
        }
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "static var initializer has invalid operand");
        return -1;
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
            if (tc_static_var_member_is_prior(base + 5, current_stmt_index, members)) {
                return 0;
            }
            /* 源序更晚（或自身）的静态成员基址 → UNDEFINED_VARIABLE。 */
            if (tc_static_var_report_forward_member(base + 5, current_stmt_index, members, line,
                                                    diag)) {
                return -1;
            }
        }
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant expression cannot reference var variable");
        return -1;
    }
    if (rhs->kind == TC_RHS_MEMBLOCK_CONSTRUCTOR) {
        size_t i = 0;

        if (rhs->u.memblock_ctor.is_fill) {
            return tc_static_var_operand_valid(&rhs->u.memblock_ctor.fill_value, current_stmt_index,
                                               members, line, diag);
        }
        for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
            if (tc_static_var_operand_valid(&rhs->u.memblock_ctor.values[i], current_stmt_index,
                                            members, line, diag) != 0) {
                return -1;
            }
        }
        return 0;
    }
    if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        size_t fi = 0;

        for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
            if (rhs->u.struct_ctor.fields[fi].has_rhs) {
                if (tc_static_var_rhs_valid((const TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs,
                                            current_stmt_index, members, line, diag) != 0) {
                    return -1;
                }
            } else if (tc_static_var_operand_valid(&rhs->u.struct_ctor.fields[fi].value_op,
                                                   current_stmt_index, members, line, diag) != 0) {
                return -1;
            }
        }
        return 0;
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
 * static let / static var 的求值早于 Pass2：常量 RHS 内的字段读操作数（如
 * add(int32, Self.s.x, 1)）与限定标识符操作数（`Self.<名>` / `<模块名>.<名>`，
 * 附录 A 的 operand 产生式、语言标准 §6.1.2）此时尚未固化，
 * 须在求值前按正常检查路径逐层解析（tc_struct_check_field_access /
 * tc_find_named_binding），否则 const_eval 报「invalid constant expression」、
 * 运行时按运行时语义求值时绑 Binding 缺失（internal error）。类型正确性仍由
 * const_eval 与 Pass2 复核，故 expected 允许为 NULL（延迟到求值时校验）。
 */
static int tc_static_let_resolve_field_operand(TcOperand *operand, const TcType *expected,
                                               const TcStructTable *struct_table,
                                               TcSymbolTable *symbols, const TcMemberIndex *members,
                                               size_t stmt_index, int line, TcDiagnostic *diag) {
    if (!operand) {
        return 0;
    }
    if (operand->kind == TC_OPERAND_VAR) {
        const TcSymbol *symbol = NULL;

        if (operand->binding.resolved || !operand->u.name ||
            strchr(operand->u.name, '.') == NULL) {
            return 0;
        }
        /* [语言标准 §4.4]：跨模块 private 成员在常量求值前即给专用码 */
        if (tc_reject_private_member_access(operand->u.name, symbols, line, diag)) {
            return -1;
        }
        symbol = tc_find_named_binding(symbols, symbols, operand->u.name, members);
        if (!symbol) {
            return 0; /* 未解析：由后续名称检查报告 */
        }
        tc_resolved_binding_set(&operand->binding, symbol);
        return 0;
    }
    if (operand->kind != TC_OPERAND_FIELD_READ) {
        return 0;
    }
    if (operand->u.field_read.resolved.resolved) {
        return 0;
    }
    {
        TcInitHistory name_hist;

        memset(&name_hist, 0, sizeof(name_hist));
        name_hist.name_members = members;
        return tc_struct_check_field_access(&operand->u.field_read, expected, struct_table,
                                            symbols, symbols, &name_hist, stmt_index, line, diag,
                                            NULL, NULL);
    }
}

static int tc_static_let_resolve_field_operands(TcRhs *rhs, const TcType *expected,
                                                const TcStructTable *struct_table,
                                                TcSymbolTable *symbols, const TcMemberIndex *members,
                                                size_t stmt_index, int line, TcDiagnostic *diag) {
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
            {
                TcInitHistory name_hist;

                memset(&name_hist, 0, sizeof(name_hist));
                name_hist.name_members = members;
                if (tc_struct_check_field_access(&access, expected, struct_table, symbols, symbols,
                                                 &name_hist, stmt_index, line, diag, NULL,
                                                 NULL) != 0) {
                    return -1;
                }
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
                                                symbols, members, stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.arith.rhs, rhs->u.arith.type,
                                                   struct_table, symbols, members, stmt_index, line, diag);
    case TC_RHS_UNARY:
        return tc_static_let_resolve_field_operand(&rhs->u.unary.operand, rhs->u.unary.type,
                                                   struct_table, symbols, members, stmt_index, line, diag);
    case TC_RHS_COMPARE:
        if (tc_static_let_resolve_field_operand(&rhs->u.compare.lhs, rhs->u.compare.type,
                                                struct_table, symbols, members, stmt_index, line,
                                                diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.compare.rhs, rhs->u.compare.type,
                                                   struct_table, symbols, members, stmt_index, line, diag);
    case TC_RHS_LOGIC_BIN:
        if (tc_static_let_resolve_field_operand(&rhs->u.logic_bin.lhs,
                                                tc_type_tag_singleton(TC_BOOL), struct_table,
                                                symbols, members, stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.logic_bin.rhs,
                                                   tc_type_tag_singleton(TC_BOOL), struct_table,
                                                   symbols, members, stmt_index, line, diag);
    case TC_RHS_LOGIC_UN:
        return tc_static_let_resolve_field_operand(&rhs->u.logic_un.operand,
                                                   tc_type_tag_singleton(TC_BOOL), struct_table,
                                                   symbols, members, stmt_index, line, diag);
    case TC_RHS_BITWISE_BIN:
        if (tc_static_let_resolve_field_operand(&rhs->u.bitwise_bin.lhs,
                                                rhs->u.bitwise_bin.type, struct_table, symbols, members,
                                                stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.bitwise_bin.rhs,
                                                   rhs->u.bitwise_bin.type, struct_table, symbols, members,
                                                   stmt_index, line, diag);
    case TC_RHS_BITWISE_UN:
        return tc_static_let_resolve_field_operand(&rhs->u.bitwise_un.operand,
                                                   rhs->u.bitwise_un.type, struct_table, symbols, members,
                                                   stmt_index, line, diag);
    case TC_RHS_SHIFT:
        if (tc_static_let_resolve_field_operand(&rhs->u.shift.value, rhs->u.shift.type,
                                                struct_table, symbols, members, stmt_index, line,
                                                diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.shift.count, rhs->u.shift.type,
                                                   struct_table, symbols, members, stmt_index, line, diag);
    case TC_RHS_FLOAT_ARITH:
        if (tc_static_let_resolve_field_operand(&rhs->u.float_arith.lhs,
                                                rhs->u.float_arith.type, struct_table, symbols, members,
                                                stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.float_arith.rhs,
                                                   rhs->u.float_arith.type, struct_table, symbols, members,
                                                   stmt_index, line, diag);
    case TC_RHS_FLOAT_UNARY:
        return tc_static_let_resolve_field_operand(&rhs->u.float_unary.operand,
                                                   rhs->u.float_unary.type, struct_table, symbols, members,
                                                   stmt_index, line, diag);
    case TC_RHS_FLOAT_COMPARE:
        if (tc_static_let_resolve_field_operand(&rhs->u.float_compare.lhs,
                                                rhs->u.float_compare.type, struct_table, symbols, members,
                                                stmt_index, line, diag) != 0) {
            return -1;
        }
        return tc_static_let_resolve_field_operand(&rhs->u.float_compare.rhs,
                                                   rhs->u.float_compare.type, struct_table,
                                                   symbols, members, stmt_index, line, diag);
    case TC_RHS_CONST_CAST:
        return tc_static_let_resolve_field_operand(&rhs->u.const_cast.source, NULL, struct_table,
                                                   symbols, members, stmt_index, line, diag);
    case TC_RHS_BITCAST:
        return tc_static_let_resolve_field_operand(&rhs->u.bitcast.source, NULL, struct_table,
                                                   symbols, members, stmt_index, line, diag);
    case TC_RHS_STRUCT_CONSTRUCTOR: {
        size_t fi = 0;

        for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
            if (rhs->u.struct_ctor.fields[fi].has_rhs) {
                if (tc_static_let_resolve_field_operands(
                        (TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs, NULL, struct_table,
                        symbols, members, stmt_index, line, diag) != 0) {
                    return -1;
                }
            } else if (tc_static_let_resolve_field_operand(
                           &rhs->u.struct_ctor.fields[fi].value_op, NULL, struct_table, symbols, members,
                           stmt_index, line, diag) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case TC_RHS_MEMBLOCK_CONSTRUCTOR: {
        /*
         * `count:` 的 `usize_operand` 名须在求值前固化：`static let` 的编译期
         * 求值（第 6b 阶段）早于 Pass2，此时 `count_name` 尚未解析，会被当成
         * `count == 0` 而误报「count must be at least 1」。来源不合法
         * （`var` / `static var`）在此即按 §5.2.1 报常量错误。
         */
        uint64_t count = 0;

        if (rhs->u.memblock_ctor.count_name) {
            if (tc_memblock_resolve_count_name(rhs->u.memblock_ctor.count_name, symbols, symbols,
                                               stmt_index, line, diag, &count, members, 0) != 0) {
                return -1;
            }
            rhs->u.memblock_ctor.count = count;
            free(rhs->u.memblock_ctor.count_name);
            rhs->u.memblock_ctor.count_name = NULL;
        }
        return 0;
    }
    default:
        return 0;
    }
}

static int tc_eval_one_static_let(TcSymbol *sym, TcRhs *rhs, TcSymbolTable *symbols,
                                  const TcStructTable *struct_table, const char *module_name,
                                  const TcMemberIndex *members, TcDiagnostic *diag) {
    /* Self.member：直接拷贝已求值的常量；其它 RHS 走通用 const_eval */
    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        const char *member = rhs->u.self_member.member_name;
        const TcSymbol *src = NULL;
        char msg[128];

        if (!member) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, sym->def_line, TC_COLUMN_UNKNOWN,
                              "invalid static let initializer");
            return -1;
        }
        src = tc_symbol_table_find(symbols, member);
        /*
         * [语言标准 §4.3、§4.4]：`Self.<名>` 只解析本模块成员。符号表全模块共享，
         * 故须按模块标记排除其它模块的同名成员（含 private）——否则跨模块
         * `Self.X` 会取到别的模块的常量值。
         */
        if (src && (!src->module_name || !module_name ||
                    strcmp(src->module_name, module_name) != 0)) {
            src = NULL;
        }
        if (!src) {
            (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", member);
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, sym->def_line, TC_COLUMN_UNKNOWN,
                              msg);
            return -1;
        }
        /*
         * `static let` 初始化器不得引用 `static var`（语言标准 §5.2.1、§4.3；
         * 编译器标准 §4.3「允许来源」）：`static var` 是可变绑定，其值只能在
         * 程序准备阶段按运行时语义求值，不能作为编译期常量来源。
         */
        if (src->sym_kind != TC_SYM_CONSTANT) {
            tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, sym->def_line, TC_COLUMN_UNKNOWN,
                              "constant expression cannot reference var variable");
            return -1;
        }
        if (!src->has_const_value) {
            /* 派生失败：挂起的 CT 类诊断仍是首个规范诊断。 */
            if (tc_const_value_withheld(src, diag)) {
                return -1;
            }
            tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, sym->def_line, TC_COLUMN_UNKNOWN,
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
    if (tc_static_let_resolve_field_operands(rhs, sym->type, struct_table, symbols, members,
                                             (size_t)sym->def_stmt_index, sym->def_line,
                                             diag) != 0) {
        return -1;
    }
    return tc_resolve_const_value(sym, rhs, symbols, symbols, struct_table, members, sym->def_line, diag);
}

int tc_func_eval_static_lets(TcProgram *program, TcSymbolTable *symbols,
                               const TcStructTable *struct_table, TcTypeTable *type_table,
                               const TcMemberIndex *members, TcDiagnostic *diag) {
    TcStaticLetEntry *entries = NULL;
    size_t entry_count = 0;
    size_t entry_cap = 0;

    /*
     * 收集全部 static let，按 Self 依赖拓扑序求值写入符号表。
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

    /*
     * static let 求值早于 Pass2 语句检查：初始化器里的 `Self.<名>` 经 `members`
     * 显式下传，只解析本模块成员（§4.3、§4.4）。
     */

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
        /* 无 static let：仍走 cleanup 释放可能已分配的表 */
        goto cleanup;
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
            tc_name_list_free(deps, dep_count);
            rc = -1;
            goto cleanup;
        }
        for (d = 0; d < dep_count; d++) {
            int from = tc_static_let_index_by_name(entries, entry_count, deps[d]);
            int to = (int)i;
            int *edge_items = NULL;

            if (from < 0) {
                /* 非 static let 名：丢掉本槽，其余仍由 tc_name_list_free 收回 */
                free(deps[d]);
                deps[d] = NULL;
                continue;
            }
            /*
             * §4.2、§5.2.1：`static let` 初始化器只能引用**源序更早**且已成功
             * 初始化的成员。引用自身或源序更晚的成员时，该名称在源序可见性上尚未
             * 建立 → TC_CE_UNDEFINED_VARIABLE；标准 §5.2.1 明确「不定义常量循环依赖
             * 错误，前向引用与自引用统一由 TC_CE_UNDEFINED_VARIABLE 处理」，故此处
             * 不再把这类引用当作依赖边去报「循环依赖」。
             */
            if (entries[from].program_index >= entries[to].program_index) {
                char msg[160];

                if (entries[from].program_index == entries[to].program_index) {
                    (void)snprintf(msg, sizeof(msg), "undefined variable '%s'", deps[d]);
                } else {
                    (void)snprintf(msg, sizeof(msg),
                                   "constant value is not available by source order");
                }
                tc_diagnostic_set(diag, TC_CE_UNDEFINED_VARIABLE, entries[to].def->line,
                                  TC_COLUMN_UNKNOWN, msg);
                tc_name_list_free(deps, dep_count);
                rc = -1;
                goto cleanup;
            }
            if (adj_count[from] == adj_cap[from]) {
                size_t new_cap = adj_cap[from] == 0 ? 4 : adj_cap[from] * 2;
                edge_items = (int *)realloc(adj[from], new_cap * sizeof(int));
                if (!edge_items) {
                    tc_name_list_free(deps, dep_count);
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
            deps[d] = NULL;
        }
        tc_name_list_free(deps, dep_count);
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
        /*
         * `memblock<T, N>` 的命名 N 须在此固化：`static let` 的编译期求值早于
         * Pass2，若等到 Pass2 才解析，以 `.count` 为基础的常量会被静默算成 0
         * （类型已在 Pass1 按 count=0 intern）。解析后重新 intern 并回写符号类型。
         */
        if (entries[idx].def->type.tag == TC_MEMBLOCK ||
            entries[idx].def->type.tag == TC_PTR) {
            const TcType *interned = NULL;

            if (tc_memblock_resolve_type_counts((TcType *)&entries[idx].def->type, symbols,
                                                symbols, (size_t)sym->def_stmt_index,
                                                entries[idx].def->line, diag, members, 0) != 0) {
                rc = -1;
                goto cleanup;
            }
            if (type_table) {
                interned = tc_type_intern(type_table, (TcType *)&entries[idx].def->type, diag);
                if (!interned) {
                    rc = -1;
                    goto cleanup;
                }
                sym->type = interned;
            }
        }
        if (tc_eval_one_static_let(sym, (TcRhs *)&entries[idx].def->rhs, symbols, struct_table,
                                   program->module_name, members, diag) != 0) {
            /*
             * CT 类（常量求值）诊断已挂起：按语言标准 §11「阶段优先」
             * 继续求值其余 static let——更晚处理阶段的 SEM 类
             * 诊断（可达性、确定初始化、调用图）优先于 CT 类诊断。
             * 本符号以「无常量值」状态保留，依赖它的常量按各自的规则报错。
             */
            if (tc_diagnostic_is_set(diag) || !tc_diagnostic_has_deferred(diag)) {
                rc = -1;
                goto cleanup;
            }
            sym->has_const_value = 0;
            sym->ct_eval_failed = 1;
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
    int rc = 0;

    /* 不执行运行时求值；校验初始化器操作数合法性并固化字段读，
     * 供 VM/AOT 运行期直接消费 resolved 元数据（base_slot / const_bits）。 */
    if (!program || !members || !symbols || !struct_table || !diag) {
        return -1;
    }
    /*
     * 初始化器里的 `Self.<名>` 须按**本模块**成员索引解析（§4.3、§4.4）：本阶段
     * 早于 Pass2，故把 `members` 显式传入字段解析。
     */
    for (i = 0; i < program->count; i++) {
        TcStatement *stmt = &program->items[i];

        if (stmt->kind != TC_STMT_STATIC_VAR_DEF) {
            continue;
        }
        if (tc_static_var_rhs_valid(&stmt->u.static_var_def.rhs, (int)i, members,
                                    stmt->u.static_var_def.line, diag) != 0) {
            rc = -1;
            break;
        }
        if (tc_static_let_resolve_field_operands((TcRhs *)&stmt->u.static_var_def.rhs,
                                                 &stmt->u.static_var_def.type, struct_table,
                                                 symbols, members, i, stmt->u.static_var_def.line,
                                                 diag) != 0) {
            rc = -1;
            break;
        }
    }
    return rc;
}
