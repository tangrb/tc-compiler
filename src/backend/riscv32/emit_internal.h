/* TC 0.0.46 RISC-V 32 emitter internals (design §10).
 *
 * Values live in stack slots (no register allocator).  A vreg that names a
 * frame/static storage object lives at its IR object offset; every other vreg
 * gets a fresh slot.  Structured values occupy their full `sizeof` bytes;
 * scalars occupy eight bytes holding the 64-bit bit pattern.  Each function
 * keeps two bases in callee-saved registers: s0 = locals base (above the
 * outgoing argument area) and s1 = static region base.
 *
 * These declarations are shared by the emitter translation units only and are
 * not part of the compiler's external contract.
 */
#ifndef TC_BACKEND_RISCV32_EMIT_INTERNAL_H
#define TC_BACKEND_RISCV32_EMIT_INTERNAL_H

#include <stddef.h>
#include <stdio.h>

#include "backend/common/emit_stats.h"
#include "backend/common/rt_abi.h"
#include "backend/riscv32/emit.h"
#include "ir/ir.h"
#include "runtime/profile.h"

#define TC_RISCV32_MAX_SOURCE_FILES 256u

/* Emitter scratch registers.  All are caller-saved t registers, so helper
 * calls may clobber them; they are never live across a call. */
#define TC_RISCV32_TMP0 5u   /* t0 */
#define TC_RISCV32_TMP1 6u   /* t1 */
#define TC_RISCV32_VALUE0 28u /* t3 */
#define TC_RISCV32_VALUE1 29u /* t4 */
#define TC_RISCV32_VALUE2 30u /* t5 */
#define TC_RISCV32_VALUE3 31u /* t6 */
/* Callee-saved bases. */
#define TC_RISCV32_LOCALS 8u  /* s0 */
#define TC_RISCV32_STATIC 9u  /* s1 */
#define TC_RISCV32_SP 2u

typedef enum {
    TC_RISCV32_SLOT_FRAME = 0,
    TC_RISCV32_SLOT_STATIC,
    TC_RISCV32_SLOT_TEMP
} TcRiscv32SlotKind;

typedef struct {
    TcRiscv32SlotKind kind;
    uint64_t offset;
    uint64_t size;
    const TcType *type;
} TcRiscv32Slot;

typedef struct TcRiscv32Emitter {
    const TcIR *ir;
    const TcLineMap *linemap;
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

    const char *source_files[TC_RISCV32_MAX_SOURCE_FILES];
    size_t source_file_count;

    const TcType **types;
    uint32_t *type_ids;
    size_t type_count;
    size_t type_capacity;

    const TcUnit *unit;
    size_t unit_index;
    TcRiscv32Slot *slots;
    size_t slot_count;
    uint64_t frame_size;
    uint64_t frame_object_end;
    uint64_t outgoing_size;
    uint64_t frame_total;
    uint64_t scratch_offset;
    uint64_t scratch_buffer_offset;
    uint64_t scratch_buffer_size;
    uint64_t sret_offset;
    uint64_t read_scratch_offset;
    uint64_t shadow_offset;
    uint64_t shadow_size;
    uint64_t call_area_offset;
    uint64_t call_area_size;
    uint64_t incoming_stack_offset;
    int unit_has_read;
    uint64_t frame_desc_count;
    uint64_t frame_max;
    TcEmitCounter emit_counter;
} TcRiscv32Emitter;

typedef enum {
    TC_RISCV32_ARG_REG = 0,
    TC_RISCV32_ARG_STACK
} TcRiscv32ArgKind;

typedef struct {
    TcRiscv32ArgKind kind;
    unsigned reg;         /* a0-a7 index (0..7) for TC_RISCV32_ARG_REG */
    uint64_t stack_offset;/* NSAA offset of the stack-resident portion */
    uint64_t size;        /* value size in bytes */
    unsigned words;       /* 4-byte words occupied in registers or memory */
    int is64;             /* 64-bit fundamental scalar */
    int split64;          /* one register word plus one stack word */
    int by_reference;     /* aggregate larger than 2*XLEN: pointer argument */
    int struct_value;     /* composite passed by value as words */
    unsigned reg_words;   /* composite words placed in registers */
    unsigned stack_words; /* composite words placed on the stack */
    uint64_t stack_byte_offset; /* first composite byte placed on the stack */
} TcRiscv32ArgSlot;

typedef struct {
    const TcAllocator *alloc;
    TcRiscv32ArgSlot *slots;
    size_t count;
    uint64_t stack_bytes;
    uint64_t outgoing_size; /* stack arguments, 16-aligned */
    int sret;
} TcRiscv32ArgPlan;

typedef enum {
    TC_RISCV32_RET_VOID = 0,
    TC_RISCV32_RET_WORD,     /* scalar <=32 bits or composite <=4 bytes in a0 */
    TC_RISCV32_RET_PAIR,     /* 64-bit scalar or composite 5..8 bytes in a0:a1 */
    TC_RISCV32_RET_INDIRECT  /* hidden result pointer in a0 */
} TcRiscv32RetKind;

typedef struct {
    TcRiscv32RetKind kind;
    uint64_t size;
} TcRiscv32RetPlan;

/* --- emit.c: shared services ------------------------------------------- */

const char *tc_riscv32_reg_name(unsigned reg);
void tc_riscv32_write(TcRiscv32Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_riscv32_error(TcRiscv32Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_riscv32_oom(TcRiscv32Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_riscv32_internal_error(TcRiscv32Emitter *emitter,
                               const char *format, ...)
    __attribute__((format(printf, 2, 3)));
unsigned tc_riscv32_label(TcRiscv32Emitter *emitter);

uint64_t tc_riscv32_type_size(const TcIR *ir, const TcType *type, int *ok);
unsigned tc_riscv32_type_width(const TcType *type);
uint64_t tc_riscv32_type_mask(const TcType *type);
int tc_riscv32_type_is_scalar_value(const TcType *type);
const TcStructDef *tc_riscv32_struct_def(const TcIR *ir, const TcType *type);
const TcField *tc_riscv32_field_at(const TcIR *ir, const TcType *type,
                                   uint32_t index);
uint32_t tc_riscv32_type_id(TcRiscv32Emitter *emitter, const TcType *type);

void tc_riscv32_move_immediate(TcRiscv32Emitter *emitter, unsigned reg,
                               uint32_t value);
void tc_riscv32_load_symbol(TcRiscv32Emitter *emitter, unsigned reg,
                            const char *symbol);
void tc_riscv32_add_constant(TcRiscv32Emitter *emitter, unsigned dst,
                             unsigned base, uint64_t value);
void tc_riscv32_mask_register(TcRiscv32Emitter *emitter, unsigned reg,
                              const TcType *type);
void tc_riscv32_normalize_bool(TcRiscv32Emitter *emitter, unsigned reg);
void tc_riscv32_sign_extend_narrow(TcRiscv32Emitter *emitter, unsigned reg,
                                   const TcType *type);
void tc_riscv32_load_bits(TcRiscv32Emitter *emitter, unsigned base_reg,
                          uint64_t offset, unsigned bytes, unsigned dst_reg);
void tc_riscv32_store_bits(TcRiscv32Emitter *emitter, unsigned base_reg,
                           uint64_t offset, unsigned bytes, unsigned src_reg);
void tc_riscv32_trap(TcRiscv32Emitter *emitter, const TcSpan *span,
                     TcErrorKind kind);

int tc_riscv32_slot_is_object(const TcRiscv32Slot *slot);
void tc_riscv32_slot_address(TcRiscv32Emitter *emitter, uint32_t vreg,
                             unsigned dst_reg);
const TcType *tc_riscv32_value_type(TcRiscv32Emitter *emitter,
                                    const TcOperand *operand);
void tc_riscv32_operand_address(TcRiscv32Emitter *emitter,
                                const TcOperand *operand, unsigned dst_reg);
void tc_riscv32_load_operand64(TcRiscv32Emitter *emitter,
                               const TcOperand *operand, unsigned dst_lo,
                               unsigned dst_hi);
void tc_riscv32_load_operand32(TcRiscv32Emitter *emitter,
                               const TcOperand *operand, unsigned dst_reg);
void tc_riscv32_store_result64(TcRiscv32Emitter *emitter,
                               const TcInstr *instr, unsigned src_lo,
                               unsigned src_hi);
void tc_riscv32_store_operand64(TcRiscv32Emitter *emitter,
                                const TcOperand *operand, unsigned src_lo,
                                unsigned src_hi);
void tc_riscv32_copy_bytes(TcRiscv32Emitter *emitter, unsigned src_reg,
                           unsigned dst_reg, uint64_t size);
void tc_riscv32_define_range(TcRiscv32Emitter *emitter, unsigned address_reg,
                             uint64_t size);
void tc_riscv32_emit_call0(TcRiscv32Emitter *emitter, const char *symbol);
void tc_riscv32_arg_immediate(TcRiscv32Emitter *emitter, unsigned word,
                              uint32_t value);
void tc_riscv32_arg_reg(TcRiscv32Emitter *emitter, unsigned word,
                        unsigned reg);
void tc_riscv32_arg_operand(TcRiscv32Emitter *emitter, unsigned word,
                            const TcOperand *operand, int high);
void tc_riscv32_arg_address(TcRiscv32Emitter *emitter, unsigned word,
                            const TcOperand *operand);
void tc_riscv32_arg_file(TcRiscv32Emitter *emitter, unsigned word,
                         const TcSpan *span);

/* --- emit_abi.c: ILP32 classification ---------------------------------- */

void tc_riscv32_plan_arguments(TcRiscv32Emitter *emitter,
                               const TcType **param_types, size_t count,
                               int start_word, TcRiscv32ArgPlan *out);
void tc_riscv32_plan_return(TcRiscv32Emitter *emitter, const TcType *ret,
                            TcRiscv32RetPlan *out);
void tc_riscv32_free_argument_plan(TcRiscv32ArgPlan *plan);

/* --- emit_ops.c: instruction lowering --------------------------------- */

void tc_riscv32_emit_instruction(TcRiscv32Emitter *emitter,
                                 const TcInstr *instr);

/* Resolve a call instruction's parameter/return signature; the caller owns
 * the returned array. */
void tc_riscv32_function_signature(TcRiscv32Emitter *emitter,
                                   const TcInstr *instr,
                                   const TcType ***out_params,
                                   size_t *out_count,
                                   const TcType **out_ret);

#endif /* TC_BACKEND_RISCV32_EMIT_INTERNAL_H */
