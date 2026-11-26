/* TC 0.0.46 RISC-V 32 assembly emitter: shared services, frame layout and
 * unit emission (design §10/§11).
 *
 * Every value has a stack slot (no regalloc).  Frame objects keep their
 * TcObject offset relative to the locals base s0; static objects keep their
 * in-region offset relative to s1; temporaries follow the object area.  Each
 * function reserves its outgoing argument area at sp and registers a frame
 * descriptor with the runtime so memory helpers apply the mandatory and
 * checked-build object rules.
 */
#include "backend/riscv32/emit_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostic/diagnostic.h"
#include "ir/type_width.h"
#include "runtime/trap.h"

#define TC_RISCV32_ARG_REGS 8u
#define TC_RISCV32_SAVED_AREA 16u

const char *tc_riscv32_reg_name(unsigned reg) {
    static const char *const names[32] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"
    };
    return names[reg & 31u];
}

static unsigned pick_scratch(unsigned a, unsigned b, unsigned c) {
    static const unsigned candidates[6] = {
        TC_RISCV32_TMP0, TC_RISCV32_TMP1, TC_RISCV32_VALUE0,
        TC_RISCV32_VALUE1, TC_RISCV32_VALUE2, TC_RISCV32_VALUE3
    };
    size_t i;
    for (i = 0; i < 6; ++i) {
        if (candidates[i] != a && candidates[i] != b && candidates[i] != c) {
            return candidates[i];
        }
    }
    return TC_RISCV32_TMP0;
}

/* ------------------------------------------------------------------------- */
/* Diagnostics and output.                                                   */
/* ------------------------------------------------------------------------- */

void tc_riscv32_write(TcRiscv32Emitter *emitter, const char *format, ...) {
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

void tc_riscv32_error(TcRiscv32Emitter *emitter, const char *format, ...) {
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

void tc_riscv32_oom(TcRiscv32Emitter *emitter, const char *format, ...) {
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

void tc_riscv32_internal_error(TcRiscv32Emitter *emitter,
                               const char *format, ...) {
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

unsigned tc_riscv32_label(TcRiscv32Emitter *emitter) {
    return emitter->next_label++;
}

/* ------------------------------------------------------------------------- */
/* Types.                                                                    */
/* ------------------------------------------------------------------------- */

unsigned tc_riscv32_type_width(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    return tc_type_bits(type, 32u);
}

uint64_t tc_riscv32_type_size(const TcIR *ir, const TcType *type, int *ok) {
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
        unsigned width = tc_riscv32_type_width(type);
        if (width == 0) {
            return 0;
        }
        if (ok != NULL) {
            *ok = 1;
        }
        return width / 8u;
    }
}

uint64_t tc_riscv32_type_mask(const TcType *type) {
    unsigned width = tc_riscv32_type_width(type);
    if (width == 0) {
        return 0;
    }
    if (width >= 64u) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << width) - 1u;
}

int tc_riscv32_type_is_scalar_value(const TcType *type) {
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

const TcStructDef *tc_riscv32_struct_def(const TcIR *ir, const TcType *type) {
    if (ir == NULL || type == NULL || type->tag != TC_TY_STRUCT) {
        return NULL;
    }
    if (type->struct_.struct_id >= ir->nstructs) {
        return NULL;
    }
    return &ir->structs[type->struct_.struct_id];
}

const TcField *tc_riscv32_field_at(const TcIR *ir, const TcType *type,
                                   uint32_t index) {
    const TcStructDef *def = tc_riscv32_struct_def(ir, type);
    if (def == NULL || index >= def->nfields) {
        return NULL;
    }
    return &def->fields[index];
}

uint32_t tc_riscv32_type_id(TcRiscv32Emitter *emitter, const TcType *type) {
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
            tc_riscv32_oom(emitter,
                           "out of memory while interning type ids");
            return 0;
        }
        emitter->types = types;
        ids = (uint32_t *)emitter->alloc->realloc(emitter->alloc->ctx,  emitter->type_ids,
                                  capacity * sizeof(*ids));
        if (ids == NULL) {
            tc_riscv32_oom(emitter,
                           "out of memory while interning type ids");
            return 0;
        }
        emitter->type_ids = ids;
        emitter->type_capacity = capacity;
    }
    emitter->types[emitter->type_count] = type;
    emitter->type_ids[emitter->type_count] =
        (uint32_t)(emitter->type_count + 1u);
    emitter->type_count++;
    return (uint32_t)emitter->type_count;
}

/* ------------------------------------------------------------------------- */
/* Immediates and register helpers.                                          */
/* ------------------------------------------------------------------------- */

void tc_riscv32_move_immediate(TcRiscv32Emitter *emitter, unsigned reg,
                               uint32_t value) {
    tc_riscv32_write(emitter, "  li %s, 0x%08x\n", tc_riscv32_reg_name(reg),
                     (unsigned)value);
}

void tc_riscv32_load_symbol(TcRiscv32Emitter *emitter, unsigned reg,
                            const char *symbol) {
    tc_riscv32_write(emitter, "  lui %s, %%hi(%s)\n", tc_riscv32_reg_name(reg), symbol);
    tc_riscv32_write(emitter, "  addi %s, %s, %%lo(%s)\n", tc_riscv32_reg_name(reg),
                     tc_riscv32_reg_name(reg), symbol);
}

void tc_riscv32_add_constant(TcRiscv32Emitter *emitter, unsigned dst,
                             unsigned base, uint64_t value) {
    uint32_t offset = (uint32_t)value;
    if (offset == 0) {
        if (dst != base) {
            tc_riscv32_write(emitter, "  mv %s, %s\n", tc_riscv32_reg_name(dst),
                             tc_riscv32_reg_name(base));
        }
        return;
    }
    if (offset <= 2047u) {
        tc_riscv32_write(emitter, "  addi %s, %s, %u\n", tc_riscv32_reg_name(dst),
                         tc_riscv32_reg_name(base), (unsigned)offset);
        return;
    }
    {
        unsigned scratch = pick_scratch(dst, base, 32u);
        tc_riscv32_move_immediate(emitter, scratch, offset);
        tc_riscv32_write(emitter, "  add %s, %s, %s\n", tc_riscv32_reg_name(dst),
                         tc_riscv32_reg_name(base), tc_riscv32_reg_name(scratch));
    }
}

void tc_riscv32_mask_register(TcRiscv32Emitter *emitter, unsigned reg,
                              const TcType *type) {
    unsigned width = tc_riscv32_type_width(type);
    if (width == 0 || width >= 32u) {
        return;
    }
    if (width == 8u) {
        tc_riscv32_write(emitter, "  andi %s, %s, 255\n", tc_riscv32_reg_name(reg),
                         tc_riscv32_reg_name(reg));
        return;
    }
    {
        unsigned scratch = pick_scratch(reg, 32u, 32u);
        tc_riscv32_move_immediate(emitter, scratch,
                                  (uint32_t)tc_riscv32_type_mask(type));
        tc_riscv32_write(emitter, "  and %s, %s, %s\n", tc_riscv32_reg_name(reg),
                         tc_riscv32_reg_name(reg), tc_riscv32_reg_name(scratch));
    }
}

void tc_riscv32_normalize_bool(TcRiscv32Emitter *emitter, unsigned reg) {
    tc_riscv32_write(emitter, "  sltu %s, zero, %s\n", tc_riscv32_reg_name(reg),
                     tc_riscv32_reg_name(reg));
}

void tc_riscv32_sign_extend_narrow(TcRiscv32Emitter *emitter, unsigned reg,
                                   const TcType *type) {
    unsigned width = tc_riscv32_type_width(type);
    if (width == 8u) {
        tc_riscv32_write(emitter, "  slli %s, %s, 24\n", tc_riscv32_reg_name(reg),
                         tc_riscv32_reg_name(reg));
        tc_riscv32_write(emitter, "  srai %s, %s, 24\n", tc_riscv32_reg_name(reg),
                         tc_riscv32_reg_name(reg));
    } else if (width == 16u) {
        tc_riscv32_write(emitter, "  slli %s, %s, 16\n", tc_riscv32_reg_name(reg),
                         tc_riscv32_reg_name(reg));
        tc_riscv32_write(emitter, "  srai %s, %s, 16\n", tc_riscv32_reg_name(reg),
                         tc_riscv32_reg_name(reg));
    }
}

/* ------------------------------------------------------------------------- */
/* Unaligned-safe scalar memory transfer.                                    */
/* ------------------------------------------------------------------------- */

void tc_riscv32_load_bits(TcRiscv32Emitter *emitter, unsigned base_reg,
                          uint64_t offset, unsigned bytes, unsigned dst_reg) {
    unsigned i;
    unsigned scratch;
    if (bytes == 0 || bytes > 4u) {
        tc_riscv32_error(emitter, "internal: load_bits size %u", bytes);
        return;
    }
    if (offset + (uint64_t)bytes - 1u > 2047u) {
        unsigned new_base = pick_scratch(base_reg, dst_reg, 32u);
        tc_riscv32_add_constant(emitter, new_base, base_reg, offset);
        base_reg = new_base;
        offset = 0;
    }
    scratch = pick_scratch(base_reg, dst_reg, 32u);
    tc_riscv32_write(emitter, "  li %s, 0\n", tc_riscv32_reg_name(dst_reg));
    for (i = 0; i < bytes; i++) {
        tc_riscv32_write(emitter, "  lbu %s, %llu(%s)\n", tc_riscv32_reg_name(scratch),
                         (unsigned long long)(offset + (uint64_t)i),
                         tc_riscv32_reg_name(base_reg));
        if (i == 0) {
            tc_riscv32_write(emitter, "  mv %s, %s\n", tc_riscv32_reg_name(dst_reg),
                             tc_riscv32_reg_name(scratch));
        } else {
            tc_riscv32_write(emitter, "  slli %s, %s, %u\n",
                             tc_riscv32_reg_name(scratch), tc_riscv32_reg_name(scratch), 8u * i);
            tc_riscv32_write(emitter, "  or %s, %s, %s\n", tc_riscv32_reg_name(dst_reg),
                             tc_riscv32_reg_name(dst_reg), tc_riscv32_reg_name(scratch));
        }
    }
}

void tc_riscv32_store_bits(TcRiscv32Emitter *emitter, unsigned base_reg,
                           uint64_t offset, unsigned bytes, unsigned src_reg) {
    unsigned i;
    unsigned scratch;
    if (bytes == 0 || bytes > 4u) {
        tc_riscv32_error(emitter, "internal: store_bits size %u", bytes);
        return;
    }
    if (offset + (uint64_t)bytes - 1u > 2047u) {
        unsigned new_base = pick_scratch(base_reg, src_reg, 32u);
        tc_riscv32_add_constant(emitter, new_base, base_reg, offset);
        base_reg = new_base;
        offset = 0;
    }
    scratch = pick_scratch(base_reg, src_reg, 32u);
    tc_riscv32_write(emitter, "  mv %s, %s\n", tc_riscv32_reg_name(scratch),
                     tc_riscv32_reg_name(src_reg));
    for (i = 0; i < bytes; i++) {
        tc_riscv32_write(emitter, "  sb %s, %llu(%s)\n", tc_riscv32_reg_name(scratch),
                         (unsigned long long)(offset + (uint64_t)i),
                         tc_riscv32_reg_name(base_reg));
        if (i + 1u < bytes) {
            tc_riscv32_write(emitter, "  srli %s, %s, 8\n",
                             tc_riscv32_reg_name(scratch), tc_riscv32_reg_name(scratch));
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Trap sites.                                                               */
/* ------------------------------------------------------------------------- */

static size_t source_file_index(TcRiscv32Emitter *emitter, const char *file) {
    size_t index = 0;
    size_t i;
    for (i = 0; i < emitter->source_file_count; i++) {
        if (strcmp(emitter->source_files[i], file) == 0) {
            return i;
        }
    }
    if (emitter->source_file_count >= TC_RISCV32_MAX_SOURCE_FILES) {
        tc_riscv32_error(emitter,
                         "too many source files to emit trap locations");
        return 0;
    }
    emitter->source_files[emitter->source_file_count] = file;
    index = emitter->source_file_count;
    emitter->source_file_count++;
    return index;
}

void tc_riscv32_trap(TcRiscv32Emitter *emitter, const TcSpan *span,
                     TcErrorKind kind) {
    const char *file =
        span != NULL && span->file != NULL ? span->file : "<source>";
    size_t index = source_file_index(emitter, file);
    tc_riscv32_move_immediate(emitter, 10u, (uint32_t)kind);
    tc_riscv32_move_immediate(emitter, 11u, span != NULL ? span->line : 0u);
    tc_riscv32_move_immediate(emitter, 12u, span != NULL ? span->column : 0u);
    {
        char label[64];
        snprintf(label, sizeof(label), ".Lsource_file_%zu", index);
        tc_riscv32_load_symbol(emitter, 13u, label);
    }
    tc_riscv32_write(emitter, "  call %s\n  ebreak\n", TC_RT_SYM_TRAP);
}

/* ------------------------------------------------------------------------- */
/* Slots, operands and memory transfers.                                     */
/* ------------------------------------------------------------------------- */

int tc_riscv32_slot_is_object(const TcRiscv32Slot *slot) {
    return slot->kind == TC_RISCV32_SLOT_FRAME ||
           slot->kind == TC_RISCV32_SLOT_STATIC;
}

void tc_riscv32_slot_address(TcRiscv32Emitter *emitter, uint32_t vreg,
                             unsigned dst_reg) {
    const TcRiscv32Slot *slot;
    if (vreg >= emitter->slot_count) {
        tc_riscv32_error(emitter, "invalid vreg %u", (unsigned)vreg);
        return;
    }
    slot = &emitter->slots[vreg];
    if (slot->kind == TC_RISCV32_SLOT_STATIC) {
        tc_riscv32_load_symbol(emitter, dst_reg, TC_RT_SYM_STATIC_START);
        tc_riscv32_add_constant(emitter, dst_reg, dst_reg, slot->offset);
    } else {
        tc_riscv32_add_constant(emitter, dst_reg, TC_RISCV32_LOCALS,
                                slot->offset);
    }
}

const TcType *tc_riscv32_operand_type(const TcUnit *unit,
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

static const TcType *field_operand_type(TcRiscv32Emitter *emitter,
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
        const TcField *field = tc_riscv32_field_at(emitter->ir, base,
                                                   operand->field.fields[i]);
        if (field == NULL) {
            return NULL;
        }
        base = field->type;
    }
    return base;
}

const TcType *tc_riscv32_value_type(TcRiscv32Emitter *emitter,
                                    const TcOperand *operand) {
    if (operand->kind == TC_OP_FIELD) {
        return field_operand_type(emitter, operand);
    }
    return tc_riscv32_operand_type(emitter->unit, operand);
}

void tc_riscv32_operand_address(TcRiscv32Emitter *emitter,
                                const TcOperand *operand, unsigned dst_reg) {
    if (operand->kind == TC_OP_VREG) {
        tc_riscv32_slot_address(emitter, operand->vreg, dst_reg);
        return;
    }
    if (operand->kind == TC_OP_FIELD) {
        const TcType *base;
        size_t i;
        uint64_t offset = 0;
        if (operand->field.base >= emitter->unit->nvregs) {
            tc_riscv32_error(emitter, "invalid field base vreg");
            return;
        }
        tc_riscv32_slot_address(emitter, operand->field.base, dst_reg);
        base = emitter->unit->vreg_types[operand->field.base];
        for (i = 0; i < operand->field.nfields; i++) {
            const TcField *field = tc_riscv32_field_at(
                emitter->ir, base, operand->field.fields[i]);
            if (field == NULL) {
                tc_riscv32_error(emitter, "invalid field path");
                return;
            }
            offset += field->offset;
            base = field->type;
        }
        if (offset != 0) {
            tc_riscv32_add_constant(emitter, dst_reg, dst_reg, offset);
        }
        return;
    }
    if (operand->kind == TC_OP_CONST) {
        const TcType *type = operand->konst.type;
        unsigned width = tc_riscv32_type_width(type);
        unsigned scratch = pick_scratch(dst_reg, 32u, 32u);
        tc_riscv32_add_constant(emitter, dst_reg, TC_RISCV32_LOCALS,
                                emitter->scratch_offset);
        tc_riscv32_move_immediate(emitter, scratch,
                                  (uint32_t)operand->konst.bits);
        tc_riscv32_store_bits(emitter, dst_reg, 0, 4u, scratch);
        if (width > 32u) {
            tc_riscv32_move_immediate(
                emitter, scratch, (uint32_t)(operand->konst.bits >> 32));
            tc_riscv32_store_bits(emitter, dst_reg, 4, 4u, scratch);
        }
        return;
    }
    tc_riscv32_error(emitter, "operand kind %d has no address",
                     (int)operand->kind);
}

void tc_riscv32_load_operand32(TcRiscv32Emitter *emitter,
                               const TcOperand *operand, unsigned dst_reg) {
    if (operand->kind == TC_OP_CONST) {
        tc_riscv32_move_immediate(emitter, dst_reg,
                                  (uint32_t)operand->konst.bits);
        return;
    }
    tc_riscv32_operand_address(emitter, operand, TC_RISCV32_TMP0);
    {
        unsigned width = tc_riscv32_type_width(
            tc_riscv32_value_type(emitter, operand));
        unsigned bytes = width == 0 ? 4u : (width >= 32u ? 4u : width / 8u);
        tc_riscv32_load_bits(emitter, TC_RISCV32_TMP0, 0, bytes, dst_reg);
    }
}

void tc_riscv32_load_operand64(TcRiscv32Emitter *emitter,
                               const TcOperand *operand, unsigned dst_lo,
                               unsigned dst_hi) {
    const TcType *type = tc_riscv32_value_type(emitter, operand);
    unsigned width = tc_riscv32_type_width(type);
    if (operand->kind == TC_OP_CONST) {
        uint64_t bits = operand->konst.bits;
        tc_riscv32_move_immediate(emitter, dst_lo, (uint32_t)bits);
        if (width > 32u) {
            tc_riscv32_move_immediate(emitter, dst_hi,
                                      (uint32_t)(bits >> 32));
        } else {
            tc_riscv32_write(emitter, "  li %s, 0\n", tc_riscv32_reg_name(dst_hi));
        }
        return;
    }
    tc_riscv32_operand_address(emitter, operand, TC_RISCV32_TMP0);
    if (width > 32u) {
        tc_riscv32_load_bits(emitter, TC_RISCV32_TMP0, 0, 4u, dst_lo);
        tc_riscv32_load_bits(emitter, TC_RISCV32_TMP0, 4, 4u, dst_hi);
    } else {
        unsigned bytes = width == 0 ? 4u : width / 8u;
        tc_riscv32_load_bits(emitter, TC_RISCV32_TMP0, 0, bytes, dst_lo);
        tc_riscv32_write(emitter, "  li %s, 0\n", tc_riscv32_reg_name(dst_hi));
    }
}

static int operand_is_tracked_object(TcRiscv32Emitter *emitter,
                                     const TcOperand *operand) {
    if (operand->kind == TC_OP_FIELD) {
        if (operand->field.base >= emitter->slot_count) {
            return 0;
        }
        return tc_riscv32_slot_is_object(
            &emitter->slots[operand->field.base]);
    }
    if (operand->kind == TC_OP_VREG) {
        if (operand->vreg >= emitter->slot_count) {
            return 0;
        }
        return tc_riscv32_slot_is_object(&emitter->slots[operand->vreg]);
    }
    return 0;
}

static void store_at_operand(TcRiscv32Emitter *emitter,
                             const TcOperand *operand, unsigned src_lo,
                             unsigned src_hi) {
    const TcType *type = tc_riscv32_value_type(emitter, operand);
    unsigned width = tc_riscv32_type_width(type);
    unsigned bytes;
    tc_riscv32_operand_address(emitter, operand, TC_RISCV32_TMP0);
    if (width > 32u) {
        tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 0, 4u, src_lo);
        tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 4, 4u, src_hi);
        bytes = 8u;
    } else {
        bytes = width == 0 ? 4u : width / 8u;
        tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 0, bytes, src_lo);
    }
    if (emitter->profile->checked_build &&
        operand_is_tracked_object(emitter, operand)) {
        tc_riscv32_define_range(emitter, TC_RISCV32_TMP0, bytes);
    }
}

void tc_riscv32_store_result64(TcRiscv32Emitter *emitter,
                               const TcInstr *instr, unsigned src_lo,
                               unsigned src_hi) {
    if (instr->dst.kind == TC_OP_DISCARD) {
        tc_riscv32_add_constant(emitter, TC_RISCV32_TMP0,
                                TC_RISCV32_LOCALS,
                                emitter->scratch_buffer_offset);
        if (tc_riscv32_type_width(instr->type) > 32u) {
            tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 4, 4u, src_hi);
        }
        tc_riscv32_store_bits(emitter, TC_RISCV32_TMP0, 0, 4u, src_lo);
        return;
    }
    store_at_operand(emitter, &instr->dst, src_lo, src_hi);
}

void tc_riscv32_store_operand64(TcRiscv32Emitter *emitter,
                                const TcOperand *operand, unsigned src_lo,
                                unsigned src_hi) {
    store_at_operand(emitter, operand, src_lo, src_hi);
}

void tc_riscv32_copy_bytes(TcRiscv32Emitter *emitter, unsigned src_reg,
                           unsigned dst_reg, uint64_t size) {
    tc_riscv32_write(emitter, "  mv a0, %s\n", tc_riscv32_reg_name(dst_reg));
    tc_riscv32_write(emitter, "  mv a1, %s\n", tc_riscv32_reg_name(src_reg));
    tc_riscv32_move_immediate(emitter, 12u, (uint32_t)size);
    tc_riscv32_write(emitter, "  call memcpy\n");
}

void tc_riscv32_define_range(TcRiscv32Emitter *emitter, unsigned address_reg,
                             uint64_t size) {
    tc_riscv32_write(emitter, "  mv a0, %s\n", tc_riscv32_reg_name(address_reg));
    tc_riscv32_move_immediate(emitter, 11u, (uint32_t)size);
    tc_riscv32_write(emitter, "  call %s\n", TC_RT_SYM_DEFINE);
}

void tc_riscv32_emit_call0(TcRiscv32Emitter *emitter, const char *symbol) {
    tc_riscv32_write(emitter, "  call %s\n", symbol);
}

/* ------------------------------------------------------------------------- */
/* Helper-call argument words.                                               */
/* ------------------------------------------------------------------------- */

void tc_riscv32_arg_immediate(TcRiscv32Emitter *emitter, unsigned word,
                              uint32_t value) {
    if (word < TC_RISCV32_ARG_REGS) {
        tc_riscv32_move_immediate(emitter, 10u + word, value);
    } else {
        tc_riscv32_move_immediate(emitter, TC_RISCV32_TMP0, value);
        tc_riscv32_write(emitter, "  sw %s, %u(sp)\n",
                         tc_riscv32_reg_name(TC_RISCV32_TMP0),
                         (unsigned)((word - TC_RISCV32_ARG_REGS) * 4u));
    }
}

void tc_riscv32_arg_reg(TcRiscv32Emitter *emitter, unsigned word,
                        unsigned reg) {
    if (word < TC_RISCV32_ARG_REGS) {
        tc_riscv32_write(emitter, "  mv %s, %s\n", tc_riscv32_reg_name(10u + word),
                         tc_riscv32_reg_name(reg));
    } else {
        tc_riscv32_write(emitter, "  sw %s, %u(sp)\n", tc_riscv32_reg_name(reg),
                         (unsigned)((word - TC_RISCV32_ARG_REGS) * 4u));
    }
}

void tc_riscv32_arg_operand(TcRiscv32Emitter *emitter, unsigned word,
                            const TcOperand *operand, int high) {
    if (word < TC_RISCV32_ARG_REGS) {
        if (!high) {
            tc_riscv32_load_operand32(emitter, operand, 10u + word);
        } else {
            tc_riscv32_load_operand64(emitter, operand, TC_RISCV32_VALUE0,
                                      TC_RISCV32_VALUE1);
            tc_riscv32_write(emitter, "  mv %s, %s\n",
                             tc_riscv32_reg_name(10u + word),
                             tc_riscv32_reg_name(TC_RISCV32_VALUE1));
        }
        return;
    }
    tc_riscv32_load_operand64(emitter, operand, TC_RISCV32_VALUE0,
                              TC_RISCV32_VALUE1);
    tc_riscv32_write(emitter, "  sw %s, %u(sp)\n",
                     high ? tc_riscv32_reg_name(TC_RISCV32_VALUE1)
                          : tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                     (unsigned)((word - TC_RISCV32_ARG_REGS) * 4u));
}

void tc_riscv32_arg_address(TcRiscv32Emitter *emitter, unsigned word,
                            const TcOperand *operand) {
    tc_riscv32_operand_address(emitter, operand, TC_RISCV32_TMP0);
    tc_riscv32_arg_reg(emitter, word, TC_RISCV32_TMP0);
}

void tc_riscv32_arg_file(TcRiscv32Emitter *emitter, unsigned word,
                         const TcSpan *span) {
    const char *file =
        span != NULL && span->file != NULL ? span->file : "<source>";
    size_t index = source_file_index(emitter, file);
    {
        char label[64];
        snprintf(label, sizeof(label), ".Lsource_file_%zu", index);
        tc_riscv32_load_symbol(emitter, TC_RISCV32_TMP0, label);
    }
    tc_riscv32_arg_reg(emitter, word, TC_RISCV32_TMP0);
}

/* ------------------------------------------------------------------------- */
/* Frame layout.                                                             */
/* ------------------------------------------------------------------------- */

static const TcType *value_type(TcRiscv32Emitter *emitter,
                                const TcOperand *operand) {
    return tc_riscv32_value_type(emitter, operand);
}

void tc_riscv32_function_signature(TcRiscv32Emitter *emitter,
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
            tc_riscv32_error(emitter,
                             "direct call without a function operand");
            return;
        }
        func_id = instr->args[0].value.func;
        if (func_id >= emitter->ir->nfuncs) {
            tc_riscv32_error(emitter, "direct call to an invalid function");
            return;
        }
        callee = &emitter->ir->funcs[func_id];
        *out_count = callee->sig.nparams;
        *out_ret = callee->sig.ret;
        if (*out_count != 0) {
            const TcType **params =
                (const TcType **)tc_allocator_calloc(emitter->alloc, *out_count, sizeof(**out_params));
            if (params == NULL) {
                tc_riscv32_oom(emitter, "out of memory");
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
            tc_riscv32_error(emitter,
                             "call_indirect without a function pointer");
            return;
        }
        signature = value_type(emitter, &instr->args[0].value);
        if (signature == NULL || signature->tag != TC_TY_FNPTR) {
            tc_riscv32_error(emitter,
                             "call_indirect without a function pointer type");
            return;
        }
        *out_count = signature->fnptr.n;
        *out_ret = signature->fnptr.ret;
        if (*out_count != 0) {
            const TcType **params =
                (const TcType **)tc_allocator_calloc(emitter->alloc, *out_count, sizeof(**out_params));
            if (params == NULL) {
                tc_riscv32_oom(emitter, "out of memory");
                return;
            }
            for (i = 0; i < *out_count; i++) {
                params[i] = signature->fnptr.params[i];
            }
            *out_params = params;
        }
        return;
    }
    tc_riscv32_error(emitter, "instruction is not a call");
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

/* Total bytes of by-reference composite arguments for one call. */
static uint64_t call_ref_copy_size(TcRiscv32Emitter *emitter,
                                   const TcType **params, size_t count) {
    uint64_t total = 0;
    size_t i;
    for (i = 0; i < count; ++i) {
        int ok = 0;
        uint64_t size;
        if (params[i] == NULL || params[i]->tag != TC_TY_STRUCT) {
            continue;
        }
        size = tc_riscv32_type_size(emitter->ir, params[i], &ok);
        if (ok && size > 8u) {
            total = align_up(total, 8u) + align_up(size, 8u);
        }
    }
    return total;
}

static int build_layout(TcRiscv32Emitter *emitter, const TcUnit *unit) {
    uint64_t cursor = 0;
    size_t v;
    size_t b;
    size_t i;
    emitter->unit = unit;
    emitter->slot_count = unit->nvregs;
    emitter->slots = (TcRiscv32Slot *)tc_allocator_calloc(emitter->alloc,
        unit->nvregs == 0 ? 1u : unit->nvregs, sizeof(TcRiscv32Slot));
    if (emitter->slots == NULL) {
        tc_riscv32_oom(emitter,
                       "out of memory while laying out %u vregs",
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
            tc_riscv32_error(emitter, "vreg %u has no type", (unsigned)v);
            return 0;
        }
        if (unit->vreg_objects != NULL && v < unit->nvreg_objects) {
            object_id = unit->vreg_objects[v];
        }
        if (object_id != UINT32_MAX) {
            const TcObject *object;
            if (object_id >= emitter->ir->nobjects) {
                tc_riscv32_error(emitter,
                                 "vreg %u refers to invalid object %u",
                                 (unsigned)v, (unsigned)object_id);
                return 0;
            }
            object = &emitter->ir->objects[object_id];
            if (object->region == TC_REG_STATIC) {
                emitter->slots[v].kind = TC_RISCV32_SLOT_STATIC;
            } else if (object->region == TC_REG_FRAME) {
                emitter->slots[v].kind = TC_RISCV32_SLOT_FRAME;
                if (object->offset + object->size >
                    emitter->frame_object_end) {
                    emitter->frame_object_end = object->offset + object->size;
                }
            } else {
                tc_riscv32_error(emitter,
                                 "vreg %u has unsupported object region %d",
                                 (unsigned)v, (int)object->region);
                return 0;
            }
            emitter->slots[v].offset = object->offset;
            emitter->slots[v].size = object->size;
            emitter->slots[v].type = type;
            continue;
        }
        size = tc_riscv32_type_size(emitter->ir, type, &ok);
        if (!ok || size == 0) {
            tc_riscv32_error(emitter, "vreg %u has an incomplete type",
                             (unsigned)v);
            return 0;
        }
        emitter->slots[v].kind = TC_RISCV32_SLOT_TEMP;
        emitter->slots[v].size = size;
        emitter->slots[v].type = type;
    }
    cursor = align_up(emitter->frame_object_end, 8u);
    for (v = 0; v < unit->nvregs; v++) {
        if (emitter->slots[v].kind != TC_RISCV32_SLOT_TEMP) {
            continue;
        }
        cursor = align_up(cursor, 8u);
        emitter->slots[v].offset = cursor;
        cursor += emitter->slots[v].size;
    }
    emitter->scratch_offset = align_up(cursor, 8u);
    cursor = emitter->scratch_offset + 8u;
    emitter->scratch_buffer_size = 0;
    emitter->scratch_buffer_offset = 0;
    emitter->call_area_size = 0;
    for (b = 0; b < unit->nblocks; b++) {
        for (i = 0; i < unit->blocks[b].n; i++) {
            const TcInstr *instr = &unit->blocks[b].instrs[i];
            uint64_t needed = 0;
            int ok = 0;
            if (instr->dst.kind == TC_OP_DISCARD) {
                /* Every discarded result is written to a reserved slot so it
                 * can never alias a live object. */
                needed = 8u;
                if (instr->op == TC_LOAD || instr->op == TC_MAKE ||
                    instr->op == TC_COPY) {
                    uint64_t size =
                        tc_riscv32_type_size(emitter->ir, instr->type, &ok);
                    if (ok && size > needed) {
                        needed = size;
                    }
                } else if (instr->op == TC_CALL ||
                           instr->op == TC_CALL_INDIRECT) {
                    const TcType **params = NULL;
                    size_t nparams = 0;
                    const TcType *ret = NULL;
                    TcRiscv32RetPlan ret_plan;
                    tc_riscv32_function_signature(emitter, instr, &params,
                                                  &nparams, &ret);
                    emitter->alloc->free(emitter->alloc->ctx, params);
                    if (emitter->failed) {
                        return 0;
                    }
                    tc_riscv32_plan_return(emitter, ret, &ret_plan);
                    if (ret_plan.kind == TC_RISCV32_RET_INDIRECT) {
                        uint64_t size = tc_riscv32_type_size(
                            emitter->ir, ret, &ok);
                        if (ok && size > needed) {
                            needed = size;
                        }
                    }
                }
            }
            if (needed > emitter->scratch_buffer_size) {
                emitter->scratch_buffer_size = needed;
            }
        }
    }
    if (emitter->scratch_buffer_size != 0) {
        emitter->scratch_buffer_offset = align_up(cursor, 8u);
        cursor = emitter->scratch_buffer_offset +
                 emitter->scratch_buffer_size;
    }
    emitter->call_area_offset = 0;
    for (b = 0; b < unit->nblocks; b++) {
        for (i = 0; i < unit->blocks[b].n; i++) {
            const TcInstr *instr = &unit->blocks[b].instrs[i];
            uint64_t needed;
            if (instr->op != TC_CALL && instr->op != TC_CALL_INDIRECT) {
                continue;
            }
            {
                const TcType **params = NULL;
                size_t nparams = 0;
                const TcType *ret = NULL;
                tc_riscv32_function_signature(emitter, instr, &params,
                                              &nparams, &ret);
                if (emitter->failed) {
                    emitter->alloc->free(emitter->alloc->ctx, params);
                    return 0;
                }
                needed = call_ref_copy_size(emitter, params, nparams);
                emitter->alloc->free(emitter->alloc->ctx, params);
            }
            if (needed > emitter->call_area_size) {
                emitter->call_area_size = needed;
            }
        }
    }
    if (emitter->call_area_size != 0) {
        emitter->call_area_offset = align_up(cursor, 8u);
        cursor = emitter->call_area_offset + emitter->call_area_size;
    }
    emitter->sret_offset = 0;
    {
        TcRiscv32RetPlan ret;
        const TcType *ret_type = NULL;
        if (unit->func_id != TC_UNIT_TOP_LEVEL &&
            unit->func_id < emitter->ir->nfuncs) {
            ret_type = emitter->ir->funcs[unit->func_id].sig.ret;
        }
        tc_riscv32_plan_return(emitter, ret_type, &ret);
        if (ret.kind == TC_RISCV32_RET_INDIRECT) {
            emitter->sret_offset = align_up(cursor, 4u);
            cursor = emitter->sret_offset + 4u;
        }
    }
    emitter->read_scratch_offset = 0;
    if (emitter->unit_has_read) {
        emitter->read_scratch_offset = align_up(cursor, 8u);
        cursor = emitter->read_scratch_offset + 8u;
    }
    emitter->shadow_offset = 0;
    emitter->shadow_size = 0;
    if (emitter->profile->checked_build) {
        emitter->shadow_size = (emitter->frame_object_end + 7u) / 8u;
        if (emitter->shadow_size == 0) {
            emitter->shadow_size = 1;
        }
        emitter->shadow_offset = align_up(cursor, 8u);
        cursor = emitter->shadow_offset + emitter->shadow_size;
    }
    emitter->frame_desc_count = 0;
    for (v = 0; v < unit->nvregs; v++) {
        if (unit->vreg_objects == NULL || v >= unit->nvreg_objects ||
            unit->vreg_objects[v] == UINT32_MAX) {
            continue;
        }
        if (emitter->slots[v].kind != TC_RISCV32_SLOT_FRAME) {
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
            if (type != NULL && type->tag == TC_TY_STRUCT) {
                if (pending + 1u > capacity) {
                    capacity = 8u;
                    stack = (const TcType **)tc_allocator_calloc(emitter->alloc, capacity,
                                                    sizeof(*stack));
                    if (stack == NULL) {
                        tc_riscv32_oom(emitter, "out of memory");
                        return 0;
                    }
                }
                stack[pending++] = type;
                count = 1;
                while (pending != 0) {
                    const TcType *current = stack[--pending];
                    const TcStructDef *def =
                        tc_riscv32_struct_def(emitter->ir, current);
                    size_t k;
                    if (def == NULL) {
                        emitter->alloc->free(emitter->alloc->ctx, stack);
                        tc_riscv32_error(emitter, "invalid struct type");
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
                                    tc_riscv32_oom(emitter, "out of memory");
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
    emitter->outgoing_size = 16u;
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
                TcRiscv32ArgPlan plan;
                TcRiscv32RetPlan ret_plan;
                tc_riscv32_function_signature(emitter, instr, &params,
                                              &nparams, &ret);
                if (emitter->failed) {
                    emitter->alloc->free(emitter->alloc->ctx, params);
                    return 0;
                }
                tc_riscv32_plan_return(emitter, ret, &ret_plan);
                tc_riscv32_plan_arguments(
                    emitter, params, nparams,
                    ret_plan.kind == TC_RISCV32_RET_INDIRECT ? 1 : 0, &plan);
                emitter->alloc->free(emitter->alloc->ctx, params);
                if (emitter->failed) {
                    tc_riscv32_free_argument_plan(&plan);
                    return 0;
                }
                if (plan.outgoing_size > emitter->outgoing_size) {
                    emitter->outgoing_size = plan.outgoing_size;
                }
                tc_riscv32_free_argument_plan(&plan);
            }
        }
    }
    emitter->outgoing_size = align_up(emitter->outgoing_size, 16u);
    cursor = align_up(cursor, 8u);
    emitter->frame_size = cursor;
    emitter->frame_total = align_up(
        TC_RISCV32_SAVED_AREA + emitter->frame_size + emitter->outgoing_size,
        16u);
    emitter->incoming_stack_offset =
        emitter->frame_total - emitter->outgoing_size -
        TC_RISCV32_SAVED_AREA;
    {
        uint64_t stack_limit = emitter->profile->stack_size;
        uint64_t depth = emitter->profile->depth_D != 0
                             ? emitter->profile->depth_D
                             : 1u;
        uint64_t worst = emitter->frame_total;
        if (stack_limit == 0) {
            stack_limit = emitter->profile->region[TC_REG_FRAME].size;
        }
        if (worst == 0) {
            worst = 16;
        }
        if (worst > stack_limit || (depth + 1u) > UINT64_MAX / worst ||
            worst * (depth + 1u) > stack_limit) {
            tc_riscv32_error(
                emitter,
                "frame size %llu cannot satisfy the profile stack budget",
                (unsigned long long)emitter->frame_total);
            return 0;
        }
    }
    return 1;
}

static void free_layout(TcRiscv32Emitter *emitter) {
    emitter->alloc->free(emitter->alloc->ctx, emitter->slots);
    emitter->slots = NULL;
    emitter->slot_count = 0;
    emitter->unit = NULL;
}

/* ------------------------------------------------------------------------- */
/* Parameter setup.                                                          */
/* ------------------------------------------------------------------------- */

/* Copy `size` bytes from the address in src_reg to s0+slot_offset using only
 * t registers, so incoming argument registers stay live. */
static void copy_from_pointer(TcRiscv32Emitter *emitter, unsigned src_reg,
                              uint64_t slot_offset, uint64_t size) {
    unsigned src = TC_RISCV32_TMP1;
    unsigned dst = TC_RISCV32_TMP0;
    unsigned byte = TC_RISCV32_VALUE0;
    unsigned count = TC_RISCV32_VALUE1;
    unsigned id = tc_riscv32_label(emitter);
    if (size == 0) {
        return;
    }
    /* Compute the destination first: add_constant may use TMP1 as a scratch
     * for a large slot offset, and TMP1 carries the source pointer below. */
    tc_riscv32_add_constant(emitter, dst, TC_RISCV32_LOCALS, slot_offset);
    tc_riscv32_write(emitter, "  mv %s, %s\n", tc_riscv32_reg_name(src),
                     tc_riscv32_reg_name(src_reg));
    tc_riscv32_move_immediate(emitter, count, (uint32_t)size);
    tc_riscv32_write(emitter, ".Lcopy_%u:\n", id);
    tc_riscv32_write(emitter, "  beq %s, zero, .Lcopy_done_%u\n",
                     tc_riscv32_reg_name(count), id);
    tc_riscv32_write(emitter, "  lbu %s, 0(%s)\n", tc_riscv32_reg_name(byte),
                     tc_riscv32_reg_name(src));
    tc_riscv32_write(emitter, "  sb %s, 0(%s)\n", tc_riscv32_reg_name(byte),
                     tc_riscv32_reg_name(dst));
    tc_riscv32_write(emitter, "  addi %s, %s, 1\n", tc_riscv32_reg_name(src),
                     tc_riscv32_reg_name(src));
    tc_riscv32_write(emitter, "  addi %s, %s, 1\n", tc_riscv32_reg_name(dst),
                     tc_riscv32_reg_name(dst));
    tc_riscv32_write(emitter, "  addi %s, %s, -1\n", tc_riscv32_reg_name(count),
                     tc_riscv32_reg_name(count));
    tc_riscv32_write(emitter, "  j .Lcopy_%u\n", id);
    tc_riscv32_write(emitter, ".Lcopy_done_%u:\n", id);
}

static void emit_parameter_setup(TcRiscv32Emitter *emitter,
                                 const TcFunc *func) {
    TcRiscv32ArgPlan plan;
    const TcType **params = NULL;
    size_t nparams = func->sig.nparams;
    size_t i;
    if (nparams != 0) {
        params = (const TcType **)tc_allocator_calloc(emitter->alloc, nparams, sizeof(*params));
        if (params == NULL) {
            tc_riscv32_oom(emitter, "out of memory");
            return;
        }
        for (i = 0; i < nparams; i++) {
            params[i] = func->sig.params[i].type;
        }
    }
    {
        TcRiscv32RetPlan ret_plan;
        tc_riscv32_plan_return(emitter, func->sig.ret, &ret_plan);
        tc_riscv32_plan_arguments(
            emitter, params, nparams,
            ret_plan.kind == TC_RISCV32_RET_INDIRECT ? 1 : 0, &plan);
    }
    emitter->alloc->free(emitter->alloc->ctx, params);
    if (emitter->failed) {
        return;
    }
    for (i = 0; i < nparams; i++) {
        const TcType *type = func->sig.params[i].type;
        const TcRiscv32ArgSlot *loc = &plan.slots[i];
        uint64_t slot_offset = emitter->slots[i].offset;
        unsigned width = tc_riscv32_type_width(type);
        int ok = 0;
        uint64_t size = tc_riscv32_type_size(emitter->ir, type, &ok);
        if (!ok) {
            tc_riscv32_error(emitter,
                             "parameter %u has an incomplete type",
                             (unsigned)i);
            tc_riscv32_free_argument_plan(&plan);
            return;
        }
        if (loc->by_reference) {
            unsigned source = TC_RISCV32_VALUE0;
            if (loc->kind == TC_RISCV32_ARG_REG) {
                tc_riscv32_write(emitter, "  mv %s, %s\n",
                                 tc_riscv32_reg_name(source),
                                 tc_riscv32_reg_name(10u + loc->reg));
            } else {
                tc_riscv32_add_constant(
                    emitter, source, TC_RISCV32_LOCALS,
                    emitter->incoming_stack_offset + loc->stack_offset);
                tc_riscv32_write(emitter, "  lw %s, 0(%s)\n",
                                 tc_riscv32_reg_name(source), tc_riscv32_reg_name(source));
            }
            copy_from_pointer(emitter, source, slot_offset, size);
            continue;
        }
        if (loc->struct_value) {
            unsigned k;
            for (k = 0; k < loc->reg_words; k++) {
                uint64_t offset = (uint64_t)k * 4u;
                uint64_t remaining = size - offset;
                unsigned bytes = remaining >= 4u ? 4u
                                                 : (unsigned)remaining;
                tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                      slot_offset + offset, bytes,
                                      10u + loc->reg + k);
            }
            if (loc->stack_words != 0) {
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
                    tc_riscv32_add_constant(
                        emitter, TC_RISCV32_TMP0, TC_RISCV32_LOCALS,
                        emitter->incoming_stack_offset +
                            loc->stack_offset + source_offset);
                    tc_riscv32_write(emitter, "  lw %s, 0(%s)\n",
                                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
                    tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                          slot_offset + destination_offset,
                                          bytes, TC_RISCV32_VALUE0);
                }
            }
            continue;
        }
        if (loc->kind == TC_RISCV32_ARG_REG) {
            if (loc->is64) {
                if (loc->split64) {
                    tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                          slot_offset, 4u,
                                          10u + loc->reg);
                    tc_riscv32_add_constant(
                        emitter, TC_RISCV32_TMP0, TC_RISCV32_LOCALS,
                        emitter->incoming_stack_offset +
                            loc->stack_offset);
                    tc_riscv32_write(emitter, "  lw %s, 0(%s)\n",
                                     tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
                    tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                          slot_offset + 4u, 4u,
                                          TC_RISCV32_VALUE0);
                } else {
                    tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                          slot_offset, 4u,
                                          10u + loc->reg);
                    tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                          slot_offset + 4u, 4u,
                                          10u + loc->reg + 1u);
                }
            } else {
                unsigned bytes = width == 0 ? 4u : width / 8u;
                tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                      slot_offset, bytes,
                                      10u + loc->reg);
            }
            continue;
        }
        tc_riscv32_add_constant(emitter, TC_RISCV32_TMP0,
                                TC_RISCV32_LOCALS,
                                emitter->incoming_stack_offset +
                                    loc->stack_offset);
        if (loc->is64) {
            tc_riscv32_write(emitter, "  lw %s, 0(%s)\n",
                             tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                             tc_riscv32_reg_name(TC_RISCV32_TMP0));
            tc_riscv32_write(emitter, "  lw %s, 4(%s)\n",
                             tc_riscv32_reg_name(TC_RISCV32_VALUE1),
                             tc_riscv32_reg_name(TC_RISCV32_TMP0));
            tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS, slot_offset,
                                  4u, TC_RISCV32_VALUE0);
            tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                  slot_offset + 4u, 4u,
                                  TC_RISCV32_VALUE1);
        } else {
            tc_riscv32_write(emitter, "  lw %s, 0(%s)\n",
                             tc_riscv32_reg_name(TC_RISCV32_VALUE0),
                             tc_riscv32_reg_name(TC_RISCV32_TMP0));
            {
                unsigned bytes = width == 0 ? 4u : width / 8u;
                tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                                      slot_offset, bytes,
                                      TC_RISCV32_VALUE0);
            }
        }
    }
    tc_riscv32_free_argument_plan(&plan);
}

/* ------------------------------------------------------------------------- */
/* Unit emission.                                                            */
/* ------------------------------------------------------------------------- */

static void emit_epilogue(TcRiscv32Emitter *emitter, size_t unit_index) {
    uint64_t total = emitter->frame_total;
    uint64_t ret_save = emitter->scratch_offset;
    tc_riscv32_write(emitter, ".Lreturn_%zu:\n", unit_index);
    /* tc_rt_frame_leave is an ordinary C function and may clobber the
     * caller-saved return registers, so preserve a0/a1 across the call. */
    tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS, ret_save, 4u, 10u);
    tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS, ret_save + 4u, 4u,
                          11u);
    tc_riscv32_write(emitter, "  call %s\n", TC_RT_SYM_FRAME_LEAVE);
    tc_riscv32_load_bits(emitter, TC_RISCV32_LOCALS, ret_save, 4u, 10u);
    tc_riscv32_load_bits(emitter, TC_RISCV32_LOCALS, ret_save + 4u, 4u,
                         11u);
    tc_riscv32_write(emitter, "  lw ra, -4(s0)\n");
    tc_riscv32_write(emitter, "  lw %s, -8(s0)\n",
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_write(emitter, "  lw s1, -12(s0)\n");
    tc_riscv32_move_immediate(emitter, TC_RISCV32_TMP1, (uint32_t)total);
    tc_riscv32_write(emitter, "  add sp, sp, %s\n",
                     tc_riscv32_reg_name(TC_RISCV32_TMP1));
    tc_riscv32_write(emitter, "  mv s0, %s\n",
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_write(emitter, "  ret\n");
}

static void emit_unit(TcRiscv32Emitter *emitter, const TcUnit *unit,
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
        symbol = TC_RISCV32_TOP_LEVEL_SYMBOL;
    } else {
        if (unit->func_id >= emitter->ir->nfuncs) {
            tc_riscv32_error(emitter, "unit refers to invalid function %u",
                             (unsigned)unit->func_id);
            free_layout(emitter);
            return;
        }
        func = &emitter->ir->funcs[unit->func_id];
        symbol = NULL;
    }
    tc_riscv32_write(emitter, "\n  .text\n  .p2align 2\n");
    if (symbol != NULL) {
        tc_riscv32_write(emitter,
                         "  .globl %s\n"
                         "  .type %s, @function\n%s:\n",
                         symbol, symbol, symbol);
    } else {
        tc_riscv32_write(emitter,
                         "  .type tc_function_%u, @function\n"
                         "tc_function_%u:\n",
                         (unsigned)unit->func_id, (unsigned)unit->func_id);
    }
    tc_riscv32_move_immediate(emitter, TC_RISCV32_TMP0,
                              (uint32_t)emitter->frame_total);
    tc_riscv32_write(emitter, "  sub sp, sp, %s\n",
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
    /* Save the caller's s0/s1 through the outgoing area before constructing
     * the new locals base; the area is overwritten by real outgoing arguments
     * only after the prologue. */
    tc_riscv32_write(emitter, "  sw s0, 8(sp)\n  sw s1, 12(sp)\n");
    tc_riscv32_move_immediate(emitter, TC_RISCV32_TMP0,
                              (uint32_t)(emitter->outgoing_size +
                                         TC_RISCV32_SAVED_AREA));
    tc_riscv32_write(emitter, "  add s0, sp, %s\n",
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_write(emitter, "  sw ra, -4(s0)\n");
    tc_riscv32_write(emitter, "  lw %s, 8(sp)\n", tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_write(emitter, "  sw %s, -8(s0)\n",
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_write(emitter, "  lw %s, 12(sp)\n", tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_write(emitter, "  sw %s, -12(s0)\n",
                     tc_riscv32_reg_name(TC_RISCV32_TMP0));
    tc_riscv32_load_symbol(emitter, TC_RISCV32_STATIC,
                           TC_RT_SYM_STATIC_START);
    if (emitter->sret_offset != 0) {
        tc_riscv32_store_bits(emitter, TC_RISCV32_LOCALS,
                              emitter->sret_offset, 4u, 10u);
    }
    if (func != NULL) {
        emit_parameter_setup(emitter, func);
        if (emitter->failed) {
            free_layout(emitter);
            return;
        }
    }
    if (unit->func_id == TC_UNIT_TOP_LEVEL) {
        tc_riscv32_emit_call0(emitter, TC_RT_SYM_PROGRAM_INIT);
    }
    {
        char label[64];
        snprintf(label, sizeof(label), ".Lunit_objs_%zu", unit_index);
        tc_riscv32_load_symbol(emitter, 10u, label);
    }
    tc_riscv32_move_immediate(emitter, 11u,
                              (uint32_t)emitter->frame_desc_count);
    tc_riscv32_write(emitter, "  mv a2, s0\n");
    tc_riscv32_move_immediate(emitter, 13u, (uint32_t)emitter->frame_size);
    if (emitter->profile->checked_build) {
        tc_riscv32_add_constant(emitter, TC_RISCV32_TMP0,
                                TC_RISCV32_LOCALS, emitter->shadow_offset);
        tc_riscv32_write(emitter, "  mv a4, %s\n",
                         tc_riscv32_reg_name(TC_RISCV32_TMP0));
        tc_riscv32_move_immediate(emitter, 15u,
                                  (uint32_t)emitter->shadow_size);
    } else {
        tc_riscv32_write(emitter, "  li a4, 0\n  li a5, 0\n");
    }
    tc_riscv32_emit_call0(emitter, TC_RT_SYM_FRAME_ENTER);
    if (emitter->profile->checked_build && func != NULL) {
        size_t p;
        for (p = 0; p < func->sig.nparams; p++) {
            int okp = 0;
            uint64_t psize = tc_riscv32_type_size(
                emitter->ir, func->sig.params[p].type, &okp);
            if (!okp) {
                continue;
            }
            tc_riscv32_add_constant(emitter, 10u, TC_RISCV32_LOCALS,
                                    emitter->slots[p].offset);
            tc_riscv32_define_range(emitter, 10u, psize);
        }
    }
    for (b = 0; b < unit->nblocks; b++) {
        tc_riscv32_write(emitter, ".L%zu_block%zu:\n", unit_index, b);
        for (i = 0; i < unit->blocks[b].n; i++) {
            tc_riscv32_emit_instruction(emitter,
                                        &unit->blocks[b].instrs[i]);
            if (emitter->failed) {
                free_layout(emitter);
                return;
            }
        }
    }
    emit_epilogue(emitter, unit_index);
    if (func == NULL) {
        tc_riscv32_write(emitter, "\n  .size %s, .-%s\n",
                         TC_RISCV32_TOP_LEVEL_SYMBOL,
                         TC_RISCV32_TOP_LEVEL_SYMBOL);
    } else {
        tc_riscv32_write(emitter,
                         "\n  .size tc_function_%u, .-tc_function_%u\n",
                         (unsigned)unit->func_id,
                         (unsigned)unit->func_id);
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
} TcRiscv32DescList;

static int desc_push(TcRiscv32Emitter *emitter, TcRiscv32DescList *list,
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

static int add_field_descs(TcRiscv32Emitter *emitter,
                           TcRiscv32DescList *list, uint64_t offset,
                           const TcType *type, int writable) {
    const TcStructDef *def = tc_riscv32_struct_def(emitter->ir, type);
    size_t i;
    if (def == NULL) {
        return type == NULL || type->tag != TC_TY_STRUCT;
    }
    for (i = 0; i < def->nfields; i++) {
        const TcField *field = &def->fields[i];
        int ok = 0;
        uint64_t size = tc_riscv32_type_size(emitter->ir, field->type, &ok);
        TcRtObjectDesc desc;
        if (!ok) {
            tc_riscv32_error(emitter, "field %u has an incomplete type",
                             (unsigned)i);
            return 0;
        }
        desc.offset = offset + field->offset;
        desc.size = size;
        desc.type_id = tc_riscv32_type_id(emitter, field->type);
        desc.flags = writable ? TC_RT_OBJECT_WRITABLE : 0;
        if (!desc_push(emitter, list, desc)) {
            tc_riscv32_oom(emitter,
                           "out of memory while building descriptors");
            return 0;
        }
        if (!add_field_descs(emitter, list, desc.offset, field->type,
                             writable)) {
            return 0;
        }
    }
    return 1;
}

static void emit_unit_descriptors(TcRiscv32Emitter *emitter,
                                  const TcUnit *unit, size_t unit_index) {
    TcRiscv32DescList list;
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
        desc.type_id = tc_riscv32_type_id(emitter, object->type);
        desc.flags = object->writable ? TC_RT_OBJECT_WRITABLE : 0;
        if (!desc_push(emitter, &list, desc)) {
            tc_riscv32_oom(emitter,
                           "out of memory while building descriptors");
            break;
        }
        if (!add_field_descs(emitter, &list, object->offset, object->type,
                             object->writable)) {
            break;
        }
    }
    tc_riscv32_write(emitter, "\n  .section .rodata\n  .p2align 3\n"
                              ".Lunit_objs_%zu:\n",
                     unit_index);
    if (list.count == 0) {
        tc_riscv32_write(emitter,
                         "  .quad 0\n  .quad 0\n  .long 0\n  .long 0\n");
    } else {
        size_t i;
        for (i = 0; i < list.count; i++) {
            tc_riscv32_write(emitter,
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

static int emit_static_region(TcRiscv32Emitter *emitter) {
    uint64_t size = 0;
    uint8_t *bytes = NULL;
    size_t i;
    uint64_t pos;
    TcRiscv32DescList list;
    for (i = 0; i < emitter->ir->nobjects; i++) {
        const TcObject *object = &emitter->ir->objects[i];
        if (object->region != TC_REG_STATIC) {
            continue;
        }
        if (object->offset > UINT64_MAX - object->size) {
            tc_riscv32_error(emitter, "static object offset overflows");
            return 0;
        }
        if (object->offset + object->size > size) {
            size = object->offset + object->size;
        }
    }
    if (size > (uint64_t)SIZE_MAX) {
        tc_riscv32_error(emitter, "static data is too large");
        return 0;
    }
    bytes = (uint8_t *)tc_allocator_calloc(emitter->alloc, (size_t)(size == 0 ? 1u : size), 1u);
    if (bytes == NULL) {
        tc_riscv32_oom(emitter,
                       "out of memory while building static data");
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
            tc_riscv32_error(emitter, "static object initializer overflows");
            return 0;
        }
        if (object->init->size != 0) {
            if (object->init->bytes == NULL) {
                emitter->alloc->free(emitter->alloc->ctx, bytes);
                tc_riscv32_error(emitter,
                                 "static initializer has no bytes");
                return 0;
            }
            memcpy(bytes + object->offset, object->init->bytes,
                   object->init->size);
        }
    }
    tc_riscv32_write(emitter, "\n  .data\n  .p2align 3\n"
                              "  .globl %s\n"
                              "  .type %s, @object\n%s:\n",
                     TC_RT_SYM_STATIC_START, TC_RT_SYM_STATIC_START,
                     TC_RT_SYM_STATIC_START);
    pos = 0;
    while (pos < size) {
        uint64_t run = 0;
        while (pos + run < size && bytes[pos + run] == 0) {
            run++;
        }
        if (run >= 8u) {
            tc_riscv32_write(emitter, "  .zero %llu\n",
                             (unsigned long long)run);
            pos += run;
            continue;
        }
        tc_riscv32_write(emitter, "  .byte 0x%02x\n", (unsigned)bytes[pos]);
        pos++;
    }
    if (size == 0) {
        tc_riscv32_write(emitter, "  .byte 0\n");
    }
    tc_riscv32_write(emitter, "  .globl %s\n%s:\n",
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
        desc.type_id = tc_riscv32_type_id(emitter, object->type);
        desc.flags = (object->writable ? TC_RT_OBJECT_WRITABLE : 0) |
                     (object->init != NULL ? TC_RT_OBJECT_HAS_INIT : 0);
        if (!desc_push(emitter, &list, desc)) {
            tc_riscv32_oom(emitter, "out of memory");
            emitter->alloc->free(emitter->alloc->ctx, list.items);
            return 0;
        }
        if (!add_field_descs(emitter, &list, object->offset, object->type,
                             object->writable)) {
            emitter->alloc->free(emitter->alloc->ctx, list.items);
            return 0;
        }
    }
    tc_riscv32_write(emitter, "  .section .rodata\n  .p2align 3\n"
                              "  .globl %s\n%s:\n",
                     TC_RT_SYM_STATIC_OBJECTS, TC_RT_SYM_STATIC_OBJECTS);
    if (list.count == 0) {
        tc_riscv32_write(emitter,
                         "  .quad 0\n  .quad 0\n  .long 0\n  .long 0\n");
    } else {
        for (i = 0; i < list.count; i++) {
            tc_riscv32_write(emitter,
                             "  .quad %llu\n  .quad %llu\n  .long %u\n"
                             "  .long %u\n",
                             (unsigned long long)list.items[i].offset,
                             (unsigned long long)list.items[i].size,
                             (unsigned)list.items[i].type_id,
                             (unsigned)list.items[i].flags);
        }
    }
    tc_riscv32_write(emitter,
                     "  .p2align 3\n  .globl %s\n%s:\n  .quad %llu\n",
                     TC_RT_SYM_STATIC_OBJECT_COUNT,
                     TC_RT_SYM_STATIC_OBJECT_COUNT,
                     (unsigned long long)list.count);
    emitter->alloc->free(emitter->alloc->ctx, list.items);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Platform constants, source strings and shadow storage.                    */
/* ------------------------------------------------------------------------- */

static void emit_platform_constants(TcRiscv32Emitter *emitter) {
    tc_riscv32_write(
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

static void emit_static_shadow(TcRiscv32Emitter *emitter) {
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
    tc_riscv32_write(emitter,
                     "\n  .bss\n  .p2align 3\n"
                     "  .globl %s\n%s:\n  .zero %llu\n",
                     TC_RT_SYM_STATIC_SHADOW, TC_RT_SYM_STATIC_SHADOW,
                     (unsigned long long)size);
}

static void emit_source_file_strings(TcRiscv32Emitter *emitter) {
    size_t i;
    if (emitter->source_file_count == 0) {
        return;
    }
    tc_riscv32_write(emitter, "\n  .section .rodata\n");
    for (i = 0; i < emitter->source_file_count; i++) {
        const char *p = emitter->source_files[i] != NULL
                            ? emitter->source_files[i]
                            : "<source>";
        tc_riscv32_write(emitter, ".Lsource_file_%zu:\n  .asciz \"", i);
        while (*p != '\0') {
            unsigned char c = (unsigned char)*p++;
            if (c == '"' || c == '\\') {
                tc_riscv32_write(emitter, "\\%c", c);
            } else if (c >= 0x20 && c < 0x7f) {
                tc_riscv32_write(emitter, "%c", c);
            } else {
                tc_riscv32_write(emitter, "\\%03o", (unsigned)c);
            }
        }
        tc_riscv32_write(emitter, "\"\n");
    }
}

/* ------------------------------------------------------------------------- */
/* Preflight and entry point.                                                */
/* ------------------------------------------------------------------------- */

static int preflight_ir(TcRiscv32Emitter *emitter) {
    size_t u;
    size_t b;
    size_t i;
    size_t f;
    if (emitter->ir->profile != NULL &&
        emitter->ir->profile->addr_width != emitter->profile->addr_width) {
        tc_riscv32_error(
            emitter,
            "IR profile width does not match the emitter profile");
        return 0;
    }
    if (emitter->profile->addr_width != 32u ||
        emitter->profile->target == NULL ||
        emitter->profile->target->addr_width != 32u ||
        !emitter->profile->little_endian) {
        tc_riscv32_error(emitter,
                         "the RISC-V 32 backend requires a 32-bit "
                         "little-endian profile");
        return 0;
    }
    if (emitter->profile->target->abi != &tc_abi_ilp32_riscv) {
        tc_riscv32_error(emitter,
                         "the RISC-V 32 backend requires the ILP32 ABI");
        return 0;
    }
    if (emitter->profile->disable_icf != 1) {
        tc_riscv32_error(emitter, "the profile must disable ICF");
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
                tc_riscv32_internal_error(
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
            tc_riscv32_error(emitter, "function %u has no body",
                             (unsigned)f);
            return 0;
        }
        for (p = 0; p < func->sig.nparams; p++) {
            const TcType *type = func->sig.params[p].type;
            int ok = 0;
            if (type == NULL ||
                (type->tag != TC_TY_STRUCT &&
                 !tc_riscv32_type_is_scalar_value(type))) {
                tc_riscv32_error(emitter,
                                 "function %u parameter %u has an "
                                 "unsupported type",
                                 (unsigned)f, (unsigned)p);
                return 0;
            }
            (void)tc_riscv32_type_size(emitter->ir, type, &ok);
            if (!ok) {
                tc_riscv32_error(emitter,
                                 "function %u parameter %u has an "
                                 "incomplete type",
                                 (unsigned)f, (unsigned)p);
                return 0;
            }
        }
        if (func->sig.ret != NULL && func->sig.ret->tag != TC_TY_VOID) {
            int ok = 0;
            const TcType *ret = func->sig.ret;
            if (ret->tag != TC_TY_STRUCT &&
                !tc_riscv32_type_is_scalar_value(ret)) {
                tc_riscv32_error(emitter,
                                 "function %u returns an unsupported type",
                                 (unsigned)f);
                return 0;
            }
            (void)tc_riscv32_type_size(emitter->ir, ret, &ok);
            if (!ok) {
                tc_riscv32_error(emitter,
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
            tc_riscv32_error(emitter, "unit entry block is invalid");
            return 0;
        }
        for (v = 0; v < unit->nvregs; v++) {
            const TcType *type = unit->vreg_types[v];
            if (type == NULL) {
                tc_riscv32_error(emitter, "vreg %u has no type",
                                 (unsigned)v);
                return 0;
            }
            if (type->tag != TC_TY_STRUCT &&
                !tc_riscv32_type_is_scalar_value(type)) {
                tc_riscv32_error(emitter,
                                 "vreg %u has no supported value type",
                                 (unsigned)v);
                return 0;
            }
            if (unit->vreg_objects != NULL && v < unit->nvreg_objects &&
                unit->vreg_objects[v] != UINT32_MAX) {
                uint32_t oid = unit->vreg_objects[v];
                if (oid >= emitter->ir->nobjects) {
                    tc_riscv32_error(emitter,
                                     "vreg %u refers to invalid object %u",
                                     (unsigned)v, (unsigned)oid);
                    return 0;
                }
                if (emitter->ir->objects[oid].region != TC_REG_FRAME &&
                    emitter->ir->objects[oid].region != TC_REG_STATIC) {
                    tc_riscv32_error(
                        emitter,
                        "vreg %u has an unsupported object region",
                        (unsigned)v);
                    return 0;
                }
            }
        }
        if (unit->func_id != TC_UNIT_TOP_LEVEL &&
            unit->func_id < emitter->ir->nfuncs) {
            size_t nparams =
                emitter->ir->funcs[unit->func_id].sig.nparams;
            if (unit->nvregs < nparams) {
                tc_riscv32_error(
                    emitter,
                    "function %u has fewer vregs than parameters",
                    (unsigned)unit->func_id);
                return 0;
            }
        }
        for (b = 0; b < unit->nblocks; b++) {
            for (i = 0; i < unit->blocks[b].n; i++) {
                const TcInstr *instr = &unit->blocks[b].instrs[i];
                if (instr->op < 0 || instr->op >= TC_OPCODE_COUNT) {
                    tc_riscv32_error(emitter, "invalid opcode %d",
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
           abi->composite_max_bytes == 8u && abi->stack_align == 16u &&
           abi->int64_pair == 1 && abi->float64_pair == 1 &&
           abi->composite_return == TC_ABI_RET_INDIRECT;
}

TcRiscv32EmitStatus tc_riscv32_emit_asm(FILE *out, const TcIR *ir,
                                        const TcPlatformProfile *profile,
                                        const TcTargetDesc *target,
                                        char *error_message,
                                        size_t error_capacity) {
    TcRiscv32Emitter emitter;
    size_t u;
    int saw_top = 0;
    if (out == NULL || ir == NULL || profile == NULL || target == NULL) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity,
                     "missing emitter argument");
        }
        return TC_RISCV32_EMIT_INTERNAL;
    }
    if (target != profile->target) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity,
                     "target descriptor does not match the profile");
        }
        return TC_RISCV32_EMIT_INTERNAL;
    }
    if (!abi_matches_classifier(target->abi)) {
        if (error_message != NULL && error_capacity != 0) {
            snprintf(error_message, error_capacity,
                     "profile ABI descriptor does not match the RISC-V 32 "
                     "classifier");
        }
        return TC_RISCV32_EMIT_INTERNAL;
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
            return TC_RISCV32_EMIT_IO;
        }
        if (emitter.oom_failed) {
            return TC_RISCV32_EMIT_OOM;
        }
        if (emitter.internal_error) {
            return TC_RISCV32_EMIT_INTERNAL;
        }
        return emitter.failed ? TC_RISCV32_EMIT_UNSUPPORTED
                              : TC_RISCV32_EMIT_INTERNAL;
    }
    tc_riscv32_write(&emitter,
                     "  /* TC 0.0.46 RISC-V 32 assembly (profile %s) */\n"
                     "  .option norelax\n",
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
        tc_riscv32_error(&emitter, "program has no top-level unit");
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
        tc_riscv32_write(&emitter,
                         "\n  .section .note.GNU-stack,\"\",@progbits\n");
    }
    emitter.alloc->free(emitter.alloc->ctx, emitter.types);
    emitter.alloc->free(emitter.alloc->ctx, emitter.type_ids);
    if (emitter.failed) {
        if (emitter.output_failed || ferror(out)) {
            return TC_RISCV32_EMIT_IO;
        }
        return emitter.oom_failed ? TC_RISCV32_EMIT_OOM
                                  : TC_RISCV32_EMIT_INTERNAL;
    }
    if (ferror(out)) {
        return TC_RISCV32_EMIT_IO;
    }
    return TC_RISCV32_EMIT_OK;
}
