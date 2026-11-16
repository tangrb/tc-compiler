/* TC 0.0.46 driver: LEX/SYN/IMPORT/SEM invocation. */
#include "driver/frontend_driver.h"

#include <stdio.h>
#include <string.h>

#include "diagnostic/diagnostic.h"
#include "driver/profile_select.h"
#include "frontend/input.h"
#include "frontend/parse.h"
#include "frontend/sem.h"

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
    tc_analysis_destroy(analysis);
    if (options->sem_only) {
        /* IMPORT/SEM accepted; CT and code generation were not run. */
        return TC_EXIT_OK;
    }
    if (options->static_only) {
        /* LEX/SYN/IMPORT/SEM/CT and IR finalization accepted; no codegen. */
        return TC_EXIT_OK;
    }
    fprintf(stderr, "%s: %s: %s\n", tool_name,
            tc_impl_error_symbol(TC_IMPL_UNAVAILABLE),
            tc_impl_error_message(TC_IMPL_UNAVAILABLE));
    result = TC_EXIT_UNAVAILABLE;
    return result;
}
