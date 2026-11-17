/* TC 0.0.46 AArch64 per-instruction lowering (design §10.3).
 *
 * Integer arithmetic and control flow lower to native instructions; floating
 * arithmetic, conversions and the object/pointer memory operations call the
 * shared runtime so the VM and AArch64 execute one implementation of the
 * standard's exact rules.  Values are read from and written to stack slots;
 * helpers preserve x19 (frame base) and x20 (static base) per AAPCS64.
 */
#include "backend/aarch64/emit_internal.h"

#include <stdlib.h>
#include <string.h>

#include "backend/common/rt_abi.h"
#include "ir/instr.h"
#include "runtime/trap.h"

/* ------------------------------------------------------------------------- */
/* Small helpers.                                                            */
/* ------------------------------------------------------------------------- */

static int int_strict_signed(const TcType *type, TcOpMode mode) {
    return tc_type_is_signed_integer(type) && mode != TC_WRAP;
}

static void emit_overflow_trap(TcA64Emitter *emitter, const TcSpan *span) {
    tc_a64_trap(emitter, span, TC_EK_INTEGER_OVERFLOW);
}

/* Load `bytes` (1-8) from [base+offset] and assemble them little-endian. */
static void load_partial_bits(TcA64Emitter *emitter, unsigned base_reg,
                              uint64_t offset, unsigned bytes,
                              unsigned dst_reg) {
    unsigned i;
    tc_a64_write(emitter, "  mov x%u, #0\n", dst_reg);
    for (i = 0; i < bytes; i++) {
        tc_a64_write(emitter, "  ldrb w15, [x%u, #%llu]\n", base_reg,
                     (unsigned long long)(offset + i));
        if (i == 0) {
            tc_a64_write(emitter, "  mov x%u, x15\n", dst_reg);
        } else {
            tc_a64_write(emitter, "  orr x%u, x%u, x15, lsl #%u\n", dst_reg,
                         dst_reg, 8u * i);
        }
    }
}

/* Store the low `bytes` (1-8) of `src_reg` to [base+offset]. */
static void store_partial_bits(TcA64Emitter *emitter, unsigned base_reg,
                               uint64_t offset, unsigned bytes,
                               unsigned src_reg) {
    unsigned i;
    tc_a64_write(emitter, "  mov x15, x%u\n", src_reg);
    for (i = 0; i < bytes; i++) {
        tc_a64_write(emitter, "  strb w15, [x%u, #%llu]\n", base_reg,
                     (unsigned long long)(offset + i));
        if (i + 1u < bytes) {
            tc_a64_write(emitter, "  lsr x15, x15, #8\n");
        }
    }
}

static void emit_copy_value(TcA64Emitter *emitter, const TcOperand *dst,
                            const TcOperand *src, const TcType *type) {
    int ok = 0;
    uint64_t size = tc_a64_type_size(emitter->ir, type, &ok);
    if (!ok) {
        tc_a64_error(emitter, "copy of an incomplete type");
        return;
    }
    tc_a64_operand_address(emitter, src, 17u);
    tc_a64_operand_address(emitter, dst, 16u);
    tc_a64_copy_bytes(emitter, size);
}

/* Address of an operand's storage.  A literal is materialized in the scratch
 * slot first (8-byte slots hold scalar bit patterns). */
static void emit_operand_storage(TcA64Emitter *emitter,
                                 const TcOperand *operand, unsigned dst_reg) {
    if (operand->kind == TC_OP_CONST) {
        tc_a64_load_const_to_scratch(emitter, operand);
        tc_a64_add_constant(emitter, dst_reg, 19u, emitter->scratch_offset);
        return;
    }
    tc_a64_operand_address(emitter, operand, dst_reg);
}

/* ------------------------------------------------------------------------- */
/* Integer arithmetic.                                                       */
/* ------------------------------------------------------------------------- */

static void emit_add_sub(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int strict = int_strict_signed(type, instr->mode);
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, strict);
    tc_a64_load_operand(emitter, &instr->args[1].value, 10u, strict);
    if (strict) {
        unsigned id = tc_a64_label(emitter);
        unsigned width = tc_a64_type_width(type);
        tc_a64_write(emitter, "  %s x11, x9, x10\n  b.vs .Loverflow_%u\n",
                     instr->op == TC_ADD ? "adds" : "subs", id);
        if (width < 64u) {
            tc_a64_write(emitter,
                         "  sxt%c x12, w11\n  cmp x12, x11\n"
                         "  b.ne .Loverflow_%u\n",
                         width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
        }
        tc_a64_write(emitter, "  b .Lsuccess_%u\n.Loverflow_%u:\n", id, id);
        emit_overflow_trap(emitter, &instr->span);
        tc_a64_write(emitter, ".Lsuccess_%u:\n", id);
        tc_a64_mask_register(emitter, 11u, type);
    } else {
        tc_a64_write(emitter, "  %s x11, x9, x10\n",
                     instr->op == TC_ADD ? "add" : "sub");
        tc_a64_mask_register(emitter, 11u, type);
    }
    tc_a64_store_result(emitter, instr, 11u);
}

static void emit_mul(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int strict = int_strict_signed(type, instr->mode);
    unsigned width = tc_a64_type_width(type);
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, strict);
    tc_a64_load_operand(emitter, &instr->args[1].value, 10u, strict);
    if (strict) {
        unsigned id = tc_a64_label(emitter);
        tc_a64_write(emitter, "  mul x11, x9, x10\n");
        if (width < 64u) {
            tc_a64_write(emitter,
                         "  sxt%c x12, w11\n  cmp x12, x11\n"
                         "  b.ne .Loverflow_%u\n",
                         width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
        } else {
            tc_a64_write(emitter,
                         "  smulh x12, x9, x10\n  asr x13, x11, #63\n"
                         "  cmp x12, x13\n  b.ne .Loverflow_%u\n",
                         id);
        }
        tc_a64_write(emitter, "  b .Lsuccess_%u\n.Loverflow_%u:\n", id, id);
        emit_overflow_trap(emitter, &instr->span);
        tc_a64_write(emitter, ".Lsuccess_%u:\n", id);
        tc_a64_mask_register(emitter, 11u, type);
    } else {
        tc_a64_write(emitter, "  mul x11, x9, x10\n");
        tc_a64_mask_register(emitter, 11u, type);
    }
    tc_a64_store_result(emitter, instr, 11u);
}

static void emit_neg_abs(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int strict = int_strict_signed(type, instr->mode);
    unsigned width = tc_a64_type_width(type);
    unsigned id = tc_a64_label(emitter);
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, strict);
    if (instr->op == TC_NEG) {
        if (strict) {
            tc_a64_write(emitter, "  negs x11, x9\n  b.vs .Loverflow_%u\n",
                         id);
            if (width < 64u) {
                tc_a64_write(emitter,
                             "  sxt%c x12, w11\n  cmp x12, x11\n"
                             "  b.ne .Loverflow_%u\n",
                             width == 8u ? 'b' : (width == 16u ? 'h' : 'w'),
                             id);
            }
            tc_a64_write(emitter, "  b .Lsuccess_%u\n.Loverflow_%u:\n", id, id);
            emit_overflow_trap(emitter, &instr->span);
            tc_a64_write(emitter, ".Lsuccess_%u:\n", id);
        } else {
            tc_a64_write(emitter, "  neg x11, x9\n");
        }
    } else if (is_signed) {
        tc_a64_write(emitter, "  cmp x9, #0\n  cneg x11, x9, lt\n");
        if (width < 64u) {
            tc_a64_write(emitter,
                         "  sxt%c x12, w11\n  cmp x12, x11\n"
                         "  b.ne .Loverflow_%u\n",
                         width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
        } else {
            tc_a64_write(emitter, "  tbnz x11, #63, .Loverflow_%u\n", id);
        }
        tc_a64_write(emitter, "  b .Lsuccess_%u\n.Loverflow_%u:\n", id, id);
        emit_overflow_trap(emitter, &instr->span);
        tc_a64_write(emitter, ".Lsuccess_%u:\n", id);
    } else {
        tc_a64_write(emitter, "  mov x11, x9\n");
    }
    tc_a64_mask_register(emitter, 11u, type);
    tc_a64_store_result(emitter, instr, 11u);
}

static void emit_bitwise(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    const char *mnemonic = instr->op == TC_AND ? "and"
                            : instr->op == TC_OR ? "orr"
                                                 : "eor";
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, 0);
    tc_a64_load_operand(emitter, &instr->args[1].value, 10u, 0);
    tc_a64_write(emitter, "  %s x11, x9, x10\n", mnemonic);
    if (type->tag != TC_TY_BOOL) {
        tc_a64_mask_register(emitter, 11u, type);
    } else {
        tc_a64_normalize_bool(emitter, 11u);
    }
    tc_a64_store_result(emitter, instr, 11u);
}

static void emit_not(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, 0);
    if (type->tag == TC_TY_BOOL) {
        tc_a64_write(emitter, "  eor x11, x9, #1\n");
    } else {
        tc_a64_write(emitter, "  mvn x11, x9\n");
        tc_a64_mask_register(emitter, 11u, type);
    }
    tc_a64_store_result(emitter, instr, 11u);
}

static void emit_div_mod(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int is_mod = instr->op == TC_MOD;
    unsigned width = tc_a64_type_width(type);
    unsigned id = tc_a64_label(emitter);
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, is_signed);
    tc_a64_load_operand(emitter, &instr->args[1].value, 10u, is_signed);
    tc_a64_write(emitter, "  cbz x10, .Ldivision_by_zero_%u\n", id);
    if (is_signed) {
        uint64_t min_bits = (uint64_t)0 - (UINT64_C(1) << (width - 1u));
        tc_a64_write(emitter, "  cmn x10, #1\n  b.ne .Lnormal_divisor_%u\n",
                     id);
        tc_a64_move_immediate(emitter, 16u, min_bits);
        tc_a64_write(emitter, "  cmp x9, x16\n  b.ne .Lnormal_divisor_%u\n",
                     id);
        if (is_mod) {
            tc_a64_write(emitter, "  movz x11, #0\n  b .Loperation_done_%u\n",
                         id);
        } else {
            tc_a64_write(emitter, "  b .Loverflow_%u\n", id);
        }
        tc_a64_write(emitter, ".Lnormal_divisor_%u:\n  sdiv x12, x9, x10\n",
                     id);
        if (is_mod) {
            tc_a64_write(emitter, "  msub x11, x10, x12, x9\n");
        } else {
            tc_a64_write(emitter, "  mov x11, x12\n");
        }
        tc_a64_mask_register(emitter, 11u, type);
        tc_a64_write(emitter, "  b .Loperation_done_%u\n", id);
        if (!is_mod) {
            tc_a64_write(emitter, ".Loverflow_%u:\n", id);
            emit_overflow_trap(emitter, &instr->span);
        }
    } else {
        tc_a64_write(emitter, "  udiv x12, x9, x10\n");
        if (is_mod) {
            tc_a64_write(emitter, "  msub x11, x10, x12, x9\n");
        } else {
            tc_a64_write(emitter, "  mov x11, x12\n");
        }
        tc_a64_mask_register(emitter, 11u, type);
    }
    tc_a64_write(emitter, ".Loperation_done_%u:\n", id);
    tc_a64_store_result(emitter, instr, 11u);
    tc_a64_write(emitter, "  b .Ldivision_finished_%u\n"
                          ".Ldivision_by_zero_%u:\n",
                 id, id);
    tc_a64_trap(emitter, &instr->span, TC_EK_DIV_ZERO);
    tc_a64_write(emitter, ".Ldivision_finished_%u:\n", id);
}

static void emit_shift(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int is_shl = instr->op == TC_SHL;
    int wrap = instr->mode == TC_WRAP;
    unsigned width = tc_a64_type_width(type);
    unsigned id = tc_a64_label(emitter);
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, is_signed);
    tc_a64_load_operand(emitter, &instr->args[1].value, 10u, is_signed);
    if (is_signed) {
        tc_a64_write(emitter, "  tbnz x10, #63, .Lnegative_shift_%u\n", id);
    }
    tc_a64_write(emitter, "  cmp x10, #%u\n  b.hs .Llarge_shift_%u\n", width,
                 id);
    if (is_shl) {
        tc_a64_write(emitter, "  lsl x11, x9, x10\n");
        if (wrap) {
            tc_a64_mask_register(emitter, 11u, type);
            tc_a64_write(emitter, "  b .Loperation_done_%u\n", id);
        } else if (is_signed) {
            tc_a64_write(emitter,
                         "  asr x12, x11, x10\n  cmp x12, x9\n"
                         "  b.ne .Loverflow_%u\n",
                         id);
            if (width < 64u) {
                tc_a64_write(emitter,
                             "  sxt%c x12, w11\n  cmp x12, x11\n"
                             "  b.ne .Loverflow_%u\n",
                             width == 8u ? 'b' : (width == 16u ? 'h' : 'w'),
                             id);
            }
            tc_a64_write(emitter, "  b .Loperation_done_%u\n", id);
        } else {
            tc_a64_write(emitter,
                         "  lsr x12, x11, x10\n  cmp x12, x9\n"
                         "  b.ne .Loverflow_%u\n",
                         id);
            if (width < 64u) {
                tc_a64_write(emitter,
                             "  and x13, x11, #0x%llx\n  cmp x13, x11\n"
                             "  b.ne .Loverflow_%u\n  mov x11, x13\n",
                             (unsigned long long)tc_a64_type_mask(type), id);
            }
            tc_a64_write(emitter, "  b .Loperation_done_%u\n", id);
        }
    } else {
        tc_a64_write(emitter, "  %s x11, x9, x10\n",
                     is_signed ? "asr" : "lsr");
        tc_a64_mask_register(emitter, 11u, type);
        tc_a64_write(emitter, "  b .Loperation_done_%u\n", id);
    }
    tc_a64_write(emitter, ".Llarge_shift_%u:\n", id);
    if (is_shl && !wrap) {
        tc_a64_write(emitter, "  cbnz x9, .Loverflow_%u\n", id);
    }
    tc_a64_write(emitter, "  movz x11, #0\n  b .Loperation_done_%u\n", id);
    if (!is_shl || !wrap) {
        tc_a64_write(emitter, ".Loverflow_%u:\n", id);
        emit_overflow_trap(emitter, &instr->span);
    }
    if (is_signed) {
        tc_a64_write(emitter, ".Lnegative_shift_%u:\n", id);
        tc_a64_trap(emitter, &instr->span, TC_EK_NEGATIVE_SHIFT);
    }
    tc_a64_write(emitter, ".Loperation_done_%u:\n", id);
    tc_a64_store_result(emitter, instr, 11u);
}

static void emit_compare_int(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_bool = type->tag == TC_TY_BOOL;
    int is_signed = tc_type_is_signed_integer(type);
    const char *condition;
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, is_signed);
    tc_a64_load_operand(emitter, &instr->args[1].value, 10u, is_signed);
    if (is_bool) {
        tc_a64_normalize_bool(emitter, 9u);
        tc_a64_normalize_bool(emitter, 10u);
    }
    switch (instr->op) {
    case TC_EQ: condition = "eq"; break;
    case TC_NE: condition = "ne"; break;
    case TC_LT: condition = is_signed ? "lt" : "lo"; break;
    case TC_LE: condition = is_signed ? "le" : "ls"; break;
    case TC_GT: condition = is_signed ? "gt" : "hi"; break;
    case TC_GE: condition = is_signed ? "ge" : "hs"; break;
    default:
        tc_a64_error(emitter, "invalid comparison");
        return;
    }
    tc_a64_write(emitter, "  cmp x9, x10\n  cset w11, %s\n", condition);
    tc_a64_store_result(emitter, instr, 11u);
}

static void emit_ptr_compare(TcA64Emitter *emitter, const TcInstr *instr) {
    const char *condition;
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, 0);
    tc_a64_load_operand(emitter, &instr->args[1].value, 10u, 0);
    switch (instr->op) {
    case TC_PTREQ:
    case TC_FUNCEQ: condition = "eq"; break;
    case TC_PTRNE:
    case TC_FUNCNE: condition = "ne"; break;
    case TC_PTRLT: condition = "lo"; break;
    case TC_PTRLE: condition = "ls"; break;
    case TC_PTRGT: condition = "hi"; break;
    case TC_PTRGE: condition = "hs"; break;
    default:
        tc_a64_error(emitter, "invalid pointer comparison");
        return;
    }
    tc_a64_write(emitter, "  cmp x9, x10\n  cset w11, %s\n", condition);
    tc_a64_store_result(emitter, instr, 11u);
}

/* ------------------------------------------------------------------------- */
/* Runtime-backed scalar operations.                                         */
/* ------------------------------------------------------------------------- */

static void emit_float_binop(TcA64Emitter *emitter, const TcInstr *instr) {
    tc_a64_load_operand(emitter, &instr->args[0].value, 3u, 0);
    tc_a64_load_operand(emitter, &instr->args[1].value, 4u, 0);
    tc_a64_move_immediate(emitter, 0u, (uint64_t)instr->op);
    tc_a64_move_immediate(emitter, 1u, (uint64_t)instr->mode);
    tc_a64_move_immediate(emitter, 2u, (uint64_t)instr->type->tag);
    tc_a64_span_arguments(emitter, &instr->span, 5u);
    tc_a64_emit_call0(emitter, TC_RT_SYM_FLOAT_BINOP);
    tc_a64_store_result(emitter, instr, 0u);
}

static void emit_float_unop(TcA64Emitter *emitter, const TcInstr *instr) {
    tc_a64_load_operand(emitter, &instr->args[0].value, 2u, 0);
    tc_a64_move_immediate(emitter, 0u, (uint64_t)instr->op);
    tc_a64_move_immediate(emitter, 1u, (uint64_t)instr->type->tag);
    tc_a64_emit_call0(emitter, TC_RT_SYM_FLOAT_UNOP);
    tc_a64_store_result(emitter, instr, 0u);
}

static void emit_float_compare(TcA64Emitter *emitter, const TcInstr *instr) {
    tc_a64_load_operand(emitter, &instr->args[0].value, 2u, 0);
    tc_a64_load_operand(emitter, &instr->args[1].value, 3u, 0);
    tc_a64_move_immediate(emitter, 0u, (uint64_t)instr->op);
    tc_a64_move_immediate(emitter, 1u, (uint64_t)instr->type->tag);
    tc_a64_emit_call0(emitter, TC_RT_SYM_FLOAT_COMPARE);
    tc_a64_store_result(emitter, instr, 0u);
}

static void emit_cast(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *source = tc_a64_value_type(emitter, &instr->args[0].value);
    if (source == NULL || instr->type == NULL) {
        tc_a64_error(emitter, "cast without source or destination type");
        return;
    }
    tc_a64_load_operand(emitter, &instr->args[0].value, 2u, 0);
    tc_a64_move_immediate(emitter, 0u, (uint64_t)source->tag);
    tc_a64_move_immediate(emitter, 1u, (uint64_t)instr->type->tag);
    tc_a64_span_arguments(emitter, &instr->span, 3u);
    tc_a64_emit_call0(emitter, TC_RT_SYM_CAST);
    tc_a64_store_result(emitter, instr, 0u);
}

static void emit_trunc_bitcast(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *source = tc_a64_value_type(emitter, &instr->args[0].value);
    if (source == NULL || instr->type == NULL) {
        tc_a64_error(emitter, "conversion without source or destination");
        return;
    }
    if (instr->op == TC_TRUNC) {
        tc_a64_load_operand(emitter, &instr->args[0].value, 2u, 0);
        tc_a64_move_immediate(emitter, 0u, (uint64_t)source->tag);
        tc_a64_move_immediate(emitter, 1u, (uint64_t)instr->type->tag);
        tc_a64_emit_call0(emitter, TC_RT_SYM_TRUNC);
    } else {
        tc_a64_load_operand(emitter, &instr->args[0].value, 1u, 0);
        tc_a64_move_immediate(emitter, 0u, (uint64_t)instr->type->tag);
        tc_a64_emit_call0(emitter, TC_RT_SYM_BITCAST);
    }
    tc_a64_store_result(emitter, instr, 0u);
}

/* ------------------------------------------------------------------------- */
/* Memory and pointers.                                                      */
/* ------------------------------------------------------------------------- */

static void emit_load(TcA64Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = tc_a64_type_size(emitter->ir, instr->type, &ok);
    uint64_t flags = instr->mode == TC_VOLATILE ? TC_RT_ACCESS_VOLATILE : 0;
    uint32_t type_id = tc_a64_type_id(emitter, instr->type);
    if (!ok) {
        tc_a64_error(emitter, "load of an incomplete type");
        return;
    }
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_a64_add_constant(emitter, 0u, 19u, emitter->scratch_buffer_offset);
    } else if (instr->dst.kind == TC_OP_VREG ||
               instr->dst.kind == TC_OP_FIELD) {
        TcOperand dst = instr->dst;
        tc_a64_operand_address(emitter, &dst, 0u);
    } else {
        tc_a64_error(emitter, "load has an unsupported destination");
        return;
    }
    tc_a64_load_operand(emitter, &instr->args[0].value, 1u, 0);
    tc_a64_move_immediate(emitter, 2u, size);
    tc_a64_move_immediate(emitter, 3u, flags);
    tc_a64_move_immediate(emitter, 4u, type_id);
    tc_a64_span_arguments(emitter, &instr->span, 5u);
    tc_a64_emit_call0(emitter, TC_RT_SYM_LOAD);
    if (instr->dst.kind != TC_OP_DISCARD) {
        TcOperand dst = instr->dst;
        if (instr->type->tag == TC_TY_BOOL) {
            /* A stored bool byte may be any non-zero value (standard §3.4). */
            tc_a64_operand_address(emitter, &dst, 16u);
            tc_a64_write(emitter,
                         "  ldrb w9, [x16]\n  cmp w9, #0\n  cset w9, ne\n"
                         "  strb w9, [x16]\n");
        }
        if (emitter->profile->checked_build) {
            int tracked = 0;
            if (dst.kind == TC_OP_VREG &&
                tc_a64_slot_is_object(&emitter->slots[dst.vreg])) {
                tracked = 1;
            } else if (dst.kind == TC_OP_FIELD) {
                tracked = tc_a64_slot_is_object(&emitter->slots[dst.field.base]);
            }
            if (tracked) {
                tc_a64_operand_address(emitter, &dst, 16u);
                tc_a64_define_range(emitter, 16u, size);
            }
        }
    }
}

static void emit_store(TcA64Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = tc_a64_type_size(emitter->ir, instr->type, &ok);
    uint64_t flags = instr->mode == TC_VOLATILE ? TC_RT_ACCESS_VOLATILE : 0;
    uint32_t type_id = tc_a64_type_id(emitter, instr->type);
    if (!ok) {
        tc_a64_error(emitter, "store of an incomplete type");
        return;
    }
    tc_a64_load_operand(emitter, &instr->args[0].value, 0u, 0);
    emit_operand_storage(emitter, &instr->args[1].value, 1u);
    tc_a64_move_immediate(emitter, 2u, size);
    tc_a64_move_immediate(emitter, 3u, flags);
    tc_a64_move_immediate(emitter, 4u, type_id);
    tc_a64_span_arguments(emitter, &instr->span, 5u);
    tc_a64_emit_call0(emitter, TC_RT_SYM_STORE);
}

static void emit_memmove(TcA64Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t element = tc_a64_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_a64_type_id(emitter, instr->type);
    if (!ok) {
        tc_a64_error(emitter, "memmove of an incomplete type");
        return;
    }
    tc_a64_load_operand(emitter, &instr->args[0].value, 0u, 0);
    tc_a64_load_operand(emitter, &instr->args[1].value, 1u, 0);
    tc_a64_load_operand(emitter, &instr->args[2].value, 2u, 0);
    tc_a64_move_immediate(emitter, 3u, element);
    tc_a64_move_immediate(emitter, 4u, type_id);
    tc_a64_span_arguments(emitter, &instr->span, 5u);
    tc_a64_emit_call0(emitter, TC_RT_SYM_MEMMOVE);
}

static void emit_addr(TcA64Emitter *emitter, const TcInstr *instr) {
    tc_a64_operand_address(emitter, &instr->args[0].value, 9u);
    tc_a64_store_result(emitter, instr, 9u);
}

static void emit_ptr_arith(TcA64Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t element = tc_a64_type_size(emitter->ir, instr->type, &ok);
    uint32_t type_id = tc_a64_type_id(emitter, instr->type);
    if (!ok) {
        tc_a64_error(emitter, "pointer arithmetic on an incomplete type");
        return;
    }
    tc_a64_load_operand(emitter, &instr->args[0].value, 0u, 0);
    tc_a64_load_operand(emitter, &instr->args[1].value, 1u, 0);
    tc_a64_move_immediate(emitter, 2u, element);
    tc_a64_move_immediate(emitter, 3u, instr->op == TC_PTRADD ? 1u : 0u);
    tc_a64_move_immediate(emitter, 4u, type_id);
    tc_a64_span_arguments(emitter, &instr->span, 5u);
    tc_a64_emit_call0(emitter, TC_RT_SYM_PTRADD);
    tc_a64_store_result(emitter, instr, 0u);
}

static void emit_ptrdiff(TcA64Emitter *emitter, const TcInstr *instr) {
    tc_a64_load_operand(emitter, &instr->args[0].value, 0u, 0);
    tc_a64_load_operand(emitter, &instr->args[1].value, 1u, 0);
    tc_a64_span_arguments(emitter, &instr->span, 2u);
    tc_a64_emit_call0(emitter, TC_RT_SYM_PTRDIFF);
    tc_a64_store_result(emitter, instr, 0u);
}

static void emit_funcaddr(TcA64Emitter *emitter, const TcInstr *instr) {
    uint32_t func_id;
    if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
        tc_a64_error(emitter, "funcaddr without a function operand");
        return;
    }
    func_id = instr->args[0].value.func;
    if (func_id >= emitter->ir->nfuncs) {
        tc_a64_error(emitter, "funcaddr of an invalid function");
        return;
    }
    tc_a64_write(emitter,
                 "  adrp x9, tc_function_%u\n"
                 "  add x9, x9, :lo12:tc_function_%u\n",
                 (unsigned)func_id, (unsigned)func_id);
    tc_a64_store_result(emitter, instr, 9u);
}

/* ------------------------------------------------------------------------- */
/* Aggregates.                                                               */
/* ------------------------------------------------------------------------- */

static const TcField *make_field_for_arg(TcA64Emitter *emitter,
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

static void emit_make(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcStructDef *def = tc_a64_struct_def(emitter->ir, instr->type);
    int ok = 0;
    uint64_t size = tc_a64_type_size(emitter->ir, instr->type, &ok);
    unsigned base_reg;
    size_t i;
    if (def == NULL || !ok) {
        tc_a64_error(emitter, "make of an incomplete struct type");
        return;
    }
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_a64_add_constant(emitter, 16u, 19u, emitter->scratch_buffer_offset);
    } else {
        TcOperand dst = instr->dst;
        tc_a64_operand_address(emitter, &dst, 16u);
    }
    tc_a64_write(emitter, "  mov x0, x16\n  mov x1, #0\n");
    tc_a64_move_immediate(emitter, 2u, size);
    tc_a64_emit_call0(emitter, "memset");
    base_reg = 12u;
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_a64_add_constant(emitter, base_reg, 19u,
                            emitter->scratch_buffer_offset);
    } else {
        TcOperand dst = instr->dst;
        tc_a64_operand_address(emitter, &dst, base_reg);
    }
    for (i = 0; i < instr->nargs; i++) {
        const TcArg *arg = &instr->args[i];
        const TcField *field = make_field_for_arg(emitter, def, arg, i);
        const TcType *field_type;
        int field_ok = 0;
        uint64_t field_size;
        if (field == NULL) {
            tc_a64_error(emitter, "make field does not match the struct");
            return;
        }
        field_type = field->type;
        field_size = tc_a64_type_size(emitter->ir, field_type, &field_ok);
        if (!field_ok) {
            tc_a64_error(emitter, "make field has an incomplete type");
            return;
        }
        if (field_type->tag == TC_TY_STRUCT) {
            if (arg->value.kind == TC_OP_CONST) {
                tc_a64_error(emitter, "aggregate make field is a literal");
                return;
            }
            tc_a64_operand_address(emitter, &arg->value, 17u);
            tc_a64_add_constant(emitter, 16u, base_reg, field->offset);
            tc_a64_copy_bytes(emitter, field_size);
        } else {
            tc_a64_load_operand(emitter, &arg->value, 9u, 0);
            if (field_type->tag == TC_TY_BOOL) {
                tc_a64_normalize_bool(emitter, 9u);
            } else {
                tc_a64_mask_register(emitter, 9u, field_type);
            }
            tc_a64_add_constant(emitter, 16u, base_reg, field->offset);
            store_partial_bits(emitter, 16u, 0,
                               (unsigned)(tc_a64_type_width(field_type) / 8u),
                               9u);
        }
    }
    if (emitter->profile->checked_build &&
        instr->dst.kind != TC_OP_DISCARD) {
        TcOperand dst = instr->dst;
        int tracked = 0;
        if (dst.kind == TC_OP_VREG &&
            tc_a64_slot_is_object(&emitter->slots[dst.vreg])) {
            tracked = 1;
        } else if (dst.kind == TC_OP_FIELD) {
            tracked = tc_a64_slot_is_object(&emitter->slots[dst.field.base]);
        }
        if (tracked) {
            tc_a64_operand_address(emitter, &dst, 16u);
            tc_a64_define_range(emitter, 16u, size);
        }
    }
}

static void emit_copy(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    if (type == NULL) {
        tc_a64_error(emitter, "copy without a type");
        return;
    }
    if (type->tag == TC_TY_STRUCT) {
        emit_copy_value(emitter, &instr->dst, &instr->args[0].value, type);
        if (emitter->profile->checked_build &&
            instr->dst.kind == TC_OP_VREG &&
            tc_a64_slot_is_object(&emitter->slots[instr->dst.vreg])) {
            int ok = 0;
            uint64_t size = tc_a64_type_size(emitter->ir, type, &ok);
            if (ok) {
                tc_a64_add_constant(emitter, 16u, 19u,
                                    emitter->slots[instr->dst.vreg].offset);
                tc_a64_define_range(emitter, 16u, size);
            }
        }
        return;
    }
    if (type->tag == TC_TY_BOOL) {
        tc_a64_load_operand(emitter, &instr->args[0].value, 9u, 0);
        tc_a64_normalize_bool(emitter, 9u);
    } else {
        tc_a64_load_operand(emitter, &instr->args[0].value, 9u, 0);
        tc_a64_mask_register(emitter, 9u, type);
    }
    tc_a64_store_result(emitter, instr, 9u);
}

/* ------------------------------------------------------------------------- */
/* Calls.                                                                    */
/* ------------------------------------------------------------------------- */

static void emit_depth_check_and_enter(TcA64Emitter *emitter,
                                       const TcSpan *span) {
    unsigned id = tc_a64_label(emitter);
    tc_a64_write(emitter,
                 "  adrp x9, %s\n  ldr x10, [x9, :lo12:%s]\n",
                 TC_RT_SYM_CALL_DEPTH, TC_RT_SYM_CALL_DEPTH);
    tc_a64_move_immediate(emitter, 11u, emitter->profile->depth_D);
    tc_a64_write(emitter, "  cmp x10, x11\n  b.lo .Ldepth_ok_%u\n", id);
    tc_a64_trap(emitter, span, TC_EK_STACK_OVERFLOW);
    tc_a64_write(emitter,
                 ".Ldepth_ok_%u:\n  add x10, x10, #1\n"
                 "  str x10, [x9, :lo12:%s]\n",
                 id, TC_RT_SYM_CALL_DEPTH);
}

static void emit_depth_leave(TcA64Emitter *emitter) {
    tc_a64_write(emitter,
                 "  adrp x9, %s\n  ldr x10, [x9, :lo12:%s]\n"
                 "  sub x10, x10, #1\n"
                 "  str x10, [x9, :lo12:%s]\n",
                 TC_RT_SYM_CALL_DEPTH, TC_RT_SYM_CALL_DEPTH,
                 TC_RT_SYM_CALL_DEPTH);
}

/* Place one argument in its AAPCS64 location. */
static void emit_argument(TcA64Emitter *emitter, const TcArg *arg,
                          const TcType *type, const TcA64ArgSlot *location) {
    int ok = 0;
    uint64_t size = tc_a64_type_size(emitter->ir, type, &ok);
    if (!ok) {
        tc_a64_error(emitter, "call argument has an incomplete type");
        return;
    }
    if (location->indirect) {
        if (arg->value.kind == TC_OP_CONST) {
            tc_a64_error(emitter, "aggregate constant argument has no address");
            return;
        }
        tc_a64_add_constant(emitter, 15u, 31u, location->copy_offset);
        tc_a64_operand_address(emitter, &arg->value, 17u);
        tc_a64_write(emitter, "  mov x16, x15\n");
        tc_a64_copy_bytes(emitter, size);
        if (location->kind == TC_A64_LOC_XREG) {
            tc_a64_write(emitter, "  mov x%u, x15\n", location->index);
        } else {
            tc_a64_add_constant(emitter, 16u, 31u, location->stack_offset);
            tc_a64_write(emitter, "  str x15, [x16]\n");
        }
        return;
    }
    if (location->kind == TC_A64_LOC_STACK) {
        if (type->tag == TC_TY_STRUCT) {
            tc_a64_operand_address(emitter, &arg->value, 17u);
            tc_a64_add_constant(emitter, 16u, 31u, location->stack_offset);
            tc_a64_copy_bytes(emitter, size);
        } else {
            tc_a64_load_operand(emitter, &arg->value, 9u, 0);
            tc_a64_add_constant(emitter, 16u, 31u, location->stack_offset);
            tc_a64_write(emitter, "  str x9, [x16]\n");
        }
        return;
    }
    if (location->kind == TC_A64_LOC_VREG) {
        if (location->hfa) {
            unsigned k;
            unsigned elem = location->hfa32 ? 4u : 8u;
            tc_a64_operand_address(emitter, &arg->value, 17u);
            for (k = 0; k < location->hfa_count; k++) {
                load_partial_bits(emitter, 17u, (uint64_t)k * elem, elem,
                                  16u);
                if (location->hfa32) {
                    tc_a64_write(emitter, "  fmov s%u, w16\n",
                                 location->index + k);
                } else {
                    tc_a64_write(emitter, "  fmov d%u, x16\n",
                                 location->index + k);
                }
            }
        } else {
            tc_a64_load_operand(emitter, &arg->value, 16u, 0);
            if (size == 4u) {
                tc_a64_write(emitter, "  fmov s%u, w16\n", location->index);
            } else {
                tc_a64_write(emitter, "  fmov d%u, x16\n", location->index);
            }
        }
        return;
    }
    /* x registers. */
    if (type->tag == TC_TY_STRUCT) {
        uint64_t written = 0;
        unsigned chunk = location->index;
        tc_a64_operand_address(emitter, &arg->value, 17u);
        while (written < size) {
            uint64_t remaining = size - written;
            unsigned bytes = (unsigned)(remaining >= 8u ? 8u : remaining);
            load_partial_bits(emitter, 17u, written, bytes, 16u);
            tc_a64_write(emitter, "  mov x%u, x16\n", chunk);
            written += 8u;
            chunk++;
        }
    } else {
        tc_a64_load_operand(emitter, &arg->value, location->index, 0);
    }
}

static void store_call_result(TcA64Emitter *emitter, const TcInstr *instr,
                              const TcType *ret) {
    TcA64RetPlan plan;
    int ok = 0;
    uint64_t size;
    tc_a64_plan_return(emitter, ret, &plan);
    if (plan.kind == TC_A64_RET_VOID) {
        return;
    }
    size = tc_a64_type_size(emitter->ir, ret, &ok);
    if (!ok) {
        return;
    }
    if (plan.kind == TC_A64_RET_INDIRECT) {
        return; /* the callee wrote into the provided buffer */
    }
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    {
        TcOperand dst = instr->dst;
        if (plan.kind == TC_A64_RET_XREGS) {
            if (ret->tag != TC_TY_STRUCT) {
                if (ret->tag == TC_TY_BOOL) {
                    tc_a64_normalize_bool(emitter, 0u);
                } else {
                    tc_a64_mask_register(emitter, 0u, ret);
                }
                tc_a64_store_result(emitter, instr, 0u);
                return;
            }
            tc_a64_operand_address(emitter, &dst, 17u);
            {
                uint64_t written = 0;
                unsigned reg = 0;
                while (written < size) {
                    uint64_t remaining = size - written;
                    unsigned bytes = (unsigned)(remaining >= 8u ? 8u : remaining);
                    store_partial_bits(emitter, 17u, written, bytes, reg);
                    written += 8u;
                    reg++;
                }
            }
            return;
        }
        /* v registers. */
        if (!plan.hfa_count) {
            tc_a64_operand_address(emitter, &dst, 17u);
            if (size == 4u) {
                tc_a64_write(emitter, "  fmov w9, s0\n");
            } else {
                tc_a64_write(emitter, "  fmov x9, d0\n");
            }
            store_partial_bits(emitter, 17u, 0, (unsigned)size, 9u);
            return;
        }
        tc_a64_operand_address(emitter, &dst, 17u);
        {
            unsigned k;
            unsigned elem = plan.hfa32 ? 4u : 8u;
            for (k = 0; k < plan.hfa_count; k++) {
                if (plan.hfa32) {
                    tc_a64_write(emitter, "  fmov w9, s%u\n", k);
                } else {
                    tc_a64_write(emitter, "  fmov x9, d%u\n", k);
                }
                store_partial_bits(emitter, 17u, (uint64_t)k * elem, elem,
                                   9u);
            }
        }
    }
}

static void emit_call(TcA64Emitter *emitter, const TcInstr *instr) {
    uint32_t func_id;
    const TcFunc *callee;
    TcA64ArgPlan plan;
    const TcType **params = NULL;
    TcA64RetPlan ret_plan;
    size_t nargs;
    size_t i;
    int indirect = instr->op == TC_CALL_INDIRECT;
    const TcType *instr_signature = NULL;
    if (indirect) {
        const TcType *signature;
        if (instr->nargs < 1) {
            tc_a64_error(emitter, "call_indirect without a function pointer");
            return;
        }
        signature = tc_a64_value_type(emitter, &instr->args[0].value);
        if (signature == NULL || signature->tag != TC_TY_FNPTR) {
            tc_a64_error(emitter, "call_indirect without a function pointer type");
            return;
        }
        nargs = signature->fnptr.n;
        if (instr->nargs - 1u != nargs) {
            tc_a64_error(emitter, "call_indirect argument count mismatch");
            return;
        }
        if (nargs != 0) {
            params = (const TcType **)calloc(nargs, sizeof(*params));
            if (params == NULL) {
                tc_a64_error(emitter, "out of memory");
                return;
            }
            for (i = 0; i < nargs; i++) {
                params[i] = signature->fnptr.params[i];
            }
        }
        callee = NULL;
        func_id = 0;
        tc_a64_plan_return(emitter, signature->fnptr.ret, &ret_plan);
        /* Record the signature for the post-call result store. */
        instr_signature = signature;
    } else {
        if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
            tc_a64_error(emitter, "direct call without a function operand");
            return;
        }
        func_id = instr->args[0].value.func;
        if (func_id >= emitter->ir->nfuncs) {
            tc_a64_error(emitter, "direct call to an invalid function");
            return;
        }
        callee = &emitter->ir->funcs[func_id];
        nargs = callee->sig.nparams;
        if (instr->nargs - 1u != nargs) {
            tc_a64_error(emitter, "call argument count mismatch");
            return;
        }
        if (nargs != 0) {
            params = (const TcType **)calloc(nargs, sizeof(*params));
            if (params == NULL) {
                tc_a64_error(emitter, "out of memory");
                return;
            }
            for (i = 0; i < nargs; i++) {
                params[i] = callee->sig.params[i].type;
            }
        }
        tc_a64_plan_return(emitter, callee->sig.ret, &ret_plan);
    }
    tc_a64_plan_arguments(emitter, params, nargs, &plan);
    if (emitter->failed) {
        free(params);
        tc_a64_free_argument_plan(&plan);
        return;
    }
    if (plan.stack_bytes != 0) {
        tc_a64_move_immediate(emitter, 16u, plan.stack_bytes);
        tc_a64_write(emitter, "  sub sp, sp, x16\n");
    }
    for (i = 0; i < nargs; i++) {
        emit_argument(emitter, &instr->args[i + 1u], params[i], &plan.slots[i]);
        if (emitter->failed) {
            free(params);
            tc_a64_free_argument_plan(&plan);
            return;
        }
    }
    free(params);
    tc_a64_free_argument_plan(&plan);
    if (ret_plan.kind == TC_A64_RET_INDIRECT) {
        if (instr->dst.kind == TC_OP_DISCARD) {
            tc_a64_add_constant(emitter, 8u, 19u,
                                emitter->scratch_buffer_offset);
        } else {
            TcOperand dst = instr->dst;
            tc_a64_operand_address(emitter, &dst, 8u);
        }
    }
    if (indirect) {
        tc_a64_load_operand(emitter, &instr->args[0].value, 16u, 0);
        {
            unsigned id = tc_a64_label(emitter);
            tc_a64_write(emitter, "  cbnz x16, .Lfp_ok_%u\n", id);
            tc_a64_trap(emitter, &instr->span, TC_EK_NULL_FUNCTION_POINTER);
            tc_a64_write(emitter, ".Lfp_ok_%u:\n", id);
        }
    }
    emit_depth_check_and_enter(emitter, &instr->span);
    if (indirect) {
        tc_a64_write(emitter, "  blr x16\n");
    } else {
        tc_a64_write(emitter, "  bl tc_function_%u\n", (unsigned)func_id);
    }
    emit_depth_leave(emitter);
    if (plan.stack_bytes != 0) {
        tc_a64_move_immediate(emitter, 16u, plan.stack_bytes);
        tc_a64_write(emitter, "  add sp, sp, x16\n");
    }
    store_call_result(emitter, instr, indirect ? instr_signature->fnptr.ret
                                               : callee->sig.ret);
}

/* ------------------------------------------------------------------------- */
/* Return.                                                                   */
/* ------------------------------------------------------------------------- */

static void emit_return(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *ret = NULL;
    TcA64RetPlan plan;
    if (emitter->unit->func_id != TC_UNIT_TOP_LEVEL &&
        emitter->unit->func_id < emitter->ir->nfuncs) {
        ret = emitter->ir->funcs[emitter->unit->func_id].sig.ret;
    }
    if (instr->nargs == 0) {
        tc_a64_write(emitter, "  b .Lreturn_%zu\n", emitter->unit_index);
        return;
    }
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        tc_a64_error(emitter, "return value in a void unit");
        return;
    }
    tc_a64_plan_return(emitter, ret, &plan);
    {
        const TcOperand *value = &instr->args[0].value;
        int ok = 0;
        uint64_t size = tc_a64_type_size(emitter->ir, ret, &ok);
        if (!ok) {
            tc_a64_error(emitter, "return of an incomplete type");
            return;
        }
        if (plan.kind == TC_A64_RET_XREGS && ret->tag != TC_TY_STRUCT) {
            tc_a64_load_operand(emitter, value, 0u, 0);
            if (ret->tag == TC_TY_BOOL) {
                tc_a64_normalize_bool(emitter, 0u);
            } else {
                tc_a64_mask_register(emitter, 0u, ret);
            }
        } else if (plan.kind == TC_A64_RET_XREGS) {
            uint64_t written = 0;
            unsigned reg = 0;
            tc_a64_operand_address(emitter, value, 17u);
            while (written < size) {
                uint64_t remaining = size - written;
                unsigned bytes = (unsigned)(remaining >= 8u ? 8u : remaining);
                load_partial_bits(emitter, 17u, written, bytes, reg);
                written += 8u;
                reg++;
            }
        } else if (plan.kind == TC_A64_RET_VREGS) {
            if (plan.hfa_count == 0) {
                tc_a64_load_operand(emitter, value, 16u, 0);
                if (size == 4u) {
                    tc_a64_write(emitter, "  fmov s0, w16\n");
                } else {
                    tc_a64_write(emitter, "  fmov d0, x16\n");
                }
            } else {
                unsigned k;
                unsigned elem = plan.hfa32 ? 4u : 8u;
                tc_a64_operand_address(emitter, value, 17u);
                for (k = 0; k < plan.hfa_count; k++) {
                    load_partial_bits(emitter, 17u, (uint64_t)k * elem, elem,
                                      16u);
                    if (plan.hfa32) {
                        tc_a64_write(emitter, "  fmov s%u, w16\n", k);
                    } else {
                        tc_a64_write(emitter, "  fmov d%u, x16\n", k);
                    }
                }
            }
        } else {
            /* Indirect return: copy into the caller-provided buffer. */
            tc_a64_load_bits(emitter, 19u, emitter->sret_offset, 8u, 16u);
            tc_a64_operand_address(emitter, value, 17u);
            /* copy_bytes consumes x16 (destination) and x17 (source). */
            tc_a64_copy_bytes(emitter, size);
        }
    }
    tc_a64_write(emitter, "  b .Lreturn_%zu\n", emitter->unit_index);
}

/* ------------------------------------------------------------------------- */
/* Control flow and standard I/O.                                            */
/* ------------------------------------------------------------------------- */

static void emit_branch(TcA64Emitter *emitter, const TcInstr *instr) {
    if (instr->args[0].value.kind != TC_OP_LABEL ||
        instr->args[0].value.label >= emitter->unit->nblocks) {
        tc_a64_error(emitter, "invalid branch label");
        return;
    }
    tc_a64_write(emitter, "  b .L%zu_block%u\n", emitter->unit_index,
                 instr->args[0].value.label);
}

static void emit_branch_if(TcA64Emitter *emitter, const TcInstr *instr) {
    if (instr->args[1].value.kind != TC_OP_LABEL ||
        instr->args[1].value.label >= emitter->unit->nblocks) {
        tc_a64_error(emitter, "invalid conditional branch label");
        return;
    }
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, 0);
    tc_a64_write(emitter, "  cbnz x9, .L%zu_block%u\n", emitter->unit_index,
                 instr->args[1].value.label);
}

static void emit_switch(TcA64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = tc_a64_value_type(emitter, &instr->args[0].value);
    size_t ntargets = instr->ntargets;
    size_t i;
    unsigned id = tc_a64_label(emitter);
    unsigned width = tc_a64_type_width(type);
    size_t ndispatch;
    if (!tc_type_is_integer(type) || ntargets == 0) {
        tc_a64_error(emitter, "switch requires an integer selector and default");
        return;
    }
    ndispatch = ntargets - 1u;
    tc_a64_load_operand(emitter, &instr->args[0].value, 9u, 0);
    if (tc_type_is_signed_integer(type)) {
        tc_a64_write(emitter, "  tbnz %s9, #%u, .Lswitch_default_%u\n",
                     width == 64u ? "x" : "w", width - 1u, id);
    }
    if (ndispatch == 0) {
        tc_a64_write(emitter, "  b .Lswitch_default_%u\n", id);
    } else {
        if (ndispatch <= 4095u) {
            tc_a64_write(emitter, "  cmp x9, #%zu\n", ndispatch);
        } else {
            tc_a64_move_immediate(emitter, 16u, ndispatch);
            tc_a64_write(emitter, "  cmp x9, x16\n");
        }
        tc_a64_write(emitter, "  b.hs .Lswitch_default_%u\n", id);
        for (i = 0; i < ndispatch; i++) {
            if (i <= 4095u) {
                tc_a64_write(emitter, "  cmp x9, #%zu\n", i);
            } else {
                tc_a64_move_immediate(emitter, 16u, i);
                tc_a64_write(emitter, "  cmp x9, x16\n");
            }
            tc_a64_write(emitter, "  b.eq .Lswitch_%u_case_%zu\n", id, i);
        }
        tc_a64_write(emitter, "  b .Lswitch_default_%u\n", id);
    }
    tc_a64_write(emitter, ".Lswitch_default_%u:\n  b .L%zu_block%u\n", id,
                 emitter->unit_index, instr->switch_targets[ntargets - 1u]);
    for (i = 0; i < ndispatch; i++) {
        tc_a64_write(emitter,
                     ".Lswitch_%u_case_%zu:\n  b .L%zu_block%u\n", id, i,
                     emitter->unit_index, instr->switch_targets[i]);
    }
}

static void emit_write(TcA64Emitter *emitter, const TcInstr *instr) {
    unsigned id = tc_a64_label(emitter);
    unsigned tag = (unsigned)instr->type->tag;
    tc_a64_move_immediate(emitter, 0u, tag);
    tc_a64_move_immediate(emitter, 1u, instr->op == TC_WRITELN ? 1u : 0u);
    tc_a64_load_operand(emitter, &instr->args[0].value, 2u, 0);
    tc_a64_emit_call0(emitter, TC_RT_SYM_WRITE);
    tc_a64_write(emitter, "  cbz w0, .Lio_ok_%u\n", id);
    tc_a64_trap(emitter, &instr->span, TC_EK_IO);
    tc_a64_write(emitter, ".Lio_ok_%u:\n", id);
}

static void emit_read(TcA64Emitter *emitter, const TcInstr *instr) {
    unsigned id = tc_a64_label(emitter);
    unsigned tag = (unsigned)instr->type->tag;
    int ok = 0;
    uint64_t size = tc_a64_type_size(emitter->ir, instr->type, &ok);
    if (!ok) {
        tc_a64_error(emitter, "read of an incomplete type");
        return;
    }
    tc_a64_move_immediate(emitter, 0u, tag);
    tc_a64_add_constant(emitter, 1u, 19u, emitter->read_scratch_offset);
    tc_a64_emit_call0(emitter, TC_RT_SYM_READ);
    tc_a64_write(emitter, "  cbz w0, .Lio_ok_%u\n", id);
    tc_a64_trap(emitter, &instr->span, TC_EK_IO);
    tc_a64_write(emitter, ".Lio_ok_%u:\n", id);
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    {
        TcOperand dst = instr->dst;
        tc_a64_load_bits(emitter, 19u, emitter->read_scratch_offset,
                         (unsigned)size, 9u);
        if (instr->type->tag == TC_TY_BOOL) {
            tc_a64_normalize_bool(emitter, 9u);
        }
        tc_a64_store_result(emitter, instr, 9u);
        if (emitter->profile->checked_build) {
            int tracked = 0;
            if (dst.kind == TC_OP_VREG &&
                tc_a64_slot_is_object(&emitter->slots[dst.vreg])) {
                tracked = 1;
            } else if (dst.kind == TC_OP_FIELD) {
                tracked = tc_a64_slot_is_object(&emitter->slots[dst.field.base]);
            }
            if (tracked) {
                tc_a64_operand_address(emitter, &dst, 16u);
                tc_a64_define_range(emitter, 16u, size);
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Dispatch.                                                                 */
/* ------------------------------------------------------------------------- */

static int type_is_float_arith(const TcType *type) {
    return tc_type_is_float(type);
}

void tc_a64_emit_instruction(TcA64Emitter *emitter, const TcInstr *instr) {
    if (emitter->failed) {
        return;
    }
    switch (instr->op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_NEG:
    case TC_ABS:
    case TC_DIV:
    case TC_MOD:
    case TC_SHL:
    case TC_SHR:
        if (type_is_float_arith(instr->type)) {
            if (instr->op == TC_NEG || instr->op == TC_ABS) {
                emit_float_unop(emitter, instr);
            } else {
                emit_float_binop(emitter, instr);
            }
        } else if (instr->op == TC_ADD || instr->op == TC_SUB) {
            emit_add_sub(emitter, instr);
        } else if (instr->op == TC_MUL) {
            emit_mul(emitter, instr);
        } else if (instr->op == TC_NEG || instr->op == TC_ABS) {
            emit_neg_abs(emitter, instr);
        } else if (instr->op == TC_DIV || instr->op == TC_MOD) {
            emit_div_mod(emitter, instr);
        } else {
            emit_shift(emitter, instr);
        }
        break;
    case TC_AND:
    case TC_OR:
    case TC_XOR:
        emit_bitwise(emitter, instr);
        break;
    case TC_NOT:
        emit_not(emitter, instr);
        break;
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        if (type_is_float_arith(instr->type)) {
            emit_float_compare(emitter, instr);
        } else {
            emit_compare_int(emitter, instr);
        }
        break;
    case TC_PTREQ:
    case TC_PTRNE:
    case TC_PTRLT:
    case TC_PTRLE:
    case TC_PTRGT:
    case TC_PTRGE:
    case TC_FUNCEQ:
    case TC_FUNCNE:
        emit_ptr_compare(emitter, instr);
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
    case TC_SIZEOF: {
        int ok = 0;
        uint64_t size = tc_a64_type_size(emitter->ir, instr->type, &ok);
        if (!ok) {
            tc_a64_error(emitter, "sizeof of an incomplete type");
            break;
        }
        tc_a64_move_immediate(emitter, 11u, size);
        tc_a64_store_result(emitter, instr, 11u);
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
        tc_a64_trap(emitter, &instr->span, TC_EK_TRAP);
        break;
    case TC_READ:
        emit_read(emitter, instr);
        break;
    case TC_WRITE:
    case TC_WRITELN:
        emit_write(emitter, instr);
        break;
    default:
        tc_a64_error(emitter, "unsupported opcode %d", (int)instr->op);
        break;
    }
}
