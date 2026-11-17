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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "backend/aarch64/emit.h"
#include "backend/arm32/emit.h"
#include "backend/common/toolchain.h"
#include "diagnostic/diagnostic.h"

#ifndef TC_AARCH64_RUNTIME_LIB
#define TC_AARCH64_RUNTIME_LIB ""
#endif
#ifndef TC_AARCH64_BAREMETAL_RUNTIME_LIB
#define TC_AARCH64_BAREMETAL_RUNTIME_LIB ""
#endif
#ifndef TC_ARM32_RUNTIME_LIB
#define TC_ARM32_RUNTIME_LIB ""
#endif
#ifndef TC_AARCH64_LINKER_SCRIPT
#define TC_AARCH64_LINKER_SCRIPT ""
#endif
#ifndef TC_ARM32_LINKER_SCRIPT
#define TC_ARM32_LINKER_SCRIPT ""
#endif
#ifndef TC_TARGET_ASSEMBLER
#define TC_TARGET_ASSEMBLER "clang"
#endif
#ifndef TC_TARGET_LINKER
#define TC_TARGET_LINKER "ld.lld"
#endif

/* One supported target/platform-instance binding (design §10.2/§11). */
typedef struct {
    const char *name;
    const char *default_runtime;
    const char *default_linker_script;
    const char *linker_emulation;
    int is_arm32;
} TcTargetInstance;

static int select_instance(const TcPlatformProfile *profile,
                           TcTargetInstance *out) {
    const char *name = profile != NULL && profile->target != NULL
                           ? profile->target->name
                           : NULL;
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "aarch64-linux-user") == 0) {
        out->name = name;
        out->default_runtime = TC_AARCH64_RUNTIME_LIB;
        out->default_linker_script = TC_AARCH64_LINKER_SCRIPT;
        out->linker_emulation = "aarch64elf";
        out->is_arm32 = 0;
        return 1;
    }
    if (strcmp(name, "aarch64-baremetal") == 0) {
        out->name = name;
        out->default_runtime = TC_AARCH64_BAREMETAL_RUNTIME_LIB;
        out->default_linker_script = TC_AARCH64_LINKER_SCRIPT;
        out->linker_emulation = "aarch64elf";
        out->is_arm32 = 0;
        return 1;
    }
    if (strcmp(name, "arm32-baremetal") == 0) {
        out->name = name;
        out->default_runtime = TC_ARM32_RUNTIME_LIB;
        out->default_linker_script = TC_ARM32_LINKER_SCRIPT;
        out->linker_emulation = "armelf";
        out->is_arm32 = 1;
        return 1;
    }
    return 0;
}

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

static int copy_file(const char *source_path, const char *destination_path) {
    FILE *input_file = fopen(source_path, "rb");
    FILE *output_file;
    char buffer[8192];
    size_t bytes_read;
    if (input_file == NULL) {
        return -1;
    }
    output_file = fopen(destination_path, "wb");
    if (output_file == NULL) {
        fclose(input_file);
        return -1;
    }
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), input_file)) != 0) {
        if (fwrite(buffer, 1, bytes_read, output_file) != bytes_read) {
            fclose(input_file);
            fclose(output_file);
            return -1;
        }
    }
    if (ferror(input_file)) {
        fclose(input_file);
        fclose(output_file);
        return -1;
    }
    if (fclose(output_file) != 0) {
        fclose(input_file);
        return -1;
    }
    fclose(input_file);
    return 0;
}

static int make_temp(char *path, size_t path_capacity, const char *suffix) {
    const char *temporary_directory = getenv("TMPDIR");
    int file_descriptor;
    if (temporary_directory == NULL || temporary_directory[0] == '\0') {
        temporary_directory = "/tmp";
    }
    if (snprintf(path, path_capacity, "%s/tc-aot-XXXXXX%s",
                 temporary_directory, suffix) >= (int)path_capacity) {
        return -1;
    }
    file_descriptor = mkstemps(path, (int)strlen(suffix));
    if (file_descriptor < 0) {
        return -1;
    }
    close(file_descriptor);
    return 0;
}

static int emit_assembly_to(const TcIR *ir,
                            const TcPlatformProfile *profile,
                            const TcTargetInstance *instance,
                            const char *path, char *error_message,
                            size_t error_capacity, int *is_unsupported) {
    FILE *output_file;
    int unsupported = 0;
    int io_error = 0;
    int ok = 0;
    *is_unsupported = 0;
    output_file = path == NULL ? stdout : fopen(path, "wb");
    if (output_file == NULL) {
        return report(TC_IMPL_IO, path);
    }
    if (instance->is_arm32) {
        TcArm32EmitStatus status =
            tc_arm32_emit_asm(output_file, ir, profile, profile->target,
                              error_message, error_capacity);
        unsupported = status == TC_ARM32_EMIT_UNSUPPORTED;
        io_error = status == TC_ARM32_EMIT_IO;
        ok = status == TC_ARM32_EMIT_OK;
    } else {
        TcAArch64EmitStatus status =
            tc_aarch64_emit_asm(output_file, ir, profile, profile->target,
                                error_message, error_capacity);
        unsupported = status == TC_AARCH64_EMIT_UNSUPPORTED;
        io_error = status == TC_AARCH64_EMIT_IO;
        ok = status == TC_AARCH64_EMIT_OK;
    }
    if (output_file != stdout && fclose(output_file) != 0) {
        return report(TC_IMPL_IO, path);
    }
    if (unsupported) {
        *is_unsupported = 1;
        return 70;
    }
    if (io_error) {
        return report(TC_IMPL_IO,
                      error_message[0] != '\0' ? error_message : NULL);
    }
    if (!ok) {
        return report(TC_IMPL_INTERNAL,
                      error_message[0] != '\0' ? error_message : NULL);
    }
    return 0;
}

static const char *runtime_archive_path(const TcCliOptions *options,
                                        const TcTargetInstance *instance) {
    if (options->runtime_path != NULL) {
        return options->runtime_path;
    }
    return instance->default_runtime;
}

static const char *linker_script_path(const TcCliOptions *options,
                                      const TcTargetInstance *instance) {
    if (options->linker_script != NULL) {
        return options->linker_script;
    }
    return instance->default_linker_script;
}

int tc_aot_driver_run(const TcAnalysis *analysis,
                      const TcPlatformProfile *profile,
                      const TcCliOptions *options) {
    const TcIR *ir;
    char error_message[512];
    char asm_path[512];
    char obj_path[512];
    char out_path[512];
    int is_unsupported = 0;
    int exit_status;
    int keep_temporaries;
    const char *emit_kind;
    const char *assembler;
    const char *linker;
    const char *target_triple;
    TcTargetInstance instance;

    if (analysis == NULL || profile == NULL || options == NULL) {
        return report(TC_IMPL_INTERNAL, "missing AOT driver argument");
    }
    if (!select_instance(profile, &instance)) {
        return report_unavailable(
            "no backend/toolchain is bound to the selected target descriptor");
    }
    ir = tc_analysis_ir(analysis);
    if (ir == NULL) {
        return report(TC_IMPL_INTERNAL, "analysis has no finalized IR");
    }
    emit_kind = options->emit_kind != NULL ? options->emit_kind : "asm";
    if (strcmp(emit_kind, "asm") != 0 && strcmp(emit_kind, "obj") != 0 &&
        strcmp(emit_kind, "elf") != 0) {
        return report(TC_IMPL_USAGE, "unknown --emit value");
    }
    assembler = options->assembler != NULL
                    ? options->assembler
                    : TC_TARGET_ASSEMBLER;
    linker = options->linker != NULL ? options->linker : TC_TARGET_LINKER;
    target_triple = tc_aot_target_triple(profile->target);
    if (target_triple == NULL) {
        return report_unavailable(
            "no toolchain mapping for the selected target descriptor");
    }
    keep_temporaries = options->keep_temps;
    error_message[0] = '\0';

    if (strcmp(emit_kind, "asm") == 0 && options->output == NULL) {
        int unsupported = 0;
        int status = emit_assembly_to(ir, profile, &instance, NULL,
                                      error_message,
                                      sizeof(error_message), &unsupported);
        if (unsupported) {
            return report_unavailable(
                error_message[0] != '\0' ? error_message : NULL);
        }
        if (status != 0) {
            return status;
        }
        return 0;
    }

    if (options->output == NULL) {
        return report(TC_IMPL_USAGE, "--output is required for --emit=obj/elf");
    }
    if (make_temp(asm_path, sizeof(asm_path), ".s") != 0) {
        return report(TC_IMPL_IO, "cannot create temporary assembly file");
    }
    exit_status = emit_assembly_to(ir, profile, &instance, asm_path,
                                   error_message, sizeof(error_message),
                                   &is_unsupported);
    if (exit_status != 0) {
        if (is_unsupported) {
            unlink(asm_path);
            return report_unavailable(
                error_message[0] != '\0' ? error_message : NULL);
        }
        unlink(asm_path);
        return exit_status;
    }
    if (strcmp(emit_kind, "asm") == 0) {
        if (copy_file(asm_path, options->output) != 0) {
            unlink(asm_path);
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep_temporaries) {
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
        argv[0] = (char *)assembler;
        argv[1] = "-target";
        argv[2] = (char *)target_triple;
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
    if (strcmp(emit_kind, "obj") == 0) {
        if (copy_file(obj_path, options->output) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report(TC_IMPL_IO, options->output);
        }
        if (!keep_temporaries) {
            unlink(asm_path);
            unlink(obj_path);
        }
        return 0;
    }

    {
        const char *runtime_archive =
            runtime_archive_path(options, &instance);
        const char *linker_script = linker_script_path(options, &instance);
        char *argv[20];
        size_t argument_index = 0;
        int status;
        if (runtime_archive == NULL || runtime_archive[0] == '\0' ||
            access(runtime_archive, R_OK) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report_unavailable("target runtime archive is unavailable");
        }
        if (linker_script == NULL || linker_script[0] == '\0' ||
            access(linker_script, R_OK) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report_unavailable("target linker script is unavailable");
        }
        if (make_temp(out_path, sizeof(out_path), ".elf") != 0) {
            unlink(asm_path);
            unlink(obj_path);
            return report(TC_IMPL_IO, "cannot create temporary ELF file");
        }
        argv[argument_index++] = (char *)linker;
        argv[argument_index++] = "-m";
        argv[argument_index++] = (char *)instance.linker_emulation;
        argv[argument_index++] = "-static";
        argv[argument_index++] = "-nostdlib";
        /* Standard §3.13.3: distinct function entry addresses; never fold
         * identical code or merge entries. */
        argv[argument_index++] = "--icf=none";
        argv[argument_index++] = "-T";
        argv[argument_index++] = (char *)linker_script;
        argv[argument_index++] = "-o";
        argv[argument_index++] = out_path;
        argv[argument_index++] = obj_path;
        argv[argument_index++] = (char *)runtime_archive;
        argv[argument_index] = NULL;
        status = run_tool(argv);
        if (status != 0) {
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
            return report(TC_IMPL_UNAVAILABLE,
                          "ld.lld could not link the target image");
        }
        if (copy_file(out_path, options->output) != 0) {
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
            return report(TC_IMPL_IO, options->output);
        }
        {
            struct stat output_status;
            if (stat(options->output, &output_status) == 0 &&
                S_ISREG(output_status.st_mode) &&
                chmod(options->output, 0755) != 0) {
                unlink(asm_path);
                unlink(obj_path);
                unlink(out_path);
                return report(TC_IMPL_IO, options->output);
            }
        }
        if (!keep_temporaries) {
            unlink(asm_path);
            unlink(obj_path);
            unlink(out_path);
        }
    }
    return 0;
}
