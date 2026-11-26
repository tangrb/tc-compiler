/* TC 0.0.46 driver: static stages plus tc-vm execution. */
#include "driver/frontend_driver.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base/path.h"
#include "diagnostic/diagnostic.h"
#include "driver/aot_driver.h"
#include "driver/profile_select.h"
#include "frontend/input.h"
#include "frontend/module.h"
#include "frontend/parse.h"
#include "frontend/sem.h"
#include "project/manifest.h"
#include "project/paths.h"
#include "vm/vm.h"

static const char *stage_name(TcStage stage) {
    switch (stage) {
    case TC_STAGE_LEX:
        return "LEX";
    case TC_STAGE_SYN:
        return "SYN";
    case TC_STAGE_IMPORT:
        return "IMPORT";
    case TC_STAGE_SEM:
        return "SEM";
    case TC_STAGE_CT:
        return "CT";
    case TC_STAGE_RT:
        return "RT";
    case TC_STAGE_COUNT:
    default:
        return "?";
    }
}

static int print_byte_offset_note(const TcSpan *span) {
    if (span->invalid_byte_offset == TC_SPAN_NO_INVALID_BYTE) {
        return 0;
    }
    return tc_cli_write(stderr, "%s:%u:%u: note: invalid byte offset %u\n",
                        span->file != NULL ? span->file : "<source>",
                        span->line, span->column, span->invalid_byte_offset);
}

static int print_frontend_diag(const TcFrontendDiag *diag) {
    const TcDiagInfo *info = tc_diag_info(diag->code);
    int failed = tc_cli_write(stderr, "%s:%u:%u: error[%s]: %s: %s\n",
                              diag->span.file != NULL ? diag->span.file
                                                      : "<source>",
                              diag->span.line, diag->span.column,
                              stage_name(diag->stage),
                              info != NULL ? info->symbol : "TC_CE_SYNTAX",
                              info != NULL ? info->print_name : "SyntaxError");
    if (failed == 0 && diag->stage == TC_STAGE_LEX) {
        failed = print_byte_offset_note(&diag->span);
    }
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return TC_EXIT_DIAGNOSTIC;
}

static int print_analysis_diag(const TcAnalysisDiag *diag) {
    const TcDiagInfo *info = tc_diag_info(diag->code);
    int failed = tc_cli_write(stderr, "%s:%u:%u: error[%s]: %s: %s\n",
                              diag->span.file != NULL ? diag->span.file
                                                      : "<source>",
                              diag->span.line, diag->span.column,
                              stage_name(diag->stage),
                              info != NULL ? info->symbol : "TC_CE_SYNTAX",
                              info != NULL ? info->print_name : "SyntaxError");
    if (failed == 0 && diag->has_related) {
        failed = tc_cli_write(stderr, "%s:%u:%u: note: related position\n",
                              diag->related.file != NULL ? diag->related.file
                                                         : "<source>",
                              diag->related.line, diag->related.column);
    }
    if (failed == 0 && diag->stage == TC_STAGE_LEX) {
        failed = print_byte_offset_note(&diag->span);
    }
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return TC_EXIT_DIAGNOSTIC;
}

static int print_impl_error(const char *tool_name, TcImplError error,
                            const char *detail) {
    const char *symbol = tc_impl_error_symbol(error);
    const char *message = tc_impl_error_message(error);
    int failed;
    if (error == TC_IMPL_OOM) {
        failed = tc_cli_write(stderr, "%s: %s: %s: %s\n", tool_name,
                              tc_impl_error_symbol(error),
                              tc_impl_error_print_name(error), message);
    } else {
        if (error == TC_IMPL_INTERNAL) {
            /* Internal failures include the documented front-end
             * implementation limits and are distinct from OOM/I/O. */
            message =
                "internal error or documented implementation limit exceeded";
        }
        if (detail != NULL) {
            failed = tc_cli_write(stderr, "%s: %s: %s: %s\n", tool_name,
                                  symbol, message, detail);
        } else {
            failed = tc_cli_write(stderr, "%s: %s: %s\n", tool_name, symbol,
                                  message);
        }
    }
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return tc_impl_error_exit(error);
}

static int guard_stderr(const TcAotStdio *stdio, const TcAnalysis *analysis,
                        TcAnalysisStatus analysis_status,
                        const TcProjectPaths *paths) {
    const char *extra_inputs[] = { paths->manifest->manifest_present
                                       ? paths->manifest->manifest_path
                                       : NULL };
    size_t nextra_inputs = paths->manifest->manifest_present ? 1u : 0u;
    TcImplError impl = tc_aot_driver_stderr_guard(
        stdio, analysis, analysis_status, extra_inputs, nextra_inputs,
        paths->source, (const char *const *)paths->includes,
        paths->ninclude_roots);
    return impl == TC_IMPL_OK ? TC_EXIT_OK : tc_impl_error_exit(impl);
}

int tc_driver_cli_guard(const TcCliOptions *options) {
    TcAotStdio stdio;
    char *directory = NULL;
    char *manifest_path = NULL;
    const char *extra_inputs[] = { NULL };
    size_t nextra_inputs = 0u;
    TcImplError impl = TC_IMPL_OK;
    int located = 0;
    tc_aot_stdio_capture(&stdio);
    if (options != NULL && options->source != NULL) {
        impl = tc_path_directory(options->source, &directory);
        if (impl == TC_IMPL_OK) {
            TcProjectError locate_error;
            located = tc_project_manifest_locate(directory, &manifest_path,
                                                 &locate_error);
            if (located < 0) {
                impl = locate_error.impl;
            } else if (located == 1) {
                extra_inputs[0] = manifest_path;
                nextra_inputs = 1u;
            }
        }
    }
    if (impl == TC_IMPL_OK) {
        impl = tc_aot_driver_guard(
            &stdio, NULL, TC_ANALYSIS_IMPL, options, extra_inputs,
            nextra_inputs, options != NULL ? options->source : NULL,
            options != NULL ? options->include_roots : NULL,
            options != NULL ? options->ninclude_roots : 0u, 0, 0);
    }
    free(directory);
    free(manifest_path);
    return tc_impl_error_exit(impl);
}

int tc_driver_report_cli_error(const char *tool_name,
                               const TcCliOptions *options) {
    int result = tc_driver_cli_guard(options);
    int failed;
    if (result != TC_EXIT_OK) {
        return result;
    }
    failed = tc_cli_write(stderr, "%s", options->parse_error);
    if (failed == 0) {
        failed = tc_cli_print_usage(stderr, tool_name);
    }
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return TC_EXIT_USAGE;
}

static int guard_outputs(const char *tool_name, const TcAotStdio *stdio,
                         const TcAnalysis *analysis,
                         TcAnalysisStatus analysis_status,
                         const TcCliOptions *options,
                         const TcProjectPaths *paths, int create_output) {
    const char *extra_inputs[] = { paths->manifest->manifest_present
                                       ? paths->manifest->manifest_path
                                       : NULL };
    size_t nextra_inputs = paths->manifest->manifest_present ? 1u : 0u;
    TcImplError impl;
    int result = guard_stderr(stdio, analysis, analysis_status, paths);
    if (result != TC_EXIT_OK) {
        return result;
    }
    impl = tc_aot_driver_guard(
        stdio, analysis, analysis_status, options, extra_inputs,
        nextra_inputs, paths->source,
        (const char *const *)paths->includes, paths->ninclude_roots,
        strcmp(tool_name, "tc-aot") == 0, create_output);
    if (impl == TC_IMPL_USAGE) {
        return TC_EXIT_USAGE;
    }
    return impl == TC_IMPL_OK ? TC_EXIT_OK
                              : print_impl_error(tool_name, impl, NULL);
}

/* ------------------------------------------------------------------------- */
/* Hosted I/O seams and trap reporting for the reference VM.                 */
/* ------------------------------------------------------------------------- */

static int host_sink_write(void *ctx, const uint8_t *bytes, size_t size) {
    (void)ctx;
    if (size == 0) {
        return 0;
    }
    if (fwrite(bytes, 1, size, stdout) != size) {
        return -1;
    }
    /* Commit each write/writeln record immediately: a buffered fwrite alone
     * cannot report a closed or full stdout at the failed record. */
    return fflush(stdout) == 0 ? 0 : -1;
}

static int host_sink_flush(void *ctx) {
    (void)ctx;
    return fflush(stdout) == 0 ? 0 : -1;
}

static int host_source_read(void *ctx, uint8_t *buf, size_t cap, size_t *n,
                            TcIoStatus *status) {
    size_t got;
    (void)ctx;
    if (buf == NULL || n == NULL || status == NULL) {
        return -1;
    }
    got = fread(buf, 1, cap, stdin);
    *n = got;
    if (got != 0) {
        *status = TC_IO_OK;
    } else if (feof(stdin)) {
        *status = TC_IO_EOF;
    } else {
        *status = TC_IO_ERROR;
    }
    if (ferror(stdin)) {
        return -1;
    }
    return 0;
}

static void host_trap(void *ctx, TcErrorKind kind, int line) {
    (void)ctx;
    (void)kind;
    (void)line;
}

static void install_host_vm_seams(TcPlatformProfile *profile, TcIo *io,
                                  TcIoSink *sink, TcIoSrc *src,
                                  TcTrapHandler *trap) {
    sink->write = host_sink_write;
    sink->flush = host_sink_flush;
    sink->ctx = NULL;
    src->read = host_source_read;
    src->ctx = NULL;
    trap->trap = host_trap;
    trap->ctx = NULL;
    io->out = sink;
    io->in = src;
    profile->io = io;
    profile->alloc = (TcAllocator *)tc_allocator_libc();
    profile->trap = trap;
}

static int print_vm_trap(const char *tool_name, const TcVmResult *result) {
    const TcDiagInfo *info;
    int failed;
    if (result == NULL || !result->trapped) {
        return TC_EXIT_INTERNAL;
    }
    info = tc_diag_info((TcDiagCode)result->kind);
    failed = tc_cli_write(stderr, "%s:%u:%u: error[RT]: %s: %s\n",
                          result->span.file != NULL ? result->span.file
                                                   : "<source>",
                          result->span.line, result->span.column,
                          info != NULL ? info->symbol : "TC_RE_TRAP",
                          info != NULL ? info->print_name : "Trap");
    (void)tool_name;
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return TC_EXIT_DIAGNOSTIC;
}

static int run_syntax_only(const char *tool_name,
                           const TcProjectPaths *paths) {
    TcSourceBytes source;
    TcParseModule *module = NULL;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcImplError read_error;
    const char *display = tc_path_display(paths->context, paths->source);
    const TcPathFile *bound_file;
    char *bound_path = NULL;
    int fd = -1;
    int status;
    int parsed;
    status = tc_module_bind_root(paths->source, &fd, &bound_path);
    if (status < 0) {
        return print_impl_error(tool_name, TC_IMPL_OOM, display);
    }
    if (status != 1) {
        return print_impl_error(tool_name, TC_IMPL_IO, display);
    }
    if (!tc_path_text_valid(bound_path)) {
        (void)close(fd);
        free(bound_path);
        return print_impl_error(tool_name, TC_IMPL_IO, display);
    }
    bound_file = tc_path_context_add(paths->context, bound_path);
    if (bound_file == NULL) {
        (void)close(fd);
        free(bound_path);
        return print_impl_error(tool_name, TC_IMPL_OOM, display);
    }
    display = bound_file->display;
    read_error = tc_source_read_fd(fd, &source);
    if (read_error != TC_IMPL_OK) {
        free(bound_path);
        return print_impl_error(tool_name, read_error, display);
    }
    parsed = tc_parse_source(display, source.bytes, source.size,
                             &module, &diag, &impl);
    tc_source_free(&source);
    free(bound_path);
    if (parsed != 0) {
        return print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                              : TC_IMPL_INTERNAL,
                                display);
    }
    if (module == NULL) {
        return print_frontend_diag(&diag);
    }
    tc_parse_module_destroy(module);
    /* Partial capability: LEX/SYN accepted, not a valid-program claim. */
    return TC_EXIT_OK;
}

static int run_prepared(const char *tool_name, const TcCliOptions *options,
                        const TcProjectPaths *paths, const TcAotStdio *stdio) {
    TcPlatformProfile profile;
    TcAnalysis *analysis;
#ifdef SIGPIPE
    /* A closed pipe is an I/O failure, not a process-terminating signal; the
     * VM's per-record sink then reports TC_RE_IO at the failed instruction. */
    (void)signal(SIGPIPE, SIG_IGN);
#endif
    TcAnalysisStatus status = TC_ANALYSIS_IMPL;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    const char *usage_detail = NULL;
    int result;
    const char *display = tc_path_display(paths->context, paths->source);
    if (options == NULL || options->source == NULL) {
        return print_impl_error(tool_name, TC_IMPL_USAGE, NULL);
    }
    if ((options->syntax_only || options->sem_only || options->static_only) &&
        (options->emit_seen || options->output_seen)) {
        usage_detail = "--emit and --output are not valid with partial checks";
    } else if (!tc_driver_select_profile(options->profile_seen ? options->profile_name
                                                        : paths->manifest->profile,
                                  &profile)) {
        usage_detail = options->profile_seen ? options->profile_name
                                             : paths->manifest->profile;
    } else if (options->device_name != NULL &&
               !tc_driver_apply_device(&profile, options->device_name)) {
        usage_detail = options->device_name;
    } else if (strcmp(tool_name, "tc-aot") == 0 &&
               options->emit_kind != NULL &&
               strcmp(options->emit_kind, "asm") != 0 &&
               options->output == NULL) {
        usage_detail = "--output is required for --emit=obj/elf/flat";
    }
    if (usage_detail != NULL) {
        result = guard_stderr(stdio, NULL, TC_ANALYSIS_IMPL, paths);
        return result == TC_EXIT_OK
                   ? print_impl_error(tool_name, TC_IMPL_USAGE, usage_detail)
                   : result;
    }
    /* Checked build is an execution option of the selected platform instance:
     * the same profile instance drives analysis and code generation. */
    profile.checked_build = options->checked ? 1 : 0;
    result = guard_outputs(tool_name, stdio, NULL, TC_ANALYSIS_IMPL, options,
                           paths, 0);
    if (result != TC_EXIT_OK) {
        return result;
    }
    if (options->syntax_only) {
        return run_syntax_only(tool_name, paths);
    }
    if (options->sem_only) {
        analysis = tc_analyze_sem_only_with_paths(
            paths->source, profile.addr_width,
            (const char *const *)paths->includes,
            paths->ninclude_roots, paths->context, &status, &diag, &impl);
    } else {
        analysis = tc_analyze_with_paths(
            paths->source, &profile, profile.addr_width,
            (const char *const *)paths->includes,
            paths->ninclude_roots, paths->context, &status, &diag, &impl);
    }
    if (analysis == NULL) {
        result = guard_outputs(tool_name, stdio, NULL, status, options, paths,
                               strcmp(tool_name, "tc-aot") == 0);
        if (result == TC_EXIT_OK) {
            result = print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                                  : TC_IMPL_INTERNAL,
                                      display);
        }
        return result;
    }
    if (status == TC_ANALYSIS_IMPL) {
        const char *failure = tc_analysis_failure_path(analysis);
        result = guard_outputs(tool_name, stdio, analysis, status, options,
                               paths, strcmp(tool_name, "tc-aot") == 0);
        if (result == TC_EXIT_OK) {
            result = print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                                  : TC_IMPL_INTERNAL,
                                      failure != NULL ? failure : display);
        }
        tc_analysis_destroy(analysis);
        return result;
    }
    if (status == TC_ANALYSIS_ROOT_NOT_PROGRAM) {
        result = guard_outputs(tool_name, stdio, analysis, status, options,
                               paths, strcmp(tool_name, "tc-aot") == 0);
        tc_analysis_destroy(analysis);
        return result == TC_EXIT_OK
                   ? print_impl_error(tool_name, TC_IMPL_USAGE,
                                      "root module must be #program")
                   : result;
    }
    if (status == TC_ANALYSIS_DIAGNOSTIC) {
        result = guard_outputs(tool_name, stdio, analysis, status, options,
                               paths, strcmp(tool_name, "tc-aot") == 0);
        if (result == TC_EXIT_OK) {
            result = print_analysis_diag(&diag);
        }
        tc_analysis_destroy(analysis);
        return result;
    }
    if (options->sem_only || options->static_only) {
        result = guard_outputs(tool_name, stdio, analysis, status, options,
                               paths, 0);
        tc_analysis_destroy(analysis);
        return result;
    }
    if (strcmp(tool_name, "tc-vm") == 0) {
        TcIo io;
        TcIoSink sink;
        TcIoSrc src;
        TcTrapHandler trap;
        TcVmResult vm_result;
        TcImplError vm_impl = TC_IMPL_OK;
        TcVmStatus vm_status;
        const TcIR *ir;
        result = guard_outputs(tool_name, stdio, analysis, status, options,
                               paths, 0);
        if (result != TC_EXIT_OK) {
            tc_analysis_destroy(analysis);
            return result;
        }
        ir = tc_analysis_ir(analysis);
        install_host_vm_seams(&profile, &io, &sink, &src, &trap);
        if (ir == NULL) {
            tc_analysis_destroy(analysis);
            return print_impl_error(tool_name, TC_IMPL_INTERNAL, NULL);
        }
        vm_status = tc_vm_run(ir, &profile, &vm_result, &vm_impl);
        if (io.out != NULL && io.out->flush != NULL) {
            (void)io.out->flush(io.out->ctx);
        }
        if (vm_status == TC_VM_STATUS_OK) {
            result = profile.exit.normal_status;
        } else if (vm_status == TC_VM_STATUS_TRAP) {
            result = print_vm_trap(tool_name, &vm_result);
        } else {
            result = print_impl_error(
                tool_name, vm_impl != TC_IMPL_OK ? vm_impl : TC_IMPL_INTERNAL,
                NULL);
        }
        tc_analysis_destroy(analysis);
        return result;
    }
    {
        const char *extra_inputs[] = { paths->manifest->manifest_present
                                           ? paths->manifest->manifest_path
                                           : NULL };
        size_t nextra_inputs = paths->manifest->manifest_present ? 1u : 0u;
        result = tc_aot_driver_run_with_stdio(
            stdio, analysis, &profile, options, extra_inputs, nextra_inputs);
    }
    tc_analysis_destroy(analysis);
    return result;
}

int tc_driver_frontend(const char *tool_name, const TcCliOptions *options) {
    TcProjectPaths *paths = NULL;
    TcProjectPathError path_error;
    TcAotStdio stdio;
    int result;
    tc_aot_stdio_capture(&stdio);
    if (options == NULL || options->source == NULL) {
        return print_impl_error(tool_name, TC_IMPL_USAGE, NULL);
    }
    if (!tc_project_paths_prepare(options->source, options->include_roots,
                                  options->ninclude_roots, &paths,
                                  &path_error)) {
        char *directory = NULL;
        char *manifest_path = NULL;
        TcProjectError locate_error;
        const char *extra_inputs[] = { NULL };
        size_t nextra_inputs = 0u;
        TcImplError safety = TC_IMPL_INTERNAL;
        int located;
        if (tc_path_directory(options->source, &directory) == TC_IMPL_OK) {
            located = tc_project_manifest_locate(directory, &manifest_path,
                                                 &locate_error);
            if (located == 1) {
                extra_inputs[0] = manifest_path;
                nextra_inputs = 1u;
            }
            if (located >= 0) {
                safety = tc_aot_driver_stderr_guard(
                    &stdio, NULL, TC_ANALYSIS_IMPL, extra_inputs, nextra_inputs,
                    options->source, options->include_roots,
                    options->ninclude_roots);
            }
        }
        free(directory);
        free(manifest_path);
        result = safety == TC_IMPL_OK
                     ? print_impl_error(tool_name, path_error.impl,
                                        path_error.detail != NULL
                                            ? path_error.detail
                                            : path_error.message)
                     : tc_impl_error_exit(path_error.impl);
        tc_project_path_error_destroy(&path_error);
        return result;
    }
    result = run_prepared(tool_name, options, paths, &stdio);
    tc_project_paths_destroy(paths);
    return result;
}
