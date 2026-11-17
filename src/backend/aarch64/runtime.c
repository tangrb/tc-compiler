/* TC 0.0.46 freestanding AArch64 runtime (design §7.2/§10.3/§11).
 *
 * The runtime is linked into the AArch64 ELF produced by the backend.  It
 * provides:
 *   - the platform seams: a semihosting console for program stdout/stdin, the
 *     first-trap diagnostic and process termination, and the abstract call
 *     depth counter;
 *   - object-table driven memory accesses: mandatory null/write-protected/
 *     code/device traps in every build, plus checked-build object bounds and
 *     defined-byte tracking for out-of-object, one-past, dangling and
 *     uninitialized accesses;
 *   - the shared W-aware semantic core for floating arithmetic, conversions,
 *     deterministic scalar formatting and token input;
 *   - the freestanding allocator used by the runtime's own token/decimal
 *     buffers (never exposed to the language).
 *
 * It is freestanding: no hosted headers, no allocation from the language, no
 * locale.  Program stdout uses semihosting; the PL011 UART is a separate
 * trap/exit-record channel so it never mixes with the program's stdout.
 */
#include <stddef.h>
#include <stdint.h>

#include "backend/aarch64/rt_abi.h"
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

#define TC_AARCH64_PL011_BASE UINT64_C(0x09000000)

#define TC_SEMIHOST_SYS_WRITE0 0x04
#define TC_SEMIHOST_SYS_EXIT 0x18
#define TC_SEMIHOST_SYS_EXIT_EXTENDED 0x20
#define TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT 0x20000u

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);

#define TC_A64_MAX_FRAMES 600u
#define TC_A64_ALLOC_ARENA_SIZE (8u * 1024u * 1024u)

/* Abstract call depth (standard §1.2/design §10.3).  Defined in .bss so the
 * loader zero-initializes it. */
unsigned long __tc_call_depth = 0;

/* ------------------------------------------------------------------------- */
/* Emitted platform/region facts.                                            */
/* ------------------------------------------------------------------------- */

extern const uint64_t __tc_a64_device_base;
extern const uint64_t __tc_a64_device_size;
extern const uint64_t __tc_a64_device_policy;
extern const uint64_t __tc_a64_heap_base;
extern const uint64_t __tc_a64_heap_size;
extern const uint64_t __tc_a64_checked;
extern const uint64_t __tc_a64_normal_status;
extern const uint64_t __tc_a64_trap_status;

extern const uint8_t __tc_static_region[];
extern const uint8_t __tc_static_region_end[];
extern uint8_t __tc_static_shadow[];
extern const TcA64ObjectDesc __tc_static_objects[];
extern const uint64_t __tc_static_object_count;

extern const uint8_t __tc_code_start[];
extern const uint8_t __tc_code_end[];
extern const uint8_t __tc_stack_bottom[];
extern const uint8_t __tc_stack_top[];

extern const uint8_t __tc_input_image[];

/* ------------------------------------------------------------------------- */
/* Trap and exit.                                                            */
/* ------------------------------------------------------------------------- */

static long semihosting_call(unsigned long op, unsigned long arg) {
    register unsigned long x0 __asm__("x0") = op;
    register unsigned long x1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return (long)x0;
}

static void uart_init(void) {
    volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)
        TC_AARCH64_PL011_BASE;
    base[0x30u / 4u] = 0x301u; /* UARTEN | TXE | RXE */
    base[0x44u / 4u] = 0x7ffu; /* clear all interrupts */
}

static void uart_putc(char c) {
    volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)
        TC_AARCH64_PL011_BASE;
    while ((base[0x18u / 4u] & (1u << 5)) != 0) {
        /* wait while TX FIFO is full */
    }
    base[0] = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *text) {
    while (*text != '\0') {
        uart_putc(*text++);
    }
}

static void uart_put_uint64(unsigned long value) {
    char digits[24];
    size_t n = 0;
    if (value == 0) {
        uart_putc('0');
        return;
    }
    while (value != 0) {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n != 0) {
        uart_putc(digits[--n]);
    }
}

static const char *aarch64_trap_error_symbol(unsigned code) {
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

__attribute__((noreturn)) static void semihosting_exit(unsigned status) {
    struct {
        uint32_t reason;
        uint32_t exit_code;
    } block;
    block.reason = TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT;
    block.exit_code = status;
    (void)semihosting_call(TC_SEMIHOST_SYS_EXIT_EXTENDED,
                           (unsigned long)(uintptr_t)&block);
    (void)semihosting_call(TC_SEMIHOST_SYS_EXIT,
                           (unsigned long)(TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT +
                                           status));
    for (;;) {
    }
}

__attribute__((noreturn)) void tc_a64_runtime_exit(uint64_t status) {
    uart_init();
    uart_puts("TC-EXIT ");
    uart_put_uint64((unsigned long)status);
    uart_puts(" none\n");
    semihosting_exit((unsigned)status);
}

__attribute__((noreturn)) void tc_a64_trap(uint64_t code, uint64_t line,
                                           uint64_t column, const char *file) {
    uint64_t profile_status = __tc_a64_trap_status;
    uart_init();
    uart_puts("error[RT]: ");
    uart_puts(aarch64_trap_error_symbol((unsigned)code));
    uart_puts(" at ");
    uart_puts(file != NULL ? file : "<source>");
    uart_putc(':');
    uart_put_uint64((unsigned long)line);
    uart_putc(':');
    uart_put_uint64((unsigned long)column);
    uart_putc('\n');
    uart_puts("TC-EXIT ");
    uart_put_uint64((unsigned long)profile_status);
    uart_putc(' ');
    uart_puts(aarch64_trap_error_symbol((unsigned)code));
    uart_putc('\n');
    semihosting_exit((unsigned)profile_status);
}

/* ------------------------------------------------------------------------- */
/* Freestanding allocator (runtime-private buffers only).                    */
/* ------------------------------------------------------------------------- */

typedef struct TcA64AllocBlock {
    size_t size;
    int free;
    struct TcA64AllocBlock *next;
} TcA64AllocBlock;

static unsigned char tc_alloc_arena[TC_A64_ALLOC_ARENA_SIZE]
    __attribute__((aligned(16)));

static TcA64AllocBlock *tc_alloc_head = NULL;

static size_t alloc_align(size_t value) {
    return (value + 15u) & ~(size_t)15u;
}

static void *runtime_alloc(size_t size) {
    TcA64AllocBlock *block = tc_alloc_head;
    size_t need = alloc_align(size == 0 ? 1u : size);
    while (block != NULL) {
        if (block->free && block->size >= need) {
            if (block->size >= need + sizeof(TcA64AllocBlock) + 16u) {
                TcA64AllocBlock *rest =
                    (TcA64AllocBlock *)((unsigned char *)block +
                                        sizeof(TcA64AllocBlock) + need);
                rest->size = block->size - need - sizeof(TcA64AllocBlock);
                rest->free = 1;
                rest->next = block->next;
                block->next = rest;
                block->size = need;
            }
            block->free = 0;
            return (unsigned char *)block + sizeof(TcA64AllocBlock);
        }
        block = block->next;
    }
    if (tc_alloc_head == NULL) {
        tc_alloc_head = (TcA64AllocBlock *)(void *)tc_alloc_arena;
        tc_alloc_head->size = TC_A64_ALLOC_ARENA_SIZE - sizeof(TcA64AllocBlock);
        tc_alloc_head->free = 1;
        tc_alloc_head->next = NULL;
        return runtime_alloc(size);
    }
    return NULL;
}

static void runtime_free(void *ptr) {
    TcA64AllocBlock *block;
    if (ptr == NULL) {
        return;
    }
    block = (TcA64AllocBlock *)((unsigned char *)ptr -
                                sizeof(TcA64AllocBlock));
    block->free = 1;
}

static void *runtime_realloc(void *ptr, size_t size) {
    TcA64AllocBlock *block;
    void *fresh;
    size_t copy;
    if (ptr == NULL) {
        return runtime_alloc(size);
    }
    if (size == 0) {
        runtime_free(ptr);
        return NULL;
    }
    block = (TcA64AllocBlock *)((unsigned char *)ptr -
                                sizeof(TcA64AllocBlock));
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
    const TcA64ObjectDesc *objects;
    uint64_t count;
    uint64_t base;
    uint64_t size;
    uint8_t *shadow;
    uint64_t shadow_size;
} TcA64Frame;

static TcA64Frame tc_frames[TC_A64_MAX_FRAMES];
static uint64_t tc_frame_count = 0;

static int checked_build(void) {
    return __tc_a64_checked != 0;
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
                          TcA64Frame **out_frame) {
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
        TcA64Frame *frame = &tc_frames[i - 1u];
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
    if (address >= __tc_a64_device_base &&
        address - __tc_a64_device_base < __tc_a64_device_size) {
        *out_region = TC_REG_DEVICE;
        return 1;
    }
    if (address >= __tc_a64_heap_base &&
        address - __tc_a64_heap_base < __tc_a64_heap_size) {
        *out_region = TC_REG_HEAP;
        return 1;
    }
    return 0;
}

/* Find the best object for an address, mirroring the reference VM: a live
 * object containing the address (or ending at it when `allow_one_past`),
 * preferring the requested element type and then the innermost object. */
static int object_lookup(uint64_t address, uint32_t prefer_type,
                         int allow_one_past, int *out_one_past,
                         uint64_t *out_start, uint64_t *out_size,
                         int *out_writable) {
    int have_best_type = 0;
    int have_best_any = 0;
    uint64_t best_type_start = 0;
    uint64_t best_type_size = 0;
    int best_type_writable = 0;
    int best_type_past = 0;
    uint64_t best_any_start = 0;
    uint64_t best_any_size = 0;
    int best_any_writable = 0;
    int best_any_past = 0;
    uint64_t i;
    if (out_one_past != NULL) {
        *out_one_past = 0;
    }
    for (i = 0; i <= tc_frame_count; i++) {
        const TcA64ObjectDesc *descs;
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
            const TcA64ObjectDesc *desc = &descs[k];
            uint64_t start = base + desc->offset;
            int live = desc->size != 0 && address >= start &&
                       address - start < desc->size;
            int one_past = allow_one_past && desc->size != 0 &&
                           address == start + desc->size;
            int type_match;
            if (!live && !one_past) {
                continue;
            }
            type_match = desc->type_id == prefer_type;
            {
                int use_type = type_match && (!have_best_type ||
                    start > best_type_start ||
                    (start == best_type_start && desc->size < best_type_size) ||
                    (start == best_type_start && desc->size == best_type_size &&
                     live && !one_past));
                int use_any = !have_best_any || start > best_any_start ||
                    (start == best_any_start && desc->size < best_any_size) ||
                    (start == best_any_start && desc->size == best_any_size &&
                     live && !one_past);
                if (type_match && use_type) {
                    have_best_type = 1;
                    best_type_start = start;
                    best_type_size = desc->size;
                    best_type_writable =
                        (desc->flags & TC_A64_OBJECT_WRITABLE) != 0;
                    best_type_past = one_past;
                }
                if (!type_match && use_any) {
                    have_best_any = 1;
                    best_any_start = start;
                    best_any_size = desc->size;
                    best_any_writable =
                        (desc->flags & TC_A64_OBJECT_WRITABLE) != 0;
                    best_any_past = one_past;
                }
            }
        }
    }
    if (have_best_type) {
        *out_start = best_type_start;
        *out_size = best_type_size;
        *out_writable = best_type_writable;
        if (out_one_past != NULL) {
            *out_one_past = best_type_past;
        }
        return 1;
    }
    if (have_best_any) {
        *out_start = best_any_start;
        *out_size = best_any_size;
        *out_writable = best_any_writable;
        if (out_one_past != NULL) {
            *out_one_past = best_any_past;
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Checked-build defined-byte tracking.                                      */
/* ------------------------------------------------------------------------- */

static int shadow_range(uint64_t address, uint8_t **out_shadow,
                        uint64_t *out_index) {
    TcRegionKind region;
    TcA64Frame *frame = NULL;
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

void tc_a64_frame_enter(const TcA64ObjectDesc *objects, uint64_t count,
                        uint64_t base, uint64_t size, uint8_t *shadow,
                        uint64_t shadow_size) {
    TcA64Frame *frame;
    if (tc_frame_count >= TC_A64_MAX_FRAMES) {
        tc_a64_trap((uint64_t)TC_EK_STACK_OVERFLOW, 0, 0, "<runtime>");
    }
    frame = &tc_frames[tc_frame_count++];
    frame->objects = objects;
    frame->count = count;
    frame->base = base;
    frame->size = size == 0 ? 1u : size;
    frame->shadow = shadow;
    frame->shadow_size = shadow_size;
    if (checked_build() && shadow != NULL && shadow_size != 0) {
        tc_rt_memset(shadow, 0, (size_t)shadow_size);
    }
}

void tc_a64_frame_leave(void) {
    if (tc_frame_count != 0) {
        tc_frame_count--;
    }
}

void tc_a64_program_init(void) {
    uint64_t i;
    if (!checked_build()) {
        return;
    }
    for (i = 0; i < __tc_static_object_count; i++) {
        const TcA64ObjectDesc *desc = &__tc_static_objects[i];
        if ((desc->flags & TC_A64_OBJECT_HAS_INIT) != 0) {
            shadow_mark((uintptr_t)__tc_static_region + desc->offset,
                        desc->size);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Memory access.                                                            */
/* ------------------------------------------------------------------------- */

__attribute__((noreturn)) static void span_trap(TcErrorKind kind,
                                                uint64_t line,
                                                uint64_t column,
                                                const char *file) {
    tc_a64_trap((uint64_t)kind, line, column, file);
}

static uint64_t check_access(uint64_t address, uint64_t size,
                             uint32_t type_id, int write, int volatile_access,
                             uint64_t line, uint64_t column, const char *file) {
    TcRegionKind region;
    TcA64Frame *frame = NULL;
    uint64_t start = 0;
    uint64_t object_size = 0;
    int writable = 0;
    int one_past = 0;
    int found;
    if (address == 0) {
        span_trap(TC_EK_NULL_POINTER, line, column, file);
    }
    if (!locate_address(address, &region, &frame)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (region == TC_REG_DEVICE) {
        /* This platform instance only maps TC_DEV_REJECT: any allowed device
         * access would need a freestanding device instance. */
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
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
    found = object_lookup(address, type_id, 1, &one_past, &start,
                          &object_size, &writable);
    if (found && !one_past && address - start <= object_size &&
        size <= object_size - (address - start)) {
        if (write && !writable) {
            span_trap(TC_EK_WRITE_PROTECTED, line, column, file);
        }
    } else if (found && one_past) {
        if (checked_build()) {
            span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
        }
    } else if (checked_build()) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (checked_build() && !write && !shadow_defined(address, size)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    return address;
}

void tc_a64_load(uint64_t dst, uint64_t address, uint64_t size,
                 uint64_t flags, uint64_t type_id, uint64_t line,
                 uint64_t column, const char *file) {
    uint64_t pointer = check_access(address, size, (uint32_t)type_id, 0,
                                    (flags & TC_A64_ACCESS_VOLATILE) != 0,
                                    line, column, file);
    tc_rt_memcpy((void *)(uintptr_t)dst, (const void *)(uintptr_t)pointer,
                 (size_t)size);
}

void tc_a64_store(uint64_t address, uint64_t src, uint64_t size,
                  uint64_t flags, uint64_t type_id, uint64_t line,
                  uint64_t column, const char *file) {
    uint64_t pointer = check_access(address, size, (uint32_t)type_id, 1,
                                    (flags & TC_A64_ACCESS_VOLATILE) != 0,
                                    line, column, file);
    tc_rt_memcpy((void *)(uintptr_t)pointer, (const void *)(uintptr_t)src,
                 (size_t)size);
    shadow_mark(pointer, size);
}

void tc_a64_define(uint64_t address, uint64_t size) {
    shadow_mark(address, size);
}

void tc_a64_memmove(uint64_t dst, uint64_t src, uint64_t count,
                    uint64_t element_size, uint64_t type_id, uint64_t line,
                    uint64_t column, const char *file) {
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
        count > UINT64_MAX / element_size) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    bytes = count * element_size;
    if (bytes == 0) {
        dst_found = object_lookup(dst, (uint32_t)type_id, 1, &dst_past,
                                  &dst_start, &dst_size, &dst_writable);
        src_found = object_lookup(src, (uint32_t)type_id, 1, &src_past,
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
    (void)check_access(dst, bytes, (uint32_t)type_id, 1, 0, line, column,
                       file);
    (void)check_access(src, bytes, (uint32_t)type_id, 0, 0, line, column,
                       file);
    if (checked_build() && !shadow_defined(src, bytes)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    memmove((void *)(uintptr_t)dst, (const void *)(uintptr_t)src,
            (size_t)bytes);
    shadow_mark(dst, bytes);
}

uint64_t tc_a64_ptradd(uint64_t pointer, uint64_t count, uint64_t element_size,
                       uint64_t add, uint64_t type_id, uint64_t line,
                       uint64_t column, const char *file) {
    TcRegionKind region;
    unsigned __int128 delta;
    __int128 result;
    int one_past = 0;
    if (pointer == 0) {
        span_trap(TC_EK_NULL_POINTER, line, column, file);
    }
    delta = (unsigned __int128)count * element_size;
    if (delta > (unsigned __int128)INT64_MAX) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (!locate_address(pointer, &region, NULL)) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    result = (__int128)pointer +
             (add != 0 ? (__int128)delta : -(__int128)delta);
    if (result < 0 || (unsigned __int128)result > (unsigned __int128)UINT64_MAX) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (region == TC_REG_DEVICE) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    if (region == TC_REG_CODE || region == TC_REG_HEAP) {
        span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
    }
    {
        uint64_t start = 0;
        uint64_t object_size = 0;
        int writable = 0;
        int found = object_lookup(pointer, (uint32_t)type_id, 1, &one_past,
                                  &start, &object_size, &writable);
        int in_bounds = 0;
        (void)writable;
        if (found) {
            if (one_past && add != 0 && count != 0) {
                in_bounds = 0;
            } else if ((uint64_t)result >= start &&
                       (uint64_t)result <= start + object_size) {
                in_bounds = 1;
            }
        }
        if (!in_bounds) {
            if (checked_build()) {
                span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
            }
            if (region == TC_REG_FRAME) {
                TcA64Frame *frame = NULL;
                TcRegionKind result_region;
                if (!locate_address((uint64_t)result, &result_region,
                                    &frame) ||
                    result_region != TC_REG_FRAME || frame == NULL) {
                    span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
                }
            } else if (region == TC_REG_STATIC) {
                uintptr_t base = (uintptr_t)__tc_static_region;
                uintptr_t end = (uintptr_t)__tc_static_region_end;
                if ((uintptr_t)result < base || (uintptr_t)result >= end) {
                    span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
                }
            } else {
                span_trap(TC_EK_ADDR_UNAVAILABLE, line, column, file);
            }
        }
    }
    return (uint64_t)result;
}

uint64_t tc_a64_ptrdiff(uint64_t a, uint64_t b, uint64_t line,
                        uint64_t column, const char *file) {
    __int128 diff = (__int128)a - (__int128)b;
    __int128 limit = (__int128)1 << 63;
    if (diff < -limit || diff > limit - 1) {
        span_trap(TC_EK_PTRDIFF_OVERFLOW, line, column, file);
    }
    return (uint64_t)diff;
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

uint64_t tc_a64_float_binop(uint64_t op, uint64_t mode, uint64_t tag,
                            uint64_t a, uint64_t b, uint64_t line,
                            uint64_t column, const char *file) {
    TcType type = type_from_tag((uint32_t)tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_float_binop(64u, &type, (TcOpcode)op, (TcOpMode)mode, a, b,
                            &out, &trap)) {
        if (trap == (TcErrorKind)0) {
            span_trap(TC_EK_TRAP, line, column, file);
        }
        span_trap(trap, line, column, file);
    }
    return out;
}

uint64_t tc_a64_float_unop(uint64_t op, uint64_t tag, uint64_t a) {
    TcType type = type_from_tag((uint32_t)tag);
    uint64_t out = 0;
    if (!tc_sem_float_unop(64u, &type, (TcOpcode)op, a, &out)) {
        tc_a64_trap((uint64_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_a64_float_compare(uint64_t op, uint64_t tag, uint64_t a,
                              uint64_t b) {
    TcType type = type_from_tag((uint32_t)tag);
    uint64_t out = 0;
    if (!tc_sem_compare(64u, &type, (TcOpcode)op, a, b, &out)) {
        tc_a64_trap((uint64_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_a64_cast(uint64_t src_tag, uint64_t dst_tag, uint64_t bits,
                     uint64_t line, uint64_t column, const char *file) {
    TcType source = type_from_tag((uint32_t)src_tag);
    TcType destination = type_from_tag((uint32_t)dst_tag);
    TcErrorKind trap = (TcErrorKind)0;
    uint64_t out = 0;
    if (!tc_sem_cast(64u, &source, &destination, bits, &out, &trap)) {
        if (trap == (TcErrorKind)0) {
            span_trap(TC_EK_TRAP, line, column, file);
        }
        span_trap(trap, line, column, file);
    }
    return out;
}

uint64_t tc_a64_trunc(uint64_t src_tag, uint64_t dst_tag, uint64_t bits) {
    TcType source = type_from_tag((uint32_t)src_tag);
    TcType destination = type_from_tag((uint32_t)dst_tag);
    uint64_t out = 0;
    if (!tc_sem_trunc(64u, &source, &destination, bits, &out)) {
        tc_a64_trap((uint64_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

uint64_t tc_a64_bitcast(uint64_t dst_tag, uint64_t bits) {
    TcType destination = type_from_tag((uint32_t)dst_tag);
    uint64_t out = 0;
    if (!tc_sem_bitcast(64u, &destination, bits, &out)) {
        tc_a64_trap((uint64_t)TC_EK_TRAP, 0, 0, "<runtime>");
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Deterministic scalar I/O.                                                 */
/* ------------------------------------------------------------------------- */

int tc_a64_write_scalar(uint64_t tag, uint64_t bits, uint64_t newline) {
    char text[512];
    TcType type = type_from_tag((uint32_t)tag);
    size_t length = 0;
    if (!tc_format_scalar(64u, &type, bits, text, sizeof(text))) {
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
    (void)semihosting_call(TC_SEMIHOST_SYS_WRITE0,
                           (unsigned long)(uintptr_t)text);
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

int tc_a64_read_scalar(uint64_t tag, uint64_t *out) {
    TcType type = type_from_tag((uint32_t)tag);
    uint64_t bits = 0;
    if (!tc_input_ready) {
        tc_input_init(&tc_input_state, &tc_image_source, tc_allocator_libc());
        tc_input_ready = 1;
    }
    if (tc_input_state.token == NULL) {
        return 1;
    }
    if (tc_input_read_scalar(&tc_input_state, 64u, &type, &bits) !=
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
