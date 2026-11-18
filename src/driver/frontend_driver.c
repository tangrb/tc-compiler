/* TC 0.0.46 driver: static stages plus tc-vm execution. */
#include "driver/frontend_driver.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "diagnostic/diagnostic.h"
#include "driver/aot_driver.h"
#include "driver/profile_select.h"
#include "frontend/input.h"
#include "frontend/parse.h"
#include "frontend/sem.h"
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

static void print_frontend_diag(const TcFrontendDiag *diag) {
    const TcDiagInfo *info = tc_diag_info(diag->code);
    fprintf(stderr, "%s:%u:%u: error[%s]: %s: %s\n",
            diag->span.file != NULL ? diag->span.file : "<source>",
            diag->span.line, diag->span.column, stage_name(diag->stage),
            info != NULL ? info->symbol : "TC_CE_SYNTAX",
            info != NULL ? info->print_name : "SyntaxError");
    if (diag->span.invalid_byte_offset != TC_SPAN_NO_INVALID_BYTE) {
        fprintf(stderr, "%s:%u:%u: note: invalid byte offset %u\n",
                diag->span.file != NULL ? diag->span.file : "<source>",
                diag->span.line, diag->span.column,
                diag->span.invalid_byte_offset);
    }
}

static void print_analysis_diag(const TcAnalysisDiag *diag) {
    const TcDiagInfo *info = tc_diag_info(diag->code);
    fprintf(stderr, "%s:%u:%u: error[%s]: %s: %s\n",
            diag->span.file != NULL ? diag->span.file : "<source>",
            diag->span.line, diag->span.column, stage_name(diag->stage),
            info != NULL ? info->symbol : "TC_CE_SYNTAX",
            info != NULL ? info->print_name : "SyntaxError");
    if (diag->has_related) {
        fprintf(stderr, "%s:%u:%u: note: related position\n",
                diag->related.file != NULL ? diag->related.file : "<source>",
                diag->related.line, diag->related.column);
    }
}

static int print_impl_error(const char *tool_name, TcImplError error,
                            const char *detail) {
    const char *symbol = tc_impl_error_symbol(error);
    const char *message = tc_impl_error_message(error);
    if (error == TC_IMPL_INTERNAL) {
        /* Internal failures include the documented front-end implementation
         * limits (source/token size, nesting).  They are never language
         * diagnostics (standard §12.5.3/§13.5) and are distinct from OOM/I/O. */
        message = "internal error or documented implementation limit exceeded";
    }
    if (detail != NULL) {
        fprintf(stderr, "%s: %s: %s: %s\n", tool_name, symbol, message, detail);
    } else {
        fprintf(stderr, "%s: %s: %s\n", tool_name, symbol, message);
    }
    if (error == TC_IMPL_USAGE) {
        return TC_EXIT_USAGE;
    }
    return TC_EXIT_INTERNAL;
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
    if (result == NULL || !result->trapped) {
        return TC_EXIT_INTERNAL;
    }
    info = tc_diag_info((TcDiagCode)result->kind);
    fprintf(stderr, "%s:%u:%u: error[RT]: %s: %s\n",
            result->span.file != NULL ? result->span.file : "<source>",
            result->span.line, result->span.column,
            info != NULL ? info->symbol : "TC_RE_TRAP",
            info != NULL ? info->print_name : "Trap");
    (void)tool_name;
    return TC_EXIT_DIAGNOSTIC;
}

static int run_syntax_only(const char *tool_name,
                           const TcCliOptions *options) {
    TcSourceBytes source;
    TcParseModule *module = NULL;
    TcFrontendDiag diag;
    TcImplError impl = TC_IMPL_OK;
    TcImplError read_error;
    int parsed;
    read_error = tc_source_read(options->source, &source);
    if (read_error != TC_IMPL_OK) {
        return print_impl_error(tool_name, read_error, options->source);
    }
    parsed = tc_parse_source(options->source, source.bytes, source.size,
                             &module, &diag, &impl);
    tc_source_free(&source);
    if (parsed != 0) {
        return print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                              : TC_IMPL_INTERNAL,
                                options->source);
    }
    if (module == NULL) {
        print_frontend_diag(&diag);
        return TC_EXIT_DIAGNOSTIC;
    }
    tc_parse_module_destroy(module);
    /* Partial capability: LEX/SYN accepted, not a valid-program claim. */
    return TC_EXIT_OK;
}

int tc_driver_frontend(const char *tool_name, const TcCliOptions *options) {
    TcPlatformProfile profile;
    TcAnalysis *analysis;
#ifdef SIGPIPE
    /* A closed pipe is an I/O failure, not a process-terminating signal; the
     * VM's per-record sink then reports TC_RE_IO at the failed instruction. */
    (void)signal(SIGPIPE, SIG_IGN);
#endif
    TcAnalysisStatus status;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    int result;
    if (options == NULL || options->source == NULL) {
        return print_impl_error(tool_name, TC_IMPL_USAGE, NULL);
    }
    if (options->syntax_only) {
        return run_syntax_only(tool_name, options);
    }
    if (!tc_driver_select_profile(options->profile_name, &profile)) {
        return print_impl_error(tool_name, TC_IMPL_USAGE,
                                options->profile_name);
    }
    if (options->device_name != NULL &&
        !tc_driver_apply_device(&profile, options->device_name)) {
        return print_impl_error(tool_name, TC_IMPL_USAGE,
                                options->device_name);
    }
    /* Checked build is an execution option of the selected platform instance:
     * the same profile instance drives analysis and code generation. */
    profile.checked_build = options->checked ? 1 : 0;
    if (options->sem_only) {
        analysis = tc_analyze_sem_only(options->source, profile.addr_width,
                                       options->include_roots,
                                       options->ninclude_roots, &status, &diag,
                                       &impl);
    } else {
        analysis = tc_analyze_with_profile(
            options->source, &profile, profile.addr_width,
            options->include_roots, options->ninclude_roots, &status, &diag,
            &impl);
    }
    if (analysis == NULL) {
        return print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                              : TC_IMPL_INTERNAL,
                                options->source);
    }
    if (status == TC_ANALYSIS_IMPL) {
        tc_analysis_destroy(analysis);
        return print_impl_error(tool_name, impl != TC_IMPL_OK ? impl
                                                              : TC_IMPL_INTERNAL,
                                options->source);
    }
    if (status == TC_ANALYSIS_ROOT_NOT_PROGRAM) {
        tc_analysis_destroy(analysis);
        return print_impl_error(tool_name, TC_IMPL_USAGE,
                                "root module must be #program");
    }
    if (status == TC_ANALYSIS_DIAGNOSTIC) {
        print_analysis_diag(&diag);
        tc_analysis_destroy(analysis);
        return TC_EXIT_DIAGNOSTIC;
    }
    if (options->sem_only) {
        /* IMPORT/SEM accepted; CT and code generation were not run. */
        tc_analysis_destroy(analysis);
        return TC_EXIT_OK;
    }
    if (options->static_only) {
        /* LEX/SYN/IMPORT/SEM/CT and IR finalization accepted; no codegen. */
        tc_analysis_destroy(analysis);
        return TC_EXIT_OK;
    }
    if (strcmp(tool_name, "tc-vm") == 0) {
        TcIo io;
        TcIoSink sink;
        TcIoSrc src;
        TcTrapHandler trap;
        TcVmResult vm_result;
        TcImplError vm_impl = TC_IMPL_OK;
        TcVmStatus vm_status;
        const TcIR *ir = tc_analysis_ir(analysis);
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
            print_vm_trap(tool_name, &vm_result);
            result = profile.exit.trap_status;
        } else {
            result = print_impl_error(
                tool_name, vm_impl != TC_IMPL_OK ? vm_impl : TC_IMPL_INTERNAL,
                NULL);
        }
        tc_analysis_destroy(analysis);
        return result;
    }
    result = tc_aot_driver_run(analysis, &profile, options);
    tc_analysis_destroy(analysis);
    return result;
}
