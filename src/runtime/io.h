/* TC 0.0.46 contract: I/O seams (standard §11, design §7.2).
 *
 * TcIoSink.write is all-or-nothing: it must not partially commit a record.
 * Return 0 on success and non-zero on failure; a failure maps to TC_RE_IO.
 * TcIoSrc.read may short-read; it reports n bytes plus OK/EOF/ERROR.  A read
 * that ends before a complete token maps to TC_RE_IO and never yields a
 * partial token.
 */
#ifndef TC_RUNTIME_IO_H
#define TC_RUNTIME_IO_H

#include <stddef.h>
#include <stdint.h>

typedef struct TcIoSink {
    int (*write)(void *ctx, const uint8_t *bytes, size_t size);
    int (*flush)(void *ctx);
    void *ctx;
} TcIoSink;

typedef enum {
    TC_IO_OK = 0,
    TC_IO_EOF,
    TC_IO_ERROR
} TcIoStatus;

typedef struct TcIoSrc {
    int (*read)(void *ctx, uint8_t *buf, size_t cap, size_t *n, TcIoStatus *status);
    void *ctx;
} TcIoSrc;

typedef struct {
    TcIoSink *out;
    TcIoSrc *in;
} TcIo;

#endif /* TC_RUNTIME_IO_H */
