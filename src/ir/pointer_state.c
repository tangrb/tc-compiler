/* TC 0.0.46 contract: pointer-state matrix implementation. */
#include "ir/pointer_state.h"

/* Bit masks per state, in TcPtrOp order. */
#define TC_PTR_BIT(op) (1u << (unsigned)(op))

static const unsigned ptr_state_mask[TC_PTR_STATE_COUNT] = {
    /* null: copy/compare/ptrdiff; dereference/arith/memmove/call/bitcast absent */
    TC_PTR_BIT(TC_PTR_OP_COPY) | TC_PTR_BIT(TC_PTR_OP_COMPARE) |
        TC_PTR_BIT(TC_PTR_OP_PTRDIFF),
    /* live: all pointer operations; object bounds are a separate check */
    TC_PTR_BIT(TC_PTR_OP_COPY) | TC_PTR_BIT(TC_PTR_OP_COMPARE) |
        TC_PTR_BIT(TC_PTR_OP_PTRDIFF) | TC_PTR_BIT(TC_PTR_OP_LOAD) |
        TC_PTR_BIT(TC_PTR_OP_STORE) | TC_PTR_BIT(TC_PTR_OP_MEMMOVE) |
        TC_PTR_BIT(TC_PTR_OP_PTRADD) | TC_PTR_BIT(TC_PTR_OP_PTRSUB) |
        TC_PTR_BIT(TC_PTR_OP_CALL_INDIRECT) | TC_PTR_BIT(TC_PTR_OP_BITCAST),
    /* one-past: compare/ptrdiff/ptrsub; ptradd only n=0; no copy/bitcast */
    TC_PTR_BIT(TC_PTR_OP_COMPARE) | TC_PTR_BIT(TC_PTR_OP_PTRDIFF) |
        TC_PTR_BIT(TC_PTR_OP_PTRSUB),
    /* dangling: copy/compare/ptrdiff/bitcast; no dereference/arith/memmove */
    TC_PTR_BIT(TC_PTR_OP_COPY) | TC_PTR_BIT(TC_PTR_OP_COMPARE) |
        TC_PTR_BIT(TC_PTR_OP_PTRDIFF) | TC_PTR_BIT(TC_PTR_OP_BITCAST),
};

int tc_ptr_state_allows(TcPtrState state, TcPtrOp op, int zero_offset) {
    if ((unsigned)state >= TC_PTR_STATE_COUNT ||
        (unsigned)op >= TC_PTR_OP_COUNT) {
        return 0;
    }
    if (op == TC_PTR_OP_PTRADD) {
        if (state == TC_PTR_STATE_ONE_PAST) {
            return zero_offset == 0;
        }
        if (state != TC_PTR_STATE_LIVE) {
            return 0;
        }
    }
    return (ptr_state_mask[state] & TC_PTR_BIT(op)) != 0;
}

const char *tc_ptr_state_name(TcPtrState state) {
    switch (state) {
    case TC_PTR_STATE_NULL:
        return "null";
    case TC_PTR_STATE_LIVE:
        return "live";
    case TC_PTR_STATE_ONE_PAST:
        return "one-past";
    case TC_PTR_STATE_DANGLING:
        return "dangling";
    default:
        return "?";
    }
}

const char *tc_ptr_op_name(TcPtrOp op) {
    switch (op) {
    case TC_PTR_OP_COPY:
        return "copy";
    case TC_PTR_OP_COMPARE:
        return "compare";
    case TC_PTR_OP_PTRDIFF:
        return "ptrdiff";
    case TC_PTR_OP_LOAD:
        return "load";
    case TC_PTR_OP_STORE:
        return "store";
    case TC_PTR_OP_MEMMOVE:
        return "memmove";
    case TC_PTR_OP_PTRADD:
        return "ptradd";
    case TC_PTR_OP_PTRSUB:
        return "ptrsub";
    case TC_PTR_OP_CALL_INDIRECT:
        return "call_indirect";
    case TC_PTR_OP_BITCAST:
        return "bitcast";
    default:
        return "?";
    }
}
