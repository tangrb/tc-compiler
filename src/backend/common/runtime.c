/* TC 0.0.46 freestanding target runtime (design §7.2/§10.3/§11).
 *
 * This core is shared by every AOT target.  It provides:
 *   - object-table driven memory accesses: mandatory null/write-protected/
 *     code/device traps in every build, plus checked-build object bounds and
 *     defined-byte tracking for out-of-object, one-past, dangling and
 *     uninitialized accesses;
 *   - the shared W-aware semantic core for floating arithmetic, conversions,
 *     deterministic scalar formatting and token input;
 *   - the freestanding allocator used by the runtime's own token/decimal
 *     buffers (never exposed to the language).
 *
 * The platform instance supplies the program stdout seam, the exit-record
 * channel and termination through `backend/common/rt_platform.h`.  The core is
 * freestanding: no hosted headers, no language-visible allocation, no locale,
 * and no device-side import resolution.
 */
#include <stddef.h>
#include <stdint.h>

#include "backend/common/rt_abi.h"
#include "backend/common/rt_platform.h"
#include "diagnostic/codes.h"
#include "ir/instr.h"
#include "ir/region.h"
#include "ir/type.h"
#include "ir/type_width.h"
#include "runtime/format.h"
#include "runtime/input.h"
#include "runtime/profile.h"
#include "runtime/rt_memory.h"
#include "runtime/semantics.h"

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);

#define TC_RT_MAX_FRAMES 600u
#if TC_RT_W == 32u
#define TC_RT_ALLOC_ARENA_SIZE (128u * 1024u)
#else
#define TC_RT_ALLOC_ARENA_SIZE (8u * 1024u * 1024u)
#endif
#define TC_RT_RECORD_BUFFER_SIZE 512u

/* Abstract call depth (standard §1.2/design §10.3).  Defined in .bss so the
 * loader zero-initializes it. */
unsigned long __tc_call_depth = 0;

/* ------------------------------------------------------------------------- */
/* Emitted platform/region facts.                                            */
/* ------------------------------------------------------------------------- */

extern const uint64_t __tc_rt_device_base;
extern const uint64_t __tc_rt_device_size;
extern const uint64_t __tc_rt_device_policy;
extern const uint64_t __tc_rt_heap_base;
extern const uint64_t __tc_rt_heap_size;
extern const uint64_t __tc_rt_checked;
extern const uint64_t __tc_rt_normal_status;
extern const uint64_t __tc_rt_trap_status;

extern const uint8_t __tc_static_region[];
extern const uint8_t __tc_static_region_end[];
extern uint8_t __tc_static_shadow[];
extern const TcRtObjectDesc __tc_static_objects[];
extern const uint64_t __tc_static_object_count;

extern const uint8_t __tc_code_start[];
extern const uint8_t __tc_code_end[];
extern const uint8_t __tc_stack_bottom[];
extern const uint8_t __tc_stack_top[];

extern const uint8_t __tc_input_image[];

/* ------------------------------------------------------------------------- */
/* Trap and exit.                                                            */
/* ------------------------------------------------------------------------- */

static const char *tc_rt_trap_error_symbol(unsigned code) {
    switch ((TcDiagCode)code) {
    case TC_RE_INTEGER_OVERFLOW: return "TC_RE_INTEGER_OVERFLOW";
    case TC_RE_DIV_ZERO: return "TC_RE_DIV_ZERO";
    case TC_RE_NEGATIVE_SHIFT: return "TC_RE_NEGATIVE_SHIFT";
    case TC_RE_FLOAT_OVERFLOW: return "TC_RE_FLOAT_OVERFLOW";
    case TC_RE_FLOAT_UNDERFLOW: return "TC_RE_FLOAT_UNDERFLOW";
    case TC_RE_FLOAT_INVALID: return "TC_RE_FLOAT_INVALID";
    case TC_RE_CAST_OVERFLOW: return "TC_RE_CAST_OVERFLOW";
    case TC_RE_NULL_POINTER: return "TC_RE_NULL_POINTER";
    case TC_RE_PTRDIFF_OVERFLOW: return "TC_RE_PTRDIFF_OVERFLOW";
    case TC_RE_WRITE_PROTECTED: return "TC_RE_WRITE_PROTECTED";
    case TC_RE_ADDR_UNAVAILABLE: return "TC_RE_ADDR_UNAVAILABLE";
    case TC_RE_STACK_OVERFLOW: return "TC_RE_STACK_OVERFLOW";
    case TC_RE_NULL_FUNCTION_POINTER: return "TC_RE_NULL_FUNCTION_POINTER";
    case TC_RE_IO: return "TC_RE_IO";
    case TC_RE_TRAP: return "TC_RE_TRAP";
    default: return "TC_RE_TRAP";
    }
}

static size_t record_append(char *buffer, size_t capacity, size_t length,
                            const char *text) {
    while (text != NULL && *text != '\0' && length < capacity) {
        buffer[length++] = *text++;
    }
    return length;
}

static size_t record_append_u64(char *buffer, size_t capacity, size_t length,
                                uint64_t value) {
    char digits[24];
    size_t count = 0;
    if (value == 0) {
        return record_append(buffer, capacity, length, "0");
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0 && length < capacity) {
        buffer[length++] = digits[--count];
    }
    return length;
}

static void record_emit(char *buffer, size_t length) {
    if (length > 0) {
        tc_rt_platform_record(buffer, length);
    }
}

__attribute__((noreturn)) void tc_rt_runtime_exit(uint32_t status) {
    char buffer[TC_RT_RECORD_BUFFER_SIZE];
    size_t length = 0;
    length = record_append(buffer, sizeof(buffer), length, "TC-EXIT ");
    length = record_append_u64(buffer, sizeof(buffer), length, status);
    length = record_append(buffer, sizeof(buffer), length, " none\n");
    record_emit(buffer, length);
    tc_rt_platform_exit(status);
}

__attribute__((noreturn)) void tc_rt_trap(uint32_t code, uint32_t line,
                                          uint32_t column, const char *file) {
    char buffer[TC_RT_RECORD_BUFFER_SIZE];
    uint32_t profile_status = (uint32_t)__tc_rt_trap_status;
    size_t length = 0;
    length = record_append(buffer, sizeof(buffer), length, "error[RT]: ");
    length = record_append(buffer, sizeof(buffer), length,
                           tc_rt_trap_error_symbol((unsigned)code));
    length = record_append(buffer, sizeof(buffer), length, " at ");
    length = record_append(buffer, sizeof(buffer), length,
                           file != NULL ? file : "<source>");
    length = record_append(buffer, sizeof(buffer), length, ":");
    length = record_append_u64(buffer, sizeof(buffer), length, line);
    length = record_append(buffer, sizeof(buffer), length, ":");
    length = record_append_u64(buffer, sizeof(buffer), length, column);
    length = record_append(buffer, sizeof(buffer), length, "\n");
    length = record_append(buffer, sizeof(buffer), length, "TC-EXIT ");
    length = record_append_u64(buffer, sizeof(buffer), length, profile_status);
    length = record_append(buffer, sizeof(buffer), length, " ");
    length = record_append(buffer, sizeof(buffer), length,
                           tc_rt_trap_error_symbol((unsigned)code));
    length = record_append(buffer, sizeof(buffer), length, "\n");
    record_emit(buffer, length);
    tc_rt_platform_exit(profile_status);
}

/* ------------------------------------------------------------------------- */
/* Freestanding allocator (runtime-private buffers only).                    */
/* ------------------------------------------------------------------------- */

typedef struct TcRtAllocBlock {
    size_t size;
    int free;
    struct TcRtAllocBlock *next;
} TcRtAllocBlock;

static unsigned char tc_alloc_arena[TC_RT_ALLOC_ARENA_SIZE]
    __attribute__((aligned(16)));

static TcRtAllocBlock *tc_alloc_head = NULL;

static size_t alloc_align(size_t value) {
    return (value + 15u) & ~(size_t)15u;
}

static void *runtime_alloc(size_t size) {
    TcRtAllocBlock *block = tc_alloc_head;
    size_t need = alloc_align(size == 0 ? 1u : size);
    while (block != NULL) {
        if (block->free && block->size >= need) {
            if (block->size >= need + sizeof(TcRtAllocBlock) + 16u) {
                TcRtAllocBlock *rest =
                    (TcRtAllocBlock *)((unsigned char *)block +
                                        sizeof(TcRtAllocBlock) + need);
                rest->size = block->size - need - sizeof(TcRtAllocBlock);
                rest->free = 1;
                rest->next = block->next;
                block->next = rest;
                block->size = need;
            }
            block->free = 0;
            return (unsigned char *)block + sizeof(TcRtAllocBlock);
        }
        block = block->next;
    }
    if (tc_alloc_head == NULL) {
        tc_alloc_head = (TcRtAllocBlock *)(void *)tc_alloc_arena;
        tc_alloc_head->size = TC_RT_ALLOC_ARENA_SIZE - sizeof(TcRtAllocBlock);
        tc_alloc_head->free = 1;
        tc_alloc_head->next = NULL;
        return runtime_alloc(size);
    }
    return NULL;
}

static void runtime_free(void *ptr) {
    TcRtAllocBlock *block;
    if (ptr == NULL) {
        return;
    }
    block = (TcRtAllocBlock *)((unsigned char *)ptr -
                                sizeof(TcRtAllocBlock));
    block->free = 1;
}

static void *runtime_realloc(void *ptr, size_t size) {
    TcRtAllocBlock *block;
    void *fresh;
    size_t copy;
    if (ptr == NULL) {
        return runtime_alloc(size);
    }
    if (size == 0) {
        runtime_free(ptr);
        return NULL;
    }
    block = (TcRtAllocBlock *)((unsigned char *)ptr -
                                sizeof(TcRtAllocBlock));
    if (block->size >= size) {
        return ptr;
    }
    fresh = runtime_alloc(size);
    if (fresh == NULL) {
        return NULL;
    }
    copy = block->size < size ? block->size : size;
    tc_rt_memcpy(fresh, ptr, copy);
    runtime_free(ptr);
    return fresh;
}

static void *runtime_alloc_ctx(void *ctx, size_t size) {
    (void)ctx;
    return runtime_alloc(size);
}

static void *runtime_realloc_ctx(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    return runtime_realloc(ptr, size);
}

static void runtime_free_ctx(void *ctx, void *ptr) {
    (void)ctx;
    runtime_free(ptr);
}

static const TcAllocator tc_runtime_allocator = {
    runtime_alloc_ctx, runtime_realloc_ctx, runtime_free_ctx, NULL
};

const TcAllocator *tc_allocator_libc(void) {
    return &tc_runtime_allocator;
}

/* ------------------------------------------------------------------------- */
/* Region and object facts.                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    const TcRtObjectDesc *objects;
    uint64_t count;
    uint64_t base;
    uint64_t size;
    uint8_t *shadow;
    uint64_t shadow_size;
} TcRtFrame;

static TcRtFrame tc_frames[TC_RT_MAX_FRAMES];
static uint64_t tc_frame_count = 0;

static int checked_build(void) {
    return __tc_rt_checked != 0;
}

static int region_contains(uint64_t address, const uint8_t *start,
                           const uint8_t *end) {
    uintptr_t base = (uintptr_t)start;
    uintptr_t limit = (uintptr_t)end;
    return (uintptr_t)address >= base && (uintptr_t)address < limit;
}

/* Classify an address.  A frame address is only valid while its frame is
 * live; the frame pointer is returned through `out_frame`. */
static int locate_address(uint64_t address, TcRegionKind *out_region,
                          TcRtFrame **out_frame) {
    uintptr_t stack_bottom = (uintptr_t)__tc_stack_bottom;
    uintptr_t stack_top = (uintptr_t)__tc_stack_top;
    uint64_t i;
    if (out_frame != NULL) {
        *out_frame = NULL;
    }
    if (region_contains(address, __tc_static_region, __tc_static_region_end)) {
        *out_region = TC_REG_STATIC;
        return 1;
    }
    for (i = tc_frame_count; i > 0; i--) {
        TcRtFrame *frame = &tc_frames[i - 1u];
        if (address >= frame->base &&
            address - frame->base < frame->size) {
            *out_region = TC_REG_FRAME;
            if (out_frame != NULL) {
                *out_frame = frame;
            }
            return 1;
        }
    }
    if (region_contains(address, __tc_code_start, __tc_code_end)) {
        *out_region = TC_REG_CODE;
        return 1;
    }
    if ((uintptr_t)address >= stack_bottom &&
        (uintptr_t)address < stack_top) {
        *out_region = TC_REG_FRAME;
        return 1;
    }
    if (address >= __tc_rt_device_base &&
        address - __tc_rt_device_base < __tc_rt_device_size) {
        *out_region = TC_REG_DEVICE;
        return 1;
    }
    if (address >= __tc_rt_heap_base &&
        address - __tc_rt_heap_base < __tc_rt_heap_size) {
        *out_region = TC_REG_HEAP;
        return 1;
    }
    return 0;
}

/* Find the best object for an address.  A live object containing the address
 * wins over an object that merely ends at it, then the requested element type,
 * then the innermost object.  This mirrors the reference VM and keeps a byte
 * access into a containing struct's padding legal even when a preceding
 * field's one-past address coincides with it. */
static int object_lookup(uint64_t address, uint32_t prefer_type,
                         int allow_one_past, int *out_one_past,
                         uint64_t *out_start, uint64_t *out_size,
                         int *out_writable) {
    const TcRtObjectDesc *best_desc = NULL;
    int best_live = 0;
    int best_type = 0;
    uint64_t best_start = 0;
    uint64_t best_size = 0;
    int best_past = 0;
    int best_writable = 0;
    uint64_t i;
    if (out_one_past != NULL) {
        *out_one_past = 0;
    }
    for (i = 0; i <= tc_frame_count; i++) {
        const TcRtObjectDesc *descs;
        uint64_t count;
        uint64_t base;
        uint64_t k;
        if (i == tc_frame_count) {
            descs = __tc_static_objects;
            count = __tc_static_object_count;
            base = (uintptr_t)__tc_static_region;
        } else {
            descs = tc_frames[tc_frame_count - 1u - i].objects;
            count = tc_frames[tc_frame_count - 1u - i].count;
            base = tc_frames[tc_frame_count - 1u - i].base;
        }
        for (k = 0; k < count; k++) {
            const TcRtObjectDesc *desc = &descs[k];
            uint64_t start = base + desc->offset;
            int live = desc->size != 0 && address >= start &&
                       address - start < desc->size;
            int one_past = allow_one_past && desc->size != 0 &&
                           address == start + desc->size;
            int type_match;
            int better;
            if (!live && !one_past) {
                continue;
            }
            type_match = desc->type_id == prefer_type;
            better = best_desc == NULL;
            if (!better && live != best_live) {
                better = live > best_live;
            } else if (!better && type_match != best_type) {
                better = type_match > best_type;
            } else if (!better && start != best_start) {
                better = start > best_start;
            } else if (!better && desc->size != best_size) {
                better = desc->size < best_size;
            }
            if (better) {
                best_desc = desc;
                best_live = live;
                best_type = type_match;
                best_start = start;
                best_size = desc->size;
                best_past = one_past;
                best_writable =
                    (desc->flags & TC_RT_OBJECT_WRITABLE) != 0;
            }
        }
    }
    if (best_desc == NULL) {
        return 0;
    }
    *out_start = best_start;
    *out_size = best_size;
    *out_writable = best_writable;
    if (out_one_past != NULL) {
        *out_one_past = best_past;
    }
    return 1;
}

/* Best object whose range fully contains an access, or (when none) an object
 * whose one-past address matches.  Containment wins so padding and
 * cross-field accesses inside a containing struct are validated against the
 * containing object, matching the reference VM. */
static int access_object(uint64_t address, uint64_t size, uint32_t type_id,
                         uint64_t *out_start, uint64_t *out_size,
                         int *out_writable) {
    const TcRtObjectDesc *best_desc = NULL;
    int best_contained = 0;
    int best_type = 0;
    uint64_t best_start = 0;
    uint64_t best_size = 0;
    int best_writable = 0;
    uint64_t i;
    for (i = 0; i <= tc_frame_count; i++) {
        const TcRtObjectDesc *descs;
        uint64_t count;
        uint64_t base;
        uint64_t k;
        if (i == tc_frame_count) {
            descs = __tc_static_objects;
            count = __tc_static_object_count;
            base = (uintptr_t)__tc_static_region;
        } else {
            descs = tc_frames[tc_frame_count - 1u - i].objects;
            count = tc_frames[tc_frame_count - 1u - i].count;
            base = tc_frames[tc_frame_count - 1u - i].base;
        }
        for (k = 0; k < count; k++) {
            const TcRtObjectDesc *desc = &descs[k];
            uint64_t start = base + desc->offset;
            uint64_t relative;
            int contained;
            int one_past;
            int type_match;
            int better;
            if (address < start) {
                continue;
            }
            relative = address - start;
            if (relative > desc->size) {
                continue;
            }
            contained = size <= desc->size - relative;
            one_past = address == start + desc->size;
            if (!contained && !one_past) {
                continue;
            }
            type_match = desc->type_id == type_id;
            better = best_desc == NULL;
            if (!better && contained != best_contained) {
                better = contained > best_contained;
            } else if (!better && type_match != best_type) {
                better = type_match > best_type;
            } else if (!better && start != best_start) {
                better = start > best_start;
            } else if (!better && desc->size != best_size) {
                better = desc->size < best_size;
            }
            if (better) {
                best_desc = desc;
                best_contained = contained;
                best_type = type_match;
                best_start = start;
                best_size = desc->size;
                best_writable =
                    (desc->flags & TC_RT_OBJECT_WRITABLE) != 0;
            }
        }
    }
    if (best_desc == NULL || !best_contained) {
        return 0;
    }
    *out_start = best_start;
    *out_size = best_size;
    *out_writable = best_writable;
    return 1;
}

/* Any object whose live/one-past interval keeps a pointer-arithmetic result
 * inside it.  Pointer provenance is not tracked, so the result is accepted
 * when any candidate interval contains it; this never rejects a defined
 * operation because the address also belongs to an adjacent object. */
static int ptr_bounds_ok(uint64_t pointer, uint64_t result, uint64_t count,
                         int add) {
    uint64_t i;
    for (i = 0; i <= tc_frame_count; i++) {
        const TcRtObjectDesc *descs;
        uint64_t count_desc;
        uint64_t base;
        uint64_t k;
        if (i == tc_frame_count) {
            descs = __tc_static_objects;
            count_desc = __tc_static_object_count;
            base = (uintptr_t)__tc_static_region;
        } else {
            descs = tc_frames[tc_frame_count - 1u - i].objects;
            count_desc = tc_frames[tc_frame_count - 1u - i].count;
            base = tc_frames[tc_frame_count - 1u - i].base;
        }
        for (k = 0; k < count_desc; k++) {
            const TcRtObjectDesc *desc = &descs[k];
            uint64_t start = base + desc->offset;
            int live;
            int one_past;
            if (pointer < start) {
                continue;
            }
            live = pointer - start < desc->size;
            one_past = pointer == start + desc->size;
            if (!live && !one_past) {
                continue;
            }
            if (one_past && add != 0 && count != 0) {
                continue;
            }
            if (result >= start && result <= start + desc->size) {
                return 1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Checked-build defined-byte tracking.                                      */
/* ------------------------------------------------------------------------- */

static int shadow_range(uint64_t address, uint8_t **out_shadow,
                        uint64_t *out_index) {
    TcRegionKind region;
    TcRtFrame *frame = NULL;
    if (!checked_build()) {
        return 0;
    }
    if (!locate_address(address, &region, &frame)) {
        return 0;
    }
    if (region == TC_REG_STATIC) {
        *out_shadow = __tc_static_shadow;
        *out_index = address - (uintptr_t)__tc_static_region;
        return 1;
    }
    if (region == TC_REG_FRAME && frame != NULL && frame->shadow != NULL) {
        uint64_t index = address - frame->base;
        if (index >= frame->shadow_size * 8u) {
            return 0;
        }
        *out_shadow = frame->shadow;
        *out_index = index;
        return 1;
    }
    return 0;
}

static void shadow_mark(uint64_t address, uint64_t size) {
    uint8_t *shadow = NULL;
    uint64_t index = 0;
    uint64_t i;
    if (!checked_build() || size == 0) {
        return;
    }
    if (!shadow_range(address, &shadow, &index)) {
        return;
    }
    for (i = 0; i < size; i++) {
        uint64_t bit = index + i;
        shadow[bit >> 3] = (uint8_t)(shadow[bit >> 3] | (1u << (bit & 7u)));
    }
}

static int shadow_defined(uint64_t address, uint64_t size) {
    uint8_t *shadow = NULL;
    uint64_t index = 0;
    uint64_t i;
    if (!checked_build() || size == 0) {
        return 1;
    }
    if (!shadow_range(address, &shadow, &index)) {
        return 1;
    }
    for (i = 0; i < size; i++) {
        uint64_t bit = index + i;
        if ((shadow[bit >> 3] & (1u << (bit & 7u))) == 0) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Frame registry and the static initializer fact.                           */
/* ------------------------------------------------------------------------- */

void tc_rt_frame_enter(const TcRtObjectDesc *objects, size_t count,
                       uintptr_t base, size_t size, uint8_t *shadow,
                       size_t shadow_size) {
    TcRtFrame *frame;
    if (tc_frame_count >= TC_RT_MAX_FRAMES) {
        tc_rt_trap((uint32_t)TC_EK_STACK_OVERFLOW, 0, 0, "<runtime>");
    }
    frame = &tc_frames[tc_frame_count++];
    frame->objects = objects;
    frame->count = (uint64_t)count;
    frame->base = (uint64_t)base;
    frame->size = size == 0 ? 1u : (uint64_t)size;
    frame->shadow = shadow;
    frame->shadow_size = (uint64_t)shadow_size;
    if (checked_build() && shadow != NULL && shadow_size != 0) {
        tc_rt_memset(shadow, 0, shadow_size);
    }
}

void tc_rt_frame_leave(void) {
    if (tc_frame_count != 0) {
        tc_frame_count--;
    }
}

void tc_rt_program_init(void) {
    uint64_t i;
    if (!checked_build()) {
        return;
    }
    for (i = 0; i < __tc_static_object_count; i++) {
        const TcRtObjectDesc *desc = &__tc_static_objects[i];
        if ((desc->flags & TC_RT_OBJECT_HAS_INIT) != 0) {
            shadow_mark((uintptr_t)__tc_static_region + desc->offset,
                        desc->size);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Memory access.                                                            */
/* ------------------------------------------------------------------------- */

__attribute__((noreturn)) static void span_trap(TcErrorKind kind,
                                                uint32_t line,
                                                uint32_t column,
                                                const char *file) {
    tc_rt_trap((uint32_t)kind, line, column, file);
}

/* The four environment device policies (standard §6.7/design §8). */
static int device_allows(uint64_t policy, int write, int volatile_access) {
    switch (policy) {
    case TC_DEV_REJECT:
        return 0;
    case TC_DEV_READ:
        return !write;
    case TC_DEV_READWRITE:
        return 1;
    case TC_DEV_VOLATILE:
        return volatile_access != 0;
    default:
        return 0;
    }
}

static uint64_t check_access(uint64_t address, uint64_t size,
                             uint32_t type_id, int write, int volatile_access,
                             uint32_t line, uint32_t column,
                             const char *file) {
    TcRegionKind region;
    TcRtFrame *frame = NULL;
    uint64_t start = 0;
    uint64_t object_size = 0;
    int writable = 0;
    int found;
    if (address == 0) {
        span_trap(TC_EK_NULL_POINTER, line, column, file);
    }
    if (!locate_address(address, &region, &frame)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (region == TC_REG_DEVICE) {
        uint64_t relative = address - __tc_rt_device_base;
        if (relative > __tc_rt_device_size ||
            size > __tc_rt_device_size - relative ||
            !device_allows(__tc_rt_device_policy, write, volatile_access)) {
            span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
        }
        return address;
    }
    if (region == TC_REG_CODE || region == TC_REG_HEAP) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (region == TC_REG_FRAME && frame == NULL) {
        /* Inside the stack region but no live frame covers the address. */
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    (void)volatile_access;
    if (address > UINT64_MAX - size) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    found = access_object(address, size, type_id, &start, &object_size,
                          &writable);
    if (found) {
        if (write && !writable) {
            span_trap(TC_EK_WRITE_PROTECTED, line, column, file);
        }
    } else if (checked_build()) {
        /* One-past dereference or an access outside every object is UB; only
         * a checked build traps. */
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (checked_build() && !write && !shadow_defined(address, size)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    return address;
}

/* A device access must be one target-sized transaction, not a byte-wise copy:
 * peripherals have side effects per register access. */
static void device_load(uint64_t dst, uint64_t address, uint64_t size) {
    switch (size) {
    case 1u:
        *(uint8_t *)(uintptr_t)dst = *(volatile uint8_t *)(uintptr_t)address;
        break;
    case 2u:
        *(uint16_t *)(uintptr_t)dst =
            *(volatile uint16_t *)(uintptr_t)address;
        break;
    case 4u:
        *(uint32_t *)(uintptr_t)dst =
            *(volatile uint32_t *)(uintptr_t)address;
        break;
    case 8u:
        *(uint64_t *)(uintptr_t)dst =
            *(volatile uint64_t *)(uintptr_t)address;
        break;
    default:
        break;
    }
}

static void device_store(uint64_t address, uint64_t src, uint64_t size) {
    switch (size) {
    case 1u:
        *(volatile uint8_t *)(uintptr_t)address =
            *(const uint8_t *)(uintptr_t)src;
        break;
    case 2u:
        *(volatile uint16_t *)(uintptr_t)address =
            *(const uint16_t *)(uintptr_t)src;
        break;
    case 4u:
        *(volatile uint32_t *)(uintptr_t)address =
            *(const uint32_t *)(uintptr_t)src;
        break;
    case 8u:
        *(volatile uint64_t *)(uintptr_t)address =
            *(const uint64_t *)(uintptr_t)src;
        break;
    default:
        break;
    }
}

void tc_rt_load(uintptr_t dst, uintptr_t address, size_t size,
                uint32_t flags, uint32_t type_id, uint32_t line,
                uint32_t column, const char *file) {
    uint64_t pointer = check_access((uint64_t)address, (uint64_t)size, type_id,
                                    0, (flags & TC_RT_ACCESS_VOLATILE) != 0,
                                    line, column, file);
    TcRegionKind region;
    if (locate_address(pointer, &region, NULL) && region == TC_REG_DEVICE) {
        device_load((uint64_t)dst, pointer, (uint64_t)size);
    } else {
        tc_rt_memcpy((void *)(uintptr_t)dst, (const void *)(uintptr_t)pointer,
                     size);
    }
}

void tc_rt_store(uintptr_t address, uintptr_t src, size_t size,
                 uint32_t flags, uint32_t type_id, uint32_t line,
                 uint32_t column, const char *file) {
    uint64_t pointer = check_access((uint64_t)address, (uint64_t)size, type_id,
                                    1, (flags & TC_RT_ACCESS_VOLATILE) != 0,
                                    line, column, file);
    TcRegionKind region;
    if (locate_address(pointer, &region, NULL) && region == TC_REG_DEVICE) {
        device_store(pointer, (uint64_t)src, (uint64_t)size);
        return;
    }
    tc_rt_memcpy((void *)(uintptr_t)pointer, (const void *)(uintptr_t)src,
                 size);
    shadow_mark(pointer, (uint64_t)size);
}

void tc_rt_define(uintptr_t address, size_t size) {
    shadow_mark((uint64_t)address, (uint64_t)size);
}

void tc_rt_memmove(uintptr_t dst, uintptr_t src, uintptr_t count,
                   size_t element_size, uint32_t type_id, uint32_t line,
                   uint32_t column, const char *file) {
    uint64_t bytes;
    int dst_past = 0;
    int src_past = 0;
    uint64_t dst_start = 0;
    uint64_t src_start = 0;
    uint64_t dst_size = 0;
    uint64_t src_size = 0;
    int dst_writable = 0;
    int src_writable = 0;
    int dst_found;
    int src_found;
    if (dst == 0 || src == 0) {
        span_trap(TC_EK_NULL_POINTER, line, column, file);
    }
    if (count != 0 && element_size != 0 &&
        (uint64_t)count > UINT64_MAX / (uint64_t)element_size) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    bytes = (uint64_t)count * (uint64_t)element_size;
    if (bytes == 0) {
        dst_found = object_lookup((uint64_t)dst, type_id, 1, &dst_past,
                                  &dst_start, &dst_size, &dst_writable);
        src_found = object_lookup((uint64_t)src, type_id, 1, &src_past,
                                  &src_start, &src_size, &src_writable);
        (void)dst_start;
        (void)src_start;
        (void)dst_size;
        (void)src_size;
        (void)dst_writable;
        (void)src_writable;
        if ((!dst_found || dst_past || !src_found || src_past) &&
            checked_build()) {
            span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
        }
        return;
    }
    (void)check_access((uint64_t)dst, bytes, type_id, 1, 0, line, column,
                       file);
    (void)check_access((uint64_t)src, bytes, type_id, 0, 0, line, column,
                       file);
    if (checked_build() && !shadow_defined((uint64_t)src, bytes)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    memmove((void *)(uintptr_t)dst, (const void *)(uintptr_t)src,
            (size_t)bytes);
    shadow_mark((uint64_t)dst, bytes);
}

uintptr_t tc_rt_ptradd(uintptr_t pointer, uintptr_t count,
                       size_t element_size, uint32_t add, uint32_t type_id,
                       uint32_t line, uint32_t column, const char *file) {
    TcRegionKind region;
    uint64_t delta;
    uint64_t result;
    uint64_t limit =
        (TC_RT_W == 32u) ? UINT64_C(0x100000000) : (UINT64_MAX);
    (void)type_id;
    if (pointer == 0) {
        span_trap(TC_EK_NULL_POINTER, line, column, file);
    }
    if (element_size != 0 &&
        (uint64_t)count > UINT64_MAX / (uint64_t)element_size) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    delta = (uint64_t)count * (uint64_t)element_size;
    if (!locate_address((uint64_t)pointer, &region, NULL)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (add != 0) {
        if (delta > limit - 1u - (uint64_t)pointer) {
            span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
        }
        result = (uint64_t)pointer + delta;
    } else {
        if (delta > (uint64_t)pointer) {
            span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
        }
        result = (uint64_t)pointer - delta;
    }
    if (region == TC_REG_DEVICE) {
        uint64_t relative;
        if (__tc_rt_device_policy == TC_DEV_REJECT ||
            result < __tc_rt_device_base) {
            span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
        }
        relative = result - __tc_rt_device_base;
        if (relative > __tc_rt_device_size) {
            span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
        }
        return (uintptr_t)result;
    }
    if (region == TC_REG_CODE || region == TC_REG_HEAP) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    {
        int in_bounds = ptr_bounds_ok((uint64_t)pointer, result,
                                      (uint64_t)count, (int)add);
        if (!in_bounds) {
            if (checked_build()) {
                span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
            }
            if (region == TC_REG_FRAME) {
                /* The frame region is bounded by the linker script. */
            }
        }
    }
    return (uintptr_t)result;
}

uint64_t tc_rt_ptrdiff(uintptr_t a, uintptr_t b, uint32_t line,
                       uint32_t column, const char *file) {
    uint64_t max_value = TC_RT_W == 32u ? (uint64_t)INT32_MAX
                                        : (uint64_t)INT64_MAX;
    uint64_t min_magnitude = TC_RT_W == 32u
                                 ? (UINT64_C(1) << 31)
                                 : (UINT64_C(1) << 63);
    uint64_t result;
    if (a >= b) {
        uint64_t difference = (uint64_t)a - (uint64_t)b;
        if (difference > max_value) {
            span_trap(TC_EK_PTRDIFF_OVERFLOW, line, column, file);
        }
        result = difference;
    } else {
        uint64_t difference = (uint64_t)b - (uint64_t)a;
        if (difference > min_magnitude) {
            span_trap(TC_EK_PTRDIFF_OVERFLOW, line, column, file);
        }
        result = UINT64_C(0) - difference;
    }
    if (TC_RT_W == 32u) {
        result &= UINT64_C(0xffffffff);
    }
    return result;
}

/* ------------------------------------------------------------------------- */
/* Scalar semantic helpers.                                                  */
/* ------------------------------------------------------------------------- */

static TcType type_from_tag(uint32_t tag) {
    TcType type;
    tc_rt_memset(&type, 0, sizeof(type));
    type.tag = (TcTypeTag)tag;
    return type;
}

uint64_t tc_rt_float_binop(uint32_t op, uint32_t mode, uint32_t tag,
                           uint64_t a, uint64_t b, uint32_t line,
                           uint32_t column, const char *file) {
    TcType type = type_from_tag((uint32_t)tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_float_binop(TC_RT_W, &type, (TcOpcode)op, (TcOpMode)mode, a, b,
                            &out, &trap)) {
        if (trap == (TcErrorKind)0) {
            span_trap(TC_EK_TRAP, line, column, file);
        }
        span_trap(trap, line, column, file);
    }
    return out;
}

uint64_t tc_rt_float_unop(uint32_t op, uint32_t tag, uint64_t a) {
    TcType type = type_from_tag((uint32_t)tag);
    uint64_t out = 0;
    if (!tc_sem_float_unop(TC_RT_W, &type, (TcOpcode)op, a, &out)) {
        tc_rt_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_rt_float_compare(uint32_t op, uint32_t tag, uint64_t a,
                             uint64_t b) {
    TcType type = type_from_tag((uint32_t)tag);
    uint64_t out = 0;
    if (!tc_sem_compare(TC_RT_W, &type, (TcOpcode)op, a, b, &out)) {
        tc_rt_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_rt_cast(uint32_t src_tag, uint32_t dst_tag, uint64_t bits,
                    uint32_t line, uint32_t column, const char *file) {
    TcType source = type_from_tag((uint32_t)src_tag);
    TcType destination = type_from_tag((uint32_t)dst_tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_cast(TC_RT_W, &source, &destination, bits, &out, &trap)) {
        if (trap == (TcErrorKind)0) {
            span_trap(TC_EK_TRAP, line, column, file);
        }
        span_trap(trap, line, column, file);
    }
    return out;
}

uint64_t tc_rt_trunc(uint32_t src_tag, uint32_t dst_tag, uint64_t bits) {
    TcType source = type_from_tag((uint32_t)src_tag);
    TcType destination = type_from_tag((uint32_t)dst_tag);
    uint64_t out = 0;
    if (!tc_sem_trunc(TC_RT_W, &source, &destination, bits, &out)) {
        tc_rt_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_rt_bitcast(uint32_t dst_tag, uint64_t bits) {
    TcType destination = type_from_tag((uint32_t)dst_tag);
    uint64_t out = 0;
    if (!tc_sem_bitcast(TC_RT_W, &destination, bits, &out)) {
        tc_rt_trap((uint32_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Deterministic scalar I/O.                                                 */
/* ------------------------------------------------------------------------- */

int tc_rt_write_scalar(uint32_t tag, uint32_t newline, uint64_t bits) {
    char text[512];
    TcType type = type_from_tag((uint32_t)tag);
    size_t length = 0;
    if (!tc_format_scalar(TC_RT_W, &type, bits, text, sizeof(text))) {
        return 1;
    }
    while (text[length] != '\0') {
        length++;
    }
    if (newline != 0) {
        if (length + 2u > sizeof(text)) {
            return 1;
        }
        text[length++] = '\n';
        text[length] = '\0';
    }
    tc_rt_platform_stdout(text, length);
    return 0;
}

/* Input image for the QEMU platform instance: an 8-byte little-endian length
 * followed by the bytes, placed by the test loader.  Reads are deliberately
 * short (at most four bytes per source call) so the standard §11.3 short-read
 * path is exercised.  A zero image is an empty stream (EOF). */
#define TC_INPUT_IMAGE_MAX UINT64_C(0x00100000)

static int image_loaded = 0;
static const uint8_t *image_data = NULL;
static size_t image_len = 0;
static size_t image_pos = 0;
static TcInput tc_input_state;
static int tc_input_ready = 0;

static void image_load(void) {
    uint64_t n = 0;
    unsigned i;
    if (image_loaded) {
        return;
    }
    for (i = 0; i < 8u; i++) {
        n |= (uint64_t)__tc_input_image[i] << (8u * i);
    }
    if (n > TC_INPUT_IMAGE_MAX) {
        n = 0;
    }
    image_data = __tc_input_image + 8;
    image_len = (size_t)n;
    image_pos = 0;
    image_loaded = 1;
}

static int image_read(void *ctx, uint8_t *buffer, size_t cap, size_t *n,
                      TcIoStatus *status) {
    size_t take;
    size_t i;
    (void)ctx;
    image_load();
    if (image_pos >= image_len) {
        *n = 0;
        *status = TC_IO_EOF;
        return 0;
    }
    take = image_len - image_pos;
    if (take > cap) {
        take = cap;
    }
    if (take > 4u) {
        take = 4u;
    }
    for (i = 0; i < take; i++) {
        buffer[i] = image_data[image_pos + i];
    }
    image_pos += take;
    *n = take;
    *status = TC_IO_OK;
    return 0;
}

static const TcIoSrc tc_image_source = { image_read, NULL };

int tc_rt_read_scalar(uint32_t tag, uint64_t *out) {
    TcType type = type_from_tag((uint32_t)tag);
    uint64_t bits = 0;
    if (!tc_input_ready) {
        tc_input_init(&tc_input_state, &tc_image_source, tc_allocator_libc());
        tc_input_ready = 1;
    }
    if (tc_input_state.token == NULL) {
        return 1;
    }
    if (tc_input_read_scalar(&tc_input_state, TC_RT_W, &type, &bits) !=
        TC_INPUT_OK) {
        return 1;
    }
    *out = bits;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Freestanding memory helpers used by the compiler and the C runtime.       */
/* ------------------------------------------------------------------------- */

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    if (d == s || n == 0) {
        return dst;
    }
    if ((uintptr_t)d < (uintptr_t)s) {
        for (i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (i = n; i != 0; i--) {
            d[i - 1u] = s[i - 1u];
        }
    }
    return dst;
}

void *memset(void *dst, int value, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = (unsigned char)value;
    }
    return dst;
}
