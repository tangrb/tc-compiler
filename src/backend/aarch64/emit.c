/* TC 0.0.46 AArch64 assembly emitter: shared services, frame layout and unit
 * emission (design §10/§11).
 *
 * Every value has a stack slot ("no regalloc", design §10.3): frame objects
 * keep their TcObject offset relative to the frame base, static objects keep
 * their in-region offset in one emitted byte array, and temporaries follow the
 * object area.  Structured values occupy their full size in memory; scalars
 * occupy eight bytes holding the bit pattern.  The prologue registers a frame
 * descriptor with the runtime so memory helpers can apply the mandatory and
 * checked-build object rules.
 */
#include "backend/aarch64/emit_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "backend/common/rt_abi.h"
#include "diagnostic/diagnostic.h"
#include "runtime/trap.h"

/* ------------------------------------------------------------------------- */
/* Diagnostics and output.                                                   */
/* ------------------------------------------------------------------------- */

void tc_a64_write(TcA64Emitter *emitter, const char *format, ...) {
    va_list ap;
    if (emitter == NULL || emitter->failed) {
        return;
    }
    va_start(ap, format);
    if (vfprintf(emitter->out, format, ap) < 0) {
        emitter->output_failed = 1;
        emitter->failed = 1;
    }
    va_end(ap);
}

void tc_a64_error(TcA64Emitter *emitter, const char *format, ...) {
    va_list ap;
    if (emitter == NULL || emitter->failed) {
        return;
    }
    emitter->failed = 1;
    if (emitter->error_message != NULL && emitter->error_capacity != 0) {
        va_start(ap, format);
        vsnprintf(emitter->error_message, emitter->error_capacity, format, ap);
        va_end(ap);
    }
}

void tc_a64_internal_error(TcA64Emitter *emitter, const char *format, ...) {
    va_list ap;
    if (emitter == NULL || emitter->failed) {
        return;
    }
    emitter->failed = 1;
    emitter->internal_error = 1;
    if (emitter->error_message != NULL && emitter->error_capacity != 0) {
        va_start(ap, format);
        vsnprintf(emitter->error_message, emitter->error_capacity, format, ap);
        va_end(ap);
    }
}

unsigned tc_a64_label(TcA64Emitter *emitter) {
    return emitter->next_label++;
}

/* ------------------------------------------------------------------------- */
/* Types.                                                                    */
/* ------------------------------------------------------------------------- */

unsigned tc_a64_type_width(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    return tc_type_bits(type, 64u);
}

uint64_t tc_a64_type_size(const TcIR *ir, const TcType *type, int *ok) {
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
        unsigned width = tc_a64_type_width(type);
        if (width == 0) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return width / 8u;
    }
}

uint64_t tc_a64_type_mask(const TcType *type) {
    unsigned width = tc_a64_type_width(type);
    if (width == 0) {
        return 0;
    }
    if (width >= 64u) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << width) - 1u;
}

int tc_a64_type_is_scalar_value(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    if (type->tag == TC_TY_BOOL || tc_type_is_integer(type)) {
        return 1;
    }
    return type->tag == TC_TY_PTR || type->tag == TC_TY_FNPTR ||
           tc_type_is_float(type);
}

int tc_a64_type_is_float(const TcType *type) {
    return type != NULL && tc_type_is_float(type);
}

const TcStructDef *tc_a64_struct_def(const TcIR *ir, const TcType *type) {
    uint32_t id;
    if (ir == NULL || type == NULL || type->tag != TC_TY_STRUCT) {
        return NULL;
    }
    id = type->struct_.struct_id;
    if (id >= ir->nstructs) {
        return NULL;
    }
    return &ir->structs[id];
}

const TcField *tc_a64_field_at(const TcIR *ir, const TcType *type,
                               uint32_t index) {
    const TcStructDef *def = tc_a64_struct_def(ir, type);
    if (def == NULL || index >= def->nfields) {
        return NULL;
    }
    return &def->fields[index];
}

uint32_t tc_a64_type_id(TcA64Emitter *emitter, const TcType *type) {
    size_t i;
    if (type == NULL) {
        return 0;
    }
    for (i = 0; i < emitter->type_count; i++) {
        if (emitter->types[i] == type) {
            return emitter->type_ids[i];
        }
    }
    if (emitter->type_count == emitter->type_capacity) {
        size_t capacity = emitter->type_capacity == 0 ? 32u
                                                      : emitter->type_capacity * 2u;
        const TcType **types = (const TcType **)realloc(
            emitter->types, capacity * sizeof(*types));
        uint32_t *ids;
        if (types == NULL) {
            tc_a64_error(emitter, "out of memory while interning types");
            return 0;
        }
        emitter->types = types;
        ids = (uint32_t *)realloc(emitter->type_ids,
                                  capacity * sizeof(*ids));
        if (ids == NULL) {
            tc_a64_error(emitter, "out of memory while interning types");
            return 0;
        }
        emitter->type_ids = ids;
        emitter->type_capacity = capacity;
    }
    emitter->types[emitter->type_count] = type;
    emitter->type_ids[emitter->type_count] = (uint32_t)(emitter->type_count + 1u);
    emitter->type_count++;
    return (uint32_t)emitter->type_count;
}

/* ------------------------------------------------------------------------- */
/* Machine-level helpers.                                                    */
/* ------------------------------------------------------------------------- */

void tc_a64_move_immediate(TcA64Emitter *emitter, unsigned reg, uint64_t value) {
    unsigned shift;
    int first = 1;
    if (value == 0) {
        tc_a64_write(emitter, "  movz x%u, #0\n", reg);
        return;
    }
    for (shift = 0; shift < 64u; shift += 16u) {
        uint64_t chunk = (value >> shift) & UINT64_C(0xffff);
        if (chunk == 0) {
            continue;
        }
        if (first) {
            tc_a64_write(emitter, "  movz x%u, #0x%llx", reg,
                         (unsigned long long)chunk);
            first = 0;
        } else {
            tc_a64_write(emitter, "  movk x%u, #0x%llx", reg,
                         (unsigned long long)chunk);
        }
        if (shift != 0) {
            tc_a64_write(emitter, ", lsl #%u", shift);
        }
        tc_a64_write(emitter, "\n");
    }
}

/* dst = base + value.  Uses x16 as scratch; dst must not be 16. */
void tc_a64_add_constant(TcA64Emitter *emitter, unsigned dst, unsigned base,
                         uint64_t value) {
    if (value == 0) {
        if (base == 31u) {
            tc_a64_write(emitter, "  mov x%u, sp\n", dst);
        } else {
            tc_a64_write(emitter, "  mov x%u, x%u\n", dst, base);
        }
        return;
    }
    if (value <= 4095u) {
        if (base == 31u) {
            tc_a64_write(emitter, "  add x%u, sp, #%llu\n", dst,
                         (unsigned long long)value);
        } else {
            tc_a64_write(emitter, "  add x%u, x%u, #%llu\n", dst, base,
                         (unsigned long long)value);
        }
        return;
    }
    {
        unsigned scratch = dst == 16u ? 13u : 16u;
        tc_a64_move_immediate(emitter, scratch, value);
        if (base == 31u) {
            tc_a64_write(emitter, "  add x%u, sp, x%u\n", dst, scratch);
        } else {
            tc_a64_write(emitter, "  add x%u, x%u, x%u\n", dst, base,
                         scratch);
        }
    }
}

void tc_a64_mask_register(TcA64Emitter *emitter, unsigned reg,
                          const TcType *type) {
    uint64_t mask = tc_a64_type_mask(type);
    if (type == NULL || type->tag == TC_TY_BOOL) {
        tc_a64_write(emitter, "  and x%u, x%u, #1\n", reg, reg);
        return;
    }
    if (mask == UINT64_MAX) {
        return;
    }
    if (mask == UINT64_C(0xff) || mask == UINT64_C(0xffff) ||
        mask == UINT64_C(0xffffffff)) {
        tc_a64_write(emitter, "  and x%u, x%u, #0x%llx\n", reg, reg,
                     (unsigned long long)mask);
        return;
    }
    {
        unsigned scratch = reg == 16u ? 13u : 16u;
        tc_a64_move_immediate(emitter, scratch, mask);
        tc_a64_write(emitter, "  and x%u, x%u, x%u\n", reg, reg, scratch);
    }
}

void tc_a64_sign_extend(TcA64Emitter *emitter, unsigned reg,
                        const TcType *type) {
    if (type == NULL || !tc_type_is_signed_integer(type)) {
        return;
    }
    switch (type->tag) {
    case TC_TY_I8:
        tc_a64_write(emitter, "  sxtb x%u, w%u\n", reg, reg);
        break;
    case TC_TY_I16:
        tc_a64_write(emitter, "  sxth x%u, w%u\n", reg, reg);
        break;
    case TC_TY_I32:
        tc_a64_write(emitter, "  sxtw x%u, w%u\n", reg, reg);
        break;
    default:
        break;
    }
}

void tc_a64_normalize_bool(TcA64Emitter *emitter, unsigned reg) {
    tc_a64_write(emitter, "  cmp w%u, #0\n  cset w%u, ne\n", reg, reg);
}

void tc_a64_load_bits(TcA64Emitter *emitter, unsigned base_reg,
                      uint64_t offset, unsigned bytes, unsigned dst_reg) {
    uint64_t off = offset;
    unsigned base = base_reg;
    if (offset > 4095u) {
        tc_a64_move_immediate(emitter, 17u, offset);
        tc_a64_write(emitter, "  add x17, x%u, x17\n", base_reg);
        base = 17u;
        off = 0;
    }
    switch (bytes) {
    case 1:
        tc_a64_write(emitter, "  ldrb w%u, [x%u, #%llu]\n", dst_reg, base,
                     (unsigned long long)off);
        break;
    case 2:
        tc_a64_write(emitter, "  ldrh w%u, [x%u, #%llu]\n", dst_reg, base,
                     (unsigned long long)off);
        break;
    case 4:
        tc_a64_write(emitter, "  ldr w%u, [x%u, #%llu]\n", dst_reg, base,
                     (unsigned long long)off);
        break;
    case 8:
        tc_a64_write(emitter, "  ldr x%u, [x%u, #%llu]\n", dst_reg, base,
                     (unsigned long long)off);
        break;
    default:
        tc_a64_error(emitter, "unsupported scalar width %u", bytes);
        break;
    }
}

void tc_a64_store_bits(TcA64Emitter *emitter, unsigned base_reg,
                       uint64_t offset, unsigned bytes, unsigned src_reg) {
    uint64_t off = offset;
    unsigned base = base_reg;
    if (offset > 4095u) {
        tc_a64_move_immediate(emitter, 17u, offset);
        tc_a64_write(emitter, "  add x17, x%u, x17\n", base_reg);
        base = 17u;
        off = 0;
    }
    switch (bytes) {
    case 1:
        tc_a64_write(emitter, "  strb w%u, [x%u, #%llu]\n", src_reg, base,
                     (unsigned long long)off);
        break;
    case 2:
        tc_a64_write(emitter, "  strh w%u, [x%u, #%llu]\n", src_reg, base,
                     (unsigned long long)off);
        break;
    case 4:
        tc_a64_write(emitter, "  str w%u, [x%u, #%llu]\n", src_reg, base,
                     (unsigned long long)off);
        break;
    case 8:
        tc_a64_write(emitter, "  str x%u, [x%u, #%llu]\n", src_reg, base,
                     (unsigned long long)off);
        break;
    default:
        tc_a64_error(emitter, "unsupported scalar width %u", bytes);
        break;
    }
}

void tc_a64_trap(TcA64Emitter *emitter, const TcSpan *span, TcErrorKind kind) {
    const TcDiagInfo *info = tc_diag_info((TcDiagCode)kind);
    const char *symbol = info != NULL ? info->symbol : "TC_RE_TRAP";
    const char *file = span != NULL && span->file != NULL ? span->file
                                                          : "<source>";
    size_t index = 0;
    size_t i;
    for (i = 0; i < emitter->source_file_count; i++) {
        if (strcmp(emitter->source_files[i], file) == 0) {
            index = i;
            break;
        }
    }
    if (i == emitter->source_file_count) {
        if (emitter->source_file_count >= TC_A64_MAX_SOURCE_FILES) {
            tc_a64_error(emitter, "too many source files to emit trap locations");
            return;
        }
        emitter->source_files[emitter->source_file_count] = file;
        index = emitter->source_file_count;
        emitter->source_file_count++;
    }
    tc_a64_write(emitter, "  /* trap %s */\n", symbol);
    tc_a64_move_immediate(emitter, 0u, (uint64_t)kind);
    tc_a64_move_immediate(emitter, 1u, span != NULL ? span->line : 0u);
    tc_a64_move_immediate(emitter, 2u, span != NULL ? span->column : 0u);
    tc_a64_write(emitter,
                 "  adrp x3, .Lsource_file_%zu\n"
                 "  add x3, x3, :lo12:.Lsource_file_%zu\n"
                 "  bl %s\n"
                 "  brk #0\n",
                 index, index, TC_RT_SYM_TRAP);
}

/* Emit the line, column and file arguments for helper register `first`. */
void tc_a64_span_arguments(TcA64Emitter *emitter, const TcSpan *span,
                           unsigned first_reg) {
    const char *file = span != NULL && span->file != NULL ? span->file
                                                          : "<source>";
    size_t index = 0;
    size_t i;
    for (i = 0; i < emitter->source_file_count; i++) {
        if (strcmp(emitter->source_files[i], file) == 0) {
            index = i;
            break;
        }
    }
    if (i == emitter->source_file_count) {
        if (emitter->source_file_count >= TC_A64_MAX_SOURCE_FILES) {
            tc_a64_error(emitter, "too many source files to emit locations");
            return;
        }
        emitter->source_files[emitter->source_file_count] = file;
        index = emitter->source_file_count;
        emitter->source_file_count++;
    }
    tc_a64_move_immediate(emitter, first_reg,
                          span != NULL ? span->line : 0u);
    tc_a64_move_immediate(emitter, first_reg + 1u,
                          span != NULL ? span->column : 0u);
    tc_a64_write(emitter,
                 "  adrp x%u, .Lsource_file_%zu\n"
                 "  add x%u, x%u, :lo12:.Lsource_file_%zu\n",
                 first_reg + 2u, index, first_reg + 2u, first_reg + 2u, index);
}

void tc_a64_emit_call0(TcA64Emitter *emitter, const char *symbol) {
    tc_a64_write(emitter, "  bl %s\n", symbol);
}

/* ------------------------------------------------------------------------- */
/* Slots, operands and memory transfers.                                     */
/* ------------------------------------------------------------------------- */

int tc_a64_slot_is_object(const TcA64Slot *slot) {
    return slot->kind == TC_A64_SLOT_FRAME || slot->kind == TC_A64_SLOT_STATIC;
}

void tc_a64_slot_address(TcA64Emitter *emitter, uint32_t vreg,
                         unsigned dst_reg) {
    const TcA64Slot *slot;
    if (vreg >= emitter->slot_count) {
        tc_a64_error(emitter, "invalid vreg %u", (unsigned)vreg);
        return;
    }
    slot = &emitter->slots[vreg];
    tc_a64_add_constant(emitter, dst_reg,
                        slot->kind == TC_A64_SLOT_STATIC ? 20u : 19u,
                        slot->offset);
}

const TcType *tc_a64_operand_type(const TcUnit *unit,
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

static const TcType *field_operand_type(TcA64Emitter *emitter,
                                        const TcOperand *operand) {
    const TcType *base;
    size_t i;
    if (operand->kind != TC_OP_FIELD) {
        return NULL;
    }
    if (operand->field.base >= emitter->unit->nvregs) {
        return NULL;
    }
    base = emitter->unit->vreg_types[operand->field.base];
    for (i = 0; i < operand->field.nfields; i++) {
        const TcField *field = tc_a64_field_at(emitter->ir, base,
                                               operand->field.fields[i]);
        if (field == NULL) {
            return NULL;
        }
        base = field->type;
    }
    return base;
}

const TcType *tc_a64_value_type(TcA64Emitter *emitter,
                                const TcOperand *operand) {
    if (operand->kind == TC_OP_FIELD) {
        return field_operand_type(emitter, operand);
    }
    return tc_a64_operand_type(emitter->unit, operand);
}

void tc_a64_operand_address(TcA64Emitter *emitter, const TcOperand *operand,
                            unsigned dst_reg) {
    if (operand->kind == TC_OP_VREG) {
        tc_a64_slot_address(emitter, operand->vreg, dst_reg);
        return;
    }
    if (operand->kind == TC_OP_FIELD) {
        const TcType *base;
        size_t i;
        uint64_t offset = 0;
        if (operand->field.base >= emitter->unit->nvregs) {
            tc_a64_error(emitter, "invalid field base vreg");
            return;
        }
        tc_a64_slot_address(emitter, operand->field.base, dst_reg);
        base = emitter->unit->vreg_types[operand->field.base];
        for (i = 0; i < operand->field.nfields; i++) {
            const TcField *field = tc_a64_field_at(emitter->ir, base,
                                                   operand->field.fields[i]);
            if (field == NULL) {
                tc_a64_error(emitter, "invalid field path");
                return;
            }
            offset += field->offset;
            base = field->type;
        }
        if (offset != 0) {
            tc_a64_add_constant(emitter, dst_reg, dst_reg, offset);
        }
        return;
    }
    tc_a64_error(emitter, "operand kind %d has no address", (int)operand->kind);
}

/* Unaligned-safe scalar access.  Language memory may place a scalar at any
 * offset (struct layout is compact), so field accesses assemble/store bytes
 * instead of relying on the target's alignment behaviour. */
static void load_bytes_at(TcA64Emitter *emitter, unsigned address_reg,
                          unsigned bytes, unsigned dst_reg) {
    unsigned i;
    tc_a64_write(emitter, "  mov x%u, #0\n", dst_reg);
    for (i = 0; i < bytes; i++) {
        tc_a64_write(emitter, "  ldrb w15, [x%u, #%u]\n", address_reg, i);
        if (i == 0) {
            tc_a64_write(emitter, "  mov x%u, x15\n", dst_reg);
        } else {
            tc_a64_write(emitter, "  orr x%u, x%u, x15, lsl #%u\n", dst_reg,
                         dst_reg, 8u * i);
        }
    }
}

static void store_bytes_at(TcA64Emitter *emitter, unsigned address_reg,
                           unsigned bytes, unsigned src_reg) {
    unsigned i;
    tc_a64_write(emitter, "  mov x15, x%u\n", src_reg);
    for (i = 0; i < bytes; i++) {
        tc_a64_write(emitter, "  strb w15, [x%u, #%u]\n", address_reg, i);
        if (i + 1u < bytes) {
            tc_a64_write(emitter, "  lsr x15, x15, #8\n");
        }
    }
}

void tc_a64_load_operand(TcA64Emitter *emitter, const TcOperand *operand,
                         unsigned reg, int sign_extend) {
    const TcType *type = NULL;
    unsigned bytes;
    if (operand->kind == TC_OP_CONST) {
        type = operand->konst.type;
        if (!tc_a64_type_is_scalar_value(type)) {
            tc_a64_error(emitter, "unsupported constant operand type");
            return;
        }
        {
            uint64_t bits = operand->konst.bits & tc_a64_type_mask(type);
            if (type->tag == TC_TY_BOOL) {
                bits = bits != 0 ? 1u : 0u;
            }
            tc_a64_move_immediate(emitter, reg, bits);
        }
        if (sign_extend) {
            tc_a64_sign_extend(emitter, reg, type);
        }
        return;
    }
    type = operand->kind == TC_OP_FIELD ? field_operand_type(emitter, operand)
                                       : tc_a64_operand_type(emitter->unit,
                                                             operand);
    if (!tc_a64_type_is_scalar_value(type)) {
        tc_a64_error(emitter, "operand has no scalar value");
        return;
    }
    tc_a64_operand_address(emitter, operand, 14u);
    bytes = (unsigned)(tc_a64_type_width(type) / 8u);
    if (operand->kind == TC_OP_FIELD) {
        load_bytes_at(emitter, 14u, bytes, reg);
    } else {
        tc_a64_load_bits(emitter, 14u, 0, bytes, reg);
    }
    if (type->tag == TC_TY_BOOL) {
        tc_a64_normalize_bool(emitter, reg);
    } else if (sign_extend) {
        tc_a64_sign_extend(emitter, reg, type);
    }
}

static int operand_is_tracked_object(TcA64Emitter *emitter,
                                     const TcOperand *operand) {
    if (operand->kind == TC_OP_FIELD) {
        if (operand->field.base >= emitter->slot_count) {
            return 0;
        }
        return tc_a64_slot_is_object(&emitter->slots[operand->field.base]);
    }
    if (operand->kind == TC_OP_VREG) {
        if (operand->vreg >= emitter->slot_count) {
            return 0;
        }
        return tc_a64_slot_is_object(&emitter->slots[operand->vreg]);
    }
    return 0;
}

void tc_a64_store_operand(TcA64Emitter *emitter, const TcOperand *operand,
                          unsigned reg) {
    const TcType *type = operand->kind == TC_OP_FIELD
                             ? field_operand_type(emitter, operand)
                             : tc_a64_operand_type(emitter->unit, operand);
    unsigned bytes;
    if (!tc_a64_type_is_scalar_value(type)) {
        tc_a64_error(emitter, "store destination has no scalar type");
        return;
    }
    bytes = (unsigned)(tc_a64_type_width(type) / 8u);
    if (type->tag == TC_TY_BOOL) {
        tc_a64_normalize_bool(emitter, reg);
    } else {
        tc_a64_mask_register(emitter, reg, type);
    }
    tc_a64_operand_address(emitter, operand, 14u);
    if (operand->kind == TC_OP_FIELD) {
        store_bytes_at(emitter, 14u, bytes, reg);
    } else {
        tc_a64_store_bits(emitter, 14u, 0, bytes, reg);
    }
    if (operand_is_tracked_object(emitter, operand) &&
        emitter->profile->checked_build) {
        tc_a64_define_range(emitter, 14u, bytes);
    }
}

void tc_a64_store_result(TcA64Emitter *emitter, const TcInstr *instr,
                         unsigned reg) {
    if (instr->dst.kind == TC_OP_DISCARD) {
        return;
    }
    if (instr->dst.kind == TC_OP_VREG || instr->dst.kind == TC_OP_FIELD) {
        TcOperand operand = instr->dst;
        tc_a64_store_operand(emitter, &operand, reg);
        return;
    }
    tc_a64_error(emitter, "unsupported result operand kind %d",
                 (int)instr->dst.kind);
}

void tc_a64_load_const_to_scratch(TcA64Emitter *emitter,
                                  const TcOperand *operand) {
    const TcType *type = operand->konst.type;
    unsigned bytes;
    if (!tc_a64_type_is_scalar_value(type)) {
        tc_a64_error(emitter, "unsupported constant operand type");
        return;
    }
    bytes = (unsigned)(tc_a64_type_width(type) / 8u);
    tc_a64_load_operand(emitter, operand, 16u, 0);
    tc_a64_store_bits(emitter, 19u, emitter->scratch_offset, bytes, 16u);
}

/* Copy `size` bytes from [x17] to [x16]; clobbers x9 and x10 only. */
void tc_a64_copy_bytes(TcA64Emitter *emitter, uint64_t size) {
    unsigned id = tc_a64_label(emitter);
    if (size == 0) {
        return;
    }
    tc_a64_move_immediate(emitter, 9u, size);
    tc_a64_write(emitter,
                 ".Lcopy_%u:\n"
                 "  ldrb w10, [x17], #1\n"
                 "  strb w10, [x16], #1\n"
                 "  subs x9, x9, #1\n"
                 "  b.ne .Lcopy_%u\n",
                 id, id);
}

void tc_a64_define_range(TcA64Emitter *emitter, unsigned address_reg,
                         uint64_t size) {
    if (!emitter->profile->checked_build || size == 0) {
        return;
    }
    if (address_reg != 0u) {
        tc_a64_write(emitter, "  mov x0, x%u\n", address_reg);
    }
    tc_a64_move_immediate(emitter, 1u, size);
    tc_a64_emit_call0(emitter, TC_RT_SYM_DEFINE);
}

/* ------------------------------------------------------------------------- */
/* Frame layout.                                                             */
/* ------------------------------------------------------------------------- */

static int unit_returns_indirect(TcA64RetPlan *plan) {
    return plan->kind == TC_A64_RET_INDIRECT;
}

static int build_layout(TcA64Emitter *emitter, const TcUnit *unit) {
    uint64_t cursor = 0;
    uint64_t frame_objects = 0;
    size_t v;
    size_t b;
    size_t i;
    emitter->unit = unit;
    emitter->slot_count = unit->nvregs;
    emitter->slots = (TcA64Slot *)calloc(unit->nvregs == 0 ? 1u : unit->nvregs,
                                         sizeof(TcA64Slot));
    if (emitter->slots == NULL) {
        tc_a64_error(emitter, "out of memory while laying out %u vregs",
                     (unsigned)unit->nvregs);
        return 0;
    }
    emitter->frame_object_end = 0;
    emitter->unit_has_read = 0;
    for (b = 0; b < unit->nblocks; b++) {
        for (i = 0; i < unit->blocks[b].n; i++) {
            if (unit->blocks[b].instrs[i].op == TC_READ) {
                emitter->unit_has_read = 1;
            }
        }
    }
    for (v = 0; v < unit->nvregs; v++) {
        const TcType *type = unit->vreg_types[v];
        uint32_t object_id = UINT32_MAX;
        int ok = 0;
        uint64_t size;
        if (type == NULL) {
            tc_a64_error(emitter, "vreg %u has no type", (unsigned)v);
            return 0;
        }
        if (unit->vreg_objects != NULL && v < unit->nvreg_objects) {
            object_id = unit->vreg_objects[v];
        }
        if (object_id != UINT32_MAX) {
            const TcObject *object;
            if (object_id >= emitter->ir->nobjects) {
                tc_a64_error(emitter, "vreg %u refers to invalid object %u",
                             (unsigned)v, (unsigned)object_id);
                return 0;
            }
            object = &emitter->ir->objects[object_id];
            if (object->region == TC_REG_STATIC) {
                emitter->slots[v].kind = TC_A64_SLOT_STATIC;
            } else if (object->region == TC_REG_FRAME) {
                emitter->slots[v].kind = TC_A64_SLOT_FRAME;
                if (object->offset + object->size > emitter->frame_object_end) {
                    emitter->frame_object_end = object->offset + object->size;
                }
                frame_objects++;
            } else {
                tc_a64_error(emitter,
                             "vreg %u has unsupported object region %d",
                             (unsigned)v, (int)object->region);
                return 0;
            }
            emitter->slots[v].offset = object->offset;
            emitter->slots[v].size = object->size;
            emitter->slots[v].type = type;
            continue;
        }
        size = tc_a64_type_size(emitter->ir, type, &ok);
        if (!ok || size == 0) {
            tc_a64_error(emitter, "vreg %u has an incomplete type",
                         (unsigned)v);
            return 0;
        }
        emitter->slots[v].kind = TC_A64_SLOT_TEMP;
        emitter->slots[v].size = size;
        emitter->slots[v].type = type;
    }
    cursor = (emitter->frame_object_end + 7u) & ~UINT64_C(7);
    for (v = 0; v < unit->nvregs; v++) {
        if (emitter->slots[v].kind != TC_A64_SLOT_TEMP) {
            continue;
        }
        cursor = (cursor + 7u) & ~UINT64_C(7);
        emitter->slots[v].offset = cursor;
        cursor += emitter->slots[v].size;
    }
    emitter->scratch_offset = (cursor + 7u) & ~UINT64_C(7);
    cursor = emitter->scratch_offset + 16u;
    emitter->scratch_buffer_size = 0;
    emitter->scratch_buffer_offset = 0;
    for (b = 0; b < unit->nblocks; b++) {
        for (i = 0; i < unit->blocks[b].n; i++) {
            const TcInstr *instr = &unit->blocks[b].instrs[i];
            uint64_t needed = 0;
            int ok = 0;
            if (instr->dst.kind != TC_OP_DISCARD) {
                continue;
            }
            if (instr->op == TC_LOAD || instr->op == TC_MAKE) {
                needed = tc_a64_type_size(emitter->ir, instr->type, &ok);
                if (!ok) {
                    needed = 0;
                }
            } else if (instr->op == TC_CALL ||
                       instr->op == TC_CALL_INDIRECT) {
                const TcType *ret = NULL;
                TcA64RetPlan ret_plan;
                if (instr->op == TC_CALL && instr->nargs >= 1 &&
                    instr->args[0].value.kind == TC_OP_FUNC &&
                    instr->args[0].value.func < emitter->ir->nfuncs) {
                    ret = emitter->ir->funcs[instr->args[0].value.func].sig.ret;
                } else if (instr->op == TC_CALL_INDIRECT &&
                           instr->nargs >= 1) {
                    const TcType *signature =
                        tc_a64_value_type(emitter, &instr->args[0].value);
                    if (signature != NULL && signature->tag == TC_TY_FNPTR) {
                        ret = signature->fnptr.ret;
                    }
                }
                tc_a64_plan_return(emitter, ret, &ret_plan);
                if (ret_plan.kind == TC_A64_RET_INDIRECT) {
                    needed = tc_a64_type_size(emitter->ir, ret, &ok);
                    if (!ok) {
                        needed = 0;
                    }
                }
            }
            if (needed > emitter->scratch_buffer_size) {
                emitter->scratch_buffer_size = needed;
            }
        }
    }
    if (emitter->scratch_buffer_size != 0) {
        emitter->scratch_buffer_offset = (cursor + 7u) & ~UINT64_C(7);
        cursor = emitter->scratch_buffer_offset + emitter->scratch_buffer_size;
    }
    emitter->sret_offset = 0;
    {
        TcA64RetPlan ret;
        const TcType *ret_type = NULL;
        if (unit->func_id != TC_UNIT_TOP_LEVEL &&
            unit->func_id < emitter->ir->nfuncs) {
            ret_type = emitter->ir->funcs[unit->func_id].sig.ret;
        }
        tc_a64_plan_return(emitter, ret_type, &ret);
        if (unit_returns_indirect(&ret)) {
            emitter->sret_offset = (cursor + 7u) & ~UINT64_C(7);
            cursor = emitter->sret_offset + 8u;
        }
    }
    emitter->read_scratch_offset = 0;
    if (emitter->unit_has_read) {
        emitter->read_scratch_offset = (cursor + 7u) & ~UINT64_C(7);
        cursor = emitter->read_scratch_offset + 8u;
    }
    emitter->shadow_offset = 0;
    emitter->shadow_size = 0;
    if (emitter->profile->checked_build) {
        emitter->shadow_size = (emitter->frame_object_end + 7u) / 8u;
        if (emitter->shadow_size == 0) {
            emitter->shadow_size = 1;
        }
        emitter->shadow_offset = (cursor + 7u) & ~UINT64_C(7);
        cursor = emitter->shadow_offset + emitter->shadow_size;
    }
    emitter->frame_desc_count = 0;
    for (v = 0; v < unit->nvregs; v++) {
        if (unit->vreg_objects == NULL || v >= unit->nvreg_objects ||
            unit->vreg_objects[v] == UINT32_MAX) {
            continue;
        }
        if (emitter->slots[v].kind != TC_A64_SLOT_FRAME) {
            continue;
        }
        {
            size_t earlier;
            int duplicate = 0;
            for (earlier = 0; earlier < v; earlier++) {
                if (unit->vreg_objects != NULL &&
                    earlier < unit->nvreg_objects &&
                    unit->vreg_objects[earlier] == unit->vreg_objects[v]) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
        }
        {
            /* Count the base object and all field sub-objects. */
            const TcType *type = emitter->slots[v].type;
            size_t pending = 0;
            size_t count = 0;
            const TcType **stack = NULL;
            size_t capacity = 0;
            (void)frame_objects;
            if (type != NULL && type->tag == TC_TY_STRUCT) {
                if (pending + 1u > capacity) {
                    capacity = 8u;
                    stack = (const TcType **)calloc(capacity,
                                                    sizeof(*stack));
                    if (stack == NULL) {
                        tc_a64_error(emitter, "out of memory");
                        return 0;
                    }
                }
                stack[pending++] = type;
                count = 1;
                while (pending != 0) {
                    const TcType *current = stack[--pending];
                    const TcStructDef *def =
                        tc_a64_struct_def(emitter->ir, current);
                    size_t k;
                    if (def == NULL) {
                        free(stack);
                        tc_a64_error(emitter, "invalid struct type");
                        return 0;
                    }
                    for (k = 0; k < def->nfields; k++) {
                        const TcType *field = def->fields[k].type;
                        count++;
                        if (field->tag == TC_TY_STRUCT) {
                            if (pending == capacity) {
                                const TcType **grown;
                                capacity *= 2u;
                                grown = (const TcType **)realloc(
                                    stack, capacity * sizeof(*stack));
                                if (grown == NULL) {
                                    free(stack);
                                    tc_a64_error(emitter, "out of memory");
                                    return 0;
                                }
                                stack = grown;
                            }
                            stack[pending++] = field;
                        }
                    }
                }
                free(stack);
            } else {
                count = 1;
            }
            emitter->frame_desc_count += count;
        }
    }
    emitter->max_outgoing = 0;
    for (b = 0; b < unit->nblocks; b++) {
        for (i = 0; i < unit->blocks[b].n; i++) {
            const TcInstr *instr = &unit->blocks[b].instrs[i];
            if (instr->op == TC_CALL || instr->op == TC_CALL_INDIRECT) {
                TcA64ArgPlan plan;
                const TcType **params = NULL;
                size_t nparams = 0;
                size_t k;
                if (instr->op == TC_CALL) {
                    if (instr->nargs < 1 ||
                        instr->args[0].value.kind != TC_OP_FUNC ||
                        instr->args[0].value.func >= emitter->ir->nfuncs) {
                        tc_a64_error(emitter, "invalid direct call target");
                        return 0;
                    }
                    nparams = emitter->ir->funcs[instr->args[0].value.func]
                                  .sig.nparams;
                    if (nparams != 0) {
                        params = (const TcType **)calloc(nparams,
                                                         sizeof(*params));
                        if (params == NULL) {
                            tc_a64_error(emitter, "out of memory");
                            return 0;
                        }
                    }
                    for (k = 0; k < nparams; k++) {
                        params[k] = emitter->ir->funcs[instr->args[0].value.func]
                                        .sig.params[k].type;
                    }
                } else if (instr->op == TC_CALL_INDIRECT &&
                           instr->nargs >= 1) {
                    const TcType *signature =
                        tc_a64_value_type(emitter, &instr->args[0].value);
                    if (signature != NULL && signature->tag == TC_TY_FNPTR) {
                        nparams = signature->fnptr.n;
                        if (nparams != 0) {
                            params = (const TcType **)calloc(
                                nparams, sizeof(*params));
                            if (params == NULL) {
                                tc_a64_error(emitter, "out of memory");
                                return 0;
                            }
                        }
                        for (k = 0; k < nparams; k++) {
                            params[k] = signature->fnptr.params[k];
                        }
                    }
                }
                if (params != NULL || nparams == 0) {
                    tc_a64_plan_arguments(emitter, params, nparams, &plan);
                    if (emitter->failed) {
                        free(params);
                        return 0;
                    }
                    if (plan.stack_bytes > emitter->max_outgoing) {
                        emitter->max_outgoing = plan.stack_bytes;
                    }
                    tc_a64_free_argument_plan(&plan);
                }
                free(params);
            }
        }
    }
    cursor = (cursor + 15u) & ~UINT64_C(15);
    {
        uint64_t stack_limit = emitter->profile->stack_size;
        uint64_t depth = emitter->profile->depth_D != 0
                             ? emitter->profile->depth_D
                             : 1u;
        if (stack_limit == 0) {
            stack_limit = emitter->profile->region[TC_REG_FRAME].size;
        }
        uint64_t worst = cursor + emitter->max_outgoing;
        if (worst == 0) {
            worst = 16;
        }
        if (worst > stack_limit || (depth + 1u) > UINT64_MAX / worst ||
            worst * (depth + 1u) > stack_limit) {
            tc_a64_error(emitter,
                         "frame size %llu cannot satisfy the profile stack "
                         "budget",
                         (unsigned long long)cursor);
            return 0;
        }
    }
    emitter->frame_size = cursor;
    return 1;
}

static void free_layout(TcA64Emitter *emitter) {
    free(emitter->slots);
    emitter->slots = NULL;
    emitter->slot_count = 0;
    emitter->unit = NULL;
}

/* ------------------------------------------------------------------------- */
/* Parameter setup.                                                          */
/* ------------------------------------------------------------------------- */

/* Store the low `remaining` bytes of a chunk register at [x19+offset]. */
static void emit_store_chunk(TcA64Emitter *emitter, uint64_t offset,
                             uint64_t remaining, unsigned reg) {
    if (remaining >= 8u) {
        tc_a64_store_bits(emitter, 19u, offset, 8u, reg);
        return;
    }
    tc_a64_write(emitter, "  mov x16, x%u\n", reg);
    if ((remaining & 4u) != 0) {
        tc_a64_store_bits(emitter, 19u, offset, 4u, 16u);
        tc_a64_write(emitter, "  lsr x16, x16, #32\n");
        offset += 4u;
    }
    if ((remaining & 2u) != 0) {
        tc_a64_store_bits(emitter, 19u, offset, 2u, 16u);
        tc_a64_write(emitter, "  lsr x16, x16, #16\n");
        offset += 2u;
    }
    if ((remaining & 1u) != 0) {
        tc_a64_store_bits(emitter, 19u, offset, 1u, 16u);
    }
}

static void emit_parameter_setup(TcA64Emitter *emitter, const TcFunc *func) {
    TcA64ArgPlan plan;
    const TcType **params = NULL;
    size_t nparams = func->sig.nparams;
    size_t i;
    if (nparams != 0) {
        params = (const TcType **)calloc(nparams, sizeof(*params));
        if (params == NULL) {
            tc_a64_error(emitter, "out of memory");
            return;
        }
        for (i = 0; i < nparams; i++) {
            params[i] = func->sig.params[i].type;
        }
    }
    tc_a64_plan_arguments(emitter, params, nparams, &plan);
    free(params);
    if (emitter->failed) {
        return;
    }
    for (i = 0; i < nparams; i++) {
        const TcType *type = func->sig.params[i].type;
        const TcA64ArgSlot *loc = &plan.slots[i];
        uint64_t slot_offset = emitter->slots[i].offset;
        int ok = 0;
        uint64_t size = tc_a64_type_size(emitter->ir, type, &ok);
        if (!ok) {
            tc_a64_error(emitter, "parameter %u has an incomplete type",
                         (unsigned)i);
            tc_a64_free_argument_plan(&plan);
            return;
        }
        if (loc->indirect) {
            if (loc->kind == TC_A64_LOC_XREG) {
                tc_a64_write(emitter, "  mov x17, x%u\n", loc->index);
            } else {
                tc_a64_add_constant(emitter, 17u, 29u,
                                    16u + loc->stack_offset);
                tc_a64_write(emitter, "  ldr x17, [x17]\n");
            }
            tc_a64_add_constant(emitter, 16u, 19u, slot_offset);
            tc_a64_copy_bytes(emitter, size);
            continue;
        }
        if (loc->kind == TC_A64_LOC_XREG) {
            if (type->tag == TC_TY_STRUCT) {
                uint64_t written = 0;
                unsigned chunk = loc->index;
                while (written < size) {
                    emit_store_chunk(emitter, slot_offset + written,
                                     size - written, chunk);
                    written += 8u;
                    chunk++;
                }
            } else if (type->tag == TC_TY_BOOL) {
                tc_a64_write(emitter, "  cmp w%u, #0\n  cset w%u, ne\n",
                             loc->index, loc->index);
                tc_a64_store_bits(emitter, 19u, slot_offset, 1u, loc->index);
            } else {
                tc_a64_store_bits(emitter, 19u, slot_offset,
                                  (unsigned)(size), loc->index);
            }
        } else if (loc->kind == TC_A64_LOC_VREG) {
            if (loc->hfa) {
                unsigned k;
                unsigned elem = loc->hfa32 ? 4u : 8u;
                for (k = 0; k < loc->hfa_count; k++) {
                    if (loc->hfa32) {
                        tc_a64_write(emitter, "  fmov w16, s%u\n",
                                     loc->index + k);
                    } else {
                        tc_a64_write(emitter, "  fmov x16, d%u\n",
                                     loc->index + k);
                    }
                    tc_a64_store_bits(emitter, 19u,
                                      slot_offset + (uint64_t)k * elem, elem,
                                      16u);
                }
            } else {
                if (size == 4u) {
                    tc_a64_write(emitter, "  fmov w16, s%u\n", loc->index);
                } else {
                    tc_a64_write(emitter, "  fmov x16, d%u\n", loc->index);
                }
                tc_a64_store_bits(emitter, 19u, slot_offset, (unsigned)size,
                                  16u);
            }
        } else {
            tc_a64_add_constant(emitter, 17u, 29u, 16u + loc->stack_offset);
            tc_a64_add_constant(emitter, 16u, 19u, slot_offset);
            tc_a64_copy_bytes(emitter, size);
        }
    }
    tc_a64_free_argument_plan(&plan);
}

/* ------------------------------------------------------------------------- */
/* Unit emission.                                                            */
/* ------------------------------------------------------------------------- */

static void emit_epilogue(TcA64Emitter *emitter, size_t unit_index) {
    tc_a64_write(emitter, ".Lreturn_%zu:\n", unit_index);
    tc_a64_write(emitter, "  mov sp, x19\n"
                          "  bl %s\n"
                          "  sub sp, x29, #16\n"
                          "  ldp x19, x20, [sp], #16\n"
                          "  ldp x29, x30, [sp], #16\n"
                          "  ret\n",
                 TC_RT_SYM_FRAME_LEAVE);
}

static void emit_unit(TcA64Emitter *emitter, const TcUnit *unit,
                      size_t unit_index) {
    const TcFunc *func = NULL;
    const char *symbol;
    size_t b;
    size_t i;
    int ok = 0;
    (void)ok;

    if (!build_layout(emitter, unit)) {
        free_layout(emitter);
        return;
    }
    emitter->unit_index = unit_index;
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        symbol = TC_AARCH64_TOP_LEVEL_SYMBOL;
    } else {
        if (unit->func_id >= emitter->ir->nfuncs) {
            tc_a64_error(emitter, "unit refers to invalid function %u",
                         (unsigned)unit->func_id);
            free_layout(emitter);
            return;
        }
        func = &emitter->ir->funcs[unit->func_id];
        symbol = NULL;
    }
    if (symbol != NULL) {
        tc_a64_write(emitter, "\n  .text\n  .p2align 2\n"
                              "  .globl %s\n"
                              "  .type %s, %%function\n"
                              "%s:\n",
                     symbol, symbol, symbol);
    } else {
        tc_a64_write(emitter, "\n  .text\n  .p2align 2\n"
                              "  .type tc_function_%u, %%function\n"
                              "tc_function_%u:\n",
                     (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    tc_a64_write(emitter, "  stp x29, x30, [sp, #-16]!\n"
                          "  mov x29, sp\n"
                          "  stp x19, x20, [sp, #-16]!\n");
    if (emitter->frame_size != 0) {
        if (emitter->frame_size <= 4095u) {
            tc_a64_write(emitter, "  sub sp, sp, #%llu\n",
                         (unsigned long long)emitter->frame_size);
        } else {
            tc_a64_move_immediate(emitter, 16u, emitter->frame_size);
            tc_a64_write(emitter, "  sub sp, sp, x16\n");
        }
    }
    tc_a64_write(emitter, "  mov x19, sp\n"
                          "  adrp x20, %s\n"
                          "  add x20, x20, :lo12:%s\n",
                 TC_RT_SYM_STATIC_START, TC_RT_SYM_STATIC_START);
    if (emitter->sret_offset != 0) {
        tc_a64_store_bits(emitter, 19u, emitter->sret_offset, 8u, 8u);
    }
    if (func != NULL) {
        emit_parameter_setup(emitter, func);
        if (emitter->failed) {
            free_layout(emitter);
            return;
        }
    }
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        tc_a64_emit_call0(emitter, TC_RT_SYM_PROGRAM_INIT);
    }
    tc_a64_write(emitter, "  adrp x0, .Lunit_objs_%zu\n"
                          "  add x0, x0, :lo12:.Lunit_objs_%zu\n",
                 unit_index, unit_index);
    tc_a64_move_immediate(emitter, 1u, emitter->frame_desc_count);
    tc_a64_write(emitter, "  mov x2, x19\n");
    tc_a64_move_immediate(emitter, 3u, emitter->frame_size);
    if (emitter->profile->checked_build) {
        tc_a64_add_constant(emitter, 4u, 19u, emitter->shadow_offset);
        tc_a64_move_immediate(emitter, 5u, emitter->shadow_size);
    } else {
        tc_a64_move_immediate(emitter, 4u, 0u);
        tc_a64_move_immediate(emitter, 5u, 0u);
    }
    tc_a64_emit_call0(emitter, TC_RT_SYM_FRAME_ENTER);
    if (emitter->profile->checked_build && func != NULL) {
        size_t p;
        for (p = 0; p < func->sig.nparams; p++) {
            int okp = 0;
            uint64_t psize = tc_a64_type_size(emitter->ir,
                                              func->sig.params[p].type, &okp);
            if (!okp) {
                continue;
            }
            tc_a64_add_constant(emitter, 16u, 19u,
                                emitter->slots[p].offset);
            tc_a64_define_range(emitter, 16u, psize);
        }
    }
    for (b = 0; b < unit->nblocks; b++) {
        tc_a64_write(emitter, ".L%zu_block%zu:\n", unit_index, b);
        for (i = 0; i < unit->blocks[b].n; i++) {
            tc_a64_emit_instruction(emitter, &unit->blocks[b].instrs[i]);
            if (emitter->failed) {
                free_layout(emitter);
                return;
            }
        }
    }
    emit_epilogue(emitter, unit_index);
    if (func == NULL) {
        tc_a64_write(emitter, "\n  .size %s, .-%s\n",
                     TC_AARCH64_TOP_LEVEL_SYMBOL,
                     TC_AARCH64_TOP_LEVEL_SYMBOL);
    } else {
        tc_a64_write(emitter, "\n  .size tc_function_%u, .-tc_function_%u\n",
                     (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    free_layout(emitter);
}

/* ------------------------------------------------------------------------- */
/* Object descriptors and static data.                                       */
/* ------------------------------------------------------------------------- */

typedef struct {
    TcRtObjectDesc *items;
    size_t count;
    size_t capacity;
} TcA64DescList;

static int desc_push(TcA64DescList *list, TcRtObjectDesc desc) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 16u : list->capacity * 2u;
        TcRtObjectDesc *items = (TcRtObjectDesc *)realloc(
            list->items, capacity * sizeof(*items));
        if (items == NULL) {
            return 0;
        }
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = desc;
    return 1;
}

static int add_field_descs(TcA64Emitter *emitter, TcA64DescList *list,
                           uint64_t offset, const TcType *type, int writable) {
    const TcStructDef *def = tc_a64_struct_def(emitter->ir, type);
    size_t i;
    if (def == NULL) {
        return type == NULL || type->tag != TC_TY_STRUCT;
    }
    for (i = 0; i < def->nfields; i++) {
        const TcField *field = &def->fields[i];
        int ok = 0;
        uint64_t size = tc_a64_type_size(emitter->ir, field->type, &ok);
        TcRtObjectDesc desc;
        if (!ok) {
            tc_a64_error(emitter, "field %u has an incomplete type",
                         (unsigned)i);
            return 0;
        }
        desc.offset = offset + field->offset;
        desc.size = size;
        desc.type_id = tc_a64_type_id(emitter, field->type);
        desc.flags = writable ? TC_RT_OBJECT_WRITABLE : 0;
        if (!desc_push(list, desc)) {
            tc_a64_error(emitter, "out of memory while building descriptors");
            return 0;
        }
        if (!add_field_descs(emitter, list, desc.offset, field->type,
                             writable)) {
            return 0;
        }
    }
    return 1;
}

static void emit_unit_descriptors(TcA64Emitter *emitter, const TcUnit *unit,
                                  size_t unit_index) {
    TcA64DescList list;
    size_t v;
    memset(&list, 0, sizeof(list));
    for (v = 0; v < unit->nvregs; v++) {
        uint32_t object_id;
        const TcObject *object;
        TcRtObjectDesc desc;
        if (unit->vreg_objects == NULL || v >= unit->nvreg_objects) {
            continue;
        }
        object_id = unit->vreg_objects[v];
        if (object_id == UINT32_MAX || object_id >= emitter->ir->nobjects) {
            continue;
        }
        object = &emitter->ir->objects[object_id];
        if (object->region != TC_REG_FRAME) {
            continue;
        }
        {
            size_t earlier;
            int duplicate = 0;
            for (earlier = 0; earlier < v; earlier++) {
                if (v < unit->nvreg_objects &&
                    earlier < unit->nvreg_objects &&
                    unit->vreg_objects[earlier] == object_id) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
        }
        desc.offset = object->offset;
        desc.size = object->size;
        desc.type_id = tc_a64_type_id(emitter, object->type);
        desc.flags = object->writable ? TC_RT_OBJECT_WRITABLE : 0;
        if (!desc_push(&list, desc)) {
            tc_a64_error(emitter, "out of memory while building descriptors");
            break;
        }
        if (!add_field_descs(emitter, &list, object->offset, object->type,
                             object->writable)) {
            break;
        }
    }
    tc_a64_write(emitter, "\n  .section .rodata\n  .p2align 3\n"
                          ".Lunit_objs_%zu:\n",
                 unit_index);
    if (list.count == 0) {
        tc_a64_write(emitter, "  .quad 0\n  .quad 0\n  .long 0\n  .long 0\n");
    } else {
        size_t i;
        for (i = 0; i < list.count; i++) {
            tc_a64_write(emitter,
                         "  .quad %llu\n  .quad %llu\n  .long %u\n  .long %u\n",
                         (unsigned long long)list.items[i].offset,
                         (unsigned long long)list.items[i].size,
                         (unsigned)list.items[i].type_id,
                         (unsigned)list.items[i].flags);
        }
    }
    free(list.items);
}

static int emit_static_region(TcA64Emitter *emitter) {
    uint64_t size = 0;
    uint8_t *bytes = NULL;
    size_t i;
    uint64_t pos;
    TcA64DescList list;
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        if (object->offset > UINT64_MAX - object->size) {
            tc_a64_error(emitter, "static object offset overflows");
            return 0;
        }
        if (object->offset + object->size > size) {
            size = object->offset + object->size;
        }
    }
    if (size > (uint64_t)SIZE_MAX) {
        tc_a64_error(emitter, "static data is too large");
        return 0;
    }
    bytes = (uint8_t *)calloc((size_t)(size == 0 ? 1u : size), 1u);
    if (bytes == NULL) {
        tc_a64_error(emitter, "out of memory while building static data");
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
            tc_a64_error(emitter, "static object initializer overflows");
            return 0;
        }
        if (object->init->size != 0) {
            if (object->init->bytes == NULL) {
                free(bytes);
                tc_a64_error(emitter, "static initializer has no bytes");
                return 0;
            }
            memcpy(bytes + object->offset, object->init->bytes,
                   object->init->size);
        }
    }
    tc_a64_write(emitter, "\n  .data\n  .p2align 3\n"
                          "  .globl %s\n"
                          "  .type %s, %%object\n"
                          "%s:\n",
                 TC_RT_SYM_STATIC_START, TC_RT_SYM_STATIC_START,
                 TC_RT_SYM_STATIC_START);
    pos = 0;
    while (pos < size) {
        uint64_t run = 0;
        while (pos + run < size && bytes[pos + run] == 0) {
            run++;
        }
        if (run >= 8u) {
            tc_a64_write(emitter, "  .zero %llu\n", (unsigned long long)run);
            pos += run;
            continue;
        }
        tc_a64_write(emitter, "  .byte 0x%02x\n", (unsigned)bytes[pos]);
        pos++;
    }
    if (size == 0) {
        tc_a64_write(emitter, "  .byte 0\n");
    }
    tc_a64_write(emitter, "  .globl %s\n%s:\n",
                 TC_RT_SYM_STATIC_END, TC_RT_SYM_STATIC_END);
    free(bytes);
    memset(&list, 0, sizeof(list));
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        TcRtObjectDesc desc;
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        desc.offset = object->offset;
        desc.size = object->size;
        desc.type_id = tc_a64_type_id(emitter, object->type);
        desc.flags = (object->writable ? TC_RT_OBJECT_WRITABLE : 0) |
                     (object->init != NULL ? TC_RT_OBJECT_HAS_INIT : 0);
        if (!desc_push(&list, desc)) {
            tc_a64_error(emitter, "out of memory");
            return 0;
        }
        if (!add_field_descs(emitter, &list, object->offset, object->type,
                             object->writable)) {
            return 0;
        }
    }
    tc_a64_write(emitter, "  .section .rodata\n  .p2align 3\n"
                          "  .globl %s\n%s:\n",
                 TC_RT_SYM_STATIC_OBJECTS, TC_RT_SYM_STATIC_OBJECTS);
    if (list.count == 0) {
        tc_a64_write(emitter, "  .quad 0\n  .quad 0\n  .long 0\n  .long 0\n");
    } else {
        for (i = 0; i < list.count; i++) {
            tc_a64_write(emitter,
                         "  .quad %llu\n  .quad %llu\n  .long %u\n  .long %u\n",
                         (unsigned long long)list.items[i].offset,
                         (unsigned long long)list.items[i].size,
                         (unsigned)list.items[i].type_id,
                         (unsigned)list.items[i].flags);
        }
    }
    tc_a64_write(emitter, "  .p2align 3\n  .globl %s\n%s:\n  .quad %llu\n",
                 TC_RT_SYM_STATIC_OBJECT_COUNT,
                 TC_RT_SYM_STATIC_OBJECT_COUNT,
                 (unsigned long long)list.count);
    free(list.items);
    return 1;
}


/* ------------------------------------------------------------------------- */
/* Platform constants, source strings and entry point.                       */
/* ------------------------------------------------------------------------- */

static void emit_platform_constants(TcA64Emitter *emitter) {
    tc_a64_write(emitter,
                 "\n  .data\n  .p2align 3\n"
                 "  .globl %s\n%s:\n  .quad %llu\n"
                 "  .globl %s\n%s:\n  .quad %llu\n"
                 "  .globl %s\n%s:\n  .quad %llu\n"
                 "  .globl %s\n%s:\n  .quad %llu\n"
                 "  .globl %s\n%s:\n  .quad %llu\n"
                 "  .globl %s\n%s:\n  .quad %llu\n"
                 "  .globl %s\n%s:\n  .quad %llu\n"
                 "  .globl %s\n%s:\n  .quad %llu\n",
                 TC_RT_SYM_DEVICE_BASE, TC_RT_SYM_DEVICE_BASE,
                 (unsigned long long)emitter->profile->region[TC_REG_DEVICE].base,
                 TC_RT_SYM_DEVICE_SIZE, TC_RT_SYM_DEVICE_SIZE,
                 (unsigned long long)emitter->profile->region[TC_REG_DEVICE].size,
                 TC_RT_SYM_DEVICE_POLICY, TC_RT_SYM_DEVICE_POLICY,
                 (unsigned long long)emitter->profile->device,
                 TC_RT_SYM_HEAP_BASE, TC_RT_SYM_HEAP_BASE,
                 (unsigned long long)emitter->profile->region[TC_REG_HEAP].base,
                 TC_RT_SYM_HEAP_SIZE, TC_RT_SYM_HEAP_SIZE,
                 (unsigned long long)emitter->profile->region[TC_REG_HEAP].size,
                 TC_RT_SYM_CHECKED, TC_RT_SYM_CHECKED,
                 (unsigned long long)(emitter->profile->checked_build ? 1u : 0u),
                 TC_RT_SYM_NORMAL_STATUS, TC_RT_SYM_NORMAL_STATUS,
                 (unsigned long long)(emitter->profile->exit.normal_status),
                 TC_RT_SYM_TRAP_STATUS, TC_RT_SYM_TRAP_STATUS,
                 (unsigned long long)(emitter->profile->exit.trap_status));
}

static void emit_static_shadow(TcA64Emitter *emitter) {
    uint64_t size = 1;
    size_t i;
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        if (object->offset + object->size > size) {
            size = object->offset + object->size;
        }
    }
    size = (size + 7u) / 8u;
    tc_a64_write(emitter,
                 "\n  .bss\n  .p2align 3\n"
                 "  .globl %s\n%s:\n  .zero %llu\n",
                 TC_RT_SYM_STATIC_SHADOW, TC_RT_SYM_STATIC_SHADOW,
                 (unsigned long long)size);
}

static void emit_source_file_strings(TcA64Emitter *emitter) {
    size_t i;
    if (emitter->source_file_count == 0) {
        return;
    }
    tc_a64_write(emitter, "\n  .section .rodata\n");
    for (i = 0; i < emitter->source_file_count; i++) {
        const char *p = emitter->source_files[i] != NULL
                            ? emitter->source_files[i]
                            : "<source>";
        tc_a64_write(emitter, ".Lsource_file_%zu:\n  .asciz \"", i);
        while (*p != '\0') {
            unsigned char c = (unsigned char)*p++;
            if (c == '"' || c == '\\') {
                tc_a64_write(emitter, "\\%c", c);
            } else if (c >= 0x20 && c < 0x7f) {
                tc_a64_write(emitter, "%c", c);
            } else {
                tc_a64_write(emitter, "\\%03o", (unsigned)c);
            }
        }
        tc_a64_write(emitter, "\"\n");
    }
}

static int preflight_ir(TcA64Emitter *emitter) {
    size_t u;
    size_t b;
    size_t i;
    size_t f;
    if (emitter->ir->profile != NULL &&
        emitter->ir->profile->addr_width != emitter->profile->addr_width) {
        tc_a64_error(emitter, "IR profile width does not match the emitter profile");
        return 0;
    }
    if (emitter->profile->addr_width != 64u ||
        emitter->profile->target == NULL ||
        emitter->profile->target->addr_width != 64u ||
        !emitter->profile->little_endian) {
        tc_a64_error(emitter,
                     "the AArch64 backend requires a 64-bit little-endian "
                     "profile");
        return 0;
    }
    if (emitter->profile->target != NULL &&
        emitter->profile->target->name != NULL &&
        strcmp(emitter->profile->target->name, "aarch64-linux-user") == 0 &&
        emitter->profile->device != TC_DEV_REJECT) {
        /* The hosted semihosting platform instance maps no device memory; a
         * non-reject policy needs the freestanding device instance. */
        tc_a64_error(emitter,
                     "the hosted AArch64 platform instance requires the "
                     "TC_DEV_REJECT device policy");
        return 0;
    }
    if (emitter->profile->disable_icf != 1) {
        tc_a64_error(emitter, "the profile must disable ICF");
        return 0;
    }
    {
        /* The platform profile bounds the static region; the reference VM
         * rejects a program whose static layout exceeds it as an
         * implementation error, so the backend applies the same bound. */
        uint64_t static_limit =
            emitter->profile->region[TC_REG_STATIC].size;
        size_t object_index;
        for (object_index = 0; object_index < emitter->ir->nobjects;
             object_index++) {
            const TcObject *object = &emitter->ir->objects[object_index];
            if (object->region != TC_REG_STATIC) {
                continue;
            }
            if (object->offset > static_limit ||
                object->size > static_limit - object->offset) {
                tc_a64_internal_error(
                    emitter,
                    "static object %u at offset %llu size %llu exceeds the "
                    "profile static region (%llu bytes)",
                    (unsigned)object_index,
                    (unsigned long long)object->offset,
                    (unsigned long long)object->size,
                    (unsigned long long)static_limit);
                return 0;
            }
        }
    }
    for (f = 0; f < emitter->ir->nfuncs; f++) {
        const TcFunc *func = &emitter->ir->funcs[f];
        size_t p;
        if (func->body == NULL) {
            tc_a64_error(emitter, "function %u has no body", (unsigned)f);
            return 0;
        }
        for (p = 0; p < func->sig.nparams; p++) {
            const TcType *type = func->sig.params[p].type;
            int ok = 0;
            if (type == NULL ||
                (type->tag != TC_TY_STRUCT &&
                 !tc_a64_type_is_scalar_value(type))) {
                tc_a64_error(emitter, "function %u parameter %u has an "
                                      "unsupported type",
                             (unsigned)f, (unsigned)p);
                return 0;
            }
            (void)tc_a64_type_size(emitter->ir, type, &ok);
            if (!ok) {
                tc_a64_error(emitter, "function %u parameter %u has an "
                                      "incomplete type",
                             (unsigned)f, (unsigned)p);
                return 0;
            }
        }
        if (func->sig.ret != NULL && func->sig.ret->tag != TC_TY_VOID) {
            int ok = 0;
            const TcType *ret = func->sig.ret;
            if (ret->tag != TC_TY_STRUCT && !tc_a64_type_is_scalar_value(ret)) {
                tc_a64_error(emitter, "function %u returns an unsupported type",
                             (unsigned)f);
                return 0;
            }
            (void)tc_a64_type_size(emitter->ir, ret, &ok);
            if (!ok) {
                tc_a64_error(emitter, "function %u returns an incomplete type",
                             (unsigned)f);
                return 0;
            }
        }
    }
    for (u = 0; u < emitter->ir->nunits; u++) {
        const TcUnit *unit = &emitter->ir->units[u];
        size_t v;
        if (unit->nblocks == 0 || unit->entry >= unit->nblocks) {
            tc_a64_error(emitter, "unit entry block is invalid");
            return 0;
        }
        for (v = 0; v < unit->nvregs; v++) {
            const TcType *type = unit->vreg_types[v];
            if (type == NULL) {
                tc_a64_error(emitter, "vreg %u has no type", (unsigned)v);
                return 0;
            }
            if (type->tag != TC_TY_STRUCT &&
                !tc_a64_type_is_scalar_value(type)) {
                tc_a64_error(emitter, "vreg %u has no supported value type",
                             (unsigned)v);
                return 0;
            }
            if (unit->vreg_objects != NULL && v < unit->nvreg_objects &&
                unit->vreg_objects[v] != UINT32_MAX) {
                uint32_t oid = unit->vreg_objects[v];
                if (oid >= emitter->ir->nobjects) {
                    tc_a64_error(emitter, "vreg %u refers to invalid object %u",
                                 (unsigned)v, (unsigned)oid);
                    return 0;
                }
                if (emitter->ir->objects[oid].region != TC_REG_FRAME &&
                    emitter->ir->objects[oid].region != TC_REG_STATIC) {
                    tc_a64_error(emitter,
                                 "vreg %u has an unsupported object region",
                                 (unsigned)v);
                    return 0;
                }
            }
        }
        if (unit->func_id != TC_UNIT_TOP_LEVEL &&
            unit->func_id < emitter->ir->nfuncs) {
            size_t nparams = emitter->ir->funcs[unit->func_id].sig.nparams;
            if (unit->nvregs < nparams) {
                tc_a64_error(emitter,
                             "function %u has fewer vregs than parameters",
                             (unsigned)unit->func_id);
                return 0;
            }
        }
        for (b = 0; b < unit->nblocks; b++) {
            for (i = 0; i < unit->blocks[b].n; i++) {
                const TcInstr *instr = &unit->blocks[b].instrs[i];
                if (instr->op < 0 || instr->op >= TC_OPCODE_COUNT) {
                    tc_a64_error(emitter, "invalid opcode %d", (int)instr->op);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int abi_matches_classifier(const TcABIDesc *abi) {
    return abi != NULL && abi->hfa == 1 &&
           abi->composite_max_bytes == 16u && abi->hfa_max_regs == 4u &&
           abi->stack_align == 16u;
}

TcAArch64EmitStatus tc_aarch64_emit_asm(FILE *out, const TcIR *ir,
                                        const TcPlatformProfile *profile,
                                        const TcTargetDesc *target,
                                        char *error_message,
                                        size_t error_capacity) {
    TcA64Emitter emitter;
    size_t u;
    int saw_top = 0;
    if (out == NULL || ir == NULL || profile == NULL || target == NULL) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity, "missing emitter argument");
        }
        return TC_AARCH64_EMIT_INTERNAL;
    }
    if (target != profile->target) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity,
                     "target descriptor does not match the profile");
        }
        return TC_AARCH64_EMIT_INTERNAL;
    }
    if (!abi_matches_classifier(target->abi)) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity,
                     "profile ABI descriptor does not match the AArch64 "
                     "classifier");
        }
        return TC_AARCH64_EMIT_INTERNAL;
    }
    memset(&emitter, 0, sizeof(emitter));
    emitter.ir = ir;
    emitter.profile = profile;
    emitter.out = out;
    emitter.error_message = error_message;
    emitter.error_capacity = error_capacity;
    if (!preflight_ir(&emitter)) {
        free(emitter.types);
        free(emitter.type_ids);
        if (emitter.internal_error) {
            return TC_AARCH64_EMIT_INTERNAL;
        }
        return emitter.failed ? TC_AARCH64_EMIT_UNSUPPORTED
                              : TC_AARCH64_EMIT_INTERNAL;
    }
    tc_a64_write(&emitter,
                 "  /* TC 0.0.46 AArch64 assembly (profile %s) */\n",
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
        tc_a64_error(&emitter, "program has no top-level unit");
    }
    if (!emitter.failed) {
        for (u = 0; u < ir->nunits; u++) {
            emit_unit_descriptors(&emitter, &ir->units[u], u);
            if (emitter.failed) {
                break;
            }
        }
    }
    if (!emitter.failed) {
        emit_static_region(&emitter);
    }
    if (!emitter.failed) {
        emit_platform_constants(&emitter);
    }
    if (!emitter.failed) {
        emit_static_shadow(&emitter);
    }
    if (!emitter.failed) {
        emit_source_file_strings(&emitter);
    }
    if (!emitter.failed) {
        tc_a64_write(&emitter,
                     "\n  .section .note.GNU-stack,\"\",%%progbits\n");
    }
    free(emitter.types);
    free(emitter.type_ids);
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
