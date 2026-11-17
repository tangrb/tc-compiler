/* TC 0.0.46 scalar width by type tag (design §2).
 *
 * The width of a scalar type depends only on its tag and the profile word
 * width, so it can be shared as a header-only fact between the front end, the
 * runtime semantic core and the freestanding AArch64 runtime archive.  Layout
 * (`sizeof`, field offsets) is never part of a type node; it stays a profile
 * function (standard §13.2.2).
 */
#ifndef TC_IR_TYPE_WIDTH_H
#define TC_IR_TYPE_WIDTH_H

#include "ir/type.h"

static inline unsigned tc_type_width_of_tag(TcTypeTag tag, unsigned addr_width) {
    if (addr_width != 32u && addr_width != 64u) {
        return 0;
    }
    switch (tag) {
    case TC_TY_I8:
    case TC_TY_U8:
    case TC_TY_BOOL:
        return 8;
    case TC_TY_I16:
    case TC_TY_U16:
        return 16;
    case TC_TY_I32:
    case TC_TY_U32:
    case TC_TY_F32:
        return 32;
    case TC_TY_I64:
    case TC_TY_U64:
    case TC_TY_F64:
        return 64;
    case TC_TY_ISIZE:
    case TC_TY_USIZE:
    case TC_TY_PTR:
    case TC_TY_FNPTR:
        return addr_width;
    case TC_TY_VOID:
    case TC_TY_STRUCT:
    default:
        return 0;
    }
}

#endif /* TC_IR_TYPE_WIDTH_H */
