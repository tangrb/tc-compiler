/* TC 0.0.46 tc-aot backend driver (design §11).
 *
 * Pipeline: finalized TcIR -> AArch64 `.s` -> clang integrated assembler ->
 * lld (+ freestanding runtime archive) -> ELF.  The driver keeps the static
 * front end authoritative: it only runs after LEX/SYN/IMPORT/SEM/CT and IR
 * finalization succeeded, and it never reports a language diagnostic.
 */
#include "driver/aot_driver.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "backend/aarch64/emit.h"
#include "backend/common/toolchain.h"
#include "diagnostic/diagnostic.h"

#ifndef TC_A64_RUNTIME_LIB
#define TC_A64_RUNTIME_LIB ""
#endif
#ifndef TC_A64_LINKER_SCRIPT
#define TC_A64_LINKER_SCRIPT ""
#endif
#ifndef TC_A64_CC
#define TC_A64_CC "clang"
#endif
#ifndef TC_A64_LD
#define TC_A64_LD "ld.lld"
#endif

static int report(TcImplError error, const char *detail) {
    fprintf(stderr, "tc-aot: %s: %s", tc_impl_error_symbol(error),
            tc_impl_error_message(error));
    if (detail != NULL) {
        fprintf(stderr, ": %s", detail);
    }
    fputc('\n', stderr);
    return error == TC_IMPL_USAGE ? 2 : 71;
}

static int report_unavailable(const char *detail) {
    fprintf(stderr, "tc-aot: %s: %s", tc_impl_error_symbol(TC_IMPL_UNAVAILABLE),
            tc_impl_error_message(TC_IMPL_UNAVAILABLE));
    if (detail != NULL) {
        fprintf(stderr, ": %s", detail);
    }
    fputc('\n', stderr);
    return 70;
}

static int run_tool(char *const argv[]) {
    pid_t pid;
    int status = 0;
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static int copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    FILE *out;
    char buffer[8192];
    size_t n;
    if (in == NULL) {
        return -1;
    }
    out = fopen(to, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) != 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    if (ferror(in)) {
        fclose(in);
        fclose(out);
        return -1;
    }
    if (fclose(out) != 0) {
        fclose(in);
        return -1;
    }
    fclose(in);
    return 0;
}

static int make_temp(char *path, size_t cap, const char *suffix) {
    const char *tmp = getenv("TMPDIR");
    int fd;
    if (tmp == NULL || tmp[0] == '\0') {
        tmp = "/tmp";
    }
    if (snprintf(path, cap, "%s/tc-aot-XXXXXX%s", tmp, suffix) >= (int)cap) {
        return -1;
    }
    fd = mkstemps(path, (int)strlen(suffix));
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

static int emit_asm_to(const TcIR *ir, const TcPlatformProfile *profile,
                       const char *path, char *err, size_t errcap,
                       int *unsupported) {
    FILE *out;
    TcA64Status status;
    *unsupported = 0;
    out = fopen(path, "wb");
    if (out == NULL) {
        return report(TC_IMPL_IO, path);
    }
    status = tc_a64_emit_asm(out, ir, profile, profile->target, err, errcap);
    if (fclose(out) != 0) {
        return report(TC_IMPL_IO, path);
    }
    if (status == TC_A64_UNSUPPORTED) {
        *unsupported = 1;
        return 70;
    }
    if (status != TC_A64_OK) {
        return report(TC_IMPL_INTERNAL, err[0] != '\0' ? err : NULL);
    }
    return 0;
}

static const char *runtime_lib_path(const TcCliOptions *options) {
    if (options->runtime_path != NULL) {
        return options->runtime_path;
    }
    return TC_A64_RUNTIME_LIB;
}

static const char *linker_script_path(const TcCliOptions *options) {
    if (options->linker_script != NULL) {
        return options->linker_script;
    }
    return TC_A64_LINKER_SCRIPT;
}

int tc_aot_driver_run(const TcAnalysis *analysis,
                      const TcPlatformProfile *profile,
                      const TcCliOptions *options) {
    const TcIR *ir;
    char err[512];
    char asm_path[512];
    char obj_path[512];
    char out_path[512];
    int unsupported = 0;
    int result;
    int keep;
    const char *emit;
    const char *cc;
    const char *ld;
    const char *triple;

    if (analysis == NULL || profile == NULL || options == NULL) {
        return report(TC_IMPL_INTERNAL, "missing AOT driver argument");
    }
    if (options->checked) {
        return report_unavailable(
            "checked build is not in the AArch64 subset");
    }
    if (strcmp(profile->target != NULL ? profile->target->name : "",
               "aarch64-linux-user") != 0) {
        return report_unavailable(
            "the AArch64 subset currently targets aarch64-linux-user only");
    }
    ir = tc_analysis_ir(analysis);
    if (ir == NULL) {
        return report(TC_IMPL_INTERNAL, "analysis has no finalized IR");
    }
    emit = options->emit != NULL ? options->emit : "asm";
    if (strcmp(emit, "asm") != 0 && strcmp(emit, "obj") != 0 &&
        strcmp(emit, "elf") != 0) {
        return report(TC_IMPL_USAGE, "unknown --emit value");
    }
    cc = options->cc != NULL ? options->cc : TC_A64_CC;
    ld = options->ld != NULL ? options->ld : TC_A64_LD;
    triple = tc_aot_target_triple(profile->target);
    if (triple == NULL) {
        return report_unavailable(
            "no toolchain mapping for the selected target descriptor");
    }
    keep = options->keep_temps;
    err[0] = '\0';

    if (strcmp(emit, "asm") == 0 && options->output == NULL) {
        TcA64Status status =
            tc_a64_emit_asm(stdout, ir, profile, profile->target, err,
                            sizeof(err));
        if (status == TC_A64_UNSUPPORTED) {
            return report_unavailable(err[0] != '\0' ? err : NULL);
        }
        if (status != TC_A64_OK) {
            return report(TC_IMPL_INTERNAL, err[0] != '\0' ? err : NULL);
        }
        return 0;
    }

    if (options->output == NULL) {
        return report(TC_IMPL_USAGE, "--output is required for --emit=obj/elf");
    }
    if (make_temp(asm_path, sizeof(asm_path), ".s") != 0) {
        return report(TC_IMPL_IO, "cannot create temporary assembly file");
    }
    result = emit_asm_to(ir, profile, asm_path, err, sizeof(err),
                         &unsupported);
    if (result != 0) {
        if (unsupported) {
            unlink(asm_path);
            return report_unavailable(err[0] != '\0' ? err : NULL);
        }
        unlink(asm_path);
        return result;
    }
    if (strcmp(emit, "asm") == 0) {
        if (copy_file(asm_path, options->output) != 0) {
            unlink(asm_path);
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep) {
            unlink(asm_path);
        }
        return 0;
    }

    if (make_temp(obj_path, sizeof(obj_path), ".o") != 0) {
        unlink(asm_path);
        return report(TC_IMPL_IO, "cannot create temporary object file");
    }
    {
        char *argv[8];
        int status;
        argv[0] = (char *)cc;
        argv[1] = "-target";
        argv[2] = (char *)triple;
        argv[3] = "-c";
        argv[4] = asm_path;
        argv[5] = "-o";
        argv[6] = obj_path;
        argv[7] = NULL;
        status = run_tool(argv);
        if (status != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report(TC_IMPL_UNAVAILABLE,
                          "clang could not assemble the emitted .s");
        }
    }
    if (strcmp(emit, "obj") == 0) {
        if (copy_file(obj_path, options->output) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep) {
            unlink(asm_path);
            unlink(obj_path);
        }
        return 0;
    }

    {
        const char *runtime = runtime_lib_path(options);
        const char *script = linker_script_path(options);
        char *argv[16];
        int n = 0;
        int status;
        if (runtime == NULL || runtime[0] == '\0' ||
            access(runtime, R_OK) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report_unavailable("AArch64 runtime archive is unavailable");
        }
        if (script == NULL || script[0] == '\0' || access(script, R_OK) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report_unavailable("AArch64 linker script is unavailable");
        }
        if (make_temp(out_path, sizeof(out_path), ".elf") != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report(TC_IMPL_IO, "cannot create temporary ELF file");
        }
        argv[n++] = (char *)ld;
        argv[n++] = "-m";
        argv[n++] = "aarch64elf";
        argv[n++] = "-static";
        argv[n++] = "-nostdlib";
        argv[n++] = "-T";
        argv[n++] = (char *)script;
        argv[n++] = "-o";
        argv[n++] = out_path;
        argv[n++] = obj_path;
        argv[n++] = (char *)runtime;
        argv[n] = NULL;
        status = run_tool(argv);
        if (status != 0) {
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
            return report(TC_IMPL_UNAVAILABLE,
                          "ld.lld could not link the AArch64 image");
        }
        if (copy_file(out_path, options->output) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep) {
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
        }
    }
    return 0;
}
