/* TC 0.0.46 deterministic scalar output formatting (standard §11.2).
 *
 * The runtime never calls the host's printf for target values.  Integers,
 * bools and floats are rendered from their bit patterns: floats are converted
 * exactly to decimal with arbitrary-precision integer arithmetic and rounded
 * to six significant digits with roundTiesToEven.  The result is independent
 * of locale and of the host floating-point formatting routine.
 */
#ifndef TC_RUNTIME_FORMAT_H
#define TC_RUNTIME_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "ir/type.h"

/* Render one scalar value.  `type` must be bool, integer or float.  Returns 1
 * on success and 0 for an unsupported type or too small an output buffer. */
int tc_format_scalar(unsigned W, const TcType *type, uint64_t bits, char *out,
                     size_t cap);

/* Exact binary32/binary64 -> decimal text (six significant digits). */
int tc_format_float(unsigned width, uint64_t bits, char *out, size_t cap);

#endif /* TC_RUNTIME_FORMAT_H */
