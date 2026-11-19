/* TC 0.0.46 freestanding byte helpers for the runtime semantic core.
 *
 * The semantic core, the deterministic formatter and the input tokenizer are
 * compiled unchanged for the hosted reference VM and for freestanding AArch64
 * images, so they cannot rely on a hosted <string.h>.  These helpers cover
 * only the byte operations the core needs; whole-program memcpy/memmove/
 * memset remain the freestanding runtime's own definitions.
 */
#ifndef TC_RUNTIME_RT_MEMORY_H
#define TC_RUNTIME_RT_MEMORY_H

#include <stddef.h>

static inline void tc_rt_memcpy(void *destination, const void *source,
                                size_t size) {
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    size_t index;
    for (index = 0; index < size; index++) {
        to[index] = from[index];
    }
}

static inline void tc_rt_memset(void *destination, int byte, size_t size) {
    unsigned char *to = (unsigned char *)destination;
    size_t index;
    for (index = 0; index < size; index++) {
        to[index] = (unsigned char)byte;
    }
}

static inline int tc_rt_memcmp(const void *left, const void *right,
                               size_t size) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    size_t index;
    for (index = 0; index < size; index++) {
        if (a[index] != b[index]) {
            return a[index] < b[index] ? -1 : 1;
        }
    }
    return 0;
}

static inline size_t tc_rt_strlen(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

#endif /* TC_RUNTIME_RT_MEMORY_H */
