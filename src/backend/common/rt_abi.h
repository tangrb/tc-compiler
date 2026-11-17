/* TC 0.0.46 generated-code/runtime ABI (design §7/§10).
 *
 * The emitters and the freestanding target runtime share this description of
 * the object table, the access flags and the helper entry points.  Each target
 * calls the helpers with its own C calling convention (AAPCS64 or AAPCS32);
 * the helper signatures use target-sized addresses/counts and 64-bit scalar
 * values so one core serves both word sizes.
 *
 * Helpers never return a trap to the caller: the first runtime trap records
 * the span and terminates the image with the profile's trap status.
 */
#ifndef TC_BACKEND_COMMON_RT_ABI_H
#define TC_BACKEND_COMMON_RT_ABI_H

#include <stdint.h>

#include "ir/region.h"

/* One storage object or field sub-object in the object table.  `offset` is
 * relative to the owning region base: the frame base for a frame descriptor
 * and the static region start for a static descriptor.  `type_id` is the
 * emitter's deterministic identity for the object's TcType. */
typedef struct TcRtObjectDesc {
    uint64_t offset;
    uint64_t size;
    uint32_t type_id;
    uint32_t flags;
} TcRtObjectDesc;

#define TC_RT_OBJECT_WRITABLE UINT32_C(1)
#define TC_RT_OBJECT_HAS_INIT UINT32_C(2)

/* Access flags passed to load/store. */
#define TC_RT_ACCESS_VOLATILE UINT32_C(1)

/* Region bases and sizes emitted by the assembly.  The static region start
 * and the code/stack boundaries come from the linker script; the device and
 * heap values come from the bound platform profile. */
#define TC_RT_SYM_STATIC_START "__tc_static_region"
#define TC_RT_SYM_STATIC_END "__tc_static_region_end"
#define TC_RT_SYM_STATIC_SHADOW "__tc_static_shadow"
#define TC_RT_SYM_STATIC_OBJECTS "__tc_static_objects"
#define TC_RT_SYM_STATIC_OBJECT_COUNT "__tc_static_object_count"
#define TC_RT_SYM_CODE_START "__tc_code_start"
#define TC_RT_SYM_CODE_END "__tc_code_end"
#define TC_RT_SYM_STACK_BOTTOM "__tc_stack_bottom"
#define TC_RT_SYM_STACK_TOP "__tc_stack_top"
#define TC_RT_SYM_DEVICE_BASE "__tc_rt_device_base"
#define TC_RT_SYM_DEVICE_SIZE "__tc_rt_device_size"
#define TC_RT_SYM_DEVICE_POLICY "__tc_rt_device_policy"
#define TC_RT_SYM_HEAP_BASE "__tc_rt_heap_base"
#define TC_RT_SYM_HEAP_SIZE "__tc_rt_heap_size"
#define TC_RT_SYM_CHECKED "__tc_rt_checked"
#define TC_RT_SYM_NORMAL_STATUS "__tc_rt_normal_status"
#define TC_RT_SYM_TRAP_STATUS "__tc_rt_trap_status"

/* Helper symbols. */
#define TC_RT_SYM_TRAP "tc_rt_trap"
#define TC_RT_SYM_EXIT "tc_rt_runtime_exit"
#define TC_RT_SYM_PROGRAM_INIT "tc_rt_program_init"
#define TC_RT_SYM_FRAME_ENTER "tc_rt_frame_enter"
#define TC_RT_SYM_FRAME_LEAVE "tc_rt_frame_leave"
#define TC_RT_SYM_LOAD "tc_rt_load"
#define TC_RT_SYM_STORE "tc_rt_store"
#define TC_RT_SYM_MEMMOVE "tc_rt_memmove"
#define TC_RT_SYM_DEFINE "tc_rt_define"
#define TC_RT_SYM_PTRADD "tc_rt_ptradd"
#define TC_RT_SYM_PTRDIFF "tc_rt_ptrdiff"
#define TC_RT_SYM_FLOAT_BINOP "tc_rt_float_binop"
#define TC_RT_SYM_FLOAT_UNOP "tc_rt_float_unop"
#define TC_RT_SYM_FLOAT_COMPARE "tc_rt_float_compare"
#define TC_RT_SYM_CAST "tc_rt_cast"
#define TC_RT_SYM_TRUNC "tc_rt_trunc"
#define TC_RT_SYM_BITCAST "tc_rt_bitcast"
#define TC_RT_SYM_WRITE "tc_rt_write_scalar"
#define TC_RT_SYM_READ "tc_rt_read_scalar"
#define TC_RT_SYM_CALL_DEPTH "__tc_call_depth"

#endif /* TC_BACKEND_COMMON_RT_ABI_H */
