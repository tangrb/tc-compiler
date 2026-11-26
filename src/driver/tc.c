#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "base/version.h"
#include "contract/contract.h"
#include "diagnostic/diagnostic.h"
#include "driver/cli.h"
#include "driver/test_runner.h"
#include "project/init.h"

typedef enum {
    TC_PROJECT_NONE,
    TC_PROJECT_INIT,
    TC_PROJECT_TEST
} TcProjectOperation;

static int print_usage(FILE *stream) {
    return tc_cli_write(stream,
            "usage: tc --init-project [--] project_name\n"
            "       tc --test [--test-timeout=<seconds>] [--] [test_name...]\n"
            "       tc --help\n"
            "       tc --version\n");
}

static int usage_error(const char *detail) {
    int failed = 0;
    if (detail != NULL) {
        failed = tc_cli_write(stderr, "tc: %s\n", detail);
    }
    if (failed == 0) {
        failed = print_usage(stderr);
    }
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return tc_impl_error_exit(TC_IMPL_USAGE);
}

static int parse_timeout(const char *value, uint64_t *out_seconds) {
    uint64_t parsed = 0;
    const unsigned char *cursor = (const unsigned char *)value;
    if (*cursor == '\0') {
        return 0;
    }
    while (*cursor != '\0') {
        unsigned int digit;
        if (*cursor < (unsigned char)'0' || *cursor > (unsigned char)'9') {
            return 0;
        }
        digit = (unsigned int)(*cursor - (unsigned char)'0');
        if (parsed > (UINT64_MAX - digit) / 10u) {
            return 0;
        }
        parsed = parsed * 10u + digit;
        cursor++;
    }
    *out_seconds = parsed;
    return 1;
}

static int run(int argc, char **argv) {
    TcProjectOperation operation = TC_PROJECT_NONE;
    const char *project_name = NULL;
    const char *timeout_text = NULL;
    uint64_t timeout_seconds = 0;
    size_t ntest_names = 0;
    int timeout_seen = 0;
    int terminated = 0;
    int i;

    if (argc < 2) {
        return usage_error("missing operation");
    }
    if (strcmp(argv[1], "--help") == 0) {
        if (argc != 2) {
            return usage_error("--help must be used alone");
        }
        if (print_usage(stdout) != 0 || tc_cli_flush(stdout) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return TC_EXIT_OK;
    }
    if (strcmp(argv[1], "--version") == 0) {
        if (argc != 2) {
            return usage_error("--version must be used alone");
        }
        if (tc_cli_write(stdout, "tc %s (language %s; project tool)\n",
                         TC_TOOLCHAIN_VERSION, TC_LANGUAGE_VERSION) != 0 ||
            tc_cli_flush(stdout) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return TC_EXIT_OK;
    }
    if (strcmp(argv[1], "--init-project") == 0) {
        operation = TC_PROJECT_INIT;
    } else if (strcmp(argv[1], "--test") == 0) {
        operation = TC_PROJECT_TEST;
    } else {
        return usage_error("operation must be the first argument");
    }

    for (i = 2; i < argc; i++) {
        char *arg = argv[i];
        if (!terminated && strcmp(arg, "--") == 0) {
            terminated = 1;
            continue;
        }
        if (!terminated && strcmp(arg, "--init-project") == 0) {
            return usage_error("--init-project cannot repeat or combine with --test");
        }
        if (!terminated && strcmp(arg, "--test") == 0) {
            return usage_error("--test cannot repeat or combine with --init-project");
        }
        if (!terminated && strcmp(arg, "--help") == 0) {
            return usage_error("--help must be used alone");
        }
        if (!terminated && strcmp(arg, "--version") == 0) {
            return usage_error("--version must be used alone");
        }
        if (!terminated &&
            strncmp(arg, "--test-timeout=", sizeof("--test-timeout=") - 1) == 0) {
            if (operation != TC_PROJECT_TEST) {
                return usage_error("--test-timeout requires --test");
            }
            if (timeout_seen) {
                return usage_error("--test-timeout cannot repeat");
            }
            if (!parse_timeout(arg + sizeof("--test-timeout=") - 1,
                               &timeout_seconds)) {
                return usage_error("--test-timeout requires nonnegative uint64 seconds");
            }
            timeout_seen = 1;
            timeout_text = arg + sizeof("--test-timeout=") - 1;
            continue;
        }
        if (!terminated && arg[0] == '-' && arg[1] != '\0') {
            return usage_error("unknown option");
        }
        if (operation == TC_PROJECT_INIT) {
            if (project_name != NULL) {
                return usage_error("--init-project accepts exactly one project name");
            }
            project_name = arg;
        } else {
            argv[2 + ntest_names++] = arg;
        }
    }
    if (operation == TC_PROJECT_INIT && project_name == NULL) {
        return usage_error("--init-project requires a project name");
    }
    if (operation == TC_PROJECT_INIT) {
        return tc_impl_error_exit(tc_project_init(project_name, stdout, stderr));
    }
    return tc_test_runner_run((const char *const *)(argv + 2), ntest_names,
                              timeout_seconds, timeout_text);
}

int main(int argc, char **argv) {
    const char *violation;
    tc_cli_ignore_sigpipe();
    violation = tc_contract_check();
    if (violation != NULL) {
        if (tc_cli_write(stderr, "tc: tc_error_internal: %s\n", violation) !=
                0 ||
            tc_cli_flush(stderr) != 0) {
            return tc_impl_error_exit(TC_IMPL_IO);
        }
        return tc_impl_error_exit(TC_IMPL_INTERNAL);
    }
    return run(argc, argv);
}
