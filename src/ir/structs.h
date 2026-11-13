/* TC 0.0.46 contract: struct definitions and field layout (design §2.1).
 *
 * offset/size are the values computed by SEM for one profile W; they are not
 * cached in TcType.  padding is the @padding(N) byte count, 0 for none.
 */
#ifndef TC_IR_STRUCTS_H
#define TC_IR_STRUCTS_H

#include <stddef.h>
#include <stdint.h>

#include "ir/type.h"

typedef struct {
    const char *name;
    const TcType *type;
    int readonly;
    uint32_t padding;
    uint64_t offset;
} TcField;

typedef struct {
    uint32_t id;
    uint32_t module; /* module instance id */
    const char *name;
    TcField *fields;
    size_t nfields;
    uint64_t size; /* sizeof S under the profile W */
} TcStructDef;

#endif /* TC_IR_STRUCTS_H */
