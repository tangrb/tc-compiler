/* Shared compiler CLI parsing and implementation-error exit mapping. */
#include "driver/cli.h"

#include <signal.h>
#include <stdarg.h>
#include <string.h>

#include "contract/contract.h"

int tc_cli_write(FILE *stream, const char *format, ...) {
    va_list ap;
    int result;
    va_start(ap, format);
    result = vfprintf(stream, format, ap);
    va_end(ap);
    return result < 0 ? -1 : 0;
}

int tc_cli_flush(FILE *stream) {
    return fflush(stream) == 0 ? 0 : -1;
}

void tc_cli_ignore_sigpipe(void) {
#ifdef SIGPIPE
    (void)signal(SIGPIPE, SIG_IGN);
#endif
}

int tc_impl_error_exit(TcImplError error) {
    switch (error) {
    case TC_IMPL_OK:
        return TC_EXIT_OK;
    case TC_IMPL_USAGE:
        return TC_EXIT_USAGE;
    case TC_IMPL_IO:
    case TC_IMPL_OOM:
    case TC_IMPL_UNAVAILABLE:
        return TC_EXIT_UNAVAILABLE;
    case TC_IMPL_INTERNAL:
    default:
        return TC_EXIT_INTERNAL;
    }
}

void tc_cli_options_init(TcCliOptions *options) {
    if (options == NULL) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->profile_name = "aarch64-linux-user";
    options->opt = TC_OPT_O0;
    options->checked = 0;
}

int tc_cli_print_usage(FILE *stream, const char *tool_name) {
    int failed = tc_cli_write(stream,
            "usage: %s [options] <source.tc>\n"
            "\n"
            "options:\n"
            "  --profile=<name>    platform profile (default aarch64-linux-user)\n"
            "  --include=<dir>     import search root (repeatable, order significant)\n"
            "  --opt=O0|O2         optimization level (zero-pass baseline; default O0)\n"
            "  --checked           enable checked-build UB access checks\n"
            "  --syntax-only       run LEX/SYN on the root module only; imports\n"
            "                      are not followed (partial check, not a valid program)\n"
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
    if (failed != 0 || tc_cli_flush(stream) != 0) {
        return -1;
    }
    return 0;
}

static void parse_error(TcCliOptions *options, const char *format, ...) {
    va_list ap;
    if (options->parse_error[0] != '\0') {
        return;
    }
    va_start(ap, format);
    (void)vsnprintf(options->parse_error, sizeof(options->parse_error), format,
                   ap);
    va_end(ap);
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
                parse_error(options, "%s: missing value for --profile\n",
                                   tool_name);
            }
            options->profile_name = arg + 10;
            options->profile_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--device=", 9) == 0) {
            if (arg[9] == '\0') {
                parse_error(options, "%s: missing value for --device\n",
                                   tool_name);
            }
            options->device_name = arg + 9;
            continue;
        }
        if (!only_positional && strncmp(arg, "--include=", 10) == 0) {
            if (arg[10] == '\0') {
                parse_error(options, "%s: missing value for --include\n",
                            tool_name);
                continue;
            }
            if (options->ninclude_roots >= TC_CLI_MAX_INCLUDE_ROOTS) {
                parse_error(options, "%s: too many include roots (max %d)\n",
                            tool_name, TC_CLI_MAX_INCLUDE_ROOTS);
                continue;
            }
            options->include_roots[options->ninclude_roots++] = arg + 10;
            continue;
        }
        if (!only_positional && strncmp(arg, "--opt=", 6) == 0) {
            if (!tc_opt_level_parse(arg + 6, &options->opt)) {
                parse_error(options, "%s: invalid optimization level '%s'\n",
                                   tool_name, arg + 6);
            }
            continue;
        }
        if (!only_positional && strncmp(arg, "--output=", 9) == 0) {
            if (arg[9] == '\0') {
                parse_error(options, "%s: missing value for --output\n",
                                   tool_name);
            }
            options->output = arg + 9;
            options->output_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--emit=", 7) == 0) {
            if (strcmp(arg + 7, "asm") != 0 && strcmp(arg + 7, "obj") != 0 &&
                strcmp(arg + 7, "elf") != 0 && strcmp(arg + 7, "flat") != 0) {
                parse_error(options, "%s: invalid --emit value '%s'\n",
                                   tool_name, arg + 7);
            }
            options->emit_kind = arg + 7;
            options->emit_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--cc=", 5) == 0) {
            if (arg[5] == '\0') {
                parse_error(options, "%s: missing value for --cc\n", tool_name);
            }
            options->assembler = arg + 5;
            continue;
        }
        if (!only_positional && strncmp(arg, "--ld=", 5) == 0) {
            if (arg[5] == '\0') {
                parse_error(options, "%s: missing value for --ld\n", tool_name);
            }
            options->linker = arg + 5;
            continue;
        }
        if (!only_positional && strncmp(arg, "--runtime=", 10) == 0) {
            if (arg[10] == '\0') {
                parse_error(options, "%s: missing value for --runtime\n",
                                   tool_name);
            }
            options->runtime_path = arg + 10;
            continue;
        }
        if (!only_positional && strncmp(arg, "--linker-script=", 16) == 0) {
            if (arg[16] == '\0') {
                parse_error(options, "%s: missing value for --linker-script\n",
                                   tool_name);
            }
            options->linker_script = arg + 16;
            continue;
        }
        if (!only_positional && strcmp(arg, "--keep-temps") == 0) {
            options->keep_temps = 1;
            continue;
        }
        if (!only_positional && arg[0] == '-' && arg[1] != '\0') {
            parse_error(options, "%s: unknown option '%s'\n", tool_name, arg);
            continue;
        }
        if (options->source != NULL) {
            parse_error(options, "%s: more than one source file\n", tool_name);
            continue;
        }
        options->source = arg;
    }
    if (options->parse_error[0] != '\0') {
        return TC_CLI_PARSE_ERROR;
    }
    if (options->help || options->version) {
        return TC_CLI_PARSE_OK;
    }
    if (options->source == NULL) {
        parse_error(options, "%s: missing source file\n", tool_name);
        return TC_CLI_PARSE_ERROR;
    }
    if (strcmp(options->profile_name, "aarch64-linux-user") != 0 &&
        strcmp(options->profile_name, "aarch64-baremetal") != 0 &&
        strcmp(options->profile_name, "arm32-baremetal") != 0 &&
        strcmp(options->profile_name, "riscv32-baremetal") != 0 &&
        strcmp(options->profile_name, "w32") != 0) {
        parse_error(options, "%s: unknown profile '%s'\n", tool_name,
                    options->profile_name);
        return TC_CLI_PARSE_ERROR;
    }
    return TC_CLI_PARSE_OK;
}
