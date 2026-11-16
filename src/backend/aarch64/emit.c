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

#define TC_AARCH64_MAX_SOURCE_FILES 256u
#define TC_AARCH64_MAX_PARAMETERS 8u

typedef enum {
    TC_AARCH64_SLOT_FRAME = 0,
    TC_AARCH64_SLOT_STATIC,
    TC_AARCH64_SLOT_TEMPORARY
} TcAArch64SlotKind;

typedef struct {
    TcAArch64SlotKind kind;
    uint64_t offset;
} TcAArch64StackSlot;

typedef struct {
    const TcIR *ir;
    const TcPlatformProfile *profile;
    FILE *out;
    char *error_message;
    size_t error_capacity;
    int failed;
    int output_failed;
    unsigned next_label;

    const char *source_files[TC_AARCH64_MAX_SOURCE_FILES];
    size_t source_file_count;

    /* Current unit layout. */
    const TcUnit *unit;
    TcAArch64StackSlot *slots;
    size_t slot_count;
    uint64_t frame_size;
    uint64_t read_scratch_offset;
    int unit_has_read;
} TcAArch64Emitter;

static void emit_error(TcAArch64Emitter *emitter, const char *fmt, ...) {
    va_list ap;
    if (emitter == NULL || emitter->failed) {
        return;
    }
    emitter->failed = 1;
    if (emitter->error_message != NULL && emitter->error_capacity != 0) {
        va_start(ap, fmt);
        vsnprintf(emitter->error_message, emitter->error_capacity, fmt, ap);
        va_end(ap);
    }
}

static const char *aarch64_opcode_name(TcOpcode op) {
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

static void write_assembly(TcAArch64Emitter *emitter, const char *fmt, ...) {
    va_list ap;
    if (emitter == NULL || emitter->failed) {
        return;
    }
    va_start(ap, fmt);
    if (vfprintf(emitter->out, fmt, ap) < 0) {
        emitter->output_failed = 1;
        emitter->failed = 1;
    }
    va_end(ap);
}

/* ------------------------------------------------------------------------- */
/* Types and sizes.                                                          */
/* ------------------------------------------------------------------------- */

static unsigned aarch64_type_width(const TcType *type) {
    return tc_type_bits(type, 64u);
}

/* Storage bytes for a scalar value type (bool is one byte). */
static unsigned aarch64_type_bytes(const TcType *type) {
    return aarch64_type_width(type) / 8u;
}

static int aarch64_type_is_supported_value(const TcType *type) {
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

static uint64_t aarch64_type_size(const TcIR *ir, const TcType *type, int *ok) {
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
        unsigned width = aarch64_type_width(type);
        if (width == 0) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return width / 8u;
    }
}

static uint64_t aarch64_type_mask(const TcType *type) {
    unsigned width = aarch64_type_width(type);
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

static unsigned allocate_label(TcAArch64Emitter *emitter) {
    return emitter->next_label++;
}

static void emit_move_immediate(TcAArch64Emitter *emitter, unsigned reg,
                                uint64_t value) {
    unsigned shift;
    int first = 1;
    if (value == 0) {
        write_assembly(emitter, "  movz x%u, #0\n", reg);
        return;
    }
    for (shift = 0; shift < 64u; shift += 16u) {
        uint64_t chunk = (value >> shift) & UINT64_C(0xffff);
        if (chunk == 0) {
            continue;
        }
        if (first) {
            write_assembly(emitter, "  movz x%u, #0x%llx", reg,
                 (unsigned long long)chunk);
            first = 0;
        } else {
            write_assembly(emitter, "  movk x%u, #0x%llx", reg,
                 (unsigned long long)chunk);
        }
        if (shift != 0) {
            write_assembly(emitter, ", lsl #%u", shift);
        }
        write_assembly(emitter, "\n");
    }
}

static void emit_mask(TcAArch64Emitter *emitter, unsigned reg, const TcType *type) {
    uint64_t mask = aarch64_type_mask(type);
    if (type == NULL || type->tag == TC_TY_BOOL) {
        write_assembly(emitter, "  and x%u, x%u, #1\n", reg, reg);
        return;
    }
    if (mask == UINT64_MAX) {
        return;
    }
    if (mask == UINT64_C(0xff) || mask == UINT64_C(0xffff) ||
        mask == UINT64_C(0xffffffff)) {
        write_assembly(emitter, "  and x%u, x%u, #0x%llx\n", reg, reg,
             (unsigned long long)mask);
        return;
    }
    emit_move_immediate(emitter, 16u, mask);
    write_assembly(emitter, "  and x%u, x%u, x16\n", reg, reg);
}

static void emit_sign_extend(TcAArch64Emitter *emitter, unsigned reg,
                             const TcType *type) {
    if (type == NULL) {
        return;
    }
    switch (type->tag) {
    case TC_TY_I8:
        write_assembly(emitter, "  sxtb x%u, w%u\n", reg, reg);
        break;
    case TC_TY_I16:
        write_assembly(emitter, "  sxth x%u, w%u\n", reg, reg);
        break;
    case TC_TY_I32:
        write_assembly(emitter, "  sxtw x%u, w%u\n", reg, reg);
        break;
    default:
        break;
    }
}

static void emit_bool_normalize(TcAArch64Emitter *emitter, unsigned reg) {
    write_assembly(emitter, "  cmp w%u, #0\n  cset w%u, ne\n", reg, reg);
}

/* Materialize `base + offset` in x17 and return the effective base/offset. */
static unsigned materialize_address(TcAArch64Emitter *emitter,
                                    unsigned base, uint64_t offset,
                                    uint64_t *out_offset) {
    if (offset <= 255u) {
        *out_offset = offset;
        return base;
    }
    emit_move_immediate(emitter, 17u, offset);
    write_assembly(emitter, "  add x17, x%u, x17\n", base);
    *out_offset = 0;
    return 17u;
}

static void emit_load_from_memory(TcAArch64Emitter *emitter, unsigned base,
                                  uint64_t offset, unsigned bytes,
                                  unsigned reg, int sign_extend,
                                  const TcType *type) {
    uint64_t off = 0;
    unsigned b = materialize_address(emitter, base, offset, &off);
    switch (bytes) {
    case 1:
        write_assembly(emitter, "  ldrb w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 2:
        write_assembly(emitter, "  ldrh w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 4:
        write_assembly(emitter, "  ldr w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 8:
        write_assembly(emitter, "  ldr x%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    default:
        emit_error(emitter, "unsupported scalar width %u", bytes);
        return;
    }
    if (sign_extend) {
        emit_sign_extend(emitter, reg, type);
    }
}

static void emit_store_to_memory(TcAArch64Emitter *emitter, unsigned base,
                                 uint64_t offset, unsigned bytes,
                                 unsigned reg) {
    uint64_t off = 0;
    unsigned b = materialize_address(emitter, base, offset, &off);
    switch (bytes) {
    case 1:
        write_assembly(emitter, "  strb w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 2:
        write_assembly(emitter, "  strh w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 4:
        write_assembly(emitter, "  str w%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    case 8:
        write_assembly(emitter, "  str x%u, [x%u, #%llu]\n", reg, b,
             (unsigned long long)off);
        break;
    default:
        emit_error(emitter, "unsupported scalar width %u", bytes);
        break;
    }
}

static void emit_epilogue(TcAArch64Emitter *emitter) {
    write_assembly(emitter, "  sub sp, x29, #16\n"
            "  ldp x19, x20, [sp], #16\n"
            "  ldp x29, x30, [sp], #16\n"
            "  ret\n");
}

static void emit_trap(TcAArch64Emitter *emitter, const TcSpan *span, TcErrorKind kind) {
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
    for (i = 0; i < emitter->source_file_count; i++) {
        if (strcmp(emitter->source_files[i], file) == 0) {
            index = i;
            break;
        }
    }
    if (i == emitter->source_file_count) {
        if (emitter->source_file_count < TC_AARCH64_MAX_SOURCE_FILES) {
            emitter->source_files[emitter->source_file_count] = file;
            index = emitter->source_file_count;
            emitter->source_file_count++;
        } else {
            emit_error(emitter,
                       "too many source files to emit trap locations");
            return;
        }
    }
    write_assembly(emitter, "  /* trap %s */\n", symbol);
    emit_move_immediate(emitter, 0u, (uint64_t)kind);
    emit_move_immediate(emitter, 1u, span != NULL ? span->line : 0u);
    emit_move_immediate(emitter, 2u, span != NULL ? span->column : 0u);
    write_assembly(emitter, "  adrp x3, .Lsource_file_%zu\n"
            "  add x3, x3, :lo12:.Lsource_file_%zu\n"
            "  bl %s\n"
            "  brk #0\n",
         index, index, TC_AARCH64_TRAP_SYMBOL);
}

/* ------------------------------------------------------------------------- */
/* Vreg locations.                                                           */
/* ------------------------------------------------------------------------- */

static int build_stack_layout(TcAArch64Emitter *emitter, const TcUnit *unit) {
    uint64_t max_object_end = 0;
    uint64_t temp_end;
    size_t v;

    emitter->unit = unit;
    emitter->slot_count = unit->nvregs;
    emitter->slots = (TcAArch64StackSlot *)calloc(unit->nvregs == 0 ? 1u : unit->nvregs,
                             sizeof(TcAArch64StackSlot));
    if (emitter->slots == NULL) {
        emit_error(emitter, "out of memory while laying out %u vregs",
             (unsigned)unit->nvregs);
        return 0;
    }
    for (v = 0; v < unit->nvregs; v++) {
        const TcType *type = unit->vreg_types[v];
        if (!aarch64_type_is_supported_value(type)) {
            emit_error(emitter, "unsupported value type in vreg %u", (unsigned)v);
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
            if (object_id >= emitter->ir->nobjects) {
                emit_error(emitter, "vreg %u refers to invalid object %u",
                     (unsigned)v, (unsigned)object_id);
                return 0;
            }
            object = &emitter->ir->objects[object_id];
            if (object->region == TC_REG_STATIC) {
                emitter->slots[v].kind = TC_AARCH64_SLOT_STATIC;
                emitter->slots[v].offset = object->offset;
                continue;
            }
            if (object->region != TC_REG_FRAME) {
                emit_error(emitter, "vreg %u has a non-frame/static object region",
                     (unsigned)v);
                return 0;
            }
            emitter->slots[v].kind = TC_AARCH64_SLOT_FRAME;
            emitter->slots[v].offset = object->offset;
            if (object->offset + object->size > max_object_end) {
                max_object_end = object->offset + object->size;
            }
            continue;
        }
        if (type->tag == TC_TY_STRUCT) {
            emit_error(emitter, "struct temporaries are not in the AArch64 subset");
            return 0;
        }
        temp_end = (temp_end + 7u) & ~UINT64_C(7);
        emitter->slots[v].kind = TC_AARCH64_SLOT_TEMPORARY;
        emitter->slots[v].offset = temp_end;
        temp_end += 8u;
    }
    emitter->unit_has_read = 0;
    for (v = 0; v < unit->nblocks; v++) {
        size_t k;
        for (k = 0; k < unit->blocks[v].n; k++) {
            if (unit->blocks[v].instrs[k].op == TC_READ) {
                emitter->unit_has_read = 1;
            }
        }
    }
    emitter->read_scratch_offset = 0;
    if (emitter->unit_has_read) {
        temp_end = (temp_end + 7u) & ~UINT64_C(7);
        emitter->read_scratch_offset = temp_end;
        temp_end += 8u;
    }
    if (temp_end < max_object_end) {
        temp_end = max_object_end;
    }
    temp_end = (temp_end + 15u) & ~UINT64_C(15);
    {
        uint64_t stack_limit = emitter->profile->stack_size;
        uint64_t depth = emitter->profile->depth_D != 0
                             ? emitter->profile->depth_D
                             : 1u;
        if (stack_limit == 0) {
            stack_limit = emitter->profile->region[TC_REG_FRAME].size;
        }
        if (temp_end > stack_limit ||
            (depth + 1u) > UINT64_MAX / (temp_end == 0 ? 16u : temp_end) ||
            temp_end * (depth + 1u) > stack_limit) {
            emit_error(emitter,
                       "frame size %llu cannot satisfy the profile stack "
                       "budget",
                       (unsigned long long)temp_end);
            return 0;
        }
    }
    emitter->frame_size = temp_end;
    return 1;
}

static void free_stack_layout(TcAArch64Emitter *emitter) {
    free(emitter->slots);
    emitter->slots = NULL;
    emitter->slot_count = 0;
    emitter->unit = NULL;
}

static int vreg_is_valid(const TcAArch64Emitter *emitter,
                         const TcUnit *unit, uint32_t vreg) {
    return unit != NULL && vreg < unit->nvregs && vreg < emitter->slot_count;
}

static void emit_load_vreg(TcAArch64Emitter *emitter, const TcUnit *unit, uint32_t vreg,
                           unsigned reg, int sign_extend) {
    const TcType *type;
    unsigned bytes;
    unsigned base;
    if (!vreg_is_valid(emitter, unit, vreg)) {
        emit_error(emitter, "invalid vreg %u", (unsigned)vreg);
        return;
    }
    type = unit->vreg_types[vreg];
    bytes = aarch64_type_bytes(type);
    base = emitter->slots[vreg].kind == TC_AARCH64_SLOT_STATIC ? 20u : 19u;
    emit_load_from_memory(emitter, base, emitter->slots[vreg].offset, bytes, reg,
                  sign_extend && type->tag != TC_TY_BOOL, type);
    if (type->tag == TC_TY_BOOL) {
        emit_bool_normalize(emitter, reg);
    }
}

static void emit_store_vreg(TcAArch64Emitter *emitter, const TcUnit *unit,
                            uint32_t vreg, unsigned reg) {
    const TcType *type;
    unsigned bytes;
    unsigned base;
    if (!vreg_is_valid(emitter, unit, vreg)) {
        emit_error(emitter, "invalid vreg %u", (unsigned)vreg);
        return;
    }
    type = unit->vreg_types[vreg];
    bytes = aarch64_type_bytes(type);
    base = emitter->slots[vreg].kind == TC_AARCH64_SLOT_STATIC ? 20u : 19u;
    if (type->tag == TC_TY_BOOL) {
        emit_bool_normalize(emitter, reg);
    } else {
        emit_mask(emitter, reg, type);
    }
    emit_store_to_memory(emitter, base, emitter->slots[vreg].offset, bytes, reg);
}

static const TcType *aarch64_operand_type(const TcUnit *unit,
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

static void emit_load_operand(TcAArch64Emitter *emitter, const TcUnit *unit,
                              const TcOperand *operand, unsigned reg,
                              int sign_extend) {
    if (operand->kind == TC_OP_VREG) {
        emit_load_vreg(emitter, unit, operand->vreg, reg, sign_extend);
        return;
    }
    if (operand->kind == TC_OP_CONST) {
        const TcType *type = operand->konst.type;
        uint64_t bits;
        if (!aarch64_type_is_supported_value(type)) {
            emit_error(emitter, "unsupported constant type in operand");
            return;
        }
        bits = operand->konst.bits & aarch64_type_mask(type);
        if (type->tag == TC_TY_BOOL) {
            bits = bits != 0 ? 1u : 0u;
        }
        emit_move_immediate(emitter, reg, bits);
        if (sign_extend) {
            emit_sign_extend(emitter, reg, type);
        }
        return;
    }
    emit_error(emitter, "unsupported operand kind %d", (int)operand->kind);
}

static void emit_store_result(TcAArch64Emitter *emitter, const TcUnit *unit,
                              const TcInstr *instr, unsigned reg) {
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (instr->dst.kind != TC_OP_VREG) {
        emit_error(emitter, "unsupported result operand kind %d", (int)instr->dst.kind);
        return;
    }
    emit_store_vreg(emitter, unit, instr->dst.vreg, reg);
}

/* ------------------------------------------------------------------------- */
/* Per-op lowering.                                                          */
/* ------------------------------------------------------------------------- */

static int is_strict_signed(const TcType *type, TcOpMode mode) {
    return tc_type_is_signed_integer(type) && mode != TC_WRAP;
}

static void emit_overflow_trap(TcAArch64Emitter *emitter, const TcSpan *span) {
    emit_trap(emitter, span, TC_EK_INTEGER_OVERFLOW);
}

static void emit_add_sub(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int strict = is_strict_signed(type, instr->mode);
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, strict);
    emit_load_operand(emitter, emitter->unit, &instr->args[1].value, 10u, strict);
    if (strict) {
        unsigned id = allocate_label(emitter);
        unsigned width = aarch64_type_width(type);
        write_assembly(emitter, "  %s x11, x9, x10\n"
                "  b.vs .Loverflow_%u\n",
             instr->op == TC_ADD ? "adds" : "subs", id);
        if (width < 64u) {
            write_assembly(emitter, "  sxt%c x12, w11\n"
                    "  cmp x12, x11\n"
                    "  b.ne .Loverflow_%u\n",
                 width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
        }
        write_assembly(emitter, "  b .Lsuccess_%u\n"
                ".Loverflow_%u:\n",
             id, id);
        emit_overflow_trap(emitter, &instr->span);
        write_assembly(emitter, ".Lsuccess_%u:\n", id);
        emit_mask(emitter, 11u, type);
    } else {
        write_assembly(emitter, "  %s x11, x9, x10\n",
             instr->op == TC_ADD ? "add" : "sub");
        emit_mask(emitter, 11u, type);
    }
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_mul(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int strict = is_strict_signed(type, instr->mode);
    unsigned width = aarch64_type_width(type);
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, strict);
    emit_load_operand(emitter, emitter->unit, &instr->args[1].value, 10u, strict);
    if (strict) {
        unsigned id = allocate_label(emitter);
        write_assembly(emitter, "  mul x11, x9, x10\n");
        if (width < 64u) {
            write_assembly(emitter, "  sxt%c x12, w11\n"
                    "  cmp x12, x11\n"
                    "  b.ne .Loverflow_%u\n",
                 width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
        } else {
            write_assembly(emitter, "  smulh x12, x9, x10\n"
                    "  asr x13, x11, #63\n"
                    "  cmp x12, x13\n"
                    "  b.ne .Loverflow_%u\n",
                 id);
        }
        write_assembly(emitter, "  b .Lsuccess_%u\n"
                ".Loverflow_%u:\n",
             id, id);
        emit_overflow_trap(emitter, &instr->span);
        write_assembly(emitter, ".Lsuccess_%u:\n", id);
        emit_mask(emitter, 11u, type);
    } else {
        write_assembly(emitter, "  mul x11, x9, x10\n");
        emit_mask(emitter, 11u, type);
    }
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_neg_abs(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int strict = is_strict_signed(type, instr->mode);
    unsigned width = aarch64_type_width(type);
    unsigned id = allocate_label(emitter);
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, strict);
    if (instr->op == TC_NEG) {
        if (strict) {
            write_assembly(emitter, "  negs x11, x9\n"
                    "  b.vs .Loverflow_%u\n",
                 id);
            if (width < 64u) {
                write_assembly(emitter, "  sxt%c x12, w11\n"
                        "  cmp x12, x11\n"
                        "  b.ne .Loverflow_%u\n",
                     width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
            }
            write_assembly(emitter, "  b .Lsuccess_%u\n"
                    ".Loverflow_%u:\n",
                 id, id);
            emit_overflow_trap(emitter, &instr->span);
            write_assembly(emitter, ".Lsuccess_%u:\n", id);
        } else {
            write_assembly(emitter, "  neg x11, x9\n");
        }
    } else {
        if (is_signed) {
            write_assembly(emitter, "  cmp x9, #0\n"
                    "  cneg x11, x9, lt\n");
            if (width < 64u) {
                write_assembly(emitter, "  sxt%c x12, w11\n"
                        "  cmp x12, x11\n"
                        "  b.ne .Loverflow_%u\n",
                     width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
            } else {
                write_assembly(emitter, "  tbnz x11, #63, .Loverflow_%u\n", id);
            }
            write_assembly(emitter, "  b .Lsuccess_%u\n"
                    ".Loverflow_%u:\n",
                 id, id);
            emit_overflow_trap(emitter, &instr->span);
            write_assembly(emitter, ".Lsuccess_%u:\n", id);
        } else {
            write_assembly(emitter, "  mov x11, x9\n");
        }
    }
    emit_mask(emitter, 11u, type);
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_bitwise(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    if (type->tag == TC_TY_BOOL) {
        emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, 0);
        emit_load_operand(emitter, emitter->unit, &instr->args[1].value, 10u, 0);
        if (instr->op == TC_AND) {
            write_assembly(emitter, "  and x11, x9, x10\n");
        } else if (instr->op == TC_OR) {
            write_assembly(emitter, "  orr x11, x9, x10\n");
        } else {
            write_assembly(emitter, "  eor x11, x9, x10\n");
        }
    } else {
        emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, 0);
        emit_load_operand(emitter, emitter->unit, &instr->args[1].value, 10u, 0);
        if (instr->op == TC_AND) {
            write_assembly(emitter, "  and x11, x9, x10\n");
        } else if (instr->op == TC_OR) {
            write_assembly(emitter, "  orr x11, x9, x10\n");
        } else {
            write_assembly(emitter, "  eor x11, x9, x10\n");
        }
        emit_mask(emitter, 11u, type);
    }
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_not(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, 0);
    if (type->tag == TC_TY_BOOL) {
        write_assembly(emitter, "  eor x11, x9, #1\n");
    } else {
        write_assembly(emitter, "  mvn x11, x9\n");
        emit_mask(emitter, 11u, type);
    }
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_div_mod(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int is_mod = instr->op == TC_MOD;
    unsigned width = aarch64_type_width(type);
    unsigned id = allocate_label(emitter);
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, is_signed);
    emit_load_operand(emitter, emitter->unit, &instr->args[1].value, 10u, is_signed);
    write_assembly(emitter, "  cbz x10, .Ldivision_by_zero_%u\n", id);
    if (is_signed) {
        uint64_t min_bits = (uint64_t)0 - (UINT64_C(1) << (width - 1u));
        write_assembly(emitter, "  cmn x10, #1\n"
                "  b.ne .Lnormal_divisor_%u\n",
             id);
        emit_move_immediate(emitter, 16u, min_bits);
        write_assembly(emitter, "  cmp x9, x16\n"
                "  b.ne .Lnormal_divisor_%u\n",
             id);
        if (is_mod) {
            write_assembly(emitter, "  movz x11, #0\n"
                    "  b .Loperation_done_%u\n",
                 id);
        } else {
            write_assembly(emitter, "  b .Loverflow_%u\n", id);
        }
        write_assembly(emitter, ".Lnormal_divisor_%u:\n"
                "  sdiv x12, x9, x10\n",
             id);
        if (is_mod) {
            write_assembly(emitter, "  msub x11, x10, x12, x9\n");
        } else {
            write_assembly(emitter, "  mov x11, x12\n");
        }
        emit_mask(emitter, 11u, type);
        write_assembly(emitter, "  b .Loperation_done_%u\n", id);
        if (!is_mod) {
            write_assembly(emitter, ".Loverflow_%u:\n", id);
            emit_overflow_trap(emitter, &instr->span);
        }
    } else {
        write_assembly(emitter, "  udiv x12, x9, x10\n");
        if (is_mod) {
            write_assembly(emitter, "  msub x11, x10, x12, x9\n");
        } else {
            write_assembly(emitter, "  mov x11, x12\n");
        }
        emit_mask(emitter, 11u, type);
    }
    write_assembly(emitter, ".Loperation_done_%u:\n", id);
    emit_store_result(emitter, emitter->unit, instr, 11u);
    write_assembly(emitter, "  b .Ldivision_finished_%u\n"
            ".Ldivision_by_zero_%u:\n",
         id, id);
    emit_trap(emitter, &instr->span, TC_EK_DIV_ZERO);
    write_assembly(emitter, ".Ldivision_finished_%u:\n", id);
}

static void emit_shift(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_signed = tc_type_is_signed_integer(type);
    int is_shl = instr->op == TC_SHL;
    int wrap = instr->mode == TC_WRAP;
    unsigned width = aarch64_type_width(type);
    unsigned id = allocate_label(emitter);
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, is_signed);
    emit_load_operand(emitter, emitter->unit, &instr->args[1].value, 10u,
                      is_signed);
    if (is_signed) {
        write_assembly(emitter, "  tbnz x10, #63, .Lnegative_shift_%u\n", id);
    }
    write_assembly(emitter, "  cmp x10, #%u\n"
            "  b.hs .Llarge_shift_%u\n",
         width, id);
    if (is_shl) {
        write_assembly(emitter, "  lsl x11, x9, x10\n");
        if (wrap) {
            emit_mask(emitter, 11u, type);
            write_assembly(emitter, "  b .Loperation_done_%u\n", id);
        } else if (is_signed) {
            write_assembly(emitter, "  asr x12, x11, x10\n"
                    "  cmp x12, x9\n"
                    "  b.ne .Loverflow_%u\n",
                 id);
            if (width < 64u) {
                write_assembly(emitter, "  sxt%c x12, w11\n"
                        "  cmp x12, x11\n"
                        "  b.ne .Loverflow_%u\n",
                     width == 8u ? 'b' : (width == 16u ? 'h' : 'w'), id);
            }
            write_assembly(emitter, "  b .Loperation_done_%u\n", id);
        } else {
            write_assembly(emitter, "  lsr x12, x11, x10\n"
                    "  cmp x12, x9\n"
                    "  b.ne .Loverflow_%u\n",
                 id);
            if (width < 64u) {
                write_assembly(emitter, "  and x13, x11, #0x%llx\n"
                        "  cmp x13, x11\n"
                        "  b.ne .Loverflow_%u\n"
                        "  mov x11, x13\n",
                     (unsigned long long)aarch64_type_mask(type), id);
            }
            write_assembly(emitter, "  b .Loperation_done_%u\n", id);
        }
    } else {
        write_assembly(emitter, "  %s x11, x9, x10\n",
             is_signed ? "asr" : "lsr");
        emit_mask(emitter, 11u, type);
        write_assembly(emitter, "  b .Loperation_done_%u\n", id);
    }
    write_assembly(emitter, ".Llarge_shift_%u:\n", id);
    if (is_shl && !wrap) {
        write_assembly(emitter, "  cbnz x9, .Loverflow_%u\n", id);
    }
    write_assembly(emitter, "  movz x11, #0\n"
            "  b .Loperation_done_%u\n",
         id);
    if (!is_shl || !wrap) {
        write_assembly(emitter, ".Loverflow_%u:\n", id);
        emit_overflow_trap(emitter, &instr->span);
    }
    if (is_signed) {
        write_assembly(emitter, ".Lnegative_shift_%u:\n", id);
        emit_trap(emitter, &instr->span, TC_EK_NEGATIVE_SHIFT);
    }
    write_assembly(emitter, ".Loperation_done_%u:\n", id);
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_compare(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = instr->type;
    int is_bool = type->tag == TC_TY_BOOL;
    int is_signed = tc_type_is_signed_integer(type);
    const char *cond;
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, is_signed);
    emit_load_operand(emitter, emitter->unit, &instr->args[1].value, 10u, is_signed);
    if (is_bool) {
        emit_bool_normalize(emitter, 9u);
        emit_bool_normalize(emitter, 10u);
    }
    switch (instr->op) {
    case TC_EQ: cond = "eq"; break;
    case TC_NE: cond = "ne"; break;
    case TC_LT: cond = is_signed ? "lt" : "lo"; break;
    case TC_LE: cond = is_signed ? "le" : "ls"; break;
    case TC_GT: cond = is_signed ? "gt" : "hi"; break;
    case TC_GE: cond = is_signed ? "ge" : "hs"; break;
    default: emit_error(emitter, "invalid comparison"); return;
    }
    write_assembly(emitter, "  cmp x9, x10\n"
            "  cset w11, %s\n",
         cond);
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_sizeof(TcAArch64Emitter *emitter, const TcInstr *instr) {
    int ok = 0;
    uint64_t size = aarch64_type_size(emitter->ir, instr->type, &ok);
    if (!ok) {
        emit_error(emitter, "sizeof of an incomplete type");
        return;
    }
    emit_move_immediate(emitter, 11u, size);
    emit_store_result(emitter, emitter->unit, instr, 11u);
}

static void emit_copy(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *src = aarch64_operand_type(emitter->unit, &instr->args[0].value);
    if (!aarch64_type_is_supported_value(src)) {
        emit_error(emitter, "copy of an unsupported value type");
        return;
    }
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, 0);
    emit_store_result(emitter, emitter->unit, instr, 9u);
}

static void emit_write(TcAArch64Emitter *emitter, const TcInstr *instr) {
    unsigned id = allocate_label(emitter);
    const TcType *type = instr->type;
    if (!aarch64_type_is_supported_value(type) ||
        (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
        emit_error(emitter, "write/writeln of an unsupported type");
        return;
    }
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 1u, 0);
    emit_move_immediate(emitter, 0u, (uint64_t)type->tag);
    emit_move_immediate(emitter, 2u, instr->op == TC_WRITELN ? 1u : 0u);
    write_assembly(emitter, "  bl %s\n"
            "  cbz w0, .Lio_ok_%u\n",
         TC_AARCH64_WRITE_SYMBOL, id);
    emit_trap(emitter, &instr->span, TC_EK_IO);
    write_assembly(emitter, ".Lio_ok_%u:\n", id);
}

static void emit_read(TcAArch64Emitter *emitter, const TcInstr *instr) {
    unsigned id = allocate_label(emitter);
    const TcType *type = instr->type;
    uint64_t off = 0;
    unsigned base;
    if (!aarch64_type_is_supported_value(type) ||
        (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
        emit_error(emitter, "read of an unsupported type");
        return;
    }
    emit_move_immediate(emitter, 0u, (uint64_t)type->tag);
    base = materialize_address(emitter, 19u, emitter->read_scratch_offset, &off);
    write_assembly(emitter, "  add x1, x%u, #%llu\n", base, (unsigned long long)off);
    write_assembly(emitter, "  bl %s\n"
            "  cbz w0, .Lio_ok_%u\n",
         TC_AARCH64_READ_SYMBOL, id);
    emit_trap(emitter, &instr->span, TC_EK_IO);
    write_assembly(emitter, ".Lio_ok_%u:\n", id);
    emit_load_from_memory(emitter, 19u, emitter->read_scratch_offset, 8u, 9u, 0, NULL);
    if (instr->dst.kind != TC_OP_DISCARD) {
        emit_store_result(emitter, emitter->unit, instr, 9u);
    }
}

static void emit_call(TcAArch64Emitter *emitter, const TcInstr *instr) {
    uint32_t func_id;
    const TcFunc *callee;
    size_t nargs;
    size_t i;
    unsigned id = allocate_label(emitter);
    if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
        emit_error(emitter, "direct call without a function operand");
        return;
    }
    func_id = instr->args[0].value.func;
    if (func_id >= emitter->ir->nfuncs) {
        emit_error(emitter, "direct call to invalid function %u", (unsigned)func_id);
        return;
    }
    callee = &emitter->ir->funcs[func_id];
    nargs = instr->nargs - 1u;
    if (nargs != callee->sig.nparams) {
        emit_error(emitter, "call argument count mismatch");
        return;
    }
    if (nargs > TC_AARCH64_MAX_PARAMETERS) {
        emit_error(emitter, "calls with more than %u arguments are not in the subset",
             (unsigned)TC_AARCH64_MAX_PARAMETERS);
        return;
    }
    for (i = 0; i < nargs; i++) {
        emit_load_operand(emitter, emitter->unit, &instr->args[i + 1u].value,
                          (unsigned)i, 0);
    }
    /* Abstract depth: a call is legal only when depth < D; the callee then
     * runs at depth+1 (standard §1.2, design §10.3). */
    write_assembly(emitter, "  adrp x9, %s\n"
            "  ldr x10, [x9, :lo12:%s]\n",
         TC_AARCH64_CALL_DEPTH_SYMBOL, TC_AARCH64_CALL_DEPTH_SYMBOL);
    emit_move_immediate(emitter, 11u, emitter->profile->depth_D);
    write_assembly(emitter, "  cmp x10, x11\n"
            "  b.lo .Ldepth_ok_%u\n",
         id);
    emit_trap(emitter, &instr->span, TC_EK_STACK_OVERFLOW);
    write_assembly(emitter, ".Ldepth_ok_%u:\n"
            "  add x10, x10, #1\n"
            "  str x10, [x9, :lo12:%s]\n"
            "  bl tc_function_%u\n"
            "  adrp x9, %s\n"
            "  ldr x10, [x9, :lo12:%s]\n"
            "  sub x10, x10, #1\n"
            "  str x10, [x9, :lo12:%s]\n",
         id, TC_AARCH64_CALL_DEPTH_SYMBOL, (unsigned)func_id,
         TC_AARCH64_CALL_DEPTH_SYMBOL, TC_AARCH64_CALL_DEPTH_SYMBOL,
         TC_AARCH64_CALL_DEPTH_SYMBOL);
    if (callee->sig.ret != NULL && callee->sig.ret->tag != TC_TY_VOID) {
        if (instr->dst.kind != TC_OP_DISCARD) {
            if (callee->sig.ret->tag == TC_TY_BOOL) {
                emit_bool_normalize(emitter, 0u);
            } else {
                emit_mask(emitter, 0u, callee->sig.ret);
            }
            emit_store_result(emitter, emitter->unit, instr, 0u);
        }
    } else if (instr->dst.kind != TC_OP_DISCARD) {
        emit_error(emitter, "void call has a result");
    }
}

static void emit_switch(TcAArch64Emitter *emitter, const TcInstr *instr) {
    const TcType *type = aarch64_operand_type(emitter->unit, &instr->args[0].value);
    size_t ntargets = instr->ntargets;
    size_t i;
    unsigned id = allocate_label(emitter);
    unsigned width = aarch64_type_width(type);
    size_t ndispatch;
    if (!tc_type_is_integer(type) || ntargets == 0) {
        emit_error(emitter, "switch requires an integer selector and a default target");
        return;
    }
    ndispatch = ntargets - 1u;
    emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, 0);
    if (tc_type_is_signed_integer(type)) {
        write_assembly(emitter, "  tbnz %s9, #%u, .Lswitch_default_%u\n",
             width == 64u ? "x" : "w", width - 1u, id);
    }
    if (ndispatch == 0) {
        write_assembly(emitter, "  b .Lswitch_default_%u\n", id);
    } else {
        if (ndispatch <= 4095u) {
            write_assembly(emitter, "  cmp x9, #%zu\n", ndispatch);
        } else {
            emit_move_immediate(emitter, 16u, ndispatch);
            write_assembly(emitter, "  cmp x9, x16\n");
        }
        write_assembly(emitter, "  b.hs .Lswitch_default_%u\n", id);
        for (i = 0; i < ndispatch; i++) {
            if (i <= 4095u) {
                write_assembly(emitter, "  cmp x9, #%zu\n", i);
            } else {
                emit_move_immediate(emitter, 16u, i);
                write_assembly(emitter, "  cmp x9, x16\n");
            }
            write_assembly(emitter, "  b.eq .Lswitch_%u_case_%zu\n", id, i);
        }
        write_assembly(emitter, "  b .Lswitch_default_%u\n", id);
    }
    write_assembly(emitter, ".Lswitch_default_%u:\n"
            "  b .L%u_block%u\n",
         id, (unsigned)(emitter->unit - emitter->ir->units),
         instr->switch_targets[ntargets - 1u]);
    for (i = 0; i < ndispatch; i++) {
        write_assembly(emitter, ".Lswitch_%u_case_%zu:\n"
                "  b .L%u_block%u\n",
             id, i, (unsigned)(emitter->unit - emitter->ir->units),
             instr->switch_targets[i]);
    }
}

static void emit_instruction(TcAArch64Emitter *emitter, const TcInstr *instr) {
    size_t unit_index = (size_t)(emitter->unit - emitter->ir->units);
    switch (instr->op) {
    case TC_ADD:
    case TC_SUB:
        emit_add_sub(emitter, instr);
        break;
    case TC_MUL:
        emit_mul(emitter, instr);
        break;
    case TC_NEG:
    case TC_ABS:
        emit_neg_abs(emitter, instr);
        break;
    case TC_DIV:
    case TC_MOD:
        emit_div_mod(emitter, instr);
        break;
    case TC_SHL:
    case TC_SHR:
        emit_shift(emitter, instr);
        break;
    case TC_AND:
    case TC_OR:
    case TC_XOR:
        emit_bitwise(emitter, instr);
        break;
    case TC_NOT:
        emit_not(emitter, instr);
        break;
    case TC_EQ:
    case TC_NE:
    case TC_LT:
    case TC_LE:
    case TC_GT:
    case TC_GE:
        emit_compare(emitter, instr);
        break;
    case TC_SIZEOF:
        emit_sizeof(emitter, instr);
        break;
    case TC_COPY:
        emit_copy(emitter, instr);
        break;
    case TC_CALL:
        emit_call(emitter, instr);
        break;
    case TC_BR:
        if (instr->args[0].value.kind != TC_OP_LABEL ||
            instr->args[0].value.label >= emitter->unit->nblocks) {
            emit_error(emitter, "invalid branch label");
            return;
        }
        write_assembly(emitter, "  b .L%zu_block%u\n", unit_index,
             instr->args[0].value.label);
        break;
    case TC_BR_IF:
        if (instr->args[1].value.kind != TC_OP_LABEL ||
            instr->args[1].value.label >= emitter->unit->nblocks) {
            emit_error(emitter, "invalid conditional branch label");
            return;
        }
        emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 9u, 0);
        write_assembly(emitter, "  cbnz x9, .L%zu_block%u\n", unit_index,
             instr->args[1].value.label);
        break;
    case TC_SWITCH:
        emit_switch(emitter, instr);
        break;
    case TC_RETURN:
        if (instr->nargs != 0) {
            const TcType *ret = NULL;
            if (emitter->unit->func_id != TC_UNIT_TOP_LEVEL &&
                emitter->unit->func_id < emitter->ir->nfuncs) {
                ret = emitter->ir->funcs[emitter->unit->func_id].sig.ret;
            }
            if (ret == NULL || ret->tag == TC_TY_VOID) {
                emit_error(emitter, "return value in a void unit");
                return;
            }
            emit_load_operand(emitter, emitter->unit, &instr->args[0].value, 0u, 0);
            if (ret->tag == TC_TY_BOOL) {
                emit_bool_normalize(emitter, 0u);
            } else {
                emit_mask(emitter, 0u, ret);
            }
        }
        write_assembly(emitter, "  b .Lreturn_%zu\n", unit_index);
        break;
    case TC_TRAP:
        emit_trap(emitter, &instr->span, TC_EK_TRAP);
        break;
    case TC_READ:
        emit_read(emitter, instr);
        break;
    case TC_WRITE:
    case TC_WRITELN:
        emit_write(emitter, instr);
        break;
    default:
        emit_error(emitter, "unsupported opcode '%s' at %s:%u:%u",
             aarch64_opcode_name(instr->op),
             instr->span.file != NULL ? instr->span.file : "<source>",
             instr->span.line, instr->span.column);
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Function and unit emission.                                               */
/* ------------------------------------------------------------------------- */

static void emit_epilogue_label(TcAArch64Emitter *emitter, size_t unit_index) {
    write_assembly(emitter, ".Lreturn_%zu:\n", unit_index);
    emit_epilogue(emitter);
}

static void emit_unit(TcAArch64Emitter *emitter, const TcUnit *unit,
                      size_t unit_index) {
    const TcFunc *func = NULL;
    const char *symbol;
    size_t b;
    size_t i;
    uint64_t frame_size;

    if (!build_stack_layout(emitter, unit)) {
        return;
    }
    frame_size = emitter->frame_size;
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        symbol = TC_AARCH64_TOP_LEVEL_SYMBOL;
    } else {
        if (unit->func_id >= emitter->ir->nfuncs) {
            emit_error(emitter, "unit refers to invalid function %u",
                 (unsigned)unit->func_id);
            free_stack_layout(emitter);
            return;
        }
        func = &emitter->ir->funcs[unit->func_id];
        symbol = NULL;
    }
    if (symbol != NULL) {
        write_assembly(emitter, "\n  .text\n  .p2align 2\n"
                "  .globl %s\n"
                "  .type %s, %%function\n"
                "%s:\n",
             symbol, symbol, symbol);
    } else {
        write_assembly(emitter, "\n  .text\n  .p2align 2\n"
                "  .type tc_function_%u, %%function\n"
                "tc_function_%u:\n",
             (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    write_assembly(emitter, "  stp x29, x30, [sp, #-16]!\n"
            "  mov x29, sp\n"
            "  stp x19, x20, [sp, #-16]!\n");
    if (frame_size != 0) {
        if (frame_size <= 4095u) {
            write_assembly(emitter, "  sub sp, sp, #%llu\n",
                 (unsigned long long)frame_size);
        } else {
            emit_move_immediate(emitter, 16u, frame_size);
            write_assembly(emitter, "  sub sp, sp, x16\n");
        }
        write_assembly(emitter, "  mov x19, sp\n");
    } else {
        write_assembly(emitter, "  mov x19, sp\n");
    }
    write_assembly(emitter, "  adrp x20, %s\n"
            "  add x20, x20, :lo12:%s\n",
         TC_AARCH64_STATIC_REGION_SYMBOL, TC_AARCH64_STATIC_REGION_SYMBOL);
    if (func != NULL) {
        size_t nparams = 0;
        if (unit->nvregs != 0 && func->sig.nparams != 0) {
            nparams = func->sig.nparams;
        }
        if (nparams > TC_AARCH64_MAX_PARAMETERS) {
            emit_error(emitter, "function %u has more than %u parameters",
                 (unsigned)unit->func_id, (unsigned)TC_AARCH64_MAX_PARAMETERS);
            free_stack_layout(emitter);
            return;
        }
        for (i = 0; i < nparams; i++) {
            const TcType *ptype = func->sig.params[i].type;
            unsigned bytes;
            if (i >= unit->nvregs || !aarch64_type_is_supported_value(ptype)) {
                emit_error(emitter, "unsupported parameter %u of function %u",
                     (unsigned)i, (unsigned)unit->func_id);
                free_stack_layout(emitter);
                return;
            }
            bytes = aarch64_type_bytes(ptype);
            if (ptype->tag == TC_TY_BOOL) {
                emit_bool_normalize(emitter, (unsigned)i);
            }
            emit_store_to_memory(emitter, 19u, emitter->slots[i].offset,
                                 bytes, (unsigned)i);
        }
    }
    for (b = 0; b < unit->nblocks; b++) {
        write_assembly(emitter, ".L%zu_block%zu:\n", unit_index, b);
        for (i = 0; i < unit->blocks[b].n; i++) {
            emit_instruction(emitter, &unit->blocks[b].instrs[i]);
            if (emitter->failed) {
                free_stack_layout(emitter);
                return;
            }
        }
    }
    emit_epilogue_label(emitter, unit_index);
    if (func == NULL) {
        write_assembly(emitter, "\n  .size %s, .-%s\n", TC_AARCH64_TOP_LEVEL_SYMBOL,
             TC_AARCH64_TOP_LEVEL_SYMBOL);
    } else {
        write_assembly(emitter, "\n  .size tc_function_%u, .-tc_function_%u\n",
             (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    free_stack_layout(emitter);
}

/* ------------------------------------------------------------------------- */
/* Static data and file names.                                               */
/* ------------------------------------------------------------------------- */

static int emit_static_region(TcAArch64Emitter *emitter) {
    uint64_t size = 0;
    uint8_t *bytes = NULL;
    size_t i;
    uint64_t pos;
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        if (object->offset > UINT64_MAX - object->size) {
            emit_error(emitter, "static object offset overflows the address space");
            return 0;
        }
        if (object->offset + object->size > size) {
            size = object->offset + object->size;
        }
    }
    if (size > (uint64_t)SIZE_MAX) {
        emit_error(emitter, "static data is too large");
        return 0;
    }
    bytes = (uint8_t *)calloc((size_t)(size == 0 ? 1u : size), 1u);
    if (bytes == NULL) {
        emit_error(emitter, "out of memory while building static data");
        return 0;
    }
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        if (object->region != TC_REG_STATIC || object->init == NULL) {
            continue;
        }
        if (object->init->size > object->size ||
            object->init->size > size - object->offset) {
            free(bytes);
            emit_error(emitter, "static object initializer overflows its object");
            return 0;
        }
        if (object->init->size != 0) {
            if (object->init->bytes == NULL) {
                free(bytes);
                emit_error(emitter, "static object initializer has no bytes");
                return 0;
            }
            memcpy(bytes + object->offset, object->init->bytes,
                   object->init->size);
        }
    }
    write_assembly(emitter, "\n  .data\n  .p2align 3\n"
            "  .globl %s\n"
            "  .type %s, %%object\n"
            "  .size %s, %llu\n"
            "%s:\n",
         TC_AARCH64_STATIC_REGION_SYMBOL, TC_AARCH64_STATIC_REGION_SYMBOL,
         TC_AARCH64_STATIC_REGION_SYMBOL, (unsigned long long)size,
         TC_AARCH64_STATIC_REGION_SYMBOL);
    pos = 0;
    while (pos < size) {
        uint64_t run = 0;
        while (pos + run < size && bytes[pos + run] == 0) {
            run++;
        }
        if (run >= 8u) {
            write_assembly(emitter, "  .zero %llu\n", (unsigned long long)run);
            pos += run;
            continue;
        }
        write_assembly(emitter, "  .byte 0x%02x\n", (unsigned)bytes[pos]);
        pos++;
    }
    if (size == 0) {
        write_assembly(emitter, "  .byte 0\n");
    }
    free(bytes);
    return 1;
}

static void emit_source_file_strings(TcAArch64Emitter *emitter) {
    size_t i;
    if (emitter->source_file_count == 0) {
        return;
    }
    write_assembly(emitter, "\n  .section .rodata\n");
    for (i = 0; i < emitter->source_file_count; i++) {
        const char *p = emitter->source_files[i] != NULL
                            ? emitter->source_files[i]
                            : "<source>";
        write_assembly(emitter, ".Lsource_file_%zu:\n  .asciz \"", i);
        while (*p != '\0') {
            unsigned char c = (unsigned char)*p++;
            if (c == '"' || c == '\\') {
                write_assembly(emitter, "\\%c", c);
            } else if (c >= 0x20 && c < 0x7f) {
                write_assembly(emitter, "%c", c);
            } else {
                write_assembly(emitter, "\\%03o", (unsigned)c);
            }
        }
        write_assembly(emitter, "\"\n");
    }
}

/* ------------------------------------------------------------------------- */
/* Preflight validation.                                                     */
/* ------------------------------------------------------------------------- */

static int check_function_signature(TcAArch64Emitter *emitter, const TcFunc *func) {
    size_t i;
    if (func->sig.nparams > TC_AARCH64_MAX_PARAMETERS) {
        emit_error(emitter, "function '%s' has more than %u parameters",
             func->name != NULL ? func->name : "?",
             (unsigned)TC_AARCH64_MAX_PARAMETERS);
        return 0;
    }
    for (i = 0; i < func->sig.nparams; i++) {
        if (!aarch64_type_is_supported_value(func->sig.params[i].type)) {
            emit_error(emitter, "function '%s' parameter %u uses an unsupported type",
                 func->name != NULL ? func->name : "?", (unsigned)i);
            return 0;
        }
    }
    if (func->sig.ret != NULL && func->sig.ret->tag != TC_TY_VOID &&
        !aarch64_type_is_supported_value(func->sig.ret)) {
        emit_error(emitter, "function '%s' returns an unsupported type",
             func->name != NULL ? func->name : "?");
        return 0;
    }
    if (func->body == NULL) {
        emit_error(emitter, "function '%s' has no body",
             func->name != NULL ? func->name : "?");
        return 0;
    }
    return 1;
}

static int check_instruction(TcAArch64Emitter *emitter, const TcUnit *unit,
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
            emit_error(emitter, "opcode '%s' needs an integer/bool type at %s:%u:%u",
                 aarch64_opcode_name(instr->op),
                 instr->span.file != NULL ? instr->span.file : "<source>",
                 instr->span.line, instr->span.column);
            return 0;
        }
        if (type->tag == TC_TY_BOOL &&
            (instr->op == TC_ADD || instr->op == TC_SUB ||
             instr->op == TC_MUL || instr->op == TC_DIV ||
             instr->op == TC_MOD || instr->op == TC_SHL ||
             instr->op == TC_SHR)) {
            emit_error(emitter, "arithmetic on bool at %s:%u:%u",
                 instr->span.file != NULL ? instr->span.file : "<source>",
                 instr->span.line, instr->span.column);
            return 0;
        }
        if (instr->op != TC_NOT && nargs != 2) {
            emit_error(emitter, "binary opcode '%s' needs two operands",
                 aarch64_opcode_name(instr->op));
            return 0;
        }
        break;
    case TC_NEG:
    case TC_ABS:
    case TC_NOT:
        if (type == NULL ||
            (!tc_type_is_integer(type) && type->tag != TC_TY_BOOL)) {
            emit_error(emitter, "opcode '%s' needs an integer/bool type",
                 aarch64_opcode_name(instr->op));
            return 0;
        }
        if (nargs != 1) {
            emit_error(emitter, "unary opcode '%s' needs one operand",
                 aarch64_opcode_name(instr->op));
            return 0;
        }
        if (type->tag == TC_TY_BOOL && instr->op != TC_NOT) {
            emit_error(emitter, "neg/abs on bool");
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
            emit_error(emitter, "comparison needs an integer/bool type");
            return 0;
        }
        if (type->tag == TC_TY_BOOL && instr->op != TC_EQ &&
            instr->op != TC_NE) {
            emit_error(emitter, "bool supports only eq/ne");
            return 0;
        }
        if (nargs != 2) {
            emit_error(emitter, "comparison needs two operands");
            return 0;
        }
        break;
    case TC_SIZEOF: {
        int ok = 0;
        uint64_t size = aarch64_type_size(emitter->ir, type, &ok);
        if (!ok || size == 0) {
            emit_error(emitter, "sizeof of an incomplete or zero-sized type");
            return 0;
        }
        break;
    }
    case TC_COPY: {
        const TcType *src;
        if (nargs != 1) {
            emit_error(emitter, "copy needs one operand");
            return 0;
        }
        src = aarch64_operand_type(unit, &instr->args[0].value);
        if (!aarch64_type_is_supported_value(src)) {
            emit_error(emitter, "copy of an unsupported value type");
            return 0;
        }
        break;
    }
    case TC_CALL: {
        uint32_t func_id;
        const TcFunc *callee;
        if (nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
            emit_error(emitter, "direct call without a function operand");
            return 0;
        }
        func_id = instr->args[0].value.func;
        if (func_id >= emitter->ir->nfuncs) {
            emit_error(emitter, "direct call to invalid function %u",
                 (unsigned)func_id);
            return 0;
        }
        callee = &emitter->ir->funcs[func_id];
        if (nargs - 1u != callee->sig.nparams) {
            emit_error(emitter, "call argument count mismatch");
            return 0;
        }
        if (nargs - 1u > TC_AARCH64_MAX_PARAMETERS) {
            emit_error(emitter,
                       "calls with more than %u arguments are not in the "
                       "subset",
                       (unsigned)TC_AARCH64_MAX_PARAMETERS);
            return 0;
        }
        if (callee->sig.ret != NULL && callee->sig.ret->tag != TC_TY_VOID &&
            !aarch64_type_is_supported_value(callee->sig.ret)) {
            emit_error(emitter, "call returns an unsupported type");
            return 0;
        }
        if ((callee->sig.ret == NULL ||
             callee->sig.ret->tag == TC_TY_VOID) &&
            instr->dst.kind != TC_OP_DISCARD) {
            emit_error(emitter, "void call has a result");
            return 0;
        }
        break;
    }
    case TC_BR:
        if (nargs != 1 || instr->args[0].value.kind != TC_OP_LABEL ||
            instr->args[0].value.label >= unit->nblocks) {
            emit_error(emitter, "invalid branch");
            return 0;
        }
        break;
    case TC_BR_IF:
        if (nargs != 2 || instr->args[1].value.kind != TC_OP_LABEL ||
            instr->args[1].value.label >= unit->nblocks) {
            emit_error(emitter, "invalid conditional branch");
            return 0;
        }
        {
            const TcType *cond =
                aarch64_operand_type(unit, &instr->args[0].value);
            if (cond == NULL || cond->tag != TC_TY_BOOL) {
                emit_error(emitter, "conditional branch needs a bool condition");
                return 0;
            }
        }
        break;
    case TC_SWITCH: {
        const TcType *selector =
            aarch64_operand_type(unit, &instr->args[0].value);
        if (selector == NULL || !tc_type_is_integer(selector) ||
            instr->ntargets == 0 || nargs != 1) {
            emit_error(emitter, "invalid switch shape");
            return 0;
        }
        break;
    }
    case TC_RETURN:
        if (nargs > 1) {
            emit_error(emitter, "return takes at most one operand");
            return 0;
        }
        break;
    case TC_TRAP:
        if (nargs != 0) {
            emit_error(emitter, "trap takes no operands");
            return 0;
        }
        break;
    case TC_READ:
        if (!aarch64_type_is_supported_value(type) ||
            (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
            emit_error(emitter, "read of an unsupported type");
            return 0;
        }
        break;
    case TC_WRITE:
    case TC_WRITELN:
        if (!aarch64_type_is_supported_value(type) ||
            (type->tag != TC_TY_BOOL && !tc_type_is_integer(type))) {
            emit_error(emitter, "write/writeln of an unsupported type");
            return 0;
        }
        if (nargs != 1) {
            emit_error(emitter, "write/writeln needs one operand");
            return 0;
        }
        break;
    default:
        emit_error(emitter, "unsupported opcode '%s' at %s:%u:%u",
             aarch64_opcode_name(instr->op),
             instr->span.file != NULL ? instr->span.file : "<source>",
             instr->span.line, instr->span.column);
        return 0;
    }
    return 1;
}

static int preflight_ir(TcAArch64Emitter *emitter) {
    size_t u;
    size_t b;
    size_t i;
    if (emitter->ir->profile != NULL &&
        emitter->ir->profile->addr_width != emitter->profile->addr_width) {
        emit_error(emitter, "IR profile width does not match the emitter profile");
        return 0;
    }
    if (emitter->profile->addr_width != 64u ||
        emitter->profile->target == NULL ||
        emitter->profile->target->addr_width != 64u ||
        !emitter->profile->little_endian) {
        emit_error(emitter,
                   "the AArch64 subset requires a 64-bit little-endian "
                   "profile");
        return 0;
    }
    for (i = 0; i < emitter->ir->nfuncs; i++) {
        if (!check_function_signature(emitter, &emitter->ir->funcs[i])) {
            return 0;
        }
    }
    for (u = 0; u < emitter->ir->nunits; u++) {
        const TcUnit *unit = &emitter->ir->units[u];
        size_t v;
        if (unit->entry >= unit->nblocks) {
            emit_error(emitter, "unit entry block is invalid");
            return 0;
        }
        for (v = 0; v < unit->nvregs; v++) {
            if (!aarch64_type_is_supported_value(unit->vreg_types[v])) {
                emit_error(emitter, "unsupported value type in vreg %u", (unsigned)v);
                return 0;
            }
            if (unit->vreg_objects != NULL && v < unit->nvreg_objects &&
                unit->vreg_objects[v] != UINT32_MAX) {
                uint32_t oid = unit->vreg_objects[v];
                if (oid >= emitter->ir->nobjects) {
                    emit_error(emitter, "vreg %u refers to invalid object %u",
                         (unsigned)v, (unsigned)oid);
                    return 0;
                }
                if (emitter->ir->objects[oid].region != TC_REG_FRAME &&
                    emitter->ir->objects[oid].region != TC_REG_STATIC) {
                    emit_error(emitter, "vreg %u has an unsupported object region",
                         (unsigned)v);
                    return 0;
                }
            }
        }
        for (b = 0; b < unit->nblocks; b++) {
            for (i = 0; i < unit->blocks[b].n; i++) {
                if (!check_instruction(emitter, unit,
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

TcAArch64EmitStatus tc_aarch64_emit_asm(FILE *out, const TcIR *ir,
                            const TcPlatformProfile *profile,
                            const TcTargetDesc *target, char *err,
                            size_t errcap) {
    TcAArch64Emitter emitter;
    size_t u;
    int saw_top = 0;
    if (out == NULL || ir == NULL || profile == NULL || target == NULL) {
        if (err != NULL && errcap != 0) {
            snprintf(err, errcap, "missing emitter argument");
        }
        return TC_AARCH64_EMIT_INTERNAL;
    }
    if (target != profile->target) {
        if (err != NULL && errcap != 0) {
            snprintf(err, errcap,
                     "target descriptor does not match the profile");
        }
        return TC_AARCH64_EMIT_INTERNAL;
    }
    memset(&emitter, 0, sizeof(emitter));
    emitter.ir = ir;
    emitter.profile = profile;
    emitter.out = out;
    emitter.error_message = err;
    emitter.error_capacity = errcap;
    if (!preflight_ir(&emitter)) {
        /* Preflight failures are capability limits (unsupported opcode/type/
         * ABI) after the static stages accepted the program.  Emission
         * failures below remain internal errors. */
        return TC_AARCH64_EMIT_UNSUPPORTED;
    }
    write_assembly(&emitter,
                   "  /* TC 0.0.46 AArch64 subset assembly (profile %s) */\n",
                   profile->target != NULL && profile->target->name != NULL
                       ? profile->target->name
                       : "?");
    for (u = 0; u < ir->nunits; u++) {
        if (ir->units[u].func_id == TC_UNIT_TOP_LEVEL) {
            saw_top = 1;
        }
        emit_unit(&emitter, &ir->units[u], u);
        if (emitter.failed) {
            break;
        }
    }
    if (!emitter.failed && !saw_top) {
        emit_error(&emitter, "program has no top-level unit");
    }
    if (!emitter.failed) {
        emit_static_region(&emitter);
    }
    if (!emitter.failed) {
        emit_source_file_strings(&emitter);
    }
    if (!emitter.failed) {
        write_assembly(&emitter, "\n  .section .note.GNU-stack,\"\",%%progbits\n");
    }
    if (emitter.failed) {
        return emitter.output_failed || ferror(out)
                   ? TC_AARCH64_EMIT_IO
                   : TC_AARCH64_EMIT_INTERNAL;
    }
    if (ferror(out)) {
        return TC_AARCH64_EMIT_IO;
    }
    return TC_AARCH64_EMIT_OK;
}
