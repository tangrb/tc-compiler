/* TC 0.0.46 contract: pointer state matrix (standard §3.12, design §4).
 *
 * The table is the single frozen fact source for pointer-state checks shared
 * by addr/ptrdiff/memmove/bounds checks in SEM, VM and AOT.  It describes
 * abstract pointer-state permission; object bounds and checked-build poison
 * checks are separate layers.  The table follows §3.12's allowed columns:
 * null copy/compare/ptrdiff; live all; one-past compare/ptrdiff/ptradd(0)/
 * ptrsub; dangling copy/compare/ptrdiff/bitcast.  Bitcast is permitted only
 * where §3.12 explicitly lists it (live/dangling).
 */
#ifndef TC_IR_POINTER_STATE_H
#define TC_IR_POINTER_STATE_H

#include "base/assert.h"

typedef enum {
    TC_PTR_STATE_NULL = 0,
    TC_PTR_STATE_LIVE,
    TC_PTR_STATE_ONE_PAST,
    TC_PTR_STATE_DANGLING,
    TC_PTR_STATE_COUNT
} TcPtrState;

typedef enum {
    TC_PTR_OP_COPY = 0,      /* copy/return/store pointer value */
    TC_PTR_OP_COMPARE,       /* ptreq/ptrne/ptrlt/ptrle/ptrgt/ptrge */
    TC_PTR_OP_PTRDIFF,       /* ptrdiff */
    TC_PTR_OP_LOAD,          /* load */
    TC_PTR_OP_STORE,         /* store */
    TC_PTR_OP_MEMMOVE,       /* memmove */
    TC_PTR_OP_PTRADD,        /* ptradd */
    TC_PTR_OP_PTRSUB,        /* ptrsub */
    TC_PTR_OP_CALL_INDIRECT, /* call_indirect */
    TC_PTR_OP_BITCAST,       /* bitcast ptr<T> <-> ptr<U> */
    TC_PTR_OP_COUNT
} TcPtrOp;

/* Returns 1 if the abstract pointer state permits the operation.
 *
 * zero_offset is meaningful for PTRADD: one-past permits only offset 0;
 * null always rejects ptradd/ptrsub.  Object bounds and the exact object of a
 * one-past ptrsub result are checked by the caller with TcObject facts. */
int tc_ptr_state_allows(TcPtrState state, TcPtrOp op, int zero_offset);

/* Stable names for diagnostics/tests. */
const char *tc_ptr_state_name(TcPtrState state);
const char *tc_ptr_op_name(TcPtrOp op);

TC_STATIC_ASSERT(TC_PTR_STATE_COUNT == 4, __LINE__);
TC_STATIC_ASSERT(TC_PTR_OP_COUNT == 10, __LINE__);

#endif /* TC_IR_POINTER_STATE_H */
