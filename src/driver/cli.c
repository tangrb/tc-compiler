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
            "  --debug             emit tc-aot source-level debug information\n"
            "  --dump-ir[=<path>]  dump finalized IR (default stderr)\n"
            "  --dump-linemap[=<path>]  dump IR source map (default stderr)\n"
            "  --trace=<kinds>     tc-vm events: ir,call,mem,io,trap\n"
            "  --trace-out=<path>  trace destination (default stderr)\n"
            "  --trace-format=text|jsonl  trace encoding (default text)\n"
            "  --trace-limit=<n>   maximum trace data records (0 = unlimited)\n"
            "  --stats             emit runtime or static statistics\n"
            "  --stats-out=<path>  statistics destination (default stderr)\n"
            "  --diag-format=text|json  diagnostic encoding (default text)\n"
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

static int parse_uint64(const char *value, uint64_t *out) {
    uint64_t parsed = 0u;
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
    *out = parsed;
    return 1;
}

static int parse_trace_kinds(const char *value, uint32_t *out) {
    uint32_t kinds = 0u;
    size_t start = 0u;
    size_t length = strlen(value);
    size_t i;
    if (length == 0u) {
        return 0;
    }
    for (i = 0u; i <= length; i++) {
        if (i == length || value[i] == ',') {
            size_t item_length = i - start;
            if (item_length == 2u && memcmp(value + start, "ir", 2u) == 0) {
                kinds |= TC_CLI_TRACE_IR;
            } else if (item_length == 4u &&
                       memcmp(value + start, "call", 4u) == 0) {
                kinds |= TC_CLI_TRACE_CALL;
            } else if (item_length == 3u &&
                       memcmp(value + start, "mem", 3u) == 0) {
                kinds |= TC_CLI_TRACE_MEM;
            } else if (item_length == 2u &&
                       memcmp(value + start, "io", 2u) == 0) {
                kinds |= TC_CLI_TRACE_IO;
            } else if (item_length == 4u &&
                       memcmp(value + start, "trap", 4u) == 0) {
                kinds |= TC_CLI_TRACE_TRAP;
            } else {
                return 0;
            }
            start = i + 1u;
        }
    }
    *out = kinds;
    return 1;
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
        if (!only_positional && strcmp(arg, "--debug") == 0) {
            options->debug = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--dump-ir") == 0) {
            if (options->dump_ir_seen) {
                parse_error(options, "%s: --dump-ir cannot repeat\n", tool_name);
            }
            options->dump_ir = "";
            options->dump_ir_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--dump-ir=", 10) == 0) {
            if (options->dump_ir_seen) {
                parse_error(options, "%s: --dump-ir cannot repeat\n", tool_name);
            } else if (arg[10] == '\0') {
                parse_error(options, "%s: missing value for --dump-ir\n",
                            tool_name);
            }
            options->dump_ir = arg + 10;
            options->dump_ir_seen = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--dump-linemap") == 0) {
            if (options->dump_linemap_seen) {
                parse_error(options, "%s: --dump-linemap cannot repeat\n", tool_name);
            }
            options->dump_linemap = "";
            options->dump_linemap_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--dump-linemap=", 15) == 0) {
            if (options->dump_linemap_seen) {
                parse_error(options, "%s: --dump-linemap cannot repeat\n", tool_name);
            } else if (arg[15] == '\0') {
                parse_error(options, "%s: missing value for --dump-linemap\n",
                            tool_name);
            }
            options->dump_linemap = arg + 15;
            options->dump_linemap_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--trace=", 8) == 0) {
            if (options->trace_seen) {
                parse_error(options, "%s: --trace cannot repeat\n", tool_name);
            } else if (!parse_trace_kinds(arg + 8, &options->trace_kinds)) {
                parse_error(options, "%s: invalid --trace kinds '%s'\n",
                            tool_name, arg + 8);
            }
            options->trace_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--trace-out=", 12) == 0) {
            if (options->trace_out_seen) {
                parse_error(options, "%s: --trace-out cannot repeat\n",
                            tool_name);
            } else if (arg[12] == '\0') {
                parse_error(options, "%s: missing value for --trace-out\n",
                            tool_name);
            }
            options->trace_out = arg + 12;
            options->trace_out_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--trace-format=", 15) == 0) {
            if (options->trace_format_seen) {
                parse_error(options, "%s: --trace-format cannot repeat\n",
                            tool_name);
            } else if (strcmp(arg + 15, "text") != 0 &&
                       strcmp(arg + 15, "jsonl") != 0) {
                parse_error(options, "%s: invalid --trace-format value '%s'\n",
                            tool_name, arg + 15);
            }
            options->trace_format = arg + 15;
            options->trace_format_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--trace-limit=", 14) == 0) {
            if (options->trace_limit_seen) {
                parse_error(options, "%s: --trace-limit cannot repeat\n",
                            tool_name);
            } else if (!parse_uint64(arg + 14, &options->trace_limit)) {
                parse_error(options,
                            "%s: --trace-limit requires nonnegative uint64\n",
                            tool_name);
            }
            options->trace_limit_seen = 1;
            continue;
        }
        if (!only_positional && strcmp(arg, "--stats") == 0) {
            options->stats = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--stats-out=", 12) == 0) {
            if (options->stats_out_seen) {
                parse_error(options, "%s: --stats-out cannot repeat\n",
                            tool_name);
            } else if (arg[12] == '\0') {
                parse_error(options, "%s: missing value for --stats-out\n",
                            tool_name);
            }
            options->stats_out = arg + 12;
            options->stats_out_seen = 1;
            continue;
        }
        if (!only_positional && strncmp(arg, "--diag-format=", 14) == 0) {
            if (options->diag_format_seen) {
                parse_error(options, "%s: --diag-format cannot repeat\n",
                            tool_name);
            } else if (strcmp(arg + 14, "text") != 0 &&
                       strcmp(arg + 14, "json") != 0) {
                parse_error(options, "%s: invalid --diag-format value '%s'\n",
                            tool_name, arg + 14);
            }
            options->diag_format = arg + 14;
            options->diag_format_seen = 1;
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
    if ((options->trace_out_seen || options->trace_format_seen ||
         options->trace_limit_seen) && !options->trace_seen) {
        parse_error(options, "%s: trace auxiliary options require --trace\n",
                    tool_name);
    } else if (options->stats_out_seen && !options->stats) {
        parse_error(options, "%s: --stats-out requires --stats\n", tool_name);
    } else if (strcmp(tool_name, "tc-aot") == 0 &&
               (options->trace_seen || options->trace_out_seen ||
                options->trace_format_seen || options->trace_limit_seen)) {
        parse_error(options, "%s: trace is only available in tc-vm\n",
                    tool_name);
    } else if (strcmp(tool_name, "tc-vm") == 0 && options->debug) {
        parse_error(options, "%s: --debug is only available in tc-aot\n",
                    tool_name);
    } else if (strcmp(tool_name, "tc-vm") == 0 &&
               (options->emit_seen || options->output_seen)) {
        parse_error(options,
                    "%s: --emit and --output are only available in tc-aot\n",
                    tool_name);
    } else if ((options->syntax_only || options->sem_only) &&
               (options->dump_ir_seen || options->dump_linemap_seen ||
                options->stats || options->trace_seen || options->debug)) {
        parse_error(options,
                    "%s: observation options require finalized IR or execution\n",
                    tool_name);
    } else if (options->static_only && strcmp(tool_name, "tc-vm") == 0 &&
               (options->stats || options->trace_seen || options->debug)) {
        parse_error(options,
                    "%s: stats/trace/debug are not available with --static-only\n",
                    tool_name);
    } else if (options->static_only && strcmp(tool_name, "tc-aot") == 0 &&
               options->debug) {
        parse_error(options,
                    "%s: --debug is not available with --static-only\n",
                    tool_name);
    } else if ((options->syntax_only || options->sem_only ||
                options->static_only) &&
               (options->emit_seen || options->output_seen)) {
        parse_error(options,
                    "%s: --emit and --output are not valid with partial checks\n",
                    tool_name);
    }
    if (options->parse_error[0] != '\0') {
        return TC_CLI_PARSE_ERROR;
    }
    return TC_CLI_PARSE_OK;
}
