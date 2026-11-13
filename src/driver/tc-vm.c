/* TC 0.0.46 contract: tc-vm entry point (S0 skeleton).
 *
 * Real front-end + VM execution starts in S1/S6.  Until then a well-formed
 * invocation fails explicitly with an implementation-level "unavailable"
 * status; it never pretends to compile or execute a program.
 */
#include <stdio.h>
#include <string.h>

#include "driver/cli.h"
#include "frontend/contract.h"
#include "frontend/diagnostic.h"
#include "runtime/profile.h"

static int validate_profile(const char *name) {
    TcPlatformProfile profile;
    int ok;
    if (strcmp(name, "aarch64-linux-user") == 0) {
        ok = tc_profile_init_aarch64_linux_user(&profile);
    } else {
        ok = tc_profile_init_aarch64_baremetal(&profile);
    }
    if (!ok || tc_profile_validate(&profile) != TC_PROFILE_OK) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *violation = tc_contract_check();
    TcCliOptions options;
    TcCliParseResult parsed;
    if (violation != NULL) {
        fprintf(stderr, "tc-vm: tc_error_internal: %s\n", violation);
        return TC_EXIT_INTERNAL;
    }
    tc_cli_options_init(&options);
    parsed = tc_cli_parse(argc, argv, &options, "tc-vm");
    if (parsed != TC_CLI_PARSE_OK) {
        tc_cli_print_usage(stderr, "tc-vm");
        return TC_EXIT_USAGE;
    }
    if (options.help) {
        tc_cli_print_usage(stdout, "tc-vm");
        return TC_EXIT_OK;
    }
    if (options.version) {
        printf("tc-vm %s (contract skeleton)\n", TC_LANGUAGE_VERSION);
        return TC_EXIT_OK;
    }
    if (!validate_profile(options.profile_name)) {
        fprintf(stderr, "tc-vm: tc_error_internal: built-in profile is invalid\n");
        return TC_EXIT_INTERNAL;
    }
    fprintf(stderr, "tc-vm: %s: %s\n", tc_impl_error_symbol(TC_IMPL_UNAVAILABLE),
            tc_impl_error_message(TC_IMPL_UNAVAILABLE));
    return TC_EXIT_UNAVAILABLE;
}
