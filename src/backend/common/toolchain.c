/* TC 0.0.46 target-to-toolchain mapping (design §10.2/§11).
 *
 * The backend emits target assembly; this table selects the clang target
 * triple used for the integrated assembler.  Target descriptors remain the
 * single source of truth for word size/ABI; the driver binds exactly one
 * profile per build.
 */
#include "backend/common/toolchain.h"

#include <string.h>

const char *tc_aot_target_triple(const TcTargetDesc *target) {
    if (target == NULL || target->name == NULL) {
        return NULL;
    }
    if (strcmp(target->name, "aarch64-linux-user") == 0 &&
        target->addr_width == 64u) {
        /* The ELF image is freestanding and contains its own startup/runtime;
         * this triple is accepted by clang's integrated assembler and lld. */
        return "aarch64-none-elf";
    }
    if (strcmp(target->name, "aarch64-baremetal") == 0 &&
        target->addr_width == 64u) {
        return "aarch64-none-elf";
    }
    if (strcmp(target->name, "arm32-baremetal") == 0 &&
        target->addr_width == 32u) {
        return "thumbv7em-none-eabihf";
    }
    if (strcmp(target->name, "riscv32-baremetal") == 0 &&
        target->addr_width == 32u) {
        return "riscv32-unknown-elf";
    }
    return NULL;
}
