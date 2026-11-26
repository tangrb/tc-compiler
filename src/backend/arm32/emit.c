/* TC 0.0.46 ARM32 assembly emitter: shared services, frame layout and unit
 * emission (design §10/§11).
 *
 * Every value has a stack slot (no regalloc).  Frame objects keep their
 * TcObject offset relative to the locals base r9; static objects keep their
 * in-region offset relative to r10; temporaries follow the object area.  Each
 * function reserves its outgoing argument area at sp and registers a frame
 * descriptor with the runtime so memory helpers apply the mandatory and
 * checked-build object rules.
 */
#include "backend/arm32/emit_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostic/diagnostic.h"
#include "ir/type_width.h"
#include "runtime/trap.h"

/* ------------------------------------------------------------------------- */
/* Diagnostics and output.                                                   */
/* ------------------------------------------------------------------------- */

void tc_arm32_write(TcArm32Emitter *emitter, const char *format, ...) {
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

void tc_arm32_error(TcArm32Emitter *emitter, const char *format, ...) {
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

void tc_arm32_oom(TcArm32Emitter *emitter, const char *format, ...) {
    va_list ap;
    if (emitter == NULL || emitter->failed) {
        return;
    }
    emitter->failed = 1;
    emitter->oom_failed = 1;
    if (emitter->error_message != NULL && emitter->error_capacity != 0) {
        va_start(ap, format);
        vsnprintf(emitter->error_message, emitter->error_capacity, format, ap);
        va_end(ap);
    }
}

void tc_arm32_internal_error(TcArm32Emitter *emitter, const char *format,
                             ...) {
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

unsigned tc_arm32_label(TcArm32Emitter *emitter) {
    return emitter->next_label++;
}

/* ------------------------------------------------------------------------- */
/* Types.                                                                    */
/* ------------------------------------------------------------------------- */

unsigned tc_arm32_type_width(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    return tc_type_bits(type, 32u);
}

uint64_t tc_arm32_type_size(const TcIR *ir, const TcType *type, int *ok) {
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
        unsigned width = tc_arm32_type_width(type);
        if (width == 0) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return width / 8u;
    }
}

uint64_t tc_arm32_type_mask(const TcType *type) {
    unsigned width = tc_arm32_type_width(type);
    if (width == 0) {
        return 0;
    }
    if (width >= 64u) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << width) - 1u;
}

int tc_arm32_type_is_scalar_value(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    switch (type->tag) {
    case TC_TY_I8:
    case TC_TY_I16:
    case TC_TY_I32:
    case TC_TY_I64:
    case TC_TY_U8:
    case TC_TY_U16:
    case TC_TY_U32:
    case TC_TY_U64:
    case TC_TY_ISIZE:
    case TC_TY_USIZE:
    case TC_TY_F32:
    case TC_TY_F64:
    case TC_TY_BOOL:
    case TC_TY_PTR:
    case TC_TY_FNPTR:
        return 1;
    default:
        return 0;
    }
}

const TcStructDef *tc_arm32_struct_def(const TcIR *ir, const TcType *type) {
    if (ir == NULL || type == NULL || type->tag != TC_TY_STRUCT) {
        return NULL;
    }
    if (type->struct_.struct_id >= ir->nstructs) {
        return NULL;
    }
    return &ir->structs[type->struct_.struct_id];
}

const TcField *tc_arm32_field_at(const TcIR *ir, const TcType *type,
                                 uint32_t index) {
    const TcStructDef *def = tc_arm32_struct_def(ir, type);
    if (def == NULL || index >= def->nfields) {
        return NULL;
    }
    return &def->fields[index];
}

uint32_t tc_arm32_type_id(TcArm32Emitter *emitter, const TcType *type) {
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
        size_t capacity = emitter->type_capacity == 0
                              ? 64u
                              : emitter->type_capacity * 2u;
        const TcType **types = (const TcType **)emitter->alloc->realloc(emitter->alloc->ctx,
            emitter->types, capacity * sizeof(*types));
        uint32_t *ids;
        if (types == NULL) {
            tc_arm32_oom(emitter, "out of memory while interning type ids");
            return 0;
        }
        emitter->types = types;
        ids = (uint32_t *)emitter->alloc->realloc(emitter->alloc->ctx,  emitter->type_ids,
                                  capacity * sizeof(*ids));
        if (ids == NULL) {
            tc_arm32_oom(emitter, "out of memory while interning type ids");
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
/* Immediates and register helpers.                                          */
/* ------------------------------------------------------------------------- */

void tc_arm32_move_immediate(TcArm32Emitter *emitter, unsigned reg,
                             uint32_t value) {
    if (value <= 0xffffu) {
        tc_arm32_write(emitter, "  movw r%u, #%u\n", reg, (unsigned)value);
        return;
    }
    tc_arm32_write(emitter, "  movw r%u, #%u\n", reg,
                   (unsigned)(value & 0xffffu));
    tc_arm32_write(emitter, "  movt r%u, #%u\n", reg,
                   (unsigned)(value >> 16));
}

void tc_arm32_load_symbol(TcArm32Emitter *emitter, unsigned reg,
                          const char *symbol) {
    tc_arm32_write(emitter, "  movw r%u, #:lower16:%s\n", reg, symbol);
    tc_arm32_write(emitter, "  movt r%u, #:upper16:%s\n", reg, symbol);
}

void tc_arm32_add_constant(TcArm32Emitter *emitter, unsigned dst,
                           unsigned base, uint64_t value) {
    if (value == 0) {
        if (dst != base) {
            tc_arm32_write(emitter, "  mov r%u, r%u\n", dst, base);
        }
        return;
    }
    if (value <= 4095u && base != 13u) {
        tc_arm32_write(emitter, "  addw r%u, r%u, #%llu\n", dst, base,
                       (unsigned long long)value);
        return;
    }
    {
        unsigned scratch = (dst != TC_ARM32_TMP0 && base != TC_ARM32_TMP0)
                               ? TC_ARM32_TMP0
                               : ((dst != TC_ARM32_TMP1 &&
                                   base != TC_ARM32_TMP1)
                                      ? TC_ARM32_TMP1
                                      : TC_ARM32_VALUE0);
        if (base == 13u) {
            tc_arm32_write(emitter, "  mov r%u, sp\n", dst);
            tc_arm32_move_immediate(emitter, scratch, (uint32_t)value);
            tc_arm32_write(emitter, "  add r%u, r%u, r%u\n", dst, dst,
                           scratch);
            return;
        }
        tc_arm32_move_immediate(emitter, scratch, (uint32_t)value);
        tc_arm32_write(emitter, "  add r%u, r%u, r%u\n", dst, base, scratch);
    }
}

void tc_arm32_mask_register(TcArm32Emitter *emitter, unsigned reg,
                            const TcType *type) {
    unsigned width = tc_arm32_type_width(type);
    if (width == 0 || width >= 32u) {
        return;
    }
    {
        unsigned scratch = reg == TC_ARM32_TMP0 ? TC_ARM32_TMP1
                                                : TC_ARM32_TMP0;
        if (width == 8u) {
            tc_arm32_write(emitter, "  and r%u, r%u, #255\n", reg, reg);
            return;
        }
        tc_arm32_move_immediate(emitter, scratch,
                                (uint32_t)tc_arm32_type_mask(type));
        tc_arm32_write(emitter, "  and r%u, r%u, r%u\n", reg, reg, scratch);
    }
}

void tc_arm32_normalize_bool(TcArm32Emitter *emitter, unsigned reg) {
    unsigned id = tc_arm32_label(emitter);
    tc_arm32_write(emitter,
                   "  cmp r%u, #0\n"
                   "  beq .Lbool_zero_%u\n"
                   "  movs r%u, #1\n"
                   ".Lbool_zero_%u:\n",
                   reg, id, reg, id);
}

/* ------------------------------------------------------------------------- */
/* Unaligned-safe scalar memory transfer.                                    */
/* ------------------------------------------------------------------------- */

void tc_arm32_load_bits(TcArm32Emitter *emitter, unsigned base_reg,
                        uint64_t offset, unsigned bytes, unsigned dst_reg) {
    unsigned i;
    unsigned scratch;
    if (bytes == 0 || bytes > 4u) {
        tc_arm32_error(emitter, "internal: load_bits size %u", bytes);
        return;
    }
    if (offset > 4095u) {
        unsigned new_base = (base_reg != TC_ARM32_TMP0 &&
                             dst_reg != TC_ARM32_TMP0)
                                ? TC_ARM32_TMP0
                                : TC_ARM32_TMP1;
        if (new_base == dst_reg) {
            new_base = TC_ARM32_VALUE0;
        }
        tc_arm32_add_constant(emitter, new_base, base_reg, offset);
        base_reg = new_base;
        offset = 0;
    }
    scratch = TC_ARM32_TMP0;
    if (scratch == base_reg || scratch == dst_reg) {
        scratch = TC_ARM32_TMP1;
    }
    if (scratch == base_reg || scratch == dst_reg) {
        scratch = TC_ARM32_VALUE0;
    }
    tc_arm32_write(emitter, "  movs r%u, #0\n", dst_reg);
    for (i = 0; i < bytes; i++) {
        tc_arm32_write(emitter,
                       "  ldrb r%u, [r%u, #%llu]\n",
                       scratch, base_reg,
                       (unsigned long long)(offset + (uint64_t)i));
        if (i == 0) {
            tc_arm32_write(emitter, "  mov r%u, r%u\n", dst_reg, scratch);
        } else {
            tc_arm32_write(emitter, "  orr r%u, r%u, r%u, lsl #%u\n",
                           dst_reg, dst_reg, scratch, 8u * i);
        }
    }
}

void tc_arm32_store_bits(TcArm32Emitter *emitter, unsigned base_reg,
                         uint64_t offset, unsigned bytes, unsigned src_reg) {
    unsigned i;
    unsigned scratch;
    if (bytes == 0 || bytes > 4u) {
        tc_arm32_error(emitter, "internal: store_bits size %u", bytes);
        return;
    }
    if (offset > 4095u) {
        unsigned new_base = (base_reg != TC_ARM32_TMP0 &&
                             src_reg != TC_ARM32_TMP0)
                                ? TC_ARM32_TMP0
                                : TC_ARM32_TMP1;
        if (new_base == src_reg) {
            new_base = TC_ARM32_VALUE0;
        }
        tc_arm32_add_constant(emitter, new_base, base_reg, offset);
        base_reg = new_base;
        offset = 0;
    }
    scratch = TC_ARM32_TMP0;
    if (scratch == base_reg || scratch == src_reg) {
        scratch = TC_ARM32_TMP1;
    }
    if (scratch == base_reg || scratch == src_reg) {
        scratch = TC_ARM32_VALUE0;
    }
    if (bytes < 4u) {
        tc_arm32_write(emitter, "  mov r%u, r%u\n", scratch, src_reg);
        for (i = 0; i < bytes; i++) {
            tc_arm32_write(emitter, "  strb r%u, [r%u, #%llu]\n", scratch,
                           base_reg,
                           (unsigned long long)(offset + (uint64_t)i));
            if (i + 1u < bytes) {
                tc_arm32_write(emitter, "  lsr r%u, r%u, #8\n", scratch,
                               scratch);
            }
        }
        return;
    }
    tc_arm32_write(emitter, "  str r%u, [r%u, #%llu]\n", src_reg, base_reg,
                   (unsigned long long)offset);
}

/* ------------------------------------------------------------------------- */
/* Trap sites.                                                               */
/* ------------------------------------------------------------------------- */

static size_t source_file_index(TcArm32Emitter *emitter, const char *file) {
    size_t index = 0;
    size_t i;
    for (i = 0; i < emitter->source_file_count; i++) {
        if (strcmp(emitter->source_files[i], file) == 0) {
            return i;
        }
    }
    if (emitter->source_file_count >= TC_ARM32_MAX_SOURCE_FILES) {
        tc_arm32_error(emitter, "too many source files to emit trap locations");
        return 0;
    }
    emitter->source_files[emitter->source_file_count] = file;
    index = emitter->source_file_count;
    emitter->source_file_count++;
    return index;
}

void tc_arm32_trap(TcArm32Emitter *emitter, const TcSpan *span,
                   TcErrorKind kind) {
    const char *file =
        span != NULL && span->file != NULL ? span->file : "<source>";
    size_t index = source_file_index(emitter, file);
    tc_arm32_move_immediate(emitter, 0u, (uint32_t)kind);
    tc_arm32_move_immediate(emitter, 1u, span != NULL ? span->line : 0u);
    tc_arm32_move_immediate(emitter, 2u, span != NULL ? span->column : 0u);
    {
        char label[64];
        snprintf(label, sizeof(label), ".Lsource_file_%zu", index);
        tc_arm32_load_symbol(emitter, 3u, label);
    }
    tc_arm32_write(emitter, "  bl %s\n  udf #0\n", TC_RT_SYM_TRAP);
}

/* ------------------------------------------------------------------------- */
/* Slots, operands and memory transfers.                                     */
/* ------------------------------------------------------------------------- */

int tc_arm32_slot_is_object(const TcArm32Slot *slot) {
    return slot->kind == TC_ARM32_SLOT_FRAME ||
           slot->kind == TC_ARM32_SLOT_STATIC;
}

void tc_arm32_slot_address(TcArm32Emitter *emitter, uint32_t vreg,
                           unsigned dst_reg) {
    const TcArm32Slot *slot;
    if (vreg >= emitter->slot_count) {
        tc_arm32_error(emitter, "invalid vreg %u", (unsigned)vreg);
        return;
    }
    slot = &emitter->slots[vreg];
    if (slot->kind == TC_ARM32_SLOT_STATIC) {
        tc_arm32_load_symbol(emitter, dst_reg, TC_RT_SYM_STATIC_START);
        tc_arm32_add_constant(emitter, dst_reg, dst_reg, slot->offset);
    } else {
        tc_arm32_add_constant(emitter, dst_reg, 9u, slot->offset);
    }
}

const TcType *tc_arm32_operand_type(const TcUnit *unit,
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

static const TcType *field_operand_type(TcArm32Emitter *emitter,
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
        const TcField *field = tc_arm32_field_at(emitter->ir, base,
                                                 operand->field.fields[i]);
        if (field == NULL) {
            return NULL;
        }
        base = field->type;
    }
    return base;
}

const TcType *tc_arm32_value_type(TcArm32Emitter *emitter,
                                  const TcOperand *operand) {
    if (operand->kind == TC_OP_FIELD) {
        return field_operand_type(emitter, operand);
    }
    return tc_arm32_operand_type(emitter->unit, operand);
}

void tc_arm32_operand_address(TcArm32Emitter *emitter,
                              const TcOperand *operand, unsigned dst_reg) {
    if (operand->kind == TC_OP_VREG) {
        tc_arm32_slot_address(emitter, operand->vreg, dst_reg);
        return;
    }
    if (operand->kind == TC_OP_FIELD) {
        const TcType *base;
        size_t i;
        uint64_t offset = 0;
        if (operand->field.base >= emitter->unit->nvregs) {
            tc_arm32_error(emitter, "invalid field base vreg");
            return;
        }
        tc_arm32_slot_address(emitter, operand->field.base, dst_reg);
        base = emitter->unit->vreg_types[operand->field.base];
        for (i = 0; i < operand->field.nfields; i++) {
            const TcField *field = tc_arm32_field_at(emitter->ir, base,
                                                     operand->field.fields[i]);
            if (field == NULL) {
                tc_arm32_error(emitter, "invalid field path");
                return;
            }
            offset += field->offset;
            base = field->type;
        }
        if (offset != 0) {
            tc_arm32_add_constant(emitter, dst_reg, dst_reg, offset);
        }
        return;
    }
    if (operand->kind == TC_OP_CONST) {
        const TcType *type = operand->konst.type;
        unsigned width = tc_arm32_type_width(type);
        unsigned scratch = dst_reg == TC_ARM32_TMP0 ? TC_ARM32_TMP1
                                                    : TC_ARM32_TMP0;
        tc_arm32_add_constant(emitter, dst_reg, 9u, emitter->scratch_offset);
        tc_arm32_move_immediate(emitter, scratch,
                                (uint32_t)operand->konst.bits);
        tc_arm32_write(emitter, "  str r%u, [r%u]\n", scratch, dst_reg);
        if (width > 32u) {
            tc_arm32_move_immediate(
                emitter, scratch, (uint32_t)(operand->konst.bits >> 32));
            tc_arm32_write(emitter, "  str r%u, [r%u, #4]\n", scratch,
                           dst_reg);
        }
        return;
    }
    tc_arm32_error(emitter, "operand kind %d has no address", (int)operand->kind);
}

void tc_arm32_load_operand32(TcArm32Emitter *emitter,
                             const TcOperand *operand, unsigned dst_reg) {
    if (operand->kind == TC_OP_CONST) {
        tc_arm32_move_immediate(emitter, dst_reg,
                                (uint32_t)operand->konst.bits);
        return;
    }
    tc_arm32_operand_address(emitter, operand, TC_ARM32_TMP0);
    {
        unsigned width = tc_arm32_type_width(
            tc_arm32_value_type(emitter, operand));
        unsigned bytes = width == 0 ? 4u : (width >= 32u ? 4u : width / 8u);
        tc_arm32_load_bits(emitter, TC_ARM32_TMP0, 0, bytes, dst_reg);
    }
}

void tc_arm32_load_operand64(TcArm32Emitter *emitter,
                             const TcOperand *operand, unsigned dst_lo,
                             unsigned dst_hi) {
    const TcType *type = tc_arm32_value_type(emitter, operand);
    unsigned width = tc_arm32_type_width(type);
    if (operand->kind == TC_OP_CONST) {
        uint64_t bits = operand->konst.bits;
        tc_arm32_move_immediate(emitter, dst_lo, (uint32_t)bits);
        if (width > 32u) {
            tc_arm32_move_immediate(emitter, dst_hi,
                                    (uint32_t)(bits >> 32));
        } else {
            tc_arm32_write(emitter, "  movs r%u, #0\n", dst_hi);
        }
        return;
    }
    tc_arm32_operand_address(emitter, operand, TC_ARM32_TMP0);
    if (width > 32u) {
        tc_arm32_load_bits(emitter, TC_ARM32_TMP0, 0, 4u, dst_lo);
        tc_arm32_load_bits(emitter, TC_ARM32_TMP0, 4, 4u, dst_hi);
    } else {
        unsigned bytes = width == 0 ? 4u : width / 8u;
        tc_arm32_load_bits(emitter, TC_ARM32_TMP0, 0, bytes, dst_lo);
        tc_arm32_write(emitter, "  movs r%u, #0\n", dst_hi);
    }
}

static int operand_is_tracked_object(TcArm32Emitter *emitter,
                                     const TcOperand *operand) {
    if (operand->kind == TC_OP_FIELD) {
        if (operand->field.base >= emitter->slot_count) {
            return 0;
        }
        return tc_arm32_slot_is_object(&emitter->slots[operand->field.base]);
    }
    if (operand->kind == TC_OP_VREG) {
        if (operand->vreg >= emitter->slot_count) {
            return 0;
        }
        return tc_arm32_slot_is_object(&emitter->slots[operand->vreg]);
    }
    return 0;
}

static void store_at_operand(TcArm32Emitter *emitter,
                             const TcOperand *operand, unsigned src_lo,
                             unsigned src_hi) {
    const TcType *type = tc_arm32_value_type(emitter, operand);
    unsigned width = tc_arm32_type_width(type);
    unsigned bytes;
    tc_arm32_operand_address(emitter, operand, TC_ARM32_TMP0);
    if (width > 32u) {
        tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 0, 4u, src_lo);
        tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 4, 4u, src_hi);
        bytes = 8u;
    } else {
        bytes = width == 0 ? 4u : width / 8u;
        tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 0, bytes, src_lo);
    }
    if (emitter->profile->checked_build &&
        operand_is_tracked_object(emitter, operand)) {
        tc_arm32_define_range(emitter, TC_ARM32_TMP0, bytes);
    }
}

void tc_arm32_store_result64(TcArm32Emitter *emitter, const TcInstr *instr,
                             unsigned src_lo, unsigned src_hi) {
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_arm32_add_constant(emitter, TC_ARM32_TMP0, 9u,
                              emitter->scratch_buffer_offset);
        if (tc_arm32_type_width(instr->type) > 32u) {
            tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 4, 4u, src_hi);
        }
        tc_arm32_store_bits(emitter, TC_ARM32_TMP0, 0, 4u, src_lo);
        return;
    }
    store_at_operand(emitter, &instr->dst, src_lo, src_hi);
}

void tc_arm32_store_operand64(TcArm32Emitter *emitter,
                              const TcOperand *operand, unsigned src_lo,
                              unsigned src_hi) {
    store_at_operand(emitter, operand, src_lo, src_hi);
}

void tc_arm32_copy_bytes(TcArm32Emitter *emitter, unsigned src_reg,
                         unsigned dst_reg, uint64_t size) {
    tc_arm32_write(emitter, "  mov r0, r%u\n", dst_reg);
    tc_arm32_write(emitter, "  mov r1, r%u\n", src_reg);
    tc_arm32_move_immediate(emitter, 2u, (uint32_t)size);
    tc_arm32_write(emitter, "  bl memcpy\n");
}

void tc_arm32_define_range(TcArm32Emitter *emitter, unsigned address_reg,
                           uint64_t size) {
    tc_arm32_write(emitter, "  mov r0, r%u\n", address_reg);
    tc_arm32_move_immediate(emitter, 1u, (uint32_t)size);
    tc_arm32_write(emitter, "  bl %s\n", TC_RT_SYM_DEFINE);
}

void tc_arm32_emit_call0(TcArm32Emitter *emitter, const char *symbol) {
    tc_arm32_write(emitter, "  bl %s\n", symbol);
}

/* ------------------------------------------------------------------------- */
/* Helper-call argument words.                                               */
/* ------------------------------------------------------------------------- */

void tc_arm32_arg_immediate(TcArm32Emitter *emitter, unsigned word,
                            uint32_t value) {
    if (word < 4u) {
        tc_arm32_move_immediate(emitter, word, value);
    } else {
        tc_arm32_move_immediate(emitter, TC_ARM32_TMP0, value);
        tc_arm32_write(emitter, "  str r%u, [sp, #%u]\n", TC_ARM32_TMP0,
                       (word - 4u) * 4u);
    }
}

void tc_arm32_arg_reg(TcArm32Emitter *emitter, unsigned word, unsigned reg) {
    if (word < 4u) {
        tc_arm32_write(emitter, "  mov r%u, r%u\n", word, reg);
    } else {
        tc_arm32_write(emitter, "  str r%u, [sp, #%u]\n", reg,
                       (word - 4u) * 4u);
    }
}

void tc_arm32_arg_operand(TcArm32Emitter *emitter, unsigned word,
                          const TcOperand *operand, int high) {
    if (word < 4u) {
        if (!high) {
            tc_arm32_load_operand32(emitter, operand, word);
        } else {
            tc_arm32_load_operand64(emitter, operand, TC_ARM32_VALUE0,
                                    TC_ARM32_VALUE1);
            tc_arm32_write(emitter, "  mov r%u, r%u\n", word,
                           TC_ARM32_VALUE1);
        }
        return;
    }
    tc_arm32_load_operand64(emitter, operand, TC_ARM32_VALUE0,
                            TC_ARM32_VALUE1);
    tc_arm32_write(emitter, "  str r%u, [sp, #%u]\n",
                   high ? TC_ARM32_VALUE1 : TC_ARM32_VALUE0,
                   (word - 4u) * 4u);
}

void tc_arm32_arg_address(TcArm32Emitter *emitter, unsigned word,
                          const TcOperand *operand) {
    tc_arm32_operand_address(emitter, operand, TC_ARM32_TMP0);
    tc_arm32_arg_reg(emitter, word, TC_ARM32_TMP0);
}

void tc_arm32_arg_file(TcArm32Emitter *emitter, unsigned word,
                       const TcSpan *span) {
    const char *file =
        span != NULL && span->file != NULL ? span->file : "<source>";
    size_t index = source_file_index(emitter, file);
    {
        char label[64];
        snprintf(label, sizeof(label), ".Lsource_file_%zu", index);
        tc_arm32_load_symbol(emitter, TC_ARM32_TMP0, label);
    }
    tc_arm32_arg_reg(emitter, word, TC_ARM32_TMP0);
}

/* ------------------------------------------------------------------------- */
/* Frame layout.                                                             */
/* ------------------------------------------------------------------------- */

static const TcType *value_type(TcArm32Emitter *emitter,
                                const TcOperand *operand) {
    return tc_arm32_value_type(emitter, operand);
}

void tc_arm32_function_signature(TcArm32Emitter *emitter,
                                 const TcInstr *instr,
                                 const TcType ***out_params,
                                 size_t *out_count,
                                 const TcType **out_ret) {
    *out_params = NULL;
    *out_count = 0;
    *out_ret = NULL;
    if (instr->op == TC_CALL) {
        uint32_t func_id;
        const TcFunc *callee;
        size_t i;
        if (instr->nargs < 1 || instr->args[0].value.kind != TC_OP_FUNC) {
            tc_arm32_error(emitter, "direct call without a function operand");
            return;
        }
        func_id = instr->args[0].value.func;
        if (func_id >= emitter->ir->nfuncs) {
            tc_arm32_error(emitter, "direct call to an invalid function");
            return;
        }
        callee = &emitter->ir->funcs[func_id];
        *out_count = callee->sig.nparams;
        *out_ret = callee->sig.ret;
        if (*out_count != 0) {
            const TcType **params =
                (const TcType **)tc_allocator_calloc(emitter->alloc, *out_count, sizeof(**out_params));
            if (params == NULL) {
                tc_arm32_oom(emitter, "out of memory");
                return;
            }
            for (i = 0; i < *out_count; i++) {
                params[i] = callee->sig.params[i].type;
            }
            *out_params = params;
        }
        return;
    }
    if (instr->op == TC_CALL_INDIRECT) {
        const TcType *signature;
        size_t i;
        if (instr->nargs < 1) {
            tc_arm32_error(emitter, "call_indirect without a function pointer");
            return;
        }
        signature = value_type(emitter, &instr->args[0].value);
        if (signature == NULL || signature->tag != TC_TY_FNPTR) {
            tc_arm32_error(emitter,
                           "call_indirect without a function pointer type");
            return;
        }
        *out_count = signature->fnptr.n;
        *out_ret = signature->fnptr.ret;
        if (*out_count != 0) {
            const TcType **params =
                (const TcType **)tc_allocator_calloc(emitter->alloc, *out_count, sizeof(**out_params));
            if (params == NULL) {
                tc_arm32_oom(emitter, "out of memory");
                return;
            }
            for (i = 0; i < *out_count; i++) {
                params[i] = signature->fnptr.params[i];
            }
            *out_params = params;
        }
        return;
    }
    tc_arm32_error(emitter, "instruction is not a call");
}

static int build_layout(TcArm32Emitter *emitter, const TcUnit *unit) {
    uint64_t cursor = 0;
    uint64_t frame_objects = 0;
    size_t v;
    size_t b;
    size_t i;
    emitter->unit = unit;
    emitter->slot_count = unit->nvregs;
    emitter->slots = (TcArm32Slot *)tc_allocator_calloc(emitter->alloc, unit->nvregs == 0 ? 1u
                                                              : unit->nvregs,
                                           sizeof(TcArm32Slot));
    if (emitter->slots == NULL) {
        tc_arm32_oom(emitter, "out of memory while laying out %u vregs",
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
            tc_arm32_error(emitter, "vreg %u has no type", (unsigned)v);
            return 0;
        }
        if (unit->vreg_objects != NULL && v < unit->nvreg_objects) {
            object_id = unit->vreg_objects[v];
        }
        if (object_id != UINT32_MAX) {
            const TcObject *object;
            if (object_id >= emitter->ir->nobjects) {
                tc_arm32_error(emitter, "vreg %u refers to invalid object %u",
                               (unsigned)v, (unsigned)object_id);
                return 0;
            }
            object = &emitter->ir->objects[object_id];
            if (object->region == TC_REG_STATIC) {
                emitter->slots[v].kind = TC_ARM32_SLOT_STATIC;
            } else if (object->region == TC_REG_FRAME) {
                emitter->slots[v].kind = TC_ARM32_SLOT_FRAME;
                if (object->offset + object->size > emitter->frame_object_end) {
                    emitter->frame_object_end = object->offset + object->size;
                }
                frame_objects++;
            } else {
                tc_arm32_error(emitter,
                               "vreg %u has unsupported object region %d",
                               (unsigned)v, (int)object->region);
                return 0;
            }
            emitter->slots[v].offset = object->offset;
            emitter->slots[v].size = object->size;
            emitter->slots[v].type = type;
            continue;
        }
        size = tc_arm32_type_size(emitter->ir, type, &ok);
        if (!ok || size == 0) {
            tc_arm32_error(emitter, "vreg %u has an incomplete type",
                           (unsigned)v);
            return 0;
        }
        emitter->slots[v].kind = TC_ARM32_SLOT_TEMP;
        emitter->slots[v].size = size;
        emitter->slots[v].type = type;
    }
    cursor = (emitter->frame_object_end + 7u) & ~UINT64_C(7);
    for (v = 0; v < unit->nvregs; v++) {
        if (emitter->slots[v].kind != TC_ARM32_SLOT_TEMP) {
            continue;
        }
        cursor = (cursor + 7u) & ~UINT64_C(7);
        emitter->slots[v].offset = cursor;
        cursor += emitter->slots[v].size;
    }
    emitter->scratch_offset = (cursor + 7u) & ~UINT64_C(7);
    cursor = emitter->scratch_offset + 8u;
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
                needed = tc_arm32_type_size(emitter->ir, instr->type, &ok);
                if (!ok) {
                    needed = 0;
                }
            } else if (instr->op == TC_CALL ||
                       instr->op == TC_CALL_INDIRECT) {
                const TcType **params = NULL;
                size_t nparams = 0;
                const TcType *ret = NULL;
                TcArm32RetPlan ret_plan;
                tc_arm32_function_signature(emitter, instr, &params, &nparams, &ret);
                emitter->alloc->free(emitter->alloc->ctx, params);
                if (emitter->failed) {
                    return 0;
                }
                tc_arm32_plan_return(emitter, ret, &ret_plan);
                if (ret_plan.kind == TC_ARM32_RET_INDIRECT) {
                    needed = tc_arm32_type_size(emitter->ir, ret, &ok);
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
        TcArm32RetPlan ret;
        const TcType *ret_type = NULL;
        if (unit->func_id != TC_UNIT_TOP_LEVEL &&
            unit->func_id < emitter->ir->nfuncs) {
            ret_type = emitter->ir->funcs[unit->func_id].sig.ret;
        }
        tc_arm32_plan_return(emitter, ret_type, &ret);
        if (ret.kind == TC_ARM32_RET_INDIRECT) {
            emitter->sret_offset = (cursor + 3u) & ~UINT64_C(3);
            cursor = emitter->sret_offset + 4u;
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
        if (emitter->slots[v].kind != TC_ARM32_SLOT_FRAME) {
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
            const TcType *type = emitter->slots[v].type;
            size_t pending = 0;
            size_t count = 0;
            const TcType **stack = NULL;
            size_t capacity = 0;
            (void)frame_objects;
            if (type != NULL && type->tag == TC_TY_STRUCT) {
                if (pending + 1u > capacity) {
                    capacity = 8u;
                    stack = (const TcType **)tc_allocator_calloc(emitter->alloc, capacity,
                                                    sizeof(*stack));
                    if (stack == NULL) {
                        tc_arm32_oom(emitter, "out of memory");
                        return 0;
                    }
                }
                stack[pending++] = type;
                count = 1;
                while (pending != 0) {
                    const TcType *current = stack[--pending];
                    const TcStructDef *def =
                        tc_arm32_struct_def(emitter->ir, current);
                    size_t k;
                    if (def == NULL) {
                        emitter->alloc->free(emitter->alloc->ctx, stack);
                        tc_arm32_error(emitter, "invalid struct type");
                        return 0;
                    }
                    for (k = 0; k < def->nfields; k++) {
                        const TcType *field = def->fields[k].type;
                        count++;
                        if (field->tag == TC_TY_STRUCT) {
                            if (pending == capacity) {
                                const TcType **grown;
                                capacity *= 2u;
                                grown = (const TcType **)emitter->alloc->realloc(emitter->alloc->ctx,
                                    stack, capacity * sizeof(*stack));
                                if (grown == NULL) {
                                    emitter->alloc->free(emitter->alloc->ctx, stack);
                                    tc_arm32_oom(emitter, "out of memory");
                                    return 0;
                                }
                                stack = grown;
                            }
                            stack[pending++] = field;
                        }
                    }
                }
                emitter->alloc->free(emitter->alloc->ctx, stack);
            } else {
                count = 1;
            }
            emitter->frame_desc_count += count;
        }
    }
    /* Helper calls also use the outgoing area: load/store/memmove/ptradd pass
     * four words on the stack, the scalar wrappers up to six.  Reserve a
     * fixed 32-byte area so helper stack arguments never overlap locals even
     * when the unit makes no TC calls. */
    emitter->outgoing_size = 32u;
    for (b = 0; b < unit->nblocks; b++) {
        for (i = 0; i < unit->blocks[b].n; i++) {
            const TcInstr *instr = &unit->blocks[b].instrs[i];
            if (instr->op != TC_CALL && instr->op != TC_CALL_INDIRECT) {
                continue;
            }
            {
                const TcType **params = NULL;
                size_t nparams = 0;
                const TcType *ret = NULL;
                TcArm32ArgPlan plan;
                TcArm32RetPlan ret_plan;
                tc_arm32_function_signature(emitter, instr, &params, &nparams, &ret);
                if (emitter->failed) {
                    emitter->alloc->free(emitter->alloc->ctx, params);
                    return 0;
                }
                tc_arm32_plan_return(emitter, ret, &ret_plan);
                tc_arm32_plan_arguments(
                    emitter, params, nparams,
                    ret_plan.kind == TC_ARM32_RET_INDIRECT ? 1 : 0, &plan);
                emitter->alloc->free(emitter->alloc->ctx, params);
                if (emitter->failed) {
                    tc_arm32_free_argument_plan(&plan);
                    return 0;
                }
                if (plan.outgoing_size > emitter->outgoing_size) {
                    emitter->outgoing_size = plan.outgoing_size;
                }
                tc_arm32_free_argument_plan(&plan);
            }
        }
    }
    cursor = (cursor + 7u) & ~UINT64_C(7);
    {
        uint64_t stack_limit = emitter->profile->stack_size;
        uint64_t depth = emitter->profile->depth_D != 0
                             ? emitter->profile->depth_D
                             : 1u;
        uint64_t worst;
        if (stack_limit == 0) {
            stack_limit = emitter->profile->region[TC_REG_FRAME].size;
        }
        worst = cursor + emitter->outgoing_size;
        if (worst == 0) {
            worst = 8;
        }
        if (worst > stack_limit || (depth + 1u) > UINT64_MAX / worst ||
            worst * (depth + 1u) > stack_limit) {
            tc_arm32_error(emitter,
                           "frame size %llu cannot satisfy the profile stack "
                           "budget",
                           (unsigned long long)cursor);
            return 0;
        }
    }
    emitter->frame_size = cursor;
    emitter->incoming_stack_offset = 40u + emitter->frame_size;
    return 1;
}

static void free_layout(TcArm32Emitter *emitter) {
    emitter->alloc->free(emitter->alloc->ctx, emitter->slots);
    emitter->slots = NULL;
    emitter->slot_count = 0;
    emitter->unit = NULL;
}

/* ------------------------------------------------------------------------- */
/* Parameter setup.                                                          */
/* ------------------------------------------------------------------------- */

static void emit_parameter_setup(TcArm32Emitter *emitter,
                                 const TcFunc *func) {
    TcArm32ArgPlan plan;
    const TcType **params = NULL;
    size_t nparams = func->sig.nparams;
    size_t i;
    if (nparams != 0) {
        params = (const TcType **)tc_allocator_calloc(emitter->alloc, nparams, sizeof(*params));
        if (params == NULL) {
            tc_arm32_oom(emitter, "out of memory");
            return;
        }
        for (i = 0; i < nparams; i++) {
            params[i] = func->sig.params[i].type;
        }
    }
    {
        TcArm32RetPlan ret_plan;
        tc_arm32_plan_return(emitter, func->sig.ret, &ret_plan);
        tc_arm32_plan_arguments(
            emitter, params, nparams,
            ret_plan.kind == TC_ARM32_RET_INDIRECT ? 1 : 0, &plan);
    }
    emitter->alloc->free(emitter->alloc->ctx, params);
    if (emitter->failed) {
        return;
    }
    for (i = 0; i < nparams; i++) {
        const TcType *type = func->sig.params[i].type;
        const TcArm32ArgSlot *loc = &plan.slots[i];
        uint64_t slot_offset = emitter->slots[i].offset;
        unsigned width = tc_arm32_type_width(type);
        int ok = 0;
        uint64_t size = tc_arm32_type_size(emitter->ir, type, &ok);
        if (!ok) {
            tc_arm32_error(emitter, "parameter %u has an incomplete type",
                           (unsigned)i);
            tc_arm32_free_argument_plan(&plan);
            return;
        }
        if (loc->struct_value) {
            unsigned k;
            for (k = 0; k < loc->reg_words; k++) {
                uint64_t offset = (uint64_t)k * 4u;
                uint64_t remaining = size - offset;
                unsigned bytes = remaining >= 4u ? 4u
                                                 : (unsigned)remaining;
                tc_arm32_store_bits(emitter, 9u, slot_offset + offset,
                                    bytes, loc->reg + k);
            }
            if (loc->stack_words != 0) {
                tc_arm32_add_constant(
                    emitter, TC_ARM32_TMP0, 9u,
                    emitter->incoming_stack_offset + loc->stack_offset);
                for (k = 0; k < loc->stack_words; k++) {
                    uint64_t source_offset = (uint64_t)k * 4u;
                    uint64_t destination_offset =
                        loc->stack_byte_offset + source_offset;
                    uint64_t remaining = size - destination_offset;
                    unsigned bytes;
                    if (remaining == 0 || remaining > size) {
                        break;
                    }
                    bytes = remaining >= 4u ? 4u : (unsigned)remaining;
                    /* store_bits uses the scratch registers, so recompute the
                     * incoming address for every stack word. */
                    tc_arm32_add_constant(
                        emitter, TC_ARM32_TMP0, 9u,
                        emitter->incoming_stack_offset +
                            loc->stack_offset + source_offset);
                    tc_arm32_write(emitter, "  ldr r%u, [r%u]\n",
                                   TC_ARM32_VALUE0, TC_ARM32_TMP0);
                    tc_arm32_store_bits(emitter, 9u,
                                        slot_offset + destination_offset,
                                        bytes, TC_ARM32_VALUE0);
                }
            }
            continue;
        }
        if (loc->kind == TC_ARM32_ARG_REG) {
            if (loc->is64) {
                tc_arm32_store_bits(emitter, 9u, slot_offset, 4u, loc->reg);
                tc_arm32_store_bits(emitter, 9u, slot_offset + 4u, 4u,
                                    loc->reg + 1u);
            } else {
                unsigned bytes = width == 0 ? 4u : width / 8u;
                tc_arm32_store_bits(emitter, 9u, slot_offset, bytes,
                                    loc->reg);
            }
            continue;
        }
        /* Stack argument: the incoming stack area starts at the caller's sp,
         * which is a fixed offset above the locals base. */
        tc_arm32_add_constant(emitter, TC_ARM32_TMP0, 9u,
                              emitter->incoming_stack_offset +
                                  loc->stack_offset);
        if (loc->is64) {
            tc_arm32_write(emitter, "  ldr r%u, [r%u]\n",
                           TC_ARM32_VALUE0, TC_ARM32_TMP0);
            tc_arm32_write(emitter, "  ldr r%u, [r%u, #4]\n",
                           TC_ARM32_VALUE1, TC_ARM32_TMP0);
            tc_arm32_store_bits(emitter, 9u, slot_offset, 4u,
                                TC_ARM32_VALUE0);
            tc_arm32_store_bits(emitter, 9u, slot_offset + 4u, 4u,
                                TC_ARM32_VALUE1);
        } else {
            tc_arm32_write(emitter, "  ldr r%u, [r%u]\n",
                           TC_ARM32_VALUE0, TC_ARM32_TMP0);
            {
                unsigned bytes = width == 0 ? 4u : width / 8u;
                tc_arm32_store_bits(emitter, 9u, slot_offset, bytes,
                                    TC_ARM32_VALUE0);
            }
        }
    }
    tc_arm32_free_argument_plan(&plan);
}

/* ------------------------------------------------------------------------- */
/* Unit emission.                                                            */
/* ------------------------------------------------------------------------- */

static uint64_t frame_total(const TcArm32Emitter *emitter) {
    return 4u + emitter->frame_size + emitter->outgoing_size;
}

static void emit_epilogue(TcArm32Emitter *emitter, size_t unit_index) {
    uint64_t total = frame_total(emitter);
    tc_arm32_write(emitter, ".Lreturn_%zu:\n", unit_index);
    tc_arm32_move_immediate(emitter, TC_ARM32_TMP0, (uint32_t)total);
    tc_arm32_write(emitter, "  add sp, sp, r%u\n", TC_ARM32_TMP0);
    tc_arm32_write(emitter, "  pop {r4-r11, pc}\n");
}

static void emit_unit(TcArm32Emitter *emitter, const TcUnit *unit,
                      size_t unit_index) {
    const TcFunc *func = NULL;
    const char *symbol;
    size_t b;
    size_t i;
    if (!build_layout(emitter, unit)) {
        free_layout(emitter);
        return;
    }
    emitter->unit_index = unit_index;
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        symbol = TC_ARM32_TOP_LEVEL_SYMBOL;
    } else {
        if (unit->func_id >= emitter->ir->nfuncs) {
            tc_arm32_error(emitter, "unit refers to invalid function %u",
                           (unsigned)unit->func_id);
            free_layout(emitter);
            return;
        }
        func = &emitter->ir->funcs[unit->func_id];
        symbol = NULL;
    }
    tc_arm32_write(emitter, "\n  .text\n  .p2align 2\n");
    if (symbol != NULL) {
        tc_arm32_write(emitter,
                       "  .globl %s\n  .thumb_func\n"
                       "  .type %s, %%function\n%s:\n",
                       symbol, symbol, symbol);
    } else {
        tc_arm32_write(emitter,
                       "  .thumb_func\n"
                       "  .type tc_function_%u, %%function\n"
                       "tc_function_%u:\n",
                       (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    tc_arm32_write(emitter, "  push {r4-r11, lr}\n");
    tc_arm32_move_immediate(emitter, TC_ARM32_TMP0,
                            (uint32_t)frame_total(emitter));
    tc_arm32_write(emitter, "  sub sp, sp, r%u\n", TC_ARM32_TMP0);
    if (emitter->outgoing_size != 0) {
        tc_arm32_add_constant(emitter, 9u, 13u, emitter->outgoing_size);
    } else {
        tc_arm32_write(emitter, "  mov r9, sp\n");
    }
    tc_arm32_load_symbol(emitter, 10u, TC_RT_SYM_STATIC_START);
    if (emitter->sret_offset != 0) {
        tc_arm32_store_bits(emitter, 9u, emitter->sret_offset, 4u, 0u);
    }
    if (func != NULL) {
        emit_parameter_setup(emitter, func);
        if (emitter->failed) {
            free_layout(emitter);
            return;
        }
    }
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        tc_arm32_emit_call0(emitter, TC_RT_SYM_PROGRAM_INIT);
    }
    /* Frame descriptor registration. */
    {
        char label[64];
        snprintf(label, sizeof(label), ".Lunit_objs_%zu", unit_index);
        tc_arm32_load_symbol(emitter, 0u, label);
    }
    tc_arm32_move_immediate(emitter, 1u,
                            (uint32_t)emitter->frame_desc_count);
    tc_arm32_write(emitter, "  mov r2, r9\n");
    tc_arm32_move_immediate(emitter, 3u, (uint32_t)emitter->frame_size);
    if (emitter->profile->checked_build) {
        tc_arm32_add_constant(emitter, TC_ARM32_TMP0, 9u,
                              emitter->shadow_offset);
        tc_arm32_write(emitter, "  str r%u, [sp, #0]\n", TC_ARM32_TMP0);
        tc_arm32_move_immediate(emitter, TC_ARM32_TMP0,
                                (uint32_t)emitter->shadow_size);
        tc_arm32_write(emitter, "  str r%u, [sp, #4]\n", TC_ARM32_TMP0);
    } else {
        tc_arm32_write(emitter, "  movs r%u, #0\n  str r%u, [sp, #0]\n"
                                "  str r%u, [sp, #4]\n",
                       TC_ARM32_TMP0, TC_ARM32_TMP0, TC_ARM32_TMP0);
    }
    tc_arm32_emit_call0(emitter, TC_RT_SYM_FRAME_ENTER);
    if (emitter->profile->checked_build && func != NULL) {
        size_t p;
        for (p = 0; p < func->sig.nparams; p++) {
            int okp = 0;
            uint64_t psize =
                tc_arm32_type_size(emitter->ir, func->sig.params[p].type,
                                   &okp);
            if (!okp) {
                continue;
            }
            tc_arm32_add_constant(emitter, 0u, 9u,
                                  emitter->slots[p].offset);
            tc_arm32_define_range(emitter, 0u, psize);
        }
    }
    for (b = 0; b < unit->nblocks; b++) {
        tc_arm32_write(emitter, ".L%zu_block%zu:\n", unit_index, b);
        for (i = 0; i < unit->blocks[b].n; i++) {
            tc_arm32_emit_instruction(emitter, &unit->blocks[b].instrs[i]);
            if (emitter->failed) {
                free_layout(emitter);
                return;
            }
        }
    }
    emit_epilogue(emitter, unit_index);
    if (func == NULL) {
        tc_arm32_write(emitter, "\n  .size %s, .-%s\n",
                       TC_ARM32_TOP_LEVEL_SYMBOL,
                       TC_ARM32_TOP_LEVEL_SYMBOL);
    } else {
        tc_arm32_write(emitter, "\n  .size tc_function_%u, .-tc_function_%u\n",
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
} TcArm32DescList;

static int desc_push(TcArm32Emitter *emitter, TcArm32DescList *list,
                     TcRtObjectDesc desc) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 16u : list->capacity * 2u;
        TcRtObjectDesc *items = (TcRtObjectDesc *)emitter->alloc->realloc(emitter->alloc->ctx,
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

static int add_field_descs(TcArm32Emitter *emitter, TcArm32DescList *list,
                           uint64_t offset, const TcType *type, int writable) {
    const TcStructDef *def = tc_arm32_struct_def(emitter->ir, type);
    size_t i;
    if (def == NULL) {
        return type == NULL || type->tag != TC_TY_STRUCT;
    }
    for (i = 0; i < def->nfields; i++) {
        const TcField *field = &def->fields[i];
        int ok = 0;
        uint64_t size = tc_arm32_type_size(emitter->ir, field->type, &ok);
        TcRtObjectDesc desc;
        if (!ok) {
            tc_arm32_error(emitter, "field %u has an incomplete type",
                           (unsigned)i);
            return 0;
        }
        desc.offset = offset + field->offset;
        desc.size = size;
        desc.type_id = tc_arm32_type_id(emitter, field->type);
        desc.flags = writable ? TC_RT_OBJECT_WRITABLE : 0;
        if (!desc_push(emitter, list, desc)) {
            tc_arm32_oom(emitter, "out of memory while building descriptors");
            return 0;
        }
        if (!add_field_descs(emitter, list, desc.offset, field->type,
                             writable)) {
            return 0;
        }
    }
    return 1;
}

static void emit_unit_descriptors(TcArm32Emitter *emitter,
                                  const TcUnit *unit, size_t unit_index) {
    TcArm32DescList list;
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
                if (earlier < unit->nvreg_objects &&
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
        desc.type_id = tc_arm32_type_id(emitter, object->type);
        desc.flags = object->writable ? TC_RT_OBJECT_WRITABLE : 0;
        if (!desc_push(emitter, &list, desc)) {
            tc_arm32_oom(emitter, "out of memory while building descriptors");
            break;
        }
        if (!add_field_descs(emitter, &list, object->offset, object->type,
                             object->writable)) {
            break;
        }
    }
    tc_arm32_write(emitter, "\n  .section .rodata\n  .p2align 3\n"
                            ".Lunit_objs_%zu:\n",
                   unit_index);
    if (list.count == 0) {
        tc_arm32_write(emitter, "  .quad 0\n  .quad 0\n  .long 0\n  .long 0\n");
    } else {
        size_t i;
        for (i = 0; i < list.count; i++) {
            tc_arm32_write(emitter,
                           "  .quad %llu\n  .quad %llu\n  .long %u\n"
                           "  .long %u\n",
                           (unsigned long long)list.items[i].offset,
                           (unsigned long long)list.items[i].size,
                           (unsigned)list.items[i].type_id,
                           (unsigned)list.items[i].flags);
        }
    }
    emitter->alloc->free(emitter->alloc->ctx, list.items);
}

static int emit_static_region(TcArm32Emitter *emitter) {
    uint64_t size = 0;
    uint8_t *bytes = NULL;
    size_t i;
    uint64_t pos;
    TcArm32DescList list;
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        if (object->offset > UINT64_MAX - object->size) {
            tc_arm32_error(emitter, "static object offset overflows");
            return 0;
        }
        if (object->offset + object->size > size) {
            size = object->offset + object->size;
        }
    }
    if (size > (uint64_t)SIZE_MAX) {
        tc_arm32_error(emitter, "static data is too large");
        return 0;
    }
    bytes = (uint8_t *)tc_allocator_calloc(emitter->alloc, (size_t)(size == 0 ? 1u : size), 1u);
    if (bytes == NULL) {
        tc_arm32_oom(emitter, "out of memory while building static data");
        return 0;
    }
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        if (object->region != TC_REG_STATIC || object->init == NULL) {
            continue;
        }
        if (object->init->size > object->size ||
            object->init->size > size - object->offset) {
            emitter->alloc->free(emitter->alloc->ctx, bytes);
            tc_arm32_error(emitter, "static object initializer overflows");
            return 0;
        }
        if (object->init->size != 0) {
            if (object->init->bytes == NULL) {
                emitter->alloc->free(emitter->alloc->ctx, bytes);
                tc_arm32_error(emitter, "static initializer has no bytes");
                return 0;
            }
            memcpy(bytes + object->offset, object->init->bytes,
                   object->init->size);
        }
    }
    tc_arm32_write(emitter, "\n  .data\n  .p2align 3\n"
                            "  .globl %s\n"
                            "  .type %s, %%object\n%s:\n",
                   TC_RT_SYM_STATIC_START, TC_RT_SYM_STATIC_START,
                   TC_RT_SYM_STATIC_START);
    pos = 0;
    while (pos < size) {
        uint64_t run = 0;
        while (pos + run < size && bytes[pos + run] == 0) {
            run++;
        }
        if (run >= 8u) {
            tc_arm32_write(emitter, "  .zero %llu\n",
                           (unsigned long long)run);
            pos += run;
            continue;
        }
        tc_arm32_write(emitter, "  .byte 0x%02x\n", (unsigned)bytes[pos]);
        pos++;
    }
    if (size == 0) {
        tc_arm32_write(emitter, "  .byte 0\n");
    }
    tc_arm32_write(emitter, "  .globl %s\n%s:\n",
                   TC_RT_SYM_STATIC_END, TC_RT_SYM_STATIC_END);
    emitter->alloc->free(emitter->alloc->ctx, bytes);
    memset(&list, 0, sizeof(list));
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        TcRtObjectDesc desc;
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        desc.offset = object->offset;
        desc.size = object->size;
        desc.type_id = tc_arm32_type_id(emitter, object->type);
        desc.flags = (object->writable ? TC_RT_OBJECT_WRITABLE : 0) |
                     (object->init != NULL ? TC_RT_OBJECT_HAS_INIT : 0);
        if (!desc_push(emitter, &list, desc)) {
            tc_arm32_oom(emitter, "out of memory");
            emitter->alloc->free(emitter->alloc->ctx, list.items);
            return 0;
        }
        if (!add_field_descs(emitter, &list, object->offset, object->type,
                             object->writable)) {
            emitter->alloc->free(emitter->alloc->ctx, list.items);
            return 0;
        }
    }
    tc_arm32_write(emitter, "  .section .rodata\n  .p2align 3\n"
                            "  .globl %s\n%s:\n",
                   TC_RT_SYM_STATIC_OBJECTS, TC_RT_SYM_STATIC_OBJECTS);
    if (list.count == 0) {
        tc_arm32_write(emitter, "  .quad 0\n  .quad 0\n  .long 0\n  .long 0\n");
    } else {
        for (i = 0; i < list.count; i++) {
            tc_arm32_write(emitter,
                           "  .quad %llu\n  .quad %llu\n  .long %u\n"
                           "  .long %u\n",
                           (unsigned long long)list.items[i].offset,
                           (unsigned long long)list.items[i].size,
                           (unsigned)list.items[i].type_id,
                           (unsigned)list.items[i].flags);
        }
    }
    tc_arm32_write(emitter, "  .p2align 3\n  .globl %s\n%s:\n  .quad %llu\n",
                   TC_RT_SYM_STATIC_OBJECT_COUNT,
                   TC_RT_SYM_STATIC_OBJECT_COUNT,
                   (unsigned long long)list.count);
    emitter->alloc->free(emitter->alloc->ctx, list.items);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Platform constants, source strings and shadow storage.                    */
/* ------------------------------------------------------------------------- */

static void emit_platform_constants(TcArm32Emitter *emitter) {
    tc_arm32_write(
        emitter,
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

static void emit_static_shadow(TcArm32Emitter *emitter) {
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
    tc_arm32_write(emitter,
                   "\n  .bss\n  .p2align 3\n"
                   "  .globl %s\n%s:\n  .zero %llu\n",
                   TC_RT_SYM_STATIC_SHADOW, TC_RT_SYM_STATIC_SHADOW,
                   (unsigned long long)size);
}

static void emit_source_file_strings(TcArm32Emitter *emitter) {
    size_t i;
    if (emitter->source_file_count == 0) {
        return;
    }
    tc_arm32_write(emitter, "\n  .section .rodata\n");
    for (i = 0; i < emitter->source_file_count; i++) {
        const char *p = emitter->source_files[i] != NULL
                            ? emitter->source_files[i]
                            : "<source>";
        tc_arm32_write(emitter, ".Lsource_file_%zu:\n  .asciz \"", i);
        while (*p != '\0') {
            unsigned char c = (unsigned char)*p++;
            if (c == '"' || c == '\\') {
                tc_arm32_write(emitter, "\\%c", c);
            } else if (c >= 0x20 && c < 0x7f) {
                tc_arm32_write(emitter, "%c", c);
            } else {
                tc_arm32_write(emitter, "\\%03o", (unsigned)c);
            }
        }
        tc_arm32_write(emitter, "\"\n");
    }
}

/* ------------------------------------------------------------------------- */
/* Preflight and entry point.                                                */
/* ------------------------------------------------------------------------- */

static int preflight_ir(TcArm32Emitter *emitter) {
    size_t u;
    size_t b;
    size_t i;
    size_t f;
    if (emitter->ir->profile != NULL &&
        emitter->ir->profile->addr_width != emitter->profile->addr_width) {
        tc_arm32_error(emitter,
                       "IR profile width does not match the emitter profile");
        return 0;
    }
    if (emitter->profile->addr_width != 32u ||
        emitter->profile->target == NULL ||
        emitter->profile->target->addr_width != 32u ||
        !emitter->profile->little_endian) {
        tc_arm32_error(emitter,
                       "the ARM32 backend requires a 32-bit little-endian "
                       "profile");
        return 0;
    }
    if (emitter->profile->target->abi != &tc_abi_aapcs32_ilp32) {
        tc_arm32_error(emitter,
                       "the ARM32 backend requires the AAPCS32/ILP32 ABI");
        return 0;
    }
    if (emitter->profile->disable_icf != 1) {
        tc_arm32_error(emitter, "the profile must disable ICF");
        return 0;
    }
    {
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
                tc_arm32_internal_error(
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
            tc_arm32_error(emitter, "function %u has no body", (unsigned)f);
            return 0;
        }
        for (p = 0; p < func->sig.nparams; p++) {
            const TcType *type = func->sig.params[p].type;
            int ok = 0;
            if (type == NULL ||
                (type->tag != TC_TY_STRUCT &&
                 !tc_arm32_type_is_scalar_value(type))) {
                tc_arm32_error(emitter,
                               "function %u parameter %u has an unsupported "
                               "type",
                               (unsigned)f, (unsigned)p);
                return 0;
            }
            (void)tc_arm32_type_size(emitter->ir, type, &ok);
            if (!ok) {
                tc_arm32_error(emitter,
                               "function %u parameter %u has an incomplete "
                               "type",
                               (unsigned)f, (unsigned)p);
                return 0;
            }
        }
        if (func->sig.ret != NULL && func->sig.ret->tag != TC_TY_VOID) {
            int ok = 0;
            const TcType *ret = func->sig.ret;
            if (ret->tag != TC_TY_STRUCT &&
                !tc_arm32_type_is_scalar_value(ret)) {
                tc_arm32_error(emitter,
                               "function %u returns an unsupported type",
                               (unsigned)f);
                return 0;
            }
            (void)tc_arm32_type_size(emitter->ir, ret, &ok);
            if (!ok) {
                tc_arm32_error(emitter,
                               "function %u returns an incomplete type",
                               (unsigned)f);
                return 0;
            }
        }
    }
    for (u = 0; u < emitter->ir->nunits; u++) {
        const TcUnit *unit = &emitter->ir->units[u];
        size_t v;
        if (unit->nblocks == 0 || unit->entry >= unit->nblocks) {
            tc_arm32_error(emitter, "unit entry block is invalid");
            return 0;
        }
        for (v = 0; v < unit->nvregs; v++) {
            const TcType *type = unit->vreg_types[v];
            if (type == NULL) {
                tc_arm32_error(emitter, "vreg %u has no type", (unsigned)v);
                return 0;
            }
            if (type->tag != TC_TY_STRUCT &&
                !tc_arm32_type_is_scalar_value(type)) {
                tc_arm32_error(emitter,
                               "vreg %u has no supported value type",
                               (unsigned)v);
                return 0;
            }
            if (unit->vreg_objects != NULL && v < unit->nvreg_objects &&
                unit->vreg_objects[v] != UINT32_MAX) {
                uint32_t oid = unit->vreg_objects[v];
                if (oid >= emitter->ir->nobjects) {
                    tc_arm32_error(emitter,
                                   "vreg %u refers to invalid object %u",
                                   (unsigned)v, (unsigned)oid);
                    return 0;
                }
                if (emitter->ir->objects[oid].region != TC_REG_FRAME &&
                    emitter->ir->objects[oid].region != TC_REG_STATIC) {
                    tc_arm32_error(emitter,
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
                tc_arm32_error(emitter,
                               "function %u has fewer vregs than parameters",
                               (unsigned)unit->func_id);
                return 0;
            }
        }
        for (b = 0; b < unit->nblocks; b++) {
            for (i = 0; i < unit->blocks[b].n; i++) {
                const TcInstr *instr = &unit->blocks[b].instrs[i];
                if (instr->op < 0 || instr->op >= TC_OPCODE_COUNT) {
                    tc_arm32_error(emitter, "invalid opcode %d",
                                   (int)instr->op);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int abi_matches_classifier(const TcABIDesc *abi) {
    return abi != NULL && abi->hfa == 0 &&
           abi->composite_max_bytes == 4u && abi->stack_align == 8u &&
           abi->int64_pair == 1 && abi->float64_pair == 1 &&
           abi->composite_return == TC_ABI_RET_INDIRECT;
}

TcArm32EmitStatus tc_arm32_emit_asm(FILE *out, const TcIR *ir,
                                    const TcPlatformProfile *profile,
                                    const TcTargetDesc *target,
                                    char *error_message,
                                    size_t error_capacity) {
    TcArm32Emitter emitter;
    size_t u;
    int saw_top = 0;
    if (out == NULL || ir == NULL || profile == NULL || target == NULL) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity, "missing emitter argument");
        }
        return TC_ARM32_EMIT_INTERNAL;
    }
    if (target != profile->target) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity,
                     "target descriptor does not match the profile");
        }
        return TC_ARM32_EMIT_INTERNAL;
    }
    if (!abi_matches_classifier(target->abi)) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity,
                     "profile ABI descriptor does not match the ARM32 "
                     "classifier");
        }
        return TC_ARM32_EMIT_INTERNAL;
    }
    memset(&emitter, 0, sizeof(emitter));
    emitter.ir = ir;
    emitter.profile = profile;
    emitter.alloc = profile->alloc != NULL ? profile->alloc
                                           : tc_allocator_libc();
    emitter.out = out;
    emitter.error_message = error_message;
    emitter.error_capacity = error_capacity;
    if (!preflight_ir(&emitter)) {
        emitter.alloc->free(emitter.alloc->ctx, emitter.types);
        emitter.alloc->free(emitter.alloc->ctx, emitter.type_ids);
        if (emitter.output_failed || ferror(out)) {
            return TC_ARM32_EMIT_IO;
        }
        if (emitter.oom_failed) {
            return TC_ARM32_EMIT_OOM;
        }
        if (emitter.internal_error) {
            return TC_ARM32_EMIT_INTERNAL;
        }
        return emitter.failed ? TC_ARM32_EMIT_UNSUPPORTED
                              : TC_ARM32_EMIT_INTERNAL;
    }
    tc_arm32_write(&emitter,
                   "  /* TC 0.0.46 ARM32 assembly (profile %s) */\n"
                   "  .syntax unified\n  .cpu cortex-m7\n"
                   "  .fpu fpv5-d16\n  .thumb\n",
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
        tc_arm32_error(&emitter, "program has no top-level unit");
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
        (void)emit_static_region(&emitter);
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
        tc_arm32_write(&emitter,
                       "\n  .section .note.GNU-stack,\"\",%%progbits\n");
    }
    emitter.alloc->free(emitter.alloc->ctx, emitter.types);
    emitter.alloc->free(emitter.alloc->ctx, emitter.type_ids);
    if (emitter.failed) {
        if (emitter.output_failed || ferror(out)) {
            return TC_ARM32_EMIT_IO;
        }
        return emitter.oom_failed ? TC_ARM32_EMIT_OOM
                                  : TC_ARM32_EMIT_INTERNAL;
    }
    if (ferror(out)) {
        return TC_ARM32_EMIT_IO;
    }
    return TC_ARM32_EMIT_OK;
}
