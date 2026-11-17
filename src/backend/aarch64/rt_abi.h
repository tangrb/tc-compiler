/* TC 0.0.46 AArch64 generated-code/runtime ABI (design §7/§10).
 *
 * The emitter and the freestanding runtime share this description of the
 * object table, the access flags and the helper entry points.  Generated code
 * calls the helpers with the AAPCS64 C convention; every helper has at most
 * eight integer parameters so all arguments travel in x0-x7.
 *
 * Helpers never return a trap to the caller: the first runtime trap records
 * the span and terminates the image with the profile's trap status.
 */
#ifndef TC_BACKEND_AARCH64_RT_ABI_H
#define TC_BACKEND_AARCH64_RT_ABI_H

#include <stdint.h>

#include "ir/region.h"

/* One storage object or field sub-object in the object table.  `offset` is
 * relative to the owning region base: the frame base for a frame descriptor
 * and the static region start for a static descriptor.  `type_id` is the
 * emitter's deterministic identity for the object's TcType. */
typedef struct TcA64ObjectDesc {
    uint64_t offset;
    uint64_t size;
    uint32_t type_id;
    uint32_t flags;
} TcA64ObjectDesc;

#define TC_A64_OBJECT_WRITABLE UINT32_C(1)
#define TC_A64_OBJECT_HAS_INIT UINT32_C(2)

/* Access flags passed to load/store. */
#define TC_A64_ACCESS_VOLATILE UINT32_C(1)

/* Region bases and sizes emitted by the assembly.  The static region start
 * and the code/stack boundaries come from the linker script; the device and
 * heap values come from the bound platform profile. */
#define TC_A64_SYM_STATIC_START "__tc_static_region"
#define TC_A64_SYM_STATIC_END "__tc_static_region_end"
#define TC_A64_SYM_STATIC_SHADOW "__tc_static_shadow"
#define TC_A64_SYM_STATIC_OBJECTS "__tc_static_objects"
#define TC_A64_SYM_STATIC_OBJECT_COUNT "__tc_static_object_count"
#define TC_A64_SYM_CODE_START "__tc_code_start"
#define TC_A64_SYM_CODE_END "__tc_code_end"
#define TC_A64_SYM_STACK_BOTTOM "__tc_stack_bottom"
#define TC_A64_SYM_STACK_TOP "__tc_stack_top"
#define TC_A64_SYM_DEVICE_BASE "__tc_a64_device_base"
#define TC_A64_SYM_DEVICE_SIZE "__tc_a64_device_size"
#define TC_A64_SYM_DEVICE_POLICY "__tc_a64_device_policy"
#define TC_A64_SYM_HEAP_BASE "__tc_a64_heap_base"
#define TC_A64_SYM_HEAP_SIZE "__tc_a64_heap_size"
#define TC_A64_SYM_CHECKED "__tc_a64_checked"
#define TC_A64_SYM_NORMAL_STATUS "__tc_a64_normal_status"
#define TC_A64_SYM_TRAP_STATUS "__tc_a64_trap_status"

/* Helper symbols. */
#define TC_A64_SYM_TRAP "tc_a64_trap"
#define TC_A64_SYM_EXIT "tc_a64_runtime_exit"
#define TC_A64_SYM_PROGRAM_INIT "tc_a64_program_init"
#define TC_A64_SYM_FRAME_ENTER "tc_a64_frame_enter"
#define TC_A64_SYM_FRAME_LEAVE "tc_a64_frame_leave"
#define TC_A64_SYM_LOAD "tc_a64_load"
#define TC_A64_SYM_STORE "tc_a64_store"
#define TC_A64_SYM_MEMMOVE "tc_a64_memmove"
#define TC_A64_SYM_DEFINE "tc_a64_define"
#define TC_A64_SYM_PTRADD "tc_a64_ptradd"
#define TC_A64_SYM_PTRDIFF "tc_a64_ptrdiff"
#define TC_A64_SYM_FLOAT_BINOP "tc_a64_float_binop"
#define TC_A64_SYM_FLOAT_UNOP "tc_a64_float_unop"
#define TC_A64_SYM_FLOAT_COMPARE "tc_a64_float_compare"
#define TC_A64_SYM_CAST "tc_a64_cast"
#define TC_A64_SYM_TRUNC "tc_a64_trunc"
#define TC_A64_SYM_BITCAST "tc_a64_bitcast"
#define TC_A64_SYM_WRITE "tc_a64_write_scalar"
#define TC_A64_SYM_READ "tc_a64_read_scalar"
#define TC_A64_SYM_CALL_DEPTH "__tc_call_depth"

#endif /* TC_BACKEND_AARCH64_RT_ABI_H */
