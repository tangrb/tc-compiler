/* TC 0.0.46 contract: storage object (standard §3.12, design §4).
 *
 * TcObject is the single source of truth for addr/ptrdiff/memmove/bounds and
 * post-dominator/DI facts.  Each storage vreg maps to an object through
 * TcUnit.vreg_objects; region+offset alone is not object identity.
 */
#ifndef TC_IR_OBJECT_H
#define TC_IR_OBJECT_H

#include <stdint.h>

#include "ir/region.h"
#include "ir/type.h"

typedef struct TcObject TcObject;

struct TcObject {
    uint32_t id;
    uint32_t module; /* static object's module instance; UINT32_MAX otherwise */
    TcRegionKind region;
    uint64_t offset; /* interpreted in the target address space */
    uint64_t size;
    const TcType *type;
    TcLifetime lifetime;
    const struct TcInitImage *init; /* static var/static region initializer */
    int writable;                   /* parameter objects are read-only */
    uint32_t parent;                /* field sub-object parent; UINT32_MAX = none */
};

#define TC_OBJECT_NO_PARENT UINT32_MAX
#define TC_OBJECT_NO_MODULE UINT32_MAX

#endif /* TC_IR_OBJECT_H */
