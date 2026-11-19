/* TC 0.0.46 contract: IR operands, lvalues and named arguments (design §3.1).
 *
 * Parse-time field references keep source names; SEM resolves them to a base
 * vreg plus field-index path.  Final IR operands never carry source names.
 */
#ifndef TC_IR_OPERAND_H
#define TC_IR_OPERAND_H

#include <stddef.h>
#include <stdint.h>

#include "ir/type.h"

typedef enum {
    TC_OP_VREG = 0, /* frame/static/program variable or temporary */
    TC_OP_FIELD,    /* x.f.g: base vreg + field index path */
    TC_OP_CONST,
    TC_OP_LABEL,
    TC_OP_FUNC,
    TC_OP_STRUCT,
    TC_OP_DISCARD /* '_' : LHS only, never read, not part of DI */
} TcOperandKind;

typedef struct {
    TcOperandKind kind;
    union {
        uint32_t vreg;
        struct {
            uint32_t base;
            uint32_t *fields;
            size_t nfields;
        } field;
        struct {
            uint64_t bits;
            const TcType *type;
        } konst;
        uint32_t label;
        uint32_t func;
        uint32_t struct_id;
    };
} TcOperand;

typedef struct {
    const char *name; /* NULL == positional argument/field */
    TcOperand value;
} TcArg;

/* Parse-only temporary; SEM replaces it with TC_OP_FIELD and the names vanish. */
typedef struct {
    const char *base;
    const char **fields;
    size_t nfields;
} TcFieldRef;

#endif /* TC_IR_OPERAND_H */
