/* TC 0.0.46 contract: target address (design §4).
 *
 * The backend maps a region plus in-region offset to the real address defined
 * by the profile; the IR never stores a host pointer.  Code/device addresses
 * have object == UINT32_MAX (they are not language objects).
 */
#ifndef TC_IR_ADDRESS_H
#define TC_IR_ADDRESS_H

#include <stdint.h>

#include "ir/region.h"
#include "ir/type.h"

typedef struct {
    TcRegionKind region;
    uint32_t object; /* TcObject id; UINT32_MAX for code/device addresses */
    uint64_t offset; /* in-region offset */
    const TcType *type;
} TcAddress;

#define TC_ADDRESS_NO_OBJECT UINT32_MAX

#endif /* TC_IR_ADDRESS_H */
