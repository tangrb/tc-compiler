/* TC 0.0.46 ARM32 AAPCS32/ILP32 argument and return classification (design
 * §10.2).
 *
 * Base AAPCS32 (softfp) rules used here:
 *   - integer/pointer/function-pointer/bool arguments use r0-r3, then the
 *     stack, in order;
 *   - 64-bit fundamental scalars (int64/uint64/float64) occupy two core
 *     register words with even register alignment or an eight-byte-aligned
 *     stack pair;
 *   - a composite argument is copied as a sequence of 32-bit words into the
 *     remaining core registers (Rule C.4); when it does not fit and no stack
 *     argument precedes it, the first words go into the remaining registers
 *     and the rest to the stack (Rule C.5); otherwise it goes to the stack
 *     (Rules C.6-C.8).  Its marshalled size is rounded up to a multiple of
 *     four and it is four-byte aligned because TC layout is compact;
 *   - once an argument is placed on the stack, every later argument is on the
 *     stack as well;
 *   - an indirect composite return result uses r0 as the hidden result
 *     pointer, shifting the first parameter word to r1;
 *   - void returns use no register, four-byte results use r0, 64-bit results
 *     use r0:r1, and larger composites are written through the hidden pointer.
 */
#include "backend/arm32/emit_internal.h"

#include <stdlib.h>
#include <string.h>

static void plan_one(TcArm32Emitter *emitter, const TcType *type,
                     unsigned *ncrn, uint64_t *nsaa, TcArm32ArgSlot *slot) {
    int ok = 0;
    uint64_t size = tc_arm32_type_size(emitter->ir, type, &ok);
    unsigned width;
    unsigned words;
    int is64 = 0;
    int struct_value = 0;
    unsigned candidate;
    memset(slot, 0, sizeof(*slot));
    if (!ok) {
        tc_arm32_error(emitter, "argument has an incomplete type");
        return;
    }
    width = tc_arm32_type_width(type);
    slot->size = size;
    if (type->tag == TC_TY_STRUCT) {
        struct_value = 1;
        words = (unsigned)((size + 3u) / 4u);
        if (words == 0) {
            words = 1;
        }
    } else if (width > 32u) {
        is64 = 1;
        words = 2;
    } else {
        words = 1;
    }
    slot->is64 = is64;
    slot->struct_value = struct_value;
    slot->words = words;
    if (*nsaa == 0 && *ncrn < 4u) {
        candidate = *ncrn;
        if (is64) {
            candidate = (candidate + 1u) & ~1u;
        }
        if (candidate + words <= 4u) {
            slot->kind = TC_ARM32_ARG_REG;
            slot->reg = candidate;
            slot->reg_words = words;
            *ncrn = candidate + words;
            return;
        }
        if (struct_value && *ncrn < 4u) {
            /* Rule C.5: split the composite between core registers and the
             * stack.  Fundamental double-word values are 8-byte aligned and
             * therefore never split. */
            unsigned reg_words = 4u - *ncrn;
            slot->kind = TC_ARM32_ARG_REG;
            slot->reg = *ncrn;
            slot->reg_words = reg_words;
            slot->stack_words = words - reg_words;
            slot->stack_byte_offset = (uint64_t)reg_words * 4u;
            slot->stack_offset = *nsaa;
            *ncrn = 4u;
            *nsaa += (uint64_t)slot->stack_words * 4u;
            return;
        }
    }
    /* Rules C.6-C.8: the whole argument goes to the stack. */
    if (is64) {
        *nsaa = (*nsaa + 7u) & ~UINT64_C(7);
    } else {
        *nsaa = (*nsaa + 3u) & ~UINT64_C(3);
    }
    slot->kind = TC_ARM32_ARG_STACK;
    slot->stack_offset = *nsaa;
    slot->stack_words = words;
    slot->stack_byte_offset = 0;
    *nsaa += (uint64_t)words * 4u;
}

void tc_arm32_plan_arguments(TcArm32Emitter *emitter,
                             const TcType **param_types, size_t count,
                             int start_word, TcArm32ArgPlan *out) {
    unsigned ncrn = start_word != 0 ? 1u : 0u;
    uint64_t nsaa = 0;
    size_t i;
    memset(out, 0, sizeof(*out));
    out->alloc = emitter->alloc;
    out->sret = start_word != 0;
    out->count = count;
    if (count != 0) {
        out->slots = (TcArm32ArgSlot *)tc_allocator_calloc(emitter->alloc, count, sizeof(*out->slots));
        if (out->slots == NULL) {
            tc_arm32_oom(emitter, "out of memory planning arguments");
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
    out->outgoing_size = (nsaa + 7u) & ~UINT64_C(7);
}

void tc_arm32_plan_return(TcArm32Emitter *emitter, const TcType *ret,
                          TcArm32RetPlan *out) {
    int ok = 0;
    uint64_t size;
    memset(out, 0, sizeof(*out));
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        out->kind = TC_ARM32_RET_VOID;
        return;
    }
    size = tc_arm32_type_size(emitter->ir, ret, &ok);
    if (!ok) {
        tc_arm32_error(emitter, "return type is incomplete");
        out->kind = TC_ARM32_RET_VOID;
        return;
    }
    out->size = size;
    if (ret->tag == TC_TY_STRUCT) {
        out->kind = size <= 4u ? TC_ARM32_RET_WORD : TC_ARM32_RET_INDIRECT;
        return;
    }
    out->kind = tc_arm32_type_width(ret) > 32u ? TC_ARM32_RET_PAIR
                                               : TC_ARM32_RET_WORD;
}

void tc_arm32_free_argument_plan(TcArm32ArgPlan *plan) {
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
