/* TC 0.0.46 contract: aggregate freeze check implementation. */
#include "contract/contract.h"

#include <string.h>

#include "diagnostic/diagnostic.h"
#include "contract/optimizer.h"
#include "ir/instr.h"
#include "runtime/alloc.h"
#include "runtime/module_loader.h"
#include "runtime/profile.h"

const char *tc_contract_check(void) {
    const char *violation = tc_diag_contract_violation();
    TcPassPipeline o0;
    TcPassPipeline o2;
    TcPlatformProfile linux_user;
    TcPlatformProfile baremetal;
    if (violation != NULL) {
        return violation;
    }
    if ((size_t)TC_OPCODE_COUNT != 51) {
        return "opcode count is not 51";
    }
    if (strcmp(TC_LANGUAGE_VERSION, "0.0.46") != 0) {
        return "language version is not 0.0.46";
    }
    if (strcmp(TC_ERR_OUT_OF_MEMORY_PRINT_NAME, "OutOfMemory") != 0 ||
        strcmp(TC_ERR_OUT_OF_MEMORY_MESSAGE, "memory allocation failed") != 0) {
        return "TC_ERR_OUT_OF_MEMORY contract text mismatch";
    }
    if (tc_impl_error_symbol(TC_IMPL_OOM)[0] == '\0') {
        return "TC_ERR_OUT_OF_MEMORY has no symbol";
    }
    if (!tc_allocator_is_valid(tc_allocator_libc())) {
        return "hosted allocator contract is invalid";
    }
    if (tc_module_loader_absent.resolve == NULL ||
        tc_module_loader_absent.resolve(NULL, "a", "b", NULL) != TC_MODULE_ERROR) {
        return "device-side module loader must not resolve imports";
    }
    if (!tc_pipeline_build(TC_OPT_O0, &o0) || !tc_pipeline_build(TC_OPT_O2, &o2) ||
        !tc_pipeline_is_zero_pass(&o0) || !tc_pipeline_is_zero_pass(&o2) ||
        !tc_pipeline_levels_equivalent(&o0, &o2)) {
        return "zero-pass baseline is not enforced";
    }
    if (!tc_profile_init_aarch64_linux_user(&linux_user) ||
        tc_profile_validate(&linux_user) != TC_PROFILE_OK) {
        return "aarch64-linux-user profile contract is invalid";
    }
    if (!tc_profile_init_aarch64_baremetal(&baremetal) ||
        tc_profile_validate(&baremetal) != TC_PROFILE_OK) {
        return "aarch64-baremetal profile contract is invalid";
    }
    return NULL;
}
