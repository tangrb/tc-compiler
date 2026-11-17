/* TC 0.0.46 AArch64 AAPCS64 argument and return classification (design
 * §10.2).  The backend is the only producer and consumer of these calls in a
 * 0.0.46 image (no C host library is exposed), but the classification follows
 * the AAPCS64 rules so the ABI stays checkable and portable:
 *
 *   - integer/pointer/function-pointer/bool arguments use x0-x7;
 *   - float32/float64 arguments use v0-v7;
 *   - a homogeneous float aggregate (1-4 members of one float type) uses one
 *     v register per member;
 *   - other composites of at most 16 bytes use consecutive x registers;
 *   - larger aggregates and exhausted register classes are passed by value on
 *     the stack, eight-byte aligned;
 *   - returns use x0/x1, v0-v3, or an x8 indirect result pointer.
 */
#include "backend/aarch64/emit_internal.h"

#include <stdlib.h>
#include <string.h>

/* Recursively flatten a struct into HFA members.  `base` is the member's
 * absolute offset in the aggregate and `cursor` is the end of the previously
 * flattened member, so gaps make the aggregate non-homogeneous. */
static int flatten_hfa(const TcIR *ir, const TcType *type, uint64_t base,
                       uint64_t *cursor, int *out_is32, unsigned *out_count) {
    const TcStructDef *def;
    size_t i;
    if (type->tag == TC_TY_F32 || type->tag == TC_TY_F64) {
        int is32 = type->tag == TC_TY_F32;
        if (base != *cursor) {
            return 0;
        }
        if (*out_count == 0) {
            *out_is32 = is32;
        } else if (*out_is32 != is32) {
            return 0;
        }
        if (*out_count >= 4) {
            return 0;
        }
        (*out_count)++;
        *cursor += is32 ? 4u : 8u;
        return 1;
    }
    if (type->tag != TC_TY_STRUCT) {
        return 0;
    }
    def = tc_a64_struct_def(ir, type);
    if (def == NULL || def->nfields == 0) {
        return 0;
    }
    {
        uint64_t local = 0;
        for (i = 0; i < def->nfields; i++) {
            const TcField *field = &def->fields[i];
            int ok = 0;
            uint64_t field_size;
            if (field->offset != local) {
                return 0;
            }
            if (!flatten_hfa(ir, field->type, base + field->offset, cursor,
                             out_is32, out_count)) {
                return 0;
            }
            field_size = tc_a64_type_size(ir, field->type, &ok);
            if (!ok) {
                return 0;
            }
            local += field_size;
        }
    }
    return 1;
}

int tc_a64_plan_hfa(const TcIR *ir, const TcType *type, int *out_is32,
                    unsigned *out_count) {
    uint64_t cursor = 0;
    unsigned count = 0;
    int is32 = 0;
    int ok = 0;
    uint64_t size;
    if (type == NULL || type->tag != TC_TY_STRUCT) {
        return 0;
    }
    if (!flatten_hfa(ir, type, 0, &cursor, &is32, &count)) {
        return 0;
    }
    size = tc_a64_type_size(ir, type, &ok);
    if (!ok || cursor != size) {
        return 0;
    }
    *out_is32 = is32;
    *out_count = count;
    return count >= 1 && count <= 4;
}

void tc_a64_plan_arguments(TcA64Emitter *emitter, const TcType **param_types,
                           size_t count, TcA64ArgPlan *out) {
    unsigned nsrn = 0;
    unsigned ngrn = 0;
    uint64_t nsaa = 0;
    size_t i;
    memset(out, 0, sizeof(*out));
    out->count = count;
    if (count != 0) {
        out->slots = (TcA64ArgSlot *)calloc(count, sizeof(*out->slots));
        if (out->slots == NULL) {
            tc_a64_error(emitter, "out of memory planning arguments");
            return;
        }
    }
    for (i = 0; i < count; i++) {
        const TcType *type = param_types[i];
        TcA64ArgSlot *slot = &out->slots[i];
        int ok = 0;
        uint64_t size = tc_a64_type_size(emitter->ir, type, &ok);
        int is32 = 0;
        unsigned hfa = 0;
        slot->size = size;
        if (!ok) {
            tc_a64_error(emitter, "argument %u has an incomplete type",
                         (unsigned)i);
            return;
        }
        if (tc_type_is_float(type)) {
            unsigned bytes = (unsigned)(size);
            if (nsrn < 8u) {
                slot->kind = TC_A64_LOC_VREG;
                slot->index = nsrn++;
                slot->size = bytes;
            } else {
                nsrn = 8u;
                nsaa = (nsaa + 7u) & ~UINT64_C(7);
                slot->kind = TC_A64_LOC_STACK;
                slot->stack_offset = nsaa;
                nsaa += (bytes + 7u) & ~UINT64_C(7);
            }
            continue;
        }
        if (type->tag == TC_TY_STRUCT) {
            if (tc_a64_plan_hfa(emitter->ir, type, &is32, &hfa)) {
                if (nsrn + hfa <= 8u) {
                    slot->kind = TC_A64_LOC_VREG;
                    slot->index = nsrn;
                    slot->hfa = 1;
                    slot->hfa32 = is32;
                    slot->hfa_count = hfa;
                    nsrn += hfa;
                    continue;
                }
                nsrn = 8u;
                nsaa = (nsaa + 7u) & ~UINT64_C(7);
                slot->kind = TC_A64_LOC_STACK;
                slot->stack_offset = nsaa;
                nsaa += (size + 7u) & ~UINT64_C(7);
                continue;
            }
            if (size <= 16u) {
                unsigned chunks = (unsigned)((size + 7u) / 8u);
                if (ngrn + chunks <= 8u) {
                    slot->kind = TC_A64_LOC_XREG;
                    slot->index = ngrn;
                    ngrn += chunks;
                    continue;
                }
                ngrn = 8u;
                nsaa = (nsaa + 7u) & ~UINT64_C(7);
                slot->kind = TC_A64_LOC_STACK;
                slot->stack_offset = nsaa;
                nsaa += (size + 7u) & ~UINT64_C(7);
                continue;
            }
            nsaa = (nsaa + 7u) & ~UINT64_C(7);
            slot->kind = TC_A64_LOC_STACK;
            slot->stack_offset = nsaa;
            nsaa += (size + 7u) & ~UINT64_C(7);
            continue;
        }
        if (ngrn < 8u) {
            slot->kind = TC_A64_LOC_XREG;
            slot->index = ngrn++;
        } else {
            nsaa = (nsaa + 7u) & ~UINT64_C(7);
            slot->kind = TC_A64_LOC_STACK;
            slot->stack_offset = nsaa;
            nsaa += ((size == 0 ? 8u : size) + 7u) & ~UINT64_C(7);
        }
    }
    out->stack_bytes = (nsaa + 15u) & ~UINT64_C(15);
}

void tc_a64_plan_return(TcA64Emitter *emitter, const TcType *ret,
                        TcA64RetPlan *out) {
    int ok = 0;
    uint64_t size;
    int is32 = 0;
    unsigned hfa = 0;
    memset(out, 0, sizeof(*out));
    if (ret == NULL || ret->tag == TC_TY_VOID) {
        out->kind = TC_A64_RET_VOID;
        return;
    }
    size = tc_a64_type_size(emitter->ir, ret, &ok);
    if (!ok) {
        tc_a64_error(emitter, "return type is incomplete");
        out->kind = TC_A64_RET_VOID;
        return;
    }
    if (tc_type_is_float(ret)) {
        out->kind = TC_A64_RET_VREGS;
        out->count = 1;
        return;
    }
    if (ret->tag != TC_TY_STRUCT) {
        out->kind = TC_A64_RET_XREGS;
        out->count = 1;
        return;
    }
    if (tc_a64_plan_hfa(emitter->ir, ret, &is32, &hfa)) {
        out->kind = TC_A64_RET_VREGS;
        out->count = hfa;
        out->hfa32 = is32;
        out->hfa_count = hfa;
        return;
    }
    if (size <= 16u) {
        out->kind = TC_A64_RET_XREGS;
        out->count = (unsigned)((size + 7u) / 8u);
        return;
    }
    out->kind = TC_A64_RET_INDIRECT;
    out->count = 1;
}

void tc_a64_free_argument_plan(TcA64ArgPlan *plan) {
    if (plan == NULL) {
        return;
    }
    free(plan->slots);
    plan->slots = NULL;
    plan->count = 0;
}
