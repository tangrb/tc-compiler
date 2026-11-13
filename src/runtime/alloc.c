/* TC 0.0.46 contract: libc-backed hosted allocator instance. */
#include "runtime/alloc.h"

#include <stdlib.h>

static void *libc_alloc(void *ctx, size_t size) {
    (void)ctx;
    return malloc(size);
}

static void *libc_realloc(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    return realloc(ptr, size);
}

static void libc_free(void *ctx, void *ptr) {
    (void)ctx;
    free(ptr);
}

static const TcAllocator tc_libc_allocator = {libc_alloc, libc_realloc, libc_free, NULL};

const TcAllocator *tc_allocator_libc(void) {
    return &tc_libc_allocator;
}

int tc_allocator_is_valid(const TcAllocator *alloc) {
    return alloc != NULL && alloc->alloc != NULL && alloc->realloc != NULL &&
           alloc->free != NULL;
}
