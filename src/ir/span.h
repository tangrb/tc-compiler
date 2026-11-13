/* TC 0.0.46 contract: source span (standard §12.5.1).
 *
 * line/column are 1-based.  column counts decoded Unicode scalar values.
 * File is the resolved module path; it is only used for diagnostics and never
 * by code generation.
 *
 * LEX errors additionally retain the longest valid UTF-8 prefix (bytes) and
 * the 0-based offset of the first invalid byte.  When there is no invalid
 * byte, invalid_byte_offset == TC_SPAN_NO_INVALID_BYTE.
 */
#ifndef TC_IR_SPAN_H
#define TC_IR_SPAN_H

#include <stdint.h>

#include "base/assert.h"

#define TC_SPAN_NO_INVALID_BYTE UINT32_MAX

typedef struct {
    const char *file;             /* resolved module path, stable for the run */
    uint32_t line;                /* 1-based */
    uint32_t column;              /* 1-based, Unicode scalar count */
    uint32_t valid_prefix_bytes;  /* LEX: longest valid UTF-8 prefix */
    uint32_t invalid_byte_offset; /* LEX: 0-based first invalid byte */
} TcSpan;

TC_STATIC_ASSERT(sizeof(uint32_t) == 4, __LINE__);

#endif /* TC_IR_SPAN_H */
