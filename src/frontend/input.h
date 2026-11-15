/* TC 0.0.46 front-end input layer (S1).
 *
 * Owns:
 *   - the per-module arena used by LEX/SYN structures,
 *   - source file reading (file open failures are implementation/API errors,
 *     standard §1.7 step 1; they are never language diagnostics),
 *   - the documented implementation limit for one source file.
 *
 * The arena allocator may be NULL, which selects tc_allocator_libc().  All
 * allocations live until tc_arena_destroy(); the parser and lexer never call
 * free individually.
 */
#ifndef TC_FRONTEND_INPUT_H
#define TC_FRONTEND_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "diagnostic/diagnostic.h"
#include "runtime/alloc.h"

/* Documented implementation limits (standard §13.5 allows documented
 * implementation limits; they must never be reported as language
 * diagnostics).  Exceeding them is TC_IMPL_INTERNAL with a clear message. */
#define TC_FRONTEND_MAX_SOURCE_BYTES (4u * 1024u * 1024u)
#define TC_FRONTEND_MAX_TOKEN_BYTES 16384u

/* Documented parser nesting limit (types and blocks).  Exceeding it is an
 * implementation-level error, never a language diagnostic. */
#define TC_FRONTEND_MAX_NESTING 256u

typedef struct TcArenaBlock TcArenaBlock;

typedef struct {
    TcArenaBlock *head;
    const TcAllocator *alloc;
    size_t total;
} TcArena;

void tc_arena_init(TcArena *arena, const TcAllocator *alloc);
void tc_arena_destroy(TcArena *arena);
void *tc_arena_alloc(TcArena *arena, size_t size);
void *tc_arena_alloc0(TcArena *arena, size_t size);
char *tc_arena_strndup(TcArena *arena, const char *bytes, size_t size);
char *tc_arena_strdup(TcArena *arena, const char *text);

/* Read a whole source file.  Path is copied by the caller as needed; the
 * returned bytes are owned by the caller and released with tc_source_free().
 * Returns TC_IMPL_OK, TC_IMPL_IO (open/read failure) or TC_IMPL_OOM. */
typedef struct {
    uint8_t *bytes;
    size_t size;
} TcSourceBytes;

TcImplError tc_source_read(const char *path, TcSourceBytes *out);
void tc_source_free(TcSourceBytes *source);

/* Implementation-limit check used by the lexer; returns NULL when the source
 * is within the documented limit, otherwise a static English description. */
const char *tc_source_limit_violation(size_t size);

#endif /* TC_FRONTEND_INPUT_H */
