/* TC 0.0.46 front-end arena (standard §1.7 step 1, front-end internal).
 *
 * One arena per module/analysis; blocks are allocated from the configured
 * allocator and released together.  The implementation is freestanding-safe so
 * the exact decimal converter can also be linked into the AArch64 runtime.
 */
#include "frontend/input.h"

#include "runtime/rt_memory.h"

struct TcArenaBlock {
    TcArenaBlock *next;
    size_t size;
    size_t used;
    /* payload follows the header, aligned to TC_ARENA_ALIGN */
    unsigned char align_pad;
};

#define TC_ARENA_BLOCK_SIZE 65536u
#define TC_ARENA_ALIGN ((size_t)_Alignof(max_align_t) > 16u ? (size_t)_Alignof(max_align_t) : 16u)

static size_t align_up(size_t value) {
    return (value + (TC_ARENA_ALIGN - 1u)) & ~(size_t)(TC_ARENA_ALIGN - 1u);
}

void tc_arena_init(TcArena *arena, const TcAllocator *alloc) {
    if (arena == NULL) {
        return;
    }
    arena->head = NULL;
    arena->alloc = alloc != NULL ? alloc : tc_allocator_libc();
    arena->total = 0;
}

void tc_arena_destroy(TcArena *arena) {
    TcArenaBlock *block;
    if (arena == NULL) {
        return;
    }
    block = arena->head;
    while (block != NULL) {
        TcArenaBlock *next = block->next;
        arena->alloc->free(arena->alloc->ctx, block);
        block = next;
    }
    arena->head = NULL;
    arena->total = 0;
}

static TcArenaBlock *arena_new_block(TcArena *arena, size_t payload) {
    size_t size = align_up(sizeof(TcArenaBlock));
    size_t bytes;
    TcArenaBlock *block;
    /* The source-size and token-size limits are enforced at input/lexing.
     * Growable token/block arrays legitimately exceed any smaller per-block
     * cap, so only an actual allocation failure is OOM. */
    bytes = size + payload;
    block = (TcArenaBlock *)arena->alloc->alloc(arena->alloc->ctx, bytes);
    if (block == NULL) {
        return NULL;
    }
    block->next = NULL;
    block->size = payload;
    block->used = 0;
    arena->total += payload;
    return block;
}

void *tc_arena_alloc(TcArena *arena, size_t size) {
    TcArenaBlock *block;
    size_t need;
    uint8_t *base;
    void *result;
    if (arena == NULL || size == 0) {
        return NULL;
    }
    need = align_up(size);
    block = arena->head;
    if (block == NULL || block->size - block->used < need) {
        TcArenaBlock *fresh = arena_new_block(arena, need > TC_ARENA_BLOCK_SIZE ? need : TC_ARENA_BLOCK_SIZE);
        if (fresh == NULL) {
            return NULL;
        }
        fresh->next = arena->head;
        arena->head = fresh;
        block = fresh;
    }
    base = (uint8_t *)block + align_up(sizeof(TcArenaBlock));
    result = base + block->used;
    block->used += need;
    return result;
}

void *tc_arena_alloc0(TcArena *arena, size_t size) {
    void *p = tc_arena_alloc(arena, size);
    if (p != NULL) {
        tc_rt_memset(p, 0, size);
    }
    return p;
}

char *tc_arena_strndup(TcArena *arena, const char *bytes, size_t size) {
    char *copy;
    if (bytes == NULL) {
        return NULL;
    }
    if (size > TC_FRONTEND_MAX_TOKEN_BYTES) {
        return NULL;
    }
    copy = (char *)tc_arena_alloc(arena, size + 1u);
    if (copy == NULL) {
        return NULL;
    }
    if (size != 0) {
        tc_rt_memcpy(copy, bytes, size);
    }
    copy[size] = '\0';
    return copy;
}

char *tc_arena_strdup(TcArena *arena, const char *text) {
    if (text == NULL) {
        return NULL;
    }
    return tc_arena_strndup(arena, text, tc_rt_strlen(text));
}

