/* TC 0.0.46 contract: tc-aot entry point.
 *
 * The static front end is shared with tc-vm.  `--syntax-only`/`--sem-only`/
 * `--static-only` are partial checks; a normal invocation still fails with the
 * implementation-level "unavailable" status after the static stages succeed,
 * because the native backend is not implemented yet.  The tool never emits an
 * artifact or silently produces wrong code.
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
        fprintf(stderr, "tc-aot: tc_error_internal: %s\n", violation);
        return TC_EXIT_INTERNAL;
    }
    tc_cli_options_init(&options);
    parsed = tc_cli_parse(argc, argv, &options, "tc-aot");
    if (parsed != TC_CLI_PARSE_OK) {
        tc_cli_print_usage(stderr, "tc-aot");
        return TC_EXIT_USAGE;
    }
    if (options.help) {
        tc_cli_print_usage(stdout, "tc-aot");
        return TC_EXIT_OK;
    }
    if (options.version) {
        printf("tc-aot %s (static frontend; backend unavailable)\n", TC_LANGUAGE_VERSION);
        return TC_EXIT_OK;
    }
    if (!tc_driver_select_profile(options.profile_name, &profile)) {
        fprintf(stderr, "tc-aot: tc_error_internal: built-in profile is invalid\n");
        return TC_EXIT_INTERNAL;
    }
    return tc_driver_frontend("tc-aot", &options);
}
