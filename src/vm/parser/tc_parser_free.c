/*
 * tc_parser_free.c — AST / TcProgram 内存释放与动态数组管理
 *
 * 与 tc_parser.c / tc_parser_rhs.c 配对：覆盖全部 TcStmtKind / TcRhsKind，
 * 含 Phase 2 的 import / struct / func / static / Self 相关节点。
 */
#include "tc_parser_free.h"

#include "tc_diagnostic.h"

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  内存释放函数                                                        */
/* ------------------------------------------------------------------ */

void tc_operand_free(TcOperand *operand) {
    if (!operand) {
        return;
    }
    if (operand->kind == TC_OPERAND_VAR) {
        free(operand->u.name);
        operand->u.name = NULL;
    }
}

static void tc_named_arg_free(TcNamedArg *arg) {
    if (!arg) {
        return;
    }
    free(arg->param_name);
    arg->param_name = NULL;
    tc_rhs_free(&arg->value);
}

static void tc_string_list_free(char **items, size_t count) {
    size_t i = 0;
    if (!items) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

void tc_rhs_free(TcRhs *rhs) {
    size_t i = 0;

    if (!rhs) {
        return;
    }
    if (rhs->kind == TC_RHS_ARITH) {
        tc_operand_free(&rhs->u.arith.lhs);
        tc_operand_free(&rhs->u.arith.rhs);
    } else if (rhs->kind == TC_RHS_UNARY) {
        tc_operand_free(&rhs->u.unary.operand);
    } else if (rhs->kind == TC_RHS_COMPARE) {
        tc_operand_free(&rhs->u.compare.lhs);
        tc_operand_free(&rhs->u.compare.rhs);
    } else if (rhs->kind == TC_RHS_LOGIC_BIN) {
        tc_operand_free(&rhs->u.logic_bin.lhs);
        tc_operand_free(&rhs->u.logic_bin.rhs);
    } else if (rhs->kind == TC_RHS_LOGIC_UN) {
        tc_operand_free(&rhs->u.logic_un.operand);
    } else if (rhs->kind == TC_RHS_BITWISE_BIN) {
        tc_operand_free(&rhs->u.bitwise_bin.lhs);
        tc_operand_free(&rhs->u.bitwise_bin.rhs);
    } else if (rhs->kind == TC_RHS_BITWISE_UN) {
        tc_operand_free(&rhs->u.bitwise_un.operand);
    } else if (rhs->kind == TC_RHS_SHIFT) {
        tc_operand_free(&rhs->u.shift.value);
        tc_operand_free(&rhs->u.shift.count);
    } else if (rhs->kind == TC_RHS_CAST) {
        tc_operand_free(&rhs->u.cast.source);
    } else if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        tc_operand_free(&rhs->u.float_arith.lhs);
        tc_operand_free(&rhs->u.float_arith.rhs);
    } else if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        tc_operand_free(&rhs->u.float_unary.operand);
    } else if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        tc_operand_free(&rhs->u.float_compare.lhs);
        tc_operand_free(&rhs->u.float_compare.rhs);
    } else if (rhs->kind == TC_RHS_CONST_CAST) {
        tc_operand_free(&rhs->u.const_cast.source);
    } else if (rhs->kind == TC_RHS_CONST_REF) {
        if (rhs->u.const_ref.name) {
            free(rhs->u.const_ref.name);
            rhs->u.const_ref.name = NULL;
        }
    } else if (rhs->kind == TC_RHS_BITCAST) {
        tc_operand_free(&rhs->u.bitcast.source);
    } else if (rhs->kind == TC_RHS_MEMBLOCK_LOAD) {
        tc_type_free(&rhs->u.memblock_load.element_type);
        tc_operand_free(&rhs->u.memblock_load.memblock);
        tc_operand_free(&rhs->u.memblock_load.index);
    } else if (rhs->kind == TC_RHS_MEMBLOCK_CONSTRUCTOR) {
        tc_type_free(&rhs->u.memblock_ctor.element_type);
        free(rhs->u.memblock_ctor.count_name);
        rhs->u.memblock_ctor.count_name = NULL;
        if (rhs->u.memblock_ctor.is_fill) {
            tc_operand_free(&rhs->u.memblock_ctor.fill_value);
        }
        for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
            tc_operand_free(&rhs->u.memblock_ctor.values[i]);
        }
        free(rhs->u.memblock_ctor.values);
        rhs->u.memblock_ctor.values = NULL;
    } else if (rhs->kind == TC_RHS_MEMBLOCK_COUNT) {
        free(rhs->u.memblock_count.memblock_name);
        rhs->u.memblock_count.memblock_name = NULL;
    } else if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        free(rhs->u.struct_ctor.struct_name);
        rhs->u.struct_ctor.struct_name = NULL;
        for (i = 0; i < rhs->u.struct_ctor.field_count; i++) {
            free(rhs->u.struct_ctor.fields[i].param_name);
            if (rhs->u.struct_ctor.fields[i].value_rhs) {
                tc_rhs_free((TcRhs *)rhs->u.struct_ctor.fields[i].value_rhs);
                free(rhs->u.struct_ctor.fields[i].value_rhs);
            } else {
                tc_operand_free(&rhs->u.struct_ctor.fields[i].value_op);
            }
        }
        free(rhs->u.struct_ctor.fields);
        rhs->u.struct_ctor.fields = NULL;
    } else if (rhs->kind == TC_RHS_FIELD_READ) {
        free(rhs->u.field_read.base);
        rhs->u.field_read.base = NULL;
        tc_string_list_free(rhs->u.field_read.fields, rhs->u.field_read.field_count);
        rhs->u.field_read.fields = NULL;
        rhs->u.field_read.field_count = 0;
    } else if (rhs->kind == TC_RHS_PTR_LOAD) {
        tc_type_free(&rhs->u.ptr_load.pointee_type);
        tc_operand_free(&rhs->u.ptr_load.ptr);
    } else if (rhs->kind == TC_RHS_PTR_ADDRESS) {
        tc_type_free(&rhs->u.ptr_address.pointee_type);
        free(rhs->u.ptr_address.name);
        rhs->u.ptr_address.name = NULL;
    } else if (rhs->kind == TC_RHS_PTR_ADD || rhs->kind == TC_RHS_PTR_SUB) {
        tc_type_free(&rhs->u.ptr_arith.pointee_type);
        tc_operand_free(&rhs->u.ptr_arith.ptr);
        tc_operand_free(&rhs->u.ptr_arith.offset);
    } else if (rhs->kind == TC_RHS_PTR_EQ || rhs->kind == TC_RHS_PTR_NE ||
               rhs->kind == TC_RHS_PTR_LT || rhs->kind == TC_RHS_PTR_LE ||
               rhs->kind == TC_RHS_PTR_GT || rhs->kind == TC_RHS_PTR_GE) {
        tc_type_free(&rhs->u.ptr_compare.pointee_type);
        tc_operand_free(&rhs->u.ptr_compare.lhs);
        tc_operand_free(&rhs->u.ptr_compare.rhs);
    } else if (rhs->kind == TC_RHS_PTR_SIZE) {
        tc_type_free(&rhs->u.ptr_size.pointee_type);
        tc_operand_free(&rhs->u.ptr_size.ptr);
    } else if (rhs->kind == TC_RHS_FUNCALL_EXPR) {
        free(rhs->u.funcall_expr.target);
        free(rhs->u.funcall_expr.qualifier);
        free(rhs->u.funcall_expr.member_name);
        rhs->u.funcall_expr.target = NULL;
        rhs->u.funcall_expr.qualifier = NULL;
        rhs->u.funcall_expr.member_name = NULL;
        for (i = 0; i < rhs->u.funcall_expr.arg_count; i++) {
            free(rhs->u.funcall_expr.args[i].param_name);
            if (rhs->u.funcall_expr.args[i].value) {
                tc_rhs_free((TcRhs *)rhs->u.funcall_expr.args[i].value);
                free(rhs->u.funcall_expr.args[i].value);
            }
        }
        free(rhs->u.funcall_expr.args);
        rhs->u.funcall_expr.args = NULL;
    } else if (rhs->kind == TC_RHS_SELF_MEMBER) {
        free(rhs->u.self_member.member_name);
        rhs->u.self_member.member_name = NULL;
    }
}

void tc_statement_free(TcStatement *stmt) {
    size_t i = 0;

    if (!stmt) {
        return;
    }
    if (stmt->kind == TC_STMT_VAR_DEF) {
        free(stmt->u.var_def.name);
        stmt->u.var_def.name = NULL;
        free(stmt->u.var_def.struct_type_name);
        stmt->u.var_def.struct_type_name = NULL;
        tc_type_free(&stmt->u.var_def.full_type);
        tc_rhs_free(&stmt->u.var_def.rhs);
    } else if (stmt->kind == TC_STMT_CONST_DEF) {
        free(stmt->u.const_def.name);
        stmt->u.const_def.name = NULL;
        free(stmt->u.const_def.struct_type_name);
        stmt->u.const_def.struct_type_name = NULL;
        tc_type_free(&stmt->u.const_def.full_type);
        tc_rhs_free(&stmt->u.const_def.rhs);
    } else if (stmt->kind == TC_STMT_ASSIGN) {
        free(stmt->u.assign.name);
        stmt->u.assign.name = NULL;
        tc_rhs_free(&stmt->u.assign.rhs);
    } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        tc_operand_free(&stmt->u.io_write.operand);
    } else if (stmt->kind == TC_STMT_READ) {
        free(stmt->u.io_read.name);
        stmt->u.io_read.name = NULL;
    } else if (stmt->kind == TC_STMT_IF) {
        tc_rhs_free(&stmt->u.if_stmt.condition);
        for (i = 0; i < stmt->u.if_stmt.then_count; i++) {
            tc_statement_free(&stmt->u.if_stmt.then_body[i]);
        }
        free(stmt->u.if_stmt.then_body);
        stmt->u.if_stmt.then_body = NULL;
        stmt->u.if_stmt.then_count = 0;
        for (i = 0; i < stmt->u.if_stmt.else_count; i++) {
            tc_statement_free(&stmt->u.if_stmt.else_body[i]);
        }
        free(stmt->u.if_stmt.else_body);
        stmt->u.if_stmt.else_body = NULL;
        stmt->u.if_stmt.else_count = 0;
    } else if (stmt->kind == TC_STMT_WHILE) {
        tc_rhs_free(&stmt->u.while_stmt.condition);
        for (i = 0; i < stmt->u.while_stmt.body_count; i++) {
            tc_statement_free(&stmt->u.while_stmt.body[i]);
        }
        free(stmt->u.while_stmt.body);
        stmt->u.while_stmt.body = NULL;
        stmt->u.while_stmt.body_count = 0;
    } else if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        /* no heap payload */
    } else if (stmt->kind == TC_STMT_LABEL_DEF) {
        free(stmt->u.label_def.name);
        stmt->u.label_def.name = NULL;
    } else if (stmt->kind == TC_STMT_GOTO) {
        free(stmt->u.goto_stmt.target);
        stmt->u.goto_stmt.target = NULL;
    } else if (stmt->kind == TC_STMT_IMPORT) {
        free(stmt->u.import_stmt.module_name);
        stmt->u.import_stmt.module_name = NULL;
    } else if (stmt->kind == TC_STMT_STRUCT_DEF) {
        free(stmt->u.struct_def.name);
        stmt->u.struct_def.name = NULL;
        for (i = 0; i < stmt->u.struct_def.field_count; i++) {
            free(stmt->u.struct_def.fields[i].name);
            free(stmt->u.struct_def.fields[i].struct_type_name);
            tc_type_free(&stmt->u.struct_def.fields[i].type);
        }
        free(stmt->u.struct_def.fields);
        stmt->u.struct_def.fields = NULL;
        stmt->u.struct_def.field_count = 0;
    } else if (stmt->kind == TC_STMT_FUNC_DEF) {
        free(stmt->u.func_def.name);
        stmt->u.func_def.name = NULL;
        free(stmt->u.func_def.return_struct_name);
        stmt->u.func_def.return_struct_name = NULL;
        tc_type_free(&stmt->u.func_def.return_type);
        for (i = 0; i < stmt->u.func_def.param_count; i++) {
            free(stmt->u.func_def.params[i].name);
            free(stmt->u.func_def.params[i].struct_type_name);
            tc_type_free(&stmt->u.func_def.params[i].type);
        }
        free(stmt->u.func_def.params);
        stmt->u.func_def.params = NULL;
        for (i = 0; i < stmt->u.func_def.body_count; i++) {
            tc_statement_free(&stmt->u.func_def.body[i]);
        }
        free(stmt->u.func_def.body);
        stmt->u.func_def.body = NULL;
        stmt->u.func_def.body_count = 0;
    } else if (stmt->kind == TC_STMT_FUNCALL) {
        free(stmt->u.funcall_stmt.target);
        free(stmt->u.funcall_stmt.qualifier);
        free(stmt->u.funcall_stmt.member_name);
        stmt->u.funcall_stmt.target = NULL;
        stmt->u.funcall_stmt.qualifier = NULL;
        stmt->u.funcall_stmt.member_name = NULL;
        for (i = 0; i < stmt->u.funcall_stmt.arg_count; i++) {
            tc_named_arg_free(&stmt->u.funcall_stmt.args[i]);
        }
        free(stmt->u.funcall_stmt.args);
        stmt->u.funcall_stmt.args = NULL;
    } else if (stmt->kind == TC_STMT_RETURN) {
        if (stmt->u.return_stmt.has_value) {
            tc_operand_free(&stmt->u.return_stmt.value);
        }
    } else if (stmt->kind == TC_STMT_FIELD_ASSIGN) {
        free(stmt->u.field_assign.base);
        stmt->u.field_assign.base = NULL;
        tc_string_list_free(stmt->u.field_assign.fields, stmt->u.field_assign.field_count);
        stmt->u.field_assign.fields = NULL;
        stmt->u.field_assign.field_count = 0;
        tc_rhs_free(&stmt->u.field_assign.rhs);
    } else if (stmt->kind == TC_STMT_STATIC_VAR_DEF) {
        free(stmt->u.static_var_def.name);
        free(stmt->u.static_var_def.struct_type_name);
        stmt->u.static_var_def.name = NULL;
        stmt->u.static_var_def.struct_type_name = NULL;
        tc_type_free(&stmt->u.static_var_def.type);
        tc_rhs_free(&stmt->u.static_var_def.rhs);
    } else if (stmt->kind == TC_STMT_STATIC_LET_DEF) {
        free(stmt->u.static_let_def.name);
        free(stmt->u.static_let_def.struct_type_name);
        stmt->u.static_let_def.name = NULL;
        stmt->u.static_let_def.struct_type_name = NULL;
        tc_type_free(&stmt->u.static_let_def.type);
        tc_rhs_free(&stmt->u.static_let_def.rhs);
    } else if (stmt->kind == TC_STMT_MEMBLOCK_STORE) {
        tc_type_free(&stmt->u.memblock_store.element_type);
        free(stmt->u.memblock_store.memblock_name);
        stmt->u.memblock_store.memblock_name = NULL;
        tc_operand_free(&stmt->u.memblock_store.index);
        tc_operand_free(&stmt->u.memblock_store.value);
    } else if (stmt->kind == TC_STMT_MEMBLOCK_COPY) {
        tc_type_free(&stmt->u.memblock_copy.element_type);
        free(stmt->u.memblock_copy.dst_name);
        free(stmt->u.memblock_copy.src_name);
        stmt->u.memblock_copy.dst_name = NULL;
        stmt->u.memblock_copy.src_name = NULL;
        tc_operand_free(&stmt->u.memblock_copy.dst_index);
        tc_operand_free(&stmt->u.memblock_copy.src_index);
        tc_operand_free(&stmt->u.memblock_copy.length);
    } else if (stmt->kind == TC_STMT_PTR_STORE) {
        tc_type_free(&stmt->u.ptr_store.pointee_type);
        tc_operand_free(&stmt->u.ptr_store.ptr);
        tc_operand_free(&stmt->u.ptr_store.value);
    } else if (stmt->kind == TC_STMT_MEMCOPY_UNSAFE) {
        tc_type_free(&stmt->u.memcopy_unsafe.element_type);
        tc_operand_free(&stmt->u.memcopy_unsafe.dst_ptr);
        tc_operand_free(&stmt->u.memcopy_unsafe.dst_index);
        tc_operand_free(&stmt->u.memcopy_unsafe.src_ptr);
        tc_operand_free(&stmt->u.memcopy_unsafe.src_index);
        tc_operand_free(&stmt->u.memcopy_unsafe.length);
    }
}

/* ------------------------------------------------------------------ */
/*  TcProgram 动态数组管理                                              */
/* ------------------------------------------------------------------ */

void tc_program_init(TcProgram *program) {
    program->mode = TC_MODULE_UNSET;
    program->module_name = NULL;
    program->source_path = NULL;
    program->items = NULL;
    program->count = 0;
    program->capacity = 0;
}

void tc_program_free(TcProgram *program) {
    size_t i = 0;
    for (i = 0; i < program->count; i++) {
        tc_statement_free(&program->items[i]);
    }
    free(program->items);
    free(program->module_name);
    free(program->source_path);
    program->items = NULL;
    program->module_name = NULL;
    program->source_path = NULL;
    program->mode = TC_MODULE_UNSET;
    program->count = 0;
    program->capacity = 0;
}

int tc_program_push(TcProgram *program, const TcStatement *stmt, TcDiagnostic *diag) {
    if (program->count == program->capacity) {
        size_t new_cap = program->capacity == 0 ? 8 : program->capacity * 2;
        TcStatement *items = (TcStatement *)realloc(program->items, new_cap * sizeof(TcStatement));
        if (!items) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN, "memory allocation failed");
            return -1;
        }
        program->items = items;
        program->capacity = new_cap;
    }
    program->items[program->count++] = *stmt;
    return 0;
}
