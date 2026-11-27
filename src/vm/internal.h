/* TC 0.0.46 reference VM internals (design §4, §9).  Private to `vm/`. */
#ifndef TC_VM_INTERNAL_H
#define TC_VM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ir/ir.h"
#include "runtime/input.h"
#include "runtime/profile.h"
#include "runtime/trap.h"
#include "vm/observe.h"
#include "vm/vm.h"

typedef struct {
    uint64_t bits; /* scalar/pointer/fnptr value */
    uint8_t *ptr;  /* struct value storage (borrowed) */
} TcVmValue;

struct TcVm;
typedef unsigned (*TcVmDebugBoundary)(void *ctx, struct TcVm *vm,
                                      uint32_t frame_index, uint64_t address);

typedef struct {
    void *ctx;
    TcVmDebugBoundary boundary;
} TcVmDebugHooks;

#define TC_VM_DEBUG_STOP UINT32_C(0x01)
#define TC_VM_DEBUG_INTERRUPT UINT32_C(0x02)
#define TC_VM_DEBUG_DISCONNECT UINT32_C(0x04)
#define TC_VM_DEBUG_DETACH UINT32_C(0x08)

typedef struct {
    uint32_t ir_object; /* original TcObject id; UINT32_MAX for synthetic */
    TcRegionKind region;
    uint64_t start; /* target address */
    uint64_t size;
    const TcType *type;
    int writable;
    uint32_t frame; /* runtime frame index; UINT32_MAX for static */
} TcVmObject;

typedef struct {
    uint64_t frame_size;   /* span of the unit's frame objects, aligned */
    uint64_t temp_size;    /* struct temporary storage in host bytes */
    size_t *temp_off;      /* per vreg; SIZE_MAX when not a struct temporary */
    uint32_t *frame_objects; /* unique FRAME TcObject ids referenced by vregs */
    size_t nframe_objects;
} TcVmUnitInfo;

typedef struct {
    const TcUnit *unit;
    const TcVmUnitInfo *info;
    uint32_t func_id;
    uint64_t base; /* target address of the frame block */
    uint8_t *mem;
    size_t mem_size;
    uint8_t *temps;
    size_t temps_size;
    uint8_t *defined; /* checked build: one bit per frame-object byte */
    size_t defined_size;
    TcVmValue *regs;
    uint32_t *vreg_obj; /* per vreg runtime object index, UINT32_MAX if none */
    uint32_t first_object;
    int valid;
    uint32_t resume_block;
    size_t resume_pc;
    int has_resume;
    int debug_counted;
    uint32_t pending_callee;
    const TcInstr *pending_call;
} TcVmFrame;

typedef struct TcVm {
    const TcIR *ir;
    const TcPlatformProfile *profile;
    const TcAllocator *alloc;
    unsigned W;
    uint64_t addr_mask;
    TcVmResult *result;
    TcImplError *out_impl;
    TcImplError impl;

    TcVmUnitInfo *units;
    size_t nunits;

    TcVmObject *objects;
    size_t nobjects;
    size_t cap_objects;

    uint32_t *static_index; /* ir->nobjects entries */
    uint8_t *static_mem;
    uint8_t *static_defined; /* checked build defined-byte bitmap */
    uint64_t static_size;
    uint64_t static_base;

    uint8_t *device_mem;
    uint64_t device_size;
    uint64_t device_base;

    TcVmFrame *frames;
    size_t nframes;
    size_t cap_frames;
    uint64_t frame_top;
    uint64_t frame_region_size;

    TcInput input;
    unsigned depth;
    TcSpan current_span;
    const TcUnit *current_unit;
    const TcInstr *current_instr;
    uint32_t current_bb;
    uint32_t current_insn;
    const TcVmObserver *observer;
    TcVmStats *stats;
    uint64_t trace_records;
    uint64_t trace_dropped;
    int trace_started;
    int trace_truncated;
    int trace_failed;
    int count_overflow;
    const TcVmDebugHooks *debug;
    const TcLineMap *debug_linemap;
    uint64_t debug_code_base;
    uint64_t debug_pc;
    uint64_t debug_insns;
    uint32_t debug_frame;
    unsigned debug_stop;
    int debug_single;
    int debug_terminated;
} TcVm;

/* Allocation helpers; failure sets vm->impl and returns NULL. */
void *tc_vm_alloc(TcVm *vm, size_t size);
void *tc_vm_calloc(TcVm *vm, size_t count, size_t size);
void tc_vm_free(TcVm *vm, void *ptr);

/* Trap/implementation failure helpers.  They record the first failure. */
int tc_vm_trap(TcVm *vm, const TcSpan *span, TcErrorKind kind);
int tc_vm_impl(TcVm *vm, TcImplError error);
int tc_vm_failed(const TcVm *vm);

void tc_vm_observe_ir(TcVm *vm, const TcInstr *instr);
void tc_vm_observe_mem(TcVm *vm, uint64_t address, uint64_t size);
void tc_vm_observe_memmove(TcVm *vm, uint64_t dst, uint64_t src,
                           uint64_t size);
void tc_vm_observe_io(TcVm *vm, uint64_t bytes, const char *result);
void tc_vm_observe_call(TcVm *vm, const TcInstr *instr,
                        const TcFunc *callee, int returning);
void tc_vm_observe_trap(TcVm *vm, TcErrorKind kind);
void tc_vm_trace_header(TcVm *vm);
void tc_vm_trace_footer(TcVm *vm, const char *status);

/* Type size under the bound profile (standard §6.13). */
uint64_t tc_vm_type_size(TcVm *vm, const TcType *type, int *ok);

/* Value access.  Return 1 on success, 0 after a trap/implementation failure. */
int tc_vm_read_operand(TcVm *vm, TcVmFrame *frame, const TcOperand *operand,
                       TcVmValue *out, const TcType **out_type);
int tc_vm_write_operand(TcVm *vm, TcVmFrame *frame, const TcOperand *operand,
                        const TcVmValue *value);
int tc_vm_operand_address(TcVm *vm, TcVmFrame *frame,
                          const TcOperand *operand, const TcType *target,
                          uint64_t *out_address);
const TcType *tc_vm_operand_type(TcVm *vm, TcVmFrame *frame,
                                 const TcOperand *operand);

/* Memory operations (all mandatory traps included). */
int tc_vm_load_value(TcVm *vm, uint64_t address, const TcType *type,
                     int volatile_access, TcVmValue *out);
int tc_vm_store_value(TcVm *vm, uint64_t address, const TcType *type,
                      const TcVmValue *value, int volatile_access);
int tc_vm_write_host_value(TcVm *vm, uint8_t *host, const TcType *type,
                           const TcVmValue *value);
int tc_vm_memmove(TcVm *vm, uint64_t dst, uint64_t src, uint64_t count,
                  const TcType *type);
int tc_vm_ptr_arith(TcVm *vm, uint64_t ptr, uint64_t count,
                    const TcType *element, int add, uint64_t *out);
int tc_vm_ptrdiff(TcVm *vm, uint64_t a, uint64_t b, uint64_t *out);

/* Function address mapping (unique per function, code region). */
uint64_t tc_vm_func_address(const TcVm *vm, uint32_t func_id);
int tc_vm_func_from_address(const TcVm *vm, uint64_t address,
                            uint32_t *out_func);

/* Call frames. */
int tc_vm_push_frame(TcVm *vm, const TcUnit *unit, uint32_t func_id,
                     uint32_t *out_index);
void tc_vm_pop_frame(TcVm *vm);
/* Setup/teardown of the execution environment (memory.c). */
int tc_vm_setup(TcVm *vm, const TcIR *ir, const TcPlatformProfile *profile,
                TcVmResult *result, TcImplError *out_impl);
void tc_vm_teardown(TcVm *vm);

/* Interpreter entry for one unit's frame (interp.c). */
typedef enum {
    TC_VM_FLOW_RETURN = 0,
    TC_VM_FLOW_END, /* fell off the end of the unit */
    TC_VM_FLOW_FAIL,
    TC_VM_FLOW_PAUSE
} TcVmFlow;

TcVmFlow tc_vm_execute(TcVm *vm, uint32_t frame_index, TcVmValue *ret,
                       int *has_ret);
TcVmFlow tc_vm_execute_resume(TcVm *vm, uint32_t frame_index, int single);
int tc_vm_debug_address(const TcVm *vm, const TcInstr *instr, uint64_t *out);
uint64_t tc_vm_debug_pc(const TcVm *vm);
int tc_vm_debug_push_top(TcVm *vm, uint32_t *out_frame);
int tc_vm_reset(TcVm *vm);

#endif /* TC_VM_INTERNAL_H */
