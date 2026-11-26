/* Shared private CLI contract for tc / tc-vm / tc-aot.
 *
 * Capabilities that are unavailable fail explicitly with exit 70, never
 * silently "supported".
 */
#ifndef TC_DRIVER_CLI_H
#define TC_DRIVER_CLI_H

#include <stddef.h>
#include <stdio.h>

#include "contract/optimizer.h"
#include "diagnostic/diagnostic.h"

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
    const char *emit_kind;     /* asm (default), obj, elf */
    const char *assembler;     /* assembler driver; NULL = build default */
    const char *linker;        /* linker; NULL = build default */
    const char *runtime_path;  /* freestanding runtime archive override */
    const char *linker_script; /* target linker script override */
    const char *device_name;   /* optional device-policy override */
    int keep_temps;            /* keep intermediate .s/.o/.elf files */
    const char *include_roots[TC_CLI_MAX_INCLUDE_ROOTS];
    size_t ninclude_roots;
    TcOptLevel opt;
    int checked;
    int debug;
    int profile_seen;
    int emit_seen;
    int output_seen;
    int syntax_only; /* partial LEX/SYN check; not a valid-program claim */
    int sem_only;    /* partial LEX/SYN/IMPORT/SEM check; CT/IR/codegen not run */
    int static_only; /* static stages through CT + IR finalization; no codegen */
    int help;
    int version;
    char parse_error[512];
} TcCliOptions;

typedef enum {
    TC_CLI_PARSE_OK = 0,
    TC_CLI_PARSE_ERROR,
    TC_CLI_PARSE_IO
} TcCliParseResult;

void tc_cli_options_init(TcCliOptions *options);
TcCliParseResult tc_cli_parse(int argc, char **argv, TcCliOptions *options,
                              const char *tool_name);
int tc_cli_write(FILE *stream, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
int tc_cli_flush(FILE *stream);
void tc_cli_ignore_sigpipe(void);
int tc_cli_print_usage(FILE *stream, const char *tool_name);
int tc_impl_error_exit(TcImplError error);

#endif /* TC_DRIVER_CLI_H */
