#include <stdio.h>
#include <string.h>

#include "base/fd.h"
#include "driver/aot_driver.h"
#include "driver/profile_select.h"
#include "ir/linemap.h"
#include "project/paths.h"
#include "runtime/profile.h"

int main(int argc, char **argv) {
    TcCliOptions options;
    TcPlatformProfile profile;
    TcProjectPaths *paths = NULL;
    TcProjectPathError path_error;
    TcAotStdio stdio;
    TcAnalysis *analysis = NULL;
    TcAnalysisStatus status = TC_ANALYSIS_IMPL;
    TcAnalysisDiag diag;
    TcImplError impl = TC_IMPL_OK;
    int result;
    if (argc != 6 && argc != 7) {
        return 2;
    }
    tc_aot_stdio_capture(&stdio);
    tc_cli_options_init(&options);
    options.output = strcmp(argv[2], "-") == 0 ? NULL : argv[2];
    options.emit_kind = argv[4];
    options.keep_temps = strcmp(argv[5], "1") == 0 ? 1 : 0;
    options.profile_name = argv[3];
    options.profile_seen = 1;
    options.debug = 1;
    if (strcmp(argv[4], "asm") != 0 && strcmp(argv[4], "obj") != 0 &&
        strcmp(argv[4], "elf") != 0 && strcmp(argv[4], "flat") != 0) {
        return 2;
    }
    if (!tc_driver_select_profile(argv[3], &profile)) {
        return 2;
    }
    if (!tc_project_paths_prepare(argv[1], NULL, 0, &paths, &path_error)) {
        result = tc_impl_error_exit(path_error.impl);
        tc_project_path_error_destroy(&path_error);
        return result;
    }
    {
        const char *extra_inputs[] = { paths->manifest->manifest_present
                                           ? paths->manifest->manifest_path
                                           : NULL };
        size_t nextra_inputs = paths->manifest->manifest_present ? 1u : 0u;
        TcImplError guard = tc_aot_driver_guard(
            &stdio, NULL, TC_ANALYSIS_IMPL, &options, extra_inputs,
            nextra_inputs, paths->source,
            (const char *const *)paths->includes, paths->ninclude_roots, 1, 0);
        if (guard != TC_IMPL_OK) {
            tc_project_paths_destroy(paths);
            return guard == TC_IMPL_USAGE ? 2 : tc_impl_error_exit(guard);
        }
    }
    analysis = tc_analyze_with_paths(
        paths->source, &profile, profile.addr_width,
        (const char *const *)paths->includes, paths->ninclude_roots,
        paths->context, &status, &diag, &impl);
    if (analysis == NULL) {
        tc_project_paths_destroy(paths);
        return tc_impl_error_exit(impl != TC_IMPL_OK ? impl
                                                     : TC_IMPL_INTERNAL);
    }
    if (status != TC_ANALYSIS_OK) {
        const char *extra_inputs[] = { paths->manifest->manifest_present
                                           ? paths->manifest->manifest_path
                                           : NULL };
        size_t nextra_inputs = paths->manifest->manifest_present ? 1u : 0u;
        TcImplError guard = tc_aot_driver_guard(
            &stdio, analysis, status, &options, extra_inputs, nextra_inputs,
            paths->source, (const char *const *)paths->includes,
            paths->ninclude_roots, 1, 1);
        tc_analysis_destroy(analysis);
        tc_project_paths_destroy(paths);
        if (guard != TC_IMPL_OK) {
            return guard == TC_IMPL_USAGE ? 2 : tc_impl_error_exit(guard);
        }
        return status == TC_ANALYSIS_DIAGNOSTIC ? 1 : 71;
    }
    {
        const char *extra_inputs[] = { paths->manifest->manifest_present
                                           ? paths->manifest->manifest_path
                                           : NULL };
        size_t nextra_inputs = paths->manifest->manifest_present ? 1u : 0u;
        result = tc_aot_driver_run_with_stdio(
            &stdio, analysis, &profile, &options, extra_inputs, nextra_inputs);
    }
    if (result == 0 && argc == 7) {
        TcLineMap *linemap = NULL;
        int fd = tc_fd_open(argv[6], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                            0666);
        FILE *out = fd < 0 ? NULL : fdopen(fd, "wb");
        if (out == NULL) {
            result = 70;
        } else {
            impl = tc_linemap_build(tc_analysis_ir(analysis), &linemap);
            if (impl == TC_IMPL_OK) {
                impl = tc_linemap_write_json(linemap, out, 0, 0u);
            }
            if (fclose(out) != 0) {
                impl = TC_IMPL_IO;
            }
            tc_linemap_destroy(linemap);
            result = tc_impl_error_exit(impl);
        }
    }
    tc_analysis_destroy(analysis);
    tc_project_paths_destroy(paths);
    return result;
}
