/*
 * tc_executor.c — TC 执行引擎实现（Phase 5：funcall/return、static、ptr、memblock）
 */
#include "tc_executor.h"

#include "tc_call_frame.h"
#include "tc_diagnostic.h"
#include "tc_executor_internal.h"
#include "tc_io.h"
#include "tc_memblock_exec.h"
#include "tc_ptr_exec.h"
#include "tc_struct_exec.h"
#include "tc_semantics.h"
#include "tc_stmt_index.h"
#include "tc_symbol.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TcExecControl tc_execute_block(const TcStatement *items, size_t count, int block_start,
                                      int block_end, TcExecuteCtx *ctx, TcDiagnostic *diag);

static TcExecControl tc_exec_control(TcExecControlKind kind, int loop_id, int target_stmt_index) {
    TcExecControl control;

    memset(&control, 0, sizeof(control));
    control.kind = kind;
    control.loop_id = loop_id;
    control.target_stmt_index = target_stmt_index;
    return control;
}

static TcExecControl tc_exec_normal(void) {
    return tc_exec_control(TC_EXEC_NORMAL, -1, -1);
}

static TcExecControl tc_exec_error(void) {
    return tc_exec_control(TC_EXEC_ERROR, -1, -1);
}

static TcExecControl tc_exec_return_value(TcValue value, int has_value) {
    TcExecControl control = tc_exec_control(TC_EXEC_RETURN, -1, -1);

    control.return_value = value;
    control.has_return_value = has_value;
    return control;
}

void tc_exec_set_internal_error(TcDiagnostic *diag, int line, const char *message) {
    tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN, message);
    diag->domain = TC_DIAG_IMPLEMENTATION;
}

int tc_exec_load_binding(const TcResolvedBinding *binding, TcTypeTag type, const TcValue *slots,
                         TcValue *out, TcDiagnostic *diag, int line) {
    if (!binding->resolved) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved binding metadata");
        return -1;
    }
    if (binding->type->tag != type) {
        tc_exec_set_internal_error(diag, line, "internal error: binding type metadata mismatch");
        return -1;
    }
    if (binding->is_const) {
        *out = tc_value_make(type, binding->const_bits);
        return 0;
    }
    if (binding->slot < 0 || !slots) {
        tc_exec_set_internal_error(diag, line, "internal error: invalid runtime slot metadata");
        return -1;
    }
    *out = slots[binding->slot];
    return 0;
}

const TcSymbol *tc_exec_find_symbol(const TcSymbolTable *symbols, const char *name) {
    size_t i = 0;
    const TcSymbol *best = NULL;

    if (!symbols || !name) {
        return NULL;
    }
    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (!sym->name || strcmp(sym->name, name) != 0) {
            continue;
        }
        if (!best || sym->def_stmt_index > best->def_stmt_index) {
            best = sym;
        }
    }
    return best;
}

const TcFuncDef *tc_find_func_def(const TcTypedProgram *prog, int func_id,
                                  const TcProgram **out_module) {
    size_t i = 0;
    size_t j = 0;

    if (!prog || func_id < 0) {
        return NULL;
    }
    for (i = 0; i < prog->program.count; i++) {
        if (prog->program.items[i].kind == TC_STMT_FUNC_DEF &&
            prog->program.items[i].u.func_def.func_id == func_id) {
            if (out_module) {
                *out_module = &prog->program;
            }
            return &prog->program.items[i].u.func_def;
        }
    }
    for (i = 0; i < prog->dep_count; i++) {
        for (j = 0; j < prog->deps[i].count; j++) {
            if (prog->deps[i].items[j].kind == TC_STMT_FUNC_DEF &&
                prog->deps[i].items[j].u.func_def.func_id == func_id) {
                if (out_module) {
                    *out_module = &prog->deps[i];
                }
                return &prog->deps[i].items[j].u.func_def;
            }
        }
    }
    return NULL;
}

int tc_func_body_index_range(const TcProgram *module, int func_id, int *out_body_start,
                             int *out_body_end) {
    size_t i = 0;
    int cursor = 0;

    if (!module || !out_body_start || !out_body_end) {
        return -1;
    }
    for (i = 0; i < module->count; i++) {
        const TcStatement *stmt = &module->items[i];
        int span = tc_stmt_subtree_index_count(stmt);

        if (stmt->kind == TC_STMT_FUNC_DEF && stmt->u.func_def.func_id == func_id) {
            *out_body_start = cursor + 1;
            *out_body_end = cursor + span;
            return 0;
        }
        cursor += span;
    }
    return -1;
}

int tc_exec_param_slot(const TcSymbolTable *symbols, const TcFuncDef *func,
                       const char *param_name, int *out_slot) {
    size_t i = 0;
    size_t pi = 0;

    if (!symbols || !func || !param_name || !out_slot) {
        return -1;
    }
    for (pi = 0; pi < func->param_count; pi++) {
        if (func->params[pi].name && strcmp(func->params[pi].name, param_name) == 0) {
            size_t seen = 0;
            for (i = 0; i < symbols->count; i++) {
                const TcSymbol *sym = &symbols->symbols[i];

                if (sym->slot_domain != TC_SLOT_PARAM || sym->def_line != func->line) {
                    continue;
                }
                if (seen == pi) {
                    *out_slot = sym->slot;
                    return 0;
                }
                seen++;
            }
            break;
        }
    }
    for (i = 0; i < symbols->count; i++) {
        const TcSymbol *sym = &symbols->symbols[i];

        if (sym->slot_domain == TC_SLOT_PARAM && sym->def_line == func->line && sym->name &&
            strcmp(sym->name, param_name) == 0) {
            *out_slot = sym->slot;
            return 0;
        }
    }
    return -1;
}

static int tc_exec_io_write(const TcIoWrite *io_write, TcExecuteCtx *ctx, int newline,
                            TcDiagnostic *diag) {
    TcValue value;

    if (io_write->operand.kind == TC_OPERAND_LIT) {
        value = tc_literal_to_value(&io_write->operand.u.lit, io_write->type->tag);
    } else if (tc_exec_load_binding(&io_write->operand.binding, io_write->type->tag, ctx->slots,
                                    &value, diag, io_write->line) != 0) {
        return -1;
    }

    if (tc_io_write_value(&value, io_write->fmt.spec, newline, stdout) != 0) {
        tc_diagnostic_set(diag, TC_RE_IO, io_write->line, TC_COLUMN_UNKNOWN, "output failed");
        return -1;
    }
    return 0;
}

static int tc_exec_io_read(const TcRead *io_read, TcExecuteCtx *ctx, TcDiagnostic *diag) {
    uint64_t bits = 0;

    if (!io_read->binding.resolved || io_read->binding.is_const || io_read->binding.slot < 0 ||
        !ctx->slots) {
        tc_exec_set_internal_error(diag, io_read->line,
                                   "internal error: unresolved read target metadata");
        return -1;
    }
    if (tc_io_read_value(io_read->type->tag, &bits, diag, io_read->line) != 0) {
        return -1;
    }
    ctx->slots[io_read->binding.slot] = tc_value_make(io_read->type->tag, bits);
    return 0;
}

int tc_eval_operand(const TcOperand *operand, TcTypeTag expected_type, TcExecuteCtx *ctx,
                    TcValue *out, TcDiagnostic *diag, int line) {
    if (operand->kind == TC_OPERAND_LIT) {
        *out = tc_literal_to_value(&operand->u.lit, expected_type);
        return 0;
    }
    if (operand->binding.resolved) {
        return tc_exec_load_binding(&operand->binding, expected_type, ctx->slots, out, diag,
                                    line);
    }
    if (operand->kind == TC_OPERAND_VAR && operand->u.name && ctx->symbols) {
        const TcSymbol *sym = tc_exec_find_symbol(ctx->symbols, operand->u.name);

        if (sym && sym->slot >= 0) {
            if (sym->has_const_value) {
                *out = sym->const_value;
            } else {
                *out = ctx->slots[sym->slot];
            }
            return 0;
        }
    }
    return tc_exec_load_binding(&operand->binding, expected_type, ctx->slots, out, diag, line);
}

static int tc_exec_eval_funcall_args(const TcFuncDef *func, TcNamedArg *args, struct TcRhs **arg_rhs,
                                     size_t arg_count, TcExecuteCtx *ctx, TcDiagnostic *diag,
                                     int line) {
    size_t i = 0;

    for (i = 0; i < arg_count; i++) {
        TcValue value;
        int slot = -1;
        const TcFuncParam *param = &func->params[i];
        const TcRhs *value_rhs = arg_rhs ? (const TcRhs *)arg_rhs[i] : &args[i].value;

        const char *param_name = func->params[i].name;

        if (tc_eval_rhs(value_rhs, param->type.tag, ctx, &value, diag, line) != 0) {
            return -1;
        }
        if (!param_name && args) {
            param_name = args[i].param_name;
        }
        if (tc_exec_param_slot(ctx->symbols, func, param_name, &slot) != 0 || slot < 0) {
            tc_exec_set_internal_error(diag, line, "internal error: unresolved parameter slot");
            return -1;
        }
        if (param->type.tag == TC_STRUCT) {
            if (tc_exec_struct_store_value(&ctx->slots[slot], &value,
                                          param->type.params.struct_type.struct_id, ctx, diag,
                                          line) != 0) {
                return -1;
            }
        } else if (param->type.tag == TC_MEMBLOCK) {
            /* 按值传参：memblock 形参深拷贝（§8.1.1），形参与实参不共享存储 */
            if (tc_exec_memblock_clone(&param->type, &value, ctx, &ctx->slots[slot], diag,
                                       line) != 0) {
                return -1;
            }
        } else {
            ctx->slots[slot] = value;
        }
    }
    return 0;
}

static int tc_exec_call_function(int func_id, TcNamedArg *args, struct TcRhs **arg_rhs,
                                 size_t arg_count, TcExecuteCtx *ctx, TcValue *ret_out,
                                 int want_return, TcDiagnostic *diag, int line) {
    const TcProgram *module = NULL;
    const TcFuncDef *func = NULL;
    int body_start = 0;
    int body_end = 0;
    TcExecControl control;
    int saved_func_id = ctx->current_func_id;
    TcStmtIndexCursor saved_index = ctx->index;

    func = tc_find_func_def(ctx->program, func_id, &module);
    if (!func || !module) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved funcall target");
        return -1;
    }
    if (tc_exec_eval_funcall_args(func, args, arg_rhs, arg_count, ctx, diag, line) != 0) {
        return -1;
    }
    if (tc_func_body_index_range(module, func_id, &body_start, &body_end) != 0) {
        tc_exec_set_internal_error(diag, line, "internal error: function body index missing");
        return -1;
    }

    if (tc_call_frame_push(&ctx->call_frame, func_id) != 0) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    ctx->current_func_id = func_id;
    tc_stmt_index_reset(&ctx->index);
    ctx->index.next = body_start;
    control = tc_execute_block(func->body, func->body_count, body_start, body_end, ctx, diag);
    ctx->index = saved_index;
    ctx->current_func_id = saved_func_id;
    tc_call_frame_pop(&ctx->call_frame);

    if (control.kind == TC_EXEC_ERROR) {
        return -1;
    }
    if (control.kind == TC_EXEC_RETURN) {
        if (want_return) {
            if (!control.has_return_value) {
                tc_exec_set_internal_error(diag, line,
                                           "internal error: missing function return value");
                return -1;
            }
            *ret_out = control.return_value;
        }
        return 0;
    }
    if (control.kind != TC_EXEC_NORMAL) {
        tc_exec_set_internal_error(diag, line, "internal error: unconsumed control in function");
        return -1;
    }
    if (want_return) {
        tc_exec_set_internal_error(diag, line, "internal error: function fell through without return");
        return -1;
    }
    return 0;
}

int tc_exec_call_function_public(int func_id, TcExecuteCtx *ctx,
                                  TcValue *ret_out, int want_return,
                                  TcDiagnostic *diag, int line) {
    const TcProgram *module = NULL;
    const TcFuncDef *func = NULL;
    int body_start = 0;
    int body_end = 0;
    TcExecControl control;
    int saved_func_id = ctx->current_func_id;
    TcStmtIndexCursor saved_index = ctx->index;

    func = tc_find_func_def(ctx->program, func_id, &module);
    if (!func || !module) {
        tc_exec_set_internal_error(diag, line, "internal error: unresolved funcall target");
        return -1;
    }
    if (tc_func_body_index_range(module, func_id, &body_start, &body_end) != 0) {
        tc_exec_set_internal_error(diag, line, "internal error: function body index missing");
        return -1;
    }

    if (tc_call_frame_push(&ctx->call_frame, func_id) != 0) {
        tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                          "memory allocation failed");
        return -1;
    }
    ctx->current_func_id = func_id;
    tc_stmt_index_reset(&ctx->index);
    ctx->index.next = body_start;
    control = tc_execute_block(func->body, func->body_count, body_start, body_end, ctx, diag);
    ctx->index = saved_index;
    ctx->current_func_id = saved_func_id;
    tc_call_frame_pop(&ctx->call_frame);

    if (control.kind == TC_EXEC_ERROR) {
        return -1;
    }
    if (control.kind == TC_EXEC_RETURN) {
        if (want_return) {
            if (!control.has_return_value) {
                tc_exec_set_internal_error(diag, line,
                                           "internal error: missing function return value");
                return -1;
            }
            *ret_out = control.return_value;
        }
        return 0;
    }
    if (control.kind != TC_EXEC_NORMAL) {
        tc_exec_set_internal_error(diag, line, "internal error: unconsumed control in function");
        return -1;
    }
    if (want_return) {
        tc_exec_set_internal_error(diag, line, "internal error: function fell through without return");
        return -1;
    }
    return 0;
}

int tc_eval_rhs(const TcRhs *rhs, TcTypeTag expected_type, TcExecuteCtx *ctx, TcValue *out,
                TcDiagnostic *diag, int line) {
    if (rhs->kind == TC_RHS_LIT) {
        *out = tc_literal_to_value(&rhs->u.lit, expected_type);
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        TcValue lhs;
        TcValue rhs_value;
        if (tc_eval_operand(&rhs->u.arith.lhs, rhs->u.arith.type->tag, ctx, &lhs, diag, line) != 0 ||
            tc_eval_operand(&rhs->u.arith.rhs, rhs->u.arith.type->tag, ctx, &rhs_value, diag,
                            line) != 0) {
            return -1;
        }
        return tc_exec_arith(rhs->u.arith.op, rhs->u.arith.type->tag, rhs->u.arith.mode, &lhs,
                             &rhs_value, out, diag, line);
    }

    if (rhs->kind == TC_RHS_UNARY) {
        TcValue operand;
        if (tc_eval_operand(&rhs->u.unary.operand, rhs->u.unary.type->tag, ctx, &operand, diag,
                            line) != 0) {
            return -1;
        }
        return tc_exec_unary(rhs->u.unary.op, rhs->u.unary.type->tag, rhs->u.unary.mode, &operand,
                             out, diag, line);
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        TcValue lhs;
        TcValue rhs_value;
        if (tc_eval_operand(&rhs->u.compare.lhs, rhs->u.compare.type->tag, ctx, &lhs, diag, line) !=
                0 ||
            tc_eval_operand(&rhs->u.compare.rhs, rhs->u.compare.type->tag, ctx, &rhs_value, diag,
                            line) != 0) {
            return -1;
        }
        return tc_exec_compare(rhs->u.compare.op, rhs->u.compare.type->tag, &lhs, &rhs_value, out, diag,
                               line);
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        TcValue lhs;
        if (tc_eval_operand(&rhs->u.logic_bin.lhs, TC_BOOL, ctx, &lhs, diag, line) != 0) {
            return -1;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_AND && lhs.bits == 0) {
            *out = tc_value_make(TC_BOOL, 0);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_OR && lhs.bits != 0) {
            *out = tc_value_make(TC_BOOL, 1);
            return 0;
        }
        if (rhs->u.logic_bin.op == TC_LOGIC_XOR) {
            /* xor 不短路：两侧均需求值 */
        }
        {
            TcValue rhs_value;
            if (tc_eval_operand(&rhs->u.logic_bin.rhs, TC_BOOL, ctx, &rhs_value, diag, line) !=
                0) {
                return -1;
            }
            return tc_exec_logic_binary(rhs->u.logic_bin.op, &lhs, &rhs_value, out, diag, line);
        }
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        TcValue operand;
        if (tc_eval_operand(&rhs->u.logic_un.operand, TC_BOOL, ctx, &operand, diag, line) != 0) {
            return -1;
        }
        return tc_exec_logic_unary(rhs->u.logic_un.op, &operand, out, diag, line);
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        TcValue lhs;
        TcValue rhs_value;
        if (tc_eval_operand(&rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type->tag, ctx, &lhs, diag,
                            line) != 0 ||
            tc_eval_operand(&rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type->tag, ctx, &rhs_value,
                            diag, line) != 0) {
            return -1;
        }
        return tc_exec_bitwise_binary(rhs->u.bitwise_bin.op, rhs->u.bitwise_bin.type->tag, &lhs,
                                      &rhs_value, out, diag, line);
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        TcValue operand;
        if (tc_eval_operand(&rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type->tag, ctx, &operand,
                            diag, line) != 0) {
            return -1;
        }
        return tc_exec_bitwise_unary(rhs->u.bitwise_un.type->tag, &operand, out, diag, line);
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        TcValue value;
        TcValue count;
        if (tc_eval_operand(&rhs->u.shift.value, rhs->u.shift.type->tag, ctx, &value, diag, line) !=
                0 ||
            tc_eval_operand(&rhs->u.shift.count, rhs->u.shift.type->tag, ctx, &count, diag, line) !=
                0) {
            return -1;
        }
        return tc_exec_shift(rhs->u.shift.op, rhs->u.shift.type->tag, rhs->u.shift.mode, &value, &count,
                             out, diag, line);
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        return tc_exec_load_binding(&rhs->u.const_ref.binding, expected_type, ctx->slots, out,
                                    diag, line);
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        tc_diagnostic_set(diag, TC_CE_CONSTANT_EXPRESSION, line, TC_COLUMN_UNKNOWN,
                          "constant cast is only allowed in let initializer");
        return -1;
    }

    if (rhs->kind == TC_RHS_BITCAST) {
        TcValue source;
        if (tc_eval_operand(&rhs->u.bitcast.source, rhs->u.bitcast.source_type->tag, ctx, &source,
                            diag, line) != 0) {
            return -1;
        }
        if (rhs->u.bitcast.target.tag == TC_PTR || rhs->u.bitcast.source_type->tag == TC_PTR) {
            if (!rhs->u.bitcast.target_type_resolved || !rhs->u.bitcast.target_type) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                                  "internal error: bitcast target type not resolved");
                return -1;
            }
            out->type = rhs->u.bitcast.target_type;
            out->bits = source.bits;
            return 0;
        }
        return tc_exec_bitcast(rhs->u.bitcast.target.tag, &source, out, diag, line);
    }

    if (rhs->kind == TC_RHS_CAST) {
        TcValue source;
        if (tc_eval_operand(&rhs->u.cast.source, rhs->u.cast.source_type->tag, ctx, &source, diag,
                            line) != 0) {
            return -1;
        }
        if (rhs->u.cast.target.tag == TC_PTR) {
            if (!rhs->u.cast.target_type_resolved || !rhs->u.cast.target_type) {
                tc_diagnostic_set(diag, TC_CE_SYNTAX, line, TC_COLUMN_UNKNOWN,
                                  "internal error: cast target type not resolved");
                return -1;
            }
            out->type = rhs->u.cast.target_type;
            out->bits = source.bits;
            return 0;
        }
        return rhs->u.cast.mode == TC_TRUNC_TRUNCATE
                   ? tc_exec_truncate(rhs->u.cast.target.tag, &source, out, diag, line)
                   : tc_exec_cast(rhs->u.cast.target.tag, &source, out, diag, line);
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        TcValue lhs;
        TcValue rhs_value;
        if (tc_eval_operand(&rhs->u.float_arith.lhs, rhs->u.float_arith.type->tag, ctx, &lhs, diag,
                            line) != 0 ||
            tc_eval_operand(&rhs->u.float_arith.rhs, rhs->u.float_arith.type->tag, ctx, &rhs_value,
                            diag, line) != 0) {
            return -1;
        }
        return tc_exec_fp_arith(rhs->u.float_arith.op, rhs->u.float_arith.type->tag,
                                rhs->u.float_arith.mode, &lhs, &rhs_value, out, diag, line);
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        TcValue operand;
        if (tc_eval_operand(&rhs->u.float_unary.operand, rhs->u.float_unary.type->tag, ctx, &operand,
                            diag, line) != 0) {
            return -1;
        }
        return tc_exec_fp_unary(rhs->u.float_unary.op, rhs->u.float_unary.type->tag,
                                rhs->u.float_unary.mode, &operand, out, diag, line);
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        TcValue lhs;
        TcValue rhs_value;
        if (tc_eval_operand(&rhs->u.float_compare.lhs, rhs->u.float_compare.type->tag, ctx, &lhs,
                            diag, line) != 0 ||
            tc_eval_operand(&rhs->u.float_compare.rhs, rhs->u.float_compare.type->tag, ctx,
                            &rhs_value, diag, line) != 0) {
            return -1;
        }
        return tc_exec_fp_compare(rhs->u.float_compare.op, rhs->u.float_compare.type->tag,
                                  rhs->u.float_compare.mode, &lhs, &rhs_value, out, diag, line);
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_CONSTRUCTOR) {
        TcType element = tc_type_scalar(rhs->u.memblock_ctor.element_type.tag);
        TcType expected = tc_type_make_memblock(&element, rhs->u.memblock_ctor.count);

        return tc_exec_memblock_ctor(rhs, &expected, ctx, out, diag, line);
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_LOAD) {
        return tc_exec_memblock_load(&rhs->u.memblock_load.element_type,
                                     &rhs->u.memblock_load.memblock, &rhs->u.memblock_load.index,
                                     ctx, out, diag, line);
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_COUNT) {
        int mb_slot = rhs->u.memblock_count.binding.resolved
                          ? rhs->u.memblock_count.binding.slot
                          : -1;

        return tc_exec_memblock_count(mb_slot, ctx, out, diag, line);
    }

    if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        return tc_exec_struct_ctor(rhs, ctx, out, diag, line);
    }

    if (rhs->kind == TC_RHS_FIELD_READ) {
        return tc_exec_struct_field_read(rhs, expected_type, ctx, out, diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_ADDRESS) {
        return tc_exec_ptr_address(&rhs->u.ptr_address.pointee_type, rhs->u.ptr_address.name, ctx,
                                   out, diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_LOAD) {
        return tc_exec_ptr_load(&rhs->u.ptr_load.pointee_type, &rhs->u.ptr_load.ptr, ctx, out,
                                diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_ADD) {
        return tc_exec_ptr_arith(1, &rhs->u.ptr_arith.pointee_type, &rhs->u.ptr_arith.ptr,
                                 &rhs->u.ptr_arith.offset, ctx, out, diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_SUB) {
        return tc_exec_ptr_arith(0, &rhs->u.ptr_arith.pointee_type, &rhs->u.ptr_arith.ptr,
                                 &rhs->u.ptr_arith.offset, ctx, out, diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_EQ) {
        return tc_exec_ptr_compare(TC_CMP_EQ, &rhs->u.ptr_compare.pointee_type,
                                   &rhs->u.ptr_compare.lhs, &rhs->u.ptr_compare.rhs, ctx, out,
                                   diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_NE) {
        return tc_exec_ptr_compare(TC_CMP_NE, &rhs->u.ptr_compare.pointee_type,
                                   &rhs->u.ptr_compare.lhs, &rhs->u.ptr_compare.rhs, ctx, out,
                                   diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_LT) {
        return tc_exec_ptr_compare(TC_CMP_LT, &rhs->u.ptr_compare.pointee_type,
                                   &rhs->u.ptr_compare.lhs, &rhs->u.ptr_compare.rhs, ctx, out,
                                   diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_LE) {
        return tc_exec_ptr_compare(TC_CMP_LE, &rhs->u.ptr_compare.pointee_type,
                                   &rhs->u.ptr_compare.lhs, &rhs->u.ptr_compare.rhs, ctx, out,
                                   diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_GT) {
        return tc_exec_ptr_compare(TC_CMP_GT, &rhs->u.ptr_compare.pointee_type,
                                   &rhs->u.ptr_compare.lhs, &rhs->u.ptr_compare.rhs, ctx, out,
                                   diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_GE) {
        return tc_exec_ptr_compare(TC_CMP_GE, &rhs->u.ptr_compare.pointee_type,
                                   &rhs->u.ptr_compare.lhs, &rhs->u.ptr_compare.rhs, ctx, out,
                                   diag, line);
    }

    if (rhs->kind == TC_RHS_PTR_SIZE) {
        return tc_exec_ptr_size(&rhs->u.ptr_size.pointee_type, &rhs->u.ptr_size.ptr, ctx, out,
                                diag, line);
    }

    if (rhs->kind == TC_RHS_FUNCALL_EXPR) {
        struct TcRhs **arg_rhs = NULL;
        size_t i = 0;

        if (rhs->u.funcall_expr.resolved_func_id < 0) {
            tc_exec_set_internal_error(diag, line, "internal error: unresolved funcall expr");
            return -1;
        }
        if (rhs->u.funcall_expr.arg_count > 0) {
            arg_rhs = (struct TcRhs **)calloc(rhs->u.funcall_expr.arg_count, sizeof(struct TcRhs *));
            if (!arg_rhs) {
                tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, line, TC_COLUMN_UNKNOWN,
                                  "memory allocation failed");
                return -1;
            }
            for (i = 0; i < rhs->u.funcall_expr.arg_count; i++) {
                arg_rhs[i] = rhs->u.funcall_expr.args[i].value;
            }
        }
        {
            int rc = tc_exec_call_function(rhs->u.funcall_expr.resolved_func_id,
                                           (TcNamedArg *)rhs->u.funcall_expr.args, arg_rhs,
                                           rhs->u.funcall_expr.arg_count, ctx, out, 1, diag,
                                           line);
            free(arg_rhs);
            return rc;
        }
    }

    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        const TcSymbol *sym = tc_exec_find_symbol(ctx->symbols, rhs->u.self_member.member_name);

        if (!sym) {
            tc_exec_set_internal_error(diag, line, "internal error: unresolved Self member");
            return -1;
        }
        if (sym->has_const_value) {
            *out = sym->const_value;
            return 0;
        }
        if (sym->slot >= 0 && ctx->slots) {
            *out = ctx->slots[sym->slot];
            out->type = expected_type != TC_VOID
                            ? tc_type_tag_singleton(expected_type)
                            : sym->type;
            return 0;
        }
        tc_exec_set_internal_error(diag, line, "internal error: unresolved Self member");
        return -1;
    }

    {
        char msg[64];
        (void)snprintf(msg, sizeof(msg), "internal error: unhandled rhs kind %d", (int)rhs->kind);
        tc_exec_set_internal_error(diag, line, msg);
        return -1;
    }
}

static TcExecControl tc_execute_statement_at(const TcStatement *stmt, int stmt_start,
                                             TcExecuteCtx *ctx, TcDiagnostic *diag);

static TcExecControl tc_execute_block(const TcStatement *items, size_t count, int block_start,
                                      int block_end, TcExecuteCtx *ctx, TcDiagnostic *diag) {
    size_t i = 0;
    int stmt_start = block_start;

    while (i < count) {
        int span = tc_stmt_subtree_index_count(&items[i]);
        int stmt_end = stmt_start + span;
        TcExecControl control;

        if (ctx->index.next < block_start || ctx->index.next >= block_end) {
            return tc_exec_normal();
        }
        if (ctx->index.next >= stmt_end) {
            stmt_start = stmt_end;
            i++;
            continue;
        }
        control = tc_execute_statement_at(&items[i], stmt_start, ctx, diag);
        if (control.kind == TC_EXEC_GOTO) {
            if (control.target_stmt_index >= block_start &&
                control.target_stmt_index < block_end) {
                ctx->index.next = control.target_stmt_index + 1;
                i = 0;
                stmt_start = block_start;
                continue;
            }
            return control;
        }
        if (control.kind == TC_EXEC_RETURN || control.kind == TC_EXEC_BREAK ||
            control.kind == TC_EXEC_CONTINUE || control.kind == TC_EXEC_ERROR) {
            return control;
        }
        if (ctx->index.next < block_start || ctx->index.next >= block_end) {
            return tc_exec_normal();
        }
        if (ctx->index.next != stmt_end) {
            i = 0;
            stmt_start = block_start;
            continue;
        }
        i++;
        stmt_start = stmt_end;
    }
    return tc_exec_normal();
}

static TcExecControl tc_execute_if_at(const TcStatement *stmt, int if_index, int seeking,
                                      TcExecuteCtx *ctx, TcDiagnostic *diag) {
    const TcIfStmt *if_stmt = &stmt->u.if_stmt;
    int then_start = if_index + 1;
    int then_span = tc_stmt_block_index_span(if_stmt->then_body, if_stmt->then_count);
    int else_start = then_start + then_span;
    int else_span = tc_stmt_block_index_span(if_stmt->else_body, if_stmt->else_count);
    int if_end = else_start + else_span;
    int run_then = 0;

    if (!seeking) {
        TcValue cond_value;
        (void)tc_stmt_index_take(&ctx->index);
        if (tc_eval_rhs(&if_stmt->condition, TC_BOOL, ctx, &cond_value, diag,
                        if_stmt->line) != 0) {
            return tc_exec_error();
        }
        if (cond_value.bits != 0) {
            run_then = 1;
        } else if (if_stmt->else_count > 0) {
            tc_stmt_index_skip_block(&ctx->index, if_stmt->then_body, if_stmt->then_count);
            run_then = 0;
        } else {
            tc_stmt_index_skip_block(&ctx->index, if_stmt->then_body, if_stmt->then_count);
            return tc_exec_normal();
        }
    } else {
        if (ctx->index.next < else_start) {
            run_then = 1;
        } else if (if_stmt->else_count > 0 && ctx->index.next < if_end) {
            run_then = 0;
        } else {
            ctx->index.next = if_end;
            return tc_exec_normal();
        }
    }

    if (run_then) {
        if (if_stmt->then_count > 0) {
            TcExecControl control =
                tc_execute_block(if_stmt->then_body, if_stmt->then_count, then_start, else_start,
                                 ctx, diag);

            if (control.kind != TC_EXEC_NORMAL) {
                return control;
            }
        }
        if (ctx->index.next == else_start) {
            ctx->index.next = if_end;
        }
        return tc_exec_normal();
    }

    return tc_execute_block(if_stmt->else_body, if_stmt->else_count, else_start, if_end, ctx, diag);
}

static TcExecControl tc_execute_while_at(const TcStatement *stmt, int while_index,
                                         TcExecuteCtx *ctx, TcDiagnostic *diag) {
    const TcWhileStmt *while_stmt = &stmt->u.while_stmt;
    int body_start = while_index + 1;
    int while_end = while_index + tc_stmt_subtree_index_count(stmt);

    if (ctx->index.next != while_index) {
        tc_exec_set_internal_error(diag, while_stmt->line,
                                   "internal error: control entered while body directly");
        return tc_exec_error();
    }
    if (while_stmt->loop_id < 0) {
        tc_exec_set_internal_error(diag, while_stmt->line,
                                   "internal error: unresolved while loop metadata");
        return tc_exec_error();
    }

    for (;;) {
        TcValue condition;
        TcExecControl control;

        if (tc_eval_rhs(&while_stmt->condition, TC_BOOL, ctx, &condition, diag,
                        while_stmt->line) != 0) {
            return tc_exec_error();
        }
        if (condition.bits == 0) {
            ctx->index.next = while_end;
            return tc_exec_normal();
        }

        ctx->index.next = body_start;
        control = tc_execute_block(while_stmt->body, while_stmt->body_count, body_start,
                                   while_end, ctx, diag);
        if (control.kind == TC_EXEC_BREAK && control.loop_id == while_stmt->loop_id) {
            ctx->index.next = while_end;
            return tc_exec_normal();
        }
        if (control.kind == TC_EXEC_CONTINUE && control.loop_id == while_stmt->loop_id) {
            continue;
        }
        if (control.kind != TC_EXEC_NORMAL) {
            return control;
        }
    }
}

static TcExecControl tc_execute_statement_at(const TcStatement *stmt, int stmt_start,
                                             TcExecuteCtx *ctx, TcDiagnostic *diag) {
    if (stmt->kind == TC_STMT_IF) {
        int seeking = (ctx->index.next != stmt_start);

        return tc_execute_if_at(stmt, stmt_start, seeking, ctx, diag);
    }

    if (stmt->kind == TC_STMT_WHILE) {
        return tc_execute_while_at(stmt, stmt_start, ctx, diag);
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        tc_stmt_index_take(&ctx->index);
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_GOTO) {
        const TcGoto *goto_stmt = &stmt->u.goto_stmt;
        char msg[128];

        tc_stmt_index_take(&ctx->index);
        if (!goto_stmt->resolved) {
            (void)snprintf(msg, sizeof(msg), "internal error: label '%s' not resolved",
                           goto_stmt->target);
            tc_exec_set_internal_error(diag, goto_stmt->line, msg);
            return tc_exec_error();
        }
        return tc_exec_control(TC_EXEC_GOTO, -1, goto_stmt->resolved_target_stmt_index);
    }

    if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        const TcLoopControlStmt *loop_control =
            stmt->kind == TC_STMT_BREAK ? &stmt->u.break_stmt : &stmt->u.continue_stmt;

        tc_stmt_index_take(&ctx->index);
        if (loop_control->loop_id < 0) {
            tc_exec_set_internal_error(diag, loop_control->line,
                                       "internal error: unresolved loop control metadata");
            return tc_exec_error();
        }
        return tc_exec_control(stmt->kind == TC_STMT_BREAK ? TC_EXEC_BREAK
                                                            : TC_EXEC_CONTINUE,
                               loop_control->loop_id, -1);
    }

    if (stmt->kind == TC_STMT_FUNC_DEF || stmt->kind == TC_STMT_STRUCT_DEF ||
        stmt->kind == TC_STMT_IMPORT || stmt->kind == TC_STMT_STATIC_LET_DEF) {
        tc_stmt_index_take(&ctx->index);
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_STATIC_VAR_DEF) {
        tc_stmt_index_take(&ctx->index);
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_RETURN) {
        const TcReturnStmt *ret = &stmt->u.return_stmt;
        TcValue value;

        tc_stmt_index_take(&ctx->index);
        if (ctx->current_func_id < 0) {
            tc_exec_set_internal_error(diag, ret->line, "internal error: return outside function");
            return tc_exec_error();
        }
        if (!ret->has_value) {
            return tc_exec_return_value(tc_value_make(TC_VOID, 0), 0);
        }
        {
            const TcFuncDef *func = tc_find_func_def(ctx->program, ctx->current_func_id, NULL);
            TcTypeTag ret_type = func ? func->return_type.tag : TC_INT32;

            if (ret->value.kind == TC_OPERAND_LIT) {
                value = tc_literal_to_value(&ret->value.u.lit, ret_type);
            } else if (tc_eval_operand(&ret->value, ret_type, ctx, &value, diag, ret->line) != 0) {
                return tc_exec_error();
            }
            if (ret_type == TC_BOOL) {
                value.bits = value.bits ? 1ULL : 0ULL;
                value.type = tc_type_tag_singleton(TC_BOOL);
            }
            if (ret_type == TC_STRUCT && func) {
                TcValue cloned;
                if (tc_exec_struct_store_value(&cloned, &value,
                                              func->return_type.params.struct_type.struct_id, ctx,
                                              diag, ret->line) != 0) {
                    return tc_exec_error();
                }
                value = cloned;
            } else if (ret_type == TC_MEMBLOCK && func) {
                /* 按值返回：memblock 返回深拷贝（§8.1.1），调用者获得独立副本 */
                TcValue cloned;
                if (tc_exec_memblock_clone(&func->return_type, &value, ctx, &cloned, diag,
                                           ret->line) != 0) {
                    return tc_exec_error();
                }
                value = cloned;
            }
        }
        return tc_exec_return_value(value, 1);
    }

    if (stmt->kind == TC_STMT_FUNCALL) {
        const TcFuncallStmt *call = &stmt->u.funcall_stmt;

        tc_stmt_index_take(&ctx->index);
        if (call->resolved_func_id < 0) {
            tc_exec_set_internal_error(diag, call->line, "internal error: unresolved funcall");
            return tc_exec_error();
        }
        if (tc_exec_call_function(call->resolved_func_id, call->args, NULL, call->arg_count, ctx,
                                  NULL, 0, diag, call->line) != 0) {
            return tc_exec_error();
        }
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_PTR_STORE) {
        const TcPtrStoreStmt *store = &stmt->u.ptr_store;

        tc_stmt_index_take(&ctx->index);
        if (tc_exec_ptr_store(&store->pointee_type, &store->ptr, &store->value, ctx, diag,
                              store->line) != 0) {
            return tc_exec_error();
        }
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_MEMBLOCK_STORE) {
        tc_stmt_index_take(&ctx->index);
        if (tc_exec_memblock_store_stmt(&stmt->u.memblock_store, ctx, diag) != 0) {
            return tc_exec_error();
        }
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_MEMBLOCK_COPY) {
        tc_stmt_index_take(&ctx->index);
        if (tc_exec_memblock_copy_stmt(&stmt->u.memblock_copy, ctx, diag) != 0) {
            return tc_exec_error();
        }
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_MEMCOPY_UNSAFE) {
        tc_stmt_index_take(&ctx->index);
        if (tc_exec_memcopy_unsafe_stmt(&stmt->u.memcopy_unsafe, ctx, diag) != 0) {
            return tc_exec_error();
        }
        return tc_exec_normal();
    }

    if (stmt->kind == TC_STMT_FIELD_ASSIGN) {
        tc_stmt_index_take(&ctx->index);
        if (tc_exec_struct_field_assign(&stmt->u.field_assign, ctx, diag) != 0) {
            return tc_exec_error();
        }
        return tc_exec_normal();
    }

    {
        tc_stmt_index_take(&ctx->index);

        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;
            TcValue value;
            TcTypeTag rhs_type = tc_type_scalar_tag(&var_def->full_type);

            if (!var_def->binding.resolved || var_def->binding.is_const ||
                var_def->binding.slot < 0 || !ctx->slots) {
                tc_exec_set_internal_error(diag, var_def->line,
                                           "internal error: unresolved var slot metadata");
                return tc_exec_error();
            }
            if (var_def->full_type.tag == TC_MEMBLOCK) {
                rhs_type = TC_MEMBLOCK;
            } else if (var_def->full_type.tag == TC_STRUCT) {
                rhs_type = TC_STRUCT;
            }
            if (tc_eval_rhs(&var_def->rhs, rhs_type, ctx, &value, diag, var_def->line) != 0) {
                return tc_exec_error();
            }
            if (var_def->full_type.tag == TC_STRUCT) {
                if (tc_exec_struct_store_value(&ctx->slots[var_def->binding.slot], &value,
                                              var_def->full_type.params.struct_type.struct_id, ctx,
                                              diag, var_def->line) != 0) {
                    return tc_exec_error();
                }
            } else if (var_def->full_type.tag == TC_MEMBLOCK) {
                /* 值语义：var 初始化深拷贝（§3.8.4），与源 memblock 不共享存储 */
                if (tc_exec_memblock_clone(&var_def->full_type, &value, ctx,
                                          &ctx->slots[var_def->binding.slot], diag,
                                          var_def->line) != 0) {
                    return tc_exec_error();
                }
            } else {
                ctx->slots[var_def->binding.slot] = value;
            }
        } else if (stmt->kind == TC_STMT_CONST_DEF) {
            (void)ctx;
        } else if (stmt->kind == TC_STMT_ASSIGN) {
            const TcAssign *assign = &stmt->u.assign;
            TcValue value;

            if (!assign->binding.resolved || assign->binding.is_const ||
                assign->binding.slot < 0 || !ctx->slots) {
                tc_exec_set_internal_error(diag, assign->line,
                                           "internal error: unresolved assignment metadata");
                return tc_exec_error();
            }
            if (tc_eval_rhs(&assign->rhs, assign->binding.type->tag, ctx, &value, diag,
                            assign->line) != 0) {
                return tc_exec_error();
            }
            if (assign->binding.type->tag == TC_STRUCT) {
                const TcSymbol *sym = tc_exec_find_symbol(ctx->symbols, assign->name);
                int sid = sym ? tc_type_struct_id(sym->type) : -1;

                if (sid < 0) {
                    tc_exec_set_internal_error(diag, assign->line,
                                               "internal error: struct assign missing id");
                    return tc_exec_error();
                }
                if (tc_exec_struct_store_value(&ctx->slots[assign->binding.slot], &value, sid, ctx,
                                              diag, assign->line) != 0) {
                    return tc_exec_error();
                }
            } else if (assign->binding.type->tag == TC_MEMBLOCK) {
                /* 值语义：整块赋值深拷贝（§3.8.4），与源 memblock 不共享存储 */
                if (tc_exec_memblock_clone(assign->binding.type, &value, ctx,
                                          &ctx->slots[assign->binding.slot], diag,
                                          assign->line) != 0) {
                    return tc_exec_error();
                }
            } else {
                ctx->slots[assign->binding.slot] = value;
            }
        } else if (stmt->kind == TC_STMT_WRITE) {
            if (tc_exec_io_write(&stmt->u.io_write, ctx, 0, diag) != 0) {
                return tc_exec_error();
            }
        } else if (stmt->kind == TC_STMT_WRITELN) {
            if (tc_exec_io_write(&stmt->u.io_write, ctx, 1, diag) != 0) {
                return tc_exec_error();
            }
        } else if (stmt->kind == TC_STMT_READ) {
            if (tc_exec_io_read(&stmt->u.io_read, ctx, diag) != 0) {
                return tc_exec_error();
            }
        } else {
            tc_exec_set_internal_error(diag, 0, "internal error: unhandled statement kind");
            return tc_exec_error();
        }
    }
    return tc_exec_normal();
}

static int tc_exec_init_static_var(const TcStaticVarDef *sv, TcExecuteCtx *ctx,
                                   TcDiagnostic *diag) {
    TcValue value;
    TcTypeTag rhs_type = sv->type.tag;

    if (sv->static_slot < 0) {
        tc_exec_set_internal_error(diag, sv->line, "internal error: unresolved static slot");
        return -1;
    }
    if (sv->type.tag == TC_MEMBLOCK) {
        rhs_type = TC_MEMBLOCK;
    } else if (sv->type.tag == TC_STRUCT) {
        rhs_type = TC_STRUCT;
    }
    if (tc_eval_rhs(&sv->rhs, rhs_type, ctx, &value, diag, sv->line) != 0) {
        return -1;
    }
    if (sv->type.tag == TC_STRUCT) {
        return tc_exec_struct_store_value(&ctx->slots[sv->static_slot], &value,
                                         sv->type.params.struct_type.struct_id, ctx, diag,
                                         sv->line);
    }
    ctx->slots[sv->static_slot] = value;
    return 0;
}

static int tc_exec_init_static_vars_program(const TcProgram *program, TcExecuteCtx *ctx,
                                            TcDiagnostic *diag) {
    size_t i = 0;

    for (i = 0; i < program->count; i++) {
        if (program->items[i].kind == TC_STMT_STATIC_VAR_DEF) {
            if (tc_exec_init_static_var(&program->items[i].u.static_var_def, ctx, diag) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int tc_exec_init_all_static_vars(const TcTypedProgram *program, TcExecuteCtx *ctx,
                                        TcDiagnostic *diag) {
    size_t i = 0;

    for (i = 0; i < program->dep_count; i++) {
        if (tc_exec_init_static_vars_program(&program->deps[i], ctx, diag) != 0) {
            return -1;
        }
    }
    return tc_exec_init_static_vars_program(&program->program, ctx, diag);
}

int tc_execute_statement(const TcStatement *stmt, TcValue *slots, const TcSymbolTable *symbols,
                         TcDiagnostic *diag) {
    TcExecuteCtx ctx;
    TcExecControl control;
    int span = tc_stmt_subtree_index_count(stmt);

    memset(&ctx, 0, sizeof(ctx));
    ctx.slots = slots;
    ctx.symbols = symbols;
    ctx.current_func_id = -1;
    tc_stmt_index_reset(&ctx.index);
    control = tc_execute_block(stmt, 1, 0, span, &ctx, diag);
    if (control.kind == TC_EXEC_ERROR) {
        return -1;
    }
    if (control.kind != TC_EXEC_NORMAL) {
        tc_exec_set_internal_error(diag, 0,
                                   "internal error: unconsumed control at statement boundary");
        return -1;
    }
    return 0;
}

int tc_execute(const TcTypedProgram *program, TcDiagnostic *diag) {
    TcValue *slots = NULL;
    TcExecuteCtx ctx;
    TcExecControl control;
    int total_span = 0;
    size_t slot_count = tc_symbol_table_runtime_slot_count(&program->symbols);

    if (slot_count > 0) {
        slots = (TcValue *)malloc(slot_count * sizeof(TcValue));
        if (!slots) {
            tc_diagnostic_set(diag, TC_ERR_OUT_OF_MEMORY, 0, TC_COLUMN_UNKNOWN,
                              "memory allocation failed");
            return -1;
        }
        tc_slots_init_uninitialized(slots, slot_count);
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.slots = slots;
    ctx.symbols = &program->symbols;
    ctx.program = program;
    ctx.current_func_id = -1;
    tc_stmt_index_reset(&ctx.index);

    if (tc_exec_init_all_static_vars(program, &ctx, diag) != 0) {
        tc_exec_memblock_heap_free(&ctx);
        tc_exec_struct_heap_free(&ctx);
        free(slots);
        return -1;
    }

    total_span = tc_stmt_block_index_span(program->program.items, program->program.count);
    control = tc_execute_block(program->program.items, program->program.count, 0, total_span, &ctx,
                               diag);
    if (control.kind == TC_EXEC_ERROR) {
        tc_exec_memblock_heap_free(&ctx);
        tc_exec_struct_heap_free(&ctx);
        free(slots);
        return -1;
    }
    if (control.kind != TC_EXEC_NORMAL) {
        tc_exec_set_internal_error(diag, 0,
                                   "internal error: unconsumed control at program boundary");
        tc_exec_memblock_heap_free(&ctx);
        tc_exec_struct_heap_free(&ctx);
        free(slots);
        return -1;
    }

    tc_exec_memblock_heap_free(&ctx);
    tc_exec_struct_heap_free(&ctx);
    free(slots);
    return 0;
}
