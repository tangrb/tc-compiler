/* TC 0.0.46 RISC-V 32 (ILP32) argument and return classification
 * (design §10.2).
 *
 * Base integer calling convention rules used here:
 *   - integer/pointer/function-pointer/bool arguments use a0-a7, then the
 *     stack, in order;
 *   - a 64-bit fundamental scalar occupies an aligned register pair, or one
 *     register plus one stack word when exactly one register remains, or two
 *     stack words;
 *   - a composite of at most 2*XLEN bytes is passed by value in as many
 *     XLEN-bit words; when the remaining registers cannot hold it, it is
 *     passed by value on the stack without consuming the remaining registers;
 *   - a composite larger than 2*XLEN is passed by reference (one pointer
 *     word); the caller copies the value to a per-call area;
 *   - stack arguments are four-byte aligned, or aligned to the value's
 *     natural alignment (eight for 64-bit scalars and align-8 composites);
 *   - an indirect composite return uses a0 as the hidden result pointer,
 *     shifting the first parameter word to a1;
 *   - void returns use no register, <=32-bit results use a0, 64-bit results
 *     and 5..8-byte composites use a0:a1, and larger composites are written
 *     through the hidden pointer.
 */
#include "backend/riscv32/emit_internal.h"

#include <stdlib.h>
#include <string.h>

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static void plan_one(TcRiscv32Emitter *emitter, const TcType *type,
                     unsigned *ncrn, uint64_t *nsaa,
                     TcRiscv32ArgSlot *slot) {
    int ok = 0;
    uint64_t size = tc_riscv32_type_size(emitter->ir, type, &ok);
    unsigned width;
    unsigned words;
    unsigned align;
    memset(slot, 0, sizeof(*slot));
    if (!ok) {
        tc_riscv32_error(emitter, "argument has an incomplete type");
        return;
    }
    width = tc_riscv32_type_width(type);
    slot->size = size;
    if (type->tag == TC_TY_STRUCT) {
        if (size > 8u) {
            slot->by_reference = 1;
            slot->words = 1;
            if (*ncrn < 8u) {
                slot->kind = TC_RISCV32_ARG_REG;
                slot->reg = *ncrn;
                *ncrn += 1u;
            } else {
                slot->kind = TC_RISCV32_ARG_STACK;
                slot->stack_offset = *nsaa;
                *nsaa += 4u;
            }
            return;
        }
        slot->struct_value = 1;
        words = (unsigned)((size + 3u) / 4u);
        if (words == 0) {
            words = 1;
        }
        slot->words = words;
        if (*ncrn + words <= 8u) {
            slot->kind = TC_RISCV32_ARG_REG;
            slot->reg = *ncrn;
            slot->reg_words = words;
            *ncrn += words;
            return;
        }
        if (*ncrn == 7u && words == 2u) {
            /* "If only one register is available, the first XLEN bits are
             * passed in a register and the remaining bits are passed on the
             * stack." */
            slot->kind = TC_RISCV32_ARG_REG;
            slot->reg = 7u;
            slot->reg_words = 1u;
            slot->stack_words = 1u;
            slot->stack_byte_offset = 4u;
            slot->stack_offset = *nsaa;
            *nsaa += 4u;
            *ncrn = 8u;
            return;
        }
        /* TC structs are byte-packed (standard §3.7); the base RISC-V
         * convention gives by-value aggregates a four-byte stack slot. */
        align = 4u;
        *nsaa = align_up(*nsaa, align);
        slot->kind = TC_RISCV32_ARG_STACK;
        slot->stack_offset = *nsaa;
        slot->stack_words = words;
        slot->stack_byte_offset = 0;
        *nsaa += (uint64_t)words * 4u;
        return;
    }
    if (width > 32u) {
        slot->is64 = 1;
        slot->words = 2;
        if (*ncrn + 2u <= 8u) {
            slot->kind = TC_RISCV32_ARG_REG;
            slot->reg = *ncrn;
            *ncrn += 2u;
            return;
        }
        if (*ncrn == 7u) {
            slot->kind = TC_RISCV32_ARG_REG;
            slot->reg = 7u;
            slot->split64 = 1;
            slot->stack_offset = *nsaa;
            *nsaa += 4u;
            *ncrn = 8u;
            return;
        }
        *nsaa = align_up(*nsaa, 8u);
        slot->kind = TC_RISCV32_ARG_STACK;
        slot->stack_offset = *nsaa;
        *nsaa += 8u;
        return;
    }
    slot->words = 1;
    if (*ncrn < 8u) {
        slot->kind = TC_RISCV32_ARG_REG;
        slot->reg = *ncrn;
        *ncrn += 1u;
        return;
    }
    slot->kind = TC_RISCV32_ARG_STACK;
    slot->stack_offset = *nsaa;
    *nsaa += 4u;
}

void tc_riscv32_plan_arguments(TcRiscv32Emitter *emitter,
                               const TcType **param_types, size_t count,
                               int start_word, TcRiscv32ArgPlan *out) {
    unsigned ncrn = start_word != 0 ? 1u : 0u;
    uint64_t nsaa = 0;
    size_t i;
    memset(out, 0, sizeof(*out));
    out->alloc = emitter->alloc;
    out->sret = start_word != 0;
    out->count = count;
    if (count != 0) {
        out->slots =
            (TcRiscv32ArgSlot *)tc_allocator_calloc(emitter->alloc, count, sizeof(*out->slots));
        if (out->slots == NULL) {
            tc_riscv32_oom(emitter, "out of memory planning arguments");
            return;
        }
    }
    for (i = 0; i < count; i++) {
        plan_one(emitter, param_types[i], &ncrn, &nsaa, &out->slots[i]);
        if (emitter->failed) {
            return;
        }
    }
    out->stack_bytes = nsaa;
    out->outgoing_size = align_up(nsaa, 16u);
}

void tc_riscv32_plan_return(TcRiscv32Emitter *emitter, const TcType *ret,
                            TcRiscv32RetPlan *out) {
    int ok = 0;
    uint64_t size;
    memset(out, 0, sizeof(*out));
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        out->kind = TC_RISCV32_RET_VOID;
        return;
    }
    size = tc_riscv32_type_size(emitter->ir, ret, &ok);
    if (!ok) {
        tc_riscv32_error(emitter, "return type is incomplete");
        out->kind = TC_RISCV32_RET_VOID;
        return;
    }
    out->size = size;
    if (ret->tag == TC_TY_STRUCT) {
        if (size > 8u) {
            out->kind = TC_RISCV32_RET_INDIRECT;
        } else if (size > 4u) {
            out->kind = TC_RISCV32_RET_PAIR;
        } else {
            out->kind = TC_RISCV32_RET_WORD;
        }
        return;
    }
    out->kind = tc_riscv32_type_width(ret) > 32u
                    ? TC_RISCV32_RET_PAIR
                    : TC_RISCV32_RET_WORD;
}

void tc_riscv32_free_argument_plan(TcRiscv32ArgPlan *plan) {
    const TcAllocator *alloc;
    if (plan == NULL) {
        return;
    }
    alloc = plan->alloc != NULL ? plan->alloc : tc_allocator_libc();
    alloc->free(alloc->ctx, plan->slots);
    plan->slots = NULL;
    plan->alloc = NULL;
    plan->count = 0;
}
