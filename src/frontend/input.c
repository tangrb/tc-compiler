/* TC 0.0.46 front-end input layer (S1). */
#include "frontend/input.h"

#include <stdio.h>
#include <string.h>

#include "frontend/input.h"

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
    if (payload > TC_FRONTEND_MAX_SOURCE_BYTES) {
        return NULL;
    }
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
        memset(p, 0, size);
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
        memcpy(copy, bytes, size);
    }
    copy[size] = '\0';
    return copy;
}

char *tc_arena_strdup(TcArena *arena, const char *text) {
    if (text == NULL) {
        return NULL;
    }
    return tc_arena_strndup(arena, text, strlen(text));
}

const char *tc_source_limit_violation(size_t size) {
    if (size > TC_FRONTEND_MAX_SOURCE_BYTES) {
        return "source file exceeds the documented S1 size limit";
    }
    return NULL;
}

TcImplError tc_source_read(const char *path, TcSourceBytes *out) {
    FILE *file;
    long length;
    uint8_t *bytes;
    size_t size;
    size_t got;
    if (out == NULL) {
        return TC_IMPL_INTERNAL;
    }
    out->bytes = NULL;
    out->size = 0;
    if (path == NULL) {
        return TC_IMPL_IO;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return TC_IMPL_IO;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return TC_IMPL_IO;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return TC_IMPL_IO;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return TC_IMPL_IO;
    }
    size = (size_t)length;
    if (tc_source_limit_violation(size) != NULL) {
        fclose(file);
        return TC_IMPL_INTERNAL;
    }
    bytes = (uint8_t *)tc_allocator_libc()->alloc(tc_allocator_libc()->ctx, size == 0 ? 1u : size);
    if (bytes == NULL) {
        fclose(file);
        return TC_IMPL_OOM;
    }
    got = size == 0 ? 0u : fread(bytes, 1u, size, file);
    if (got != size || ferror(file)) {
        tc_allocator_libc()->free(tc_allocator_libc()->ctx, bytes);
        fclose(file);
        return TC_IMPL_IO;
    }
    if (fclose(file) != 0) {
        tc_allocator_libc()->free(tc_allocator_libc()->ctx, bytes);
        return TC_IMPL_IO;
    }
    out->bytes = bytes;
    out->size = size;
    return TC_IMPL_OK;
}

void tc_source_free(TcSourceBytes *source) {
    if (source == NULL || source->bytes == NULL) {
        return;
    }
    tc_allocator_libc()->free(tc_allocator_libc()->ctx, source->bytes);
    source->bytes = NULL;
    source->size = 0;
}
