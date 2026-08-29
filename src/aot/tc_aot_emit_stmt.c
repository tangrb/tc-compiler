/*
 * tc_aot_emit_stmt.c — AOT Codegen 语句发射
 *
 * 将 TcStatement 转译为原生 C99 控制流与 shim 调用。
 * if → 原生 C if-else；while → for(;;)+显式条件；label/goto → tc_label_<stmt_index>。
 */
#include "tc_aot_codegen.h"
#include "tc_aot_codegen_internal.h"

#include "tc_diagnostic.h"
#include "tc_struct_check.h"
#include "tc_symbol.h"

#include <stdio.h>
#include <string.h>

static TcTypeTag tc_aot_memcopy_index_type(const TcOperand *op, const TcAotEmitCtx *ctx,
                                           int stmt_index) {
    const TcSymbol *sym = NULL;

    if (op->kind == TC_OPERAND_FIELD_READ && op->u.field_read.resolved.resolved &&
        op->u.field_read.resolved.field_type) {
        return op->u.field_read.resolved.field_type->tag;
    }
    if (op->binding.resolved && op->binding.type) {
        return op->binding.type->tag;
    }
    if (op->kind == TC_OPERAND_VAR && op->u.name && ctx && ctx->program) {
        sym = tc_symbol_table_find_visible(&ctx->program->symbols, op->u.name, stmt_index,
                                           &ctx->sym_index);
        if (sym && sym->type) {
            return sym->type->tag;
        }
    }
    if (op->kind == TC_OPERAND_LIT) {
        if (op->u.lit.unsigned_suffix) {
            return TC_USIZE;
        }
        if (op->u.lit.negative) {
            return TC_ISIZE;
        }
    }
    return TC_USIZE;
}

int tc_aot_emit_statement_impl(FILE *out, const TcStatement *stmt, TcAotEmitCtx *ctx,
                               const char *indent) {
    const TcSymbolTable *symbols = &ctx->program->symbols;

    if (stmt->kind == TC_STMT_WHILE) {
        const TcWhileStmt *while_stmt = &stmt->u.while_stmt;
        char loop_indent[64];
        char body_indent[64];
        char control_indent[64];
        char cond_name[32];
        size_t i = 0;
        int while_stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(loop_indent, sizeof(loop_indent), indent, 1);
        tc_aot_sub_indent(body_indent, sizeof(body_indent), loop_indent, 1);
        tc_aot_sub_indent(control_indent, sizeof(control_indent), body_indent, 1);
        if (loop_indent[0] == '\0' || body_indent[0] == '\0' || control_indent[0] == '\0') {
            return -1;
        }
        if (snprintf(cond_name, sizeof(cond_name), "tc_cond_%d", while_stmt_index) < 0) {
            return -1;
        }

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%sfor (;;) {\n", loop_indent);
        fprintf(out, "%suint64_t %s;\n", body_indent, cond_name);
        if (tc_aot_emit_rhs(out, &while_stmt->condition, TC_BOOL, cond_name, body_indent, ctx,
                            while_stmt_index, while_stmt->line) != 0) {
            return -1;
        }
        fprintf(out, "%sif (%s == 0) {\n", body_indent, cond_name);
        fprintf(out, "%sbreak;\n", control_indent);
        fprintf(out, "%s}\n", body_indent);

        if (tc_aot_block_path_push(
                &ctx->block_path,
                (TcBlockId){while_stmt_index, TC_BLOCK_WHILE}) != 0 ||
            tc_aot_loop_stack_push(&ctx->loops, while_stmt->loop_id) != 0) {
            return -1;
        }
        for (i = 0; i < while_stmt->body_count; i++) {
            if (tc_aot_emit_statement_impl(out, &while_stmt->body[i], ctx, body_indent) != 0) {
                return -1;
            }
        }
        tc_aot_loop_stack_pop(&ctx->loops);
        tc_aot_block_path_pop(&ctx->block_path);

        fprintf(out, "%s}\n", loop_indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_IF) {
        const TcIfStmt *if_stmt = &stmt->u.if_stmt;
        char block_indent[64];
        char branch_indent[64];
        size_t i = 0;
        int if_stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(block_indent, sizeof(block_indent), indent, 1);
        tc_aot_sub_indent(branch_indent, sizeof(branch_indent), block_indent, 1);

        fprintf(out, "%s{\n", indent);
        fprintf(out, "%suint64_t _cond;\n", block_indent);

        if (tc_aot_emit_rhs(out, &if_stmt->condition, TC_BOOL, "_cond", block_indent, ctx,
                            if_stmt_index, if_stmt->line) != 0) {
            return -1;
        }

        fprintf(out, "%sif (_cond != 0) {\n", block_indent);
        if (tc_aot_block_path_push(
                &ctx->block_path,
                (TcBlockId){if_stmt_index, TC_BLOCK_IF_THEN}) != 0) {
            return -1;
        }
        for (i = 0; i < if_stmt->then_count; i++) {
            if (tc_aot_emit_statement_impl(out, &if_stmt->then_body[i], ctx, branch_indent) != 0) {
                return -1;
            }
        }
        tc_aot_block_path_pop(&ctx->block_path);

        if (if_stmt->else_count > 0) {
            fprintf(out, "%s} else {\n", block_indent);
            if (tc_aot_block_path_push(
                    &ctx->block_path,
                    (TcBlockId){if_stmt_index, TC_BLOCK_IF_ELSE}) != 0) {
                return -1;
            }
            for (i = 0; i < if_stmt->else_count; i++) {
                if (tc_aot_emit_statement_impl(out, &if_stmt->else_body[i], ctx,
                                               branch_indent) != 0) {
                    return -1;
                }
            }
            tc_aot_block_path_pop(&ctx->block_path);
        }

        fprintf(out, "%s}\n", block_indent);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_LABEL_DEF) {
        int stmt_index_val = tc_stmt_index_take(&ctx->index);

        fprintf(out, "%sif (0) goto tc_label_%d;\n", indent, stmt_index_val);
        fprintf(out, "%stc_label_%d: ;\n", indent, stmt_index_val);
        return 0;
    }

    if (stmt->kind == TC_STMT_GOTO) {
        const TcLabelEntry *entry = NULL;

        tc_stmt_index_take(&ctx->index);
        entry = tc_aot_resolve_goto_label(symbols, stmt->u.goto_stmt.target,
                                          ctx->current_func_id, &ctx->block_path);
        if (!entry) {
            return -1;
        }
        fprintf(out, "%sgoto tc_label_%d;\n", indent, entry->stmt_index);
        return 0;
    }

    if (stmt->kind == TC_STMT_BREAK || stmt->kind == TC_STMT_CONTINUE) {
        const TcLoopControlStmt *loop_control =
            stmt->kind == TC_STMT_BREAK ? &stmt->u.break_stmt : &stmt->u.continue_stmt;

        tc_stmt_index_take(&ctx->index);
        if (ctx->loops.depth == 0 ||
            ctx->loops.loop_ids[ctx->loops.depth - 1] != loop_control->loop_id) {
            return -1;
        }
        fprintf(out, "%s%s;\n", indent,
                stmt->kind == TC_STMT_BREAK ? "break" : "continue");
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNC_DEF || stmt->kind == TC_STMT_STRUCT_DEF ||
        stmt->kind == TC_STMT_IMPORT || stmt->kind == TC_STMT_STATIC_LET_DEF ||
        stmt->kind == TC_STMT_STATIC_VAR_DEF) {
        tc_stmt_index_take(&ctx->index);
        return 0;
    }

    if (stmt->kind == TC_STMT_RETURN) {
        const TcReturnStmt *ret = &stmt->u.return_stmt;
        char abort_indent[64];

        tc_stmt_index_take(&ctx->index);
        if (ctx->current_func_id < 0) {
            return -1;
        }
        if (!ret->has_value) {
            if (ctx->embed_mode) {
                fprintf(out, "%sreturn 0;\n", indent);
            } else {
                fprintf(out, "%sreturn;\n", indent);
            }
            return 0;
        }
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        if (ret->value.kind == TC_OPERAND_LIT) {
            fprintf(out, "%s    tc_aot_ret_%d = ", indent, ctx->current_func_id);
            tc_aot_emit_literal_expr(out, ctx->current_return_type, &ret->value.u.lit);
            fprintf(out, ";\n");
        } else {
            char ret_tmp[32];
            snprintf(ret_tmp, sizeof(ret_tmp), "tc_aot_ret_%d", ctx->current_func_id);
            if (tc_aot_emit_operand_assign(out, &ret->value, ctx->current_return_type, ret_tmp,
                                           abort_indent, ctx, ctx->index.next - 1) != 0) {
                return -1;
            }
        }
        if (ctx->embed_mode) {
            fprintf(out, "%s    return 0;\n", indent);
        } else {
            fprintf(out, "%s    return;\n", indent);
        }
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_FUNCALL) {
        const TcFuncallStmt *call = &stmt->u.funcall_stmt;
        int stmt_index = tc_stmt_index_take(&ctx->index);

        if (call->resolved_func_id < 0) {
            return -1;
        }
        return tc_aot_emit_funcall(out, call->resolved_func_id, call->args, call->arg_count, NULL, 0,
                                   0, indent, ctx, stmt_index, call->line, 0, NULL);
    }

    if (stmt->kind == TC_STMT_PTR_STORE) {
        const TcPtrStoreStmt *store = &stmt->u.ptr_store;
        char abort_indent[64];
        int stmt_index = tc_stmt_index_take(&ctx->index);

        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _ptr, _val;\n", indent);
        if (tc_aot_emit_operand_assign(out, &store->ptr, TC_PTR, "_ptr", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &store->value, store->pointee_type.tag, "_val",
                                       abort_indent, ctx, stmt_index) != 0) {
            return -1;
        }
        fprintf(out, "%s    if (tc_aot_ptr_store(slots, _ptr, _val, %s, tc_aot_cur_diag, %d) != 0)\n",
                abort_indent, tc_aot_type_enum(store->pointee_type.tag), store->line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, store->line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_MEMBLOCK_STORE) {
        const TcMemblockStoreStmt *store = &stmt->u.memblock_store;
        char abort_indent[64];
        int stmt_index = tc_stmt_index_take(&ctx->index);
        int mb_slot = -1;
        const TcType *element = &store->element_type;
        size_t elem_bytes = (tc_sizeof_bits_ex(element, tc_struct_table_width_bits,
                                               ctx->program->struct_table) + 7U) / 8U;

        if (store->binding.resolved && store->binding.slot >= 0) {
            mb_slot = store->binding.slot;
        } else if (tc_aot_resolve_var_slot(symbols, &ctx->sym_index, store->memblock_name,
                                           stmt_index, &mb_slot) != 0) {
            return -1;
        }
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _idx, _val;\n", indent);
        if (tc_aot_emit_operand_assign(out, &store->index, TC_USIZE, "_idx", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &store->value, element->tag, "_val", abort_indent, ctx,
                                       stmt_index) != 0) {
            return -1;
        }
        fprintf(out,
                "%s    if (tc_aot_memblock_store(slots[%d], %zu, _idx, _val, %s, tc_aot_cur_diag, %d) != 0)\n",
                abort_indent, mb_slot, elem_bytes, tc_aot_type_enum(element->tag), store->line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, store->line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_MEMBLOCK_COPY) {
        const TcMemblockCopyStmt *copy = &stmt->u.memblock_copy;
        char abort_indent[64];
        int stmt_index = tc_stmt_index_take(&ctx->index);
        int dst_slot = -1;
        int src_slot = -1;
        const TcType *element = &copy->element_type;
        size_t elem_bytes = (tc_sizeof_bits_ex(element, tc_struct_table_width_bits,
                                               ctx->program->struct_table) + 7U) / 8U;

        if (copy->dst_binding.resolved && copy->dst_binding.slot >= 0 &&
            copy->src_binding.resolved && copy->src_binding.slot >= 0) {
            dst_slot = copy->dst_binding.slot;
            src_slot = copy->src_binding.slot;
        } else if (tc_aot_resolve_var_slot(symbols, &ctx->sym_index, copy->dst_name, stmt_index,
                                           &dst_slot) != 0 ||
                   tc_aot_resolve_var_slot(symbols, &ctx->sym_index, copy->src_name, stmt_index,
                                           &src_slot) != 0) {
            return -1;
        }
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _dst_idx, _src_idx, _len;\n", indent);
        if (tc_aot_emit_operand_assign(out, &copy->dst_index, TC_USIZE, "_dst_idx", abort_indent,
                                       ctx, stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &copy->src_index, TC_USIZE, "_src_idx", abort_indent,
                                       ctx, stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &copy->length, TC_USIZE, "_len", abort_indent, ctx,
                                       stmt_index) != 0) {
            return -1;
        }
        fprintf(out,
                "%s    if (tc_aot_memblock_copy(slots[%d], _dst_idx, slots[%d], _src_idx, _len, "
                "%zu, tc_aot_cur_diag, %d) != 0)\n",
                abort_indent, dst_slot, src_slot, elem_bytes, copy->line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, copy->line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_MEMCOPY_UNSAFE) {
        const TcMemcopyUnsafeStmt *mc = &stmt->u.memcopy_unsafe;
        char abort_indent[64];
        int stmt_index = tc_stmt_index_take(&ctx->index);
        const TcType *element = &mc->element_type;
        size_t elem_bytes = (tc_sizeof_bits_ex(element, tc_struct_table_width_bits,
                                               ctx->program->struct_table) + 7U) / 8U;

        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t _dptr, _sptr, _d_idx, _s_idx;\n", indent);
        fprintf(out, "%s    int64_t _len;\n", indent);
        if (tc_aot_emit_operand_assign(out, &mc->dst_ptr, TC_PTR, "_dptr", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &mc->src_ptr, TC_PTR, "_sptr", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &mc->dst_index, TC_USIZE, "_d_idx", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &mc->src_index, TC_USIZE, "_s_idx", abort_indent, ctx,
                                       stmt_index) != 0 ||
            tc_aot_emit_operand_assign(out, &mc->length, TC_ISIZE, "_len", abort_indent, ctx,
                                       stmt_index) != 0) {
            return -1;
        }
        fprintf(out,
                "%s    if (tc_aot_memcopy_unsafe(slots, _dptr, _d_idx, %s, _sptr, _s_idx, %s, "
                "_len, %zu, %s, tc_aot_cur_diag, %d) != 0)\n",
                abort_indent, tc_aot_type_enum(tc_aot_memcopy_index_type(&mc->dst_index, ctx,
                                                                        stmt_index)),
                tc_aot_type_enum(tc_aot_memcopy_index_type(&mc->src_index, ctx, stmt_index)),
                elem_bytes,
                tc_aot_type_enum(element->tag), mc->line);
        fprintf(out, "%s        tc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, mc->line);
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    if (stmt->kind == TC_STMT_FIELD_ASSIGN) {
        const TcFieldAssign *assign = &stmt->u.field_assign;
        const TcStructTable *table = ctx->program->struct_table;
        const TcSymbol *base_sym = NULL;
        size_t offset = 0;
        const TcType *field_type = NULL;
        size_t nbytes = 0;
        char tmp[48];
        char abort_indent[64];
        int stmt_index = 0;
        TcDiagnostic local_diag;

        stmt_index = tc_stmt_index_take(&ctx->index);
        tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
        base_sym = tc_symbol_table_find_visible(symbols, assign->base, stmt_index, &ctx->sym_index);
        if (!base_sym || base_sym->slot < 0 || tc_type_struct_id(base_sym->type) < 0) {
            return -1;
        }
        tc_diagnostic_init(&local_diag);
        if (tc_struct_path_offset_bytes(table, tc_type_struct_id(base_sym->type), assign->fields,
                                        assign->field_count, &offset, &field_type, &local_diag,
                                        assign->line) != 0) {
            return -1;
        }
        snprintf(tmp, sizeof(tmp), "_tc_fa_%d", stmt_index);
        fprintf(out, "%s{\n", indent);
        fprintf(out, "%s    uint64_t %s;\n", indent, tmp);
        if (tc_aot_emit_rhs(out, &assign->rhs, field_type->tag, tmp, abort_indent, ctx, stmt_index,
                            assign->line) != 0) {
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
            /* struct/memblock 字段按值语义内联拷贝内容（§3.9.3） */
            fprintf(out, "%s    tc_aot_struct_memcpy_field(slots[%d], %zu, %zu, %s);\n", indent,
                    base_sym->slot, offset, nbytes, tmp);
        } else {
            nbytes = (tc_sizeof_bits_ex(field_type, tc_struct_table_width_bits,
                                        ctx->program->struct_table) + 7U) / 8U;
            fprintf(out, "%s    tc_aot_struct_store_bits(slots[%d], %zu, %zu, %s);\n", indent,
                    base_sym->slot, offset, nbytes, tmp);
        }
        fprintf(out, "%s}\n", indent);
        return 0;
    }

    {
        int stmt_index = tc_stmt_index_take(&ctx->index);
        const TcSymbol *symbol = NULL;

        if (stmt->kind == TC_STMT_VAR_DEF) {
            const TcVarDef *var_def = &stmt->u.var_def;
            TcTypeTag expected_type = tc_type_scalar_tag(&var_def->full_type);
            int slot = -1;

            if (var_def->full_type.tag == TC_MEMBLOCK) {
                expected_type = TC_MEMBLOCK;
            } else if (var_def->full_type.tag == TC_PTR) {
                expected_type = TC_PTR;
            } else if (var_def->full_type.tag == TC_STRUCT) {
                expected_type = TC_STRUCT;
            }
            if (var_def->binding.resolved && !var_def->binding.is_const &&
                var_def->binding.slot >= 0) {
                slot = var_def->binding.slot;
            } else {
                symbol = tc_aot_find_def_symbol(symbols, var_def->name, var_def->line);
                if (!symbol) {
                    return -1;
                }
                slot = symbol->slot;
            }
            return tc_aot_emit_rhs_slot(out, &var_def->rhs, expected_type, slot, indent, ctx,
                                        stmt_index, var_def->line);
        }

        if (stmt->kind == TC_STMT_CONST_DEF) {
            return 0;
        }

        if (stmt->kind == TC_STMT_ASSIGN) {
            const TcAssign *assign = &stmt->u.assign;
            int slot = -1;
            TcTypeTag assign_type = TC_INT32;

            if (assign->binding.resolved && assign->binding.slot >= 0) {
                slot = assign->binding.slot;
                assign_type = assign->binding.type->tag;
            } else {
                symbol = tc_symbol_table_find_visible(symbols, assign->name, stmt_index,
                                                        &ctx->sym_index);
                if (!symbol) {
                    return -1;
                }
                slot = symbol->slot;
                assign_type = tc_type_tag_of(symbol->type);
            }
            return tc_aot_emit_rhs_slot(out, &assign->rhs, assign_type, slot, indent, ctx, stmt_index,
                                        assign->line);
        }

        if (stmt->kind == TC_STMT_WRITE || stmt->kind == TC_STMT_WRITELN) {
            const TcIoWrite *io = &stmt->u.io_write;
            int newline = stmt->kind == TC_STMT_WRITELN ? 1 : 0;
            char abort_indent[64];

            tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
            fprintf(out, "%sif (tc_aot_write(%s, (TcFormatFullSpec){%d, %d, %d, %d, %d, %d, %d, %s}, ",
                    indent, tc_aot_type_enum(io->type->tag),
                    io->fmt.flag_minus, io->fmt.flag_plus, io->fmt.flag_hash, io->fmt.flag_zero,
                    io->fmt.width, io->fmt.precision_set, io->fmt.precision,
                    tc_aot_format_enum(io->fmt.spec));
            tc_aot_emit_operand_expr(out, &io->operand, io->type->tag, ctx, stmt_index);
            fprintf(out, ", %d, tc_aot_cur_diag, %d) != 0)\n", newline, io->line);
            fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, io->line);
            return 0;
        }

        if (stmt->kind == TC_STMT_READ) {
            const TcRead *io_read = &stmt->u.io_read;
            char abort_indent[64];
            int slot = -1;

            if (io_read->binding.resolved && !io_read->binding.is_const &&
                io_read->binding.slot >= 0) {
                slot = io_read->binding.slot;
            } else {
                symbol = tc_symbol_table_find_visible(symbols, io_read->name, stmt_index,
                                                      &ctx->sym_index);
                if (!symbol) {
                    return -1;
                }
                slot = symbol->slot;
            }
            tc_aot_sub_indent(abort_indent, sizeof(abort_indent), indent, 1);
            fprintf(out, "%sif (tc_aot_read(%s, &slots[%d], tc_aot_cur_diag, %d) != 0)\n", indent,
                    tc_aot_type_enum(io_read->type->tag), slot, io_read->line);
            fprintf(out, "%stc_aot_abort(tc_aot_cur_diag, %d);\n", abort_indent, io_read->line);
            return 0;
        }
    }

    return -1;
}
