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
    } else {
        return 0;
    }
    return ok && tc_profile_validate(out) == TC_PROFILE_OK;
}
