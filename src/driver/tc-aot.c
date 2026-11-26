/* TC 0.0.46 contract: tc-aot entry point.
 *
 * The static front end is shared with tc-vm.  `--syntax-only`/`--sem-only`/
 * `--static-only` are partial checks.  A normal invocation emits deterministic
 * target assembly and can assemble/link an ELF or flat image with
 * --emit=obj/elf/flat.
 * Unsupported platform instances are reported as unavailable and never
 * silently lowered.
 */
#include <stdio.h>

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
    violation = tc_contract_check();
    if (violation != NULL) {
        if (tc_cli_write(stderr, "tc-aot: tc_error_internal: %s\n", violation) !=
                0 ||
            tc_cli_flush(stderr) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return tc_impl_error_exit(TC_IMPL_INTERNAL);
    }
    tc_cli_options_init(&options);
    parsed = tc_cli_parse(argc, argv, &options, "tc-aot");
    if (parsed != TC_CLI_PARSE_OK) {
        if (parsed == TC_CLI_PARSE_IO) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        if (tc_cli_print_usage(stderr, "tc-aot") != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return TC_EXIT_USAGE;
    }
    if (options.help) {
        if (tc_cli_print_usage(stdout, "tc-aot") != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return TC_EXIT_OK;
    }
    if (options.version) {
        if (tc_cli_write(stdout, "tc-aot %s (language %s; native AOT)\n",
                         TC_TOOLCHAIN_VERSION, TC_LANGUAGE_VERSION) != 0 ||
            tc_cli_flush(stdout) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return TC_EXIT_OK;
    }
    if (!tc_driver_select_profile(options.profile_name, &profile)) {
        if (tc_cli_write(
                stderr,
                "tc-aot: tc_error_internal: built-in profile is invalid\n") !=
                0 ||
            tc_cli_flush(stderr) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return tc_impl_error_exit(TC_IMPL_INTERNAL);
    }
    return tc_driver_frontend("tc-aot", &options);
}
