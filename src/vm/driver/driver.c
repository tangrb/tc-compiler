/*
 * driver.c — TC-VM 驱动层（基于 libtc）
 */
#include "tc_driver.h"

#include "tc_diagnostic.h"
#include "tc_lib.h"
#include "tc_warning.h"

int tc_run_source(const char *source, int check_only, TcDiagnostic *diag) {
    TcTypedProgram typed;
    int rc = 0;

    if (tc_compile_source(source, &typed, diag) != 0) {
        return -1;
    }

    if (typed.warnings.count > 0) {
        tc_warning_list_print(&typed.warnings, stderr);
    }

    if (check_only) {
        tc_typed_program_free(&typed);
        return 0;
    }

    rc = tc_run_typed(&typed, diag);
    tc_typed_program_free(&typed);
    return rc;
}

int tc_run_file(const char *path, int check_only, TcDiagnostic *diag) {
    TcTypedProgram typed;
    int rc = 0;

    if (tc_compile_file(path, &typed, diag) != 0) {
        return -1;
    }

    if (typed.warnings.count > 0) {
        tc_warning_list_print(&typed.warnings, stderr);
    }

    if (check_only) {
        tc_typed_program_free(&typed);
        return 0;
    }

    rc = tc_run_typed(&typed, diag);
    tc_typed_program_free(&typed);
    return rc;
}
