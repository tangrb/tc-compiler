/* TC 0.0.46 contract: shared CLI parsing. */
#include "driver/cli.h"

#include <string.h>

#include "contract/contract.h"

void tc_cli_options_init(TcCliOptions *options) {
    if (options == NULL) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->profile_name = "aarch64-linux-user";
    options->opt = TC_OPT_O0;
    options->checked = 0;
}

void tc_cli_print_usage(FILE *stream, const char *tool_name) {
    fprintf(stream,
            "usage: %s [options] <source.tc>\n"
            "\n"
            "options:\n"
            "  --profile=<name>    platform profile (default aarch64-linux-user)\n"
            "  --include=<dir>     import search root (repeatable, order significant)\n"
            "  --opt=O0|O2         optimization level (zero-pass baseline; default O0)\n"
            "  --checked           enable checked-build UB access checks\n"
            "  --syntax-only       run LEX/SYN only; exit 0 means no LEX/SYN\n"
            "                      diagnostic (partial check, not a valid program)\n"
            "  --sem-only          run LEX/SYN/IMPORT/SEM; exit 0 means no\n"
            "                      diagnostic from those stages (CT/IR/codegen not run)\n"
            "  --static-only       run LEX/SYN/IMPORT/SEM/CT and finalize IR; exit 0\n"
            "                      means no diagnostic from those stages (no codegen)\n"
            "  --output=<path>     output path\n"
            "  --emit=asm|obj|elf|flat  tc-aot artifact kind (default asm)\n"
            "  --cc=<path>         clang used to assemble tc-aot output\n"
            "  --ld=<path>         ld.lld used to link tc-aot output\n"
            "  --device=<policy>   device region policy for the platform instance:\n"
            "                      reject|read|readwrite|volatile\n"
            "  --runtime=<path>    freestanding target runtime archive\n"
            "  --linker-script=<path> target image linker script\n"
            "  --keep-temps        keep tc-aot intermediate files\n"
            "  --version           print version and exit\n"
            "  --help              print this help and exit\n"
            "\n"
            "A normal tc-vm invocation executes the reference VM.  A normal\n"
            "tc-aot invocation emits target assembly; --emit=obj/elf/flat also\n"
            "assembles/links with the bundled freestanding target runtime.\n",
            tool_name);
}

TcCliParseResult tc_cli_parse(int argc, char **argv, TcCliOptions *options,
                              const char *tool_name) {
    int i;
    int only_positional = 0;
    if (options == NULL || tool_name == NULL) {
        return TC_CLI_PARSE_ERROR;
    }
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!only_positional && strcmp(arg, "--") == 0) {
            only_positional = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--help") == 0) {
            options->help = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--version") == 0) {
            options->version = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--checked") == 0) {
            options->checked = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--syntax-only") == 0) {
            options->syntax_only = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--sem-only") == 0) {
            options->sem_only = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--static-only") == 0) {
            options->static_only = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--profile=", 10) == 0) {
            if (arg[10] == '\0') {
                fprintf(stderr, "%s: missing value for --profile\n", tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            options->profile_name = arg + 10;
            continue;
        }
        if (!only_positional && strncmp(arg, "--device=", 9) == 0) {
            if (arg[9] == '\0') {
                fprintf(stderr, "%s: missing value for --device\n", tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            options->device_name = arg + 9;
            continue;
        }
        if (!only_positional && strncmp(arg, "--include=", 10) == 0) {
            if (arg[10] == '\0') {
                fprintf(stderr, "%s: missing value for --include\n", tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            if (options->ninclude_roots >= TC_CLI_MAX_INCLUDE_ROOTS) {
                fprintf(stderr, "%s: too many include roots (max %d)\n", tool_name,
                        TC_CLI_MAX_INCLUDE_ROOTS);
                return TC_CLI_PARSE_ERROR;
            }
            options->include_roots[options->ninclude_roots++] = arg + 10;
            continue;
        }
        if (!only_positional && strncmp(arg, "--opt=", 6) == 0) {
            if (!tc_opt_level_parse(arg + 6, &options->opt)) {
                fprintf(stderr, "%s: invalid optimization level '%s'\n", tool_name, arg + 6);
                return TC_CLI_PARSE_ERROR;
            }
            continue;
        }
        if (!only_positional && strncmp(arg, "--output=", 9) == 0) {
            if (arg[9] == '\0') {
                fprintf(stderr, "%s: missing value for --output\n", tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            options->output = arg + 9;
            continue;
        }
        if (!only_positional && strncmp(arg, "--emit=", 7) == 0) {
            if (strcmp(arg + 7, "asm") != 0 && strcmp(arg + 7, "obj") != 0 &&
                strcmp(arg + 7, "elf") != 0 && strcmp(arg + 7, "flat") != 0) {
                fprintf(stderr, "%s: invalid --emit value '%s'\n",
                        tool_name, arg + 7);
                return TC_CLI_PARSE_ERROR;
            }
            options->emit_kind = arg + 7;
            continue;
        }
        if (!only_positional && strncmp(arg, "--cc=", 5) == 0) {
            if (arg[5] == '\0') {
                fprintf(stderr, "%s: missing value for --cc\n", tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            options->assembler = arg + 5;
            continue;
        }
        if (!only_positional && strncmp(arg, "--ld=", 5) == 0) {
            if (arg[5] == '\0') {
                fprintf(stderr, "%s: missing value for --ld\n", tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            options->linker = arg + 5;
            continue;
        }
        if (!only_positional && strncmp(arg, "--runtime=", 10) == 0) {
            if (arg[10] == '\0') {
                fprintf(stderr, "%s: missing value for --runtime\n", tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            options->runtime_path = arg + 10;
            continue;
        }
        if (!only_positional && strncmp(arg, "--linker-script=", 16) == 0) {
            if (arg[16] == '\0') {
                fprintf(stderr, "%s: missing value for --linker-script\n",
                        tool_name);
                return TC_CLI_PARSE_ERROR;
            }
            options->linker_script = arg + 16;
            continue;
        }
        if (!only_positional && strcmp(arg, "--keep-temps") == 0) {
            options->keep_temps = 1;
            continue;
        }
        if (!only_positional && arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "%s: unknown option '%s'\n", tool_name, arg);
            return TC_CLI_PARSE_ERROR;
        }
        if (options->source != NULL) {
            fprintf(stderr, "%s: more than one source file\n", tool_name);
            return TC_CLI_PARSE_ERROR;
        }
        options->source = arg;
    }
    if (options->help || options->version) {
        return TC_CLI_PARSE_OK;
    }
    if (options->source == NULL) {
        fprintf(stderr, "%s: missing source file\n", tool_name);
        return TC_CLI_PARSE_ERROR;
    }
    if (strcmp(options->profile_name, "aarch64-linux-user") != 0 &&
        strcmp(options->profile_name, "aarch64-baremetal") != 0 &&
        strcmp(options->profile_name, "arm32-baremetal") != 0 &&
        strcmp(options->profile_name, "w32") != 0) {
        fprintf(stderr, "%s: unknown profile '%s'\n", tool_name, options->profile_name);
        return TC_CLI_PARSE_ERROR;
    }
    return TC_CLI_PARSE_OK;
}
