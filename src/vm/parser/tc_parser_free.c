/*
 * tc_parser_free.c — AST / TcProgram 内存释放与动态数组管理
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

void tc_rhs_free(TcRhs *rhs) {
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
    } else {
        /* unknown RHS kind, skip */
    }
}

void tc_statement_free(TcStatement *stmt) {
    if (!stmt) {
        return;
    }
    if (stmt->kind == TC_STMT_VAR_DEF) {
        if (stmt->u.var_def.name) {
            free(stmt->u.var_def.name);
            stmt->u.var_def.name = NULL;
        }
        tc_rhs_free(&stmt->u.var_def.rhs);
    } else if (stmt->kind == TC_STMT_CONST_DEF) {
        if (stmt->u.const_def.name) {
            free(stmt->u.const_def.name);
            stmt->u.const_def.name = NULL;
        }
        tc_rhs_free(&stmt->u.const_def.rhs);
    } else if (stmt->kind == TC_STMT_ASSIGN) {
        if (stmt->u.assign.name) {
            free(stmt->u.assign.name);
            stmt->u.assign.name = NULL;
        }
        tc_rhs_free(&stmt->u.assign.rhs);
    } else if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
        tc_operand_free(&stmt->u.io_write.operand);
    } else if (stmt->kind == TC_STMT_READ) {
        if (stmt->u.io_read.name) {
            free(stmt->u.io_read.name);
            stmt->u.io_read.name = NULL;
        }
    } else if (stmt->kind == TC_STMT_IF) {
        size_t i = 0;

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
        size_t i = 0;

        tc_rhs_free(&stmt->u.while_stmt.condition);
        for (i = 0; i < stmt->u.while_stmt.body_count; i++) {
            tc_statement_free(&stmt->u.while_stmt.body[i]);
        }
        free(stmt->u.while_stmt.body);
        stmt->u.while_stmt.body = NULL;
        stmt->u.while_stmt.body_count = 0;
    } else if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        /* loop control statements have no heap payload */
    } else if (stmt->kind == TC_STMT_LABEL_DEF) {
        if (stmt->u.label_def.name) {
            free(stmt->u.label_def.name);
            stmt->u.label_def.name = NULL;
        }
    } else if (stmt->kind == TC_STMT_GOTO) {
        if (stmt->u.goto_stmt.target) {
            free(stmt->u.goto_stmt.target);
            stmt->u.goto_stmt.target = NULL;
        }
    } else {
        /* unknown STMT kind, skip */
    }
}

/* ------------------------------------------------------------------ */
/*  TcProgram 动态数组管理                                              */
/* ------------------------------------------------------------------ */

void tc_program_init(TcProgram *program) {
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
    program->items = NULL;
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
