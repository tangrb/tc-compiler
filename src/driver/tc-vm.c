/* TC 0.0.46 contract: tc-vm entry point.
 *
 * `--syntax-only`/`--sem-only`/`--static-only` are partial checks, not
 * valid-program claims.  A normal invocation executes the finalized program on
 * the reference VM after the static stages succeed; the first runtime trap is
 * reported as error[RT] and the profile's trap exit status is returned.
 */
#include <stdio.h>
#include <string.h>

#include "contract/contract.h"
#include "diagnostic/diagnostic.h"
#include "driver/cli.h"
#include "driver/frontend_driver.h"
#include "driver/profile_select.h"

int main(int argc, char **argv) {
    const char *violation;
    TcPlatformProfile profile;
    TcCliOptions options;
    TcCliParseResult parsed;
    tc_cli_ignore_sigpipe();
    tc_cli_reduce_protocol_init();
    if (tc_cli_reduce_protocol_failed()) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    if (argc == 2 && strcmp(argv[1], "--tc-reduce-auth") == 0) {
        return tc_cli_print_reduce_auth(argv[0]);
    }
    violation = tc_contract_check();
    if (violation != NULL) {
        if (!tc_cli_reduce_protocol_active() &&
            (tc_cli_write(stderr, "tc-vm: tc_error_internal: %s\n", violation) !=
                 0 ||
             tc_cli_flush(stderr) != 0)) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return tc_impl_error_exit(TC_IMPL_INTERNAL);
    }
    tc_cli_options_init(&options);
    parsed = tc_cli_parse(argc, argv, &options, "tc-vm");
    if (parsed != TC_CLI_PARSE_OK) {
        if (parsed == TC_CLI_PARSE_IO) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return tc_driver_report_cli_error("tc-vm", &options);
    }
    if (options.help || options.version) {
        int guard = tc_driver_cli_guard(&options);
        if (guard != TC_EXIT_OK) {
            return guard;
        }
    }
    if (options.help) {
        if (tc_cli_print_usage(stdout, "tc-vm") != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return TC_EXIT_OK;
    }
    if (options.version) {
        if (tc_cli_write(stdout, "tc-vm %s (language %s; reference VM)\n",
                         TC_TOOLCHAIN_VERSION, TC_LANGUAGE_VERSION) != 0 ||
            tc_cli_flush(stdout) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return TC_EXIT_OK;
    }
    if (!tc_driver_select_profile(options.profile_name, &profile)) {
        int guard = tc_driver_cli_guard(&options);
        if (guard != TC_EXIT_OK) {
            return guard;
        }
        if (tc_cli_write(
                stderr,
                "tc-vm: tc_error_internal: built-in profile is invalid\n") !=
                0 ||
            tc_cli_flush(stderr) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return tc_impl_error_exit(TC_IMPL_INTERNAL);
    }
    return tc_driver_frontend("tc-vm", &options);
}
