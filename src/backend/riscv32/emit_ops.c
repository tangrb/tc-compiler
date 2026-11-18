/* TC 0.0.46 RISC-V 32 per-instruction lowering (design §10.3).
 *
 * Control flow, calls, frame access, static addressing and result placement
 * are emitted natively.  Scalar arithmetic, comparisons, conversions,
 * floating point, memory/object checks, pointer helpers and I/O call the
 * shared runtime core, so the reference VM and the native target execute one
 * implementation of the standard's exact rules.  Values are read from and
 * written to stack slots; helpers preserve s0-s11, including the s0 locals
 * base and the s1 static base.
 */
#include "backend/riscv32/emit_internal.h"

#include <stdlib.h>
#include <string.h>

#include "ir/instr.h"
#include "runtime/trap.h"

static int is_signed_narrow(const TcType *type) {
    return type != NULL && tc_type_is_signed_integer(type) &&
           tc_riscv32_type_width(type) < 32u;
}

static void store_word_sp(TcRiscv32Emitter *emitter, uint64_t offset,
                          unsigned reg) {
    if (offset <= 2047u) {
        tc_riscv32_write(emitter, "  sw %s, %llu(sp)\n", tc_riscv32_reg_name(reg),
                         (unsigned long long)offset);
        return;
    }
    {
        unsigned scratch = reg == TC_RISCV32_TMP0 ? TC_RISCV32_TMP1
                                                  : TC_RISCV32_TMP0;
        tc_riscv32_add_constant(emitter, scratch, TC_RISCV32_SP, offset);
        tc_riscv32_write(emitter, "  sw %s, 0(%s)\n", tc_riscv32_reg_name(reg),
                         tc_riscv32_reg_name(scratch));
    }
}

/* ------------------------------------------------------------------------- */
/* Scalar helpers.                                                           */
/* ------------------------------------------------------------------------- */

static void emit_int_binop(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->mode);
    tc_riscv32_arg_immediate(emitter, 2u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 4u, &instr->args[0].value, 1);
    tc_riscv32_arg_operand(emitter, 5u, &instr->args[1].value, 0);
    tc_riscv32_arg_operand(emitter, 6u, &instr->args[1].value, 1);
    tc_riscv32_arg_immediate(emitter, 7u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 8u, instr->span.column);
    tc_riscv32_arg_file(emitter, 9u, &instr->span);
    tc_riscv32_emit_call0(emitter, "tc_riscv32_int_binop");
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_int_unop(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->mode);
    tc_riscv32_arg_immediate(emitter, 2u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 4u, &instr->args[0].value, 1);
    tc_riscv32_emit_call0(emitter, "tc_riscv32_int_unop");
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_int_compare(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_riscv32_arg_operand(emitter, 4u, &instr->args[1].value, 0);
    tc_riscv32_arg_operand(emitter, 5u, &instr->args[1].value, 1);
    tc_riscv32_emit_call0(emitter, "tc_riscv32_int_compare");
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_float_binop(TcRiscv32Emitter *emitter,
                             const TcInstr *instr) {
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->mode);
    tc_riscv32_arg_immediate(emitter, 2u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 4u, &instr->args[0].value, 1);
    tc_riscv32_arg_operand(emitter, 5u, &instr->args[1].value, 0);
    tc_riscv32_arg_operand(emitter, 6u, &instr->args[1].value, 1);
    tc_riscv32_arg_immediate(emitter, 7u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 8u, instr->span.column);
    tc_riscv32_arg_file(emitter, 9u, &instr->span);
    tc_riscv32_emit_call0(emitter, "tc_riscv32_float_binop");
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_float_unop(TcRiscv32Emitter *emitter,
                            const TcInstr *instr) {
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_riscv32_emit_call0(emitter, "tc_riscv32_float_unop");
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_float_compare(TcRiscv32Emitter *emitter,
                               const TcInstr *instr) {
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->op);
    tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_riscv32_arg_operand(emitter, 4u, &instr->args[1].value, 0);
    tc_riscv32_arg_operand(emitter, 5u, &instr->args[1].value, 1);
    tc_riscv32_emit_call0(emitter, "tc_riscv32_float_compare");
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_cast(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    const TcType *source =
        tc_riscv32_value_type(emitter, &instr->args[0].value);
    if (source == NULL || instr->type == NULL) {
        tc_riscv32_error(emitter,
                         "cast without source or destination type");
        return;
    }
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)source->tag);
    tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
    tc_riscv32_arg_immediate(emitter, 4u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 5u, instr->span.column);
    tc_riscv32_arg_file(emitter, 6u, &instr->span);
    tc_riscv32_emit_call0(emitter, "tc_riscv32_cast");
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_trunc_bitcast(TcRiscv32Emitter *emitter,
                               const TcInstr *instr) {
    const TcType *source =
        tc_riscv32_value_type(emitter, &instr->args[0].value);
    if (source == NULL || instr->type == NULL) {
        tc_riscv32_error(emitter,
                         "conversion without source or destination");
        return;
    }
    if (instr->op == TC_TRUNC) {
        tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)source->tag);
        tc_riscv32_arg_immediate(emitter, 1u, (uint32_t)instr->type->tag);
        tc_riscv32_arg_operand(emitter, 2u, &instr->args[0].value, 0);
        tc_riscv32_arg_operand(emitter, 3u, &instr->args[0].value, 1);
        tc_riscv32_emit_call0(emitter, "tc_riscv32_trunc");
    } else {
        tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->type->tag);
        tc_riscv32_arg_operand(emitter, 1u, &instr->args[0].value, 0);
        tc_riscv32_arg_operand(emitter, 2u, &instr->args[0].value, 1);
        tc_riscv32_emit_call0(emitter, "tc_riscv32_bitcast");
    }
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

/* ------------------------------------------------------------------------- */
/* Memory, pointers and functions.                                          */
/* ------------------------------------------------------------------------- */

static void emit_dst_address(TcRiscv32Emitter *emitter,
                             const TcInstr *instr, unsigned reg) {
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_riscv32_add_constant(emitter, reg, TC_RISCV32_LOCALS,
                                emitter->scratch_buffer_offset);
        return;
    }
    tc_riscv32_operand_address(emitter, &instr->dst, reg);
}

static void define_dst_range(TcRiscv32Emitter *emitter,
                             const TcInstr *instr, uint64_t size) {
    int tracked = 0;
    if (!emitter->profile->checked_build ||
        instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (instr->dst.kind == TC_OP_VREG &&
        tc_riscv32_slot_is_object(&emitter->slots[instr->dst.vreg])) {
        tracked = 1;
    } else if (instr->dst.kind == TC_OP_FIELD &&
               tc_riscv32_slot_is_object(
                   &emitter->slots[instr->dst.field.base])) {
        tracked = 1;
    }
    if (!tracked) {
        return;
    }
    tc_riscv32_operand_address(emitter, &instr->dst, TC_RISCV32_TMP0);
    tc_riscv32_define_range(emitter, TC_RISCV32_TMP0, size);
}

static void emit_load(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = tc_riscv32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_riscv32_type_id(emitter, instr->type);
    uint32_t flags = instr->mode == TC_VOLATILE ? TC_RT_ACCESS_VOLATILE : 0;
    if (!ok) {
        tc_riscv32_error(emitter, "load of an incomplete type");
        return;
    }
    emit_dst_address(emitter, instr, 10u);
    tc_riscv32_arg_operand(emitter, 1u, &instr->args[0].value, 0);
    tc_riscv32_arg_immediate(emitter, 2u, (uint32_t)size);
    tc_riscv32_arg_immediate(emitter, 3u, flags);
    tc_riscv32_arg_immediate(emitter, 4u, type_id);
    tc_riscv32_arg_immediate(emitter, 5u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 6u, instr->span.column);
    tc_riscv32_arg_file(emitter, 7u, &instr->span);
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_LOAD);
    if (instr->dst.kind != TC_OP_DISCARD && instr->type->tag == TC_TY_BOOL) {
        emit_dst_address(emitter, instr, TC_RISCV32_TMP0);
        tc_riscv32_write(emitter, "  lbu a0, 0(%s)\n",
                         tc_riscv32_reg_name(TC_RISCV32_TMP0));
        tc_riscv32_normalize_bool(emitter, 10u);
        tc_riscv32_write(emitter, "  sb a0, 0(%s)\n",
                         tc_riscv32_reg_name(TC_RISCV32_TMP0));
    }
    if (instr->dst.kind != TC_OP_DISCARD) {
        define_dst_range(emitter, instr, size);
    }
}

static void emit_store(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = tc_riscv32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_riscv32_type_id(emitter, instr->type);
    uint32_t flags = instr->mode == TC_VOLATILE ? TC_RT_ACCESS_VOLATILE : 0;
    if (!ok) {
        tc_riscv32_error(emitter, "store of an incomplete type");
        return;
    }
    if (instr->type->tag == TC_TY_BOOL) {
        tc_riscv32_operand_address(emitter, &instr->args[1].value,
                                   TC_RISCV32_TMP0);
        tc_riscv32_write(emitter, "  lbu a0, 0(%s)\n",
                         tc_riscv32_reg_name(TC_RISCV32_TMP0));
        tc_riscv32_normalize_bool(emitter, 10u);
        tc_riscv32_add_constant(emitter, TC_RISCV32_TMP0,
                                TC_RISCV32_LOCALS,
                                emitter->scratch_offset);
        tc_riscv32_write(emitter, "  sb a0, 0(%s)\n",
                         tc_riscv32_reg_name(TC_RISCV32_TMP0));
        tc_riscv32_arg_reg(emitter, 1u, TC_RISCV32_TMP0);
        tc_riscv32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    } else {
        tc_riscv32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
        tc_riscv32_arg_address(emitter, 1u, &instr->args[1].value);
    }
    tc_riscv32_arg_immediate(emitter, 2u, (uint32_t)size);
    tc_riscv32_arg_immediate(emitter, 3u, flags);
    tc_riscv32_arg_immediate(emitter, 4u, type_id);
    tc_riscv32_arg_immediate(emitter, 5u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 6u, instr->span.column);
    tc_riscv32_arg_file(emitter, 7u, &instr->span);
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_STORE);
}

static void emit_memmove(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t element = tc_riscv32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_riscv32_type_id(emitter, instr->type);
    if (!ok) {
        tc_riscv32_error(emitter, "memmove of an incomplete type");
        return;
    }
    tc_riscv32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 1u, &instr->args[1].value, 0);
    tc_riscv32_arg_operand(emitter, 2u, &instr->args[2].value, 0);
    tc_riscv32_arg_immediate(emitter, 3u, (uint32_t)element);
    tc_riscv32_arg_immediate(emitter, 4u, type_id);
    tc_riscv32_arg_immediate(emitter, 5u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 6u, instr->span.column);
    tc_riscv32_arg_file(emitter, 7u, &instr->span);
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_MEMMOVE);
}

static void emit_addr(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    tc_riscv32_operand_address(emitter, &instr->args[0].value, 10u);
    tc_riscv32_move_immediate(emitter, 11u, 0u);
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_ptr_arith(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t element = tc_riscv32_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_riscv32_type_id(emitter, instr->type);
    if (!ok) {
        tc_riscv32_error(emitter,
                         "pointer arithmetic on an incomplete type");
        return;
    }
    tc_riscv32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 1u, &instr->args[1].value, 0);
    tc_riscv32_arg_immediate(emitter, 2u, (uint32_t)element);
    tc_riscv32_arg_immediate(emitter, 3u,
                             instr->op == TC_PTRADD ? 1u : 0u);
    tc_riscv32_arg_immediate(emitter, 4u, type_id);
    tc_riscv32_arg_immediate(emitter, 5u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 6u, instr->span.column);
    tc_riscv32_arg_file(emitter, 7u, &instr->span);
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_PTRADD);
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_ptrdiff(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    tc_riscv32_arg_operand(emitter, 0u, &instr->args[0].value, 0);
    tc_riscv32_arg_operand(emitter, 1u, &instr->args[1].value, 0);
    tc_riscv32_arg_immediate(emitter, 2u, instr->span.line);
    tc_riscv32_arg_immediate(emitter, 3u, instr->span.column);
    tc_riscv32_arg_file(emitter, 4u, &instr->span);
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_PTRDIFF);
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_value_compare(TcRiscv32Emitter *emitter,
                               const TcInstr *instr) {
    const char *branch;
    unsigned id = tc_riscv32_label(emitter);
    tc_riscv32_load_operand32(emitter, &instr->args[0].value, 10u);
    tc_riscv32_load_operand32(emitter, &instr->args[1].value, 11u);
    switch (instr->op) {
    case TC_PTREQ:
    case TC_FUNCEQ:
        branch = "beq";
        break;
    case TC_PTRNE:
    case TC_FUNCNE:
        branch = "bne";
        break;
    case TC_PTRLT:
        branch = "bltu";
        break;
    case TC_PTRLE:
        branch = "bgeu"; /* a1 >= a0  <=>  a0 <= a1 */
        break;
    case TC_PTRGT:
        branch = "bltu"; /* swap operands below */
        break;
    case TC_PTRGE:
        branch = "bgeu";
        break;
    default:
        tc_riscv32_error(emitter, "invalid pointer comparison");
        return;
    }
    tc_riscv32_write(emitter, "  li a2, 0\n");
    if (instr->op == TC_PTRLE) {
        tc_riscv32_write(emitter, "  bgeu a1, a0, .Lcmp_true_%u\n", id);
    } else if (instr->op == TC_PTRGT) {
        tc_riscv32_write(emitter, "  bltu a1, a0, .Lcmp_true_%u\n", id);
    } else {
        tc_riscv32_write(emitter, "  %s a0, a1, .Lcmp_true_%u\n", branch,
                         id);
    }
    tc_riscv32_write(emitter, "  j .Lcmp_done_%u\n"
                              ".Lcmp_true_%u:\n  li a2, 1\n"
                              ".Lcmp_done_%u:\n",
                     id, id, id);
    tc_riscv32_store_result64(emitter, instr, 12u, 0u);
}

static void emit_funcaddr(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    uint32_t func_id;
    if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
        tc_riscv32_error(emitter, "funcaddr without a function operand");
        return;
    }
    func_id = instr->args[0].value.func;
    if (func_id >= emitter->ir->nfuncs) {
        tc_riscv32_error(emitter, "funcaddr of an invalid function");
        return;
    }
    {
        char symbol[64];
        snprintf(symbol, sizeof(symbol), "tc_function_%u",
                 (unsigned)func_id);
        tc_riscv32_load_symbol(emitter, 10u, symbol);
    }
    tc_riscv32_move_immediate(emitter, 11u, 0u);
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

/* ------------------------------------------------------------------------- */
/* Calls.                                                                    */
/* ------------------------------------------------------------------------- */

static void emit_depth_enter(TcRiscv32Emitter *emitter,
                             const TcSpan *span) {
    unsigned id = tc_riscv32_label(emitter);
    tc_riscv32_load_symbol(emitter, TC_RISCV32_TMP0,
                           TC_RT_SYM_CALL_DEPTH);
    tc_riscv32_write(emitter, "  lw %s, 0(%s)\n",
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_move_immediate(emitter, TC_RISCV32_VALUE1,
                              emitter->profile->depth_D);
    tc_riscv32_write(emitter, "  bltu %s, %s, .Ldepth_ok_%u\n",
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_VALUE1), id);
    tc_riscv32_trap(emitter, span, TC_EK_STACK_OVERFLOW);
    tc_riscv32_write(emitter, ".Ldepth_ok_%u:\n"
                              "  addi %s, %s, 1\n"
                              "  sw %s, 0(%s)\n",
                     id, tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
}

static void emit_depth_leave(TcRiscv32Emitter *emitter) {
    tc_riscv32_load_symbol(emitter, TC_RISCV32_TMP0,
                           TC_RT_SYM_CALL_DEPTH);
    tc_riscv32_write(emitter, "  lw %s, 0(%s)\n"
                              "  addi %s, %s, -1\n"
                              "  sw %s, 0(%s)\n",
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_TMP0),
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
}

static void store_call_result(TcRiscv32Emitter *emitter,
                              const TcInstr *instr, const TcType *ret,
                              TcRiscv32RetPlan *plan) {
    int ok = 0;
    uint64_t size;
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        return;
    }
    size = tc_riscv32_type_size(emitter->ir, ret, &ok);
    if (!ok) {
        return;
    }
    if (plan->kind == TC_RISCV32_RET_INDIRECT) {
        /* The callee wrote into the provided buffer. */
        if (emitter->profile->checked_build &&
            instr->dst.kind != TC_OP_DISCARD) {
            define_dst_range(emitter, instr, size);
        }
        return;
    }
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (ret->tag == TC_TY_STRUCT) {
        tc_riscv32_operand_address(emitter, &instr->dst, TC_RISCV32_TMP0);
        if (size <= 4u) {
            tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 0,
                                  (unsigned)size, 10u);
        } else {
            tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 0, 4u, 10u);
            tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 4,
                                  (unsigned)(size - 4u), 11u);
        }
        if (emitter->profile->checked_build) {
            define_dst_range(emitter, instr, size);
        }
        return;
    }
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_call(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    const TcType **params = NULL;
    size_t nparams = 0;
    const TcType *ret = NULL;
    TcRiscv32ArgPlan plan;
    TcRiscv32RetPlan ret_plan;
    size_t i;
    int indirect = instr->op == TC_CALL_INDIRECT;
    uint32_t func_id = 0;
    uint64_t *ref_offsets = NULL;
    tc_riscv32_function_signature(emitter, instr, &params, &nparams, &ret);
    if (emitter->failed) {
        free(params);
        return;
    }
    if (indirect) {
        if (instr->nargs - 1u != nparams) {
            tc_riscv32_error(emitter,
                             "call_indirect argument count mismatch");
            free(params);
            return;
        }
    } else {
        if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
            tc_riscv32_error(emitter,
                             "direct call without a function operand");
            free(params);
            return;
        }
        func_id = instr->args[0].value.func;
        if (instr->nargs - 1u != nparams) {
            tc_riscv32_error(emitter, "call argument count mismatch");
            free(params);
            return;
        }
    }
    tc_riscv32_plan_return(emitter, ret, &ret_plan);
    tc_riscv32_plan_arguments(
        emitter, params, nparams,
        ret_plan.kind == TC_RISCV32_RET_INDIRECT ? 1 : 0, &plan);
    if (emitter->failed) {
        free(params);
        tc_riscv32_free_argument_plan(&plan);
        return;
    }
    if (nparams != 0) {
        ref_offsets = (uint64_t *)calloc(nparams, sizeof(*ref_offsets));
        if (ref_offsets == NULL) {
            tc_riscv32_error(emitter, "out of memory");
            free(params);
            tc_riscv32_free_argument_plan(&plan);
            return;
        }
    }
    /* Stack-resident portions and by-reference copies first: memcpy clobbers
     * a0-a2, so no register argument may be live yet. */
    {
        uint64_t ref_cursor = 0;
        for (i = 0; i < nparams; i++) {
            const TcRiscv32ArgSlot *loc = &plan.slots[i];
            const TcArg *arg = &instr->args[i + 1u];
            if (loc->by_reference) {
                ref_cursor = (ref_cursor + 7u) & ~UINT64_C(7);
                ref_offsets[i] = ref_cursor;
                tc_riscv32_add_constant(
                    emitter, 10u, TC_RISCV32_LOCALS,
                    emitter->call_area_offset + ref_cursor);
                tc_riscv32_operand_address(emitter, &arg->value, 11u);
                tc_riscv32_move_immediate(emitter, 12u,
                                          (uint32_t)loc->size);
                tc_riscv32_write(emitter, "  call memcpy\n");
                ref_cursor += (loc->size + 7u) & ~UINT64_C(7);
                continue;
            }
            if (loc->struct_value &&
                loc->kind == TC_RISCV32_ARG_STACK) {
                tc_riscv32_add_constant(emitter, 10u, TC_RISCV32_SP,
                                        loc->stack_offset);
                tc_riscv32_operand_address(emitter, &arg->value, 11u);
                tc_riscv32_move_immediate(emitter, 12u,
                                          (uint32_t)loc->size);
                tc_riscv32_write(emitter, "  call memcpy\n");
                continue;
            }
            if (loc->struct_value && loc->kind == TC_RISCV32_ARG_REG &&
                loc->stack_words != 0) {
                uint64_t bytes = loc->size - loc->stack_byte_offset;
                tc_riscv32_add_constant(emitter, 10u, TC_RISCV32_SP,
                                        loc->stack_offset);
                tc_riscv32_operand_address(emitter, &arg->value, 11u);
                tc_riscv32_add_constant(emitter, 11u, 11u,
                                        loc->stack_byte_offset);
                tc_riscv32_move_immediate(emitter, 12u, (uint32_t)bytes);
                tc_riscv32_write(emitter, "  call memcpy\n");
                continue;
            }
            if (loc->is64 && !loc->split64 &&
                loc->kind == TC_RISCV32_ARG_STACK) {
                tc_riscv32_load_operand64(emitter, &arg->value,
                                          TC_RISCV32_VALUE0,
                                          TC_RISCV32_VALUE1);
                store_word_sp(emitter, loc->stack_offset,
                              TC_RISCV32_VALUE0);
                store_word_sp(emitter, loc->stack_offset + 4u,
                              TC_RISCV32_VALUE1);
                continue;
            }
            if (!loc->is64 && !loc->struct_value &&
                loc->kind == TC_RISCV32_ARG_STACK) {
                tc_riscv32_load_operand32(emitter, &arg->value,
                                          TC_RISCV32_VALUE0);
                if (is_signed_narrow(params[i])) {
                    tc_riscv32_sign_extend_narrow(
                        emitter, TC_RISCV32_VALUE0, params[i]);
                }
                store_word_sp(emitter, loc->stack_offset,
                              TC_RISCV32_VALUE0);
                continue;
            }
        }
    }
    if (ret_plan.kind == TC_RISCV32_RET_INDIRECT) {
        if (instr->dst.kind == TC_OP_DISCARD) {
            tc_riscv32_add_constant(emitter, 10u, TC_RISCV32_LOCALS,
                                    emitter->scratch_buffer_offset);
        } else {
            tc_riscv32_operand_address(emitter, &instr->dst, 10u);
        }
    }
    /* Register arguments. */
    for (i = 0; i < nparams; i++) {
        const TcRiscv32ArgSlot *loc = &plan.slots[i];
        const TcArg *arg = &instr->args[i + 1u];
        unsigned reg = 10u + loc->reg;
        if (loc->by_reference) {
            tc_riscv32_add_constant(
                emitter, TC_RISCV32_TMP0, TC_RISCV32_LOCALS,
                emitter->call_area_offset + ref_offsets[i]);
            if (loc->kind == TC_RISCV32_ARG_REG) {
                tc_riscv32_write(emitter, "  mv %s, %s\n", tc_riscv32_reg_name(reg),
                                 tc_riscv32_reg_name(TC_RISCV32_TMP0));
            } else {
                store_word_sp(emitter, loc->stack_offset,
                              TC_RISCV32_TMP0);
            }
            continue;
        }
        if (loc->struct_value) {
            unsigned k;
            for (k = 0; k < loc->reg_words; k++) {
                uint64_t offset = (uint64_t)k * 4u;
                uint64_t remaining = loc->size - offset;
                unsigned bytes = remaining >= 4u ? 4u
                                                 : (unsigned)remaining;
                tc_riscv32_operand_address(emitter, &arg->value,
                                           TC_RISCV32_TMP0);
                if (offset != 0) {
                    tc_riscv32_add_constant(emitter, TC_RISCV32_TMP1,
                                            TC_RISCV32_TMP0, offset);
                } else {
                    tc_riscv32_write(emitter, "  mv %s, %s\n",
                                     tc_riscv32_reg_name(TC_RISCV32_TMP1),
                                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
                }
                tc_riscv32_load_bits(emitter, TC_RISCV32_TMP1, 0, bytes,
                                     10u + loc->reg + k);
            }
            continue;
        }
        if (loc->kind != TC_RISCV32_ARG_REG) {
            continue;
        }
        if (loc->is64) {
            if (loc->split64) {
                /* Only a7 is available: load the low word there and pass the
                 * high word on the stack through a scratch register. */
                tc_riscv32_load_operand64(emitter, &arg->value, reg,
                                          TC_RISCV32_VALUE1);
                store_word_sp(emitter, loc->stack_offset,
                              TC_RISCV32_VALUE1);
            } else {
                tc_riscv32_load_operand64(emitter, &arg->value, reg,
                                          reg + 1u);
            }
        } else {
            tc_riscv32_load_operand32(emitter, &arg->value, reg);
            if (is_signed_narrow(params[i])) {
                tc_riscv32_sign_extend_narrow(emitter, reg, params[i]);
            }
        }
    }
    if (indirect) {
        tc_riscv32_load_operand32(emitter, &instr->args[0].value,
                                  TC_RISCV32_TMP1);
    }
    emit_depth_enter(emitter, &instr->span);
    if (indirect) {
        unsigned id = tc_riscv32_label(emitter);
        tc_riscv32_write(emitter, "  bne %s, zero, .Lfp_ok_%u\n",
                         tc_riscv32_reg_name(TC_RISCV32_TMP1), id);
        tc_riscv32_trap(emitter, &instr->span,
                        TC_EK_NULL_FUNCTION_POINTER);
        tc_riscv32_write(emitter, ".Lfp_ok_%u:\n"
                                  "  jalr ra, 0(%s)\n",
                         id, tc_riscv32_reg_name(TC_RISCV32_TMP1));
    } else {
        tc_riscv32_write(emitter, "  call tc_function_%u\n",
                         (unsigned)func_id);
    }
    emit_depth_leave(emitter);
    store_call_result(emitter, instr, ret, &ret_plan);
    free(ref_offsets);
    free(params);
    tc_riscv32_free_argument_plan(&plan);
}

/* ------------------------------------------------------------------------- */
/* Aggregates.                                                               */
/* ------------------------------------------------------------------------- */

static const TcField *make_field_for_arg(TcRiscv32Emitter *emitter,
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

static void emit_make(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    const TcStructDef *def =
        tc_riscv32_struct_def(emitter->ir, instr->type);
    int ok = 0;
    uint64_t size = tc_riscv32_type_size(emitter->ir, instr->type, &ok);
    size_t i;
    if (def == NULL || !ok) {
        tc_riscv32_error(emitter, "make of an incomplete struct type");
        return;
    }
    emit_dst_address(emitter, instr, 10u);
    tc_riscv32_write(emitter, "  li a1, 0\n");
    tc_riscv32_move_immediate(emitter, 12u, (uint32_t)size);
    tc_riscv32_write(emitter, "  call memset\n");
    for (i = 0; i < instr->nargs; i++) {
        const TcArg *arg = &instr->args[i];
        const TcField *field =
            make_field_for_arg(emitter, def, arg, i);
        const TcType *field_type;
        unsigned width;
        int field_ok = 0;
        uint64_t field_size;
        if (field == NULL) {
            tc_riscv32_error(emitter,
                             "make field does not match the struct");
            return;
        }
        field_type = field->type;
        width = tc_riscv32_type_width(field_type);
        field_size =
            tc_riscv32_type_size(emitter->ir, field_type, &field_ok);
        if (!field_ok) {
            tc_riscv32_error(emitter,
                             "make field has an incomplete type");
            return;
        }
        if (field_type->tag == TC_TY_STRUCT) {
            if (arg->value.kind == TC_OP_CONST) {
                tc_riscv32_error(emitter,
                                 "aggregate make field is a literal");
                return;
            }
            emit_dst_address(emitter, instr, 10u);
            if (field->offset != 0) {
                tc_riscv32_add_constant(emitter, 10u, 10u, field->offset);
            }
            tc_riscv32_operand_address(emitter, &arg->value, 11u);
            tc_riscv32_move_immediate(emitter, 12u,
                                      (uint32_t)field_size);
            tc_riscv32_write(emitter, "  call memcpy\n");
        } else {
            tc_riscv32_load_operand64(emitter, &arg->value,
                                      TC_RISCV32_VALUE0,
                                      TC_RISCV32_VALUE1);
            if (field_type->tag == TC_TY_BOOL) {
                tc_riscv32_normalize_bool(emitter, TC_RISCV32_VALUE0);
            } else if (width < 32u) {
                tc_riscv32_mask_register(emitter, TC_RISCV32_VALUE0,
                                         field_type);
            }
            emit_dst_address(emitter, instr, TC_RISCV32_TMP0);
            if (field->offset != 0) {
                tc_riscv32_add_constant(emitter, TC_RISCV32_TMP0,
                                        TC_RISCV32_TMP0, field->offset);
            }
            if (width > 32u) {
                tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 0, 4u,
                                      TC_RISCV32_VALUE0);
                tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 4, 4u,
                                      TC_RISCV32_VALUE1);
            } else {
                tc_riscv32_store_bits(
                    emitter, TC_RISCV32_TMP0, 0,
                    width >= 32u ? 4u : width / 8u,
                    TC_RISCV32_VALUE0);
            }
        }
    }
    if (emitter->profile->checked_build &&
        instr->dst.kind != TC_OP_DISCARD) {
        define_dst_range(emitter, instr, size);
    }
}

static void emit_copy(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int ok = 0;
    uint64_t size;
    if (type == NULL) {
        tc_riscv32_error(emitter, "copy without a type");
        return;
    }
    size = tc_riscv32_type_size(emitter->ir, type, &ok);
    if (!ok) {
        tc_riscv32_error(emitter, "copy of an incomplete type");
        return;
    }
    if (type->tag == TC_TY_STRUCT) {
        emit_dst_address(emitter, instr, 10u);
        tc_riscv32_operand_address(emitter, &instr->args[0].value, 11u);
        tc_riscv32_move_immediate(emitter, 12u, (uint32_t)size);
        tc_riscv32_write(emitter, "  call memcpy\n");
        if (emitter->profile->checked_build) {
            define_dst_range(emitter, instr, size);
        }
        return;
    }
    tc_riscv32_load_operand64(emitter, &instr->args[0].value, 10u, 11u);
    if (type->tag == TC_TY_BOOL) {
        tc_riscv32_normalize_bool(emitter, 10u);
    } else if (tc_riscv32_type_width(type) < 32u) {
        tc_riscv32_mask_register(emitter, 10u, type);
    }
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

/* ------------------------------------------------------------------------- */
/* Control flow, return and I/O.                                             */
/* ------------------------------------------------------------------------- */

static void emit_long_jump_target(TcRiscv32Emitter *emitter,
                                   const char *target) {
    unsigned id = tc_riscv32_label(emitter);
    tc_riscv32_write(emitter, ".Llong_%u:\n", id);
    tc_riscv32_write(emitter, "  auipc t0, %%pcrel_hi(%s)\n", target);
    tc_riscv32_write(emitter,
                     "  jalr x0, %%pcrel_lo(.Llong_%u)(t0)\n", id);
}

static void emit_long_jump_block(TcRiscv32Emitter *emitter,
                                 size_t unit_index, unsigned block) {
    char target[64];
    snprintf(target, sizeof(target), ".L%zu_block%u", unit_index, block);
    emit_long_jump_target(emitter, target);
}

static void emit_branch(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    if (instr->args[0].value.kind != TC_OP_LABEL ||
        instr->args[0].value.label >= emitter->unit->nblocks) {
        tc_riscv32_error(emitter, "invalid branch label");
        return;
    }
    emit_long_jump_block(emitter, emitter->unit_index,
                         instr->args[0].value.label);
}

static void emit_branch_if(TcRiscv32Emitter *emitter,
                           const TcInstr *instr) {
    if (instr->args[1].value.kind != TC_OP_LABEL ||
        instr->args[1].value.label >= emitter->unit->nblocks) {
        tc_riscv32_error(emitter,
                         "invalid conditional branch label");
        return;
    }
    tc_riscv32_load_operand32(emitter, &instr->args[0].value, 10u);
    {
        unsigned id = tc_riscv32_label(emitter);
        tc_riscv32_write(emitter, "  beq a0, zero, .Lbrif_skip_%u\n", id);
        emit_long_jump_block(emitter, emitter->unit_index,
                             instr->args[1].value.label);
        tc_riscv32_write(emitter, ".Lbrif_skip_%u:\n", id);
    }
}

static void emit_switch(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    const TcType *type =
        tc_riscv32_value_type(emitter, &instr->args[0].value);
    size_t ntargets = instr->ntargets;
    size_t i;
    unsigned id = tc_riscv32_label(emitter);
    unsigned width = tc_riscv32_type_width(type);
    size_t ndispatch;
    if (!tc_type_is_integer(type) || ntargets == 0) {
        tc_riscv32_error(emitter,
                         "switch requires an integer selector and default");
        return;
    }
    ndispatch = ntargets - 1u;
    tc_riscv32_load_operand64(emitter, &instr->args[0].value, 10u, 11u);
    if (width > 32u) {
        unsigned low = tc_riscv32_label(emitter);
        tc_riscv32_write(emitter,
                         "  beq a1, zero, .Lswitch_low_%u\n", low);
        emit_long_jump_block(emitter, emitter->unit_index,
                             instr->switch_targets[ntargets - 1u]);
        tc_riscv32_write(emitter, ".Lswitch_low_%u:\n", low);
    }
    if (ndispatch == 0) {
        emit_long_jump_block(emitter, emitter->unit_index,
                             instr->switch_targets[ntargets - 1u]);
    } else {
        unsigned in_range;
        tc_riscv32_move_immediate(emitter, 12u, (uint32_t)ndispatch);
        in_range = tc_riscv32_label(emitter);
        tc_riscv32_write(emitter,
                         "  bltu a0, a2, .Lswitch_inrange_%u\n",
                         in_range);
        emit_long_jump_block(emitter, emitter->unit_index,
                             instr->switch_targets[ntargets - 1u]);
        tc_riscv32_write(emitter, ".Lswitch_inrange_%u:\n", in_range);
        for (i = 0; i < ndispatch; i++) {
            unsigned next;
            tc_riscv32_move_immediate(emitter, 12u, (uint32_t)i);
            next = tc_riscv32_label(emitter);
            tc_riscv32_write(emitter,
                             "  bne a0, a2, .Lswitch_next_%u\n", next);
            emit_long_jump_block(emitter, emitter->unit_index,
                                 instr->switch_targets[i]);
            tc_riscv32_write(emitter, ".Lswitch_next_%u:\n", next);
        }
        emit_long_jump_block(emitter, emitter->unit_index,
                             instr->switch_targets[ntargets - 1u]);
    }
    tc_riscv32_write(emitter, ".Lswitch_default_%u:\n", id);
    emit_long_jump_block(emitter, emitter->unit_index,
                         instr->switch_targets[ntargets - 1u]);
    for (i = 0; i < ndispatch; i++) {
        tc_riscv32_write(emitter, ".Lswitch_case_%u_%zu:\n", id, i);
        emit_long_jump_block(emitter, emitter->unit_index,
                             instr->switch_targets[i]);
    }
}

static void emit_return(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    const TcType *ret = NULL;
    TcRiscv32RetPlan plan;
    if (emitter->unit->func_id != TC_UNIT_TOP_LEVEL &&
        emitter->unit->func_id < emitter->ir->nfuncs) {
        ret = emitter->ir->funcs[emitter->unit->func_id].sig.ret;
    }
    if (instr->nargs == 0) {
        char target[64];
        snprintf(target, sizeof(target), ".Lreturn_%zu",
                 emitter->unit_index);
        emit_long_jump_target(emitter, target);
        return;
    }
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        tc_riscv32_error(emitter, "return value in a void unit");
        return;
    }
    tc_riscv32_plan_return(emitter, ret, &plan);
    {
        const TcOperand *value = &instr->args[0].value;
        int ok = 0;
        uint64_t size = tc_riscv32_type_size(emitter->ir, ret, &ok);
        if (!ok) {
            tc_riscv32_error(emitter,
                             "return of an incomplete type");
            return;
        }
        if (plan.kind == TC_RISCV32_RET_WORD &&
            ret->tag != TC_TY_STRUCT) {
            tc_riscv32_load_operand64(emitter, value, 10u, 11u);
            if (ret->tag == TC_TY_BOOL) {
                tc_riscv32_normalize_bool(emitter, 10u);
            } else if (tc_riscv32_type_width(ret) < 32u) {
                if (tc_type_is_signed_integer(ret)) {
                    tc_riscv32_sign_extend_narrow(emitter, 10u, ret);
                } else {
                    tc_riscv32_mask_register(emitter, 10u, ret);
                }
            }
        } else if (plan.kind == TC_RISCV32_RET_PAIR &&
                   ret->tag != TC_TY_STRUCT) {
            tc_riscv32_load_operand64(emitter, value, 10u, 11u);
        } else if (plan.kind == TC_RISCV32_RET_WORD) {
            tc_riscv32_operand_address(emitter, value, TC_RISCV32_TMP0);
            tc_riscv32_load_bits(emitter, TC_RISCV32_TMP0, 0,
                                 (unsigned)size, 10u);
        } else if (plan.kind == TC_RISCV32_RET_PAIR) {
            tc_riscv32_operand_address(emitter, value, TC_RISCV32_TMP0);
            tc_riscv32_load_bits(emitter, TC_RISCV32_TMP0, 0, 4u, 10u);
            tc_riscv32_load_bits(emitter, TC_RISCV32_TMP0, 4,
                                 (unsigned)(size - 4u), 11u);
        } else {
            /* Indirect result: copy through the saved hidden pointer; the
             * memcpy return value is that pointer. */
            tc_riscv32_load_bits(emitter, TC_RISCV32_LOCALS,
                                 emitter->sret_offset, 4u,
                                 TC_RISCV32_TMP0);
            tc_riscv32_operand_address(emitter, value, TC_RISCV32_TMP1);
            tc_riscv32_write(emitter, "  mv a0, %s\n",
                             tc_riscv32_reg_name(TC_RISCV32_TMP0));
            tc_riscv32_write(emitter, "  mv a1, %s\n",
                             tc_riscv32_reg_name(TC_RISCV32_TMP1));
            tc_riscv32_move_immediate(emitter, 12u, (uint32_t)size);
            tc_riscv32_write(emitter, "  call memcpy\n");
        }
    }
    {
        char target[64];
        snprintf(target, sizeof(target), ".Lreturn_%zu",
                 emitter->unit_index);
        emit_long_jump_target(emitter, target);
    }
}

static void emit_read(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    unsigned id = tc_riscv32_label(emitter);
    unsigned width = tc_riscv32_type_width(instr->type);
    int ok = 0;
    uint64_t size = tc_riscv32_type_size(emitter->ir, instr->type, &ok);
    if (!ok) {
        tc_riscv32_error(emitter, "read of an incomplete type");
        return;
    }
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->type->tag);
    tc_riscv32_add_constant(emitter, 11u, TC_RISCV32_LOCALS,
                            emitter->read_scratch_offset);
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_READ);
    tc_riscv32_write(emitter, "  beq a0, zero, .Lio_ok_%u\n", id);
    tc_riscv32_trap(emitter, &instr->span, TC_EK_IO);
    tc_riscv32_write(emitter, ".Lio_ok_%u:\n", id);
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (width > 32u) {
        tc_riscv32_load_bits(emitter, TC_RISCV32_LOCALS,
                             emitter->read_scratch_offset, 4u, 10u);
        tc_riscv32_load_bits(emitter, TC_RISCV32_LOCALS,
                             emitter->read_scratch_offset + 4u, 4u, 11u);
    } else {
        tc_riscv32_load_bits(emitter, TC_RISCV32_LOCALS,
                             emitter->read_scratch_offset,
                             (unsigned)size, 10u);
        tc_riscv32_write(emitter, "  li a1, 0\n");
    }
    tc_riscv32_store_result64(emitter, instr, 10u, 11u);
}

static void emit_write(TcRiscv32Emitter *emitter, const TcInstr *instr) {
    unsigned id = tc_riscv32_label(emitter);
    tc_riscv32_arg_immediate(emitter, 0u, (uint32_t)instr->type->tag);
    tc_riscv32_arg_immediate(emitter, 1u,
                             instr->op == TC_WRITELN ? 1u : 0u);
    tc_riscv32_load_operand64(emitter, &instr->args[0].value, 12u, 13u);
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_WRITE);
    tc_riscv32_write(emitter, "  beq a0, zero, .Lio_ok_%u\n", id);
    tc_riscv32_trap(emitter, &instr->span, TC_EK_IO);
    tc_riscv32_write(emitter, ".Lio_ok_%u:\n", id);
}

/* ------------------------------------------------------------------------- */
/* Dispatch.                                                                 */
/* ------------------------------------------------------------------------- */

void tc_riscv32_emit_instruction(TcRiscv32Emitter *emitter,
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
            tc_riscv32_error(emitter, "arithmetic without a type");
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
            tc_riscv32_error(emitter,
                             "unary arithmetic without a type");
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
            tc_riscv32_error(emitter, "comparison without a type");
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
        uint64_t size =
            tc_riscv32_type_size(emitter->ir, instr->type, &ok);
        if (!ok) {
            tc_riscv32_error(emitter,
                             "sizeof of an incomplete type");
            break;
        }
        tc_riscv32_move_immediate(emitter, 10u, (uint32_t)size);
        tc_riscv32_move_immediate(emitter, 11u, 0u);
        tc_riscv32_store_result64(emitter, instr, 10u, 11u);
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
        tc_riscv32_trap(emitter, &instr->span, TC_EK_TRAP);
        break;
    case TC_READ:
        emit_read(emitter, instr);
        break;
    case TC_WRITE:
    case TC_WRITELN:
        emit_write(emitter, instr);
        break;
    default:
        tc_riscv32_error(emitter, "unsupported opcode %d", (int)instr->op);
        break;
    }
}
