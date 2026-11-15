/* TC 0.0.46 contract: tc-vm entry point (S0 skeleton).
 *
 * S1 provides the LEX/SYN front-end.  `--syntax-only` reports only lexical
 * and syntax diagnostics and exits 0 when the source has none; it is a partial
 * check, not a valid-program claim.  A normal invocation still fails with the
 * implementation-level "unavailable" status after LEX/SYN succeeds, because
 * IMPORT/SEM/CT and VM execution start in later phases.
 */
#include <stdio.h>

#include "contract/contract.h"
#include "diagnostic/diagnostic.h"
#include "driver/cli.h"
#include "driver/frontend_driver.h"
#include "driver/profile_select.h"

int main(int argc, char **argv) {
    const char *violation = tc_contract_check();
    TcPlatformProfile profile;
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
        printf("tc-vm %s (S1 frontend)\n", TC_LANGUAGE_VERSION);
        return TC_EXIT_OK;
    }
    if (!tc_driver_select_profile(options.profile_name, &profile)) {
        fprintf(stderr, "tc-vm: tc_error_internal: built-in profile is invalid\n");
        return TC_EXIT_INTERNAL;
    }
    return tc_driver_frontend("tc-vm", &options);
}
