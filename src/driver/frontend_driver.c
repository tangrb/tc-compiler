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
#include "driver/observe.h"
#include "driver/profile_select.h"
#include "frontend/input.h"
#include "frontend/module.h"
#include "frontend/parse.h"
#include "frontend/sem.h"
#include "project/manifest.h"
#include "project/paths.h"
#include "vm/observe.h"
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

static int json_string(FILE *out, const char *value) {
    return tc_observe_json_string(out, value) == 0 ? 0 : -1;
}

static int print_diag_json(const TcSpan *span, TcStage stage, TcDiagCode code) {
    const TcDiagInfo *info = tc_diag_info(code);
    FILE *out = stderr;
    if (tc_cli_write(out, "{\"code\":") != 0 ||
        json_string(out, info != NULL ? info->symbol : "TC_CE_SYNTAX") != 0 ||
        tc_cli_write(out, ",\"severity\":\"error\",\"stage\":") != 0 ||
        json_string(out, stage_name(stage)) != 0 ||
        tc_cli_write(out, ",\"print_name\":") != 0 ||
        json_string(out, info != NULL ? info->print_name : "SyntaxError") != 0 ||
        tc_cli_write(out, ",\"file\":") != 0 ||
        json_string(out, span->file != NULL ? span->file : "<source>") != 0 ||
        tc_cli_write(out, ",\"line\":%u,\"col\":%u}\n", span->line,
                     span->column) != 0 ||
        tc_cli_flush(out) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return TC_EXIT_DIAGNOSTIC;
}

static int print_related_json(const TcSpan *span) {
    return tc_cli_write(stderr, "{\"severity\":\"note\","
                        "\"note\":\"related position\",\"file\":") == 0 &&
                   json_string(stderr, span->file != NULL ? span->file
                                                           : "<source>") == 0 &&
                   tc_cli_write(stderr, ",\"line\":%u,\"col\":%u}\n",
                                span->line, span->column) == 0 &&
                   tc_cli_flush(stderr) == 0
               ? TC_EXIT_DIAGNOSTIC
               : tc_impl_error_exit(TC_IMPL_IO);
}

static int print_invalid_byte_json(const TcSpan *span) {
    return tc_cli_write(stderr, "{\"severity\":\"note\","
                        "\"note\":\"invalid byte offset\",\"file\":") == 0 &&
                   json_string(stderr, span->file != NULL ? span->file
                                                           : "<source>") == 0 &&
                   tc_cli_write(stderr, ",\"line\":%u,\"col\":%u,"
                                "\"offset\":%u}\n",
                                span->line, span->column,
                                span->invalid_byte_offset) == 0 &&
                   tc_cli_flush(stderr) == 0
               ? TC_EXIT_DIAGNOSTIC
               : tc_impl_error_exit(TC_IMPL_IO);
}

static int print_byte_offset_note(const TcSpan *span) {
    if (span->invalid_byte_offset == TC_SPAN_NO_INVALID_BYTE) {
        return 0;
    }
    return tc_cli_write(stderr, "%s:%u:%u: note: invalid byte offset %u\n",
                        span->file != NULL ? span->file : "<source>",
                        span->line, span->column, span->invalid_byte_offset);
}

static int print_frontend_diag(const TcFrontendDiag *diag, int json) {
    const TcDiagInfo *info = tc_diag_info(diag->code);
    int failed;
    if (json) {
        int result = print_diag_json(&diag->span, diag->stage, diag->code);
        if (result != TC_EXIT_DIAGNOSTIC) {
            return result;
        }
        if (diag->stage == TC_STAGE_LEX &&
            diag->span.invalid_byte_offset != TC_SPAN_NO_INVALID_BYTE) {
            return print_invalid_byte_json(&diag->span);
        }
        return TC_EXIT_DIAGNOSTIC;
    }
    failed = tc_cli_write(stderr, "%s:%u:%u: error[%s]: %s: %s\n",
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

static int print_analysis_diag(const TcAnalysisDiag *diag, int json) {
    const TcDiagInfo *info = tc_diag_info(diag->code);
    int failed;
    if (json) {
        int result = print_diag_json(&diag->span, diag->stage, diag->code);
        if (result != TC_EXIT_DIAGNOSTIC) {
            return result;
        }
        if (diag->has_related) {
            result = print_related_json(&diag->related);
            if (result != TC_EXIT_DIAGNOSTIC) {
                return result;
            }
        }
        if (diag->stage == TC_STAGE_LEX &&
            diag->span.invalid_byte_offset != TC_SPAN_NO_INVALID_BYTE) {
            return print_invalid_byte_json(&diag->span);
        }
        return TC_EXIT_DIAGNOSTIC;
    }
    failed = tc_cli_write(stderr, "%s:%u:%u: error[%s]: %s: %s\n",
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

static int preflight_before_error(const char *tool_name,
                                  const TcCliOptions *options,
                                  const TcAotStdio *stdio,
                                  const char *const *extra_inputs,
                                  size_t nextra_inputs, const char *source,
                                  const char *const *include_roots,
                                  size_t ninclude_roots) {
    TcAotOutputs outputs;
    int aot = strcmp(tool_name, "tc-aot") == 0;
    TcImplError impl = tc_aot_outputs_preflight(
        stdio, NULL, TC_ANALYSIS_IMPL, options, extra_inputs, nextra_inputs,
        source, include_roots, ninclude_roots, aot, 0, &outputs);
    TcImplError cleanup = tc_aot_outputs_discard(&outputs);
    if (cleanup != TC_IMPL_OK) {
        return tc_impl_error_exit(cleanup);
    }
    if (impl == TC_IMPL_OK) {
        return TC_EXIT_OK;
    }
    if (impl != TC_IMPL_USAGE) {
        TcImplError guard_impl = tc_aot_driver_stderr_guard(
            stdio, NULL, TC_ANALYSIS_IMPL, extra_inputs, nextra_inputs, source,
            include_roots, ninclude_roots);
        if (guard_impl != TC_IMPL_OK) {
            return tc_impl_error_exit(guard_impl);
        }
    }
    return impl == TC_IMPL_USAGE ? TC_EXIT_USAGE
                                 : print_impl_error(tool_name, impl, NULL);
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

static int print_vm_trap(const char *tool_name, const TcCliOptions *options,
                         const TcVmResult *result, const TcLineMap *linemap) {
    const TcDiagInfo *info;
    const char *file;
    const char *module;
    const char *func;
    const char *entity_kind;
    char op[64];
    int failed;
    if (result == NULL || !result->trapped) {
        return TC_EXIT_INTERNAL;
    }
    info = tc_diag_info((TcDiagCode)result->kind);
    file = result->span.file != NULL ? result->span.file : "<source>";
    module = result->module != NULL ? result->module : file;
    func = result->entity_kind == 0 ? "<top>" : result->func;
    entity_kind = result->entity_kind == 0 ? "unit" : "function";
    if (func == NULL) {
        func = "<top>";
    }
    tc_observe_instr_name(result, linemap, op, sizeof(op));
    if (options != NULL && options->diag_format != NULL &&
        strcmp(options->diag_format, "json") == 0) {
        failed = tc_cli_write(stderr, "{\"code\":") != 0 ||
                 json_string(stderr, info != NULL ? info->symbol
                                                  : "TC_RE_TRAP") != 0 ||
                 tc_cli_write(stderr, ",\"severity\":\"error\","
                              "\"stage\":\"RT\",\"print_name\":") != 0 ||
                 json_string(stderr, info != NULL ? info->print_name
                                                  : "Trap") != 0 ||
                 tc_cli_write(stderr, ",\"file\":") != 0 ||
                 json_string(stderr, file) != 0 ||
                 tc_cli_write(stderr, ",\"line\":%u,\"col\":%u,"
                              "\"func\":", result->span.line,
                              result->span.column) != 0 ||
                 json_string(stderr, func) != 0 ||
                 tc_cli_write(stderr, ",\"bb\":%u,\"insn\":%u,\"op\":",
                              result->bb, result->insn) != 0 ||
                 json_string(stderr, op) != 0 ||
                 tc_cli_write(stderr, ",\"depth\":%u,\"kind\":",
                              result->depth) != 0 ||
                 json_string(stderr, entity_kind) != 0 ||
                 tc_cli_write(stderr, ",\"module\":") != 0 ||
                 json_string(stderr, module) != 0 ||
                 tc_cli_write(stderr, "}\n") != 0;
    } else {
        failed = tc_cli_write(stderr,
                              "%s:%u:%u: error[RT]: %s: %s"
                              " [func=%s bb=%u insn=%u op=%s depth=%u"
                              " kind=%s module=%s]\n",
                              file, result->span.line, result->span.column,
                              info != NULL ? info->symbol : "TC_RE_TRAP",
                              info != NULL ? info->print_name : "Trap", func,
                              result->bb, result->insn, op, result->depth,
                              entity_kind, module);
    }
    (void)tool_name;
    if (failed != 0 || tc_cli_flush(stderr) != 0) {
        return tc_impl_error_exit(TC_IMPL_IO);
    }
    return TC_EXIT_DIAGNOSTIC;
}

static int run_syntax_only(const char *tool_name, const TcCliOptions *options,
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
        return print_frontend_diag(&diag, options->diag_format != NULL &&
                                            strcmp(options->diag_format,
                                                   "json") == 0);
    }
    tc_parse_module_destroy(module);
    /* Partial capability: LEX/SYN accepted, not a valid-program claim. */
    return TC_EXIT_OK;
}

static int run_prepared(const char *tool_name, const TcCliOptions *options,
                        const TcProjectPaths *paths, const TcAotStdio *stdio) {
    TcPlatformProfile profile;
    TcAnalysis *analysis = NULL;
    TcAotOutputs outputs;
    TcAnalysisStatus status = TC_ANALYSIS_IMPL;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcImplError verify_impl = TC_IMPL_OK;
    const char *usage_detail = NULL;
    const char *display = tc_path_display(paths->context, paths->source);
    const char *extra_inputs[] = { paths->manifest->manifest_present
                                       ? paths->manifest->manifest_path
                                       : NULL };
    size_t nextra_inputs = paths->manifest->manifest_present ? 1u : 0u;
    int aot = strcmp(tool_name, "tc-aot") == 0;
    int json = options->diag_format != NULL &&
               strcmp(options->diag_format, "json") == 0;
    int result = TC_EXIT_OK;
#ifdef SIGPIPE
    /* A closed pipe is an I/O failure, not a process-terminating signal; the
     * VM's per-record sink then reports TC_RE_IO at the failed instruction. */
    (void)signal(SIGPIPE, SIG_IGN);
#endif
    tc_aot_outputs_init(&outputs);
    if (options == NULL || options->source == NULL) {
        return print_impl_error(tool_name, TC_IMPL_USAGE, NULL);
    }
    if (!tc_driver_select_profile(options->profile_seen ? options->profile_name
                                                        : paths->manifest->profile,
                                  &profile)) {
        usage_detail = options->profile_seen ? options->profile_name
                                             : paths->manifest->profile;
    } else if (options->device_name != NULL &&
               !tc_driver_apply_device(&profile, options->device_name)) {
        usage_detail = options->device_name;
    } else if (aot && options->emit_kind != NULL &&
               strcmp(options->emit_kind, "asm") != 0 &&
               options->output == NULL) {
        usage_detail = "--output is required for --emit=obj/elf/flat";
    }
    if (usage_detail != NULL) {
        result = preflight_before_error(
            tool_name, options, stdio, extra_inputs, nextra_inputs, paths->source,
            (const char *const *)paths->includes, paths->ninclude_roots);
        return result == TC_EXIT_OK
                   ? print_impl_error(tool_name, TC_IMPL_USAGE, usage_detail)
                   : result;
    }
    profile.checked_build = options->checked ? 1 : 0;
    impl = tc_aot_outputs_preflight(stdio, NULL, TC_ANALYSIS_IMPL, options,
                                    extra_inputs, nextra_inputs, paths->source,
                                    (const char *const *)paths->includes,
                                    paths->ninclude_roots, aot,
                                    aot && !options->static_only, &outputs);
    if (impl != TC_IMPL_OK) {
        TcImplError guard_impl = tc_aot_driver_stderr_guard(
            stdio, NULL, TC_ANALYSIS_IMPL, extra_inputs, nextra_inputs,
            paths->source, (const char *const *)paths->includes,
            paths->ninclude_roots);
        if (guard_impl != TC_IMPL_OK) {
            return tc_impl_error_exit(guard_impl);
        }
        return impl == TC_IMPL_USAGE ? TC_EXIT_USAGE
                                    : print_impl_error(tool_name, impl, NULL);
    }
    if (options->syntax_only) {
        result = run_syntax_only(tool_name, options, paths);
        goto done;
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
        result = guard_stderr(stdio, NULL, status, paths);
        if (result == TC_EXIT_OK) {
            result = print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                                  : TC_IMPL_INTERNAL,
                                      display);
        }
        goto done;
    }
    result = guard_stderr(stdio, analysis, status, paths);
    if (result != TC_EXIT_OK) {
        goto done;
    }
    verify_impl = tc_aot_outputs_verify(stdio, analysis, extra_inputs,
                                        nextra_inputs, aot, &outputs);
    if (verify_impl != TC_IMPL_OK) {
        result = verify_impl == TC_IMPL_USAGE
                     ? TC_EXIT_USAGE
                     : print_impl_error(tool_name, verify_impl, NULL);
        goto done;
    }
    if (status == TC_ANALYSIS_IMPL) {
        const char *failure = tc_analysis_failure_path(analysis);
        result = print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                              : TC_IMPL_INTERNAL,
                                  failure != NULL ? failure : display);
        goto done;
    }
    if (status == TC_ANALYSIS_ROOT_NOT_PROGRAM) {
        result = print_impl_error(tool_name, TC_IMPL_USAGE,
                                  "root module must be #program");
        goto done;
    }
    if (status == TC_ANALYSIS_DIAGNOSTIC) {
        result = print_analysis_diag(&diag, json);
        goto done;
    }
    if (options->sem_only) {
        result = TC_EXIT_OK;
        goto done;
    }
    if (options->static_only) {
        if (options->dump_ir_seen) {
            impl = tc_observe_dump_ir(tc_analysis_ir(analysis), &outputs);
            if (impl != TC_IMPL_OK) {
                result = print_impl_error(tool_name, impl, NULL);
                goto done;
            }
        }
        if (aot) {
            TcAotObserveStats stats;
            impl = tc_observe_stats_begin_ir(tc_analysis_ir(analysis), &profile,
                                             options, &outputs, &stats);
            if (impl == TC_IMPL_OK) {
                impl = tc_observe_stats_finish_aot(&stats, NULL);
            }
            if (impl != TC_IMPL_OK) {
                result = print_impl_error(tool_name, impl, NULL);
                goto done;
            }
        }
        result = TC_EXIT_OK;
        goto done;
    }
    if (!aot) {
        TcIo io;
        TcIoSink sink;
        TcIoSrc src;
        TcTrapHandler trap;
        TcVmResult vm_result;
        TcVmStats vm_stats;
        TcVmObserver observer;
        TcObserveStream trace_stream;
        TcLineMap *linemap = NULL;
        TcImplError vm_impl = TC_IMPL_OK;
        TcVmStatus vm_status;
        const TcIR *ir = tc_analysis_ir(analysis);
        int observed = options->trace_seen || options->stats;
        int tool_error = TC_IMPL_OK;
        memset(&vm_result, 0, sizeof(vm_result));
        memset(&vm_stats, 0, sizeof(vm_stats));
        memset(&observer, 0, sizeof(observer));
        memset(&trace_stream, 0, sizeof(trace_stream));
        if (ir == NULL) {
            result = print_impl_error(tool_name, TC_IMPL_INTERNAL, NULL);
            goto done;
        }
        if (options->dump_ir_seen) {
            impl = tc_observe_dump_ir(ir, &outputs);
            if (impl != TC_IMPL_OK) {
                result = print_impl_error(tool_name, impl, NULL);
                goto done;
            }
        }
        install_host_vm_seams(&profile, &io, &sink, &src, &trap);
        if (options->trace_seen) {
            impl = tc_linemap_build(ir, &linemap);
            if (impl != TC_IMPL_OK) {
                result = print_impl_error(tool_name, impl, NULL);
                goto done;
            }
            impl = tc_observe_trace_begin(&outputs, &trace_stream);
            if (impl != TC_IMPL_OK) {
                tc_linemap_destroy(linemap);
                result = print_impl_error(tool_name, impl, NULL);
                goto done;
            }
        }
        if (observed) {
            observer.linemap = linemap;
            observer.kinds = options->trace_kinds;
            observer.limit = options->trace_limit;
            observer.jsonl = options->trace_format != NULL &&
                             strcmp(options->trace_format, "jsonl") == 0;
            observer.trace = options->trace_seen ? trace_stream.file : NULL;
            observer.stats = options->stats;
            observer.stats_out = &vm_stats;
            vm_status = tc_vm_run_observed(ir, &profile, &vm_result, &vm_impl,
                                           &observer, &vm_stats);
        } else {
            vm_status = tc_vm_run(ir, &profile, &vm_result, &vm_impl);
        }
        if (options->trace_seen) {
            impl = tc_observe_stream_finish(&trace_stream);
            if (impl != TC_IMPL_OK) {
                tool_error = impl;
            }
        }
        if (vm_result.trapped) {
            if (linemap == NULL) {
                impl = tc_linemap_build(ir, &linemap);
                if (impl != TC_IMPL_OK) {
                    tool_error = impl;
                }
            }
            result = print_vm_trap(tool_name, options, &vm_result, linemap);
        } else if (vm_status == TC_VM_STATUS_OK) {
            result = profile.exit.normal_status;
        } else {
            result = print_impl_error(
                tool_name, vm_impl != TC_IMPL_OK ? vm_impl : TC_IMPL_INTERNAL,
                NULL);
        }
        if (options->stats && (observed || vm_status == TC_VM_STATUS_OK ||
                               vm_status == TC_VM_STATUS_TRAP)) {
            impl = tc_observe_stats_vm(options, &profile, &vm_stats, &outputs);
            if (impl != TC_IMPL_OK) {
                tool_error = impl;
            }
        }
        impl = tc_aot_outputs_release_observations(&outputs);
        if (impl != TC_IMPL_OK) {
            tool_error = impl;
        }
        tc_linemap_destroy(linemap);
        if (tool_error != TC_IMPL_OK) {
            result = tc_impl_error_exit(tool_error);
        }
        goto done;
    }
    result = tc_aot_driver_run_prepared(stdio, analysis, &profile, options,
                                        extra_inputs, nextra_inputs, &outputs);
done:
    tc_analysis_destroy(analysis);
    impl = result == TC_EXIT_OK ? tc_aot_outputs_release(&outputs)
                                : tc_aot_outputs_discard(&outputs);
    if (impl != TC_IMPL_OK) {
        return tc_impl_error_exit(impl);
    }
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
        int preflight_result;
        int located;
        if (tc_path_directory(options->source, &directory) == TC_IMPL_OK) {
            located = tc_project_manifest_locate(directory, &manifest_path,
                                                 &locate_error);
            if (located == 1) {
                extra_inputs[0] = manifest_path;
                nextra_inputs = 1u;
            }
        }
        preflight_result = preflight_before_error(
            tool_name, options, &stdio, extra_inputs, nextra_inputs,
            options->source, options->include_roots, options->ninclude_roots);
        free(directory);
        free(manifest_path);
        result = preflight_result == TC_EXIT_OK
                     ? print_impl_error(tool_name, path_error.impl,
                                        path_error.detail != NULL
                                            ? path_error.detail
                                            : path_error.message)
                     : preflight_result;
        tc_project_path_error_destroy(&path_error);
        return result;
    }
    result = run_prepared(tool_name, options, paths, &stdio);
    tc_project_paths_destroy(paths);
    return result;
}
