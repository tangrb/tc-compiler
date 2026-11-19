/* TC 0.0.46 contract: memory regions and lifetimes (standard §3.12/§13.2). */
#ifndef TC_IR_REGION_H
#define TC_IR_REGION_H

#include "base/assert.h"

/* The five regions of the abstract machine.  The order is normative for the
 * profile region table: profile.region[TC_REG_*] selects the same region. */
typedef enum {
    TC_REG_FRAME = 0,  /* frame region */
    TC_REG_STATIC,     /* program globals and static var */
    TC_REG_DEVICE,     /* device memory (not a language object) */
    TC_REG_CODE,       /* code (not a language object) */
    TC_REG_HEAP,       /* host (heap) objects */
    TC_REG_COUNT
} TcRegionKind;

typedef enum {
    TC_LIFE_FRAME = 0,  /* call entry .. return */
    TC_LIFE_PROGRAM,    /* program start .. end */
    TC_LIFE_HOST        /* created/released by the host allocator */
} TcLifetime;

#define TC_REGION_COUNT 5

TC_STATIC_ASSERT(TC_REG_COUNT == TC_REGION_COUNT, __LINE__);

#endif /* TC_IR_REGION_H */
