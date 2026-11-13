/* TC 0.0.46 contract: shared CLI contract for tc-vm / tc-aot (design §11).
 *
 * The tools are the only entry points.  In the S0 contract phase argument
 * parsing, profile selection and contract checks are real; front-end and code
 * generation are explicitly unavailable (exit 70), never silently "supported".
 */
#ifndef TC_DRIVER_CLI_H
#define TC_DRIVER_CLI_H

#include <stddef.h>
#include <stdio.h>

#include "contract/optimizer.h"

#define TC_EXIT_OK 0
#define TC_EXIT_DIAGNOSTIC 1
#define TC_EXIT_USAGE 2
#define TC_EXIT_UNAVAILABLE 70
#define TC_EXIT_INTERNAL 71

#define TC_CLI_MAX_INCLUDE_ROOTS 32

typedef struct {
    const char *source;
    const char *profile_name;
    const char *output;
    const char *include_roots[TC_CLI_MAX_INCLUDE_ROOTS];
    size_t ninclude_roots;
    TcOptLevel opt;
    int checked;
    int help;
    int version;
} TcCliOptions;

typedef enum {
    TC_CLI_PARSE_OK = 0,
    TC_CLI_PARSE_ERROR
} TcCliParseResult;

void tc_cli_options_init(TcCliOptions *options);
TcCliParseResult tc_cli_parse(int argc, char **argv, TcCliOptions *options,
                              const char *tool_name);
void tc_cli_print_usage(FILE *stream, const char *tool_name);

#endif /* TC_DRIVER_CLI_H */
