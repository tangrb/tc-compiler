/* TC 0.0.46 ARM32 emitter internals (design §10).
 *
 * Values live in stack slots (no register allocator).  A vreg that names a
 * frame/static storage object lives at its IR object offset; every other vreg
 * gets a fresh slot.  Structured values occupy their full `sizeof` bytes;
 * scalars occupy eight bytes holding the 64-bit bit pattern.  Each function
 * keeps two bases in callee-saved registers: r9 = locals base (above the
 * outgoing argument area) and r10 = static region base.
 *
 * These declarations are shared by the emitter translation units only and are
 * not part of the compiler's external contract.
 */
#ifndef TC_BACKEND_ARM32_EMIT_INTERNAL_H
#define TC_BACKEND_ARM32_EMIT_INTERNAL_H

#include <stddef.h>
#include <stdio.h>

#include "backend/arm32/emit.h"
#include "backend/common/rt_abi.h"
#include "ir/ir.h"
#include "runtime/profile.h"

#define TC_ARM32_MAX_SOURCE_FILES 256u
#define TC_ARM32_MAX_PARAMS 64u

/* Emitter scratch registers; both are callee-saved and pushed by every
 * function, so generated sequences may use them freely between helper calls. */
#define TC_ARM32_TMP0 12u
#define TC_ARM32_TMP1 11u
#define TC_ARM32_VALUE0 8u
#define TC_ARM32_VALUE1 7u

typedef enum {
    TC_ARM32_SLOT_FRAME = 0,
    TC_ARM32_SLOT_STATIC,
    TC_ARM32_SLOT_TEMP
} TcArm32SlotKind;

typedef struct {
    TcArm32SlotKind kind;
    uint64_t offset;
    uint64_t size;
    const TcType *type;
} TcArm32Slot;

typedef struct TcArm32Emitter {
    const TcIR *ir;
    const TcPlatformProfile *profile;
    const TcAllocator *alloc;
    FILE *out;
    char *error_message;
    size_t error_capacity;
    int failed;
    int internal_error;
    int oom_failed;
    int output_failed;
    unsigned next_label;

    const char *source_files[TC_ARM32_MAX_SOURCE_FILES];
    size_t source_file_count;

    const TcType **types;
    uint32_t *type_ids;
    size_t type_count;
    size_t type_capacity;

    const TcUnit *unit;
    size_t unit_index;
    TcArm32Slot *slots;
    size_t slot_count;
    uint64_t frame_size;
    uint64_t frame_object_end;
    uint64_t outgoing_size;
    uint64_t scratch_offset;
    uint64_t scratch_buffer_offset;
    uint64_t scratch_buffer_size;
    uint64_t sret_offset;
    uint64_t read_scratch_offset;
    uint64_t shadow_offset;
    uint64_t shadow_size;
    uint64_t incoming_stack_offset;
    int unit_has_read;
    uint64_t frame_desc_count;
} TcArm32Emitter;

typedef enum {
    TC_ARM32_ARG_REG = 0,
    TC_ARM32_ARG_STACK
} TcArm32ArgKind;

typedef struct {
    TcArm32ArgKind kind;
    unsigned reg;         /* register number for TC_ARM32_ARG_REG */
    uint64_t stack_offset;/* NSAA offset for TC_ARM32_ARG_STACK */
    uint64_t size;        /* value size in bytes */
    unsigned words;       /* register/stack words occupied (1 or 2) */
    int is64;             /* 64-bit fundamental (two words, even-aligned) */
    int struct_value;     /* composite passed by value as words */
    unsigned reg_words;   /* composite words placed in registers */
    unsigned stack_words; /* composite words placed on the stack */
    uint64_t stack_byte_offset; /* first composite byte placed on the stack */
} TcArm32ArgSlot;

typedef struct {
    const TcAllocator *alloc;
    TcArm32ArgSlot *slots;
    size_t count;
    uint64_t stack_bytes;
    uint64_t outgoing_size; /* stack arguments plus caller copies, 8-aligned */
    int sret;
} TcArm32ArgPlan;

typedef enum {
    TC_ARM32_RET_VOID = 0,
    TC_ARM32_RET_WORD,     /* scalar/composite of at most four bytes in r0 */
    TC_ARM32_RET_PAIR,     /* 64-bit scalar in r0:r1 */
    TC_ARM32_RET_INDIRECT  /* hidden result pointer in r0 */
} TcArm32RetKind;

typedef struct {
    TcArm32RetKind kind;
    uint64_t size;
} TcArm32RetPlan;

/* --- emit.c: shared services ------------------------------------------- */

void tc_arm32_write(TcArm32Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_arm32_error(TcArm32Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_arm32_oom(TcArm32Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_arm32_internal_error(TcArm32Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
unsigned tc_arm32_label(TcArm32Emitter *emitter);

uint64_t tc_arm32_type_size(const TcIR *ir, const TcType *type, int *ok);
unsigned tc_arm32_type_width(const TcType *type);
uint64_t tc_arm32_type_mask(const TcType *type);
int tc_arm32_type_is_scalar_value(const TcType *type);
const TcStructDef *tc_arm32_struct_def(const TcIR *ir, const TcType *type);
const TcField *tc_arm32_field_at(const TcIR *ir, const TcType *type,
                                 uint32_t index);
uint32_t tc_arm32_type_id(TcArm32Emitter *emitter, const TcType *type);

void tc_arm32_move_immediate(TcArm32Emitter *emitter, unsigned reg,
                             uint32_t value);
void tc_arm32_load_symbol(TcArm32Emitter *emitter, unsigned reg,
                          const char *symbol);
void tc_arm32_add_constant(TcArm32Emitter *emitter, unsigned dst,
                           unsigned base, uint64_t value);
void tc_arm32_mask_register(TcArm32Emitter *emitter, unsigned reg,
                            const TcType *type);
void tc_arm32_normalize_bool(TcArm32Emitter *emitter, unsigned reg);
void tc_arm32_load_bits(TcArm32Emitter *emitter, unsigned base_reg,
                        uint64_t offset, unsigned bytes, unsigned dst_reg);
void tc_arm32_store_bits(TcArm32Emitter *emitter, unsigned base_reg,
                         uint64_t offset, unsigned bytes, unsigned src_reg);
void tc_arm32_trap(TcArm32Emitter *emitter, const TcSpan *span,
                   TcErrorKind kind);

int tc_arm32_slot_is_object(const TcArm32Slot *slot);
void tc_arm32_slot_address(TcArm32Emitter *emitter, uint32_t vreg,
                           unsigned dst_reg);
const TcType *tc_arm32_value_type(TcArm32Emitter *emitter,
                                  const TcOperand *operand);
void tc_arm32_operand_address(TcArm32Emitter *emitter,
                              const TcOperand *operand, unsigned dst_reg);
void tc_arm32_load_operand64(TcArm32Emitter *emitter,
                             const TcOperand *operand, unsigned dst_lo,
                             unsigned dst_hi);
void tc_arm32_load_operand32(TcArm32Emitter *emitter,
                             const TcOperand *operand, unsigned dst_reg);
void tc_arm32_store_result64(TcArm32Emitter *emitter, const TcInstr *instr,
                             unsigned src_lo, unsigned src_hi);
void tc_arm32_store_operand64(TcArm32Emitter *emitter,
                              const TcOperand *operand, unsigned src_lo,
                              unsigned src_hi);
void tc_arm32_copy_bytes(TcArm32Emitter *emitter, unsigned src_reg,
                         unsigned dst_reg, uint64_t size);
void tc_arm32_define_range(TcArm32Emitter *emitter, unsigned address_reg,
                           uint64_t size);
void tc_arm32_emit_call0(TcArm32Emitter *emitter, const char *symbol);
void tc_arm32_arg_immediate(TcArm32Emitter *emitter, unsigned word,
                            uint32_t value);
void tc_arm32_arg_reg(TcArm32Emitter *emitter, unsigned word, unsigned reg);
void tc_arm32_arg_operand(TcArm32Emitter *emitter, unsigned word,
                          const TcOperand *operand, int high);
void tc_arm32_arg_address(TcArm32Emitter *emitter, unsigned word,
                          const TcOperand *operand);
void tc_arm32_arg_file(TcArm32Emitter *emitter, unsigned word,
                       const TcSpan *span);

/* --- emit_abi.c: AAPCS32 classification -------------------------------- */

void tc_arm32_plan_arguments(TcArm32Emitter *emitter,
                             const TcType **param_types, size_t count,
                             int start_word, TcArm32ArgPlan *out);
void tc_arm32_plan_return(TcArm32Emitter *emitter, const TcType *ret,
                          TcArm32RetPlan *out);
void tc_arm32_free_argument_plan(TcArm32ArgPlan *plan);

/* --- emit_ops.c: instruction lowering --------------------------------- */

void tc_arm32_emit_instruction(TcArm32Emitter *emitter,
                               const TcInstr *instr);

/* Resolve a call instruction's parameter/return signature; the caller owns
 * the returned array. */
void tc_arm32_function_signature(TcArm32Emitter *emitter,
                                 const TcInstr *instr,
                                 const TcType ***out_params,
                                 size_t *out_count,
                                 const TcType **out_ret);

#endif /* TC_BACKEND_ARM32_EMIT_INTERNAL_H */
