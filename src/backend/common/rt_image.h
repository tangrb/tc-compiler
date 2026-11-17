/* TC 0.0.46 loader-image stdin for hosted/semihosting platform instances.
 *
 * The image is an eight-byte little-endian length followed by that many
 * bytes, placed at __tc_input_image by the loader.  Reads are deliberately
 * short (at most four bytes) so the standard §11.3 short-read path is
 * exercised.  A zero image is an empty stream.
 */
#ifndef TC_BACKEND_COMMON_RT_IMAGE_H
#define TC_BACKEND_COMMON_RT_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define TC_INPUT_IMAGE_MAX UINT64_C(0x00100000)

extern const uint8_t __tc_input_image[];

static int tc_rt_image_input(char *bytes, size_t capacity, size_t *n,
                             int *eof) {
    static int loaded = 0;
    static const uint8_t *data = NULL;
    static size_t length = 0;
    static size_t position = 0;
    size_t take;
    size_t i;
    if (!loaded) {
        uint64_t count = 0;
        unsigned k;
        for (k = 0; k < 8u; k++) {
            count |= (uint64_t)__tc_input_image[k] << (8u * k);
        }
        if (count > TC_INPUT_IMAGE_MAX) {
            count = 0;
        }
        data = __tc_input_image + 8;
        length = (size_t)count;
        position = 0;
        loaded = 1;
    }
    if (position >= length) {
        *n = 0;
        *eof = 1;
        return 0;
    }
    take = length - position;
    if (take > capacity) {
        take = capacity;
    }
    if (take > 4u) {
        take = 4u;
    }
    for (i = 0; i < take; i++) {
        bytes[i] = (char)data[position + i];
    }
    position += take;
    *n = take;
    *eof = 0;
    return 0;
}

#endif /* TC_BACKEND_COMMON_RT_IMAGE_H */
