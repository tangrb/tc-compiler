/* TC 0.0.46 runtime semantic core (standard §6.3-§6.6, §10.6, §13.2).
 *
 * The semantic core is target-word-size aware: `isize`/`usize`/`ptr`/`fnptr`
 * have width W, fixed-width types keep their width.  Every operation reports
 * failure through a `TcErrorKind` (the 15 `TC_RE_*` codes); the compile-time
 * evaluator maps the same kinds to the six `TC_CE_CONST_*` codes, so constant
 * and runtime evaluation share one implementation of rounding, overflow and
 * exception priority.
 *
 * Functions return 1 when a result was produced and 0 when the operation
 * trapped (`*trap` is then a valid runtime code).  A 0 return with a zero
 * `*trap` is an internal contract violation and must not be reported as a
 * language diagnostic.
 */
#ifndef TC_RUNTIME_SEMANTICS_H
#define TC_RUNTIME_SEMANTICS_H

#include <stdint.h>

#include "ir/instr.h"
#include "ir/type.h"
#include "runtime/trap.h"

/* Bit width of a scalar/pointer/fnptr type under W.  Returns 0 for struct,
 * void, and an invalid W. */
unsigned tc_sem_type_width(const TcType *type, unsigned W);

/* Result width mask (low `width` bits). */
uint64_t tc_sem_mask(unsigned width);

/* Integer arithmetic/bit operations.  `type` is the instruction type T. */
int tc_sem_int_binop(unsigned W, const TcType *type, TcOpcode op, TcOpMode mode,
                     uint64_t a, uint64_t b, uint64_t *out, TcErrorKind *trap);
int tc_sem_int_unop(unsigned W, const TcType *type, TcOpcode op, TcOpMode mode,
                    uint64_t a, uint64_t *out, TcErrorKind *trap);

/* IEEE binary32/binary64 arithmetic (add/sub/mul/div/mod). */
int tc_sem_float_binop(unsigned W, const TcType *type, TcOpcode op,
                       TcOpMode mode, uint64_t a, uint64_t b, uint64_t *out,
                       TcErrorKind *trap);
/* abs/neg operate on the sign bit only; never trap. */
int tc_sem_float_unop(unsigned W, const TcType *type, TcOpcode op, uint64_t a,
                      uint64_t *out);

/* eq/ne/lt/le/gt/ge; result is a bool bit (0/1).  NaN never traps here. */
int tc_sem_compare(unsigned W, const TcType *type, TcOpcode op, uint64_t a,
                   uint64_t b, uint64_t *out_bool);

/* cast/trunc/bitcast.  Source and target full types are required; cast traps
 * with TC_EK_CAST_OVERFLOW, trunc/bitcast never trap. */
int tc_sem_cast(unsigned W, const TcType *src, const TcType *dst, uint64_t bits,
                uint64_t *out, TcErrorKind *trap);
int tc_sem_trunc(unsigned W, const TcType *src, const TcType *dst,
                 uint64_t bits, uint64_t *out);
int tc_sem_bitcast(unsigned W, const TcType *dst, uint64_t bits,
                   uint64_t *out);

/* bool read normalization: any nonzero byte is true (standard §3.4). */
uint64_t tc_sem_bool_from_byte(uint8_t byte);

uint64_t tc_sem_nan_bits(unsigned width);
uint64_t tc_sem_inf_bits(unsigned width, int negative);

#endif /* TC_RUNTIME_SEMANTICS_H */
