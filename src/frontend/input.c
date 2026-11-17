/* TC 0.0.46 front-end input layer (LEX/SYN). */
#include "frontend/input.h"

#include <stdio.h>

const char *tc_source_limit_violation(size_t size) {
    if (size > TC_FRONTEND_MAX_SOURCE_BYTES) {
        return "source file exceeds the documented source size limit";
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
