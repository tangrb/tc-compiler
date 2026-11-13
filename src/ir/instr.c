/* TC 0.0.46 contract: instruction shape helpers. */
#include "ir/instr.h"

#include "base/assert.h"

TC_STATIC_ASSERT((size_t)TC_OPCODE_COUNT == 51, __LINE__);

int tc_opcode_has_result(TcOpcode op) {
    switch (op) {
    case TC_STORE:
    case TC_MEMMOVE:
    case TC_BR:
    case TC_BR_IF:
    case TC_SWITCH:
    case TC_RETURN:
    case TC_TRAP:
    case TC_WRITE:
    case TC_WRITELN:
        return 0;
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_NEG:
    case TC_ABS:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
    case TC_NOT:
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
    case TC_CAST:
    case TC_TRUNC:
    case TC_BITCAST:
    case TC_LOAD:
    case TC_ADDR:
    case TC_PTRADD:
    case TC_PTRSUB:
    case TC_PTRDIFF:
    case TC_PTREQ:
    case TC_PTRNE:
    case TC_PTRLT:
    case TC_PTRLE:
    case TC_PTRGT:
    case TC_PTRGE:
    case TC_SIZEOF:
    case TC_FUNCADDR:
    case TC_CALL:
    case TC_CALL_INDIRECT:
    case TC_FUNCEQ:
    case TC_FUNCNE:
    case TC_MAKE:
    case TC_COPY:
    case TC_READ:
        return 1;
    case TC_OPCODE_COUNT:
    default:
        return 0;
    }
}

int tc_opcode_is_terminator(TcOpcode op) {
    switch (op) {
    case TC_BR:
    case TC_BR_IF:
    case TC_SWITCH:
    case TC_RETURN:
    case TC_TRAP:
        return 1;
    default:
        return 0;
    }
}

int tc_instr_lhs_is_valid(const TcInstr *instr) {
    if (instr == NULL) {
        return 0;
    }
    /* Standard §6.15.1: any result instruction may discard its result with
     * `_` (including non-void call and read).  No-result instructions must use
     * `_`; a result instruction may also write a vreg/field. */
    if (instr->dst.kind == TC_OP_DISCARD) {
        return 1;
    }
    if (!tc_opcode_has_result(instr->op)) {
        return 0;
    }
    return instr->dst.kind == TC_OP_VREG || instr->dst.kind == TC_OP_FIELD;
}
