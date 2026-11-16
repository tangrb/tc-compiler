/* TC 0.0.46 AArch64 subset assembly emitter (design §10/§11).
 *
 * Calling convention (AAPCS64 subset, design §10.2):
 *   - integer/pointer/fnptr/bool arguments in x0-x7, scalar result in x0;
 *   - x19/x20 are callee-saved and hold the frame base and static base;
 *   - x29/x30 form the frame record; sp stays 16-byte aligned;
 *   - narrow integers are passed zero-extended and stored/loaded at their
 *     declared width; bool storage is normalized to 0x00/0x01.
 *
 * Every value has a stack slot ("no regalloc", design §10.3): frame objects
 * keep their TcObject offset so VM and AOT agree on the in-region offset, and
 * temporaries follow the object area.  Static objects are emitted as one
 * byte array whose bytes at TcObject.offset are the CT initializer image.
 */
#include "backend/aarch64/emit.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostic/diagnostic.h"
#include "runtime/trap.h"

#define TC_A64_MAX_LOCAL_FILES 64u
#define TC_A64_MAX_PARAMS 8u

typedef enum {
    TC_LOC_FRAME = 0,
    TC_LOC_STATIC,
    TC_LOC_TEMP
} TcLocKind;

typedef struct {
    TcLocKind kind;
    uint64_t offset;
} TcLoc;

typedef struct {
    const TcIR *ir;
    const TcPlatformProfile *profile;
    FILE *out;
    char *err;
    size_t errcap;
    int failed;
    unsigned next_label;

    const char *files[TC_A64_MAX_LOCAL_FILES];
    size_t nfiles;

    /* Current unit layout. */
    const TcUnit *unit;
    TcLoc *loc;
    size_t nloc;
    uint64_t frame_size;
    uint64_t read_scratch;
    int has_read;
} TcA64;

static void fail(TcA64 *e, const char *fmt, ...) {
    va_list ap;
    if (e == NULL || e->failed) {
        return;
    }
    e->failed = 1;
    if (e->err != NULL && e->errcap != 0) {
        va_start(ap, fmt);
        vsnprintf(e->err, e->errcap, fmt, ap);
        va_end(ap);
    }
}

static const char *opcode_name(TcOpcode op) {
    static const char *const names[TC_OPCODE_COUNT] = {
        "add", "sub", "mul", "div", "mod", "neg", "abs", "shl", "shr",
        "and", "or", "xor", "not", "eq", "ne", "lt", "le", "gt", "ge",
        "cast", "trunc", "bitcast", "load", "store", "memmove", "addr",
        "ptradd", "ptrsub", "ptrdiff", "ptreq", "ptrne", "ptrlt", "ptrle",
        "ptrgt", "ptrge", "sizeof", "funcaddr", "call", "call_indirect",
        "funceq", "funcne", "make", "copy", "br", "br_if", "switch",
        "return", "trap", "read", "write", "writeln"
    };
    if (op >= 0 && op < TC_OPCODE_COUNT) {
        return names[(int)op];
    }
    return "?";
}

static void emit(TcA64 *e, const char *fmt, ...) {
    va_list ap;
    if (e == NULL || e->failed) {
        return;
    }
    va_start(ap, fmt);
    if (vfprintf(e->out, fmt, ap) < 0) {
        e->failed = 1;
    }
    va_end(ap);
}

/* ------------------------------------------------------------------------- */
/* Types and sizes.                                                          */
/* ------------------------------------------------------------------------- */

static unsigned type_width(const TcType *type) {
    return tc_type_bits(type, 64u);
}

/* Storage bytes for a scalar value type (bool is one byte). */
static unsigned type_bytes(const TcType *type) {
    return type_width(type) / 8u;
}

static int type_is_supported_value(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    if (type->tag == TC_TY_BOOL) {
        return 1;
    }
    if (tc_type_is_integer(type)) {
        return 1;
    }
    return type->tag == TC_TY_PTR || type->tag == TC_TY_FNPTR;
}

static uint64_t type_size(const TcIR *ir, const TcType *type, int *ok) {
    if (ok != NULL) {
        *ok = 0;
    }
    if (type == NULL) {
        return 0;
    }
    if (type->tag == TC_TY_STRUCT) {
        uint32_t id = type->struct_.struct_id;
        if (ir == NULL || id >= ir->nstructs) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return ir->structs[id].size;
    }
    {
        unsigned width = type_width(type);
        if (width == 0) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return width / 8u;
    }
}

static uint64_t type_mask(const TcType *type) {
    unsigned width = type_width(type);
    if (width == 0) {
        return 0;
    }
    if (width >= 64u) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << width) - 1u;
}

/* ------------------------------------------------------------------------- */
/* Instruction emission helpers.                                             */
/* ------------------------------------------------------------------------- */

static unsigned fresh_label(TcA64 *e) {
    return e->next_label++;
}

static void emit_mov_imm(TcA64 *e, unsigned reg, uint64_t value) {
    unsigned shift;
    int first = 1;
    if (value == 0) {
        emit(e, "  movz x%u, #0\n", reg);
        return;
    }
    for (shift = 0; shift < 64u; shift += 16u) {
        uint64_t chunk = (value >> shift) & UINT64_C(0xffff);
        if (chunk == 0) {
            continue;
        }
        if (first) {
            emit(e, "  movz x%u, #0x%llx", reg,
                 (unsigned long long)chunk);
            first = 0;
        } else {
            emit(e, "  movk x%u, #0x%llx", reg,
                 (unsigned long long)chunk);
        }
        if (shift != 0) {
            emit(e, ", lsl #%u", shift);
        }
        emit(e, "\n");
    }
}

static void emit_mask(TcA64 *e, unsigned reg, const TcType *type) {
    uint64_t mask = type_mask(type);
    if (type == NULL || type->tag == TC_TY_BOOL) {
        emit(e, "  and x%u, x%u, #1\n", reg, reg);
        return;
    }
    if (mask == UINT64_MAX) {
        return;
    }
    if (mask == UINT64_C(0xff) || mask == UINT64_C(0xffff) ||
        mask == UINT64_C(0xffffffff)) {
        emit(e, "  and x%u, x%u, #0x%llx\n", reg, reg,
             (unsigned long long)mask);
        return;
    }
    emit_mov_imm(e, 16u, mask);
    emit(e, "  and x%u, x%u, x16\n", reg, reg);
}

static void emit_sign_extend(TcA64 *e, unsigned reg, const TcType *type) {
    if (type == NULL) {
        return;
    }
    switch (type->tag) {
    case TC_TY_I8:
        emit(e, "  sxtb x%u, w%u\n", reg, reg);
        break;
    case TC_TY_I16:
        emit(e, "  sxth x%u, w%u\n", reg, reg);
        break;
    case TC_TY_I32:
        emit(e, "  sxtw x%u, w%u\n", reg, reg);
        break;
    default:
        break;
    }
}

static void emit_bool_normalize(TcA64 *e, unsigned reg) {
    emit(e, "  cmp w%u, #0\n  cset w%u, ne\n", reg, reg);
}

/* Materialize `base + offset` in x17 and return the effective base/offset. */
static unsigned emit_address(TcA64 *e, unsigned base, uint64_t offset,
                             uint64_t *out_offset) {
    if (offset <= 255u) {
        *out_offset = offset;
        return base;
    }
    emit_mov_imm(e, 17u, offset);
    emit(e, "  add x17, x%u, x17\n", base);
    *out_offset = 0;
    return 17u;
}

static void emit_load_mem(TcA64 *e, unsigned base, uint64_t offset,
                          unsigned bytes, unsigned reg, int sign_extend,
                          const TcType *type) {
    uint64_t off = 0;
    unsigned b = emit_address(e, base, offset, &off);
    switch (bytes) {
    case 1:
        emit(e, "  ldrb w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 2:
        emit(e, "  ldrh w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 4:
        emit(e, "  ldr w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 8:
        emit(e, "  ldr x%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    default:
        fail(e, "unsupported scalar width %u", bytes);
        return;
    }
    if (sign_extend) {
        emit_sign_extend(e, reg, type);
    }
}

static void emit_store_mem(TcA64 *e, unsigned base, uint64_t offset,
                           unsigned bytes, unsigned reg) {
    uint64_t off = 0;
    unsigned b = emit_address(e, base, offset, &off);
    switch (bytes) {
    case 1:
        emit(e, "  strb w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 2:
        emit(e, "  strh w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 4:
        emit(e, "  str w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 8:
        emit(e, "  str x%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    default:
        fail(e, "unsupported scalar width %u", bytes);
        break;
    }
}

static void emit_epilogue(TcA64 *e) {
    emit(e, "  sub sp, x29, #16\n"
            "  ldp x19, x20, [sp], #16\n"
            "  ldp x29, x30, [sp], #16\n"
            "  ret\n");
}

static void emit_trap(TcA64 *e, const TcSpan *span, TcErrorKind kind) {
    const char *file = NULL;
    size_t index = 0;
    size_t i;
    const TcDiagInfo *info = tc_diag_info((TcDiagCode)kind);
    const char *symbol = info != NULL ? info->symbol : "TC_RE_TRAP";

    if (span != NULL && span->file != NULL) {
        file = span->file;
    } else {
        file = "<source>";
    }
    for (i = 0; i < e->nfiles; i++) {
        if (strcmp(e->files[i], file) == 0) {
            index = i;
            break;
        }
    }
    if (i == e->nfiles) {
        if (e->nfiles < TC_A64_MAX_LOCAL_FILES) {
            e->files[e->nfiles] = file;
            index = e->nfiles;
            e->nfiles++;
        } else {
            index = 0;
        }
    }
    emit(e, "  /* trap %s */\n", symbol);
    emit_mov_imm(e, 0u, (uint64_t)kind);
    emit_mov_imm(e, 1u, span != NULL ? span->line : 0u);
    emit_mov_imm(e, 2u, span != NULL ? span->column : 0u);
    emit(e, "  adrp x3, .Ltcfile_%zu\n"
            "  add x3, x3, :lo12:.Ltcfile_%zu\n"
            "  bl %s\n"
            "  brk #0\n",
         index, index, TC_A64_TRAP_SYMBOL);
}

/* ------------------------------------------------------------------------- */
/* Vreg locations.                                                           */
/* ------------------------------------------------------------------------- */

static int build_layout(TcA64 *e, const TcUnit *unit) {
    uint64_t max_object_end = 0;
    uint64_t temp_end;
    size_t v;

    e->unit = unit;
    e->nloc = unit->nvregs;
    e->loc = (TcLoc *)calloc(unit->nvregs == 0 ? 1u : unit->nvregs,
                             sizeof(TcLoc));
    if (e->loc == NULL) {
        fail(e, "out of memory while laying out %u vregs",
             (unsigned)unit->nvregs);
        return 0;
    }
    for (v = 0; v < unit->nvregs; v++) {
        const TcType *type = unit->vreg_types[v];
        if (!type_is_supported_value(type)) {
            fail(e, "unsupported value type in vreg %u", (unsigned)v);
            return 0;
        }
    }
    temp_end = 0;
    for (v = 0; v < unit->nvregs; v++) {
        uint32_t object_id = UINT32_MAX;
        const TcType *type = unit->vreg_types[v];
        if (unit->vreg_objects != NULL && v < unit->nvreg_objects) {
            object_id = unit->vreg_objects[v];
        }
        if (object_id != UINT32_MAX) {
            const TcObject *object;
            if (object_id >= e->ir->nobjects) {
                fail(e, "vreg %u refers to invalid object %u",
                     (unsigned)v, (unsigned)object_id);
                return 0;
            }
            object = &e->ir->objects[object_id];
            if (object->region == TC_REG_STATIC) {
                e->loc[v].kind = TC_LOC_STATIC;
                e->loc[v].offset = object->offset;
                continue;
            }
            if (object->region != TC_REG_FRAME) {
                fail(e, "vreg %u has a non-frame/static object region",
                     (unsigned)v);
                return 0;
            }
            e->loc[v].kind = TC_LOC_FRAME;
            e->loc[v].offset = object->offset;
            if (object->offset + object->size > max_object_end) {
                max_object_end = object->offset + object->size;
            }
            continue;
        }
        if (type->tag == TC_TY_STRUCT) {
            fail(e, "struct temporaries are not in the AArch64 subset");
            return 0;
        }
        temp_end = (temp_end + 7u) & ~UINT64_C(7);
        e->loc[v].kind = TC_LOC_TEMP;
        e->loc[v].offset = temp_end;
        temp_end += 8u;
    }
    e->has_read = 0;
    for (v = 0; v < unit->nblocks; v++) {
        size_t k;
        for (k = 0; k < unit->blocks[v].n; k++) {
            if (unit->blocks[v].instrs[k].op == TC_READ) {
                e->has_read = 1;
            }
        }
    }
    e->read_scratch = 0;
    if (e->has_read) {
        temp_end = (temp_end + 7u) & ~UINT64_C(7);
        e->read_scratch = temp_end;
        temp_end += 8u;
    }
    if (temp_end < max_object_end) {
        temp_end = max_object_end;
    }
    temp_end = (temp_end + 15u) & ~UINT64_C(15);
    {
        uint64_t stack_limit = e->profile->stack_size;
        uint64_t depth = e->profile->depth_D != 0 ? e->profile->depth_D : 1u;
        if (stack_limit == 0) {
            stack_limit = e->profile->region[TC_REG_FRAME].size;
        }
        if (temp_end > stack_limit ||
            (depth + 1u) > UINT64_MAX / (temp_end == 0 ? 16u : temp_end) ||
            temp_end * (depth + 1u) > stack_limit) {
            fail(e, "frame size %llu cannot satisfy the profile stack budget",
                 (unsigned long long)temp_end);
            return 0;
        }
    }
    e->frame_size = temp_end;
    return 1;
}

static void free_layout(TcA64 *e) {
    free(e->loc);
    e->loc = NULL;
    e->nloc = 0;
    e->unit = NULL;
}

static int vreg_valid(const TcA64 *e, const TcUnit *unit, uint32_t vreg) {
    return unit != NULL && vreg < unit->nvregs && vreg < e->nloc;
}

static void emit_load_vreg(TcA64 *e, const TcUnit *unit, uint32_t vreg,
                           unsigned reg, int sign_extend) {
    const TcType *type;
    unsigned bytes;
    unsigned base;
    if (!vreg_valid(e, unit, vreg)) {
        fail(e, "invalid vreg %u", (unsigned)vreg);
        return;
    }
    type = unit->vreg_types[vreg];
    bytes = type_bytes(type);
    base = e->loc[vreg].kind == TC_LOC_STATIC ? 20u : 19u;
    emit_load_mem(e, base, e->loc[vreg].offset, bytes, reg,
                  sign_extend && type->tag != TC_TY_BOOL, type);
    if (type->tag == TC_TY_BOOL) {
        emit_bool_normalize(e, reg);
    }
}

static void emit_store_vreg(TcA64 *e, const TcUnit *unit, uint32_t vreg,
                            unsigned reg) {
    const TcType *type;
    unsigned bytes;
    unsigned base;
    if (!vreg_valid(e, unit, vreg)) {
        fail(e, "invalid vreg %u", (unsigned)vreg);
        return;
    }
    type = unit->vreg_types[vreg];
    bytes = type_bytes(type);
    base = e->loc[vreg].kind == TC_LOC_STATIC ? 20u : 19u;
    if (type->tag == TC_TY_BOOL) {
        emit_bool_normalize(e, reg);
    } else {
        emit_mask(e, reg, type);
    }
    emit_store_mem(e, base, e->loc[vreg].offset, bytes, reg);
}

static const TcType *operand_type(const TcUnit *unit,
                                  const TcOperand *operand) {
    if (operand == NULL) {
        return NULL;
    }
    if (operand->kind == TC_OP_VREG) {
        if (unit == NULL || operand->vreg >= unit->nvregs) {
            return NULL;
        }
        return unit->vreg_types[operand->vreg];
    }
    if (operand->kind == TC_OP_CONST) {
        return operand->konst.type;
    }
    return NULL;
}

static void emit_load_operand(TcA64 *e, const TcUnit *unit,
                              const TcOperand *operand, unsigned reg,
                              int sign_extend) {
    if (operand->kind == TC_OP_VREG) {
        emit_load_vreg(e, unit, operand->vreg, reg, sign_extend);
        return;
    }
    if (operand->kind == TC_OP_CONST) {
        const TcType *type = operand->konst.type;
        uint64_t bits;
        if (!type_is_supported_value(type)) {
            fail(e, "unsupported constant type in operand");
            return;
        }
        bits = operand->konst.bits & type_mask(type);
        if (type->tag == TC_TY_BOOL) {
            bits = bits != 0 ? 1u : 0u;
        }
        emit_mov_imm(e, reg, bits);
        if (sign_extend) {
            emit_sign_extend(e, reg, type);
        }
        return;
    }
    fail(e, "unsupported operand kind %d", (int)operand->kind);
}

static void emit_store_result(TcA64 *e, const TcUnit *unit,
                              const TcInstr *instr, unsigned reg) {
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (instr->dst.kind != TC_OP_VREG) {
        fail(e, "unsupported result operand kind %d", (int)instr->dst.kind);
        return;
    }
    emit_store_vreg(e, unit, instr->dst.vreg, reg);
}

/* ------------------------------------------------------------------------- */
/* Per-op lowering.                                                          */
/* ------------------------------------------------------------------------- */

static int is_strict_signed(const TcType *type, TcOpMode mode) {
    return tc_type_is_signed_integer(type) && mode != TC_WRAP;
}

static void emit_overflow_trap(TcA64 *e, const TcSpan *span) {
    emit_trap(e, span, TC_EK_INTEGER_OVERFLOW);
}

static void emit_add_sub(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    int strict = is_strict_signed(type, instr->mode);
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, strict);
    emit_load_operand(e, e->unit, &instr->args[1].value, 10u, strict);
    if (strict) {
        unsigned id = fresh_label(e);
        unsigned width = type_width(type);
        emit(e, "  %s x11, x9, x10\n"
                "  b.vs .Lovf_%u\n",
             instr->op == TC_ADD ? "adds" : "subs", id);
        if (width < 64u) {
            emit(e, "  sxt%c x12, w11\n"
                    "  cmp x12, x11\n"
                    "  b.ne .Lovf_%u\n",
                 width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
        }
        emit(e, "  b .Lok_%u\n"
                ".Lovf_%u:\n",
             id, id);
        emit_overflow_trap(e, &instr->span);
        emit(e, ".Lok_%u:\n", id);
        emit_mask(e, 11u, type);
    } else {
        emit(e, "  %s x11, x9, x10\n",
             instr->op == TC_ADD ? "add" : "sub");
        emit_mask(e, 11u, type);
    }
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_mul(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    int strict = is_strict_signed(type, instr->mode);
    unsigned width = type_width(type);
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, strict);
    emit_load_operand(e, e->unit, &instr->args[1].value, 10u, strict);
    if (strict) {
        unsigned id = fresh_label(e);
        emit(e, "  mul x11, x9, x10\n");
        if (width < 64u) {
            emit(e, "  sxt%c x12, w11\n"
                    "  cmp x12, x11\n"
                    "  b.ne .Lovf_%u\n",
                 width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
        } else {
            emit(e, "  smulh x12, x9, x10\n"
                    "  asr x13, x11, #63\n"
                    "  cmp x12, x13\n"
                    "  b.ne .Lovf_%u\n",
                 id);
        }
        emit(e, "  b .Lok_%u\n"
                ".Lovf_%u:\n",
             id, id);
        emit_overflow_trap(e, &instr->span);
        emit(e, ".Lok_%u:\n", id);
        emit_mask(e, 11u, type);
    } else {
        emit(e, "  mul x11, x9, x10\n");
        emit_mask(e, 11u, type);
    }
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_neg_abs(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int strict = is_strict_signed(type, instr->mode);
    unsigned width = type_width(type);
    unsigned id = fresh_label(e);
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, strict);
    if (instr->op == TC_NEG) {
        if (strict) {
            emit(e, "  negs x11, x9\n"
                    "  b.vs .Lovf_%u\n",
                 id);
            if (width < 64u) {
                emit(e, "  sxt%c x12, w11\n"
                        "  cmp x12, x11\n"
                        "  b.ne .Lovf_%u\n",
                     width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
            }
            emit(e, "  b .Lok_%u\n"
                    ".Lovf_%u:\n",
                 id, id);
            emit_overflow_trap(e, &instr->span);
            emit(e, ".Lok_%u:\n", id);
        } else {
            emit(e, "  neg x11, x9\n");
        }
    } else {
        if (is_signed) {
            emit(e, "  cmp x9, #0\n"
                    "  cneg x11, x9, lt\n");
            if (width < 64u) {
                emit(e, "  sxt%c x12, w11\n"
                        "  cmp x12, x11\n"
                        "  b.ne .Lovf_%u\n",
                     width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
            } else {
                emit(e, "  tbnz x11, #63, .Lovf_%u\n", id);
            }
            emit(e, "  b .Lok_%u\n"
                    ".Lovf_%u:\n",
                 id, id);
            emit_overflow_trap(e, &instr->span);
            emit(e, ".Lok_%u:\n", id);
        } else {
            emit(e, "  mov x11, x9\n");
        }
    }
    emit_mask(e, 11u, type);
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_bitwise(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    if (type->tag == TC_TY_BOOL) {
        emit_load_operand(e, e->unit, &instr->args[0].value, 9u, 0);
        emit_load_operand(e, e->unit, &instr->args[1].value, 10u, 0);
        if (instr->op == TC_AND) {
            emit(e, "  and x11, x9, x10\n");
        } else if (instr->op == TC_OR) {
            emit(e, "  orr x11, x9, x10\n");
        } else {
            emit(e, "  eor x11, x9, x10\n");
        }
    } else {
        emit_load_operand(e, e->unit, &instr->args[0].value, 9u, 0);
        emit_load_operand(e, e->unit, &instr->args[1].value, 10u, 0);
        if (instr->op == TC_AND) {
            emit(e, "  and x11, x9, x10\n");
        } else if (instr->op == TC_OR) {
            emit(e, "  orr x11, x9, x10\n");
        } else {
            emit(e, "  eor x11, x9, x10\n");
        }
        emit_mask(e, 11u, type);
    }
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_not(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, 0);
    if (type->tag == TC_TY_BOOL) {
        emit(e, "  eor x11, x9, #1\n");
    } else {
        emit(e, "  mvn x11, x9\n");
        emit_mask(e, 11u, type);
    }
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_div_mod(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int is_mod = instr->op == TC_MOD;
    unsigned width = type_width(type);
    unsigned id = fresh_label(e);
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, is_signed);
    emit_load_operand(e, e->unit, &instr->args[1].value, 10u, is_signed);
    emit(e, "  cbz x10, .Ldivzero_%u\n", id);
    if (is_signed) {
        uint64_t min_bits = (uint64_t)0 - (UINT64_C(1) << (width - 1u));
        emit(e, "  cmn x10, #1\n"
                "  b.ne .Lnormal_%u\n",
             id);
        emit_mov_imm(e, 16u, min_bits);
        emit(e, "  cmp x9, x16\n"
                "  b.ne .Lnormal_%u\n",
             id);
        if (is_mod) {
            emit(e, "  movz x11, #0\n"
                    "  b .Ldone_%u\n",
                 id);
        } else {
            emit(e, "  b .Lovf_%u\n", id);
        }
        emit(e, ".Lnormal_%u:\n"
                "  sdiv x12, x9, x10\n",
             id);
        if (is_mod) {
            emit(e, "  msub x11, x10, x12, x9\n");
        } else {
            emit(e, "  mov x11, x12\n");
        }
        emit_mask(e, 11u, type);
        emit(e, "  b .Ldone_%u\n", id);
        if (!is_mod) {
            emit(e, ".Lovf_%u:\n", id);
            emit_overflow_trap(e, &instr->span);
        }
    } else {
        emit(e, "  udiv x12, x9, x10\n");
        if (is_mod) {
            emit(e, "  msub x11, x10, x12, x9\n");
        } else {
            emit(e, "  mov x11, x12\n");
        }
        emit_mask(e, 11u, type);
    }
    emit(e, ".Ldone_%u:\n", id);
    emit_store_result(e, e->unit, instr, 11u);
    emit(e, "  b .Lfinish_%u\n"
            ".Ldivzero_%u:\n",
         id, id);
    emit_trap(e, &instr->span, TC_EK_DIV_ZERO);
    emit(e, ".Lfinish_%u:\n", id);
}

static void emit_shift(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int is_shl = instr->op == TC_SHL;
    int wrap = instr->mode == TC_WRAP;
    unsigned width = type_width(type);
    unsigned id = fresh_label(e);
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, is_signed);
    emit_load_operand(e, e->unit, &instr->args[1].value, 10u,
                      is_signed);
    if (is_signed) {
        emit(e, "  tbnz x10, #63, .Lneg_%u\n", id);
    }
    emit(e, "  cmp x10, #%u\n"
            "  b.hs .Lbig_%u\n",
         width, id);
    if (is_shl) {
        emit(e, "  lsl x11, x9, x10\n");
        if (wrap) {
            emit_mask(e, 11u, type);
            emit(e, "  b .Ldone_%u\n", id);
        } else if (is_signed) {
            emit(e, "  asr x12, x11, x10\n"
                    "  cmp x12, x9\n"
                    "  b.ne .Lovf_%u\n",
                 id);
            if (width < 64u) {
                emit(e, "  sxt%c x12, w11\n"
                        "  cmp x12, x11\n"
                        "  b.ne .Lovf_%u\n",
                     width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
            }
            emit(e, "  b .Ldone_%u\n", id);
        } else {
            emit(e, "  lsr x12, x11, x10\n"
                    "  cmp x12, x9\n"
                    "  b.ne .Lovf_%u\n",
                 id);
            if (width < 64u) {
                emit(e, "  and x13, x11, #0x%llx\n"
                        "  cmp x13, x11\n"
                        "  b.ne .Lovf_%u\n"
                        "  mov x11, x13\n",
                     (unsigned long long)type_mask(type), id);
            }
            emit(e, "  b .Ldone_%u\n", id);
        }
    } else {
        emit(e, "  %s x11, x9, x10\n",
             is_signed ? "asr" : "lsr");
        emit_mask(e, 11u, type);
        emit(e, "  b .Ldone_%u\n", id);
    }
    emit(e, ".Lbig_%u:\n", id);
    if (is_shl && !wrap) {
        emit(e, "  cbnz x9, .Lovf_%u\n", id);
    }
    emit(e, "  movz x11, #0\n"
            "  b .Ldone_%u\n",
         id);
    if (!is_shl || !wrap) {
        emit(e, ".Lovf_%u:\n", id);
        emit_overflow_trap(e, &instr->span);
    }
    if (is_signed) {
        emit(e, ".Lneg_%u:\n", id);
        emit_trap(e, &instr->span, TC_EK_NEGATIVE_SHIFT);
    }
    emit(e, ".Ldone_%u:\n", id);
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_compare(TcA64 *e, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_bool = type->tag == TC_TY_BOOL;
    int is_signed = tc_type_is_signed_integer(type);
    const char *cond;
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, is_signed);
    emit_load_operand(e, e->unit, &instr->args[1].value, 10u, is_signed);
    if (is_bool) {
        emit_bool_normalize(e, 9u);
        emit_bool_normalize(e, 10u);
    }
    switch (instr->op) {
    case TC_EQ: cond = "eq"; break;
    case TC_NE: cond = "ne"; break;
    case TC_LT: cond = is_signed ? "lt" : "lo"; break;
    case TC_LE: cond = is_signed ? "le" : "ls"; break;
    case TC_GT: cond = is_signed ? "gt" : "hi"; break;
    case TC_GE: cond = is_signed ? "ge" : "hs"; break;
    default: fail(e, "invalid comparison"); return;
    }
    emit(e, "  cmp x9, x10\n"
            "  cset w11, %s\n",
         cond);
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_sizeof(TcA64 *e, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = type_size(e->ir, instr->type, &ok);
    if (!ok) {
        fail(e, "sizeof of an incomplete type");
        return;
    }
    emit_mov_imm(e, 11u, size);
    emit_store_result(e, e->unit, instr, 11u);
}

static void emit_copy(TcA64 *e, const TcInstr *instr) {
    const TcType *src = operand_type(e->unit, &instr->args[0].value);
    if (!type_is_supported_value(src)) {
        fail(e, "copy of an unsupported value type");
        return;
    }
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, 0);
    emit_store_result(e, e->unit, instr, 9u);
}

static void emit_write(TcA64 *e, const TcInstr *instr) {
    unsigned id = fresh_label(e);
    const TcType *type = instr->type;
    if (!type_is_supported_value(type) ||
        (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
        fail(e, "write/writeln of an unsupported type");
        return;
    }
    emit_load_operand(e, e->unit, &instr->args[0].value, 1u, 0);
    emit_mov_imm(e, 0u, (uint64_t)type->tag);
    emit_mov_imm(e, 2u, instr->op == TC_WRITELN ? 1u : 0u);
    emit(e, "  bl %s\n"
            "  cbz w0, .Liook_%u\n",
         TC_A64_WRITE_SYMBOL, id);
    emit_trap(e, &instr->span, TC_EK_IO);
    emit(e, ".Liook_%u:\n", id);
}

static void emit_read(TcA64 *e, const TcInstr *instr) {
    unsigned id = fresh_label(e);
    const TcType *type = instr->type;
    uint64_t off = 0;
    unsigned base;
    if (!type_is_supported_value(type) ||
        (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
        fail(e, "read of an unsupported type");
        return;
    }
    emit_mov_imm(e, 0u, (uint64_t)type->tag);
    base = emit_address(e, 19u, e->read_scratch, &off);
    emit(e, "  add x1, x%u, #%llu\n", base, (unsigned long long)off);
    emit(e, "  bl %s\n"
            "  cbz w0, .Liook_%u\n",
         TC_A64_READ_SYMBOL, id);
    emit_trap(e, &instr->span, TC_EK_IO);
    emit(e, ".Liook_%u:\n", id);
    emit_load_mem(e, 19u, e->read_scratch, 8u, 9u, 0, NULL);
    if (instr->dst.kind != TC_OP_DISCARD) {
        emit_store_result(e, e->unit, instr, 9u);
    }
}

static void emit_call(TcA64 *e, const TcInstr *instr) {
    uint32_t func_id;
    const TcFunc *callee;
    size_t nargs;
    size_t i;
    unsigned id = fresh_label(e);
    if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
        fail(e, "direct call without a function operand");
        return;
    }
    func_id = instr->args[0].value.func;
    if (func_id >= e->ir->nfuncs) {
        fail(e, "direct call to invalid function %u", (unsigned)func_id);
        return;
    }
    callee = &e->ir->funcs[func_id];
    nargs = instr->nargs - 1u;
    if (nargs != callee->sig.nparams) {
        fail(e, "call argument count mismatch");
        return;
    }
    if (nargs > TC_A64_MAX_PARAMS) {
        fail(e, "calls with more than %u arguments are not in the subset",
             (unsigned)TC_A64_MAX_PARAMS);
        return;
    }
    for (i = 0; i < nargs; i++) {
        emit_load_operand(e, e->unit, &instr->args[i + 1u].value,
                          (unsigned)i, 0);
    }
    /* Abstract depth: a call is legal only when depth < D; the callee then
     * runs at depth+1 (standard §1.2, design §10.3). */
    emit(e, "  adrp x9, %s\n"
            "  ldr x10, [x9, :lo12:%s]\n",
         TC_A64_DEPTH_SYMBOL, TC_A64_DEPTH_SYMBOL);
    emit_mov_imm(e, 11u, e->profile->depth_D);
    emit(e, "  cmp x10, x11\n"
            "  b.lo .Ldok_%u\n",
         id);
    emit_trap(e, &instr->span, TC_EK_STACK_OVERFLOW);
    emit(e, ".Ldok_%u:\n"
            "  add x10, x10, #1\n"
            "  str x10, [x9, :lo12:%s]\n"
            "  bl tc_fn_%u\n"
            "  adrp x9, %s\n"
            "  ldr x10, [x9, :lo12:%s]\n"
            "  sub x10, x10, #1\n"
            "  str x10, [x9, :lo12:%s]\n",
         id, TC_A64_DEPTH_SYMBOL, (unsigned)func_id, TC_A64_DEPTH_SYMBOL,
         TC_A64_DEPTH_SYMBOL, TC_A64_DEPTH_SYMBOL);
    if (callee->sig.ret != NULL && callee->sig.ret->tag != TC_TY_VOID) {
        if (instr->dst.kind != TC_OP_DISCARD) {
            if (callee->sig.ret->tag == TC_TY_BOOL) {
                emit_bool_normalize(e, 0u);
            } else {
                emit_mask(e, 0u, callee->sig.ret);
            }
            emit_store_result(e, e->unit, instr, 0u);
        }
    } else if (instr->dst.kind != TC_OP_DISCARD) {
        fail(e, "void call has a result");
    }
}

static void emit_switch(TcA64 *e, const TcInstr *instr) {
    const TcType *type = operand_type(e->unit, &instr->args[0].value);
    size_t ntargets = instr->ntargets;
    size_t i;
    unsigned id = fresh_label(e);
    unsigned width = type_width(type);
    size_t ndispatch;
    if (!tc_type_is_integer(type) || ntargets == 0) {
        fail(e, "switch requires an integer selector and a default target");
        return;
    }
    ndispatch = ntargets - 1u;
    emit_load_operand(e, e->unit, &instr->args[0].value, 9u, 0);
    if (tc_type_is_signed_integer(type)) {
        emit(e, "  tbnz %s9, #%u, .Lswdef_%u\n",
             width == 64u ? "x" : "w", width - 1u, id);
    }
    if (ndispatch == 0) {
        emit(e, "  b .Lswdef_%u\n", id);
    } else {
        if (ndispatch <= 4095u) {
            emit(e, "  cmp x9, #%zu\n", ndispatch);
        } else {
            emit_mov_imm(e, 16u, ndispatch);
            emit(e, "  cmp x9, x16\n");
        }
        emit(e, "  b.hs .Lswdef_%u\n", id);
        for (i = 0; i < ndispatch; i++) {
            if (i <= 4095u) {
                emit(e, "  cmp x9, #%zu\n", i);
            } else {
                emit_mov_imm(e, 16u, i);
                emit(e, "  cmp x9, x16\n");
            }
            emit(e, "  b.eq .Lsw%u_%zu\n", id, i);
        }
        emit(e, "  b .Lswdef_%u\n", id);
    }
    emit(e, ".Lswdef_%u:\n"
            "  b .L%u_b%u\n",
         id, (unsigned)(e->unit - e->ir->units),
         instr->switch_targets[ntargets - 1u]);
    for (i = 0; i < ndispatch; i++) {
        emit(e, ".Lsw%u_%zu:\n"
                "  b .L%u_b%u\n",
             id, i, (unsigned)(e->unit - e->ir->units),
             instr->switch_targets[i]);
    }
}

static void emit_instruction(TcA64 *e, const TcInstr *instr) {
    size_t unit_index = (size_t)(e->unit - e->ir->units);
    switch (instr->op) {
    case TC_ADD:
    case TC_SUB:
        emit_add_sub(e, instr);
        break;
    case TC_MUL:
        emit_mul(e, instr);
        break;
    case TC_NEG:
    case TC_ABS:
        emit_neg_abs(e, instr);
        break;
    case TC_DIV:
    case TC_MOD:
        emit_div_mod(e, instr);
        break;
    case TC_SHL:
    case TC_SHR:
        emit_shift(e, instr);
        break;
    case TC_AND:
    case TC_OR:
    case TC_XOR:
        emit_bitwise(e, instr);
        break;
    case TC_NOT:
        emit_not(e, instr);
        break;
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        emit_compare(e, instr);
        break;
    case TC_SIZEOF:
        emit_sizeof(e, instr);
        break;
    case TC_COPY:
        emit_copy(e, instr);
        break;
    case TC_CALL:
        emit_call(e, instr);
        break;
    case TC_BR:
        if (instr->args[0].value.kind != TC_OP_LABEL ||
            instr->args[0].value.label >= e->unit->nblocks) {
            fail(e, "invalid branch label");
            return;
        }
        emit(e, "  b .L%zu_b%u\n", unit_index,
             instr->args[0].value.label);
        break;
    case TC_BR_IF:
        if (instr->args[1].value.kind != TC_OP_LABEL ||
            instr->args[1].value.label >= e->unit->nblocks) {
            fail(e, "invalid conditional branch label");
            return;
        }
        emit_load_operand(e, e->unit, &instr->args[0].value, 9u, 0);
        emit(e, "  cbnz x9, .L%zu_b%u\n", unit_index,
             instr->args[1].value.label);
        break;
    case TC_SWITCH:
        emit_switch(e, instr);
        break;
    case TC_RETURN:
        if (instr->nargs != 0) {
            const TcType *ret = NULL;
            if (e->unit->func_id != TC_UNIT_TOP_LEVEL &&
                e->unit->func_id < e->ir->nfuncs) {
                ret = e->ir->funcs[e->unit->func_id].sig.ret;
            }
            if (ret == NULL || ret->tag == TC_TY_VOID) {
                fail(e, "return value in a void unit");
                return;
            }
            emit_load_operand(e, e->unit, &instr->args[0].value, 0u, 0);
            if (ret->tag == TC_TY_BOOL) {
                emit_bool_normalize(e, 0u);
            } else {
                emit_mask(e, 0u, ret);
            }
        }
        emit(e, "  b .Lret_%zu\n", unit_index);
        break;
    case TC_TRAP:
        emit_trap(e, &instr->span, TC_EK_TRAP);
        break;
    case TC_READ:
        emit_read(e, instr);
        break;
    case TC_WRITE:
    case TC_WRITELN:
        emit_write(e, instr);
        break;
    default:
        fail(e, "unsupported opcode '%s' at %s:%u:%u",
             opcode_name(instr->op),
             instr->span.file != NULL ? instr->span.file : "<source>",
             instr->span.line, instr->span.column);
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Function and unit emission.                                               */
/* ------------------------------------------------------------------------- */

static void emit_epilogue_label(TcA64 *e, size_t unit_index) {
    emit(e, ".Lret_%zu:\n", unit_index);
    emit_epilogue(e);
}

static void emit_unit(TcA64 *e, const TcUnit *unit, size_t unit_index) {
    const TcFunc *func = NULL;
    const char *symbol;
    size_t b;
    size_t i;
    uint64_t frame_size;

    if (!build_layout(e, unit)) {
        return;
    }
    frame_size = e->frame_size;
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        symbol = TC_A64_TOP_SYMBOL;
    } else {
        if (unit->func_id >= e->ir->nfuncs) {
            fail(e, "unit refers to invalid function %u",
                 (unsigned)unit->func_id);
            free_layout(e);
            return;
        }
        func = &e->ir->funcs[unit->func_id];
        symbol = NULL;
    }
    if (symbol != NULL) {
        emit(e, "\n  .text\n  .p2align 2\n"
                "  .globl %s\n"
                "  .type %s, %%function\n"
                "%s:\n",
             symbol, symbol, symbol);
    } else {
        emit(e, "\n  .text\n  .p2align 2\n"
                "  .type tc_fn_%u, %%function\n"
                "tc_fn_%u:\n",
             (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    emit(e, "  stp x29, x30, [sp, #-16]!\n"
            "  mov x29, sp\n"
            "  stp x19, x20, [sp, #-16]!\n");
    if (frame_size != 0) {
        if (frame_size <= 4095u) {
            emit(e, "  sub sp, sp, #%llu\n",
                 (unsigned long long)frame_size);
        } else {
            emit_mov_imm(e, 16u, frame_size);
            emit(e, "  sub sp, sp, x16\n");
        }
        emit(e, "  mov x19, sp\n");
    } else {
        emit(e, "  mov x19, sp\n");
    }
    emit(e, "  adrp x20, %s\n"
            "  add x20, x20, :lo12:%s\n",
         TC_A64_STATIC_SYMBOL, TC_A64_STATIC_SYMBOL);
    if (func != NULL) {
        size_t nparams = 0;
        if (unit->nvregs != 0 && func->sig.nparams != 0) {
            nparams = func->sig.nparams;
        }
        if (nparams > TC_A64_MAX_PARAMS) {
            fail(e, "function %u has more than %u parameters",
                 (unsigned)unit->func_id, (unsigned)TC_A64_MAX_PARAMS);
            free_layout(e);
            return;
        }
        for (i = 0; i < nparams; i++) {
            const TcType *ptype = func->sig.params[i].type;
            unsigned bytes;
            if (i >= unit->nvregs || !type_is_supported_value(ptype)) {
                fail(e, "unsupported parameter %u of function %u",
                     (unsigned)i, (unsigned)unit->func_id);
                free_layout(e);
                return;
            }
            bytes = type_bytes(ptype);
            if (ptype->tag == TC_TY_BOOL) {
                emit_bool_normalize(e, (unsigned)i);
            }
            emit_store_mem(e, 19u, e->loc[i].offset, bytes, (unsigned)i);
        }
    }
    for (b = 0; b < unit->nblocks; b++) {
        emit(e, ".L%zu_b%zu:\n", unit_index, b);
        for (i = 0; i < unit->blocks[b].n; i++) {
            emit_instruction(e, &unit->blocks[b].instrs[i]);
            if (e->failed) {
                free_layout(e);
                return;
            }
        }
    }
    emit_epilogue_label(e, unit_index);
    if (func == NULL) {
        emit(e, "\n  .size %s, .-%s\n", TC_A64_TOP_SYMBOL,
             TC_A64_TOP_SYMBOL);
    } else {
        emit(e, "\n  .size tc_fn_%u, .-tc_fn_%u\n",
             (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    free_layout(e);
}

/* ------------------------------------------------------------------------- */
/* Static data and file names.                                               */
/* ------------------------------------------------------------------------- */

static int emit_static_data(TcA64 *e) {
    uint64_t size = 0;
    uint8_t *bytes = NULL;
    size_t i;
    uint64_t pos;
    for (i = 0; i < e->ir->nobjects; i++) {
        const TcObject *object = &e->ir->objects[i];
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        if (object->offset > UINT64_MAX - object->size) {
            fail(e, "static object offset overflows the address space");
            return 0;
        }
        if (object->offset + object->size > size) {
            size = object->offset + object->size;
        }
    }
    if (size > (uint64_t)SIZE_MAX) {
        fail(e, "static data is too large");
        return 0;
    }
    bytes = (uint8_t *)calloc((size_t)(size == 0 ? 1u : size), 1u);
    if (bytes == NULL) {
        fail(e, "out of memory while building static data");
        return 0;
    }
    for (i = 0; i < e->ir->nobjects; i++) {
        const TcObject *object = &e->ir->objects[i];
        if (object->region != TC_REG_STATIC || object->init == NULL) {
            continue;
        }
        if (object->init->size > object->size ||
            object->init->size > size - object->offset) {
            free(bytes);
            fail(e, "static object initializer overflows its object");
            return 0;
        }
        if (object->init->size != 0) {
            if (object->init->bytes == NULL) {
                free(bytes);
                fail(e, "static object initializer has no bytes");
                return 0;
            }
            memcpy(bytes + object->offset, object->init->bytes,
                   object->init->size);
        }
    }
    emit(e, "\n  .data\n  .p2align 3\n"
            "  .globl %s\n"
            "  .type %s, %%object\n"
            "  .size %s, %llu\n"
            "%s:\n",
         TC_A64_STATIC_SYMBOL, TC_A64_STATIC_SYMBOL,
         TC_A64_STATIC_SYMBOL, (unsigned long long)size,
         TC_A64_STATIC_SYMBOL);
    pos = 0;
    while (pos < size) {
        uint64_t run = 0;
        while (pos + run < size && bytes[pos + run] == 0) {
            run++;
        }
        if (run >= 8u) {
            emit(e, "  .zero %llu\n", (unsigned long long)run);
            pos += run;
            continue;
        }
        emit(e, "  .byte 0x%02x\n", (unsigned)bytes[pos]);
        pos++;
    }
    if (size == 0) {
        emit(e, "  .byte 0\n");
    }
    free(bytes);
    return 1;
}

static void emit_file_strings(TcA64 *e) {
    size_t i;
    if (e->nfiles == 0) {
        return;
    }
    emit(e, "\n  .section .rodata\n");
    for (i = 0; i < e->nfiles; i++) {
        const char *p = e->files[i] != NULL ? e->files[i] : "<source>";
        emit(e, ".Ltcfile_%zu:\n  .asciz \"", i);
        while (*p != '\0') {
            unsigned char c = (unsigned char)*p++;
            if (c == '"' || c == '\\') {
                emit(e, "\\%c", c);
            } else if (c >= 0x20 && c < 0x7f) {
                emit(e, "%c", c);
            } else {
                emit(e, "\\%03o", (unsigned)c);
            }
        }
        emit(e, "\"\n");
    }
}

/* ------------------------------------------------------------------------- */
/* Preflight validation.                                                     */
/* ------------------------------------------------------------------------- */

static int check_signature(TcA64 *e, const TcFunc *func) {
    size_t i;
    if (func->sig.nparams > TC_A64_MAX_PARAMS) {
        fail(e, "function '%s' has more than %u parameters",
             func->name != NULL ? func->name : "?",
             (unsigned)TC_A64_MAX_PARAMS);
        return 0;
    }
    for (i = 0; i < func->sig.nparams; i++) {
        if (!type_is_supported_value(func->sig.params[i].type)) {
            fail(e, "function '%s' parameter %u uses an unsupported type",
                 func->name != NULL ? func->name : "?", (unsigned)i);
            return 0;
        }
    }
    if (func->sig.ret != NULL && func->sig.ret->tag != TC_TY_VOID &&
        !type_is_supported_value(func->sig.ret)) {
        fail(e, "function '%s' returns an unsupported type",
             func->name != NULL ? func->name : "?");
        return 0;
    }
    if (func->body == NULL) {
        fail(e, "function '%s' has no body",
             func->name != NULL ? func->name : "?");
        return 0;
    }
    return 1;
}

static int check_instruction(TcA64 *e, const TcUnit *unit,
                             const TcInstr *instr) {
    const TcType *type = instr->type;
    size_t nargs = instr->nargs;
    switch (instr->op) {
    case TC_ADD:
    case TC_SUB:
    case TC_MUL:
    case TC_DIV:
    case TC_MOD:
    case TC_SHL:
    case TC_SHR:
    case TC_AND:
    case TC_OR:
    case TC_XOR:
        if (type == NULL ||
            (!tc_type_is_integer(type) && type->tag != TC_TY_BOOL)) {
            fail(e, "opcode '%s' needs an integer/bool type at %s:%u:%u",
                 opcode_name(instr->op),
                 instr->span.file != NULL ? instr->span.file : "<source>",
                 instr->span.line, instr->span.column);
            return 0;
        }
        if (type->tag == TC_TY_BOOL &&
            (instr->op == TC_ADD || instr->op == TC_SUB ||
             instr->op == TC_MUL || instr->op == TC_DIV ||
             instr->op == TC_MOD || instr->op == TC_SHL ||
             instr->op == TC_SHR)) {
            fail(e, "arithmetic on bool at %s:%u:%u",
                 instr->span.file != NULL ? instr->span.file : "<source>",
                 instr->span.line, instr->span.column);
            return 0;
        }
        if (instr->op != TC_NOT && nargs != 2) {
            fail(e, "binary opcode '%s' needs two operands",
                 opcode_name(instr->op));
            return 0;
        }
        break;
    case TC_NEG:
    case TC_ABS:
    case TC_NOT:
        if (type == NULL ||
            (!tc_type_is_integer(type) && type->tag != TC_TY_BOOL)) {
            fail(e, "opcode '%s' needs an integer/bool type",
                 opcode_name(instr->op));
            return 0;
        }
        if (nargs != 1) {
            fail(e, "unary opcode '%s' needs one operand",
                 opcode_name(instr->op));
            return 0;
        }
        if (type->tag == TC_TY_BOOL && instr->op != TC_NOT) {
            fail(e, "neg/abs on bool");
            return 0;
        }
        break;
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        if (type == NULL ||
            (!tc_type_is_integer(type) && type->tag != TC_TY_BOOL)) {
            fail(e, "comparison needs an integer/bool type");
            return 0;
        }
        if (type->tag == TC_TY_BOOL && instr->op != TC_EQ &&
            instr->op != TC_NE) {
            fail(e, "bool supports only eq/ne");
            return 0;
        }
        if (nargs != 2) {
            fail(e, "comparison needs two operands");
            return 0;
        }
        break;
    case TC_SIZEOF: {
        int ok = 0;
        uint64_t size = type_size(e->ir, type, &ok);
        if (!ok || size == 0) {
            fail(e, "sizeof of an incomplete or zero-sized type");
            return 0;
        }
        break;
    }
    case TC_COPY: {
        const TcType *src;
        if (nargs != 1) {
            fail(e, "copy needs one operand");
            return 0;
        }
        src = operand_type(unit, &instr->args[0].value);
        if (!type_is_supported_value(src)) {
            fail(e, "copy of an unsupported value type");
            return 0;
        }
        break;
    }
    case TC_CALL: {
        uint32_t func_id;
        const TcFunc *callee;
        if (nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
            fail(e, "direct call without a function operand");
            return 0;
        }
        func_id = instr->args[0].value.func;
        if (func_id >= e->ir->nfuncs) {
            fail(e, "direct call to invalid function %u",
                 (unsigned)func_id);
            return 0;
        }
        callee = &e->ir->funcs[func_id];
        if (nargs - 1u != callee->sig.nparams) {
            fail(e, "call argument count mismatch");
            return 0;
        }
        if (nargs - 1u > TC_A64_MAX_PARAMS) {
            fail(e, "calls with more than %u arguments are not in the subset",
                 (unsigned)TC_A64_MAX_PARAMS);
            return 0;
        }
        if (callee->sig.ret != NULL && callee->sig.ret->tag != TC_TY_VOID &&
            !type_is_supported_value(callee->sig.ret)) {
            fail(e, "call returns an unsupported type");
            return 0;
        }
        if ((callee->sig.ret == NULL ||
             callee->sig.ret->tag == TC_TY_VOID) &&
            instr->dst.kind != TC_OP_DISCARD) {
            fail(e, "void call has a result");
            return 0;
        }
        break;
    }
    case TC_BR:
        if (nargs != 1 || instr->args[0].value.kind != TC_OP_LABEL ||
            instr->args[0].value.label >= unit->nblocks) {
            fail(e, "invalid branch");
            return 0;
        }
        break;
    case TC_BR_IF:
        if (nargs != 2 || instr->args[1].value.kind != TC_OP_LABEL ||
            instr->args[1].value.label >= unit->nblocks) {
            fail(e, "invalid conditional branch");
            return 0;
        }
        {
            const TcType *cond =
                operand_type(unit, &instr->args[0].value);
            if (cond == NULL || cond->tag != TC_TY_BOOL) {
                fail(e, "conditional branch needs a bool condition");
                return 0;
            }
        }
        break;
    case TC_SWITCH: {
        const TcType *selector =
            operand_type(unit, &instr->args[0].value);
        if (selector == NULL || !tc_type_is_integer(selector) ||
            instr->ntargets == 0 || nargs != 1) {
            fail(e, "invalid switch shape");
            return 0;
        }
        break;
    }
    case TC_RETURN:
        if (nargs > 1) {
            fail(e, "return takes at most one operand");
            return 0;
        }
        break;
    case TC_TRAP:
        if (nargs != 0) {
            fail(e, "trap takes no operands");
            return 0;
        }
        break;
    case TC_READ:
        if (!type_is_supported_value(type) ||
            (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
            fail(e, "read of an unsupported type");
            return 0;
        }
        break;
    case TC_WRITE:
    case TC_WRITELN:
        if (!type_is_supported_value(type) ||
            (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
            fail(e, "write/writeln of an unsupported type");
            return 0;
        }
        if (nargs != 1) {
            fail(e, "write/writeln needs one operand");
            return 0;
        }
        break;
    default:
        fail(e, "unsupported opcode '%s' at %s:%u:%u",
             opcode_name(instr->op),
             instr->span.file != NULL ? instr->span.file : "<source>",
             instr->span.line, instr->span.column);
        return 0;
    }
    return 1;
}

static int preflight(TcA64 *e) {
    size_t u;
    size_t b;
    size_t i;
    if (e->ir->profile != NULL &&
        e->ir->profile->addr_width != e->profile->addr_width) {
        fail(e, "IR profile width does not match the emitter profile");
        return 0;
    }
    if (e->profile->addr_width != 64u ||
        e->profile->target == NULL ||
        e->profile->target->addr_width != 64u ||
        !e->profile->little_endian) {
        fail(e, "the AArch64 subset requires a 64-bit little-endian profile");
        return 0;
    }
    for (i = 0; i < e->ir->nfuncs; i++) {
        if (!check_signature(e, &e->ir->funcs[i])) {
            return 0;
        }
    }
    for (u = 0; u < e->ir->nunits; u++) {
        const TcUnit *unit = &e->ir->units[u];
        size_t v;
        if (unit->entry >= unit->nblocks) {
            fail(e, "unit entry block is invalid");
            return 0;
        }
        for (v = 0; v < unit->nvregs; v++) {
            if (!type_is_supported_value(unit->vreg_types[v])) {
                fail(e, "unsupported value type in vreg %u", (unsigned)v);
                return 0;
            }
            if (unit->vreg_objects != NULL && v < unit->nvreg_objects &&
                unit->vreg_objects[v] != UINT32_MAX) {
                uint32_t oid = unit->vreg_objects[v];
                if (oid >= e->ir->nobjects) {
                    fail(e, "vreg %u refers to invalid object %u",
                         (unsigned)v, (unsigned)oid);
                    return 0;
                }
                if (e->ir->objects[oid].region != TC_REG_FRAME &&
                    e->ir->objects[oid].region != TC_REG_STATIC) {
                    fail(e, "vreg %u has an unsupported object region",
                         (unsigned)v);
                    return 0;
                }
            }
        }
        for (b = 0; b < unit->nblocks; b++) {
            for (i = 0; i < unit->blocks[b].n; i++) {
                if (!check_instruction(e, unit,
                                       &unit->blocks[b].instrs[i])) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Entry point.                                                              */
/* ------------------------------------------------------------------------- */

TcA64Status tc_a64_emit_asm(FILE *out, const TcIR *ir,
                            const TcPlatformProfile *profile,
                            const TcTargetDesc *target, char *err,
                            size_t errcap) {
    TcA64 e;
    size_t u;
    int saw_top = 0;
    if (out == NULL || ir == NULL || profile == NULL || target == NULL) {
        if (err != NULL && errcap != 0) {
            snprintf(err, errcap, "missing emitter argument");
        }
        return TC_A64_INTERNAL;
    }
    if (target != profile->target) {
        if (err != NULL && errcap != 0) {
            snprintf(err, errcap,
                     "target descriptor does not match the profile");
        }
        return TC_A64_INTERNAL;
    }
    memset(&e, 0, sizeof(e));
    e.ir = ir;
    e.profile = profile;
    e.out = out;
    e.err = err;
    e.errcap = errcap;
    if (!preflight(&e)) {
        /* Preflight failures are capability limits (unsupported opcode/type/
         * ABI) after the static stages accepted the program.  Emission
         * failures below remain internal errors. */
        return TC_A64_UNSUPPORTED;
    }
    emit(&e, "  /* TC 0.0.46 AArch64 subset assembly (profile %s) */\n",
         profile->target != NULL && profile->target->name != NULL
             ? profile->target->name
             : "?");
    for (u = 0; u < ir->nunits; u++) {
        if (ir->units[u].func_id == TC_UNIT_TOP_LEVEL) {
            saw_top = 1;
        }
        emit_unit(&e, &ir->units[u], u);
        if (e.failed) {
            break;
        }
    }
    if (!e.failed && !saw_top) {
        fail(&e, "program has no top-level unit");
    }
    if (!e.failed) {
        emit_static_data(&e);
    }
    if (!e.failed) {
        emit_file_strings(&e);
    }
    if (!e.failed) {
        emit(&e, "\n  .section .note.GNU-stack,\"\",%%progbits\n");
    }
    if (e.failed || ferror(out)) {
        return TC_A64_INTERNAL;
    }
    return TC_A64_OK;
}
