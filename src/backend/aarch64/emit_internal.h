/* TC 0.0.46 AArch64 emitter internals (design §10).
 *
 * The emitter lowers one finalized `TcIR` to deterministic GNU-syntax AArch64
 * assembly.  Values live in stack slots (no register allocator): a vreg that
 * names a frame/static storage object lives at its IR object offset, every
 * other vreg gets a fresh slot.  Structured values occupy their full `sizeof`
 * bytes in memory; scalars occupy eight bytes holding the bit pattern.
 *
 * These declarations are shared by the emitter translation units only and are
 * not part of the compiler's external contract.
 */
#ifndef TC_BACKEND_AARCH64_EMIT_INTERNAL_H
#define TC_BACKEND_AARCH64_EMIT_INTERNAL_H

#include <stddef.h>
#include <stdio.h>

#include "backend/aarch64/emit.h"
#include "backend/common/emit_stats.h"
#include "backend/common/rt_abi.h"
#include "ir/ir.h"
#include "runtime/profile.h"

#define TC_A64_MAX_SOURCE_FILES 256u
#define TC_A64_MAX_PARAMS 64u   /* only a planning-array limit, not semantic */

typedef enum {
    TC_A64_SLOT_FRAME = 0,
    TC_A64_SLOT_STATIC,
    TC_A64_SLOT_TEMP
} TcA64SlotKind;

typedef struct {
    TcA64SlotKind kind;
    uint64_t offset; /* frame-relative, static-region-relative or slot offset */
    uint64_t size;   /* bytes occupied in memory */
    const TcType *type;
} TcA64Slot;

typedef struct TcA64Emitter {
    const TcIR *ir;
    const TcLineMap *linemap;
    const TcPlatformProfile *profile;
    const TcAllocator *alloc;
    FILE *out;
    char *error_message;
    size_t error_capacity;
    int failed;
    int internal_error; /* capability limit of the platform instance */
    int oom_failed;
    int output_failed;
    unsigned next_label;

    const char *source_files[TC_A64_MAX_SOURCE_FILES];
    size_t source_file_count;

    /* Deterministic type identity shared by descriptors and helpers. */
    const TcType **types;
    uint32_t *type_ids;
    size_t type_count;
    size_t type_capacity;

    const TcUnit *unit;
    size_t unit_index;
    TcA64Slot *slots;
    size_t slot_count;
    uint64_t frame_size;
    uint64_t frame_object_end;
    uint64_t scratch_offset; /* 16-byte scratch for constants/store sources */
    uint64_t scratch_buffer_offset; /* discarded aggregate result buffer */
    uint64_t scratch_buffer_size;
    uint64_t sret_offset;    /* saved x8 for an indirect-returning function */
    uint64_t read_scratch_offset;
    uint64_t shadow_offset;
    uint64_t shadow_size;
    int unit_has_read;
    uint64_t max_outgoing; /* largest stack-argument area of one call site */
    uint64_t frame_desc_count; /* base objects plus field sub-objects */
    uint64_t frame_max;
    TcEmitCounter emit_counter;
} TcA64Emitter;

/* ABI argument location. */
typedef enum {
    TC_A64_LOC_XREG = 0,
    TC_A64_LOC_VREG,
    TC_A64_LOC_STACK
} TcA64ArgLoc;

typedef struct {
    TcA64ArgLoc kind;
    unsigned index;       /* register number, unused for stack */
    uint64_t stack_offset; /* NSAA offset for stack arguments */
    uint64_t size;
    int hfa;              /* aggregate passed as consecutive float members */
    int hfa32;            /* HFA element is float32 (else float64) */
    unsigned hfa_count;
    int indirect;         /* composite >16 bytes: caller copy, pointer passed */
    uint64_t copy_offset; /* outgoing-area offset of the caller's copy */
} TcA64ArgSlot;

typedef struct {
    const TcAllocator *alloc;
    TcA64ArgSlot *slots;
    size_t count;
    uint64_t stack_bytes;
    int sret;
} TcA64ArgPlan;

typedef enum {
    TC_A64_RET_VOID = 0,
    TC_A64_RET_XREGS,
    TC_A64_RET_VREGS,
    TC_A64_RET_INDIRECT
} TcA64RetKind;

typedef struct {
    TcA64RetKind kind;
    unsigned count;
    int hfa32;
    unsigned hfa_count;
} TcA64RetPlan;

/* --- emit.c: shared services ------------------------------------------- */

void tc_a64_write(TcA64Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_a64_error(TcA64Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void tc_a64_oom(TcA64Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
/* Record a platform-instance capacity limit: the tool reports an internal
 * implementation error rather than an unsupported language capability. */
void tc_a64_internal_error(TcA64Emitter *emitter, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
unsigned tc_a64_label(TcA64Emitter *emitter);

uint64_t tc_a64_type_size(const TcIR *ir, const TcType *type, int *ok);
unsigned tc_a64_type_width(const TcType *type);
uint64_t tc_a64_type_mask(const TcType *type);
int tc_a64_type_is_scalar_value(const TcType *type);
int tc_a64_type_is_float(const TcType *type);
const TcStructDef *tc_a64_struct_def(const TcIR *ir, const TcType *type);
const TcField *tc_a64_field_at(const TcIR *ir, const TcType *type,
                               uint32_t index);
uint32_t tc_a64_type_id(TcA64Emitter *emitter, const TcType *type);

void tc_a64_move_immediate(TcA64Emitter *emitter, unsigned reg, uint64_t value);
void tc_a64_add_constant(TcA64Emitter *emitter, unsigned dst, unsigned base,
                         uint64_t value);
void tc_a64_mask_register(TcA64Emitter *emitter, unsigned reg,
                          const TcType *type);
void tc_a64_sign_extend(TcA64Emitter *emitter, unsigned reg,
                        const TcType *type);
void tc_a64_normalize_bool(TcA64Emitter *emitter, unsigned reg);
void tc_a64_load_bits(TcA64Emitter *emitter, unsigned base_reg,
                      uint64_t offset, unsigned bytes, unsigned dst_reg);
void tc_a64_store_bits(TcA64Emitter *emitter, unsigned base_reg,
                       uint64_t offset, unsigned bytes, unsigned src_reg);
void tc_a64_trap(TcA64Emitter *emitter, const TcSpan *span, TcErrorKind kind);
void tc_a64_span_arguments(TcA64Emitter *emitter, const TcSpan *span,
                           unsigned first_reg);

int tc_a64_slot_is_object(const TcA64Slot *slot);
void tc_a64_slot_address(TcA64Emitter *emitter, uint32_t vreg,
                         unsigned dst_reg);
const TcType *tc_a64_operand_type(const TcUnit *unit,
                                  const TcOperand *operand);
const TcType *tc_a64_value_type(TcA64Emitter *emitter,
                                const TcOperand *operand);
void tc_a64_operand_address(TcA64Emitter *emitter, const TcOperand *operand,
                            unsigned dst_reg);
void tc_a64_load_operand(TcA64Emitter *emitter, const TcOperand *operand,
                         unsigned reg, int sign_extend);
void tc_a64_store_result(TcA64Emitter *emitter, const TcInstr *instr,
                         unsigned reg);
void tc_a64_store_operand(TcA64Emitter *emitter, const TcOperand *operand,
                          unsigned reg);
void tc_a64_load_const_to_scratch(TcA64Emitter *emitter,
                                  const TcOperand *operand);
void tc_a64_copy_bytes(TcA64Emitter *emitter, uint64_t size);
void tc_a64_define_range(TcA64Emitter *emitter, unsigned address_reg,
                         uint64_t size);
void tc_a64_emit_call0(TcA64Emitter *emitter, const char *symbol);

/* --- emit_abi.c: AAPCS64 classification -------------------------------- */

void tc_a64_plan_arguments(TcA64Emitter *emitter, const TcType **param_types,
                           size_t count, TcA64ArgPlan *out);
void tc_a64_plan_return(TcA64Emitter *emitter, const TcType *ret,
                        TcA64RetPlan *out);
int tc_a64_plan_hfa(const TcIR *ir, const TcType *type, int *out_is32,
                    unsigned *out_count);
void tc_a64_free_argument_plan(TcA64ArgPlan *plan);

/* --- emit_ops.c: instruction lowering --------------------------------- */

void tc_a64_emit_instruction(TcA64Emitter *emitter, const TcInstr *instr);

#endif /* TC_BACKEND_AARCH64_EMIT_INTERNAL_H */
