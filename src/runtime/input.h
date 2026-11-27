/* TC 0.0.46 standard input token reader (standard §11.3).
 *
 * `read T` consumes one complete whitespace-delimited token and parses it as
 * the exact scalar type T.  Leading ASCII whitespace (0x09-0x0D and 0x20) is
 * skipped; a token must match the whole input token, so `12abc` is an error.
 * EOF before a token, a short/incomplete read, an invalid token or a value
 * outside T's range all map to TC_RE_IO at the read instruction.
 *
 * Floating tokens are converted by the exact decimal -> binary32/binary64
 * routine that also converts source literals; no host floating-point parsing
 * or locale-dependent conversion is used.
 */
#ifndef TC_RUNTIME_INPUT_H
#define TC_RUNTIME_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "ir/type.h"
#include "runtime/alloc.h"
#include "runtime/io.h"

typedef struct {
    const TcIoSrc *src;
    const TcAllocator *alloc;
    uint8_t buffer[256];
    size_t pos;
    size_t len;
    int eof;
    int error;
    char *token;
    size_t token_cap;
    size_t token_len;
    uint64_t consumed;
    uint64_t last_consumed;
    int last_eof;
    int consumed_overflow;
    char *replay;
    size_t replay_cap;
    size_t replay_len;
    size_t replay_pos;
    int token_committed;
} TcInput;

typedef enum {
    TC_INPUT_OK = 0,
    TC_INPUT_IO, /* invalid token, range error, EOF or source error */
    TC_INPUT_OOM,
    TC_INPUT_PAUSE
} TcInputStatus;

void tc_input_init(TcInput *input, const TcIoSrc *src,
                   const TcAllocator *alloc);
void tc_input_destroy(TcInput *input);

/* Parse one scalar T value.  On TC_INPUT_OK *bits holds the type-width bit
 * pattern (bool is normalized to 0/1). */
TcInputStatus tc_input_read_scalar(TcInput *input, unsigned W,
                                   const TcType *type, uint64_t *bits);
int tc_input_restart(TcInput *input);

#endif /* TC_RUNTIME_INPUT_H */
