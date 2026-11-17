/* TC 0.0.46 ARM32 per-instruction lowering (design §10.3).
 *
 * Control flow, calls, frame access, static addressing and result placement
 * are emitted natively.  Scalar arithmetic, comparisons, conversions,
 * floating point, memory/object checks, pointer helpers and I/O call the
 * shared runtime core, so the reference VM and the 32-bit backend execute one
 * implementation of the standard's exact rules.  Values are read from and
 * written to stack slots; helpers preserve r4-r11, including the r9 locals
 * base and r10 static base.
 */
#include "backend/arm32/emit_internal.h"

#include <stdlib.h>
#include <string.h>

#include "ir/instr.h"
#include "runtime/trap.h"

static int is_int_opcode(TcOpcode op) {
    switch (op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_NEG:
    case TC_ABS:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
    case TC_NOT:
        return 1;
    default:
        return 0;
    }
}

static int is_compare_opcode(TcOpcode op) {
    switch (op) {
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Scalar helpers.                                                           */
/* ------------------------------------------------------------------------- */

static void emit_int_binop(TcArm32Emitter *emitter, const TcInstr *instr) {
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->mode);
    tc_arm32_arg_immediate(emitter, 2u, (uint32_t)instr->type->tag);
    tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 4u, &instr->args[0].value, 1);
    tc_arm32_arg_operand(emitter, 5u, &instr->args[1].value, 0);
    tc_arm32_arg_operand(emitter, 6u, &instr->args[1].value, 1);
    tc_arm32_arg_immediate(emitter, 7u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 8u, instr->span.column);
    tc_arm32_arg_file(emitter, 9u, &instr->span);
    tc_arm32_emit_call0(emitter, "tc_arm32_int_binop");
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_int_unop(TcArm32Emitter *emitter, const TcInstr *instr) {
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->mode);
    tc_arm32_arg_immediate(emitter, 2u, (uint32_t)instr->type->tag);
    tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 4u, &instr->args[0].value, 1);
    tc_arm32_emit_call0(emitter, "tc_arm32_int_unop");
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_int_compare(TcArm32Emitter *emitter, const TcInstr *instr) {
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_arm32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_arm32_arg_operand(emitter, 4u, &instr->args[1].value, 0);
    tc_arm32_arg_operand(emitter, 5u, &instr->args[1].value, 1);
    tc_arm32_emit_call0(emitter, "tc_arm32_int_compare");
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_float_binop(TcArm32Emitter *emitter, const TcInstr *instr) {
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->mode);
    tc_arm32_arg_immediate(emitter, 2u, (uint32_t)instr->type->tag);
    tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 4u, &instr->args[0].value, 1);
    tc_arm32_arg_operand(emitter, 5u, &instr->args[1].value, 0);
    tc_arm32_arg_operand(emitter, 6u, &instr->args[1].value, 1);
    tc_arm32_arg_immediate(emitter, 7u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 8u, instr->span.column);
    tc_arm32_arg_file(emitter, 9u, &instr->span);
    tc_arm32_emit_call0(emitter, "tc_arm32_float_binop");
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_float_unop(TcArm32Emitter *emitter, const TcInstr *instr) {
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_arm32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_arm32_emit_call0(emitter, "tc_arm32_float_unop");
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_float_compare(TcArm32Emitter *emitter,
                               const TcInstr *instr) {
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_arm32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_arm32_arg_operand(emitter, 4u, &instr->args[1].value, 0);
    tc_arm32_arg_operand(emitter, 5u, &instr->args[1].value, 1);
    tc_arm32_emit_call0(emitter, "tc_arm32_float_compare");
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_cast(TcArm32Emitter *emitter, const TcInstr *instr) {
    const TcType *source =
        tc_arm32_value_type(emitter, &instr->args[0].value);
    if (source == NULL || instr->type == NULL) {
        tc_arm32_error(emitter, "cast without source or destination type");
        return;
    }
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)source->tag);
    tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_arm32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_arm32_arg_immediate(emitter, 4u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 5u, instr->span.column);
    tc_arm32_arg_file(emitter, 6u, &instr->span);
    tc_arm32_emit_call0(emitter, "tc_arm32_cast");
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_trunc_bitcast(TcArm32Emitter *emitter,
                               const TcInstr *instr) {
    const TcType *source =
        tc_arm32_value_type(emitter, &instr->args[0].value);
    if (source == NULL || instr->type == NULL) {
        tc_arm32_error(emitter, "conversion without source or destination");
        return;
    }
    if (instr->op == TC_TRUNC) {
        tc_arm32_arg_immediate(emitter, 0u, (uint32_t)source->tag);
        tc_arm32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
        tc_arm32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
        tc_arm32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
        tc_arm32_emit_call0(emitter, "tc_arm32_trunc");
    } else {
        tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->type->tag);
        tc_arm32_arg_operand(emitter, 1u, &instr->args[0].value, 0);
        tc_arm32_arg_operand(emitter, 2u, &instr->args[0].value, 1);
        tc_arm32_emit_call0(emitter, "tc_arm32_bitcast");
    }
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

/* ------------------------------------------------------------------------- */
/* Memory, pointers and functions.                                          */
/* ------------------------------------------------------------------------- */

static void emit_dst_address(TcArm32Emitter *emitter, const TcInstr *instr,
                             unsigned reg) {
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_arm32_add_constant(emitter, reg, 9u,
                              emitter->scratch_buffer_offset);
        return;
    }
    tc_arm32_operand_address(emitter, &instr->dst, reg);
}

static void define_dst_range(TcArm32Emitter *emitter, const TcInstr *instr,
                             uint64_t size) {
    int tracked = 0;
    if (!emitter->profile->checked_build ||
        instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (instr->dst.kind == TC_OP_VREG &&
        tc_arm32_slot_is_object(&emitter->slots[instr->dst.vreg])) {
        tracked = 1;
    } else if (instr->dst.kind == TC_OP_FIELD &&
               tc_arm32_slot_is_object(&emitter->slots[instr->dst.field.base])) {
        tracked = 1;
    }
    if (!tracked) {
        return;
    }
    tc_arm32_operand_address(emitter, &instr->dst, TC_ARM32_TMP0);
    tc_arm32_define_range(emitter, TC_ARM32_TMP0, size);
}

static void emit_load(TcArm32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = tc_arm32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_arm32_type_id(emitter, instr->type);
    uint32_t flags = instr->mode == TC_VOLATILE ? TC_RT_ACCESS_VOLATILE : 0;
    if (!ok) {
        tc_arm32_error(emitter, "load of an incomplete type");
        return;
    }
    emit_dst_address(emitter, instr, 0u);
    tc_arm32_arg_operand(emitter, 1u, &instr->args[0].value, 0);
    tc_arm32_arg_immediate(emitter, 2u, (uint32_t)size);
    tc_arm32_arg_immediate(emitter, 3u, flags);
    tc_arm32_arg_immediate(emitter, 4u, type_id);
    tc_arm32_arg_immediate(emitter, 5u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 6u, instr->span.column);
    tc_arm32_arg_file(emitter, 7u, &instr->span);
    tc_arm32_emit_call0(emitter, TC_RT_SYM_LOAD);
    if (instr->dst.kind != TC_OP_DISCARD && instr->type->tag == TC_TY_BOOL) {
        emit_dst_address(emitter, instr, TC_ARM32_TMP0);
        tc_arm32_write(emitter, "  ldrb r0, [r%u]\n", TC_ARM32_TMP0);
        tc_arm32_normalize_bool(emitter, 0u);
        tc_arm32_write(emitter, "  strb r0, [r%u]\n", TC_ARM32_TMP0);
    }
    if (instr->dst.kind != TC_OP_DISCARD) {
        define_dst_range(emitter, instr, size);
    }
}

static void emit_store(TcArm32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = tc_arm32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_arm32_type_id(emitter, instr->type);
    uint32_t flags = instr->mode == TC_VOLATILE ? TC_RT_ACCESS_VOLATILE : 0;
    if (!ok) {
        tc_arm32_error(emitter, "store of an incomplete type");
        return;
    }
    if (instr->type->tag == TC_TY_BOOL) {
        tc_arm32_operand_address(emitter, &instr->args[1].value,
                                 TC_ARM32_TMP0);
        tc_arm32_write(emitter, "  ldrb r0, [r%u]\n", TC_ARM32_TMP0);
        tc_arm32_normalize_bool(emitter, 0u);
        tc_arm32_add_constant(emitter, TC_ARM32_TMP0, 9u,
                              emitter->scratch_offset);
        tc_arm32_write(emitter, "  strb r0, [r%u]\n", TC_ARM32_TMP0);
        tc_arm32_arg_reg(emitter, 1u, TC_ARM32_TMP0);
        tc_arm32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    } else {
        tc_arm32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
        tc_arm32_arg_address(emitter, 1u, &instr->args[1].value);
    }
    tc_arm32_arg_immediate(emitter, 2u, (uint32_t)size);
    tc_arm32_arg_immediate(emitter, 3u, flags);
    tc_arm32_arg_immediate(emitter, 4u, type_id);
    tc_arm32_arg_immediate(emitter, 5u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 6u, instr->span.column);
    tc_arm32_arg_file(emitter, 7u, &instr->span);
    tc_arm32_emit_call0(emitter, TC_RT_SYM_STORE);
}

static void emit_memmove(TcArm32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t element = tc_arm32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_arm32_type_id(emitter, instr->type);
    if (!ok) {
        tc_arm32_error(emitter, "memmove of an incomplete type");
        return;
    }
    tc_arm32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 1u, &instr->args[1].value, 0);
    tc_arm32_arg_operand(emitter, 2u, &instr->args[2].value, 0);
    tc_arm32_arg_immediate(emitter, 3u, (uint32_t)element);
    tc_arm32_arg_immediate(emitter, 4u, type_id);
    tc_arm32_arg_immediate(emitter, 5u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 6u, instr->span.column);
    tc_arm32_arg_file(emitter, 7u, &instr->span);
    tc_arm32_emit_call0(emitter, TC_RT_SYM_MEMMOVE);
}

static void emit_addr(TcArm32Emitter *emitter, const TcInstr *instr) {
    tc_arm32_operand_address(emitter, &instr->args[0].value, 0u);
    tc_arm32_move_immediate(emitter, 1u, 0u);
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_ptr_arith(TcArm32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t element = tc_arm32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_arm32_type_id(emitter, instr->type);
    if (!ok) {
        tc_arm32_error(emitter, "pointer arithmetic on an incomplete type");
        return;
    }
    tc_arm32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 1u, &instr->args[1].value, 0);
    tc_arm32_arg_immediate(emitter, 2u, (uint32_t)element);
    tc_arm32_arg_immediate(emitter, 3u, instr->op == TC_PTRADD ? 1u : 0u);
    tc_arm32_arg_immediate(emitter, 4u, type_id);
    tc_arm32_arg_immediate(emitter, 5u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 6u, instr->span.column);
    tc_arm32_arg_file(emitter, 7u, &instr->span);
    tc_arm32_emit_call0(emitter, TC_RT_SYM_PTRADD);
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_ptrdiff(TcArm32Emitter *emitter, const TcInstr *instr) {
    tc_arm32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    tc_arm32_arg_operand(emitter, 1u, &instr->args[1].value, 0);
    tc_arm32_arg_immediate(emitter, 2u, instr->span.line);
    tc_arm32_arg_immediate(emitter, 3u, instr->span.column);
    tc_arm32_arg_file(emitter, 4u, &instr->span);
    tc_arm32_emit_call0(emitter, TC_RT_SYM_PTRDIFF);
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_value_compare(TcArm32Emitter *emitter,
                               const TcInstr *instr) {
    const char *condition;
    unsigned id = tc_arm32_label(emitter);
    tc_arm32_load_operand32(emitter, &instr->args[0].value, 0u);
    tc_arm32_load_operand32(emitter, &instr->args[1].value, 1u);
    switch (instr->op) {
    case TC_PTREQ:
    case TC_FUNCEQ:
        condition = "eq";
        break;
    case TC_PTRNE:
    case TC_FUNCNE:
        condition = "ne";
        break;
    case TC_PTRLT:
        condition = "lo";
        break;
    case TC_PTRLE:
        condition = "ls";
        break;
    case TC_PTRGT:
        condition = "hi";
        break;
    case TC_PTRGE:
        condition = "hs";
        break;
    default:
        tc_arm32_error(emitter, "invalid pointer comparison");
        return;
    }
    tc_arm32_write(emitter, "  movs r2, #0\n  cmp r0, r1\n"
                            "  b%s .Lcmp_true_%u\n"
                            "  b .Lcmp_done_%u\n"
                            ".Lcmp_true_%u:\n  movs r2, #1\n"
                            ".Lcmp_done_%u:\n",
                   condition, id, id, id, id);
    tc_arm32_store_result64(emitter, instr, 2u, 0u);
}

static void emit_funcaddr(TcArm32Emitter *emitter, const TcInstr *instr) {
    uint32_t func_id;
    if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
        tc_arm32_error(emitter, "funcaddr without a function operand");
        return;
    }
    func_id = instr->args[0].value.func;
    if (func_id >= emitter->ir->nfuncs) {
        tc_arm32_error(emitter, "funcaddr of an invalid function");
        return;
    }
    {
        char symbol[64];
        snprintf(symbol, sizeof(symbol), "tc_function_%u", (unsigned)func_id);
        tc_arm32_load_symbol(emitter, 0u, symbol);
    }
    tc_arm32_move_immediate(emitter, 1u, 0u);
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

/* ------------------------------------------------------------------------- */
/* Calls.                                                                    */
/* ------------------------------------------------------------------------- */

static void emit_depth_enter(TcArm32Emitter *emitter, const TcSpan *span) {
    unsigned id = tc_arm32_label(emitter);
    tc_arm32_load_symbol(emitter, TC_ARM32_TMP0, TC_RT_SYM_CALL_DEPTH);
    tc_arm32_write(emitter, "  ldr r%u, [r%u]\n", TC_ARM32_VALUE0,
                   TC_ARM32_TMP0);
    tc_arm32_move_immediate(emitter, TC_ARM32_VALUE1,
                            emitter->profile->depth_D);
    tc_arm32_write(emitter, "  cmp r%u, r%u\n  blo .Ldepth_ok_%u\n",
                   TC_ARM32_VALUE0, TC_ARM32_VALUE1, id);
    tc_arm32_trap(emitter, span, TC_EK_STACK_OVERFLOW);
    tc_arm32_write(emitter,
                   ".Ldepth_ok_%u:\n  add r%u, r%u, #1\n"
                   "  str r%u, [r%u]\n",
                   id, TC_ARM32_VALUE0, TC_ARM32_VALUE0, TC_ARM32_VALUE0,
                   TC_ARM32_TMP0);
}

static void emit_depth_leave(TcArm32Emitter *emitter) {
    tc_arm32_load_symbol(emitter, TC_ARM32_TMP0, TC_RT_SYM_CALL_DEPTH);
    tc_arm32_write(emitter,
                   "  ldr r%u, [r%u]\n"
                   "  sub r%u, r%u, #1\n  str r%u, [r%u]\n",
                   TC_ARM32_VALUE0, TC_ARM32_TMP0, TC_ARM32_VALUE0,
                   TC_ARM32_VALUE0, TC_ARM32_VALUE0, TC_ARM32_TMP0);
}

static void store_call_result(TcArm32Emitter *emitter, const TcInstr *instr,
                              const TcType *ret, TcArm32RetPlan *plan) {
    int ok = 0;
    uint64_t size;
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        return;
    }
    size = tc_arm32_type_size(emitter->ir, ret, &ok);
    if (!ok) {
        return;
    }
    if (plan->kind == TC_ARM32_RET_INDIRECT) {
        return; /* the callee wrote into the provided buffer */
    }
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (ret->tag == TC_TY_STRUCT) {
        tc_arm32_operand_address(emitter, &instr->dst, TC_ARM32_TMP0);
        tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 0,
                            (unsigned)size, 0u);
        return;
    }
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_call(TcArm32Emitter *emitter, const TcInstr *instr) {
    const TcType **params = NULL;
    size_t nparams = 0;
    const TcType *ret = NULL;
    TcArm32ArgPlan plan;
    TcArm32RetPlan ret_plan;
    size_t i;
    int indirect = instr->op == TC_CALL_INDIRECT;
    uint32_t func_id = 0;
    tc_arm32_function_signature(emitter, instr, &params, &nparams, &ret);
    if (emitter->failed) {
        free(params);
        return;
    }
    if (indirect) {
        /* args[0] is the function pointer; parameters start at args[1]. */
        if (instr->nargs - 1u != nparams) {
            tc_arm32_error(emitter, "call_indirect argument count mismatch");
            free(params);
            return;
        }
    } else {
        if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
            tc_arm32_error(emitter, "direct call without a function operand");
            free(params);
            return;
        }
        func_id = instr->args[0].value.func;
        if (instr->nargs - 1u != nparams) {
            tc_arm32_error(emitter, "call argument count mismatch");
            free(params);
            return;
        }
    }
    tc_arm32_plan_return(emitter, ret, &ret_plan);
    tc_arm32_plan_arguments(
        emitter, params, nparams,
        ret_plan.kind == TC_ARM32_RET_INDIRECT ? 1 : 0, &plan);
    if (emitter->failed) {
        free(params);
        tc_arm32_free_argument_plan(&plan);
        return;
    }
    /* Stack-resident portions first: memcpy clobbers r0-r3, so no register
     * argument may be live yet.  Composite arguments are marshalled as memory
     * words (AAPCS32 Rules C.4/C.5). */
    for (i = 0; i < nparams; i++) {
        const TcArm32ArgSlot *loc = &plan.slots[i];
        const TcArg *arg = &instr->args[i + 1u];
        if (loc->struct_value && loc->stack_words != 0) {
            uint64_t bytes = loc->size - loc->stack_byte_offset;
            tc_arm32_operand_address(emitter, &arg->value, TC_ARM32_VALUE0);
            if (loc->stack_byte_offset != 0) {
                tc_arm32_add_constant(emitter, TC_ARM32_VALUE0,
                                      TC_ARM32_VALUE0,
                                      loc->stack_byte_offset);
            }
            tc_arm32_add_constant(emitter, 0u, 13u, loc->stack_offset);
            tc_arm32_write(emitter, "  mov r1, r%u\n", TC_ARM32_VALUE0);
            tc_arm32_move_immediate(emitter, 2u, (uint32_t)bytes);
            tc_arm32_write(emitter, "  bl memcpy\n");
            continue;
        }
        if (loc->kind != TC_ARM32_ARG_STACK) {
            continue;
        }
        tc_arm32_load_operand64(emitter, &arg->value, TC_ARM32_VALUE0,
                                TC_ARM32_VALUE1);
        if (loc->stack_offset <= 4095u) {
            tc_arm32_write(emitter, "  str r%u, [sp, #%llu]\n",
                           TC_ARM32_VALUE0,
                           (unsigned long long)loc->stack_offset);
            if (loc->is64) {
                tc_arm32_write(emitter, "  str r%u, [sp, #%llu]\n",
                               TC_ARM32_VALUE1,
                               (unsigned long long)(loc->stack_offset + 4u));
            }
        } else {
            tc_arm32_add_constant(emitter, TC_ARM32_TMP0, 13u,
                                  loc->stack_offset);
            tc_arm32_write(emitter, "  str r%u, [r%u]\n", TC_ARM32_VALUE0,
                           TC_ARM32_TMP0);
            if (loc->is64) {
                tc_arm32_write(emitter, "  str r%u, [r%u, #4]\n",
                               TC_ARM32_VALUE1, TC_ARM32_TMP0);
            }
        }
    }
    /* Hidden indirect result pointer occupies r0. */
    if (ret_plan.kind == TC_ARM32_RET_INDIRECT) {
        if (instr->dst.kind == TC_OP_DISCARD) {
            tc_arm32_add_constant(emitter, 0u, 9u,
                                  emitter->scratch_buffer_offset);
        } else {
            tc_arm32_operand_address(emitter, &instr->dst, 0u);
        }
    }
    /* Register arguments, including the register words of split composites. */
    for (i = 0; i < nparams; i++) {
        const TcArm32ArgSlot *loc = &plan.slots[i];
        const TcArg *arg = &instr->args[i + 1u];
        if (loc->struct_value) {
            unsigned k;
            for (k = 0; k < loc->reg_words; k++) {
                uint64_t offset = (uint64_t)k * 4u;
                uint64_t remaining = loc->size - offset;
                unsigned bytes = remaining >= 4u ? 4u
                                                 : (unsigned)remaining;
                /* load_bits uses the scratch register, so recompute the
                 * composite address for each word. */
                tc_arm32_operand_address(emitter, &arg->value,
                                         TC_ARM32_TMP0);
                if (offset != 0) {
                    tc_arm32_add_constant(emitter, TC_ARM32_TMP1,
                                          TC_ARM32_TMP0, offset);
                } else {
                    tc_arm32_write(emitter, "  mov r%u, r%u\n",
                                   TC_ARM32_TMP1, TC_ARM32_TMP0);
                }
                tc_arm32_load_bits(emitter, TC_ARM32_TMP1, 0, bytes,
                                   loc->reg + k);
            }
            continue;
        }
        if (loc->kind != TC_ARM32_ARG_REG) {
            continue;
        }
        tc_arm32_load_operand64(emitter, &arg->value, loc->reg,
                                loc->is64 ? loc->reg + 1u
                                          : TC_ARM32_VALUE1);
    }
    emit_depth_enter(emitter, &instr->span);
    if (indirect) {
        unsigned id = tc_arm32_label(emitter);
        tc_arm32_load_operand32(emitter, &instr->args[0].value,
                                TC_ARM32_TMP1);
        tc_arm32_write(emitter, "  cmp r%u, #0\n"
                                "  bne .Lfp_ok_%u\n",
                       TC_ARM32_TMP1, id);
        tc_arm32_trap(emitter, &instr->span, TC_EK_NULL_FUNCTION_POINTER);
        tc_arm32_write(emitter, ".Lfp_ok_%u:\n"
                                "  orr r%u, r%u, #1\n"
                                "  blx r%u\n",
                       id, TC_ARM32_TMP1, TC_ARM32_TMP1, TC_ARM32_TMP1);
    } else {
        tc_arm32_write(emitter, "  bl tc_function_%u\n", (unsigned)func_id);
    }
    emit_depth_leave(emitter);
    store_call_result(emitter, instr, ret, &ret_plan);
    free(params);
    tc_arm32_free_argument_plan(&plan);
}

/* ------------------------------------------------------------------------- */
/* Aggregates.                                                               */
/* ------------------------------------------------------------------------- */

static const TcField *make_field_for_arg(TcArm32Emitter *emitter,
                                         const TcStructDef *def,
                                         const TcArg *arg, size_t index) {
    if (arg->name == NULL) {
        if (index >= def->nfields) {
            return NULL;
        }
        return &def->fields[index];
    }
    {
        size_t i;
        for (i = 0; i < def->nfields; i++) {
            if (def->fields[i].name != NULL &&
                strcmp(def->fields[i].name, arg->name) == 0) {
                return &def->fields[i];
            }
        }
    }
    (void)emitter;
    return NULL;
}

static void emit_make(TcArm32Emitter *emitter, const TcInstr *instr) {
    const TcStructDef *def = tc_arm32_struct_def(emitter->ir, instr->type);
    int ok = 0;
    uint64_t size = tc_arm32_type_size(emitter->ir, instr->type, &ok);
    size_t i;
    if (def == NULL || !ok) {
        tc_arm32_error(emitter, "make of an incomplete struct type");
        return;
    }
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_arm32_add_constant(emitter, TC_ARM32_VALUE0, 9u,
                              emitter->scratch_buffer_offset);
    } else {
        tc_arm32_operand_address(emitter, &instr->dst, TC_ARM32_VALUE0);
    }
    tc_arm32_write(emitter, "  mov r0, r%u\n  movs r1, #0\n",
                   TC_ARM32_VALUE0);
    tc_arm32_move_immediate(emitter, 2u, (uint32_t)size);
    tc_arm32_write(emitter, "  bl memset\n");
    for (i = 0; i < instr->nargs; i++) {
        const TcArg *arg = &instr->args[i];
        const TcField *field = make_field_for_arg(emitter, def, arg, i);
        const TcType *field_type;
        unsigned width;
        int field_ok = 0;
        uint64_t field_size;
        if (field == NULL) {
            tc_arm32_error(emitter, "make field does not match the struct");
            return;
        }
        field_type = field->type;
        width = tc_arm32_type_width(field_type);
        field_size = tc_arm32_type_size(emitter->ir, field_type, &field_ok);
        if (!field_ok) {
            tc_arm32_error(emitter, "make field has an incomplete type");
            return;
        }
        if (field_type->tag == TC_TY_STRUCT) {
            if (arg->value.kind == TC_OP_CONST) {
                tc_arm32_error(emitter, "aggregate make field is a literal");
                return;
            }
            tc_arm32_operand_address(emitter, &arg->value, TC_ARM32_TMP0);
            tc_arm32_add_constant(emitter, 0u, TC_ARM32_VALUE0,
                                  field->offset);
            tc_arm32_write(emitter, "  mov r1, r%u\n", TC_ARM32_TMP0);
            tc_arm32_move_immediate(emitter, 2u, (uint32_t)field_size);
            tc_arm32_write(emitter, "  bl memcpy\n");
        } else {
            tc_arm32_load_operand64(emitter, &arg->value, 0u, 1u);
            if (field_type->tag == TC_TY_BOOL) {
                tc_arm32_normalize_bool(emitter, 0u);
            } else if (width < 32u) {
                tc_arm32_mask_register(emitter, 0u, field_type);
            }
            tc_arm32_add_constant(emitter, TC_ARM32_TMP0, TC_ARM32_VALUE0,
                                  field->offset);
            if (width > 32u) {
                tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 0, 4u, 0u);
                tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 4, 4u, 1u);
            } else {
                tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 0,
                                    width >= 32u ? 4u : width / 8u, 0u);
            }
        }
    }
    if (emitter->profile->checked_build &&
        instr->dst.kind != TC_OP_DISCARD) {
        define_dst_range(emitter, instr, size);
    }
}

static void emit_copy(TcArm32Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int ok = 0;
    uint64_t size;
    if (type == NULL) {
        tc_arm32_error(emitter, "copy without a type");
        return;
    }
    size = tc_arm32_type_size(emitter->ir, type, &ok);
    if (!ok) {
        tc_arm32_error(emitter, "copy of an incomplete type");
        return;
    }
    if (type->tag == TC_TY_STRUCT) {
        tc_arm32_operand_address(emitter, &instr->args[0].value,
                                 TC_ARM32_TMP0);
        emit_dst_address(emitter, instr, 0u);
        tc_arm32_write(emitter, "  mov r1, r%u\n", TC_ARM32_TMP0);
        tc_arm32_move_immediate(emitter, 2u, (uint32_t)size);
        tc_arm32_write(emitter, "  bl memcpy\n");
        if (emitter->profile->checked_build) {
            define_dst_range(emitter, instr, size);
        }
        return;
    }
    tc_arm32_load_operand64(emitter, &instr->args[0].value, 0u, 1u);
    if (type->tag == TC_TY_BOOL) {
        tc_arm32_normalize_bool(emitter, 0u);
    } else if (tc_arm32_type_width(type) < 32u) {
        tc_arm32_mask_register(emitter, 0u, type);
    }
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

/* ------------------------------------------------------------------------- */
/* Control flow, return and I/O.                                             */
/* ------------------------------------------------------------------------- */

static void emit_branch(TcArm32Emitter *emitter, const TcInstr *instr) {
    if (instr->args[0].value.kind != TC_OP_LABEL ||
        instr->args[0].value.label >= emitter->unit->nblocks) {
        tc_arm32_error(emitter, "invalid branch label");
        return;
    }
    tc_arm32_write(emitter, "  b .L%zu_block%u\n", emitter->unit_index,
                   instr->args[0].value.label);
}

static void emit_branch_if(TcArm32Emitter *emitter, const TcInstr *instr) {
    if (instr->args[1].value.kind != TC_OP_LABEL ||
        instr->args[1].value.label >= emitter->unit->nblocks) {
        tc_arm32_error(emitter, "invalid conditional branch label");
        return;
    }
    tc_arm32_load_operand32(emitter, &instr->args[0].value, 0u);
    tc_arm32_write(emitter, "  cmp r0, #0\n  bne .L%zu_block%u\n",
                   emitter->unit_index, instr->args[1].value.label);
}

static void emit_switch(TcArm32Emitter *emitter, const TcInstr *instr) {
    const TcType *type =
        tc_arm32_value_type(emitter, &instr->args[0].value);
    size_t ntargets = instr->ntargets;
    size_t i;
    unsigned id = tc_arm32_label(emitter);
    unsigned width = tc_arm32_type_width(type);
    size_t ndispatch;
    if (!tc_type_is_integer(type) || ntargets == 0) {
        tc_arm32_error(emitter,
                       "switch requires an integer selector and default");
        return;
    }
    ndispatch = ntargets - 1u;
    tc_arm32_load_operand64(emitter, &instr->args[0].value, 0u, 1u);
    if (width > 32u) {
        tc_arm32_write(emitter, "  cmp r1, #0\n"
                                "  bne .Lswitch_default_%u\n",
                       id);
    }
    if (ndispatch == 0) {
        tc_arm32_write(emitter, "  b .Lswitch_default_%u\n", id);
    } else {
        tc_arm32_move_immediate(emitter, TC_ARM32_TMP0,
                                (uint32_t)ndispatch);
        tc_arm32_write(emitter, "  cmp r0, r%u\n"
                                "  bhs .Lswitch_default_%u\n",
                       TC_ARM32_TMP0, id);
        for (i = 0; i < ndispatch; i++) {
            if (i <= 255u) {
                tc_arm32_write(emitter, "  cmp r0, #%zu\n", i);
            } else {
                tc_arm32_move_immediate(emitter, TC_ARM32_TMP0,
                                        (uint32_t)i);
                tc_arm32_write(emitter, "  cmp r0, r%u\n", TC_ARM32_TMP0);
            }
            tc_arm32_write(emitter, "  beq .Lswitch_%u_case_%zu\n", id, i);
        }
        tc_arm32_write(emitter, "  b .Lswitch_default_%u\n", id);
    }
    tc_arm32_write(emitter, ".Lswitch_default_%u:\n  b .L%zu_block%u\n", id,
                   emitter->unit_index, instr->switch_targets[ntargets - 1u]);
    for (i = 0; i < ndispatch; i++) {
        tc_arm32_write(emitter,
                       ".Lswitch_%u_case_%zu:\n  b .L%zu_block%u\n", id, i,
                       emitter->unit_index, instr->switch_targets[i]);
    }
}

static void emit_return(TcArm32Emitter *emitter, const TcInstr *instr) {
    const TcType *ret = NULL;
    TcArm32RetPlan plan;
    if (emitter->unit->func_id != TC_UNIT_TOP_LEVEL &&
        emitter->unit->func_id < emitter->ir->nfuncs) {
        ret = emitter->ir->funcs[emitter->unit->func_id].sig.ret;
    }
    if (instr->nargs == 0) {
        tc_arm32_write(emitter, "  b .Lreturn_%zu\n", emitter->unit_index);
        return;
    }
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        tc_arm32_error(emitter, "return value in a void unit");
        return;
    }
    tc_arm32_plan_return(emitter, ret, &plan);
    {
        const TcOperand *value = &instr->args[0].value;
        int ok = 0;
        uint64_t size = tc_arm32_type_size(emitter->ir, ret, &ok);
        if (!ok) {
            tc_arm32_error(emitter, "return of an incomplete type");
            return;
        }
        if (plan.kind == TC_ARM32_RET_WORD && ret->tag != TC_TY_STRUCT) {
            tc_arm32_load_operand64(emitter, value, 0u, 1u);
            if (ret->tag == TC_TY_BOOL) {
                tc_arm32_normalize_bool(emitter, 0u);
            } else if (tc_arm32_type_width(ret) < 32u) {
                tc_arm32_mask_register(emitter, 0u, ret);
            }
        } else if (plan.kind == TC_ARM32_RET_PAIR) {
            tc_arm32_load_operand64(emitter, value, 0u, 1u);
        } else if (plan.kind == TC_ARM32_RET_WORD) {
            tc_arm32_operand_address(emitter, value, TC_ARM32_TMP0);
            tc_arm32_load_bits(emitter, TC_ARM32_TMP0, 0,
                               (unsigned)size, 0u);
        } else {
            /* Indirect result: copy through the saved hidden pointer and
             * return that pointer in r0. */
            tc_arm32_add_constant(emitter, 0u, 9u, emitter->sret_offset);
            tc_arm32_write(emitter, "  ldr r%u, [r0]\n", TC_ARM32_VALUE0);
            tc_arm32_operand_address(emitter, value, TC_ARM32_TMP0);
            tc_arm32_write(emitter, "  mov r0, r%u\n  mov r1, r%u\n",
                           TC_ARM32_VALUE0, TC_ARM32_TMP0);
            tc_arm32_move_immediate(emitter, 2u, (uint32_t)size);
            tc_arm32_write(emitter, "  bl memcpy\n"
                                    "  mov r0, r%u\n",
                           TC_ARM32_VALUE0);
        }
    }
    tc_arm32_write(emitter, "  b .Lreturn_%zu\n", emitter->unit_index);
}

static void emit_read(TcArm32Emitter *emitter, const TcInstr *instr) {
    unsigned id = tc_arm32_label(emitter);
    unsigned width = tc_arm32_type_width(instr->type);
    int ok = 0;
    uint64_t size = tc_arm32_type_size(emitter->ir, instr->type, &ok);
    if (!ok) {
        tc_arm32_error(emitter, "read of an incomplete type");
        return;
    }
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->type->tag);
    tc_arm32_add_constant(emitter, 1u, 9u, emitter->read_scratch_offset);
    tc_arm32_emit_call0(emitter, TC_RT_SYM_READ);
    tc_arm32_write(emitter, "  cmp r0, #0\n  beq .Lio_ok_%u\n", id);
    tc_arm32_trap(emitter, &instr->span, TC_EK_IO);
    tc_arm32_write(emitter, ".Lio_ok_%u:\n", id);
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (width > 32u) {
        tc_arm32_load_bits(emitter, 9u, emitter->read_scratch_offset, 4u, 0u);
        tc_arm32_load_bits(emitter, 9u, emitter->read_scratch_offset + 4u,
                           4u, 1u);
    } else {
        tc_arm32_load_bits(emitter, 9u, emitter->read_scratch_offset,
                           (unsigned)size, 0u);
        tc_arm32_write(emitter, "  movs r1, #0\n");
    }
    tc_arm32_store_result64(emitter, instr, 0u, 1u);
}

static void emit_write(TcArm32Emitter *emitter, const TcInstr *instr) {
    unsigned id = tc_arm32_label(emitter);
    tc_arm32_arg_immediate(emitter, 0u, (uint32_t)instr->type->tag);
    tc_arm32_arg_immediate(emitter, 1u,
                           instr->op == TC_WRITELN ? 1u : 0u);
    tc_arm32_load_operand64(emitter, &instr->args[0].value, 2u, 3u);
    tc_arm32_emit_call0(emitter, TC_RT_SYM_WRITE);
    tc_arm32_write(emitter, "  cmp r0, #0\n  beq .Lio_ok_%u\n", id);
    tc_arm32_trap(emitter, &instr->span, TC_EK_IO);
    tc_arm32_write(emitter, ".Lio_ok_%u:\n", id);
}

/* ------------------------------------------------------------------------- */
/* Dispatch.                                                                 */
/* ------------------------------------------------------------------------- */

void tc_arm32_emit_instruction(TcArm32Emitter *emitter,
                               const TcInstr *instr) {
    const TcType *type;
    switch (instr->op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
        type = instr->type;
        if (type == NULL) {
            tc_arm32_error(emitter, "arithmetic without a type");
            break;
        }
        if (tc_type_is_float(type)) {
            emit_float_binop(emitter, instr);
        } else {
            emit_int_binop(emitter, instr);
        }
        break;
    case TC_NEG:
    case TC_ABS:
        type = instr->type;
        if (type == NULL) {
            tc_arm32_error(emitter, "unary arithmetic without a type");
            break;
        }
        if (tc_type_is_float(type)) {
            emit_float_unop(emitter, instr);
        } else {
            emit_int_unop(emitter, instr);
        }
        break;
    case TC_NOT:
        emit_int_unop(emitter, instr);
        break;
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        type = instr->type;
        if (type == NULL) {
            tc_arm32_error(emitter, "comparison without a type");
            break;
        }
        if (tc_type_is_float(type)) {
            emit_float_compare(emitter, instr);
        } else {
            emit_int_compare(emitter, instr);
        }
        break;
    case TC_CAST:
        emit_cast(emitter, instr);
        break;
    case TC_TRUNC:
    case TC_BITCAST:
        emit_trunc_bitcast(emitter, instr);
        break;
    case TC_LOAD:
        emit_load(emitter, instr);
        break;
    case TC_STORE:
        emit_store(emitter, instr);
        break;
    case TC_MEMMOVE:
        emit_memmove(emitter, instr);
        break;
    case TC_ADDR:
        emit_addr(emitter, instr);
        break;
    case TC_PTRADD:
    case TC_PTRSUB:
        emit_ptr_arith(emitter, instr);
        break;
    case TC_PTRDIFF:
        emit_ptrdiff(emitter, instr);
        break;
    case TC_PTREQ:
    case TC_PTRNE:
    case TC_PTRLT:
    case TC_PTRLE:
    case TC_PTRGT:
    case TC_PTRGE:
    case TC_FUNCEQ:
    case TC_FUNCNE:
        emit_value_compare(emitter, instr);
        break;
    case TC_SIZEOF: {
        int ok = 0;
        uint64_t size = tc_arm32_type_size(emitter->ir, instr->type, &ok);
        if (!ok) {
            tc_arm32_error(emitter, "sizeof of an incomplete type");
            break;
        }
        tc_arm32_move_immediate(emitter, 0u, (uint32_t)size);
        tc_arm32_move_immediate(emitter, 1u, 0u);
        tc_arm32_store_result64(emitter, instr, 0u, 1u);
        break;
    }
    case TC_FUNCADDR:
        emit_funcaddr(emitter, instr);
        break;
    case TC_CALL:
    case TC_CALL_INDIRECT:
        emit_call(emitter, instr);
        break;
    case TC_MAKE:
        emit_make(emitter, instr);
        break;
    case TC_COPY:
        emit_copy(emitter, instr);
        break;
    case TC_BR:
        emit_branch(emitter, instr);
        break;
    case TC_BR_IF:
        emit_branch_if(emitter, instr);
        break;
    case TC_SWITCH:
        emit_switch(emitter, instr);
        break;
    case TC_RETURN:
        emit_return(emitter, instr);
        break;
    case TC_TRAP:
        tc_arm32_trap(emitter, &instr->span, TC_EK_TRAP);
        break;
    case TC_READ:
        emit_read(emitter, instr);
        break;
    case TC_WRITE:
    case TC_WRITELN:
        emit_write(emitter, instr);
        break;
    default:
        tc_arm32_error(emitter, "unsupported opcode %d", (int)instr->op);
        break;
    }
    (void)is_int_opcode;
    (void)is_compare_opcode;
}
