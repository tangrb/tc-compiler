/*
 * tc_aot_emit_rhs.c — AOT Codegen 右值表达式发射
 *
 * 将 TcRhs 转译为对 tc_aot_rt.h shim 的 C99 调用（含 ptr/memblock/struct）。
 * 标量语义委托 tc_sem_* / tc_io，与 TC-VM 一致。
 */
#include "tc_aot_codegen.h"
#include "tc_aot_codegen_internal.h"

#include "tc_diagnostic.h"
#include "tc_struct_check.h"
#include "tc_symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int tc_aot_emit_rhs_slot(FILE *out, const TcRhs *rhs, TcTypeTag expected_type, int dst_slot,
                         const char *indent, TcAotEmitCtx *ctx, int stmt_index, int line) {
    char dst_expr[32];

    snprintf(dst_expr, sizeof(dst_expr), "slots[%d]", dst_slot);
    return tc_aot_emit_rhs(out, rhs, expected_type, dst_expr, indent, ctx, stmt_index, line);
}

int tc_aot_emit_funcall(FILE *out, int func_id, const TcNamedArg *stmt_args,
                        size_t stmt_arg_count, const TcAotFuncallExprArg *expr_args,
                        size_t expr_arg_count, int use_expr_args, const char *indent,
                        TcAotEmitCtx *ctx, int stmt_index, int line, int want_result,
                        const char *dst_expr) {
    const TcProgram *module = NULL;
    const TcFuncDef *func = NULL;
    char abort_indent[64];
    size_t pi = 0;

    func = tc_aot_find_func_def(ctx->program, func_id, &module);
    if (!func) {
        return -1;
    }
    tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);

    for (pi = 0; pi < func->param_count; pi++) {
        const TcFuncParam *param = &func->params[pi];
        const TcRhs *arg_rhs = NULL;
        int param_slot = -1;

        if (use_expr_args) {
            arg_rhs = tc_aot_find_expr_arg_rhs(param->name, expr_args, expr_arg_count);
        } else {
            arg_rhs = tc_aot_find_named_arg_rhs(param->name, stmt_args, stmt_arg_count);
        }
        if (!arg_rhs) {
            return -1;
        }
        if (tc_aot_param_slot(&ctx->program->symbols, func, param->name, &param_slot) != 0 ||
            param_slot < 0) {
            return -1;
        }
        if (tc_aot_emit_rhs_slot(out, arg_rhs, param->type.tag, param_slot, indent, ctx,
                                 stmt_index, line) != 0) {
            return -1;
        }
    }

    fprintf(out, "%stc_aot_func_%d(tc_aot_cur_diag);\n", indent, func_id);
    fprintf(out, "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, %d);\n",
            abort_indent, line);
    if (want_result && dst_expr) {
        fprintf(out, "%s%s = tc_aot_ret_%d;\n", indent, dst_expr, func_id);
    }
    return 0;
}

int tc_aot_emit_rhs(FILE *out, const TcRhs *rhs, TcTypeTag expected_type,
                    const char *dst_expr, const char *indent, TcAotEmitCtx *ctx,
                    int stmt_index, int line) {
    const TcSymbolTable *symbols = &ctx->program->symbols;
    char abort_indent[64];

    tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);

    if (rhs->kind == TC_RHS_LIT) {
        fprintf(out, "%s%s = ", indent, dst_expr);
        tc_aot_emit_literal_expr(out, expected_type, &rhs->u.lit);
        fprintf(out, ";\n");
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_REF) {
        if (rhs->u.const_ref.binding.resolved) {
            if (rhs->u.const_ref.binding.is_const) {
                fprintf(out, "%s%s = 0x%016" PRIx64 "ULL;\n", indent, dst_expr,
                        rhs->u.const_ref.binding.const_bits);
            } else if (rhs->u.const_ref.binding.slot >= 0) {
                if (expected_type == TC_STRUCT) {
                    const TcSymbol *sym = tc_symbol_table_find_visible(
                        symbols, rhs->u.const_ref.name, stmt_index, &ctx->sym_index);
                    size_t bytes = 0;

                    if (sym && tc_type_struct_id(sym->type) >= 0 && ctx->program->struct_table) {
                        const TcStructEntry *e =
                            tc_struct_table_get(ctx->program->struct_table,
                                               tc_type_struct_id(sym->type));
                        if (e) {
                            bytes = (e->width_bits + 7U) / 8U;
                        }
                    }
                    if (bytes == 0) {
                        return -1;
                    }
                    fprintf(out,
                            "%s%s = tc_aot_struct_clone(slots[%d], %zu, tc_aot_cur_diag, %d);\n",
                            indent, dst_expr, rhs->u.const_ref.binding.slot, bytes, line);
                    fprintf(out,
                            "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                            "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                            abort_indent, line);
                } else if (expected_type == TC_MEMBLOCK) {
                    /* 值语义：memblock 标识符 RHS 深拷贝（§3.8.4），与源不共享存储 */
                    const TcType *type = rhs->u.const_ref.binding.type;

                    if (!type || type->tag != TC_MEMBLOCK ||
                        !type->params.memblock_type.element) {
                        return -1;
                    }
                    {
                        size_t elem_bits = tc_sizeof_bits_ex(
                            type->params.memblock_type.element, tc_struct_table_width_bits,
                            ctx->program->struct_table);
                        size_t elem_bytes = (elem_bits + 7U) / 8U;
                        uint64_t count = tc_type_memblock_count(type);

                        fprintf(out,
                                "%s%s = tc_aot_memblock_clone(slots[%d], %zu, %" PRIu64
                                "ULL, tc_aot_cur_diag, %d);\n",
                                indent, dst_expr, rhs->u.const_ref.binding.slot, elem_bytes, count,
                                line);
                        fprintf(out,
                                "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                                "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                                abort_indent, line);
                    }
                } else {
                    fprintf(out, "%s%s = slots[%d];\n", indent, dst_expr,
                            rhs->u.const_ref.binding.slot);
                }
            } else {
                return -1;
            }
            return 0;
        }
        {
            const TcSymbol *symbol = tc_symbol_table_find_visible(
                symbols, rhs->u.const_ref.name, stmt_index, &ctx->sym_index);

            if (!symbol) {
                return -1;
            }
            if (symbol->sym_kind == TC_SYM_CONSTANT && symbol->has_const_value) {
                fprintf(out, "%s%s = 0x%016" PRIx64 "ULL;\n", indent, dst_expr,
                        symbol->const_value.bits);
            } else if (expected_type == TC_MEMBLOCK &&
                       symbol->type->tag == TC_MEMBLOCK &&
                       symbol->type->params.memblock_type.element) {
                /* 值语义：memblock 标识符 RHS 深拷贝（§3.8.4），与源不共享存储 */
                size_t elem_bits = tc_sizeof_bits_ex(
                    symbol->type->params.memblock_type.element, tc_struct_table_width_bits,
                    ctx->program->struct_table);
                size_t elem_bytes = (elem_bits + 7U) / 8U;
                uint64_t count = tc_type_memblock_count(symbol->type);

                fprintf(out, "%s%s = tc_aot_memblock_clone(slots[%d], %zu, %" PRIu64
                             "ULL, tc_aot_cur_diag, %d);\n",
                        indent, dst_expr, symbol->slot, elem_bytes, count, line);
                fprintf(out,
                        "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                        "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                        abort_indent, line);
            } else if (expected_type == TC_STRUCT) {
                size_t bytes = 0;
                if (tc_type_struct_id(symbol->type) >= 0 && ctx->program->struct_table) {
                    const TcStructEntry *e =
                        tc_struct_table_get(ctx->program->struct_table,
                                           tc_type_struct_id(symbol->type));
                    if (e) {
                        bytes = (e->width_bits + 7U) / 8U;
                    }
                }
                if (bytes == 0) {
                    return -1;
                }
                fprintf(out, "%s%s = tc_aot_struct_clone(slots[%d], %zu, tc_aot_cur_diag, %d);\n",
                        indent, dst_expr, symbol->slot, bytes, line);
                fprintf(out,
                        "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                        "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                        abort_indent, line);
            } else {
                fprintf(out, "%s%s = slots[%d];\n", indent, dst_expr, symbol->slot);
            }
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_ARITH) {
        const char *mode =
            rhs->u.arith.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";
        const char *op_name = NULL;

        switch (rhs->u.arith.op) {
        case TC_ADD:
            op_name = "TC_ADD";
            break;
        case TC_SUB:
            op_name = "TC_SUB";
            break;
        case TC_MUL:
            op_name = "TC_MUL";
            break;
        case TC_DIV:
            op_name = "TC_DIV";
            break;
        case TC_MOD:
            op_name = "TC_MOD";
            break;
        }

        fprintf(out, "%sif (tc_aot_arith(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.arith.type->tag), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.arith.lhs, rhs->u.arith.type->tag, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.arith.rhs, rhs->u.arith.type->tag, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_UNARY) {
        const char *mode =
            rhs->u.unary.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";
        const char *op_name = rhs->u.unary.op == TC_UNARY_ABS ? "TC_UNARY_ABS" : "TC_UNARY_NEG";

        fprintf(out, "%sif (tc_aot_unary(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.unary.type->tag), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.unary.operand, rhs->u.unary.type->tag, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_COMPARE) {
        const char *op_name = "TC_CMP_EQ";
        switch (rhs->u.compare.op) {
        case TC_CMP_EQ:
            op_name = "TC_CMP_EQ";
            break;
        case TC_CMP_NE:
            op_name = "TC_CMP_NE";
            break;
        case TC_CMP_LT:
            op_name = "TC_CMP_LT";
            break;
        case TC_CMP_LE:
            op_name = "TC_CMP_LE";
            break;
        case TC_CMP_GT:
            op_name = "TC_CMP_GT";
            break;
        case TC_CMP_GE:
            op_name = "TC_CMP_GE";
            break;
        }
        fprintf(out, "%sif (tc_aot_compare(%s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.compare.type->tag), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.compare.lhs, rhs->u.compare.type->tag, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.compare.rhs, rhs->u.compare.type->tag, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_UN) {
        fprintf(out, "%sif (tc_aot_logic_unary(TC_LOGIC_NOT, &%s, ", indent, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_un.operand, TC_BOOL, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_LOGIC_BIN) {
        const char *op_name;
        if (rhs->u.logic_bin.op == TC_LOGIC_AND) {
            op_name = "TC_LOGIC_AND";
        } else if (rhs->u.logic_bin.op == TC_LOGIC_XOR) {
            op_name = "TC_LOGIC_XOR";
        } else {
            op_name = "TC_LOGIC_OR";
        }
        fprintf(out, "%sif (tc_aot_logic(%s, &%s, ", indent, op_name, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.lhs, TC_BOOL, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.logic_bin.rhs, TC_BOOL, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_BIN) {
        const char *op_name = "TC_BIT_AND";

        switch (rhs->u.bitwise_bin.op) {
        case TC_BIT_AND:
            op_name = "TC_BIT_AND";
            break;
        case TC_BIT_OR:
            op_name = "TC_BIT_OR";
            break;
        case TC_BIT_XOR:
            op_name = "TC_BIT_XOR";
            break;
        }

        fprintf(out, "%sif (tc_aot_bitwise_binary(%s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.bitwise_bin.type->tag), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_bin.lhs, rhs->u.bitwise_bin.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_bin.rhs, rhs->u.bitwise_bin.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITWISE_UN) {
        fprintf(out, "%sif (tc_aot_bitwise_unary(%s, &%s, ", indent,
                tc_aot_type_enum(rhs->u.bitwise_un.type->tag), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitwise_un.operand, rhs->u.bitwise_un.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_SHIFT) {
        const char *op_name =
            rhs->u.shift.op == TC_SHIFT_SHL ? "TC_SHIFT_SHL" : "TC_SHIFT_SHR";
        const char *mode =
            rhs->u.shift.mode == TC_ARITH_WRAP ? "TC_ARITH_WRAP" : "TC_ARITH_STRICT";

        fprintf(out, "%sif (tc_aot_shift(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.shift.type->tag), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.shift.value, rhs->u.shift.type->tag, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.shift.count, rhs->u.shift.type->tag, ctx, stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_BITCAST) {
        if (rhs->u.bitcast.target.tag == TC_PTR || rhs->u.bitcast.source_type->tag == TC_PTR) {
            /* 等宽位重解释：ptr ↔ 整数 / ptr ↔ ptr，仅复制位模式 */
            fprintf(out, "%s%s = ", indent, dst_expr);
            tc_aot_emit_operand_expr(out, &rhs->u.bitcast.source, rhs->u.bitcast.source_type->tag,
                                     ctx, stmt_index);
            fprintf(out, ";\n");
            return 0;
        }
        fprintf(out, "%sif (tc_aot_bitcast(%s, %s, &%s, ", indent,
                tc_aot_type_enum(rhs->u.bitcast.target.tag),
                tc_aot_type_enum(rhs->u.bitcast.source_type->tag), dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.bitcast.source, rhs->u.bitcast.source_type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_CONST_CAST) {
        return -1;
    }

    if (rhs->kind == TC_RHS_FLOAT_ARITH) {
        const char *mode = rhs->u.float_arith.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                    : "TC_FLOAT_STRICT";
        const char *op_name = "TC_ADD";

        switch (rhs->u.float_arith.op) {
        case TC_ADD:
            op_name = "TC_ADD";
            break;
        case TC_SUB:
            op_name = "TC_SUB";
            break;
        case TC_MUL:
            op_name = "TC_MUL";
            break;
        case TC_DIV:
            op_name = "TC_DIV";
            break;
        case TC_MOD:
            op_name = "TC_MOD";
            break;
        }
        fprintf(out, "%sif (tc_aot_fp_arith(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_arith.type->tag), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_arith.lhs, rhs->u.float_arith.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.float_arith.rhs, rhs->u.float_arith.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_UNARY) {
        const char *mode = rhs->u.float_unary.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                    : "TC_FLOAT_STRICT";
        const char *op_name =
            rhs->u.float_unary.op == TC_UNARY_ABS ? "TC_UNARY_ABS" : "TC_UNARY_NEG";

        fprintf(out, "%sif (tc_aot_fp_unary(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_unary.type->tag), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_unary.operand, rhs->u.float_unary.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_FLOAT_COMPARE) {
        const char *mode = rhs->u.float_compare.mode == TC_FLOAT_IEEE ? "TC_FLOAT_IEEE"
                                                                      : "TC_FLOAT_STRICT";
        const char *op_name = "TC_CMP_EQ";

        switch (rhs->u.float_compare.op) {
        case TC_CMP_EQ:
            op_name = "TC_CMP_EQ";
            break;
        case TC_CMP_NE:
            op_name = "TC_CMP_NE";
            break;
        case TC_CMP_LT:
            op_name = "TC_CMP_LT";
            break;
        case TC_CMP_LE:
            op_name = "TC_CMP_LE";
            break;
        case TC_CMP_GT:
            op_name = "TC_CMP_GT";
            break;
        case TC_CMP_GE:
            op_name = "TC_CMP_GE";
            break;
        }
        fprintf(out, "%sif (tc_aot_fp_compare(%s, %s, %s, &%s, ", indent, op_name,
                tc_aot_type_enum(rhs->u.float_compare.type->tag), mode, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.float_compare.lhs, rhs->u.float_compare.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.float_compare.rhs, rhs->u.float_compare.type->tag, ctx,
                                 stmt_index);
        fprintf(out, ", tc_aot_cur_diag, %d) != 0)\n", line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_ADDRESS) {
        int slot = -1;

        if (rhs->u.ptr_address.name &&
            tc_aot_resolve_var_slot(symbols, &ctx->sym_index, rhs->u.ptr_address.name, stmt_index,
                                    &slot) == 0) {
            fprintf(out, "%s%s = tc_aot_ptr_address(%d);\n", indent, dst_expr, slot);
            return 0;
        }
        return -1;
    }

    if (rhs->kind == TC_RHS_PTR_LOAD) {
        fprintf(out, "%sif (tc_aot_ptr_load(slots, ", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_load.ptr, TC_PTR, ctx, stmt_index);
        fprintf(out, ", &%s, tc_aot_cur_diag, %d) != 0)\n", dst_expr, line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_ADD || rhs->kind == TC_RHS_PTR_SUB) {
        int is_add = rhs->kind == TC_RHS_PTR_ADD ? 1 : 0;

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _off;\n", indent);
        fprintf(out, "%s    _off = ", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_arith.offset, TC_USIZE, ctx, stmt_index);
        fprintf(out, ";\n");
        fprintf(out, "%s    if (tc_aot_ptr_arith(%d, ", indent, is_add);
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_arith.ptr, TC_PTR, ctx, stmt_index);
        fprintf(out, ", (int64_t)_off, &%s, tc_aot_cur_diag, %d) != 0)\n", dst_expr, line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_EQ || rhs->kind == TC_RHS_PTR_NE || rhs->kind == TC_RHS_PTR_LT ||
        rhs->kind == TC_RHS_PTR_LE || rhs->kind == TC_RHS_PTR_GT || rhs->kind == TC_RHS_PTR_GE) {
        TcCompareOp op = TC_CMP_EQ;

        switch (rhs->kind) {
        case TC_RHS_PTR_EQ:
            op = TC_CMP_EQ;
            break;
        case TC_RHS_PTR_NE:
            op = TC_CMP_NE;
            break;
        case TC_RHS_PTR_LT:
            op = TC_CMP_LT;
            break;
        case TC_RHS_PTR_LE:
            op = TC_CMP_LE;
            break;
        case TC_RHS_PTR_GT:
            op = TC_CMP_GT;
            break;
        case TC_RHS_PTR_GE:
            op = TC_CMP_GE;
            break;
        default:
            break;
        }
        fprintf(out, "%sif (tc_aot_ptr_compare(%s, ", indent, tc_aot_ptr_compare_op(op));
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_compare.lhs, TC_PTR, ctx, stmt_index);
        fprintf(out, ", ");
        tc_aot_emit_operand_expr(out, &rhs->u.ptr_compare.rhs, TC_PTR, ctx, stmt_index);
        fprintf(out, ", &%s, tc_aot_cur_diag, %d) != 0)\n", dst_expr, line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        return 0;
    }

    if (rhs->kind == TC_RHS_PTR_SIZE) {
        TcType pointee = rhs->u.ptr_size.pointee_type;
        size_t sizeof_bits =
            tc_sizeof_bits_ex(&pointee, tc_struct_table_width_bits, ctx->program->struct_table);

        (void)rhs->u.ptr_size.ptr;
        fprintf(out, "%s%s = tc_aot_ptr_size(%zu);\n", indent, dst_expr, sizeof_bits);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_CONSTRUCTOR) {
        const TcType *element = &rhs->u.memblock_ctor.element_type;
        size_t elem_bits =
            tc_sizeof_bits_ex(element, tc_struct_table_width_bits, ctx->program->struct_table);
        size_t elem_bytes = (elem_bits + 7U) / 8U;
        size_t i = 0;
        const char *set_shim = (element->tag == TC_STRUCT) ? "tc_aot_memblock_set_elem_struct"
                                                           : "tc_aot_memblock_set_elem";

        fprintf(out, "%s%s = tc_aot_memblock_alloc(%" PRIu64 "ULL, %zu, tc_aot_cur_diag, %d);\n",
                indent, dst_expr, rhs->u.memblock_ctor.count, elem_bytes, line);
        fprintf(out, "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent,
                line);
        if (rhs->u.memblock_ctor.is_fill) {
            char fill_tmp[32];
            snprintf(fill_tmp, sizeof(fill_tmp), "_tc_fill_%d", stmt_index);
            fprintf(out, "%s{\n", indent);
            fprintf(out, "%s    uint64_t %s;\n", indent, fill_tmp);
            if (tc_aot_emit_operand_assign(out, &rhs->u.memblock_ctor.fill_value, element->tag,
                                           fill_tmp, abort_indent, ctx, stmt_index) != 0) {
                return -1;
            }
            for (i = 0; i < rhs->u.memblock_ctor.count; i++) {
                fprintf(out, "%s    %s(%s, %zu, %zu, %s);\n", abort_indent, set_shim, dst_expr,
                        elem_bytes, i, fill_tmp);
            }
            fprintf(out, "%s}\n", indent);
        } else {
            for (i = 0; i < rhs->u.memblock_ctor.value_count; i++) {
                char elem_tmp[32];
                snprintf(elem_tmp, sizeof(elem_tmp), "_tc_mb_%d_%zu", stmt_index, i);
                fprintf(out, "%s{\n", indent);
                fprintf(out, "%s    uint64_t %s;\n", indent, elem_tmp);
                if (tc_aot_emit_operand_assign(out, &rhs->u.memblock_ctor.values[i], element->tag,
                                               elem_tmp, abort_indent, ctx, stmt_index) != 0) {
                    return -1;
                }
                fprintf(out, "%s    %s(%s, %zu, %zu, %s);\n", abort_indent, set_shim, dst_expr,
                        elem_bytes, i, elem_tmp);
                fprintf(out, "%s}\n", indent);
            }
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_LOAD) {
        const TcType *element = &rhs->u.memblock_load.element_type;
        size_t elem_bytes = (tc_sizeof_bits_ex(element, tc_struct_table_width_bits,
                                               ctx->program->struct_table) + 7U) / 8U;

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _mb_idx;\n", indent);
        fprintf(out, "%s    _mb_idx = ", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.memblock_load.index, TC_USIZE, ctx, stmt_index);
        fprintf(out, ";\n");
        fprintf(out, "%s    if (tc_aot_memblock_load(", indent);
        tc_aot_emit_operand_expr(out, &rhs->u.memblock_load.memblock, TC_MEMBLOCK, ctx, stmt_index);
        fprintf(out, ", %zu, _mb_idx, %s, &%s, tc_aot_cur_diag, %d) != 0)\n", elem_bytes,
                tc_aot_type_enum(element->tag), dst_expr, line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (rhs->kind == TC_RHS_MEMBLOCK_COUNT) {
        const TcSymbol *sym = tc_symbol_table_find_visible(
            symbols, rhs->u.memblock_count.memblock_name, stmt_index, &ctx->sym_index);

        if (rhs->u.memblock_count.binding.resolved && rhs->u.memblock_count.binding.slot >= 0) {
            uint64_t decl_count =
                tc_type_memblock_count(rhs->u.memblock_count.binding.type);

            fprintf(out, "%s{\n", indent);
            fprintf(out, "%s    uint64_t _mb_cnt = tc_aot_memblock_get_count(slots[%d]);\n",
                    indent, rhs->u.memblock_count.binding.slot);
            if (decl_count > 0) {
                fprintf(out, "%s    if (_mb_cnt == 0) _mb_cnt = %" PRIu64 "ULL;\n", indent,
                        decl_count);
            }
            fprintf(out, "%s    %s = _mb_cnt;\n", indent, dst_expr);
            fprintf(out, "%s}\n", indent);
            return 0;
        }
        if (sym && sym->slot >= 0) {
            fprintf(out, "%s{\n", indent);
            fprintf(out, "%s    uint64_t _mb_cnt = tc_aot_memblock_get_count(slots[%d]);\n", indent,
                    sym->slot);
            if (tc_type_memblock_count(sym->type) > 0) {
                fprintf(out, "%s    if (_mb_cnt == 0) _mb_cnt = %" PRIu64 "ULL;\n", indent,
                        tc_type_memblock_count(sym->type));
            }
            fprintf(out, "%s    %s = _mb_cnt;\n", indent, dst_expr);
            fprintf(out, "%s}\n", indent);
            return 0;
        }
        if (sym && tc_type_memblock_count(sym->type) > 0) {
            fprintf(out, "%s%s = %" PRIu64 "ULL;\n", indent, dst_expr,
                    tc_type_memblock_count(sym->type));
            return 0;
        }
        return -1;
    }

    if (rhs->kind == TC_RHS_SELF_MEMBER) {
        const TcSymbol *sym = tc_aot_find_symbol_by_name(symbols, rhs->u.self_member.member_name);

        if (!sym) {
            return -1;
        }
        if (sym->sym_kind == TC_SYM_CONSTANT && sym->has_const_value) {
            fprintf(out, "%s%s = 0x%016" PRIx64 "ULL;\n", indent, dst_expr,
                    sym->const_value.bits);
            return 0;
        }
        if (sym->slot >= 0) {
            fprintf(out, "%s%s = slots[%d];\n", indent, dst_expr, sym->slot);
            return 0;
        }
        return -1;
    }

    if (rhs->kind == TC_RHS_STRUCT_CONSTRUCTOR) {
        const TcStructTable *table = ctx->program->struct_table;
        const TcStructEntry *entry = NULL;
        size_t bytes = 0;
        size_t bit_off = 0;
        size_t i = 0;
        TcDiagnostic local_diag;

        entry = tc_struct_table_find(table, rhs->u.struct_ctor.struct_name);
        if (!entry) {
            return -1;
        }
        bytes = (entry->width_bits + 7U) / 8U;
        fprintf(out, "%s%s = tc_aot_struct_alloc(%zu, tc_aot_cur_diag, %d);\n", indent, dst_expr,
                bytes, line);
        fprintf(out,
                "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) tc_aot_abort(tc_aot_cur_diag, %d);\n",
                abort_indent, line);
        tc_diagnostic_init(&local_diag);
        for (i = 0; i < entry->field_count; i++) {
            const TcStructField *field = &entry->fields[i];
            size_t offset = bit_off / 8U;
            size_t nbytes = 0;
            size_t fi = 0;
            char tmp[48];
            int found = 0;
            int tmp_id = ctx->tmp_seq++;

            if (field->type.tag == TC_STRUCT) {
                const TcStructEntry *nested =
                    tc_struct_table_get(table, field->type.params.struct_type.struct_id);
                nbytes = nested ? (nested->width_bits + 7U) / 8U : 0;
            } else {
                nbytes = (tc_sizeof_bits_ex(&field->type, tc_struct_table_width_bits,
                                            ctx->program->struct_table) + 7U) / 8U;
            }
            snprintf(tmp, sizeof(tmp), "_tc_sf_%d_%d", stmt_index, tmp_id);
            for (fi = 0; fi < rhs->u.struct_ctor.field_count; fi++) {
                if (strcmp(rhs->u.struct_ctor.fields[fi].param_name, field->name) != 0) {
                    continue;
                }
                found = 1;
                fprintf(out, "%s{\n", indent);
                fprintf(out, "%s    uint64_t %s;\n", indent, tmp);
                if (rhs->u.struct_ctor.fields[fi].has_rhs) {
                    if (tc_aot_emit_rhs(out, (const TcRhs *)rhs->u.struct_ctor.fields[fi].value_rhs,
                                        field->type.tag, tmp, abort_indent, ctx, stmt_index,
                                        line) != 0) {
                        return -1;
                    }
                } else if (tc_aot_emit_operand_assign(out, &rhs->u.struct_ctor.fields[fi].value_op,
                                                      field->type.tag, tmp, abort_indent, ctx,
                                                      stmt_index) != 0) {
                    return -1;
                }
                if (field->type.tag == TC_STRUCT || field->type.tag == TC_MEMBLOCK) {
                    /* struct/memblock 字段按值语义内联拷贝内容（§3.9.3） */
                    fprintf(out, "%s    tc_aot_struct_memcpy_field(%s, %zu, %zu, %s);\n",
                            abort_indent, dst_expr, offset, nbytes, tmp);
                } else {
                    fprintf(out, "%s    tc_aot_struct_store_bits(%s, %zu, %zu, %s);\n", abort_indent,
                            dst_expr, offset, nbytes, tmp);
                }
                fprintf(out, "%s}\n", indent);
                break;
            }
            if (!found) {
                return -1;
            }
            if (field->type.tag == TC_STRUCT) {
                const TcStructEntry *nested =
                    tc_struct_table_get(table, field->type.params.struct_type.struct_id);
                bit_off += nested ? nested->width_bits : 0;
            } else {
                bit_off += tc_sizeof_bits_ex(&field->type, tc_struct_table_width_bits,
                                             ctx->program->struct_table);
            }
            bit_off += (size_t)field->padding * 8U;
        }
        (void)local_diag;
        return 0;
    }

    if (rhs->kind == TC_RHS_FIELD_READ) {
        const TcStructTable *table = ctx->program->struct_table;
        const TcSymbol *base_sym = tc_symbol_table_find_visible(
            symbols, rhs->u.field_read.base, stmt_index, &ctx->sym_index);
        size_t offset = 0;
        const TcType *field_type = NULL;
        size_t nbytes = 0;
        TcDiagnostic local_diag;

        if (!base_sym || base_sym->slot < 0 || tc_type_struct_id(base_sym->type) < 0) {
            return -1;
        }
        tc_diagnostic_init(&local_diag);
        if (tc_struct_path_offset_bytes(table, tc_type_struct_id(base_sym->type),
                                        rhs->u.field_read.fields,
                                        rhs->u.field_read.field_count, &offset, &field_type,
                                        &local_diag, line) != 0) {
            return -1;
        }
        if (field_type->tag == TC_STRUCT || field_type->tag == TC_MEMBLOCK) {
            if (field_type->tag == TC_STRUCT) {
                const TcStructEntry *nested =
                    tc_struct_table_get(table, field_type->params.struct_type.struct_id);
                nbytes = nested ? (nested->width_bits + 7U) / 8U : 0;
            } else {
                nbytes = (tc_sizeof_bits_ex(field_type, tc_struct_table_width_bits,
                                            ctx->program->struct_table) + 7U) / 8U;
            }
            /* struct/memblock 字段抽出为独立堆块（值语义，§3.9.4） */
            fprintf(out,
                    "%s%s = tc_aot_struct_extract(slots[%d], %zu, %zu, tc_aot_cur_diag, %d);\n",
                    indent, dst_expr, base_sym->slot, offset, nbytes, line);
            fprintf(out,
                    "%sif (tc_aot_cur_diag->domain != TC_DIAG_NONE) "
                    "tc_aot_abort(tc_aot_cur_diag, %d);\n",
                    abort_indent, line);
        } else {
            nbytes = (tc_sizeof_bits_ex(field_type, tc_struct_table_width_bits,
                                        ctx->program->struct_table) + 7U) / 8U;
            fprintf(out, "%stc_aot_struct_load_bits(slots[%d], %zu, %zu, &%s);\n", indent,
                    base_sym->slot, offset, nbytes, dst_expr);
        }
        return 0;
    }

    if (rhs->kind == TC_RHS_FUNCALL_EXPR) {
        if (rhs->u.funcall_expr.resolved_func_id < 0) {
            return -1;
        }
        return tc_aot_emit_funcall(out, rhs->u.funcall_expr.resolved_func_id, NULL, 0,
                                   (const TcAotFuncallExprArg *)rhs->u.funcall_expr.args,
                                   rhs->u.funcall_expr.arg_count, 1, indent, ctx, stmt_index, line,
                                   1, dst_expr);
    }

    if (rhs->kind != TC_RHS_CAST) {
        return -1;
    }

    if (rhs->u.cast.target.tag == TC_PTR) {
        /* ptr<U>→ptr<T>：等宽所指类型，仅复制指针位模式 */
        fprintf(out, "%s%s = ", indent, dst_expr);
        tc_aot_emit_operand_expr(out, &rhs->u.cast.source, rhs->u.cast.source_type->tag, ctx,
                                 stmt_index);
        fprintf(out, ";\n");
        return 0;
    }

    {
        const char *mode =
            rhs->u.cast.mode == TC_TRUNC_TRUNCATE ? "TC_TRUNC_TRUNCATE" : "TC_TRUNC_STRICT";

        fprintf(out, "%sif (tc_aot_cast(%s, %s, ", indent,
                tc_aot_type_enum(rhs->u.cast.target.tag), mode);
        tc_aot_emit_operand_expr(out, &rhs->u.cast.source, rhs->u.cast.source_type->tag, ctx, stmt_index);
        fprintf(out, ", %s, &%s, tc_aot_cur_diag, %d) != 0)\n",
                tc_aot_type_enum(rhs->u.cast.source_type->tag), dst_expr, line);
        fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, line);
    }
    return 0;
}
