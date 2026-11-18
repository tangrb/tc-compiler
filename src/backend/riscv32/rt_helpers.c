/* TC 0.0.46 RISC-V 32 scalar helper wrappers (ILP32).
 *
 * Generated code passes 64-bit scalar operands as two 32-bit words so the
 * emitter needs no register-pair scheduling at call sites.  The wrappers
 * reassemble the pair and execute the shared semantic core with W=32, which
 * keeps VM and AOT results identical for the word-size-dependent types.
 */
#include <stdint.h>

#include "backend/common/rt_platform.h"
#include "ir/type.h"
#include "runtime/semantics.h"
#include "runtime/trap.h"

void tc_rt_trap(uint32_t code, uint32_t line, uint32_t column,
                const char *file) __attribute__((noreturn));

static void zero_type(TcType *type);

__attribute__((noreturn)) static void raise_trap(uint32_t kind, uint32_t line,
                                                 uint32_t column,
                                                 const char *file) {
    if (kind == 0u) {
        kind = (uint32_t)TC_EK_TRAP;
    }
    tc_rt_trap(kind, line, column, file);
}

static uint64_t pack64(uint32_t lo, uint32_t hi) {
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static TcType type_from_tag(uint32_t tag) {
    TcType type;
    zero_type(&type);
    type.tag = (TcTypeTag)tag;
    return type;
}

static void zero_type(TcType *type) {
    unsigned char *bytes = (unsigned char *)type;
    size_t i;
    for (i = 0; i < sizeof(*type); i++) {
        bytes[i] = 0;
    }
}

uint64_t tc_riscv32_int_binop(uint32_t op, uint32_t mode, uint32_t tag,
                              uint32_t a_lo, uint32_t a_hi, uint32_t b_lo,
                              uint32_t b_hi, uint32_t line, uint32_t column,
                              const char *file) {
    TcType type = type_from_tag(tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_int_binop(TC_RT_W, &type, (TcOpcode)op, (TcOpMode)mode,
                          pack64(a_lo, a_hi), pack64(b_lo, b_hi), &out,
                          &trap)) {
        raise_trap((uint32_t)trap, line, column, file);
    }
    return out;
}

uint64_t tc_riscv32_int_unop(uint32_t op, uint32_t mode, uint32_t tag,
                             uint32_t a_lo, uint32_t a_hi) {
    TcType type = type_from_tag(tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_int_unop(TC_RT_W, &type, (TcOpcode)op, (TcOpMode)mode,
                         pack64(a_lo, a_hi), &out, &trap)) {
        raise_trap((uint32_t)trap, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_riscv32_int_compare(uint32_t op, uint32_t tag, uint32_t a_lo,
                                uint32_t a_hi, uint32_t b_lo, uint32_t b_hi) {
    TcType type = type_from_tag(tag);
    uint64_t out = 0;
    if (!tc_sem_compare(TC_RT_W, &type, (TcOpcode)op, pack64(a_lo, a_hi),
                        pack64(b_lo, b_hi), &out)) {
        raise_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_riscv32_float_binop(uint32_t op, uint32_t mode, uint32_t tag,
                                uint32_t a_lo, uint32_t a_hi, uint32_t b_lo,
                                uint32_t b_hi, uint32_t line, uint32_t column,
                                const char *file) {
    TcType type = type_from_tag(tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_float_binop(TC_RT_W, &type, (TcOpcode)op, (TcOpMode)mode,
                            pack64(a_lo, a_hi), pack64(b_lo, b_hi), &out,
                            &trap)) {
        raise_trap((uint32_t)trap, line, column, file);
    }
    return out;
}

uint64_t tc_riscv32_float_unop(uint32_t op, uint32_t tag, uint32_t a_lo,
                               uint32_t a_hi) {
    TcType type = type_from_tag(tag);
    uint64_t out = 0;
    if (!tc_sem_float_unop(TC_RT_W, &type, (TcOpcode)op, pack64(a_lo, a_hi),
                           &out)) {
        raise_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_riscv32_float_compare(uint32_t op, uint32_t tag, uint32_t a_lo,
                                  uint32_t a_hi, uint32_t b_lo,
                                  uint32_t b_hi) {
    TcType type = type_from_tag(tag);
    uint64_t out = 0;
    if (!tc_sem_compare(TC_RT_W, &type, (TcOpcode)op, pack64(a_lo, a_hi),
                        pack64(b_lo, b_hi), &out)) {
        raise_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_riscv32_cast(uint32_t src_tag, uint32_t dst_tag, uint32_t lo,
                         uint32_t hi, uint32_t line, uint32_t column,
                         const char *file) {
    TcType source = type_from_tag(src_tag);
    TcType destination = type_from_tag(dst_tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_cast(TC_RT_W, &source, &destination, pack64(lo, hi), &out,
                     &trap)) {
        raise_trap((uint32_t)trap, line, column, file);
    }
    return out;
}

uint64_t tc_riscv32_trunc(uint32_t src_tag, uint32_t dst_tag, uint32_t lo,
                          uint32_t hi) {
    TcType source = type_from_tag(src_tag);
    TcType destination = type_from_tag(dst_tag);
    uint64_t out = 0;
    if (!tc_sem_trunc(TC_RT_W, &source, &destination, pack64(lo, hi), &out)) {
        raise_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_riscv32_bitcast(uint32_t dst_tag, uint32_t lo, uint32_t hi) {
    TcType destination = type_from_tag(dst_tag);
    uint64_t out = 0;
    if (!tc_sem_bitcast(TC_RT_W, &destination, pack64(lo, hi), &out)) {
        raise_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}
