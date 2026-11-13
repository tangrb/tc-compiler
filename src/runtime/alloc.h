/* TC 0.0.46 contract: allocator seam (design §7.2).
 *
 * Sizes are interpreted in the target address space; a 32-bit profile must
 * never leak the host 64-bit size_t into target object bounds.  The libc
 * instance below is for the hosted compiler/VM only; freestanding builds use
 * an arena/bump instance supplied by the platform profile.
 */
#ifndef TC_RUNTIME_ALLOC_H
#define TC_RUNTIME_ALLOC_H

#include <stddef.h>

typedef struct TcAllocator {
    void *(*alloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *ptr, size_t size);
    void (*free)(void *ctx, void *ptr);
    void *ctx;
} TcAllocator;

/* Process-wide hosted allocator instance.  Never used by a freestanding
 * profile's runtime. */
const TcAllocator *tc_allocator_libc(void);

/* A well-formed allocator has all three callbacks non-NULL. */
int tc_allocator_is_valid(const TcAllocator *alloc);

#endif /* TC_RUNTIME_ALLOC_H */
