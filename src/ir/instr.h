/* TC 0.0.46 contract: flattened TAC instruction (design §3.2).
 *
 * One source instruction is exactly one op.  Non-result instructions must use
 * TC_OP_DISCARD as dst.  The opcode table itself is S3 work; this header only
 * freezes the representation and the result/terminator structure.
 */
#ifndef TC_IR_INSTR_H
#define TC_IR_INSTR_H

#include <stddef.h>
#include <stdint.h>

#include "ir/operand.h"
#include "ir/span.h"
#include "ir/type.h"

typedef enum {
    /* standard §6.2.1 arithmetic */
    TC_ADD = 0,
    TC_SUB,
    TC_MUL,
    TC_DIV,
    TC_MOD,
    TC_NEG,
    TC_ABS,
    TC_SHL,
    TC_SHR,
    /* §6.2.2 bit/logical */
    TC_AND,
    TC_OR,
    TC_XOR,
    TC_NOT,
    /* §6.2.3 compare */
    TC_EQ,
    TC_NE,
    TC_LT,
    TC_LE,
    TC_GT,
    TC_GE,
    /* §6.2.4 conversion */
    TC_CAST,
    TC_TRUNC,
    TC_BITCAST,
    /* §6.2.5 memory/pointer */
    TC_LOAD,
    TC_STORE,
    TC_MEMMOVE,
    TC_ADDR,
    TC_PTRADD,
    TC_PTRSUB,
    TC_PTRDIFF,
    TC_PTREQ,
    TC_PTRNE,
    TC_PTRLT,
    TC_PTRLE,
    TC_PTRGT,
    TC_PTRGE,
    TC_SIZEOF,
    /* §6.2.6 function */
    TC_FUNCADDR,
    TC_CALL,
    TC_CALL_INDIRECT,
    TC_FUNCEQ,
    TC_FUNCNE,
    /* §6.2.7 struct/copy */
    TC_MAKE,
    TC_COPY,
    /* §6.2.8 control */
    TC_BR,
    TC_BR_IF,
    TC_SWITCH,
    TC_RETURN,
    TC_TRAP,
    /* §6.2.9 I/O */
    TC_READ,
    TC_WRITE,
    TC_WRITELN,
    TC_OPCODE_COUNT
} TcOpcode;

typedef enum {
    TC_STRICT = 0, /* default */
    TC_WRAP,       /* add/sub/mul/neg/shl */
    TC_IEEE,       /* float add/sub/mul/div/mod */
    TC_NSW,        /* @fast only: add/sub/mul/shl */
    TC_NUW,        /* @fast only: add/sub/mul/shl */
    TC_VOLATILE    /* load/store */
} TcOpMode;

#define TC_OPCODE_COUNT_ ((size_t)TC_OPCODE_COUNT)

typedef struct {
    TcOpcode op;
    TcOpMode mode;
    const TcType *type;        /* type parameter; position fixed by opcode table */
    TcOperand dst;             /* VREG/FIELD/DISCARD */
    TcArg *args;
    size_t nargs;
    uint32_t *switch_targets;  /* 0 -> L0 ... d-1 -> Ld-1, otherwise Ld */
    size_t ntargets;
    TcSpan span;               /* diagnostic anchor: file + line + column */
} TcInstr;

/* No-result instructions are exactly the standard §6.2 effect/control ops whose
 * LHS must be '_' (standard §6.15.1, design §3.2). */
int tc_opcode_has_result(TcOpcode op);
int tc_opcode_is_terminator(TcOpcode op);
int tc_instr_lhs_is_valid(const TcInstr *instr);

#endif /* TC_IR_INSTR_H */
