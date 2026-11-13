/* TC 0.0.46 contract: type identity (standard §3.1-§3.9, design §2).
 *
 * A complete type is a stable, shared, interned node.  Symbols, bindings,
 * values, signatures and objects hold const TcType*; a bare tag is never a
 * substitute for a complete type.  Pointer equality is type equality after the
 * table is frozen.  Layout/sizeof is deliberately NOT cached here: it depends
 * on the profile word size W and belongs to the profile.
 */
#ifndef TC_IR_TYPE_H
#define TC_IR_TYPE_H

#include <stddef.h>
#include <stdint.h>

#include "base/assert.h"

typedef enum {
    TC_TY_I8 = 0,
    TC_TY_I16,
    TC_TY_I32,
    TC_TY_I64,
    TC_TY_U8,
    TC_TY_U16,
    TC_TY_U32,
    TC_TY_U64,
    TC_TY_ISIZE, /* platform word type: width = W, independent identity */
    TC_TY_USIZE, /* platform word type: width = W, independent identity */
    TC_TY_F32,
    TC_TY_F64,
    TC_TY_BOOL,
    TC_TY_VOID,
    TC_TY_PTR,
    TC_TY_FNPTR,
    TC_TY_STRUCT,
    TC_TY_COUNT
} TcTypeTag;

#define TC_TYPE_SCALAR_COUNT 14
#define TC_TYPE_TAG_COUNT ((size_t)TC_TY_COUNT)

typedef struct TcType TcType;

struct TcType {
    TcTypeTag tag;
    union {
        struct {
            const TcType *pointee;
        } ptr;
        struct {
            const TcType *ret;
            const TcType **params; /* n entries; parameter names do not exist */
            size_t n;
        } fnptr;
        struct {
            uint32_t struct_id; /* nominal identity */
        } struct_;
    } u;
};

struct TcAllocator;

typedef struct TcTypeTable TcTypeTable;

/* The allocator must be non-NULL and remain valid for the table lifetime.
 * Use tc_allocator_libc() for host-side builds. */
TcTypeTable *tc_type_table_create(const struct TcAllocator *alloc);
void tc_type_table_destroy(TcTypeTable *table);

/* Scalar singleton lookup.  Only scalar tags are accepted; returns NULL for
 * TC_TY_PTR/TC_TY_FNPTR/TC_TY_STRUCT and out-of-range tags. */
const TcType *tc_type_scalar(TcTypeTable *table, TcTypeTag tag);

/* Compound interning.  Returns NULL for a contract violation:
 *   - ptr: pointee NULL or void; a struct pointer is allowed (completeness is
 *     diagnosed by SEM, not by the type table);
 *   - fnptr: ret NULL, a param NULL or void;
 *   - intern after freeze, or allocation failure. */
const TcType *tc_type_ptr(TcTypeTable *table, const TcType *pointee);
const TcType *tc_type_fnptr(TcTypeTable *table, const TcType *ret,
                            const TcType *const *params, size_t nparams);
const TcType *tc_type_struct(TcTypeTable *table, uint32_t struct_id);

/* Analyze freezes the table after Pass2; the executor/AOT never mutates it. */
int tc_type_table_freeze(TcTypeTable *table);
int tc_type_table_is_frozen(const TcTypeTable *table);

/* Type equality is interned pointer equality (standard §3.9).  NULL only
 * equals NULL. */
int tc_type_equal(const TcType *a, const TcType *b);

int tc_type_is_value(const TcType *type);
int tc_type_is_integer(const TcType *type);
int tc_type_is_signed_integer(const TcType *type);
int tc_type_is_float(const TcType *type);
int tc_type_is_scalar(const TcType *type);

/* Bit width under a given profile word size.  The caller supplies W (32/64)
 * from TcPlatformProfile.addr_width; isize/usize/ptr/fnptr equal W.  void and
 * struct return 0 (struct size is computed by SEM layout, not here), and an
 * invalid W returns 0. */
unsigned tc_type_bits(const TcType *type, unsigned addr_width);

#endif /* TC_IR_TYPE_H */
