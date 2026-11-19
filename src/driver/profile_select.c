/* TC 0.0.46 contract: profile name dispatch. */
#include "driver/profile_select.h"

#include <string.h>

int tc_driver_select_profile(const char *name, TcPlatformProfile *out) {
    int ok;
    if (name == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(name, "aarch64-linux-user") == 0) {
        ok = tc_profile_init_aarch64_linux_user(out);
    } else if (strcmp(name, "aarch64-baremetal") == 0) {
        ok = tc_profile_init_aarch64_baremetal(out);
    } else if (strcmp(name, "arm32-baremetal") == 0) {
        ok = tc_profile_init_arm32_baremetal(out);
    } else if (strcmp(name, "riscv32-baremetal") == 0) {
        ok = tc_profile_init_riscv32_baremetal(out);
    } else if (strcmp(name, "w32") == 0) {
        ok = tc_profile_init_generic_w32(out);
    } else {
        return 0;
    }
    return ok && tc_profile_validate(out) == TC_PROFILE_OK;
}

int tc_driver_apply_device(TcPlatformProfile *profile, const char *name) {
    if (profile == NULL || name == NULL) {
        return 0;
    }
    if (strcmp(name, "reject") == 0) {
        profile->device = TC_DEV_REJECT;
    } else if (strcmp(name, "read") == 0) {
        profile->device = TC_DEV_READ;
    } else if (strcmp(name, "readwrite") == 0) {
        profile->device = TC_DEV_READWRITE;
    } else if (strcmp(name, "volatile") == 0) {
        profile->device = TC_DEV_VOLATILE;
    } else {
        return 0;
    }
    return 1;
}
