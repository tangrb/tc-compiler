/* TC 0.0.46 contract: shared profile selection for the tool entry points.
 *
 * Keeping the name-to-profile mapping in one place prevents tc-vm and tc-aot
 * from diverging on which profile instance a name denotes.  A valid name still
 * resolves to a structurally validated profile; unavailable execution is a
 * later-stage concern.
 */
#ifndef TC_DRIVER_PROFILE_SELECT_H
#define TC_DRIVER_PROFILE_SELECT_H

#include "runtime/profile.h"

/* Returns 1 and fills out on success; 0 for an unknown name or an invalid
 * built-in profile. */
int tc_driver_select_profile(const char *name, TcPlatformProfile *out);

#endif /* TC_DRIVER_PROFILE_SELECT_H */
