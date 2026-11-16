/* TC 0.0.46 target-to-toolchain mapping (design §10.2/§11).
 *
 * The backend emits AArch64 assembly; this table selects the clang target
 * triple used for the integrated assembler.  Target descriptors remain the
 * single source of truth for word size/ABI; the driver binds exactly one
 * profile per build.
 */
#ifndef TC_BACKEND_COMMON_TOOLCHAIN_H
#define TC_BACKEND_COMMON_TOOLCHAIN_H

#include "runtime/profile.h"

/* Return the clang/ELF triple for a supported target, or NULL when the target
 * has no implemented toolchain. */
const char *tc_aot_target_triple(const TcTargetDesc *target);

#endif /* TC_BACKEND_COMMON_TOOLCHAIN_H */
