/*
 * tc_scope.c — 本库成员索引与 Self 使用检查
 *
 * 与 tc_module.c 协作：
 *   tc_module_check_structure 末尾调用 tc_scope_check_self_usage；
 *   成员索引供后续限定名 / private 访问解析（Phase 2 仅建表与查找）。
 */
#include "tc_scope.h"

#include "tc_diagnostic.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  成员索引                                                            */
/* ------------------------------------------------------------------ */

void tc_member_index_init(TcMemberIndex *index) {
    index->items = NULL;
    index->count = 0;
    index->capacity = 0;
}

void tc_member_index_free(TcMemberIndex *index) {
    size_t i = 0;
    if (!index) {
        return;
    }
    for (i = 0; i < index->count; i++) {
        free(index->items[i].name);
    }
    free(index->items);
    index->items = NULL;
    index->count = 0;
    index->capacity = 0;
}

static int tc_member_index_push(TcMemberIndex *index, const char *name, TcMemberKind kind,
                                TcVisibility visibility, int stmt_index, TcDiagnostic *diag) {
    TcMemberEntry *items;
    char *copy;

    if (index->count == index->capacity) {
        size_t new_cap = index->capacity == 0 ? 8 : index->capacity * 2;
        items = (TcMemberEntry *)realloc(index->items, new_cap * sizeof(TcMemberEntry));
        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        index->items = items;
        index->capacity = new_cap;
    }
    copy = strdup(name);
    if (!copy) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    index->items[index->count].name = copy;
    index->items[index->count].kind = kind;
    index->items[index->count].visibility = visibility;
    index->items[index->count].stmt_index = stmt_index;
    index->count++;
    return 0;
}

int tc_member_index_build(const TcProgram *program, TcMemberIndex *out, TcDiagnostic *diag) {
    size_t i = 0;

    tc_member_index_init(out);
    if (!program) {
        return 0;
    }
    /* 仅索引库级顶层成员；普通 var/let / 可执行语句不入表 */
    for (i = 0; i < program->count; i++) {
        const TcStatement *stmt = &program->items[i];
        if (stmt->kind == TC_STMT_FUNC_DEF) {
            if (tc_member_index_push(out, stmt->u.func_def.name, TC_MEMBER_FUNC,
                                     stmt->u.func_def.visibility, (int)i, diag) != 0) {
                tc_member_index_free(out);
                return -1;
            }
        } else if (stmt->kind == TC_STMT_STATIC_VAR_DEF) {
            if (tc_member_index_push(out, stmt->u.static_var_def.name, TC_MEMBER_STATIC_VAR,
                                     stmt->u.static_var_def.visibility, (int)i, diag) != 0) {
                tc_member_index_free(out);
                return -1;
            }
        } else if (stmt->kind == TC_STMT_STATIC_LET_DEF) {
            if (tc_member_index_push(out, stmt->u.static_let_def.name, TC_MEMBER_STATIC_LET,
                                     stmt->u.static_let_def.visibility, (int)i, diag) != 0) {
                tc_member_index_free(out);
                return -1;
            }
        } else if (stmt->kind == TC_STMT_STRUCT_DEF) {
            if (tc_member_index_push(out, stmt->u.struct_def.name, TC_MEMBER_STRUCT,
                                     stmt->u.struct_def.visibility, (int)i, diag) != 0) {
                tc_member_index_free(out);
                return -1;
            }
        }
    }
    return 0;
}

const TcMemberEntry *tc_member_index_find(const TcMemberIndex *index, const char *name) {
    size_t i = 0;
    if (!index || !name) {
        return NULL;
    }
    for (i = 0; i < index->count; i++) {
        if (strcmp(index->items[i].name, name) == 0) {
            return &index->items[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Self 使用检查（#program 禁用）                                      */
/* ------------------------------------------------------------------ */

/**
 * RHS 是否提及 Self（Self.member 或 Self.xxx(...) 调用）。
 * 当前仅下钻 FUNCALL_EXPR 实参；其它复合 RHS 的 Self 由语句层覆盖。
 */
static int tc_rhs_mentions_self(const TcRhs *rhs, int *out_line) {
    size_t i = 0;
    if (!rhs) {
        return 0;
    }
    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        return 1;
    }
    if (rhs->kind == TC_RHS_FUNCALL_EXPR && rhs->u.funcall_expr.is_self) {
        return 1;
    }
    if (rhs->kind == TC_RHS_FUNCALL_EXPR) {
        for (i = 0; i < rhs->u.funcall_expr.arg_count; i++) {
            if (rhs->u.funcall_expr.args[i].value &&
                tc_rhs_mentions_self((const TcRhs *)rhs->u.funcall_expr.args[i].value, out_line)) {
                return 1;
            }
        }
    }
    (void)out_line;
    return 0;
}

/**
 * 在 #program 模式下禁止出现 Self；#lib 直接放行。
 * 递归进入 if/while/func 体，覆盖嵌套作用域。
 */
static int tc_stmt_check_self(const TcStatement *stmt, TcModuleMode mode, TcDiagnostic *diag) {
    size_t i = 0;

    if (mode == TC_MODULE_LIB) {
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNCALL && stmt->u.funcall_stmt.is_self) {
        tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, stmt->u.funcall_stmt.line,
                          TC_COLUMN_UNKNOWN, "Self is not allowed in #program");
        return -1;
    }
    if (stmt->kind == TC_STMT_VAR_DEF && tc_rhs_mentions_self(&stmt->u.var_def.rhs, NULL)) {
        tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, stmt->u.var_def.line, TC_COLUMN_UNKNOWN,
                          "Self is not allowed in #program");
        return -1;
    }
    if (stmt->kind == TC_STMT_CONST_DEF && tc_rhs_mentions_self(&stmt->u.const_def.rhs, NULL)) {
        tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, stmt->u.const_def.line,
                          TC_COLUMN_UNKNOWN, "Self is not allowed in #program");
        return -1;
    }
    if (stmt->kind == TC_STMT_ASSIGN && tc_rhs_mentions_self(&stmt->u.assign.rhs, NULL)) {
        tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, stmt->u.assign.line, TC_COLUMN_UNKNOWN,
                          "Self is not allowed in #program");
        return -1;
    }
    if (stmt->kind == TC_STMT_IF) {
        if (tc_rhs_mentions_self(&stmt->u.if_stmt.condition, NULL)) {
            tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, stmt->u.if_stmt.line,
                              TC_COLUMN_UNKNOWN, "Self is not allowed in #program");
            return -1;
        }
        for (i = 0; i < stmt->u.if_stmt.then_count; i++) {
            if (tc_stmt_check_self(&stmt->u.if_stmt.then_body[i], mode, diag) != 0) {
                return -1;
            }
        }
        for (i = 0; i < stmt->u.if_stmt.else_count; i++) {
            if (tc_stmt_check_self(&stmt->u.if_stmt.else_body[i], mode, diag) != 0) {
                return -1;
            }
        }
    }
    if (stmt->kind == TC_STMT_WHILE) {
        if (tc_rhs_mentions_self(&stmt->u.while_stmt.condition, NULL)) {
            tc_diagnostic_set(diag, TC_ERR_PROGRAM_MODE_MISUSE, stmt->u.while_stmt.line,
                              TC_COLUMN_UNKNOWN, "Self is not allowed in #program");
            return -1;
        }
        for (i = 0; i < stmt->u.while_stmt.body_count; i++) {
            if (tc_stmt_check_self(&stmt->u.while_stmt.body[i], mode, diag) != 0) {
                return -1;
            }
        }
    }
    if (stmt->kind == TC_STMT_FUNC_DEF) {
        for (i = 0; i < stmt->u.func_def.body_count; i++) {
            if (tc_stmt_check_self(&stmt->u.func_def.body[i], mode, diag) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int tc_scope_check_self_usage(const TcProgram *program, TcDiagnostic *diag) {
    size_t i = 0;
    if (!program) {
        return 0;
    }
    for (i = 0; i < program->count; i++) {
        if (tc_stmt_check_self(&program->items[i], program->mode, diag) != 0) {
            return -1;
        }
    }
    return 0;
}
